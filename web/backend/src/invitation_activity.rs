//! Read-only, admin-only analytics. Attribution is the job's persisted invitation,
//! never an inference from the user's current grant. No prompts, tokens or raw errors.
use axum::{Json, extract::{Path, Query, State}, http::{HeaderMap, StatusCode}};
use serde::Deserialize;
use serde_json::{Value, json};
use uuid::Uuid;
use crate::{AppState, error::{ApiError, ApiResult}};

#[derive(Default, Deserialize)]
#[serde(rename_all="camelCase", deny_unknown_fields)]
pub struct ActivityQuery { user_id:Option<Uuid>, #[serde(default)] users_page:u32, #[serde(default)] jobs_page:u32 }
const PAGE:i64=25;

pub async fn activity(State(state):State<AppState>, headers:HeaderMap,
    Path(id):Path<Uuid>, Query(query):Query<ActivityQuery>) -> ApiResult<Json<Value>> {
    crate::invitations::admin(&headers)?;
    if query.users_page>10000 || query.jobs_page>10000 {
        return Err(ApiError::public(StatusCode::BAD_REQUEST,"Página fuera de rango."));
    }
    let mut tx=state.db.begin().await?;
    sqlx::query("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY").execute(&mut *tx).await?;
    let offer:Option<Value>=sqlx::query_scalar("SELECT jsonb_build_object('id',id,'label',label,'credits',credits,'dailyLimit',daily_limit,'weeklyLimit',weekly_limit,'monthlyLimit',monthly_limit,'expiresAt',expires_at,'revokedAt',revoked_at) FROM cloud_invitations WHERE id=$1")
        .bind(id).fetch_optional(&mut *tx).await?;
    let offer=offer.ok_or_else(||ApiError::public(StatusCode::NOT_FOUND,"Invitación no encontrada."))?;
    if let Some(user)=query.user_id {
        let member:bool=sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM cloud_invitation_redemptions WHERE invitation_id=$1 AND user_id=$2)").bind(id).bind(user).fetch_one(&mut *tx).await?;
        if !member{return Err(ApiError::public(StatusCode::NOT_FOUND,"El usuario no pertenece a esta invitación."));}
    }
    let cost=crate::credits::cost();
    let summary:Value=sqlx::query_scalar(r#"
        SELECT jsonb_build_object('total',count(*),
          'completed',count(*) FILTER(WHERE status='completed'),
          'failed',count(*) FILTER(WHERE status='failed'),
          'cancelled',count(*) FILTER(WHERE status='cancelled'),
          'active',count(*) FILTER(WHERE status IN ('queued','running')),
          'creditsUsed',count(*) FILTER(WHERE status='completed')*$3,
          'creditsReserved',count(*) FILTER(WHERE status IN ('queued','running'))*$3,
          'musicSeconds',coalesce(sum(duration_seconds) FILTER(WHERE status='completed'),0),
          'averageTurnaroundSeconds',round(avg(greatest(0,updated_at-created_at)) FILTER(WHERE status='completed')),
          'lastActivityAt',max(updated_at))
        FROM cloud_jobs WHERE invitation_id=$1 AND ($2::uuid IS NULL OR user_id=$2)
    "#).bind(id).bind(query.user_id).bind(cost).fetch_one(&mut *tx).await?;
    let users_total:i64=sqlx::query_scalar("SELECT count(*) FROM cloud_invitation_redemptions WHERE invitation_id=$1").bind(id).fetch_one(&mut *tx).await?;
    let users:Vec<Value>=sqlx::query_scalar(r#"
        SELECT jsonb_build_object('id',u.id,'email',u.email,'displayName',u.display_name,
          'emailVerified',u.email_verified,'redeemedAt',r.redeemed_at,
          'completed',a.completed,'failed',a.failed,'cancelled',a.cancelled,'active',a.active,
          'creditsUsed',a.completed*$2,'creditsReserved',a.active*$2,
          'creditsRemaining',greatest(0,i.credits-(a.completed+a.active)*$2),
          'lastActivityAt',a.last_at)
        FROM cloud_invitation_redemptions r JOIN users u ON u.id=r.user_id
        JOIN cloud_invitations i ON i.id=r.invitation_id
        CROSS JOIN LATERAL (SELECT count(*) FILTER(WHERE status='completed') AS completed,
          count(*) FILTER(WHERE status='failed') AS failed,
          count(*) FILTER(WHERE status='cancelled') AS cancelled,
          count(*) FILTER(WHERE status IN ('queued','running')) AS active,max(updated_at) AS last_at
          FROM cloud_jobs WHERE invitation_id=r.invitation_id AND user_id=u.id) a
        WHERE r.invitation_id=$1 ORDER BY r.redeemed_at DESC,u.id LIMIT $3 OFFSET $4
    "#).bind(id).bind(cost).bind(PAGE).bind(i64::from(query.users_page)*PAGE).fetch_all(&mut *tx).await?;
    let jobs:Vec<Value>=sqlx::query_scalar(r#"
        SELECT jsonb_build_object('id',j.id,'userId',j.user_id,'email',u.email,
          'title',left(coalesce(nullif(j.result_manifest->>'title',''),'Composición'),180),
          'status',j.status,'durationSeconds',j.duration_seconds,'bpm',j.bpm,'behavior',j.behavior,
          'engineVersion',j.engine_version,'createdAt',j.created_at,'updatedAt',j.updated_at,
          'turnaroundSeconds',CASE WHEN j.status IN ('completed','failed','cancelled') THEN greatest(0,j.updated_at-j.created_at) END,
          'creditsUsed',CASE WHEN j.status='completed' THEN $3 ELSE 0 END,
          'creditsReserved',CASE WHEN j.status IN ('queued','running') THEN $3 ELSE 0 END)
        FROM cloud_jobs j JOIN users u ON u.id=j.user_id
        WHERE j.invitation_id=$1 AND ($2::uuid IS NULL OR j.user_id=$2)
        ORDER BY j.created_at DESC,j.id DESC LIMIT $4 OFFSET $5
    "#).bind(id).bind(query.user_id).bind(cost).bind(PAGE).bind(i64::from(query.jobs_page)*PAGE).fetch_all(&mut *tx).await?;
    tx.commit().await?;
    Ok(Json(json!({"invitation":offer,"summary":summary,"users":users,"usersTotal":users_total,
        "jobs":jobs,"jobsTotal":summary["total"],"pageSize":PAGE,"usersPage":query.users_page,
        "jobsPage":query.jobs_page,"selectedUserId":query.user_id,"creditsPerWork":cost,
        "generatedAt":crate::crypto::unix_time(),"scope":"invitation_attributed_jobs"})))
}
