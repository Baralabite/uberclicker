use serde::Serialize;
use serialport::{SerialPort, SerialPortType};
use std::{
    io::{ErrorKind, Read, Write},
    sync::{
        atomic::{AtomicU32, Ordering},
        Arc, Mutex,
    },
    time::Duration,
};
use tauri::State;

struct Connection {
    session: u32,
    port: Box<dyn SerialPort>,
}
#[derive(Default)]
pub struct SerialState {
    connection: Arc<Mutex<Option<Connection>>>,
    next: AtomicU32,
}
#[derive(Serialize)]
pub struct PortInfo {
    path: String,
    label: String,
}

fn ports() -> Result<Vec<PortInfo>, String> {
    let mut ports = serialport::available_ports()
        .map_err(|_| "Could not list USB serial devices")?
        .into_iter()
        .filter_map(|port| {
            let label = match port.port_type {
                SerialPortType::UsbPort(usb) => format!(
                    "{} — {}",
                    usb.product.unwrap_or_else(|| "USB serial".into()),
                    port.port_name
                ),
                SerialPortType::Unknown => port.port_name.clone(),
                _ => return None,
            };
            Some(PortInfo {
                path: port.port_name,
                label,
            })
        })
        .collect::<Vec<_>>();
    ports.sort_by(|a, b| a.path.cmp(&b.path));
    Ok(ports)
}

#[tauri::command]
pub async fn serial_ports() -> Result<Vec<PortInfo>, String> {
    tauri::async_runtime::spawn_blocking(ports)
        .await
        .map_err(|_| "USB discovery failed")?
}

#[tauri::command]
pub async fn serial_open(path: String, state: State<'_, SerialState>) -> Result<u32, String> {
    let connection = state.connection.clone();
    let session = state.next.fetch_add(1, Ordering::Relaxed).wrapping_add(1);
    tauri::async_runtime::spawn_blocking(move || {
        // Only enumerate-and-select serial devices; never accept an arbitrary filesystem path.
        if !ports()?.iter().any(|port| port.path == path) {
            return Err("USB device is no longer available. Refresh the device list.".into());
        }
        let mut active = connection.lock().unwrap();
        if active.is_some() {
            return Err("Disconnect the current USB device first".into());
        }
        let port = serialport::new(path, 115_200)
            .timeout(Duration::from_millis(100))
            .open()
            .map_err(|_| {
                "Could not open USB device. Close other serial apps and check device permissions."
            })?;
        *active = Some(Connection { session, port });
        Ok(session)
    })
    .await
    .map_err(|_| "USB connection failed")?
}

fn active(connection: &mut Option<Connection>, session: u32) -> Result<&mut Connection, String> {
    connection
        .as_mut()
        .filter(|c| c.session == session)
        .ok_or_else(|| "USB serial disconnected".into())
}

#[tauri::command]
pub async fn serial_read(session: u32, state: State<'_, SerialState>) -> Result<Vec<u8>, String> {
    let connection = state.connection.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let mut guard = connection.lock().unwrap();
        let port = &mut active(&mut guard, session)?.port;
        match read_chunk(port) {
            Ok(bytes) => Ok(bytes),
            Err(_) => {
                *guard = None;
                Err("USB serial disconnected. Reconnect and reread settings.".into())
            }
        }
    })
    .await
    .map_err(|_| "USB read failed")?
}

fn read_chunk(reader: &mut impl Read) -> std::io::Result<Vec<u8>> {
    let mut bytes = [0; 1024];
    match reader.read(&mut bytes) {
        Ok(0) => Err(ErrorKind::UnexpectedEof.into()),
        Ok(n) => Ok(bytes[..n].to_vec()),
        Err(e)
            if matches!(
                e.kind(),
                ErrorKind::TimedOut | ErrorKind::WouldBlock | ErrorKind::Interrupted
            ) =>
        {
            Ok(Vec::new())
        }
        Err(e) => Err(e),
    }
}

#[tauri::command]
pub async fn serial_write(
    session: u32,
    bytes: Vec<u8>,
    state: State<'_, SerialState>,
) -> Result<(), String> {
    if bytes.is_empty() || bytes.len() > 8196 {
        return Err("Invalid USB message size".into());
    }
    let connection = state.connection.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let mut guard = connection.lock().unwrap();
        if active(&mut guard, session)?.port.write_all(&bytes).is_err() {
            *guard = None;
            return Err("USB write failed. Reconnect and reread before retrying.".into());
        }
        Ok(())
    })
    .await
    .map_err(|_| "USB write failed")?
}

#[tauri::command]
pub async fn serial_close(session: u32, state: State<'_, SerialState>) -> Result<(), String> {
    let connection = state.connection.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let mut active = connection.lock().unwrap();
        // A late close from an old reader must not close a newly selected device.
        if active.as_ref().is_some_and(|c| c.session == session) {
            *active = None;
        }
    })
    .await
    .map_err(|_| "Could not close USB device".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[cfg(unix)]
    #[test]
    fn usb_io_roundtrips_over_real_serial_pair_and_rejects_old_sessions() {
        let (mut local, mut device) = serialport::TTYPort::pair().unwrap();
        local.set_timeout(Duration::from_millis(100)).unwrap();
        device.set_timeout(Duration::from_millis(100)).unwrap();
        let mut connection = Some(Connection {
            session: 7,
            port: Box::new(local),
        });
        assert!(active(&mut connection, 6).is_err());
        assert!(connection.is_some());
        let port = &mut active(&mut connection, 7).unwrap().port;
        let frame = b"UC:{\"v\":2,\"id\":1,\"op\":\"status\"}\n";
        port.write_all(frame).unwrap();
        assert_eq!(read_chunk(&mut device).unwrap(), frame);
        let reply = b"Boot log\nUC:{\"v\":2,\"id\":1,\"ok\":true,\"result\":{}}\n";
        device.write_all(reply).unwrap();
        assert_eq!(read_chunk(port).unwrap(), reply);
    }
    #[test]
    fn usb_chunks_are_bounded_and_distinguish_idle_from_unplug() {
        let mut reader = std::io::Cursor::new(vec![42; 1500]);
        assert_eq!(read_chunk(&mut reader).unwrap().len(), 1024);
        assert_eq!(read_chunk(&mut reader).unwrap().len(), 476);
        assert_eq!(
            read_chunk(&mut reader).unwrap_err().kind(),
            ErrorKind::UnexpectedEof
        );
        struct Idle;
        impl Read for Idle {
            fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
                Err(ErrorKind::TimedOut.into())
            }
        }
        assert!(read_chunk(&mut Idle).unwrap().is_empty());
    }
}
