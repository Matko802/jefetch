pub mod graphics;
pub mod image;

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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn language_logos_resolve() {
        for (name, alias) in [("clang", "llvm"), ("python", "py"), ("rust", "rs")] {
            let logo = by_name(name).unwrap_or_else(|| panic!("missing logo {name}"));
            assert_eq!(logo.name, name);
            assert!(
                by_name(alias).is_some(),
                "missing alias {alias} for {name}"
            );
            assert!(!logo.slots.is_empty());
            assert!(!logo.lines.is_empty());
        }
    }

    #[test]
    fn unknown_stays_last_fallback() {
        assert!(by_name("unknown").is_some());
        assert!(by_name("definitely-not-a-logo").is_none());
    }
}
