use super::*;

fn receiver(output_on_startup: bool) -> Shared {
    let mut settings = Settings::default();
    settings.output_on_startup = output_on_startup;
    // State transitions should not require keyboard permissions on the test host.
    settings.previous.keyboard.enabled = false;
    settings.next.keyboard.enabled = false;
    Shared {
        settings,
        status: Status::default(),
        epoch: 0,
        startup_pending: output_on_startup,
    }
}

#[test]
fn startup_enables_once_after_connection_and_reconnect_stays_paused() {
    let mut state = receiver(true);
    assert!(!state.status.armed);
    state.connected();
    assert!(state.status.armed);
    assert!(!state.startup_pending);
    // The relay clears these on disconnection.
    state.status.connected = false;
    state.status.armed = false;
    state.connected();
    assert!(!state.status.armed);
}

#[test]
fn startup_off_and_explicit_pause_stay_paused() {
    let mut state = receiver(false);
    state.connected();
    assert!(!state.status.armed);
    state.set_armed(true).unwrap();
    assert!(state.status.armed);
    state.set_armed(false).unwrap();
    state.connected();
    assert!(!state.status.armed);

    let mut state = receiver(true);
    state.set_armed(false).unwrap();
    state.connected();
    assert!(!state.status.armed);
}

#[test]
fn startup_preference_survives_saving_to_disk() {
    let path =
        std::env::temp_dir().join(format!("uberclicker-settings-{}.json", std::process::id()));
    let state = receiver(false);
    let settings = state.settings.clone();
    let (commands, _) = watch::channel(None);
    let client = Client {
        shared: Arc::new(Mutex::new(state)),
        commands,
        path: path.clone(),
    };
    write_settings(&settings, &client).unwrap();
    let saved: Settings = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    fs::remove_file(path).unwrap();
    assert!(!saved.output_on_startup);
}

#[test]
#[ignore = "Temporarily creates a uniquely named OS login entry; run explicitly or in desktop CI"]
fn autostart_registration_round_trip() {
    use tauri::test::{mock_builder, mock_context, noop_assets, MockRuntime};
    struct Cleanup(tauri::App<MockRuntime>);
    impl Drop for Cleanup {
        fn drop(&mut self) {
            let _ = self.0.autolaunch().disable();
        }
    }
    let app = Cleanup(
        mock_builder()
            .plugin(
                tauri_plugin_autostart::Builder::new()
                    .app_name(format!("UberClicker-Autostart-Test-{}", std::process::id()))
                    .arg("--list")
                    .build(),
            )
            .build(mock_context(noop_assets()))
            .unwrap(),
    );
    let manager = app.0.autolaunch();
    assert!(!manager.is_enabled().unwrap());
    manager.enable().unwrap();
    assert!(manager.is_enabled().unwrap());
    manager.enable().unwrap();
    assert!(manager.is_enabled().unwrap());
    manager.disable().unwrap();
    assert!(!manager.is_enabled().unwrap());
}
