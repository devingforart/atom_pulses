//! Prepaid Cloud units. All mutations use server-owned prices and SQL transactions.
use axum::{Json, extract::State, http::{HeaderMap, StatusCode}};
use serde::Deserialize;
use serde_json::{Value, json};
use uuid::Uuid;
use crate::{AppState, auth::require_user, error::{ApiError, ApiResult}};

pub fn enabled() -> bool { std::env::var("PULSO_CREDITS_ENABLED").as_deref() == Ok("true") }
pub const BASIC_MONTHLY_CREDITS: i64 = 20;
pub const BASIC_MONTHLY_USD_CENTS: i64 = 1400;
pub fn cost() -> i64 { 3 }

fn paid_monthly_cycle(invoice: &Value) -> bool {
    invoice["status"] == "paid"
        && invoice["paid_out_of_band"] != true
        && matches!(invoice["billing_reason"].as_str(), Some("subscription_create" | "subscription_cycle"))
}

fn contains_monthly_price(lines: &Value, price_id: &str) -> bool {
    lines["data"].as_array().is_some_and(|items| items.iter().any(|line| {
        line.pointer("/pricing/price_details/price").and_then(Value::as_str)
            .or_else(|| line.pointer("/price/id").and_then(Value::as_str)) == Some(price_id)
            && line["proration"] != true
            && line.pointer("/parent/subscription_item_details/proration") != Some(&Value::Bool(true))
    }))
}

pub async fn wallet(State(state): State<AppState>, headers: HeaderMap) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    let mut conn = state.db.acquire().await?;
    let free = crate::demo::owner(&user.email) || crate::demo::eligible(&mut conn, &user).await?
        || state.config.cloud_unlimited_emails.iter().any(|email| email.eq_ignore_ascii_case(&user.email));
    drop(conn);
    let balance: Option<(i64,i64,bool)> = sqlx::query_as("SELECT available,reserved,blocked FROM credit_wallets WHERE user_id=$1")
        .bind(user.id).fetch_optional(&state.db).await?;
    let packs: Vec<(String,i64,i64,String)> = sqlx::query_as("SELECT id,credits,amount_cents,currency FROM credit_packs WHERE active ORDER BY amount_cents")
        .fetch_all(&state.db).await?;
    let history: Vec<(String,i64,i64,String)> = sqlx::query_as("SELECT kind,available_delta,reserved_delta,created_at::text FROM credit_ledger WHERE user_id=$1 ORDER BY id DESC LIMIT 50")
        .bind(user.id).fetch_all(&state.db).await?;
    let (available,reserved,blocked) = balance.unwrap_or_default();
    Ok(Json(json!({"enabled":enabled() && !free,"available":available,"reserved":reserved,"blocked":blocked,"perWork":cost(),
        "basicPlan":{"monthlyCredits":BASIC_MONTHLY_CREDITS,"amountCents":BASIC_MONTHLY_USD_CENTS,"currency":"usd"},
        "packs":packs.into_iter().map(|(id,credits,amount,currency)|json!({"id":id,"credits":credits,"amount":amount,"currency":currency})).collect::<Vec<_>>(),
        "history":history.into_iter().map(|(kind,available,reserved,at)|json!({"kind":kind,"available":available,"reserved":reserved,"at":at})).collect::<Vec<_>>() })))
}

// Grant only against an authenticated, paid Stripe invoice for the configured
// monthly price. The invoice reference is unique; duplicate/reordered webhook
// deliveries cannot mint credits twice.
pub async fn grant_paid_monthly_invoice(state: &AppState, event_invoice: &Value) -> ApiResult<()> {
    if !enabled() { return Ok(()); }
    let Some(price_id) = state.config.stripe_monthly_price_id.as_deref() else { return Ok(()); };
    let invoice_id = event_invoice["id"].as_str().ok_or_else(|| ApiError::internal("Paid invoice missing id"))?;
    let invoice = crate::billing::stripe_get_value(state, &format!("invoices/{invoice_id}"), &[]).await?;
    if !paid_monthly_cycle(&invoice) { return Ok(()); }
    let Some(subscription_id) = invoice.pointer("/parent/subscription_details/subscription").and_then(Value::as_str)
        .or_else(|| invoice["subscription"].as_str()) else { return Ok(()); };
    let Some(customer_id) = invoice["customer"].as_str() else { return Ok(()); };
    let lines = crate::billing::stripe_get_value(state, &format!("invoices/{invoice_id}/lines"), &[("limit", "100")]).await?;
    if !contains_monthly_price(&lines, price_id) { return Ok(()); }
    let subscription = crate::billing::stripe_get_value(state, &format!("subscriptions/{subscription_id}"), &[]).await?;
    if subscription["customer"] != customer_id || !subscription["items"]["data"].as_array().is_some_and(|items| items.len() == 1 && items[0].pointer("/price/id").and_then(Value::as_str) == Some(price_id) && items[0]["quantity"].as_i64() == Some(1)) {
        return Err(ApiError::internal("Paid invoice does not match the monthly subscription"));
    }
    let user: Uuid = sqlx::query_scalar("SELECT id FROM users WHERE stripe_customer_id=$1")
        .bind(customer_id).fetch_optional(&state.db).await?
        .ok_or_else(|| ApiError::internal("Paid subscription customer has no PULSO account"))?;
    let mut tx = state.db.begin().await?;
    let inserted = sqlx::query("INSERT INTO credit_ledger(user_id,reference,kind,available_delta,reserved_delta) VALUES($1,$2,'subscription_grant',$3,0) ON CONFLICT(reference) DO NOTHING")
        .bind(user).bind(format!("subscription_invoice:{invoice_id}"))
        .bind(BASIC_MONTHLY_CREDITS).execute(&mut *tx).await?;
    if inserted.rows_affected() == 1 {
        sqlx::query("INSERT INTO credit_wallets(user_id,available) VALUES($1,$2) ON CONFLICT(user_id) DO UPDATE SET available=credit_wallets.available+EXCLUDED.available")
            .bind(user).bind(BASIC_MONTHLY_CREDITS).execute(&mut *tx).await?;
    }
    tx.commit().await?;
    Ok(())
}

pub async fn reserve(tx: &mut sqlx::Transaction<'_,sqlx::Postgres>, user: Uuid, job: Uuid, amount: i64) -> ApiResult<()> {
    if amount == 0 { return Ok(()); }
    let result = sqlx::query("UPDATE credit_wallets SET available=available-$2,reserved=reserved+$2 WHERE user_id=$1 AND available >= $2 AND NOT blocked")
        .bind(user).bind(amount).execute(&mut **tx).await?;
    if result.rows_affected() != 1 { return Err(ApiError::public(StatusCode::PAYMENT_REQUIRED,"Créditos insuficientes. Recarga tu saldo para componer.")); }
    sqlx::query("INSERT INTO credit_ledger(user_id,reference,kind,available_delta,reserved_delta) VALUES($1,$2,'reserve',-$3,$3)")
        .bind(user).bind(format!("reserve:{job}")).bind(amount).execute(&mut **tx).await?;
    sqlx::query("UPDATE cloud_jobs SET credit_cost=$2 WHERE id=$1").bind(job).bind(amount).execute(&mut **tx).await?;
    Ok(())
}

#[derive(Deserialize)]
#[serde(rename_all="camelCase")]
pub struct Buy { pack_id: String, request_key: Uuid }
pub async fn checkout(State(state): State<AppState>, headers: HeaderMap, Json(input): Json<Buy>) -> ApiResult<Json<Value>> {
    if state.config.demo_lab { return Err(ApiError::public(StatusCode::FORBIDDEN,"Pagos desactivados en el laboratorio local.")); }
    let user = require_user(&state,&headers).await?;
    if !enabled() || !user.email_verified { return Err(ApiError::public(StatusCode::FORBIDDEN,"La recarga no está habilitada para esta cuenta.")); }
    let entitled=user.studio_owned || matches!(user.subscription.as_str(),"active"|"trialing") || state.config.cloud_beta_emails.iter().any(|email|email.eq_ignore_ascii_case(&user.email));
    if !entitled { return Err(ApiError::public(StatusCode::FORBIDDEN,"Necesitas acceso Cloud antes de comprar créditos.")); }
    let blocked: bool=sqlx::query_scalar("SELECT COALESCE((SELECT blocked FROM credit_wallets WHERE user_id=$1),FALSE)")
        .bind(user.id).fetch_one(&state.db).await?;
    if blocked { return Err(ApiError::public(StatusCode::FORBIDDEN,"Tu saldo está en revisión por un reembolso o disputa. Contacta con soporte.")); }
    let mut tx = state.db.begin().await?;
    sqlx::query("SELECT pg_advisory_xact_lock(hashtext($1))").bind(format!("credit-order:{}:{}",user.id,input.request_key)).execute(&mut *tx).await?;
    let prior: Option<(Uuid,String,i64,i64,String)> = sqlx::query_as("SELECT id,pack_id,credits,amount_cents,currency FROM credit_orders WHERE user_id=$1 AND request_key=$2")
        .bind(user.id).bind(input.request_key).fetch_optional(&mut *tx).await?;
    let (id,pack,credits,amount,currency) = if let Some(order)=prior { order } else {
        let pack: Option<(i64,i64,String)> = sqlx::query_as("SELECT credits,amount_cents,currency FROM credit_packs WHERE id=$1 AND active")
            .bind(&input.pack_id).fetch_optional(&mut *tx).await?;
        let (credits,amount,currency)=pack.ok_or_else(||ApiError::public(StatusCode::BAD_REQUEST,"Paquete no disponible."))?;
        let id=Uuid::new_v4();
        sqlx::query("INSERT INTO credit_orders(id,user_id,request_key,pack_id,credits,amount_cents,currency) VALUES($1,$2,$3,$4,$5,$6,$7)")
            .bind(id).bind(user.id).bind(input.request_key).bind(&input.pack_id).bind(credits).bind(amount).bind(&currency).execute(&mut *tx).await?;
        (id,input.pack_id.clone(),credits,amount,currency)
    };
    if pack != input.pack_id { return Err(ApiError::public(StatusCode::CONFLICT,"La solicitud ya corresponde a otro paquete.")); }
    tx.commit().await?;
    let fields=vec![("mode","payment".into()),("customer_email",user.email),
        ("metadata[pulso_credit_order]",id.to_string()),("payment_intent_data[metadata][pulso_credit_order]",id.to_string()),("client_reference_id",user.id.to_string()),
        ("line_items[0][price_data][currency]",currency),
        ("line_items[0][price_data][unit_amount]",amount.to_string()),
        ("line_items[0][price_data][product_data][name]",format!("PULSO Cloud · {credits} créditos")),
        ("line_items[0][quantity]","1".into()),
        ("success_url",format!("{}/account?credits=processing",state.config.public_url)),
        ("cancel_url",format!("{}/account?credits=cancelled",state.config.public_url))];
    let session=crate::billing::stripe_post(&state,"checkout/sessions",fields,&format!("pulso-credits-{id}")).await?;
    sqlx::query("UPDATE credit_orders SET session_id=$2 WHERE id=$1 AND (session_id IS NULL OR session_id=$2)")
        .bind(id).bind(&session.id).execute(&state.db).await?;
    Ok(Json(json!({"url":session.url})))
}

// Called only AFTER existing webhook signature validation. Re-fetch current
// provider state, then serialize fulfillment by order row (not event ID).
pub async fn fulfill(state: &AppState, session_id: &str) -> ApiResult<bool> {
    let session=crate::billing::stripe_get_value(state,&format!("checkout/sessions/{session_id}"),&[]).await?;
    let Some(raw)=session.pointer("/metadata/pulso_credit_order").and_then(Value::as_str) else { return Ok(false); };
    let id=Uuid::parse_str(raw).map_err(|_|ApiError::internal("Invalid credit order"))?;
    if session["payment_status"] != "paid" { return Ok(true); }
    let mut tx=state.db.begin().await?;
    let (user,credits,amount,currency,fulfilled,blocked): (Uuid,i64,i64,String,bool,bool)=sqlx::query_as("SELECT user_id,credits,amount_cents,currency,fulfilled,payment_blocked FROM credit_orders WHERE id=$1 FOR UPDATE")
        .bind(id).fetch_one(&mut *tx).await?;
    if fulfilled || blocked { tx.commit().await?; return Ok(true); }
    if session["amount_total"].as_i64()!=Some(amount) || session["currency"].as_str()!=Some(currency.as_str()) || session["client_reference_id"].as_str()!=Some(user.to_string().as_str()) || session["mode"]!="payment" {
        return Err(ApiError::internal("Credit payment does not match immutable order"));
    }
    sqlx::query("INSERT INTO credit_wallets(user_id,available) VALUES($1,$2) ON CONFLICT(user_id) DO UPDATE SET available=credit_wallets.available+EXCLUDED.available")
        .bind(user).bind(credits).execute(&mut *tx).await?;
    sqlx::query("INSERT INTO credit_ledger(user_id,reference,kind,available_delta,reserved_delta) VALUES($1,$2,'purchase',$3,0)")
        .bind(user).bind(format!("purchase:{id}")).bind(credits).execute(&mut *tx).await?;
    sqlx::query("UPDATE credit_orders SET fulfilled=TRUE,session_id=$2 WHERE id=$1").bind(id).bind(session_id).execute(&mut *tx).await?;
    tx.commit().await?;
    Ok(true)
}

// Refund/dispute funds cannot continue paying for generation. Freeze for
// reconciliation; never erase ledger history or silently create negative credit.
pub async fn payment_reversed(state: &AppState, object: &Value) -> ApiResult<()> {
    let Some(intent) = object["payment_intent"].as_str().or_else(|| object.pointer("/payment_intent/id").and_then(Value::as_str)) else { return Ok(()); };
    let payment=crate::billing::stripe_get_value(state,&format!("payment_intents/{intent}"),&[]).await?;
    let Some(raw)=payment.pointer("/metadata/pulso_credit_order").and_then(Value::as_str) else { return Ok(()); };
    let id=Uuid::parse_str(raw).map_err(|_|ApiError::internal("Invalid credit reversal order"))?;
    let mut tx=state.db.begin().await?;
    let user: Uuid=sqlx::query_scalar("UPDATE credit_orders SET payment_blocked=TRUE WHERE id=$1 RETURNING user_id")
        .bind(id).fetch_one(&mut *tx).await?;
    sqlx::query("INSERT INTO credit_wallets(user_id,blocked) VALUES($1,TRUE) ON CONFLICT(user_id) DO UPDATE SET blocked=TRUE")
        .bind(user).execute(&mut *tx).await?;
    tx.commit().await?;
    tracing::warn!(%id,%user,"Credit payment reversed/disputed: wallet frozen pending reconciliation");
    Ok(())
}

#[cfg(test)] mod tests {
    use serde_json::json;
    #[test] fn basic_plan_contract() { assert_eq!(super::cost(),3); assert_eq!(super::BASIC_MONTHLY_CREDITS,20); assert_eq!(super::BASIC_MONTHLY_USD_CENTS,1400); }
    #[test] fn only_paid_creation_or_renewal_can_grant() {
        assert!(super::paid_monthly_cycle(&json!({"status":"paid","billing_reason":"subscription_create"})));
        assert!(super::paid_monthly_cycle(&json!({"status":"paid","billing_reason":"subscription_cycle"})));
        assert!(!super::paid_monthly_cycle(&json!({"status":"open","billing_reason":"subscription_cycle"})));
        assert!(!super::paid_monthly_cycle(&json!({"status":"paid","billing_reason":"subscription_update"})));
        assert!(!super::paid_monthly_cycle(&json!({"status":"paid","billing_reason":"subscription_cycle","paid_out_of_band":true})));
    }
    #[test] fn only_non_prorated_configured_price_can_grant() {
        let lines=json!({"data":[{"pricing":{"price_details":{"price":"price_monthly"}},"parent":{"subscription_item_details":{"proration":false}}}]});
        assert!(super::contains_monthly_price(&lines,"price_monthly"));
        assert!(!super::contains_monthly_price(&lines,"price_other"));
        let proration=json!({"data":[{"pricing":{"price_details":{"price":"price_monthly"}},"parent":{"subscription_item_details":{"proration":true}}}]});
        assert!(!super::contains_monthly_price(&proration,"price_monthly"));
    }
}
