use std::time::{SystemTime, UNIX_EPOCH};

use axum::{
    Json,
    body::Bytes,
    extract::State,
    http::{HeaderMap, StatusCode},
};
use hmac::{Hmac, Mac};
use serde::Deserialize;
use serde_json::{Value, json};
use sha2::Sha256;
use uuid::Uuid;

use crate::{
    AppState,
    auth::require_user,
    error::{ApiError, ApiResult},
    models::{CheckoutInput, RedeemPromotionInput},
};

type HmacSha256 = Hmac<Sha256>;
fn now() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

#[derive(Deserialize)]
struct StripeCreated {
    id: String,
    #[serde(default)]
    url: Option<String>,
}

#[derive(Deserialize)]
struct StripePrice {
    currency: String,
    unit_amount: Option<i64>,
    recurring: Option<StripeRecurring>,
}

#[derive(Deserialize)]
struct StripeRecurring {
    interval: String,
}

#[derive(Deserialize)]
struct StripeList<T> {
    data: Vec<T>,
}

#[derive(Deserialize)]
struct StripePromotionCode {
    id: String,
    active: bool,
}

async fn stripe_post(
    state: &AppState,
    path: &str,
    fields: Vec<(&str, String)>,
) -> ApiResult<StripeCreated> {
    let response = state
        .http
        .post(format!("https://api.stripe.com/v1/{path}"))
        .basic_auth(&state.config.stripe_secret_key, Some(""))
        .header("Stripe-Version", "2025-06-30.basil")
        .form(&fields)
        .send()
        .await?;
    if !response.status().is_success() {
        let status = response.status();
        let message = response.text().await.unwrap_or_default();
        tracing::error!(%status, %message, "Stripe request failed");
        return Err(ApiError::public(
            StatusCode::BAD_GATEWAY,
            "Stripe no pudo iniciar la operación.",
        ));
    }
    Ok(response.json().await?)
}

async fn stripe_price(state: &AppState, price_id: &str) -> ApiResult<StripePrice> {
    let response = state
        .http
        .get(format!("https://api.stripe.com/v1/prices/{price_id}"))
        .basic_auth(&state.config.stripe_secret_key, Some(""))
        .header("Stripe-Version", "2025-06-30.basil")
        .send()
        .await?;
    if !response.status().is_success() {
        tracing::error!(status = %response.status(), %price_id, "Stripe price lookup failed");
        return Err(ApiError::public(
            StatusCode::BAD_GATEWAY,
            "No pudimos consultar los precios actuales.",
        ));
    }
    Ok(response.json().await?)
}

fn normalized_code(value: Option<&str>) -> Option<String> {
    value
        .map(str::trim)
        .filter(|code| !code.is_empty())
        .filter(|code| code.len() <= 64 && code.chars().all(|character| character.is_ascii_alphanumeric() || character == '-' || character == '_'))
        .map(str::to_owned)
}

async fn stripe_promotion_code(state: &AppState, code: &str) -> ApiResult<String> {
    let response = state
        .http
        .get("https://api.stripe.com/v1/promotion_codes")
        .basic_auth(&state.config.stripe_secret_key, Some(""))
        .header("Stripe-Version", "2025-06-30.basil")
        .query(&[("code", code), ("active", "true"), ("limit", "1")])
        .send()
        .await?;
    if !response.status().is_success() {
        tracing::error!(status = %response.status(), "Stripe promotion-code lookup failed");
        return Err(ApiError::public(StatusCode::BAD_GATEWAY, "No pudimos validar el código de descuento."));
    }
    let result: StripeList<StripePromotionCode> = response.json().await?;
    result
        .data
        .into_iter()
        .find(|item| item.active)
        .map(|item| item.id)
        .ok_or_else(|| ApiError::public(StatusCode::BAD_REQUEST, "El código de descuento no es válido o ya venció."))
}

pub async fn plans(State(state): State<AppState>) -> ApiResult<Json<Value>> {
    let studio = stripe_price(&state, &state.config.stripe_studio_price_id).await?;
    let public = |price: StripePrice| {
        json!({
            "amount": price.unit_amount.unwrap_or_default(),
            "currency": price.currency.to_uppercase(),
            "interval": price.recurring.map(|item| item.interval).unwrap_or_default()
        })
    };
    let monthly = match &state.config.stripe_monthly_price_id {
        Some(price) => Some(public(stripe_price(&state, price).await?)),
        None => None,
    };
    let annual = match &state.config.stripe_annual_price_id {
        Some(price) => Some(public(stripe_price(&state, price).await?)),
        None => None,
    };
    Ok(Json(
        json!({ "studio": public(studio), "monthly": monthly, "annual": annual }),
    ))
}

async fn ensure_customer(
    state: &AppState,
    user_id: Uuid,
    email: &str,
    name: &str,
    existing: Option<String>,
) -> ApiResult<String> {
    if let Some(customer) = existing {
        return Ok(customer);
    }
    let customer = stripe_post(
        state,
        "customers",
        vec![
            ("email", email.into()),
            ("name", name.into()),
            ("metadata[pulso_user_id]", user_id.to_string()),
        ],
    )
    .await?
    .id;
    sqlx::query(
        "UPDATE users SET stripe_customer_id=$1 WHERE id=$2 AND stripe_customer_id IS NULL",
    )
    .bind(&customer)
    .bind(user_id)
    .execute(&state.db)
    .await?;
    Ok(customer)
}

pub async fn checkout(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(input): Json<CheckoutInput>,
) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    if input.plan == "studio" && user.studio_owned {
        return Err(ApiError::public(
            StatusCode::CONFLICT,
            "Ya tienes una licencia de PULSO Studio.",
        ));
    }
    if input.plan.starts_with("cloud_")
        && matches!(user.subscription.as_str(), "active" | "trialing")
    {
        return Err(ApiError::public(
            StatusCode::CONFLICT,
            "Ya tienes una suscripción activa. Adminístrala desde tu cuenta.",
        ));
    }
    let (price, mode) = match input.plan.as_str() {
        "studio" => (state.config.stripe_studio_price_id.clone(), "payment"),
        "cloud_monthly" => (
            state
                .config
                .stripe_monthly_price_id
                .clone()
                .ok_or_else(|| {
                    ApiError::public(
                        StatusCode::SERVICE_UNAVAILABLE,
                        "PULSO Cloud estará disponible próximamente.",
                    )
                })?,
            "subscription",
        ),
        "cloud_annual" => (
            state.config.stripe_annual_price_id.clone().ok_or_else(|| {
                ApiError::public(
                    StatusCode::SERVICE_UNAVAILABLE,
                    "PULSO Cloud estará disponible próximamente.",
                )
            })?,
            "subscription",
        ),
        _ => {
            return Err(ApiError::public(
                StatusCode::BAD_REQUEST,
                "Período de facturación inválido.",
            ));
        }
    };
    let customer = ensure_customer(
        &state,
        user.id,
        &user.email,
        &user.display_name,
        user.stripe_customer_id,
    )
    .await?;
    let supplied_promotion_code = input
        .promotion_code
        .as_deref()
        .map(str::trim)
        .filter(|code| !code.is_empty());
    let promotion_code = match supplied_promotion_code {
        Some(_) if normalized_code(input.promotion_code.as_deref()).is_none() => {
            return Err(ApiError::public(
                StatusCode::BAD_REQUEST,
                "El código de descuento tiene un formato inválido.",
            ));
        }
        Some(code) if code.eq_ignore_ascii_case(&state.config.southatoms_promo_code) => {
            return Err(ApiError::public(
                StatusCode::BAD_REQUEST,
                "SouthAtoms es un código de acceso gratuito. Canjéalo en la sección de invitaciones.",
            ));
        }
        Some(code) => Some(stripe_promotion_code(&state, code).await?),
        None => None,
    };
    let mut fields = vec![
        ("mode", mode.into()),
        ("customer", customer),
        ("client_reference_id", user.id.to_string()),
        ("line_items[0][price]", price),
        ("line_items[0][quantity]", "1".into()),
        ("automatic_tax[enabled]", "true".into()),
        ("success_url", format!("{}/account?checkout=success", state.config.public_url)),
        ("cancel_url", format!("{}/pricing?checkout=canceled", state.config.public_url)),
        ("metadata[pulso_user_id]", user.id.to_string()),
        ("metadata[pulso_plan]", input.plan.clone()),
    ];
    if let Some(promotion_code) = promotion_code {
        fields.push(("discounts[0][promotion_code]", promotion_code));
    } else {
        // Stripe's hosted checkout remains the canonical UI for any other
        // promotion code, including region-specific campaigns.
        fields.push(("allow_promotion_codes", "true".into()));
    }
    let session = stripe_post(
        &state,
        "checkout/sessions",
        fields,
    )
    .await?;
    let url = session
        .url
        .ok_or_else(|| ApiError::internal("Stripe checkout had no URL"))?;
    Ok(Json(json!({ "url": url })))
}

pub async fn redeem_promo(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(input): Json<RedeemPromotionInput>,
) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    let code = normalized_code(Some(&input.code)).ok_or_else(|| {
        ApiError::public(StatusCode::BAD_REQUEST, "Ingresa un código de invitación válido.")
    })?;
    if !code.eq_ignore_ascii_case(&state.config.southatoms_promo_code) {
        return Err(ApiError::public(StatusCode::BAD_REQUEST, "El código de invitación no es válido."));
    }

    let timestamp = now();
    let updates_until = timestamp + state.config.promotion_updates_days * 24 * 60 * 60;
    let campaign = "southatoms";
    let result = sqlx::query(
        "INSERT INTO promotion_redemptions(user_id,campaign,redeemed_at) VALUES($1,$2,$3) ON CONFLICT DO NOTHING",
    )
    .bind(user.id)
    .bind(campaign)
    .bind(timestamp)
    .execute(&state.db)
    .await?;

    sqlx::query("INSERT INTO entitlements(user_id,product,status,updates_until,source,created_at,updated_at) VALUES($1,'studio','active',$2,'promotion:southatoms',$3,$3) ON CONFLICT(user_id) DO UPDATE SET status='active',updates_until=GREATEST(COALESCE(entitlements.updates_until,0),EXCLUDED.updates_until),source=CASE WHEN entitlements.source='stripe' THEN entitlements.source ELSE EXCLUDED.source END,updated_at=EXCLUDED.updated_at")
        .bind(user.id)
        .bind(updates_until)
        .bind(timestamp)
        .execute(&state.db)
        .await?;

    tracing::info!(user_id = %user.id, campaign, new_redemption = result.rows_affected() == 1, "promotion entitlement granted");
    Ok(Json(json!({
        "granted": true,
        "alreadyRedeemed": result.rows_affected() == 0,
        "updatesUntil": updates_until,
    })))
}

pub async fn portal(State(state): State<AppState>, headers: HeaderMap) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    let customer = user.stripe_customer_id.ok_or_else(|| {
        ApiError::public(
            StatusCode::BAD_REQUEST,
            "Todavía no existe una cuenta de facturación.",
        )
    })?;
    let session = stripe_post(
        &state,
        "billing_portal/sessions",
        vec![
            ("customer", customer),
            ("return_url", format!("{}/account", state.config.public_url)),
        ],
    )
    .await?;
    Ok(Json(
        json!({ "url": session.url.ok_or_else(|| ApiError::internal("Stripe portal had no URL"))? }),
    ))
}

fn verify_signature(secret: &str, header: &str, body: &[u8]) -> ApiResult<()> {
    let mut timestamp = None;
    let mut signatures = Vec::new();
    for part in header.split(',') {
        if let Some((key, value)) = part.split_once('=') {
            if key == "t" {
                timestamp = value.parse::<i64>().ok();
            }
            if key == "v1" {
                signatures.push(value);
            }
        }
    }
    let timestamp =
        timestamp.ok_or_else(|| ApiError::public(StatusCode::BAD_REQUEST, "Firma inválida."))?;
    if (now() - timestamp).abs() > 300 {
        return Err(ApiError::public(StatusCode::BAD_REQUEST, "Firma vencida."));
    }
    let signed = format!("{timestamp}.{}", String::from_utf8_lossy(body));
    let valid = signatures.into_iter().any(|signature| {
        let Ok(bytes) = hex::decode(signature) else {
            return false;
        };
        let Ok(mut mac) = HmacSha256::new_from_slice(secret.as_bytes()) else {
            return false;
        };
        mac.update(signed.as_bytes());
        mac.verify_slice(&bytes).is_ok()
    });
    if !valid {
        return Err(ApiError::public(StatusCode::BAD_REQUEST, "Firma inválida."));
    }
    Ok(())
}

fn text<'a>(value: &'a Value, pointer: &str) -> Option<&'a str> {
    value.pointer(pointer)?.as_str()
}

async fn update_subscription(state: &AppState, object: &Value, deleted: bool) -> ApiResult<()> {
    let customer = text(object, "/customer")
        .ok_or_else(|| ApiError::internal("subscription missing customer"))?;
    let user_id: Option<Uuid> =
        sqlx::query_scalar("SELECT id FROM users WHERE stripe_customer_id=$1")
            .bind(customer)
            .fetch_optional(&state.db)
            .await?;
    let Some(user_id) = user_id else {
        tracing::warn!(%customer, "Stripe customer is not linked to PULSO");
        return Ok(());
    };
    let status = if deleted {
        "canceled"
    } else {
        text(object, "/status").unwrap_or("none")
    };
    let subscription_id = text(object, "/id");
    let price_id = text(object, "/items/data/0/price/id");
    let period_end = object
        .pointer("/current_period_end")
        .and_then(Value::as_i64)
        .or_else(|| {
            object
                .pointer("/items/data/0/current_period_end")
                .and_then(Value::as_i64)
        });
    sqlx::query("INSERT INTO subscriptions (user_id,stripe_subscription_id,status,price_id,current_period_end,updated_at) VALUES ($1,$2,$3,$4,$5,$6) \
                 ON CONFLICT (user_id) DO UPDATE SET stripe_subscription_id=EXCLUDED.stripe_subscription_id,status=EXCLUDED.status,price_id=EXCLUDED.price_id,current_period_end=EXCLUDED.current_period_end,updated_at=EXCLUDED.updated_at")
        .bind(user_id).bind(subscription_id).bind(status).bind(price_id).bind(period_end).bind(now()).execute(&state.db).await?;
    Ok(())
}

async fn grant_studio(state: &AppState, object: &Value) -> ApiResult<()> {
    if text(object, "/metadata/pulso_plan") != Some("studio")
        || text(object, "/payment_status") != Some("paid")
    {
        return Ok(());
    }
    let user_id = text(object, "/metadata/pulso_user_id")
        .and_then(|value| Uuid::parse_str(value).ok())
        .ok_or_else(|| ApiError::internal("checkout session missing PULSO user"))?;
    let updates_until = now() + 365 * 24 * 60 * 60;
    sqlx::query("INSERT INTO entitlements(user_id,product,status,updates_until,source,created_at,updated_at) VALUES($1,'studio','active',$2,'stripe',$3,$3) ON CONFLICT(user_id) DO UPDATE SET status='active',updates_until=GREATEST(COALESCE(entitlements.updates_until,0),EXCLUDED.updates_until),updated_at=EXCLUDED.updated_at")
        .bind(user_id).bind(updates_until).bind(now()).execute(&state.db).await?;
    Ok(())
}

pub async fn webhook(
    State(state): State<AppState>,
    headers: HeaderMap,
    body: Bytes,
) -> ApiResult<StatusCode> {
    let signature = headers
        .get("stripe-signature")
        .and_then(|value| value.to_str().ok())
        .ok_or_else(|| ApiError::public(StatusCode::BAD_REQUEST, "Falta la firma de Stripe."))?;
    verify_signature(&state.config.stripe_webhook_secret, signature, &body)?;
    let event: Value = serde_json::from_slice(&body)
        .map_err(|_| ApiError::public(StatusCode::BAD_REQUEST, "Evento inválido."))?;
    let event_id = text(&event, "/id")
        .ok_or_else(|| ApiError::public(StatusCode::BAD_REQUEST, "Evento sin identificador."))?;
    let event_type = text(&event, "/type").unwrap_or("unknown");
    let already_processed: bool =
        sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM stripe_events WHERE event_id=$1)")
            .bind(event_id)
            .fetch_one(&state.db)
            .await?;
    if already_processed {
        return Ok(StatusCode::OK);
    }
    if matches!(
        event_type,
        "customer.subscription.created"
            | "customer.subscription.updated"
            | "customer.subscription.deleted"
    ) {
        update_subscription(
            &state,
            &event["data"]["object"],
            event_type.ends_with("deleted"),
        )
        .await?;
    } else if event_type == "checkout.session.completed" {
        grant_studio(&state, &event["data"]["object"]).await?;
    }
    // Record the event only after its side effects succeed. Stripe can safely retry a
    // transient database failure; concurrent duplicates remain harmless because the
    // subscription write is an upsert and the event key is unique.
    sqlx::query("INSERT INTO stripe_events (event_id,event_type,processed_at) VALUES ($1,$2,$3) ON CONFLICT DO NOTHING")
        .bind(event_id)
        .bind(event_type)
        .bind(now())
        .execute(&state.db)
        .await?;
    Ok(StatusCode::OK)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn verifies_stripe_signature_and_rejects_tampering() {
        let body = br#"{"id":"evt_1"}"#;
        let timestamp = now();
        let mut mac = HmacSha256::new_from_slice(b"whsec_test").unwrap();
        mac.update(format!("{timestamp}.{}", String::from_utf8_lossy(body)).as_bytes());
        let header = format!(
            "t={timestamp},v1={}",
            hex::encode(mac.finalize().into_bytes())
        );
        assert!(verify_signature("whsec_test", &header, body).is_ok());
        assert!(verify_signature("whsec_test", &header, b"tampered").is_err());
    }
}
