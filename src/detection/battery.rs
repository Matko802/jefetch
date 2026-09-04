use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct BatteryInfo {
    pub name: String,
    pub manufacturer: String,
    pub model: String,
    pub technology: String,
    pub capacity_percent: u8,
    pub status: String,

    pub energy_now: f64,
    pub energy_full: f64,
    pub temp_c: f64,
    pub voltage_mv: u64,
}

pub fn detect() -> Vec<BatteryInfo> {
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/sys/class/power_supply") else {
        return out;
    };
    for entry in entries.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if !name.starts_with("BAT") {
            continue;
        }
        let base = format!("/sys/class/power_supply/{}", name);
        let t = |p: &str| read_file(&format!("{}/{}", base, p))
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        let cap = t("capacity").parse::<u8>().ok().unwrap_or(0);
        if cap == 0 && t("capacity").is_empty() {
            continue;
        }
        let f = |p: &str| t(p).parse::<f64>().ok().unwrap_or(0.0);
        out.push(BatteryInfo {
            name,
            manufacturer: t("manufacturer"),
            model: t("model_name"),
            technology: t("technology"),
            capacity_percent: cap,
            status: t("status"),
            energy_now: f("energy_now") / 3_600_000.0,
            energy_full: f("energy_full") / 3_600_000.0,
            temp_c: f("temp") / 10.0,
            voltage_mv: t("voltage_now").parse::<u64>().ok().unwrap_or(0) / 1000,
        });
    }
    out
}
