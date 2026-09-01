// Fastfetch-style format string engine.
//
// A format string is a mix of literal text, `{placeholder}` tokens,
// `{#color}` blocks, `{$N}` constants and `{?expr}..{?}` conditionals, e.g.:
//
//   "{key}: $c1{name}@$c5{freq-max}{freq-ghz}GHz"
//   "{#white}│ {#blue} OS {name}"
//   "{?6}({#yellow}{6}{#}){?}"
//
// The engine RESOLVES placeholders through a Resolver and computes the
// visible (ANSI-stripped) length for alignment.

use super::color::{expand_dollar_code, named_color_sgr, RESET};

#[derive(Debug)]
pub struct Result {
    pub text: String,
    pub length: usize,
}

pub trait Resolver {
    /// Named or numbered placeholder value ("name", "1", "all", "key").
    fn get_placeholder(&self, name: &str) -> Option<String>;
    /// The module's key name.
    fn key(&self) -> &str;
    /// Special color names the module defines (e.g. "keys" -> keyColor ANSI).
    fn get_color(&self, name: &str) -> Option<String> {
        let _ = name;
        None
    }
    /// Module constants referenced via {$N}.
    fn get_constant(&self, index: usize) -> Option<String> {
        let _ = index;
        None
    }
}

/// Parse and render a format string against a resolver.
pub fn format<F: Resolver + ?Sized>(fmt: &str, resolver: &F) -> Result {
    let mut text = String::new();
    let mut length = 0usize;
    let bytes = fmt.as_bytes();
    let mut i = 0;
    let n = bytes.len();
    // Conditional depth tracking: a stack of "skip" flags.
    let mut cond: Vec<bool> = Vec::new();

    while i < n {
        let c = bytes[i];
        // Skip rendering entirely while inside a false conditional.
        if cond.iter().any(|&s| s) {
            // Still scan for structure tokens so conditionals balance.
            match c {
                b'{' => {
                    // Consume the token so invalids don't echo later.
                    if let Some((token, next)) = read_brace(fmt, i) {
                        i = next;
                        track_cond(&token, &mut cond, resolver);
                        continue;
                    }
                }
                _ => {
                    i += utf8_len(c);
                    continue;
                }
            }
        }
        match c {
            b'{' => {
                if let Some((token, next)) = read_brace(fmt, i) {
                    handle_token(
                        &token,
                        &mut text,
                        &mut length,
                        resolver,
                        &mut cond,
                    );
                    i = next;
                    continue;
                }
                text.push('{');
                length += 1;
                i += 1;
            }
            b'$' => {
                if let Some((code, next)) = read_dollar(fmt, i) {
                    if let Some(s) = expand_dollar_code(&code) {
                        text.push_str(&s);
                    } else {
                        text.push_str("$");
                        text.push_str(&code);
                        length += 1 + code.len();
                    }
                    i = next;
                    continue;
                }
                text.push('$');
                length += 1;
                i += 1;
            }
            _ => {
                let len = utf8_len(c);
                text.push_str(&fmt[i..i + len]);
                length += 1;
                i += len;
            }
        }
    }

    Result { text, length }
}

/// Read a `{...}` token (possibly nested) starting at index `i` (which points
/// at `{`). Returns the inner text and the index just after the closing `}`.
fn read_brace(fmt: &str, i: usize) -> Option<(String, usize)> {
    let bytes = fmt.as_bytes();
    let mut depth = 1;
    let mut j = i + 1;
    while j < bytes.len() {
        match bytes[j] {
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some((fmt[i + 1..j].to_string(), j + 1));
                }
            }
            _ => {}
        }
        j += 1;
    }
    None
}

/// Read a `$`-code starting at `$`. Returns (code, next_index).
/// Supports `$reset`, `$c1`, `${...}`, `$b`, etc.
fn read_dollar(fmt: &str, i: usize) -> Option<(String, usize)> {
    let bytes = fmt.as_bytes();
    if i + 1 >= bytes.len() {
        return None;
    }
    if bytes[i + 1] == b'{' {
        // Braced form: ${code}
        let inner = i + 2;
        let mut j = inner;
        while j < bytes.len() && bytes[j] != b'}' {
            j += 1;
        }
        if j < bytes.len() {
            return Some((fmt[inner..j].to_string(), j + 1));
        }
        return None;
    }
    // Alphanumeric short form $c1 / $reset
    let mut len = 0;
    for &b in &bytes[i + 1..] {
        if b.is_ascii_alphanumeric() || b == b'-' {
            len += 1;
        } else {
            break;
        }
    }
    if len > 0 {
        Some((fmt[i + 1..i + 1 + len].to_string(), i + 1 + len))
    } else {
        None
    }
}

/// Handle a single `{token}`.
#[allow(clippy::too_many_arguments)]
fn handle_token<F: Resolver + ?Sized>(
    token: &str,
    text: &mut String,
    length: &mut usize,
    resolver: &F,
    cond: &mut Vec<bool>,
) {
    let t = token.trim();

    // Conditional open: {?expr}
    if let Some(rest) = t.strip_prefix('?') {
        let expr = rest.trim();
        let has = if let Some(name) = expr.strip_prefix('!') {
            !has_value(resolver, name)
        } else {
            has_value(resolver, expr)
        };
        cond.push(!has);
        return;
    }
    // Conditional close: {?}
    if t == "?" {
        cond.pop();
        return;
    }

    // Color block: {#...} or {#}
    if t.starts_with('#') {
        let name = t[1..].trim();
        if name.is_empty() {
            // reset: {"#"} -> default color
            text.push_str(RESET);
            return;
        }
        if name.eq_ignore_ascii_case("keys") {
            if let Some(c) = resolver.get_color("keys") {
                text.push_str(&c);
                return;
            }
        }
        if let Some(sgr) = named_color_sgr(name) {
            text.push_str(&sgr);
            return;
        }
        // Unknown color: render literally.
        push_rendered(text, length, &format!("{{#{}}}", name));
        return;
    }

    // Constant: {$N}
    if let Some(idx) = t.strip_prefix('$') {
        if let Ok(n) = idx.parse::<usize>() {
            if let Some(v) = resolver.get_constant(n) {
                push_rendered(text, length, &v);
            }
            return;
        }
    }

    // Regular placeholder, possibly with options.
    resolve_placeholder(t, text, length, resolver);
}

fn has_value<F: Resolver + ?Sized>(resolver: &F, name: &str) -> bool {
    let name = name.trim();
    if name.is_empty() {
        return false;
    }
    resolver
        .get_placeholder(name)
        .map(|v| !v.trim().is_empty())
        .unwrap_or(false)
}

fn resolve_placeholder<F: Resolver + ?Sized>(
    name: &str,
    text: &mut String,
    length: &mut usize,
    resolver: &F,
) {
    let trimmed = name.trim();
    if trimmed.eq_ignore_ascii_case("key") {
        push_rendered(text, length, resolver.key());
        return;
    }

    // Split off width/format options on ',' or ':'.
    let mut base = trimmed;
    let mut opts: Option<&str> = None;
    for sep in [',', ':'] {
        if let Some(pos) = trimmed.find(sep) {
            base = trimmed[..pos].trim();
            opts = Some(&trimmed[pos + 1..]);
            break;
        }
    }

    let val = resolver.get_placeholder(base);
    match val {
        Some(v) => {
            let rendered = opts
                .map(|o| apply_options(&v, o))
                .unwrap_or_else(|| v.clone());
            push_rendered(text, length, &rendered);
        }
        None => {
            push_rendered(text, length, &format!("{{{}}}", name));
        }
    }
}

/// Apply fastfetch width options: `{v,10}`, `{v,-10}`, `{v:10}`, `{v:-10}`.
/// Positive width right-aligns (pad-left), negative left-aligns (pad-right).
fn apply_options(v: &str, opts: &str) -> String {
    let opt = opts.trim();
    if opt.is_empty() {
        return v.to_string();
    }

    // Support fractional style options like "28.5" (truncate to 28 cols) and
    // things like "pct", "1" etc. Simplest: numeric width handling.
    let (neg, num) = if let Some(rest) = opt.strip_prefix('-') {
        (true, rest)
    } else {
        (false, opt)
    };

    if let Some(width) = num.parse::<usize>().ok() {
        let cur = visible_len(v);
        return if cur < width {
            let pad = width - cur;
            if neg {
                // left align: pad right
                format!("{}{}", v, " ".repeat(pad))
            } else {
                // right align: pad left
                format!("{}{}", " ".repeat(pad), v)
            }
        } else if cur > width {
            truncate_to_width(v, width)
        } else {
            v.to_string()
        };
    }
    v.to_string()
}

fn truncate_to_width(s: &str, width: usize) -> String {
    let mut out = String::new();
    let mut w = 0;
    for c in s.chars() {
        let cw = char_width(c);
        if w + cw > width {
            break;
        }
        out.push(c);
        w += cw;
    }
    out
}

fn char_width(c: char) -> usize {
    if c.is_ascii() {
        1
    } else {
        2
    }
}

fn push_rendered(out: &mut String, length: &mut usize, rendered: &str) {
    let mut i = 0;
    let b = rendered.as_bytes();
    while i < b.len() {
        if b[i] == 0x1b {
            if i + 1 < b.len() && b[i + 1] == b'[' {
                let mut j = i + 2;
                while j < b.len() && !b[j].is_ascii_alphabetic() {
                    j += 1;
                }
                out.push_str(&rendered[i..=j.min(b.len() - 1)]);
                i = (j + 1).min(b.len());
                continue;
            }
        }
        let len = utf8_len(b[i]);
        out.push_str(&rendered[i..i + len]);
        *length += 1;
        i += len;
    }
}

/// Track conditional opens/closes even while inside a skipped region.
fn track_cond<F: Resolver + ?Sized>(token: &str, cond: &mut Vec<bool>, resolver: &F) {
    let t = token.trim();
    if let Some(rest) = t.strip_prefix('?') {
        let expr = rest.trim();
        let has = if let Some(name) = expr.strip_prefix('!') {
            !has_value(resolver, name)
        } else {
            has_value(resolver, expr)
        };
        cond.push(!has);
    } else if t == "?" {
        cond.pop();
    }
}

/// Visible (ANSI-stripped) character length of a rendered string.
pub fn visible_len(s: &str) -> usize {
    let mut count = 0;
    let b = s.as_bytes();
    let mut i = 0;
    while i < b.len() {
        if b[i] == 0x1b && i + 1 < b.len() && b[i + 1] == b'[' {
            let mut j = i + 2;
            while j < b.len() && !b[j].is_ascii_alphabetic() {
                j += 1;
            }
            i = (j + 1).min(b.len());
            continue;
        }
        count += 1;
        i += utf8_len(b[i]);
    }
    count
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

/// Convenience wrapper for modules that simply map names to values.
pub struct MapResolver<'a> {
    pub key_name: &'a str,
    pub values: &'a [(&'a str, String)],
}

impl<'a> Resolver for MapResolver<'a> {
    fn get_placeholder(&self, name: &str) -> Option<String> {
        self.values
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(name))
            .map(|(_, v)| v.clone())
    }
    fn key(&self) -> &str {
        self.key_name
    }
}

pub fn format_map<'a>(fmt: &str, key: &'a str, values: &'a [(&'a str, String)]) -> String {
    let r = format(fmt, &MapResolver { key_name: key, values });
    r.text
}

pub use super::color::RESET as ANSI_RESET;