use crate::detection::getenv;
use std::io::Read;

#[derive(Debug, Clone, Default)]
pub struct TerminalInfo {
    pub name: String,
    pub version: String,
    pub font: String,
    pub exe: String,
}

const SHELLS: &[&str] = &[
    "sh", "bash", "zsh", "fish", "csh", "tcsh", "ksh", "dash", "ash",
    "posh", "elvish", "oil", "nushell", "pwsh", "yash",
    "busybox",

    "login", "systemd", "init", "sshd", "sudo", "doas", "su",
    "tmux", "screen", "byobu",
    "git", "ssh",
];

static CACHE: std::sync::OnceLock<TerminalInfo> = std::sync::OnceLock::new();

pub fn detect() -> TerminalInfo {
    CACHE.get_or_init(detect_uncached).clone()
}

fn detect_uncached() -> TerminalInfo {
    let mut info = TerminalInfo::default();

    if let Some(v) = getenv("TERM_PROGRAM") {
        if !v.is_empty() {
            info.name = v;
        }
    }
    if info.name.is_empty() {
        if getenv("KONSOLE_VERSION").is_some() {
            info.name = "Konsole".to_string();
        } else if getenv("WT_SESSION").is_some() {
            info.name = "Windows Terminal".to_string();
        } else if let Some(v) = getenv("TERMINAL") {
            if !v.is_empty() {
                info.name = v.rsplit('/').next().unwrap_or(&v).to_string();
            }
        }
    }

    if info.name.is_empty() {
        let (name, exe) = detect_from_process_tree();
        info.name = name;
        info.exe = exe;

        if !info.exe.is_empty() && info.name == "ai.opencode.desktop" {

        }
    }

    if info.name.is_empty() {
        if let Some(term) = getenv("TERM") {
            if term.starts_with("xterm") {
                info.name = "xterm".to_string();
            } else if !term.is_empty() {
                info.name = term;
            }
        }
    }

    if info.name.eq_ignore_ascii_case("kitty") {
        if let Some(f) = kitty_font() {
            info.font = f;
        }

        if let Some(v) = kitty_version() {
            info.version = v;
        }
    } else if !info.name.is_empty() {

        if let Some(v) = generic_version(&info.name) {
            info.version = v;
        }
    }

    info
}

fn detect_from_process_tree() -> (String, String) {

    let mut pid: u32 = unsafe { libc::getppid() as u32 };

    for _ in 0..20 {
        if pid == 0 || pid == 1 {
            break;
        }

        let Some((comm, ppid, cmdline)) = read_proc_info(pid) else {
            break;
        };

        let base = comm.rsplit('/').next().unwrap_or(&comm).to_ascii_lowercase();

        if SHELLS.iter().any(|s| *s == base) {
            pid = ppid;
            continue;
        }

        let name = terminal_name_from_proc(&base, &comm, &cmdline);

        let exe = if name == "ai.opencode.desktop" || base == "electron" {
            cmdline.clone()
        } else {
            String::new()
        };
        return (name, exe);
    }

    (String::new(), String::new())
}

fn read_proc_info(pid: u32) -> Option<(String, u32, String)> {
    let comm_path = format!("/proc/{}/comm", pid);
    let mut comm = std::fs::read_to_string(&comm_path).ok()?;
    comm.truncate(comm.trim_end().len());

    while comm.ends_with('\n') || comm.ends_with('\r') {
        comm.pop();
    }
    if comm.is_empty() {
        return None;
    }

    let status = std::fs::read_to_string(format!("/proc/{}/status", pid)).ok()?;
    let mut ppid: u32 = 0;
    for line in status.lines() {
        if let Some(rest) = line.strip_prefix("PPid:") {
            ppid = rest.trim().parse().unwrap_or(0);
            break;
        }
    }

    let mut cmdline = String::new();
    if let Ok(mut f) = std::fs::File::open(format!("/proc/{}/cmdline", pid)) {
        let mut buf = [0u8; 4096];
        if let Ok(n) = f.read(&mut buf) {
            let raw = &buf[..n];
            let parts: Vec<String> = raw
                .split(|&c| c == 0)
                .filter(|s| !s.is_empty())
                .map(|s| String::from_utf8_lossy(s).into_owned())
                .collect();
            cmdline = parts.join(" ");
        }
    }

    Some((comm, ppid, cmdline))
}

fn terminal_name_from_proc(base: &str, _comm: &str, cmdline: &str) -> String {

    if base == "electron" {

        if let Some(idx) = cmdline.find("--user-data-dir=") {
            let rest = &cmdline[idx + 16..];
            if let Some(end) = rest.find(|c: char| c == ' ' || c == '\n') {
                let path = &rest[..end];
                if let Some(app) = path.rsplit('/').next() {
                    if !app.is_empty() && app.starts_with(|c: char| c.is_ascii_alphabetic() || c == '.') {
                        return app.to_string();
                    }
                }
            }
        }

        if let Some(idx) = cmdline.find("app.asar") {
            let prefix = &cmdline[..idx];
            if let Some(slash) = prefix.rfind('/') {
                let before = &prefix[..slash];
                if let Some(slash2) = before.rfind('/') {
                    let app = &before[slash2 + 1..];
                    if !app.is_empty() {
                        return app.to_string();
                    }
                }
            }
        }
        return "electron".to_string();
    }

    let mut name = if base.starts_with('.') {
        base[1..].to_string()
    } else {
        base.to_string()
    };
    if let Some(stripped) = name.strip_suffix("-wrapped") {
        name = stripped.to_string();
    }

    if let Some(stripped) = name.strip_suffix(".wrapped") {
        name = stripped.to_string();
    }
    name
}

fn kitty_version() -> Option<String> {

    let cache_path = terminal_version_cache_path("kitty");
    if let Some(cached) = read_version_cache(&cache_path, 3600 * 24) {
        return Some(cached);
    }

    let out = crate::detection::run_capture_timeout("kitty", &["--version"], 400)?;
    let mut parts = out.split_whitespace();
    let first = parts.next()?;
    if !first.eq_ignore_ascii_case("kitty") {
        return None;
    }
    let ver = parts.next()?.to_string();
    if ver.chars().next()?.is_ascii_digit() {
        write_version_cache(&cache_path, &ver);
        Some(ver)
    } else {
        None
    }
}

fn generic_version(name: &str) -> Option<String> {

    let bin = match name.to_ascii_lowercase().as_str() {
        "alacritty" => "alacritty",
        "ghostty" => "ghostty",
        "foot" => "foot",
        "wezterm" => "wezterm",
        "konsole" => return None,
        _ => return None,
    };
    let cache_path = terminal_version_cache_path(bin);
    if let Some(cached) = read_version_cache(&cache_path, 3600 * 24) {
        return Some(cached);
    }
    let out = crate::detection::run_capture_timeout(bin, &["--version"], 400)?;

    for tok in out.split_whitespace() {
        if tok.chars().next().map(|c| c.is_ascii_digit()).unwrap_or(false) && tok.contains('.') {
            let ver = tok.trim_matches(|c| c == ',' || c == ')').to_string();
            write_version_cache(&cache_path, &ver);
            return Some(ver);
        }
    }
    None
}

fn terminal_version_cache_path(bin: &str) -> String {
    if let Some(dir) = std::env::var_os("XDG_CACHE_HOME") {
        format!("{}/jefetch/terminal-{}.version", dir.to_string_lossy(), bin)
    } else if let Some(home) = std::env::var_os("HOME") {
        format!("{}/.cache/jefetch/terminal-{}.version", home.to_string_lossy(), bin)
    } else {
        format!("/tmp/jefetch-terminal-{}.version", bin)
    }
}

fn read_version_cache(path: &str, ttl_secs: u64) -> Option<String> {
    let meta = std::fs::metadata(path).ok()?;
    let mtime = meta.modified().ok()?;
    if mtime.elapsed().ok()?.as_secs() > ttl_secs {
        return None;
    }

    if let Ok(bin_meta) = std::fs::metadata(format!("/run/current-system/sw/bin/{}", path.rsplit('/').next().unwrap_or("").trim_start_matches("terminal-").trim_end_matches(".version"))) {
        if let Ok(bin_mtime) = bin_meta.modified() {
            if bin_mtime > mtime {
                return None;
            }
        }
    }
    let txt = std::fs::read_to_string(path).ok()?;
    let v = txt.trim().to_string();
    if v.is_empty() { None } else { Some(v) }
}

fn write_version_cache(path: &str, ver: &str) {
    let _ = std::fs::create_dir_all(std::path::Path::new(path).parent().unwrap_or(std::path::Path::new("/tmp")));
    let _ = std::fs::write(path, ver);
}

fn kitty_font() -> Option<String> {
    if let Some((family, size)) = kitty_tty_font().or_else(kitty_kitten_font) {
        return Some(pretty_font(&family, &size));
    }
    let (family, size) = kitty_conf_font();
    if family.contains('$') {
        return None;
    }
    if family.is_empty() {
        return None;
    }
    Some(pretty_font(&family, &size))
}

fn pretty_font(family: &str, size: &str) -> String {
    let family = family.trim();
    let size = size.trim().trim_end_matches(".0");
    if size.is_empty() || size == "0" {
        family.to_string()
    } else {
        format!("{family} ({size}pt)")
    }
}

fn hex_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len() * 2);
    for b in s.bytes() {
        out.push_str(&format!("{b:02x}"));
    }
    out
}

fn hex_decode(s: &str) -> Option<String> {
    if s.len() % 2 != 0 || s.is_empty() {
        return None;
    }
    let mut bytes = Vec::with_capacity(s.len() / 2);
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() {
        let hi = (b[i] as char).to_digit(16)?;
        let lo = (b[i + 1] as char).to_digit(16)?;
        bytes.push((hi * 16 + lo) as u8);
        i += 2;
    }
    String::from_utf8(bytes).ok()
}

fn kitty_tty_font() -> Option<(String, String)> {
    let family_key = "kitty-query-font_family";
    let size_key = "kitty-query-font_size";
    let mut req = vec![0x1b, b'P', b'+', b'q'];
    req.extend_from_slice(hex_encode(family_key).as_bytes());
    req.push(b';');
    req.extend_from_slice(hex_encode(size_key).as_bytes());
    req.extend_from_slice(&[0x1b, b'\\']);

    let file = std::fs::OpenOptions::new().read(true).write(true).open("/dev/tty").ok()?;
    let fd = {
        use std::os::unix::io::AsRawFd;
        file.as_raw_fd()
    };
    let mut orig: libc::termios = unsafe { std::mem::zeroed() };
    if unsafe { libc::tcgetattr(fd, &mut orig) } != 0 {
        return None;
    }
    let mut raw = orig;
    raw.c_lflag &= !(libc::ICANON | libc::ECHO);
    raw.c_cc[libc::VMIN as usize] = 0;
    raw.c_cc[libc::VTIME as usize] = 1;
    if unsafe { libc::tcsetattr(fd, libc::TCSANOW, &raw) } != 0 {
        return None;
    }
    let result = kitty_tty_exchange(fd, &req, family_key, size_key);
    unsafe {
        libc::tcsetattr(fd, libc::TCSANOW, &orig);
    }
    result
}

fn kitty_tty_exchange(fd: i32, req: &[u8], family_key: &str, size_key: &str) -> Option<(String, String)> {
    use std::io::Write;
    use std::os::unix::io::FromRawFd;
    let mut writer = unsafe { std::fs::File::from_raw_fd(fd) };
    if writer.write_all(req).is_err() || writer.flush().is_err() {
        std::mem::forget(writer);
        return None;
    }
    std::mem::forget(writer);
    let mut reader = unsafe { std::fs::File::from_raw_fd(fd) };
    let mut buf = Vec::new();
    let mut tmp = [0u8; 512];
    for _ in 0..4 {
        match reader.read(&mut tmp) {
            Ok(0) => continue,
            Ok(n) => {
                buf.extend_from_slice(&tmp[..n]);
                if buf.windows(2).filter(|w| w == b"\x1b\\").count() >= 2 {
                    break;
                }
            }
            Err(_) => break,
        }
    }
    std::mem::forget(reader);
    kitty_tty_parse(&buf, family_key, size_key)
}

fn kitty_tty_parse(buf: &[u8], family_key: &str, size_key: &str) -> Option<(String, String)> {
    let text = String::from_utf8_lossy(buf);
    let mut family = None;
    let mut size = None;
    for part in text.split("\x1bP1+r") {
        let Some(end) = part.find("\x1b\\") else { continue };
        let body = &part[..end];
        let Some(eq) = body.find('=') else { continue };
        let key = hex_decode(&body[..eq]);
        let val = hex_decode(&body[eq + 1..]);
        match (key.as_deref(), val) {
            (Some(k), Some(v)) if k == family_key => family = Some(v),
            (Some(k), Some(v)) if k == size_key => size = Some(v),
            _ => {}
        }
    }
    Some((family?, size.unwrap_or_default()))
}

fn kitty_kitten_font() -> Option<(String, String)> {
    let out = crate::detection::run_capture_timeout("kitty", &["+kitten", "query-terminal"], 800)?;
    let mut family = String::new();
    let mut size = String::new();
    for line in out.lines() {
        let line = line.trim();
        if let Some(v) = line.strip_prefix("font_family:") {
            family = v.trim().to_string();
        } else if let Some(v) = line.strip_prefix("font_size:") {
            size = v.trim().to_string();
        }
    }
    if family.is_empty() {
        return None;
    }
    Some((family, size))
}

fn kitty_conf_font() -> (String, String) {
    let Some(home) = getenv("HOME") else {
        return (String::new(), String::new());
    };
    for path in [
        format!("{}/.config/kitty/kitty.conf", home),
        format!("{}/.config/kitty/conf.d/*.conf", home),
    ] {
        if path.contains('*') {
            continue;
        }
        if let Ok(text) = std::fs::read_to_string(&path) {
            let mut font = String::new();
            let mut size = String::new();
            for line in text.lines() {
                let line = line.trim();
                if let Some(v) = line.split_whitespace().next().and_then(|k| {
                    if k == "font_family" {
                        line.split_once(' ').map(|(_, v)| v)
                    } else {
                        None
                    }
                }) {
                    font = v.trim().trim_matches('"').to_string();
                } else if let Some(rest) = line.strip_prefix("font_size") {
                    let v = rest.trim().trim_matches('"');
                    if !v.is_empty() && size.is_empty() {
                        size = v.to_string();
                    }
                }
                if !font.is_empty() && !size.is_empty() {
                    break;
                }
            }
            if !font.is_empty() || !size.is_empty() {
                return (font, size);
            }
        }
    }
    (String::new(), String::new())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pretty_font_formats_size() {
        assert_eq!(pretty_font("DepartureMonoNF", "10.0"), "DepartureMonoNF (10pt)");
        assert_eq!(pretty_font("DepartureMonoNF", "10"), "DepartureMonoNF (10pt)");
        assert_eq!(pretty_font("monospace", ""), "monospace");
        assert_eq!(pretty_font("  Hack  ", "12"), "Hack (12pt)");
    }

    #[test]
    fn tty_response_parses() {
        let fam = hex_encode("kitty-query-font_family");
        let siz = hex_encode("kitty-query-font_size");
        let buf = format!(
            "\x1bP1+r{}={}\x1b\\\x1bP1+r{}={}\x1b\\",
            fam,
            hex_encode("DepartureMonoNF"),
            siz,
            hex_encode("10.0")
        );
        let (family, size) = kitty_tty_parse(
            buf.as_bytes(),
            "kitty-query-font_family",
            "kitty-query-font_size",
        )
        .expect("parse");
        assert_eq!(family, "DepartureMonoNF");
        assert_eq!(size, "10.0");
        assert!(kitty_tty_parse(b"garbage", "kitty-query-font_family", "kitty-query-font_size").is_none());
    }
}
