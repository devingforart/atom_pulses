use serde_json::json;

use crate::{AppState, error::ApiResult};

pub async fn send_action(
    state: &AppState,
    recipient: &str,
    subject: &str,
    title: &str,
    action_url: &str,
) -> ApiResult<()> {
    let Some(api_key) = &state.config.mail_api_key else {
        tracing::warn!(%recipient, %action_url, "email provider disabled; action URL logged for development");
        return Ok(());
    };
    state
        .http
        .post("https://api.resend.com/emails")
        .bearer_auth(api_key)
        .json(&json!({
            "from": state.config.mail_from,
            "to": [recipient],
            "subject": subject,
            "html": format!("<h1>{title}</h1><p><a href=\"{action_url}\">Continuar en PULSO</a></p><p>El enlace vence pronto. Si no solicitaste esta acción, puedes ignorar este mensaje.</p>")
        }))
        .send()
        .await?
        .error_for_status()?;
    Ok(())
}
