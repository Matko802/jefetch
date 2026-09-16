use std::env;

fn main() {
    let target = env::var("TARGET").unwrap_or_else(|_| String::from("unknown"));
    println!("cargo:rustc-env=JEFETCH_TARGET={}", target);
    let lib = if target.contains("musl") {
        "musl"
    } else if target.contains("gnu") {
        "glibc"
    } else {
        "unknown"
    };
    println!("cargo:rustc-env=JEFETCH_LIB={}", lib);
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=TARGET");
}
