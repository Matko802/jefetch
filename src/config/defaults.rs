// Default JSONC config written on first run. Config is JSONC-only.

/// The stock `config.jsonc` written on first run. It reproduces fastfetch's
/// default structure and display settings.
pub const DEFAULT_JSONC_CONFIG: &str = r#"{
    "modules": [
        "title", "separator", "os", "host", "kernel", "uptime", "packages",
        "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
        "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
        "colors"
    ],
    "display": {
        "separator": ": ",
        "separatorColor": "",
        "keyColor": "bold_cyan",
        "titleColor": "bold_blue",
        "padding": 0,
        "brightColor": true
    },
    "logo": {
        // Builtin logo id (e.g. "nixos", "arch", "ubuntu"). Empty = OS auto-detect.
        "source": "",
        //  "animation": "spin z speed=1.5",
        //  "style": "flat",   // "flat" or "3d"
        //  "chars": "ascii",  // "ascii" keeps logo chars, or custom ".,-~:;=!*#$@"
        "padding": {
            "top": 0,
            "left": 0,
            "right": 4
        }
    }
}
"#;