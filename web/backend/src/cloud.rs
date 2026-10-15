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

const CLOUD_BEHAVIOR: &str = "adaptive";
const CLOUD_AI_SOVEREIGN: bool = true;
const _: () = assert!(CLOUD_AI_SOVEREIGN);
// The local AI-only editorial worker can scale its own budget to 180 minutes
// for an unusually large cast. The API must not kill that same worker at 45
// minutes just because it is running in Cloud.
const CLOUD_WORKER_DEADLINE: Duration = Duration::from_secs(185 * 60);
const MAX_CHECKPOINT_RECOVERIES: u8 = 3;
const RECOVERY_CALL_SECONDS: u64 = 600;
const RECOVERY_CALL_USD: u64 = 20;

#[derive(Debug, Deserialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct CreateJob {
    prompt: String,
    duration_seconds: i32,
    bpm: f64,
    idempotency_key: String,
    seed: Option<String>,
}

fn composer_profile() -> Value {
    serde_json::from_str(include_str!("../../shared/composer-profile.json"))
        .expect("the shared composer profile must be valid JSON")
}

#[derive(Debug, FromRow, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CloudJob {
    id: Uuid,
    prompt: String,
    duration_seconds: i32,
    bpm: f64,
    behavior: String,
    ai_sovereign: bool,
    seed: String,
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
        && input.idempotency_key.len() >= 8
        && input.idempotency_key.len() <= 100
        && input
            .idempotency_key
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
        && input.seed.as_ref().is_none_or(|seed| {
            seed.parse::<i64>().is_ok_and(|value| value > 0)
                && !seed.starts_with('+')
                && !seed.starts_with('0')
        })
}

fn available(state: &AppState, user: &crate::models::AuthenticatedUser) -> bool {
    state
        .config
        .cloud_worker_path
        .as_ref()
        .is_some_and(|path| path.is_file())
        && std::env::var("OPENAI_API_KEY").is_ok_and(|key| !key.trim().is_empty())
        && license_allows_cloud(user, &state.config.cloud_beta_emails)
}

fn license_allows_cloud(user: &crate::models::AuthenticatedUser, beta_emails: &[String]) -> bool {
    user.studio_owned
        || matches!(user.subscription.as_str(), "active" | "trialing")
        || beta_emails
            .iter()
            .any(|email| email.eq_ignore_ascii_case(&user.email))
}

fn unlimited_for(email: &str, allowlist: &[String]) -> bool {
    allowlist
        .iter()
        .any(|allowed| allowed.eq_ignore_ascii_case(email.trim()))
}

fn quota_problem(
    completed: i64,
    active: i64,
    completed_limit: i64,
    unlimited: bool,
) -> Option<&'static str> {
    if active > 0 {
        Some("Ya tienes una composición en curso. Espera a que termine o cancélala.")
    } else if !unlimited && completed >= completed_limit {
        Some("Alcanzaste el cupo de obras completadas de las últimas 24 horas.")
    } else {
        None
    }
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
        "SELECT id,prompt,duration_seconds,bpm,behavior,ai_sovereign,seed::text AS seed,status,stage,completed_steps,total_steps,result_manifest,error_code,created_at,updated_at FROM cloud_jobs WHERE user_id=$1 AND idempotency_key=$2")
        .bind(user.id).bind(&input.idempotency_key).fetch_optional(&mut *tx).await? {
        if existing.prompt != input.prompt.trim()
            || existing.duration_seconds != input.duration_seconds
            || existing.bpm != input.bpm
            || input.seed.as_ref().is_some_and(|seed| seed != &existing.seed) {
            return Err(ApiError::public(StatusCode::CONFLICT,
                "Esta clave de solicitud ya pertenece a otra composición."));
        }
        tx.commit().await?;
        return Ok((StatusCode::OK, Json(existing)));
    }
    let daily_completed: i64 =
        sqlx::query_scalar("SELECT COUNT(*) FROM cloud_jobs WHERE user_id=$1 AND updated_at >= $2 AND status='completed'")
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
    if let Some(problem) = quota_problem(
        daily_completed,
        active,
        state.config.cloud_daily_jobs,
        unlimited_for(&user.email, &state.config.cloud_unlimited_emails),
    ) {
        return Err(ApiError::public(StatusCode::TOO_MANY_REQUESTS, problem));
    }
    let mut seed_bytes = [0_u8; 8];
    getrandom::fill(&mut seed_bytes).map_err(|error| ApiError::internal(error.to_string()))?;
    let seed = input
        .seed
        .as_ref()
        .map(|seed| seed.parse::<i64>().expect("validated seed"))
        .unwrap_or_else(|| (i64::from_le_bytes(seed_bytes) & i64::MAX).max(1));
    let id = Uuid::new_v4();
    let now = unix_time();
    sqlx::query("INSERT INTO cloud_jobs(id,user_id,idempotency_key,prompt,duration_seconds,bpm,behavior,seed,ai_sovereign,status,stage,created_at,updated_at) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,'queued','queued',$10,$10)")
        .bind(id).bind(user.id).bind(&input.idempotency_key).bind(input.prompt.trim())
        .bind(input.duration_seconds).bind(input.bpm).bind(CLOUD_BEHAVIOR).bind(seed).bind(CLOUD_AI_SOVEREIGN)
        .bind(now).execute(&mut *tx).await?;
    tx.commit().await?;
    Ok((
        StatusCode::CREATED,
        Json(CloudJob {
            id,
            prompt: input.prompt.trim().to_owned(),
            duration_seconds: input.duration_seconds,
            bpm: input.bpm,
            behavior: CLOUD_BEHAVIOR.into(),
            ai_sovereign: CLOUD_AI_SOVEREIGN,
            seed: seed.to_string(),
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
        "SELECT id,prompt,duration_seconds,bpm,behavior,ai_sovereign,seed::text AS seed,status,stage,completed_steps,total_steps,result_manifest,error_code,created_at,updated_at FROM cloud_jobs WHERE user_id=$1 ORDER BY created_at DESC LIMIT 30")
        .bind(user.id).fetch_all(&state.db).await?;
    Ok(Json(jobs))
}

pub async fn status(State(state): State<AppState>, headers: HeaderMap) -> ApiResult<Json<Value>> {
    let user = require_user(&state, &headers).await?;
    Ok(Json(serde_json::json!({
        "available": user.email_verified && available(&state, &user),
        "dailyJobLimit": state.config.cloud_daily_jobs,
        "unlimited": unlimited_for(&user.email, &state.config.cloud_unlimited_emails),
        "composerProfile": composer_profile()["name"],
    })))
}

pub async fn get(
    State(state): State<AppState>,
    headers: HeaderMap,
    UrlPath(id): UrlPath<Uuid>,
) -> ApiResult<Json<CloudJob>> {
    let user = require_user(&state, &headers).await?;
    let mut job = sqlx::query_as::<_, CloudJob>(
        "SELECT id,prompt,duration_seconds,bpm,behavior,ai_sovereign,seed::text AS seed,status,stage,completed_steps,total_steps,result_manifest,error_code,created_at,updated_at FROM cloud_jobs WHERE user_id=$1 AND id=$2")
        .bind(user.id).bind(id).fetch_optional(&state.db).await?
        .ok_or_else(|| ApiError::public(StatusCode::NOT_FOUND, "Obra no encontrada."))?;
    enrich_instruments(&state, &mut job).await;
    Ok(Json(job))
}

// The MIDI exporter has always written a private sidecar for each track. Enrich
// the selected job on demand so existing Cloud works get the same sound intent
// that Local Studio already reads, without scanning every job on each poll.
async fn enrich_instruments(state: &AppState, job: &mut CloudJob) {
    let Some(tracks) = job
        .result_manifest
        .as_mut()
        .and_then(|manifest| manifest.get_mut("tracks"))
        .and_then(Value::as_array_mut)
    else {
        return;
    };
    for track in tracks {
        if track.get("instrument").is_some() {
            continue;
        }
        let Some(stem) = track
            .get("filename")
            .and_then(Value::as_str)
            .and_then(|filename| filename.strip_suffix(".mid"))
        else {
            continue;
        };
        if !stem.starts_with("track-") || !stem[6..].bytes().all(|byte| byte.is_ascii_digit()) {
            continue;
        }
        let sidecar_path = state
            .config
            .cloud_output_dir
            .join(job.id.to_string())
            .join(format!("{stem}.pulso.json"));
        let Some(sidecar) = read_json(&sidecar_path).await else {
            continue;
        };
        if let Some(instrument) = instrument_from_sidecar(&sidecar) {
            track["instrument"] = instrument;
        }
    }
}

fn instrument_from_sidecar(sidecar: &Value) -> Option<Value> {
    let part = sidecar.get("parts")?.as_array()?.first()?;
    Some(serde_json::json!({
        "catalog_id": part.get("catalog_id"),
        "department": part.get("department"),
        "source_voice": part.get("source_voice"),
        "role": part.get("role"),
        "orchestral_function": part.get("orchestral_function"),
        "articulation": part.get("articulation"),
        "live_preset_intent": part.get("live_preset_intent"),
    }))
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
    ai_sovereign: bool,
}

async fn claim(state: &AppState) -> ApiResult<Option<ClaimedJob>> {
    let mut tx = state.db.begin().await?;
    let job = sqlx::query_as::<_, ClaimedJob>(
        "SELECT id,prompt,duration_seconds,bpm,behavior,seed,ai_sovereign FROM cloud_jobs WHERE status='queued' ORDER BY created_at LIMIT 1 FOR UPDATE SKIP LOCKED")
        .fetch_optional(&mut *tx).await?;
    if let Some(job) = &job {
        sqlx::query("UPDATE cloud_jobs SET status='running',stage='starting',attempt=attempt+1,lease_until=$2,updated_at=$3 WHERE id=$1")
            .bind(job.id).bind(unix_time()+120).bind(unix_time()).execute(&mut *tx).await?;
    }
    tx.commit().await?;
    Ok(job)
}

fn worker_command(
    worker: &Path,
    openai_key: &str,
    input: &Path,
    directory: &Path,
    output_root: &Path,
    recovery: bool,
) -> Command {
    let mut command = Command::new(worker);
    command
        .env_clear()
        .env("OPENAI_API_KEY", openai_key)
        .env("PULSO_TRACE_PATH", directory.join("api-events.jsonl"))
        .env("HOME", output_root)
        .env("XDG_CONFIG_HOME", output_root)
        .arg(input)
        .arg(directory)
        .kill_on_drop(true);
    // Local Studio reads this same versioned profile. A single file prevents
    // silent musical-path drift when one surface gains an editorial flag.
    for (key, value) in composer_profile()["workerEnvironment"]
        .as_object()
        .expect("composer profile needs workerEnvironment")
    {
        command.env(key, value.as_str().expect("profile values must be strings"));
    }
    if composer_profile()["defaultFirstPass"].as_bool() == Some(true) {
        command.env("PULSO_AI_FIRST_PASS_AUDITION", "1");
    }
    if composer_profile()["defaultPhraseArchitecture"].as_bool() == Some(true) {
        command.env("PULSO_AI_PHRASE_ARCHITECTURE_EXPERIMENT", "1");
    }
    if recovery {
        command
            .env(
                "PULSO_AUTO_RECOVERY_MAX_SECONDS",
                RECOVERY_CALL_SECONDS.to_string(),
            )
            .env("PULSO_AUTO_RECOVERY_MAX_USD", RECOVERY_CALL_USD.to_string());
    }
    command
}

fn valid_resume(checkpoint: &Value, resume: &Value) -> bool {
    resume["schema_version"].as_u64() == Some(2)
        && checkpoint["resumeFile"].as_str() == Some("resume-score.json")
        && checkpoint["completed_units"]
            .as_u64()
            .is_some_and(|n| n > 0)
        && checkpoint["completed_units"] == resume["completed_units"]
        && checkpoint["notes"].as_u64().is_some_and(|n| n > 0)
        && resume["cast_ids"].as_array().is_some_and(|cast| {
            !cast.is_empty() && Some(cast.len() as u64) == checkpoint["parts"].as_u64()
        })
        && resume["sections"]
            .as_array()
            .is_some_and(|sections| !sections.is_empty())
        && resume["blueprint_json"]
            .as_str()
            .is_some_and(|blueprint| !blueprint.is_empty() && blueprint.len() <= 2 * 1024 * 1024)
        && resume["notes"].as_array().is_some_and(|notes| {
            notes.len() == checkpoint["notes"].as_u64().unwrap_or_default() as usize
                && notes.iter().all(|note| {
                    note["instrument_id"].as_str().is_some_and(|id| {
                        resume["cast_ids"]
                            .as_array()
                            .is_some_and(|cast| cast.iter().any(|owner| owner.as_str() == Some(id)))
                    }) && note["start_beat"].as_f64().is_some_and(f64::is_finite)
                        && note["duration_beats"]
                            .as_f64()
                            .is_some_and(|v| v.is_finite() && v > 0.0)
                        && note["pitch"]
                            .as_i64()
                            .is_some_and(|v| (0..=127).contains(&v))
                        && note["velocity"]
                            .as_i64()
                            .is_some_and(|v| (1..=127).contains(&v))
                })
        })
}

fn permanent_worker_error(error: &str) -> bool {
    let lower = error.to_ascii_lowercase();
    [
        "insufficient_quota",
        "no credits remaining",
        "invalid api key",
        "unauthorized",
        "authentication",
        "cancelled",
        "canceled",
    ]
    .iter()
    .any(|marker| lower.contains(marker))
}

async fn run_one(state: &AppState, job: ClaimedJob) -> ApiResult<()> {
    let directory = state.config.cloud_output_dir.join(job.id.to_string());
    tokio::fs::create_dir_all(&directory).await?;
    let original_body = serde_json::json!({
        "prompt": job.prompt, "duration_seconds": job.duration_seconds,
        "bpm": job.bpm, "behavior": job.behavior, "seed": job.seed.to_string(),
        "ai_sovereign": job.ai_sovereign,
        "first_pass_audition": composer_profile()["defaultFirstPass"].as_bool() == Some(true),
    });
    let worker = state
        .config
        .cloud_worker_path
        .as_ref()
        .ok_or_else(|| ApiError::configuration("missing PULSO_CLOUD_WORKER_PATH"))?;
    // The composer needs the OpenAI key, but must not inherit the API server's
    // Stripe, database, mail or signing credentials.
    let openai_key = std::env::var("OPENAI_API_KEY")
        .map_err(|_| ApiError::configuration("missing OPENAI_API_KEY"))?;
    let mut recovery_count = 0_u8;
    let deadline = tokio::time::Instant::now() + CLOUD_WORKER_DEADLINE;
    loop {
        let input = if recovery_count == 0 {
            directory.join("job.json")
        } else {
            directory.join(format!("job-attempt-{recovery_count}.json"))
        };
        let mut body = original_body.clone();
        if recovery_count > 0 {
            // The file is produced from an accepted C++ checkpoint. Snapshot
            // it before launching the next worker so a later checkpoint write
            // cannot change the input under a running process.
            let resume = directory.join(format!("resume-attempt-{recovery_count}.json"));
            tokio::fs::copy(directory.join("resume-score.json"), &resume).await?;
            body["resume_checkpoint_file"] = Value::String(resume.to_string_lossy().into_owned());
        }
        tokio::fs::write(
            &input,
            serde_json::to_vec(&body).map_err(|e| ApiError::internal(e.to_string()))?,
        )
        .await?;
        let mut command = worker_command(
            worker,
            &openai_key,
            &input,
            &directory,
            &state.config.cloud_output_dir,
            recovery_count > 0,
        );
        let mut child = command
            .spawn()
            .map_err(|e| ApiError::internal(e.to_string()))?;
        let mut heartbeat = interval(Duration::from_secs(5));
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
                let manifest = if status.success() {
                    read_json(&directory.join("manifest.json")).await
                } else {
                    None
                };
                if let Some(manifest) = manifest {
                    sqlx::query("UPDATE cloud_jobs SET status='completed',stage='ready',result_manifest=$2,error_code=NULL,lease_until=NULL,updated_at=$3 WHERE id=$1")
                        .bind(job.id).bind(manifest).bind(unix_time())
                        .execute(&state.db).await?;
                    return Ok(());
                }
                let error = read_json(&directory.join("error.json"))
                    .await
                    .and_then(|value| value["error"].as_str().map(str::to_owned))
                    .unwrap_or_default();
                let checkpoint = read_json(&directory.join("checkpoint.json")).await;
                let resume = read_json(&directory.join("resume-score.json")).await;
                if recovery_count < MAX_CHECKPOINT_RECOVERIES
                    && tokio::time::Instant::now() + Duration::from_secs(60) < deadline
                    && !permanent_worker_error(&error)
                    && checkpoint
                        .as_ref()
                        .zip(resume.as_ref())
                        .is_some_and(|(checkpoint, resume)| valid_resume(checkpoint, resume))
                {
                    recovery_count += 1;
                    sqlx::query("UPDATE cloud_jobs SET stage='recovery',attempt=attempt+1,lease_until=$2,updated_at=$3 WHERE id=$1 AND status='running'")
                        .bind(job.id).bind(unix_time()+120).bind(unix_time())
                        .execute(&state.db).await?;
                    break;
                }
                sqlx::query("UPDATE cloud_jobs SET status='failed',stage='generation',error_code=$2,lease_until=NULL,updated_at=$3 WHERE id=$1")
                    .bind(job.id)
                    .bind(if status.success() { "missing_result" } else { "worker_failed" })
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
    fn active_studio_entitlement_grants_cloud_without_beta_allowlist() {
        let mut user = crate::models::AuthenticatedUser {
            id: Uuid::nil(),
            email: "studio@example.com".into(),
            display_name: "Studio".into(),
            stripe_customer_id: None,
            subscription: "none".into(),
            email_verified: true,
            studio_owned: true,
            studio_updates_until: None,
        };
        assert!(license_allows_cloud(&user, &[]));
        user.studio_owned = false;
        assert!(!license_allows_cloud(&user, &[]));
        user.subscription = "active".into();
        assert!(license_allows_cloud(&user, &[]));
    }
    #[test]
    fn only_completed_cloud_jobs_consume_the_daily_quota() {
        assert!(quota_problem(0, 0, 2, false).is_none());
        assert!(quota_problem(1, 0, 2, false).is_none());
        assert!(
            quota_problem(2, 0, 2, false)
                .unwrap()
                .contains("completadas")
        );
        assert!(quota_problem(2, 0, 2, true).is_none());
        assert!(quota_problem(0, 1, 2, true).unwrap().contains("en curso"));
        assert!(unlimited_for(
            "DevingForArt@gmail.com",
            &["devingforart@gmail.com".into()]
        ));
        assert!(!unlimited_for(
            "another@example.com",
            &["devingforart@gmail.com".into()]
        ));
    }
    #[test]
    fn cloud_worker_budget_covers_the_ai_only_editorial_ceiling() {
        assert!(CLOUD_WORKER_DEADLINE >= Duration::from_secs(180 * 60));
        assert_eq!(CLOUD_BEHAVIOR, "adaptive");
    }
    #[test]
    fn cloud_worker_recreates_the_local_editorial_environment_without_server_secrets() {
        let command = worker_command(
            Path::new("worker"),
            "test-only",
            Path::new("job.json"),
            Path::new("jobs/work"),
            Path::new("jobs"),
            false,
        );
        let env: std::collections::HashMap<_, _> = command
            .as_std()
            .get_envs()
            .filter_map(|(key, value)| {
                value.map(|value| {
                    (
                        key.to_string_lossy().into_owned(),
                        value.to_string_lossy().into_owned(),
                    )
                })
            })
            .collect();
        assert_eq!(
            env.get("PULSO_AI_EDITORIAL_V2").map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_LOCAL_EDITORIAL_V2").map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_AI_JOINT_SHORT_CAST").map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_AI_JOINT_NARRATIVE_ANCHORS")
                .map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_AI_GLOBAL_NARRATIVE_EDITOR")
                .map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_AI_FIRST_PASS_AUDITION").map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_AI_PHRASE_ARCHITECTURE_EXPERIMENT")
                .map(String::as_str),
            Some("1")
        );
        assert_eq!(
            env.get("PULSO_DISABLE_SECURE_CREDENTIALS")
                .map(String::as_str),
            Some("1")
        );
        assert!(
            env.get("PULSO_TRACE_PATH")
                .is_some_and(|path| path.ends_with("api-events.jsonl"))
        );
        assert!(!env.contains_key("DATABASE_URL"));
        assert!(!env.contains_key("STRIPE_SECRET_KEY"));
    }
    #[test]
    fn accepted_checkpoint_only_can_be_retried() {
        let checkpoint = serde_json::json!({"resumeFile":"resume-score.json",
            "completed_units":8,"notes":2,"parts":1});
        let note = serde_json::json!({"instrument_id":"piano","start_beat":0.0,
            "duration_beats":1.0,"pitch":60,"velocity":80});
        let resume = serde_json::json!({"schema_version":2,"completed_units":8,"notes":[note.clone(),note],
            "cast_ids":["piano"],"sections":[{"start_bar":0,"bars":4}],
            "blueprint_json":"{}"});
        assert!(valid_resume(&checkpoint, &resume));
        let mut rejected = checkpoint.clone();
        rejected["resumeFile"] = Value::Null;
        assert!(!valid_resume(&rejected, &resume));
        let mut missing_notes = resume.clone();
        missing_notes.as_object_mut().unwrap().remove("notes");
        assert!(!valid_resume(&checkpoint, &missing_notes));
        assert!(permanent_worker_error("OpenAI insufficient_quota"));
        assert!(!permanent_worker_error(
            "Joint AI score reached its global time budget"
        ));
        let retry = worker_command(
            Path::new("worker"),
            "test-only",
            Path::new("job.json"),
            Path::new("jobs/work"),
            Path::new("jobs"),
            true,
        );
        let retry_env: std::collections::HashMap<_, _> = retry
            .as_std()
            .get_envs()
            .filter_map(|(key, value)| {
                value.map(|value| {
                    (
                        key.to_string_lossy().into_owned(),
                        value.to_string_lossy().into_owned(),
                    )
                })
            })
            .collect();
        assert_eq!(
            retry_env
                .get("PULSO_AUTO_RECOVERY_MAX_SECONDS")
                .map(String::as_str),
            Some("600")
        );
        assert_eq!(
            retry_env
                .get("PULSO_AUTO_RECOVERY_MAX_USD")
                .map(String::as_str),
            Some("20")
        );
    }
    #[test]
    fn input_boundaries_and_idempotency_key() {
        let valid = CreateJob {
            prompt: "Una obra con desarrollo".into(),
            duration_seconds: 390,
            bpm: 120.0,
            idempotency_key: Uuid::new_v4().to_string(),
            seed: None,
        };
        assert!(valid_request(&valid));
        assert!(!valid_request(&CreateJob {
            duration_seconds: 29,
            ..valid.clone()
        }));
        assert!(!valid_request(&CreateJob {
            bpm: 181.0,
            ..valid.clone()
        }));
        assert!(!valid_request(&CreateJob {
            bpm: f64::NAN,
            ..valid.clone()
        }));
        assert!(valid_request(&CreateJob {
            seed: Some("1168010360189720986".into()),
            ..valid.clone()
        }));
        assert!(!valid_request(&CreateJob {
            seed: Some("0".into()),
            ..valid.clone()
        }));
        assert!(!valid_request(&CreateJob {
            seed: Some("9223372036854775808".into()),
            ..valid
        }));
    }

    #[test]
    fn old_midi_sidecars_recover_sound_intent() {
        let sidecar = serde_json::json!({"parts": [{
            "catalog_id": "analog_pad",
            "department": "harmony",
            "source_voice": "harmonic_foundation",
            "role": "primary_chord_bed",
            "live_preset_intent": "dark low-passed pad"
        }]});
        let instrument = instrument_from_sidecar(&sidecar).expect("part should exist");
        assert_eq!(instrument["catalog_id"], "analog_pad");
        assert_eq!(instrument["source_voice"], "harmonic_foundation");
        assert_eq!(instrument["live_preset_intent"], "dark low-passed pad");
        assert!(instrument_from_sidecar(&serde_json::json!({"parts": []})).is_none());
    }
}
