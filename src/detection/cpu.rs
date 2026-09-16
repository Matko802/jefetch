use crate::detection::read_file;
use std::collections::HashSet;

#[derive(Debug, Clone, Default)]
pub struct CpuInfo {
    pub model: String,
    pub vendor: String,
    pub packages: usize,
    pub physical_cores: usize,
    pub logical_cores: usize,

    pub freq_max_mhz: u64,

    pub freq_cur_mhz: u64,

    pub pe_cores: Option<usize>,
    pub ee_cores: Option<usize>,
}

pub fn march() -> Option<String> {
    march_from_text(&read_file("/proc/cpuinfo").unwrap_or_default())
}

fn march_from_text(text: &str) -> Option<String> {
    let mut flags = String::new();
    let mut features = String::new();
    let mut cpu_arch: Option<String> = None;
    for line in text.lines() {
        let line = line.trim();
        let Some((k, v)) = line.split_once(':') else { continue };
        match k.trim() {
            "flags" if flags.is_empty() => flags = v.trim().to_string(),
            "Features" | "features" if features.is_empty() => features = v.trim().to_string(),
            "CPU architecture" if cpu_arch.is_none() => {
                cpu_arch = Some(v.trim().to_string());
            }
            _ => {}
        }
    }
    if !flags.is_empty() {
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
    }
    if !features.is_empty() || cpu_arch.is_some() {
        return arm_march(&features, cpu_arch.as_deref());
    }
    None
}

fn arm_march(features: &str, cpu_arch: Option<&str>) -> Option<String> {
    let has = |f: &str| features.split_whitespace().any(|x| x.eq_ignore_ascii_case(f));
    let is_64bit = has("asimd") || has("fp") && matches!(cpu_arch, Some(a) if a.trim() == "8" || a.to_ascii_lowercase().contains("aarch64"));
    if !is_64bit {
        if has("half") || has("thumb") || has("edsp") || has("neon") || has("tls") {
            return Some("armv7-a".to_string());
        }
        if let Some(a) = cpu_arch {
            let a = a.trim();
            if let Ok(n) = a.parse::<u32>() {
                return Some(match n {
                    8 => "armv8-a".to_string(),
                    7 => "armv7-a".to_string(),
                    6 => "armv6".to_string(),
                    5 => "armv5".to_string(),
                    _ => format!("armv{}-a", n),
                });
            }
            let l = a.to_ascii_lowercase();
            if l.contains("aarch64") {
                return Some("armv8-a".to_string());
            }
        }
        return None;
    }
    if has("sve2") || has("sve2p1") || has("mops") || has("hbc") {
        return Some("armv9-a".to_string());
    }
    if has("sve") {
        return Some("armv8.2-a+sve".to_string());
    }
    if has("asimddp") || has("sha3") || has("sm3") || has("sm4") {
        return Some("armv8.2-a".to_string());
    }
    if has("asimdrdm") || has("lrcpc") || has("dcpop") || has("sha512") {
        return Some("armv8.1-a".to_string());
    }
    if let Some(a) = cpu_arch {
        let a = a.trim();
        if a == "8" || a.to_ascii_lowercase().contains("aarch64") {
            return Some("armv8-a".to_string());
        }
        if let Ok(n) = a.parse::<u32>() {
            if n >= 9 {
                return Some("armv9-a".to_string());
            }
            if n == 8 {
                return Some("armv8-a".to_string());
            }
        }
    }
    Some("armv8-a".to_string())
}

pub fn numa_nodes() -> u64 {
    let Ok(e) = std::fs::read_dir("/sys/devices/system/node") else {
        return 1;
    };
    e.flatten().count() as u64
}

pub fn detect() -> CpuInfo {
    let text = read_file("/proc/cpuinfo").unwrap_or_default();
    detect_from_text(&text)
}

fn detect_from_text(text: &str) -> CpuInfo {
    let mut info = CpuInfo::default();

    let mut unique_cores: HashSet<(String, String)> = HashSet::new();
    let mut phys_ids: HashSet<String> = HashSet::new();
    let mut phys_id = String::new();
    let mut core_id = String::new();
    let mut cur_mhz: Vec<u64> = Vec::new();

    let mut arm_hardware: Option<String> = None;
    let mut arm_model_line: Option<String> = None;
    let mut arm_implementer: Option<String> = None;
    let mut arm_parts: Vec<String> = Vec::new();
    let mut arm_arch: Option<String> = None;

    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() {

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
            "physical id" => {
                phys_id = val.to_string();
                if !val.is_empty() {
                    phys_ids.insert(val.to_string());
                }
            }
            "core id" => core_id = val.to_string(),
            "cpu MHz" => {
                if let Ok(m) = val.parse::<f64>() {
                    cur_mhz.push(m as u64);
                }
            }
            "Model" if arm_model_line.is_none() && !val.is_empty() => {
                arm_model_line = Some(val.to_string());
            }
            "Hardware" if arm_hardware.is_none() && !val.is_empty() => {
                arm_hardware = Some(val.to_string());
            }
            "CPU implementer" if arm_implementer.is_none() && !val.is_empty() => {
                arm_implementer = Some(val.to_string());
            }
            "CPU part" if !val.is_empty() && !arm_parts.contains(&val.to_string()) => {
                arm_parts.push(val.to_string());
            }
            "CPU architecture" if arm_arch.is_none() && !val.is_empty() => {
                arm_arch = Some(val.to_string());
            }
            _ => {}
        }
    }
    if !phys_id.is_empty() || !core_id.is_empty() {
        unique_cores.insert((phys_id, core_id));
    }

    if info.logical_cores == 0 {
        info.logical_cores = count_cpus_sysfs();
    }

    info.packages = phys_ids.len();
    if info.packages == 0 && info.logical_cores > 0 {
        info.packages = 1;
    }

    info.physical_cores = unique_cores
        .iter()
        .filter(|(p, c)| !p.is_empty() || !c.is_empty())
        .count();
    if info.physical_cores == 0 {
        info.physical_cores = count_physical_cores_sysfs().unwrap_or(0);
    }
    if info.physical_cores == 0 {
        info.physical_cores = info.logical_cores;
    }

    if info.model.is_empty() {
        info.model = arm_model_fallback(
            arm_model_line.as_deref(),
            arm_hardware.as_deref(),
            arm_implementer.as_deref(),
            &arm_parts,
            arm_arch.as_deref(),
        );
    }
    if info.vendor.is_empty() {
        if let Some(imp) = arm_implementer.as_deref() {
            let v = arm_vendor_name(imp);
            if !v.is_empty() {
                info.vendor = v;
            }
        }
    }
    if info.vendor.is_empty() && !info.model.is_empty() {
        let m = info.model.to_ascii_lowercase();
        for (prefix, vendor) in [
            ("apple ", "Apple"),
            ("qualcomm ", "Qualcomm"),
            ("samsung ", "Samsung"),
            ("broadcom ", "Broadcom"),
            ("nvidia ", "NVIDIA"),
            ("ampere ", "Ampere"),
            ("cavium ", "Cavium"),
            ("fujitsu ", "Fujitsu"),
            ("huawei ", "Huawei"),
            ("mediatek ", "MediaTek"),
            ("rockchip ", "Rockchip"),
        ] {
            if m.starts_with(prefix) {
                info.vendor = vendor.to_string();
                break;
            }
        }
    }

    info.freq_max_mhz = max_freq_sysfs("cpuinfo_max_freq");
    info.freq_cur_mhz = max_freq_sysfs("scaling_cur_freq");
    if info.freq_cur_mhz == 0 {
        info.freq_cur_mhz = cur_mhz.first().copied().unwrap_or(0);
    }

    detect_hybrid(&mut info);

    if info.freq_max_mhz == 0 {
        info.freq_max_mhz = info.freq_cur_mhz;

        if info.freq_max_mhz == 0 {
            info.freq_max_mhz = cur_mhz.first().copied().unwrap_or(0);
        }
    }

    info
}

fn count_cpus_sysfs() -> usize {
    let Ok(entries) = std::fs::read_dir("/sys/devices/system/cpu") else {
        return 0;
    };
    entries
        .flatten()
        .filter(|e| {
            let n = e.file_name().to_string_lossy().into_owned();
            n.len() > 3
                && n.starts_with("cpu")
                && n[3..].chars().all(|c| c.is_ascii_digit())
        })
        .count()
}

fn count_physical_cores_sysfs() -> Option<usize> {
    let Ok(entries) = std::fs::read_dir("/sys/devices/system/cpu") else {
        return None;
    };
    let mut set: HashSet<(String, String)> = HashSet::new();
    let mut any = false;
    for e in entries.flatten() {
        let n = e.file_name().to_string_lossy().into_owned();
        if !(n.len() > 3 && n.starts_with("cpu") && n[3..].chars().all(|c| c.is_ascii_digit())) {
            continue;
        }
        let base = e.path().join("topology");
        let pkg = std::fs::read_to_string(base.join("physical_package_id"))
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        let core = std::fs::read_to_string(base.join("core_id"))
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        if pkg.is_empty() && core.is_empty() {
            continue;
        }
        any = true;
        set.insert((pkg, core));
    }
    if !any {
        return None;
    }
    Some(set.len())
}

fn max_freq_sysfs(file: &str) -> u64 {
    let mut best = 0u64;
    for cpu in 0..count_cpus_sysfs().max(1) {
        let p = format!("/sys/devices/system/cpu/cpu{}/cpufreq/{}", cpu, file);
        if let Ok(s) = std::fs::read_to_string(&p) {
            if let Ok(khz) = s.trim().parse::<u64>() {
                best = best.max(khz / 1000);
            }
        }
        if cpu > 4096 {
            break;
        }
    }
    best
}

fn arm_vendor_name(implementer: &str) -> String {
    match implementer.trim().to_ascii_lowercase().as_str() {
        "0x41" => "ARM".to_string(),
        "0x42" => "Broadcom".to_string(),
        "0x43" => "Cavium".to_string(),
        "0x44" => "DEC".to_string(),
        "0x46" => "Fujitsu".to_string(),
        "0x48" => "HiSilicon".to_string(),
        "0x49" => "Infineon".to_string(),
        "0x4d" => "Motorola".to_string(),
        "0x4e" => "NVIDIA".to_string(),
        "0x50" => "APM".to_string(),
        "0x51" => "Qualcomm".to_string(),
        "0x53" => "Samsung".to_string(),
        "0x56" => "Marvell".to_string(),
        "0x61" => "Apple".to_string(),
        "0x69" => "Intel".to_string(),
        "0xc0" => "Ampere".to_string(),
        _ => String::new(),
    }
}

fn arm_part_name(implementer: &str, part: &str) -> Option<String> {
    let imp = implementer.trim().to_ascii_lowercase();
    let p = part.trim().to_ascii_lowercase();
    let p = p.strip_prefix("0x").unwrap_or(&p);
    let num = u32::from_str_radix(p.trim_start_matches('0'), 16)
        .ok()
        .or_else(|| u32::from_str_radix(p, 16).ok())?;
    match imp.as_str() {
        "0x41" => Some(
            match num {
                0xd00 => "Cortex-A32",
                0xd02 => "Cortex-A34",
                0xd03 => "Cortex-A53",
                0xd04 => "Cortex-A35",
                0xd05 => "Cortex-A55",
                0xd06 => "Cortex-A65",
                0xd07 => "Cortex-A57",
                0xd08 => "Cortex-A72",
                0xd09 => "Cortex-A73",
                0xd0a => "Cortex-A75",
                0xd0b => "Cortex-A76",
                0xd0c => "Neoverse-N1",
                0xd0d => "Cortex-A77",
                0xd0e => "Cortex-A76AE",
                0xd13 => "Cortex-R52",
                0xd20 => "Cortex-M23",
                0xd21 => "Cortex-M33",
                0xd22 => "Cortex-M55",
                0xd40 => "Neoverse-V1",
                0xd41 => "Cortex-A78",
                0xd44 => "Cortex-X1",
                0xd46 => "Cortex-A510",
                0xd47 => "Cortex-A710",
                0xd48 => "Cortex-X2",
                0xd49 => "Neoverse-N2",
                0xd4a => "Neoverse-E1",
                0xd4b => "Cortex-A78C",
                0xd4d => "Cortex-A715",
                0xd4e => "Cortex-X3",
                0xd4f => "Neoverse-V2",
                0xc05 => "Cortex-A5",
                0xc07 => "Cortex-A7",
                0xc08 => "Cortex-A8",
                0xc09 => "Cortex-A9",
                0xc0d => "Cortex-A12",
                0xc0e => "Cortex-A17",
                0xc0f => "Cortex-A15",
                _ => return None,
            }
            .to_string(),
        ),
        "0x51" => Some(
            match num {
                0x001 => "Oryon",
                0x800 => "Kryo",
                0x801 => "Kryo 2xx",
                0x802 | 0x803 => "Kryo 385",
                0x804 | 0x805 => "Kryo 485",
                0xc00 => "Kryo 585",
                _ => return None,
            }
            .to_string(),
        ),
        "0x61" => Some(
            match num {
                0x020 | 0x021 | 0x022 | 0x023 => "M1",
                0x030 | 0x031 | 0x032 | 0x033 => "M2",
                0x034 | 0x035 | 0x036 => "M3",
                _ => "Silicon",
            }
            .to_string(),
        ),
        _ => None,
    }
}

fn device_tree_model() -> Option<String> {
    for p in [
        "/proc/device-tree/model",
        "/sys/firmware/devicetree/base/model",
    ] {
        if let Ok(b) = std::fs::read(p) {
            let s: String = b
                .into_iter()
                .take_while(|&c| c != 0)
                .map(|c| c as char)
                .collect();
            let s = s.trim().to_string();
            if !s.is_empty() {
                return Some(s);
            }
        }
    }
    None
}

fn arm_model_fallback(
    model_line: Option<&str>,
    hardware: Option<&str>,
    implementer: Option<&str>,
    parts: &[String],
    arch: Option<&str>,
) -> String {
    if let Some(m) = model_line {
        let m = m.trim();
        if !m.is_empty() {
            return m.to_string();
        }
    }
    if let Some(imp) = implementer {
        let vendor = arm_vendor_name(imp);
        let mut names: Vec<String> = Vec::new();
        for p in parts {
            if let Some(n) = arm_part_name(imp, p) {
                if !names.contains(&n) {
                    names.push(n);
                }
            }
        }
        if !names.is_empty() {
            let cores = names.join(" + ");
            if vendor.is_empty() || vendor == "ARM" {
                if cores.starts_with("Cortex") || cores.starts_with("Neoverse") {
                    return format!("ARM {}", cores);
                }
                return cores;
            }
            if vendor == "Apple" {
                if cores == "Silicon" {
                    return "Apple Silicon".to_string();
                }
                return format!("Apple {}", cores);
            }
            if cores.starts_with(&vendor) {
                return cores;
            }
            return format!("{} {}", vendor, cores);
        }
        if !vendor.is_empty() {
            if let Some(a) = arch {
                let a = a.trim();
                if let Ok(n) = a.parse::<u32>() {
                    return format!("{} ARMv{} Processor", vendor, n);
                }
                if a.to_ascii_lowercase().contains("aarch64") {
                    return format!("{} ARMv8 Processor", vendor);
                }
            }
            return format!("{} Processor", vendor);
        }
    }
    if let Some(h) = hardware {
        let h = h.trim();
        if !h.is_empty() {
            return h.to_string();
        }
    }
    device_tree_model().unwrap_or_default()
}

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

#[cfg(test)]
mod tests {
    use super::*;

    const RPI_CPUINFO: &str = "processor\t: 0\nBogoMIPS\t: 108.00\nFeatures\t: fp asimd evtstrm crc32 cpuid\nCPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0xd08\nCPU revision\t: 3\n\nprocessor\t: 1\nBogoMIPS\t: 108.00\nFeatures\t: fp asimd evtstrm crc32 cpuid\nCPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0xd08\nCPU revision\t: 3\n\nHardware\t: BCM2835\nModel\t\t: Raspberry Pi 4 Model B Rev 1.2\n";

    const BIG_LITTLE_CPUINFO: &str = "processor\t: 0\nFeatures\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\nCPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\nCPU part\t: 0xd0b\nCPU revision\t: 1\n\nprocessor\t: 4\nFeatures\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm\nCPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\nCPU part\t: 0xd05\nCPU revision\t: 0\n";

    #[test]
    fn arm_rpi_model_prefers_model_line() {
        let info = detect_from_text(RPI_CPUINFO);
        assert_eq!(info.model, "Raspberry Pi 4 Model B Rev 1.2");
        assert_eq!(info.vendor, "ARM");
        assert_eq!(info.logical_cores, 2);
        assert_eq!(info.packages, 1);
    }

    #[test]
    fn arm_big_little_joins_core_names() {
        let info = detect_from_text(BIG_LITTLE_CPUINFO);
        assert_eq!(info.model, "ARM Cortex-A76 + Cortex-A55");
        assert_eq!(info.logical_cores, 2);
    }

    #[test]
    fn arm_march_tiers() {
        assert_eq!(
            march_from_text("Features\t: fp asimd evtstrm crc32 cpuid\nCPU architecture: 8\n").as_deref(),
            Some("armv8-a")
        );
        assert_eq!(
            march_from_text("Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp\nCPU architecture: 8\n").as_deref(),
            Some("armv8.2-a")
        );
        assert_eq!(
            march_from_text("Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm lrcpc dcpop asimddp sve\nCPU architecture: 8\n").as_deref(),
            Some("armv8.2-a+sve")
        );
        assert_eq!(
            march_from_text("Features\t: fp asimd evtstrm sve2 mops hbc\nCPU architecture: 8\n").as_deref(),
            Some("armv9-a")
        );
        assert_eq!(
            march_from_text("Features\t: half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae evtstrm crc32\nCPU architecture: 7\n").as_deref(),
            Some("armv7-a")
        );
    }

    #[test]
    fn x86_march_still_detected() {
        let text = "processor\t: 0\nvendor_id\t: GenuineIntel\nmodel name\t: Intel(R) Core(TM) i7\nflags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ssse3 sse4_1 sse4_2 popcnt lahf_lm cx16 movbe\n";
        assert_eq!(march_from_text(text).as_deref(), Some("x86_64-v2"));
    }

    #[test]
    fn x86_detect_sets_packages() {
        let text = "processor\t: 0\nvendor_id\t: GenuineIntel\nmodel name\t: Intel(R) Core(TM) i7\nphysical id\t: 0\ncore id\t\t: 0\ncpu MHz\t\t: 3400.000\n\nprocessor\t: 1\nvendor_id\t: GenuineIntel\nmodel name\t: Intel(R) Core(TM) i7\nphysical id\t: 0\ncore id\t\t: 1\ncpu MHz\t\t: 3400.000\n";
        let info = detect_from_text(text);
        assert_eq!(info.logical_cores, 2);
        assert_eq!(info.physical_cores, 2);
        assert_eq!(info.packages, 1);
        assert_eq!(info.model, "Intel(R) Core(TM) i7");
    }
}
