# jefetch Wiki

fastfetch clone in pure Rust — static `musl`, Linux-only, 1:1 output, 3D spinning logo.

| Page | What you'll find |
|------|------------------|
| [Installation](Installation.md) | Install & update |
| [Configuration](Configuration.md) | `config.jsonc`, all sections |
| [Animation](Animation.md) | Spin, speed, music mode |
| [Logos](Logos.md) | Builtin / custom, colors, padding |
| [Modules](Modules.md) | Module list & options |
| [Development](Development.md) | Build, layout, contributing |
| [FAQ](FAQ.md) | Quick answers |

```sh
nix run github:Matko802/jefetch
# or: git clone https://github.com/Matko802/jefetch && cd jefetch && make deps && make && sudo make install
```

First run auto-creates `~/.config/jefetch/config.jsonc`.
