// A small hand-written TOML parser, no external deps.
//
// Supports the subset of TOML used by sharkfetch's own config.toml:
//   - full-line and inline comments (`#`)
//   - bare and quoted keys
//   - `[table]` and `[table.subtable]` headers
//   - string values (basic and literal single-quoted)
//   - integer, float, and boolean values
//   - arrays (including multi-line arrays) of the above scalars
//
// Nested inline tables `{ key = "val" }` are NOT supported.

#[derive(Debug, Clone, PartialEq)]
pub enum TomlValue {
    Str(String),
    Int(i64),
    Float(f64),
    Bool(bool),
    Array(Vec<TomlValue>),
}

impl TomlValue {
    pub fn as_str(&self) -> Option<&str> {
        match self {
            TomlValue::Str(s) => Some(s),
            _ => None,
        }
    }
    pub fn as_i64(&self) -> Option<i64> {
        match self {
            TomlValue::Int(i) => Some(*i),
            _ => None,
        }
    }
    pub fn as_bool(&self) -> Option<bool> {
        match self {
            TomlValue::Bool(b) => Some(*b),
            _ => None,
        }
    }
    pub fn as_array(&self) -> Option<&[TomlValue]> {
        match self {
            TomlValue::Array(a) => Some(a),
            _ => None,
        }
    }
    pub fn as_str_array(&self) -> Option<Vec<String>> {
        self.as_array().map(|a| {
            a.iter().filter_map(|v| v.as_str().map(|s| s.to_string())).collect()
        })
    }
}

/// Parsed TOML document: the root table plus named tables, in declaration
/// order (root table first).
#[derive(Debug, Clone, Default)]
pub struct TomlDoc {
    pub root: Vec<(String, TomlValue)>,
    pub tables: Vec<(Vec<String>, Vec<(String, TomlValue)>)>,
}

impl TomlDoc {
    /// Look up a value in the root table.
    pub fn get(&self, key: &str) -> Option<&TomlValue> {
        self.root.iter().find(|(k, _)| k == key).map(|(_, v)| v)
    }
    /// Look up a value in a named table (e.g. ["display"] → display.separator).
    pub fn get_in<'a>(&'a self, table: &str, key: &str) -> Option<&'a TomlValue> {
        self.tables
            .iter()
            .find(|(path, _)| path.len() == 1 && path[0] == table)
            .and_then(|(_, pairs)| pairs.iter().find(|(k, _)| k == key))
            .map(|(_, v)| v)
    }
}

/// Split raw input into logical lines, joining multi-line array definitions
/// (handled by counting `[`/`]`), and stripping `#` comments.
fn logical_lines(input: &str) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut pending = String::new();
    let mut bracket_depth: i32 = 0;

    for raw in input.lines() {
        let line = strip_comment(raw).trim().to_string();
        if line.is_empty() {
            continue;
        }
        if bracket_depth > 0 {
            // Continuation line of a multi-line array.
            pending.push(' ');
            pending.push_str(&line);
        } else if pending_is_open_array(&pending) {
            // Previous line opened an array that hasn't closed yet.
            pending.push(' ');
            pending.push_str(&line);
        } else {
            pending = line;
        }

        // Update bracket depth across the whole accumulated pending text.
        bracket_depth = 0;
        let mut in_basic = false;
        let mut in_literal = false;
        let mut chars = pending.chars().peekable();
        while let Some(c) = chars.next() {
            match c {
                '"' if !in_literal => in_basic = !in_basic,
                '\'' if !in_basic => in_literal = !in_literal,
                '[' if !in_basic && !in_literal => bracket_depth += 1,
                ']' if !in_basic && !in_literal => bracket_depth -= 1,
                _ => {}
            }
            let _ = &mut chars;
        }

        if bracket_depth <= 0 {
            out.push(std::mem::take(&mut pending));
            bracket_depth = 0;
        }
    }

    if !pending.is_empty() && bracket_depth > 0 {
        // Unterminated array; push what we have (will error on parse).
        out.push(pending);
    }
    out
}

/// Heuristic: does the accumulated text represent an array that is not yet
/// closed? Used only for lines that don't themselves leave bracket_depth>0.
fn pending_is_open_array(s: &str) -> bool {
    // A trailing '=' or ',' indicates the value continues on the next line.
    let t = s.trim_end();
    t.ends_with('=') || t.ends_with(',')
}

pub fn parse(input: &str) -> Result<TomlDoc, String> {
    let mut doc = TomlDoc::default();
    let mut current_table: Vec<String> = Vec::new();
    let mut current_pairs: Vec<(String, TomlValue)> = Vec::new();
    let mut line_num = 0usize;

    for logical in logical_lines(input) {
        line_num += 1;
        let line = logical.trim();
        if line.is_empty() {
            continue;
        }

        // Table header: [table.sub]
        if line.starts_with('[') && line.ends_with(']') && !line[1..].starts_with('[') {
            flush(&mut doc, &mut current_table, &mut current_pairs);
            let inner = &line[1..line.len() - 1];
            current_table = inner
                .split('.')
                .map(|s| s.trim().trim_matches('"').trim_matches('\'').to_string())
                .collect();
            if current_table.is_empty() {
                return Err(format!("line {line_num}: empty table name"));
            }
            continue;
        }

        // key = value
        let Some((key, val_str)) = line.split_once('=') else {
            return Err(format!("line {line_num}: expected `key = value`, got `{line}`"));
        };
        let key = key.trim().trim_matches('"').trim_matches('\'').to_string();
        let val_str = val_str.trim();
        let value = parse_value(val_str, line_num)?;
        current_pairs.push((key, value));
    }

    flush(&mut doc, &mut current_table, &mut current_pairs);
    Ok(doc)
}

fn flush(doc: &mut TomlDoc, table: &mut Vec<String>, pairs: &mut Vec<(String, TomlValue)>) {
    if pairs.is_empty() {
        return;
    }
    let pairs = std::mem::take(pairs);
    if table.is_empty() {
        doc.root = pairs;
    } else {
        doc.tables.push((table.clone(), pairs));
    }
}

/// Remove everything from a `#` comment to end of line, keeping `#` inside
/// quoted strings literal.
fn strip_comment(line: &str) -> String {
    let bytes = line.as_bytes();
    let mut in_basic = false;
    let mut in_literal = false;
    for (i, &b) in bytes.iter().enumerate() {
        match b {
            b'"' if !in_literal => in_basic = !in_basic,
            b'\'' if !in_basic => in_literal = !in_literal,
            b'#' if !in_basic && !in_literal => {
                return line[..i].to_string();
            }
            _ => {}
        }
    }
    line.to_string()
}

fn parse_value(s: &str, line_num: usize) -> Result<TomlValue, String> {
    // A value may span lines if it came from a multi-line array; normalize
    // internal newlines to spaces.
    let s: String = s.split_whitespace().collect::<Vec<_>>().join(" ");
    let s = s.trim();
    if s.is_empty() {
        return Err(format!("line {line_num}: empty value"));
    }
    // Array (possibly joined across lines).
    if s.starts_with('[') && s.ends_with(']') {
        let inner = &s[1..s.len() - 1];
        let mut items = Vec::new();
        let mut parts = Vec::new();
        let mut cur = String::new();
        let mut depth = 0i32;
        for c in inner.chars() {
            match c {
                '[' => depth += 1,
                ']' => depth -= 1,
                ',' if depth == 0 => {
                    parts.push(std::mem::take(&mut cur));
                    continue;
                }
                _ => {}
            }
            cur.push(c);
        }
        if !cur.trim().is_empty() {
            parts.push(cur);
        }
        for part in parts {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            items.push(parse_value(part, line_num)?);
        }
        return Ok(TomlValue::Array(items));
    }
    // String (basic or literal).
    if s.starts_with('"') {
        if s.len() < 2 || !s.ends_with('"') {
            return Err(format!("line {line_num}: unterminated string"));
        }
        return Ok(TomlValue::Str(parse_basic_string(s, line_num)?));
    }
    if s.starts_with('\'') {
        if s.len() < 2 || !s.ends_with('\'') {
            return Err(format!("line {line_num}: unterminated string"));
        }
        return Ok(TomlValue::Str(s[1..s.len() - 1].to_string()));
    }
    // Bool.
    if s == "true" {
        return Ok(TomlValue::Bool(true));
    }
    if s == "false" {
        return Ok(TomlValue::Bool(false));
    }
    // Number.
    if let Ok(i) = s.parse::<i64>() {
        return Ok(TomlValue::Int(i));
    }
    if let Ok(f) = s.parse::<f64>() {
        return Ok(TomlValue::Float(f));
    }
    Err(format!("line {line_num}: unsupported TOML value `{s}`"))
}

fn parse_basic_string(s: &str, _line_num: usize) -> Result<String, String> {
    let inner = &s[1..s.len() - 1];
    let mut out = String::new();
    let mut chars = inner.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' {
            let esc = chars.next().ok_or("bad escape")?;
            out.push(match esc {
                'n' => '\n',
                't' => '\t',
                'r' => '\r',
                '"' => '"',
                '\\' => '\\',
                'b' => '\u{0008}',
                'f' => '\u{000C}',
                other => return Err(format!("invalid escape \\{other}")),
            });
        } else {
            out.push(c);
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple_table() {
        let doc = parse("[display]\nseparator = \": \"\nkeyColor = \"bold_cyan\"\n").unwrap();
        assert_eq!(
            doc.get_in("display", "separator").and_then(|v| v.as_str()),
            Some(": ")
        );
        assert_eq!(
            doc.get_in("display", "keyColor").and_then(|v| v.as_str()),
            Some("bold_cyan")
        );
    }

    #[test]
    fn parses_multiline_array() {
        let doc = parse("modules = [\n    \"a\", \"b\",\n    \"c\",\n]\n").unwrap();
        assert_eq!(
            doc.get("modules").and_then(|v| v.as_str_array()),
            Some(vec!["a".to_string(), "b".to_string(), "c".to_string()])
        );
    }

    #[test]
    fn parses_integers_and_bools() {
        let doc = parse("padding = 0\nbright = true\n").unwrap();
        assert_eq!(doc.get("padding").and_then(|v| v.as_i64()), Some(0));
        assert_eq!(doc.get("bright").and_then(|v| v.as_bool()), Some(true));
    }

    #[test]
    fn strips_comments() {
        let doc = parse("a = \"x\" # trailing\n# full comment\nb = 1\n").unwrap();
        assert_eq!(doc.get("a").and_then(|v| v.as_str()), Some("x"));
        assert_eq!(doc.get("b").and_then(|v| v.as_i64()), Some(1));
    }
}
