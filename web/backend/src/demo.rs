//! Promotional access is enforced by the same API locally and in Cloud.
use axum::{Json, extract::{State, Query}, http::{HeaderMap, StatusCode}};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use sqlx::{PgConnection, FromRow};
use uuid::Uuid;
use crate::{AppState, auth::require_user, crypto::unix_time, models::AuthenticatedUser, error::{ApiError, ApiResult}};

pub fn owner(email: &str) -> bool { email.trim().eq_ignore_ascii_case("devingforart@gmail.com") }

pub async fn eligible(conn: &mut PgConnection, user: &AuthenticatedUser) -> ApiResult<bool> {
    if owner(&user.email) { return Ok(false); }
    // Redeeming a campaign must not downgrade an already-paid account.
    let invited = crate::invitations::grant(conn,user.id).await?.is_some();
    let (legacy,paid):(bool,bool) = sqlx::query_as("SELECT EXISTS(SELECT 1 FROM entitlements e WHERE e.user_id=$1 AND e.status='active' AND e.source LIKE 'promotion:%'), (EXISTS(SELECT 1 FROM subscriptions s WHERE s.user_id=$1 AND s.status IN ('active','trialing')) OR EXISTS(SELECT 1 FROM entitlements e WHERE e.user_id=$1 AND e.status='active' AND e.source='stripe'))")
        .bind(user.id).fetch_one(conn).await?;
    Ok((invited || legacy) && !paid)
}

#[derive(Debug, Serialize)]
#[serde(rename_all="camelCase")]
pub struct Window { pub days: i64, pub limit: i64, pub completed: i64, pub remaining: i64, pub resets_at: Option<i64> }
pub fn windows(completions: &[i64], active: i64, now: i64) -> Vec<Window> {
    policy_windows(completions,active,now,[3,7,12])
}
fn policy_windows(completions: &[i64], active: i64, now: i64, limits:[i64;3]) -> Vec<Window> {
    [1,7,30].into_iter().zip(limits).map(|(days,limit)| {
        let mut within: Vec<_> = completions.iter().copied().filter(|at| *at > now-days*86400 && *at <= now).collect();
        within.sort_unstable_by(|a,b| b.cmp(a));
        let completed=within.len() as i64;
        // The limit-th newest completion must expire before another admission.
        let resets_at=if completed>=limit { Some(within[limit as usize-1]+days*86400) } else { None };
        Window { days, limit, completed, remaining:(limit-completed-active).max(0), resets_at }
    }).collect()
}
#[derive(Debug, Serialize)]
#[serde(rename_all="camelCase")]
pub struct Access { pub windows: Vec<Window>, pub can_compose: bool, pub next_available_at: Option<i64>, pub survey_due: bool, pub completed_works: i64, pub credits:Option<i64>, pub credits_remaining:Option<i64> }
pub fn survey_due(total:i64, last:Option<(i64,i64)>, snooze:i64, now:i64) -> bool {
    total>0 && now>=snooze && last.is_none_or(|(at,count)| now>=at+7*86400 && total>=count+3)
}
pub async fn access(conn: &mut PgConnection, user: Uuid, now:i64) -> ApiResult<Access> {
    let grant=crate::invitations::grant(conn,user).await?;
    // One MVCC snapshot: a running->completed transition must never disappear
    // between separate active and completion queries during admission.
    let (completions,active,total):(Vec<i64>,i64,i64)=sqlx::query_as("SELECT COALESCE(array_agg(updated_at) FILTER (WHERE status='completed' AND updated_at>$2),ARRAY[]::bigint[]), COUNT(*) FILTER (WHERE status IN ('queued','running')),COUNT(*) FILTER (WHERE status='completed') FROM cloud_jobs WHERE user_id=$1")
        .bind(user).bind(now-30*86400).fetch_one(&mut *conn).await?;
    let last:Option<(i64,i64)>=sqlx::query_as("SELECT created_at,completed_works FROM demo_feedback WHERE user_id=$1 ORDER BY created_at DESC LIMIT 1")
        .bind(user).fetch_optional(&mut *conn).await?;
    let snooze:Option<i64>=sqlx::query_scalar("SELECT until_at FROM demo_feedback_snooze WHERE user_id=$1")
        .bind(user).fetch_optional(&mut *conn).await?;
    let windows=if let Some(g)=&grant { policy_windows(&completions,active,now,[g.daily_limit,g.weekly_limit,g.monthly_limit]) }
        else { windows(&completions,active,now) };
    let used:i64=if let Some(g)=&grant {
        sqlx::query_scalar("SELECT count(*) FROM cloud_jobs WHERE user_id=$1 AND status='completed' AND invitation_id=$2")
            .bind(user).bind(g.id).fetch_one(&mut *conn).await?
    } else { 0 };
    let credits=grant.as_ref().map(|g|g.credits);
    let credits_remaining=credits.map(|n|(n-(used+active)*crate::credits::cost()).max(0));
    let next_available_at=windows.iter().filter_map(|w|w.resets_at).max();
    Ok(Access{can_compose:active==0 && windows.iter().all(|w|w.remaining>0) && credits_remaining.is_none_or(|n|n>=crate::credits::cost()),windows,next_available_at,credits,credits_remaining,
        survey_due:survey_due(total,last,snooze.unwrap_or(0),now),completed_works:total})
}

#[derive(Deserialize)]
#[serde(rename_all="camelCase")]
pub struct FeedbackInput { request_key:Uuid, overall:i16, musicality:i16, ease_of_use:i16, strengths:String, improvements:String, locale:String }
fn valid(input:&FeedbackInput)->bool {
    [input.overall,input.musicality,input.ease_of_use].iter().all(|n|(1..=5).contains(n)) &&
    [&input.strengths,&input.improvements].iter().all(|s|(1..=2000).contains(&s.trim().chars().count())) &&
    ["es","en","pt-BR"].contains(&input.locale.as_str())
}
pub async fn submit(State(state):State<AppState>,headers:HeaderMap,Json(input):Json<FeedbackInput>)->ApiResult<Json<Value>> {
    let user=require_user(&state,&headers).await?;
    if !valid(&input) { return Err(ApiError::public(StatusCode::BAD_REQUEST,"Revisa las puntuaciones y los comentarios de la encuesta.")); }
    let mut tx=state.db.begin().await?;
    sqlx::query("SELECT pg_advisory_xact_lock(hashtext($1::text))").bind(user.id.to_string()).execute(&mut *tx).await?;
    if !user.email_verified || !eligible(&mut tx,&user).await? { return Err(ApiError::public(StatusCode::FORBIDDEN,"Esta encuesta es para cuentas demo verificadas.")); }
    let prior:Option<Uuid>=sqlx::query_scalar("SELECT id FROM demo_feedback WHERE user_id=$1 AND request_key=$2")
        .bind(user.id).bind(input.request_key).fetch_optional(&mut *tx).await?;
    if let Some(id)=prior { return Ok(Json(json!({"id":id,"saved":true}))); }
    let now=unix_time();
    // Manual feedback remains available after the first work, at most once/day.
    let recent:bool=sqlx::query_scalar("SELECT EXISTS(SELECT 1 FROM demo_feedback WHERE user_id=$1 AND created_at>$2)")
        .bind(user.id).bind(now-86400).fetch_one(&mut *tx).await?;
    let usage=access(&mut tx,user.id,now).await?;
    if usage.completed_works==0 || recent { return Err(ApiError::public(StatusCode::CONFLICT,"Ya guardamos tu opinión de hoy o todavía no completaste una obra.")); }
    let job:Option<Uuid>=sqlx::query_scalar("SELECT id FROM cloud_jobs WHERE user_id=$1 AND status='completed' ORDER BY updated_at DESC LIMIT 1").bind(user.id).fetch_optional(&mut *tx).await?;
    let id=Uuid::new_v4();
    sqlx::query("INSERT INTO demo_feedback(id,user_id,request_key,completed_works,last_job_id,overall,musicality,ease_of_use,strengths,improvements,locale,created_at) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12)")
        .bind(id).bind(user.id).bind(input.request_key).bind(usage.completed_works).bind(job)
        .bind(input.overall).bind(input.musicality).bind(input.ease_of_use).bind(input.strengths.trim()).bind(input.improvements.trim()).bind(input.locale).bind(now).execute(&mut *tx).await?;
    tx.commit().await?;
    Ok(Json(json!({"id":id,"saved":true})))
}
pub async fn snooze(State(state):State<AppState>,headers:HeaderMap)->ApiResult<StatusCode> {
    let user=require_user(&state,&headers).await?;
    let mut conn=state.db.acquire().await?;
    if !user.email_verified || !eligible(&mut conn,&user).await? { return Err(ApiError::public(StatusCode::FORBIDDEN,"Acceso demo requerido.")); }
    sqlx::query("INSERT INTO demo_feedback_snooze(user_id,until_at) VALUES($1,$2) ON CONFLICT(user_id) DO UPDATE SET until_at=EXCLUDED.until_at")
        .bind(user.id).bind(unix_time()+86400).execute(&mut *conn).await?;
    Ok(StatusCode::NO_CONTENT)
}
#[derive(Deserialize)]
pub struct Page { #[serde(default)] offset:i64 }
#[derive(Serialize,FromRow)]
#[serde(rename_all="camelCase")]
pub struct Feedback { id:Uuid,email:String,display_name:String,version:i32,completed_works:i64,last_job_id:Option<Uuid>,overall:i16,musicality:i16,ease_of_use:i16,strengths:String,improvements:String,locale:String,created_at:i64 }
pub async fn review(State(state):State<AppState>,headers:HeaderMap,Query(page):Query<Page>)->ApiResult<Json<Value>> {
    let user=require_user(&state,&headers).await?;
    if !owner(&user.email) || !user.email_verified { return Err(ApiError::public(StatusCode::FORBIDDEN,"Acceso restringido.")); }
    if !(0..=1_000_000).contains(&page.offset) { return Err(ApiError::public(StatusCode::BAD_REQUEST,"Página inválida.")); }
    let rows=sqlx::query_as::<_,Feedback>("SELECT f.id,u.email,u.display_name,f.version,f.completed_works,f.last_job_id,f.overall,f.musicality,f.ease_of_use,f.strengths,f.improvements,f.locale,f.created_at FROM demo_feedback f JOIN users u ON u.id=f.user_id ORDER BY f.created_at DESC,f.id LIMIT 50 OFFSET $1")
        .bind(page.offset).fetch_all(&state.db).await?;
    let total:i64=sqlx::query_scalar("SELECT COUNT(*) FROM demo_feedback").fetch_one(&state.db).await?;
    Ok(Json(json!({"items":rows,"total":total})))
}

#[cfg(test)] mod tests {
    use super::*;
    #[test] fn rolling_limits_and_failures_never_enter_usage(){
        let now=40*86400; let w=windows(&[now-1,now-2,now-3],0,now);
        assert_eq!(w[0].remaining,0); assert_eq!(w[1].remaining,4); assert_eq!(w[2].remaining,9);
        assert_eq!(w[0].resets_at,Some(now-3+86400));
        assert_eq!(windows(&[now-86400],0,now)[0].completed,0);
        assert_eq!(windows(&[],1,now)[0].remaining,2);
        assert_eq!(windows(&vec![now-2*86400;7],0,now)[1].remaining,0);
        assert_eq!(windows(&vec![now-8*86400;12],0,now)[2].remaining,0);
    }
    #[test] fn surveys_are_optional_and_infrequent(){
        assert!(!survey_due(0,None,0,100)); assert!(survey_due(1,None,0,100));
        assert!(!survey_due(4,Some((100,1)),0,200));
        assert!(survey_due(4,Some((100,1)),0,100+7*86400));
        assert!(!survey_due(3,Some((100,1)),0,100+7*86400));
        assert!(!survey_due(1,None,200,100));
        assert!(owner(" DEVINGFORART@gmail.com ")); assert!(!owner("another@gmail.com"));
    }
}
