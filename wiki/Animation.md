# Animation — Areofyl 1:1 Spin

sharkfetch embeds a **faithful Rust port of areofyl/fetch's `fetch.c` 3D engine** (`src/anim.rs:1`) — not a subprocess call. Static and animated share the **same `render_modules()`** output; only the logo moves (`src/app.rs:240`).

## Enabling

=== "TOML"

    ```toml
    [logo]
    name = "nixos"
    animation = "spin"              # default: spin y, speed=2, size=2, depth=2
    # animation = "spin y speed=2.0"
    # animation = "spin x speed=1.5"
    # animation = "spin z"
    # animation = "spin xyz speed=1.0 speed_x=1 speed_y=-1 speed_z=0.5"
    # animation = "off"             # force static
    ```

=== "JSONC"

    ```jsonc
    {
        "logo": {
            "source": "nixos",
            "animation": "spin y speed=2.0"
            // "animation": "spin xyz speed=1.5 speed_z=-1"
        }
    }
    ```

| Value | Result |
|-------|--------|
| `off`/`none`/`static`/`false`/`0` | Always static (`src/app.rs:85`) |
| `spin` / `spin y` / `areo` / `rotate` / `on`/`true`/`1` | Animated |
| Any other non-empty string | Animated (future types) |
| Unset / commented | Static |

CLI override: `sharkfetch --static` forces static even if config says `spin` (`src/app.rs:85`).

Quit animation: `q` / `Q` / `Ctrl-C` / `Esc` (reads `/dev/tty` + `stdin` non-blocking, restores `?25h`/`?1049l` `src/app.rs:298`).

---

## Tabs: Axis & Speed/Direction

The engine supports **X / Y / Z** axes independently, each with its own speed/direction — identical math to `fetch.c:4268` `K1=37*logo_height/36` `K2=5.5` with Blinn-Phong.

=== "Axes"

    | Config | Spins around |
    |--------|--------------|
    | `spin x` | X axis (pitch) — `A += 0.04*speed` |
    | `spin y` | Y axis (yaw) — `B += 0.06*speed` (default) |
    | `spin z` | Z axis (roll, in-plane) — `C += 0.05*speed` |
    | `spin xy` / `spin yx` | X+Y |
    | `spin xyz` / `spin zyx` | All three |
    | `spin` (bare) | Defaults to `y` |

    Examples:
    ```toml
    animation = "spin x"
    animation = "spin y"
    animation = "spin z"
    animation = "spin xyz"
    ```

=== "Speed & Direction"

    | Key | Effect | Example |
    |-----|--------|---------|
    | `speed=1.5` | Overall multiplier (scales all axes) | `spin y speed=2.0` |
    | `speed_x=0.5` | X only | `spin xyz speed_x=1 speed_y=-1` |
    | `speed_y=-1` | Negative = reverse direction | `spin y speed_y=-1` |
    | `speed_z=0.3` | Z only | `spin z speed_z=2` |

    Parsing: `extract_number(low, "speed")` etc. (`src/anim.rs:82`) accepts `speed=2`, `speed:2`, `speed 2`, `speed_x=-1.5` with floats.

    Full examples:
    ```toml
    animation = "spin y speed=2.0"
    animation = "spin xyz speed=1.5 speed_z=-1"
    animation = "spin xy speed_x=0.8 speed_y=1.2"
    ```

=== "Presets (copy-paste)"

    ```toml
    # Gentle areofyl-like (default): Y only, solid blocks
    animation = "spin y speed=2.0"

    # Fast tumble
    animation = "spin xyz speed=2.5"

    # Slow Y, reverse Z
    animation = "spin yz speed_y=0.6 speed_z=-1"

    # X-only pitch
    animation = "spin x speed=1.0"

    # Disable
    animation = "off"
    ```

---

## What the Math Does (1:1 `fetch.c`)

For the curious (`src/anim.rs:292` `build_points`, `src/anim.rs:536` `render_frame`):

1. **Heightmap** `hmap[r][c] = char_weight_utf8(cell)` — `M=1.0`, `█=1.0`, `▓=0.75` … (`src/anim.rs:110`), spaces `0`.
2. **Auto-depth** boost if stddev < 0.25 (`src/anim.rs:318`).
3. **Normals** from `dhdx/SX`, `dhdy/SY` (`SX=0.07`, `SY=0.14`).
4. **Point cloud**: `subdiv = size as usize` (default `2` → 2×2 per cell), `z_layers = max(6*size,6)` (default `12`), extruded sides with `is_edge` check.
5. **Projection** per frame `A/B/C` → `y1/z1/x2/z2/x3/y3` → `zc=z3+K2`, `ooz=1/zc`, `xs=half_aw+K1*2*x3*ooz`, `ys=y_center-K1*y3*ooz` (`src/anim.rs:595`). `K1=37*logo_height/36`, `half_aw=30`, `y_center=1+info*0.5`.
6. **Shading** `L=0.08+0.62*diff+0.30*spec` (`spec=(dot)^16`) → `ci = lum*smax+0.5` → `shading[ci]` where `DEFAULT_SHADING=["░","▒","▓","█"]` (`src/anim.rs:15`) maps to solid blocks (fetch `--shading-mode blocks` look; fetch default ascii is `.,-~:;=!*#$@`).

Colors: if logo has ANSI (`logo.colors` or inline `\x1b[34m`), `colorbuf` preserves `34` → `\x1b[1;34m`; else two-tone `1;37` inner / `1;35` outer (`src/anim.rs:668`).

## Layout

Animated and static share one `base_lines = render_modules(entries)` (`src/app.rs:215`). The 3D logo is rendered into a `60×render_height` canvas (`ANIM_WIDTH=60` `src/anim.rs:9`, `render_height=max(info+2,36)` `src/app.rs:243`) centered vertically (`y_center`) and placed left of the info with `GAP=2`. Text column is **fixed** — no jitter.

## Performance

- `build_points` runs once per animation start (not per frame) — the cloud is reused.
- Frame loop: `poll` `stdin`/`/dev/tty` non-blocking, `usleep 50ms` equivalent via `8×10ms` slices (`src/app.rs:267`), `?25l`/`?1049h` hide/alternate screen.

Next: [Logos](Logos) →
