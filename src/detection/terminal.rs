use crate::detection::getenv;
use std::io::Read;

#[derive(Debug, Clone, Default)]
pub struct TerminalInfo {
    pub name: String,
    pub version: String,
    pub font: String,
    pub exe: String,
}

/// Processes to skip when walking up the process tree toward the terminal.
/// Fastfetch skips shells and known non-terminal wrappers.
const SHELLS: &[&str] = &[
    "sh", "bash", "zsh", "fish", "csh", "tcsh", "ksh", "dash", "ash",
    "posh", "elvish", "oil", "nushell", "pwsh", "yash",
    "busybox",
    // Non-terminal wrappers / init
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

    // ── Env-based detection (fastfetch order) ───────────────────────────
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

    // ── Process-tree walk ───────────────────────────────────────────────
    // Fastfetch's primary detection: walk from the current process up,
    // skipping shells, and report the first non-shell ancestor as the terminal.
    if info.name.is_empty() {
        let (name, exe) = detect_from_process_tree();
        info.name = name;
        info.exe = exe;
        // For electron, fastfetch shows the full cmdline as the terminal value
        if !info.exe.is_empty() && info.name == "ai.opencode.desktop" {
            // Keep exe for later use in render
        }
    }

    // ── Last-resort fallback: TERM env ──────────────────────────────────
    if info.name.is_empty() {
        if let Some(term) = getenv("TERM") {
            if term.starts_with("xterm") {
                info.name = "xterm".to_string();
            } else if !term.is_empty() {
                info.name = term;
            }
        }
    }

    // Best-effort font detection for a few terminals.
    if info.name.eq_ignore_ascii_case("kitty") {
        if let Some(f) = kitty_font() {
            info.font = f;
        }
        // Match fastfetch: "kitty 0.48.2" — get version via `kitty --version`.
        if let Some(v) = kitty_version() {
            info.version = v;
        }
    } else if !info.name.is_empty() {
        // Generic version probe for other terminals (fastfetch does per-terminal).
        if let Some(v) = generic_version(&info.name) {
            info.version = v;
        }
    }

    info
}

/// Walk up from the current process (getppid), skip shells and common
/// non-terminal wrappers, and return the first remaining ancestor's name and exe.
/// For electron-based apps, try to extract the real app name from cmdline.
fn detect_from_process_tree() -> (String, String) {
    // Start from our parent PID (the shell that invoked us).
    let mut pid: u32 = unsafe { libc::getppid() as u32 };

    // Guard against infinite loops (max ~20 ancestors).
    for _ in 0..20 {
        if pid == 0 || pid == 1 {
            break;
        }

        let Some((comm, ppid, cmdline)) = read_proc_info(pid) else {
            break;
        };

        let base = comm.rsplit('/').next().unwrap_or(&comm).to_ascii_lowercase();

        // If this process is a known shell or non-terminal wrapper, keep walking.
        if SHELLS.iter().any(|s| *s == base) {
            pid = ppid;
            continue;
        }

        // Found a non-shell process – this is the terminal.
        // Apply naming heuristics.
        let name = terminal_name_from_proc(&base, &comm, &cmdline);
        // For electron, keep full cmdline as exe to match fastfetch's "Terminal: ai.opencode.desktop --standard-schemes=..."
        let exe = if name == "ai.opencode.desktop" || base == "electron" {
            cmdline.clone()
        } else {
            String::new()
        };
        return (name, exe);
    }

    (String::new(), String::new())
}

/// Read comm, ppid, and cmdline for a given PID from /proc.
fn read_proc_info(pid: u32) -> Option<(String, u32, String)> {
    let comm_path = format!("/proc/{}/comm", pid);
    let mut comm = std::fs::read_to_string(&comm_path).ok()?;
    comm.truncate(comm.trim_end().len());
    // read_to_string may leave trailing whitespace; trim it
    while comm.ends_with('\n') || comm.ends_with('\r') {
        comm.pop();
    }
    if comm.is_empty() {
        return None;
    }

    // Read PPid from /proc/<pid>/status (format: "PPid:\t<id>").
    let status = std::fs::read_to_string(format!("/proc/{}/status", pid)).ok()?;
    let mut ppid: u32 = 0;
    for line in status.lines() {
        if let Some(rest) = line.strip_prefix("PPid:") {
            ppid = rest.trim().parse().unwrap_or(0);
            break;
        }
    }

    // Read cmdline (null-separated).
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

/// Derive a pretty terminal name from a process that survived shell filtering.
fn terminal_name_from_proc(base: &str, _comm: &str, cmdline: &str) -> String {
    // electron-based apps: extract app name from --user-data-dir or app.asar path.
    if base == "electron" {
        // Look for --user-data-dir=<home>/.config/<app>
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
        // Look for app.asar path: .../<app-name>/resources/app.asar
        if let Some(idx) = cmdline.find("app.asar") {
            let prefix = &cmdline[..idx];
            if let Some(slash) = prefix.rfind('/') {
                let before = &prefix[..slash]; // .../app-name/resources
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

    // Nix wrappers: ".kitty-wrapped" or "kitty-wrapped" → "kitty"
    // Also handles generic "*-wrapped" from nixpkgs wrapGApps etc.
    let mut name = if base.starts_with('.') {
        base[1..].to_string()
    } else {
        base.to_string()
    };
    if let Some(stripped) = name.strip_suffix("-wrapped") {
        name = stripped.to_string();
    }
    // Some wrappers use ".app-wrapped" -> after dot strip still has -wrapped
    // Handle case like ".kitty-wrapped" already stripped to "kitty-wrapped"
    // and also "kitty.wrapped" variant.
    if let Some(stripped) = name.strip_suffix(".wrapped") {
        name = stripped.to_string();
    }
    name
}

fn kitty_version() -> Option<String> {
    // Persistent cache like packages: ~/.cache/sharkfetch/terminal-kitty.version
    let cache_path = terminal_version_cache_path("kitty");
    if let Some(cached) = read_version_cache(&cache_path, 3600 * 24) {
        return Some(cached);
    }
    // `kitty --version` -> "kitty 0.48.2 created by Kovid Goyal"
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
    // Fastfetch tries `<terminal> --version` for many terminals; we do a cheap
    // best-effort for common ones without spawning for every unknown.
    let bin = match name.to_ascii_lowercase().as_str() {
        "alacritty" => "alacritty",
        "ghostty" => "ghostty",
        "foot" => "foot",
        "wezterm" => "wezterm",
        "konsole" => return None, // version via env
        _ => return None,
    };
    let cache_path = terminal_version_cache_path(bin);
    if let Some(cached) = read_version_cache(&cache_path, 3600 * 24) {
        return Some(cached);
    }
    let out = crate::detection::run_capture_timeout(bin, &["--version"], 400)?;
    // Take first token that looks like a version (digit.digit)
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
        format!("{}/sharkfetch/terminal-{}.version", dir.to_string_lossy(), bin)
    } else if let Some(home) = std::env::var_os("HOME") {
        format!("{}/.cache/sharkfetch/terminal-{}.version", home.to_string_lossy(), bin)
    } else {
        format!("/tmp/sharkfetch-terminal-{}.version", bin)
    }
}

fn read_version_cache(path: &str, ttl_secs: u64) -> Option<String> {
    let meta = std::fs::metadata(path).ok()?;
    let mtime = meta.modified().ok()?;
    if mtime.elapsed().ok()?.as_secs() > ttl_secs {
        return None;
    }
    // Also invalidate if binary newer than cache (kitty updated)
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

/// Read kitty's font_family from its config file(s).
fn kitty_font() -> Option<String> {
    let home = getenv("HOME")?;
    for path in [
        format!("{}/.config/kitty/kitty.conf", home),
        format!("{}/.config/kitty/conf.d/*.conf", home),
    ] {
        if path.contains('*') {
            continue;
        }
        if let Ok(text) = std::fs::read_to_string(&path) {
            let mut font = None;
            for line in text.lines() {
                let line = line.trim();
                if line.starts_with("font_family") {
                    let Some((_, v)) = line.split_once(' ') else {
                        continue;
                    };
                    font = Some(v.trim().trim_matches('"').to_string());
                    break;
                }
            }
            if let Some(f) = font {
                return Some(f);
            }
        }
    }
    None
}
