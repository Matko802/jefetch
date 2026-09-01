use crate::detection::getenv;

#[derive(Debug, Clone, Default)]
pub struct DeInfo {
    pub name: String,
}

/// Desktop environment from environment variables (fastfetch order).
pub fn detect() -> DeInfo {
    let mut info = DeInfo::default();
    for key in [
        "XDG_CURRENT_DESKTOP",
        "XDG_SESSION_DESKTOP",
        "DESKTOP_SESSION",
        "GDMSESSION",
    ] {
        if let Some(v) = getenv(key) {
            let v = v.trim().to_string();
            if !v.is_empty() {
                info.name = v;
                break;
            }
        }
    }
    info
}