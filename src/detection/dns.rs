use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct DnsInfo {
    pub servers: Vec<String>,
    pub domain: String,
}

pub fn detect() -> Option<DnsInfo> {
    let text = read_file("/etc/resolv.conf")?;
    let mut info = DnsInfo::default();
    for line in text.lines() {
        let line = line.trim();
        let Some((key, val)) = line.split_once(char::is_whitespace) else {
            continue;
        };
        let key = key.trim();
        let val = val.trim();
        match key {
            "nameserver" => {
                if !val.is_empty() {
                    info.servers.push(val.to_string());
                }
            }
            "search" | "domain" => {
                if info.domain.is_empty() {
                    info.domain = val.split_whitespace().next().unwrap_or("").to_string();
                }
            }
            _ => {}
        }
    }
    if info.servers.is_empty() {
        None
    } else {
        Some(info)
    }
}
