# jefetch Wiki

A fastfetch clone in pure Rust. Static musl binary, Linux only, same output,
and the logo can spin in 3D.

| Page | Covers |
|------|--------|
| [Installation](Installation.md) | Installing and updating |
| [Configuration](Configuration.md) | Config file, modules, logos, animation, music mode |

```sh
nix run github:Matko802/jefetch
# or: git clone https://github.com/Matko802/jefetch && cd jefetch && make deps && make && sudo make install
```

Running it once creates `~/.config/jefetch/config.jsonc` for you.
