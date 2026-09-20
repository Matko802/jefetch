
use crate::app::ResolvedLogo;

pub const ALPHA_CUT: u8 = 128;

pub const DEFAULT_COLS: usize = 48;
pub const MAX_COLS: usize = 128;
pub const MAX_ROWS: usize = 64;

pub struct RawImage {
    pub width: usize,
    pub height: usize,
    pub rgba: Vec<u8>,
}

pub struct LogoImage {
    pub cols: usize,
    pub rows: usize,
    pub rgba: Vec<u8>,
}

pub fn expand_tilde(path: &str) -> String {
    if let Some(rest) = path.strip_prefix("~/") {
        if let Some(home) = std::env::var_os("HOME") {
            return format!("{}/{}", home.to_string_lossy(), rest);
        }
    }
    path.to_string()
}

pub fn looks_like_image(path: &str) -> bool {
    let lower = path.to_ascii_lowercase();
    let base = lower.rsplit('/').next().unwrap_or(&lower);
    let ext = base.rsplit('.').next().unwrap_or("");
    matches!(ext, "png" | "jpg" | "jpeg" | "gif" | "bmp")
}

pub fn load(path: &str) -> Result<RawImage, String> {
    let expanded = expand_tilde(path);
    let reader = image::ImageReader::open(&expanded)
        .map_err(|e| format!("cannot open image '{}': {}", path, e))?;
    let reader = reader
        .with_guessed_format()
        .map_err(|e| format!("cannot sniff image '{}': {}", path, e))?;
    let img = reader
        .decode()
        .map_err(|e| format!("cannot decode image '{}': {}", path, e))?;
    let rgba = img.to_rgba8();
    let (w, h) = (rgba.width() as usize, rgba.height() as usize);
    if w == 0 || h == 0 {
        return Err(format!("image '{}' is empty", path));
    }
    Ok(RawImage {
        width: w,
        height: h,
        rgba: rgba.into_raw(),
    })
}

pub fn target_cells(
    src_w: usize,
    src_h: usize,
    cfg_w: Option<u32>,
    cfg_h: Option<u32>,
) -> (usize, usize) {
    let aspect = src_w.max(1) as f64 / src_h.max(1) as f64;
    let (mut cols, mut rows) = match (cfg_w, cfg_h) {
        (Some(w), Some(h)) => (w as usize, h as usize),
        (Some(w), None) => {
            let c = w as usize;
            let r = ((c as f64 / aspect / 2.0).round() as usize).max(1);
            (c, r)
        }
        (None, Some(h)) => {
            let r = h as usize;
            let c = ((r as f64 * aspect * 2.0).round() as usize).max(1);
            (c, r)
        }
        (None, None) => {
            let c = src_w.min(DEFAULT_COLS).max(1);
            let r = ((c as f64 / aspect / 2.0).round() as usize).max(1);
            (c, r)
        }
    };
    cols = cols.clamp(1, MAX_COLS);
    rows = rows.clamp(1, MAX_ROWS);
    (cols, rows)
}

pub fn resize_box(
    src: &[u8],
    src_w: usize,
    src_h: usize,
    dst_w: usize,
    dst_h: usize,
) -> Vec<u8> {
    let mut out = vec![0u8; dst_w * dst_h * 4];
    if src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0 {
        return out;
    }
    let x_scale = src_w as f64 / dst_w as f64;
    let y_scale = src_h as f64 / dst_h as f64;
    for dy in 0..dst_h {
        let y0 = dy as f64 * y_scale;
        let y1 = (y0 + y_scale).min(src_h as f64);
        for dx in 0..dst_w {
            let x0 = dx as f64 * x_scale;
            let x1 = (x0 + x_scale).min(src_w as f64);
            let mut rs = 0.0;
            let mut gs = 0.0;
            let mut bs = 0.0;
            let mut asum = 0.0;
            let mut area = 0.0;
            let sy0 = y0 as usize;
            let sy1 = (y1.ceil() as usize).min(src_h);
            let sx0 = x0 as usize;
            let sx1 = (x1.ceil() as usize).min(src_w);
            for sy in sy0..sy1 {
                let oy0 = y0.max(sy as f64);
                let oy1 = y1.min(sy as f64 + 1.0);
                if oy1 <= oy0 {
                    continue;
                }
                for sx in sx0..sx1 {
                    let ox0 = x0.max(sx as f64);
                    let ox1 = x1.min(sx as f64 + 1.0);
                    if ox1 <= ox0 {
                        continue;
                    }
                    let w = (ox1 - ox0) * (oy1 - oy0);
                    let p = (sy * src_w + sx) * 4;
                    let a = src[p + 3] as f64 / 255.0;
                    rs += src[p] as f64 * a * w;
                    gs += src[p + 1] as f64 * a * w;
                    bs += src[p + 2] as f64 * a * w;
                    asum += a * w;
                    area += w;
                }
            }
            let o = (dy * dst_w + dx) * 4;
            if asum > 0.0 {
                out[o] = (rs / asum).round().clamp(0.0, 255.0) as u8;
                out[o + 1] = (gs / asum).round().clamp(0.0, 255.0) as u8;
                out[o + 2] = (bs / asum).round().clamp(0.0, 255.0) as u8;
                out[o + 3] = ((asum / area.max(1e-9)) * 255.0).round().clamp(0.0, 255.0) as u8;
            }
        }
    }
    out
}

impl LogoImage {
    pub fn load(path: &str, cfg_w: Option<u32>, cfg_h: Option<u32>) -> Result<LogoImage, String> {
        let raw = load(path)?;
        Ok(Self::from_raw(&raw, cfg_w, cfg_h))
    }

    pub fn from_raw(raw: &RawImage, cfg_w: Option<u32>, cfg_h: Option<u32>) -> LogoImage {
        let (cols, rows) = target_cells(raw.width, raw.height, cfg_w, cfg_h);
        let rgba = resize_box(&raw.rgba, raw.width, raw.height, cols, rows * 2);
        LogoImage { cols, rows, rgba }
    }

    fn pixel(&self, x: usize, y: usize) -> (u8, u8, u8, u8) {
        let p = (y * self.cols + x) * 4;
        (self.rgba[p], self.rgba[p + 1], self.rgba[p + 2], self.rgba[p + 3])
    }

    pub fn to_resolved(&self, padding_right: usize) -> ResolvedLogo {
        let mut lines: Vec<String> = Vec::with_capacity(self.rows);
        let mut width = 0usize;
        for r in 0..self.rows {
            let mut line = String::new();
            let mut last = self.cols;
            while last > 0 {
                let (_, _, _, a0) = self.pixel(last - 1, r * 2);
                let (_, _, _, a1) = self.pixel(last - 1, r * 2 + 1);
                if a0 > ALPHA_CUT || a1 > ALPHA_CUT {
                    break;
                }
                last -= 1;
            }
            for c in 0..last {
                let (r0, g0, b0, a0) = self.pixel(c, r * 2);
                let (r1, g1, b1, a1) = self.pixel(c, r * 2 + 1);
                let top = a0 > ALPHA_CUT;
                let bot = a1 > ALPHA_CUT;
                match (top, bot) {
                    (false, false) => line.push(' '),
                    (true, false) => {
                        line.push_str(&format!("\x1b[38;2;{};{};{}m▀", r0, g0, b0));
                    }
                    (false, true) => {
                        line.push_str(&format!("\x1b[38;2;{};{};{}m▄", r1, g1, b1));
                    }
                    (true, true) => {
                        line.push_str(&format!(
                            "\x1b[38;2;{};{};{}m\x1b[48;2;{};{};{}m▀",
                            r0, g0, b0, r1, g1, b1
                        ));
                    }
                }
            }
            if last > 0 {
                line.push_str("\x1b[0m");
            }
            width = width.max(last);
            lines.push(line);
        }
        ResolvedLogo {
            lines,
            colors: vec![String::new(); self.rows],
            width,
            padding_right,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn bmp_bytes(w: u32, h: u32, pixels: &[(u8, u8, u8)]) -> Vec<u8> {
        let row_stride = ((w * 3 + 3) / 4) * 4;
        let data_len = row_stride * h;
        let mut v = Vec::with_capacity(54 + data_len as usize);
        v.extend_from_slice(b"BM");
        v.extend_from_slice(&(54 + data_len).to_le_bytes());
        v.extend_from_slice(&[0u8; 4]);
        v.extend_from_slice(&54u32.to_le_bytes());
        v.extend_from_slice(&40u32.to_le_bytes());
        v.extend_from_slice(&w.to_le_bytes());
        v.extend_from_slice(&h.to_le_bytes());
        v.extend_from_slice(&1u16.to_le_bytes());
        v.extend_from_slice(&24u16.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(&data_len.to_le_bytes());
        v.extend_from_slice(&[0u8; 16]);
        for row in (0..h).rev() {
            for col in 0..w {
                let (r, g, b) = pixels[(row * w + col) as usize];
                v.push(b);
                v.push(g);
                v.push(r);
            }
            while (v.len() - 54) % 4 != 0 {
                v.push(0);
            }
        }
        v
    }

    fn tmp_path(tag: &str) -> String {
        format!(
            "{}/jefetch-imgtest-{}-{}.bmp",
            std::env::temp_dir().to_string_lossy(),
            std::process::id(),
            tag
        )
    }

    fn write_tmp(tag: &str, data: &[u8]) -> String {
        let p = tmp_path(tag);
        std::fs::write(&p, data).unwrap();
        p
    }

    #[test]
    fn loads_bmp_pixels() {
        let px = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255)];
        let p = write_tmp("load", &bmp_bytes(2, 2, &px));
        let raw = load(&p).unwrap();
        let _ = std::fs::remove_file(&p);
        assert_eq!((raw.width, raw.height), (2, 2));
        assert_eq!(&raw.rgba[0..4], &[255, 0, 0, 255]);
        assert_eq!(&raw.rgba[4..8], &[0, 255, 0, 255]);
        assert_eq!(&raw.rgba[8..12], &[0, 0, 255, 255]);
        assert_eq!(&raw.rgba[12..16], &[255, 255, 255, 255]);
    }

    #[test]
    fn load_rejects_missing_and_garbage() {
        assert!(load("/nonexistent-jefetch-img-xyz.png").is_err());
        let p = write_tmp("garbage", b"this is not an image");
        let r = load(&p);
        let _ = std::fs::remove_file(&p);
        assert!(r.is_err());
    }

    #[test]
    fn target_cells_keeps_terminal_aspect() {
        assert_eq!(target_cells(96, 48, None, None), (48, 12));
        assert_eq!(target_cells(16, 16, None, None), (16, 8));
        assert_eq!(target_cells(100, 100, Some(40), None), (40, 20));
        assert_eq!(target_cells(100, 100, None, Some(10)), (20, 10));
        assert_eq!(target_cells(10, 10, Some(500), Some(500)), (MAX_COLS, MAX_ROWS));
        assert_eq!(target_cells(10, 10, Some(0), Some(0)), (1, 1));
    }

    #[test]
    fn resize_box_averages_solid() {
        let src = vec![200u8, 10, 30, 255].repeat(16);
        let out = resize_box(&src, 4, 4, 2, 2);
        assert_eq!(out.len(), 16);
        for px in out.chunks(4) {
            assert_eq!(px, &[200, 10, 30, 255]);
        }
    }

    #[test]
    fn resize_box_unpremultiplies_alpha() {
        let mut src = Vec::new();
        for _ in 0..2 {
            src.extend_from_slice(&[255, 0, 0, 255, 0, 0, 0, 0]);
        }
        let out = resize_box(&src, 2, 2, 1, 1);
        assert_eq!(out[0..3], [255, 0, 0]);
        assert!((out[3] as i32 - 128).abs() <= 1, "alpha {}", out[3]);
    }

    #[test]
    fn static_render_uses_half_blocks() {
        let img = LogoImage {
            cols: 2,
            rows: 1,
            rgba: vec![
                255, 0, 0, 255, 255, 0, 0, 255,
                0, 0, 255, 255, 0, 0, 255, 255,
            ],
        };
        let logo = img.to_resolved(2);
        assert_eq!(logo.lines.len(), 1);
        assert_eq!(logo.width, 2);
        let line = &logo.lines[0];
        assert!(
            line.contains("\x1b[38;2;255;0;0m\x1b[48;2;0;0;255m▀"),
            "got {:?}",
            line
        );
        assert_eq!(crate::print::format::visible_len(line), 2);
    }

    #[test]
    fn static_render_handles_transparency() {
        let img = LogoImage {
            cols: 2,
            rows: 1,
            rgba: vec![
                0, 0, 0, 0, 255, 0, 0, 255,
                0, 255, 0, 255, 0, 0, 0, 0,
            ],
        };
        let logo = img.to_resolved(2);
        let line = &logo.lines[0];
        assert!(line.contains("\x1b[38;2;0;255;0m▄"), "got {:?}", line);
        assert!(line.contains("\x1b[38;2;255;0;0m▀"), "got {:?}", line);
    }

    #[test]
    fn static_render_trims_transparent_tail() {
        let img = LogoImage {
            cols: 3,
            rows: 1,
            rgba: vec![0u8; 3 * 2 * 4],
        };
        let logo = img.to_resolved(2);
        assert_eq!(logo.width, 0);
        assert!(!logo.lines[0].contains("\x1b[38"), "got {:?}", logo.lines[0]);
    }

    #[test]
    fn looks_like_image_matches_basic_formats() {
        assert!(looks_like_image("~/pic.PNG"));
        assert!(looks_like_image("/tmp/a.jpg"));
        assert!(looks_like_image("/tmp/a.jpeg"));
        assert!(looks_like_image("x.gif"));
        assert!(looks_like_image("x.bmp"));
        assert!(!looks_like_image("~/logo.txt"));
        assert!(!looks_like_image("arch"));
    }
}
