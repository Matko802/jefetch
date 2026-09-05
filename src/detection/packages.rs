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
    if std::path::Path::new("/nix/var/nix/profiles/system").exists() {
        nix_system = count_nix_filtered("/run/current-system").unwrap_or(0);

        nix_user = count_nix_filtered(&format!("{}/.nix-profile", std::env::var("HOME").unwrap_or_default())).unwrap_or(0);
        if nix_user == 0 {
            if let Some(home) = std::env::var_os("HOME") {
                let p = format!("{}/.local/state/nix/profiles/profile", home.to_string_lossy());
                nix_user = count_nix_filtered(&p).unwrap_or(0);
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

    if info.amounts.is_empty() {
        if let Some(n) = count_nix() {
            info.amounts.push(("nix".to_string(), n));
        }
    }

    info
}

fn count_flatpak_system() -> usize {
    run_capture_timeout("flatpak", &["list", "--system", "--columns=application"], 400)
        .map(|s| s.lines().filter(|l| !l.trim().is_empty()).count())
        .unwrap_or(0)
}
fn count_flatpak_user() -> usize {
    run_capture_timeout("flatpak", &["list", "--user", "--columns=application"], 400)
        .map(|s| s.lines().filter(|l| !l.trim().is_empty()).count())
        .unwrap_or(0)
}

fn count_nix_filtered(path: &str) -> Option<usize> {
    let out = run_capture_timeout("nix-store", &["-q", "--requisites", path], 1200)?;
    let mut count = 0;
    for line in out.lines() {
        let pkg = line.rsplit('/').next().unwrap_or(line);
        if is_valid_nix_pkg(pkg) {
            count += 1;
        }
    }
    Some(count)
}

fn is_valid_nix_pkg(pkg: &str) -> bool {

    use std::path::Path;
    if !Path::new(&format!("/nix/store/{}", pkg)).exists() && !Path::new(pkg).exists() {

    }
    let mut s = pkg.to_string();

    if let Some(slash) = s.rfind('/') {
        s = s[slash + 1..].to_string();
    }
    if s.starts_with("nixos-system-nixos-")
        || s.ends_with("-doc")
        || s.ends_with("-man")
        || s.ends_with("-info")
        || s.ends_with("-dev")
        || s.ends_with("-bin")
    {
        return false;
    }

    let mut state = 0;
    for c in s.chars() {
        match state {
            0 => if c.is_ascii_digit() { state = 1; },
            1 => {
                if c.is_ascii_digit() { continue; }
                else if c == '.' { state = 2; }
                else { state = 0; }
            }
            2 => if c.is_ascii_digit() { return true; } else { state = 0; },
            _ => {}
        }
    }
    false
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
                let count = out.lines().count().to_string();
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
    let n = out.lines().count();
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
