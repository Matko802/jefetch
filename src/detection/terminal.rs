use crate::detection::{getenv, scan_proc_comm};

#[derive(Debug, Clone, Default)]
pub struct TerminalInfo {
    pub name: String,
    pub font: String,
}

/// Common terminal emulator process names.
const TERMS: &[&str] = &[
    "alacritty",
    "kitty",
    "konsole",
    "gnome-terminal",
    "gnome-console",
    "kgx",
    "xterm",
    "urxvt",
    "rxvt",
    "foot",
    "wezterm",
    "st",
    "stterm",
    "xfce4-terminal",
    "lxterminal",
    "terminator",
    "deepin-terminal",
    "mate-terminal",
    "tilix",
    "tabby",
    "rio",
    "ghostty",
    "contour",
];

pub fn detect() -> TerminalInfo {
    let mut info = TerminalInfo::default();

    // Env-based detection (fastfetch order: TERM_PROGRAM first).
    if let Some(v) = getenv("TERM_PROGRAM") {
        if !v.is_empty() {
            info.name = v;
        }
    }
    if info.name.is_empty() {
        if let Some(_v) = getenv("KONSOLE_VERSION") {
            info.name = "Konsole".to_string();
        } else if let Some(_v) = getenv("WT_SESSION") {
            info.name = "Windows Terminal".to_string();
        } else if let Some(v) = getenv("TERMINAL") {
            if !v.is_empty() {
                info.name = v.rsplit('/').next().unwrap_or(&v).to_string();
            }
        }
    }

    // Scan /proc as a fallback.
    if info.name.is_empty() {
        if let Some(t) = scan_proc_comm(TERMS) {
            info.name = t;
        }
    }

    // Map TERM like "xterm-256color" back to "xterm".
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
    }

    info
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