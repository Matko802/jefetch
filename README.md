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

The config file is located in `~/.config/sharkfetch/`

##Any distro with Nix:

```sh
nix develop   # drop into a shell with cargo
make          # build inside the dev shell
```
Or
```sh
nix run github:Matko802/sharkfetch
```

## As a flake input

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

## As an overlay


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

## Standalone build from source

```sh
nix build github:Matko802/sharkfetch
nix run github:Matko802/sharkfetch
```

## Develop

```sh
nix develop github:Matko802/sharkfetch
```

## License

This project is released under the MIT License. See [LICENSE](https://github.com/Matko802/sharkfetch/blob/main/LICENSE).
