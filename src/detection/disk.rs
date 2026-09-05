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
    let mountpoints: Vec<String> = if folders.is_empty() {
        physical_mountpoints()
    } else {
        folders.to_vec()
    };
    let mut out = Vec::new();
    let mut seen_dev: Vec<u64> = Vec::new();
    let mut seen_pool: Vec<(u64, u64, String, String)> = Vec::new();
    for mp in mountpoints {
        let dev = device_id(&mp);
        if let Some(d) = dev {
            if seen_dev.contains(&d) {
                continue;
            }
            seen_dev.push(d);
        }
        if let Some(info) = stat_one(&mp) {
            if info.total == 0 {
                continue;
            }
            let pool = (
                info.total,
                info.used,
                info.filesystem.clone(),
                info.mount_from.clone(),
            );
            if seen_pool.contains(&pool) {
                continue;
            }
            seen_pool.push(pool);
            out.push(info);
        }
    }
    out
}

fn device_id(mp: &str) -> Option<u64> {
    let c = std::ffi::CString::new(mp).ok()?;
    let mut st: libc::stat = unsafe { std::mem::zeroed() };
    if unsafe { libc::stat(c.as_ptr(), &mut st) } != 0 {
        return None;
    }
    Some(st.st_dev as u64)
}

fn physical_mountpoints() -> Vec<String> {
    let text = read_file("/proc/self/mounts").unwrap_or_default();
    parse_physical_mounts(&text)
}

fn parse_physical_mounts(text: &str) -> Vec<String> {
    const HIDE_FS: &[&str] = &[
        "proc", "sysfs", "devpts", "devtmpfs", "cgroup", "cgroup2", "mqueue", "debugfs",
        "tracefs", "securityfs", "configfs", "fusectl", "nsfs", "binfmt_misc", "autofs",
        "hugetlbfs", "rpc_pipefs", "efivarfs", "ramfs", "overlay", "squashfs", "fuse.portal",
        "fuse.gvfsd-fuse",
    ];
    let mut out = Vec::new();
    for line in text.lines() {
        let parts: Vec<&str> = line.split(' ').collect();
        if parts.len() < 3 {
            continue;
        }
        let fs = parts[2];
        let mp = parts[1].replace("\\040", " ");
        if HIDE_FS.contains(&fs) {
            continue;
        }
        if fs == "tmpfs" && mp != "/tmp" {
            continue;
        }
        if mp == "/proc" || mp == "/sys" || mp == "/dev" {
            continue;
        }
        if mp.starts_with("/proc/") || mp.starts_with("/sys/") || mp.starts_with("/dev/") {
            continue;
        }
        if mp.starts_with("/run/") && !mp.starts_with("/run/media/") {
            continue;
        }
        if !out.iter().any(|m| m == &mp) {
            out.push(mp);
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

    const SAMPLE: &str = "dev /dev devtmpfs rw 0 0\n\
        /dev/nvme0n1p2 / btrfs rw 0 0\n\
        /dev/nvme0n1p2 /home btrfs rw 0 0\n\
        /dev/nvme0n1p1 /boot vfat rw 0 0\n\
        tmpfs /tmp tmpfs rw 0 0\n\
        /dev/loop0 /snap/core squashfs ro 0 0\n\
        overlay /var/lib/docker/overlay2 overlay rw 0 0\n\
        proc /proc proc rw 0 0\n";

    #[test]
    fn physical_mounts_skip_pseudo() {
        assert_eq!(
            parse_physical_mounts(SAMPLE),
            vec![
                "/".to_string(),
                "/home".to_string(),
                "/boot".to_string(),
                "/tmp".to_string(),
            ]
        );
    }

    #[test]
    fn physical_mounts_skip_run_noise() {
        let text = "tmpfs /run tmpfs rw 0 0\n\
            tmpfs /run/user/1000 tmpfs rw 0 0\n\
            gvfsd-fuse /run/user/1000/gvfs fuse.gvfsd-fuse rw 0 0\n\
            /dev/sdb1 /run/media/matko/USB vfat rw 0 0\n\
            /dev/nvme0n1p2 / btrfs rw 0 0\n";
        assert_eq!(
            parse_physical_mounts(text),
            vec!["/run/media/matko/USB".to_string(), "/".to_string()]
        );
    }
}
