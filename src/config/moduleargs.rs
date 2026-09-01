// Per-module shared arguments, mirroring fastfetch's FFModuleArgs.
use super::json::JsonValue;

#[derive(Debug, Clone, Default)]
pub struct ModuleArgs {
    pub key: Option<String>,
    pub key_color: Option<String>,
    pub format: Option<String>,
    pub prefix: Option<String>,
    pub hide_if_empty: bool,
    pub hide_if_not_supported: bool,
    pub output_color: Option<String>,
    pub output_custom_color: Option<u32>,
    pub title: bool,
    pub r#type: Option<String>,
    pub has_fmt: bool,
}

impl ModuleArgs {
    // Parse the common fields shared by all modules from a JSON object.
    pub fn parse(obj: &JsonValue) -> Self {
        let mut a = ModuleArgs::default();
        if let Some(v) = obj.get("key") {
            if let Some(s) = v.as_str() {
                a.key = Some(s.to_string());
            }
        }
        if let Some(v) = obj.get("keyColor") {
            if let Some(s) = v.as_str() {
                a.key_color = Some(s.to_string());
            }
        }
        if let Some(v) = obj.get("format") {
            if let Some(s) = v.as_str() {
                a.format = Some(s.to_string());
                a.has_fmt = true;
            }
        }
        if let Some(v) = obj.get("prefix") {
            if let Some(s) = v.as_str() {
                a.prefix = Some(s.to_string());
            }
        }
        if let Some(v) = obj.get("hideIfEmpty") {
            if let Some(b) = v.as_bool() {
                a.hide_if_empty = b;
            }
        }
        if let Some(v) = obj.get("hideIfNotSupported") {
            if let Some(b) = v.as_bool() {
                a.hide_if_not_supported = b;
            }
        }
        if let Some(v) = obj.get("outputColor") {
            if let Some(s) = v.as_str() {
                a.output_color = Some(s.to_string());
            }
        }
        if let Some(v) = obj.get("type") {
            if let Some(s) = v.as_str() {
                a.r#type = Some(s.to_string());
            }
        }
        a
    }

    pub fn type_or(&self) -> &str {
        self.r#type.as_deref().unwrap_or("")
    }
}
