// A minimal JSON/JSONC value type, mirroring the subset of yyjson that
// fastfetch relies on. Strings are stored decoded (borrowed where possible).

pub type JsonResult<T> = Result<T, String>;

#[derive(Debug, Clone)]
pub enum JsonValue {
    Null,
    Bool(bool),
    Int(i64),
    Uint(u64),
    Float(f64),
    Str(String),
    Arr(Vec<JsonValue>),
    Obj(Vec<(String, JsonValue)>),
}

impl JsonValue {
    pub fn kind(&self) -> &'static str {
        match self {
            JsonValue::Null => "null",
            JsonValue::Bool(_) => "bool",
            JsonValue::Int(_) | JsonValue::Uint(_) | JsonValue::Float(_) => "number",
            JsonValue::Str(_) => "string",
            JsonValue::Arr(_) => "array",
            JsonValue::Obj(_) => "object",
        }
    }

    pub fn get(&self, key: &str) -> Option<&JsonValue> {
        match self {
            JsonValue::Obj(m) => m.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }

    pub fn get_path(&self, path: &[&str]) -> Option<&JsonValue> {
        let mut cur = self;
        for k in path {
            cur = cur.get(k)?;
        }
        Some(cur)
    }

    pub fn obj(&self) -> Option<&Vec<(String, JsonValue)>> {
        match self {
            JsonValue::Obj(m) => Some(m),
            _ => None,
        }
    }

    pub fn arr(&self) -> Option<&Vec<JsonValue>> {
        match self {
            JsonValue::Arr(a) => Some(a),
            _ => None,
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            JsonValue::Str(s) => Some(s),
            _ => None,
        }
    }

    pub fn as_bool(&self) -> Option<bool> {
        match self {
            JsonValue::Bool(b) => Some(*b),
            _ => None,
        }
    }

    pub fn as_u64(&self) -> Option<u64> {
        match self {
            JsonValue::Uint(v) => Some(*v),
            JsonValue::Int(v) => Some(*v as u64),
            _ => None,
        }
    }

    pub fn as_i64(&self) -> Option<i64> {
        match self {
            JsonValue::Int(v) => Some(*v),
            JsonValue::Uint(v) => Some(*v as i64),
            _ => None,
        }
    }

    pub fn as_f64(&self) -> Option<f64> {
        match self {
            JsonValue::Float(v) => Some(*v),
            JsonValue::Int(v) => Some(*v as f64),
            JsonValue::Uint(v) => Some(*v as f64),
            _ => None,
        }
    }

    pub fn is_null(&self) -> bool {
        matches!(self, JsonValue::Null)
    }

    /// Serialize to a compact JSON string (used for custom logo strings).
    pub fn to_json_string(&self) -> String {
        let mut out = String::new();
        self.write(&mut out);
        out
    }

    /// Serialize with 2-space indentation, matching fastfetch's --json output.
    pub fn to_json_pretty(&self) -> String {
        let mut out = String::new();
        self.write_pretty(0, &mut out);
        out
    }

    fn write_pretty(&self, indent: usize, out: &mut String) {
        let pad = |n: usize, out: &mut String| {
            for _ in 0..n {
                out.push_str("  ");
            }
        };
        match self {
            JsonValue::Arr(a) => {
                if a.is_empty() {
                    out.push_str("[]");
                    return;
                }
                out.push_str("[\n");
                for (i, v) in a.iter().enumerate() {
                    pad(indent + 1, out);
                    v.write_pretty(indent + 1, out);
                    if i + 1 < a.len() {
                        out.push(',');
                    }
                    out.push('\n');
                }
                pad(indent, out);
                out.push(']');
            }
            JsonValue::Obj(m) => {
                if m.is_empty() {
                    out.push_str("{}");
                    return;
                }
                out.push_str("{\n");
                for (i, (k, v)) in m.iter().enumerate() {
                    pad(indent + 1, out);
                    out.push('"');
                    for c in k.chars() {
                        match c {
                            '"' => out.push_str("\\\""),
                            '\\' => out.push_str("\\\\"),
                            c => out.push(c),
                        }
                    }
                    out.push('"');
                    out.push_str(": ");
                    v.write_pretty(indent + 1, out);
                    if i + 1 < m.len() {
                        out.push(',');
                    }
                    out.push('\n');
                }
                pad(indent, out);
                out.push('}');
            }
            other => other.write(out),
        }
    }

    fn write(&self, out: &mut String) {
        match self {
            JsonValue::Null => out.push_str("null"),
            JsonValue::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            JsonValue::Int(v) => out.push_str(&v.to_string()),
            JsonValue::Uint(v) => out.push_str(&v.to_string()),
            JsonValue::Float(v) => out.push_str(&v.to_string()),
            JsonValue::Str(s) => {
                out.push('"');
                for c in s.chars() {
                    match c {
                        '"' => out.push_str("\\\""),
                        '\\' => out.push_str("\\\\"),
                        '\n' => out.push_str("\\n"),
                        '\r' => out.push_str("\\r"),
                        '\t' => out.push_str("\\t"),
                        c if (c as u32) < 0x20 => {
                            out.push_str(&format!("\\u{:04x}", c as u32))
                        }
                        c => out.push(c),
                    }
                }
                out.push('"');
            }
            JsonValue::Arr(a) => {
                out.push('[');
                for (i, v) in a.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    v.write(out);
                }
                out.push(']');
            }
            JsonValue::Obj(m) => {
                out.push('{');
                for (i, (k, v)) in m.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    out.push('"');
                    out.push_str(k);
                    out.push('"');
                    out.push(':');
                    v.write(out);
                }
                out.push('}');
            }
        }
    }
}

impl From<&str> for JsonValue {
    fn from(s: &str) -> Self {
        JsonValue::Str(s.to_string())
    }
}
