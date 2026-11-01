use reqwest::{blocking::Client, header, redirect::Policy, Url};
use serde_json::Value;
use std::{process::Command, sync::Mutex, time::Duration};

const ORIGIN: &str = "https://app.uberclicker.com";
const COOKIE: &str = "__Secure-better-auth.session_token";
// Serialize requests so a refresh cannot restore credentials after sign-out.
static REQUEST: Mutex<()> = Mutex::new(());

fn allowed_path(path: &str, post: bool) -> bool {
    if !post {
        return matches!(
            path,
            "/api/config" | "/api/account" | "/api/auth/get-session"
        );
    }
    matches!(
        path,
        "/api/auth/email-otp/send-verification-otp"
            | "/api/auth/sign-in/email-otp"
            | "/api/auth/sign-out"
            | "/api/auth/update-user"
            | "/v1/rooms"
            | "/api/clickers/link"
            | "/api/clickers/routes"
            | "/api/billing/checkout"
            | "/api/billing/portal"
            | "/api/billing/sync"
    ) || regex::Regex::new(
        r"^/api/(rooms/[a-f0-9]{32}/(rename|revoke)|clickers/[a-f0-9]{32}/unlink)$",
    )
    .unwrap()
    .is_match(path)
        || regex::Regex::new(r"^/api/rooms/[a-f0-9]{32}/clicker-link$")
            .unwrap()
            .is_match(path)
}

fn session_cookie(headers: &header::HeaderMap) -> Option<String> {
    headers
        .get_all(header::SET_COOKIE)
        .iter()
        .filter_map(|h| h.to_str().ok())
        .filter_map(|h| h.split(';').next())
        .filter_map(|h| h.split_once('='))
        .find_map(|(name, value)| (name == COOKIE).then(|| value.to_owned()))
}

#[tauri::command]
pub async fn account_request(path: String, body: Option<Value>) -> Result<Value, String> {
    if !allowed_path(&path, body.is_some()) {
        return Err("Unsupported account request".into());
    }
    tauri::async_runtime::spawn_blocking(move || request(&path, body))
        .await
        .map_err(|_| "Account request interrupted".to_string())?
}

fn request(path: &str, body: Option<Value>) -> Result<Value, String> {
    let _guard = REQUEST.lock().map_err(|_| "Account session unavailable")?;
    let entry = keyring::Entry::new("com.uberclicker.companion", "account")
        .map_err(|_| "Operating system credential store is unavailable")?;
    let cookie = match entry.get_password() {
        Ok(value) => Some(value),
        Err(keyring::Error::NoEntry) => None,
        Err(_) => return Err("Unlock your operating system credential store and try again".into()),
    };
    let client = Client::builder()
        .redirect(Policy::none())
        .timeout(Duration::from_secs(30))
        .build()
        .map_err(|_| "Could not connect to your account")?;
    let url = format!("{ORIGIN}{path}");
    let mut request = if let Some(body) = body {
        client.post(url).json(&body)
    } else {
        client.get(url)
    };
    request = request.header(header::ORIGIN, ORIGIN);
    if let Some(cookie) = cookie {
        request = request.header(header::COOKIE, format!("{COOKIE}={cookie}"));
    }
    let response = request.send().map_err(|_| {
        "Could not reach your account. Check your internet connection and try again."
    })?;
    let status = response.status();
    let updated = session_cookie(response.headers());
    let mut data: Value = response
        .json()
        .map_err(|_| "Unexpected account response. Please try again.")?;
    if let Some(value) = updated {
        if value.is_empty() {
            match entry.delete_credential() {
                Ok(()) | Err(keyring::Error::NoEntry) => {}
                Err(_) => return Err("Could not clear your saved account session".into()),
            }
        } else {
            entry.set_password(&value).map_err(|_| {
                "Could not save your account session in the operating system credential store"
            })?;
        }
    }
    if !status.is_success() {
        return Err(data["message"]
            .as_str()
            .or(data["error"].as_str())
            .unwrap_or("Account request failed. Please try again.")
            .to_owned());
    }
    // The renderer needs account data, never the session credential.
    if let Some(object) = data.as_object_mut() {
        object.remove("token");
    }
    if let Some(session) = data.get_mut("session").and_then(Value::as_object_mut) {
        session.remove("token");
    }
    Ok(data)
}

fn external_url(url: &str) -> Result<Url, String> {
    let parsed = Url::parse(url).map_err(|_| "Invalid external link")?;
    let allowed = matches!(
        parsed.host_str(),
        Some("checkout.stripe.com" | "billing.stripe.com")
    ) || (parsed.host_str() == Some("app.uberclicker.com")
        && regex::Regex::new(r"^/manual/[a-f0-9]{32}$")
            .unwrap()
            .is_match(parsed.path()));
    if parsed.scheme() != "https"
        || !parsed.username().is_empty()
        || parsed.password().is_some()
        || parsed.port().is_some()
        || !allowed
    {
        return Err("Unsupported external link".into());
    }
    Ok(parsed)
}

#[tauri::command]
pub async fn account_open_external(url: String) -> Result<(), String> {
    let url = external_url(&url)?.to_string();
    tauri::async_runtime::spawn_blocking(move || {
        #[cfg(target_os = "macos")]
        let status = Command::new("open").arg(&url).status();
        #[cfg(target_os = "windows")]
        let status = Command::new("rundll32.exe")
            .args(["url.dll,FileProtocolHandler", &url])
            .status();
        #[cfg(target_os = "linux")]
        let status = Command::new("xdg-open").arg(&url).status();
        match status {
            Ok(status) if status.success() => Ok(()),
            _ => Err("Could not open your browser. Please try again.".into()),
        }
    })
    .await
    .map_err(|_| "Browser request interrupted".to_string())?
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn account_requests_cannot_escape_the_account_api() {
        assert!(allowed_path("/api/account", false));
        assert!(allowed_path("/api/auth/sign-in/email-otp", true));
        for path in [
            "https://evil.test/api/account",
            "//evil.test",
            "/api/../admin",
            "/api/billing/webhook",
            "/api/auth/sign-up/email",
            "/api/account?redirect=evil",
        ] {
            assert!(!allowed_path(path, true));
        }
        assert!(!allowed_path("/api/auth/sign-out", false));
    }
    #[test]
    fn only_session_cookie_is_saved_including_deletion() {
        let mut headers = header::HeaderMap::new();
        headers.append(header::SET_COOKIE, "other=ignored; Secure".parse().unwrap());
        headers.append(
            header::SET_COOKIE,
            format!("{COOKIE}=signed%2Bvalue; Path=/; HttpOnly; Secure")
                .parse()
                .unwrap(),
        );
        assert_eq!(session_cookie(&headers).as_deref(), Some("signed%2Bvalue"));
        headers.insert(
            header::SET_COOKIE,
            format!("{COOKIE}=; Max-Age=0").parse().unwrap(),
        );
        assert_eq!(session_cookie(&headers).as_deref(), Some(""));
    }
    #[test]
    fn external_links_are_restricted_to_stripe_and_manual_views() {
        assert!(external_url("https://checkout.stripe.com/c/pay/test#fragment").is_ok());
        assert!(external_url("https://billing.stripe.com/p/session/test").is_ok());
        assert!(external_url(
            "https://app.uberclicker.com/manual/0123456789abcdef0123456789abcdef"
        )
        .is_ok());
        for url in [
            "file:///etc/passwd",
            "https://checkout.stripe.com.evil.test",
            "https://evil.test",
            "https://user@billing.stripe.com",
            "https://app.uberclicker.com/api/account",
            "http://checkout.stripe.com",
            "https://billing.stripe.com:444/",
        ] {
            assert!(external_url(url).is_err());
        }
    }
}
