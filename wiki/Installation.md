# Installation

All methods produce the **same static musl binary**. Verify with `ldd` → `not a dynamic executable`.

## System Requirements

- Linux `x86_64` (NixOS tested, any distro works for the binary)
- For building: `cargo` + `rustup` stable, or Nix

---

## Tabs: Pick Your Method

=== "Nix (flake input) — Recommended"

    **As a flake input (your `flake.nix`):**
    ```nix
    {
      inputs = {
        nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
        sharkfetch.url = "github:Matko802/sharkfetch";
        sharkfetch.inputs.nixpkgs.follows = "nixpkgs";
      };
      outputs = { nixpkgs, sharkfetch, ... }: {
        nixosConfigurations.myhost = nixpkgs.lib.nixosSystem {
          system = "x86_64-linux";
          modules = [{
            nixpkgs.overlays = [ sharkfetch.overlays.default ];
            environment.systemPackages = [ sharkfetch.packages.x86_64-linux.default ];
          }];
        };
      };
    }
    ```
    ```sh
    nh os switch -H myhost   # or nixos-rebuild switch
    sharkfetch
    ```

    **One-off run (no install):**
    ```sh
    nix run github:Matko802/sharkfetch
    nix run github:Matko802/sharkfetch -- --help
    nix run github:Matko802/sharkfetch -- --static
    ```

=== "Nix (develop shell)"

    ```sh
    git clone https://github.com/Matko802/sharkfetch && cd sharkfetch
    nix develop                 # drops into devShell with cargo + musl target
    make                        # or ./build.sh
    ./target/x86_64-unknown-linux-musl/release/sharkfetch
    ```

=== "Cargo / Make (any distro)"

    ```sh
    git clone https://github.com/Matko802/sharkfetch && cd sharkfetch
    make deps                   # installs musl target via rustup if needed
    make                        # == ./build.sh  (release, static musl)
    sudo make install           # installs to /usr/local/bin/sharkfetch

    # Verify:
    ldd target/x86_64-unknown-linux-musl/release/sharkfetch
    # not a dynamic executable

    # Fallback (dynamic, vanilla cargo without musl):
    cargo build --release       # target/release/sharkfetch
    ```

=== "Makefile targets"

    | Target | What it does |
    |--------|--------------|
    | `make deps` | `rustup target add x86_64-unknown-linux-musl` |
    | `make` / `make release` | `./build.sh` → static musl release |
    | `make debug` | debug build |
    | `make test` | `cargo test --lib` (5 + 8 jsonc tests) |
    | `sudo make install` | copies musl binary to `/usr/local/bin` or `DESTDIR` |

=== "Prebuilt (nix build)"

    ```sh
    nix build github:Matko802/sharkfetch
    ./result/bin/sharkfetch
    ls -lh result/bin/sharkfetch
    file result/bin/sharkfetch  # ELF 64-bit LSB executable, statically linked
    ```

---

## Updating

=== "Nix flake"

    ```sh
    cd /path/to/your/flake
    nix flake lock --update-input sharkfetch
    nh os switch -H myhost
    ```

=== "Git"

    ```sh
    cd sharkfetch
    git pull --rebase origin main
    ./build.sh
    sudo make install
    ```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `ldd` says `not a dynamic executable` is **good** — that's static | — |
| `cargo build` uses Nix's cargo (no musl) | Use `./build.sh` (routes via `rustup` stable) |
| `~/.config/sharkfetch/config.toml` already exists after install | Delete it if you want to switch to JSONC — see [Configuration](Configuration) |
| Animation not starting | Check `animation = "spin"` is uncommented and not `off`/`static`/`false` |

Next: [Configuration](Configuration) →
