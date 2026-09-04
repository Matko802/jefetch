use super::color::{expand_dollar_code, named_color_sgr, RESET};

#[derive(Debug)]
pub struct Result {
    pub text: String,
    pub length: usize,
}

pub trait Resolver {

    fn get_placeholder(&self, name: &str) -> Option<String>;

    fn key(&self) -> &str;

    fn get_color(&self, name: &str) -> Option<String> {
        let _ = name;
        None
    }

    fn get_constant(&self, index: usize) -> Option<String> {
        let _ = index;
        None
    }
}

pub fn format<F: Resolver + ?Sized>(fmt: &str, resolver: &F) -> Result {
    let mut text = String::new();
    let mut length = 0usize;
    let bytes = fmt.as_bytes();
    let mut i = 0;
    let n = bytes.len();

    let mut cond: Vec<bool> = Vec::new();

    while i < n {
        let c = bytes[i];

        if cond.iter().any(|&s| s) {

            match c {
                b'{' => {

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

fn read_dollar(fmt: &str, i: usize) -> Option<(String, usize)> {
    let bytes = fmt.as_bytes();
    if i + 1 >= bytes.len() {
        return None;
    }
    if bytes[i + 1] == b'{' {

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

#[allow(clippy::too_many_arguments)]
fn handle_token<F: Resolver + ?Sized>(
    token: &str,
    text: &mut String,
    length: &mut usize,
    resolver: &F,
    cond: &mut Vec<bool>,
) {
    let t = token.trim();

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

    if t == "?" {
        cond.pop();
        return;
    }

    if t.starts_with('#') {
        let name = t[1..].trim();
        if name.is_empty() {

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

        push_rendered(text, length, &format!("{{#{}}}", name));
        return;
    }

    if let Some(idx) = t.strip_prefix('$') {
        if let Ok(n) = idx.parse::<usize>() {
            if let Some(v) = resolver.get_constant(n) {
                push_rendered(text, length, &v);
            }
            return;
        }
    }

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

fn apply_options(v: &str, opts: &str) -> String {
    let opt = opts.trim();
    if opt.is_empty() {
        return v.to_string();
    }

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

                format!("{}{}", v, " ".repeat(pad))
            } else {

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

pub fn truncate_visible(s: &str, max: usize) -> String {
    let mut out = String::new();
    let b = s.as_bytes();
    let mut i = 0;
    let mut w = 0;
    let mut cut = false;
    while i < b.len() {
        if b[i] == 0x1b && i + 1 < b.len() && b[i + 1] == b'[' {
            let mut j = i + 2;
            while j < b.len() && !b[j].is_ascii_alphabetic() {
                j += 1;
            }
            let end = (j + 1).min(b.len());
            out.push_str(&s[i..end]);
            i = end;
            continue;
        }
        if w + 1 > max {
            cut = true;
            break;
        }
        let len = utf8_len(b[i]);
        out.push_str(&s[i..i + len]);
        w += 1;
        i += len;
    }
    if cut {
        out.push_str("\x1b[0m");
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn truncate_visible_cuts_cells_not_bytes() {
        assert_eq!(truncate_visible("hello world", 5), "hello\x1b[0m");
        assert_eq!(truncate_visible("hi", 10), "hi");
        assert_eq!(truncate_visible("hi", 2), "hi");
        assert_eq!(
            truncate_visible("\x1b[1;31mhello\x1b[0m world", 5),
            "\x1b[1;31mhello\x1b[0m\x1b[0m"
        );
        assert_eq!(
            truncate_visible("ab\x1b[0mcd", 3),
            "ab\x1b[0mc\x1b[0m"
        );
        assert_eq!(visible_len(&truncate_visible("\x1b[36mOS: NixOS x86_64", 8)), 8);
    }
}
