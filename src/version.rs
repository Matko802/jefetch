pub const RAW: &str = "0.1.1";
pub const COMPILED_ON: &str = "2026-09-01";
pub const BUILD_TYPE: &str = "Release";
pub const TARGET: &str = match option_env!("JEFETCH_TARGET") {
    Some(s) => s,
    None => "unknown",
};
pub const SYSTEM_LIB: &str = match option_env!("JEFETCH_LIB") {
    Some(s) => s,
    None => "unknown",
};

pub fn print_full() {
    println!("jefetch {}", RAW);
    println!("Compiled on: {}", COMPILED_ON);
    println!("Build type: {}", BUILD_TYPE);
    println!("Compile target: {}", TARGET);
    println!("System lib: {}", SYSTEM_LIB);
    println!("Compressed: No");
    println!("Features: None");
}
