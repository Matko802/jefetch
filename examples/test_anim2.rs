use sharkfetch::anim::{AnimConfig, render_frame};
use sharkfetch::app::ResolvedLogo;
use sharkfetch::logo;
use sharkfetch::print::format::visible_len;

fn resolved_from_builtin(name: &str) -> Option<ResolvedLogo> {
    let lc = sharkfetch::config::configfile::LogoConfig::default();
    // mimic builtin_logo_v logic via App internals? We'll just build manually like in app.rs builtin
    let logo = logo::by_name(name)?;
    let slots: Vec<&str> = if logo.slots.is_empty() { vec![logo.color] } else { logo.slots.to_vec() };
    let bold = "\x1b[1m";
    let mut lines: Vec<String> = Vec::new();
    let mut art_width = 0usize;
    let mut carry = format!("\x1b[{}m", slots[0]);
    for rawin in logo.lines {
        let mut out = String::new();
        out.push_str(bold);
        out.push_str(&carry);
        let mut chars = rawin.chars().peekable();
        while let Some(c) = chars.next() {
            if c == '$' {
                if let Some(&d) = chars.peek() {
                    if let Some(n) = d.to_digit(10) {
                        let n1 = n as usize;
                        if (1..=slots.len()).contains(&n1) {
                            carry = format!("\x1b[{}m", slots[n1-1]);
                            out.push_str(&carry);
                            chars.next();
                            continue;
                        }
                    } else if d == '$' {
                        out.push('$');
                        chars.next();
                        continue;
                    }
                }
                out.push('$');
            } else {
                out.push(c);
            }
        }
        out.push_str("\x1b[0m");
        art_width = art_width.max(visible_len(&out));
        lines.push(out);
    }
    let width = art_width;
    let n = lines.len();
    Some(ResolvedLogo { lines, colors: vec![String::new(); n], width, padding_right: 4 })
}

fn main() {
    for name in ["nixos", "arch", "gentoo", "ubuntu", "fedora"] {
        if let Some(logo) = resolved_from_builtin(name) {
            println!("=== logo {}: {} lines width {} ===", name, logo.lines.len(), logo.width);
            let cfg = AnimConfig::default();
            for frame in [0,5,10,20] {
                let rendered = render_frame(&logo, frame, &cfg);
                println!("frame {}: width {} lines {}", frame, rendered.width, rendered.lines.len());
                // count non-empty lines
                let filled = rendered.lines.iter().filter(|l| !l.trim().is_empty()).count();
                println!(" filled lines {}", filled);
                // show first few rendered lines stripped truncated to 80
                for (i, line) in rendered.lines.iter().enumerate().take(5) {
                    let v = visible_len(line);
                    let preview: String = line.chars().take(80).collect();
                    println!(" row {} v={} {:?}", i, v, preview);
                }
                // check frame diff
                if frame!=0 {
                    let prev = render_frame(&logo, frame-1, &cfg);
                    if rendered.lines == prev.lines {
                        println!("  WARN identical to previous frame");
                    }
                }
            }
            println!();
        } else {
            println!("logo {} not found", name);
        }
    }
}
