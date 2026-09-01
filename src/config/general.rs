// General (global, non-module) config, mirroring fastfetch's FFGlobalGeneralConfig.
use super::json::JsonValue;

#[derive(Debug, Clone, Default)]
pub struct GeneralConfig {
    pub module_overflow: String,
    pub processes: bool,
    pub thread_timeout: u32,
    pub add_nested_block: bool,
    pub error_output: bool,
    pub disable_linewrap: bool,
    pub show_hidden: bool,
    pub file_icon: String,
    pub file_style: String,
    pub file_color: u8,
    pub file_size: String,
    pub file_lines: String,
    pub file_max_pkg_count: u32,
    pub player: String,
    pub net_dns_worker: u32,
    pub wm_worker: u32,
    pub custom_cpu_name: String,
    pub custom_temperature_name: String,
    pub num_fonts: u32,
    pub font_worker: u32,
    pub font_prefer_mirror: bool,
    pub logo_title_key: bool,
    pub video_decoders: Vec<String>,
    pub video_encoders: Vec<String>,
    pub use_a2fa: bool,
    pub target_path: String,
    pub lazy_packages: bool,
}

impl GeneralConfig {
    pub fn parse(root: &JsonValue) -> Self {
        let mut g = GeneralConfig::default();
        if let Some(obj) = root.get("general").and_then(|v| v.obj()) {
            for (k, v) in obj {
                match k.as_str() {
                    "moduleOverflow" => {
                        if let Some(s) = v.as_str() {
                            g.module_overflow = s.to_string();
                        }
                    }
                    "processes" => g.processes = v.as_bool().unwrap_or(false),
                    "threadTimeout" => g.thread_timeout = v.as_u64().unwrap_or(0) as u32,
                    "addNestedBlock" => g.add_nested_block = v.as_bool().unwrap_or(false),
                    "errorOutput" => g.error_output = v.as_bool().unwrap_or(false),
                    "disableLinewrap" => g.disable_linewrap = v.as_bool().unwrap_or(false),
                    "showHidden" => g.show_hidden = v.as_bool().unwrap_or(false),
                    "fileIcon" => {
                        if let Some(s) = v.as_str() {
                            g.file_icon = s.to_string();
                        }
                    }
                    "player" => {
                        if let Some(s) = v.as_str() {
                            g.player = s.to_string();
                        }
                    }
                    "numFonts" => g.num_fonts = v.as_u64().unwrap_or(0) as u32,
                    "fontPreferMirror" => g.font_prefer_mirror = v.as_bool().unwrap_or(false),
                    "logoTitleKey" => g.logo_title_key = v.as_bool().unwrap_or(false),
                    "lazyPackages" => g.lazy_packages = v.as_bool().unwrap_or(false),
                    _ => {}
                }
            }
        }
        g
    }
}
