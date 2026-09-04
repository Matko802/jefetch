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

    pub logo_name: Option<String>,
}

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

    pub fn load_config(&mut self) {
        if !self.options.no_config {
            if let Some(p) = &self.options.config_path {
                if let Some(cfg) = load_config_file(p) {
                    self.config = cfg;
                    return;
                }
            }

            for dir in config_search_dirs() {
                let candidate_jsonc = format!("{}/sharkfetch/config.jsonc", dir);
                if let Some(cfg) = load_config_file(&candidate_jsonc) {
                    self.config = cfg;
                    return;
                }
            }

        }
    }

    pub fn ensure_default_config(&self) -> Option<String> {
        let dir = config_search_dirs().first()?.to_string();
        let path_jsonc = format!("{}/sharkfetch/config.jsonc", dir);
        if let Ok(content) = std::fs::read_to_string(&path_jsonc) {
            if content.trim().is_empty() {

                if let Ok(_) = std::fs::create_dir_all(format!("{}/sharkfetch", dir)) {
                    if let Ok(_) = std::fs::write(&path_jsonc, crate::config::defaults::DEFAULT_JSONC_CONFIG) {
                        return Some(path_jsonc);
                    }
                }
            }
            return None;
        }
        if let Ok(_) = std::fs::create_dir_all(format!("{}/sharkfetch", dir)) {
            if let Ok(_) = std::fs::write(&path_jsonc, crate::config::defaults::DEFAULT_JSONC_CONFIG) {
                return Some(path_jsonc);
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

            if a == "off" || a == "none" || a == "static" || a == "false" || a == "0" {
                return false;
            }

            if a.contains("spin") || a.contains("areo") || a.contains("rotate") || a == "on" || a == "true" || a == "1" {
                return true;
            }

            if !a.trim().is_empty() {
                return true;
            }
        }
        false
    }

    pub fn run(&mut self) -> i32 {
        self.load_config();

        if self.config.loaded_from.is_none() && !self.options.no_config {
            self.ensure_default_config();
        }

        if let Some(name) = &self.options.logo_name {
            self.config.logo.source = Some(name.clone());
            self.config.logo.logo_type = Some("builtin".to_string());
        }
        self.pick_logo();

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

            default_structure()
                .into_iter()
                .map(ModuleEntry::Name)
                .collect()
        };

        if self.options.json {
            self.print_json(&entries);
            return 0;
        }

        if !self.options.force_static && stdout_is_tty() {
            return self.run_live(&entries, self.should_animate());
        }

        if self.should_animate() {
            return self.run_animated(&entries);
        }

        let lines = self.render_modules(&entries);

        let logo_pad = self
            .logo
            .as_ref()
            .map(|l| l.width)
            .unwrap_or(0);
        let mut out = String::new();

        if let Some(l) = &self.logo {

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

    fn run_live(&mut self, entries: &[ModuleEntry], start_animated: bool) -> i32 {
        let base_logo = self.logo.clone();
        let mut animated = start_animated && base_logo.is_some();
        let mut base_lines = self.render_modules(entries);

        let mut anim_cfg =
            crate::anim::AnimConfig::from_animation_str(self.config.logo.animation.as_deref());
        anim_cfg.apply_logo_overrides(&self.config.logo);
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
        const GAP: usize = 2;
        const ANIM_W: usize = 60;
        let mut last_refresh = std::time::Instant::now();
        let mut needs_draw = true;

        let restore = |tty_fd: i32, is_tty: bool, orig_term: &libc::termios| {
            print!("\x1b[?1049l\x1b[?25h");
            let _ = std::io::Write::flush(&mut std::io::stdout());
            if is_tty && tty_fd != -1 {
                unsafe { libc::tcsetattr(tty_fd, libc::TCSANOW, orig_term); }
            }
        };
        loop {

            if last_refresh.elapsed() >= std::time::Duration::from_secs(1) {
                base_lines = self.render_modules(entries);
                last_refresh = std::time::Instant::now();
                needs_draw = true;
            }

            let info_count = base_lines.len();
            let render_height = (info_count + 2).max(36);
            if animated {

                let logo = base_logo.as_ref().expect("animated needs a logo");
                let anim_logo =
                    crate::anim::render_frame(logo, frame, &anim_cfg, render_height, info_count);
                out.clear();
                out.push_str("\x1b[2J\x1b[H");
                let n = anim_logo.lines.len();
                for row in 0..n {

                    let logo_canvas = anim_logo.lines.get(row).map(|s| s.as_str()).unwrap_or("");
                    let mut line = String::new();
                    line.push_str(logo_canvas);

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
                frame = frame.wrapping_add(1);
            } else if needs_draw {

                out.clear();
                out.push_str("\x1b[2J\x1b[H");
                let logo_h = base_logo.as_ref().map(|l| l.lines.len()).unwrap_or(0);
                let logo_start = render_height.saturating_sub(logo_h) / 2;
                for row in 0..render_height {
                    let mut line = String::new();
                    if let Some(l) = &base_logo {
                        if row >= logo_start && row < logo_start + logo_h {
                            let i = row - logo_start;
                            let logo_line = l.lines.get(i).cloned().unwrap_or_default();
                            let color_name =
                                l.colors.get(i).map(|s| s.as_str()).unwrap_or("");
                            let lcol = colorize_logo(&logo_line, color_name);
                            let vis = crate::print::format::visible_len(&lcol);
                            line.push_str(&lcol);

                            if vis < ANIM_W {
                                line.push_str(&" ".repeat(ANIM_W - vis));
                            }
                        } else {
                            line.push_str(&" ".repeat(ANIM_W));
                        }
                    }
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
                needs_draw = false;
            }

            let mut quit = false;
            for _ in 0..8 {
                std::thread::sleep(std::time::Duration::from_millis(10));
                if let Some(b) = poll_key_byte(tty_fd, is_tty) {
                    match classify_key(b) {
                        KeyAction::Quit => {
                            quit = true;
                            break;
                        }
                        KeyAction::Toggle => {
                            if base_logo.is_some() {
                                animated = !animated;
                                needs_draw = true;
                            }
                        }
                        KeyAction::Ignore => {}
                    }
                }
                if quit {
                    break;
                }
            }
            if quit {
                restore(tty_fd, is_tty, &orig_term);
                return 0;
            }
        }
    }

    fn run_animated(&mut self, entries: &[ModuleEntry]) -> i32 {
        self.run_live(entries, true)
    }

    #[allow(dead_code)]
    fn animated_logo(base: &ResolvedLogo, _frame: usize) -> ResolvedLogo {
        base.clone()
    }

    fn pick_logo(&mut self) {
        self.logo = resolve_logo(&self.config);
        if self.logo.is_none() {

            let id = crate::detection::os::detect().id.to_ascii_lowercase();
            self.logo = builtin_logo_v(&id, &self.config.logo)
                .or_else(|| builtin_logo_v("linux", &self.config.logo))
                .or_else(|| builtin_logo_v("unknown", &self.config.logo));
        }

        self.apply_logo_colors();
    }

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

                    if let Some(colon) = v.find(": ") {
                        let key_part = &v[..colon];
                        let rest = &v[colon + 2..];
                        let colored_key = match self.config.display.key_color.as_deref().map(|c| crate::print::color::color_code_to_ansi(c)) {
                            Some(crate::print::color::ApplyResult::Ansi { start, end }) => format!("{}{}{}", start, key_part, end),
                            _ => {

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

fn resolve_logo(cfg: &Config) -> Option<ResolvedLogo> {

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

    if cfg
        .logo
        .logo_type
        .as_deref()
        .map(|t| t.eq_ignore_ascii_case("none"))
        .unwrap_or(false)
    {
        return None;
    }

    if let Some(src) = &cfg.logo.source {
        let expanded = expand_tilde(src);
        if let Ok(text) = std::fs::read_to_string(&expanded) {
            return Some(logo_from_lines(&text, &cfg.logo));
        }
    }

    if let Some(src) = &cfg.logo.source {
        if src.contains('\n') {
            return Some(logo_from_lines(src, &cfg.logo));
        }
    }

    let id = crate::detection::os::detect().id;
    let name = cfg
        .logo
        .source
        .clone()
        .filter(|s| !s.is_empty())
        .or_else(|| {
            cfg.logo
                .logo_type
                .clone()
                .filter(|t| !t.eq_ignore_ascii_case("auto"))
        })
        .unwrap_or(id);
    builtin_logo_v(name.to_ascii_lowercase().as_str(), &cfg.logo)
}

fn logo_from_lines(text: &str, lc: &LogoConfig) -> ResolvedLogo {
    let mut lines: Vec<String> = text
        .lines()
        .map(|l| l.trim_end_matches('\r').to_string())
        .collect();
    if lines.is_empty() {
        lines.push(String::new());
    }

    let mut colors: Vec<String> = vec![String::new(); lines.len()];
    for (spec, cname) in &lc.color_map {
        let ansi = color::named_color_sgr(cname).unwrap_or_default();
        apply_line_spec(&mut colors, spec, &ansi);
    }

    if let Some(c) = &lc.color {
        if let Some(ansi) = color::named_color_sgr(c) {
            for c in colors.iter_mut() {
                *c = ansi.clone();
            }
        }
    }

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

    let mut carry = format!("\x1b[{}m", slots[0]);
    for rawin in logo.lines {
        let mut out = String::new();

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

        out.push_str(color::RESET);
        art_width = art_width.max(crate::print::format::visible_len(&out));
        lines.push(out);
    }

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

#[allow(dead_code)]
pub const PADDING_RIGHT: usize = 2;

fn default_structure() -> Vec<String> {

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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeyAction {
    Quit,
    Toggle,
    Ignore,
}

pub fn classify_key(b: u8) -> KeyAction {
    match b {
        b'q' | b'Q' | 0x03 | 0x1b => KeyAction::Quit,
        b't' | b'T' => KeyAction::Toggle,
        _ => KeyAction::Ignore,
    }
}

fn poll_key_byte(tty_fd: i32, is_tty: bool) -> Option<u8> {
    if is_tty && tty_fd != -1 {
        let mut buf = [0u8; 16];
        let n =
            unsafe { libc::read(tty_fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
        if n > 0 {
            return Some(buf[0]);
        }
    }
    let mut buf = [0u8; 16];
    let flags = unsafe { libc::fcntl(libc::STDIN_FILENO, libc::F_GETFL) };
    if flags == -1 {
        return None;
    }
    unsafe { libc::fcntl(libc::STDIN_FILENO, libc::F_SETFL, flags | libc::O_NONBLOCK); }
    let n = unsafe {
        libc::read(
            libc::STDIN_FILENO,
            buf.as_mut_ptr() as *mut libc::c_void,
            buf.len(),
        )
    };
    unsafe { libc::fcntl(libc::STDIN_FILENO, libc::F_SETFL, flags); }
    if n > 0 {
        Some(buf[0])
    } else {
        None
    }
}

pub fn stdout_is_tty() -> bool {
    unsafe { libc::isatty(libc::STDOUT_FILENO) == 1 }
}

pub fn load_config_file(path: &str) -> Option<Config> {
    let text = std::fs::read_to_string(path).ok()?;
    let mut cfg = Config::from_jsonc(&text).ok()?;
    cfg.loaded_from = Some(path.to_string());
    Some(cfg)
}

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
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn live_keys_classify() {
        assert_eq!(classify_key(b'q'), KeyAction::Quit);
        assert_eq!(classify_key(b'Q'), KeyAction::Quit);
        assert_eq!(classify_key(0x03), KeyAction::Quit);
        assert_eq!(classify_key(0x1b), KeyAction::Quit);
        assert_eq!(classify_key(b't'), KeyAction::Toggle);
        assert_eq!(classify_key(b'T'), KeyAction::Toggle);
        assert_eq!(classify_key(b'a'), KeyAction::Ignore);
        assert_eq!(classify_key(b' '), KeyAction::Ignore);
    }
}
