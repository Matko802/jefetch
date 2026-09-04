use crate::detection::read_file;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct DisplayInfo {
    pub width: u32,
    pub height: u32,
    pub refresh_rate: u32,
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
        out.push(DisplayInfo {
            width,
            height,
            refresh_rate: 0,
            name: dir_name,
        });
    }
    out
}
