pub fn format_bytes(bytes: u64, _unit: &str) -> String {
    if bytes == 0 {
        return "0 B".to_string();
    }
    let (value, suffix) = humanize_unit(bytes);
    format!("{:.2} {}", value, suffix)
}

pub fn format_bytes_smart(bytes: u64) -> String {
    let (value, suffix) = humanize_unit(bytes);
    format!("{:.2} {}", value, suffix)
}

pub fn humanize_unit(bytes: u64) -> (f64, &'static str) {
    const UNITS: [&str; 6] = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"];
    let mut value = bytes as f64;
    let mut unit = 0usize;
    while value >= 1024.0 && unit < UNITS.len() - 1 {
        value /= 1024.0;
        unit += 1;
    }
    (value, UNITS[unit])
}

pub fn format_bytes_plain(bytes: u64) -> String {
    let (value, suffix) = humanize_unit(bytes);
    format!("{:.2} {}", value, suffix)
}

pub fn format_uptime(secs: u64) -> String {
    let days = secs / 86400;
    let hours = (secs % 86400) / 3600;
    let mins = (secs % 3600) / 60;
    let mut parts = Vec::new();
    if days > 0 {
        parts.push(format!("{} day{}", days, if days == 1 { "" } else { "s" }));
    }
    if hours > 0 {
        parts.push(format!("{} hour{}", hours, if hours == 1 { "" } else { "s" }));
    }
    if mins > 0 {
        parts.push(format!("{} min{}", mins, if mins == 1 { "" } else { "s" }));
    }
    if parts.is_empty() {
        parts.push(format!("{} secs", secs));
    }
    parts.join(", ")
}

pub fn percent(used: u64, total: u64) -> Option<u8> {
    if total == 0 {
        return None;
    }
    Some(((used as f64 / total as f64) * 100.0).round().clamp(0.0, 100.0) as u8)
}

pub fn percent_bar(used: u64, total: u64) -> String {
    let mut out = String::new();
    let pct = percent(used, total).unwrap_or(0);
    let filled = ((pct as f64 / 100.0) * 10.0).round() as usize;
    for i in 0..10 {
        out.push(if i < filled { '█' } else { '░' });
    }
    out
}

pub fn truncate_to_width(s: &str, width: usize, _ellipsis: bool) -> String {
    let mut out = String::new();
    let mut w = 0;
    for c in s.chars() {
        let cw = if c.is_ascii() { 1 } else { 2 };
        if w + cw > width {
            if _ellipsis && width == 1 {
                return "…".to_string();
            }
            break;
        }
        out.push(c);
        w += cw;
    }
    out
}

pub fn terminal_width() -> usize {
    terminal_size().0
}

pub fn terminal_size() -> (usize, usize) {
    use std::os::unix::io::AsRawFd;
    let fd = std::io::stdout().as_raw_fd();
    unsafe {
        let mut ws: libc::winsize = std::mem::zeroed();
        if libc::ioctl(fd, libc::TIOCGWINSZ, &mut ws) == 0 {
            let cols = if ws.ws_col > 0 { ws.ws_col as usize } else { 80 };
            let rows = if ws.ws_row > 0 { ws.ws_row as usize } else { 24 };
            return (cols, rows);
        }
    }
    (80, 24)
}

pub fn colors_enabled() -> bool {
    if let Ok(v) = std::env::var("NO_COLOR") {
        if !v.is_empty() {
            return false;
        }
    }
    if let Ok(term) = std::env::var("TERM") {
        if term.eq_ignore_ascii_case("dumb") {
            return false;
        }
    }
    true
}

pub fn utf8_supported() -> bool {
    for key in ["LC_ALL", "LC_CTYPE", "LANG"] {
        if let Ok(v) = std::env::var(key) {
            if v.is_empty() {
                continue;
            }
            let lower = v.to_ascii_lowercase().replace(['-', '_'], "");
            if !lower.contains("utf8") {
                return false;
            }
        }
    }
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    fn swap_env(key: &str, val: Option<&str>) -> Option<String> {
        let old = std::env::var(key).ok();
        match val {
            Some(v) => std::env::set_var(key, v),
            None => std::env::remove_var(key),
        }
        old
    }

    fn restore_env(key: &str, old: Option<String>) {
        match old {
            Some(v) => std::env::set_var(key, v),
            None => std::env::remove_var(key),
        }
    }

    #[test]
    fn colors_follow_no_color_and_dumb() {
        let old_no = swap_env("NO_COLOR", None);
        let old_term = swap_env("TERM", Some("xterm-256color"));
        assert!(colors_enabled());
        swap_env("NO_COLOR", Some("1"));
        assert!(!colors_enabled());
        swap_env("NO_COLOR", Some(""));
        assert!(colors_enabled());
        swap_env("NO_COLOR", None);
        swap_env("TERM", Some("dumb"));
        assert!(!colors_enabled());
        swap_env("TERM", Some("DUMB"));
        assert!(!colors_enabled());
        restore_env("NO_COLOR", old_no);
        restore_env("TERM", old_term);
    }

    #[test]
    fn utf8_follows_locale() {
        let old_all = swap_env("LC_ALL", None);
        let old_ctype = swap_env("LC_CTYPE", None);
        let old_lang = swap_env("LANG", Some("en_US.UTF-8"));
        assert!(utf8_supported());
        swap_env("LC_ALL", Some("C"));
        assert!(!utf8_supported());
        swap_env("LC_ALL", Some("C.UTF-8"));
        assert!(utf8_supported());
        swap_env("LC_ALL", None);
        swap_env("LANG", Some("POSIX"));
        assert!(!utf8_supported());
        restore_env("LC_ALL", old_all);
        restore_env("LC_CTYPE", old_ctype);
        restore_env("LANG", old_lang);
    }
}
