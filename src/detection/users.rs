use crate::detection::getenv;
use std::fs::File;
use std::io::Read;

#[derive(Debug, Clone, Default)]
pub struct UserInfo {
    pub user: String,
    pub tty: String,
    pub host: String,
}

#[repr(C)]
struct UtmpEntry {
    ut_type: i16,
    __pad1: i16,
    ut_pid: i32,
    ut_line: [u8; 32],
    ut_id: [u8; 4],
    ut_user: [u8; 32],
    ut_host: [u8; 256],
    ut_exit: [u8; 8],
    ut_session: i64,
    ut_tv: [u8; 16],
    ut_addr_v6: [u8; 16],
    __glibc_reserved: [u8; 20],
}

const USER_PROCESS: i16 = 7;

const RECORD_STRIDES: [usize; 3] = [384, 400, 380];

pub fn detect() -> Vec<UserInfo> {
    const LINE_LEN: usize = 32;
    const USER_LEN: usize = 32;
    const HOST_LEN: usize = 256;

    let path = ["/run/utmp", "/var/run/utmp", "/var/adm/utmpx"]
        .iter()
        .find(|p| std::path::Path::new(p).exists());
    let Some(path) = path else { return Vec::new() };
    let Ok(mut f) = File::open(path) else { return Vec::new() };
    let mut buf = Vec::new();
    if f.read_to_end(&mut buf).is_err() {
        return Vec::new();
    }

    let mut best: Vec<UserInfo> = Vec::new();
    for &stride in &RECORD_STRIDES {
        let out = parse_with_stride(&buf, stride, LINE_LEN, USER_LEN, HOST_LEN);
        if out.len() > best.len() {
            best = out;
        }
    }
    best
}

fn parse_with_stride(buf: &[u8], stride: usize, line_len: usize, user_len: usize, host_len: usize) -> Vec<UserInfo> {
    let mut out = Vec::new();
    let mut off = 0usize;
    while off + stride <= buf.len() {
        let mut entry: UtmpEntry = UtmpEntry {
            ut_type: 0,
            __pad1: 0,
            ut_pid: 0,
            ut_line: [0; 32],
            ut_id: [0; 4],
            ut_user: [0; 32],
            ut_host: [0; 256],
            ut_exit: [0; 8],
            ut_session: 0,
            ut_tv: [0; 16],
            ut_addr_v6: [0; 16],
            __glibc_reserved: [0; 20],
        };
        let dst = unsafe {
            std::slice::from_raw_parts_mut(
                &mut entry as *mut UtmpEntry as *mut u8,
                std::mem::size_of::<UtmpEntry>(),
            )
        };
        let n = dst.len().min(stride);
        dst[..n].copy_from_slice(&buf[off..off + n]);
        if entry.ut_type != USER_PROCESS {
            off += stride;
            continue;
        }
        let user = cstr_of(&entry.ut_user[..user_len]);
        if user.is_empty() {
            off += stride;
            continue;
        }
        out.push(UserInfo {
            user,
            tty: cstr_of(&entry.ut_line[..line_len]),
            host: cstr_of(&entry.ut_host[..host_len]),
        });
        off += stride;
    }
    out
}

fn cstr_of(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..end]).into_owned()
}

pub fn hint() -> String {
    let v = detect();
    if !v.is_empty() {
        return v[0].user.clone();
    }
    getenv("USER").unwrap_or_default()
}
