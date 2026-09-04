// Faithful Rust port of areofyl/fetch 3D spinning engine (fetch.c)
// Replicates build_points (heightmap + extruded sides + normals), K1/K2
// projection onto a sized canvas, z-buffer and Blinn-Phong
// (diffuse + specular). The logo keeps its own ANSI two-tone colours.

use crate::app::ResolvedLogo;

const K2: f32 = 5.5;
const ANIM_WIDTH: i32 = 60;
const GAP: usize = 2;
const MAX_POINTS: usize = 400_000;

// Solid blocks — 1:1 to fetch --shading-mode blocks (visually matches
// areofyl's solid look; ascii ".,-~:;=!*#$@" is fetch default but too faint)
const DEFAULT_SHADING: &[&str] = &["░", "▒", "▓", "█"];

#[derive(Debug, Clone)]
pub struct AnimConfig {
    pub spin_x: bool,
    pub spin_y: bool,
    pub spin_z: bool,
    pub speed: f32,
    /// per-axis speed multipliers (1.0 = normal, negative = reverse)
    pub speed_x: f32,
    pub speed_y: f32,
    pub speed_z: f32,
    pub size: f32,
    pub depth: f32,
    pub depth_user_set: bool,
    pub height: i32,
    pub light_x: f32,
    pub light_y: f32,
    pub light_z: f32,
    pub shading: Vec<String>,
}

impl Default for AnimConfig {
    fn default() -> Self {
        Self {
            spin_x: false,
            spin_y: true,
            spin_z: false,
            speed: 2.0,
            speed_x: 1.0,
            speed_y: 1.0,
            speed_z: 1.0,
            size: 2.0,
            depth: 2.0,
            depth_user_set: true,
            height: 0,
            light_x: -0.4082,
            light_y: 0.8165,
            light_z: -0.4082,
            shading: DEFAULT_SHADING.iter().map(|s| s.to_string()).collect(),
        }
    }
}

impl AnimConfig {
    pub fn from_animation_str(s: Option<&str>) -> Self {
        let mut cfg = Self::default();
        if let Some(raw) = s {
            let low = raw.to_ascii_lowercase();
            // detect axes: tolerate "spin x", "spin=x", "spin: x y z", "xyz", etc.
            let has_x = low.contains('x');
            let has_y = low.contains('y');
            let has_z = low.contains('z');
            // Only override spin if animation string mentions axes/spin
            if low.contains("spin") || has_x || has_y || has_z || low.contains("rotate") {
                if has_x || has_y || has_z {
                    cfg.spin_x = has_x;
                    cfg.spin_y = has_y;
                    cfg.spin_z = has_z;
                } else if low.contains("spin") {
                    // bare "spin" -> keep default (spin_y)
                }
            }
            if let Some(v) = extract_number(&low, "speed_x") {
                cfg.speed_x = v;
            }
            if let Some(v) = extract_number(&low, "speed_y") {
                cfg.speed_y = v;
            }
            if let Some(v) = extract_number(&low, "speed_z") {
                cfg.speed_z = v;
            }
            if let Some(v) = extract_number(&low, "speed") {
                // generic speed scales all axes unless per-axis already set
                // (and keeps sign for direction: negative = reverse)
                cfg.speed = v;
            }
            if let Some(v) = extract_number(&low, "size") {
                cfg.size = v;
            }
            if let Some(v) = extract_number(&low, "depth") {
                cfg.depth = v;
                cfg.depth_user_set = true;
            }
            if let Some(v) = extract_number(&low, "height") {
                cfg.height = v as i32;
            }
        }
        cfg
    }
}

fn extract_number(s: &str, key: &str) -> Option<f32> {
    let start = s.find(key)? + key.len();
    let rest = &s[start..];
    // Skip the separator(s) between the key and the value (e.g. "=", ":", spaces).
    let rest = rest.trim_start_matches(|c: char| !c.is_ascii_digit() && c != '-' && c != '.');
    let mut end = 0;
    for (i, c) in rest.char_indices() {
        if c.is_ascii_digit() || c == '.' || c == '-' {
            end = i + c.len_utf8();
        } else {
            break;
        }
    }
    if end == 0 {
        return None;
    }
    rest[..end].parse::<f32>().ok()
}

#[inline]
fn utf8_len(first: u8) -> usize {
    match first {
        0x00..=0x7F => 1,
        0xC0..=0xDF => 2,
        0xE0..=0xEF => 3,
        _ => 4,
    }
}

fn char_weight_utf8(ch: &str) -> f32 {
    if ch.is_empty() {
        return 0.0;
    }
    let bytes = ch.as_bytes();
    if bytes[0] < 0x80 {
        match bytes[0] as char {
            'M' => return 1.00,
            'N' => return 0.88,
            'm' => return 0.76,
            'd' => return 0.66,
            'h' | 'b' => return 0.56,
            'y' => return 0.46,
            'o' | 'n' => return 0.38,
            's' => return 0.30,
            '+' => return 0.22,
            ':' => return 0.18,
            '=' => return 0.22,
            '-' => return 0.14,
            '`' => return 0.08,
            '.' => return 0.10,
            '/' => return 0.12,
            '\'' => return 0.06,
            ' ' => return 0.0,
            c => {
                if c >= 'A' && c <= 'Z' {
                    return 0.80;
                }
                if c >= 'a' && c <= 'z' {
                    return 0.50;
                }
                if c >= '0' && c <= '9' {
                    return 0.40;
                }
                return 0.15;
            }
        }
    }
    if bytes.len() >= 3 && bytes[0] == 0xe2 && bytes[1] == 0x96 {
        match bytes[2] {
            0x88 => return 1.00, // █
            0x93 => return 0.75, // ▓
            0x92 => return 0.50, // ▒
            0x91 => return 0.25, // ░
            0x80 => return 0.50, // ▀
            0x84 => return 0.50, // ▄
            0x8c => return 0.50, // ▌
            0x90 => return 0.50, // ▐
            0x82 => return 0.55, // ▂
            0x81 => return 0.30, // ▁
            _ => return 0.50,
        }
    }
    if bytes[0] == 0xe2 && (bytes[1] == 0x94 || bytes[1] == 0x95) {
        return 0.20; // box drawing
    }
    0.30
}

struct Point {
    x: f32,
    y: f32,
    z: f32,
    nx: f32,
    ny: f32,
    nz: f32,
    color: i32,
}

// Parse ResolvedLogo.lines (may contain ANSI) into per-cell (glyph, color) grid.
fn parse_cells(logo: &ResolvedLogo) -> (Vec<Vec<(String, i32)>>, bool, usize, usize) {
    let mut cells: Vec<Vec<(String, i32)>> = Vec::new();
    let mut has_ansi = false;
    let mut max_cols = 0usize;
    for line in &logo.lines {
        let mut row: Vec<(String, i32)> = Vec::new();
        let s = line.as_str();
        let bytes = s.as_bytes();
        let mut i = 0usize;
        let mut cur: i32 = 0;
        let n = s.len();
        while i < n {
            let b = bytes[i];
            if b == 0x1b && i + 1 < n && bytes[i + 1] == b'[' {
                let mut j = i + 2;
                while j < n && (bytes[j].is_ascii_digit() || bytes[j] == b';') {
                    j += 1;
                }
                if j < n && bytes[j] as char == 'm' {
                    let seq = &s[i + 2..j];
                    if seq.is_empty() {
                        cur = 0;
                    } else {
                        for part in seq.split(';') {
                            if part.is_empty() {
                                cur = 0;
                                continue;
                            }
                            if let Ok(num) = part.parse::<i32>() {
                                if (30..=37).contains(&num) || (90..=97).contains(&num) {
                                    cur = num;
                                    has_ansi = true;
                                } else if num == 0 || num == 39 {
                                    cur = 0;
                                }
                            }
                        }
                    }
                    i = j + 1;
                    continue;
                }
                i = j + 1;
                continue;
            }
            let mut actual = utf8_len(b);
            if i + actual > n {
                actual = n - i;
            }
            let mut valid = true;
            for k in 1..actual {
                if i + k >= n || (bytes[i + k] & 0xC0) != 0x80 {
                    valid = false;
                    break;
                }
            }
            if !valid {
                actual = 1;
            }
            let ch = &s[i..i + actual];
            row.push((ch.to_string(), cur));
            i += actual;
        }
        if row.len() > max_cols {
            max_cols = row.len();
        }
        cells.push(row);
    }
    let rows = cells.len();
    // Fallback: many builtin logos store colour in `logo.colors` (e.g. "34")
    // not inline ANSI. Treat that as has_ansi so the 3D keeps the logo colour.
    if !has_ansi {
        for c in &logo.colors {
            if !c.is_empty() {
                // c is like "34" or "1;34" — extract the fg number
                for part in c.split(';') {
                    if let Ok(num) = part.parse::<i32>() {
                        if (30..=37).contains(&num) || (90..=97).contains(&num) {
                            has_ansi = true;
                            break;
                        }
                    }
                }
                if has_ansi { break; }
            }
        }
    }
    // If still no ANSI but we have a base colour, inject it into cells
    // so the point cloud gets coloured instead of grey.
    if has_ansi {
        // Fill in per-cell colour from logo.colors where cell colour is 0
        for (r, row) in cells.iter_mut().enumerate() {
            if r >= logo.colors.len() { break; }
            let col_s = &logo.colors[r];
            if col_s.is_empty() { continue; }
            let mut col_num = 0;
            for part in col_s.split(';') {
                if let Ok(num) = part.parse::<i32>() {
                    if (30..=37).contains(&num) || (90..=97).contains(&num) {
                        col_num = num;
                    }
                }
            }
            if col_num == 0 { continue; }
            for cell in row.iter_mut() {
                if cell.1 == 0 {
                    cell.1 = col_num;
                }
            }
        }
    }
    (cells, has_ansi, rows, max_cols)
}

fn build_points(
    cells: &[Vec<(String, i32)>],
    has_ansi: bool,
    config: &AnimConfig,
    rows: usize,
    cols: usize,
) -> Vec<Point> {
    if rows == 0 || cols == 0 {
        return Vec::new();
    }
    const SX: f32 = 0.07;
    const SY: f32 = 0.14;
    let cx = (cols as f32 - 1.0) * 0.5;
    let cy = (rows as f32 - 1.0) * 0.5;

    let mut hmap = vec![vec![0.0f32; cols]; rows];
    for r in 0..rows {
        for c in 0..cols {
            hmap[r][c] = if c < cells[r].len() {
                char_weight_utf8(&cells[r][c].0)
            } else {
                0.0
            };
        }
    }

    let mut effective_depth = config.depth;
    if !config.depth_user_set {
        let mut sum = 0.0f32;
        let mut sum2 = 0.0f32;
        let mut n = 0usize;
        for r in 0..rows {
            for c in 0..cols {
                let h = hmap[r][c];
                if h > 0.0 {
                    sum += h;
                    sum2 += h * h;
                    n += 1;
                }
            }
        }
        if n > 0 {
            let mean = sum / n as f32;
            let var = sum2 / n as f32 - mean * mean;
            let std = if var > 0.0 { var.sqrt() } else { 0.0 };
            if std < 0.25 {
                let boost = 1.0 + 2.0 * (0.25 - std) / 0.25;
                effective_depth *= boost;
            }
        }
    }
    let zmax = 0.18 * effective_depth;

    // Per-cell gradient normals.
    let mut gnx = vec![vec![0.0f32; cols]; rows];
    let mut gny = vec![vec![0.0f32; cols]; rows];
    let mut gnz = vec![vec![1.0f32; cols]; rows];
    for r in 0..rows {
        for c in 0..cols {
            if hmap[r][c] <= 0.0 {
                gnx[r][c] = 0.0;
                gny[r][c] = 0.0;
                gnz[r][c] = 1.0;
                continue;
            }
            let dhdx = if c > 0 && c + 1 < cols {
                (hmap[r][c + 1] - hmap[r][c - 1]) * 0.5
            } else if c == 0 && cols > 1 {
                hmap[r][c + 1] - hmap[r][c]
            } else if cols > 1 {
                hmap[r][c] - hmap[r][c - 1]
            } else {
                0.0
            };
            let dhdy = if r > 0 && r + 1 < rows {
                (hmap[r + 1][c] - hmap[r - 1][c]) * 0.5
            } else if r == 0 && rows > 1 {
                hmap[r + 1][c] - hmap[r][c]
            } else if rows > 1 {
                hmap[r][c] - hmap[r - 1][c]
            } else {
                0.0
            };
            let n_x = -dhdx / SX;
            let n_y = dhdy / SY;
            let l = (n_x * n_x + n_y * n_y + 1.0).sqrt();
            if l > 1e-6 {
                gnx[r][c] = n_x / l;
                gny[r][c] = n_y / l;
                gnz[r][c] = 1.0 / l;
            }
        }
    }

    let z_layers = ((6.0 * config.size) as i32).max(6) as usize;
    let mut subdiv = (config.size) as usize;
    if subdiv < 1 {
        subdiv = 1;
    }

    let mut points: Vec<Point> = Vec::new();
    points.reserve(MAX_POINTS.min(rows * cols * subdiv * subdiv * 3));

    for row in 0..rows {
        for col in 0..cols {
            let h = hmap[row][col];
            if h <= 0.0 {
                continue;
            }
            for sr in 0..subdiv {
                for sc in 0..subdiv {
                    let frow = row as f32 + sr as f32 / subdiv as f32;
                    let fcol = col as f32 + sc as f32 / subdiv as f32;
                    let ih = if sr == 0 && sc == 0 {
                        h
                    } else {
                        let fr = sr as f32 / subdiv as f32;
                        let fc = sc as f32 / subdiv as f32;
                        let mut nr = row + 1;
                        if nr >= rows {
                            nr = rows - 1;
                        }
                        let mut nc = col + 1;
                        if nc >= cols {
                            nc = cols - 1;
                        }
                        let h00 = hmap[row][col];
                        let h10 = hmap[nr][col];
                        let h01 = hmap[row][nc];
                        let h11 = hmap[nr][nc];
                        let v = h00 * (1.0 - fr) * (1.0 - fc)
                            + h10 * fr * (1.0 - fc)
                            + h01 * (1.0 - fr) * fc
                            + h11 * fr * fc;
                        if v <= 0.0 {
                            continue;
                        }
                        v
                    };
                    if ih <= 0.0 {
                        continue;
                    }
                    let ox = (fcol - cx) * SX;
                    let oy = (cy - frow) * SY;
                    let zr = ih * zmax;

                    let mut is_edge = false;
                    'outer: for dr in -1i32..=1 {
                        for dc in -1i32..=1 {
                            if dr == 0 && dc == 0 {
                                continue;
                            }
                            let nr = row as i32 + dr;
                            let nc = col as i32 + dc;
                            let nh = if nr >= 0 && nr < rows as i32 && nc >= 0 && nc < cols as i32
                            {
                                hmap[nr as usize][nc as usize]
                            } else {
                                0.0
                            };
                            if nh <= 0.0 {
                                is_edge = true;
                                break 'outer;
                            }
                        }
                    }
                    let layers = if is_edge || ih < 0.15 { 2 } else { z_layers };
                    if layers < 2 {
                        continue;
                    }
                    for k in 0..layers {
                        if points.len() >= MAX_POINTS {
                            break;
                        }
                        let t = k as f32 / (layers - 1) as f32 - 0.5;
                        let px = ox;
                        let py = oy;
                        let pz = t * 2.0 * zr;
                        let col_val = if has_ansi {
                            cells[row][col].1
                        } else {
                            if k == 0 || k == layers - 1 {
                                1
                            } else {
                                0
                            }
                        };
                        let (nx, ny, nz) = if k == 0 {
                            (gnx[row][col], gny[row][col], -gnz[row][col])
                        } else if k == layers - 1 {
                            (gnx[row][col], gny[row][col], gnz[row][col])
                        } else {
                            let mut ex = 0.0f32;
                            let mut ey = 0.0f32;
                            for dr in -1i32..=1 {
                                for dc in -1i32..=1 {
                                    if dr == 0 && dc == 0 {
                                        continue;
                                    }
                                    let nr = row as i32 + dr;
                                    let nc = col as i32 + dc;
                                    let nh = if nr >= 0 && nr < rows as i32
                                        && nc >= 0 && nc < cols as i32
                                    {
                                        hmap[nr as usize][nc as usize]
                                    } else {
                                        0.0
                                    };
                                    if nh < h {
                                        ex += dc as f32;
                                        ey += -(dr as f32);
                                    }
                                }
                            }
                            let el = (ex * ex + ey * ey).sqrt();
                            if el > 1e-6 {
                                ex /= el;
                                ey /= el;
                            }
                            let tn = k as f32 / (layers - 1) as f32 * 2.0 - 1.0;
                            let side = (1.0 - tn * tn).sqrt().max(0.0);
                            (ex * side, ey * side, tn)
                        };
                        points.push(Point {
                            x: px,
                            y: py,
                            z: pz,
                            nx,
                            ny,
                            nz,
                            color: col_val,
                        });
                    }
                }
            }
        }
    }
    points
}

/// Render a single 3D frame of the logo into a canvas of `render_height` rows
/// and ANIM_WIDTH columns. The returned ResolvedLogo has `render_height` lines
/// each `ANIM_WIDTH` wide (RGB-coloured), so callers can lay the logo beside
/// system-info lines, vertically centred like areofyl.
pub fn render_frame(
    logo: &ResolvedLogo,
    frame: usize,
    config: &AnimConfig,
    render_height: usize,
    info_line_count: usize,
) -> ResolvedLogo {
    if logo.lines.is_empty() {
        return logo.clone();
    }
    let (cells, has_ansi, rows, cols) = parse_cells(logo);
    if rows == 0 || cols == 0 {
        return logo.clone();
    }

    let points = build_points(&cells, has_ansi, config, rows, cols);
    if points.is_empty() {
        return logo.clone();
    }

    // --- areofyl layout sizing ---
    let render_height = render_height.max(1);
    let logo_height = render_height.min((ANIM_WIDTH * 3 / 5) as usize).max(1);
    let k1 = 37.0 * logo_height as f32 / 36.0;
    let half_aw = ANIM_WIDTH as f32 * 0.5;
    let w = ANIM_WIDTH as usize;
    let h = render_height;

    let mut zbuf = vec![0.0f32; h * w];
    let mut lumbuf = vec![0.0f32; h * w];
    let mut colorbuf = vec![0i32; h * w];

    let mul = frame as f32;
    let a = if config.spin_x { mul * 0.04 * config.speed * config.speed_x } else { 0.0 };
    let b = if config.spin_y { mul * 0.06 * config.speed * config.speed_y } else { 0.0 };
    let c_ang = if config.spin_z { mul * 0.05 * config.speed * config.speed_z } else { 0.0 };
    let (ca, sa) = (a.cos(), a.sin());
    let (cb, sb) = (b.cos(), b.sin());
    let (cc, sc) = (c_ang.cos(), c_ang.sin());

    let lx = config.light_x;
    let ly = config.light_y;
    let lz = config.light_z;
    let hx0 = lx;
    let hy0 = ly;
    let hz0 = lz - 1.0;
    let hl0 = (hx0 * hx0 + hy0 * hy0 + hz0 * hz0).sqrt();
    let (hlx, hly, hlz) = if hl0 > 1e-6 {
        (hx0 / hl0, hy0 / hl0, hz0 / hl0)
    } else {
        (0.0, 0.0, -1.0)
    };

    // Vertically centre the logo on the info block like areofyl.
    let y_center = if info_line_count > 0 && info_line_count + 2 <= render_height {
        1.0 + info_line_count as f32 * 0.5
    } else {
        h as f32 * 0.5
    };
    let k1x2 = k1 * 2.0;

    for p in &points {
        let (px, py, pz) = (p.x, p.y, p.z);
        let (nx, ny, nz) = (p.nx, p.ny, p.nz);
        let y1 = py * ca - pz * sa;
        let z1 = py * sa + pz * ca;
        let x2 = px * cb + z1 * sb;
        let z2 = -px * sb + z1 * cb;
        let y2 = y1;
        let ny1 = ny * ca - nz * sa;
        let nz1 = ny * sa + nz * ca;
        let nx2 = nx * cb + nz1 * sb;
        let nz2 = -nx * sb + nz1 * cb;
        let ny2 = ny1;
        // Z rotation (around view axis) — enabled via `spin z`
        let x3 = x2 * cc - y2 * sc;
        let y3 = x2 * sc + y2 * cc;
        let z3 = z2;
        let nx3 = nx2 * cc - ny2 * sc;
        let ny3 = nx2 * sc + ny2 * cc;
        let nz3 = nz2;

        let zc = z3 + K2;
        if zc < 0.1 {
            continue;
        }
        let ooz = 1.0 / zc;
        let xs = (half_aw + k1x2 * x3 * ooz) as i32;
        let ys = (y_center - k1 * y3 * ooz) as i32;
        if xs < 0 || xs >= w as i32 || ys < 0 || ys >= h as i32 {
            continue;
        }
        let idx = ys as usize * w + xs as usize;
        if ooz > zbuf[idx] {
            let mut diff = nx3 * lx + ny3 * ly + nz3 * lz;
            if diff < 0.0 {
                diff = 0.0;
            }
            let mut spec_dot = nx3 * hlx + ny3 * hly + nz3 * hlz;
            if spec_dot < 0.0 {
                spec_dot = 0.0;
            }
            let mut spec = spec_dot * spec_dot;
            spec = spec * spec;
            spec = spec * spec;
            let mut lum = 0.08 + 0.62 * diff + 0.30 * spec;
            if lum > 1.0 {
                lum = 1.0;
            }
            zbuf[idx] = ooz;
            lumbuf[idx] = lum;
            colorbuf[idx] = p.color;
        }
    }

    // Render each canvas row to a (possibly empty) coloured glyph string.
    let smax = config.shading.len().saturating_sub(1);
    let mut lines: Vec<String> = Vec::with_capacity(h);
    for row in 0..h {
        let mut line = String::new();
        let mut prev_color: i32 = -2;
        for col in 0..w {
            let idx = row * w + col;
            if zbuf[idx] <= 0.0 {
                if prev_color != -2 && prev_color != -1 {
                    line.push_str("\x1b[0m");
                    prev_color = -1;
                }
                line.push(' ');
                continue;
            }
            let lum = lumbuf[idx];
            let mut ci = (lum * smax as f32 + 0.5) as usize;
            if ci > smax {
                ci = smax;
            }
            let c = colorbuf[idx];
            if c != prev_color {
                if prev_color != -2 && prev_color != -1 {
                    line.push_str("\x1b[0m");
                }
                if has_ansi && c > 0 && c < 128 {
                    line.push_str(&format!("\x1b[1;{}m", c));
                } else if has_ansi {
                    line.push_str("\x1b[0m");
                } else if c == 1 {
                    line.push_str("\x1b[1;37m");
                } else {
                    line.push_str("\x1b[1;35m");
                }
                prev_color = c;
            }
            line.push_str(&config.shading[ci]);
        }
        if prev_color != -2 && prev_color != -1 {
            line.push_str("\x1b[0m");
        }
        lines.push(line);
    }

    let width = w;
    ResolvedLogo {
        lines,
        colors: Vec::new(),
        width,
        padding_right: GAP,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn speeds_parse_from_spin_string() {
        let cfg = AnimConfig::from_animation_str(Some("spin z speed=1.5"));
        assert!(cfg.spin_z);
        assert!((cfg.speed - 1.5).abs() < 1e-4, "speed={}", cfg.speed);

        let cfg = AnimConfig::from_animation_str(Some("spin xyz speed=1.0 speed_y=-1"));
        assert!(cfg.spin_x && cfg.spin_y && cfg.spin_z);
        assert!((cfg.speed - 1.0).abs() < 1e-4);
        assert!((cfg.speed_y - (-1.0)).abs() < 1e-4);

        let cfg = AnimConfig::from_animation_str(Some("spin y speed:2.0"));
        assert!((cfg.speed - 2.0).abs() < 1e-4);

        let cfg = AnimConfig::from_animation_str(Some("spin z speed=1.5"));
        assert!((cfg.speed_z - 1.0).abs() < 1e-4, "per-axis default stays 1.0");
    }
}
