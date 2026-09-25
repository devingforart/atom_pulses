use axum::{
    Json,
    http::StatusCode,
    response::{IntoResponse, Response},
};
use serde_json::json;

pub type ApiResult<T> = Result<T, ApiError>;

#[derive(Debug, thiserror::Error)]
pub enum ApiError {
    #[error("{message}")]
    Public { status: StatusCode, message: String },
    #[error(transparent)]
    Database(#[from] sqlx::Error),
    #[error(transparent)]
    Upstream(#[from] reqwest::Error),
    #[error("configuration: {0}")]
    Configuration(String),
    #[error("internal error: {0}")]
    Internal(String),
}

impl ApiError {
    pub fn public(status: StatusCode, message: impl Into<String>) -> Self {
        Self::Public {
            status,
            message: message.into(),
        }
    }
    pub fn configuration(message: impl Into<String>) -> Self {
        Self::Configuration(message.into())
    }
    pub fn internal(message: impl Into<String>) -> Self {
        Self::Internal(message.into())
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, message) = match self {
            Self::Public { status, message } => (status, message),
            Self::Configuration(message) => {
                tracing::error!(%message, "configuration error");
                (
                    StatusCode::SERVICE_UNAVAILABLE,
                    "El servicio todavía no está configurado.".into(),
                )
            }
            problem => {
                tracing::error!(error = %problem, "request failed");
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    "No pudimos completar la solicitud.".into(),
                )
            }
        };
        (status, Json(json!({ "error": message }))).into_response()
    }
}
