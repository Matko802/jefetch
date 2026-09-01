use crate::detection::run_capture_timeout;

#[derive(Debug, Clone, Default)]
pub struct PackagesInfo {
    pub amounts: Vec<(String, usize)>,
}

/// Count installed packages. Supports the Nix package manager (NixOS)
/// by querying the system profile closure.
pub fn detect() -> PackagesInfo {
    let mut info = PackagesInfo::default();

    if let Some(v) = getenv("NIXOS_VERSION") {
        let _ = v;
    }

    // Primary manager on NixOS: closures of the system profile.
    if std::path::Path::new("/nix/var/nix/profiles/system").exists() {
        if let Some(n) = count_nix() {
            info.amounts.push(("Nix".to_string(), n));
            return info;
        }
    }

    info
}

fn count_nix() -> Option<usize> {
    // Count packages in the current environment by enumerating the store
    // once, then counting the closures of the running generation.
    let out = run_capture_timeout(
        "nix-store",
        &["-q", "--requisites", "/nix/var/nix/profiles/system"],
        8000,
    )?;
    Some(out.lines().count())
}

fn getenv(name: &str) -> Option<String> {
    std::env::var(name).ok()
}