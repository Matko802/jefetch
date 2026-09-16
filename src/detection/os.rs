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

static CACHE: std::sync::OnceLock<OSInfo> = std::sync::OnceLock::new();

pub fn detect() -> OSInfo {
    CACHE.get_or_init(detect_uncached).clone()
}

fn detect_uncached() -> OSInfo {
    let mut info = OSInfo::default();
    info.arch = arch();

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
    normalize_arch(&uname_machine().unwrap_or_else(|| std::env::consts::ARCH.to_string()))
}

fn uname_machine() -> Option<String> {
    unsafe {
        let mut u: libc::utsname = std::mem::zeroed();
        if libc::uname(&mut u) != 0 {
            return None;
        }
        let raw = &u.machine as *const libc::c_char as *const u8;
        let len = libc::strlen(raw as *const libc::c_char);
        let bytes = std::slice::from_raw_parts(raw, len);
        std::str::from_utf8(bytes).ok().map(|s| s.to_string())
    }
}

fn normalize_arch(a: &str) -> String {
    match a {
        "x86_64" | "x86-64" | "amd64" => "x86_64".to_string(),
        "aarch64" | "arm64" | "aarch64_be" => "aarch64".to_string(),
        "armv7l" | "armv7b" | "armv7hl" => "armv7l".to_string(),
        "armv6l" => "armv6l".to_string(),
        "arm" | "armv5tel" | "armv5tejl" | "armv8l" => "arm".to_string(),
        "i386" | "i486" | "i586" | "i686" | "x86" => "i686".to_string(),
        "riscv64" => "riscv64".to_string(),
        "loongarch64" => "loongarch64".to_string(),
        "ppc64le" | "ppc64" | "powerpc64le" => "ppc64le".to_string(),
        "s390x" => "s390x".to_string(),
        other => other.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn arch_normalizes_common_machines() {
        assert_eq!(normalize_arch("x86_64"), "x86_64");
        assert_eq!(normalize_arch("amd64"), "x86_64");
        assert_eq!(normalize_arch("aarch64"), "aarch64");
        assert_eq!(normalize_arch("arm64"), "aarch64");
        assert_eq!(normalize_arch("armv7l"), "armv7l");
        assert_eq!(normalize_arch("armv6l"), "armv6l");
        assert_eq!(normalize_arch("i686"), "i686");
        assert_eq!(normalize_arch("riscv64"), "riscv64");
    }
}
