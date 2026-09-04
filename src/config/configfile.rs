use crate::config::json::JsonValue;
use crate::config::parse;
use super::display::DisplayConfig;
use super::general::GeneralConfig;
use super::moduleargs::ModuleArgs;

#[derive(Debug, Clone, Default)]
pub struct LogoConfig {
    pub logo_type: Option<String>,
    pub source: Option<String>,
    pub color: Option<String>,

    pub color_map: Vec<(String, String)>,
    pub padding_top: Option<usize>,
    pub padding_left: Option<usize>,
    pub padding_right: Option<usize>,
    pub font_size: Option<u32>,
    pub logo_key: Option<String>,
    pub width: Option<u32>,
    pub height: Option<u32>,

    pub animation: Option<String>,

    pub style: Option<String>,

    pub chars: Option<String>,
}

impl LogoConfig {
    pub fn parse(root: &JsonValue) -> Self {
        let mut l = LogoConfig::default();
        if let Some(obj) = root.get("logo").and_then(|v| v.obj()) {
            for (k, v) in obj {
                match k.as_str() {
                    "type" => {
                        if let Some(s) = v.as_str() {
                            l.logo_type = Some(s.to_string());
                        }
                    }
                    "source" => {
                        if let Some(s) = v.as_str() {
                            l.source = Some(s.to_string());
                        } else {

                            l.source = Some(v.to_json_string());
                        }
                    }
                    "color" => {
                        if let Some(s) = v.as_str() {
                            l.color = Some(s.to_string());
                        } else if let Some(map) = v.obj() {
                            for (line, col) in map {
                                if let Some(cn) = col.as_str() {
                                    l.color_map.push((line.clone(), cn.to_string()));
                                }
                            }
                        }
                    }
                    "padding" => {
                        if let Some(p) = v.obj() {
                            for (pk, pv) in p {
                                match pk.as_str() {
                                    "top" => l.padding_top = pv.as_u64().map(|x| x as usize),
                                    "left" => l.padding_left = pv.as_u64().map(|x| x as usize),
                                    "right" => l.padding_right = pv.as_u64().map(|x| x as usize),
                                    _ => {}
                                }
                            }
                        } else if let Some(n) = v.as_u64() {
                            l.padding_top = Some(n as usize);
                            l.padding_left = Some(n as usize);
                            l.padding_right = Some(n as usize);
                        }
                    }
                    "fontSize" => l.font_size = v.as_u64().map(|x| x as u32),
                    "logoKey" => {
                        if let Some(s) = v.as_str() {
                            l.logo_key = Some(s.to_string());
                        }
                    }
                    "width" => l.width = v.as_u64().map(|x| x as u32),
                    "height" => l.height = v.as_u64().map(|x| x as u32),
                    "animation" => {
                        if let Some(s) = v.as_str() {
                            l.animation = Some(s.to_string());
                        }
                    }
                    "style" | "mode" => {
                        if let Some(s) = v.as_str() {
                            l.style = Some(s.to_string());
                        }
                    }
                    "chars" | "characters" => {
                        if let Some(s) = v.as_str() {
                            l.chars = Some(s.to_string());
                        }
                    }
                    _ => {}
                }
            }
        }

        if l.animation.is_none() {
            if let Some(v) = root.get("animation").and_then(|v| v.as_str()) {
                l.animation = Some(v.to_string());
            }
        }
        l
    }
}

#[derive(Debug, Clone)]
pub enum ModuleEntry {

    Name(String),

    Object {
        module: String,
        args: ModuleArgs,
        raw: JsonValue,
    },
}

impl ModuleEntry {
    pub fn module(&self) -> &str {
        match self {
            ModuleEntry::Name(n) => n,
            ModuleEntry::Object { module, .. } => module.as_str(),
        }
    }
}

#[derive(Debug, Clone)]
pub struct Config {
    pub logo: LogoConfig,
    pub display: DisplayConfig,
    pub general: GeneralConfig,
    pub modules: Vec<ModuleEntry>,
    pub loaded_from: Option<String>,

    pub module_options: std::collections::HashMap<String, JsonValue>,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            logo: LogoConfig::default(),
            display: DisplayConfig::default(),
            general: GeneralConfig::default(),
            modules: Vec::new(),
            loaded_from: None,
            module_options: std::collections::HashMap::new(),
        }
    }
}

impl Config {
    pub fn from_jsonc(text: &str) -> Result<Config, String> {
        let root = parse(text)?;
        Config::from_json_value(&root)
    }

    pub fn from_json_value(root: &JsonValue) -> Result<Config, String> {
        let mut cfg = Config {
            logo: LogoConfig::parse(root),
            display: DisplayConfig::parse(root),
            general: GeneralConfig::parse(root),
            modules: Vec::new(),
            loaded_from: None,
            module_options: std::collections::HashMap::new(),
        };

        if let Some(obj) = root.obj() {
            let reserved = ["logo", "display", "general", "modules"];
            for (k, v) in obj {
                if reserved.contains(&k.as_str()) {
                    continue;
                }
                if !matches!(v, JsonValue::Obj(_)) {
                    continue;
                }
                cfg.module_options.insert(k.to_ascii_lowercase(), v.clone());
            }
        }

        if let Some(modules) = root.get("modules") {
            cfg.modules = parse_modules(modules)?;
        }

        Ok(cfg)
    }

    pub fn module_options(&self, module: &str) -> Option<&JsonValue> {
        self.module_options.get(&module.to_ascii_lowercase())
    }
}

fn parse_modules(v: &JsonValue) -> Result<Vec<ModuleEntry>, String> {
    let arr = v
        .arr()
        .ok_or_else(|| format!("`modules` must be an array, got {}", v.kind()))?;
    let mut out = Vec::new();
    for item in arr {
        match item {
            JsonValue::Str(s) => out.push(ModuleEntry::Name(s.clone())),
            JsonValue::Obj(_) => {
                let t = item
                    .get("type")
                    .and_then(|v| v.as_str())
                    .ok_or_else(|| "module object missing `type`".to_string())?;
                let args = ModuleArgs::parse(item);
                out.push(ModuleEntry::Object {
                    module: t.to_string(),
                    args,
                    raw: item.clone(),
                });
            }
            other => {
                return Err(format!(
                    "module entry must be a string or object, got {}",
                    other.kind()
                ))
            }
        }
    }
    Ok(out)
}
