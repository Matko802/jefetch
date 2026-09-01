use crate::detection::getenv;
use std::collections::HashMap;

// SIOC* request codes are not exported by libc for musl; the values are ABI
// stable across Linux architectures.
const SIOCGIFFLAGS: libc::c_int = 0x8913;
const SIOCGIFMTU: libc::c_int = 0x8921;
const SIOCGIFHWADDR: libc::c_int = 0x8927;
const SIOCETHTOOL: libc::c_int = 0x8946;

#[derive(Debug, Clone, Default)]
pub struct IpInfo {
    pub name: String,
    pub ipv4: Vec<String>,
    pub ipv6: Vec<String>,
    pub mac: String,
    pub mtu: u64,
    pub speed: u64,
    pub flags: String,
}

pub fn detect() -> Vec<IpInfo> {
    let mut map: HashMap<String, IpInfo> = HashMap::new();
    addrs_via_getifaddrs(&mut map);
    for info in map.values_mut() {
        if info.mac.is_empty() {
            info.mac = mac_for(&info.name);
        }
        if info.mtu == 0 {
            info.mtu = mtu_for(&info.name);
        }
        info.speed = speed_mbps(&info.name);
        if info.flags.is_empty() {
            info.flags = flags_for(&info.name);
        }
    }
    let mut v: Vec<IpInfo> = map.into_values().collect();
    v.sort_by(|a, b| a.name.cmp(&b.name));
    v.into_iter()
        .filter(|i| !(i.ipv4.is_empty() && i.ipv6.is_empty()))
        .collect()
}

/// Enumerate interface addresses (IPv4 + IPv6) with getifaddrs.
fn addrs_via_getifaddrs(map: &mut HashMap<String, IpInfo>) {
    const SKIP: &[&str] = &["lo"];
    unsafe {
        let mut ifap: *mut libc::ifaddrs = std::ptr::null_mut();
        if libc::getifaddrs(&mut ifap) != 0 {
            return;
        }
        let mut p = ifap;
        while !p.is_null() {
            let ifa = &*p;
            let name = std::ffi::CStr::from_ptr(ifa.ifa_name)
                .to_string_lossy()
                .into_owned();
            if SKIP.contains(&name.as_str()) {
                p = ifa.ifa_next;
                continue;
            }
            let entry = map.entry(name.clone()).or_default();
            entry.name = name.clone();
            let addr = ifa.ifa_addr;
            if !addr.is_null() {
                match (*addr).sa_family as i32 {
                    libc::AF_INET => {
                        let sa = &*(addr as *const libc::sockaddr_in);
                        let ip = format_addr4(sa.sin_addr.s_addr);
                        if !ip.is_empty() && !entry.ipv4.contains(&ip) {
                            entry.ipv4.push(ip);
                        }
                    }
                    libc::AF_INET6 => {
                        let sa = &*(addr as *const libc::sockaddr_in6);
                        let ip = format_addr6(&sa.sin6_addr);
                        if !ip.is_empty() && !entry.ipv6.contains(&ip) {
                            entry.ipv6.push(ip);
                        }
                    }
                    _ => {}
                }
            }
            p = ifa.ifa_next;
        }
        libc::freeifaddrs(ifap);
    }
}

/// Render the raw 32-bit s_addr as an IPv4 dotted string.
fn format_addr4(s_addr: u32) -> String {
    let b = match cfg!(target_endian = "little") {
        true => s_addr.to_le_bytes(),
        false => s_addr.to_be_bytes(),
    };
    format!("{}.{}.{}.{}", b[0], b[1], b[2], b[3])
}

/// Format an IPv6 address manually (avoid inet_ntop availability issues).
fn format_addr6(addr: &libc::in6_addr) -> String {
    let bytes = addr.s6_addr;
    let groups: Vec<u16> = bytes
        .chunks_exact(2)
        .map(|c| u16::from_be_bytes([c[0], c[1]]))
        .collect();
    // Longest run of consecutive zero groups collapses to "::".
    let mut best: (usize, usize) = (0, 0);
    let mut i = 0;
    while i < groups.len() {
        if groups[i] == 0 {
            let start = i;
            while i < groups.len() && groups[i] == 0 {
                i += 1;
            }
            if i - start > best.1 && i - start >= 2 {
                best = (start, i - start);
            }
        } else {
            i += 1;
        }
    }
    let mut out = String::new();
    if best.1 > 0 {
        let (s, l) = best;
        let head = if s == 0 { String::new() } else { format_hex(&groups[..s]) };
        let tail = if s + l == groups.len() {
            String::new()
        } else {
            format_hex(&groups[s + l..])
        };
        out.push_str(&head);
        out.push_str("::");
        out.push_str(&tail);
    } else if groups.iter().any(|&g| g == 0) {
        // Single zero group cannot elide; fall back to full form.
        out = format_hex(&groups);
    } else {
        out = format_hex(&groups);
    }
    out
}

fn format_hex(groups: &[u16]) -> String {
    groups
        .iter()
        .map(|g| format!("{:x}", g))
        .collect::<Vec<_>>()
        .join(":")
}

fn mac_for(ifname: &str) -> String {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    if fd < 0 {
        return String::new();
    }
    let mut namebuf = [0i8; libc::IFNAMSIZ];
    for (dst, src) in namebuf.iter_mut().zip(ifname.as_bytes()) {
        *dst = *src as i8;
    }
    let mut ifr: libc::ifreq = unsafe { std::mem::zeroed() };
    ifr.ifr_name = namebuf;
    let ret = unsafe { libc::ioctl(fd, SIOCGIFHWADDR, &mut ifr) };
    unsafe { libc::close(fd) };
    if ret != 0 {
        return String::new();
    }
    let data = unsafe { ifr.ifr_ifru.ifru_hwaddr.sa_data };
    let mut mac = String::new();
    for i in 0..6 {
        if i > 0 {
            mac.push(':');
        }
        mac.push_str(&format!("{:02x}", data[i] as u8));
    }
    mac
}

fn mtu_for(ifname: &str) -> u64 {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    if fd < 0 {
        return 0;
    }
    let mut namebuf = [0i8; libc::IFNAMSIZ];
    for (dst, src) in namebuf.iter_mut().zip(ifname.as_bytes()) {
        *dst = *src as i8;
    }
    let mut ifr: libc::ifreq = unsafe { std::mem::zeroed() };
    ifr.ifr_name = namebuf;
    let ret = unsafe { libc::ioctl(fd, SIOCGIFMTU, &mut ifr) };
    unsafe { libc::close(fd) };
    if ret != 0 {
        return 0;
    }
    unsafe { ifr.ifr_ifru.ifru_mtu as u64 }
}

/// Link speed in Mbps via ethtool ioctl (legacy ETHTOOL_GSET).
fn speed_mbps(ifname: &str) -> u64 {
    const ETHTOOL_GSET: u32 = 0x00000001;
    #[repr(C)]
    struct ethtool_cmd {
        cmd: u32,
        supported: u32,
        advertising: u32,
        speed: u32,
        duplex: u8,
        port: u8,
        phy_address: u8,
        autoneg: u8,
        mdio_support: u8,
        maxtxpkt: u8,
        maxrxpkt: u8,
        speed_hi: u16,
        eth_tp_mdix_ctrl: u8,
        eth_tp_mdix: u8,
        lp_advertising: u32,
        reserved: [u32; 2],
    }
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    if fd < 0 {
        return 0;
    }
    let mut namebuf = [0i8; libc::IFNAMSIZ];
    for (dst, src) in namebuf.iter_mut().zip(ifname.as_bytes()) {
        *dst = *src as i8;
    }
    let mut ifr: libc::ifreq = unsafe { std::mem::zeroed() };
    ifr.ifr_name = namebuf;
    let mut data: ethtool_cmd = unsafe { std::mem::zeroed() };
    data.cmd = ETHTOOL_GSET;
    ifr.ifr_ifru.ifru_data = &mut data as *mut ethtool_cmd as *mut libc::c_char;
    let ret = unsafe { libc::ioctl(fd, SIOCETHTOOL, &mut ifr) };
    unsafe { libc::close(fd) };
    if ret != 0 {
        return 0;
    }
    let speed = data.speed as u64 | ((data.speed_hi as u64) << 16);
    if speed == 0xffff || speed == 0 {
        return 0;
    }
    speed
}

fn flags_for(ifname: &str) -> String {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    if fd < 0 {
        return String::new();
    }
    let mut namebuf = [0i8; libc::IFNAMSIZ];
    for (dst, src) in namebuf.iter_mut().zip(ifname.as_bytes()) {
        *dst = *src as i8;
    }
    let mut ifr: libc::ifreq = unsafe { std::mem::zeroed() };
    ifr.ifr_name = namebuf;
    let ret = unsafe { libc::ioctl(fd, SIOCGIFFLAGS, &mut ifr) };
    unsafe { libc::close(fd) };
    if ret != 0 {
        return String::new();
    }
    let f = unsafe { ifr.ifr_ifru.ifru_flags } as i64;
    let mut parts = Vec::new();
    if f & libc::IFF_UP as i64 != 0 {
        parts.push("UP");
    }
    if f & libc::IFF_BROADCAST as i64 != 0 {
        parts.push("BROADCAST");
    }
    if f & libc::IFF_LOOPBACK as i64 != 0 {
        parts.push("LOOPBACK");
    }
    if f & libc::IFF_RUNNING as i64 != 0 {
        parts.push("RUNNING");
    }
    if f & libc::IFF_MULTICAST as i64 != 0 {
        parts.push("MULTICAST");
    }
    parts.join(",")
}

/// Convenience for the dns module: the system hostname (used by modules
/// that need it without full user detection).
pub fn hostname_hint() -> Option<String> {
    getenv("HOSTNAME").filter(|h| !h.is_empty())
}