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
        "colors",
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
        "source": "",
        "animation": "speed=1 xy",
        "sharkvis": "xzy return=10 boom=25 chars=blocks color=sharkvis",
        "padding": {
            "top": 0,
            "left": 0,
            "right": 0
        }
    }
}
"#;
