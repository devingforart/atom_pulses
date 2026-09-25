use axum::{
    Json,
    extract::{Query, State},
};
use serde::{Deserialize, Serialize};

use crate::{
    AppState,
    crypto::sign_json,
    error::{ApiError, ApiResult},
    releases,
};

#[derive(Deserialize)]
pub struct UpdateQuery {
    channel: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct UpdatePayload {
    schema: u8,
    channel: String,
    version: String,
    published_at: Option<String>,
    download_page: String,
    size_bytes: u64,
    sha256: Option<String>,
    minimum_os: &'static str,
}

pub async fn manifest(
    State(state): State<AppState>,
    Query(query): Query<UpdateQuery>,
) -> ApiResult<Json<serde_json::Value>> {
    let channel = query.channel.unwrap_or_else(|| "stable".into());
    if channel != "stable" && channel != "beta" {
        return Err(ApiError::public(
            axum::http::StatusCode::BAD_REQUEST,
            "Canal de actualización inválido.",
        ));
    }
    let release = releases::latest(&state).await?;
    let asset = releases::windows_asset(&state, &release).ok_or_else(|| {
        ApiError::public(
            axum::http::StatusCode::NOT_FOUND,
            "No hay un instalador disponible.",
        )
    })?;
    let payload = UpdatePayload {
        schema: 1,
        channel,
        version: release.tag_name.trim_start_matches('v').into(),
        published_at: release.published_at.clone(),
        download_page: format!("{}/account", state.config.public_url),
        size_bytes: asset.size,
        sha256: asset
            .digest
            .clone()
            .and_then(|value| value.strip_prefix("sha256:").map(str::to_owned)),
        minimum_os: "Windows 10 22H2 x64",
    };
    let seed = state
        .config
        .license_signing_key
        .as_deref()
        .ok_or_else(|| ApiError::configuration("missing PULSO_LICENSE_SIGNING_KEY"))?;
    let signature = sign_json(seed, &payload)?;
    Ok(Json(
        serde_json::json!({"payload": payload, "signedEnvelope": signature}),
    ))
}
