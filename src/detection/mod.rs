// Common Linux platform helpers used by the detection modules.
// Pure Rust: reads /proc and /sys, uses libc syscalls where needed.

pub mod battery;
pub mod board;
pub mod brightness;
pub mod cpu;
pub mod de;
pub mod disk;
pub mod display;
pub mod dns;
pub mod gpu;
pub mod kernel;
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

/// Read a key=value line-style file (e.g. /proc/cpuinfo, /etc/os-release).
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

/// Strip quotes from a value like "ubuntu 24.04".
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

/// Cached fastfetch --json output (60s TTL) for use by modules that want to
/// be exactly like fastfetch without reimplementing every detection.
/// Excludes terminalfont which does a DECRQSS kitty-query that leaks as ^[P1+r garbage
/// and causes the 1s stutter when cache misses.
pub fn fastfetch_json() -> Option<String> {
    use std::sync::OnceLock;
    static CACHE: OnceLock<Option<String>> = OnceLock::new();
    CACHE.get_or_init(|| {
        let cache_path = {
            if let Some(dir) = std::env::var_os("XDG_CACHE_HOME") {
                format!("{}/sharkfetch/fastfetch.json", dir.to_string_lossy())
            } else if let Some(home) = std::env::var_os("HOME") {
                format!("{}/.cache/sharkfetch/fastfetch.json", home.to_string_lossy())
            } else {
                "/tmp/sharkfetch-fastfetch.json".to_string()
            }
        };
        // Use file cache if fresh (<60s) — keeps warm runs instant (4 ms) and avoids the kitty-query stutter
        // If stale, return stale immediately and refresh in background so the next run is instant too
        if let Ok(meta) = std::fs::metadata(&cache_path) {
            if let Ok(mtime) = meta.modified() {
                let is_fresh = mtime.elapsed().map(|e| e.as_secs() < 60).unwrap_or(false);
                if let Ok(txt) = std::fs::read_to_string(&cache_path) {
                    if !txt.is_empty() {
                        if is_fresh {
                            return Some(txt);
                        }
                        // Stale: return stale now, refresh in background
                        let cache_clone = cache_path.clone();
                        std::thread::spawn(move || {
                            if let Some(out) = run_capture_timeout(
                                "/run/current-system/sw/bin/fastfetch",
                                &["--json", "--structure", "title:separator:os:host:kernel:uptime:packages:shell:display:wm:theme:icons:font:cursor:terminal:cpu:gpu:memory:swap:disk:localip:locale:break:colors"],
                                800,
                            ) {
                                let _ = std::fs::create_dir_all(std::path::Path::new(&cache_clone).parent().unwrap_or(std::path::Path::new("/tmp")));
                                let _ = std::fs::write(&cache_clone, &out);
                            }
                        });
                        return Some(txt);
                    }
                }
            }
        }
        // No cache: spawn in background and return None immediately so first run is instant (4 ms) via native detection
        let cache_clone = cache_path.clone();
        std::thread::spawn(move || {
            if let Some(out) = run_capture_timeout(
                "/run/current-system/sw/bin/fastfetch",
                &["--json", "--structure", "title:separator:os:host:kernel:uptime:packages:shell:display:wm:theme:icons:font:cursor:terminal:cpu:gpu:memory:swap:disk:localip:locale:break:colors"],
                800,
            ) {
                let _ = std::fs::create_dir_all(std::path::Path::new(&cache_clone).parent().unwrap_or(std::path::Path::new("/tmp")));
                let _ = std::fs::write(&cache_clone, &out);
            }
        });
        None
    }).clone()
}

/// Run a command and return trimmed stdout (used sparingly, prefer /proc).
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

/// Run a command with a hard timeout and return trimmed stdout on success.
/// Saves/restores termios so a killed `fastfetch` (DECRQSS kitty-query) doesn't leave ECHO off.
pub fn run_capture_timeout(cmd: &str, args: &[&str], timeout_ms: u64) -> Option<String> {
    let saved_stdin = termios_save(libc::STDIN_FILENO);
    let saved_stdout = termios_save(libc::STDOUT_FILENO);
    let (tx, rx) = std::sync::mpsc::channel();
    let cmd_s = cmd.to_string();
    let args_s: Vec<String> = args.iter().map(|s| s.to_string()).collect();
    std::thread::spawn(move || {
        let out = std::process::Command::new(&cmd_s).args(&args_s).output();
        let _ = tx.send(out);
    });
    let res = rx.recv_timeout(std::time::Duration::from_millis(timeout_ms));
    termios_restore(libc::STDIN_FILENO, &saved_stdin);
    termios_restore(libc::STDOUT_FILENO, &saved_stdout);
    if res.is_err() {
        let _ = std::process::Command::new("stty").arg("sane").output();
        let _ = std::process::Command::new("pkill").args(["-f", cmd]).output();
    }
    match res {
        Ok(Ok(out)) if out.status.success() => String::from_utf8(out.stdout).ok().map(|s| s.trim().to_string()).filter(|s| !s.is_empty()),
        _ => None,
    }
}

fn termios_save(fd: i32) -> Option<libc::termios> {
    let mut term = unsafe { std::mem::zeroed::<libc::termios>() };
    if unsafe { libc::tcgetattr(fd, &mut term) } == 0 {
        Some(term)
    } else {
        None
    }
}
fn termios_restore(fd: i32, saved: &Option<libc::termios>) {
    // Always ensure ECHO and ICANON are on, even if saved state had them off (e.g., after a killed fastfetch)
    if let Some(mut term) = saved.clone() {
        term.c_lflag |= libc::ECHO | libc::ICANON;
        unsafe { libc::tcsetattr(fd, libc::TCSANOW, &term as *const libc::termios); }
    } else {
        unsafe {
            let mut t: libc::termios = std::mem::zeroed();
            if libc::tcgetattr(fd, &mut t) == 0 {
                t.c_lflag |= libc::ECHO | libc::ICANON;
                libc::tcsetattr(fd, libc::TCSANOW, &t);
            }
        }
    }
    // Best-effort stty sane as well, in case tcsetattr didn't cover all flags
    let _ = std::process::Command::new("stty").arg("sane").output();
}

/// Scan /proc/<pid>/comm for a process whose basename is in `names`
/// (case-insensitive). Returns the first match in process enumeration order.
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

/// Details about a running process matched by its comm basename.
#[derive(Debug, Clone, Default)]
pub struct ProcInfo {
    pub pid: u32,
    pub ppid: u32,
    pub comm: String,
    pub exe_path: String,
    pub cmdline: String,
}

/// Find the first process whose comm basename is in `names` and return
/// pid/ppid/exe path/cmdline (used for JSON output of shell/terminal).
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
        // Read ppid from /proc/<pid>/stat (field 4 after the `) ` marker).
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
