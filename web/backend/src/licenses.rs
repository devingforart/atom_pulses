use axum::{
    Json,
    extract::{Path, State},
    http::{HeaderMap, StatusCode},
};
use serde::{Deserialize, Serialize};
use serde_json::json;
use uuid::Uuid;

use crate::{
    AppState,
    auth::require_user,
    crypto::{hash, random_token, sign_json, unix_time},
    error::{ApiError, ApiResult},
};

const DEVICE_CODE_SECONDS: i64 = 10 * 60;
const OFFLINE_LICENSE_SECONDS: i64 = 30 * 24 * 60 * 60;
const MAX_ACTIVE_DEVICES: i64 = 2;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceCodeInput {
    device_id: String,
    device_name: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceApproveInput {
    code: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceTokenInput {
    code: String,
    device_secret: String,
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RefreshInput {
    refresh_token: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct LicenseClaims {
    version: u8,
    issuer: &'static str,
    subject: Uuid,
    activation_id: Uuid,
    device_id: String,
    product: &'static str,
    issued_at: i64,
    expires_at: i64,
}

fn normalized_code(code: &str) -> String {
    code.chars()
        .filter(|c| c.is_ascii_alphanumeric())
        .collect::<String>()
        .to_uppercase()
}

async fn entitled(state: &AppState, user_id: Uuid) -> ApiResult<bool> {
    let active: bool = sqlx::query_scalar(
        "SELECT EXISTS(SELECT 1 FROM entitlements WHERE user_id=$1 AND status='active') OR EXISTS(SELECT 1 FROM subscriptions WHERE user_id=$1 AND status IN ('active','trialing'))"
    ).bind(user_id).fetch_one(&state.db).await?;
    Ok(active)
}

fn issue_license(
    state: &AppState,
    user_id: Uuid,
    activation_id: Uuid,
    device_id: String,
) -> ApiResult<(String, i64)> {
    let now = unix_time();
    let expires_at = now + OFFLINE_LICENSE_SECONDS;
    let seed = state
        .config
        .license_signing_key
        .as_deref()
        .ok_or_else(|| ApiError::configuration("missing PULSO_LICENSE_SIGNING_KEY"))?;
    let token = sign_json(
        seed,
        &LicenseClaims {
            version: 1,
            issuer: "pulso.music",
            subject: user_id,
            activation_id,
            device_id,
            product: "studio",
            issued_at: now,
            expires_at,
        },
    )?;
    Ok((token, expires_at))
}

pub async fn create_device_code(
    State(state): State<AppState>,
    Json(input): Json<DeviceCodeInput>,
) -> ApiResult<Json<serde_json::Value>> {
    let device_id = input.device_id.trim();
    let device_name = input.device_name.trim();
    if device_id.len() < 8
        || device_id.len() > 160
        || device_name.is_empty()
        || device_name.len() > 100
    {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "Dispositivo inválido.",
        ));
    }
    let raw = Uuid::new_v4().simple().to_string()[..8].to_uppercase();
    let code = format!("{}-{}", &raw[..4], &raw[4..8]);
    let secret = random_token(32)?;
    let now = unix_time();
    sqlx::query("INSERT INTO device_codes(code_hash,secret_hash,device_id,device_name,expires_at,created_at) VALUES($1,$2,$3,$4,$5,$6)")
        .bind(hash(&normalized_code(&code))).bind(hash(&secret)).bind(device_id).bind(device_name).bind(now + DEVICE_CODE_SECONDS).bind(now).execute(&state.db).await?;
    Ok(Json(
        json!({ "code": code, "deviceSecret": secret, "verificationUrl": format!("{}/activate?code={}", state.config.public_url, code), "expiresAt": now + DEVICE_CODE_SECONDS, "pollIntervalSeconds": 5 }),
    ))
}

pub async fn approve_device(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(input): Json<DeviceApproveInput>,
) -> ApiResult<Json<serde_json::Value>> {
    let user = require_user(&state, &headers).await?;
    if !user.email_verified {
        return Err(ApiError::public(
            StatusCode::FORBIDDEN,
            "Verifica tu email antes de activar PULSO.",
        ));
    }
    if !entitled(&state, user.id).await? {
        return Err(ApiError::public(
            StatusCode::PAYMENT_REQUIRED,
            "Necesitas una licencia activa de PULSO.",
        ));
    }
    let updated = sqlx::query("UPDATE device_codes SET user_id=$1,approved_at=$2 WHERE code_hash=$3 AND expires_at>$2 AND approved_at IS NULL AND consumed_at IS NULL")
        .bind(user.id).bind(unix_time()).bind(hash(&normalized_code(&input.code))).execute(&state.db).await?;
    if updated.rows_affected() != 1 {
        return Err(ApiError::public(
            StatusCode::NOT_FOUND,
            "El código no existe o venció.",
        ));
    }
    Ok(Json(json!({"approved": true})))
}

pub async fn exchange_device_token(
    State(state): State<AppState>,
    Json(input): Json<DeviceTokenInput>,
) -> ApiResult<Json<serde_json::Value>> {
    let code_hash = hash(&normalized_code(&input.code));
    let now = unix_time();
    let mut transaction = state.db.begin().await?;
    let row: Option<(Uuid, String, String)> = sqlx::query_as("SELECT user_id,device_id,device_name FROM device_codes WHERE code_hash=$1 AND secret_hash=$2 AND approved_at IS NOT NULL AND consumed_at IS NULL AND expires_at>$3 FOR UPDATE")
        .bind(&code_hash).bind(hash(&input.device_secret)).bind(now).fetch_optional(&mut *transaction).await?;
    let Some((user_id, device_id, device_name)) = row else {
        return Err(ApiError::public(
            StatusCode::ACCEPTED,
            "Activación pendiente o código vencido.",
        ));
    };
    // Serialize activation changes for this account so two simultaneous device
    // exchanges cannot both observe an available slot.
    sqlx::query_scalar::<_, Uuid>("SELECT id FROM users WHERE id=$1 FOR UPDATE")
        .bind(user_id)
        .fetch_one(&mut *transaction)
        .await?;
    let has_entitlement: bool = sqlx::query_scalar(
        "SELECT EXISTS(SELECT 1 FROM entitlements WHERE user_id=$1 AND status='active') OR EXISTS(SELECT 1 FROM subscriptions WHERE user_id=$1 AND status IN ('active','trialing'))",
    )
    .bind(user_id)
    .fetch_one(&mut *transaction)
    .await?;
    if !has_entitlement {
        return Err(ApiError::public(
            StatusCode::PAYMENT_REQUIRED,
            "La licencia ya no está activa.",
        ));
    }
    let existing: Option<Uuid> = sqlx::query_scalar(
        "SELECT id FROM activations WHERE user_id=$1 AND device_id=$2 AND revoked_at IS NULL",
    )
    .bind(user_id)
    .bind(&device_id)
    .fetch_optional(&mut *transaction)
    .await?;
    let active_count: i64 = sqlx::query_scalar(
        "SELECT COUNT(*) FROM activations WHERE user_id=$1 AND revoked_at IS NULL",
    )
    .bind(user_id)
    .fetch_one(&mut *transaction)
    .await?;
    if existing.is_none() && active_count >= MAX_ACTIVE_DEVICES {
        return Err(ApiError::public(
            StatusCode::CONFLICT,
            "Alcanzaste el máximo de 2 dispositivos. Revoca uno desde tu cuenta.",
        ));
    }
    let activation_id = existing.unwrap_or_else(Uuid::new_v4);
    let refresh = random_token(48)?;
    sqlx::query("INSERT INTO activations(id,user_id,device_id,device_name,refresh_token_hash,last_seen_at,created_at) VALUES($1,$2,$3,$4,$5,$6,$6) ON CONFLICT(user_id,device_id) DO UPDATE SET device_name=EXCLUDED.device_name,refresh_token_hash=EXCLUDED.refresh_token_hash,last_seen_at=EXCLUDED.last_seen_at,revoked_at=NULL")
        .bind(activation_id).bind(user_id).bind(&device_id).bind(device_name).bind(hash(&refresh)).bind(now).execute(&mut *transaction).await?;
    let consumed = sqlx::query(
        "UPDATE device_codes SET consumed_at=$1 WHERE code_hash=$2 AND consumed_at IS NULL",
    )
    .bind(now)
    .bind(&code_hash)
    .execute(&mut *transaction)
    .await?;
    if consumed.rows_affected() != 1 {
        return Err(ApiError::public(
            StatusCode::CONFLICT,
            "El código de activación ya fue utilizado.",
        ));
    }
    transaction.commit().await?;
    let (license, expires_at) = issue_license(&state, user_id, activation_id, device_id)?;
    Ok(Json(
        json!({"licenseToken": license, "refreshToken": refresh, "expiresAt": expires_at}),
    ))
}

pub async fn refresh(
    State(state): State<AppState>,
    Json(input): Json<RefreshInput>,
) -> ApiResult<Json<serde_json::Value>> {
    let row: Option<(Uuid, Uuid, String)> = sqlx::query_as("SELECT id,user_id,device_id FROM activations WHERE refresh_token_hash=$1 AND revoked_at IS NULL")
        .bind(hash(&input.refresh_token)).fetch_optional(&state.db).await?;
    let Some((activation_id, user_id, device_id)) = row else {
        return Err(ApiError::public(
            StatusCode::UNAUTHORIZED,
            "Activación inválida o revocada.",
        ));
    };
    if !entitled(&state, user_id).await? {
        return Err(ApiError::public(
            StatusCode::PAYMENT_REQUIRED,
            "La licencia ya no está activa.",
        ));
    }
    sqlx::query("UPDATE activations SET last_seen_at=$1 WHERE id=$2")
        .bind(unix_time())
        .bind(activation_id)
        .execute(&state.db)
        .await?;
    let (license, expires_at) = issue_license(&state, user_id, activation_id, device_id)?;
    Ok(Json(
        json!({"licenseToken": license, "expiresAt": expires_at}),
    ))
}

pub async fn list(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> ApiResult<Json<serde_json::Value>> {
    let user = require_user(&state, &headers).await?;
    let rows: Vec<(Uuid,String,i64,i64)> = sqlx::query_as("SELECT id,device_name,created_at,last_seen_at FROM activations WHERE user_id=$1 AND revoked_at IS NULL ORDER BY last_seen_at DESC").bind(user.id).fetch_all(&state.db).await?;
    let devices: Vec<_> = rows.into_iter().map(|(id,name,created,last_seen)| json!({"id":id,"name":name,"createdAt":created,"lastSeenAt":last_seen})).collect();
    Ok(Json(
        json!({"devices": devices, "limit": MAX_ACTIVE_DEVICES}),
    ))
}

pub async fn revoke(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> ApiResult<StatusCode> {
    let user = require_user(&state, &headers).await?;
    let result = sqlx::query(
        "UPDATE activations SET revoked_at=$1 WHERE id=$2 AND user_id=$3 AND revoked_at IS NULL",
    )
    .bind(unix_time())
    .bind(id)
    .bind(user.id)
    .execute(&state.db)
    .await?;
    if result.rows_affected() == 0 {
        return Err(ApiError::public(
            StatusCode::NOT_FOUND,
            "Dispositivo no encontrado.",
        ));
    }
    Ok(StatusCode::NO_CONTENT)
}
