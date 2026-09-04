# Development

Zero-crate, `libc`-only. Inspired by `sharkvis` `Makefile`.

## Build

=== "Nix (recommended)"

    ```sh
    nix develop
    make            # static musl via rustup stable
    ./target/x86_64-unknown-linux-musl/release/sharkfetch
    ```

=== "Make"

    ```sh
    make deps       # rustup target add x86_64-unknown-linux-musl
    make            # release static
    make debug      # debug
    make test       # cargo test --lib
    sudo make install
    ```

=== "build.sh"

    ```sh
    ./build.sh              # release static (musl via rustup, fallback to dynamic)
    ./build.sh debug
    ./build.sh test         # cargo test --lib --tests
    ```

**Must use `./build.sh` / `make`** — direct `cargo build --release` uses Nix's cargo (no musl) and produces a dynamic binary. Verify:

```sh
ldd target/x86_64-unknown-linux-musl/release/sharkfetch
# not a dynamic executable
```

Fallback: vanilla `cargo` without `rustup` still builds `target/release/sharkfetch` (dynamic).

## Project Layout

```
src/
  main.rs              # --static flag, startup stty/DCS drain
  app.rs               # CliOptions, ResolvedLogo, App::run(), animation
  anim.rs              # 1:1 areofyl fetch.c port (build_points → K1/K2 → zbuf → Blinn-Phong)
  logo/
    mod.rs             # Logo struct, by_name(), LOGOS
    data.rs            # generated 530 logos (ffgen)
  config/
    mod.rs             # hand-rolled JSONC parser
    json.rs            # JsonValue, parse()
    toml.rs            # hand-rolled TOML parser
    toml_config.rs     # DEFAULT_TOML_CONFIG + DEFAULT_JSONC_CONFIG
    configfile.rs      # Config + LogoConfig (animation string)
    display.rs         # DisplayConfig
    general.rs         # GeneralConfig
    moduleargs.rs      # ModuleArgs
  detection/
    mod.rs             # fastfetch_json() dead, run_capture_timeout + termios
    os.rs              # OnceLock os::detect()
    terminal.rs        # TerminalInfo, kitty version cache
    packages.rs        # nix + flatpak counts, persistent cache
    shell.rs           # find_shell_via_proc tree walk
  modules/
    exec_impl.rs       # native renderers + dead fastfetch_* helpers
  print/
    format.rs          # visible_len(), strip_ansi()
    color.rs           # color_code_to_ansi(), named_color_sgr()
  lib.rs               # pub mod anim, app, ...
Cargo.toml             # deps: libc = "0.2", lto=false, codegen-units=16
Makefile               # deps/make/install, sharkvis-style
build.sh               # musl routing
flake.nix              # packages + devShell + overlay
```

## Key Invariants

- **Deps**: only `libc` (`Cargo.toml:9`). No other crates — JSONC/TOML parsers are hand-written.
- **Fastfetch parity**: `Host` filtered, `Packages` split `nix-system/nix-user/flatpak-*` with `isValidNixPkg`, `Display` `in 24"` + `Hz` + `[External]`, `Theme [GTK3]` etc.
- **Animation**: `should_animate()` (`src/app.rs:85`) checks `force_static` then `animation` contains `spin`/`areo`/`rotate`. `--static` forces static. `render_frame()` (`src/anim.rs:536`) shares `base_lines` with static — only logo spins.
- **Termios**: `run_capture_timeout` saves/restores termios, `main.rs` drains pending DCS kitty query, `run_animated` saves raw mode and restores `?25h`/`?1049l`.

## Adding a Module

1. Add detection in `src/detection/<name>.rs` (use `OnceLock` caching, `run_capture_timeout` with termios restore).
2. Add renderer in `src/modules/exec_impl.rs` — implement `json_result()` + `run_instance()` branch.
3. Register in `default_structure()` (`src/app.rs:765`) and `field_map` if needed.
4. Add a test in `tests/` or inline `#[cfg(test)]`.

Example minimal module:
```rust
// src/detection/foo.rs
static CACHE: OnceLock<String> = OnceLock::new();
pub fn get() -> String { CACHE.get_or_init(|| capture()).clone() }

// src/modules/exec_impl.rs
"foo" => Some(ModuleOutput { key: "Foo".into(), values: vec![foo::get()], .. })
```

## Animation Math (for contributors)

`src/anim.rs:292` `build_points`:
- `char_weight_utf8` (`src/anim.rs:110`) → `hmap`
- `effective_depth` auto-boost if `stddev < 0.25`
- `gnx/gny/gnz` from `dhdx/SX`, `dhdy/SY`
- `subdiv = size as usize`, `z_layers = max(6*size,6)`, extruded `PX/PY/PZ` + `NX/NY/NZ` + `PCOLOR`

`src/anim.rs:536` `render_frame`:
- `K1=37*logo_height/36` `K2=5.5` `half_aw=30` `y_center=1+info*0.5`
- Per point: `A=0.04*speed*speed_x`, `B=0.06*speed*speed_y`, `C=0.05*speed*speed_z` → `y1/z1/x2/z2/x3/y3` → `ooz=1/(z3+K2)` → `xs/ys`
- `L=0.08+0.62*diff+0.30*spec` (`spec=(dot)^16`) → `ci = lum*smax+0.5` → `shading[ci]` (`░▒▓█`)

Layout: `render_height=max(info+2,36)` `src/app.rs:243`, canvas `60×render_height`, info at `fetch_start=1` + `GAP=2`.

## Testing

```sh
./build.sh test
cargo test --lib --tests
```

- 5 lib tests + 8 jsonc tests must pass
- `hyperfine` warm `~4ms` vs fastfetch `~63ms` (15×)
- Check no warnings: `cargo build` should be clean

## Releasing

```sh
git add -A && git commit -m "feat: ..."
git pull --rebase origin main
git push origin main
# update fish-flake:
nix flake lock --update-input sharkfetch  # in /home/matko/fish-flake
# (do not commit fish-flake unless asked)
```

Next: [FAQ](FAQ) →
