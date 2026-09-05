//! sharkvis integration.
//!
//! When `sharkvis` (the terminal audio visualizer) is running, jefetch can
//! borrow its look and groove:
//!
//! * the spinning logo is tinted with the sharkvis gradient colors
//!   (`gradient_low` .. `gradient_high` from the sharkvis config, lerped by
//!   the current audio energy), and
//! * the spin slows down on the beat (`speed_mult = 1 - depth * beat`).
//!
//! Live data flows through a tiny state file when available, with a
//! lightweight built-in PulseAudio monitor as fallback. Everything here is
//! `std` + `libc` only — no new crates.
//!
//! ## Live state file protocol (written by sharkvis, read by jefetch)
//!
//! Plain `key=value` text, whitespace / newline / `;` / `,` separated,
//! case-insensitive keys. Example:
//!
//! ```text
//! color=#ff8800 energy=0.42 beat=1
//! ```
//!
//! | Key | Meaning |
//! |-----|---------|
//! | `color` | `#rrggbb`, `rrggbb`, `r,g,b` or a basic color name |
//! | `energy` / `level` / `bass` / `volume` | `0..1` audio energy (values `>1` are treated as percent) |
//! | `beat` | `1`/`0`, `true`/`false` or a `0..1` beat envelope |
//!
//! Searched in order: `$XDG_RUNTIME_DIR/sharkvis/state`,
//! `/run/user/$UID/sharkvis/state`, `$TMPDIR/sharkvis-$UID.state`,
//! `/tmp/sharkvis-$UID.state`, `/tmp/sharkvis.state`.
//! Files older than ~1s are treated as stale and ignored.

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::time::{Duration, Instant};

pub type Rgb = (u8, u8, u8);

/// How sharkvis integration behaves. Parsed from the `animation` string
/// (`sharkvis`, `sharkvis=on|off|auto`, `no-sharkvis`) and/or the
/// `logo.sharkvis` config key. Default is `Off`: nothing happens unless
/// the animation string (or logo key) explicitly enables it.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum SharkvisMode {
    /// Enable automatically while a `sharkvis` process is running.
    Auto,
    /// Always try (monitor + state file even without a process match).
    On,
    /// Never integrate.
    #[default]
    Off,
}

impl SharkvisMode {
    pub fn parse_value(v: &str) -> Option<SharkvisMode> {
        match v.trim().to_ascii_lowercase().as_str() {
            "on" | "true" | "1" | "yes" | "enable" | "enabled" => Some(SharkvisMode::On),
            "off" | "false" | "0" | "no" | "disable" | "disabled" => Some(SharkvisMode::Off),
            "auto" => Some(SharkvisMode::Auto),
            _ => None,
        }
    }

    pub fn enabled(self, running: bool) -> bool {
        match self {
            SharkvisMode::Off => false,
            SharkvisMode::On => true,
            SharkvisMode::Auto => running,
        }
    }
}

/// Depth of the beat slowdown dip (multiplied with the volume follow).
pub const DEFAULT_BEAT_DEPTH: f32 = 0.6;
pub const MAX_BEAT_DEPTH: f32 = 0.9;

/// Sensitivity of the music drive. Higher = hotter reaction at low volume.
pub const DEFAULT_SENSE: f32 = 4.0;
pub const MAX_SENSE: f32 = 12.0;

/// Normalized music drive `0..1` from raw energy. Raw monitor energy lives
/// in a small band (often 0.05..0.4), so linear mapping barely moves the
/// logo; saturating exponential normalization uses the full range:
/// quiet still breathes, loud pegs at 1.
pub fn drive(energy: f32, sense: f32) -> f32 {
    let s = sense.clamp(0.5, MAX_SENSE);
    (1.0 - (-energy.clamp(0.0, 1.0) * s).exp()).clamp(0.0, 1.0)
}

/// Speed follows the drive continuously: silence crawls at 0.5x, mid
/// drive spins at 1x, full drive at 1.5x — times the beat dip.
pub fn volume_speed_mult(drive: f32, beat: f32, depth: f32) -> f32 {
    let vol = 0.5 + drive.clamp(0.0, 1.0);
    let dip = 1.0 - depth.clamp(0.0, MAX_BEAT_DEPTH) * beat.clamp(0.0, 1.0);
    (vol * dip).clamp(0.2, 2.0)
}

/// Depth of the beat zoom: `scale = 1 + grow * beat`.
pub const DEFAULT_GROW: f32 = 0.12;
pub const MAX_GROW: f32 = 0.3;

/// Frame produced by [`Sync::poll`].
#[derive(Debug, Clone)]
pub struct LiveFrame {
    pub active: bool,
    /// Vertical logo gradient, bottom → top (sharkvis `gradient_low/high`).
    pub grad: Option<(Rgb, Rgb)>,
    /// Fallback single color (live state color when gradients are unknown).
    pub flat: Option<Rgb>,
    /// sharkvis `[visualizer] glyphs` charset as a shading ramp.
    pub glyphs: Option<Vec<String>>,
    pub energy: f32,
    /// Normalized drive `0..1` (see [`drive`]).
    pub drive: f32,
    pub beat: f32,
    pub speed_mult: f32,
}

impl LiveFrame {
    pub fn inactive() -> LiveFrame {
        LiveFrame {
            active: false,
            grad: None,
            flat: None,
            glyphs: None,
            energy: 0.0,
            drive: 0.0,
            beat: 0.0,
            speed_mult: 1.0,
        }
    }
}

pub fn lerp_rgb(lo: Rgb, hi: Rgb, t: f32) -> Rgb {
    let t = t.clamp(0.0, 1.0);
    let mix = |a: u8, b: u8| (a as f32 + (b as f32 - a as f32) * t + 0.5) as u8;
    (mix(lo.0, hi.0), mix(lo.1, hi.1), mix(lo.2, hi.2))
}

// ---------------------------------------------------------------------------
// process detection
// ---------------------------------------------------------------------------

/// True when a `sharkvis` process is currently running (via `/proc` scan).
/// `JEFETCH_SHARKVIS_RUNNING=1|0` overrides for tests / scripting.
pub fn is_running() -> bool {
    if let Ok(v) = std::env::var("JEFETCH_SHARKVIS_RUNNING") {
        match v.trim().to_ascii_lowercase().as_str() {
            "1" | "true" | "yes" | "on" => return true,
            "0" | "false" | "no" | "off" => return false,
            _ => {}
        }
    }
    scan_proc()
}

fn scan_proc() -> bool {
    let dir = match std::fs::read_dir("/proc") {
        Ok(d) => d,
        Err(_) => return false,
    };
    for entry in dir.flatten() {
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if !name.bytes().all(|b| b.is_ascii_digit()) {
            continue;
        }
        let comm_path = format!("/proc/{}/comm", name);
        if let Ok(comm) = std::fs::read_to_string(&comm_path) {
            if comm.trim() == "sharkvis" {
                return true;
            }
        }
    }
    false
}

// ---------------------------------------------------------------------------
// sharkvis config (gradient colors)
// ---------------------------------------------------------------------------

/// Candidate sharkvis config paths. `JEFETCH_SHARKVIS_CONFIG` wins (tests).
pub fn config_paths() -> Vec<String> {
    let mut out = Vec::new();
    if let Ok(p) = std::env::var("JEFETCH_SHARKVIS_CONFIG") {
        if !p.trim().is_empty() {
            out.push(p);
            return out;
        }
    }
    if let Ok(p) = std::env::var("SHARKVIS_CONFIG") {
        if !p.trim().is_empty() {
            out.push(p);
        }
    }
    if let Ok(home) = std::env::var("HOME") {
        out.push(format!("{}/.config/sharkvis/config", home));
    }
    out.push("./config".to_string());
    out
}

/// `(gradient_low, gradient_high)` from the sharkvis config, if parseable.
pub fn gradient_colors() -> Option<(Rgb, Rgb)> {
    gradient_colors_from_paths(&config_paths())
}

/// sharkvis `[visualizer] glyphs` charset as a shading ramp (dark → bright).
pub fn glyph_ramp() -> Option<Vec<String>> {
    for p in config_paths() {
        if let Ok(text) = std::fs::read_to_string(&p) {
            if let Some(g) = parse_sharkvis_glyphs(&text) {
                return Some(g);
            }
        }
    }
    None
}

/// Gradients + charset from the first config file that provides either.
fn visual_from_paths(paths: &[String]) -> (Option<(Rgb, Rgb)>, Option<Vec<String>>) {
    let mut grad: Option<(Rgb, Rgb)> = None;
    let mut glyphs: Option<Vec<String>> = None;
    for p in paths {
        let Ok(text) = std::fs::read_to_string(p) else {
            continue;
        };
        if grad.is_none() {
            grad = parse_sharkvis_config(&text);
        }
        if glyphs.is_none() {
            glyphs = parse_sharkvis_glyphs(&text);
        }
        if grad.is_some() && glyphs.is_some() {
            break;
        }
    }
    (grad, glyphs)
}

fn gradient_colors_from_paths(paths: &[String]) -> Option<(Rgb, Rgb)> {
    for p in paths {
        if let Ok(text) = std::fs::read_to_string(p) {
            if let Some(g) = parse_sharkvis_config(&text) {
                return Some(g);
            }
        }
    }
    None
}

fn parse_sharkvis_config(text: &str) -> Option<(Rgb, Rgb)> {
    let mut section = String::from("general");
    let mut low: Option<Rgb> = None;
    let mut high: Option<Rgb> = None;
    for raw_line in text.lines() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') {
            if let Some(end) = line.find(']') {
                section = line[1..end].trim().to_ascii_lowercase();
            }
            continue;
        }
        let eq = match line.find('=') {
            Some(i) => i,
            None => continue,
        };
        let key = line[..eq].trim().to_ascii_lowercase();
        let mut val = line[eq + 1..].trim();
        if let Some(semi) = val.find(';') {
            val = val[..semi].trim();
        }
        if section != "color" {
            continue;
        }
        match key.as_str() {
            "gradient_low" => low = parse_color(val),
            "gradient_high" => high = parse_color(val),
            _ => {}
        }
    }
    match (low, high) {
        (Some(l), Some(h)) => Some((l, h)),
        (Some(l), None) => Some((l, l)),
        (None, Some(h)) => Some((h, h)),
        (None, None) => None,
    }
}

/// Parse the `[visualizer] glyphs` value into single-char ramp steps.
/// sharkvis keeps the raw value (no `;` comment stripping); each char —
/// e.g. `1234567` or `▁▂▃▄▅▆▇█` — becomes one shading step.
fn parse_sharkvis_glyphs(text: &str) -> Option<Vec<String>> {
    let mut section = String::from("general");
    for raw_line in text.lines() {
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') {
            if let Some(end) = line.find(']') {
                section = line[1..end].trim().to_ascii_lowercase();
            }
            continue;
        }
        if section != "visualizer" {
            continue;
        }
        let eq = match line.find('=') {
            Some(i) => i,
            None => continue,
        };
        if line[..eq].trim().to_ascii_lowercase() != "glyphs" {
            continue;
        }
        // sharkvis trims the value the same way.
        let ramp: Vec<String> = line[eq + 1..].trim().chars().map(|c| c.to_string()).collect();
        if ramp.is_empty() {
            return None;
        }
        // Whitespace-only values carry no shading info.
        if ramp.iter().all(|s| s.trim().is_empty()) {
            return None;
        }
        return Some(if ramp.len() > 64 { ramp[..64].to_vec() } else { ramp });
    }
    None
}

/// Parse `#rrggbb`, `rrggbb` or a basic color name.
pub fn parse_color(s: &str) -> Option<Rgb> {
    let t = s.trim();
    if t.is_empty() {
        return None;
    }
    // comma separated r,g,b
    if t.contains(',') {
        let parts: Vec<&str> = t.split(',').collect();
        if parts.len() == 3 {
            if let (Ok(r), Ok(g), Ok(b)) = (
                parts[0].trim().parse::<u8>(),
                parts[1].trim().parse::<u8>(),
                parts[2].trim().parse::<u8>(),
            ) {
                return Some((r, g, b));
            }
        }
        return None;
    }
    let hex = t.strip_prefix('#').unwrap_or(t);
    if hex.len() == 6 && hex.bytes().all(|b| b.is_ascii_hexdigit()) {
        if let Ok(v) = u32::from_str_radix(hex, 16) {
            return Some((((v >> 16) & 0xff) as u8, ((v >> 8) & 0xff) as u8, (v & 0xff) as u8));
        }
    }
    named_color(t)
}

fn named_color(name: &str) -> Option<Rgb> {
    Some(match name.to_ascii_lowercase().as_str() {
        "white" => (255, 255, 255),
        "red" => (255, 0, 0),
        "green" => (0, 255, 0),
        "blue" => (0, 0, 255),
        "yellow" => (255, 255, 0),
        "magenta" | "purple" => (255, 0, 255),
        "cyan" => (0, 255, 255),
        "orange" => (255, 136, 0),
        "lime" => (136, 255, 0),
        "teal" => (0, 255, 136),
        "pink" => (255, 0, 136),
        "gray" | "grey" => (136, 136, 136),
        "black" => (0, 0, 0),
        _ => return None,
    })
}

// ---------------------------------------------------------------------------
// live state file
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Copy, Default)]
pub struct LiveState {
    pub color: Option<Rgb>,
    pub energy: Option<f32>,
    pub beat: Option<f32>,
}

/// Fresh live state from sharkvis, if its state file exists and is recent.
pub fn read_live_state() -> Option<LiveState> {
    let stale = stale_after();
    for p in state_paths() {
        let meta = std::fs::metadata(&p).ok()?;
        let fresh = meta
            .modified()
            .ok()
            .and_then(|t| t.elapsed().ok())
            .is_some_and(|age| age <= stale);
        if !fresh {
            continue;
        }
        if let Ok(text) = std::fs::read_to_string(&p) {
            let st = parse_state_text(&text);
            if st.color.is_some() || st.energy.is_some() || st.beat.is_some() {
                return Some(st);
            }
        }
    }
    None
}

fn stale_after() -> Duration {
    if let Ok(v) = std::env::var("JEFETCH_SHARKVIS_STALE_MS") {
        if let Ok(ms) = v.trim().parse::<u64>() {
            return Duration::from_millis(ms.max(1));
        }
    }
    Duration::from_millis(1000)
}

fn state_paths() -> Vec<String> {
    if let Ok(p) = std::env::var("JEFETCH_SHARKVIS_STATE") {
        if !p.trim().is_empty() {
            return vec![p];
        }
    }
    let mut out = Vec::new();
    let uid = unsafe { libc::getuid() };
    if let Ok(rt) = std::env::var("XDG_RUNTIME_DIR") {
        if !rt.is_empty() {
            out.push(format!("{}/sharkvis/state", rt.trim_end_matches('/')));
        }
    }
    out.push(format!("/run/user/{}/sharkvis/state", uid));
    if let Ok(tmp) = std::env::var("TMPDIR") {
        if !tmp.is_empty() {
            out.push(format!("{}/sharkvis-{}.state", tmp.trim_end_matches('/'), uid));
        }
    }
    out.push(format!("/tmp/sharkvis-{}.state", uid));
    out.push("/tmp/sharkvis.state".to_string());
    out
}

/// Parse state file text. Accepts `k=v` / `k:v` pairs separated by
/// whitespace, newlines or `;`; pairs may also be comma-separated
/// (`a=1,b=2`) and colors may be `color=R,G,B`.
pub fn parse_state_text(text: &str) -> LiveState {
    let mut st = LiveState::default();
    let normalized: String = text
        .chars()
        .map(|c| if c == ';' || c == '\n' || c == '\r' { ' ' } else { c })
        .collect();
    // Split into comma-parts first so `color=255,0,136` and `a=1,b=2`
    // both work, then re-join rgb triplets.
    let mut parts: Vec<String> = Vec::new();
    for chunk in normalized.split_whitespace() {
        for p in chunk.split(',') {
            let p = p.trim();
            if !p.is_empty() {
                parts.push(p.to_string());
            }
        }
    }
    let is_bare_num = |s: &str| s.parse::<u8>().is_ok() && !s.contains(['=', ':']);
    let mut toks: Vec<String> = Vec::with_capacity(parts.len());
    let mut i = 0;
    while i < parts.len() {
        let p = parts[i].as_str();
        match p.find(['=', ':']) {
            Some(sep) => {
                let (k, v) = (p[..sep].trim(), p[sep + 1..].trim());
                let color_key = matches!(
                    k.to_ascii_lowercase().as_str(),
                    "color" | "colour" | "rgb"
                );
                if color_key && is_bare_num(v) && i + 2 < parts.len() && is_bare_num(&parts[i + 1]) && is_bare_num(&parts[i + 2]) {
                    toks.push(format!("{}={},{},{}", k, v, parts[i + 1], parts[i + 2]));
                    i += 3;
                    continue;
                }
                toks.push(p.to_string());
            }
            None => {
                // Bare `color R G B` form.
                if matches!(p.to_ascii_lowercase().as_str(), "color" | "colour" | "rgb")
                    && i + 3 < parts.len()
                    && is_bare_num(&parts[i + 1])
                    && is_bare_num(&parts[i + 2])
                    && is_bare_num(&parts[i + 3])
                {
                    toks.push(format!("{}={},{},{}", p, parts[i + 1], parts[i + 2], parts[i + 3]));
                    i += 4;
                    continue;
                }
                toks.push(p.to_string());
            }
        }
        i += 1;
    }
    for tok in toks {
        let (k, v) = match tok.find(['=', ':']) {
            Some(p) => (tok[..p].trim().to_ascii_lowercase(), tok[p + 1..].trim().to_string()),
            None => continue,
        };
        if v.is_empty() {
            continue;
        }
        match k.as_str() {
            "color" | "colour" | "rgb" => {
                if let Some(c) = parse_color(&v) {
                    st.color = Some(c);
                }
            }
            "energy" | "level" | "bass" | "volume" | "rms" => {
                if let Some(e) = parse_level(&v) {
                    st.energy = Some(e);
                }
            }
            "beat" | "kick" | "onset" => {
                if let Some(b) = parse_beat(&v) {
                    st.beat = Some(b);
                }
            }
            _ => {}
        }
    }
    st
}

fn parse_level(v: &str) -> Option<f32> {
    let f: f32 = v.parse().ok()?;
    if !f.is_finite() {
        return None;
    }
    Some(if f > 1.0 { (f / 100.0).clamp(0.0, 1.0) } else { f.clamp(0.0, 1.0) })
}

fn parse_beat(v: &str) -> Option<f32> {
    match v.to_ascii_lowercase().as_str() {
        "true" | "yes" | "on" | "hit" => return Some(1.0),
        "false" | "no" | "off" => return Some(0.0),
        _ => {}
    }
    parse_level(v)
}

// ---------------------------------------------------------------------------
// fallback beat monitor (tiny PulseAudio RMS sampler, std + libc only)
// ---------------------------------------------------------------------------

const BEAT_RATE: u32 = 8000;
const BEAT_WINDOW: usize = 256;

struct BeatSample {
    energy_bits: AtomicU32,
    beat_bits: AtomicU32,
    updated_ms: AtomicU64,
    dead: AtomicBool,
}

fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// Background PulseAudio monitor. Samples the default sink monitor at 8kHz
/// mono and tracks RMS energy plus a simple onset (beat) envelope.
/// Created lazily and only while sharkvis integration is active.
pub struct BeatMonitor {
    sample: std::sync::Arc<BeatSample>,
    stop: std::sync::Arc<AtomicBool>,
    thread: Option<std::thread::JoinHandle<()>>,
}

impl BeatMonitor {
    pub fn start() -> BeatMonitor {
        let sample = std::sync::Arc::new(BeatSample {
            energy_bits: AtomicU32::new(0),
            beat_bits: AtomicU32::new(0),
            updated_ms: AtomicU64::new(now_ms()),
            dead: AtomicBool::new(false),
        });
        let stop = std::sync::Arc::new(AtomicBool::new(false));
        let s = sample.clone();
        let st = stop.clone();
        let thread = std::thread::spawn(move || beat_thread(s, st));
        BeatMonitor {
            sample,
            stop,
            thread: Some(thread),
        }
    }

    /// `(energy, beat)` in `0..1`, or `None` when no usable data (yet).
    pub fn sample(&self) -> Option<(f32, f32)> {
        if self.sample.dead.load(Ordering::Relaxed) {
            return None;
        }
        if now_ms().saturating_sub(self.sample.updated_ms.load(Ordering::Relaxed)) > 1500 {
            return None;
        }
        Some((
            f32::from_bits(self.sample.energy_bits.load(Ordering::Relaxed)),
            f32::from_bits(self.sample.beat_bits.load(Ordering::Relaxed)),
        ))
    }
}

impl Drop for BeatMonitor {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::SeqCst);
        if let Some(t) = self.thread.take() {
            let _ = t.join();
        }
    }
}

fn beat_thread(sample: std::sync::Arc<BeatSample>, stop: std::sync::Arc<AtomicBool>) {
    let mut client = match BeatClient::connect("jefetch-beat") {
        Ok(c) => c,
        Err(_) => {
            sample.dead.store(true, Ordering::SeqCst);
            return;
        }
    };
    let dev = match client.default_monitor() {
        Ok(d) => d,
        Err(_) => {
            sample.dead.store(true, Ordering::SeqCst);
            return;
        }
    };
    let mut rec = match client.record(&dev, BEAT_RATE, 1) {
        Ok(r) => r,
        Err(_) => {
            sample.dead.store(true, Ordering::SeqCst);
            return;
        }
    };
    let mut raw = vec![0u8; BEAT_WINDOW * 2];
    let mut energy = 0.0f32;
    let mut avg = 0.0f32;
    let mut beat = 0.0f32;
    loop {
        if stop.load(Ordering::SeqCst) {
            break;
        }
        let mut got = 0usize;
        while got < raw.len() {
            if stop.load(Ordering::SeqCst) {
                break;
            }
            match rec.read_chunk(&mut raw[got..]) {
                Ok(0) => break,
                Ok(n) => got += n,
                Err(_) => {
                    sample.dead.store(true, Ordering::SeqCst);
                    return;
                }
            }
        }
        if got < raw.len() {
            if stop.load(Ordering::SeqCst) {
                break;
            }
            continue;
        }
        let mut sum = 0.0f32;
        for i in 0..BEAT_WINDOW {
            let v = i16::from_le_bytes([raw[2 * i], raw[2 * i + 1]]) as f32 / 32768.0;
            sum += v * v;
        }
        let rms = (sum / BEAT_WINDOW as f32).sqrt();
        let target = (rms * 4.0).clamp(0.0, 1.0);
        energy += (target - energy) * 0.4;
        avg += (energy - avg) * 0.05;
        if energy > avg * 1.25 + 0.04 && energy > 0.06 {
            beat = 1.0;
        } else {
            beat *= 0.92;
        }
        sample.energy_bits.store(energy.to_bits(), Ordering::Relaxed);
        sample.beat_bits.store(beat.clamp(0.0, 1.0).to_bits(), Ordering::Relaxed);
        sample.updated_ms.store(now_ms(), Ordering::Relaxed);
    }
}

// --- minimal PulseAudio native client (record-only) -------------------------

struct BeatClient {
    sock: std::os::unix::net::UnixStream,
    tag: u32,
}

struct BeatRecord {
    sock: std::os::unix::net::UnixStream,
    stream: u32,
    pending: Vec<u8>,
    off: usize,
}

fn pa_server_candidates() -> Vec<String> {
    let mut v = Vec::new();
    if let Ok(srv) = std::env::var("PULSE_SERVER") {
        for part in srv.split(' ') {
            let part = part.trim();
            if part.is_empty() || part.starts_with("tcp:") {
                continue;
            }
            v.push(part.strip_prefix("unix:").unwrap_or(part).to_string());
        }
    }
    if let Ok(rt) = std::env::var("XDG_RUNTIME_DIR") {
        v.push(format!("{}/pulse/native", rt.trim_end_matches('/')));
    }
    let uid = unsafe { libc::getuid() };
    v.push(format!("/run/user/{}/pulse/native", uid));
    v
}

fn pa_cookie() -> Vec<u8> {
    let mut cookie = vec![0u8; 256];
    let mut paths = Vec::new();
    if let Ok(rt) = std::env::var("XDG_RUNTIME_DIR") {
        paths.push(format!("{}/pulse/cookie", rt.trim_end_matches('/')));
    }
    if let Ok(home) = std::env::var("HOME") {
        paths.push(format!("{}/.config/pulse/cookie", home));
        paths.push(format!("{}/.pulse-cookie", home));
    }
    for p in paths {
        if let Ok(n) = std::fs::File::open(&p).and_then(|mut f| {
            use std::io::Read;
            f.read(&mut cookie)
        }) {
            if n > 0 {
                break;
            }
        }
    }
    cookie
}

struct PaWriter {
    raw: Vec<u8>,
}

impl PaWriter {
    fn new() -> PaWriter {
        PaWriter { raw: Vec::new() }
    }
    fn u32(&mut self, v: u32) {
        self.raw.push(b'L');
        self.raw.extend_from_slice(&v.to_be_bytes());
    }
    fn u8v(&mut self, v: u8) {
        self.raw.push(b'B');
        self.raw.push(v);
    }
    fn string(&mut self, s: &str) {
        self.raw.push(b't');
        self.raw.extend_from_slice(s.as_bytes());
        self.raw.push(0);
    }
    fn string_null(&mut self) {
        self.raw.push(b'N');
    }
    fn arbitrary(&mut self, data: &[u8]) {
        self.raw.push(b'x');
        self.raw.extend_from_slice(&(data.len() as u32).to_be_bytes());
        self.raw.extend_from_slice(data);
    }
    fn sample_spec(&mut self, format: u8, channels: u8, rate: u32) {
        self.raw.push(b'a');
        self.raw.push(format);
        self.raw.push(channels);
        self.raw.extend_from_slice(&rate.to_be_bytes());
    }
    fn channel_map(&mut self, map: &[u8]) {
        self.raw.push(b'm');
        self.raw.push(map.len() as u8);
        self.raw.extend_from_slice(map);
    }
    fn cvolume(&mut self, channels: u8) {
        self.raw.push(b'v');
        self.raw.push(channels);
        for _ in 0..channels {
            self.raw.extend_from_slice(&0x10000u32.to_be_bytes());
        }
    }
    fn proplist(&mut self, app: &str) {
        self.raw.push(b'P');
        self.string("application.name");
        self.u32(app.len() as u32);
        self.arbitrary(app.as_bytes());
        self.string("application.process.binary");
        self.u32(app.len() as u32);
        self.arbitrary(app.as_bytes());
        self.string_null();
    }
    fn boolean(&mut self, b: bool) {
        self.raw.push(if b { b'1' } else { b'0' });
    }
}

struct PaReader<'a> {
    data: &'a [u8],
    pos: usize,
}

impl<'a> PaReader<'a> {
    fn new(data: &'a [u8]) -> PaReader<'a> {
        PaReader { data, pos: 0 }
    }
    fn byte(&mut self) -> Result<u8, String> {
        let b = self.data.get(self.pos).copied().ok_or("pulse: truncated")?;
        self.pos += 1;
        Ok(b)
    }
    fn expect(&mut self, t: u8) -> Result<(), String> {
        let got = self.byte()?;
        if got != t {
            return Err(format!("pulse: bad tag {} != {}", got as char, t as char));
        }
        Ok(())
    }
    fn u32(&mut self) -> Result<u32, String> {
        self.expect(b'L')?;
        if self.pos + 4 > self.data.len() {
            return Err("pulse: truncated".into());
        }
        let v = u32::from_be_bytes(self.data[self.pos..self.pos + 4].try_into().unwrap());
        self.pos += 4;
        Ok(v)
    }
    fn string_or_null(&mut self) -> Result<Option<String>, String> {
        if self.data.get(self.pos) == Some(&b'N') {
            self.pos += 1;
            return Ok(None);
        }
        self.expect(b't')?;
        let start = self.pos;
        while self.data.get(self.pos) != Some(&0) {
            if self.pos >= self.data.len() {
                return Err("pulse: unterminated string".into());
            }
            self.pos += 1;
        }
        let s = std::str::from_utf8(&self.data[start..self.pos])
            .map_err(|_| "pulse: bad string".to_string())?
            .to_string();
        self.pos += 1;
        Ok(Some(s))
    }
    fn skip_sample_spec(&mut self) -> Result<(), String> {
        self.expect(b'a')?;
        if self.pos + 6 > self.data.len() {
            return Err("pulse: truncated".into());
        }
        self.pos += 6;
        Ok(())
    }
    fn skip_channel_map(&mut self) -> Result<(), String> {
        self.expect(b'm')?;
        let n = self.byte()? as usize;
        if self.pos + n > self.data.len() {
            return Err("pulse: truncated".into());
        }
        self.pos += n;
        Ok(())
    }
    fn skip_cvolume(&mut self) -> Result<(), String> {
        self.expect(b'v')?;
        let n = self.byte()? as usize;
        if self.pos + 4 * n > self.data.len() {
            return Err("pulse: truncated".into());
        }
        self.pos += 4 * n;
        Ok(())
    }
    fn boolean(&mut self) -> Result<bool, String> {
        match self.byte()? {
            b'1' => Ok(true),
            b'0' => Ok(false),
            b => Err(format!("pulse: bad bool {}", b as char)),
        }
    }
}

impl BeatClient {
    fn connect(app: &str) -> Result<BeatClient, String> {
        let mut last_err = "no pulse socket".to_string();
        for path in pa_server_candidates() {
            match std::os::unix::net::UnixStream::connect(&path) {
                Ok(sock) => {
                    let _ = sock.set_read_timeout(Some(Duration::from_millis(1500)));
                    let _ = sock.set_write_timeout(Some(Duration::from_millis(1500)));
                    let mut c = BeatClient { sock, tag: 0 };
                    c.auth()?;
                    c.set_client_name(app)?;
                    return Ok(c);
                }
                Err(e) => last_err = format!("pulse {}: {}", path, e),
            }
        }
        Err(last_err)
    }

    fn next_tag(&mut self) -> u32 {
        self.tag += 1;
        self.tag
    }

    fn send(&mut self, cmd: u32, tag: u32, body: &PaWriter) -> Result<(), String> {
        use std::io::Write;
        let mut payload = Vec::with_capacity(body.raw.len() + 10);
        payload.push(b'L');
        payload.extend_from_slice(&cmd.to_be_bytes());
        payload.push(b'L');
        payload.extend_from_slice(&tag.to_be_bytes());
        payload.extend_from_slice(&body.raw);
        let mut frame = Vec::with_capacity(20 + payload.len());
        frame.extend_from_slice(&(payload.len() as u32).to_be_bytes());
        frame.extend_from_slice(&u32::MAX.to_be_bytes());
        frame.extend_from_slice(&[0; 12]);
        frame.extend_from_slice(&payload);
        self.sock.write_all(&frame).map_err(|e| format!("pulse write: {}", e))
    }

    fn read_packet(&mut self) -> Result<(u32, u32, Vec<u8>), String> {
        use std::io::Read;
        let mut desc = [0u8; 20];
        self.sock.read_exact(&mut desc).map_err(|e| format!("pulse read: {}", e))?;
        let len = u32::from_be_bytes(desc[0..4].try_into().unwrap()) as usize;
        let channel = u32::from_be_bytes(desc[4..8].try_into().unwrap());
        if len == 0 || len > 4 * 1024 * 1024 {
            return Err(format!("pulse: bogus frame {}", len));
        }
        let mut payload = vec![0u8; len];
        self.sock.read_exact(&mut payload).map_err(|e| format!("pulse read: {}", e))?;
        if channel != u32::MAX {
            return Err("pulse: unexpected memblock".into());
        }
        let mut r = PaReader::new(&payload);
        let cmd = r.u32()?;
        let tag = r.u32()?;
        Ok((cmd, tag, payload[r.pos..].to_vec()))
    }

    fn reply_for(&mut self, want: u32) -> Result<Vec<u8>, String> {
        loop {
            let (cmd, tag, rest) = self.read_packet()?;
            if tag != want {
                continue;
            }
            match cmd {
                2 => return Ok(rest),
                0 => {
                    let code = PaReader::new(&rest).u32().unwrap_or(0);
                    return Err(format!("pulse: server error {}", code));
                }
                _ => {}
            }
        }
    }

    fn auth(&mut self) -> Result<(), String> {
        let tag = self.next_tag();
        let mut body = PaWriter::new();
        body.u32(35);
        body.arbitrary(&pa_cookie());
        self.send(8, tag, &body)?;
        self.reply_for(tag).map(|_| ())
    }

    fn set_client_name(&mut self, app: &str) -> Result<(), String> {
        let tag = self.next_tag();
        let mut body = PaWriter::new();
        body.proplist(app);
        self.send(9, tag, &body)?;
        self.reply_for(tag).map(|_| ())
    }

    fn default_monitor(&mut self) -> Result<String, String> {
        let tag = self.next_tag();
        let mut body = PaWriter::new();
        body.u32(0xFFFF_FFFF);
        body.string_null();
        self.send(21, tag, &body)?;
        let reply = self.reply_for(tag)?;
        let mut r = PaReader::new(&reply);
        let _index = r.u32()?;
        let _name = r.string_or_null()?;
        let _desc = r.string_or_null()?;
        r.skip_sample_spec()?;
        r.skip_channel_map()?;
        let _module = r.u32()?;
        r.skip_cvolume()?;
        let _mute = r.boolean()?;
        let _monitor_index = r.u32()?;
        match r.string_or_null()? {
            Some(m) if !m.is_empty() => Ok(m),
            _ => Err("pulse: default sink has no monitor".into()),
        }
    }

    fn record(mut self, device: &str, rate: u32, channels: u8) -> Result<BeatRecord, String> {
        let tag = self.next_tag();
        let mut body = PaWriter::new();
        body.sample_spec(3, channels, rate);
        body.channel_map(if channels >= 2 { &[1u8, 2] } else { &[0u8] });
        body.u32(0xFFFF_FFFF);
        body.string(device);
        body.u32(u32::MAX);
        body.boolean(false);
        body.u32(320);
        for _ in 0..11 {
            body.boolean(false);
        }
        body.proplist("jefetch-beat");
        body.u32(0xFFFF_FFFF);
        body.boolean(false);
        body.boolean(false);
        body.boolean(false);
        body.u8v(0);
        body.cvolume(channels);
        for _ in 0..5 {
            body.boolean(false);
        }
        self.send(5, tag, &body)?;
        let reply = self.reply_for(tag)?;
        let stream = PaReader::new(&reply).u32()?;
        Ok(BeatRecord {
            sock: self.sock,
            stream,
            pending: Vec::new(),
            off: 0,
        })
    }
}

impl BeatRecord {
    fn read_chunk(&mut self, out: &mut [u8]) -> Result<usize, String> {
        use std::io::Read;
        if self.off >= self.pending.len() {
            self.pending.clear();
            self.off = 0;
            loop {
                let mut desc = [0u8; 20];
                self.sock.read_exact(&mut desc).map_err(|e| format!("pulse read: {}", e))?;
                let len = u32::from_be_bytes(desc[0..4].try_into().unwrap()) as usize;
                let channel = u32::from_be_bytes(desc[4..8].try_into().unwrap());
                if len > 4 * 1024 * 1024 {
                    return Err("pulse: bogus frame".into());
                }
                let mut payload = vec![0u8; len];
                self.sock.read_exact(&mut payload).map_err(|e| format!("pulse read: {}", e))?;
                if channel == u32::MAX {
                    let mut r = PaReader::new(&payload);
                    let cmd = r.u32()?;
                    let _tag = r.u32()?;
                    if cmd == 0 {
                        return Err("pulse: server error".into());
                    }
                    if cmd == 65 {
                        return Err("pulse: stream killed".into());
                    }
                    continue;
                }
                if channel == self.stream {
                    self.pending = payload;
                    break;
                }
            }
        }
        let n = (self.pending.len() - self.off).min(out.len());
        out[..n].copy_from_slice(&self.pending[self.off..self.off + n]);
        self.off += n;
        Ok(n)
    }
}

// ---------------------------------------------------------------------------
// high-level sync used by the live view
// ---------------------------------------------------------------------------

/// Polls sharkvis state with cheap caching. Not shared between threads;
/// construct one per live view.
pub struct Sync {
    running: bool,
    running_at: Option<Instant>,
    gradients: Option<(Rgb, Rgb)>,
    glyphs: Option<Vec<String>>,
    visual_at: Option<Instant>,
    monitor: Option<BeatMonitor>,
    last: LiveFrame,
}

impl Sync {
    pub fn new() -> Sync {
        Sync {
            running: false,
            running_at: None,
            gradients: None,
            glyphs: None,
            visual_at: None,
            monitor: None,
            last: LiveFrame::inactive(),
        }
    }

    pub fn poll(&mut self, mode: SharkvisMode, beat_depth: f32, sense: f32) -> LiveFrame {
        if mode == SharkvisMode::Off {
            self.monitor = None;
            self.last = LiveFrame::inactive();
            return self.last.clone();
        }
        let now = Instant::now();
        if self.running_at.is_none_or(|t| now.duration_since(t) >= Duration::from_secs(1)) {
            self.running = is_running();
            self.running_at = Some(now);
        }
        if !mode.enabled(self.running) {
            self.monitor = None;
            self.last = LiveFrame::inactive();
            return self.last.clone();
        }
        if self.visual_at.is_none_or(|t| now.duration_since(t) >= Duration::from_secs(5)) {
            let (grad, glyphs) = visual_from_paths(&config_paths());
            self.gradients = grad;
            self.glyphs = glyphs;
            self.visual_at = Some(now);
        }

        // Live state file wins (exact colors + energy from sharkvis itself).
        let mut energy: Option<f32> = None;
        let mut beat: Option<f32> = None;
        let mut color: Option<Rgb> = None;
        if let Some(live) = read_live_state() {
            energy = live.energy;
            beat = live.beat;
            color = live.color;
        }
        // Fallback: local monitor for the beat.
        if energy.is_none() || beat.is_none() {
            if self.monitor.is_none() {
                self.monitor = Some(BeatMonitor::start());
            }
            if let Some(mon) = &self.monitor {
                if let Some((e, b)) = mon.sample() {
                    if energy.is_none() {
                        energy = Some(e);
                    }
                    if beat.is_none() {
                        beat = Some(b);
                    }
                }
            }
        }

        let energy = energy.unwrap_or(0.0).clamp(0.0, 1.0);
        let beat = beat.unwrap_or(0.0).clamp(0.0, 1.0);
        let drv = drive(energy, sense);
        // Both gradient ends when known (vertical logo gradient mirroring
        // the sharkvis bars); otherwise the single live color, if any.
        let grad = self.gradients;
        let flat = if grad.is_none() { color } else { None };
        let frame = LiveFrame {
            active: true,
            grad,
            flat,
            glyphs: self.glyphs.clone(),
            energy,
            drive: drv,
            beat,
            speed_mult: volume_speed_mult(drv, beat, beat_depth),
        };
        self.last = frame;
        self.last.clone()
    }

    /// Last frame (used before the first poll or when throttling).
    pub fn last(&self) -> LiveFrame {
        self.last.clone()
    }
}

impl Default for Sync {
    fn default() -> Self {
        Sync::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests below mutate process env; serialize them.
    static ENV_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    #[test]
    fn mode_values_parse() {
        assert_eq!(SharkvisMode::parse_value("on"), Some(SharkvisMode::On));
        assert_eq!(SharkvisMode::parse_value("OFF"), Some(SharkvisMode::Off));
        assert_eq!(SharkvisMode::parse_value("auto"), Some(SharkvisMode::Auto));
        assert_eq!(SharkvisMode::parse_value("bogus"), None);
        assert!(SharkvisMode::Auto.enabled(true));
        assert!(!SharkvisMode::Auto.enabled(false));
        assert!(SharkvisMode::On.enabled(false));
        assert!(!SharkvisMode::Off.enabled(true));
    }

    #[test]
    fn colors_parse() {
        assert_eq!(parse_color("#ff8800"), Some((255, 136, 0)));
        assert_eq!(parse_color("ff0000"), Some((255, 0, 0)));
        assert_eq!(parse_color("red"), Some((255, 0, 0)));
        assert_eq!(parse_color("255,0,136"), Some((255, 0, 136)));
        assert_eq!(parse_color("nope"), None);
        assert_eq!(parse_color(""), None);
    }

    #[test]
    fn sharkvis_config_parses_gradients() {        let cfg = "[general]\nbars = 0\n[color]\ngradient_low = ffff00\ngradient_high = ff0000\n";
        assert_eq!(
            parse_sharkvis_config(cfg),
            Some(((255, 255, 0), (255, 0, 0)))
        );
        assert_eq!(parse_sharkvis_config("[general]\n"), None);
        let single = "[color]\ngradient_low = #00ff00 ; comment\n";
        assert_eq!(
            parse_sharkvis_config(single),
            Some(((0, 255, 0), (0, 255, 0)))
        );
    }

    #[test]
    fn state_text_parses() {
        let st = parse_state_text("color=#ff8800 energy=0.42 beat=1");
        assert_eq!(st.color, Some((255, 136, 0)));
        assert!((st.energy.unwrap() - 0.42).abs() < 1e-4);
        assert_eq!(st.beat, Some(1.0));
        let st = parse_state_text("energy=42 beat=true");
        assert!((st.energy.unwrap() - 0.42).abs() < 1e-4);
        assert_eq!(st.beat, Some(1.0));
        let st = parse_state_text("color=255,0,136;level=0.5,kick=0");
        assert_eq!(st.color, Some((255, 0, 136)));
        assert_eq!(st.beat, Some(0.0));
        let st = parse_state_text("nothing here");
        assert!(st.color.is_none() && st.energy.is_none() && st.beat.is_none());
    }

    #[test]
    fn lerp_drive_and_speed_math() {
        assert_eq!(lerp_rgb((0, 0, 0), (255, 255, 255), 0.5), (128, 128, 128));
        assert_eq!(lerp_rgb((0, 0, 0), (255, 0, 0), 0.0), (0, 0, 0));
        assert_eq!(lerp_rgb((0, 0, 0), (255, 0, 0), 1.0), (255, 0, 0));
        // Drive: silence maps to 0, small energy already moves a lot.
        assert!((drive(0.0, DEFAULT_SENSE) - 0.0).abs() < 1e-5);
        assert!((drive(0.25, DEFAULT_SENSE) - 0.6321).abs() < 1e-3);
        assert!((drive(1.0, DEFAULT_SENSE) - 0.9817).abs() < 1e-3);
        // Hotter sense reacts even earlier.
        assert!(drive(0.1, 8.0) > drive(0.1, 2.0));
        // Volume follow on drive: 0 -> 0.5x, 0.5 -> 1x, 1 -> 1.5x.
        assert!((volume_speed_mult(0.0, 0.0, 0.6) - 0.5).abs() < 1e-5);
        assert!((volume_speed_mult(0.5, 0.0, 0.6) - 1.0).abs() < 1e-5);
        assert!((volume_speed_mult(1.0, 0.0, 0.6) - 1.5).abs() < 1e-5);
        // Beat dips on top: full drive + beat = 1.5 * 0.4.
        assert!((volume_speed_mult(1.0, 1.0, 0.6) - 0.6).abs() < 1e-5);
        // beat=0 disables the dip entirely.
        assert!((volume_speed_mult(1.0, 1.0, 0.0) - 1.5).abs() < 1e-5);
    }

    #[test]
    fn stale_state_file_ignored() {
        let _guard = ENV_LOCK.lock().unwrap();
        let path = std::env::temp_dir().join(format!("jefetch-sharkvis-{}", std::process::id()));
        std::fs::write(&path, "color=#ff0000 energy=1 beat=1").unwrap();
        // Make it look old.
        let old = std::time::SystemTime::now() - Duration::from_secs(30);
        let _ = filetime_set(&path, old);
        std::env::set_var("JEFETCH_SHARKVIS_STATE", path.to_string_lossy().as_ref());
        std::env::set_var("JEFETCH_SHARKVIS_STALE_MS", "1000");
        assert!(read_live_state().is_none(), "stale file must be ignored");
        // Fresh file parses.
        std::fs::write(&path, "color=#00ff00 energy=0.5 beat=0").unwrap();
        let st = read_live_state().expect("fresh file reads");
        assert_eq!(st.color, Some((0, 255, 0)));
        std::env::remove_var("JEFETCH_SHARKVIS_STATE");
        std::env::remove_var("JEFETCH_SHARKVIS_STALE_MS");
        let _ = std::fs::remove_file(&path);
    }

    #[cfg(unix)]
    fn filetime_set(path: &std::path::Path, t: std::time::SystemTime) -> std::io::Result<()> {
        use std::os::unix::io::AsRawFd;
        let f = std::fs::File::options().write(true).open(path)?;
        let secs = t.duration_since(std::time::UNIX_EPOCH).unwrap().as_secs() as libc::time_t;
        let times = [
            libc::timespec { tv_sec: secs, tv_nsec: 0 },
            libc::timespec { tv_sec: secs, tv_nsec: 0 },
        ];
        let r = unsafe { libc::futimens(f.as_raw_fd(), times.as_ptr()) };
        if r == 0 { Ok(()) } else { Err(std::io::Error::last_os_error()) }
    }

    #[test]
    fn running_override_env() {
        let _guard = ENV_LOCK.lock().unwrap();
        std::env::set_var("JEFETCH_SHARKVIS_RUNNING", "1");
        assert!(is_running());
        std::env::set_var("JEFETCH_SHARKVIS_RUNNING", "0");
        assert!(!is_running());
        std::env::remove_var("JEFETCH_SHARKVIS_RUNNING");
    }

    #[test]
    fn sync_off_stays_inactive() {
        let mut s = Sync::new();
        let f = s.poll(SharkvisMode::Off, DEFAULT_BEAT_DEPTH, DEFAULT_SENSE);
        assert!(!f.active);
        assert!((f.speed_mult - 1.0).abs() < 1e-5);
    }    #[test]
    fn sync_auto_inactive_without_process() {
        let _guard = ENV_LOCK.lock().unwrap();
        std::env::set_var("JEFETCH_SHARKVIS_RUNNING", "0");
        // Point state away so no live file interferes.
        std::env::set_var("JEFETCH_SHARKVIS_STATE", "/nonexistent-jefetch-state");
        let mut s = Sync::new();
        let f = s.poll(SharkvisMode::Auto, DEFAULT_BEAT_DEPTH, DEFAULT_SENSE);
        assert!(!f.active);
        std::env::remove_var("JEFETCH_SHARKVIS_RUNNING");
        std::env::remove_var("JEFETCH_SHARKVIS_STATE");
    }

    #[test]
    fn glyphs_parse_from_config() {
        let cfg = "[general]\nbars = 0\n[visualizer]\nmode = bars\nglyphs = 1234567\n";
        assert_eq!(
            parse_sharkvis_glyphs(cfg),
            Some(vec!["1", "2", "3", "4", "5", "6", "7"].into_iter().map(str::to_string).collect::<Vec<_>>())
        );
        let blocks = "[visualizer]\nglyphs = \u{2581}\u{2582}\u{2583}\u{2584}\u{2585}\u{2586}\u{2587}\u{2588}\n";
        assert_eq!(parse_sharkvis_glyphs(blocks).map(|v| v.len()), Some(8));
        assert_eq!(parse_sharkvis_glyphs("[general]\n"), None);
        assert_eq!(parse_sharkvis_glyphs("[visualizer]\nmode = bars\n"), None);
    }

    #[test]
    fn sync_poll_uses_state_file_when_running() {
        let _guard = ENV_LOCK.lock().unwrap();
        let path = std::env::temp_dir().join(format!("jefetch-sharkvis-live-{}", std::process::id()));
        std::fs::write(&path, "color=#ff8800 energy=0.6 beat=1").unwrap();
        std::env::set_var("JEFETCH_SHARKVIS_STATE", path.to_string_lossy().as_ref());
        std::env::set_var("JEFETCH_SHARKVIS_CONFIG", "/nonexistent-jefetch-config");
        std::env::set_var("JEFETCH_SHARKVIS_RUNNING", "1");
        let mut s = Sync::new();
        let f = s.poll(SharkvisMode::Auto, DEFAULT_BEAT_DEPTH, DEFAULT_SENSE);
        assert!(f.active);
        assert_eq!(f.grad, None);
        assert_eq!(f.flat, Some((255, 136, 0)), "single live color without gradients");
        assert!((f.beat - 1.0).abs() < 1e-5);
        assert!((f.drive - 0.9093).abs() < 1e-3, "drive saturates at energy 0.6");
        assert!((f.speed_mult - 0.5637).abs() < 1e-3, "vol 1.41 * dip 0.4");
        std::env::remove_var("JEFETCH_SHARKVIS_STATE");
        std::env::remove_var("JEFETCH_SHARKVIS_CONFIG");
        std::env::remove_var("JEFETCH_SHARKVIS_RUNNING");
        let _ = std::fs::remove_file(&path);
    }

    #[test]
    fn sync_poll_reports_gradient_and_glyphs() {
        let _guard = ENV_LOCK.lock().unwrap();
        let cfg_path =
            std::env::temp_dir().join(format!("jefetch-sharkvis-cfg-{}", std::process::id()));
        std::fs::write(
            &cfg_path,
            "[color]\ngradient_low = 000000\ngradient_high = ff0000\n[visualizer]\nglyphs = 123\n",
        )
        .unwrap();
        let state_path =
            std::env::temp_dir().join(format!("jefetch-sharkvis-nocolor-{}", std::process::id()));
        std::fs::write(&state_path, "energy=0.5 beat=0").unwrap();
        std::env::set_var("JEFETCH_SHARKVIS_CONFIG", cfg_path.to_string_lossy().as_ref());
        std::env::set_var("JEFETCH_SHARKVIS_STATE", state_path.to_string_lossy().as_ref());
        std::env::set_var("JEFETCH_SHARKVIS_RUNNING", "1");
        let mut s = Sync::new();
        let f = s.poll(SharkvisMode::Auto, DEFAULT_BEAT_DEPTH, DEFAULT_SENSE);
        assert!(f.active);
        assert_eq!(f.grad, Some(((0, 0, 0), (255, 0, 0))), "both gradient ends");
        assert_eq!(f.flat, None);
        assert_eq!(
            f.glyphs,
            Some(vec!["1".to_string(), "2".to_string(), "3".to_string()])
        );
        assert!((f.drive - 0.8647).abs() < 1e-3);
        assert!((f.speed_mult - 1.3647).abs() < 1e-3, "volume follow at energy 0.5");
        std::env::remove_var("JEFETCH_SHARKVIS_CONFIG");
        std::env::remove_var("JEFETCH_SHARKVIS_STATE");
        std::env::remove_var("JEFETCH_SHARKVIS_RUNNING");
        let _ = std::fs::remove_file(&cfg_path);
        let _ = std::fs::remove_file(&state_path);
    }
}
