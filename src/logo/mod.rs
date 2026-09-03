// Built-in ASCII logos imported from fastfetch (see data.rs).
// Name + aliases map to a distro; `color` is the base SGR; `slots` is the
// fastfetch FF_COLOR palette (as SGR payload strings). Lines may contain
// fastfetch-style `$N` slot markers (1..n); each `$N` expands to
// \x1b[{slots[N-1]}m so multi-color logos render correctly.

#[derive(Debug, Clone)]
pub struct Logo {
    pub name: &'static str,
    /// Other distro names that map to this logo (fastfetch `names[1..]`).
    pub aliases: &'static [&'static str],
    /// Base color (SGR payload) for logos without `$N` slots.
    pub color: &'static str,
    /// Per-slot colors as SGR payloads (1-indexed). Empty for single-color logos.
    pub slots: &'static [&'static str],
    /// fastfetch colorKeys (SGR payload), if any.
    pub color_keys: Option<&'static str>,
    /// fastfetch colorTitle (SGR payload), if any.
    pub color_title: Option<&'static str>,
    pub lines: &'static [&'static str],
}

/// Alias kept for generated-code compatibility (data.rs uses `LogoData`).
pub type LogoData = Logo;

include!("data.rs");

pub fn by_name(name: &str) -> Option<&'static Logo> {
    LOGOS.iter().find(|l| {
        l.name.eq_ignore_ascii_case(name) || l.aliases.iter().any(|a| a.eq_ignore_ascii_case(name))
    })
}

pub fn list_names() -> impl Iterator<Item = &'static str> {
    LOGOS.iter().map(|l| l.name)
}
