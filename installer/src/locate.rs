//! Find the Azimuth folder and the player's own Forza Horizon 2 media folder.
use crate::util::{ask_line, fail, Result};
use std::path::{Path, PathBuf};

fn is_azimuth(p: &Path) -> bool {
    p.join("azimuth.exe").is_file()
}

/// Look for Azimuth in the usual places without asking.
pub fn find_azimuth() -> Option<PathBuf> {
    let mut candidates = vec![std::env::current_dir().unwrap_or_default()];
    if let Ok(exe) = std::env::current_exe() {
        candidates.extend(exe.parent().map(Path::to_path_buf));
    }
    if let Some(home) = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")).map(PathBuf::from) {
        candidates.extend(["Games/Azimuth", "Azimuth", "Downloads/Azimuth", "Desktop/Azimuth"].map(|s| home.join(s)));
    }
    candidates.into_iter().find(|p| is_azimuth(p))
}

pub fn azimuth_dir(arg: Option<&Path>) -> Result<PathBuf> {
    if let Some(p) = arg {
        return if is_azimuth(p) { Ok(p.to_path_buf()) } else { fail(format!("{} has no azimuth.exe", p.display())) };
    }
    if let Some(p) = find_azimuth() {
        return Ok(p);
    }
    say!("Could not find Azimuth by itself.");
    while let Some(line) = ask_line("Path to your Azimuth folder (the one with azimuth.exe), or Enter to quit:") {
        let p = PathBuf::from(line);
        if is_azimuth(&p) {
            return Ok(p);
        }
        say!("  no azimuth.exe in {}", p.display());
    }
    fail("Azimuth folder not found. Run again with --azimuth <folder>.")
}

/// The folder holding `audio/`, `UI.zip` and the rest of the game's media.
pub fn media_dir(azimuth: &Path, arg: Option<&Path>) -> Result<PathBuf> {
    let check = |media: &Path| media.join("audio").join("RadioInfo_EN.xml").is_file() && media.join("UI.zip").is_file();
    if let Some(p) = arg {
        for m in [p.to_path_buf(), p.join("media"), p.join("Mount").join("media")] {
            if check(&m) {
                return Ok(m);
            }
        }
        return fail(format!("{} does not look like the Forza Horizon 2 media folder", p.display()));
    }
    for folder in game_folders(azimuth) {
        for m in [folder.join("Mount").join("media"), folder.join("media")] {
            if check(&m) {
                return Ok(m);
            }
        }
    }
    fail("Could not find your Forza Horizon 2 files from Azimuth's settings. Run again with --game <folder with media>.")
}

/// Game folders listed in Azimuth's data/azimuth.ini, the selected title first.
fn game_folders(azimuth: &Path) -> Vec<PathBuf> {
    let Ok(raw) = std::fs::read(azimuth.join("data").join("azimuth.ini")) else { return vec![] };
    let text = if raw.starts_with(&[0xFF, 0xFE]) {
        String::from_utf16_lossy(&raw[2..].chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect::<Vec<_>>())
    } else {
        String::from_utf8_lossy(&raw).into_owned()
    };
    let (mut section, mut selected) = (String::new(), String::new());
    let mut folders: Vec<(String, String)> = vec![];
    for line in text.lines().map(str::trim) {
        if let Some(s) = line.strip_prefix('[').and_then(|l| l.strip_suffix(']')) {
            section = s.to_string();
        } else if let Some((k, v)) = line.split_once('=') {
            match (k.trim(), section.as_str()) {
                ("game", "launcher") => selected = format!("title-{}", v.trim()),
                ("folder", s) if s.starts_with("title-") => folders.push((s.to_string(), v.trim().to_string())),
                _ => {}
            }
        }
    }
    folders.sort_by_key(|(s, _)| *s != selected);
    folders.into_iter().map(|(_, f)| native_path(&f, azimuth)).collect()
}

/// azimuth.ini stores Windows paths. On Linux (Wine/Proton) map Z:\ to / and C:\ into the prefix Azimuth runs in.
fn native_path(win: &str, azimuth: &Path) -> PathBuf {
    if cfg!(windows) {
        return PathBuf::from(win);
    }
    let rest = win.get(2..).unwrap_or("").replace('\\', "/");
    let drive = win.chars().next().map(|c| c.to_ascii_uppercase());
    match drive {
        Some('Z') => PathBuf::from(rest),
        Some('C') => {
            let s = azimuth.to_string_lossy();
            match s.find("/drive_c") {
                Some(i) => PathBuf::from(format!("{}/drive_c{rest}", &s[..i])),
                None => PathBuf::from(rest),
            }
        }
        _ => PathBuf::from(rest),
    }
}
