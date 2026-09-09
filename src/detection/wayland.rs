use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::Duration;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct WlOutput {
    pub name: String,
    pub make: String,
    pub model: String,
    pub width: u32,
    pub height: u32,
    pub refresh_mhz: u32,
}

impl WlOutput {
    pub fn refresh_hz(&self) -> u32 {
        if self.refresh_mhz == 0 {
            return 0;
        }
        (self.refresh_mhz + 500) / 1000
    }
}

const WL_DISPLAY_ID: u32 = 1;
const WL_REGISTRY_ID: u32 = 2;
const WL_REGISTRY_GLOBAL: u16 = 0;
const WL_DISPLAY_SYNC: u16 = 0;
const WL_DISPLAY_GET_REGISTRY: u16 = 1;
const WL_REGISTRY_BIND: u16 = 0;
const WL_OUTPUT_MODE: u16 = 1;
const WL_OUTPUT_NAME: u16 = 4;
const WL_OUTPUT_DESCRIPTION: u16 = 5;
const WL_CALLBACK_DONE: u16 = 0;
const WL_OUTPUT_MODE_CURRENT: u32 = 0x1;
const MAX_MESSAGES: usize = 512;
const READ_TIMEOUT: Duration = Duration::from_millis(1500);

fn put_u32(buf: &mut Vec<u8>, v: u32) {
    buf.extend_from_slice(&v.to_le_bytes());
}

fn put_string(buf: &mut Vec<u8>, s: &str) {
    let bytes = s.as_bytes();
    put_u32(buf, bytes.len() as u32 + 1);
    buf.extend_from_slice(bytes);
    buf.push(0);
    while buf.len() % 4 != 0 {
        buf.push(0);
    }
}

fn put_header(buf: &mut Vec<u8>, object: u32, opcode: u16, body_len: usize) {
    let size = (8 + body_len) as u32;
    put_u32(buf, object);
    put_u32(buf, (size << 16) | opcode as u32);
}

fn get_registry_msg() -> Vec<u8> {
    let mut body = Vec::new();
    put_u32(&mut body, WL_REGISTRY_ID);
    let mut msg = Vec::new();
    put_header(&mut msg, WL_DISPLAY_ID, WL_DISPLAY_GET_REGISTRY, body.len());
    msg.extend_from_slice(&body);
    msg
}

fn bind_msg(name: u32, interface: &str, version: u32, new_id: u32) -> Vec<u8> {
    let mut body = Vec::new();
    put_u32(&mut body, name);
    put_string(&mut body, interface);
    put_u32(&mut body, version);
    put_u32(&mut body, new_id);
    let mut msg = Vec::new();
    put_header(&mut msg, WL_REGISTRY_ID, WL_REGISTRY_BIND, body.len());
    msg.extend_from_slice(&body);
    msg
}

fn sync_msg(new_id: u32) -> Vec<u8> {
    let mut body = Vec::new();
    put_u32(&mut body, new_id);
    let mut msg = Vec::new();
    put_header(&mut msg, WL_DISPLAY_ID, WL_DISPLAY_SYNC, body.len());
    msg.extend_from_slice(&body);
    msg
}

#[derive(Debug, Default)]
struct Reader {
    buf: Vec<u8>,
}

impl Reader {
    fn take_u32(&mut self) -> Option<u32> {
        if self.buf.len() < 4 {
            return None;
        }
        let v = u32::from_le_bytes([self.buf[0], self.buf[1], self.buf[2], self.buf[3]]);
        self.buf.drain(..4);
        Some(v)
    }

    fn take_string(&mut self) -> Option<String> {
        let len = self.take_u32()? as usize;
        if len == 0 || len > self.buf.len() + 1 {
            return None;
        }
        let end = len - 1;
        let s = String::from_utf8_lossy(&self.buf[..end]).into_owned();
        let padded = (len + 3) & !3;
        if padded > self.buf.len() {
            return None;
        }
        self.buf.drain(..padded);
        Some(s)
    }
}

#[derive(Debug, Default)]
struct OutputAcc {
    name: String,
    make: String,
    model: String,
    width: u32,
    height: u32,
    refresh_mhz: u32,
    have_current: bool,
}

fn apply_output_event(acc: &mut OutputAcc, opcode: u16, r: &mut Reader) {
    match opcode {
        0 => {
            let _ = r.take_u32();
            let _ = r.take_u32();
            let _ = r.take_u32();
            let _ = r.take_u32();
            let _ = r.take_u32();
            acc.make = r.take_string().unwrap_or_default();
            acc.model = r.take_string().unwrap_or_default();
            let _ = r.take_u32();
        }
        WL_OUTPUT_MODE => {
            let flags = r.take_u32().unwrap_or(0);
            let w = r.take_u32().unwrap_or(0);
            let h = r.take_u32().unwrap_or(0);
            let hz = r.take_u32().unwrap_or(0);
            if flags & WL_OUTPUT_MODE_CURRENT != 0 {
                acc.width = w;
                acc.height = h;
                acc.refresh_mhz = hz;
                acc.have_current = true;
            }
        }
        WL_OUTPUT_NAME => {
            acc.name = r.take_string().unwrap_or_default();
        }
        WL_OUTPUT_DESCRIPTION => {
            let _ = r.take_string();
        }
        _ => {}
    }
}

fn parse_messages(
    data: &[u8],
    outputs: &mut std::collections::HashMap<u32, OutputAcc>,
    globals: &mut Vec<(u32, String, u32)>,
    sync_done: &mut bool,
    sync_id: u32,
) -> usize {
    let mut off = 0;
    let mut count = 0;
    while off + 8 <= data.len() && count < MAX_MESSAGES {
        count += 1;
        let object = u32::from_le_bytes(data[off..off + 4].try_into().unwrap_or([0; 4]));
        let size_op =
            u32::from_le_bytes(data[off + 4..off + 8].try_into().unwrap_or([0; 4]));
        let size = (size_op >> 16) as usize;
        let opcode = (size_op & 0xffff) as u16;
        if size < 8 || off + size > data.len() {
            break;
        }
        let mut r = Reader { buf: data[off + 8..off + size].to_vec() };
        if object == WL_REGISTRY_ID && opcode == WL_REGISTRY_GLOBAL {
            let name = r.take_u32().unwrap_or(0);
            let interface = r.take_string().unwrap_or_default();
            let version = r.take_u32().unwrap_or(0);
            if !interface.is_empty() {
                globals.push((name, interface, version));
            }
        } else if object == sync_id && opcode == WL_CALLBACK_DONE {
            *sync_done = true;
        } else if let Some(acc) = outputs.get_mut(&object) {
            apply_output_event(acc, opcode, &mut r);
        }
        off += size;
    }
    off
}

pub fn query_outputs() -> Vec<WlOutput> {
    let path = match socket_path() {
        Some(p) => p,
        None => return Vec::new(),
    };
    let stream = match UnixStream::connect(&path) {
        Ok(s) => s,
        Err(_) => return Vec::new(),
    };
    query_on_stream(stream)
}

fn query_on_stream(mut stream: UnixStream) -> Vec<WlOutput> {
    if stream.set_read_timeout(Some(READ_TIMEOUT)).is_err() {
        return Vec::new();
    }
    if stream.set_write_timeout(Some(READ_TIMEOUT)).is_err() {
        return Vec::new();
    }
    if stream.write_all(&get_registry_msg()).is_err() {
        return Vec::new();
    }
    let mut data = Vec::new();
    let mut tmp = [0u8; 65536];
    let mut globals: Vec<(u32, String, u32)> = Vec::new();
    let mut outputs: std::collections::HashMap<u32, OutputAcc> =
        std::collections::HashMap::new();
    let mut next_id = 3u32;
    let mut bound = false;
    let mut sync_id = 0u32;
    let mut sync_done = false;
    let mut consumed = 0usize;
    loop {
        match stream.read(&mut tmp) {
            Ok(0) => break,
            Ok(n) => {
                data.extend_from_slice(&tmp[..n]);
            }
            Err(_) => break,
        }
        if !bound {
            let mut fresh: Vec<(u32, String, u32)> = Vec::new();
            consumed +=
                parse_messages(&data[consumed..], &mut outputs, &mut fresh, &mut sync_done, sync_id);
            let mut out = Vec::new();
            for (name, interface, version) in fresh {
                if interface == "wl_output" {
                    let id = next_id;
                    next_id += 1;
                    outputs.insert(id, OutputAcc::default());
                    let ver = version.min(4);
                    out.extend_from_slice(&bind_msg(name, &interface, ver, id));
                    globals.push((name, interface, version));
                }
            }
            if out.is_empty() {
                break;
            }
            sync_id = next_id;
            next_id += 1;
            out.extend_from_slice(&sync_msg(sync_id));
            if stream.write_all(&out).is_err() {
                return Vec::new();
            }
            bound = true;
            continue;
        }
        consumed +=
            parse_messages(&data[consumed..], &mut outputs, &mut globals, &mut sync_done, sync_id);
        if sync_done {
            break;
        }
        if data.len() > 1 << 20 {
            break;
        }
    }
    let mut result = Vec::new();
    for acc in outputs.values() {
        if !acc.have_current {
            continue;
        }
        result.push(WlOutput {
            name: acc.name.clone(),
            make: acc.make.clone(),
            model: acc.model.clone(),
            width: acc.width,
            height: acc.height,
            refresh_mhz: acc.refresh_mhz,
        });
    }
    result.sort_by(|a, b| a.name.cmp(&b.name));
    result
}

fn socket_path() -> Option<String> {
    if let Ok(display) = std::env::var("WAYLAND_DISPLAY") {
        if display.contains('/') {
            return Some(display);
        }
        if let Ok(runtime) = std::env::var("XDG_RUNTIME_DIR") {
            if !runtime.is_empty() {
                return Some(format!("{}/{}", runtime.trim_end_matches('/'), display));
            }
        }
        let uid = unsafe { libc::getuid() };
        let candidate = format!("/run/user/{}/{}", uid, display);
        if std::path::Path::new(&candidate).exists() {
            return Some(candidate);
        }
        return None;
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tagged(object: u32, opcode: u16, body: &[u8]) -> Vec<u8> {
        let mut msg = Vec::new();
        put_header(&mut msg, object, opcode, body.len());
        msg.extend_from_slice(body);
        msg
    }

    fn str_body(s: &str) -> Vec<u8> {
        let mut body = Vec::new();
        put_string(&mut body, s);
        body
    }

    fn global_msg(name: u32, interface: &str, version: u32) -> Vec<u8> {
        let mut body = Vec::new();
        put_u32(&mut body, name);
        put_string(&mut body, interface);
        put_u32(&mut body, version);
        tagged(WL_REGISTRY_ID, WL_REGISTRY_GLOBAL, &body)
    }

    fn mode_msg(object: u32, current: bool, w: u32, h: u32, mhz: u32) -> Vec<u8> {
        let mut body = Vec::new();
        put_u32(&mut body, if current { WL_OUTPUT_MODE_CURRENT } else { 0 });
        put_u32(&mut body, w);
        put_u32(&mut body, h);
        put_u32(&mut body, mhz);
        tagged(object, WL_OUTPUT_MODE, &body)
    }

    fn geometry_msg(object: u32, make: &str, model: &str) -> Vec<u8> {
        let mut body = Vec::new();
        put_u32(&mut body, 0);
        put_u32(&mut body, 0);
        put_u32(&mut body, 0);
        put_u32(&mut body, 0);
        put_u32(&mut body, 0);
        put_string(&mut body, make);
        put_string(&mut body, model);
        put_u32(&mut body, 0);
        tagged(object, 0, &body)
    }

    #[test]
    fn parses_current_mode_and_name() {
        let mut data = Vec::new();
        data.extend_from_slice(&geometry_msg(3, "DEL", "DELL S2721DGF"));
        data.extend_from_slice(&mode_msg(3, false, 1920, 1080, 165000));
        data.extend_from_slice(&mode_msg(3, true, 1920, 1080, 143976));
        data.extend_from_slice(&tagged(3, WL_OUTPUT_NAME, &str_body("DP-1")));
        let mut outputs = std::collections::HashMap::new();
        outputs.insert(3, OutputAcc::default());
        let mut globals = Vec::new();
        let mut done = false;
        parse_messages(&data, &mut outputs, &mut globals, &mut done, 99);
        let acc = outputs.get(&3).unwrap();
        assert!(acc.have_current);
        assert_eq!((acc.width, acc.height, acc.refresh_mhz), (1920, 1080, 143976));
        assert_eq!(acc.name, "DP-1");
        assert_eq!(acc.make, "DEL");
        assert!(!done);
    }

    #[test]
    fn refresh_rounds_to_whole_hertz() {
        let o = WlOutput { refresh_mhz: 143976, ..Default::default() };
        assert_eq!(o.refresh_hz(), 144);
        let o = WlOutput { refresh_mhz: 165000, ..Default::default() };
        assert_eq!(o.refresh_hz(), 165);
        let o = WlOutput::default();
        assert_eq!(o.refresh_hz(), 0);
    }

    #[test]
    fn malformed_stream_is_ignored_safely() {
        let mut outputs = std::collections::HashMap::new();
        outputs.insert(3, OutputAcc::default());
        let mut globals = Vec::new();
        let mut done = false;
        parse_messages(&[0u8; 5], &mut outputs, &mut globals, &mut done, 99);
        parse_messages(&[1u8, 0, 0, 0, 9, 0, 255, 255, 1, 2, 3], &mut outputs, &mut globals, &mut done, 99);
        assert!(!outputs.get(&3).unwrap().have_current);
        assert!(!done);
    }

    #[test]
    fn full_handshake_against_fake_compositor() {
        use std::os::unix::net::UnixListener;
        let dir = std::env::temp_dir().join(format!("jefetch-wltest-{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);
        let sock = dir.join("fake-wayland");
        let _ = std::fs::remove_file(&sock);
        let listener = UnixListener::bind(&sock).unwrap();
        let handle = std::thread::spawn(move || {
            let (mut conn, _) = listener.accept().unwrap();
            conn.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
            let mut buf = vec![0u8; 65536];
            let mut seen = Vec::new();
            loop {
                let n = conn.read(&mut buf).unwrap_or(0);
                if n == 0 {
                    break;
                }
                seen.extend_from_slice(&buf[..n]);
                if seen.len() >= 8 {
                    break;
                }
            }
            let mut out = Vec::new();
            out.extend_from_slice(&global_msg(7, "wl_output", 4));
            out.extend_from_slice(&global_msg(8, "wl_compositor", 6));
            let _ = conn.write_all(&out);
            let mut buf2 = vec![0u8; 65536];
            let mut seen2 = Vec::new();
            loop {
                let n = conn.read(&mut buf2).unwrap_or(0);
                if n == 0 {
                    break;
                }
                seen2.extend_from_slice(&buf2[..n]);
                if seen2.len() >= 32 {
                    break;
                }
            }
            let mut ev = Vec::new();
            ev.extend_from_slice(&geometry_msg(3, "DEL", "DELL S2721DGF"));
            ev.extend_from_slice(&mode_msg(3, false, 1920, 1080, 165000));
            ev.extend_from_slice(&mode_msg(3, true, 1920, 1080, 143976));
            ev.extend_from_slice(&tagged(3, WL_OUTPUT_NAME, &str_body("DP-1")));
            ev.extend_from_slice(&tagged(3, 2, &[]));
            let mut cb = Vec::new();
            put_u32(&mut cb, 4242);
            ev.extend_from_slice(&tagged(4, WL_CALLBACK_DONE, &cb));
            let _ = conn.write_all(&ev);
            std::thread::sleep(Duration::from_millis(200));
        });
        let stream = UnixStream::connect(&sock).unwrap();
        let found = query_on_stream(stream);
        handle.join().unwrap();
        let _ = std::fs::remove_dir_all(&dir);
        assert_eq!(found.len(), 1);
        assert_eq!((found[0].width, found[0].height), (1920, 1080));
        assert_eq!(found[0].refresh_mhz, 143976);
        assert_eq!(found[0].refresh_hz(), 144);
        assert_eq!(found[0].name, "DP-1");
    }
}
