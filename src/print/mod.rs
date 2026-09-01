pub mod color;
pub mod format;

use crate::config::display::DisplayConfig;
use crate::config::moduleargs::ModuleArgs;
use color::ApplyResult;

/// A rendered module: a key plus one or more value lines.
pub struct ModuleRender<'a> {
    pub key: String,
    pub value_lines: Vec<String>,
    pub display: &'a DisplayConfig,
    pub args: &'a ModuleArgs,
    /// Number of spaces to pad to the right of each value (logo column width).
    pub pad_right: usize,
}

impl<'a> ModuleRender<'a> {
    /// Build the final ANSI lines ready for printing.
    pub fn to_ansi_lines(&self) -> Vec<String> {
        if self.value_lines.is_empty() {
            return Vec::new();
        }
        // Compute the key prefix: colored key + separator + padding.
        let key_prefix = self.key_prefix();
        let key_visible = format::visible_len(&key_prefix);

        let padding = self.display.padding;
        let pad_right = self.pad_right;

        let mut out = Vec::new();
        for (idx, line) in self.value_lines.iter().enumerate() {
            let value = format!("{}{}", line, " ".repeat(pad_right));
            if idx == 0 {
                let rendered = format!(
                    "{}{}{}{}",
                    key_prefix,
                    self.separator_with_color(),
                    " ".repeat(padding),
                    value
                );
                out.push(rendered);
            } else {
                let blank = " ".repeat(key_visible + 1 + padding);
                let rendered = format!("{}{}", blank, value);
                out.push(rendered);
            }
        }
        out
    }

    fn key_prefix(&self) -> String {
        let key = if self.args.key.as_deref().unwrap_or("") != "" {
            self.args.key.clone().unwrap()
        } else {
            self.key.clone()
        };
        let color: &str = self
            .args
            .key_color
            .as_deref()
            .or(self.display.key_color.as_deref())
            .unwrap_or("");
        match color {
            "" => key,
            c => {
                let (s, end) = match color::color_code_to_ansi(c) {
                    ApplyResult::Ansi { start, end } => (start, end),
                    _ => (String::new(), String::new()),
                };
                format!("{}{}{}", s, key, end)
            }
        }
    }

    fn separator_with_color(&self) -> String {
        let s = &self.display.separator;
        match &self.display.separator_color {
            Some(c) => match color::color_code_to_ansi(c) {
                ApplyResult::Ansi {
                    start,
                    end,
                } => format!("{}{}{}", start, s, end),
                _ => s.to_string(),
            },
            None => s.to_string(),
        }
    }
}
