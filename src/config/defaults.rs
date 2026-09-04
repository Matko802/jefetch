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
        //  "animation": "spin xyz speed=2.0 fps=30",
        //  "animation": "spin y flat chars=ascii",
        //  "animation": "spin z chars=.,-~:;=!*#$@",
        //  "animation": "off",
        //  "style": "flat",
        //  "style": "3d",
        //  "chars": "ascii",
        //  "chars": "blocks",
        //  "chars": ".,-~:;=!*#$@",
        //  "type": "none",
        //  "type": "file",
        //  "color": "red",
        "padding": {
            "top": 0,
            "left": 0,
            "right": 4
        }
    }
}
"#;
