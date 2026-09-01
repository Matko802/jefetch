// Display options, mirroring fastfetch's FFDisplayConfig.
use super::json::JsonValue;

#[derive(Debug, Clone)]
pub struct DisplayConfig {
    pub separator: String,
    pub separator_color: Option<String>,
    pub key_color: Option<String>,
    pub title_color: Option<String>,
    pub key_width: usize,
    pub key_width_right_aligned: bool,
    pub padding: usize,
    pub bar_border_left: String,
    pub bar_border_right: String,
    pub bar_char_elapsed: String,
    pub bar_char_total: String,
    pub bar_width: usize,
    pub percent_type: u8,
    pub colors: Vec<u32>,
    pub pipe: Option<String>,
    pub bright_color: bool,
    pub color_align: bool,
    pub hide_cursor: bool,
    pub is_smart: bool,
}

impl Default for DisplayConfig {
    fn default() -> Self {
        DisplayConfig {
            separator: ":".to_string(),
            separator_color: Some("32".to_string()),
            key_color: None,
            title_color: Some("34".to_string()),
            key_width: 0,
            key_width_right_aligned: false,
            padding: 1,
            bar_border_left: "[".to_string(),
            bar_border_right: "]".to_string(),
            bar_char_elapsed: "-".to_string(),
            bar_char_total: "-".to_string(),
            bar_width: 20,
            percent_type: 0,
            colors: vec![
                41, 42, 43, 44, 45, 46, 47, 100, 101, 102, 103, 104, 105, 106, 107,
            ],
            pipe: None,
            bright_color: true,
            color_align: true,
            hide_cursor: true,
            is_smart: false,
        }
    }
}

impl DisplayConfig {
    pub fn parse(root: &JsonValue) -> Self {
        let mut d = DisplayConfig::default();
        if let Some(obj) = root.get("display").and_then(|v| v.obj()) {
            for (k, v) in obj {
                match k.as_str() {
                    "separator" => {
                        if let Some(s) = v.as_str() {
                            d.separator = s.to_string();
                        }
                    }
                    "separatorColor" => {
                        if let Some(s) = v.as_str() {
                            d.separator_color = Some(s.to_string());
                        }
                    }
                    "keyColor" => {
                        if let Some(s) = v.as_str() {
                            d.key_color = Some(s.to_string());
                        }
                    }
                    "titleColor" => {
                        if let Some(s) = v.as_str() {
                            d.title_color = Some(s.to_string());
                        }
                    }
                    "keyWidth" => {
                        d.key_width = v.as_u64().unwrap_or(0) as usize;
                    }
                    "keyWidthRightAligned" => {
                        d.key_width_right_aligned = v.as_bool().unwrap_or(false);
                    }
                    "padding" => {
                        d.padding = v.as_u64().unwrap_or(1) as usize;
                    }
                    "barBorderLeft" => {
                        if let Some(s) = v.as_str() {
                            d.bar_border_left = s.to_string();
                        }
                    }
                    "barBorderRight" => {
                        if let Some(s) = v.as_str() {
                            d.bar_border_right = s.to_string();
                        }
                    }
                    "barCharElapsed" => {
                        if let Some(s) = v.as_str() {
                            d.bar_char_elapsed = s.to_string();
                        }
                    }
                    "barCharTotal" => {
                        if let Some(s) = v.as_str() {
                            d.bar_char_total = s.to_string();
                        }
                    }
                    "barWidth" => {
                        d.bar_width = v.as_u64().unwrap_or(20) as usize;
                    }
                    "percentType" => {
                        d.percent_type = v.as_u64().unwrap_or(0) as u8;
                    }
                    "pipe" => {
                        if let Some(s) = v.as_str() {
                            d.pipe = Some(s.to_string());
                        }
                    }
                    "brightColor" => {
                        d.bright_color = v.as_bool().unwrap_or(true);
                    }
                    "colorAlign" => {
                        d.color_align = v.as_bool().unwrap_or(true);
                    }
                    "hideCursor" => {
                        d.hide_cursor = v.as_bool().unwrap_or(false);
                    }
                    "isSmart" => {
                        d.is_smart = v.as_bool().unwrap_or(false);
                    }
                    _ => {}
                }
            }
        }
        d
    }
}
