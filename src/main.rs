use sharkfetch::app::{App, CliOptions};
use sharkfetch::modules;

const USAGE: &str = r#"sharkfetch - A fastfetch-like system information tool (Rust + musl)

Usage: sharkfetch [options]

Options:
  -h, --help                  Show this help message
  -v, --version               Show the full version
  -s, --structure <modules>   Set custom `module:module:module` structure
  -c, --config <path>         Load a custom config file
      --no-config             Don't load config file
      --list-modules          List all available modules
      --list-presets          List available presets
      --list-config-paths     List search paths for config files
      --list-data-paths       List search paths for presets and logos
      --list-logos            List available logos
  -j, --json                  Enable JSON output (NYI in phase 1)
"#;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();

    let mut opts = CliOptions::default();
    let mut i = 0usize;
    while i < args.len() {
        let a = &args[i];
        match a.as_str() {
            "-h" | "--help" => {
                println!("{}", USAGE);
                return;
            }
            "-v" | "--version" => {
                sharkfetch::version::print_full();
                return;
            }
            "--version-raw" => {
                println!("{}", sharkfetch::version::RAW);
                return;
            }
            "--list-modules" => {
                list_modules();
                return;
            }
            "--list-logos" => {
                for name in sharkfetch::logo::list_names() {
                    println!("{}", name);
                }
                return;
            }
            "--list-config-paths" => {
                for d in sharkfetch::app::config_search_dirs_pub() {
                    println!("{}", d);
                }
                return;
            }
            "--no-config" => {
                opts.no_config = true;
            }
            "-s" | "--structure" => {
                if i + 1 < args.len() {
                    opts.structure = Some(args[i + 1].clone());
                    i += 1;
                }
            }
            "-c" | "--config" => {
                if i + 1 < args.len() {
                    opts.config_path = Some(args[i + 1].clone());
                    i += 1;
                }
            }
            "-j" | "--json" => {
                opts.json = true;
            }
            other => {
                // Support --key=value style.
                if let Some((k, v)) = other.split_once('=') {
                    match k {
                        "-s" | "--structure" => opts.structure = Some(v.to_string()),
                        "-c" | "--config" => opts.config_path = Some(v.to_string()),
                        _ => {
                            eprintln!("Error: unknown option: {}", other);
                            std::process::exit(400);
                        }
                    }
                } else {
                    eprintln!("Error: unknown option: {}", other);
                    std::process::exit(400);
                }
            }
        }
        i += 1;
    }

    let mut app = App::new(opts);
    std::process::exit(app.run());
}

fn list_modules() {
    for (idx, m) in modules::list().enumerate() {
        println!("{}) {:<14}: {}", idx + 1, m.name, m.description);
    }
}