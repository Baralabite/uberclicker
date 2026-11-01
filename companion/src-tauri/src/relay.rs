use crate::{
    model::{Clock, Credentials, Mapping, Seen, API},
    Shared,
};
use enigo::Enigo;
use futures_util::{SinkExt, StreamExt};
use serde_json::{json, Value};
use std::{
    sync::{mpsc, Arc, Mutex},
    time::{Duration, Instant},
};
use tokio::sync::{oneshot, watch};
use tokio_tungstenite::{
    connect_async,
    tungstenite::{client::IntoClientRequest, http::HeaderValue, Message},
};

struct Job {
    epoch: u64,
    deadline: Instant,
    mapping: Mapping,
    pro_presenter: Option<(crate::propresenter::Config, String, String)>,
    reply: oneshot::Sender<(&'static str, String)>,
}
fn output_thread(shared: Arc<Mutex<Shared>>) -> mpsc::SyncSender<Job> {
    let (sender, receiver) = mpsc::sync_channel::<Job>(8);
    std::thread::spawn(move || {
        let mut keyboard = None;
        for job in receiver {
            let mut state = shared.lock().unwrap();
            if !state.status.armed || state.epoch != job.epoch || Instant::now() >= job.deadline {
                let _ = job.reply.send(("rejected", String::new()));
                continue;
            }
            if let Some((config, transmitter, button)) = job.pro_presenter {
                let (outcome, error) =
                    crate::propresenter::send(&config, &transmitter, &button, job.deadline);
                if outcome == "unknown" {
                    state.status.armed = false;
                    state.epoch += 1;
                }
                let _ = job.reply.send((outcome, error));
                continue;
            }
            if job.mapping.keyboard.enabled && keyboard.is_none() {
                match Enigo::new(&enigo::Settings::default()) {
                    Ok(input) => keyboard = Some(input),
                    Err(_) => {
                        state.status.armed = false;
                        state.epoch += 1;
                        let _ = job.reply.send(("rejected", "Keyboard unavailable. Check OS input permissions, then enable output again.".into()));
                        continue;
                    }
                }
            }
            // Permissions/initialization may take time. Recheck freshness immediately before output.
            if Instant::now() >= job.deadline {
                let _ = job.reply.send(("rejected", String::new()));
                continue;
            }
            match crate::output::send(&job.mapping, &mut keyboard) {
                Ok(()) => {
                    let _ = job.reply.send(("submitted", String::new()));
                }
                Err(error) => {
                    state.status.armed = false;
                    state.epoch += 1;
                    keyboard = None;
                    let _ = job.reply.send(("unknown", error));
                }
            }
        }
    });
    sender
}

pub async fn run(shared: Arc<Mutex<Shared>>, mut commands: watch::Receiver<Option<Credentials>>) {
    let output = output_thread(shared.clone());
    let mut backoff = 1;
    loop {
        let credentials = commands.borrow_and_update().clone();
        let Some(credentials) = credentials else {
            if commands.changed().await.is_err() {
                return;
            }
            continue;
        };
        let changed = tokio::select! {
            result = connection(shared.clone(), &credentials, &output) => {
                if let Err(e) = result { shared.lock().unwrap().status.error = e; }
                false
            },
            result = commands.changed() => { if result.is_err() { return; } true }
        };
        {
            let mut s = shared.lock().unwrap();
            s.status.connected = false;
            s.status.armed = false;
            s.epoch += 1;
        }
        if changed {
            backoff = 1;
            continue;
        }
        tokio::select! {
            _ = tokio::time::sleep(Duration::from_secs(backoff)) => {},
            result = commands.changed() => { if result.is_err() { return; } backoff = 1; continue; }
        }
        backoff = (backoff * 2).min(30);
    }
}

async fn connection(
    shared: Arc<Mutex<Shared>>,
    credentials: &Credentials,
    output: &mpsc::SyncSender<Job>,
) -> Result<(), String> {
    let url = format!(
        "{}/v1/rooms/{}/client",
        API.replace("https://", "wss://"),
        credentials.room
    );
    let mut request = url
        .into_client_request()
        .map_err(|_| "Invalid cloud address")?;
    request.headers_mut().insert(
        "Authorization",
        HeaderValue::from_str(&format!("Bearer {}", credentials.client_token))
            .map_err(|_| "Invalid cloud credentials")?,
    );
    let (socket, _) = tokio::time::timeout(Duration::from_secs(15), connect_async(request))
        .await
        .map_err(|_| "Cloud connection timed out")?
        .map_err(|_| "Cloud connection failed. Check your network and reconnect.")?;
    let (mut writer, mut reader) = socket.split();
    let mut clock = Clock::default();
    let mut seen = Seen::default();
    let mut sync: Option<(u64, Instant)> = None;
    let mut heartbeat = tokio::time::interval(Duration::from_secs(20));
    let mut state_poll = tokio::time::interval(Duration::from_millis(50));
    let mut last_message = Instant::now();
    let mut last_sync = Instant::now();
    let mut nonce = 0u64;
    let mut advertised_armed = false;
    loop {
        tokio::select! {
            _ = heartbeat.tick() => {
                if last_message.elapsed() > Duration::from_secs(60) { return Err("Cloud connection stopped responding".into()); }
                writer.send(Message::Text("ping".into())).await.map_err(|_| "Cloud disconnected")?;
            },
            _ = state_poll.tick() => {
                let armed = shared.lock().unwrap().status.armed;
                if armed != advertised_armed {
                    writer.send(Message::Text(json!({"v":1,"type":"arm","armed":armed}).to_string().into())).await.map_err(|_| "Cloud disconnected")?;
                    advertised_armed = armed;
                }
            },
            incoming = reader.next() => {
                let message = incoming.ok_or("Cloud disconnected")?.map_err(|_| "Cloud disconnected")?;
                last_message = Instant::now();
                let text = match message { Message::Text(t) => t, Message::Ping(p) => { writer.send(Message::Pong(p)).await.map_err(|_| "Cloud disconnected")?; continue; }, Message::Close(_) => return Err("Cloud disconnected".into()), _ => continue };
                if text == "pong" { continue; }
                if text.len() > 1024 { return Err("Cloud returned an oversized message".into()); }
                let message: Value = serde_json::from_str(&text).map_err(|_| "Cloud returned invalid data")?;
                if message["v"] != 1 { return Err("Cloud protocol is unsupported".into()); }
                match message["type"].as_str() {
                    Some("ready") => {
                        nonce += 1; sync = Some((nonce, Instant::now())); last_sync = Instant::now();
                        writer.send(Message::Text(json!({"v":1,"type":"sync","nonce":nonce}).to_string().into())).await.map_err(|_| "Cloud disconnected")?;
                    },
                    Some("sync") => {
                        if let Some((expected, start)) = sync {
                            if message["nonce"].as_u64() == Some(expected) {
                                let now = message["now"].as_i64().ok_or("Cloud returned an invalid clock")?;
                                if !clock.sync(now, start) { return Err("Cloud round trip exceeded 500 ms. Reconnecting before accepting clicks.".into()); }
                                sync = None;
                                let mut s = shared.lock().unwrap(); s.connected(); s.status.relay_rtt_ms = Some(start.elapsed().as_millis() as u64);
                            }
                        }
                    },
                    Some("click") => {
                        let id = message["id"].as_str().ok_or("Cloud returned an invalid click")?.to_string();
                        let button = message["button"].as_str().ok_or("Cloud returned an invalid button")?;
                        if button != "previous" && button != "next" { return Err("Cloud returned an invalid button".into()); }
                        // No periodic idle requests: resync on the first real click after a long gap.
                        if last_sync.elapsed() > Duration::from_secs(300) {
                            nonce += 1; let start = Instant::now();
                            writer.send(Message::Text(json!({"v":1,"type":"sync","nonce":nonce}).to_string().into())).await.map_err(|_| "Cloud disconnected")?;
                            let server_now = tokio::time::timeout(Duration::from_millis(500), async {
                                while let Some(reply) = reader.next().await {
                                    let reply = reply.map_err(|_| "Cloud disconnected")?;
                                    if let Message::Text(text) = reply {
                                        if text == "pong" { continue; }
                                        if text.len() > 1024 { return Err("Cloud returned an oversized message"); }
                                        let data: Value = serde_json::from_str(&text).map_err(|_| "Invalid cloud clock reply")?;
                                        if data["v"] == 1 && data["type"] == "sync" && data["nonce"].as_u64() == Some(nonce) {
                                            return data["now"].as_i64().ok_or("Invalid cloud clock");
                                        }
                                        if data["type"] == "click" { return Err("Concurrent clicks during clock synchronization"); }
                                    } else if matches!(reply, Message::Close(_)) { return Err("Cloud disconnected"); }
                                }
                                Err("Cloud disconnected")
                            }).await.map_err(|_| "Clock synchronization timed out")??;
                            if !clock.sync(server_now, start) { return Err("Cloud clock could not be synchronized".into()); }
                            last_sync = Instant::now();
                        }
                        let deadline = message["sentAt"].as_i64().zip(message["expiresAt"].as_i64()).and_then(|(sent, expires)| clock.deadline(sent, expires));
                        let (reply, done) = oneshot::channel();
                        let job = {
                            let mut s = shared.lock().unwrap(); s.status.received += 1;
                            let transmitter = message["transmitterId"].as_str().filter(|id| crate::model::is_hex(id, 12)).unwrap_or("");
                            s.status.last_transmitter = transmitter.into();
                            if s.status.armed && seen.accept(&id) {
                                deadline.map(|deadline| Job { epoch: s.epoch, deadline, pro_presenter: s.settings.pro_presenter.enabled.then(|| (s.settings.pro_presenter.clone(), transmitter.into(), button.into())), mapping: if button == "previous" { s.settings.previous.clone() } else { s.settings.next.clone() }, reply })
                            } else { None }
                        };
                        let mut outcome = "rejected";
                        if let Some(job) = job {
                            if output.try_send(job).is_ok() {
                                if let Ok((status, error)) = done.await {
                                    outcome = status;
                                    shared.lock().unwrap().status.error = error;
                                }
                            }
                        }
                        shared.lock().unwrap().status.last_event = format!("{}: {}", button, outcome);
                        writer.send(Message::Text(json!({"v":1,"type":"ack","id":id,"status":outcome}).to_string().into())).await.map_err(|_| "Acknowledgement could not be sent")?;
                    },
                    _ => {}
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{Settings, Status};
    #[tokio::test]
    async fn restricted_clicks_never_fall_back_to_keyboard_or_osc() {
        let listener = std::net::UdpSocket::bind("127.0.0.1:0").unwrap();
        listener.set_nonblocking(true).unwrap();
        let mut settings = Settings::default();
        settings.pro_presenter.enabled = true;
        settings.next.osc.enabled = true;
        settings.next.osc.port = listener.local_addr().unwrap().port();
        let shared = Arc::new(Mutex::new(Shared {
            settings: settings.clone(),
            status: Status {
                armed: true,
                ..Default::default()
            },
            epoch: 0,
            startup_pending: false,
        }));
        let output = output_thread(shared.clone());
        let (reply, done) = oneshot::channel();
        output
            .send(Job {
                epoch: 0,
                deadline: Instant::now() + Duration::from_millis(750),
                mapping: settings.next,
                pro_presenter: Some((settings.pro_presenter, "aabbccddeeff".into(), "next".into())),
                reply,
            })
            .unwrap();
        let (outcome, error) = done.await.unwrap();
        assert_eq!(outcome, "rejected");
        assert!(error.contains("no assigned"));
        assert!(shared.lock().unwrap().status.armed);
        assert_eq!(
            listener.recv(&mut [0; 64]).unwrap_err().kind(),
            std::io::ErrorKind::WouldBlock
        );
    }
}
