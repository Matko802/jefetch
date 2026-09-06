pub mod battery;
pub mod board;
pub mod brightness;
pub mod cpu;
pub mod de;
pub mod disk;
pub mod display;
pub mod dns;
pub mod gpu;
pub mod initsystem;
pub mod kernel;
pub mod lm;
pub mod localip;
pub mod memory;
pub mod os;
pub mod packages;
pub mod publicip;
pub mod shell;
pub mod terminal;
pub mod theme;
pub mod uptime;
pub mod user;
pub mod users;
pub mod wifi;
pub mod wm;

pub fn read_file<P: AsRef<std::path::Path>>(path: P) -> Option<String> {
    std::fs::read_to_string(path).ok()
}

pub fn read_file_lines<P: AsRef<std::path::Path>>(path: P) -> Vec<String> {
    read_file(path)
        .map(|s| s.lines().map(|l| l.to_string()).collect())
        .unwrap_or_default()
}

pub fn parse_key_value_file(path: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    for line in read_file_lines(path) {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if let Some(eq) = line.find('=') {
            let k = line[..eq].trim().to_string();
            let v = line[eq + 1..].trim().to_string();
            out.push((k, v));
        }
    }
    out
}

pub fn unquote(v: &str) -> String {
    let mut s = v.to_string();
    if s.len() >= 2 {
        let b = s.as_bytes();
        let first = b[0];
        let last = b[b.len() - 1];
        if (first == b'"' && last == b'"') || (first == b'\'' && last == b'\'') {
            s = s[1..s.len() - 1].to_string();
        }
    }
    s
}

pub fn getenv(name: &str) -> Option<String> {
    std::env::var(name).ok()
}

pub fn run_capture(cmd: &str, args: &[&str]) -> Option<String> {
    let out = std::process::Command::new(cmd)
        .args(args)
        .output()
        .ok()?;
    if !out.status.success() {
        return None;
    }
    String::from_utf8(out.stdout)
        .ok()
        .map(|s| s.trim().to_string())
}

pub fn run_capture_lines(cmd: &str, args: &[&str]) -> Vec<String> {
    run_capture(cmd, args)
        .map(|s| s.lines().map(|l| l.to_string()).collect())
        .unwrap_or_default()
}

pub fn run_capture_timeout(cmd: &str, args: &[&str], timeout_ms: u64) -> Option<String> {
    let child = std::process::Command::new(cmd)
        .args(args)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .spawn()
        .ok()?;
    let pid = child.id();
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let out = child.wait_with_output();
        let _ = tx.send(out);
    });
    let out = match rx.recv_timeout(std::time::Duration::from_millis(timeout_ms)) {
        Ok(out) => out,
        Err(_) => {
            unsafe {
                libc::kill(pid as libc::pid_t, libc::SIGKILL);
            }
            return None;
        }
    };
    let out = out.ok()?;
    if !out.status.success() {
        return None;
    }
    String::from_utf8(out.stdout)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

pub fn scan_proc_comm(names: &[&str]) -> Option<String> {
    let entries = std::fs::read_dir("/proc").ok()?;
    for entry in entries.flatten() {
        let pid = entry.file_name().to_string_lossy().into_owned();
        if !pid.chars().all(|c| c.is_ascii_digit()) {
            continue;
        }
        let comm = std::fs::read_to_string(format!("/proc/{}/comm", pid)).ok();
        let Some(comm) = comm else { continue };
        let comm = comm.trim();
        let base = comm.rsplit('/').next().unwrap_or(comm).to_lowercase();
        for n in names {
            if base == *n {
                return Some(comm.to_string());
            }
        }
    }
    None
}

#[derive(Debug, Clone, Default)]
pub struct ProcInfo {
    pub pid: u32,
    pub ppid: u32,
    pub comm: String,
    pub exe_path: String,
    pub cmdline: String,
}

pub fn proc_by_comm(names: &[&str]) -> Option<ProcInfo> {
    let Ok(entries) = std::fs::read_dir("/proc") else {
        return None;
    };
    for entry in entries.flatten() {
        let pid = entry.file_name().to_string_lossy().into_owned();
        if !pid.chars().all(|c| c.is_ascii_digit()) || pid.len() > 7 {
            continue;
        }
        let comm = std::fs::read_to_string(format!("/proc/{}/comm", pid)).ok();
        let Some(comm) = comm else { continue };
        let comm = comm.trim().to_string();
        let base = comm.rsplit('/').next().unwrap_or(&comm).to_lowercase();
        if !names.iter().any(|n| base == *n) {
            continue;
        }
        let info = ProcInfo {
            pid: pid.parse().unwrap_or(0),
            ppid: 0,
            comm: comm.clone(),
            exe_path: std::fs::read_link(format!("/proc/{}/exe", pid))
                .map(|p| p.to_string_lossy().into_owned())
                .unwrap_or_default(),
            cmdline: std::fs::read(format!("/proc/{}/cmdline", pid))
                .map(|b| {
                    b.split(|&c| c == 0)
                        .filter(|s| !s.is_empty())
                        .map(|s| String::from_utf8_lossy(s).into_owned())
                        .collect::<Vec<_>>()
                        .join(" ")
                })
                .unwrap_or_default(),
        };

        if let Ok(stat) = std::fs::read_to_string(format!("/proc/{}/status", pid)) {
            for line in stat.lines() {
                if let Some(rest) = line.strip_prefix("PPid:") {
                    let p: u32 = rest.trim().parse().unwrap_or(0);
                    return Some(ProcInfo { ppid: p, ..info });
                }
            }
        }
        return Some(info);
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn capture_timeout_respects_deadline() {
        let t = std::time::Instant::now();
        assert!(run_capture_timeout("sleep", &["10"], 100).is_none());
        assert!(t.elapsed() < std::time::Duration::from_secs(5));
        assert_eq!(
            run_capture_timeout("echo", &["hi"], 2000).as_deref(),
            Some("hi")
        );
    }
}
