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

pub fn detect(folders: &[String]) -> Vec<DiskInfo> {
    if !folders.is_empty() {
        let mut out = Vec::new();
        for mp in folders {
            if let Some(info) = stat_one(mp) {
                out.push(info);
            }
        }
        return out;
    }
    let mut out = Vec::new();
    let mut seen_from: Vec<String> = Vec::new();
    for (from, mp, _) in physical_mounts() {
        if seen_from.iter().any(|s| s == &from) {
            continue;
        }
        if let Some(info) = stat_one(&mp) {
            seen_from.push(from);
            out.push(info);
        }
    }
    out
}

fn physical_mounts() -> Vec<(String, String, String)> {
    let mut out = Vec::new();
    let text = read_file("/proc/self/mounts").unwrap_or_default();
    for line in text.lines() {
        let parts: Vec<&str> = line.split(' ').collect();
        if parts.len() < 3 {
            continue;
        }
        let from = parts[0].to_string();
        let mp = parts[1].replace("\\040", " ");
        let fs = parts[2].to_string();
        if wants_mount(&from, &mp, &fs, is_block_device(&from)) {
            out.push((from, mp, fs));
        }
    }
    out
}

fn wants_mount(from: &str, mp: &str, fs: &str, is_block: bool) -> bool {
    if mp == "/" {
        return true;
    }
    if from == "none" {
        return false;
    }
    if fs == "zfs" || fs == "fuse.sshfs" {
        return true;
    }
    if !from.starts_with("/dev/") {
        return false;
    }
    let base = &from[5..];
    if base.starts_with("loop") || base.starts_with("ram") || base.starts_with("fd") {
        return false;
    }
    if mp == "/boot" || mp == "/boot/efi" || mp == "/efi" {
        return false;
    }
    is_block
}

fn is_block_device(path: &str) -> bool {
    let c = match std::ffi::CString::new(path) {
        Ok(c) => c,
        Err(_) => return false,
    };
    let mut st: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe { libc::stat(c.as_ptr(), &mut st) } != 0 {
        return false;
    }
    (st.st_mode as u32 & libc::S_IFMT as u32) == libc::S_IFBLK as u32
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn physical_mounts_follow_fastfetch_rules() {
        assert!(wants_mount("/dev/nvme0n1p2", "/", "btrfs", false));
        assert!(wants_mount("/dev/sda1", "/mnt/ssd", "ext4", true));
        assert!(wants_mount("/dev/sdb1", "/run/media/matko/USB", "vfat", true));
        assert!(wants_mount("tank", "/tank", "zfs", false));
        assert!(wants_mount("sshfs#host:/x", "/mnt/x", "fuse.sshfs", false));
        assert!(!wants_mount("tmpfs", "/tmp", "tmpfs", false));
        assert!(!wants_mount("overlay", "/var/lib/docker/overlay2", "overlay", false));
        assert!(!wants_mount("/dev/loop0", "/snap/core", "squashfs", true));
        assert!(!wants_mount("proc", "/proc", "proc", false));
        assert!(!wants_mount("none", "/mnt/x", "ext4", true));
        assert!(!wants_mount("gvfsd-fuse", "/run/user/1000/gvfs", "fuse.gvfsd-fuse", false));
        assert!(!wants_mount("/dev/nvme0n1p1", "/boot", "vfat", true));
        assert!(!wants_mount("/dev/nvme0n1p1", "/boot/efi", "vfat", true));
        assert!(!wants_mount("/dev/sda1", "/mnt/ssd", "ext4", false));
    }

    #[test]
    fn block_check_matches_reality() {
        assert!(is_block_device("/dev/nvme0n1") || !std::path::Path::new("/dev/nvme0n1").exists());
        assert!(!is_block_device("/tmp"));
        assert!(!is_block_device("/nonexistent-jefetch-disk"));
    }
}
