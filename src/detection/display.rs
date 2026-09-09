use crate::detection::read_file;
use crate::detection::wayland::WlOutput;

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
    let live = crate::detection::wayland::query_outputs();
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
        let (mut width, mut height) = (width, height);
        let mut refresh = 0;
        if let Some(wl) = match_live_output(&live, &connector, &edid) {
            if wl.width > 0 && wl.height > 0 {
                width = wl.width;
                height = wl.height;
            }
            refresh = wl.refresh_hz();
        }
        let (_, size_in) = edid_timing(&edid, width, height);
        if refresh == 0 {
            let (max_refresh, _) = edid_timing(&edid, width, height);
            refresh = max_refresh;
        }
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

fn edid_make_model(edid: &[u8]) -> (String, String) {
    let mut make = String::new();
    if edid.len() >= 10 {
        let word = u16::from_be_bytes([edid[8], edid[9]]) as u32;
        let mut code = String::new();
        for shift in [10u32, 5, 0] {
            let v = ((word >> shift) & 31) as u8;
            if v >= 1 && v <= 26 {
                code.push((b'A' + v - 1) as char);
            }
        }
        make = code;
    }
    let mut model = String::new();
    for k in 0..4 {
        let o = 54 + k * 18;
        if edid.len() < o + 18 {
            break;
        }
        let d = &edid[o..o + 18];
        if d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0xfc {
            model = String::from_utf8_lossy(&d[5..18])
                .trim_matches(|c: char| c == '\n' || c == '\r' || c == ' ' || c == '\0')
                .to_string();
            if !model.is_empty() {
                break;
            }
        }
    }
    (make, model)
}

fn match_live_output<'a>(
    live: &'a [WlOutput],
    connector: &str,
    edid: &[u8],
) -> Option<&'a WlOutput> {
    if live.is_empty() {
        return None;
    }
    if let Some(hit) = live.iter().find(|o| !o.name.is_empty() && o.name == connector) {
        return Some(hit);
    }
    let (make, model) = edid_make_model(edid);
    let mut best: Option<&'a WlOutput> = None;
    let mut best_score = 0u32;
    for o in live {
        let mut score = 0u32;
        let (omake, omodel) = (o.make.to_lowercase(), o.model.to_lowercase());
        if !make.is_empty() {
            let mk = make.to_lowercase();
            if omake.contains(&mk) || mk.contains(omake.as_str()) && !omake.is_empty() {
                score += 2;
            }
        }
        if !model.is_empty() {
            let md = model.to_lowercase();
            if omodel.contains(&md) || md.contains(omodel.as_str()) && !omodel.is_empty() {
                score += 3;
            }
        }
        if score > best_score {
            best_score = score;
            best = Some(o);
        }
    }
    if best.is_some() {
        return best;
    }
    if live.len() == 1 {
        return Some(&live[0]);
    }
    None
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

    fn live_output(name: &str, w: u32, h: u32, mhz: u32) -> WlOutput {
        WlOutput {
            name: name.to_string(),
            make: "DEL".to_string(),
            model: "DELL S2721DGF".to_string(),
            width: w,
            height: h,
            refresh_mhz: mhz,
        }
    }

    fn dell_edid() -> Vec<u8> {
        let mut edid = vec![0u8; 128];
        edid[8] = 0x10;
        edid[9] = 0xAC;
        let o = 54;
        edid[o] = 0;
        edid[o + 1] = 0;
        edid[o + 2] = 0;
        edid[o + 3] = 0xfc;
        edid[o + 4] = 0;
        let text = b"DELL S2721DGF\n";
        edid[o + 5..o + 5 + text.len()].copy_from_slice(text);
        edid
    }

    #[test]
    fn live_match_prefers_connector_name() {
        let live = vec![
            live_output("HDMI-A-1", 2560, 1440, 60000),
            live_output("DP-1", 1920, 1080, 143976),
        ];
        let hit = match_live_output(&live, "DP-1", &[]).unwrap();
        assert_eq!(hit.refresh_hz(), 144);
        assert_eq!((hit.width, hit.height), (1920, 1080));
    }

    #[test]
    fn live_match_falls_back_to_edid_model() {
        let other = WlOutput {
            name: "X".to_string(),
            make: "XXX".to_string(),
            model: "YYY".to_string(),
            width: 800,
            height: 600,
            refresh_mhz: 60000,
        };
        let mut second = live_output("", 1920, 1080, 143976);
        second.make = String::new();
        second.model = "DELL S2721DGF".to_string();
        let live = vec![other, second];
        let hit = match_live_output(&live, "DP-9", &dell_edid()).unwrap();
        assert_eq!(hit.refresh_hz(), 144);
    }

    #[test]
    fn live_match_single_output_wins_without_names() {
        let live = vec![live_output("", 1920, 1080, 143976)];
        let hit = match_live_output(&live, "DP-9", &[]).unwrap();
        assert_eq!(hit.refresh_hz(), 144);
    }

    #[test]
    fn live_match_empty_means_edid_fallback() {
        assert!(match_live_output(&[], "DP-1", &dell_edid()).is_none());
        let live = vec![live_output("A", 800, 600, 60000), live_output("B", 800, 600, 60000)];
        assert!(match_live_output(&live, "DP-9", &[]).is_none());
    }

    #[test]
    fn edid_make_model_parses() {
        let (make, model) = edid_make_model(&dell_edid());
        assert_eq!(make, "DEL");
        assert_eq!(model, "DELL S2721DGF");
        assert_eq!(edid_make_model(&[]), (String::new(), String::new()));
    }
}
