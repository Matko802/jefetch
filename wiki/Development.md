# Development

Zero-crate (only `libc`), hand-rolled JSONC parser.

```sh
./build.sh            # release, static musl (also: make)
./build.sh debug
./build.sh test       # cargo test --lib
nix develop && make   # Nix shell
ldd target/x86_64-unknown-linux-musl/release/jefetch  # must say: not a dynamic executable
```

```
src/main.rs      startup / cli
src/app.rs       App::run(), config, animation
src/anim.rs      3D engine
src/logo/        530 logos (data.rs generated)
src/config/      JSONC parser, Config structs
src/detection/   os, terminal, packages, shell, ...
src/modules/     renderers
src/print/       color & width helpers
```

New module: detect in `src/detection/<name>.rs` (cache with `OnceLock`), render in `src/modules/exec_impl.rs`, add to the default structure and a test. Tests must pass with zero warnings (`./build.sh test`).

Release: `git add -A && git commit -m "feat: ..." && git pull --rebase origin main && git push origin main`.

Next: [FAQ](FAQ.md) →
