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
                let candidate_jsonc = format!("{}/jefetch/config.jsonc", dir);
                if let Some(cfg) = load_config_file(&candidate_jsonc) {
                    self.config = cfg;
                    return;
                }
            }

        }
    }

    pub fn ensure_default_config(&self) -> Option<String> {
        let dir = config_search_dirs().first()?.to_string();
        let path_jsonc = format!("{}/jefetch/config.jsonc", dir);
        if let Ok(content) = std::fs::read_to_string(&path_jsonc) {
            if content.trim().is_empty() {

                if let Ok(_) = std::fs::create_dir_all(format!("{}/jefetch", dir)) {
                    if let Ok(_) = std::fs::write(&path_jsonc, crate::config::defaults::DEFAULT_JSONC_CONFIG) {
                        return Some(path_jsonc);
                    }
                }
            }
            return None;
        }
        if let Ok(_) = std::fs::create_dir_all(format!("{}/jefetch", dir)) {
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
        let mut anim_cfg = crate::anim::AnimConfig::from_animation_str(
            self.config.logo.animation.as_deref(),
        );
        anim_cfg.apply_logo_overrides(&self.config.logo);
        if !anim_cfg.speed_set {
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
        anim_cfg.sharkvis != crate::sharkvis::SharkvisMode::Off
    }

    fn build_entries(&self) -> Vec<ModuleEntry> {
        if let Some(s) = &self.options.structure {
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
        }
    }

    fn apply_logo_overrides(&mut self) {
        if let Some(name) = self.options.logo_name.clone() {
            self.config.logo.source = Some(name);
            self.config.logo.logo_type = Some("builtin".to_string());
        }
        if self.config.logo.color.is_none() {
            if let Some(c) =
                crate::anim::AnimConfig::animation_color(self.config.logo.animation.as_deref())
            {
                self.config.logo.color = Some(c);
            }
        }
    }

    fn config_watch_path(&self) -> Option<String> {
        if self.options.no_config {
            return None;
        }
        if let Some(p) = &self.config.loaded_from {
            return Some(p.clone());
        }
        config_search_dirs()
            .first()
            .map(|d| format!("{}/jefetch/config.jsonc", d))
    }

    pub fn run(&mut self) -> i32 {
        self.load_config();

        if self.config.loaded_from.is_none() && !self.options.no_config {
            self.ensure_default_config();
        }

        self.apply_logo_overrides();
        self.pick_logo();

        let entries: Vec<ModuleEntry> = self.build_entries();

        if self.options.json {
            self.print_json(&entries);
            return 0;
        }

        if !self.options.force_static && stdout_is_tty() {
            return self.run_live(entries, self.should_animate());
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

    fn run_live(&mut self, mut entries: Vec<ModuleEntry>, start_animated: bool) -> i32 {
        let mut base_logo = self.logo.clone();
        let mut animated = start_animated && base_logo.is_some();
        let mut base_lines = self.render_modules(&entries);

        let mut anim_cfg =
            crate::anim::AnimConfig::from_animation_str(self.config.logo.animation.as_deref());
        anim_cfg.apply_logo_overrides(&self.config.logo);
        let mut cloud = base_logo
            .as_ref()
            .and_then(|l| crate::anim::build_cloud(l, &anim_cfg));
        let mut shark_sync = crate::sharkvis::Sync::new();
        let mut shark_live: crate::sharkvis::LiveFrame;
        let mut shark_polled = std::time::Instant::now()
            .checked_sub(std::time::Duration::from_secs(1))
            .unwrap_or_else(std::time::Instant::now);
        let mut spin_phase: f64 = 0.0;
        let mut yaw_phase: f64 = 0.0;
        let mut pitch_phase: f64 = 0.0;
        let mut roll_phase: f64 = 0.0;
        let mut last_fx = std::time::Instant::now();
        let watch_path = self.config_watch_path();
        let mut last_stamp = watch_path.as_deref().and_then(config_stamp);
        print!("\x1b[0m\x1b[2J\x1b[3J\x1b[H\x1b[?25l");
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
                unsafe {
                    LIVE_SAVED_TERM = Some(orig_term);
                }
                install_live_signal_handlers();
            } else {
                tty_fd = -1;
            }
        }
        let is_tty = tty_fd != -1;
        let _tty_guard = tty_file;
        let mut out = String::new();
        const GAP: usize = 2;
        let mut last_refresh = std::time::Instant::now();
        let mut last_config_check = std::time::Instant::now()
            .checked_sub(std::time::Duration::from_secs(1))
            .unwrap_or_else(std::time::Instant::now);
        let mut needs_draw = true;
        let mut pending: std::collections::VecDeque<u8> = std::collections::VecDeque::new();
        let (info_tx, info_rx) = std::sync::mpsc::channel::<(u64, Vec<String>)>();
        let mut refresh_gen: u64 = 0;
        let mut refresh_busy: Option<u64> = None;

        let restore = |tty_fd: i32, is_tty: bool, orig_term: &libc::termios| {
            print!("\x1b[?25h\x1b[0m\n");
            let _ = std::io::Write::flush(&mut std::io::stdout());
            if is_tty && tty_fd != -1 {
                unsafe { libc::tcsetattr(tty_fd, libc::TCSANOW, orig_term); }
            }
        };
        loop {

            if last_config_check.elapsed() >= std::time::Duration::from_millis(250) {
                last_config_check = std::time::Instant::now();
                if let Some(path) = &watch_path {
                    let stamp = config_stamp(path);
                    if stamp != last_stamp {
                        last_stamp = stamp;
                        if let Some(cfg) = load_config_file(path) {
                            self.config = cfg;
                            self.apply_logo_overrides();
                            self.pick_logo();
                            base_logo = self.logo.clone();
                            entries = self.build_entries();
                            anim_cfg = crate::anim::AnimConfig::from_animation_str(
                                self.config.logo.animation.as_deref(),
                            );
                            anim_cfg.apply_logo_overrides(&self.config.logo);
                            cloud = base_logo
                                .as_ref()
                                .and_then(|l| crate::anim::build_cloud(l, &anim_cfg));
                            animated = self.should_animate() && base_logo.is_some();
                            refresh_gen = refresh_gen.wrapping_add(1);
                            refresh_busy = None;
                            last_refresh = std::time::Instant::now()
                                - std::time::Duration::from_secs(2);
                        }
                    }
                }
            }
            if last_refresh.elapsed() >= std::time::Duration::from_secs(1)
                && refresh_busy.is_none()
            {
                last_refresh = std::time::Instant::now();
                let gen = refresh_gen;
                refresh_busy = Some(gen);
                let cfg = self.config.clone();
                let disabled = self.options.structure_disabled.clone();
                let ents = entries.clone();
                let tx = info_tx.clone();
                std::thread::spawn(move || {
                    let lines = Self::render_modules_with(&cfg, &disabled, &ents);
                    let _ = tx.send((gen, lines));
                });
            }
            while let Ok((gen, lines)) = info_rx.try_recv() {
                if Some(gen) == refresh_busy {
                    refresh_busy = None;
                }
                if gen == refresh_gen {
                    base_lines = lines;
                    needs_draw = true;
                }
            }

            let info_count = base_lines.len();
            let mut render_height = (info_count + 2).max(36);
            let (cols, rows) = crate::common::terminal_size();
            if rows > 0 {
                render_height = render_height.min(rows.max(1));
            }
            if animated {

                if shark_polled.elapsed() >= std::time::Duration::from_millis(30) {
                    shark_live = shark_sync.poll(anim_cfg.sharkvis, anim_cfg.beat_depth);
                    shark_polled = std::time::Instant::now();
                } else {
                    shark_live = shark_sync.last();
                }
                spin_phase += f64::from(shark_live.speed_mult);
                let mut fx = crate::anim::RenderFx::none();
                if shark_live.active {
                    if let Some(g) = shark_live.grad {
                        fx.grad = Some(g);
                    } else if let Some(c) = shark_live.flat {
                        fx.grad = Some((c, c));
                    }
                    if !anim_cfg.original_glyphs && !anim_cfg.shading_explicit {
                        fx.shading = shark_live.glyphs.clone();
                    }
                    let (yaw_step, pitch_step) =
                        crate::anim::stereo_spin(shark_live.left, shark_live.right);
                    let fx_now = std::time::Instant::now();
                    let dt = fx_now
                        .duration_since(last_fx)
                        .as_secs_f32()
                        .clamp(0.001, 0.5);
                    last_fx = fx_now;
                    if anim_cfg.motion == crate::anim::Motion::Revert {
                        let tau = if anim_cfg.retract <= 0.0 {
                            f32::INFINITY
                        } else {
                            crate::anim::REVERT_TAU / anim_cfg.retract.clamp(0.1, 10.0)
                        };
                        yaw_phase =
                            crate::anim::revert_step(yaw_phase, f64::from(yaw_step), dt, tau);
                        pitch_phase =
                            crate::anim::revert_step(pitch_phase, f64::from(pitch_step), dt, tau);
                        let roll_step = if shark_live.energy > crate::anim::AUDIO_FLOOR {
                            f64::from(shark_live.energy) * f64::from(crate::anim::AUDIO_ROLL)
                        } else {
                            0.0
                        };
                        roll_phase =
                            crate::anim::revert_step(roll_phase, roll_step, dt, tau);
                        fx.audio = [
                            pitch_phase as f32,
                            yaw_phase as f32,
                            roll_phase as f32,
                        ];
                    } else {
                        yaw_phase += f64::from(yaw_step);
                        pitch_phase += f64::from(pitch_step);
                        if shark_live.energy > crate::anim::AUDIO_FLOOR {
                            roll_phase += f64::from(shark_live.energy)
                                * f64::from(crate::anim::AUDIO_ROLL);
                        }
                        fx.audio = [
                            pitch_phase as f32,
                            yaw_phase as f32,
                            roll_phase as f32,
                        ];
                    }
                    let boom = anim_cfg.boom.unwrap_or(0.0);
                    fx.scale =
                        1.0 + anim_cfg.grow * shark_live.beat + boom * shark_live.energy;
                }
                let mut render_cfg = anim_cfg.clone();
                if shark_live.active {
                    render_cfg.speed = 0.0;
                }
                let anim_logo = match cloud.as_mut() {
                    Some(c) => crate::anim::render_cloud_with_fx(
                        c,
                        spin_phase,
                        &render_cfg,
                        render_height,
                        info_count,
                        &fx,
                    ),
                    None => base_logo.clone().expect("animated needs a logo"),
                };
                out.clear();
                out.push_str("\x1b[H");
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
                    out.push_str(&crate::print::format::truncate_visible(
                        line.trim_end(),
                        cols,
                    ));
                    out.push_str("\x1b[K");
                    if row + 1 < n {
                        out.push('\n');
                    }
                }
                out.push_str("\x1b[J");
                print!("{}", out);
                let _ = std::io::Write::flush(&mut std::io::stdout());
            } else if needs_draw {

                out.clear();
                out.push_str("\x1b[H");
                let logo_h = base_logo.as_ref().map(|l| l.lines.len()).unwrap_or(0);
                let logo_w = base_logo.as_ref().map(|l| l.width).unwrap_or(0);
                let logo_gap = base_logo.as_ref().map(|l| l.padding_right).unwrap_or(0);
                let logo_start = 1;
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
                            line.push_str(&" ".repeat(logo_w.saturating_sub(vis)));
                        } else if logo_w > 0 {
                            line.push_str(&" ".repeat(logo_w));
                        }
                    }
                    let info_row = row as isize - 1;
                    if info_row >= 0 && (info_row as usize) < info_count {
                        if base_logo.is_some() {
                            line.push_str(&" ".repeat(logo_gap));
                        }
                        line.push_str(base_lines.get(info_row as usize).map(|s| s.as_str()).unwrap_or(""));
                    }
                    out.push_str(&crate::print::format::truncate_visible(
                        line.trim_end(),
                        cols,
                    ));
                    out.push_str("\x1b[K");
                    if row + 1 < render_height {
                        out.push('\n');
                    }
                }
                out.push_str("\x1b[J");
                print!("{}", out);
                let _ = std::io::Write::flush(&mut std::io::stdout());
                needs_draw = false;
            }

            let mut quit = false;
            let slices = (anim_cfg.frame_interval().as_millis() / 10).clamp(1, 200) as usize;
            for _ in 0..slices {
                std::thread::sleep(std::time::Duration::from_millis(10));
                match poll_key_action(tty_fd, is_tty, &mut pending) {
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
        self.run_live(entries.to_vec(), true)
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
        Self::render_modules_with(&self.config, &self.options.structure_disabled, entries)
    }

    fn render_modules_with(
        cfg: &Config,
        disabled: &[String],
        entries: &[ModuleEntry],
    ) -> Vec<String> {

        let mut ordered: Vec<(usize, Option<ModuleOutput>)> = Vec::new();
        if entries.len() > 1 {
            std::thread::scope(|s| {
                let mut handles = Vec::new();
                for (idx, entry) in entries.iter().enumerate() {
                    let entry = entry.clone();
                    let disabled = disabled.to_vec();
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
                if disabled
                    .iter()
                    .any(|d| d.eq_ignore_ascii_case(&name))
                {
                    ordered.push((idx, None));
                    continue;
                }
                let args = match entry {
                    ModuleEntry::Object { args, .. } => args.clone(),
                    ModuleEntry::Name(_) => crate::config::moduleargs::ModuleArgs::default(),
                };
                let raw = match entry {
                    ModuleEntry::Object { raw, .. } => Some(raw.clone()),
                    ModuleEntry::Name(_) => None,
                };
                let inst = ModuleInstance {
                    module: entry.module().to_string(),
                    entry: entry.clone(),
                    args,
                    raw,
                };
                ordered.push((idx, modules::run_instance(&inst, cfg)));
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
            let padding = cfg.display.padding;
            let sep_render = separator_colored(cfg.display.separator.as_str(), cfg);
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
                        let colored_key = match cfg.display.key_color.as_deref().map(|c| crate::print::color::color_code_to_ansi(c)) {
                            Some(crate::print::color::ApplyResult::Ansi { start, end }) => format!("{}{}{}", start, key_part, end),
                            _ => {

                                match crate::print::color::color_code_to_ansi("bold_cyan") {
                                    crate::print::color::ApplyResult::Ansi { start, end } => format!("{}{}{}", start, key_part, end),
                                    _ => key_part.to_string(),
                                }
                            }
                        };
                        let sep = separator_colored(cfg.display.separator.as_str(), cfg);
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

fn sgr_has_bg(slot: &str) -> bool {
    let parts: Vec<&str> = slot.split(';').collect();
    let mut i = 0;
    while i < parts.len() {
        match parts[i].parse::<i32>() {
            Ok(48) => return true,
            Ok(n) if (40..=47).contains(&n) || (100..=107).contains(&n) => return true,
            _ => {}
        }
        i += 1;
    }
    false
}

fn color_payload(name: &str) -> Option<String> {
    let sgr = color::named_color_sgr(name)?;
    let t = sgr.strip_prefix("\x1b[").unwrap_or(&sgr);
    let t = t.strip_suffix('m').unwrap_or(t);
    if t.is_empty() {
        None
    } else {
        Some(t.to_string())
    }
}

fn builtin_logo_v(name: &str, lc: &crate::config::configfile::LogoConfig) -> Option<ResolvedLogo> {
    let logo = crate::logo::by_name(name)?;

    let mut slots: Vec<String> = if logo.slots.is_empty() {
        vec![logo.color.to_string()]
    } else {
        logo.slots.iter().map(|s| s.to_string()).collect()
    };
    if let Some(forced) = lc.color.as_deref().and_then(color_payload) {
        for s in slots.iter_mut() {
            s.clone_from(&forced);
        }
    }
    let pad_top = lc.padding_top.unwrap_or(0);
    let pad_left = lc.padding_left.unwrap_or(0);
    let pad_right = lc.padding_right.unwrap_or(4);
    let bold = "\x1b[1m";
    let mut lines: Vec<String> = Vec::new();
    let mut art_width = 0usize;

    let keep_trailing = std::iter::once(logo.color)
        .chain(logo.slots.iter().copied())
        .any(sgr_has_bg);
    let mut carry = format!("\x1b[{}m", slots[0]);
    for rawin in logo.lines {
        let rawin = if keep_trailing {
            *rawin
        } else {
            rawin.trim_end()
        };
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

fn esc_followup_action(tail: &[u8]) -> KeyAction {
    match tail.first() {
        Some(b'[') | Some(b'O') => KeyAction::Ignore,
        _ => KeyAction::Quit,
    }
}

fn poll_key_action(
    tty_fd: i32,
    is_tty: bool,
    pending: &mut std::collections::VecDeque<u8>,
) -> KeyAction {
    match poll_key_byte(tty_fd, is_tty, pending) {
        Some(0x1b) => {
            std::thread::sleep(std::time::Duration::from_millis(25));
            let mut tail: Vec<u8> = Vec::new();
            loop {
                match poll_key_byte(tty_fd, is_tty, pending) {
                    Some(b) => {
                        tail.push(b);
                        if tail.len() >= 16 {
                            break;
                        }
                    }
                    None => break,
                }
            }
            esc_followup_action(&tail)
        }
        Some(b) => classify_key(b),
        None => KeyAction::Ignore,
    }
}

fn debug_log_keys(src: &str, buf: &[u8]) {
    if let Ok(p) = std::env::var("JEFETCH_DEBUG_KEYS") {
        if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(p) {
            use std::io::Write;
            let hex: Vec<String> = buf.iter().map(|b| format!("{:02x}", b)).collect();
            let _ = writeln!(f, "{}: {}", src, hex.join(" "));
        }
    }
}

fn poll_key_byte(
    tty_fd: i32,
    is_tty: bool,
    pending: &mut std::collections::VecDeque<u8>,
) -> Option<u8> {
    if let Some(b) = pending.pop_front() {
        return Some(b);
    }
    if is_tty && tty_fd != -1 {
        let mut buf = [0u8; 16];
        let n =
            unsafe { libc::read(tty_fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
        if n > 0 {
            let n = n as usize;
            debug_log_keys("tty", &buf[..n]);
            pending.extend(&buf[1..n]);
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
        let n = n as usize;
        debug_log_keys("stdin", &buf[..n]);
        pending.extend(&buf[1..n]);
        Some(buf[0])
    } else {
        None
    }
}

pub fn stdout_is_tty() -> bool {
    unsafe { libc::isatty(libc::STDOUT_FILENO) == 1 }
}

static mut LIVE_SAVED_TERM: Option<libc::termios> = None;

extern "C" fn live_signal_restore(sig: libc::c_int) {
    unsafe {
        let seq = b"\x1b[?25h\x1b[0m\n";
        let _ = libc::write(
            libc::STDOUT_FILENO,
            seq.as_ptr() as *const libc::c_void,
            seq.len(),
        );
        let saved: Option<libc::termios> = std::ptr::addr_of!(LIVE_SAVED_TERM).read();
        if let Some(t) = saved {
            libc::tcsetattr(libc::STDOUT_FILENO, libc::TCSANOW, &t);
        }
        libc::signal(sig, libc::SIG_DFL);
        libc::raise(sig);
    }
}

fn install_live_signal_handlers() {
    for sig in [
        libc::SIGTERM,
        libc::SIGINT,
        libc::SIGHUP,
        libc::SIGBUS,
        libc::SIGFPE,
        libc::SIGILL,
        libc::SIGSEGV,
        libc::SIGABRT,
    ] {
        unsafe {
            libc::signal(
                sig,
                live_signal_restore as extern "C" fn(libc::c_int) as libc::sighandler_t,
            );
        }
    }
}

fn config_stamp(path: &str) -> Option<(std::time::SystemTime, u64)> {
    let meta = std::fs::metadata(path).ok()?;
    Some((meta.modified().ok()?, meta.len()))
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

    fn animate_app(animation: Option<&str>, sharkvis: Option<&str>) -> App {
        let mut app = App::new(CliOptions::default());
        app.config.logo.animation = animation.map(|s| s.to_string());
        app.config.logo.sharkvis = sharkvis.map(|s| s.to_string());
        app
    }

    #[test]
    fn should_animate_needs_speed() {
        assert!(!animate_app(None, None).should_animate());
        assert!(!animate_app(Some("off"), None).should_animate());
        assert!(!animate_app(Some("spin y"), None).should_animate());
        assert!(!animate_app(Some("spin y sharkvis"), None).should_animate());
        assert!(animate_app(Some("spin y speed=2"), None).should_animate());
        assert!(animate_app(Some("spin y speed=0"), None).should_animate());
        assert!(animate_app(Some("spin xz flat"), Some("speed=0")).should_animate());
        assert!(!animate_app(Some("spin xz flat"), Some("off")).should_animate());
        assert!(!animate_app(Some("spin xz flat"), None).should_animate());
    }

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

    #[test]
    fn esc_sequences_are_swallowed() {
        assert_eq!(esc_followup_action(&[]), KeyAction::Quit);
        assert_eq!(esc_followup_action(b"[A"), KeyAction::Ignore);
        assert_eq!(esc_followup_action(b"[B"), KeyAction::Ignore);
        assert_eq!(esc_followup_action(b"O"), KeyAction::Ignore);
        assert_eq!(esc_followup_action(b"t"), KeyAction::Quit);
    }

    #[test]
    fn sgr_bg_detect() {
        assert!(sgr_has_bg("47"));
        assert!(sgr_has_bg("1;44"));
        assert!(sgr_has_bg("48;5;200"));
        assert!(sgr_has_bg("38;2;1;2;3;48;2;4;5;6"));
        assert!(!sgr_has_bg("34"));
        assert!(!sgr_has_bg("38;5;225"));
        assert!(!sgr_has_bg("1;31"));
        assert!(!sgr_has_bg(""));
    }

    #[test]
    fn builtin_trims_phantom_trailing_space() {
        let lc = crate::config::configfile::LogoConfig::default();
        let enso = builtin_logo_v("enso", &lc).expect("enso exists");
        assert!(enso.width <= 41, "enso width {}", enso.width);
        let kiba = builtin_logo_v("kibaos", &lc).expect("kibaos exists");
        assert!(!kiba.lines.is_empty());
    }

    #[test]
    fn config_stamp_tracks_changes() {
        let path = std::env::temp_dir().join(format!("jefetch-stamp-{}.json", std::process::id()));
        let s = path.to_string_lossy().into_owned();
        let _ = std::fs::remove_file(&path);
        assert_eq!(config_stamp(&s), None);
        std::fs::write(&path, r#"{"a": 1}"#).unwrap();
        let first = config_stamp(&s).expect("stamp after create");
        assert_eq!(config_stamp(&s), Some(first));
        std::fs::write(&path, r#"{"a": 1, "b": 2}"#).unwrap();
        let second = config_stamp(&s).expect("stamp after modify");
        assert_ne!(second, first);
        std::fs::remove_file(&path).unwrap();
        assert_eq!(config_stamp(&s), None);
    }
}
