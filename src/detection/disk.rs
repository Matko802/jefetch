use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct DiskInfo {
    pub mountpoint: String,
    pub mount_from: String,
    pub filesystem: String,
    pub total: u64,
    pub used: u64,
    pub available: u64,
    pub options: String,
    pub name: String,
}

/// Detect disks for the given mount folders. By default the root filesystem.
pub fn detect(folders: &[String]) -> Vec<DiskInfo> {
    let mountpoints: Vec<String> = if folders.is_empty() {
        vec!["/".to_string()]
    } else {
        folders.to_vec()
    };
    let mut out = Vec::new();
    for mp in mountpoints {
        if let Some(info) = stat_one(&mp) {
            out.push(info);
        }
    }
    out
}

fn stat_one(mp: &str) -> Option<DiskInfo> {
    let mut st: libc::statvfs = unsafe { std::mem::zeroed() };
    let c = std::ffi::CString::new(mp).ok()?;
    if unsafe { libc::statvfs(c.as_ptr(), &mut st) } != 0 {
        return None;
    }
    let frsize = st.f_frsize.max(1) as u64;
    let total = st.f_blocks as u64 * frsize;
    let free = st.f_bfree as u64 * frsize;
    let available = st.f_bavail as u64 * frsize;
    let used = total.saturating_sub(free);

    let (filesystem, mount_from, options) = mounts_info(mp);
    let name = label_for(&mount_from);
    Some(DiskInfo {
        mountpoint: mp.to_string(),
        filesystem,
        mount_from,
        total,
        used,
        available,
        options,
        name,
    })
}

/// Look up the filesystem type, source device and mount options of a mount
/// point in /proc/self/mounts (space-escaped like `/dev/sda2\040...`).
fn mounts_info(mp: &str) -> (String, String, String) {
    let mut fs = String::new();
    let mut from = String::new();
    let mut opts = String::new();
    if let Some(text) = read_file("/proc/self/mounts") {
        for line in text.lines() {
            let parts: Vec<&str> = line.split(' ').collect();
            if parts.len() < 4 {
                continue;
            }
            let mpoint = parts[1].replace("\\040", " ");
            if mpoint == mp {
                from = parts[0].to_string();
                fs = parts[2].to_string();
                opts = parts[3].to_string();
                break;
            }
        }
    }
    (fs, from, opts)
}

/// Resolve the filesystem name (label) from /dev/disk/by-label symlinks.
fn label_for(mount_from: &str) -> String {
    if mount_from.is_empty() {
        return String::new();
    }
    if let Ok(entries) = std::fs::read_dir("/dev/disk/by-label") {
        for e in entries.flatten() {
            let Ok(target) = std::fs::read_link(e.path()) else {
                continue;
            };
            if target.to_string_lossy().trim_start_matches("../..") == mount_from
                || std::fs::canonicalize(e.path())
                    .map(|c| c.to_string_lossy() == mount_from)
                    .unwrap_or(false)
            {
                return e.file_name().to_string_lossy().into_owned();
            }
        }
    }
    mount_from
        .rsplit('/')
        .next()
        .unwrap_or("")
        .to_string()
}