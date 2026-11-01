use crate::model::{Mapping, Osc};
use enigo::{Direction, Enigo, Key, Keyboard};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, UdpSocket};

pub fn key(name: &str) -> Result<Key, String> {
    Ok(match name {
        "LeftArrow" => Key::LeftArrow,
        "RightArrow" => Key::RightArrow,
        "UpArrow" => Key::UpArrow,
        "DownArrow" => Key::DownArrow,
        "PageUp" => Key::PageUp,
        "PageDown" => Key::PageDown,
        "Home" => Key::Home,
        "End" => Key::End,
        "Return" => Key::Return,
        "Escape" => Key::Escape,
        "Space" => Key::Space,
        "Tab" => Key::Tab,
        "Backspace" => Key::Backspace,
        "F5" => Key::F5,
        "Control" => Key::Control,
        "Alt" => Key::Alt,
        "Shift" => Key::Shift,
        "Meta" => Key::Meta,
        "b" | "n" | "p" | "w" => Key::Unicode(name.chars().next().unwrap()),
        _ => return Err("Unsupported keyboard key".into()),
    })
}
pub fn osc_packet(osc: &Osc) -> Vec<u8> {
    let mut bytes = osc.address.as_bytes().to_vec();
    bytes.push(0);
    while bytes.len() % 4 != 0 {
        bytes.push(0);
    }
    bytes.extend_from_slice(b",i\0\0");
    bytes.extend_from_slice(&osc.value.to_be_bytes());
    bytes
}
pub fn send(mapping: &Mapping, keyboard: &mut Option<Enigo>) -> Result<(), String> {
    let mut failure = None;
    if mapping.keyboard.enabled {
        let enigo = keyboard
            .as_mut()
            .ok_or("Keyboard permission is not ready. Pause and enable output again.")?;
        let mut pressed = Vec::new();
        let result = (|| {
            for modifier in &mapping.keyboard.modifiers {
                let k = key(modifier)?;
                enigo
                    .key(k, Direction::Press)
                    .map_err(|_| "Could not press keyboard modifier")?;
                pressed.push(k);
            }
            let k = key(&mapping.keyboard.key)?;
            enigo
                .key(k, Direction::Press)
                .map_err(|_| "Could not press key")?;
            pressed.push(k);
            std::thread::sleep(std::time::Duration::from_millis(20));
            Ok::<(), String>(())
        })();
        // Release every pressed key even when a later press or an earlier release fails.
        for k in pressed.into_iter().rev() {
            if enigo.key(k, Direction::Release).is_err() {
                failure =
                    Some("Key release failed. Pause output and check keyboard permissions.".into());
            }
        }
        if let Err(e) = result {
            failure = Some(e);
        }
    }
    if mapping.osc.enabled {
        let ip = if mapping.osc.host == "localhost" {
            IpAddr::V4(Ipv4Addr::LOCALHOST)
        } else {
            mapping
                .osc
                .host
                .parse::<IpAddr>()
                .map_err(|_| "Invalid OSC address")?
        };
        let address = SocketAddr::new(ip, mapping.osc.port);
        let bind = if address.is_ipv4() {
            "0.0.0.0:0"
        } else {
            "[::]:0"
        };
        let result = UdpSocket::bind(bind).and_then(|s| {
            s.set_write_timeout(Some(std::time::Duration::from_millis(100)))?;
            s.send_to(&osc_packet(&mapping.osc), address)
        });
        if result.is_err() {
            failure = Some("OSC packet could not be sent".into());
        }
    }
    failure.map_or(Ok(()), Err)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn osc_reaches_real_udp_listener() {
        let receiver = UdpSocket::bind("127.0.0.1:0").unwrap();
        receiver
            .set_read_timeout(Some(std::time::Duration::from_secs(1)))
            .unwrap();
        let mut mapping = crate::model::Settings::default().next;
        mapping.keyboard.enabled = false;
        mapping.osc.enabled = true;
        mapping.osc.port = receiver.local_addr().unwrap().port();
        mapping.osc.value = -7;
        send(&mapping, &mut None).unwrap();
        let mut bytes = [0; 128];
        let n = receiver.recv(&mut bytes).unwrap();
        assert_eq!(&bytes[..n], b"/next\0\0\0,i\0\0\xff\xff\xff\xf9");
    }
}
