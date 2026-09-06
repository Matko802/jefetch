use crate::detection::{getenv, run_capture_timeout, scan_proc_comm};

#[derive(Debug, Clone, Default)]
pub struct WmInfo {
    pub name: String,
    pub version: String,
    pub session_type: String,
}

const WMS: &[&str] = &[
    "hyprland",
    "sway",
    "dwm",
    "i3",
    "bspwm",
    "openbox",
    "xmonad",
    "awesome",
    "qtile",
    "river",
    "wayfire",
    "kwin_wayland",
    "mutter",
    "weston",
    "xfwm4",
    "cinnamon",
    "muffin",
    "gnome-shell",
    "dwl",
    "labwc",
    "niri",
    "mango",
];

static CACHE: std::sync::OnceLock<WmInfo> = std::sync::OnceLock::new();

pub fn detect() -> WmInfo {
    CACHE.get_or_init(detect_uncached).clone()
}

fn detect_uncached() -> WmInfo {
    let mut info = WmInfo::default();

    if let Some(w) = scan_proc_comm(WMS) {
        info.name = w;
    } else {

        for key in [
            "DWMSESSION",
            "DESKTOP_SESSION",
            "GDMSESSION",
            "WM",
            "SWAY_DESKTOP_SESSION",
        ] {
            if let Some(v) = getenv(key) {
                if !v.is_empty() {
                    info.name = v;
                    break;
                }
            }
        }
    }

    if getenv("WAYLAND_DISPLAY").is_some() {
        info.session_type = "Wayland".to_string();
    } else if getenv("DISPLAY").is_some() {
        info.session_type = "X11".to_string();
    }

    if !info.name.is_empty() {
        info.version = wm_version(&info.name);
    }

    info
}

fn wm_version(name: &str) -> String {
    let bins = [name.to_string(), name.to_ascii_lowercase()];
    for bin in bins.iter().map(|s| s.as_str()) {
        if let Some(out) = run_capture_timeout(bin, &["--version"], 500) {
            if let Some(v) = first_version_token(&out) {
                return v;
            }
        }
    }
    String::new()
}

fn first_version_token(out: &str) -> Option<String> {
    let line = out.lines().next()?.trim();
    let bytes = line.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i].is_ascii_digit() {
            let mut j = i;
            loop {
                while j < bytes.len() && bytes[j].is_ascii_digit() {
                    j += 1;
                }
                if j < bytes.len() && bytes[j] == b'.' {
                    let mut k = j + 1;
                    while k < bytes.len() && bytes[k].is_ascii_digit() {
                        k += 1;
                    }
                    if k == j + 1 {
                        break;
                    }
                    j = k;
                } else {
                    break;
                }
            }
            if j > i && line[i..j].contains('.') {
                return Some(line[i..j].to_string());
            }
            i = if j > i { j } else { i + 1 };
        } else {
            i += 1;
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extracts_first_version() {
        assert_eq!(first_version_token("niri 26.04 (Nixpkgs)"), Some("26.04".to_string()));
        assert_eq!(first_version_token("sway version 1.11"), Some("1.11".to_string()));
        assert_eq!(first_version_token("i3 version 4.24 (2024-11-07)"), Some("4.24".to_string()));
        assert_eq!(first_version_token("no version here"), None);
        assert_eq!(first_version_token(""), None);
    }
}
