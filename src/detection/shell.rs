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

    let shell_path = find_shell_via_proc().or_else(|| getenv("SHELL")).unwrap_or_default();
    if shell_path.is_empty() {
        return info;
    }
    info.shell_path = shell_path;

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
        "sudo", "su", "doas", "strace", "gdb", "lldb", "login", "ltrace", "perf", "time", "script", "proot", "fastfetch", "jefetch", "flatpak",
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

        let is_shell = KNOWN_SHELLS.iter().any(|s| base == *s || base_exe == *s);
        let is_skip = SKIP.iter().any(|s| base == *s || base_exe == *s) || base == "sh" || comm == "sh";
        if is_shell && !is_skip {

            if !exe_path.is_empty() && !exe_path.contains(" (deleted)") {
                return Some(exe_path);
            }
            return Some(comm);
        }

        if is_skip || base == "sh" {

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

        if let Ok(status) = std::fs::read_to_string(format!("/proc/{}/status", pid)) {
            for line in status.lines() {
                if let Some(rest) = line.strip_prefix("PPid:") {
                    let ppid: u32 = rest.trim().parse().unwrap_or(0);

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

    for w in words {
        if w.chars().next().map_or(false, |c| c.is_ascii_digit()) {
            return w.to_string();
        }
    }

    if line.starts_with(base) || line.contains(base) {
        line.trim().to_string()
    } else {
        line.trim().to_string()
    }
}

fn version_output(path: &str) -> String {

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
