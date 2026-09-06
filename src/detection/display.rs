use crate::detection::read_file;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct DisplayInfo {
    pub width: u32,
    pub height: u32,
    pub refresh_rate: u32,
    pub size_in: u32,
    pub dtype: String,
    pub name: String,
}

pub fn detect() -> Vec<DisplayInfo> {
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/sys/class/drm") else {
        return out;
    };
    for entry in entries.flatten() {
        let dir_name = entry.file_name().to_string_lossy().into_owned();

        if let Some(rest) = dir_name.strip_prefix("card") {
            if !rest.contains('-') {
                continue;
            }
        } else {
            continue;
        }
        let path = entry.path();
        let status = read_file(path.join("status"))
            .map(|s| s.trim().to_string())
            .unwrap_or_default();
        if !status.eq_ignore_ascii_case("connected") {
            continue;
        }

        let modes = read_file(path.join("modes")).unwrap_or_default();
        let line = modes.lines().find(|l| !l.trim().is_empty());
        let Some(line) = line else { continue };
        let mode = line.trim();
        let Some((w, h)) = mode.split_once('x') else {
            continue;
        };
        let Ok(width) = w.parse::<u32>() else { continue };
        let Ok(height) = h.trim().parse::<u32>() else {
            continue;
        };
        if width == 0 || height == 0 {
            continue;
        }
        let connector = connector_name(&dir_name);
        let edid = std::fs::read(path.join("edid")).unwrap_or_default();
        let (refresh, size_in) = edid_timing(&edid, width, height);
        out.push(DisplayInfo {
            width,
            height,
            refresh_rate: refresh,
            size_in,
            dtype: connector_type(&connector),
            name: dir_name,
        });
    }
    out
}

fn connector_name(dir_name: &str) -> String {
    let rest = dir_name.strip_prefix("card").unwrap_or(dir_name);
    match rest.find('-') {
        Some(sep) => rest[sep + 1..].to_string(),
        None => rest.to_string(),
    }
}

fn connector_type(connector: &str) -> String {
    let c = connector.to_ascii_uppercase();
    if c.starts_with("EDP") || c.starts_with("LVDS") || c.starts_with("DSI") {
        return "Internal".to_string();
    }
    if c.starts_with("DP")
        || c.starts_with("HDMI")
        || c.starts_with("DVI")
        || c.starts_with("VGA")
        || c.starts_with("COMPOSITE")
        || c.starts_with("SVIDEO")
        || c.starts_with("COMPONENT")
        || c.starts_with("TV")
    {
        return "External".to_string();
    }
    String::new()
}

fn edid_timing(edid: &[u8], width: u32, height: u32) -> (u32, u32) {
    let mut size_in = 0;
    if edid.len() >= 23 {
        let (w, h) = (edid[21] as f64, edid[22] as f64);
        if w > 0.0 && h > 0.0 {
            size_in = ((w * w + h * h).sqrt() / 2.54 + 0.5) as u32;
        }
    }
    let mut refresh = 0;
    for k in 0..4 {
        let o = 54 + k * 18;
        if edid.len() < o + 12 {
            break;
        }
        refresh = refresh.max(descriptor_refresh(&edid[o..], width, height));
    }
    let mut off = 128;
    while edid.len() >= off + 128 {
        let block = &edid[off..off + 128];
        if block[0] == 0x02 && block[2] >= 4 {
            let mut d = block[2] as usize;
            while d + 18 <= 128 {
                refresh = refresh.max(descriptor_refresh(&block[d..], width, height));
                d += 18;
            }
        }
        off += 128;
    }
    (refresh, size_in)
}

fn descriptor_refresh(d: &[u8], width: u32, height: u32) -> u32 {
    if d.len() < 12 {
        return 0;
    }
    let clock = u16::from_le_bytes([d[0], d[1]]) as u64;
    if clock == 0 {
        return 0;
    }
    let hact = d[2] as u32 | (((d[4] >> 4) as u32) << 8);
    let vact = d[5] as u32 | (((d[7] >> 4) as u32) << 8);
    if hact != width || vact != height {
        return 0;
    }
    let htot = hact + (d[3] as u32 | (((d[4] & 0xF) as u32) << 8));
    let vtot = vact + (d[6] as u32 | (((d[7] & 0xF) as u32) << 8));
    if htot == 0 || vtot == 0 {
        return 0;
    }
    (clock * 10_000 / (htot as u64 * vtot as u64)) as u32
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_edid_size_and_refresh() {
        let mut edid = vec![0u8; 128];
        edid[21] = 53;
        edid[22] = 30;
        let o = 54;
        edid[o] = 0x02;
        edid[o + 1] = 0x3A;
        edid[o + 2] = 0x80;
        edid[o + 3] = 0x18;
        edid[o + 4] = 0x71;
        edid[o + 5] = 0x38;
        edid[o + 6] = 0x2D;
        edid[o + 7] = 0x40;
        let (refresh, size) = edid_timing(&edid, 1920, 1080);
        assert_eq!(refresh, 60);
        assert_eq!(size, 24);
        assert_eq!(edid_timing(&edid, 1280, 720), (0, 24));
        assert_eq!(edid_timing(&[], 1920, 1080), (0, 0));
    }

    #[test]
    fn classifies_connectors() {
        assert_eq!(connector_type("DP-1"), "External");
        assert_eq!(connector_type("HDMI-A-1"), "External");
        assert_eq!(connector_type("eDP-1"), "Internal");
        assert_eq!(connector_type("LVDS-1"), "Internal");
        assert_eq!(connector_name("card1-DP-2"), "DP-2");
    }
}
