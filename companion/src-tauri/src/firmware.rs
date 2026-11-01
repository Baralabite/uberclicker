use serde_json::{json, Value};
use std::time::Duration;

const RELEASES: &str = "https://api.github.com/repos/Baralabite/uberclicker/releases";

fn release_url(path: &str) -> Result<String, String> {
    let asset = regex::Regex::new(r"^releases/([0-9]+\.[0-9]+\.[0-9]+-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)/(manifest\.json|[A-Za-z0-9_-][A-Za-z0-9_.-]*\.bin)$").unwrap();
    let captures = asset
        .captures(path)
        .filter(|c| c[1].len() <= 31 && path.len() <= 180)
        .ok_or("Invalid firmware release path")?;
    Ok(format!(
        "https://github.com/Baralabite/uberclicker/releases/download/v{}/{}",
        &captures[1], &captures[2]
    ))
}

fn release_entry(release: &Value) -> Option<Value> {
    if release["draft"] != false || release["prerelease"] != true {
        return None;
    }
    let version = release["tag_name"].as_str()?.strip_prefix('v')?;
    release_url(&format!("releases/{version}/manifest.json")).ok()?;
    let manifest = release["assets"]
        .as_array()?
        .iter()
        .find(|a| a["name"] == "manifest.json" && a["state"] == "uploaded")?;
    let hash = manifest["digest"].as_str()?.strip_prefix("sha256:")?;
    if hash.len() != 64
        || !hash
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
    {
        return None;
    }
    Some(
        json!({"version": version, "channel": "prerelease", "manifestSha256": hash,
        "publishedAt": release["published_at"], "releaseUrl": release["html_url"]}),
    )
}

async fn download(client: &reqwest::Client, url: &str) -> Result<Vec<u8>, String> {
    let mut response = client
        .get(url)
        .send()
        .await
        .map_err(|e| format!("Firmware download failed: {e}"))?;
    if !response.status().is_success() {
        return Err(format!("Firmware download failed ({}). Check your connection; GitHub rate limits may require waiting before retrying.", response.status()));
    }
    const LIMIT: usize = 4 * 1024 * 1024;
    if response
        .content_length()
        .is_some_and(|size| size > LIMIT as u64)
    {
        return Err("Firmware download exceeds maximum size".into());
    }
    let mut bytes = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(|e| e.to_string())? {
        if bytes.len() + chunk.len() > LIMIT {
            return Err("Firmware download exceeds maximum size".into());
        }
        bytes.extend_from_slice(&chunk);
    }
    Ok(bytes)
}

#[tauri::command]
pub async fn firmware_download(path: String) -> Result<Vec<u8>, String> {
    let client = reqwest::Client::builder()
        .user_agent("UberClicker-Companion")
        .timeout(Duration::from_secs(90))
        .https_only(true)
        .redirect(reqwest::redirect::Policy::limited(5))
        .build()
        .map_err(|e| e.to_string())?;
    if path != "releases/index.json" {
        return download(&client, &release_url(&path)?).await;
    }
    // GitHub supplies the manifest digest; the shared installer verifies it before opening USB.
    let mut entries = Vec::new();
    for page in 1.. {
        let raw = download(&client, &format!("{RELEASES}?per_page=100&page={page}")).await?;
        let releases: Vec<Value> =
            serde_json::from_slice(&raw).map_err(|_| "Invalid GitHub release index")?;
        entries.extend(releases.iter().filter_map(release_entry));
        if releases.len() < 100 {
            break;
        }
    }
    entries.sort_by(|a, b| b["publishedAt"].as_str().cmp(&a["publishedAt"].as_str()));
    serde_json::to_vec(&json!({"indexVersion": 1, "releases": entries})).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn downloads_only_published_firmware_paths() {
        for path in [
            "releases/0.1.0-ci.42.1/manifest.json",
            "releases/0.1.0-ci.42.1/boot_app0.bin",
        ] {
            assert!(release_url(path)
                .unwrap()
                .starts_with("https://github.com/Baralabite/uberclicker/releases/download/v"));
        }
        for path in [
            "https://evil.test/a.bin",
            "releases/../secret.bin",
            "releases/0.1.0-test/../../a.bin",
            "releases/0.1.0-test/a.bin?redirect=evil",
            "releases/0.1.0-test/a.bin#fragment",
            "releases/0.1.0-test/%2e%2e.bin",
            "releases/0.1.0-test/firmware-source.zip",
        ] {
            assert!(release_url(path).is_err(), "{path}");
        }
    }
    #[test]
    fn index_excludes_desktop_drafts_stable_and_unverified_assets() {
        let release = json!({"tag_name":"v0.1.0-ci.42.1", "draft":false, "prerelease":true,
            "assets":[{"name":"manifest.json", "state":"uploaded", "digest":format!("sha256:{}", "a".repeat(64))}]});
        assert_eq!(release_entry(&release).unwrap()["version"], "0.1.0-ci.42.1");
        for (key, value) in [
            ("draft", json!(true)),
            ("prerelease", json!(false)),
            ("tag_name", json!("companion-build-42")),
            ("tag_name", json!("v1.0.0")),
            ("assets", json!([])),
            (
                "assets",
                json!([{"name":"manifest.json", "state":"uploaded", "digest":null}]),
            ),
        ] {
            let mut bad = release.clone();
            bad[key] = value;
            assert!(release_entry(&bad).is_none());
        }
    }
}
