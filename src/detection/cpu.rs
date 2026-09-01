use crate::detection::read_file;
use std::collections::HashSet;

#[derive(Debug, Clone, Default)]
pub struct CpuInfo {
    pub model: String,
    pub vendor: String,
    pub packages: usize,
    pub physical_cores: usize,
    pub logical_cores: usize,
    /// Maximum frequency in MHz (from sysfs, fallback /proc/cpuinfo).
    pub freq_max_mhz: u64,
    /// Current frequency in MHz (from sysfs scaling_cur_freq).
    pub freq_cur_mhz: u64,
    /// Performance core count for hybrid CPUs, if known.
    pub pe_cores: Option<usize>,
    pub ee_cores: Option<usize>,
}

/// Approximate the microarchitecture level from /proc/cpuinfo flags,
/// following the x86-64-vN (PSABI) ladder.
pub fn march() -> Option<String> {
    let text = read_file("/proc/cpuinfo").unwrap_or_default();
    let mut flags = String::new();
    for line in text.lines() {
        let line = line.trim();
        if let Some(rest) = line.strip_prefix("flags") {
            if let Some((_, v)) = rest.split_once(':') {
                flags = v.trim().to_string();
            }
        }
    }
    let has = |f: &str| flags.split_whitespace().any(|x| x == f);
    if has("avx512f") && has("avx512bw") && has("avx512cd") && has("avx512dq") && has("avx512vl") {
        return Some("x86_64-v4".to_string());
    }
    if has("avx2") && has("bmi1") && has("bmi2") && has("f16c") && has("fma") && has("lzcnt")
        && has("movbe") && has("osxsave")
    {
        return Some("x86_64-v3".to_string());
    }
    if has("cx16") && has("lahf_lm") && has("popcnt") && has("sse4_1") && has("sse4_2")
        && has("ssse3")
    {
        return Some("x86_64-v2".to_string());
    }
    if has("cmpxchg8b") && has("fxsr") && has("mmx") && has("sse") && has("sse2") {
        return Some("x86_64".to_string());
    }
    None
}

/// Number of NUMA nodes on the system.
pub fn numa_nodes() -> u64 {
    let Ok(e) = std::fs::read_dir("/sys/devices/system/node") else {
        return 1;
    };
    e.flatten().count() as u64
}

pub fn detect() -> CpuInfo {
    let mut info = CpuInfo::default();

    let text = read_file("/proc/cpuinfo").unwrap_or_default();
    let mut unique_cores: HashSet<(String, String)> = HashSet::new();
    let mut phys_id = String::new();
    let mut core_id = String::new();
    let mut cur_mhz: Vec<u64> = Vec::new();

    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() {
            // Processor block ended; record the core.
            if !phys_id.is_empty() || !core_id.is_empty() {
                unique_cores.insert((phys_id.clone(), core_id.clone()));
                phys_id.clear();
                core_id.clear();
            }
            continue;
        }
        let Some(eq) = line.find(':') else { continue };
        let key = line[..eq].trim();
        let val = line[eq + 1..].trim();
        match key {
            "processor" => info.logical_cores += 1,
            "model name" if info.model.is_empty() => info.model = val.to_string(),
            "vendor_id" if info.vendor.is_empty() => info.vendor = val.to_string(),
            "physical id" => phys_id = val.to_string(),
            "core id" => core_id = val.to_string(),
            "cpu MHz" => {
                if let Ok(m) = val.parse::<f64>() {
                    cur_mhz.push(m as u64);
                }
            }
            _ => {}
        }
    }
    if !phys_id.is_empty() || !core_id.is_empty() {
        unique_cores.insert((phys_id, core_id));
    }

    if info.logical_cores == 0 {
        fn count_from_topology(dir: &str) -> usize {
            std::fs::read_dir(dir)
                .ok()
                .map(|it| it.filter_map(|e| e.ok()).count())
                .unwrap_or(0)
        }
        info.logical_cores = count_from_topology("/sys/devices/system/cpu/cpu1");
    }

    info.physical_cores = unique_cores
        .iter()
        .filter(|(p, c)| !p.is_empty() || !c.is_empty())
        .count();
    if info.physical_cores == 0 {
        info.physical_cores = info.logical_cores;
    }

    // Frequency: prefer the sysfs cpufreq values.
    info.freq_max_mhz = read_file("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq")
        .and_then(|s| s.trim().parse::<u64>().ok())
        .map(|khz| khz / 1000)
        .unwrap_or_default();
    info.freq_cur_mhz = read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq")
        .and_then(|s| s.trim().parse::<u64>().ok())
        .map(|khz| khz / 1000)
        .unwrap_or_default();
    if info.freq_cur_mhz == 0 {
        info.freq_cur_mhz = cur_mhz.first().copied().unwrap_or(0);
    }

    detect_hybrid(&mut info);

    if info.freq_max_mhz == 0 {
        info.freq_max_mhz = info.freq_cur_mhz;
        // AMD reports base frequency in cpuinfo; Intel max is in sysfs.
        if info.freq_max_mhz == 0 {
            info.freq_max_mhz = cur_mhz.first().copied().unwrap_or(0);
        }
    }

    info
}

/// For hybrid Intel CPUs, /sys/devices/system/cpu/hybrid_cpu_list lists the
/// performance CPUs (e.g. "0-6,8-14"), one entry per enabled thread. Count
/// unique core_ids among them to get physical P cores.
fn detect_hybrid(info: &mut CpuInfo) {
    let list = read_file("/sys/devices/system/cpu/hybrid_cpu_list");
    let Some(list) = list else { return };

    let mut cores: HashSet<String> = HashSet::new();
    for part in list.trim().split(',') {
        let (a, b) = if let Some((a, b)) = part.split_once('-') {
            let a: usize = a.trim().parse().ok().unwrap_or(0);
            let b: usize = b.trim().parse().ok().unwrap_or(a);
            (a, b)
        } else if let Ok(n) = part.trim().parse::<usize>() {
            (n, n)
        } else {
            continue;
        };
        for cpu in a..=b {
            if let Some(t) = read_file(&format!(
                "/sys/devices/system/cpu/cpu{}/topology/core_id",
                cpu
            )) {
                cores.insert(t.trim().to_string());
            }
        }
    }
    if cores.is_empty() {
        return;
    }
    let p = cores.len();
    info.pe_cores = Some(p);
    info.physical_cores = info.physical_cores.max(p);
    info.ee_cores = Some(info.physical_cores.saturating_sub(p));
}