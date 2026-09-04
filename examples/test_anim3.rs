use sharkfetch::anim::{AnimConfig, render_frame};
use sharkfetch::app::ResolvedLogo;
use sharkfetch::logo;
use sharkfetch::print::format::visible_len;

fn resolved_from_builtin(name: &str) -> Option<ResolvedLogo> {
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
    let name = std::env::args().nth(1).unwrap_or("nixos".to_string());
    let logo = resolved_from_builtin(&name).unwrap();
    println!("logo {} {}x{}", name, logo.width, logo.lines.len());
    for (i, l) in logo.lines.iter().enumerate() {
        println!("{:2}: {:?}", i, l);
        // print visible
        let v = visible_len(l);
        println!("    visible_len {}", v);
    }
    let cfg = AnimConfig::default();
    for frame in 0..8 {
        let rendered = render_frame(&logo, frame*3, &cfg);
        println!("\n=== FRAME {} (frame*3={}) width {} ===", frame, frame*3, rendered.width);
        for (i, line) in rendered.lines.iter().enumerate() {
            // print with borders to see alignment
            let v = visible_len(line);
            // raw with ansi visible as escape
            // also produce stripped
            let stripped = sharkfetch::app::strip_ansi(line);
            println!("{:2} [{:2}] |{}| stripped: {:?}", i, v, line, stripped);
        }
    }
}
