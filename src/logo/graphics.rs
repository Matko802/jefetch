//! Native terminal image protocols for image logos.
//!
//! When the terminal speaks kitty graphics, sixel, or iTerm2 inline
//! images, an image logo prints as a real image. Otherwise —
//! unsupported terminal or piped output — it falls back to the
//! half-block conversion in [`super::image`].
//!
//! Only the one-shot static path uses this: animated output and the
//! live view keep the regular text rendering.

use super::image::{self as logo_image, RawImage};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GraphicsProto {
    Kitty,
    Sixel,
    Iterm2,
}

/// A `/dev/tty` handle with the terminal in noncanonical mode and the
/// fd nonblocking, restored on drop. Mirrors the kitty font query in
/// `detection::terminal`.
struct TtyQuery {
    fd: i32,
    orig_term: libc::termios,
    orig_flags: i32,
    _file: std::fs::File,
}

impl TtyQuery {
    fn open() -> Option<TtyQuery> {
        let file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open("/dev/tty")
            .ok()?;
        use std::os::unix::io::AsRawFd;
        let fd = file.as_raw_fd();
        let mut orig_term: libc::termios = unsafe { std::mem::zeroed() };
        if unsafe { libc::tcgetattr(fd, &mut orig_term) } != 0 {
            return None;
        }
        let orig_flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
        if orig_flags == -1 {
            return None;
        }
        let mut raw = orig_term;
        raw.c_lflag &= !(libc::ICANON | libc::ECHO);
        raw.c_cc[libc::VMIN as usize] = 0;
        raw.c_cc[libc::VTIME as usize] = 0;
        if unsafe { libc::tcsetattr(fd, libc::TCSANOW, &raw) } != 0 {
            return None;
        }
        if unsafe { libc::fcntl(fd, libc::F_SETFL, orig_flags | libc::O_NONBLOCK) } == -1 {
            unsafe { libc::tcsetattr(fd, libc::TCSANOW, &orig_term) };
            return None;
        }
        Some(TtyQuery { fd, orig_term, orig_flags, _file: file })
    }

    fn write_all(&self, mut bytes: &[u8]) -> bool {
        while !bytes.is_empty() {
            let n = unsafe {
                libc::write(
                    self.fd,
                    bytes.as_ptr() as *const libc::c_void,
                    bytes.len() as libc::size_t,
                )
            };
            if n <= 0 {
                let err = std::io::Error::last_os_error();
                if err.kind() == std::io::ErrorKind::WouldBlock {
                    std::thread::sleep(std::time::Duration::from_millis(5));
                    continue;
                }
                return false;
            }
            bytes = &bytes[n as usize..];
        }
        true
    }

    /// Read until `done` matches or `budget_ms` elapses.
    fn read_until(&self, budget_ms: u64, done: impl Fn(&[u8]) -> bool) -> Vec<u8> {
        let mut buf = Vec::new();
        let mut tmp = [0u8; 512];
        let start = std::time::Instant::now();
        loop {
            let n = unsafe {
                libc::read(
                    self.fd,
                    tmp.as_mut_ptr() as *mut libc::c_void,
                    tmp.len() as libc::size_t,
                )
            };
            if n > 0 {
                buf.extend_from_slice(&tmp[..n as usize]);
                if done(&buf) {
                    break;
                }
            }
            if start.elapsed().as_millis() >= budget_ms as u128 {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        buf
    }

    /// Throw away any pending input (late protocol acks).
    fn drain(&self, budget_ms: u64) {
        self.read_until(budget_ms, |_| false);
    }
}

impl Drop for TtyQuery {
    fn drop(&mut self) {
        unsafe {
            libc::fcntl(self.fd, libc::F_SETFL, self.orig_flags);
            libc::tcsetattr(self.fd, libc::TCSANOW, &self.orig_term);
        }
    }
}

const KITTY_PROBE_ID: u32 = 31;

fn kitty_probe() -> Vec<u8> {
    format!(
        "\x1b_Gi={},s=1,v=1,a=q,t=d,f=24,m=0;AAAA\x1b\\",
        KITTY_PROBE_ID
    )
    .into_bytes()
}

pub fn kitty_response_ok(buf: &[u8]) -> bool {
    let text = String::from_utf8_lossy(buf);
    text.contains(&format!("_Gi={}", KITTY_PROBE_ID)) && text.contains("OK")
}

/// Primary device attributes reply ends with `c`; sixel support is
/// parameter `4` (`ESC [ ? 61 ; 4 ; … c`).
fn da_complete(buf: &[u8]) -> bool {
    let text = String::from_utf8_lossy(buf);
    match text.find("\x1b[?") {
        Some(i) => text[i..].contains('c'),
        None => false,
    }
}

pub fn sixel_response_ok(buf: &[u8]) -> bool {
    let text = String::from_utf8_lossy(buf);
    for part in text.split("\x1b[?").skip(1) {
        let body = part.split('c').next().unwrap_or("");
        for p in body.split(';') {
            let digits: String = p.chars().filter(|c| c.is_ascii_digit()).collect();
            if digits == "4" {
                return true;
            }
        }
    }
    false
}

/// Best-effort protocol detection. Env fast paths first (no I/O), then
/// a single round trip carrying both the kitty and sixel probes.
/// Returns `None` without a tty, on timeouts, or when unsupported.
pub fn detect() -> Option<GraphicsProto> {
    if let Ok(tp) = std::env::var("TERM_PROGRAM") {
        if tp == "iTerm.app" || tp == "WezTerm" {
            return Some(GraphicsProto::Iterm2);
        }
        if tp == "ghostty" {
            return Some(GraphicsProto::Kitty);
        }
    }
    if std::env::var_os("KITTY_WINDOW_ID").is_some() {
        return Some(GraphicsProto::Kitty);
    }
    if std::env::var("TERM").map(|t| t == "xterm-kitty").unwrap_or(false) {
        return Some(GraphicsProto::Kitty);
    }
    let tty = TtyQuery::open()?;
    let mut req = kitty_probe();
    req.extend_from_slice(b"\x1b[c");
    if !tty.write_all(&req) {
        return None;
    }
    let buf = tty.read_until(300, |b| kitty_response_ok(b) || da_complete(b));
    if kitty_response_ok(&buf) {
        return Some(GraphicsProto::Kitty);
    }
    if sixel_response_ok(&buf) {
        return Some(GraphicsProto::Sixel);
    }
    None
}

pub fn parse_cpr(buf: &[u8]) -> Option<(u32, u32)> {
    let text = String::from_utf8_lossy(buf);
    let i = text.rfind("\x1b[")?;
    let rest = &text[i + 2..];
    let end = rest.find('R')?;
    let (r, c) = rest[..end].split_once(';')?;
    Some((r.parse().ok()?, c.parse().ok()?))
}

pub fn cursor_pos() -> Option<(u32, u32)> {
    let tty = TtyQuery::open()?;
    if !tty.write_all(b"\x1b[6n") {
        return None;
    }
    let buf = tty.read_until(200, |b| parse_cpr(b).is_some());
    parse_cpr(&buf)
}

/// Cell size in pixels as `(width, height)`, via `CSI 14 t`.
pub fn parse_cell_size(buf: &[u8]) -> Option<(u32, u32)> {
    let text = String::from_utf8_lossy(buf);
    let i = text.rfind("\x1b[4;")?;
    let rest = &text[i + 4..];
    let end = rest.find('t')?;
    let (h, w) = rest[..end].split_once(';')?;
    Some((w.parse().ok()?, h.parse().ok()?))
}

pub fn cell_size() -> Option<(u32, u32)> {
    let tty = TtyQuery::open()?;
    if !tty.write_all(b"\x1b[14t") {
        return None;
    }
    let buf = tty.read_until(200, |b| parse_cell_size(b).is_some());
    parse_cell_size(&buf)
}

fn b64(data: &[u8]) -> String {
    use base64::Engine as _;
    base64::engine::general_purpose::STANDARD.encode(data)
}

// ---------------------------------------------------------------------------
// kitty graphics
// ---------------------------------------------------------------------------

const KITTY_CHUNK: usize = 4096;

/// Chunked transmit. Per the spec, only the first chunk carries the
/// full control set — continuation chunks must have only `m` (and
/// optionally `q`). `q=1` suppresses `OK` chatter (failures still
/// report, and the caller drains them); `q=2` would only suppress
/// failures while `OK`s leak into the shell.
pub fn kitty_transmit_seq(rgba: &[u8], w: u32, h: u32, id: u32) -> String {
    let enc = b64(rgba);
    let bytes = enc.as_bytes();
    let mut out = String::with_capacity(bytes.len() + 128);
    let mut i = 0;
    let mut first = true;
    while i < bytes.len() {
        let j = (i + KITTY_CHUNK).min(bytes.len());
        let last = j == bytes.len();
        if first {
            out.push_str(&format!(
                "\x1b_Ga=t,f=32,s={},v={},i={},m={},q=1;{}",
                w,
                h,
                id,
                if last { 0 } else { 1 },
                &enc[i..j]
            ));
            first = false;
        } else {
            out.push_str(&format!(
                "\x1b_Gm={},q=1;{}",
                if last { 0 } else { 1 },
                &enc[i..j]
            ));
        }
        out.push_str("\x1b\\");
        i = j;
    }
    out
}

pub fn kitty_place_seq(id: u32, cols: u32, rows: u32) -> String {
    format!("\x1b_Ga=p,i={},c={},r={},C=1,q=1\x1b\\", id, cols, rows)
}

// ---------------------------------------------------------------------------
// sixel
// ---------------------------------------------------------------------------

/// Sixel-encode RGBA (`w` × `h`) with a uniform 6×6×6 palette,
/// alpha composited on black.
pub fn sixel_encode(rgba: &[u8], w: usize, h: usize) -> String {
    if w == 0 || h == 0 || rgba.len() < w * h * 4 {
        return String::new();
    }
    let mut regs = vec![0u8; w * h];
    let mut used = [false; 216];
    for (i, px) in rgba.chunks_exact(4).enumerate() {
        let a = px[3] as u32;
        let r6 = px[0] as u32 * a * 5 / (255 * 255);
        let g6 = px[1] as u32 * a * 5 / (255 * 255);
        let b6 = px[2] as u32 * a * 5 / (255 * 255);
        let reg = (r6 * 36 + g6 * 6 + b6) as usize;
        regs[i] = reg as u8;
        used[reg] = true;
    }
    let mut out = String::with_capacity(w * h / 2);
    out.push_str("\x1bPq");
    out.push_str(&format!("\"1;1;{};{}", w, h));
    for (reg, u) in used.iter().enumerate() {
        if !*u {
            continue;
        }
        let (r6, g6, b6) = (reg / 36, (reg % 36) / 6, reg % 6);
        out.push_str(&format!("#{};2;{};{};{}", 16 + reg, r6 * 20, g6 * 20, b6 * 20));
    }
    let bands = h.div_ceil(6);
    for band in 0..bands {
        let y0 = band * 6;
        for (reg, u) in used.iter().enumerate() {
            if !*u {
                continue;
            }
            out.push('#');
            out.push_str(&(16 + reg).to_string());
            let mut x = 0;
            while x < w {
                let mut bits = 0u8;
                for k in 0..6 {
                    let y = y0 + k;
                    if y < h && regs[y * w + x] as usize == reg {
                        bits |= 1 << k;
                    }
                }
                let mut run = 1;
                while x + run < w && run < 255 {
                    let mut next = 0u8;
                    for k in 0..6 {
                        let y = y0 + k;
                        if y < h && regs[y * w + x + run] as usize == reg {
                            next |= 1 << k;
                        }
                    }
                    if next != bits {
                        break;
                    }
                    run += 1;
                }
                let ch = (63 + bits) as char;
                if run > 3 {
                    out.push_str(&format!("!{}{}", run, ch));
                } else {
                    for _ in 0..run {
                        out.push(ch);
                    }
                }
                x += run;
            }
        }
        if band + 1 < bands {
            out.push('-');
        }
    }
    out.push_str("\x1b\\");
    out
}

// ---------------------------------------------------------------------------
// iTerm2 inline images
// ---------------------------------------------------------------------------

pub fn png_bytes(rgba: &[u8], w: u32, h: u32) -> Option<Vec<u8>> {
    use ::image::ImageEncoder;
    let mut buf = Vec::new();
    ::image::codecs::png::PngEncoder::new(&mut buf)
        .write_image(rgba, w, h, ::image::ExtendedColorType::Rgba8)
        .ok()?;
    Some(buf)
}

pub fn iterm2_seq(png: &[u8], name: &str, cols: u32, rows: u32) -> String {
    format!(
        "\x1b]1337;File=name={};size={};width={};height={};preserveAspectRatio=0;inline=1:{}\x07",
        b64(name.as_bytes()),
        png.len(),
        cols,
        rows,
        b64(png)
    )
}

// ---------------------------------------------------------------------------
// layout + display
// ---------------------------------------------------------------------------

fn stdout_is_tty() -> bool {
    unsafe { libc::isatty(libc::STDOUT_FILENO) == 1 }
}

fn write_out(s: &str) -> bool {
    use std::io::Write;
    let stdout = std::io::stdout();
    let mut lock = stdout.lock();
    lock.write_all(s.as_bytes()).is_ok() && lock.flush().is_ok()
}

fn cup(row: u32, col: usize) -> String {
    format!("\x1b[{};{}H", row.max(1), col.max(1))
}

struct Layout {
    start_row: u32,
    image_row: u32,
    image_col: usize,
    text_col: usize,
    term_cols: usize,
    rows: usize,
}

/// Cursor-anchored layout mirroring the block path: text starts at the
/// reported cursor row, the image sits `pad_top` below it, and text
/// starts after `pad_left + logo_cols + gap`.
fn begin_layout(
    logo_cols: usize,
    logo_rows: usize,
    gap: usize,
    pad_left: usize,
    pad_top: usize,
    text_rows: usize,
) -> Option<Layout> {
    let (sr, _sc) = cursor_pos()?;
    let (tc, _) = crate::common::terminal_size();
    let text_col = 1 + pad_left + logo_cols + gap;
    if text_col > tc {
        return None;
    }
    Some(Layout {
        start_row: sr,
        image_row: sr + pad_top as u32,
        image_col: 1 + pad_left,
        text_col,
        term_cols: tc,
        rows: (pad_top + logo_rows).max(text_rows).max(1),
    })
}

fn push_row(out: &mut String, lay: &Layout, i: usize, text: &str) {
    out.push_str(&cup(lay.start_row + i as u32, lay.text_col));
    let avail = lay.term_cols.saturating_sub(lay.text_col) + 1;
    if avail > 0 {
        out.push_str(&crate::print::format::truncate_visible(text, avail));
    }
    out.push_str("\r\n");
}

fn image_id() -> u32 {
    10000 + std::process::id() % 50000
}

/// Transmit resolution for cell-scaled protocols (kitty, iTerm2):
/// sharp enough to upscale cleanly, small enough to stay fast.
fn hires_dims(cols: usize, rows: usize) -> (usize, usize) {
    let tw = (cols * 8).clamp(1, 800);
    let th = (tw * rows * 2 / cols.max(1)).clamp(1, 800);
    (tw, th)
}

fn display_kitty(raw: &RawImage, spec: &NativeSpec) -> bool {
    let lay = match begin_layout(
        spec.cols,
        spec.rows,
        spec.gap,
        spec.pad_left,
        spec.pad_top,
        spec.text.len(),
    ) {
        Some(l) => l,
        None => return false,
    };
    let (tw, th) = hires_dims(spec.cols, spec.rows);
    let px = logo_image::resize_box(&raw.rgba, raw.width, raw.height, tw, th);
    let id = image_id();
    // Transmit first on its own: with `q=1` success is silent, and any
    // failure text is swallowed by the drain below.
    if !write_out(&kitty_transmit_seq(&px, tw as u32, th as u32, id)) {
        return false;
    }
    let mut out = String::with_capacity(4096);
    out.push_str(&cup(lay.image_row, lay.image_col));
    out.push_str(&kitty_place_seq(id, spec.cols as u32, spec.rows as u32));
    for i in 0..lay.rows {
        push_row(
            &mut out,
            &lay,
            i,
            spec.text.get(i).map(|s| s.as_str()).unwrap_or(""),
        );
    }
    if !write_out(&out) {
        return false;
    }
    // Swallow any failure text so it never leaks into the shell.
    if let Some(tty) = TtyQuery::open() {
        tty.drain(50);
    }
    true
}

fn display_sixel(raw: &RawImage, spec: &NativeSpec) -> bool {
    let (cell_w, cell_h) = match cell_size() {
        Some((w, h)) if w > 0 && h > 0 => (w as usize, h as usize),
        _ => return false,
    };
    let mut pw = spec.cols * cell_w;
    let mut ph = spec.rows * cell_h;
    if pw > 1000 {
        let s = 1000.0 / pw as f64;
        pw = 1000;
        ph = ((ph as f64 * s) as usize).max(1);
    }
    let px = logo_image::resize_box(&raw.rgba, raw.width, raw.height, pw, ph);
    let lay = match begin_layout(
        spec.cols,
        spec.rows,
        spec.gap,
        spec.pad_left,
        spec.pad_top,
        spec.text.len(),
    ) {
        Some(l) => l,
        None => return false,
    };
    let mut out = String::with_capacity(pw * ph / 4);
    out.push_str(&sixel_encode(&px, pw, ph));
    for i in 0..lay.rows {
        push_row(
            &mut out,
            &lay,
            i,
            spec.text.get(i).map(|s| s.as_str()).unwrap_or(""),
        );
    }
    write_out(&out)
}

fn display_iterm2(raw: &RawImage, spec: &NativeSpec) -> bool {
    let (tw, th) = hires_dims(spec.cols, spec.rows);
    let px = logo_image::resize_box(&raw.rgba, raw.width, raw.height, tw, th);
    let png = match png_bytes(&px, tw as u32, th as u32) {
        Some(p) => p,
        None => return false,
    };
    let lay = match begin_layout(
        spec.cols,
        spec.rows,
        spec.gap,
        spec.pad_left,
        spec.pad_top,
        spec.text.len(),
    ) {
        Some(l) => l,
        None => return false,
    };
    let mut out = String::with_capacity(png.len() * 2);
    out.push_str(&iterm2_seq(&png, &spec.path, spec.cols as u32, spec.rows as u32));
    for i in 0..lay.rows {
        push_row(
            &mut out,
            &lay,
            i,
            spec.text.get(i).map(|s| s.as_str()).unwrap_or(""),
        );
    }
    write_out(&out)
}
pub struct NativeSpec {
    pub path: String,
    pub cols: usize,
    pub rows: usize,
    pub gap: usize,
    pub pad_left: usize,
    pub pad_top: usize,
    pub text: Vec<String>,
}

/// Print an image logo as a real image when the terminal supports it.
/// Returns `false` on any failure so the caller falls back to
/// half-block / ascii conversion.
pub fn display_native(spec: &NativeSpec) -> bool {
    if !stdout_is_tty() {
        return false;
    }
    let proto = match detect() {
        Some(p) => p,
        None => return false,
    };
    let raw = match logo_image::load(&spec.path) {
        Ok(r) => r,
        Err(_) => return false,
    };
    match proto {
        GraphicsProto::Kitty => display_kitty(&raw, spec),
        GraphicsProto::Sixel => display_sixel(&raw, spec),
        GraphicsProto::Iterm2 => display_iterm2(&raw, spec),
    }
}


    #[test]
    fn kitty_ok_parses() {
        let ok = format!("\x1b_Gi={},OK\x1b\\", KITTY_PROBE_ID);
        assert!(kitty_response_ok(ok.as_bytes()));
        assert!(!kitty_response_ok(b"\x1b_Gi=31;Eno images\x1b\\"));
        assert!(!kitty_response_ok(b"garbage"));
        assert!(!kitty_response_ok(format!("\x1b_Gi={},OK\x1b\\", KITTY_PROBE_ID + 1).as_bytes()));
    }

    #[test]
    fn sixel_da_parses() {
        assert!(sixel_response_ok(b"\x1b[?62;4;22c"));
        assert!(sixel_response_ok(b"\x1b[?61;4;6;7;14;101;102c"));
        assert!(!sixel_response_ok(b"\x1b[?62c"));
        assert!(!sixel_response_ok(b"garbage"));
        // 14 without 4 must not match.
        assert!(!sixel_response_ok(b"\x1b[?62;14c"));
    }

    #[test]
    fn cpr_parses() {
        assert_eq!(parse_cpr(b"\x1b[12;40R"), Some((12, 40)));
        assert_eq!(parse_cpr(b"noise\x1b[1;1R"), Some((1, 1)));
        assert_eq!(parse_cpr(b"garbage"), None);
    }

    #[test]
    fn cell_size_parses() {
        assert_eq!(parse_cell_size(b"\x1b[4;32;16t"), Some((16, 32)));
        assert_eq!(parse_cell_size(b"garbage"), None);
    }

    #[test]
    fn kitty_transmit_chunks_and_reassembles() {
        // 3200 bytes -> 4268 b64 chars -> 2 chunks.
        let rgba = vec![128u8; 3200];
        let seq = kitty_transmit_seq(&rgba, 40, 20, 777);
        assert_eq!(seq.matches("m=1").count(), 1);
        assert_eq!(seq.matches("m=0").count(), 1);
        // Full control set only on the first chunk (spec discipline).
        assert_eq!(seq.matches("a=t").count(), 1);
        assert_eq!(seq.matches("f=32").count(), 1);
        assert_eq!(seq.matches("i=777").count(), 1);
        assert!(seq.contains("s=40") && seq.contains("v=20"));
        assert!(seq.contains("q=1"));
        assert!(!seq.contains("q=2"), "q=2 hides failures but leaks OKs");
        // Payload reassembles to the input.
        let mut payload = String::new();
        for part in seq.split("\x1b_G") {
            if let Some(semi) = part.find(';') {
                let rest = &part[semi + 1..];
                if let Some(end) = rest.find("\x1b\\") {
                    payload.push_str(&rest[..end]);
                }
            }
        }
        use base64::Engine as _;
        assert_eq!(
            base64::engine::general_purpose::STANDARD.decode(&payload).unwrap(),
            rgba
        );
    }

    #[test]
    fn kitty_single_chunk_carries_full_keys() {
        let rgba = vec![1u8; 64];
        let seq = kitty_transmit_seq(&rgba, 4, 4, 5);
        assert_eq!(seq.matches("\x1b_G").count(), 1);
        assert!(seq.contains("a=t") && seq.contains("m=0") && seq.contains("i=5"));
    }

    #[test]
    fn kitty_place_pins_cells() {
        let s = kitty_place_seq(7, 48, 24);
        assert!(s.contains("a=p") && s.contains("i=7"));
        assert!(s.contains("c=48") && s.contains("r=24"));
        assert!(s.contains("C=1"));
    }

    #[test]
    fn sixel_encodes_regs_and_rle() {
        // 4x1 solid red -> single reg, RLE run.
        let px = vec![255u8, 0, 0, 255].repeat(4);
        let s = sixel_encode(&px, 4, 1);
        assert!(s.starts_with("\x1bPq"), "got {:?}", &s[..8.min(s.len())]);
        assert!(s.ends_with("\x1b\\"));
        assert!(s.contains("#196;2;100;0;0"), "red reg, got {:?}", s);
        assert!(s.contains("!4@"), "RLE run of 4, got {:?}", s);
    }

    #[test]
    fn sixel_composites_transparency_on_black() {
        let px = vec![0u8, 0, 0, 0];
        let s = sixel_encode(&px, 1, 1);
        assert!(s.contains("#16;2;0;0;0"), "black reg, got {:?}", s);
    }

    #[test]
    fn sixel_rejects_empty() {
        assert!(sixel_encode(&[], 0, 0).is_empty());
    }

    #[test]
    fn png_round_trips_magic() {
        let px = vec![1u8, 2, 3, 255].repeat(4);
        let png = png_bytes(&px, 2, 2).expect("encodes");
        assert_eq!(&png[..8], b"\x89PNG\r\n\x1a\n");
    }

    #[test]
    fn iterm2_sequence_shape() {
        let png = vec![0u8; 16];
        let s = iterm2_seq(&png, "logo.png", 48, 24);
        assert!(s.starts_with("\x1b]1337;File="));
        assert!(s.contains("inline=1") && s.contains("width=48") && s.contains("height=24"));
        assert!(s.ends_with("\x07"));
        use base64::Engine as _;
        let body = s.split(':').last().unwrap().trim_end_matches('\x07');
        assert_eq!(
            base64::engine::general_purpose::STANDARD.decode(body).unwrap(),
            png
        );
    }

    #[test]
    fn env_fast_paths_skip_queries() {
        let _g = crate::sharkvis::test_env_lock();
        let prev_tp = std::env::var_os("TERM_PROGRAM");
        let prev_kw = std::env::var_os("KITTY_WINDOW_ID");
        let prev_term = std::env::var_os("TERM");
        std::env::remove_var("KITTY_WINDOW_ID");
        std::env::set_var("TERM", "xterm-256color");
        std::env::set_var("TERM_PROGRAM", "iTerm.app");
        assert_eq!(detect(), Some(GraphicsProto::Iterm2));
        std::env::set_var("TERM_PROGRAM", "WezTerm");
        assert_eq!(detect(), Some(GraphicsProto::Iterm2));
        std::env::set_var("TERM_PROGRAM", "ghostty");
        assert_eq!(detect(), Some(GraphicsProto::Kitty));
        std::env::remove_var("TERM_PROGRAM");
        std::env::set_var("KITTY_WINDOW_ID", "3");
        assert_eq!(detect(), Some(GraphicsProto::Kitty));
        std::env::remove_var("KITTY_WINDOW_ID");
        std::env::set_var("TERM", "xterm-kitty");
        assert_eq!(detect(), Some(GraphicsProto::Kitty));
        if let Some(v) = prev_tp {
            std::env::set_var("TERM_PROGRAM", v);
        } else {
            std::env::remove_var("TERM_PROGRAM");
        }
        if let Some(v) = prev_kw {
            std::env::set_var("KITTY_WINDOW_ID", v);
        } else {
            std::env::remove_var("KITTY_WINDOW_ID");
        }
        if let Some(v) = prev_term {
            std::env::set_var("TERM", v);
        } else {
            std::env::remove_var("TERM");
        }
    }
}
