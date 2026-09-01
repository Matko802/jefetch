use crate::detection::read_file;

#[derive(Debug, Clone, Default)]
pub struct WifiInfo {
    pub name: String,
    pub ssid: String,
    pub signal_quality: u8,
    pub protocol: String,
    pub security: String,
}

/// Best-effort Wi-Fi detection: find the 802.11 interface, then signal from
/// /proc/net/wireless and SSID via `iwgetid` when available.
pub fn detect() -> Vec<WifiInfo> {
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/sys/class/net") else {
        return out;
    };
    for entry in entries.flatten() {
        let ifname = entry.file_name().to_string_lossy().into_owned();
        let uevent = read_file(format!("/sys/class/net/{}/uevent", ifname))
            .unwrap_or_default();
        if !uevent.contains("DEVTYPE=wlan") {
            continue;
        }
        let sig = wireless_signal(&ifname);
        if sig.is_none() {
            continue;
        }
        let ssid = iwgetid_ssid(&ifname).unwrap_or_default();
        out.push(WifiInfo {
            protocol: "802.11".to_string(),
            name: ifname,
            ssid,
            signal_quality: sig.unwrap_or(0),
            security: String::new(),
        });
    }
    out
}

/// Read signal quality (percent) from /proc/net/wireless.
/// Format: Inter-| sta | Quality | Discarded packets | Missed | WE
///         face | essid | level  | ...
fn wireless_signal(ifname: &str) -> Option<u8> {
    let text = read_file("/proc/net/wireless")?;
    for line in text.lines() {
        let line = line.trim();
        let (name, rest) = line.split_once(':')?;
        if name.trim() != ifname {
            continue;
        }
        let rest = rest.trim();
        // Quality is the second whitespace token: "50/70".
        let quality = rest.split_whitespace().nth(1)?;
        let (cur, max) = quality.split_once('/')?;
        let cur: f64 = cur.parse().ok()?;
        let max: f64 = max.parse().ok()?;
        if max <= 0.0 {
            return None;
        }
        return Some(((cur / max) * 100.0).round() as u8);
    }
    None
}

/// SSID via `iwgetid -r` (fastfetch uses nl80211; this avoids netlink code).
fn iwgetid_ssid(ifname: &str) -> Option<String> {
    let out = crate::detection::run_capture("iwgetid", &["-r", ifname])?;
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
}