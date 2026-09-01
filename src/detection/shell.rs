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
    // Prefer $SHELL, fall back to the parent process (fastfetch reads /proc).
    info.shell_path = getenv("SHELL").unwrap_or_else(|| {
        parent_comm().unwrap_or_else(|| "".to_string())
    });
    if info.shell_path.is_empty() {
        return info;
    }

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

fn parent_comm() -> Option<String> {
    let ppid = std::fs::read_to_string("/proc/self/stat")
        .ok()
        .and_then(|s| {
            // comm may contain spaces/parens; find last ')'
            let close = s.rfind(')')?;
            let after = &s[close + 1..];
            after.split_whitespace().nth(1).and_then(|v| v.parse::<u32>().ok())
        })?;
    let name = getenv("SHELL").unwrap_or_default();
    if !name.is_empty() {
        return Some(name);
    }
    let dir = format!("/proc/{}/exe", ppid);
    std::fs::read_link(dir)
        .ok()
        .map(|p| p.to_string_lossy().into_owned())
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
