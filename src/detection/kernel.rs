use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct KernelInfo {
    pub sysname: String,
    pub release: String,
    pub version: String,
}

pub fn detect() -> KernelInfo {
    let mut info = KernelInfo::default();
    info.sysname = uname_sysname();
    info.release = read_file("/proc/sys/kernel/osrelease")
        .map(|s| s.trim().to_string())
        .unwrap_or_default();

    // /proc/sys/kernel/version holds the gcc build version string.
    if let Some(v) = read_file("/proc/sys/kernel/version") {
        info.version = v.trim().to_string();
    }
    info
}

fn uname_sysname() -> String {
    // Fastfetch reports "Linux".
    "Linux".to_string()
}
