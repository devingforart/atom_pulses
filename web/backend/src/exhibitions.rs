use axum::{
    Json,
    body::Body,
    extract::{Path, State},
    http::{HeaderMap, StatusCode, header},
    response::Response,
};
use serde_json::{Value, json};
use uuid::Uuid;

use crate::{
    AppState,
    auth::require_user,
    crypto,
    error::{ApiError, ApiResult},
};

fn unavailable() -> ApiError {
    ApiError::public(StatusCode::NOT_FOUND, "Exhibición no disponible.")
}

fn valid_token(token: &str) -> bool {
    token.len() == 43
        && token
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || c == b'_' || c == b'-')
}

fn valid_filename(filename: &str) -> bool {
    filename.len() <= 120
        && filename.ends_with(".mid")
        && !filename.contains("..")
        && filename
            .bytes()
            .all(|c| c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_' | b'.'))
}

// Public projection: never serialize a CloudJob, the prompt, logs or author identity.
fn public_manifest(manifest: &Value) -> ApiResult<Value> {
    let tracks: Vec<Value> = manifest["tracks"].as_array().ok_or_else(unavailable)?
        .iter().filter(|track| track["filename"].as_str().is_some_and(valid_filename))
        .map(|track| json!({"filename": track["filename"], "name": track["name"], "notes": track["notes"]}))
        .collect();
    if tracks.is_empty() {
        return Err(unavailable());
    }
    Ok(json!({"title": manifest["title"], "key": manifest["key"],
        "bpm": manifest["bpm"], "bars": manifest["bars"],
        "fullFile": "full-song.mid", "tracks": tracks}))
}

pub async fn create(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    let manifest: Option<Value> = sqlx::query_scalar(
        "SELECT result_manifest FROM cloud_jobs WHERE id=$1 AND user_id=$2 AND status='completed'",
    )
    .bind(id)
    .bind(user.id)
    .fetch_optional(&state.db)
    .await?
    .flatten();
    public_manifest(&manifest.ok_or_else(unavailable)?)?;
    let token = crypto::random_token(32)?;
    sqlx::query("INSERT INTO cloud_exhibitions(token_hash,job_id,created_at) VALUES($1,$2,$3)")
        .bind(crypto::hash(&token))
        .bind(id)
        .bind(crypto::unix_time())
        .execute(&state.db)
        .await?;
    Ok(Json(
        json!({"url": format!("{}/share/{}", state.config.public_url.trim_end_matches('/'), token)}),
    ))
}

pub async fn revoke(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> ApiResult<StatusCode> {
    let user = require_user(&state, &headers).await?;
    sqlx::query("DELETE FROM cloud_exhibitions WHERE job_id=$1 AND EXISTS(SELECT 1 FROM cloud_jobs WHERE id=$1 AND user_id=$2)")
        .bind(id).bind(user.id).execute(&state.db).await?;
    Ok(StatusCode::NO_CONTENT)
}

async fn exhibition(state: &AppState, token: &str) -> ApiResult<(Uuid, Value)> {
    if !valid_token(token) {
        return Err(unavailable());
    }
    sqlx::query_as::<_, (Uuid, Value)>("SELECT j.id,j.result_manifest FROM cloud_exhibitions e JOIN cloud_jobs j ON j.id=e.job_id WHERE e.token_hash=$1 AND j.status='completed' AND j.result_manifest IS NOT NULL")
        .bind(crypto::hash(token)).fetch_optional(&state.db).await?.ok_or_else(unavailable)
}

pub async fn get(State(state): State<AppState>, Path(token): Path<String>) -> ApiResult<Response> {
    let (id, manifest) = exhibition(&state, &token).await?;
    let body = json!({"id": id, "resultManifest": public_manifest(&manifest)?});
    Response::builder()
        .header(header::CONTENT_TYPE, "application/json")
        .header(header::CACHE_CONTROL, "no-store")
        .body(Body::from(body.to_string()))
        .map_err(|error| ApiError::internal(error.to_string()))
}

pub async fn track(
    State(state): State<AppState>,
    Path((token, filename)): Path<(String, String)>,
) -> ApiResult<Response> {
    if !valid_filename(&filename) {
        return Err(unavailable());
    }
    let (id, manifest) = exhibition(&state, &token).await?;
    let manifest = public_manifest(&manifest)?;
    if !manifest["tracks"]
        .as_array()
        .is_some_and(|tracks| tracks.iter().any(|track| track["filename"] == filename))
    {
        return Err(unavailable());
    }
    let file = state
        .config
        .cloud_output_dir
        .join(id.to_string())
        .join(filename);
    let bytes = tokio::fs::read(file).await.map_err(|_| unavailable())?;
    Response::builder()
        .header(header::CONTENT_TYPE, "audio/midi")
        .header(header::CACHE_CONTROL, "no-store")
        .body(Body::from(bytes))
        .map_err(|error| ApiError::internal(error.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn projection_excludes_private_data_and_path_traversal() {
        let manifest = json!({"title":"Title", "prompt":"private", "logs":"secret", "tracks":[
            {"filename":"track-01.mid", "name":"Voice", "notes":10, "private":"secret"},
            {"filename":"../job.json", "name":"bad"}]});
        let public = public_manifest(&manifest).unwrap();
        assert_eq!(public["tracks"].as_array().unwrap().len(), 1);
        assert!(public.get("prompt").is_none());
        assert!(public.get("logs").is_none());
        assert!(public["tracks"][0].get("private").is_none());
        assert!(!valid_filename("../track.mid"));
        assert!(!valid_token("guessable-id"));
        assert!(valid_token(&"a".repeat(43)));
    }
}
