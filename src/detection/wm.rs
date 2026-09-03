use crate::detection::{getenv, scan_proc_comm};

#[derive(Debug, Clone, Default)]
pub struct WmInfo {
    pub name: String,
    pub session_type: String,
}

/// Known window manager process names (from /proc comm/maps).
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

    // Prefer the compositor name found in /proc (fastfetch scans /proc).
    if let Some(w) = scan_proc_comm(WMS) {
        info.name = w;
    } else {
        // Environment fallbacks.
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

    info
}