use crate::app::ResolvedLogo;

const K2: f32 = 5.5;
const ANIM_WIDTH: i32 = 60;
const GAP: usize = 2;
const MAX_POINTS: usize = 400_000;

const DEFAULT_SHADING: &[&str] = &["░", "▒", "▓", "█"];

#[derive(Debug, Clone)]
pub struct AnimConfig {
    pub spin_x: bool,
    pub spin_y: bool,
    pub spin_z: bool,
    pub speed: f32,

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

    pub flat: bool,

    pub original_glyphs: bool,
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
            flat: false,
            original_glyphs: false,
        }
    }
}

const OPTION_KEYS: &[&str] = &[
    "speed_x", "speed_y", "speed_z", "speed", "size", "depth", "height",
    "style", "mode", "characters", "chars", "glyphs", "glyph", "shading",
    "symbols", "symbol", "ramp",
];

const QUADRANT_GLYPHS: &[&str] = &[
    " ", "▘", "▝", "▀", "▖", "▌", "▞", "▛", "▗", "▚", "▐", "▜", "▄", "▙", "▟", "█",
];

impl AnimConfig {

    fn sub_divs(&self) -> (usize, usize) {
        if self.original_glyphs {
            (1, 1)
        } else {
            (2, 2)
        }
    }

    pub fn from_animation_str(s: Option<&str>) -> Self {
        let mut cfg = Self::default();
        if let Some(raw) = s {
            let low = raw.to_ascii_lowercase();

            let mut flat_opt: Option<bool> = None;
            for key in ["style", "mode"] {
                if let Some(v) = extract_word(&low, raw, key, true) {
                    if let Some(f) = Self::parse_style_value(&v) {
                        flat_opt = Some(f);
                    }
                }
            }
            if flat_opt.is_none() {
                if has_word(&low, "flat") {
                    flat_opt = Some(true);
                } else if has_word(&low, "3d") {
                    flat_opt = Some(false);
                }
            }
            if let Some(f) = flat_opt {
                cfg.flat = f;
            }

            let mut chars_opt: Option<String> = None;
            for key in [
                "characters", "chars", "glyphs", "glyph", "shading", "symbols", "symbol",
                "ramp",
            ] {

                if let Some(v) = extract_word(&low, raw, key, false) {
                    chars_opt = Some(v);
                }
            }
            if let Some(v) = chars_opt {
                cfg.apply_chars_value(&v);
            } else if has_word(&low, "ascii") || has_word(&low, "original") {
                cfg.original_glyphs = true;
            } else if has_word(&low, "blocks") || has_word(&low, "block") {
                cfg.original_glyphs = false;
                cfg.shading = DEFAULT_SHADING.iter().map(|s| s.to_string()).collect();
            }

            let axis_src = blank_option_spans(&low, OPTION_KEYS);
            let has_x = axis_src.contains('x');
            let has_y = axis_src.contains('y');
            let has_z = axis_src.contains('z');

            if low.contains("spin") || has_x || has_y || has_z || low.contains("rotate") {
                if has_x || has_y || has_z {
                    cfg.spin_x = has_x;
                    cfg.spin_y = has_y;
                    cfg.spin_z = has_z;
                } else if low.contains("spin") {

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

    fn parse_style_value(v: &str) -> Option<bool> {
        let t = v.to_ascii_lowercase();
        let t = t.trim();
        if t.contains("flat") || t == "2d" || t.contains("plain") {
            return Some(true);
        }
        if t.contains("3d") || t.contains("three") || t.contains("depth") {
            return Some(false);
        }
        None
    }

    fn apply_chars_value(&mut self, v: &str) {
        let t = v.trim();
        if t.is_empty() {
            return;
        }
        let l = t.to_ascii_lowercase();
        if l == "ascii"
            || l == "original"
            || l == "keep"
            || l == "logo"
            || l == "same"
            || l.contains("original")
            || l.contains("ascii")
        {
            self.original_glyphs = true;
            return;
        }
        if l == "blocks"
            || l == "block"
            || l == "solid"
            || l == "default"
            || l == "shaded"
        {
            self.original_glyphs = false;
            self.shading = DEFAULT_SHADING.iter().map(|s| s.to_string()).collect();
            return;
        }

        let ramp: Vec<String> = t.chars().map(|c| c.to_string()).collect();
        if !ramp.is_empty() {
            self.original_glyphs = false;
            self.shading = ramp;
        }
    }

    pub fn apply_logo_overrides(&mut self, logo: &crate::config::configfile::LogoConfig) {
        if let Some(s) = &logo.style {
            if let Some(f) = Self::parse_style_value(s) {
                self.flat = f;
            }
        }
        if let Some(c) = &logo.chars {
            self.apply_chars_value(c);
        }
    }
}

fn is_word_char(c: char) -> bool {
    c.is_ascii_alphanumeric() || c == '_'
}

fn find_key(s: &str, key: &str) -> Option<usize> {
    let mut from = 0;
    while from + key.len() <= s.len() {
        let rel = s[from..].find(key)?;
        let pos = from + rel;
        let prev_ok = pos == 0 || !s[..pos].chars().rev().next().is_some_and(is_word_char);
        let after = pos + key.len();
        let next_ok =
            after >= s.len() || !s[after..].chars().next().is_some_and(is_word_char);
        if prev_ok && next_ok {
            return Some(pos);
        }
        from = pos + 1;
    }
    None
}

fn has_word(s: &str, word: &str) -> bool {
    find_key(s, word).is_some()
}

fn extract_word(low: &str, raw: &str, key: &str, stop_comma: bool) -> Option<String> {
    let pos = find_key(low, key)? + key.len();
    let low_rest = &low[pos..];

    let mut off = 0;
    for (i, c) in low_rest.char_indices() {
        if c == '=' || c == ':' || c == ' ' || c == '\t' || c == ',' {
            off = i + c.len_utf8();
        } else {
            break;
        }
    }
    let raw_rest = raw.get(pos + off..)?;
    let low_rest = &low_rest[off..];
    if raw_rest.is_empty() {
        return None;
    }
    let first = raw_rest.chars().next().unwrap();
    if first == '"' || first == '\'' {
        let q = first;
        let body = &raw_rest[q.len_utf8()..];
        let end = body.find(q)?;
        return Some(body[..end].to_string());
    }
    let mut end = 0;
    for (i, c) in low_rest.char_indices() {
        if c == ' ' || c == '\t' || c == '\n' || c == '\r' || (stop_comma && c == ',') {
            break;
        }
        end = i + c.len_utf8();
    }
    if end == 0 {
        return None;
    }

    Some(raw_rest[..end].to_string())
}

fn extract_number(s: &str, key: &str) -> Option<f32> {
    let start = find_key(s, key)? + key.len();
    let rest = &s[start..];

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

fn blank_option_spans(s: &str, keys: &[&str]) -> String {
    let mut buf: Vec<char> = s.chars().collect();
    for key in keys {
        let k: Vec<char> = key.chars().collect();
        if k.is_empty() {
            continue;
        }
        let mut i = 0;
        while i + k.len() <= buf.len() {
            if buf[i..i + k.len()] != k[..] {
                i += 1;
                continue;
            }
            let prev_ok = i == 0 || !is_word_char(buf[i - 1]);
            let after = i + k.len();
            let next_ok = after >= buf.len() || !is_word_char(buf[after]);
            if !(prev_ok && next_ok) {
                i += 1;
                continue;
            }
            for j in i..after {
                buf[j] = ' ';
            }
            let mut j = after;
            while j < buf.len()
                && (buf[j] == '=' || buf[j] == ':' || buf[j] == ' ' || buf[j] == '\t' || buf[j] == ',')
            {
                buf[j] = ' ';
                j += 1;
            }
            if j < buf.len() && (buf[j] == '"' || buf[j] == '\'') {
                let q = buf[j];
                buf[j] = ' ';
                j += 1;
                while j < buf.len() && buf[j] != q {
                    buf[j] = ' ';
                    j += 1;
                }
                if j < buf.len() {
                    buf[j] = ' ';
                    j += 1;
                }
            } else {

                while j < buf.len()
                    && buf[j] != ' '
                    && buf[j] != '\t'
                    && buf[j] != '\n'
                    && buf[j] != '\r'
                {
                    buf[j] = ' ';
                    j += 1;
                }
            }
            i = j;
        }
    }
    buf.into_iter().collect()
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
            0x88 => return 1.00,
            0x93 => return 0.75,
            0x92 => return 0.50,
            0x91 => return 0.25,
            0x80 => return 0.50,
            0x84 => return 0.50,
            0x8c => return 0.50,
            0x90 => return 0.50,
            0x82 => return 0.55,
            0x81 => return 0.30,
            _ => return 0.50,
        }
    }
    if bytes[0] == 0xe2 && (bytes[1] == 0x94 || bytes[1] == 0x95) {
        return 0.20;
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

    glyph: char,
}

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

    if !has_ansi {
        for c in &logo.colors {
            if !c.is_empty() {

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

    if has_ansi {

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

    let (sbr, _) = config.sub_divs();
    let mut subdiv = if config.original_glyphs {
        1
    } else {
        (config.size * sbr as f32) as usize
    };
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
            let glyph_ch = cells[row][col].0.chars().next().unwrap_or(' ');
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

                    if config.flat {
                        if points.len() >= MAX_POINTS {
                            break;
                        }
                        let col_val = if has_ansi { cells[row][col].1 } else { 1 };
                        points.push(Point {
                            x: ox,
                            y: oy,
                            z: 0.0,
                            nx: 0.0,
                            ny: 0.0,
                            nz: 1.0,
                            color: col_val,
                            glyph: glyph_ch,
                        });
                        continue;
                    }

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
                            glyph: glyph_ch,
                        });
                    }
                }
            }
        }
    }
    points
}

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

    let render_height = render_height.max(1);
    let logo_height = render_height.min((ANIM_WIDTH * 3 / 5) as usize).max(1);
    let k1 = 37.0 * logo_height as f32 / 36.0;
    let half_aw = ANIM_WIDTH as f32 * 0.5;
    let w = ANIM_WIDTH as usize;
    let h = render_height;

    let (sub_rows, sub_cols) = config.sub_divs();
    let sw = w * sub_cols;
    let sh = h * sub_rows;
    let mut zbuf = vec![0.0f32; sh * sw];
    let mut lumbuf = vec![0.0f32; sh * sw];
    let mut colorbuf = vec![0i32; sh * sw];
    let mut glyphbuf = vec![' '; sh * sw];

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
        let xs = ((half_aw + k1x2 * x3 * ooz) * sub_cols as f32) as i32;
        let ys = ((y_center - k1 * y3 * ooz) * sub_rows as f32) as i32;
        if xs < 0 || xs >= sw as i32 || ys < 0 || ys >= sh as i32 {
            continue;
        }
        let idx = ys as usize * sw + xs as usize;
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
            glyphbuf[idx] = p.glyph;
        }
    }

    let scount = config.shading.len();
    let smax = scount.saturating_sub(1);
    let total_sub = sub_rows * sub_cols;
    let full_mask = (1u32 << total_sub) - 1;

    fn push_color(line: &mut String, has_ansi: bool, c: i32, prev_color: &mut i32) {
        if c != *prev_color {
            if *prev_color != -2 && *prev_color != -1 {
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
            *prev_color = c;
        }
    }
    let mut lines: Vec<String> = Vec::with_capacity(h);
    for row in 0..h {
        let mut line = String::new();
        let mut prev_color: i32 = -2;
        for col in 0..w {
            if config.original_glyphs {

                let idx = row * sw + col;
                if zbuf[idx] <= 0.0 {
                    if prev_color != -2 && prev_color != -1 {
                        line.push_str("\x1b[0m");
                        prev_color = -1;
                    }
                    line.push(' ');
                    continue;
                }
                push_color(&mut line, has_ansi, colorbuf[idx], &mut prev_color);
                line.push(glyphbuf[idx]);
                continue;
            }
            let mut mask = 0u32;
            let mut bit = 0u32;
            let mut n = 0usize;
            let mut lsum = 0.0f32;
            let mut best = 0.0f32;
            let mut best_c = 0i32;
            for sr in 0..sub_rows {
                for sc in 0..sub_cols {
                    let idx = (row * sub_rows + sr) * sw + (col * sub_cols + sc);
                    let z = zbuf[idx];
                    if z > 0.0 {
                        mask |= 1 << bit;
                        lsum += lumbuf[idx];
                        n += 1;
                        if z > best {
                            best = z;
                            best_c = colorbuf[idx];
                        }
                    }
                    bit += 1;
                }
            }
            if n == 0 {
                if prev_color != -2 && prev_color != -1 {
                    line.push_str("\x1b[0m");
                    prev_color = -1;
                }
                line.push(' ');
                continue;
            }
            let coverage = n as f32 / total_sub as f32;
            let ink = lsum / n as f32 * coverage;

            let mut ci = (ink * smax as f32 + 0.5) as usize;
            if ci > smax {
                ci = smax;
            }

            let glyph: &str = if mask != full_mask
                && (coverage - ink).abs() <= ((ci as f32 + 1.0) / scount as f32 - ink).abs()
            {
                QUADRANT_GLYPHS[mask as usize]
            } else {
                &config.shading[ci]
            };
            push_color(&mut line, has_ansi, best_c, &mut prev_color);
            line.push_str(glyph);
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
    use crate::app::ResolvedLogo;

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

    #[test]
    fn option_words_do_not_fake_axes() {

        let cfg = AnimConfig::from_animation_str(Some("spin y size=2"));
        assert!(cfg.spin_y && !cfg.spin_x && !cfg.spin_z, "{:?}", cfg);

        let cfg = AnimConfig::from_animation_str(Some("spin y style=flat"));
        assert!(cfg.spin_y && !cfg.spin_x && !cfg.spin_z, "{:?}", cfg);
        assert!(cfg.flat);

        let cfg = AnimConfig::from_animation_str(Some("spin z speed_x=0.5"));
        assert!(cfg.spin_z && !cfg.spin_x && !cfg.spin_y, "{:?}", cfg);
        assert!((cfg.speed_x - 0.5).abs() < 1e-4);
    }

    #[test]
    fn generic_speed_not_clobbered_by_per_axis() {

        let cfg = AnimConfig::from_animation_str(Some("spin xyz speed_y=-1"));
        assert!((cfg.speed - 2.0).abs() < 1e-4, "speed={}", cfg.speed);
        assert!((cfg.speed_y - (-1.0)).abs() < 1e-4);
    }

    #[test]
    fn style_and_chars_parse() {
        let cfg = AnimConfig::from_animation_str(Some("spin z speed=1.5 flat"));
        assert!(cfg.flat);
        assert!(cfg.spin_z && !cfg.spin_x && !cfg.spin_y);

        let cfg = AnimConfig::from_animation_str(Some("spin z style=3d"));
        assert!(!cfg.flat);

        let cfg = AnimConfig::from_animation_str(Some("spin z speed=1.5 chars=ascii"));
        assert!(cfg.original_glyphs);

        let cfg = AnimConfig::from_animation_str(Some("spin z chars=.,-~:;=!*#$@"));
        assert!(!cfg.original_glyphs);
        assert_eq!(cfg.shading.len(), 12);
        assert_eq!(cfg.shading[0], ".");
        assert_eq!(cfg.shading[11], "@");

        let cfg = AnimConfig::from_animation_str(Some("spin z ascii"));
        assert!(cfg.original_glyphs);

        let cfg = AnimConfig::from_animation_str(Some("spin z speed=1.5"));
        assert!(!cfg.flat && !cfg.original_glyphs);
        assert_eq!(cfg.shading.len(), 4);
    }

    fn test_logo() -> ResolvedLogo {
        ResolvedLogo {
            lines: vec!["AB".to_string(), "CD".to_string()],
            colors: Vec::new(),
            width: 2,
            padding_right: 2,
        }
    }

    fn joined_text(logo: &ResolvedLogo) -> String {
        crate::app::strip_ansi(&logo.lines.join("\n"))
    }

    #[test]
    fn original_glyphs_keep_logo_chars() {
        let mut cfg = AnimConfig::from_animation_str(Some("spin"));
        cfg.spin_x = false;
        cfg.spin_y = false;
        cfg.spin_z = false;
        cfg.original_glyphs = true;
        let out = render_frame(&test_logo(), 0, &cfg, 36, 4);
        let text = joined_text(&out);
        assert!(text.contains('A'), "keeps logo chars, got:\n{}", text);
    }

    #[test]
    fn shading_ramp_draws_blocks_by_default() {
        let mut cfg = AnimConfig::from_animation_str(Some("spin"));
        cfg.spin_x = false;
        cfg.spin_y = false;
        cfg.spin_z = false;
        let out = render_frame(&test_logo(), 0, &cfg, 36, 4);
        let text = joined_text(&out);
        assert!(
            text.contains('█') || text.contains('▓') || text.contains('▒') || text.contains('░'),
            "draws shading blocks, got:\n{}",
            text
        );
        assert!(!text.contains('A'), "no logo chars in block mode");
    }

    fn bbox_hole_ratio(logo: &ResolvedLogo, frame: usize) -> f32 {
        let cfg = AnimConfig::from_animation_str(Some("spin y speed=2.0"));
        let out = render_frame(logo, frame, &cfg, 36, 4);
        let text = joined_text(&out);
        let rows: Vec<Vec<char>> = text.lines().map(|l| l.chars().collect()).collect();
        let mut min_r = usize::MAX;
        let mut max_r = 0usize;
        let mut min_c = usize::MAX;
        let mut max_c = 0usize;
        for (r, row) in rows.iter().enumerate() {
            for (c, ch) in row.iter().enumerate() {
                if *ch != ' ' {
                    min_r = min_r.min(r);
                    max_r = max_r.max(r);
                    min_c = min_c.min(c);
                    max_c = max_c.max(c);
                }
            }
        }
        if max_r <= min_r || max_c <= min_c {
            return 1.0;
        }
        let mut holes = 0usize;
        let mut total = 0usize;
        for r in min_r..=max_r {
            for c in min_c..=max_c {
                total += 1;
                if rows[r][c] == ' ' {
                    holes += 1;
                }
            }
        }
        holes as f32 / total as f32
    }

    fn solid_test_logo() -> ResolvedLogo {

        ResolvedLogo {
            lines: vec!["████████".to_string(); 8],
            colors: Vec::new(),
            width: 8,
            padding_right: 2,
        }
    }

    #[test]
    fn rotated_frames_stay_solid() {
        for frame in [0usize, 5, 13, 27] {
            let ratio = bbox_hole_ratio(&solid_test_logo(), frame);
            assert!(
                ratio < 0.25,
                "frame {} has {:.0}% holes inside logo bbox",
                frame,
                ratio * 100.0
            );
        }
    }

    #[test]
    fn flat_style_renders() {
        let mut cfg = AnimConfig::from_animation_str(Some("spin z flat"));
        cfg.spin_x = false;
        cfg.spin_y = false;
        cfg.spin_z = false;
        let out = render_frame(&test_logo(), 0, &cfg, 36, 4);
        assert_eq!(out.lines.len(), 36);
        let text = joined_text(&out);
        assert!(
            text.trim().len() > 4,
            "flat plane renders something, got:\n{}",
            text
        );
    }
}
