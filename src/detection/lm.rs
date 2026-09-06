use crate::detection::{proc_by_comm, run_capture_timeout};

#[derive(Debug, Clone, Default)]
pub struct LoginManagerInfo {
    pub name: String,
    pub version: String,
}

const KNOWN: &[&str] = &[
    "gdm", "gdm3", "sddm", "sddm-greeter", "lightdm", "lxdm", "ly", "greetd", "agreety",
    "emptty", "slim", "xdm", "wdm", "tdm", "entrance", "nodm",
];

pub fn detect() -> LoginManagerInfo {
    let key = display_manager_target();
    if key.is_none() {
        return detect_uncached();
    }
    {
        let guard = cache_slot().lock().unwrap_or_else(|e| e.into_inner());
        if let (Some(info), cached) = (&guard.0, &guard.1) {
            if cached == &Some(key.clone()) {
                return info.clone();
            }
        }
    }
    let info = detect_uncached();
    *cache_slot().lock().unwrap_or_else(|e| e.into_inner()) = (Some(info.clone()), Some(key));
    info
}

fn cache_slot() -> &'static std::sync::Mutex<(Option<LoginManagerInfo>, Option<Option<String>>)> {
    static SLOT: std::sync::OnceLock<std::sync::Mutex<(Option<LoginManagerInfo>, Option<Option<String>>)>> =
        std::sync::OnceLock::new();
    SLOT.get_or_init(|| std::sync::Mutex::new((None, None)))
}

fn display_manager_target() -> Option<String> {
    std::fs::read_link("/etc/systemd/system/display-manager.service")
        .ok()
        .map(|p| p.to_string_lossy().into_owned())
}

fn detect_uncached() -> LoginManagerInfo {
    let mut info = LoginManagerInfo::default();
    let raw = display_manager_service().or_else(|| running_manager());
    let Some(raw) = raw else { return info };
    info.name = normalize(&raw);
    if !info.name.is_empty() {
        info.version = manager_version(&raw, &info.name);
    }
    info
}

fn display_manager_service() -> Option<String> {
    let link = std::fs::read_link("/etc/systemd/system/display-manager.service").ok()?;
    service_name(&link.to_string_lossy())
}

fn service_name(target: &str) -> Option<String> {
    let base = target.rsplit('/').next().unwrap_or(target);
    let name = base.strip_suffix(".service").unwrap_or(base);
    if name.is_empty() || name == "display-manager" {
        return None;
    }
    Some(name.to_string())
}

fn running_manager() -> Option<String> {
    let hit = proc_by_comm(KNOWN)?;
    let base = hit
        .exe_path
        .rsplit('/')
        .next()
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .unwrap_or(hit.comm);
    Some(base)
}

fn normalize(raw: &str) -> String {
    match raw.to_ascii_lowercase().as_str() {
        "gdm" | "gdm3" => "GDM".to_string(),
        "sddm" | "sddm-greeter" | "sddm-helper" => "SDDM".to_string(),
        "lightdm" => "LightDM".to_string(),
        "lxdm" => "LXDM".to_string(),
        "ly" => "ly".to_string(),
        "greetd" | "agreety" => "greetd".to_string(),
        "emptty" => "emptty".to_string(),
        "slim" => "SLiM".to_string(),
        "xdm" => "XDM".to_string(),
        "wdm" => "WDM".to_string(),
        "tdm" => "TDM".to_string(),
        "entrance" => "Entrance".to_string(),
        "nodm" => "nodm".to_string(),
        "" => String::new(),
        other => other.to_string(),
    }
}

fn manager_version(bin: &str, name: &str) -> String {
    let out = run_capture_timeout(bin, &["--version"], 500).unwrap_or_default();
    accept_version_line(&out, name)
}

fn accept_version_line(out: &str, name: &str) -> String {
    let first = out.lines().next().unwrap_or("").trim();
    if first.is_empty() || first.len() > 64 {
        return String::new();
    }
    let lower = first.to_ascii_lowercase();
    if lower.contains(&name.to_ascii_lowercase()) {
        return first.to_string();
    }
    if lower.split_whitespace().any(|t| t.chars().next().is_some_and(|c| c.is_ascii_digit()) && t.contains('.')) {
        return first.to_string();
    }
    String::new()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_service_symlink() {
        assert_eq!(service_name("/usr/lib/systemd/system/sddm.service"), Some("sddm".to_string()));
        assert_eq!(service_name("gdm.service"), Some("gdm".to_string()));
        assert_eq!(service_name("/run/systemd/generator/display-manager.service"), None);
    }

    #[test]
    fn normalizes_manager_names() {
        assert_eq!(normalize("gdm3"), "GDM");
        assert_eq!(normalize("sddm-greeter"), "SDDM");
        assert_eq!(normalize("lightdm"), "LightDM");
        assert_eq!(normalize("slim"), "SLiM");
        assert_eq!(normalize("ly"), "ly");
    }

    #[test]
    fn accepts_sane_version_lines() {
        assert_eq!(accept_version_line("sddm 0.21.0", "SDDM"), "sddm 0.21.0");
        assert_eq!(accept_version_line("GDM 48.0", "GDM"), "GDM 48.0");
        assert_eq!(accept_version_line("", "GDM"), "");
        assert_eq!(accept_version_line("unrelated banner text", "GDM"), "");
    }
}
