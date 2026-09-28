use std::{path::Path, time::Duration};

use axum::{
    Json,
    body::Body,
    extract::{Path as UrlPath, State},
    http::{HeaderMap, StatusCode, header},
    response::Response,
};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use sqlx::FromRow;
use tokio::{process::Command, time::interval};
use uuid::Uuid;

use crate::{
    AppState,
    auth::require_user,
    crypto::unix_time,
    error::{ApiError, ApiResult},
};

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CreateJob {
    prompt: String,
    duration_seconds: i32,
    bpm: f64,
    behavior: String,
    idempotency_key: String,
}

#[derive(Debug, FromRow, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CloudJob {
    id: Uuid,
    prompt: String,
    duration_seconds: i32,
    bpm: f64,
    behavior: String,
    status: String,
    stage: String,
    completed_steps: i32,
    total_steps: i32,
    result_manifest: Option<Value>,
    error_code: Option<String>,
    created_at: i64,
    updated_at: i64,
}

fn valid_request(input: &CreateJob) -> bool {
    !input.prompt.trim().is_empty()
        && input.prompt.chars().count() <= 600
        && (30..=900).contains(&input.duration_seconds)
        && input.bpm.is_finite()
        && (60.0..=180.0).contains(&input.bpm)
        && matches!(
            input.behavior.as_str(),
            "adaptive" | "hypnotic" | "narrative"
        )
        && input.idempotency_key.len() >= 8
        && input.idempotency_key.len() <= 100
        && input
            .idempotency_key
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
}

fn available(state: &AppState, user: &crate::models::AuthenticatedUser) -> bool {
    state
        .config
        .cloud_worker_path
        .as_ref()
        .is_some_and(|path| path.is_file())
        && std::env::var("OPENAI_API_KEY").is_ok_and(|key| !key.trim().is_empty())
        && (matches!(user.subscription.as_str(), "active" | "trialing")
            || (state.config.cloud_beta_studio && user.studio_owned)
            || state
                .config
                .cloud_beta_emails
                .iter()
                .any(|email| email == &user.email.to_ascii_lowercase()))
}

pub async fn create(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(input): Json<CreateJob>,
) -> ApiResult<(StatusCode, Json<CloudJob>)> {
    let user = require_user(&state, &headers).await?;
    if !user.email_verified || !available(&state, &user) {
        return Err(ApiError::public(
            StatusCode::FORBIDDEN,
            "PULSO Cloud no está activo para esta cuenta.",
        ));
    }
    if !valid_request(&input) {
        return Err(ApiError::public(
            StatusCode::BAD_REQUEST,
            "Revisa el prompt y los parámetros de la obra.",
        ));
    }
    let mut tx = state.db.begin().await?;
    // Serialize quota checks per account, even when requests reach different API replicas.
    sqlx::query("SELECT pg_advisory_xact_lock(hashtext($1::text))")
        .bind(user.id.to_string())
        .execute(&mut *tx)
        .await?;
    if let Some(existing) = sqlx::query_as::<_, CloudJob>(
        "SELECT id,prompt,duration_seconds,bpm,behavior,status,stage,completed_steps,total_steps,result_manifest,error_code,created_at,updated_at FROM cloud_jobs WHERE user_id=$1 AND idempotency_key=$2")
        .bind(user.id).bind(&input.idempotency_key).fetch_optional(&mut *tx).await? {
        if existing.prompt != input.prompt.trim()
            || existing.duration_seconds != input.duration_seconds
            || existing.bpm != input.bpm
            || existing.behavior != input.behavior {
            return Err(ApiError::public(StatusCode::CONFLICT,
                "Esta clave de solicitud ya pertenece a otra composición."));
        }
        tx.commit().await?;
        return Ok((StatusCode::OK, Json(existing)));
    }
    let daily: i64 =
        sqlx::query_scalar("SELECT COUNT(*) FROM cloud_jobs WHERE user_id=$1 AND created_at >= $2")
            .bind(user.id)
            .bind(unix_time() - 86_400)
            .fetch_one(&mut *tx)
            .await?;
    let active: i64 = sqlx::query_scalar(
        "SELECT COUNT(*) FROM cloud_jobs WHERE user_id=$1 AND status IN ('queued','running')",
    )
    .bind(user.id)
    .fetch_one(&mut *tx)
    .await?;
    if daily >= state.config.cloud_daily_jobs || active > 0 {
        return Err(ApiError::public(
            StatusCode::TOO_MANY_REQUESTS,
            "Alcanzaste el límite de obras o ya tienes una composición en curso.",
        ));
    }
    let mut seed_bytes = [0_u8; 8];
    getrandom::fill(&mut seed_bytes).map_err(|error| ApiError::internal(error.to_string()))?;
    let seed = (i64::from_le_bytes(seed_bytes) & i64::MAX).max(1);
    let id = Uuid::new_v4();
    let now = unix_time();
    sqlx::query("INSERT INTO cloud_jobs(id,user_id,idempotency_key,prompt,duration_seconds,bpm,behavior,seed,status,stage,created_at,updated_at) VALUES($1,$2,$3,$4,$5,$6,$7,$8,'queued','queued',$9,$9)")
        .bind(id).bind(user.id).bind(&input.idempotency_key).bind(input.prompt.trim())
        .bind(input.duration_seconds).bind(input.bpm).bind(&input.behavior).bind(seed)
        .bind(now).execute(&mut *tx).await?;
    tx.commit().await?;
    Ok((
        StatusCode::CREATED,
        Json(CloudJob {
            id,
            prompt: input.prompt.trim().to_owned(),
            duration_seconds: input.duration_seconds,
            bpm: input.bpm,
            behavior: input.behavior,
            status: "queued".into(),
            stage: "queued".into(),
            completed_steps: 0,
            total_steps: 0,
            result_manifest: None,
            error_code: None,
            created_at: now,
            updated_at: now,
        }),
    ))
}

pub async fn list(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> ApiResult<Json<Vec<CloudJob>>> {
    let user = require_user(&state, &headers).await?;
    let jobs = sqlx::query_as::<_, CloudJob>(
        "SELECT id,prompt,duration_seconds,bpm,behavior,status,stage,completed_steps,total_steps,result_manifest,error_code,created_at,updated_at FROM cloud_jobs WHERE user_id=$1 ORDER BY created_at DESC LIMIT 30")
        .bind(user.id).fetch_all(&state.db).await?;
    Ok(Json(jobs))
}

pub async fn status(State(state): State<AppState>, headers: HeaderMap) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    Ok(Json(serde_json::json!({
        "available": user.email_verified && available(&state, &user),
        "dailyJobLimit": state.config.cloud_daily_jobs,
    })))
}

pub async fn get(
    State(state): State<AppState>,
    headers: HeaderMap,
    UrlPath(id): UrlPath<Uuid>,
) -> ApiResult<Json<CloudJob>> {
    let user = require_user(&state, &headers).await?;
    let job = sqlx::query_as::<_, CloudJob>(
        "SELECT id,prompt,duration_seconds,bpm,behavior,status,stage,completed_steps,total_steps,result_manifest,error_code,created_at,updated_at FROM cloud_jobs WHERE user_id=$1 AND id=$2")
        .bind(user.id).bind(id).fetch_optional(&state.db).await?
        .ok_or_else(|| ApiError::public(StatusCode::NOT_FOUND, "Obra no encontrada."))?;
    Ok(Json(job))
}

pub async fn cancel(
    State(state): State<AppState>,
    headers: HeaderMap,
    UrlPath(id): UrlPath<Uuid>,
) -> ApiResult<StatusCode> {
    let user = require_user(&state, &headers).await?;
    let updated = sqlx::query("UPDATE cloud_jobs SET cancel_requested=TRUE,status=CASE WHEN status='queued' THEN 'cancelled' ELSE status END,updated_at=$3 WHERE id=$1 AND user_id=$2 AND status IN ('queued','running')")
        .bind(id).bind(user.id).bind(unix_time()).execute(&state.db).await?;
    if updated.rows_affected() == 0 {
        return Err(ApiError::public(
            StatusCode::CONFLICT,
            "Esta obra ya terminó o no existe.",
        ));
    }
    Ok(StatusCode::NO_CONTENT)
}

pub async fn track(
    State(state): State<AppState>,
    headers: HeaderMap,
    UrlPath((id, filename)): UrlPath<(Uuid, String)>,
) -> ApiResult<Response> {
    let user = require_user(&state, &headers).await?;
    let manifest: Option<Value> = sqlx::query_scalar(
        "SELECT result_manifest FROM cloud_jobs WHERE id=$1 AND user_id=$2 AND status='completed'",
    )
    .bind(id)
    .bind(user.id)
    .fetch_optional(&state.db)
    .await?
    .flatten();
    let manifest =
        manifest.ok_or_else(|| ApiError::public(StatusCode::NOT_FOUND, "MIDI no disponible."))?;
    let permitted = manifest["tracks"].as_array().is_some_and(|tracks| {
        tracks
            .iter()
            .any(|track| track["filename"].as_str() == Some(filename.as_str()))
    }) || manifest["fullFile"].as_str() == Some(filename.as_str());
    if !permitted || filename.contains('/') || filename.contains('\\') || filename.contains("..") {
        return Err(ApiError::public(
            StatusCode::NOT_FOUND,
            "MIDI no disponible.",
        ));
    }
    let file = state
        .config
        .cloud_output_dir
        .join(id.to_string())
        .join(&filename);
    let bytes = tokio::fs::read(file)
        .await
        .map_err(|_| ApiError::public(StatusCode::NOT_FOUND, "MIDI no disponible."))?;
    Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_TYPE, "audio/midi")
        .header(
            header::CONTENT_DISPOSITION,
            format!("attachment; filename=\"{filename}\""),
        )
        .body(Body::from(bytes))
        .map_err(|error| ApiError::internal(error.to_string()))
}

#[derive(FromRow)]
struct ClaimedJob {
    id: Uuid,
    prompt: String,
    duration_seconds: i32,
    bpm: f64,
    behavior: String,
    seed: i64,
}

async fn claim(state: &AppState) -> ApiResult<Option<ClaimedJob>> {
    let mut tx = state.db.begin().await?;
    let job = sqlx::query_as::<_, ClaimedJob>(
        "SELECT id,prompt,duration_seconds,bpm,behavior,seed FROM cloud_jobs WHERE status='queued' ORDER BY created_at LIMIT 1 FOR UPDATE SKIP LOCKED")
        .fetch_optional(&mut *tx).await?;
    if let Some(job) = &job {
        sqlx::query("UPDATE cloud_jobs SET status='running',stage='starting',attempt=attempt+1,lease_until=$2,updated_at=$3 WHERE id=$1")
            .bind(job.id).bind(unix_time()+120).bind(unix_time()).execute(&mut *tx).await?;
    }
    tx.commit().await?;
    Ok(job)
}

async fn run_one(state: &AppState, job: ClaimedJob) -> ApiResult<()> {
    let directory = state.config.cloud_output_dir.join(job.id.to_string());
    tokio::fs::create_dir_all(&directory).await?;
    let input = directory.join("job.json");
    let body = serde_json::json!({
        "prompt": job.prompt, "duration_seconds": job.duration_seconds,
        "bpm": job.bpm, "behavior": job.behavior, "seed": job.seed.to_string(),
    });
    tokio::fs::write(
        &input,
        serde_json::to_vec(&body).map_err(|e| ApiError::internal(e.to_string()))?,
    )
    .await?;
    let worker = state
        .config
        .cloud_worker_path
        .as_ref()
        .ok_or_else(|| ApiError::configuration("missing PULSO_CLOUD_WORKER_PATH"))?;
    // The composer needs the OpenAI key, but must not inherit the API server's
    // Stripe, database, mail or signing credentials.
    let openai_key = std::env::var("OPENAI_API_KEY")
        .map_err(|_| ApiError::configuration("missing OPENAI_API_KEY"))?;
    let mut command = Command::new(worker);
    command
        .env_clear()
        .env("OPENAI_API_KEY", openai_key)
        .env("HOME", &state.config.cloud_output_dir)
        .env("XDG_CONFIG_HOME", &state.config.cloud_output_dir)
        .arg(&input)
        .arg(&directory)
        .kill_on_drop(true);
    let mut child = command
        .spawn()
        .map_err(|e| ApiError::internal(e.to_string()))?;
    let mut heartbeat = interval(Duration::from_secs(5));
    let deadline = tokio::time::Instant::now() + Duration::from_secs(45 * 60);
    loop {
        heartbeat.tick().await;
        let cancelled: bool =
            sqlx::query_scalar("SELECT cancel_requested FROM cloud_jobs WHERE id=$1")
                .bind(job.id)
                .fetch_one(&state.db)
                .await?;
        if cancelled || tokio::time::Instant::now() > deadline {
            let _ = child.kill().await;
            sqlx::query("UPDATE cloud_jobs SET status=$2,stage=$2,error_code=$3,lease_until=NULL,updated_at=$4 WHERE id=$1")
                .bind(job.id).bind(if cancelled { "cancelled" } else { "failed" })
                .bind(if cancelled { None } else { Some("time_budget") })
                .bind(unix_time()).execute(&state.db).await?;
            return Ok(());
        }
        if let Some(status) = child
            .try_wait()
            .map_err(|e| ApiError::internal(e.to_string()))?
        {
            let manifest_path = directory.join("manifest.json");
            let manifest = if status.success() {
                read_json(&manifest_path).await
            } else {
                None
            };
            let cancelled: bool =
                sqlx::query_scalar("SELECT cancel_requested FROM cloud_jobs WHERE id=$1")
                    .bind(job.id)
                    .fetch_one(&state.db)
                    .await?;
            if cancelled {
                sqlx::query("UPDATE cloud_jobs SET status='cancelled',stage='cancelled',lease_until=NULL,updated_at=$2 WHERE id=$1")
                    .bind(job.id).bind(unix_time()).execute(&state.db).await?;
                return Ok(());
            }
            let error_code: Option<&str> = if manifest.is_some() {
                None
            } else if status.success() {
                Some("missing_result")
            } else {
                Some("worker_failed")
            };
            sqlx::query("UPDATE cloud_jobs SET status=$2,stage=$3,result_manifest=$4,error_code=$5,lease_until=NULL,updated_at=$6 WHERE id=$1")
                .bind(job.id).bind(if manifest.is_some() { "completed" } else { "failed" })
                .bind(if manifest.is_some() { "ready" } else { "generation" })
                .bind(manifest).bind(error_code)
                .bind(unix_time()).execute(&state.db).await?;
            return Ok(());
        }
        let progress = read_json(&directory.join("progress.json")).await;
        let stage = progress
            .as_ref()
            .and_then(|value| value["stage"].as_str())
            .unwrap_or("working");
        let completed = progress
            .as_ref()
            .and_then(|value| value["completed"].as_i64())
            .unwrap_or(0);
        let total = progress
            .as_ref()
            .and_then(|value| value["total"].as_i64())
            .unwrap_or(0);
        sqlx::query("UPDATE cloud_jobs SET stage=$2,completed_steps=$3,total_steps=$4,lease_until=$5,updated_at=$6 WHERE id=$1 AND status='running'")
            .bind(job.id).bind(stage).bind(completed as i32).bind(total as i32)
            .bind(unix_time()+120).bind(unix_time()).execute(&state.db).await?;
    }
}

async fn read_json(path: &Path) -> Option<Value> {
    let bytes = tokio::fs::read(path).await.ok()?;
    serde_json::from_slice(&bytes).ok()
}

async fn recover_expired(state: &AppState) -> ApiResult<()> {
    let now = unix_time();
    let expired: Vec<Uuid> = sqlx::query_scalar(
        "SELECT id FROM cloud_jobs WHERE status='running' AND lease_until<$1 LIMIT 50",
    )
    .bind(now)
    .fetch_all(&state.db)
    .await?;
    for id in expired {
        let manifest = read_json(
            &state
                .config
                .cloud_output_dir
                .join(id.to_string())
                .join("manifest.json"),
        )
        .await
        .filter(|value| {
            value["tracks"]
                .as_array()
                .is_some_and(|tracks| !tracks.is_empty())
        });
        let completed = manifest.is_some();
        sqlx::query("UPDATE cloud_jobs SET status=$2,stage=$3,result_manifest=$4,error_code=$5,lease_until=NULL,updated_at=$6 WHERE id=$1 AND status='running' AND lease_until<$6")
            .bind(id).bind(if completed { "completed" } else { "failed" })
            .bind(if completed { "ready" } else { "interrupted" })
            .bind(manifest).bind(if completed { None } else { Some("worker_interrupted") })
            .bind(now).execute(&state.db).await?;
    }
    Ok(())
}

pub async fn worker_loop(state: AppState) {
    // Never silently re-bill an interrupted AI request after a process restart.
    if let Err(error) = recover_expired(&state).await {
        tracing::error!(%error, "cloud startup recovery failed");
    }
    let mut timer = interval(Duration::from_secs(3));
    loop {
        timer.tick().await;
        if let Err(error) = recover_expired(&state).await {
            tracing::error!(%error, "cloud lease recovery failed");
        }
        match claim(&state).await {
            Ok(Some(job)) => {
                let id = job.id;
                if let Err(error) = run_one(&state, job).await {
                    tracing::error!(%error, %id, "cloud job failed");
                    let _ = sqlx::query("UPDATE cloud_jobs SET status='failed',stage='internal',error_code='internal',lease_until=NULL,updated_at=$2 WHERE id=$1 AND status='running'")
                        .bind(id).bind(unix_time()).execute(&state.db).await;
                }
            }
            Ok(None) => {}
            Err(error) => tracing::error!(%error, "cloud job claim failed"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn input_boundaries_and_idempotency_key() {
        let valid = CreateJob {
            prompt: "Una obra con desarrollo".into(),
            duration_seconds: 390,
            bpm: 120.0,
            behavior: "hypnotic".into(),
            idempotency_key: Uuid::new_v4().to_string(),
        };
        assert!(valid_request(&valid));
        assert!(!valid_request(&CreateJob {
            bpm: f64::NAN,
            ..valid
        }));
    }
}
