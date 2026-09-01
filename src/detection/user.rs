use crate::detection::{getenv, read_file};

#[derive(Debug, Clone, Default)]
pub struct UserInfo {
    pub user_name: String,
    pub user_name_part: String,
    pub host_name: String,
    pub host_name_part: String,
}

pub fn detect() -> UserInfo {
    let user = getenv("LOGNAME")
        .or_else(|| getenv("USER"))
        .or_else(|| {
            getpwuid_name().map(|s| s.to_string())
        })
        .unwrap_or_else(|| "unknown".to_string());

    let host = getenv("HOSTNAME")
        .or_else(|| read_file("/proc/sys/kernel/hostname").map(|s| s.trim().to_string()))
        .or_else(|| get_hostname_syscall())
        .unwrap_or_else(|| "unknown".to_string());

    let mut info = UserInfo {
        user_name: user.clone(),
        host_name: host.clone(),
        ..Default::default()
    };

    // Apply user@host truncation rules fastfetch uses.
    info.user_name_part = user.split('@').next().unwrap_or(&user).to_string();
    info.host_name_part = host.split('.').next().unwrap_or(&host).to_string();
    info
}

fn getpwuid_name() -> Option<String> {
    let uid = unsafe { libc::geteuid() };
    unsafe {
        let pwd = libc::getpwuid(uid);
        if pwd.is_null() {
            return None;
        }
        let name = (*pwd).pw_name;
        if name.is_null() {
            return None;
        }
        Some(std::ffi::CStr::from_ptr(name).to_string_lossy().into_owned())
    }
}

fn get_hostname_syscall() -> Option<String> {
    let mut buf = [0i8; 256];
    let ret = unsafe { libc::gethostname(buf.as_mut_ptr() as *mut libc::c_char, buf.len()) };
    if ret == 0 {
        let s = unsafe { std::ffi::CStr::from_ptr(buf.as_ptr()) };
        Some(s.to_string_lossy().into_owned())
    } else {
        None
    }
}
