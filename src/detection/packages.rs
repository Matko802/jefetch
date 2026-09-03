use crate::detection::run_capture_timeout;

#[derive(Debug, Clone, Default)]
pub struct PackagesInfo {
    pub amounts: Vec<(String, usize)>,
}

/// Count installed packages. Supports the Nix package manager (NixOS)
/// by querying the system profile closure.
pub fn detect() -> PackagesInfo {
    let mut info = PackagesInfo::default();

    // Try to get exact counts from fastfetch's JSON (most accurate, matches fastfetch 1:1).
    // Fastfetch is at /run/current-system/sw/bin/fastfetch on NixOS.
    if let Some(fast) = try_fastfetch_json() {
        for (k, v) in fast {
            if v > 0 {
                info.amounts.push((k, v));
            }
        }
        if !info.amounts.is_empty() {
            return info;
        }
    }

    // Fallback: manual Nix + flatpak counts with fastfetch's isValidNixPkg filtering.
    let mut nix_system = 0;
    let mut nix_user = 0;
    if std::path::Path::new("/nix/var/nix/profiles/system").exists() {
        nix_system = count_nix_filtered("/run/current-system").unwrap_or(0);
        // Also try user profiles
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
    // Fallback single Nix if split failed
    if info.amounts.is_empty() {
        if let Some(n) = count_nix() {
            info.amounts.push(("nix".to_string(), n));
        }
    }

    info
}

fn try_fastfetch_json() -> Option<Vec<(String, usize)>> {
    // Use cached fastfetch JSON if recent (10s) to avoid spawning fastfetch every run.
    let cache_path = {
        if let Some(dir) = std::env::var_os("XDG_CACHE_HOME") {
            format!("{}/sharkfetch/fastfetch-packages.json", dir.to_string_lossy())
        } else if let Some(home) = std::env::var_os("HOME") {
            format!("{}/.cache/sharkfetch/fastfetch-packages.json", home.to_string_lossy())
        } else {
            "/tmp/sharkfetch-fastfetch-packages.json".to_string()
        }
    };
    let use_cache = std::fs::metadata(&cache_path)
        .and_then(|m| m.modified())
        .map(|t| t.elapsed().map(|e| e.as_secs() < 10).unwrap_or(false))
        .unwrap_or(false);
    let json_str = if use_cache {
        std::fs::read_to_string(&cache_path).ok()?
    } else {
        let out = run_capture_timeout(
            "/run/current-system/sw/bin/fastfetch",
            &["--json"],
            800,
        )?;
        let _ = std::fs::create_dir_all(std::path::Path::new(&cache_path).parent().unwrap_or(std::path::Path::new("/tmp")));
        let _ = std::fs::write(&cache_path, &out);
        out
    };
    // Very cheap parse: look for "flatpakSystem", "flatpakUser", "nixSystem", "nixUser"
    let mut res = Vec::new();
    for (key, label) in [
        ("flatpakSystem", "flatpak-system"),
        ("flatpakUser", "flatpak-user"),
        ("nixSystem", "nix-system"),
        ("nixUser", "nix-user"),
    ] {
        if let Some(idx) = json_str.find(&format!("\"{}\"", key)) {
            let substr = &json_str[idx..];
            if let Some(colon) = substr.find(':') {
                let rest = &substr[colon + 1..];
                let num_str: String = rest.chars().skip_while(|c| !c.is_ascii_digit()).take_while(|c| c.is_ascii_digit()).collect();
                if let Ok(n) = num_str.parse::<usize>() {
                    if n > 0 {
                        res.push((label.to_string(), n));
                    }
                }
            }
        }
    }
    if res.is_empty() { None } else { Some(res) }
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
    // Mirrors fastfetch's isValidNixPkg in packages_nix.c
    use std::path::Path;
    if !Path::new(&format!("/nix/store/{}", pkg)).exists() && !Path::new(pkg).exists() {
        // Fastfetch checks ffPathExists on full path, but we check basename existence via store dir
        // Keep simple: just check naming.
    }
    let mut s = pkg.to_string();
    // Strip hash prefix like "abc123-foo-1.2.3" -> need basename after hash
    // fastfetch does ffStrbufSubstrAfterLastC(pkg, '/') then checks
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
    // Must contain version pattern digit '.' digit
    let mut state = 0; // 0 START, 1 DIGIT, 2 DOT
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
    // Persistent cache in XDG_CACHE_HOME/sharkfetch/packages.count + tmp fallback.
    // Stale cache is returned immediately while background refresh updates it,
    // so warm and “cold” (post-reboot) runs stay ~5 ms.
    let uid = unsafe { libc::getuid() };
    let cache_path = {
        if let Some(dir) = std::env::var_os("XDG_CACHE_HOME") {
            format!("{}/sharkfetch/packages.count", dir.to_string_lossy())
        } else if let Some(home) = std::env::var_os("HOME") {
            format!("{}/.cache/sharkfetch/packages.count", home.to_string_lossy())
        } else {
            format!("/tmp/sharkfetch-packages.{}.cache", uid)
        }
    };
    let tmp_cache = format!("/tmp/sharkfetch-packages.{}.cache", uid);

    // Helper to read cached value if exists.
    let read_cached = |p: &str| -> Option<(usize, std::time::SystemTime)> {
        let txt = std::fs::read_to_string(p).ok()?;
        let n = txt.trim().parse::<usize>().ok()?;
        let meta = std::fs::metadata(p).ok()?;
        let mtime = meta.modified().ok()?;
        Some((n, mtime))
    };

    // Try persistent cache first, then tmp.
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
        // Stale or profile changed: return stale immediately, refresh in background.
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

    // No cache: synchronous (first run ever) — still slow but only once.
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