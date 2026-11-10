//! Growth manages offers; this API alone owns redemption and usage enforcement.
use axum::{Json, extract::{Path, State}, http::{HeaderMap, StatusCode}};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use sqlx::{FromRow, PgConnection};
use uuid::Uuid;
use crate::{AppState, auth::require_user, crypto::{hash, unix_time}, error::{ApiError, ApiResult}};

fn invalid(message: &str) -> ApiError { ApiError::public(StatusCode::BAD_REQUEST, message) }
pub fn normalize(code: &str) -> Option<String> {
    let code = code.trim().to_ascii_uppercase();
    ((8..=64).contains(&code.len()) && code.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'-')).then_some(code)
}
pub(crate) fn admin(headers: &HeaderMap) -> ApiResult<()> {
    let expected = std::env::var("PULSO_GROWTH_ADMIN_TOKEN").unwrap_or_default();
    if expected.len() < 32 { return Err(ApiError::configuration("Growth invitation management is not configured")); }
    let supplied = headers.get("authorization").and_then(|h| h.to_str().ok()).and_then(|s| s.strip_prefix("Bearer ")).unwrap_or("");
    let a = hash(&expected); let b = hash(supplied);
    if a.bytes().zip(b.bytes()).fold(0u8, |n,(a,b)| n | (a ^ b)) != 0 {
        return Err(ApiError::public(StatusCode::UNAUTHORIZED, "Acceso restringido."));
    }
    Ok(())
}
#[derive(Debug, Deserialize)]
#[serde(rename_all="camelCase", deny_unknown_fields)]
pub struct Create {
    id: Uuid, code: String, label: String, target_email: Option<String>,
    credits: i64, daily_limit: i64, weekly_limit: i64, monthly_limit: i64,
    max_redemptions: i64, expires_at: Option<i64>,
}
#[derive(Serialize, FromRow)]
#[serde(rename_all="camelCase")]
pub struct Invitation {
    id: Uuid, code_hint: String, label: String, target_email: Option<String>, credits: i64,
    daily_limit: i64, weekly_limit: i64, monthly_limit: i64, max_redemptions: i64,
    expires_at: Option<i64>, revoked_at: Option<i64>, created_at: i64, redemptions: i64, email_status: Option<String>,
}
const LIST: &str = "SELECT i.id,i.code_hint,i.label,i.target_email,i.credits,i.daily_limit,i.weekly_limit,i.monthly_limit,i.max_redemptions,i.expires_at,i.revoked_at,i.created_at,(SELECT e.status FROM cloud_invitation_emails e WHERE e.invitation_id=i.id) AS email_status,(SELECT count(*) FROM cloud_invitation_redemptions r WHERE r.invitation_id=i.id) AS redemptions FROM cloud_invitations i ORDER BY i.created_at DESC LIMIT 500";
pub async fn list(State(state): State<AppState>, headers: HeaderMap) -> ApiResult<Json<Value>> {
    admin(&headers)?;
    Ok(Json(json!({"items": sqlx::query_as::<_,Invitation>(LIST).fetch_all(&state.db).await?})))
}
fn valid(input: &Create, now: i64) -> bool {
    normalize(&input.code).is_some() && (1..=120).contains(&input.label.trim().chars().count())
    && (3..=1_000_000).contains(&input.credits) && (1..=1000).contains(&input.daily_limit)
    && (input.daily_limit..=10000).contains(&input.weekly_limit)
    && (input.weekly_limit..=100000).contains(&input.monthly_limit)
    && (1..=10000).contains(&input.max_redemptions)
    && input.expires_at.is_none_or(|at| at > now && at <= now + 366*86400)
    && input.target_email.as_ref().is_none_or(|email| email.len() <= 254 && email.contains('@') && !email.chars().any(char::is_whitespace))
    && (input.target_email.is_none() || input.max_redemptions == 1)
}
pub async fn create(State(state): State<AppState>, headers: HeaderMap, Json(input): Json<Create>) -> ApiResult<Json<Value>> {
    admin(&headers)?;
    let now = unix_time();
    if !valid(&input, now) { return Err(invalid("Revisa código, email, créditos, límites y vencimiento. Los límites deben ser día ≤ semana ≤ mes.")); }
    let code = normalize(&input.code).unwrap();
    // Legacy SouthAtoms must keep its existing meaning.
    if code.eq_ignore_ascii_case(&state.config.southatoms_promo_code) { return Err(invalid("Este código está reservado.")); }
    let email = input.target_email.as_ref().map(|s| s.to_lowercase());
    let hint = format!("{}…{}", &code[..3], &code[code.len()-3..]);
    let mut tx = state.db.begin().await?;
    sqlx::query("INSERT INTO cloud_invitations(id,code_hash,code_hint,label,target_email,credits,daily_limit,weekly_limit,monthly_limit,max_redemptions,expires_at,created_at) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12) ON CONFLICT DO NOTHING")
        .bind(input.id).bind(hash(&code)).bind(hint).bind(input.label.trim()).bind(&email)
        .bind(input.credits).bind(input.daily_limit).bind(input.weekly_limit).bind(input.monthly_limit)
        .bind(input.max_redemptions).bind(input.expires_at).bind(now).execute(&mut *tx).await?;
    let matches: bool = sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM cloud_invitations WHERE id=$1 AND code_hash=$2 AND label=$3 AND target_email IS NOT DISTINCT FROM $4 AND credits=$5 AND daily_limit=$6 AND weekly_limit=$7 AND monthly_limit=$8 AND max_redemptions=$9 AND expires_at IS NOT DISTINCT FROM $10)")
        .bind(input.id).bind(hash(&code)).bind(input.label.trim()).bind(email).bind(input.credits)
        .bind(input.daily_limit).bind(input.weekly_limit).bind(input.monthly_limit).bind(input.max_redemptions).bind(input.expires_at).fetch_one(&mut *tx).await?;
    if !matches { return Err(ApiError::public(StatusCode::CONFLICT,"El código o identificador ya existe con otros ajustes.")); }
    tx.commit().await?;
    Ok(Json(json!({"id":input.id,"code":code,"saved":true})))
}
pub async fn revoke(State(state): State<AppState>, headers: HeaderMap, Path(id): Path<Uuid>) -> ApiResult<Json<Value>> {
    admin(&headers)?;
    let result = sqlx::query("UPDATE cloud_invitations SET revoked_at=COALESCE(revoked_at,$2) WHERE id=$1").bind(id).bind(unix_time()).execute(&state.db).await?;
    if result.rows_affected() == 0 { return Err(ApiError::public(StatusCode::NOT_FOUND,"Invitación no encontrada.")); }
    Ok(Json(json!({"revoked":true})))
}

#[derive(FromRow)]
pub struct Grant { pub id:Uuid, pub credits:i64, pub daily_limit:i64, pub weekly_limit:i64, pub monthly_limit:i64 }
pub async fn grant(conn: &mut PgConnection, user: Uuid) -> ApiResult<Option<Grant>> {
    Ok(sqlx::query_as("SELECT i.id,i.credits,i.daily_limit,i.weekly_limit,i.monthly_limit FROM cloud_invitation_redemptions r JOIN cloud_invitations i ON i.id=r.invitation_id WHERE r.user_id=$1 AND i.revoked_at IS NULL AND (i.expires_at IS NULL OR i.expires_at>$2) ORDER BY r.redeemed_at DESC LIMIT 1")
        .bind(user).bind(unix_time()).fetch_optional(conn).await?)
}
pub async fn has_access(state: &AppState, user: Uuid) -> ApiResult<bool> {
    let mut conn = state.db.acquire().await?;
    Ok(grant(&mut conn,user).await?.is_some())
}
pub async fn redeem(state: &AppState, headers: &HeaderMap, code: &str) -> ApiResult<Json<Value>> {
    let user = require_user(state,headers).await?;
    if !user.email_verified { return Err(ApiError::public(StatusCode::FORBIDDEN,"Verifica tu email antes de canjear la invitación.")); }
    let code = normalize(code).ok_or_else(|| invalid("Código de invitación inválido."))?;
    let mut tx = state.db.begin().await?;
    sqlx::query("SELECT pg_advisory_xact_lock(hashtext($1::text))").bind(user.id.to_string()).execute(&mut *tx).await?;
    let row: Option<(Uuid,Option<String>,i64,Option<i64>,Option<i64>)> = sqlx::query_as("SELECT id,target_email,max_redemptions,expires_at,revoked_at FROM cloud_invitations WHERE code_hash=$1 FOR UPDATE")
        .bind(hash(&code)).fetch_optional(&mut *tx).await?;
    let (id,email,max,expires,revoked) = row.ok_or_else(|| invalid("Código de invitación inválido."))?;
    if revoked.is_some() || expires.is_some_and(|at|at<=unix_time()) || email.is_some_and(|e| !e.eq_ignore_ascii_case(&user.email)) {
        return Err(invalid("La invitación no está disponible para esta cuenta o venció."));
    }
    let already:bool = sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM cloud_invitation_redemptions WHERE user_id=$1 AND invitation_id=$2)").bind(user.id).bind(id).fetch_one(&mut *tx).await?;
    if !already {
        if grant(&mut tx,user.id).await?.is_some() { return Err(ApiError::public(StatusCode::CONFLICT,"Ya tienes otra invitación activa; no se acumulan accesos promocionales.")); }
        let count:i64 = sqlx::query_scalar("SELECT count(*) FROM cloud_invitation_redemptions WHERE invitation_id=$1").bind(id).fetch_one(&mut *tx).await?;
        if count>=max { return Err(invalid("La invitación alcanzó su máximo de participantes.")); }
        sqlx::query("INSERT INTO cloud_invitation_redemptions(user_id,invitation_id,redeemed_at) VALUES($1,$2,$3)").bind(user.id).bind(id).bind(unix_time()).execute(&mut *tx).await?;
    }
    tx.commit().await?;
    Ok(Json(json!({"granted":true,"alreadyRedeemed":already,"cloudOnly":true})))
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn codes_are_canonical_and_safe() {
        assert_eq!(normalize("  Artist-123  ").unwrap(),"ARTIST-123");
        for code in ["short","with spaces","código-123", "<script>"] { assert!(normalize(code).is_none()); }
    }
    #[test] fn policies_are_bounded() {
        let mut input=Create{id:Uuid::new_v4(),code:"TEST-1234".into(),label:"Artists".into(),target_email:None,credits:30,daily_limit:3,weekly_limit:7,monthly_limit:12,max_redemptions:1,expires_at:Some(200)};
        assert!(valid(&input,100)); input.weekly_limit=2; assert!(!valid(&input,100));
        input.weekly_limit=7; input.credits=2; assert!(!valid(&input,100));
    }
}
