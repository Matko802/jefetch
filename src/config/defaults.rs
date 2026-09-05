pub const DEFAULT_JSONC_CONFIG: &str = r#"{
    "modules": [
        "title",
        "separator",
        "os",
        "host",
        "kernel",
        "uptime",
        { "type": "packages", "combined": true },
        "shell",
        "display",
        "wm",
        "theme",
        "icons",
        "font",
        "cursor",
        "terminal",
        "cpu",
        "gpu",
        "memory",
        "swap",
        "disk",
        "localip",
        "locale",
        "break",
        "colors"
    ],
    "display": {
        "separator": "->",
        "separatorColor": "",
        "keyColor": "bold_cyan",
        "titleColor": "bold_blue",
        "padding": 1,
        "brightColor": true
    },
    "logo": {
        "source": "",
        "animation": "spin xz flat",
        "sharkvis": "speed=0 boom=10 chars=ascii",
        "padding": {
            "top": 0,
            "left": 0,
            "right": 4
        }
    }
}
"#;
