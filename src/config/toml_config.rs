// Convert a parsed sharkfetch `config.toml` into a `Config`.
//
// Supported schema (self-generated; see DEFAULT_TOML_CONFIG):
//   [display]
//   separator = "..."
//   ...
//   [logo]
//   name = "nixos" | type = "none"
//   modules = ["title", "separator", "os", ...]

use super::configfile::{Config, LogoConfig, ModuleEntry};
use super::display::DisplayConfig;
use super::toml::TomlDoc;

/// The stock config.toml written on first run. It reproduces fastfetch's
/// default structure and display settings.
pub const DEFAULT_TOML_CONFIG: &str = r#"# sharkfetch configuration
# This file is auto-generated. It reproduces fastfetch's default output.

# Default fastfetch structure (implemented subset).
modules = [
    "title", "separator", "os", "host", "kernel", "uptime", "packages",
    "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
    "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
    "colors",
]

[display]
separator = ": "
separatorColor = ""
keyColor = "bold_cyan"
titleColor = "bold_blue"
padding = 0
brightColor = true

[logo]
# Builtin logo id (e.g. "nixos", "arch", "ubuntu"). Empty = OS auto-detect.
name = ""

[logo.padding]
top = 0
left = 0
right = 4
"#;

/// True when the given path looks like our TOML config (ends in .toml).
pub fn is_toml_path(path: &str) -> bool {
    path.ends_with(".toml")
}

/// Parse a sharkfetch `config.toml` into a `Config`.
pub fn from_toml(text: &str) -> Result<Config, String> {
    let doc = crate::config::toml::parse(text)?;

    let mut cfg = Config {
        logo: parse_logo(&doc),
        display: parse_display(&doc),
        ..Config::default()
    };

    // Modules array → list of names.
    if let Some(arr) = doc.get("modules") {
        if let Some(names) = arr.as_str_array() {
            cfg.modules = names.into_iter().map(ModuleEntry::Name).collect();
        }
    }

    Ok(cfg)
}

fn parse_display(doc: &TomlDoc) -> DisplayConfig {
    let mut d = DisplayConfig::default();
    let get_str = |key: &str| -> Option<String> {
        doc.get_in("display", key).and_then(|v| v.as_str()).map(String::from)
    };
    if let Some(v) = get_str("separator") {
        d.separator = v;
    }
    if let Some(v) = get_str("separatorColor") {
        if !v.is_empty() {
            d.separator_color = Some(v);
        }
    }
    if let Some(v) = get_str("keyColor") {
        if !v.is_empty() {
            d.key_color = Some(v);
        }
    }
    if let Some(v) = get_str("titleColor") {
        if !v.is_empty() {
            d.title_color = Some(v);
        }
    }
    if let Some(v) = doc.get_in("display", "padding").and_then(|v| v.as_i64()) {
        d.padding = v.max(0) as usize;
    }
    if let Some(v) = doc.get_in("display", "brightColor").and_then(|v| v.as_bool()) {
        d.bright_color = v;
    }
    d
}

fn parse_logo(doc: &TomlDoc) -> LogoConfig {
    let mut l = LogoConfig::default();
    if let Some(v) = doc.get_in("logo", "name").and_then(|v| v.as_str()) {
        if !v.is_empty() {
            l.logo_type = Some("builtin".to_string());
            // Store the logo id; the loader maps it via pick_logo.
            l.source = Some(v.to_string());
        }
    }
    // `type = "none"` disables the logo.
    if let Some(v) = doc.get_in("logo", "type").and_then(|v| v.as_str()) {
        if v.eq_ignore_ascii_case("none") {
            l.logo_type = Some("none".to_string());
            l.source = None;
        }
    }
    // logo.padding: may be an integer in [logo] or a sub-table [logo.padding].
    if let Some(v) = doc.get_in("logo", "padding").and_then(|v| v.as_i64()) {
        let n = v.max(0) as usize;
        l.padding_top = Some(n);
        l.padding_left = Some(n);
        l.padding_right = Some(n);
    }
    // Separate keys in [logo] table.
    if let Some(v) = doc.get_in("logo", "padding_top").and_then(|v| v.as_i64()) {
        l.padding_top = Some(v.max(0) as usize);
    }
    if let Some(v) = doc.get_in("logo", "padding_left").and_then(|v| v.as_i64()) {
        l.padding_left = Some(v.max(0) as usize);
    }
    if let Some(v) = doc.get_in("logo", "padding_right").and_then(|v| v.as_i64()) {
        l.padding_right = Some(v.max(0) as usize);
    }
    // Sub-table [logo.padding] with top/left/right keys.
    if let Some((_, pairs)) = doc.tables.iter().find(|(p, _)| p == &vec!["logo".to_string(), "padding".to_string()]) {
        for (k, v) in pairs {
            if let Some(n) = v.as_i64() {
                let n = n.max(0) as usize;
                match k.as_str() {
                    "top" => l.padding_top = Some(n),
                    "left" => l.padding_left = Some(n),
                    "right" => l.padding_right = Some(n),
                    _ => {}
                }
            }
        }
    }
    l
}
