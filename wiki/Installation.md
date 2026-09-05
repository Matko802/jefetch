# Installation

Every method below gives you the same static musl binary. You need Linux
x86_64 and either `cargo` + `rustup`, or Nix.

```sh
nix run github:Matko802/jefetch                      # try it once
git clone https://github.com/Matko802/jefetch && cd jefetch
make deps && make && sudo make install               # installs to /usr/local/bin/jefetch
nix build github:Matko802/jefetch && ./result/bin/jefetch
```

To update: `nix flake lock --update-input jefetch`, or
`git pull --rebase origin main && sudo make install`.

Sanity check that the binary is really static:

```sh
ldd target/x86_64-unknown-linux-musl/release/jefetch
# not a dynamic executable
```

Don't run bare `cargo build --release` on NixOS, its cargo has no musl
target. `make` handles that by going through rustup when it's there.

Other Makefile targets: `make deps`, `make test`, `sudo make install`.

Next: [Configuration](Configuration.md) →
