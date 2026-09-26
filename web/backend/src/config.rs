use std::{env, net::SocketAddr, path::PathBuf};

use crate::error::{ApiError, ApiResult};

#[derive(Clone, Debug)]
pub struct Config {
    pub bind: SocketAddr,
    pub database_url: String,
    pub public_url: String,
    pub frontend_dir: PathBuf,
    pub secure_cookies: bool,
    pub stripe_secret_key: String,
    pub stripe_webhook_secret: String,
    pub stripe_monthly_price_id: Option<String>,
    pub stripe_annual_price_id: Option<String>,
    pub stripe_studio_price_id: String,
    pub southatoms_promo_code: String,
    pub promotion_updates_days: i64,
    pub github_owner: String,
    pub github_repo: String,
    pub github_token: Option<String>,
    pub github_asset_pattern: String,
    pub license_signing_key: Option<String>,
    pub mail_api_key: Option<String>,
    pub mail_from: String,
}

impl Config {
    pub fn from_env() -> ApiResult<Self> {
        let required = |name: &str| {
            env::var(name).map_err(|_| ApiError::configuration(format!("missing {name}")))
        };
        Ok(Self {
            bind: env::var("PULSO_WEB_BIND")
                .unwrap_or_else(|_| "0.0.0.0:8080".into())
                .parse()
                .map_err(|_| ApiError::configuration("invalid PULSO_WEB_BIND"))?,
            database_url: required("DATABASE_URL")?,
            public_url: env::var("PUBLIC_URL")
                .unwrap_or_else(|_| "http://localhost:8080".into())
                .trim_end_matches('/')
                .into(),
            frontend_dir: env::var("FRONTEND_DIR")
                .unwrap_or_else(|_| "../frontend/dist".into())
                .into(),
            secure_cookies: env::var("SECURE_COOKIES")
                .map(|v| v != "false")
                .unwrap_or(true),
            stripe_secret_key: required("STRIPE_SECRET_KEY")?,
            stripe_webhook_secret: required("STRIPE_WEBHOOK_SECRET")?,
            stripe_monthly_price_id: env::var("STRIPE_MONTHLY_PRICE_ID")
                .ok()
                .filter(|value| !value.is_empty()),
            stripe_annual_price_id: env::var("STRIPE_ANNUAL_PRICE_ID")
                .ok()
                .filter(|value| !value.is_empty()),
            stripe_studio_price_id: required("STRIPE_STUDIO_PRICE_ID")?,
            // It is intentionally overridable in the production environment so a
            // campaign can be rotated without a software release. The default is
            // the launch code requested for SouthAtoms collaborators.
            southatoms_promo_code: env::var("PULSO_SOUTHATOMS_PROMO_CODE")
                .unwrap_or_else(|_| "SouthAtoms".into()),
            promotion_updates_days: env::var("PULSO_PROMOTION_UPDATES_DAYS")
                .ok()
                .and_then(|value| value.parse::<i64>().ok())
                .filter(|value| *value > 0 && *value <= 3650)
                .unwrap_or(365),
            github_owner: env::var("GITHUB_OWNER").unwrap_or_else(|_| "devingforart".into()),
            github_repo: env::var("GITHUB_REPO").unwrap_or_else(|_| "atom_pulses".into()),
            github_token: env::var("GITHUB_TOKEN").ok().filter(|v| !v.is_empty()),
            github_asset_pattern: env::var("GITHUB_ASSET_PATTERN")
                .unwrap_or_else(|_| "windows-x64.zip".into()),
            license_signing_key: env::var("PULSO_LICENSE_SIGNING_KEY")
                .ok()
                .filter(|value| !value.is_empty()),
            mail_api_key: env::var("RESEND_API_KEY")
                .ok()
                .filter(|value| !value.is_empty()),
            mail_from: env::var("MAIL_FROM")
                .unwrap_or_else(|_| "PULSO <noreply@pulso.music>".into()),
        })
    }
}
