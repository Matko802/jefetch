# Development

Zero-crate (only `libc`), hand-rolled JSONC parser. Build with `./build.sh` or `make` — **not** `cargo build --release` directly (Nix's cargo has no musl target → dynamic binary).

## Build

=== "build.sh"

    ```sh
    ./build.sh          # release, static musl
    ./build.sh debug
    ./build.sh test     # cargo test --lib
    ```

=== "Make"

    ```sh
    make deps && make
    make debug
    make test
    sudo make install
    ```

=== "Nix"

    ```sh
    nix develop
    make
    ```

Verify static: `ldd target/x86_64-unknown-linux-musl/release/sharkfetch` → `not a dynamic executable`.

## Layout

```
src/
  main.rs          # startup / cli
  app.rs           # App::run(), config, animation
  anim.rs          # areofyl 1:1 3D engine
  logo/            # 530 logos (data.rs generated)
  config/          # JSONC parser, Config structs
  detection/       # os, terminal, packages, shell
  modules/         # renderers
  print/           # color & width helpers
  lib.rs           # pub mod ...
```

## Adding a Module

1. Detect in `src/detection/<name>.rs` (cache with `OnceLock`).
2. Render in `src/modules/exec_impl.rs`.
3. Add to the default structure and a test.

## Testing

```sh
./build.sh test
```

Tests must pass with zero warnings.

## Releasing

```sh
git add -A && git commit -m "feat: ..."
git pull --rebase origin main && git push origin main
```

Next: [FAQ](FAQ) →
