use super::{ModuleInstance, ModuleOutput};
use crate::config::configfile::Config;
use crate::print::format::{self, Resolver};

pub fn run(inst: &ModuleInstance, cfg: &Config) -> Option<ModuleOutput> {
    let name = inst.module.to_ascii_lowercase();

    match name.as_str() {
        "break" => return Some(super::ModuleOutput::blank()),
        "separator" => return Some(render_separator(cfg)),
        _ => {}
    }

    let base = super::exec_impl::render(name.as_str(), inst, cfg)?;

    let values: Vec<String> = if let Some(fmt_str) = &inst.args.format {
        if name == "packages" && super::exec_impl::packages_owns_format(fmt_str) {
            base.values.clone()
        } else {
            base.values
                .iter()
                .map(|v| render(fmt_str, inst, v))
                .collect()
        }
    } else {
        base.values.clone()
    };

    let raw_key = inst
        .args
        .key
        .clone()
        .unwrap_or_else(|| base.key.clone());
    let key_rendered = render_key(&raw_key, &base.key, inst);

    let mut out = super::ModuleOutput::supported("", values);
    out.key = key_rendered;

    if !out.key.contains('\x1b') {
        if let Some(c) = inst
            .args
            .key_color
            .as_deref()
            .or(cfg.display.key_color.as_deref())
        {
            if let crate::print::color::ApplyResult::Ansi { start, end } =
                crate::print::color::color_code_to_ansi(c)
            {
                out.key = format!("{}{}{}", start, out.key, end);
            }
        }
    }

    Some(out)
}

fn render_key(raw_key: &str, base_key: &str, inst: &ModuleInstance) -> String {
    let r = format::format(raw_key, &KeyResolver { base_key, inst });
    r.text
}

struct KeyResolver<'a> {
    base_key: &'a str,
    inst: &'a ModuleInstance,
}

impl<'a> Resolver for KeyResolver<'a> {
    fn get_placeholder(&self, name: &str) -> Option<String> {
        match name {
            "key" => Some(self.base_key.to_string()),
            _ => None,
        }
    }
    fn key(&self) -> &str {
        self.base_key
    }
    fn get_color(&self, name: &str) -> Option<String> {

        if name == "keys" {
            let c = self.inst.args.key_color.as_deref().unwrap_or("");
            return crate::print::color::named_color_sgr(c);
        }
        None
    }
}

fn render(fmt_str: &str, inst: &ModuleInstance, value: &str) -> String {
    let value_s = value.to_string();
    let key_s = inst.args.key.clone().unwrap_or_default();
    let placeholders: &[(&str, String)] = &[("value", value_s), ("title", key_s.clone())];
    let r = format::format(
        fmt_str,
        &ValueResolver {
            key_name: &key_s,
            values: placeholders,
            inst,
        },
    );
    r.text
}

struct ValueResolver<'a> {
    key_name: &'a str,
    values: &'a [(&'a str, String)],
    inst: &'a ModuleInstance,
}

impl<'a> Resolver for ValueResolver<'a> {
    fn get_placeholder(&self, name: &str) -> Option<String> {
        if let Some(v) = self
            .values
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(name))
        {
            return Some(v.1.clone());
        }

        if name.eq_ignore_ascii_case("default") {
            render_empty("default");
            return self.values.first().map(|(_, v)| v.clone());
        }

        if name.eq_ignore_ascii_case("all")
            || name == "1"
            || name == "value"
            || name == "value1"
        {
            return self.values.first().map(|(_, v)| v.clone());
        }
        None
    }
    fn key(&self) -> &str {
        self.key_name
    }
    fn get_color(&self, name: &str) -> Option<String> {
        if name == "keys" {
            let c = self.inst.args.key_color.as_deref().unwrap_or("");
            return crate::print::color::named_color_sgr(c);
        }
        None
    }
}

#[allow(unused_variables)]
fn render_empty(_: &str) {}

fn render_separator(_cfg: &Config) -> ModuleOutput {

    let u = crate::detection::user::detect();
    let title_len = 1
        + crate::print::format::visible_len(&u.user_name_part)
        + crate::print::format::visible_len(&u.host_name_part);
    let mut line = String::new();
    while crate::print::format::visible_len(&line) < title_len {
        line.push('-');
    }
    ModuleOutput::supported("", vec![line])
}
