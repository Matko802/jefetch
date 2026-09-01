use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct BoardInfo {
    pub name: String,
    pub vendor: String,
    pub version: String,
    pub date: String,
}

/// Read motherboard info from DMI (works on x86; values can be empty on
/// some systems without DMI populated).
pub fn detect() -> BoardInfo {
    let t = |p: &str| read_file(p).map(|s| s.trim().to_string()).unwrap_or_default();
    BoardInfo {
        name: t("/sys/class/dmi/id/board_name"),
        vendor: t("/sys/class/dmi/id/board_vendor"),
        version: t("/sys/class/dmi/id/board_version"),
        date: t("/sys/class/dmi/id/board_asset_tag"),
    }
}