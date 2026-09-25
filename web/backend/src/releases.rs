use std::time::{Duration, Instant};

use axum::{
    Json,
    body::Body,
    extract::State,
    http::{HeaderMap, HeaderValue, StatusCode, header},
    response::Response,
};
use serde::{Deserialize, Serialize};

use crate::{
    AppState,
    auth::{optional_user, require_user},
    error::{ApiError, ApiResult},
    models::AuthenticatedUser,
};

#[derive(Clone, Debug, Deserialize)]
pub struct GitHubAsset {
    pub name: String,
    pub url: String,
    pub size: u64,
    pub digest: Option<String>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct GitHubRelease {
    pub tag_name: String,
    pub name: Option<String>,
    pub published_at: Option<String>,
    pub assets: Vec<GitHubAsset>,
    #[serde(default)]
    pub draft: bool,
    #[serde(default)]
    pub prerelease: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PublicRelease {
    version: String,
    name: String,
    published_at: Option<String>,
    size_bytes: Option<u64>,
    available: bool,
}

fn github_request(state: &AppState, url: String) -> reqwest::RequestBuilder {
    let request = state
        .http
        .get(url)
        .header("Accept", "application/vnd.github+json")
        .header("X-GitHub-Api-Version", "2026-03-10")
        .header("User-Agent", "pulso-web-api");
    if let Some(token) = &state.config.github_token {
        request.bearer_auth(token)
    } else {
        request
    }
}

pub(crate) async fn latest(state: &AppState) -> ApiResult<GitHubRelease> {
    {
        let cache = state.release_cache.read().await;
        if let Some((instant, release)) = &*cache
            && instant.elapsed() < Duration::from_secs(60)
        {
            return Ok(release.clone());
        }
    }
    let url = format!(
        "https://api.github.com/repos/{}/{}/releases/latest",
        state.config.github_owner, state.config.github_repo
    );
    let response = github_request(state, url).send().await?;
    if response.status() == reqwest::StatusCode::NOT_FOUND {
        return Err(ApiError::public(
            StatusCode::NOT_FOUND,
            "Todavía no hay una versión publicada.",
        ));
    }
    if !response.status().is_success() {
        tracing::error!(status = %response.status(), "GitHub release lookup failed");
        return Err(ApiError::public(
            StatusCode::BAD_GATEWAY,
            "No pudimos consultar la última versión.",
        ));
    }
    let release: GitHubRelease = response.json().await?;
    *state.release_cache.write().await = Some((Instant::now(), release.clone()));
    Ok(release)
}

fn covered_by_updates(release: &GitHubRelease, updates_until: i64) -> bool {
    release
        .published_at
        .as_deref()
        .and_then(|published| {
            time::OffsetDateTime::parse(published, &time::format_description::well_known::Rfc3339)
                .ok()
        })
        .is_some_and(|published| published.unix_timestamp() <= updates_until)
}

async fn release_for_user(state: &AppState, user: &AuthenticatedUser) -> ApiResult<GitHubRelease> {
    let current = latest(state).await?;
    if matches!(user.subscription.as_str(), "active" | "trialing")
        || user.studio_updates_until.is_none()
        || user
            .studio_updates_until
            .is_some_and(|until| covered_by_updates(&current, until))
    {
        return Ok(current);
    }
    let updates_until = user.studio_updates_until.unwrap_or_default();
    let url = format!(
        "https://api.github.com/repos/{}/{}/releases?per_page=100",
        state.config.github_owner, state.config.github_repo
    );
    let response = github_request(state, url).send().await?;
    if !response.status().is_success() {
        tracing::error!(status = %response.status(), "GitHub release history lookup failed");
        return Err(ApiError::public(
            StatusCode::BAD_GATEWAY,
            "No pudimos consultar las versiones incluidas en tu licencia.",
        ));
    }
    let releases: Vec<GitHubRelease> = response.json().await?;
    releases
        .into_iter()
        .find(|release| {
            !release.draft
                && !release.prerelease
                && covered_by_updates(release, updates_until)
                && windows_asset(state, release).is_some()
        })
        .ok_or_else(|| {
            ApiError::public(
                StatusCode::NOT_FOUND,
                "No encontramos una versión cubierta por tu licencia. Contacta a soporte.",
            )
        })
}

pub(crate) fn windows_asset<'a>(
    state: &AppState,
    release: &'a GitHubRelease,
) -> Option<&'a GitHubAsset> {
    let pattern = state.config.github_asset_pattern.to_lowercase();
    release
        .assets
        .iter()
        .find(|asset| asset.name.to_lowercase().ends_with(&pattern))
}

pub async fn latest_public(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> ApiResult<Json<PublicRelease>> {
    let release = match optional_user(&state, &headers).await? {
        Some(user) if user.studio_owned => release_for_user(&state, &user).await?,
        _ => latest(&state).await?,
    };
    let asset = windows_asset(&state, &release);
    Ok(Json(PublicRelease {
        version: release.tag_name.trim_start_matches('v').to_string(),
        name: release
            .name
            .clone()
            .unwrap_or_else(|| format!("PULSO {}", release.tag_name)),
        published_at: release.published_at.clone(),
        size_bytes: asset.map(|item| item.size),
        available: asset.is_some(),
    }))
}

pub async fn download_windows(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> ApiResult<Response> {
    let user = require_user(&state, &headers).await?;
    if !user.studio_owned && !matches!(user.subscription.as_str(), "active" | "trialing") {
        return Err(ApiError::public(
            StatusCode::PAYMENT_REQUIRED,
            "Necesitas una licencia Studio o una suscripción Cloud activa para descargar PULSO.",
        ));
    }
    let release = release_for_user(&state, &user).await?;
    let asset = windows_asset(&state, &release).ok_or_else(|| {
        ApiError::public(
            StatusCode::NOT_FOUND,
            "La versión actual no contiene el instalador de Windows.",
        )
    })?;
    let response = github_request(&state, asset.url.clone())
        .header("Accept", "application/octet-stream")
        .send()
        .await?;
    if !response.status().is_success() {
        tracing::error!(status = %response.status(), asset = %asset.name, "GitHub asset download failed");
        return Err(ApiError::public(
            StatusCode::BAD_GATEWAY,
            "No pudimos abrir el instalador.",
        ));
    }
    let safe_name = asset.name.replace(['\r', '\n', '"'], "");
    let mut result = Response::new(Body::from_stream(response.bytes_stream()));
    result.headers_mut().insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static("application/octet-stream"),
    );
    result.headers_mut().insert(
        header::CONTENT_DISPOSITION,
        HeaderValue::from_str(&format!("attachment; filename=\"{safe_name}\""))
            .map_err(|error| ApiError::internal(error.to_string()))?,
    );
    result.headers_mut().insert(
        header::CACHE_CONTROL,
        HeaderValue::from_static("private, no-store"),
    );
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn release(published_at: Option<&str>) -> GitHubRelease {
        GitHubRelease {
            tag_name: "v1.0.0".into(),
            name: None,
            published_at: published_at.map(str::to_owned),
            assets: Vec::new(),
            draft: false,
            prerelease: false,
        }
    }

    #[test]
    fn update_window_includes_only_releases_published_before_expiry() {
        let expiry = time::OffsetDateTime::parse(
            "2026-09-25T12:00:00Z",
            &time::format_description::well_known::Rfc3339,
        )
        .unwrap()
        .unix_timestamp();
        assert!(covered_by_updates(
            &release(Some("2026-09-25T11:59:59Z")),
            expiry
        ));
        assert!(!covered_by_updates(
            &release(Some("2026-09-25T12:00:01Z")),
            expiry
        ));
        assert!(!covered_by_updates(&release(None), expiry));
    }
}
