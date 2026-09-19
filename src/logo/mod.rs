#[derive(Debug, Clone)]
pub struct Logo {
    pub name: &'static str,

    pub aliases: &'static [&'static str],

    pub color: &'static str,

    pub slots: &'static [&'static str],

    pub color_keys: Option<&'static str>,

    pub color_title: Option<&'static str>,
    pub lines: &'static [&'static str],
}

pub type LogoData = Logo;

include!("data.rs");

pub fn by_name(name: &str) -> Option<&'static Logo> {
    LOGOS.iter().find(|l| {
        l.name.eq_ignore_ascii_case(name) || l.aliases.iter().any(|a| a.eq_ignore_ascii_case(name))
    })
}

pub fn list_names() -> impl Iterator<Item = &'static str> {
    LOGOS.iter().map(|l| l.name)
}
