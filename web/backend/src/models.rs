use serde::{Deserialize, Serialize};
use sqlx::FromRow;
use uuid::Uuid;

#[derive(Clone, Debug, FromRow)]
pub struct UserRecord {
    pub id: Uuid,
    pub email: String,
    pub display_name: String,
    pub password_hash: String,
    pub email_verified: bool,
}

#[derive(Clone, Debug, FromRow)]
pub struct AuthenticatedUser {
    pub id: Uuid,
    pub email: String,
    pub display_name: String,
    pub stripe_customer_id: Option<String>,
    pub subscription: String,
    pub email_verified: bool,
    pub studio_owned: bool,
    pub studio_updates_until: Option<i64>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PublicUser {
    pub id: Uuid,
    pub email: String,
    pub display_name: String,
    pub subscription: String,
    pub email_verified: bool,
    pub studio_owned: bool,
    pub studio_updates_until: Option<i64>,
}

impl From<AuthenticatedUser> for PublicUser {
    fn from(value: AuthenticatedUser) -> Self {
        Self {
            id: value.id,
            email: value.email,
            display_name: value.display_name,
            subscription: value.subscription,
            email_verified: value.email_verified,
            studio_owned: value.studio_owned,
            studio_updates_until: value.studio_updates_until,
        }
    }
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RegisterInput {
    pub email: String,
    pub password: String,
    pub display_name: String,
}

#[derive(Debug, Deserialize)]
pub struct LoginInput {
    pub email: String,
    pub password: String,
}

#[derive(Debug, Deserialize)]
pub struct CheckoutInput {
    pub plan: String,
    #[serde(default)]
    pub promotion_code: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct RedeemPromotionInput {
    pub code: String,
}
