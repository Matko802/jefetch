<div align="center">

<img src="Logo/jefetch.png" width="120" alt="Its pronounced as ye fetch" />

# jefetch
<sub>Its pronounced as ye fetch<sub>

Inspired by [fastfetch](https://github.com/fastfetch-cli/fastfetch) and [fetch](https://github.com/areofyl/fetch)

</div>

## Features

- Uses Fastfetch logos
- Uses Musl lib instead of glibc
- integrated with [sharkvis](https://github.com/Matko802/sharkvis)

## Building

```sh
git clone https://github.com/Matko802/jefetch.git
cd jefetch
make deps
make
sudo make install
```
## Updating it

```sh
cd jefetch && git pull && sudo make install
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

## Update

Nix flake:

```sh
cd ~/fish-flake
nix flake update jefetch; nh os switch -H machine1
```

Standalone:

```sh
cd jefetch && git pull && sudo make install
```

Restart running copies after updating (`q` to quit, relaunch).

## Any distro with Nix:

```sh
nix run github:Matko802/jefetch
```

### As flake input

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

### As overlay

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

## License

This project is released under the MIT License. See [LICENSE](https://github.com/Matko802/jefetch/blob/main/LICENSE).
