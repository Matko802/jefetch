# Modules

`modules` is the ordered list shown in output. Unknown names are skipped.

```jsonc
{ "modules": ["title", "separator", "os", "kernel", "break", "colors"] }
```

Each entry is a bare name or an object with options:

```jsonc
{ "modules": ["os", { "type": "cpu", "temp": true }, "break", "colors"] }
```

## Available Modules

`title`, `separator`, `os`, `host`, `kernel`, `uptime`, `packages`, `shell`, `display`, `wm`/`de`, `theme`, `icons`, `font`, `cursor`, `terminal`, `terminalfont`, `cpu`, `gpu`, `memory`, `swap`, `disk`, `localip`/`ip`, `battery`, `locale`, `break`, `colors`.

Option blocks for a module go under its name (also settable as a top-level key):

```jsonc
{
    "modules": ["disk", "gpu"],
    "disk": { "folders": "/" },
    "gpu": { "driverSpecific": true }
}
```

`disk.folders` accepts one path or a list. Without it, all physical
disks are listed (pseudo, container and duplicate-pool mounts skipped).

`packages` shows a per-manager breakdown by default; `combined` shows one total:

```jsonc
{ "modules": [{ "type": "packages", "combined": true }] }
```

## CLI Structure Override

```sh
jefetch --structure "os:kernel:uptime:break:colors"
```

`--structure` overrides `modules` at runtime.

Next: [Development](Development.md) →
