use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct BoardInfo {
    pub name: String,
    pub vendor: String,
    pub version: String,
    pub date: String,
}

pub fn detect() -> BoardInfo {
    let t = |p: &str| read_file(p).map(|s| s.trim().to_string()).unwrap_or_default();
    let mut info = BoardInfo {
        name: t("/sys/class/dmi/id/board_name"),
        vendor: t("/sys/class/dmi/id/board_vendor"),
        version: t("/sys/class/dmi/id/board_version"),
        date: t("/sys/class/dmi/id/board_asset_tag"),
    };
    if info.name.is_empty() {
        info.name = device_tree_model();
    }
    if info.vendor.is_empty() {
        info.vendor = device_tree_vendor(&info.name);
    }
    info
}

pub fn device_tree_model() -> String {
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
                return s;
            }
        }
    }
    String::new()
}

pub fn product_name() -> String {
    let t = |p: &str| read_file(p).map(|s| s.trim().to_string()).unwrap_or_default();
    let dmi = t("/sys/class/dmi/id/product_name");
    if !dmi.is_empty() {
        return dmi;
    }
    device_tree_model()
}

fn device_tree_vendor(model: &str) -> String {
    let first = model.split_whitespace().next().unwrap_or("");
    match first {
        "Raspberry" => "Raspberry Pi".to_string(),
        "NVIDIA" => "NVIDIA".to_string(),
        "Apple" => "Apple".to_string(),
        "Samsung" => "Samsung".to_string(),
        "Qualcomm" => "Qualcomm".to_string(),
        "Lenovo" => "Lenovo".to_string(),
        "ASUS" => "ASUS".to_string(),
        "Pine64" => "Pine64".to_string(),
        "Radxa" => "Radxa".to_string(),
        "Orange" => "Orange Pi".to_string(),
        "Banana" => "Banana Pi".to_string(),
        "Hardkernel" => "Hardkernel".to_string(),
        "SolidRun" => "SolidRun".to_string(),
        _ => String::new(),
    }
}
