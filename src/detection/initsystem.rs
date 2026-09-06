use crate::detection::{read_file, run_capture_timeout};

#[derive(Debug, Clone, Default)]
pub struct InitSystemInfo {
    pub name: String,
    pub version: String,
}

pub fn detect() -> InitSystemInfo {
    static CACHE: std::sync::OnceLock<InitSystemInfo> = std::sync::OnceLock::new();
    CACHE.get_or_init(detect_uncached).clone()
}

fn detect_uncached() -> InitSystemInfo {
    let mut info = InitSystemInfo::default();
    let comm = read_file("/proc/1/comm").map(|s| s.trim().to_string()).unwrap_or_default();
    let exe = std::fs::read_link("/proc/1/exe")
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_default();
    info.name = normalize(&comm, &exe);
    if info.name.is_empty() {
        return info;
    }
    if info.name == "systemd" {
        info.version = systemd_version();
    }
    info
}

fn normalize(comm: &str, exe: &str) -> String {
    let exe_base = exe.rsplit('/').next().unwrap_or("");
    if comm == "systemd" || exe_base == "systemd" || exe.contains("systemd") {
        return "systemd".to_string();
    }
    match comm {
        "init" => {
            if exe_base.contains("openrc") {
                "OpenRC".to_string()
            } else if exe_base.contains("runit") {
                "runit".to_string()
            } else {
                "SysVinit".to_string()
            }
        }
        "openrc-init" | "openrc" => "OpenRC".to_string(),
        "runit" | "runsvdir" => "runit".to_string(),
        "s6-svscan" | "s6" => "s6".to_string(),
        "dinit" => "dinit".to_string(),
        "sinit" => "sinit".to_string(),
        "shepherd" => "GNU Shepherd".to_string(),
        "launchd" => "launchd".to_string(),
        "" => String::new(),
        other => other.to_string(),
    }
}

fn systemd_version() -> String {
    let out = run_capture_timeout("systemctl", &["--version"], 500).unwrap_or_default();
    parse_systemctl_version(&out)
}

fn parse_systemctl_version(out: &str) -> String {
    let first = out.lines().next().unwrap_or("").trim();
    let rest = first
        .strip_prefix("systemd")
        .unwrap_or(first)
        .trim()
        .to_string();
    rest
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normalizes_pid1_names() {
        assert_eq!(normalize("systemd", "/usr/lib/systemd/systemd"), "systemd");
        assert_eq!(normalize("init", "/usr/lib/systemd/systemd"), "systemd");
        assert_eq!(normalize("init", "/sbin/openrc-init"), "OpenRC");
        assert_eq!(normalize("openrc-init", ""), "OpenRC");
        assert_eq!(normalize("init", "/sbin/init"), "SysVinit");
        assert_eq!(normalize("runit", ""), "runit");
        assert_eq!(normalize("s6-svscan", ""), "s6");
        assert_eq!(normalize("dinit", ""), "dinit");
        assert_eq!(normalize("", ""), "");
    }

    #[test]
    fn parses_systemctl_version() {
        assert_eq!(parse_systemctl_version("systemd 257 (257.7-1-cachyos)\n+PAM ..."), "257 (257.7-1-cachyos)");
        assert_eq!(parse_systemctl_version("systemd 252\n"), "252");
        assert_eq!(parse_systemctl_version(""), "");
    }
}
