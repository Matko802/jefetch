use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct GpuInfo {
    pub vendor: String,
    pub vendor_name: String,
    pub model: String,
    pub driver: String,
    pub device_id: String,
}

/// Known PCI vendor IDs for display adapters.
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

/// Enumerate GPUs from sysfs DRM cards.
pub fn detect() -> Vec<GpuInfo> {
    let ids = parse_pci_ids();
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/sys/class/drm") else {
        return out;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        // Only interface "cardN" dirs; skip cardN-<connector> duplicates.
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

        if let Some(model) = ids.lookup(&vendor, &device) {
            g.model = model;
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

/// Minimal pci.ids parser ("XXXX  <vendor>" / "  XXXX  <device>").
struct PciIds {
    map: Vec<(String, String, String)>, // (vendor, device, device_name)
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
                    continue; // subsystem lines, ignored
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

fn parse_pci_ids() -> PciIds {
    for path in ["/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids"] {
        if let Ok(text) = std::fs::read_to_string(path) {
            return PciIds::parse(&text);
        }
    }
    PciIds { map: Vec::new() }
}