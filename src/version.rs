pub const RAW: &str = "0.1.0";
pub const COMPILED_ON: &str = "2026-09-01";
pub const BUILD_TYPE: &str = "Release";
pub const TARGET: &str = "x86_64-unknown-linux-musl";
pub const SYSTEM_LIB: &str = "musl";

pub fn print_full() {
    println!("sharkfetch {}", RAW);
    println!("Compiled on: {}", COMPILED_ON);
    println!("Build type: {}", BUILD_TYPE);
    println!("Compile target: {}", TARGET);
    println!("System lib: {}", SYSTEM_LIB);
    println!("Compressed: No");
    println!("Features: None");
}
