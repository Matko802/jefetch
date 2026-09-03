use crate::detection::{getenv, run_capture};

#[derive(Debug, Clone, Default)]
pub struct ShellInfo {
    pub process_path: Option<String>,
    pub shell_path: String,
    pub shell_name: String,
    pub shell_version: String,
    pub shell_base_name: String,
}

pub fn detect() -> ShellInfo {
    let mut info = ShellInfo::default();

    // Walk up the process tree to find the actual shell, like fastfetch's getShellInfo.
    // $SHELL is the login shell, not necessarily the current shell (e.g., fish login, bash subshell).
    let shell_path = find_shell_via_proc().or_else(|| getenv("SHELL")).unwrap_or_default();
    if shell_path.is_empty() {
        return info;
    }
    info.shell_path = shell_path;

    // Basename without leading '-'
    let base = info
        .shell_path
        .rsplit('/')
        .next()
        .unwrap_or(&info.shell_path)
        .trim_start_matches('-')
        .to_string();
    info.shell_base_name = base.clone();
    info.shell_name = base.clone();
    let raw = version_output(&info.shell_path);
    info.shell_version = parse_version(&raw, &base);
    info
}

fn find_shell_via_proc() -> Option<String> {
    const KNOWN_SHELLS: &[&str] = &[
        "sh", "bash", "zsh", "fish", "csh", "tcsh", "ksh", "dash", "ash", "posh", "elvish", "oil", "nushell",
        "pwsh", "yash", "busybox", "nu", "xonsh", "elvish", "oil.ovm",
    ];
    const SKIP: &[&str] = &[
        "sudo", "su", "doas", "strace", "gdb", "lldb", "login", "ltrace", "perf", "time", "script", "proot", "fastfetch", "sharkfetch", "flatpak",
    ];
    let mut pid: u32 = unsafe { libc::getppid() as u32 };
    for _ in 0..20 {
        if pid == 0 || pid == 1 {
            break;
        }
        let exe_path = std::fs::read_link(format!("/proc/{}/exe", pid)).ok().and_then(|p| Some(p.to_string_lossy().into_owned())).unwrap_or_default();
        let comm = std::fs::read_to_string(format!("/proc/{}/comm", pid)).ok().map(|s| s.trim().to_string()).unwrap_or_default();
        let base = comm.rsplit('/').next().unwrap_or(&comm).trim_start_matches('-').to_ascii_lowercase();
        let base_exe = exe_path.rsplit('/').next().unwrap_or(&exe_path).trim_start_matches('-').to_ascii_lowercase();
        // Check if this is a known shell (by comm or exe)
        let is_shell = KNOWN_SHELLS.iter().any(|s| base == *s || base_exe == *s);
        let is_skip = SKIP.iter().any(|s| base == *s || base_exe == *s) || base == "sh" || comm == "sh";
        if is_shell && !is_skip {
            // Return the exe path if available, else comm
            if !exe_path.is_empty() && !exe_path.contains(" (deleted)") {
                return Some(exe_path);
            }
            return Some(comm);
        }
        // If it's a skip wrapper, continue walking
        if is_skip || base == "sh" {
            // get ppid
            if let Ok(status) = std::fs::read_to_string(format!("/proc/{}/status", pid)) {
                for line in status.lines() {
                    if let Some(rest) = line.strip_prefix("PPid:") {
                        pid = rest.trim().parse().unwrap_or(0);
                        break;
                    }
                }
                continue;
            }
            break;
        }
        // If it's not a known shell and not a skip, check parent
        // For unknown shells, we still walk one more step to find a known shell
        if let Ok(status) = std::fs::read_to_string(format!("/proc/{}/status", pid)) {
            for line in status.lines() {
                if let Some(rest) = line.strip_prefix("PPid:") {
                    let ppid: u32 = rest.trim().parse().unwrap_or(0);
                    // If parent is a known shell, return it
                    let parent_comm = std::fs::read_to_string(format!("/proc/{}/comm", ppid)).ok().map(|s| s.trim().to_string()).unwrap_or_default();
                    let parent_base = parent_comm.rsplit('/').next().unwrap_or(&parent_comm).trim_start_matches('-').to_ascii_lowercase();
                    if KNOWN_SHELLS.iter().any(|s| parent_base == *s) {
                        let parent_exe = std::fs::read_link(format!("/proc/{}/exe", ppid)).ok().map(|p| p.to_string_lossy().into_owned()).unwrap_or(parent_comm);
                        return Some(parent_exe);
                    }
                    pid = ppid;
                    break;
                }
            }
        } else {
            break;
        }
    }
    None
}

/// Extract a clean version from `--version` output like
/// `fish, version 3.7.1`, `GNU bash, version 5.2.26(1)-release ...`,
/// or `zsh 5.9 (x86_64...)\n...`.
fn parse_version(raw: &str, base: &str) -> String {
    let first = raw.lines().find(|l| !l.trim().is_empty());
    let Some(line) = first else {
        return String::new();
    };
    let words: Vec<&str> = line.split_whitespace().collect();
    for (i, w) in words.iter().enumerate() {
        if w.eq_ignore_ascii_case("version") {
            if let Some(v) = words.get(i + 1) {
                return v.trim_end_matches(',').to_string();
            }
        }
    }
    // No "version" word: take the first token that starts with a digit.
    for w in words {
        if w.chars().next().map_or(false, |c| c.is_ascii_digit()) {
            return w.to_string();
        }
    }
    // Last resort: the token that looks like a version within known formats,
    // else base name alone.
    if line.starts_with(base) || line.contains(base) {
        line.trim().to_string()
    } else {
        line.trim().to_string()
    }
}

fn version_output(path: &str) -> String {
    // Try common version flags for the resolved real path.
    let real = resolve(path);
    if real.is_empty() {
        return String::new();
    }
    let out = run_capture(&real, &["--version"]);
    out.unwrap_or_default()
}

fn resolve(path: &str) -> String {
    if path.contains('/') {
        std::fs::canonicalize(path)
            .map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_else(|_| path.to_string())
    } else {
        path.to_string()
    }
}
