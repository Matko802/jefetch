# Modules

`modules` is the ordered fastfetch structure. Sharkfetch implements the subset below; unknown names are skipped.

## Default Structure

```toml
modules = [
    "title", "separator", "os", "host", "kernel", "uptime", "packages",
    "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal",
    "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break",
    "colors",
]
```

`default_structure()` (`src/app.rs:765`) matches `fastfetch` `DEFAULT_STRUCTURE`.

## Tabs: Module Config Styles

=== "Bare string"

    ```jsonc
    { "modules": ["os", "kernel", "uptime", "cpu", "gpu"] }
    ```

=== "Object with args"

    ```jsonc
    {
        "modules": [
            "os",
            { "type": "cpu", "temp": true },
            { "type": "disk", "folders": "/" },
            "break",
            "colors"
        ]
    }
    ```

`ModuleEntry::Name` vs `Object` (`src/config/configfile.rs:99`), `ModuleArgs::parse()` (`src/config/moduleargs.rs`), `module_options()` top-level sections (`src/config/configfile.rs:185`).

## All Implemented Modules

| Module | Source | Notes |
|--------|--------|-------|
| `title` | `user@host` | Colored via `titleColor` |
| `separator` | `---` | `separator` + `separatorColor` |
| `os` | `src/detection/os.rs` | `/etc/os-release` `PRETTY_NAME` |
| `host` | `DMI` / `sys` | Filters `System Product Name` |
| `kernel` | `uname -r` | `7.2.2-cachyos` |
| `uptime` | `/proc/uptime` | `2 hours, 9 mins` (`, ` join) |
| `packages` | `nix` + `flatpak` | `2207 (nix-system)` filtered via `isValidNixPkg`, `7 (flatpak-system)` |
| `shell` | `proc` tree walk | `find_shell_via_proc` (`src/detection/shell.rs`), `fish 4.8.1` |
| `display` | `DRM`/`xrandr` | `DP-1 @ 1920x1080`, or `24G2W1G3- 1920x1080 in 24", 165 Hz [External]` |
| `de`/`wm` | `env`/`wayland` | `niri (Wayland)` via `WmInfo` `OnceLock` |
| `theme` | `gsettings`/`GTK` | `MatkosAmoled [GTK3]` |
| `icons` | GTK | `Papirus-Dark [GTK3]` |
| `font` | GTK | `DepartureMono Nerd Font 10` |
| `cursor` | GTK | `Adwaita` |
| `terminal` | `TERM`/`kitty` | `kitty 0.48.2`, `.kitty-wrapped` → `kitty` (`src/detection/terminal.rs`) |
| `terminalfont` | kitty query | Excluded from `--structure` (avoids `^[P1+r` garbage), cached `OnceLock` |
| `cpu` | `/proc/cpuinfo` | `(12) @ 4.46 GHz` |
| `gpu` | `PCI` ids | `RX 6600 [Discrete]` |
| `memory` | `/proc/meminfo` | `6.83 GiB / 15.41 GiB (44%)` |
| `swap` | `/proc/meminfo` | `475 MiB / 24.96 GiB (2%)` |
| `disk` | `statvfs` | Colored `%` green/yellow/red, second disk at key column |
| `localip`/`ip` | `getifaddrs` | `Local IP (enp9s0): 192.168.1.48/24` + `vboxnet0`/`virbr0` |
| `battery` | `sysfs` | If present |
| `locale` | `env` | `sk_SK.UTF-8` |
| `break` | — | Blank line |
| `colors` | — | Color palette |

---

## Tabs: Display & General

=== "TOML"

    ```toml
    [display]
    separator = ": "
    separatorColor = ""
    keyColor = "bold_cyan"
    titleColor = "bold_blue"
    padding = 0
    brightColor = true

    [general]
    # thread, cache, etc. (see src/config/general.rs)
    ```

=== "JSONC"

    ```jsonc
    {
        "display": {
            "separator": ": ",
            "separatorColor": "",
            "keyColor": "bold_cyan",
            "titleColor": "bold_blue",
            "brightColor": true
        },
        "general": {
            // see src/config/general.rs
        }
    }
    ```

Derived colors: if `display.keyColor`/`titleColor` are unset, they fall back to logo `slots[1]`/`slots[0]` (`src/app.rs:335`).

## Module-Specific Top-Level Sections

Any top-level object whose key is a known module name is treated as its options:

```jsonc
{
    "modules": ["cpu", "gpu"],
    "cpu": { "temp": true },
    "gpu": { "driverSpecific": true }
}
```

### `disk`

```jsonc
"disk": { "folders": "/" }
"disk": { "folders": ["/", "/home", "/mnt/ssd"] }
```

Extra disks: add `disk=/home` lines in `fetch` config (areofyl), or object `{"type":"disk","folders":"/home"}`.

### `localip`

Shows `enp9s0`/`vboxnet0`/`virbr0` with CIDR. No extra config.

## CLI Structure Overrides

```sh
sharkfetch --structure "os:kernel:uptime:break:colors"
sharkfetch --structure "title:separator:os:break:colors" --no-config
```

`--structure` (`src/app.rs:116` `CliOptions::structure`) overrides `modules` at runtime.

Next: [Development](Development) →
