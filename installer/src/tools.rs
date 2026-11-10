//! ffmpeg for the station's runtime: the DLL starts it to decode the BBC stream while the station is heard. It is always
//! the Windows build, because the game runs Windows code (under Wine/Proton too). Fetched from BtbN's latest release into
//! <azimuth>/Radio1Dance/tools and checked against the SHA-256 the project publishes. Nothing here runs it.
use crate::net::{download, get_text, sums_lookup};
use crate::util::{fail, Result};
use std::fs::File;
use std::io::{copy, BufReader};
use std::path::{Path, PathBuf};

const BASE: &str = "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download";
const ASSET: &str = "ffmpeg-master-latest-win64-lgpl.zip";

/// `<dir>/ffmpeg.exe`, downloaded unless an earlier install left a verified copy.
pub fn ffmpeg(dir: &Path) -> Result<PathBuf> {
    std::fs::create_dir_all(dir)?;
    let exe = dir.join("ffmpeg.exe");
    let stamp = dir.join("ffmpeg.sha256");
    if exe.is_file() && stamp.is_file() {
        say!("  ffmpeg is already installed (delete {} to fetch the newest)", dir.display());
        return Ok(exe);
    }
    let sums = get_text(&format!("{BASE}/checksums.sha256"))?;
    let Some(want) = sums_lookup(&sums, ASSET) else { return fail(format!("no checksum for {ASSET} in BtbN's checksums.sha256")) };
    let archive = dir.join(ASSET);
    download(&format!("{BASE}/{ASSET}"), &archive, Some(&want), "ffmpeg")?;
    say!("  unpacking ffmpeg ...");
    extract_exe(&archive, &exe)?;
    std::fs::remove_file(&archive).ok();
    std::fs::write(stamp, want)?;
    Ok(exe)
}

/// Pull `bin/ffmpeg.exe` out of the zip.
fn extract_exe(archive: &Path, out: &Path) -> Result<()> {
    let mut z = zip::ZipArchive::new(BufReader::new(File::open(archive)?))?;
    for i in 0..z.len() {
        let mut e = z.by_index(i)?;
        if !e.is_dir() && e.name()?.replace('\\', "/").ends_with("/bin/ffmpeg.exe") {
            copy(&mut e, &mut File::create(out)?)?;
            return Ok(());
        }
    }
    fail(format!("{} has no bin/ffmpeg.exe", archive.display()))
}
