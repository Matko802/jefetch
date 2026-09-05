# Installation

Same static `musl` binary every way. Needs Linux x86_64, `cargo` + `rustup` (or Nix).

```sh
nix run github:Matko802/jefetch                      # one-off
git clone https://github.com/Matko802/jefetch && cd jefetch
make deps && make && sudo make install               # /usr/local/bin/jefetch
nix build github:Matko802/jefetch && ./result/bin/jefetch
```

Update: `nix flake lock --update-input jefetch`, or `git pull --rebase origin main && ./build.sh`.

Verify: `ldd target/x86_64-unknown-linux-musl/release/jefetch` → `not a dynamic executable`.
Use `./build.sh` / `make`, not `cargo build --release` (Nix's cargo lacks the musl target).

Makefile: `make deps`, `make` / `make release`, `make debug`, `make test`, `sudo make install`.

Next: [Configuration](Configuration.md) →
