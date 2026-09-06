use crate::config::configfile::ModuleEntry;
use crate::config::moduleargs::ModuleArgs;

#[derive(Debug, Clone, Copy)]
pub struct ModuleInfo {
    pub name: &'static str,
    pub description: &'static str,
    pub has_format: bool,
}

macro_rules! mod_info {
    ($name:literal, $desc:literal, $has_format:expr) => {
        ModuleInfo {
            name: $name,
            description: $desc,
            has_format: $has_format,
        }
    };
}

pub static MODULES: &[ModuleInfo] = &[
    mod_info!("Battery", "Print battery information", true),
    mod_info!("BIOS", "Print first-stage bootloader information (name, version, release date, etc.)", true),
    mod_info!("Bluetooth", "List connected Bluetooth devices", false),
    mod_info!("BluetoothRadio", "List Bluetooth radios (supported versions, vendors, etc.)", false),
    mod_info!("Board", "Print motherboard name and other information", true),
    mod_info!("Bootmgr", "Print second-stage bootloader information (name, firmware, etc.)", true),
    mod_info!("Break", "Print an empty line", false),
    mod_info!("Brightness", "Print the current brightness level of your monitors", true),
    mod_info!("Btrfs", "Print Linux BTRFS volumes", true),
    mod_info!("Camera", "Print available cameras", true),
    mod_info!("Chassis", "Print chassis type information (desktop, laptop, etc.)", true),
    mod_info!("Codec", "Print hardware video acceleration codec types (decode / encode)", true),
    mod_info!("Command", "Run custom shell scripts", true),
    mod_info!("Colors", "Display the terminal's 16-color palette", false),
    mod_info!("CPU", "Print CPU name, frequency, etc", true),
    mod_info!("CPUCache", "Print CPU caches", true),
    mod_info!("CPUUsage", "Print CPU usage", true),
    mod_info!("Cursor", "Print cursor style name", true),
    mod_info!("Custom", "Print a custom string, with or without key", true),
    mod_info!("DateTime", "Print the current date and time", true),
    mod_info!("DesktopEnvironment", "Print the desktop environment", true),
    mod_info!("Disk", "Print mounted disks and their usage", true),
    mod_info!("DiskIO", "Print disk I/O", true),
    mod_info!("Display", "Print displays and their specifications (size, resolution and refresh rate)", true),
    mod_info!("DNS", "Print configured DNS servers", true),
    mod_info!("Editor", "Print information about default editor ($VISUAL or $EDITOR)", false),
    mod_info!("Font", "Print system font names", true),
    mod_info!("Gamepad", "List connected gamepads", true),
    mod_info!("GPU", "Print GPU names, memory sizes and core counts", true),
    mod_info!("Host", "Print your computer's product name", true),
    mod_info!("InitSystem", "Print init system (pid 1) name and version", true),
    mod_info!("Kernel", "Print system kernel version", false),
    mod_info!("Keyboard", "List connected keyboards", true),
    mod_info!("LM", "Print login manager (desktop manager) name and version", true),
    mod_info!("Loadavg", "Print system load averages", true),
    mod_info!("Locale", "Print user locale", false),
    mod_info!("LocalIp", "List local IP addresses (IPv4 or IPv6), MAC addresses, etc", true),
    mod_info!("Logo", "Query built-in logo for JSON output", true),
    mod_info!("Media", "Print the name of currently playing song", true),
    mod_info!("Memory", "Print system memory usage information", true),
    mod_info!("Monitor", "Same as Display module, but with a different default output format", true),
    mod_info!("Mouse", "List connected mice", true),
    mod_info!("NetIO", "Print network I/O throughput", true),
    mod_info!("OpenCL", "Print the highest OpenCL version supported by the GPU", true),
    mod_info!("OpenGL", "Print the highest OpenGL version supported by the GPU", true),
    mod_info!("OS", "Print the OS or Linux distribution name and version", true),
    mod_info!("Packages", "List installed package managers and count of installed packages", true),
    mod_info!("PhysicalDisk", "Print physical disk information", true),
    mod_info!("PhysicalMemory", "Print system physical memory devices", true),
    mod_info!("Player", "Print the music player name that is currently active", false),
    mod_info!("PowerAdapter", "Print power adapter name and charging watts", true),
    mod_info!("Processes", "Print number of running processes", true),
    mod_info!("PublicIp", "Print your public IP address and related information", true),
    mod_info!("Separator", "Print a separator line", false),
    mod_info!("Shell", "Print the current shell name and version", true),
    mod_info!("Sound", "Print sound devices, volume levels, etc", true),
    mod_info!("Swap", "Print swap (paging file) space usage", true),
    mod_info!("Terminal", "Print the current terminal name and version", true),
    mod_info!("TerminalFont", "Print the font name and size used by the current terminal", true),
    mod_info!("TerminalSize", "Print the current terminal size", true),
    mod_info!("TerminalTheme", "Print the current terminal theme (foreground and background colors)", true),
    mod_info!("Title", "Print the title, including your username and hostname", false),
    mod_info!("Theme", "Print the current desktop environment theme", true),
    mod_info!("TPM", "Print information about the Trusted Platform Module (TPM) security device", true),
    mod_info!("Uptime", "Print how long the system has been running", true),
    mod_info!("Users", "Print users who are currently logged in", true),
    mod_info!("Version", "Print the Fastfetch version and build information", false),
    mod_info!("Vulkan", "Print the highest Vulkan version supported by the GPU", true),
    mod_info!("Wallpaper", "Print the file path of the current wallpaper", true),
    mod_info!("Weather", "Print weather information", true),
    mod_info!("WM", "Print the window manager name and version", true),
    mod_info!("Wifi", "Print connected Wi-Fi info (SSID, connection and security protocol)", true),
    mod_info!("WMTheme", "Print the current window manager theme", true),
    mod_info!("Zpool", "Print ZFS storage pools", true),
];

pub fn from_name(name: &str) -> Option<&'static ModuleInfo> {
    let lower = name.to_ascii_lowercase();
    MODULES
        .iter()
        .find(|m| m.name.to_ascii_lowercase() == lower)
}

pub fn list() -> impl Iterator<Item = &'static ModuleInfo> {
    MODULES.iter()
}

#[derive(Debug, Clone, Default)]
pub struct ModuleOutput {
    pub key: String,
    pub values: Vec<String>,

    pub supported: bool,

    pub blank: bool,
}

impl ModuleOutput {
    pub fn supported(key: &str, values: Vec<String>) -> Self {
        ModuleOutput {
            key: key.to_string(),
            values,
            supported: true,
            blank: false,
        }
    }
    pub fn blank() -> Self {
        ModuleOutput {
            key: String::new(),
            values: vec![String::new()],
            supported: true,
            blank: true,
        }
    }
    pub fn unsupported() -> Self {
        ModuleOutput {
            key: String::new(),
            values: Vec::new(),
            supported: false,
            blank: false,
        }
    }
}

pub struct ModuleInstance {
    pub module: String,
    pub entry: ModuleEntry,
    pub args: ModuleArgs,
    pub raw: Option<crate::config::json::JsonValue>,
}

pub fn run_instance(
    inst: &ModuleInstance,
    cfg: &crate::config::configfile::Config,
) -> Option<ModuleOutput> {
    exec::run(inst, cfg)
}

mod exec;
pub(crate) mod exec_impl;
