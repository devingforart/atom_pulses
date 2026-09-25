mod auth;
mod billing;
mod config;
mod crypto;
mod error;
mod licenses;
mod mail;
mod models;
mod releases;
mod updates;

use std::{sync::Arc, time::Instant};

use axum::{
    Router,
    extract::DefaultBodyLimit,
    http::{HeaderValue, StatusCode, header},
    routing::{get, post},
};
use config::Config;
use error::{ApiError, ApiResult};
use releases::GitHubRelease;
use sqlx::{PgPool, postgres::PgPoolOptions};
use tokio::sync::RwLock;
use tower_http::{
    compression::CompressionLayer,
    services::{ServeDir, ServeFile},
    set_header::SetResponseHeaderLayer,
    trace::TraceLayer,
};
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};

#[derive(Clone)]
pub struct AppState {
    db: PgPool,
    http: reqwest::Client,
    config: Arc<Config>,
    release_cache: Arc<RwLock<Option<(Instant, GitHubRelease)>>>,
}

fn router(state: AppState) -> Router {
    let frontend = state.config.frontend_dir.clone();
    let index = frontend.join("index.html");
    let api = Router::new()
        .route("/health", get(|| async { StatusCode::NO_CONTENT }))
        .route("/auth/register", post(auth::register))
        .route("/auth/login", post(auth::login))
        .route("/auth/logout", post(auth::logout))
        .route("/auth/me", get(auth::me))
        .route("/auth/forgot-password", post(auth::request_password_reset))
        .route("/auth/reset-password", post(auth::reset_password))
        .route("/auth/verify-email", post(auth::verify_email))
        .route("/auth/resend-verification", post(auth::resend_verification))
        .route("/billing/checkout", post(billing::checkout))
        .route("/billing/plans", get(billing::plans))
        .route("/billing/portal", post(billing::portal))
        .route("/billing/webhook", post(billing::webhook))
        .route("/releases/latest", get(releases::latest_public))
        .route("/downloads/latest/windows", get(releases::download_windows))
        .route("/licenses/device-code", post(licenses::create_device_code))
        .route("/licenses/device-approve", post(licenses::approve_device))
        .route(
            "/licenses/device-token",
            post(licenses::exchange_device_token),
        )
        .route("/licenses/refresh", post(licenses::refresh))
        .route("/licenses/devices", get(licenses::list))
        .route(
            "/licenses/devices/{id}",
            axum::routing::delete(licenses::revoke),
        )
        .route("/updates/manifest", get(updates::manifest))
        .fallback(|| async { StatusCode::NOT_FOUND });
    Router::new()
        .nest("/api", api)
        .fallback_service(ServeDir::new(frontend).fallback(ServeFile::new(index)))
        .layer(SetResponseHeaderLayer::if_not_present(
            header::X_CONTENT_TYPE_OPTIONS,
            HeaderValue::from_static("nosniff"),
        ))
        .layer(SetResponseHeaderLayer::if_not_present(
            header::REFERRER_POLICY,
            HeaderValue::from_static("strict-origin-when-cross-origin"),
        ))
        .layer(SetResponseHeaderLayer::if_not_present(
            header::X_FRAME_OPTIONS,
            HeaderValue::from_static("DENY"),
        ))
        .layer(SetResponseHeaderLayer::if_not_present(
            header::CONTENT_SECURITY_POLICY,
            HeaderValue::from_static("default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'"),
        ))
        .layer(DefaultBodyLimit::max(1024 * 1024))
        .layer(CompressionLayer::new())
        .layer(TraceLayer::new_for_http())
        .with_state(state)
}

#[tokio::main]
async fn main() -> ApiResult<()> {
    tracing_subscriber::registry()
        .with(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "pulso_web_api=info,tower_http=info".into()),
        )
        .with(tracing_subscriber::fmt::layer())
        .init();
    let config = Arc::new(Config::from_env()?);
    let db = PgPoolOptions::new()
        .max_connections(10)
        .connect(&config.database_url)
        .await?;
    sqlx::migrate!()
        .run(&db)
        .await
        .map_err(|error| ApiError::internal(error.to_string()))?;
    sqlx::query("DELETE FROM sessions WHERE expires_at < EXTRACT(EPOCH FROM NOW())::BIGINT")
        .execute(&db)
        .await?;
    sqlx::query("DELETE FROM auth_tokens WHERE expires_at < EXTRACT(EPOCH FROM NOW())::BIGINT")
        .execute(&db)
        .await?;
    sqlx::query("DELETE FROM device_codes WHERE expires_at < EXTRACT(EPOCH FROM NOW())::BIGINT")
        .execute(&db)
        .await?;
    let state = AppState {
        db,
        http: reqwest::Client::builder()
            .redirect(reqwest::redirect::Policy::limited(5))
            .timeout(std::time::Duration::from_secs(30))
            .build()?,
        config: config.clone(),
        release_cache: Arc::new(RwLock::new(None)),
    };
    let listener = tokio::net::TcpListener::bind(config.bind)
        .await
        .map_err(|error| ApiError::internal(error.to_string()))?;
    tracing::info!(address = %config.bind, "PULSO web API ready");
    axum::serve(listener, router(state))
        .with_graceful_shutdown(shutdown())
        .await
        .map_err(|error| ApiError::internal(error.to_string()))
}

async fn shutdown() {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };
    #[cfg(unix)]
    let terminate = async {
        if let Ok(mut signal) =
            tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
        {
            signal.recv().await;
        }
    };
    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();
    tokio::select! { _ = ctrl_c => {}, _ = terminate => {} }
}
