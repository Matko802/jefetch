// Top-level application logic: parse CLI, load config, run modules,
// render against the logo and print.

use crate::config::configfile::{Config, LogoConfig, ModuleEntry};
use crate::modules::{self, ModuleInstance, ModuleOutput};
use crate::print::color;

#[derive(Debug, Default)]
pub struct CliOptions {
    pub structure: Option<String>,
    pub structure_disabled: Vec<String>,
    pub config_path: Option<String>,
    pub no_config: bool,
    pub json: bool,
    pub force_static: bool,
}

/// A resolved logo: plain lines plus the ANSI color for each line.
#[derive(Debug, Clone)]
pub struct ResolvedLogo {
    pub lines: Vec<String>,
    pub colors: Vec<String>,
    pub width: usize,
    pub padding_right: usize,
}

pub struct App {
    pub options: CliOptions,
    pub config: Config,
    pub logo: Option<ResolvedLogo>,
}

impl App {
    pub fn new(options: CliOptions) -> App {
        App {
            options,
            config: Config::default(),
            logo: None,
        }
    }

    /// Load config from `-c` path, default paths, or fall back to defaults.
    pub fn load_config(&mut self) {
        if !self.options.no_config {
            if let Some(p) = &self.options.config_path {
                // Handle both .toml (sharkfetch) and .jsonc (fastfetch) for -c
                let cfg = if p.ends_with(".toml") {
                    load_toml_config_file(p)
                } else {
                    load_config_file(p)
                };
                if let Some(cfg) = cfg {
                    self.config = cfg;
                    return;
                }
            }
            // sharkfetch's own config: prefer JSONC if present, else TOML (both supported).
            for dir in config_search_dirs() {
                let candidate_jsonc = format!("{}/sharkfetch/config.jsonc", dir);
                if let Some(cfg) = load_config_file(&candidate_jsonc) {
                    self.config = cfg;
                    return;
                }
                let candidate_toml = format!("{}/sharkfetch/config.toml", dir);
                if let Some(cfg) = load_toml_config_file(&candidate_toml) {
                    self.config = cfg;
                    return;
                }
            }

        }
    }

    /// Ensure a default config exists, creating the directory and file on
    /// first run. Supports both JSONC and TOML.
    /// - If `config.jsonc` exists and is empty → populates it with `DEFAULT_JSONC_CONFIG`.
    /// - Else if either `config.jsonc` or `config.toml` already exists (with content) → nothing is created.
    /// - Else (neither exists) → generates `config.toml`.
    /// So to use JSONC: `rm ~/.config/sharkfetch/config.toml; touch ~/.config/sharkfetch/config.jsonc`
    /// and run `sharkfetch` once — it will fill `config.jsonc` with the default JSONC template.
    pub fn ensure_default_config(&self) -> Option<String> {
        let dir = config_search_dirs().first()?.to_string();
        let path_toml = format!("{}/sharkfetch/config.toml", dir);
        let path_jsonc = format!("{}/sharkfetch/config.jsonc", dir);
        let jsonc_exists = std::path::Path::new(&path_jsonc).exists();
        let toml_exists = std::path::Path::new(&path_toml).exists();

        // JSONC takes precedence: if it exists but is empty, populate it
        if jsonc_exists {
            if let Ok(content) = std::fs::read_to_string(&path_jsonc) {
                if content.trim().is_empty() {
                    // Empty file created by user → fill with default JSONC
                    if let Ok(_) = std::fs::create_dir_all(format!("{}/sharkfetch", dir)) {
                        if let Ok(_) = std::fs::write(&path_jsonc, crate::config::toml_config::DEFAULT_JSONC_CONFIG) {
                            return Some(path_jsonc);
                        }
                    }
                }
            }
            return None;
        }
        if toml_exists {
            return None;
        }
        if let Ok(_) = std::fs::create_dir_all(format!("{}/sharkfetch", dir)) {
            if let Ok(_) = std::fs::write(&path_toml, crate::config::toml_config::DEFAULT_TOML_CONFIG) {
                return Some(path_toml);
            }
        }
        None
    }

    fn should_animate(&self) -> bool {
        if self.options.force_static {
            return false;
        }
        if let Some(anim) = &self.config.logo.animation {
            let a = anim.to_ascii_lowercase();
            // "off", "none", "static", "false" => no animation
            if a == "off" || a == "none" || a == "static" || a == "false" || a == "0" {
                return false;
            }
            // "spin", "spin xyz", "areofetch", etc. => animate
            if a.contains("spin") || a.contains("areo") || a.contains("rotate") || a == "on" || a == "true" || a == "1" {
                return true;
            }
            // Any non-empty animation string means animate (for future types)
            if !a.trim().is_empty() {
                return true;
            }
        }
        false
    }

    pub fn run(&mut self) -> i32 {
        self.load_config();
        // Create default config only if none was loaded (first run).
        if self.config.loaded_from.is_none() && !self.options.no_config {
            self.ensure_default_config();
        }
        self.pick_logo();

        // Build the ordered list of module entries to run.
        let entries: Vec<ModuleEntry> = if let Some(s) = &self.options.structure {
            s.split(':')
                .map(|x| x.trim().to_string())
                .filter(|x| !x.is_empty())
                .map(|name| {
                    self.config
                        .modules
                        .iter()
                        .find(|m| m.module().eq_ignore_ascii_case(&name))
                        .cloned()
                        .unwrap_or_else(|| ModuleEntry::Name(name))
                })
                .collect()
        } else if !self.config.modules.is_empty() {
            self.config.modules.clone()
        } else {
            // No config: default structure.
            default_structure()
                .into_iter()
                .map(ModuleEntry::Name)
                .collect()
        };

        if self.options.json {
            self.print_json(&entries);
            return 0;
        }

        // Areofetch-like animated mode
        if self.should_animate() {
            return self.run_animated(&entries);
        }

        let lines = self.render_modules(&entries);

        // Print, respecting logo width.
        let logo_pad = self
            .logo
            .as_ref()
            .map(|l| l.width)
            .unwrap_or(0);
        let mut out = String::new();

        if let Some(l) = &self.logo {
            // Print logo lines combined with module lines. Pad every logo line
            // to the logo's max width so the text column starts at a fixed
            // offset regardless of the ragged logo edge.
            let n = lines.len().max(l.lines.len());
            for row in 0..n {
                let logo_line = l
                    .lines
                    .get(row)
                    .map(|s| s.clone())
                    .unwrap_or_else(|| " ".repeat(logo_pad));
                let text_line = lines.get(row).map(|s| s.as_str()).unwrap_or("");
                let color_name = l.colors.get(row).map(|s| s.as_str()).unwrap_or("");
                let lcol = colorize_logo(&logo_line, color_name);
                let lcol_visible = crate::print::format::visible_len(&lcol);
                let pad_needed = logo_pad.saturating_sub(lcol_visible);
                let gap = l.padding_right;
                let line = format!(
                    "{}{}{}{}",
                    lcol,
                    " ".repeat(pad_needed),
                    " ".repeat(gap),
                    text_line
                );
                out.push_str(line.trim_end());
                out.push('\n');
            }
        } else {
            for line in &lines {
                out.push_str(line.trim_end());
                out.push('\n');
            }
        }

        print!("{}", out);
        0
    }

    fn run_animated(&mut self, _entries: &[ModuleEntry]) -> i32 {
        self.run_animated_fallback()
    }

    fn run_animated_fallback(&mut self) -> i32 {
        let base_logo = match &self.logo {
            Some(l) => l.clone(),
            None => return 0,
        };
        let entries: Vec<ModuleEntry> = if let Some(s) = &self.options.structure {
            s.split(':').map(|x| x.trim().to_string()).filter(|x| !x.is_empty()).map(|name| {
                self.config.modules.iter().find(|m| m.module().eq_ignore_ascii_case(&name)).cloned().unwrap_or_else(|| ModuleEntry::Name(name))
            }).collect()
        } else if !self.config.modules.is_empty() {
            self.config.modules.clone()
        } else {
            default_structure().into_iter().map(ModuleEntry::Name).collect()
        };
        let base_lines = self.render_modules(&entries);
        // Build anim config from logo.animation ("spin", "spin x", "spin y", etc.)
        let anim_cfg = crate::anim::AnimConfig::from_animation_str(self.config.logo.animation.as_deref());
        print!("\x1b[?25l\x1b[?1049h");
        let _ = std::io::Write::flush(&mut std::io::stdout());
        let mut orig_term = unsafe { std::mem::zeroed::<libc::termios>() };
        let mut tty_fd: i32 = -1;
        let tty_file = std::fs::OpenOptions::new().read(true).write(true).open("/dev/tty").ok();
        if let Some(f) = &tty_file {
            use std::os::unix::io::AsRawFd;
            tty_fd = f.as_raw_fd();
            if unsafe { libc::tcgetattr(tty_fd, &mut orig_term) } == 0 {
                let mut raw_term = orig_term;
                raw_term.c_lflag &= !(libc::ICANON | libc::ECHO);
                raw_term.c_cc[libc::VMIN as usize] = 0;
                raw_term.c_cc[libc::VTIME as usize] = 0;
                unsafe { libc::tcsetattr(tty_fd, libc::TCSANOW, &raw_term); }
            } else {
                tty_fd = -1;
            }
        }
        let is_tty = tty_fd != -1;
        let _tty_guard = tty_file;
        let mut frame: usize = 0;
        let mut out = String::new();
        // areofyl 1:1 layout: logo canvas (ANIM_WIDTH=60) on the left,
        // info on the right, logo vertically centred on the info block.
        let info_count = base_lines.len();
        let render_height = (info_count + 2).max(36);
        const GAP: usize = 2;
        loop {
            // True 3D port: per-frame 3D projection + Blinn-Phong — 1:1 fetch.c
            let anim_logo = crate::anim::render_frame(&base_logo, frame, &anim_cfg, render_height, info_count);
            out.clear();
            out.push_str("\x1b[2J\x1b[H");
            let n = anim_logo.lines.len();
            for row in 0..n {
                // Logo canvas row (blank-filled to ANIM_WIDTH=60); already coloured.
                let logo_canvas = anim_logo.lines.get(row).map(|s| s.as_str()).unwrap_or("");
                let mut line = String::new();
                line.push_str(logo_canvas);
                // Info line: row 1..=info_count holds the k-th info line (fetch_start=1).
                let info_row = row as isize - 1;
                if info_row >= 0 && (info_row as usize) < info_count {
                    line.push_str(&" ".repeat(GAP));
                    line.push_str(base_lines.get(info_row as usize).map(|s| s.as_str()).unwrap_or(""));
                }
                out.push_str(line.trim_end());
                out.push('\n');
            }
            print!("{}", out);
            let _ = std::io::Write::flush(&mut std::io::stdout());
            for _ in 0..8 {
                std::thread::sleep(std::time::Duration::from_millis(10));
                let mut should_quit = false;
                if is_tty && tty_fd != -1 {
                    let mut buf = [0u8; 16];
                    let n = unsafe { libc::read(tty_fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
                    if n > 0 {
                        for &b in &buf[..n as usize] {
                            if b == b'q' || b == b'Q' || b == 0x03 || b == 0x1b {
                                should_quit = true;
                                break;
                            }
                        }
                    }
                }
                if !should_quit {
                    let mut buf = [0u8; 16];
                    let flags = unsafe { libc::fcntl(libc::STDIN_FILENO, libc::F_GETFL) };
                    if flags != -1 {
                        unsafe { libc::fcntl(libc::STDIN_FILENO, libc::F_SETFL, flags | libc::O_NONBLOCK); }
                        let n = unsafe { libc::read(libc::STDIN_FILENO, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
                        if n > 0 {
                            for &b in &buf[..n as usize] {
                                if b == b'q' || b == b'Q' || b == 0x03 {
                                    should_quit = true;
                                    break;
                                }
                            }
                        }
                        unsafe { libc::fcntl(libc::STDIN_FILENO, libc::F_SETFL, flags); }
                    }
                }
                if should_quit {
                    print!("\x1b[?1049l\x1b[?25h");
                    let _ = std::io::Write::flush(&mut std::io::stdout());
                    if is_tty && tty_fd != -1 {
                        unsafe { libc::tcsetattr(tty_fd, libc::TCSANOW, &orig_term); }
                    }
                    return 0;
                }
            }
            frame = frame.wrapping_add(1);
        }
    }

    // Kept for compatibility but now unused; 3D lives in `crate::anim`.
    #[allow(dead_code)]
    fn animated_logo(base: &ResolvedLogo, _frame: usize) -> ResolvedLogo {
        base.clone()
    }

    fn pick_logo(&mut self) {
        self.logo = resolve_logo(&self.config);
        if self.logo.is_none() {
            // Fall back to the detected distro logo (fastfetch auto-detects).
            let id = crate::detection::os::detect().id.to_ascii_lowercase();
            self.logo = builtin_logo_v(&id, &self.config.logo)
                .or_else(|| builtin_logo_v("linux", &self.config.logo))
                .or_else(|| builtin_logo_v("unknown", &self.config.logo));
        }
        // fastfetch derives the default key/title colors from the distro logo
        // (unless the config explicitly sets them).
        self.apply_logo_colors();
    }

    /// Apply fastfetch's logo-derived display colors: title <- colorTitle or
    /// slots[0], keys <- colorKeys or slots[1], bold. Only when the user
    /// hasn't configured them.
    fn apply_logo_colors(&mut self) {
        let id = crate::detection::os::detect().id.to_ascii_lowercase();
        let Some(logo) = crate::logo::by_name(&id) else {
            return;
        };
        if self.config.display.title_color.is_none() {
            let sgr = logo
                .color_title
                .or_else(|| logo.slots.first().copied())
                .unwrap_or("34");
            self.config.display.title_color = Some(format!("bold_{}", sgr));
        }
        if self.config.display.key_color.is_none() {
            let sgr = logo
                .color_keys
                .or(logo.slots.get(1).copied())
                .unwrap_or("36");
            self.config.display.key_color = Some(format!("bold_{}", sgr));
        }
    }

    /// --json output: an array of per-module objects, fastfetch-style.
    fn print_json(&self, entries: &[ModuleEntry]) {
        use crate::config::json::JsonValue;
        let mut items: Vec<JsonValue> = Vec::new();
        for entry in entries {
            let name = entry.module().to_string();
            if self
                .options
                .structure_disabled
                .iter()
                .any(|d| d.eq_ignore_ascii_case(&name))
            {
                continue;
            }
            let inst = self.instance_for(entry.clone());
            let type_name = modules::exec_impl::json_type_name(&name);
            if let Some(err) = modules::exec_impl::json_error(&name, &inst, &self.config) {
                items.push(JsonValue::Obj(vec![
                    ("type".to_string(), JsonValue::Str(type_name.to_string())),
                    ("error".to_string(), JsonValue::Str(err)),
                ]));
                continue;
            }
            if let Some(result) = modules::exec_impl::json_result(&name, &inst, &self.config) {
                items.push(JsonValue::Obj(vec![
                    ("type".to_string(), JsonValue::Str(type_name.to_string())),
                    ("result".to_string(), result),
                ]));
            }
        }
        print!("{}", JsonValue::Arr(items).to_json_pretty());
    }

    fn render_modules(&self, entries: &[ModuleEntry]) -> Vec<String> {
        // Parallelize heavy detections (packages, gpu, disk, etc.) with
        // std::thread::scope — still zero-crate, uses only std.
        // Keep original order by sorting on original index.
        let mut ordered: Vec<(usize, Option<ModuleOutput>)> = Vec::new();
        if entries.len() > 1 {
            std::thread::scope(|s| {
                let mut handles = Vec::new();
                for (idx, entry) in entries.iter().enumerate() {
                    let entry = entry.clone();
                    let cfg = &self.config;
                    let disabled = self.options.structure_disabled.clone();
                    handles.push(s.spawn(move || {
                        let name = entry.module().to_string();
                        if disabled.iter().any(|d| d.eq_ignore_ascii_case(&name)) {
                            return (idx, None);
                        }
                        let args = match &entry {
                            ModuleEntry::Object { args, .. } => args.clone(),
                            ModuleEntry::Name(_) => crate::config::moduleargs::ModuleArgs::default(),
                        };
                        let raw = match &entry {
                            ModuleEntry::Object { raw, .. } => Some(raw.clone()),
                            ModuleEntry::Name(_) => None,
                        };
                        let inst = ModuleInstance {
                            module: entry.module().to_string(),
                            entry,
                            args,
                            raw,
                        };
                        let out = modules::run_instance(&inst, cfg);
                        (idx, out)
                    }));
                }
                for h in handles {
                    ordered.push(h.join().unwrap());
                }
            });
            ordered.sort_by_key(|(idx, _)| *idx);
        } else {
            for (idx, entry) in entries.iter().enumerate() {
                let name = entry.module().to_string();
                if self
                    .options
                    .structure_disabled
                    .iter()
                    .any(|d| d.eq_ignore_ascii_case(&name))
                {
                    ordered.push((idx, None));
                    continue;
                }
                let inst = self.instance_for(entry.clone());
                ordered.push((idx, modules::run_instance(&inst, &self.config)));
            }
        }

        let mut lines: Vec<String> = Vec::new();
        for (_, maybe_out) in ordered {
            let Some(out) = maybe_out else { continue };
            if out.blank {
                lines.push(String::new());
                continue;
            }
            if !out.supported || out.values.is_empty() {
                continue;
            }
            let key_visible = crate::print::format::visible_len(&out.key);
            if key_visible == 0 {
                for v in &out.values {
                    lines.push(v.clone());
                }
                continue;
            }
            let padding = self.config.display.padding;
            let sep_render = separator_colored(self.config.display.separator.as_str(), &self.config);
            let indent = key_visible + crate::print::format::visible_len(&sep_render) + padding;
            for (idx, v) in out.values.iter().enumerate() {
                if idx == 0 {
                    lines.push(format!(
                        "{}{}{}{}",
                        out.key,
                        sep_render,
                        " ".repeat(padding),
                        v
                    ));
                } else if v.starts_with("Disk (") {
                    // Subsequent disks already contain their own key like "Disk (/mnt/ssd): ..."
                    // Color the Disk (...) part like the first disk's key and put at key column
                    if let Some(colon) = v.find(": ") {
                        let key_part = &v[..colon];
                        let rest = &v[colon + 2..];
                        let colored_key = match self.config.display.key_color.as_deref().map(|c| crate::print::color::color_code_to_ansi(c)) {
                            Some(crate::print::color::ApplyResult::Ansi { start, end }) => format!("{}{}{}", start, key_part, end),
                            _ => {
                                // Fallback to bold_cyan like first disk
                                match crate::print::color::color_code_to_ansi("bold_cyan") {
                                    crate::print::color::ApplyResult::Ansi { start, end } => format!("{}{}{}", start, key_part, end),
                                    _ => key_part.to_string(),
                                }
                            }
                        };
                        let sep = separator_colored(self.config.display.separator.as_str(), &self.config);
                        lines.push(format!("{}{}{}{}", colored_key, sep, " ".repeat(padding), rest));
                    } else {
                        lines.push(v.clone());
                    }
                } else {
                    lines.push(format!("{}{}", " ".repeat(indent), v));
                }
            }
        }
        lines
    }

    fn instance_for(&self, entry: ModuleEntry) -> ModuleInstance {
        let name = entry.module();

        let args = match &entry {
            ModuleEntry::Object { args, .. } => args.clone(),
            ModuleEntry::Name(_) => crate::config::moduleargs::ModuleArgs::default(),
        };

        let raw = match &entry {
            ModuleEntry::Object { raw, .. } => Some(raw.clone()),
            ModuleEntry::Name(_) => None,
        };

        ModuleInstance {
            module: name.to_string(),
            entry,
            args,
            raw,
        }
    }
}

/// Render the separator string with the configured separator color.
fn separator_colored(_sep: &str, cfg: &crate::config::configfile::Config) -> String {
    let s = cfg.display.separator.clone();
    match &cfg.display.separator_color {
        Some(c) => match color::color_code_to_ansi(c) {
            color::ApplyResult::Ansi { start, end } => format!("{}{}{}", start, s, end),
            _ => s,
        },
        None => s,
    }
}

/// Build a ResolvedLogo from config: file logos, custom logos, or builtin.
fn resolve_logo(cfg: &Config) -> Option<ResolvedLogo> {
    // `type: "builtin"` with a source holding the builtin logo id.
    if cfg
        .logo
        .logo_type
        .as_deref()
        .map(|t| t.eq_ignore_ascii_case("builtin"))
        .unwrap_or(false)
    {
        let id = cfg
            .logo
            .source
            .clone()
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| crate::detection::os::detect().id);
        return builtin_logo_v(id.to_ascii_lowercase().as_str(), &cfg.logo);
    }

    // `type: "none"` → no logo.
    if cfg
        .logo
        .logo_type
        .as_deref()
        .map(|t| t.eq_ignore_ascii_case("none"))
        .unwrap_or(false)
    {
        return None;
    }

    // File logo (e.g. "type": "file", "source": ".../shork.txt").
    if let Some(src) = &cfg.logo.source {
        let expanded = expand_tilde(src);
        if let Ok(text) = std::fs::read_to_string(&expanded) {
            return Some(logo_from_lines(&text, &cfg.logo));
        }
    }

    // Custom source string.
    if let Some(src) = &cfg.logo.source {
        if src.contains('\n') {
            return Some(logo_from_lines(src, &cfg.logo));
        }
    }

    // Builtin logo by type.
    let id = crate::detection::os::detect().id;
    let name = cfg
        .logo
        .logo_type
        .clone()
        .filter(|t| !t.eq_ignore_ascii_case("auto"))
        .unwrap_or(id);
    builtin_logo_v(name.to_ascii_lowercase().as_str(), &cfg.logo)
}

/// Turn raw logo text lines into a ResolvedLogo, applying color map + padding.
fn logo_from_lines(text: &str, lc: &LogoConfig) -> ResolvedLogo {
    let mut lines: Vec<String> = text
        .lines()
        .map(|l| l.trim_end_matches('\r').to_string())
        .collect();
    if lines.is_empty() {
        lines.push(String::new());
    }

    // Per-line colors from `color: { "1": "green", "2-3": "blue" }`.
    let mut colors: Vec<String> = vec![String::new(); lines.len()];
    for (spec, cname) in &lc.color_map {
        let ansi = color::named_color_sgr(cname).unwrap_or_default();
        apply_line_spec(&mut colors, spec, &ansi);
    }
    // A plain `color: "green"` string applies to all lines.
    if let Some(c) = &lc.color {
        if let Some(ansi) = color::named_color_sgr(c) {
            for c in colors.iter_mut() {
                *c = ansi.clone();
            }
        }
    }

    // Padding: left prefix, top blank lines.
    if let Some(top) = lc.padding_top {
        for _ in 0..top {
            lines.insert(0, String::new());
            colors.insert(0, String::new());
        }
    }
    if let Some(left) = lc.padding_left {
        for l in lines.iter_mut() {
            *l = format!("{}{}", " ".repeat(left), l);
        }
    }

    let art_width = lines.iter().map(|l| crate::print::format::visible_len(l)).max().unwrap_or(0);
    let pad_r = lc.padding_right.unwrap_or(0);
    let width = art_width;
    ResolvedLogo {
        lines,
        colors,
        width,
        padding_right: pad_r,
    }
}

fn builtin_logo_v(name: &str, lc: &crate::config::configfile::LogoConfig) -> Option<ResolvedLogo> {
    let logo = crate::logo::by_name(name)?;
    // fastfetch builds a `colors[]` from the logo's slots; logos without slot
    // markers fall back to their single base `color`.
    let slots: Vec<&str> = if logo.slots.is_empty() {
        vec![logo.color]
    } else {
        logo.slots.to_vec()
    };
    let pad_top = lc.padding_top.unwrap_or(0);
    let pad_left = lc.padding_left.unwrap_or(0);
    let pad_right = lc.padding_right.unwrap_or(4);
    let bold = "\x1b[1m";
    let mut lines: Vec<String> = Vec::new();
    let mut art_width = 0usize;
    // carryColor persists across lines exactly like fastfetch logoLineCacheBuild.
    let mut carry = format!("\x1b[{}m", slots[0]);
    for rawin in logo.lines {
        let mut out = String::new();
        // Every line starts with bold (brightColor) then the carried color so
        // trailing unmarked glyphs keep the previous segment's color.
        out.push_str(bold);
        out.push_str(&carry);
        if pad_left > 0 {
            out.push_str(&" ".repeat(pad_left));
        }
        let mut chars = rawin.chars().peekable();
        while let Some(c) = chars.next() {
            if c == '$' {
                if let Some(&d) = chars.peek() {
                    if let Some(n) = d.to_digit(10) {
                        let n1 = n as usize;
                        if (1..=slots.len()).contains(&n1) {
                            carry = format!("\x1b[{}m", slots[n1 - 1]);
                            out.push_str(&carry);
                            chars.next();
                            continue;
                        }
                    } else if d == '$' {
                        // `$$` collapses to a single literal `$` (fastfetch).
                        out.push('$');
                        chars.next();
                        continue;
                    }
                }
                out.push('$');
            } else {
                out.push(c);
            }
        }
        // Reset at the end of each line so the color cannot bleed past the logo.
        out.push_str(color::RESET);
        art_width = art_width.max(crate::print::format::visible_len(&out));
        lines.push(out);
    }
    // Padding top: blank lines at the beginning (like fastfetch).
    for _ in 0..pad_top {
        lines.insert(0, String::new());
    }
    let width = art_width;
    let line_count = lines.len();
    Some(ResolvedLogo {
        lines,
        colors: vec![String::new(); line_count],
        width,
        padding_right: pad_right,
    })
}

fn expand_tilde(path: &str) -> String {
    if let Some(rest) = path.strip_prefix("~/") {
        if let Some(home) = std::env::var_os("HOME") {
            return format!("{}/{}", home.to_string_lossy(), rest);
        }
    }
    path.to_string()
}

/// Apply a line spec like "1", "3", "1-4" (fastfetch uses 1-based indexing)
/// or "0" (0-based) to set the ANSI color on the matching logo lines.
fn apply_line_spec(colors: &mut [String], spec: &str, ansi: &str) {
    let parse = |s: &str| s.trim().parse::<usize>().ok();
    if let Some((a, b)) = spec.split_once('-') {
        let a = parse(a);
        let b = parse(b);
        if let (Some(a), Some(b)) = (a, b) {
            for i in a.saturating_sub(1)..=b.saturating_sub(1) {
                if i < colors.len() {
                    colors[i] = ansi.to_string();
                }
            }
            return;
        }
    }
    if let Some(i) = parse(spec) {
        let idx = if i == 0 { 0 } else { i.saturating_sub(1) };
        if idx < colors.len() {
            colors[idx] = ansi.to_string();
        }
    }
}

fn colorize_logo(line: &str, color_name: &str) -> String {
    // `color_name` is either a raw ANSI sequence (from logo_from_lines /
    // builtin_logo_v base color) or empty (per-slot colored lines already
    // carry their own ANSI codes — leave those untouched).
    if color_name.trim().is_empty() {
        return line.to_string();
    }
    if color_name.starts_with('\x1b') {
        return format!("{}{}{}", color_name, line, color::RESET);
    }
    match color::color_code_to_ansi(color_name) {
        color::ApplyResult::Ansi { start, end } => format!("{}{}{}", start, line, end),
        _ => line.to_string(),
    }
}

/// Padding to the right of the logo before module text.
#[allow(dead_code)]
pub const PADDING_RIGHT: usize = 2;

fn default_structure() -> Vec<String> {
    // Match fastfetch's DEFAULT_STRUCTURE (implemented subset, in order).
    [
        "title", "separator", "os", "host", "kernel", "uptime", "packages", "shell",
        "display", "de", "wm", "theme", "icons", "font", "cursor", "terminal",
        "terminalfont", "cpu", "gpu", "memory", "swap", "disk", "localip", "battery",
        "locale", "break", "colors",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect()
}

/// Config search dirs in the order fastfetch checks them (config-first).
/// Exposed as `_pub` so the CLI can print `--list-config-paths`.
pub fn config_search_dirs_pub() -> Vec<String> {
    config_search_dirs()
}

fn config_search_dirs() -> Vec<String> {
    let mut dirs = Vec::new();
    if let Some(xdg) = std::env::var_os("XDG_CONFIG_HOME") {
        dirs.push(xdg.to_string_lossy().into_owned());
    } else if let Some(home) = std::env::var_os("HOME") {
        dirs.push(format!("{}/.config", home.to_string_lossy()));
    }
    if let Some(home) = std::env::var_os("HOME") {
        dirs.push(format!("{}/.config", home.to_string_lossy()));
    }
    dirs
}

pub fn load_config_file(path: &str) -> Option<Config> {
    let text = std::fs::read_to_string(path).ok()?;
    let mut cfg = Config::from_jsonc(&text).ok()?;
    cfg.loaded_from = Some(path.to_string());
    Some(cfg)
}

pub fn load_toml_config_file(path: &str) -> Option<Config> {
    let text = std::fs::read_to_string(path).ok()?;
    let mut cfg = crate::config::toml_config::from_toml(&text).ok()?;
    cfg.loaded_from = Some(path.to_string());
    Some(cfg)
}

/// Strip ANSI codes; keep for tests.
pub fn strip_ansi(s: &str) -> String {
    let mut out = String::new();
    let mut i = 0;
    let b = s.as_bytes();
    while i < b.len() {
        if b[i] == 0x1b && i + 1 < b.len() && b[i + 1] == b'[' {
            let mut j = i + 2;
            while j < b.len() && !b[j].is_ascii_alphabetic() {
                j += 1;
            }
            i = (j + 1).min(b.len());
            continue;
        }
        let len = utf8_len(b[i]);
        out.push_str(&s[i..i + len]);
        i += len;
    }
    out
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