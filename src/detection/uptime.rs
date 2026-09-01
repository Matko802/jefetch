use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct UptimeInfo {
    pub uptime_secs: u64,
    pub boot_time_secs: u64,
}

pub fn detect() -> UptimeInfo {
    let mut info = UptimeInfo::default();
    if let Some(v) = read_file("/proc/uptime") {
        if let Some(secs) = v.split_whitespace().next() {
            if let Ok(f) = secs.parse::<f64>() {
                info.uptime_secs = f as u64;
            }
        }
    }
    if let Some(boot) = read_file("/proc/sys/kernel/btime") {
        info.boot_time_secs = boot.trim().parse().unwrap_or(0);
    }
    // Fallback: boot time = now - uptime when btime is unavailable.
    if info.boot_time_secs == 0 && info.uptime_secs > 0 {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        info.boot_time_secs = now.saturating_sub(info.uptime_secs);
    }
    info
}
