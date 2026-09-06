use super::{ModuleInstance, ModuleOutput};
use crate::common;
use crate::config::configfile::Config;

pub fn render(name: &str, inst: &ModuleInstance, cfg: &Config) -> Option<ModuleOutput> {
    match name {
        "title" => render_title(cfg),
        "os" => render_os(cfg),
        "kernel" => render_kernel(cfg),
        "uptime" => render_uptime(cfg),
        "memory" => render_memory(cfg),
        "shell" => render_shell(cfg),
        "custom" => render_custom(inst),
        "command" => render_command(inst),
        "colors" => render_colors(inst, cfg),
        "datetime" => render_datetime(cfg),
        "loadavg" => render_loadavg(cfg),
        "processes" => render_processes(cfg),
        "locale" => render_locale(cfg),
        "swap" => render_swap(cfg),
        "wm" => render_wm(cfg),
        "de" => render_de(cfg),
        "initsystem" => render_initsystem(cfg),
        "lm" => render_lm(cfg),
        "terminal" => render_terminal(cfg),
        "terminalfont" => render_terminal_font(cfg),
        "packages" => render_packages(inst, cfg),
        "board" => render_board(cfg),
        "host" => render_host(cfg),
        "cpu" => render_cpu(inst),
        "gpu" => render_gpu(cfg),
        "display" => render_display(cfg),
        "disk" => render_disk(inst, cfg),
        "battery" => render_battery(cfg),
        "users" => render_users(cfg),
        "brightness" => render_brightness(cfg),
        "dns" => render_dns(cfg),
        "localip" => render_localip(cfg),
        "wifi" => render_wifi(cfg),
        "publicip" => render_publicip(inst, cfg),
        "theme" => render_theme(cfg, "theme"),
        "icons" => render_theme(cfg, "icons"),
        "cursor" => render_theme(cfg, "cursor"),
        "font" => render_theme(cfg, "font"),
        "break" => Some(ModuleOutput::blank()),
        "separator" => None,
        _ => None,
    }
}

fn render_custom(_inst: &ModuleInstance) -> Option<ModuleOutput> {

    Some(ModuleOutput::supported("", vec![String::new()]))
}

fn render_command(inst: &ModuleInstance) -> Option<ModuleOutput> {
    use crate::config::json::JsonValue;
    let raw = inst.raw.as_ref()?;
    let text = match raw {
        JsonValue::Obj(m) => m
            .iter()
            .find(|(k, _)| k == "text")
            .and_then(|(_, v)| v.as_str())
            .map(|s| s.to_string()),
        _ => None,
    }?;

    let out = std::process::Command::new("/bin/sh")
        .arg("-c")
        .arg(&text)
        .output()
        .ok()?;
    let stdout = String::from_utf8_lossy(&out.stdout);
    let value = stdout.trim().to_string();
    if value.is_empty() {
        return None;
    }
    Some(ModuleOutput::supported("", vec![value]))
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum ColorsSymbol {
    Block,
    Background,
    Circle,
    Diamond,
    Triangle,
    Square,
    Star,
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum ColorsBrightness {
    Default,
    Normal,
    Light,
}

#[derive(Debug, Clone, Copy)]
struct ColorsOpts {
    symbol: ColorsSymbol,
    pad: usize,
    width: usize,
    range: (u8, u8),
    brightness: ColorsBrightness,
}

impl Default for ColorsOpts {
    fn default() -> Self {
        ColorsOpts {
            symbol: ColorsSymbol::Background,
            pad: 0,
            width: 3,
            range: (0, 15),
            brightness: ColorsBrightness::Default,
        }
    }
}

fn render_colors(inst: &ModuleInstance, cfg: &Config) -> Option<ModuleOutput> {
    let opts = ColorsOpts::from_module(inst, cfg);
    let rows = colors_rows(&opts);
    if rows.is_empty() {
        return None;
    }
    Some(ModuleOutput::supported("", rows))
}

fn colors_rows(opts: &ColorsOpts) -> Vec<String> {
    let pad = " ".repeat(opts.pad);
    match opts.symbol {
        ColorsSymbol::Block | ColorsSymbol::Background => {
            let mut rows = Vec::new();
            if opts.brightness != ColorsBrightness::Light {
                let mut row = String::new();
                for i in opts.range.0..=opts.range.1.min(7) {
                    if opts.symbol == ColorsSymbol::Block {
                        row.push_str(&format!("\x1b[3{i}m"));
                        row.push_str(&"█".repeat(opts.width));
                    } else {
                        row.push_str(&format!("\x1b[4{i}m"));
                        row.push_str(&" ".repeat(opts.width));
                    }
                }
                if !row.is_empty() {
                    row.push_str("\x1b[m");
                    rows.push(format!("{pad}{row}"));
                }
            }
            if opts.brightness != ColorsBrightness::Normal {
                let mut row = String::new();
                if opts.symbol == ColorsSymbol::Background && needs_linux_console_blink() {
                    row.push_str("\x1b[5m");
                }
                for i in opts.range.0.max(8)..=opts.range.1 {
                    if opts.symbol == ColorsSymbol::Block {
                        row.push_str(&format!("\x1b[9{}m", i - 8));
                        row.push_str(&"█".repeat(opts.width));
                    } else {
                        row.push_str(&format!("\x1b[10{}m", i - 8));
                        row.push_str(&" ".repeat(opts.width));
                    }
                }
                let bare = row.replace("\x1b[5m", "");
                if !bare.is_empty() {
                    row.push_str("\x1b[m");
                    rows.push(format!("{pad}{row}"));
                }
            }
            rows
        }
        _ => {
            let glyph = match opts.symbol {
                ColorsSymbol::Circle => "● ",
                ColorsSymbol::Diamond => "◆ ",
                ColorsSymbol::Triangle => "▲ ",
                ColorsSymbol::Square => "■ ",
                ColorsSymbol::Star => "★ ",
                _ => "███ ",
            };
            let mut row = String::new();
            if opts.brightness == ColorsBrightness::Default {
                for i in (1..=8).rev() {
                    row.push_str(&format!("\x1b[38;5;{i}m{glyph}"));
                }
            } else {
                let prefix = if opts.brightness == ColorsBrightness::Normal { '3' } else { '9' };
                for i in 0..=7 {
                    row.push_str(&format!("\x1b[{prefix}{i}m{glyph}"));
                }
            }
            while row.ends_with(' ') {
                row.pop();
            }
            row.push_str("\x1b[m");
            vec![format!("{pad}{row}")]
        }
    }
}

fn needs_linux_console_blink() -> bool {
    match std::env::var("TERM") {
        Ok(t) => !t.starts_with("xterm"),
        Err(_) => true,
    }
}

impl ColorsOpts {
    fn from_module(inst: &ModuleInstance, cfg: &Config) -> Self {
        let mut opts = ColorsOpts::default();
        let get = |key: &str| -> Option<J> {
            inst.raw
                .as_ref()
                .and_then(|r| r.get(key))
                .or_else(|| cfg.module_options("colors").and_then(|o| o.get(key)))
                .cloned()
        };
        if let Some(v) = get("symbol").and_then(|v| v.as_str().map(|s| s.to_string())) {
            opts.symbol = match v.to_ascii_lowercase().as_str() {
                "block" => ColorsSymbol::Block,
                "background" => ColorsSymbol::Background,
                "circle" => ColorsSymbol::Circle,
                "diamond" => ColorsSymbol::Diamond,
                "triangle" => ColorsSymbol::Triangle,
                "square" => ColorsSymbol::Square,
                "star" => ColorsSymbol::Star,
                _ => ColorsSymbol::Background,
            };
        }
        if let Some(n) = get("paddingLeft").and_then(|v| v.as_u64()) {
            opts.pad = n.min(64) as usize;
        }
        if let Some(b) = get("block") {
            if let Some(w) = b.get("width").and_then(|v| v.as_u64()) {
                opts.width = w.clamp(1, 64) as usize;
            }
            if let Some(range) = b.get("range").and_then(|v| v.arr()) {
                if range.len() == 2 {
                    if let (Some(a), Some(b)) = (range[0].as_u64(), range[1].as_u64()) {
                        if a <= b && b <= 15 {
                            opts.range = (a as u8, b as u8);
                        }
                    }
                }
            }
        }
        if let Some(v) = get("brightness").and_then(|v| v.as_str().map(|s| s.to_string())) {
            opts.brightness = match v.to_ascii_lowercase().as_str() {
                "normal" => ColorsBrightness::Normal,
                "light" => ColorsBrightness::Light,
                _ => ColorsBrightness::Default,
            };
        }
        opts
    }
}

fn render_datetime(cfg: &Config) -> Option<ModuleOutput> {
    use std::os::unix::io::AsRawFd;
    let t = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .ok()?
        .as_secs() as i64;
    let mut tm = std::mem::MaybeUninit::<libc::tm>::uninit();

    let _fd = std::io::stdout().as_raw_fd();
    let tm = unsafe {
        let p = libc::localtime_r(&t, tm.as_mut_ptr());
        if p.is_null() {
            return None;
        }
        tm.assume_init()
    };
    let text = format!(
        "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec
    );
    Some(render_single("DateTime", text, cfg))
}

fn render_loadavg(cfg: &Config) -> Option<ModuleOutput> {
    let la = crate::detection::read_file("/proc/loadavg")?;
    let vals: Vec<String> = la
        .split_whitespace()
        .take(3)
        .map(|s| s.to_string())
        .collect();
    if vals.len() < 3 {
        return None;
    }
    Some(render_single(
        "Loadavg",
        format!("{} {} {}", vals[0], vals[1], vals[2]),
        cfg,
    ))
}

fn render_processes(cfg: &Config) -> Option<ModuleOutput> {
    let n = std::fs::read_dir("/proc")
        .ok()?
        .filter_map(|e| e.ok())
        .filter(|e| e.file_name().to_string_lossy().chars().all(|c| c.is_ascii_digit()))
        .count();
    Some(render_single("Processes", n.to_string(), cfg))
}

fn render_locale(cfg: &Config) -> Option<ModuleOutput> {
    let l = crate::detection::getenv("LC_ALL")
        .or_else(|| crate::detection::getenv("LC_CTYPE"))
        .or_else(|| crate::detection::getenv("LANG"))?;
    if l.is_empty() {
        return None;
    }
    Some(render_single("Locale", l, cfg))
}

fn render_swap(cfg: &Config) -> Option<ModuleOutput> {
    let m = crate::detection::memory::detect();
    if m.swap_total == 0 {
        return None;
    }
    let pct = common::percent(m.swap_used, m.swap_total).unwrap_or(0);
    let text = format!(
        "{} / {} {}",
        common::format_bytes(m.swap_used, ""),
        common::format_bytes(m.swap_total, ""),
        pct_colored(pct)
    );
    Some(render_single("Swap", text, cfg))
}

fn render_title(cfg: &Config) -> Option<ModuleOutput> {
    let u = crate::detection::user::detect();
    let colorize = |s: String| {
        match &cfg.display.title_color {
            Some(c) => match crate::print::color::color_code_to_ansi(c) {
                crate::print::color::ApplyResult::Ansi { start, end } => {
                    format!("{}{}{}", start, s, end)
                }
                _ => s,
            },
            None => s,
        }
    };
    let title = format!(
        "{}@{}",
        colorize(u.user_name_part),
        colorize(u.host_name_part)
    );
    Some(ModuleOutput::supported("", vec![title]))
}

fn render_os(cfg: &Config) -> Option<ModuleOutput> {
    let os = crate::detection::os::detect();
    if os.name.is_empty() {
        return None;
    }
    let value = if os.version.is_empty() {
        format!("{} {}", os.name, os.arch)
    } else {
        format!("{} {} {}", os.name, os.version, os.arch)
    };
    Some(render_single("OS", value, cfg))
}

fn render_kernel(cfg: &Config) -> Option<ModuleOutput> {
    let k = crate::detection::kernel::detect();
    if k.release.is_empty() {
        return None;
    }
    Some(render_single(
        "Kernel",
        format!("Linux {}", k.release.trim()),
        cfg,
    ))
}

fn render_uptime(cfg: &Config) -> Option<ModuleOutput> {
    let u = crate::detection::uptime::detect();
    if u.uptime_secs == 0 {
        return None;
    }
    let text = common::format_uptime(u.uptime_secs);
    Some(render_single("Uptime", text, cfg))
}

fn render_memory(cfg: &Config) -> Option<ModuleOutput> {
    use crate::detection::memory::detect;
    let m = detect();
    if m.mem_total == 0 {
        return None;
    }
    let pct = common::percent(m.mem_used, m.mem_total).unwrap_or(0);
    let text = format!(
        "{} / {} {}",
        common::format_bytes(m.mem_used, ""),
        common::format_bytes(m.mem_total, ""),
        pct_colored(pct)
    );
    Some(render_single("Memory", text, cfg))
}

fn render_shell(cfg: &Config) -> Option<ModuleOutput> {
    let s = crate::detection::shell::detect();
    if s.shell_path.is_empty() {
        return None;
    }
    let text = if s.shell_version.is_empty() {
        s.shell_base_name.clone()
    } else {
        format!("{} {}", s.shell_base_name, s.shell_version)
    };
    Some(render_single("Shell", text, cfg))
}

fn render_wm(cfg: &Config) -> Option<ModuleOutput> {
    let w = crate::detection::wm::detect();
    if w.name.is_empty() {
        return None;
    }
    let mut value = w.name.clone();
    if !w.version.is_empty() {
        value.push(' ');
        value.push_str(&w.version);
    }
    if !w.session_type.is_empty() {
        value.push_str(&format!(" ({})", w.session_type));
    }
    Some(render_single("WM", value, cfg))
}

fn render_initsystem(cfg: &Config) -> Option<ModuleOutput> {
    let i = crate::detection::initsystem::detect();
    if i.name.is_empty() {
        return None;
    }
    let value = if i.version.is_empty() {
        i.name
    } else {
        format!("{} {}", i.name, i.version)
    };
    Some(render_single("Init System", value, cfg))
}

fn render_lm(cfg: &Config) -> Option<ModuleOutput> {
    let m = crate::detection::lm::detect();
    if m.name.is_empty() {
        return None;
    }
    let value = if m.version.is_empty() {
        m.name
    } else if m.version.to_ascii_lowercase().contains(&m.name.to_ascii_lowercase()) {
        m.version
    } else {
        format!("{} {}", m.name, m.version)
    };
    Some(render_single("LM", value, cfg))
}

fn render_de(cfg: &Config) -> Option<ModuleOutput> {
    let d = crate::detection::de::detect();
    if d.name.is_empty() {
        return None;
    }

    let wm = crate::detection::wm::detect();
    if !wm.name.is_empty() && d.name.eq_ignore_ascii_case(&wm.name) {
        return None;
    }
    Some(render_single("DE", d.name, cfg))
}

fn render_terminal(cfg: &Config) -> Option<ModuleOutput> {
    let t = crate::detection::terminal::detect();
    if t.name.is_empty() {
        return None;
    }

    let value = if !t.exe.is_empty() {
        t.exe
    } else if t.version.is_empty() {
        t.name
    } else {
        format!("{} {}", t.name, t.version)
    };
    Some(render_single("Terminal", value, cfg))
}

fn render_terminal_font(cfg: &Config) -> Option<ModuleOutput> {
    let t = crate::detection::terminal::detect();
    if t.font.is_empty() {
        return None;
    }
    Some(render_single("Terminal Font", t.font, cfg))
}

fn render_packages(inst: &ModuleInstance, cfg: &Config) -> Option<ModuleOutput> {
    let p = crate::detection::packages::detect();
    if p.amounts.is_empty() {
        return None;
    }
    let combined = matches!(
        inst.raw.as_ref().and_then(|r| r.get("combined")),
        Some(J::Bool(true))
    ) || matches!(
        cfg.module_options("packages").and_then(|o| o.get("combined")),
        Some(J::Bool(true))
    );
    if let Some(fmt) = packages_format(inst, cfg) {
        if let Some(expanded) = expand_packages_template(&fmt, &p.amounts) {
            return Some(ModuleOutput::supported("Packages", vec![expanded]));
        }
    }
    Some(ModuleOutput::supported(
        "Packages",
        format_packages(&p.amounts, combined),
    ))
}

fn packages_format(inst: &ModuleInstance, cfg: &Config) -> Option<String> {
    if let Some(f) = &inst.args.format {
        return Some(f.clone());
    }
    cfg.module_options("packages")
        .and_then(|o| o.get("format"))
        .and_then(|v| v.as_str())
        .map(|s| s.to_string())
}

pub fn packages_owns_format(fmt: &str) -> bool {
    fmt.contains("{all}")
        || [
            "{flatpak-system}",
            "{flatpak-user}",
            "{nix-system}",
            "{nix-user}",
            "{nix-default}",
            "{nix}",
        ]
        .iter()
        .any(|k| fmt.contains(k))
}

fn expand_packages_template(fmt: &str, amounts: &[(String, usize)]) -> Option<String> {
    let mut out = fmt.to_string();
    let mut touched = false;
    if out.contains("{all}") {
        let total: usize = amounts.iter().map(|(_, n)| n).sum();
        out = out.replace("{all}", &total.to_string());
        touched = true;
    }
    for (name, n) in amounts {
        let key = format!("{{{}}}", name);
        if out.contains(&key) {
            out = out.replace(&key, &n.to_string());
            touched = true;
        }
    }
    touched.then_some(out)
}

fn format_packages(amounts: &[(String, usize)], combined: bool) -> Vec<String> {
    if combined {
        let total: usize = amounts.iter().map(|(_, n)| n).sum();
        vec![total.to_string()]
    } else {
        vec![amounts
            .iter()
            .map(|(name, n)| format!("{} ({})", n, name.to_lowercase()))
            .collect::<Vec<_>>()
            .join(", ")]
    }
}

fn render_board(_cfg: &Config) -> Option<ModuleOutput> {
    let b = crate::detection::board::detect();
    if b.name.is_empty() {
        return None;
    }
    let value = if b.version.is_empty() {
        b.name.clone()
    } else {
        format!("{} ({})", b.name, b.version)
    };
    Some(render_single("Board", value, _cfg))
}

fn render_host(_cfg: &Config) -> Option<ModuleOutput> {
    let name = crate::detection::read_file("/sys/class/dmi/id/product_name")
        .map(|s| s.trim().to_string())
        .unwrap_or_default();

    let is_generic = |s: &str| {
        let l = s.to_ascii_lowercase();
        l == "system product name"
            || l == "to be filled by o.e.m."
            || l == "default string"
            || l == "system name"
            || l == "invalid"
            || l.contains("to be filled")
            || l.contains("default string")
    };
    if name.is_empty() || is_generic(&name) {
        return None;
    }
    Some(render_single("Host", name, _cfg))
}

fn render_cpu(inst: &ModuleInstance) -> Option<ModuleOutput> {
    use crate::config::json::JsonValue;
    let c = crate::detection::cpu::detect();
    if c.model.is_empty() {
        return None;
    }

    let mut model = c.model.clone();

    if let Some(stripped) = model.strip_suffix(" with Radeon Graphics") {
        model = stripped.to_string();
    }

    let cores = if c.logical_cores > 0 { c.logical_cores } else { c.physical_cores };
    let show_pe = matches!(
        inst.raw.as_ref().and_then(|r| r.get("showPeCoreCount")),
        Some(JsonValue::Bool(true))
    );

    let mut value: String;
    if show_pe {
        if let (Some(p), Some(e)) = (c.pe_cores, c.ee_cores) {
            if e > 0 {
                value = format!("{} ({}P + {}E)", model, p, e);
            } else {
                value = format!("{} ({}P)", model, p);
            }
        } else {
            value = format!("{} ({})", model, cores);
        }
    } else {
        value = format!("{} ({})", model, cores);
    }

    let freq = if c.freq_max_mhz > 0 {
        c.freq_max_mhz
    } else {
        c.freq_cur_mhz
    };
    if freq > 0 {
        value = format!("{} @ {:.2} GHz", value, freq as f64 / 1000.0);
    }

    Some(render_single("CPU", value, &Config::default()))
}

fn render_gpu(_cfg: &Config) -> Option<ModuleOutput> {
    let gpus = crate::detection::gpu::detect();
    if gpus.is_empty() {
        return None;
    }
    let mut values = Vec::new();
    for g in &gpus {
        let v = if g.dtype.is_empty() {
            g.model.clone()
        } else {
            format!("{} [{}]", g.model, g.dtype)
        };
        values.push(v);
    }
    Some(ModuleOutput::supported("GPU", values))
}

fn render_display(_cfg: &Config) -> Option<ModuleOutput> {
    let ds = crate::detection::display::detect();
    if ds.is_empty() {
        return None;
    }
    let mut values = Vec::new();
    for d in &ds {
        let mut v = format!("{}x{}", d.width, d.height);
        if d.size_in > 0 {
            v.push_str(&format!(" in {}\"", d.size_in));
        }
        if d.refresh_rate > 0 {
            v.push_str(&format!(", {} Hz", d.refresh_rate));
        }
        if !d.dtype.is_empty() {
            v.push_str(&format!(" [{}]", d.dtype));
        }
        values.push(v);
    }
    Some(ModuleOutput::supported("Display", values))
}

fn pct_colored(pct_val: u8) -> String {
    let pct_s = pct_val.to_string();
    let color = if pct_val <= 50 {
        "green"
    } else if pct_val <= 80 {
        "bright_yellow"
    } else {
        "bright_red"
    };
    match crate::print::color::color_code_to_ansi(color) {
        crate::print::color::ApplyResult::Ansi { start, end } => {
            format!("({}{}%{})", start, pct_s, end)
        }
        _ => format!("({}%)", pct_s),
    }
}

fn render_disk(inst: &ModuleInstance, _cfg: &Config) -> Option<ModuleOutput> {
    use crate::config::json::JsonValue;
    let folders: Vec<String> = if let Some(raw) = &inst.raw {
        if let JsonValue::Obj(m) = raw {
            m.iter()
                .filter(|(k, _)| k == "folders")
                .filter_map(|(_, v)| v.as_str().map(|s| s.to_string()))
                .collect()
        } else {
            Vec::new()
        }
    } else {
        Vec::new()
    };
    let disks = crate::detection::disk::detect(&folders);
    if disks.is_empty() {
        return None;
    }
    let mut values = Vec::new();
    for d in disks.iter() {
        let fs = &d.filesystem;
        let used = common::format_bytes(d.used, "");
        let total = common::format_bytes(d.total, "");
        let pct_val = common::percent(d.used, d.total).unwrap_or(0);
        let mut v = format!("{} / {} {}", used, total, pct_colored(pct_val));
        if !fs.is_empty() {
            v.push_str(&format!(" - {}", fs));
        }
        values.push(v);
    }
    let mut out = ModuleOutput::supported("Disk", values);
    out.repeat_key = true;
    Some(out)
}

fn render_battery(_cfg: &Config) -> Option<ModuleOutput> {
    let bats = crate::detection::battery::detect();
    if bats.is_empty() {
        return None;
    }
    let mut values = Vec::new();
    for b in &bats {
        let mut parts = Vec::new();
        if !b.model.is_empty() {
            parts.push(b.model.clone());
        }
        if b.energy_full > 0.0 && b.energy_now > 0.0 {
            parts.push(format!("{:.2} Wh / {:.2} Wh", b.energy_now, b.energy_full));
        }
        parts.push(format!("{}%", b.capacity_percent));
        if !b.status.is_empty() {
            parts.push(b.status.clone());
        }
        values.push(parts.join(", "));
    }
    Some(ModuleOutput::supported("Battery", values))
}

fn render_users(_cfg: &Config) -> Option<ModuleOutput> {
    let users = crate::detection::users::detect();
    if users.is_empty() {
        return None;
    }
    let mut values = Vec::new();
    for u in &users {
        let mut v = u.user.clone();
        if !u.tty.is_empty() {
            v.push_str(&format!(" ({})", u.tty));
        }
        if !u.host.is_empty() {
            v.push_str(&format!(" | {}", u.host));
        }
        values.push(v);
    }
    Some(ModuleOutput::supported("Users", values))
}

fn render_brightness(_cfg: &Config) -> Option<ModuleOutput> {
    let bs = crate::detection::brightness::detect();
    if bs.is_empty() {
        return None;
    }
    let b = &bs[0];
    let bar = common::percent_bar(b.value as u64, b.max as u64);
    Some(ModuleOutput::supported(
        "Brightness",
        vec![format!("{}% {}", b.percentage, bar)],
    ))
}

fn render_dns(_cfg: &Config) -> Option<ModuleOutput> {
    let d = crate::detection::dns::detect()?;
    let mut values = Vec::new();
    for s in d.servers {
        values.push(s);
    }
    Some(ModuleOutput::supported("DNS", values))
}

fn render_localip(_cfg: &Config) -> Option<ModuleOutput> {
    let ips = crate::detection::localip::detect();
    if ips.is_empty() {
        return None;
    }
    let mut list = ips;
    if let Some(src) = crate::detection::localip::outbound_src_ip() {
        if let Some(pos) = list
            .iter()
            .position(|i| i.ipv4.iter().any(|a| a == &src))
        {
            let only = list.swap_remove(pos);
            list = vec![only];
        }
    }
    if list.len() > 1 {
        if let Some(def) = crate::detection::localip::default_iface() {
            if let Some(pos) = list.iter().position(|i| i.name == def) {
                let only = list.swap_remove(pos);
                list = vec![only];
            }
        }
    }
    if list.len() > 1 {
        let filtered: Vec<_> = std::mem::take(&mut list)
            .into_iter()
            .filter(|i| !crate::detection::localip::is_virtual(&i.name))
            .collect();
        if !filtered.is_empty() {
            list = filtered;
        }
    }
    let mut values = Vec::new();
    if let Some(i) = list.into_iter().next() {
        let (key, value) = localip_line(&i);
        if value.is_empty() {
            return None;
        }
        values.push(value);
        return Some(ModuleOutput::supported(&key, values));
    }
    None
}

fn localip_line(i: &crate::detection::localip::IpInfo) -> (String, String) {
    let key = format!("Local IP ({})", i.name);
    if let (Some(ip), Some(prefix)) = (i.ipv4.first(), i.prefix4.first()) {
        return (key, format!("{}/{}", ip, prefix));
    }
    if let (Some(ip), Some(prefix)) = (i.ipv6.first(), i.prefix6.first()) {
        return (key, format!("{}/{}", ip, prefix));
    }
    (key, String::new())
}

fn render_wifi(_cfg: &Config) -> Option<ModuleOutput> {
    let ws = crate::detection::wifi::detect();
    if ws.is_empty() {
        return None;
    }
    let mut values = Vec::new();
    for w in ws {
        let mut v = format!("{} {}", w.protocol, w.name);
        if !w.ssid.is_empty() {
            v.push_str(&format!(" ({})", w.ssid));
        }
        if w.signal_quality > 0 {
            v.push_str(&format!(" - {}%", w.signal_quality));
        }
        values.push(v);
    }
    Some(ModuleOutput::supported("WiFi", values))
}

fn render_publicip(inst: &ModuleInstance, _cfg: &Config) -> Option<ModuleOutput> {
    use crate::config::json::JsonValue;
    let mut timeout_ms = 1000u128;
    if let Some(raw) = &inst.raw {
        if let JsonValue::Obj(m) = raw {
            for (k, v) in m {
                if k == "timeout" {
                    if let Some(t) = v.as_u64() {
                        timeout_ms = t.max(100) as u128;
                    }
                }
            }
        }
    }
    let ip = crate::detection::publicip::detect(timeout_ms)?;
    Some(ModuleOutput::supported("Public IP", vec![ip]))
}

fn render_theme(_cfg: &Config, which: &str) -> Option<ModuleOutput> {
    let t = crate::detection::theme::detect();
    let mut value = String::new();
    let mut key = "Theme";
    match which {
        "theme" => {
            value = t.gtk_theme;
            key = "Theme";
        }
        "icons" => {
            value = t.icon_theme;
            key = "Icons";
        }
        "cursor" => {
            value = t.cursor_theme;
            key = "Cursor";
        }
        "font" => {
            value = pretty_pango_font(&t.font);
            key = "Font";
        }
        _ => {}
    }
    if value.is_empty() {
        return None;
    }
    Some(render_single(key, value, _cfg))
}

fn pretty_pango_font(raw: &str) -> String {
    let raw = raw.trim();
    if let Some((name, size)) = raw.rsplit_once(' ') {
        if !name.is_empty() {
            if let Ok(n) = size.parse::<f64>() {
                if n > 0.0 {
                    let size = if n.fract() == 0.0 {
                        format!("{}", n as u64)
                    } else {
                        size.to_string()
                    };
                    return format!("{name} ({size}pt)");
                }
            }
        }
    }
    raw.to_string()
}

fn render_single(key: &str, value: String, _cfg: &Config) -> ModuleOutput {
    ModuleOutput::supported(key, vec![value])
}

use crate::config::json::JsonValue as J;

fn jobj(kv: Vec<(&str, J)>) -> J {
    J::Obj(kv.into_iter().map(|(k, v)| (k.to_string(), v)).collect())
}

pub fn json_type_name(name: &str) -> &'static str {
    match name {
        "os" => "OS",
        "kernel" => "Kernel",
        "wm" => "WM",
        "de" => "DE",
        "initsystem" => "InitSystem",
        "lm" => "LM",
        "terminal" => "Terminal",
        "terminalfont" => "TerminalFont",
        "shell" => "Shell",
        "packages" => "Packages",
        "wmtheme" => "WMTheme",
        "colors" => "Colors",
        "custom" => "Custom",
        "break" => "Break",
        "separator" => "Separator",
        "command" => "Command",
        "board" => "Board",
        "host" => "Host",
        "cpu" => "CPU",
        "gpu" => "GPU",
        "display" => "Display",
        "disk" => "Disk",
        "memory" => "Memory",
        "swap" => "Swap",
        "uptime" => "Uptime",
        "title" => "Title",
        "battery" => "Battery",
        "users" => "Users",
        "brightness" => "Brightness",
        "dns" => "DNS",
        "localip" => "LocalIp",
        "wifi" => "Wifi",
        "publicip" => "PublicIp",
        "theme" => "Theme",
        "icons" => "Icons",
        "cursor" => "Cursor",
        "font" => "Font",
        _ => "Unknown",
    }
}

pub fn json_error(name: &str, _inst: &ModuleInstance, _cfg: &Config) -> Option<String> {
    match name {

        "custom" | "colors" | "break" | "separator" => {
            Some("Unsupported for JSON format".to_string())
        }

        "wmtheme" => {
            let wm = crate::detection::wm::detect();
            Some(format!("Unknown WM: {}", wm.name))
        }
        "terminalfont" => {
            let t = crate::detection::terminal::detect();
            Some(format!("Unknown terminal: {}", t.name))
        }
        _ => None,
    }
}

pub fn json_result(name: &str, inst: &ModuleInstance, _cfg: &Config) -> Option<J> {

    if json_error(name, inst, _cfg).is_some() {
        return None;
    }
    match name {
        "title" => None,
        "os" => {
            let o = crate::detection::os::detect();
            Some(jobj(vec![
                ("buildID", J::Str(o.build_id)),
                ("codename", J::Str(o.codename)),
                ("id", J::Str(o.id)),
                ("idLike", J::Str(o.id_like)),
                ("name", J::Str(o.name)),
                ("prettyName", J::Str(o.pretty_name)),
                ("variant", J::Str(o.variant)),
                ("variantID", J::Str(o.variant_id)),
                ("version", J::Str(o.version)),
                ("versionID", J::Str(o.version_id)),
            ]))
        }
        "kernel" => {
            let k = crate::detection::kernel::detect();
            let page = unsafe { libc::sysconf(libc::_SC_PAGESIZE) };
            Some(jobj(vec![
                ("architecture", J::Str(crate::detection::os::arch())),
                ("name", J::Str(k.sysname)),
                ("release", J::Str(k.release)),
                ("version", J::Str(k.version)),
                ("pageSize", J::Int(page)),
            ]))
        }
        "wm" => {
            let w = crate::detection::wm::detect();
            if w.name.is_empty() {
                return None;
            }
            Some(jobj(vec![
                ("processName", J::Str(w.name.clone())),
                ("prettyName", J::Str(w.name)),
                ("protocolName", J::Str(w.session_type)),
                ("pluginName", J::Str(String::new())),
                ("version", J::Str(String::new())),
            ]))
        }
        "de" => {
            let d = crate::detection::de::detect();
            if d.name.is_empty() {
                return None;
            }
            Some(jobj(vec![
                ("processName", J::Str(d.name.clone())),
                ("prettyName", J::Str(d.name)),
                ("protocolName", J::Str(String::new())),
                ("pluginName", J::Str(String::new())),
                ("version", J::Str(String::new())),
            ]))
        }
        "initsystem" => {
            let i = crate::detection::initsystem::detect();
            if i.name.is_empty() {
                return None;
            }
            Some(jobj(vec![
                ("name", J::Str(i.name)),
                ("version", J::Str(i.version)),
            ]))
        }
        "lm" => {
            let m = crate::detection::lm::detect();
            if m.name.is_empty() {
                return None;
            }
            Some(jobj(vec![
                ("name", J::Str(m.name)),
                ("version", J::Str(m.version)),
            ]))
        }
        "terminal" => {
            let t = crate::detection::terminal::detect();
            if t.name.is_empty() {
                return None;
            }
            let s = t.name.to_ascii_lowercase();
            let proc = crate::detection::proc_by_comm(&[&s]);
            let exe = proc
                .as_ref()
                .map(|p| p.cmdline.clone())
                .unwrap_or_default();
            let exe_path = proc
                .as_ref()
                .map(|p| p.exe_path.clone())
                .unwrap_or_default();
            let exe_name = if exe_path.is_empty() {
                t.name.clone()
            } else {
                exe_path.rsplit('/').next().unwrap_or("").to_string()
            };
            let pid = proc
                .as_ref()
                .map(|p| J::Uint(p.pid as u64))
                .unwrap_or(J::Null);
            let ppid = proc
                .as_ref()
                .map(|p| J::Uint(p.ppid as u64))
                .unwrap_or(J::Null);
            Some(jobj(vec![
                ("processName", J::Str(t.name.clone())),
                ("exe", J::Str(exe)),
                ("exeName", J::Str(exe_name)),
                ("exePath", J::Str(exe_path)),
                ("pid", pid),
                ("ppid", ppid),
                ("prettyName", J::Str(t.name.clone())),
                ("version", J::Str(t.version.clone())),
                ("tty", J::Str(String::new())),
            ]))
        }
        "shell" => {
            let s = crate::detection::shell::detect();
            if s.shell_name.is_empty() {
                return None;
            }
            let base = s.shell_base_name.clone();
            let path = s.shell_path.clone();
            Some(jobj(vec![
                ("exe", J::Str(base.clone())),
                ("exeName", J::Str(base.clone())),
                ("exePath", J::Str(path)),
                ("pid", J::Null),
                ("ppid", J::Null),
                ("processName", J::Str(base.clone())),
                ("prettyName", J::Str(base)),
                ("version", J::Str(s.shell_version)),
                ("tty", J::Int(0)),
            ]))
        }
        "packages" => {
            let p = crate::detection::packages::detect();
            if p.amounts.is_empty() {
                return None;
            }
            let mut all = 0usize;
            let mut nix_system = 0usize;
            let mut nix_user = 0usize;
            let mut flatpak_system = 0usize;
            let mut flatpak_user = 0usize;
            for (k, v) in &p.amounts {
                all += *v;
                match k.as_str() {
                    "nix" => nix_system = *v,
                    "nix-user" => nix_user = *v,
                    "flatpak" => flatpak_system = *v,
                    "flatpak-user" => flatpak_user = *v,
                    _ => {}
                }
            }
            Some(jobj(vec![
                ("all", J::Uint(all as u64)),
                ("flatpakSystem", J::Uint(flatpak_system as u64)),
                ("flatpakUser", J::Uint(flatpak_user as u64)),
                ("nixSystem", J::Uint(nix_system as u64)),
                ("nixUser", J::Uint(nix_user as u64)),
            ]))
        }
        "board" => {
            let b = crate::detection::board::detect();
            if b.name.is_empty() {
                return None;
            }
            let serial = crate::detection::read_file("/sys/class/dmi/id/board_serial")
                .unwrap_or_default()
                .trim()
                .to_string();
            Some(jobj(vec![
                ("name", J::Str(b.name)),
                ("vendor", J::Str(b.vendor)),
                ("version", J::Str(b.version)),
                ("serial", J::Str(serial)),
            ]))
        }
        "host" => {
            let name = crate::detection::read_file("/sys/class/dmi/id/product_name")
                .unwrap_or_default()
                .trim()
                .to_string();
            if name.is_empty() {
                return None;
            }
            Some(jobj(vec![("name", J::Str(name))]))
        }
        "cpu" => {
            let c = crate::detection::cpu::detect();
            if c.physical_cores == 0 {
                return None;
            }
            let base = if c.freq_cur_mhz != 0 {
                c.freq_cur_mhz
            } else {
                c.freq_max_mhz
            };
            let core_types = if let (Some(pe), Some(ee)) = (c.pe_cores, c.ee_cores) {
                J::Arr(vec![
                    jobj(vec![
                        ("count", J::Uint(pe as u64)),
                        ("freq", J::Uint(c.freq_max_mhz)),
                    ]),
                    jobj(vec![
                        ("count", J::Uint(ee as u64)),
                        ("freq", J::Uint(c.freq_max_mhz)),
                    ]),
                ])
            } else {
                J::Arr(vec![jobj(vec![
                    ("count", J::Uint(c.logical_cores as u64)),
                    ("freq", J::Uint(c.freq_max_mhz)),
                ])])
            };
            let march = crate::detection::cpu::march().map(J::Str).unwrap_or(J::Null);
            Some(jobj(vec![
                ("cpu", J::Str(c.model.clone())),
                ("vendor", J::Str(c.vendor)),
                ("packages", J::Uint(c.packages as u64)),
                (
                    "cores",
                    jobj(vec![
                        ("physical", J::Uint(c.physical_cores as u64)),
                        ("logical", J::Uint(c.logical_cores as u64)),
                        ("online", J::Uint(c.logical_cores as u64)),
                    ]),
                ),
                (
                    "frequency",
                    jobj(vec![
                        ("base", J::Uint(base)),
                        ("max", J::Uint(c.freq_max_mhz)),
                    ]),
                ),
                ("coreTypes", core_types),
                ("temperature", J::Null),
                ("march", march),
                ("numaNodes", J::Uint(crate::detection::cpu::numa_nodes())),
                ("codeName", J::Null),
                ("technology", J::Null),
            ]))
        }
        "gpu" => {
            let gpus = crate::detection::gpu::detect();
            if gpus.is_empty() {
                return None;
            }
            Some(J::Arr(gpus.into_iter().map(|g| {
                let devid = g.device_id.parse::<u32>().ok().map(|n| J::Uint(n as u64)).unwrap_or(J::Null);
                jobj(vec![
                    ("index", J::Null),
                    ("coreCount", J::Null),
                    ("coreUsage", J::Null),
                    (
                        "memory",
                        jobj(vec![
                            (
                                "dedicated",
                                jobj(vec![("total", J::Null), ("used", J::Null)]),
                            ),
                            (
                                "shared",
                                jobj(vec![("total", J::Null), ("used", J::Null)]),
                            ),
                            ("type", J::Null),
                        ]),
                    ),
                    ("driver", J::Str(g.driver)),
                    ("name", J::Str(g.model)),
                    ("temperature", J::Null),
                    ("type", J::Null),
                    ("vendor", J::Str(g.vendor_name)),
                    ("platformApi", J::Str(String::new())),
                    ("frequency", J::Null),
                    ("deviceId", devid),
                    ("pcieSpeed", J::Null),
                ])
            }).collect()))
        }
        "display" => {
            let ds = crate::detection::display::detect();
            if ds.is_empty() {
                return None;
            }
            Some(J::Arr(ds.into_iter().map(|d| {
                jobj(vec![
                    ("id", J::Null),
                    ("name", J::Str(d.name.clone())),
                    ("primary", J::Bool(false)),
                    (
                        "output",
                        jobj(vec![
                            ("width", J::Uint(d.width as u64)),
                            ("height", J::Uint(d.height as u64)),
                            ("refreshRate", J::Float(d.refresh_rate as f64)),
                            ("drrStatus", J::Null),
                            ("dpi", J::Uint(96)),
                        ]),
                    ),
                    (
                        "scaled",
                        jobj(vec![
                            ("width", J::Uint(d.width as u64)),
                            ("height", J::Uint(d.height as u64)),
                        ]),
                    ),
                    (
                        "preferred",
                        jobj(vec![
                            ("width", J::Uint(d.width as u64)),
                            ("height", J::Uint(d.height as u64)),
                            ("refreshRate", J::Float(60.0)),
                        ]),
                    ),
                    (
                        "physical",
                        jobj(vec![("width", J::Null), ("height", J::Null)]),
                    ),
                    ("rotation", J::Int(0)),
                    ("bitDepth", J::Null),
                    ("hdrStatus", J::Null),
                    ("type", J::Null),
                    (
                        "manufactureDate",
                        jobj(vec![("year", J::Null), ("week", J::Null)]),
                    ),
                    ("serial", J::Str(String::new())),
                    ("platformApi", J::Str(String::new())),
                ])
            }).collect()))
        }
        "disk" => {
            let disks = crate::detection::disk::detect(&[]);
            if disks.is_empty() {
                return None;
            }
            Some(J::Arr(disks.into_iter().map(|d| {
                let mut volume = vec![];
                if d.options.split(',').any(|o| o.starts_with("subvol=")) && d.mountpoint != "/" {
                    volume.push(J::Str("Subvolume".to_string()));
                }
                if d.options.split(',').any(|o| o == "ro") {
                    volume.push(J::Str("Read-only".to_string()));
                }
                if volume.is_empty() {
                    volume.push(J::Str("Regular".to_string()));
                }
                jobj(vec![
                    (
                        "bytes",
                        jobj(vec![
                            ("available", J::Uint(d.available)),
                            ("free", J::Uint(d.total.saturating_sub(d.used))),
                            ("total", J::Uint(d.total)),
                            ("used", J::Uint(d.used)),
                        ]),
                    ),
                    (
                        "files",
                        jobj(vec![("total", J::Null), ("used", J::Null)]),
                    ),
                    ("filesystem", J::Str(d.filesystem)),
                    ("mountpoint", J::Str(d.mountpoint)),
                    ("mountFrom", J::Str(d.mount_from)),
                    ("name", J::Str(d.name)),
                    ("volumeType", J::Arr(volume)),
                    ("createTime", J::Null),
                ])
            }).collect()))
        }
        "memory" => {
            let m = crate::detection::memory::detect();
            if m.mem_total == 0 {
                return None;
            }
            Some(jobj(vec![
                ("total", J::Uint(m.mem_total)),
                ("used", J::Uint(m.mem_used)),
            ]))
        }
        "swap" => {
            let swaps = proc_swaps();
            if swaps.is_empty() {
                return None;
            }
            Some(J::Arr(swaps.into_iter().map(|(n, u, t)| {
                jobj(vec![
                    ("name", J::Str(n)),
                    ("used", J::Uint(u)),
                    ("total", J::Uint(t)),
                ])
            }).collect()))
        }
        "uptime" => {
            let u = crate::detection::uptime::detect();
            if u.uptime_secs == 0 {
                return None;
            }

            Some(jobj(vec![
                ("uptime", J::Uint(u.uptime_secs * 1000)),
                ("bootTime", J::Str(boot_time_iso(u.boot_time_secs))),
            ]))
        }
        "command" => {
            let out = render_command(inst)?;
            out.values.into_iter().next().map(J::Str)
        }
        "battery" => {
            let bs = crate::detection::battery::detect();
            if bs.is_empty() {
                return None;
            }
            Some(J::Arr(bs.into_iter().map(|b| {
                jobj(vec![
                    ("capacity", J::Uint(b.capacity_percent as u64)),
                    ("maxCapacity", J::Uint(b.capacity_percent as u64)),
                    ("percentage", J::Uint(b.capacity_percent as u64)),
                    ("status", J::Str(b.status)),
                    ("manufacturer", J::Str(b.manufacturer)),
                    ("model", J::Str(b.model)),
                    ("energyNow", J::Float(b.energy_now)),
                    ("energyFull", J::Float(b.energy_full)),
                    ("temperature", J::Float(b.temp_c)),
                ])
            }).collect()))
        }
        "users" => {
            let us = crate::detection::users::detect();
            if us.is_empty() {
                return None;
            }
            Some(J::Arr(us.into_iter().map(|u| {
                jobj(vec![
                    ("userName", J::Str(u.user)),
                    ("hostName", J::Str(u.host)),
                    ("tty", J::Str(u.tty)),
                ])
            }).collect()))
        }
        "brightness" => {
            let bs = crate::detection::brightness::detect();
            if bs.is_empty() {
                return None;
            }
            let b = &bs[0];
            Some(jobj(vec![
                ("name", J::Str(b.name.clone())),
                ("value", J::Uint(b.value)),
                ("max", J::Uint(b.max)),
                ("percentage", J::Uint(b.percentage as u64)),
            ]))
        }
        "dns" => {
            let d = crate::detection::dns::detect()?;
            Some(J::Arr(
                d.servers.into_iter().map(J::Str).collect(),
            ))
        }
        "localip" => {
            let ips = crate::detection::localip::detect();
            if ips.is_empty() {
                return None;
            }
            Some(J::Arr(ips.into_iter().map(|i| {
                jobj(vec![
                    ("name", J::Str(i.name)),
                    ("mac", J::Str(i.mac)),
                    ("ipv4", J::Arr(i.ipv4.into_iter().map(J::Str).collect())),
                    ("ipv6", J::Arr(i.ipv6.into_iter().map(J::Str).collect())),
                    ("mtu", J::Uint(i.mtu)),
                    ("flags", J::Str(i.flags)),
                ])
            }).collect()))
        }
        "wifi" => {
            let ws = crate::detection::wifi::detect();
            if ws.is_empty() {
                return None;
            }
            Some(J::Arr(ws.into_iter().map(|w| {
                jobj(vec![
                    ("name", J::Str(w.name)),
                    ("ssid", J::Str(w.ssid)),
                    ("signalQuality", J::Uint(w.signal_quality as u64)),
                    ("protocol", J::Str(w.protocol)),
                ])
            }).collect()))
        }
        "publicip" => {
            let ip = crate::detection::publicip::detect(publicip_timeout(inst))?;
            Some(jobj(vec![("ip", J::Str(ip))]))
        }
        "theme" | "icons" | "cursor" | "font" => {
            let t = crate::detection::theme::detect();
            let value = match name {
                "theme" => t.gtk_theme,
                "icons" => t.icon_theme,
                "cursor" => t.cursor_theme,
                "font" => t.font,
                _ => String::new(),
            };
            if value.is_empty() {
                return None;
            }
            Some(jobj(vec![("name", J::Str(value))]))
        }
        _ => None,
    }
}

fn publicip_timeout(inst: &ModuleInstance) -> u128 {
    let mut timeout_ms = 1000u128;
    if let Some(J::Obj(m)) = &inst.raw {
        for (k, v) in m {
            if k == "timeout" {
                if let Some(t) = v.as_u64() {
                    timeout_ms = t.max(100) as u128;
                }
            }
        }
    }
    timeout_ms
}

fn proc_swaps() -> Vec<(String, u64, u64)> {
    let mut out = Vec::new();
    if let Some(text) = crate::detection::read_file("/proc/swaps") {
        for line in text.lines().skip(1) {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() < 5 {
                continue;
            }
            let name = parts[0].to_string();
            let total_kb: u64 = parts[2].parse().unwrap_or(0);
            let used_kb: u64 = parts[3].parse().unwrap_or(0);
            if total_kb > 0 {
                out.push((name, used_kb * 1024, total_kb * 1024));
            }
        }
    }
    out
}

fn boot_time_iso(secs: u64) -> String {
    let t = secs as i64;
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    unsafe {
        libc::localtime_r(&t, &mut tm);
    }
    format!(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.000{:}{:02}{:02}",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        if tm.tm_gmtoff >= 0 { '+' } else { '-' },
        (tm.tm_gmtoff / 3600).abs(),
        (tm.tm_gmtoff % 3600).abs() / 60,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn colors_default_matches_fastfetch() {
        let opts = ColorsOpts::default();
        let rows = colors_rows(&opts);
        assert_eq!(rows.len(), 2);
        let mut normal = String::new();
        for bg in 40..=47 {
            normal.push_str(&format!("\x1b[{bg}m   "));
        }
        normal.push_str("\x1b[m");
        assert_eq!(rows[0], normal);
        assert!(rows[1].ends_with("\x1b[m"));
        assert!(rows[1].contains("\x1b[100m   "));
        assert!(rows[1].contains("\x1b[107m   "));
    }

    #[test]
    fn colors_symbols_and_brightness() {
        let mut opts = ColorsOpts::default();
        opts.symbol = ColorsSymbol::Square;
        let rows = colors_rows(&opts);
        assert_eq!(rows.len(), 1);
        assert!(rows[0].starts_with("\x1b[38;5;8m■ "));
        assert!(rows[0].contains("\x1b[38;5;1m■\x1b[m"));
        opts.brightness = ColorsBrightness::Normal;
        let rows = colors_rows(&opts);
        assert!(rows[0].starts_with("\x1b[30m■ "));
        opts.brightness = ColorsBrightness::Light;
        opts.symbol = ColorsSymbol::Background;
        opts.range = (0, 7);
        let rows = colors_rows(&opts);
        assert!(rows.is_empty());
        opts.brightness = ColorsBrightness::Normal;
        let rows = colors_rows(&opts);
        assert_eq!(rows.len(), 1);
        assert!(rows[0].starts_with("\x1b[40m   "));
    }

    #[test]
    fn packages_template_expands() {
        assert_eq!(
            expand_packages_template("{all}", &sample_amounts()),
            Some("2206".to_string())
        );
        assert_eq!(
            expand_packages_template("{nix-system} sys", &sample_amounts()),
            Some("2199 sys".to_string())
        );
        assert_eq!(expand_packages_template("{value}", &sample_amounts()), None);
        assert!(packages_owns_format("{all}"));
        assert!(!packages_owns_format("{value}"));
    }

    fn sample_amounts() -> Vec<(String, usize)> {
        vec![
            ("flatpak-system".to_string(), 7),
            ("nix-system".to_string(), 2199),
        ]
    }

    #[test]
    fn packages_split_lists_managers() {
        assert_eq!(
            format_packages(&sample_amounts(), false),
            vec!["7 (flatpak-system), 2199 (nix-system)".to_string()]
        );
    }

    #[test]
    fn packages_combined_totals() {
        assert_eq!(
            format_packages(&sample_amounts(), true),
            vec!["2206".to_string()]
        );
        assert_eq!(format_packages(&[], true), vec!["0".to_string()]);
    }

    #[test]
    fn packages_combined_single_manager_shows_bare_number() {
        let one = vec![("nix-system".to_string(), 2199)];
        assert_eq!(format_packages(&one, true), vec!["2199".to_string()]);
        assert_eq!(
            format_packages(&one, false),
            vec!["2199 (nix-system)".to_string()]
        );
    }

    #[test]
    fn localip_line_matches_fastfetch() {
        let vpn = crate::detection::localip::IpInfo {
            name: "proton0".to_string(),
            ipv4: vec!["10.2.0.2".to_string()],
            prefix4: vec![32],
            ..Default::default()
        };
        assert_eq!(
            localip_line(&vpn),
            (
                "Local IP (proton0)".to_string(),
                "10.2.0.2/32".to_string()
            )
        );
        let lan = crate::detection::localip::IpInfo {
            name: "enp9s0".to_string(),
            ipv4: vec!["192.168.1.48".to_string()],
            prefix4: vec![24],
            ipv6: vec!["fe80::1".to_string()],
            prefix6: vec![64],
            ..Default::default()
        };
        assert_eq!(
            localip_line(&lan),
            (
                "Local IP (enp9s0)".to_string(),
                "192.168.1.48/24".to_string()
            )
        );
        let empty = crate::detection::localip::IpInfo {
            name: "eth0".to_string(),
            ..Default::default()
        };
        assert_eq!(
            localip_line(&empty),
            ("Local IP (eth0)".to_string(), String::new())
        );
    }
}

#[cfg(test)]
mod pango_tests {
    use super::pretty_pango_font;

    #[test]
    fn splits_pango_size() {
        assert_eq!(pretty_pango_font("DepartureMono Nerd Font 10"), "DepartureMono Nerd Font (10pt)");
        assert_eq!(pretty_pango_font("Inter 11.5"), "Inter (11.5pt)");
        assert_eq!(pretty_pango_font("monospace"), "monospace");
        assert_eq!(pretty_pango_font(""), "");
    }
}
