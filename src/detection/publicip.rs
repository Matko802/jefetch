use std::net::TcpStream;
use std::time::Duration;

/// Fetch IPv4 address by talking plain HTTP over raw TCP sockets.
/// Each provider is probed in its own worker thread (so a hung network node
/// can't stall the others); the first valid reply wins.
pub fn detect(timeout_ms: u128) -> Option<String> {
    let (tx, rx) = std::sync::mpsc::channel::<String>();
    for host in ["api.ipify.org", "ipv4.icanhazip.com", "ifconfig.me", "ipinfo.io/ip"] {
        let tx = tx.clone();
        std::thread::spawn(move || {
            if let Some(ip) = http_get(host, 80) {
                let _ = tx.send(ip);
            }
        });
    }
    drop(tx);
    match rx.recv_timeout(Duration::from_millis(timeout_ms as u64)) {
        Ok(ip) => Some(ip),
        Err(_) => None,
    }
}

fn http_get(host: &str, port: u16) -> Option<String> {
    // (host, port) triggers getaddrinfo-based resolution; blocking only the
    // worker thread (the channel timeout bounds the total fetch time).
    let stream = TcpStream::connect((host, port)).ok()?;
    stream.set_read_timeout(Some(Duration::from_secs(4))).ok()?;
    stream.set_write_timeout(Some(Duration::from_secs(4))).ok()?;
    use std::io::{Read, Write};
    let mut s = stream;
    let req = format!(
        "GET / HTTP/1.0\r\nHost: {}\r\nUser-Agent: sharkfetch/0.1\r\nAccept: text/plain\r\nConnection: close\r\n\r\n",
        host
    );
    s.write_all(req.as_bytes()).ok()?;
    let mut buf = Vec::new();
    s.read_to_end(&mut buf).ok()?;
    let body = String::from_utf8_lossy(&buf);
    body.lines()
        .map(|l| l.trim())
        .find(|l| {
            l.chars().all(|c| c.is_ascii_digit() || c == '.')
                && l.split('.').count() == 4
        })
        .map(|l| l.to_string())
}