use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct GpuInfo {
    pub vendor: String,
    pub vendor_name: String,
    pub model: String,
    pub driver: String,
    pub device_id: String,
}

const VENDOR_NAMES: &[(&str, &str)] = &[
    ("0x1002", "AMD/ATI"),
    ("0x10de", "NVIDIA"),
    ("0x8086", "Intel"),
    ("0x1a03", "ASPEED"),
    ("0x106b", "Apple"),
    ("0x1022", "AMD"),
    ("0x14e3", "Loongson"),
    ("0x13b5", "Arm"),
    ("0x19e5", "Huawei"),
    ("0x1518", "Kontron"),
];

pub fn detect() -> Vec<GpuInfo> {
    let ids = parse_pci_ids();
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/sys/class/drm") else {
        return out;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();

        if !name.starts_with("card") {
            continue;
        }
        let after = &name[4..];
        if !after.chars().all(|c| c.is_ascii_digit()) {
            continue;
        }
        let base = entry.path();
        let vendor = read_file(base.join("device/vendor"))
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        if vendor.is_empty() {
            continue;
        }
        let device = read_file(base.join("device/device"))
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        let revision = read_file(base.join("device/revision"))
            .map(|s| {
                s.trim()
                    .strip_prefix("0x")
                    .or_else(|| s.trim().strip_prefix("0X"))
                    .unwrap_or(s.trim())
                    .to_ascii_uppercase()
            })
            .unwrap_or_default();

        let mut g = GpuInfo {
            vendor: vendor.clone(),
            device_id: device.clone(),
            vendor_name: vendor_name(&vendor),
            model: String::new(),
            driver: String::new(),
        };
        if let Some(uevent) = read_file(base.join("device/uevent")) {
            for line in uevent.lines() {
                if let Some((_, v)) = line.split_once('=') {
                    if line.starts_with("DRIVER=") {
                        g.driver = v.trim().to_string();
                        break;
                    }
                }
            }
        }

        if let Some(name) = amdgpu_lookup(&vendor, &device, &revision) {
            g.model = name;
        }
        if g.model.is_empty() {
            if let Some(model) = ids.lookup(&vendor, &device) {
                g.model = model;
            }
        }
        if g.model.is_empty() {
            g.model = format!("{} ({})", g.vendor_name, device);
        }
        out.push(g);
    }
    out
}

fn vendor_name(id: &str) -> String {
    VENDOR_NAMES
        .iter()
        .find(|(v, _)| v.eq_ignore_ascii_case(id))
        .map(|(_, n)| n.to_string())
        .unwrap_or_else(|| id.to_string())
}

#[derive(Clone)]
struct PciIds {
    map: Vec<(String, String, String)>,
}

impl PciIds {
    fn parse(text: &str) -> PciIds {
        let mut map = Vec::new();
        let mut cur_vendor = String::new();
        for line in text.lines() {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if !line.starts_with('\t') {
                if let Some((id, name)) = split_ids_line(line) {
                    cur_vendor = id;
                    let _ = name;
                }
            } else {
                let line = line.trim_start_matches('\t');
                if line.starts_with('\t') {
                    continue;
                }
                if let Some((dev, name)) = split_ids_line(line) {
                    map.push((cur_vendor.clone(), dev, name));
                }
            }
        }
        PciIds { map }
    }

    fn lookup(&self, vendor: &str, device: &str) -> Option<String> {
        self.map
            .iter()
            .find(|(v, d, _)| {
                v.eq_ignore_ascii_case(vendor.trim_start_matches("0x"))
                    && d.eq_ignore_ascii_case(device.trim_start_matches("0x"))
            })
            .map(|(_, _, n)| n.clone())
    }
}

fn split_ids_line(line: &str) -> Option<(String, String)> {
    let line = line.trim();
    let id: String = line.chars().take(4).collect();
    let rest = &line[4..];
    if id.len() != 4 || !id.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    Some((id.to_ascii_lowercase(), rest.trim().to_string()))
}

static PCI_CACHE: std::sync::OnceLock<PciIds> = std::sync::OnceLock::new();
static AMDGPU_CACHE: std::sync::OnceLock<Vec<(String, String, String)>> =
    std::sync::OnceLock::new();

fn pci_ids_paths() -> Vec<String> {
    let mut out = Vec::new();
    for key in ["PCI_IDS_PATH", "JEFETCH_PCI_IDS"] {
        if let Ok(p) = std::env::var(key) {
            if !p.trim().is_empty() {
                out.push(p);
            }
        }
    }
    out.push("/usr/share/hwdata/pci.ids".to_string());
    out.push("/usr/share/misc/pci.ids".to_string());
    out.push("/etc/pci.ids".to_string());
    out.push("/run/current-system/sw/share/hwdata/pci.ids".to_string());
    out.push("/run/current-system/sw/share/misc/pci.ids".to_string());
    out.push("/run/current-system/sw/share/pci.ids".to_string());
    if let Some(p) = newest_store_file("-hwdata-", "share/hwdata/pci.ids") {
        out.push(p);
    }
    out
}

fn amdgpu_ids_paths() -> Vec<String> {
    let mut out = Vec::new();
    if let Ok(p) = std::env::var("AMDGPU_IDS_PATH") {
        if !p.trim().is_empty() {
            out.push(p);
        }
    }
    out.push("/run/current-system/sw/share/libdrm/amdgpu.ids".to_string());
    out.push("/usr/share/libdrm/amdgpu.ids".to_string());
    if let Some(p) = newest_store_file("-libdrm-", "share/libdrm/amdgpu.ids") {
        out.push(p);
    }
    out
}

fn newest_store_file(marker: &str, suffix: &str) -> Option<String> {
    let dir = std::fs::read_dir("/nix/store").ok()?;
    let mut best: Option<(Vec<u64>, String)> = None;
    for entry in dir.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let Some(version) = store_version(&name, marker) else {
            continue;
        };
        let path = format!("/nix/store/{}/{}", name, suffix);
        if !std::path::Path::new(&path).exists() {
            continue;
        }
        let replace = match &best {
            Some((v, _)) => version > *v,
            None => true,
        };
        if replace {
            best = Some((version, path));
        }
    }
    best.map(|(_, p)| p)
}

fn store_version(dir_name: &str, marker: &str) -> Option<Vec<u64>> {
    let (_, ver) = dir_name.split_once(marker)?;
    if ver.is_empty() {
        return None;
    }
    let parts: Option<Vec<u64>> = ver.split('.').map(|p| p.parse::<u64>().ok()).collect();
    let parts = parts?;
    if parts.is_empty() {
        return None;
    }
    Some(parts)
}

fn amdgpu_lookup(vendor: &str, device: &str, revision: &str) -> Option<String> {
    if !vendor.eq_ignore_ascii_case("0x1002") && !vendor.eq_ignore_ascii_case("0x1022") {
        return None;
    }
    if device.is_empty() || revision.is_empty() {
        return None;
    }
    let table = AMDGPU_CACHE.get_or_init(|| {
        for p in amdgpu_ids_paths() {
            if let Ok(text) = std::fs::read_to_string(&p) {
                return parse_amdgpu_ids(&text);
            }
        }
        Vec::new()
    });
    let did = device.trim_start_matches("0x").trim_start_matches("0X");
    table
        .iter()
        .find(|(d, r, _)| d.eq_ignore_ascii_case(did) && r.eq_ignore_ascii_case(revision))
        .map(|(_, _, n)| n.clone())
}

fn parse_amdgpu_ids(text: &str) -> Vec<(String, String, String)> {
    let mut out = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let mut parts = line.split(',');
        let (Some(did), Some(rev), Some(name)) = (parts.next(), parts.next(), parts.next()) else {
            continue;
        };
        let (did, rev, name) = (did.trim(), rev.trim(), name.trim());
        if did.is_empty() || rev.is_empty() || name.is_empty() {
            continue;
        }
        out.push((did.to_string(), rev.to_string(), name.to_string()));
    }
    out
}

fn parse_pci_ids() -> PciIds {
    if let Some(cached) = PCI_CACHE.get() {
        return PciIds { map: cached.map.clone() };
    }
    for path in pci_ids_paths() {
        if let Ok(text) = std::fs::read_to_string(&path) {
            let ids = PciIds::parse(&text);
            let _ = PCI_CACHE.set(PciIds { map: ids.map.clone() });
            return ids;
        }
    }
    PciIds { map: Vec::new() }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn store_version_orders_releases() {
        let v = |n: &str| store_version(n, "-hwdata-").unwrap();
        assert!(v("abc-hwdata-0.409") > v("abc-hwdata-0.381"));
        assert!(v("abc-hwdata-0.409") == v("xyz-hwdata-0.409"));
        assert!(store_version("abc-hwdata-", "-hwdata-").is_none());
        assert!(store_version("abc-pciutils-3.15.0", "-hwdata-").is_none());
        assert!(store_version("abc-libdrm-2.4.134", "-libdrm-").is_some());
    }

    #[test]
    fn amdgpu_ids_match_device_and_revision() {
        let text = "73FF,\tC1,\tAMD Radeon RX 6600 XT\n73FF,\tC7,\tAMD Radeon RX 6600\n1435,\tC1,\tAMD Radeon 660M\n";
        let table = parse_amdgpu_ids(text);
        let hit = table
            .iter()
            .find(|(d, r, _)| d.eq_ignore_ascii_case("73ff") && r.eq_ignore_ascii_case("c7"))
            .map(|(_, _, n)| n.clone());
        assert_eq!(hit.as_deref(), Some("AMD Radeon RX 6600"));
        assert_eq!(table.len(), 3);
    }

    #[test]
    fn pci_ids_parse_finds_device() {
        let text = "# comment\n\n1002  Advanced Micro Devices, Inc. [AMD/ATI]\n\t73ff  Navi 23 [Radeon RX 6600/6600 XT/6600M]\n";
        let ids = PciIds::parse(text);
        assert_eq!(
            ids.lookup("0x1002", "0x73ff").as_deref(),
            Some("Navi 23 [Radeon RX 6600/6600 XT/6600M]")
        );
        assert_eq!(ids.lookup("0x1002", "0x0000"), None);
    }
}
