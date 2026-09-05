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
        "separatorColor": "red",
        "keyColor": "",
        "titleColor": "",
        "padding": 1,
        "brightColor": true
    },
    "logo": {
        "source": "cachyos",
        "animation": "spin speed=0 xyz",
        "sharkvis": "motion=revert retract=1 boom=10 chars=blocks",
        "padding": {
            "top": 0,
            "left": 0,
            "right": 0
        }
    }
}
"#;
