#[derive(Debug, Clone, Default)]
pub struct MemoryInfo {
    pub mem_total: u64,
    pub mem_used: u64,
    pub mem_free: u64,
    pub mem_available: u64,
    pub mem_buffers: u64,
    pub mem_cached: u64,
    pub swap_total: u64,
    pub swap_used: u64,
    pub swap_free_val: u64,
}

pub fn detect() -> MemoryInfo {
    let mut info = MemoryInfo::default();
    for line in crate::detection::read_file_lines("/proc/meminfo") {
        let mut parts = line.split_whitespace();
        let key = parts.next().unwrap_or("").trim_end_matches(':');
        let val_kb: u64 = parts.next().and_then(|v| v.parse().ok()).unwrap_or(0);

        let val_bytes = val_kb.saturating_mul(1024);
        match key {
            "MemTotal" => info.mem_total = val_bytes,
            "MemFree" => info.mem_free = val_bytes,
            "MemAvailable" => info.mem_available = val_bytes,
            "Buffers" => info.mem_buffers = val_bytes,
            "Cached" => info.mem_cached = val_bytes,
            "SwapTotal" => info.swap_total = val_bytes,
            "SwapFree" => info.swap_free_val = val_bytes,
            _ => {}
        }
    }
    info.mem_used = fastfetch_used(info.mem_total, info.mem_available);
    info.swap_used = info.swap_total.saturating_sub(info.swap_free_val);
    info
}

fn fastfetch_used(total: u64, available: u64) -> u64 {
    if available >= total {
        0
    } else {
        total - available
    }
}
