//! Explicit admin delivery. No plaintext invitation code is persisted or logged.
use axum::{Json, extract::{Path,State}, http::{HeaderMap,StatusCode}};
use serde::Deserialize;
use serde_json::{Value,json};
use sqlx::FromRow;
use uuid::Uuid;
use crate::{AppState,crypto::{hash,unix_time},error::{ApiError,ApiResult}};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Send {code:String}
#[derive(FromRow)]
struct Offer {code_hash:String,target_email:Option<String>,label:String,credits:i64,daily_limit:i64,weekly_limit:i64,monthly_limit:i64,expires_at:Option<i64>,revoked_at:Option<i64>}
fn escape(s:&str)->String{s.replace('&',"&amp;").replace('<',"&lt;").replace('>',"&gt;").replace('"',"&quot;").replace('\'',"&#39;")}
fn content(o:&Offer,code:&str,url:&str,cost:i64)->String {
    let expiry=o.expires_at.and_then(|t|time::OffsetDateTime::from_unix_timestamp(t).ok()).map(|t|format!("Vence: {t} (UTC).\n")).unwrap_or_default();
    format!("Te invitamos a crear música con PULSO Cloud.\n\n{}\nTu código personal: {code}\n\n1. Creá tu cuenta con este mismo email y verificalo.\n2. En Precios → Código de acceso, ingresá tu código.\n3. Entrá al estudio Cloud y describí tu idea musical.\n\nAbrir PULSO: {url}\n\nTu acceso: {} créditos totales, hasta {} obras completadas ({cost} créditos por obra). El presupuesto no se renueva cada mes.\nLímites simultáneos: {} obras cada 24 horas, {} cada 7 días y {} cada 30 días, en ventanas móviles. Las fallidas no consumen créditos; las obras en curso los reservan.\n{expiry}El acceso puede revocarse. No incluye licencia Studio y no se acumula con otra invitación activa.\n\nSi no esperabas esta invitación, podés ignorarla.",o.label,o.credits,o.credits/cost,o.daily_limit,o.weekly_limit,o.monthly_limit)
}
pub async fn send(State(s):State<AppState>,headers:HeaderMap,Path(id):Path<Uuid>,Json(input):Json<Send>)->ApiResult<Json<Value>> {
    crate::invitations::admin(&headers)?;
    let bad=||ApiError::public(StatusCode::BAD_REQUEST,"Invitación no válida para enviar a un destinatario.");
    let code=crate::invitations::normalize(&input.code).ok_or_else(bad)?;
    let now=unix_time();
    let mut tx=s.db.begin().await?;
    let o:Offer=sqlx::query_as("SELECT code_hash,target_email,label,credits,daily_limit,weekly_limit,monthly_limit,expires_at,revoked_at FROM cloud_invitations WHERE id=$1 FOR UPDATE").bind(id).fetch_optional(&mut *tx).await?.ok_or_else(bad)?;
    if o.code_hash!=hash(&code)||o.revoked_at.is_some()||o.expires_at.is_some_and(|v|v<=now){return Err(bad())}
    let recipient=o.target_email.as_ref().ok_or_else(bad)?;
    let text=content(&o,&code,&s.config.public_url,crate::credits::cost());
    let payload=json!({"from":s.config.mail_from,"to":[recipient],"subject":"Tu invitación personal a PULSO Cloud","text":text,"html":format!("<div style=\"font-family:Arial,sans-serif;max-width:600px;margin:auto;color:#242420\"><h1 style=\"color:#d84f2b\">Tu próxima idea empieza en PULSO</h1><p style=\"white-space:pre-line;line-height:1.7\">{}</p><p><a href=\"{}\" style=\"color:#d84f2b\">Abrir PULSO</a></p></div>",escape(&text),escape(&s.config.public_url))});
    let fingerprint=hash(&payload.to_string());
    let previous:Option<(String,i64,i64,String)>=sqlx::query_as("SELECT status,first_attempt_at,updated_at,payload_hash FROM cloud_invitation_emails WHERE invitation_id=$1").bind(id).fetch_optional(&mut *tx).await?;
    if let Some((status,first,last,old))=&previous {
        if status=="accepted"{return Ok(Json(json!({"status":"accepted"})))}
        if status=="sending" && now-last<30{return Ok(Json(json!({"status":"sending"})))}
        // Resend remembers idempotency keys for 24 h. Never duplicate an ambiguous send later.
        if *first>0&&(now-first>=23*3600||old!=&fingerprint){return Ok(Json(json!({"status":"unknown","message":"No se reenviará automáticamente. Revisá el envío en Resend o compartí el código manualmente."})))}
    }
    if s.config.demo_lab||s.config.mail_api_key.as_ref().is_none_or(|k|k.is_empty()) {
        sqlx::query("INSERT INTO cloud_invitation_emails(invitation_id,status,first_attempt_at,updated_at,payload_hash) VALUES($1,'failed',0,$2,$3) ON CONFLICT(invitation_id) DO NOTHING").bind(id).bind(now).bind(&fingerprint).execute(&mut *tx).await?;
        tx.commit().await?;
        return Ok(Json(json!({"status":"failed","message":"Correo no configurado en este entorno. La invitación está guardada y podés copiarla."})))
    }
    sqlx::query("INSERT INTO cloud_invitation_emails(invitation_id,status,first_attempt_at,updated_at,payload_hash) VALUES($1,'sending',$2,$2,$3) ON CONFLICT(invitation_id) DO UPDATE SET status='sending',updated_at=$2,first_attempt_at=CASE WHEN cloud_invitation_emails.first_attempt_at=0 THEN $2 ELSE cloud_invitation_emails.first_attempt_at END,payload_hash=$3").bind(id).bind(now).bind(fingerprint).execute(&mut *tx).await?;
    tx.commit().await?;
    let result=s.http.post("https://api.resend.com/emails").timeout(std::time::Duration::from_secs(20)).bearer_auth(s.config.mail_api_key.as_ref().unwrap()).header("Idempotency-Key",format!("pulso-invitation/{id}")).json(&payload).send().await;
    let (status,provider)=match result {
        Ok(r) if r.status().is_success()=>{let body=r.json::<Value>().await.ok();match body.and_then(|v|v["id"].as_str().map(str::to_owned)){Some(id)=>("accepted",Some(id)),None=>("unknown",None)}},
        Ok(r) if r.status().is_client_error() && r.status().as_u16()!=409=>("failed",None),
        _=>("unknown",None)
    };
    sqlx::query("UPDATE cloud_invitation_emails SET status=$2,provider_id=$3,updated_at=$4 WHERE invitation_id=$1").bind(id).bind(status).bind(provider).bind(unix_time()).execute(&s.db).await?;
    Ok(Json(json!({"status":status})))
}
#[cfg(test)]mod tests{
    use super::*;
    #[test]fn email_escapes_untrusted_text(){assert_eq!(escape("<a & \"'>"),"&lt;a &amp; &quot;&#39;&gt;")}
    #[test]fn email_contains_actual_terms(){let o=Offer{code_hash:String::new(),target_email:Some("a@b.com".into()),label:"Demo".into(),credits:36,daily_limit:3,weekly_limit:7,monthly_limit:12,expires_at:None,revoked_at:None};let t=content(&o,"PULSO-1234","https://pulso-audio.com",3);for s in ["PULSO-1234","12 obras completadas","3 obras cada 24 horas","7 cada 7 días","12 cada 30 días","no se renueva"]{assert!(t.contains(s),"{s}")}}
}
