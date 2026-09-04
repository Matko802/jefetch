pub fn format_bytes(bytes: u64, _unit: &str) -> String {
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
    Some(((used as f64 / total as f64) * 100.0).round() as u8)
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
    use std::os::unix::io::AsRawFd;
    let fd = std::io::stdout().as_raw_fd();
    unsafe {
        let mut ws: libc::winsize = std::mem::zeroed();
        if libc::ioctl(fd, libc::TIOCGWINSZ, &mut ws) == 0 && ws.ws_col > 0 {
            return ws.ws_col as usize;
        }
    }
    80
}
