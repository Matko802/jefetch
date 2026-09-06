use crate::detection::run_capture_timeout;
use std::sync::{Mutex, OnceLock};

#[derive(Debug, Clone, Default)]
pub struct PackagesInfo {
    pub amounts: Vec<(String, usize)>,
}

static PKG_CACHE: OnceLock<Mutex<(Option<PackagesInfo>, std::time::Instant)>> = OnceLock::new();

fn cache_slot() -> &'static Mutex<(Option<PackagesInfo>, std::time::Instant)> {
    PKG_CACHE.get_or_init(|| {
        Mutex::new((
            None,
            std::time::Instant::now() - std::time::Duration::from_secs(3600),
        ))
    })
}

pub fn detect() -> PackagesInfo {
    {
        let guard = cache_slot().lock().unwrap_or_else(|e| e.into_inner());
        if let (Some(info), at) = (&guard.0, guard.1) {
            if at.elapsed() < std::time::Duration::from_secs(60) {
                return info.clone();
            }
        }
    }
    let info = detect_uncached();
    *cache_slot().lock().unwrap_or_else(|e| e.into_inner()) =
        (Some(info.clone()), std::time::Instant::now());
    info
}

pub fn detect_uncached() -> PackagesInfo {
    let mut info = PackagesInfo::default();

    let mut nix_system = 0;
    let mut nix_user = 0;
    let mut nix_default = 0;
    if std::path::Path::new("/nix/var/nix/profiles/system").exists() {
        nix_system = count_nix_profile("/run/current-system");
        nix_default = count_nix_profile("/nix/var/nix/profiles/default");
        for candidate in nix_user_candidates() {
            let n = count_nix_profile(&candidate);
            if n > 0 {
                nix_user = n;
                break;
            }
        }
    }
    let flat_system = count_flatpak_system();
    let flat_user = count_flatpak_user();

    if flat_system > 0 {
        info.amounts.push(("flatpak-system".to_string(), flat_system));
    }
    if flat_user > 0 {
        info.amounts.push(("flatpak-user".to_string(), flat_user));
    }
    if nix_system > 0 {
        info.amounts.push(("nix-system".to_string(), nix_system));
    }
    if nix_user > 0 {
        info.amounts.push(("nix-user".to_string(), nix_user));
    }
    if nix_default > 0 {
        info.amounts.push(("nix-default".to_string(), nix_default));
    }

    if info.amounts.is_empty() {
        if let Some(n) = count_nix() {
            info.amounts.push(("nix".to_string(), n));
        }
    }

    info
}

fn count_flatpak_system() -> usize {    run_capture_timeout("flatpak", &["list", "--system", "--columns=application"], 400)
        .map(|s| s.lines().filter(|l| !l.trim().is_empty()).count())
        .unwrap_or(0)
}
fn count_flatpak_user() -> usize {
    run_capture_timeout("flatpak", &["list", "--user", "--columns=application"], 400)
        .map(|s| s.lines().filter(|l| !l.trim().is_empty()).count())
        .unwrap_or(0)
}

fn nix_user_candidates() -> Vec<String> {
    let home = std::env::var_os("HOME").map(|h| h.to_string_lossy().into_owned());
    let user = std::env::var("USER").unwrap_or_default();
    let mut out = Vec::new();
    if let Some(h) = &home {
        out.push(format!("{}/.nix-profile", h));
        out.push(format!("{}/.local/state/nix/profiles/profile", h));
    }
    if !user.is_empty() {
        out.push(format!("/etc/profiles/per-user/{}", user));
        out.push(format!("/nix/var/nix/profiles/per-user/{}/profile", user));
    }
    out
}

fn count_nix_profile(path: &str) -> usize {
    let out = run_capture_timeout("nix-store", &["-q", "--requisites", path], 2500).unwrap_or_default();
    filter_nix_requisites(&out)
}

fn cut_hash(path: &str) -> &str {
    let base = path.rsplit('/').next().unwrap_or(path);
    match base.find('-') {
        Some(i) => &base[i + 1..],
        None => base,
    }
}

fn has_dotted_version(name: &str) -> bool {
    let b = name.as_bytes();
    let mut i = 0;
    while i < b.len() {
        if b[i].is_ascii_digit() {
            let mut j = i;
            while j < b.len() && b[j].is_ascii_digit() {
                j += 1;
            }
            let mut k = j;
            let mut groups = 0;
            while k < b.len() && b[k] == b'.' {
                let mut m = k + 1;
                while m < b.len() && b[m].is_ascii_digit() {
                    m += 1;
                }
                if m == k + 1 {
                    break;
                }
                groups += 1;
                k = m;
            }
            if groups >= 1 {
                return true;
            }
            i = if j > i { j } else { i + 1 };
        } else {
            i += 1;
        }
    }
    false
}

fn keep_nix_name(name: &str) -> bool {
    if name.starts_with("nixos-system-nixos-")
        || name.ends_with("-doc")
        || name.ends_with("-man")
        || name.ends_with("-info")
        || name.ends_with("-dev")
        || name.ends_with("-bin")
    {
        return false;
    }
    has_dotted_version(name)
}

fn filter_nix_requisites(out: &str) -> usize {
    let mut count = 0;
    let mut last = String::new();
    for line in out.lines() {
        let line = line.trim();
        if line.is_empty() || !std::path::Path::new(line).is_dir() {
            continue;
        }
        let name = cut_hash(line);
        if !keep_nix_name(name) || name == last {
            continue;
        }
        last = name.to_string();
        count += 1;
    }
    count
}

fn count_nix() -> Option<usize> {

    let uid = unsafe { libc::getuid() };
    let cache_path = {
        if let Some(dir) = std::env::var_os("XDG_CACHE_HOME") {
            format!("{}/jefetch/packages.count", dir.to_string_lossy())
        } else if let Some(home) = std::env::var_os("HOME") {
            format!("{}/.cache/jefetch/packages.count", home.to_string_lossy())
        } else {
            format!("/tmp/jefetch-packages.{}.cache", uid)
        }
    };
    let tmp_cache = format!("/tmp/jefetch-packages.{}.cache", uid);

    let read_cached = |p: &str| -> Option<(usize, std::time::SystemTime)> {
        let txt = std::fs::read_to_string(p).ok()?;
        let n = txt.trim().parse::<usize>().ok()?;
        let meta = std::fs::metadata(p).ok()?;
        let mtime = meta.modified().ok()?;
        Some((n, mtime))
    };

    let mut cached: Option<(usize, std::time::SystemTime, String)> = None;
    for p in [&cache_path, &tmp_cache] {
        if let Some((n, mtime)) = read_cached(p) {
            cached = Some((n, mtime, p.clone()));
            break;
        }
    }

    if let Some((n, mtime, path)) = cached {
        let stale = mtime.elapsed().map(|e| e.as_secs() > 300).unwrap_or(true);
        let sys_newer = std::fs::metadata("/nix/var/nix/profiles/system")
            .and_then(|m| m.modified())
            .map(|sys_m| sys_m > mtime)
            .unwrap_or(false);
        if !stale && !sys_newer {
            return Some(n);
        }

        let cache_path_clone = path.clone();
        let tmp_clone = tmp_cache.clone();
        std::thread::spawn(move || {
            if let Some(out) = run_capture_timeout(
                "nix-store",
                &["-q", "--requisites", "/nix/var/nix/profiles/system"],
                2500,
            ) {
                let count = filter_nix_requisites(&out).to_string();
                let _ = std::fs::create_dir_all(
                    std::path::Path::new(&cache_path_clone)
                        .parent()
                        .unwrap_or(std::path::Path::new("/tmp")),
                );
                let _ = std::fs::write(&cache_path_clone, &count);
                let _ = std::fs::write(&tmp_clone, &count);
            }
        });
        return Some(n);
    }

    let out = run_capture_timeout(
        "nix-store",
        &["-q", "--requisites", "/nix/var/nix/profiles/system"],
        2500,
    )?;
    let n = filter_nix_requisites(&out);
    let s = n.to_string();
    let _ = std::fs::create_dir_all(
        std::path::Path::new(&cache_path)
            .parent()
            .unwrap_or(std::path::Path::new("/tmp")),
    );
    let _ = std::fs::write(&cache_path, &s);
    let _ = std::fs::write(&tmp_cache, &s);
    Some(n)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cut_hash_strips_store_prefix() {
        assert_eq!(cut_hash("/nix/store/abc123-glibc-2.34-210"), "glibc-2.34-210");
        assert_eq!(cut_hash("/nix/store/abc123-nixos-system-nixos-25.11"), "nixos-system-nixos-25.11");
        assert_eq!(cut_hash("nodashes"), "nodashes");
    }

    #[test]
    fn dotted_version_matches_fastfetch_regex() {
        assert!(has_dotted_version("libunistring-1.0"));
        assert!(has_dotted_version("bzip2-1.0.6.0.2"));
        assert!(has_dotted_version("bash-5.1-p16"));
        assert!(!has_dotted_version("tzdata"));
        assert!(!has_dotted_version("2048"));
    }

    #[test]
    fn requisites_filter_matches_fastfetch() {
        let out = "/nix/store/aaa-glibc-2.34-210\n\
                   /nix/store/bbb-glibc-2.34-210\n\
                   /nix/store/ccc-foo-1.0-doc\n\
                   /nix/store/ddd-nixos-system-nixos-25.11\n\
                   /nix/store/eee-tzdata\n";
        let _ = std::fs::create_dir_all("/tmp/jefetch-test-pkg/aaa-glibc-2.34-210");
        let _ = std::fs::create_dir_all("/tmp/jefetch-test-pkg/bbb-glibc-2.34-210");
        let _ = std::fs::create_dir_all("/tmp/jefetch-test-pkg/ccc-foo-1.0-doc");
        let _ = std::fs::create_dir_all("/tmp/jefetch-test-pkg/ddd-nixos-system-nixos-25.11");
        let _ = std::fs::create_dir_all("/tmp/jefetch-test-pkg/eee-tzdata");
        let remapped = out.replace("/nix/store/", "/tmp/jefetch-test-pkg/");
        assert_eq!(filter_nix_requisites(&remapped), 1);
        let _ = std::fs::remove_dir_all("/tmp/jefetch-test-pkg");
    }
}
