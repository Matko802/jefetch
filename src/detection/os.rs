use crate::detection::{parse_key_value_file, read_file_lines, unquote};

#[derive(Debug, Clone, Default)]
pub struct OSInfo {
    pub name: String,
    pub version: String,
    pub version_id: String,
    pub id: String,
    pub id_like: String,
    pub pretty_name: String,
    pub arch: String,
    pub build_id: String,
    pub codename: String,
    pub variant: String,
    pub variant_id: String,
}

pub fn detect() -> OSInfo {
    let mut info = OSInfo::default();
    info.arch = arch();

    // Prefer /etc/os-release (or /usr/lib/os-release).
    let mut osrel = parse_key_value_file("/etc/os-release");
    if osrel.is_empty() {
        osrel = parse_key_value_file("/usr/lib/os-release");
    }
    for (k, v) in osrel {
        let v = unquote(&v);
        match k.as_str() {
            "NAME" => info.name = v,
            "VERSION" => info.version = v,
            "VERSION_ID" => info.version_id = v,
            "ID" => info.id = v,
            "ID_LIKE" => info.id_like = v,
            "PRETTY_NAME" => info.pretty_name = v,
            "BUILD_ID" => info.build_id = v,
            "CODENAME" | "VERSION_CODENAME" => info.codename = v,
            "VARIANT" => info.variant = v,
            "VARIANT_ID" => info.variant_id = v,
            _ => {}
        }
    }

    // Fallback for very minimal systems.
    if info.name.is_empty() {
        for line in read_file_lines("/etc/issue") {
            let line = line.trim_end();
            if !line.is_empty() {
                info.name = line.trim_end_matches("\\n").trim_end_matches('\\').to_string();
                break;
            }
        }
    }
    if info.name.is_empty() {
        info.name = "Linux".to_string();
    }

    info
}

pub fn arch() -> String {
    let a = std::env::consts::ARCH;
    // fastfetch reports x86_64 etc; std already gives matching triplets.
    match a {
        "x86_64" => "x86_64".to_string(),
        "aarch64" => "aarch64".to_string(),
        other => other.to_string(),
    }
}
