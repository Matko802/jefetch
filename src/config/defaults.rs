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
        "source": "",
        //  "animation": "spin xyz speed=2.0 fps=30 flat chars=ascii",
        //  "animation": "off",
        //  "style": "flat",
        //  "chars": "ascii",
        //  "type": "none",
        //  "color": "red",
        "padding": {
            "top": 0,
            "left": 0,
            "right": 4
        }
    }
}
"#;
