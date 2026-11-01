use serde::{Deserialize, Serialize};
use std::{
    collections::VecDeque,
    net::IpAddr,
    time::{Duration, Instant},
};

pub const API: &str = "https://api.uberclicker.com";
#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Credentials {
    pub v: u8,
    pub room: String,
    pub tx_token: String,
    pub client_token: String,
}
pub fn is_hex(value: &str, len: usize) -> bool {
    value.len() == len
        && value
            .bytes()
            .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}
impl Credentials {
    pub fn validate(&self) -> Result<(), String> {
        if self.v != 1
            || !is_hex(&self.room, 32)
            || !is_hex(&self.tx_token, 64)
            || !is_hex(&self.client_token, 64)
        {
            return Err("Invalid cloud credentials".into());
        }
        Ok(())
    }
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Keyboard {
    pub enabled: bool,
    pub key: String,
    pub modifiers: Vec<String>,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Osc {
    pub enabled: bool,
    pub host: String,
    pub port: u16,
    pub address: String,
    pub value: i32,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Mapping {
    pub keyboard: Keyboard,
    pub osc: Osc,
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
pub struct Settings {
    #[serde(default)]
    pub pro_presenter: crate::propresenter::Config,
    #[serde(default = "default_output_on_startup")]
    pub output_on_startup: bool,
    pub previous: Mapping,
    pub next: Mapping,
}
fn default_output_on_startup() -> bool {
    true
}
impl Default for Settings {
    fn default() -> Self {
        let mapping = |key: &str, address: &str| Mapping {
            keyboard: Keyboard {
                enabled: true,
                key: key.into(),
                modifiers: vec![],
            },
            osc: Osc {
                enabled: false,
                host: "127.0.0.1".into(),
                port: 8000,
                address: address.into(),
                value: 1,
            },
        };
        Self {
            output_on_startup: true,
            pro_presenter: Default::default(),
            previous: mapping("LeftArrow", "/previous"),
            next: mapping("RightArrow", "/next"),
        }
    }
}
impl Settings {
    pub fn validate(&self) -> Result<(), String> {
        self.pro_presenter.validate()?;
        for m in [&self.previous, &self.next] {
            crate::output::key(&m.keyboard.key)?;
            if m.keyboard.modifiers.len() > 4
                || m.keyboard
                    .modifiers
                    .iter()
                    .any(|k| !["Control", "Alt", "Shift", "Meta"].contains(&k.as_str()))
            {
                return Err("Invalid keyboard modifiers".into());
            }
            let mut modifiers = m.keyboard.modifiers.clone();
            modifiers.sort();
            modifiers.dedup();
            if modifiers.len() != m.keyboard.modifiers.len() {
                return Err("Duplicate keyboard modifier".into());
            }
            if m.osc.host != "localhost" && m.osc.host.parse::<IpAddr>().is_err() {
                return Err("OSC host must be localhost or an IP address".into());
            }
            if m.osc.port == 0
                || !m.osc.address.starts_with('/')
                || m.osc.address.len() > 64
                || !m
                    .osc
                    .address
                    .bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b"/_-".contains(&b))
            {
                return Err("OSC needs a port from 1–65535 and an address such as /next".into());
            }
        }
        Ok(())
    }
}
#[derive(Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Status {
    pub connected: bool,
    pub armed: bool,
    pub room: String,
    pub received: u64,
    pub last_event: String,
    pub error: String,
    pub relay_rtt_ms: Option<u64>,
    pub last_transmitter: String,
}

pub struct Clock {
    pub server_ms: i64,
    pub local: Instant,
    pub synced: bool,
}
impl Default for Clock {
    fn default() -> Self {
        Self {
            server_ms: 0,
            local: Instant::now(),
            synced: false,
        }
    }
}
impl Clock {
    pub fn sync(&mut self, server_ms: i64, started: Instant) -> bool {
        let rtt = started.elapsed();
        if rtt > Duration::from_millis(500) {
            self.synced = false;
            return false;
        }
        self.server_ms = server_ms + rtt.as_millis() as i64 / 2;
        self.local = Instant::now();
        self.synced = true;
        true
    }
    pub fn deadline(&self, sent: i64, expires: i64) -> Option<Instant> {
        if !self.synced || expires.checked_sub(sent)? != 750 {
            return None;
        }
        let now = self
            .server_ms
            .checked_add(self.local.elapsed().as_millis() as i64)?;
        let remaining = expires.checked_sub(now)?;
        if sent > now + 100 || remaining <= 0 || remaining > 850 {
            return None;
        }
        Some(Instant::now() + Duration::from_millis(remaining as u64))
    }
}
#[derive(Default)]
pub struct Seen(VecDeque<String>);
impl Seen {
    pub fn accept(&mut self, id: &str) -> bool {
        let Some((session, seq)) = id.split_once(':') else {
            return false;
        };
        if !is_hex(session, 32)
            || seq.parse::<u32>().map_or(true, |n| n == 0)
            || self.0.iter().any(|old| old == id)
        {
            return false;
        }
        self.0.push_back(id.into());
        if self.0.len() > 256 {
            self.0.pop_front();
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn startup_preference_defaults_on_and_preserves_existing_mappings() {
        let mut settings = Settings::default();
        assert!(settings.output_on_startup);
        settings.next.osc.port = 9000;
        let mut legacy = serde_json::to_value(&settings).unwrap();
        legacy.as_object_mut().unwrap().remove("outputOnStartup");
        let restored: Settings = serde_json::from_value(legacy).unwrap();
        assert!(restored.output_on_startup);
        assert_eq!(restored.next.osc.port, 9000);
        settings.output_on_startup = false;
        let restored: Settings =
            serde_json::from_str(&serde_json::to_string(&settings).unwrap()).unwrap();
        assert!(!restored.output_on_startup);
    }
    #[test]
    fn reject_stale_future_and_duplicates() {
        let mut clock = Clock::default();
        assert!(clock.deadline(1000, 1750).is_none());
        assert!(clock.sync(1000, Instant::now()));
        assert!(clock.deadline(1000, 1750).is_some());
        assert!(clock.deadline(0, 750).is_none());
        assert!(clock.deadline(2000, 2750).is_none());
        assert!(clock.deadline(i64::MIN, i64::MAX).is_none());
        let mut seen = Seen::default();
        let id = format!("{}:1", "a".repeat(32));
        assert!(seen.accept(&id));
        assert!(!seen.accept(&id));
        assert!(!seen.accept("bad"));
    }
    #[test]
    fn mappings_validate_before_save() {
        let mut s = Settings::default();
        assert!(s.validate().is_ok());
        s.next.osc.address = "/next\0/injected".into();
        assert!(s.validate().is_err());
        s = Settings::default();
        s.next.keyboard.key = "RunShellCommand".into();
        assert!(s.validate().is_err());
        s = Settings::default();
        s.next.osc.host = "example.com".into();
        assert!(s.validate().is_err());
    }
}
