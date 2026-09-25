use std::time::{SystemTime, UNIX_EPOCH};

use base64::{Engine, engine::general_purpose::URL_SAFE_NO_PAD};
use ed25519_dalek::{Signer, SigningKey};
use serde::Serialize;
use sha2::{Digest, Sha256};

use crate::error::{ApiError, ApiResult};

pub fn unix_time() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as i64
}

pub fn random_token(bytes: usize) -> ApiResult<String> {
    let mut value = vec![0_u8; bytes];
    getrandom::fill(&mut value).map_err(|error| ApiError::internal(error.to_string()))?;
    Ok(URL_SAFE_NO_PAD.encode(value))
}

pub fn hash(value: &str) -> String {
    hex::encode(Sha256::digest(value.as_bytes()))
}

pub fn sign_json<T: Serialize>(seed_hex: &str, payload: &T) -> ApiResult<String> {
    let seed = hex::decode(seed_hex)
        .map_err(|_| ApiError::configuration("invalid license signing key"))?;
    let seed: [u8; 32] = seed
        .try_into()
        .map_err(|_| ApiError::configuration("license signing key must be a 32-byte hex seed"))?;
    let key = SigningKey::from_bytes(&seed);
    let body =
        serde_json::to_vec(payload).map_err(|error| ApiError::internal(error.to_string()))?;
    let signature = key.sign(&body);
    Ok(format!(
        "{}.{}",
        URL_SAFE_NO_PAD.encode(body),
        URL_SAFE_NO_PAD.encode(signature.to_bytes())
    ))
}
