mod model;
mod output;
mod propresenter;
mod relay;
mod serial;
#[cfg(test)]
mod startup_tests;

use model::{Credentials, Settings, Status, API};
use serde::Serialize;
use std::{
    fs,
    path::PathBuf,
    sync::{Arc, Mutex},
};
use tauri::{
    menu::{Menu, MenuItem},
    tray::TrayIconBuilder,
    Manager, State,
};
use tauri_plugin_autostart::ManagerExt;
use tokio::sync::watch;

pub struct Shared {
    pub settings: Settings,
    pub status: Status,
    pub epoch: u64,
    pub startup_pending: bool,
}
impl Shared {
    fn set_armed(&mut self, armed: bool) -> Result<(), String> {
        self.startup_pending = false;
        if armed && !self.status.connected {
            return Err("Wait for a cloud connection before enabling output".into());
        }
        if armed
            && !self.settings.pro_presenter.enabled
            && (self.settings.previous.keyboard.enabled || self.settings.next.keyboard.enabled)
        {
            enigo::Enigo::new(&enigo::Settings::default()).map_err(|_| "Keyboard unavailable. Grant Accessibility permission on macOS or check input support on your Linux desktop.")?;
        }
        self.status.armed = armed;
        self.status.error.clear();
        self.epoch += 1;
        Ok(())
    }

    fn connected(&mut self) {
        self.status.connected = true;
        self.status.error.clear();
        if self.startup_pending {
            if let Err(error) = self.set_armed(self.settings.output_on_startup) {
                self.status.error = error;
            }
        }
    }
}
pub struct Client {
    pub shared: Arc<Mutex<Shared>>,
    pub commands: watch::Sender<Option<Credentials>>,
    pub path: PathBuf,
}
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct View {
    settings: Settings,
    status: Status,
    api: &'static str,
}
fn entry() -> Result<keyring::Entry, String> {
    keyring::Entry::new("com.uberclicker.companion", "cloud")
        .map_err(|_| "Operating system credential store is unavailable".into())
}
fn saved_credentials() -> Result<Option<Credentials>, String> {
    match entry()?.get_password() {
        Ok(text) => {
            let c: Credentials =
                serde_json::from_str(&text).map_err(|_| "Saved cloud credentials are invalid")?;
            c.validate()?;
            Ok(Some(c))
        }
        Err(keyring::Error::NoEntry) => Ok(None),
        Err(_) => Err("Unlock your operating system credential store and reconnect".into()),
    }
}
#[tauri::command]
fn view(client: State<Client>) -> View {
    let s = client.shared.lock().unwrap();
    View {
        settings: s.settings.clone(),
        status: s.status.clone(),
        api: API,
    }
}
#[tauri::command]
fn save_settings(settings: Settings, client: State<Client>) -> Result<(), String> {
    settings.validate()?;
    let mut s = client.shared.lock().unwrap();
    if s.status.armed {
        return Err("Pause output before changing mappings".into());
    }
    write_settings(&settings, &client)?;
    s.settings = settings;
    s.startup_pending = false;
    s.epoch += 1;
    Ok(())
}
fn write_settings(settings: &Settings, client: &Client) -> Result<(), String> {
    fs::create_dir_all(client.path.parent().unwrap())
        .map_err(|_| "Could not create settings folder")?;
    let bytes = serde_json::to_vec_pretty(&settings).map_err(|_| "Could not encode settings")?;
    fs::write(client.path.with_extension("tmp"), bytes).map_err(|_| "Could not write settings")?;
    fs::rename(client.path.with_extension("tmp"), &client.path)
        .map_err(|_| "Could not save settings")?;
    Ok(())
}
#[tauri::command]
fn set_output_on_startup(enabled: bool, client: State<Client>) -> Result<(), String> {
    let mut s = client.shared.lock().unwrap();
    let mut settings = s.settings.clone();
    settings.output_on_startup = enabled;
    write_settings(&settings, &client)?;
    s.settings = settings;
    if !enabled {
        s.startup_pending = false;
    }
    Ok(())
}
#[tauri::command]
fn autostart_enabled(app: tauri::AppHandle) -> Result<bool, String> {
    app.autolaunch()
        .is_enabled()
        .map_err(|e| format!("Could not read Start on boot: {e}"))
}
#[tauri::command]
fn set_start_on_boot(enabled: bool, app: tauri::AppHandle) -> Result<bool, String> {
    let manager = app.autolaunch();
    // Avoid trying to remove a Windows registry entry that does not exist.
    if manager
        .is_enabled()
        .map_err(|e| format!("Could not read Start on boot: {e}"))?
        != enabled
    {
        if enabled {
            manager.enable()
        } else {
            manager.disable()
        }
        .map_err(|e| format!("Could not change Start on boot: {e}"))?;
    }
    autostart_enabled(app)
}
#[tauri::command]
fn import_room(code: String, client: State<'_, Client>) -> Result<(), String> {
    if client.shared.lock().unwrap().status.armed {
        return Err("Pause output before joining a channel".into());
    }
    if code.len() > 512 {
        return Err("Invalid Client Companion code".into());
    }
    let credentials: Credentials = serde_json::from_str(&code)
        .map_err(|_| "Paste the Client Companion code from app.uberclicker.com")?;
    store_and_connect(credentials, &client)
}
fn store_and_connect(credentials: Credentials, client: &Client) -> Result<(), String> {
    credentials.validate()?;
    entry()?
        .set_password(
            &serde_json::to_string(&credentials).map_err(|_| "Could not encode credentials")?,
        )
        .map_err(|_| "Could not save credentials in your operating system credential store")?;
    {
        let mut s = client.shared.lock().unwrap();
        s.status.armed = false;
        s.startup_pending = false;
        s.status.connected = false;
        s.status.room = credentials.room.clone();
        s.status.error.clear();
        s.epoch += 1;
    }
    client.commands.send_replace(Some(credentials));
    Ok(())
}
#[tauri::command]
fn reconnect(client: State<Client>) -> Result<(), String> {
    let credentials = saved_credentials()?.ok_or(
        "Create a Cloud Channel at app.uberclicker.com and paste its Client Companion code first",
    )?;
    {
        let mut s = client.shared.lock().unwrap();
        s.status.armed = false;
        s.startup_pending = false;
        s.epoch += 1;
    }
    client.commands.send_replace(Some(credentials));
    Ok(())
}
#[tauri::command]
fn disconnect(client: State<Client>) {
    {
        let mut s = client.shared.lock().unwrap();
        s.status.armed = false;
        s.startup_pending = false;
        s.status.connected = false;
        s.epoch += 1;
    }
    client.commands.send_replace(None);
}
#[tauri::command]
fn pairing(client: State<Client>) -> Result<String, String> {
    if client.shared.lock().unwrap().status.armed {
        return Err("Pause output before pairing a transmitter".into());
    }
    let c = saved_credentials()?.ok_or(
        "Create a Cloud Channel at app.uberclicker.com and paste its Client Companion code first",
    )?;
    // Only the publisher capability goes to the clicker; it cannot receive or acknowledge clicks.
    Ok(serde_json::json!({"v":1,"room":c.room,"token":c.tx_token}).to_string())
}
#[tauri::command]
fn arm(armed: bool, client: State<Client>) -> Result<(), String> {
    client.shared.lock().unwrap().set_armed(armed)
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _, _| {
            if let Some(w) = app.get_webview_window("main") {
                let _ = w.show();
                let _ = w.set_focus();
            }
        }))
        .plugin(tauri_plugin_autostart::init(
            tauri_plugin_autostart::MacosLauncher::LaunchAgent,
            None,
        ))
        .setup(|app| {
            let path = app.path().app_config_dir()?.join("settings.json");
            let settings = fs::read(&path)
                .ok()
                .and_then(|bytes| serde_json::from_slice::<Settings>(&bytes).ok())
                .filter(|s| s.validate().is_ok())
                .unwrap_or_default();
            let credentials = saved_credentials();
            let status = Status {
                room: credentials
                    .as_ref()
                    .ok()
                    .and_then(|c| c.as_ref())
                    .map(|c| c.room.clone())
                    .unwrap_or_default(),
                error: credentials.as_ref().err().cloned().unwrap_or_default(),
                ..Status::default()
            };
            let shared = Arc::new(Mutex::new(Shared {
                startup_pending: settings.output_on_startup && !status.room.is_empty(),
                settings,
                status,
                epoch: 0,
            }));
            let (commands, receiver) = watch::channel(credentials.unwrap_or_default());
            app.manage(Client {
                shared: shared.clone(),
                commands,
                path,
            });
            app.manage(serial::SerialState::default());
            tauri::async_runtime::spawn(relay::run(shared, receiver));
            let show = MenuItem::with_id(app, "show", "Open UberClicker", true, None::<&str>)?;
            let pause = MenuItem::with_id(app, "pause", "Pause output", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show, &pause, &quit])?;
            #[cfg(target_os = "macos")]
            let tray_icon = tauri::include_image!("icons/tray-icon.png");
            #[cfg(not(target_os = "macos"))]
            let tray_icon = {
                let mut pixels = vec![0u8; 32 * 32 * 4];
                for y in 5..27 {
                    for x in 5..27 {
                        if x < 14 || x > 18 {
                            let i = (y * 32 + x) * 4;
                            pixels[i..i + 4].copy_from_slice(&[30, 74, 125, 255]);
                        }
                    }
                }
                tauri::image::Image::new_owned(pixels, 32, 32)
            };
            TrayIconBuilder::new()
                .icon(tray_icon)
                .icon_as_template(cfg!(target_os = "macos"))
                .tooltip("UberClicker Companion")
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                    "pause" => {
                        let client = app.state::<Client>();
                        let mut s = client.shared.lock().unwrap();
                        s.status.armed = false;
                        s.startup_pending = false;
                        s.epoch += 1;
                    }
                    "quit" => app.exit(0),
                    _ => {}
                })
                .build(app)?;
            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
        })
        .invoke_handler(tauri::generate_handler![
            view,
            save_settings,
            propresenter::propresenter_presentations,
            set_output_on_startup,
            autostart_enabled,
            set_start_on_boot,
            import_room,
            reconnect,
            disconnect,
            pairing,
            arm,
            serial::serial_ports,
            serial::serial_open,
            serial::serial_read,
            serial::serial_write,
            serial::serial_close
        ])
        .run(tauri::generate_context!())
        .expect("Could not start UberClicker Companion");
}
