use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct BrightnessInfo {
    pub name: String,
    pub value: u64,
    pub max: u64,
    pub percentage: u8,
}

/// Brightness of the main backlight device.
pub fn detect() -> Vec<BrightnessInfo> {
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/sys/class/backlight") else {
        return out;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        let base = format!("/sys/class/backlight/{}", name);
        let value = read_file(&format!("{}/brightness", base))
            .and_then(|s| s.trim().parse::<u64>().ok());
        let max = read_file(&format!("{}/max_brightness", base))
            .and_then(|s| s.trim().parse::<u64>().ok());
        let (Some(value), Some(max)) = (value, max) else { continue };
        if max == 0 {
            continue;
        }
        let percentage = ((value as f64 / max as f64) * 100.0).round() as u8;
        out.push(BrightnessInfo {
            name,
            value,
            max,
            percentage,
        });
    }
    out
}