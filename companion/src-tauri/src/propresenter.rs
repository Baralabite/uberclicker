use regex::{Regex, RegexBuilder};
use reqwest::blocking::Client;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{
    collections::HashSet,
    net::IpAddr,
    time::{Duration, Instant},
};

#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Assignment {
    pub transmitter_id: String,
    pub name: String,
    pub presentation_ids: Vec<String>,
    #[serde(default)]
    pub name_rules: Vec<NameRule>,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct NameRule {
    pub kind: RuleKind,
    pub pattern: String,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RuleKind {
    Contains,
    Regex,
}
impl NameRule {
    fn compile(&self) -> Result<Regex, String> {
        if self.pattern.trim().is_empty() || self.pattern.len() > 512 {
            return Err("Name rules need a non-blank pattern of at most 512 UTF-8 bytes".into());
        }
        let (pattern, ignore_case) = match self.kind {
            RuleKind::Contains => (regex::escape(&self.pattern), true),
            RuleKind::Regex => (self.pattern.clone(), false),
        };
        RegexBuilder::new(&pattern)
            .case_insensitive(ignore_case)
            .size_limit(1_048_576)
            .build()
            .map_err(|e| format!("Invalid name rule: {e}"))
    }
}
impl Assignment {
    fn allows(&self, id: &str, name: Option<&str>) -> bool {
        self.presentation_ids
            .iter()
            .any(|allowed| allowed.eq_ignore_ascii_case(id))
            || name
                .filter(|name| !name.trim().is_empty())
                .is_some_and(|name| {
                    self.name_rules
                        .iter()
                        .any(|rule| rule.compile().is_ok_and(|pattern| pattern.is_match(name)))
                })
    }
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Config {
    pub enabled: bool,
    pub host: String,
    pub port: u16,
    pub assignments: Vec<Assignment>,
}
impl Default for Config {
    fn default() -> Self {
        Self {
            enabled: false,
            host: "127.0.0.1".into(),
            port: 50001,
            assignments: vec![],
        }
    }
}
fn uuid(value: &str) -> bool {
    value.len() == 36
        && value.bytes().enumerate().all(|(i, b)| {
            if [8, 13, 18, 23].contains(&i) {
                b == b'-'
            } else {
                b.is_ascii_hexdigit()
            }
        })
}
impl Config {
    pub fn validate(&self) -> Result<(), String> {
        if (self.host != "localhost" && self.host.parse::<IpAddr>().is_err()) || self.port == 0 {
            return Err(
                "ProPresenter needs localhost or an IP address and a port from 1–65535".into(),
            );
        }
        let mut transmitters = HashSet::new();
        if self.assignments.len() > 64 {
            return Err("Too many transmitter mappings".into());
        }
        for a in &self.assignments {
            if a.name_rules.len() > 16 {
                return Err("Use at most 16 name rules per transmitter".into());
            }
            for (index, rule) in a.name_rules.iter().enumerate() {
                rule.compile()
                    .map_err(|e| format!("{} — name rule {}: {e}", a.name, index + 1))?;
            }
            if !crate::model::is_hex(&a.transmitter_id, 12)
                || !transmitters.insert(&a.transmitter_id)
                || a.name.trim().is_empty()
                || a.name.len() > 80
                || a.presentation_ids.len() > 4096
                || a.presentation_ids.iter().any(|id| !uuid(id))
            {
                return Err(
                    "Use a unique 12-character transmitter ID, a name, and valid presentation IDs"
                        .into(),
                );
            }
        }
        Ok(())
    }
    fn base(&self) -> String {
        let host = if self.host.contains(':') {
            format!("[{}]", self.host)
        } else {
            self.host.clone()
        };
        format!("http://{host}:{}/", self.port)
    }
    fn assignment(&self, transmitter: &str) -> Option<&Assignment> {
        self.assignments
            .iter()
            .find(|a| a.transmitter_id == transmitter)
    }
}
fn client() -> Result<Client, String> {
    Client::builder()
        .no_proxy()
        .redirect(reqwest::redirect::Policy::none())
        .timeout(Duration::from_secs(3))
        .build()
        .map_err(|_| "Could not create ProPresenter connection".into())
}
fn read(client: &Client, url: &str, timeout: Duration) -> Result<Value, String> {
    let error = "Could not read ProPresenter. Check its Network settings, host and port.";
    let response = client.get(url).timeout(timeout).send().map_err(|_| error)?;
    if !response.status().is_success() {
        return Err(error.into());
    }
    response.json().map_err(|_| error.into())
}
#[derive(Serialize)]
pub struct Presentation {
    pub id: String,
    pub name: String,
    pub library: String,
}
fn presentations(config: &Config) -> Result<Vec<Presentation>, String> {
    config.validate()?;
    let client = client()?;
    let libraries = read(
        &client,
        &format!("{}v1/libraries", config.base()),
        Duration::from_secs(3),
    )?;
    let mut result = Vec::new();
    let mut seen = HashSet::new();
    for library in libraries
        .as_array()
        .ok_or("Invalid ProPresenter library list")?
    {
        let name = library["id"]["name"]
            .as_str()
            .ok_or("Invalid ProPresenter library name")?;
        let mut url =
            reqwest::Url::parse(&config.base()).map_err(|_| "Invalid ProPresenter address")?;
        url.path_segments_mut()
            .map_err(|_| "Invalid ProPresenter address")?
            .extend(["v1", "library", name]);
        let items = read(&client, url.as_str(), Duration::from_secs(3))?;
        for item in items["items"]
            .as_array()
            .ok_or("Invalid ProPresenter presentation list")?
        {
            let id = item["uuid"]
                .as_str()
                .filter(|id| uuid(id))
                .ok_or("Invalid ProPresenter presentation ID")?;
            let title = item["name"]
                .as_str()
                .ok_or("Invalid ProPresenter presentation name")?;
            if seen.insert(id.to_ascii_lowercase()) {
                result.push(Presentation {
                    id: id.into(),
                    name: title.into(),
                    library: name.into(),
                });
            }
        }
    }
    Ok(result)
}
#[tauri::command]
pub async fn propresenter_presentations(config: Config) -> Result<Vec<Presentation>, String> {
    tauri::async_runtime::spawn_blocking(move || presentations(&config))
        .await
        .map_err(|_| "ProPresenter request failed")?
}

// Always target the authorized UUID: a live presentation change must never redirect a click to another deck.
pub fn send(
    config: &Config,
    transmitter: &str,
    button: &str,
    deadline: Instant,
) -> (&'static str, String) {
    let rejected = |message: &str| ("rejected", message.to_string());
    if !["previous", "next"].contains(&button) {
        return rejected("Invalid ProPresenter button");
    }
    let Some(assignment) = config
        .assignment(transmitter)
        .filter(|a| !a.presentation_ids.is_empty() || !a.name_rules.is_empty())
    else {
        return rejected("This transmitter has no assigned ProPresenter presentations");
    };
    let client = match client() {
        Ok(c) => c,
        Err(e) => return ("rejected", e),
    };
    let Some(remaining) = deadline
        .checked_duration_since(Instant::now())
        .filter(|d| !d.is_zero())
    else {
        return rejected("Click expired");
    };
    let active = match read(
        &client,
        &format!("{}v1/presentation/active", config.base()),
        remaining,
    ) {
        Ok(v) => v,
        Err(e) => return ("rejected", e),
    };
    let Some(id) = active["presentation"]["id"]["uuid"]
        .as_str()
        .filter(|id| uuid(id))
    else {
        return rejected("No live ProPresenter presentation");
    };
    if !assignment.allows(id, active["presentation"]["id"]["name"].as_str()) {
        return rejected("This transmitter cannot control the live presentation");
    }
    let Some(remaining) = deadline
        .checked_duration_since(Instant::now())
        .filter(|d| !d.is_zero())
    else {
        return rejected("Click expired");
    };
    match client
        .get(format!(
            "{}v1/presentation/{id}/{button}/trigger",
            config.base()
        ))
        .timeout(remaining)
        .send()
    {
        Ok(response) if response.status().is_success() => ("submitted", String::new()),
        // Never retry a trigger: a lost response does not mean the cue did not fire.
        _ => (
            "unknown",
            "ProPresenter did not confirm the slide change. Output paused.".into(),
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        io::{Read, Write},
        net::TcpListener,
        thread,
    };
    const DECK: &str = "942C3FC3-C4B2-44F7-A55D-4CC913BB8A5D";
    const OTHER: &str = "942C3FC3-C4B2-44F7-A55D-4CC913BB8A5E";
    const TX: &str = "aabbccddeeff";
    fn config() -> Config {
        Config {
            enabled: true,
            assignments: vec![Assignment {
                transmitter_id: TX.into(),
                name: "Tx 1".into(),
                presentation_ids: vec![DECK.to_lowercase()],
                name_rules: vec![],
            }],
            ..Default::default()
        }
    }
    fn server(steps: Vec<(String, String)>) -> (u16, thread::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        listener.set_nonblocking(true).unwrap();
        let port = listener.local_addr().unwrap().port();
        let handle = thread::spawn(move || {
            for (path, response) in steps {
                let start = Instant::now();
                let mut stream = loop {
                    if let Ok((stream, _)) = listener.accept() {
                        break stream;
                    }
                    assert!(
                        start.elapsed() < Duration::from_secs(3),
                        "Missing request: {path}"
                    );
                    thread::sleep(Duration::from_millis(2));
                };
                stream.set_nonblocking(false).unwrap();
                stream
                    .set_read_timeout(Some(Duration::from_secs(2)))
                    .unwrap();
                let mut request = Vec::new();
                while !request.ends_with(b"\r\n\r\n") {
                    let mut byte = [0];
                    assert_eq!(stream.read(&mut byte).unwrap(), 1);
                    request.push(byte[0]);
                }
                assert_eq!(
                    String::from_utf8(request).unwrap().lines().next().unwrap(),
                    format!("GET {path} HTTP/1.1")
                );
                if response == "DROP" {
                    continue;
                }
                write!(stream, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}", response.len(), response).unwrap();
            }
        });
        (port, handle)
    }
    fn live(id: &str) -> String {
        serde_json::json!({"presentation":{"id":{"uuid":id}}}).to_string()
    }
    #[test]
    fn loads_libraries_and_preserves_distinct_presentations_with_same_name() {
        let (port, server) = server(vec![
            ("/v1/libraries".into(), r#"[{"id":{"name":"Sunday / AM"}}]"#.into()),
            ("/v1/library/Sunday%20%2F%20AM".into(), serde_json::json!({"items":[{"uuid":DECK,"name":"Sermon"},{"uuid":OTHER,"name":"Sermon"}]}).to_string()),
        ]);
        let list = presentations(&Config { port, ..config() }).unwrap();
        assert_eq!(list.len(), 2);
        assert_eq!(list[0].library, "Sunday / AM");
        assert_ne!(list[0].id, list[1].id);
        server.join().unwrap();
    }
    #[test]
    fn only_triggers_explicitly_allowed_live_uuid_in_both_directions() {
        for button in ["previous", "next"] {
            let (port, server) = server(vec![
                ("/v1/presentation/active".into(), live(DECK)),
                (
                    format!("/v1/presentation/{DECK}/{button}/trigger"),
                    "".into(),
                ),
            ]);
            assert_eq!(
                send(
                    &Config { port, ..config() },
                    TX,
                    button,
                    Instant::now() + Duration::from_millis(750)
                )
                .0,
                "submitted"
            );
            server.join().unwrap();
        }
    }
    #[test]
    fn blocks_unassigned_live_decks_missing_live_state_and_malformed_responses() {
        for response in [live(OTHER), "{}".into(), "not json".into()] {
            let (port, server) = server(vec![("/v1/presentation/active".into(), response)]);
            assert_eq!(
                send(
                    &Config { port, ..config() },
                    TX,
                    "next",
                    Instant::now() + Duration::from_millis(750)
                )
                .0,
                "rejected"
            );
            server.join().unwrap();
        }
    }
    #[test]
    fn unknown_transmitters_empty_assignments_and_expired_clicks_never_connect() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        listener.set_nonblocking(true).unwrap();
        let mut config = Config {
            port: listener.local_addr().unwrap().port(),
            ..config()
        };
        let deadline = Instant::now() + Duration::from_millis(750);
        for transmitter in ["", "112233445566"] {
            assert_eq!(send(&config, transmitter, "next", deadline).0, "rejected");
        }
        assert_eq!(
            send(
                &config,
                TX,
                "next",
                Instant::now() - Duration::from_millis(1)
            )
            .0,
            "rejected"
        );
        config.assignments[0].presentation_ids.clear();
        assert_eq!(send(&config, TX, "next", deadline).0, "rejected");
        assert_eq!(
            listener.accept().unwrap_err().kind(),
            std::io::ErrorKind::WouldBlock
        );
    }
    #[test]
    fn stalled_status_request_expires_without_triggering() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            stream
                .set_read_timeout(Some(Duration::from_secs(2)))
                .unwrap();
            let mut bytes = [0; 1024];
            assert!(stream.read(&mut bytes).unwrap() > 0);
            thread::sleep(Duration::from_millis(150));
            listener.set_nonblocking(true).unwrap();
            assert_eq!(
                listener.accept().unwrap_err().kind(),
                std::io::ErrorKind::WouldBlock
            );
        });
        let start = Instant::now();
        assert_eq!(
            send(
                &Config { port, ..config() },
                TX,
                "next",
                start + Duration::from_millis(50)
            )
            .0,
            "rejected"
        );
        assert!(start.elapsed() < Duration::from_millis(500));
        server.join().unwrap();
    }
    #[test]
    fn uncertain_trigger_is_not_retried() {
        let (port, server) = server(vec![
            ("/v1/presentation/active".into(), live(DECK)),
            (
                format!("/v1/presentation/{DECK}/next/trigger"),
                "DROP".into(),
            ),
        ]);
        assert_eq!(
            send(
                &Config { port, ..config() },
                TX,
                "next",
                Instant::now() + Duration::from_millis(750)
            )
            .0,
            "unknown"
        );
        server.join().unwrap();
    }
    fn name_rule(kind: RuleKind, pattern: &str) -> NameRule {
        NameRule {
            kind,
            pattern: pattern.into(),
        }
    }
    #[test]
    fn name_rules_are_additive_literal_or_regex_and_deny_missing_names() {
        let mut a = config().assignments.remove(0);
        a.name_rules = vec![
            name_rule(RuleKind::Contains, "Sermon [AM]"),
            name_rule(RuleKind::Regex, r"^Sunday [0-9]+$"),
        ];
        assert!(a.allows(DECK, None)); // Explicit UUID selections still work without a name.
        assert!(a.allows(OTHER, Some("Weekly SERMON [am] slides")));
        assert!(!a.allows(OTHER, Some("Sermon A"))); // Contains is not regex.
        assert!(a.allows(OTHER, Some("Sunday 42")));
        assert!(!a.allows(OTHER, Some("sunday 42")));
        assert!(!a.allows(OTHER, Some("Sunday 42 extra")));
        assert!(!a.allows(OTHER, None));
        a.name_rules = vec![name_rule(RuleKind::Regex, r"(?i)^sunday (am|pm)$")];
        assert!(a.allows(OTHER, Some("SUNDAY PM")));
        assert!(!a.allows(OTHER, Some("Monday PM")));
        a.name_rules = vec![name_rule(RuleKind::Regex, ".*")];
        assert!(!a.allows(OTHER, None));
        assert!(!a.allows(OTHER, Some("")));
        a.name_rules = vec![name_rule(RuleKind::Regex, "[")];
        assert!(!a.allows(OTHER, Some("Anything")));
    }
    #[test]
    fn name_only_assignments_use_current_name_without_catalog_or_uuid_selection() {
        for (kind, pattern) in [
            (RuleKind::Contains, "sermon"),
            (RuleKind::Regex, "(?i)^sermon .*$"),
        ] {
            let live_name = |name: &str| {
                serde_json::json!({"presentation":{"id":{"uuid":OTHER,"name":name}}}).to_string()
            };
            let (port, server) = server(vec![
                (
                    "/v1/presentation/active".into(),
                    live_name("Sermon November"),
                ),
                (format!("/v1/presentation/{OTHER}/next/trigger"), "".into()),
                ("/v1/presentation/active".into(), live_name("Announcements")),
            ]);
            let mut c = Config { port, ..config() };
            c.assignments[0].presentation_ids.clear();
            c.assignments[0].name_rules = vec![name_rule(kind, pattern)];
            assert!(c.validate().is_ok());
            assert_eq!(
                send(&c, TX, "next", Instant::now() + Duration::from_millis(750)).0,
                "submitted"
            );
            assert_eq!(
                send(&c, TX, "next", Instant::now() + Duration::from_millis(750)).0,
                "rejected"
            );
            server.join().unwrap();
        }
    }
    #[test]
    fn validates_patterns_and_bounds_rules_before_saving() {
        let mut c = config();
        for pattern in ["", "   ", "[", r"(?=sermon)", r"(a)\1", r"\w{10000}"] {
            c.assignments[0].name_rules = vec![name_rule(RuleKind::Regex, pattern)];
            assert!(
                c.validate().unwrap_err().contains("Tx 1 — name rule 1"),
                "{pattern}"
            );
        }
        c.assignments[0].name_rules = vec![name_rule(RuleKind::Contains, &"a".repeat(513))];
        assert!(c.validate().is_err());
        c.assignments[0].name_rules = vec![name_rule(RuleKind::Contains, "sermon"); 17];
        assert!(c.validate().is_err());
        assert!(
            serde_json::from_str::<NameRule>(r#"{"kind":"invalid","pattern":"sermon"}"#).is_err()
        );
    }
    #[test]
    fn legacy_assignments_default_to_no_rules_and_rules_persist() {
        let c = config();
        let mut legacy = serde_json::to_value(&c).unwrap();
        legacy["assignments"][0]
            .as_object_mut()
            .unwrap()
            .remove("nameRules");
        let mut restored: Config = serde_json::from_value(legacy).unwrap();
        assert!(restored.assignments[0].name_rules.is_empty());
        assert!(restored.assignments[0].allows(DECK, None));
        assert!(!restored.assignments[0].allows(OTHER, Some("Sermon")));
        restored.assignments[0].name_rules = vec![name_rule(RuleKind::Regex, r"(?i)^sermon\b")];
        let saved: Config =
            serde_json::from_str(&serde_json::to_string(&restored).unwrap()).unwrap();
        assert_eq!(saved.assignments[0].name_rules[0].pattern, r"(?i)^sermon\b");
        assert!(saved.assignments[0].allows(OTHER, Some("Sermon 1")));
    }
    #[test]
    fn rejects_duplicate_transmitters_and_invalid_ids_and_migrates_settings() {
        let mut c = config();
        assert!(c.validate().is_ok());
        c.assignments.push(c.assignments[0].clone());
        assert!(c.validate().is_err());
        c.assignments.pop();
        c.assignments[0].presentation_ids = vec!["active".into()];
        assert!(c.validate().is_err());
        let mut legacy = serde_json::to_value(crate::model::Settings::default()).unwrap();
        legacy.as_object_mut().unwrap().remove("proPresenter");
        let settings: crate::model::Settings = serde_json::from_value(legacy).unwrap();
        assert!(!settings.pro_presenter.enabled);
        assert!(settings.next.keyboard.enabled);
        let mut settings = settings;
        settings.pro_presenter = config();
        let saved: crate::model::Settings =
            serde_json::from_str(&serde_json::to_string(&settings).unwrap()).unwrap();
        assert_eq!(saved.pro_presenter.assignments[0].transmitter_id, TX);
    }
}
