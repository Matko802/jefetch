<div align="center">

# sharkfetch

Fastfetch clone in pure Rust — static `musl`, Linux-only, 530 logos

Inspired by <sub>[fastfetch](https://github.com/fastfetch-cli/fastfetch)</sub>

</div>

## Features

- Drop-in `fastfetch` replacement for NixOS / Linux
- 530 ASCII logos (all fastfetch `src/logo/ascii` + `_/unknown`) — one static binary
- Pure Rust, zero crates except `libc`, fully static `x86_64-unknown-linux-musl` (`not a dynamic executable`)
- Fast — `~4 ms` warm (`fastfetch` `~63 ms` on same machine), parallel modules, persistent `~/.cache/sharkfetch` for `nix-store` / `fastfetch --json`
- Identical output to `fastfetch` (NixOS snowflake, Fedora, Arch, etc. — bold + `carryColor`, `$$` → `$`, `padding: top 0 left 0 right 4`)

## Building

Rust (cargo) is required. No system deps.

```sh
git clone https://github.com/Matko802/sharkfetch.git
cd sharkfetch
./build.sh          # release (static musl) -> target/x86_64-unknown-linux-musl/release/sharkfetch
./build.sh debug    # debug
./build.sh test     # tests
```

`build.sh` uses `rustup` `stable-x86_64-unknown-linux-gnu` + `x86_64-unknown-linux-musl` target and does `cargo build --target x86_64-unknown-linux-musl --release --offline`.

On NixOS or any distro with Nix, prefer the flake (no toolchain needed):

```sh
nix develop   # shell with cargo + musl toolchain
./build.sh
```

Prefer not to touch your system at all?

```sh
nix run github:Matko802/sharkfetch
```

Manual install:

```sh
sudo install -Dm755 target/x86_64-unknown-linux-musl/release/sharkfetch /usr/local/bin/sharkfetch
sharkfetch
```

To install somewhere else:

```sh
install -Dm755 target/x86_64-unknown-linux-musl/release/sharkfetch $HOME/.local/bin/sharkfetch
```

### Run it

```sh
sharkfetch              # auto-detect OS logo
sharkfetch --list-logos # 530 names
sharkfetch --help
```

Config is auto-created at `~/.config/sharkfetch/config.toml` on first run (reproduces `fastfetch` defaults):

```toml
modules = ["title", "separator", "os", "host", "kernel", "uptime", "packages", "shell", "display", "wm", "theme", "icons", "font", "cursor", "terminal", "cpu", "gpu", "memory", "swap", "disk", "localip", "locale", "break", "colors"]

[display]
separator = ": "
keyColor = "bold_cyan"
titleColor = "bold_blue"

[logo]
name = ""               # empty = auto-detect, or "nixos", "arch", "fedora", ...

[logo.padding]
top = 0
left = 0
right = 4
```

`sharkfetch` also reads `~/.config/fastfetch/config.jsonc` as fallback, so existing `fastfetch` configs work.

## Nix flakes

sharkfetch ships with its own flake, so you can pull it straight from GitHub.

### As a flake input

```nix
{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    sharkfetch = {
      url = "github:Matko802/sharkfetch";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, sharkfetch, ... }: {
    packages.x86_64-linux.default = sharkfetch.packages.x86_64-linux.default;
  };
}
```

### As an overlay

The flake also exposes `overlays.default`, so you can enable it with
`nixpkgs.overlays = [ sharkfetch.overlays.default ];` and get `pkgs.sharkfetch`.

A full NixOS example that pulls the flake in as both an overlay and a package:

```nix
{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    sharkfetch = {
      url = "github:Matko802/sharkfetch";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { nixpkgs, sharkfetch, ... }:
    let
      system = "x86_64-linux";
    in
    {
      nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
        inherit system;
        modules = [
          {
            nixpkgs.overlays = [ sharkfetch.overlays.default ];
            environment.systemPackages = [ sharkfetch.packages.${system}.default ];
          }
        ];
      };
    };
}
```

### Standalone build from source

```sh
nix build github:Matko802/sharkfetch
nix run github:Matko802/sharkfetch
```

### Development

```sh
nix develop github:Matko802/sharkfetch
./build.sh test
```

## License

MIT — see [LICENSE](LICENSE).
