pub const RESET: &str = "\x1b[0m";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ApplyResult {

    Ansi { start: String, end: String },

    None,
}

pub fn color_code_to_ansi(color: &str) -> ApplyResult {
    if let Some(sgr) = named_color_sgr(color) {
        return ApplyResult::Ansi {
            start: sgr,
            end: RESET.to_string(),
        };
    }

    if let Some(num) = color.parse::<u16>().ok() {
        return ansi_from_fg(num);
    }

    if let Some(hex) = color.strip_prefix('#') {
        if hex.len() == 6 {
            if let Ok(v) = u32::from_str_radix(hex, 16) {
                let r = (v >> 16) & 0xff;
                let g = (v >> 8) & 0xff;
                let b = v & 0xff;
                return ApplyResult::Ansi {
                    start: format!("\x1b[38;2;{};{};{}m", r, g, b),
                    end: RESET.to_string(),
                };
            }
        }
    }

    ApplyResult::None
}

pub fn named_color_sgr(name: &str) -> Option<String> {
    let n = name.trim();
    if n.is_empty() || n == "reset" || n == "reset_default" || n == "#" {
        return Some(RESET.to_string());
    }
    if n == "default" || n == "fg_default" {
        return Some("\x1b[39m".to_string());
    }
    if n == "bg_default" {
        return Some("\x1b[49m".to_string());
    }

    let (style, base) = split_style(n);
    let mut codes: Vec<u8> = Vec::new();
    match style.as_str() {
        "bold" => codes.push(1),
        "italic" => codes.push(3),
        "underline" => codes.push(4),
        "invert" | "reverse" => codes.push(7),
        "dim" => codes.push(2),
        "strikethrough" => codes.push(9),
        _ => {}
    }

    let is_bg = base.starts_with("bg_") || base.starts_with("bg-");
    let base: &str = &base;
    let base = base
        .strip_prefix("bg_")
        .or_else(|| base.strip_prefix("bg-"))
        .or_else(|| base.strip_prefix("fg_"))
        .or_else(|| base.strip_prefix("fg-"))
        .unwrap_or(base);

    let base_escape = match base.to_ascii_lowercase().as_str() {
        "black" => 30,
        "red" => 31,
        "green" => 32,
        "yellow" => 33,
        "blue" => 34,
        "magenta" | "purple" => 35,
        "cyan" => 36,
        "white" => 37,
        "bright_black" | "bright-black" | "gray" | "grey" => 90,
        "bright_red" | "bright-red" => 91,
        "bright_green" | "bright-green" => 92,
        "bright_yellow" | "bright-yellow" => 93,
        "bright_blue" | "bright-blue" => 94,
        "bright_magenta" | "bright-magenta" | "bright_purple" => 95,
        "bright_cyan" | "bright-cyan" => 96,
        "bright_white" | "bright-white" => 97,
        _ => 0,
    };
    if base_escape == 0 {

        if let Some(num) = base.parse::<u16>().ok() {
            codes.push(num as u8);
            return Some(sgr_from_codes(&codes));
        }
        if let Some(sgr) = hex_or_semicolons(base) {
            return Some(sgr);
        }
        return None;
    }
    if is_bg {
        codes.push(base_escape + 10);
    } else {
        codes.push(base_escape);
    }
    Some(sgr_from_codes(&codes))
}

fn split_style(n: &str) -> (String, String) {
    for prefix in [
        "bold_", "bold-", "italic_", "italic-", "underline_", "underline-",
        "invert_", "invert-", "reverse_", "dim_", "dim-", "strikethrough_",
        "strikethrough-",
    ] {
        if n.starts_with(prefix) {
            return (prefix.trim_end_matches(['_', '-']).to_string(), n[prefix.len()..].to_string());
        }
    }
    (String::new(), n.to_string())
}

fn hex_or_semicolons(s: &str) -> Option<String> {

    if let Some(hex) = s.strip_prefix('#') {
        if hex.len() == 6 {
            if let Ok(v) = u32::from_str_radix(hex, 16) {
                return Some(format!(
                    "\x1b[38;2;{};{};{}m",
                    (v >> 16) & 0xff,
                    (v >> 8) & 0xff,
                    v & 0xff
                ));
            }
        }
    }
    if s.contains(';') && s.split(';').all(|p| p.parse::<u8>().is_ok()) {
        return Some(format!("\x1b[{}m", s));
    }
    None
}

fn sgr_from_codes(codes: &[u8]) -> String {
    let joined = codes
        .iter()
        .map(u8::to_string)
        .collect::<Vec<_>>()
        .join(";");
    format!("\x1b[{}m", joined)
}

fn ansi_from_fg(num: u16) -> ApplyResult {
    ApplyResult::Ansi {
        start: format!("\x1b[{}m", num),
        end: RESET.to_string(),
    }
}

pub fn expand_dollar_code(code: &str) -> Option<String> {
    match code {
        "reset" => Some(RESET.to_string()),
        "b" => Some("\x1b[1m".to_string()),
        "i" => Some("\x1b[3m".to_string()),
        "u" => Some("\x1b[4m".to_string()),
        "s" => Some("\x1b[9m".to_string()),
        "d" => Some("\x1b[2m".to_string()),
        "c1" => Some("\x1b[32m".to_string()),
        "c2" => Some("\x1b[36m".to_string()),
        "c3" => Some("\x1b[34m".to_string()),
        "c4" => Some("\x1b[35m".to_string()),
        "c5" => Some("\x1b[31m".to_string()),
        "c6" => Some("\x1b[33m".to_string()),
        "c7" => Some("\x1b[37m".to_string()),
        "c8" => Some("\x1b[90m".to_string()),
        "c9" => Some("\x1b[91m".to_string()),
        _ => named_color_sgr(code),
    }
}
