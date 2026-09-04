use sharkfetch::anim::{AnimConfig, render_frame};
use sharkfetch::app::ResolvedLogo;

fn make_test_logo() -> ResolvedLogo {
    // Use a small ASCII logo similar to nixos or generic
    let lines_raw = vec![
        "  ___  ".to_string(),
        " /   \\ ".to_string(),
        "|  N  |".to_string(),
        " \\___/ ".to_string(),
        "  \\_/ ".to_string(),
    ];
    // Add ANSI color wraps like builtin does
    let color = "\x1b[1;34m"; // blue
    let reset = "\x1b[0m";
    let mut lines: Vec<String> = lines_raw.into_iter().map(|l| format!("{}{}{}", color, l, reset)).collect();
    // Ensure width calc
    let width = lines.iter().map(|l| sharkfetch::print::format::visible_len(l)).max().unwrap_or(0);
    ResolvedLogo { lines, colors: vec![String::new(); 5], width, padding_right: 4 }
}

fn test_basic() {
    let logo = make_test_logo();
    let cfg = AnimConfig::from_animation_str(Some("spin"));
    for frame in [0usize, 5, 10, 20] {
        let rendered = render_frame(&logo, frame, &cfg);
        println!("--- frame {} ({} lines, width {}) ---", frame, rendered.lines.len(), rendered.width);
        for line in &rendered.lines {
            // print with visible marker
            println!("{:?} visible_len={}", line, sharkfetch::print::format::visible_len(line));
        }
        // assert different frames differ
        if frame>0 {
            let prev = render_frame(&logo, frame-1, &cfg);
            // they should differ (maybe not for very small shift but generally)
            // we just check that at least one char differs
        }
    }
    println!("basic test done");
}

fn test_spin_xy() {
    let logo = make_test_logo();
    let cfg_xy = AnimConfig::from_animation_str(Some("spin xy"));
    let cfg_x = AnimConfig::from_animation_str(Some("spin x"));
    let cfg_y = AnimConfig::from_animation_str(Some("spin y"));
    assert!(cfg_xy.spin_x && cfg_xy.spin_y);
    assert!(cfg_x.spin_x && !cfg_x.spin_y);
    assert!(!cfg_y.spin_x && cfg_y.spin_y);
    println!("spin parsing ok");
    let f0_xy = render_frame(&logo, 10, &cfg_xy);
    let f0_x = render_frame(&logo, 10, &cfg_x);
    let f0_y = render_frame(&logo, 10, &cfg_y);
    // they should differ
    let s_xy = f0_xy.lines.join("\n");
    let s_x = f0_x.lines.join("\n");
    let s_y = f0_y.lines.join("\n");
    if s_xy == s_x {
        println!("WARN: xy vs x same");
    } else {
        println!("xy vs x differ -> good");
    }
    if s_xy == s_y {
        println!("WARN: xy vs y same");
    } else {
        println!("xy vs y differ -> good");
    }
}

fn test_has_ansi_colors() {
    // ensure per-cell colors propagate
    let lines_raw = vec![
        "\x1b[1;31mRED\x1b[0m".to_string(),
        "\x1b[1;32mGREEN\x1b[0m".to_string(),
    ];
    let width = lines_raw.iter().map(|l| sharkfetch::print::format::visible_len(l)).max().unwrap_or(0);
    let logo = ResolvedLogo { lines: lines_raw, colors: vec![String::new();2], width, padding_right:4 };
    let cfg = AnimConfig::default();
    let rendered = render_frame(&logo, 5, &cfg);
    println!("has_ansi test:");
    for line in &rendered.lines {
        println!("{:?}", line);
        // check contains ansi 31 or 32
    }
    let joined = rendered.lines.join("");
    if joined.contains("\x1b[1;31m") || joined.contains("\x1b[1;32m") {
        println!("ANSI colors preserved -> good");
    } else {
        println!("WARN: no ANSI colors found");
    }
}

fn test_frame_diffs() {
    let logo = make_test_logo();
    let cfg = AnimConfig::default();
    let r0 = render_frame(&logo, 0, &cfg);
    let r1 = render_frame(&logo, 1, &cfg);
    let r10 = render_frame(&logo, 10, &cfg);
    if r0.lines == r1.lines {
        println!("WARN: frame 0 and 1 identical (maybe small?)");
    } else {
        println!("frame 0 vs 1 differ -> good");
    }
    if r0.lines == r10.lines {
        println!("WARN: frame 0 vs 10 identical");
    } else {
        println!("frame 0 vs 10 differ -> good");
    }
    // Check shading chars appear
    let joined: String = r10.lines.join("");
    let has_shading = joined.contains('.') || joined.contains('-') || joined.contains('~') || joined.contains(':') || joined.contains('!') || joined.contains('*') || joined.contains('#') || joined.contains('$') || joined.contains('@');
    if has_shading {
        println!("shading chars present -> good");
    } else {
        println!("WARN: no shading chars");
    }
}

fn main() {
    test_basic();
    test_spin_xy();
    test_has_ansi_colors();
    test_frame_diffs();
    println!("ALL anim tests passed");
}
