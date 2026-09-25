use argon2::{
    Argon2, PasswordHash, PasswordHasher, PasswordVerifier,
    password_hash::{SaltString, rand_core::OsRng},
};
use axum::{
    Json,
    extract::State,
    http::{HeaderMap, HeaderValue, StatusCode, header},
    response::IntoResponse,
};
use serde::Deserialize;
use serde_json::json;
use uuid::Uuid;

use crate::{
    AppState,
    crypto::{hash as token_hash, random_token as make_token, unix_time},
    error::{ApiError, ApiResult},
    mail,
    models::{AuthenticatedUser, LoginInput, PublicUser, RegisterInput, UserRecord},
};

const COOKIE_NAME: &str = "pulso_session";
const SESSION_SECONDS: i64 = 60 * 60 * 24 * 30;
const VERIFY_SECONDS: i64 = 60 * 60 * 24;
const RESET_SECONDS: i64 = 60 * 30;

#[derive(Deserialize)]
pub struct EmailInput {
    email: String,
}

#[derive(Deserialize)]
pub struct TokenInput {
    token: String,
}

#[derive(Deserialize)]
pub struct ResetPasswordInput {
    token: String,
    password: String,
}

fn normalize_email(email: &str) -> String {
    email.trim().to_lowercase()
}
fn session_token() -> ApiResult<String> {
    make_token(32)
}

fn cookie_value(headers: &HeaderMap) -> Option<String> {
    headers
        .get(header::COOKIE)?
        .to_str()
        .ok()?
        .split(';')
        .find_map(|item| {
            let (name, value) = item.trim().split_once('=')?;
            (name == COOKIE_NAME).then(|| value.to_string())
        })
}

fn set_cookie(token: &str, secure: bool) -> ApiResult<HeaderValue> {
    let secure_flag = if secure { "; Secure" } else { "" };
    HeaderValue::from_str(&format!("{COOKIE_NAME}={token}; Path=/; HttpOnly; SameSite=Lax; Max-Age={SESSION_SECONDS}{secure_flag}"))
        .map_err(|error| ApiError::internal(error.to_string()))
}

fn clear_cookie(secure: bool) -> ApiResult<HeaderValue> {
    let secure_flag = if secure { "; Secure" } else { "" };
    HeaderValue::from_str(&format!(
        "{COOKIE_NAME}=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0{secure_flag}"
    ))
    .map_err(|error| ApiError::internal(error.to_string()))
}

async fn new_session(state: &AppState, user_id: Uuid) -> ApiResult<String> {
    let token = session_token()?;
    let now = unix_time();
    sqlx::query(
        "INSERT INTO sessions (token_hash, user_id, expires_at, created_at) VALUES ($1,$2,$3,$4)",
    )
    .bind(token_hash(&token))
    .bind(user_id)
    .bind(now + SESSION_SECONDS)
    .bind(now)
    .execute(&state.db)
    .await?;
    Ok(token)
}

async fn new_action_token(
    state: &AppState,
    user_id: Uuid,
    purpose: &str,
    ttl: i64,
) -> ApiResult<String> {
    let token = make_token(32)?;
    let now = unix_time();
    sqlx::query("DELETE FROM auth_tokens WHERE user_id=$1 AND purpose=$2 AND used_at IS NULL")
        .bind(user_id)
        .bind(purpose)
        .execute(&state.db)
        .await?;
    sqlx::query("INSERT INTO auth_tokens(token_hash,user_id,purpose,expires_at,created_at) VALUES($1,$2,$3,$4,$5)")
        .bind(token_hash(&token)).bind(user_id).bind(purpose).bind(now + ttl).bind(now).execute(&state.db).await?;
    Ok(token)
}

pub async fn optional_user(
    state: &AppState,
    headers: &HeaderMap,
) -> ApiResult<Option<AuthenticatedUser>> {
    let Some(token) = cookie_value(headers) else {
        return Ok(None);
    };
    let user = sqlx::query_as::<_, AuthenticatedUser>(
        "SELECT u.id,u.email,u.display_name,u.stripe_customer_id,COALESCE(su.status,'none') AS subscription,u.email_verified,EXISTS(SELECT 1 FROM entitlements e WHERE e.user_id=u.id AND e.status='active') AS studio_owned,(SELECT e.updates_until FROM entitlements e WHERE e.user_id=u.id AND e.status='active' LIMIT 1) AS studio_updates_until \
         FROM sessions s JOIN users u ON u.id=s.user_id LEFT JOIN subscriptions su ON su.user_id=u.id \
         WHERE s.token_hash=$1 AND s.expires_at>$2")
        .bind(token_hash(&token)).bind(unix_time()).fetch_optional(&state.db).await?;
    Ok(user)
}

pub async fn require_user(state: &AppState, headers: &HeaderMap) -> ApiResult<AuthenticatedUser> {
    optional_user(state, headers)
        .await?
        .ok_or_else(|| ApiError::public(StatusCode::UNAUTHORIZED, "Ingresa para continuar."))
}

pub async fn register(
    State(state): State<AppState>,
    Json(input): Json<RegisterInput>,
) -> ApiResult<impl IntoResponse> {
    let email = normalize_email(&input.email);
    let display_name = input.display_name.trim();
    if !email.contains('@')
        || email.len() > 254
        || display_name.len() < 2
        || display_name.len() > 80
    {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "Revisa el email y el nombre.",
        ));
    }
    if input.password.len() < 12 || input.password.len() > 256 {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "La contraseña debe tener al menos 12 caracteres.",
        ));
    }
    let salt = SaltString::generate(&mut OsRng);
    let password_hash = Argon2::default()
        .hash_password(input.password.as_bytes(), &salt)
        .map_err(|error| ApiError::internal(error.to_string()))?
        .to_string();
    let id = Uuid::new_v4();
    let inserted = sqlx::query("INSERT INTO users (id,email,display_name,password_hash,created_at) VALUES ($1,$2,$3,$4,$5)")
        .bind(id).bind(&email).bind(display_name).bind(password_hash).bind(unix_time()).execute(&state.db).await;
    if let Err(sqlx::Error::Database(error)) = &inserted
        && error.is_unique_violation()
    {
        return Err(ApiError::public(
            StatusCode::CONFLICT,
            "Ya existe una cuenta con ese email.",
        ));
    }
    inserted?;
    let verification = new_action_token(&state, id, "verify_email", VERIFY_SECONDS).await?;
    let action_url = format!(
        "{}/verify-email?token={}",
        state.config.public_url, verification
    );
    if let Err(error) = mail::send_action(
        &state,
        &email,
        "Verifica tu cuenta PULSO",
        "Verifica tu email",
        &action_url,
    )
    .await
    {
        tracing::error!(%error, %email, "verification email delivery failed");
    }
    let token = new_session(&state, id).await?;
    let user = PublicUser {
        id,
        email,
        display_name: display_name.into(),
        subscription: "none".into(),
        email_verified: false,
        studio_owned: false,
        studio_updates_until: None,
    };
    Ok((
        [(
            header::SET_COOKIE,
            set_cookie(&token, state.config.secure_cookies)?,
        )],
        Json(json!({ "user": user })),
    ))
}

pub async fn login(
    State(state): State<AppState>,
    Json(input): Json<LoginInput>,
) -> ApiResult<impl IntoResponse> {
    let record = sqlx::query_as::<_, UserRecord>(
        "SELECT id,email,display_name,password_hash,email_verified FROM users WHERE email=$1",
    )
    .bind(normalize_email(&input.email))
    .fetch_optional(&state.db)
    .await?;
    let Some(record) = record else {
        return Err(ApiError::public(
            StatusCode::UNAUTHORIZED,
            "Email o contraseña incorrectos.",
        ));
    };
    let parsed = PasswordHash::new(&record.password_hash)
        .map_err(|error| ApiError::internal(error.to_string()))?;
    if Argon2::default()
        .verify_password(input.password.as_bytes(), &parsed)
        .is_err()
    {
        return Err(ApiError::public(
            StatusCode::UNAUTHORIZED,
            "Email o contraseña incorrectos.",
        ));
    }
    let token = new_session(&state, record.id).await?;
    let subscription: String = sqlx::query_scalar(
        "SELECT COALESCE((SELECT status FROM subscriptions WHERE user_id=$1),'none')",
    )
    .bind(record.id)
    .fetch_one(&state.db)
    .await?;
    let studio: Option<i64> = sqlx::query_scalar(
        "SELECT updates_until FROM entitlements WHERE user_id=$1 AND status='active'",
    )
    .bind(record.id)
    .fetch_optional(&state.db)
    .await?
    .flatten();
    let user = PublicUser {
        id: record.id,
        email: record.email,
        display_name: record.display_name,
        subscription,
        email_verified: record.email_verified,
        studio_owned: studio.is_some(),
        studio_updates_until: studio,
    };
    Ok((
        [(
            header::SET_COOKIE,
            set_cookie(&token, state.config.secure_cookies)?,
        )],
        Json(json!({ "user": user })),
    ))
}

pub async fn request_password_reset(
    State(state): State<AppState>,
    Json(input): Json<EmailInput>,
) -> ApiResult<StatusCode> {
    let email = normalize_email(&input.email);
    let user_id: Option<Uuid> = sqlx::query_scalar("SELECT id FROM users WHERE email=$1")
        .bind(&email)
        .fetch_optional(&state.db)
        .await?;
    if let Some(user_id) = user_id {
        let token = new_action_token(&state, user_id, "reset_password", RESET_SECONDS).await?;
        let url = format!("{}/reset-password?token={}", state.config.public_url, token);
        if let Err(error) = mail::send_action(
            &state,
            &email,
            "Restablece tu contraseña PULSO",
            "Crea una contraseña nueva",
            &url,
        )
        .await
        {
            tracing::error!(%error, %email, "password reset email delivery failed");
        }
    }
    Ok(StatusCode::NO_CONTENT)
}

pub async fn reset_password(
    State(state): State<AppState>,
    Json(input): Json<ResetPasswordInput>,
) -> ApiResult<StatusCode> {
    if input.password.len() < 12 || input.password.len() > 256 {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "La contraseña debe tener al menos 12 caracteres.",
        ));
    }
    let now = unix_time();
    let user_id: Option<Uuid> = sqlx::query_scalar("UPDATE auth_tokens SET used_at=$1 WHERE token_hash=$2 AND purpose='reset_password' AND used_at IS NULL AND expires_at>$1 RETURNING user_id")
        .bind(now).bind(token_hash(&input.token)).fetch_optional(&state.db).await?;
    let Some(user_id) = user_id else {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "El enlace es inválido o venció.",
        ));
    };
    let salt = SaltString::generate(&mut OsRng);
    let password_hash = Argon2::default()
        .hash_password(input.password.as_bytes(), &salt)
        .map_err(|error| ApiError::internal(error.to_string()))?
        .to_string();
    let mut tx = state.db.begin().await?;
    sqlx::query("UPDATE users SET password_hash=$1 WHERE id=$2")
        .bind(password_hash)
        .bind(user_id)
        .execute(&mut *tx)
        .await?;
    sqlx::query("DELETE FROM sessions WHERE user_id=$1")
        .bind(user_id)
        .execute(&mut *tx)
        .await?;
    tx.commit().await?;
    Ok(StatusCode::NO_CONTENT)
}

pub async fn verify_email(
    State(state): State<AppState>,
    Json(input): Json<TokenInput>,
) -> ApiResult<StatusCode> {
    let now = unix_time();
    let user_id: Option<Uuid> = sqlx::query_scalar("UPDATE auth_tokens SET used_at=$1 WHERE token_hash=$2 AND purpose='verify_email' AND used_at IS NULL AND expires_at>$1 RETURNING user_id")
        .bind(now).bind(token_hash(&input.token)).fetch_optional(&state.db).await?;
    let Some(user_id) = user_id else {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "El enlace es inválido o venció.",
        ));
    };
    sqlx::query("UPDATE users SET email_verified=TRUE WHERE id=$1")
        .bind(user_id)
        .execute(&state.db)
        .await?;
    Ok(StatusCode::NO_CONTENT)
}

pub async fn resend_verification(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> ApiResult<StatusCode> {
    let user = require_user(&state, &headers).await?;
    if !user.email_verified {
        let token = new_action_token(&state, user.id, "verify_email", VERIFY_SECONDS).await?;
        let url = format!("{}/verify-email?token={}", state.config.public_url, token);
        mail::send_action(
            &state,
            &user.email,
            "Verifica tu cuenta PULSO",
            "Verifica tu email",
            &url,
        )
        .await?;
    }
    Ok(StatusCode::NO_CONTENT)
}

pub async fn me(State(state): State<AppState>, headers: HeaderMap) -> ApiResult<impl IntoResponse> {
    Ok(Json(
        json!({ "user": PublicUser::from(require_user(&state, &headers).await?) }),
    ))
}

pub async fn logout(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> ApiResult<impl IntoResponse> {
    if let Some(token) = cookie_value(&headers) {
        sqlx::query("DELETE FROM sessions WHERE token_hash=$1")
            .bind(token_hash(&token))
            .execute(&state.db)
            .await?;
    }
    Ok((
        StatusCode::NO_CONTENT,
        [(
            header::SET_COOKIE,
            clear_cookie(state.config.secure_cookies)?,
        )],
    ))
}
