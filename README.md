<div align="center">

<img src="Logo/jefetch.png" width="120" alt="jefetch logo :3" />

# jefetch

Rust Based Cli System Stat fetcher

Inspired by <sub>[fastfetch](https://github.com/fastfetch-cli/fastfetch)</sub>

</div>

## Features

- Drop-in `fastfetch` replacement
- 530 ASCII logos (all `fastfetch` logos)
- Areofyl-style 3D spinning logo
- Pure Rust, zero crates except `libc`
- Fully static `musl`

## Building

```sh
git clone https://github.com/Matko802/jefetch.git
cd jefetch
make deps
make
sudo make install
```

## Usage

```sh
jefetch
jefetch --static
jefetch --logo arch
jefetch --list-logos
jefetch --help
```

| Option | Description |
| ------ | ----------- |
| `--help` | show help |
| `--static` | force static (no animation) |
| `--logo <name>` | override logo for one run |
| `--list-logos` | show all logos |
| `--list-modules` | list of available modules |

The config file is located in `~/.config/jefetch/config.jsonc`

## Any distro with Nix:

```sh
nix develop   # drop into a shell with cargo
make          # build inside the dev shell
```
Or
```sh
nix run github:Matko802/jefetch
```

## As a flake input

```nix
{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    jefetch = {
      url = "github:Matko802/jefetch";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { nixpkgs, jefetch, ... }: {
    packages.x86_64-linux.default = jefetch.packages.x86_64-linux.default;
  };
}
```

## As an overlay

```nix
{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    jefetch = {
      url = "github:Matko802/jefetch";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    { nixpkgs, jefetch, ... }:
    let
      system = "x86_64-linux";
    in
    {
      nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
        inherit system;
        modules = [
          {
            nixpkgs.overlays = [ jefetch.overlays.default ];
            environment.systemPackages = [ jefetch.packages.${system}.default ];
          }
        ];
      };
    };
}
```

## Standalone build from source

```sh
nix build github:Matko802/jefetch
nix run github:Matko802/jefetch
```

## Develop

```sh
nix develop github:Matko802/jefetch
```

## License

This project is released under the MIT License. See [LICENSE](https://github.com/Matko802/jefetch/blob/main/LICENSE).
