use crate::detection::{getenv, read_file};

#[derive(Debug, Clone, Default)]
pub struct GtkThemeInfo {
    pub gtk_theme: String,
    pub icon_theme: String,
    pub cursor_theme: String,
    pub font: String,
    pub font_size: u32,
    pub color_scheme: String,
    pub desktop: String,
}

/// GTK theme settings from gsettings when available, else the config file
/// ~/.config/gtk-3.0/settings.ini (fastfetch uses both).
pub fn detect() -> GtkThemeInfo {
    let mut info = GtkThemeInfo {
        desktop: desktop_name(),
        ..Default::default()
    };
    parse_settings_file(&mut info);
    // gsettings overrides file-based settings when present.
    if let Some(v) = gsettings_lookup("gtk-theme") {
        info.gtk_theme = v;
    }
    if let Some(v) = gsettings_lookup("icon-theme") {
        info.icon_theme = v;
    }
    if let Some(v) = gsettings_lookup("cursor-theme") {
        info.cursor_theme = v;
    }
    if let Some(v) = gsettings_lookup("font-name") {
        info.font = v;
    }
    if let Some(v) = gsettings_lookup("color-scheme") {
        info.color_scheme = v;
    }
    info
}

fn parse_settings_file(info: &mut GtkThemeInfo) {
    let home = getenv("HOME").unwrap_or_default();
    for path in [
        format!("{}/.config/gtk-3.0/settings.ini", home),
        format!("{}/.gtkrc-2.0", home),
        format!("{}/.config/gtk-4.0/settings.ini", home),
    ] {
        if let Some(text) = read_file(&path) {
            for line in text.lines() {
                let line = line.trim();
                if line.starts_with('#') || line.starts_with(';') {
                    continue;
                }
                let Some((k, v)) = line.split_once('=') else { continue };
                let k = k.trim();
                let v = v.trim().trim_matches('"').to_string();
                match k {
                    "gtk-theme-name" => info.gtk_theme = v,
                    "gtk-icon-theme-name" => info.icon_theme = v,
                    "gtk-cursor-theme-name" => info.cursor_theme = v,
                    "gtk-font-name" => {
                        if info.font.is_empty() {
                            info.font = v.clone();
                        }
                    }
                    "gtk-font-size" => {
                        info.font_size = v.parse::<u32>().ok().unwrap_or(0);
                    }
                    "gtk-color-scheme" => info.color_scheme = v,
                    _ => {}
                }
            }
        }
    }
}

fn gsettings_lookup(key: &str) -> Option<String> {
    let schema = "org.gnome.desktop.interface";
    let v = crate::detection::run_capture("gsettings", &["get", schema, key])?;
    let v = v.trim().trim_matches('\'').to_string();
    if v.is_empty() {
        None
    } else {
        Some(v)
    }
}

fn desktop_name() -> String {
    for key in ["XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP"] {
        if let Some(v) = getenv(key) {
            if !v.is_empty() {
                return v;
            }
        }
    }
    String::new()
}