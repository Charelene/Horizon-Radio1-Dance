//! HTTPS downloads with SHA-256 verification.
use crate::util::{fail, mb, Line, Result};
use sha2::{Digest, Sha256};
use std::fs::File;
use std::io::{Read, Write};
use std::path::Path;
use std::sync::OnceLock;
use std::time::Duration;

fn agent() -> &'static ureq::Agent {
    static AGENT: OnceLock<ureq::Agent> = OnceLock::new();
    AGENT.get_or_init(|| {
        ureq::Agent::config_builder()
            .user_agent("radio1-dance-installer")
            .timeout_connect(Some(Duration::from_secs(20)))
            .timeout_recv_response(Some(Duration::from_secs(60)))
            .build()
            .into()
    })
}

pub fn get_text(url: &str) -> Result<String> {
    let mut resp = agent().get(url).call().map_err(|e| format!("GET {url}: {e}"))?;
    Ok(resp.body_mut().read_to_string().map_err(|e| format!("GET {url}: {e}"))?)
}

pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// Pick a file's hash out of a "<hash>  <name>" checksum list.
pub fn sums_lookup(sums: &str, file: &str) -> Option<String> {
    sums.lines().find_map(|l| {
        let mut it = l.split_whitespace();
        let (h, n) = (it.next()?, it.next()?);
        (n.trim_start_matches('*') == file).then(|| h.to_ascii_lowercase())
    })
}

/// Download `url` to `dest`. With `sha`, the file is only kept if its SHA-256 matches.
pub fn download(url: &str, dest: &Path, sha: Option<&str>, label: &str) -> Result<()> {
    if let Some(dir) = dest.parent() {
        std::fs::create_dir_all(dir)?;
    }
    let resp = agent().get(url).call().map_err(|e| format!("download {url}: {e}"))?;
    let total = resp.body().content_length();
    let mut reader = resp.into_body().into_reader();
    let part = dest.with_extension("part");
    let mut out = File::create(&part)?;
    let mut hasher = Sha256::new();
    let mut buf = vec![0u8; 256 * 1024];
    let (mut got, mut line) = (0u64, Line::new());
    loop {
        let n = reader.read(&mut buf).map_err(|e| format!("download {url}: {e}"))?;
        if n == 0 {
            break;
        }
        out.write_all(&buf[..n])?;
        hasher.update(&buf[..n]);
        got += n as u64;
        match total {
            Some(t) if t > 0 => line.update(&format!("{label}  {}%  {}", got * 100 / t, mb(got))),
            _ => line.update(&format!("{label}  {}", mb(got))),
        }
    }
    out.flush()?;
    drop(out);
    let digest = hex(&hasher.finalize());
    if let Some(want) = sha {
        if !digest.eq_ignore_ascii_case(want) {
            std::fs::remove_file(&part).ok();
            return fail(format!("checksum mismatch for {label}: expected {want}, got {digest}. Nothing was installed from it."));
        }
    }
    std::fs::rename(&part, dest)?;
    line.done(&format!("{label}  {}{}", mb(got), if sha.is_some() { "  (sha256 ok)" } else { "" }));
    Ok(())
}
