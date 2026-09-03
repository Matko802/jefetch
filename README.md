<div align="center">

# sharkfetch

Rust Based Cli System Stat fetcher

Inspired by <sub>[fastfetch](https://github.com/fastfetch-cli/fastfetch)</sub>

</div>

## Features

- Drop-in `fastfetch` replacement
- All ASCII logos from  `fastfetch`
- Pure Rust
- Uses Musl

## Building
```sh
git clone https://github.com/Matko802/sharkfetch.git
cd sharkfetch
make deps
make
sudo make install
```

On NixOS or any distro with Nix, prefer the flake (no system packages needed):

```sh
nix develop   # drop into a shell with cargo
make          # build inside the dev shell
```

Prefer not to touch your system at all?

```sh
nix run github:Matko802/sharkfetch
```

To install somewhere else instead of `/usr/local`:

```sh
make PREFIX=$HOME/.local install
```

### Run it

```sh
sharkfetch
```

## Usage

```sh
sharkfetch
sharkfetch --list-logos
sharkfetch --help
```

| Option | Description |
| ------ | ----------- |
| `--help` | show help |
| `--list-logos` | list 530 logos |
| `--list-modules` | list available modules |

The config file is looked up in `~/.config/sharkfetch/config.toml`, then
`~/.config/fastfetch/config.jsonc`. It is auto-created on first run.

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
```

## License

This project is released under the MIT License. See [LICENSE](https://github.com/Matko802/sharkfetch/blob/main/LICENSE).
