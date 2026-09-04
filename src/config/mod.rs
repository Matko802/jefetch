// A small hand-written JSONC (JSON with comments) parser, no external deps.
// Supports: line comments (`//`), block comments (`/* */`), trailing commas,
// nested objects/arrays, all JSON string escapes, numbers, booleans and null.

pub mod json;

use crate::config::json::{JsonResult, JsonValue};

pub mod configfile;
pub mod defaults;
pub mod display;
pub mod general;
pub mod moduleargs;

pub fn parse(input: &str) -> JsonResult<JsonValue> {
    let mut p = Parser {
        bytes: input.as_bytes(),
        pos: 0,
    };
    p.skip_ws_comments();
    let value = p.parse_value()?;
    p.skip_ws_comments();
    if p.pos != p.bytes.len() {
        return Err(format!("unexpected trailing data at byte {}", p.pos));
    }
    Ok(value)
}

struct Parser<'a> {
    bytes: &'a [u8],
    pos: usize,
}

impl<'a> Parser<'a> {
    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.pos).copied()
    }

    fn peek2(&self) -> Option<(u8, u8)> {
        let a = self.bytes.get(self.pos).copied()?;
        let b = self.bytes.get(self.pos + 1).copied()?;
        Some((a, b))
    }

    fn bump(&mut self) -> Option<u8> {
        let c = self.bytes.get(self.pos).copied()?;
        self.pos += 1;
        Some(c)
    }

    fn skip_ws(&mut self) {
        while let Some(c) = self.peek() {
            if c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' {
                self.pos += 1;
            } else {
                break;
            }
        }
    }

    // Skip whitespace and comments.
    fn skip_ws_comments(&mut self) {
        loop {
            self.skip_ws();
            match self.peek2() {
                Some((b'/', b'/')) => {
                    while let Some(c) = self.peek() {
                        if c == b'\n' {
                            break;
                        }
                        self.pos += 1;
                    }
                }
                Some((b'/', b'*')) => {
                    self.pos += 2;
                    loop {
                        match self.peek2() {
                            Some((b'*', b'/')) => {
                                self.pos += 2;
                                break;
                            }
                            None => break,
                            _ => self.pos += 1,
                        }
                    }
                }
                _ => return,
            }
        }
    }

    fn expect(&mut self, c: u8) -> JsonResult<()> {
        match self.bump() {
            Some(x) if x == c => Ok(()),
            other => Err(format!(
                "expected {:?} at byte {}, got {:?}",
                c as char,
                self.pos,
                other.map(|b| b as char)
            )),
        }
    }

    fn parse_value(&mut self) -> JsonResult<JsonValue> {
        self.skip_ws_comments();
        match self.peek() {
            Some(b'{') => self.parse_object(),
            Some(b'[') => self.parse_array(),
            Some(b'"') => {
                let s = self.parse_string()?;
                Ok(JsonValue::Str(s))
            }
            Some(b't') => {
                self.parse_literal(b"true")?;
                Ok(JsonValue::Bool(true))
            }
            Some(b'f') => {
                self.parse_literal(b"false")?;
                Ok(JsonValue::Bool(false))
            }
            Some(b'n') => {
                self.parse_literal(b"null")?;
                Ok(JsonValue::Null)
            }
            Some(c) if c == b'-' || c.is_ascii_digit() => self.parse_number(),
            Some(c) => Err(format!(
                "unexpected character {:?} at byte {}",
                c as char, self.pos
            )),
            None => Err("unexpected end of input".to_string()),
        }
    }

    fn parse_literal(&mut self, lit: &[u8]) -> JsonResult<()> {
        if self.bytes[self.pos..].starts_with(lit) {
            self.pos += lit.len();
            Ok(())
        } else {
            Err(format!(
                "invalid literal at byte {}",
                self.pos
            ))
        }
    }

    fn parse_object(&mut self) -> JsonResult<JsonValue> {
        self.expect(b'{')?;
        let mut members = Vec::new();
        loop {
            self.skip_ws_comments();
            match self.peek() {
                Some(b'}') => {
                    self.pos += 1;
                    return Ok(JsonValue::Obj(members));
                }
                Some(b'"') => {
                    let key = self.parse_string()?;
                    self.skip_ws_comments();
                    self.expect(b':')?;
                    let val = self.parse_value()?;
                    members.push((key, val));
                    self.skip_ws_comments();
                    match self.peek() {
                        Some(b',') => {
                            self.pos += 1;
                        }
                        Some(b'}') => {
                            self.pos += 1;
                            return Ok(JsonValue::Obj(members));
                        }
                        _ => {
                            return Err(format!(
                                "expected ',' or '}}' after object member at byte {}",
                                self.pos
                            ))
                        }
                    }
                }
                Some(_) => {
                    return Err(format!(
                        "expected object key or '}}' at byte {}",
                        self.pos
                    ))
                }
                None => return Err("unexpected end of object".to_string()),
            }
        }
    }

    fn parse_array(&mut self) -> JsonResult<JsonValue> {
        self.expect(b'[')?;
        let mut items = Vec::new();
        loop {
            self.skip_ws_comments();
            match self.peek() {
                Some(b']') => {
                    self.pos += 1;
                    return Ok(JsonValue::Arr(items));
                }
                _ => {
                    let val = self.parse_value()?;
                    items.push(val);
                    self.skip_ws_comments();
                    match self.peek() {
                        Some(b',') => {
                            self.pos += 1;
                        }
                        Some(b']') => {
                            self.pos += 1;
                            return Ok(JsonValue::Arr(items));
                        }
                        _ => {
                            return Err(format!(
                                "expected ',' or ']' after array item at byte {}",
                                self.pos
                            ))
                        }
                    }
                }
            }
        }
    }

    fn parse_string(&mut self) -> JsonResult<String> {
        self.expect(b'"')?;
        let mut out = String::new();
        loop {
            match self.bump() {
                None => return Err("unterminated string".to_string()),
                Some(b'"') => return Ok(out),
                Some(b'\\') => {
                    let esc = match self.bump() {
                        None => return Err("unterminated escape".to_string()),
                        Some(e) => e,
                    };
                    match esc {
                        b'"' => out.push('"'),
                        b'\\' => out.push('\\'),
                        b'/' => out.push('/'),
                        b'b' => out.push('\u{0008}'),
                        b'f' => out.push('\u{000C}'),
                        b'n' => out.push('\n'),
                        b'r' => out.push('\r'),
                        b't' => out.push('\t'),
                        b'u' => {
                            let cp = self.parse_hex4()?;
                            // Handle surrogate pairs.
                            if (0xD800..=0xDBFF).contains(&cp) {
                                if let Some((b'\\', b'u')) = self.peek2() {
                                    self.pos += 2;
                                    let low = self.parse_hex4()?;
                                    if (0xDC00..=0xDFFF).contains(&low) {
                                        let c = 0x10000
                                            + ((cp - 0xD800) << 10)
                                            + (low - 0xDC00);
                                        out.push(
                                            char::from_u32(c).unwrap_or('\u{FFFD}'),
                                        );
                                    } else {
                                        out.push('\u{FFFD}');
                                        out.push(char::from_u32(low).unwrap_or('\u{FFFD}'));
                                    }
                                } else {
                                    out.push('\u{FFFD}');
                                }
                            } else if (0xDC00..=0xDFFF).contains(&cp) {
                                out.push('\u{FFFD}');
                            } else {
                                out.push(char::from_u32(cp).unwrap_or('\u{FFFD}'));
                            }
                        }
                        other => {
                            return Err(format!(
                                "invalid escape \\{} at byte {}",
                                other as char,
                                self.pos
                            ))
                        }
                    }
                }
                Some(c) => {
                    // Copy a UTF-8 run of bytes as-is.
                    let start = self.pos - 1;
                    let len = utf8_len(c);
                    if let Some(end) = self.bytes.get(start..start + len) {
                        match std::str::from_utf8(end) {
                            Ok(s) => {
                                out.push_str(s);
                                self.pos = start + len;
                            }
                            Err(_) => {
                                out.push('\u{FFFD}');
                            }
                        }
                    } else {
                        out.push('\u{FFFD}');
                    }
                }
            }
        }
    }

    fn parse_hex4(&mut self) -> JsonResult<u32> {
        let mut v: u32 = 0;
        for _ in 0..4 {
            let c = self
                .bump()
                .ok_or_else(|| "truncated \\u escape".to_string())?;
            let d = match c {
                b'0'..=b'9' => (c - b'0') as u32,
                b'a'..=b'f' => (c - b'a' + 10) as u32,
                b'A'..=b'F' => (c - b'A' + 10) as u32,
                _ => return Err(format!("invalid \\u escape digit {:?}", c as char)),
            };
            v = v * 16 + d;
        }
        Ok(v)
    }

    fn parse_number(&mut self) -> JsonResult<JsonValue> {
        let start = self.pos;
        self.bump(); // '-' or digit

        let mut is_float = false;
        while let Some(c) = self.peek() {
            match c {
                b'0'..=b'9' => {
                    self.pos += 1;
                }
                b'.' | b'e' | b'E' | b'+' | b'-' => {
                    is_float = true;
                    self.pos += 1;
                }
                _ => break,
            }
        }
        let text = &self.bytes[start..self.pos];
        let s = std::str::from_utf8(text)
            .map_err(|_| "invalid number".to_string())?;
        if is_float {
            let f: f64 = s
                .parse()
                .map_err(|_| format!("invalid number '{}'", s))?;
            Ok(JsonValue::Float(f))
        } else if s.starts_with('-') {
            if let Ok(v) = s.parse::<i64>() {
                Ok(JsonValue::Int(v))
            } else {
                // Underflow: fall back to f64.
                let f: f64 = s
                    .parse()
                    .map_err(|_| format!("invalid number '{}'", s))?;
                Ok(JsonValue::Float(f))
            }
        } else if let Ok(v) = s.parse::<u64>() {
            Ok(JsonValue::Uint(v))
        } else {
            let f: f64 = s
                .parse()
                .map_err(|_| format!("invalid number '{}'", s))?;
            Ok(JsonValue::Float(f))
        }
    }
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
