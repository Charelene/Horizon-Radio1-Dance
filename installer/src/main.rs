#[macro_use]
mod util;
mod data;
mod locate;
mod net;
mod station;
mod tools;

use std::path::{Path, PathBuf};
use util::{ask_yes_no, fail, remove_dir_if_exists, Result};

const HELP: &str = "BBC Radio 1 Dance installer for Forza Horizon 2 (Azimuth)

usage: r1d-installer [options]
  --azimuth <dir>    the Azimuth folder (the one with azimuth.exe)
  --game <dir>       the Forza Horizon 2 folder holding media/ (found from Azimuth's settings by default)
  --uninstall        remove the mod and everything it installed
  --pause-on-error   wait for Enter after an error (for when it runs in its own window)
  -y, --yes          do not ask questions
";

#[derive(Default)]
pub struct Args {
    pub azimuth: Option<PathBuf>,
    pub game: Option<PathBuf>,
    pub uninstall: bool,
    pub pause: bool,
}

fn parse_args() -> Result<Args> {
    let mut a = Args::default();
    let mut it = std::env::args().skip(1);
    while let Some(arg) = it.next() {
        let mut value = |what: &str| it.next().ok_or_else(|| format!("{arg} needs {what}"));
        match arg.as_str() {
            "--azimuth" => a.azimuth = Some(value("a folder")?.into()),
            "--game" => a.game = Some(value("a folder")?.into()),
            "--uninstall" => a.uninstall = true,
            "--pause-on-error" => a.pause = true,
            "-y" | "--yes" => util::assume_yes(true),
            "-h" | "--help" => {
                print!("{HELP}");
                std::process::exit(0);
            }
            other => return fail(format!("unknown option {other}\n\n{HELP}")),
        }
    }
    Ok(a)
}

fn main() {
    let args = match parse_args() {
        Ok(a) => a,
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(2);
        }
    };
    let code = match run(&args) {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("\nerror: {e}");
            if args.pause {
                util::wait_for_enter();
            }
            1
        }
    };
    std::process::exit(code);
}

pub fn run(args: &Args) -> Result<()> {
    say!("{} for Forza Horizon 2\n", data::STATION_NAME);
    let azimuth = locate::azimuth_dir(args.azimuth.as_deref())?;
    say!("Azimuth: {}", azimuth.display());
    if args.uninstall {
        return uninstall(&azimuth);
    }
    install(args, &azimuth)
}

fn uninstall(azimuth: &Path) -> Result<()> {
    if !ask_yes_no(&format!("Remove {}?", data::STATION_NAME), true) {
        return Ok(());
    }
    let dll = azimuth.join("fh2-mods").join(data::MOD_FILE);
    if dll.exists() {
        std::fs::remove_file(&dll).map_err(|e| format!("cannot remove {} (is the game running?): {e}", dll.display()))?;
    }
    // best effort: on Windows the running installer (and the setup's uninstaller) sit in this folder and cannot delete themselves
    if let Err(e) = remove_dir_if_exists(&azimuth.join(data::MOD_DIR)) {
        say!("  could not remove everything in the {} folder ({e})", data::MOD_DIR);
    }
    say!("Removed. The FH2 Mod Platform and your other mods were left alone.");
    for d in data::COMPANION_DIRS {
        if azimuth.join(d).join("overrides").is_dir() {
            say!("{d} takes its station files back the next time the game starts.");
            break;
        }
    }
    Ok(())
}

fn install(args: &Args, azimuth: &Path) -> Result<()> {
    let media = locate::media_dir(azimuth, args.game.as_deref())?;
    say!("Game media: {}", media.display());
    let st = data::station();
    let me = azimuth.join(data::MOD_DIR);
    let overrides = me.join("overrides");
    std::fs::create_dir_all(&me)?;

    say!("\n[1/3] Tools");
    ensure_platform(azimuth)?;
    let ffmpeg = tools::ffmpeg(&me.join("tools"))?;
    say!("  ffmpeg: {}", ffmpeg.display());

    say!("\n[2/3] Building the station from your game files");
    let plan = station::plan(azimuth, &media)?;
    let number = station::pick_number(&plan.used);
    say!("  radios already installed use station numbers {:?}; this one takes {number}", plan.used);
    if let Some(c) = plan.companion {
        say!("  {c} is installed: building on top of its files so its stations stay on the radio");
    }
    remove_dir_if_exists(&overrides)?;
    let langs = station::write_radio_info(&plan, &overrides.join("audio"), &station::station_block(&st, number))?;
    let hud_dir = overrides.join("UI").join("Controls");
    std::fs::create_dir_all(&hud_dir)?;
    std::fs::write(hud_dir.join("HUDRadioControl.xaml"), station::patched_hud(&media, plan.hud.as_deref())?)?;
    let icon_dir = overrides.join("UI").join("Textures").join("Anthem").join("HUD");
    std::fs::create_dir_all(&icon_dir)?;
    std::fs::write(icon_dir.join(format!("{}.swatchbin", data::ICON_NAME)), data::ICON)?;

    say!("\n[3/3] The mod");
    let ini = me.join("stream.ini");
    if ini.is_file() {
        say!("  kept your stream.ini (delete it and run this again for the default stream)");
    } else {
        std::fs::write(&ini, format!(
            "; {} for Forza Horizon 2: where the live audio and the song names come from.\r\n\
             ; url: the BBC's HLS stream. service: the id of the BBC's now-playing feed.\r\n\
             ; Delete this file and run the installer again to get the defaults back.\r\n\
             [stream]\r\nurl={}\r\nservice={}\r\n",
            data::STATION_NAME, st.stream.url, st.stream.service
        ))?;
    }
    let dll = azimuth.join("fh2-mods").join(data::MOD_FILE);
    std::fs::create_dir_all(azimuth.join("fh2-mods"))?;
    std::fs::write(&dll, data::MOD_DLL).map_err(|e| format!("cannot write {} (is the game running?): {e}", dll.display()))?;
    remove_dir_if_exists(&me.join("cache"))?;

    say!("\nDone: {langs} languages patched, station number {number}, mod installed.");
    if plan.companion.is_some() {
        say!("Run this installer again after you reinstall or update the other radio mods, so their stations stay on the radio.");
    }
    say!("Start the game and scroll the radio to \"{}\". It plays live: the stream is only downloaded while the station is on.", data::STATION_NAME);
    Ok(())
}

/// The mod needs the FH2 Mod Platform next to azimuth.exe; offer to fetch it if it is missing.
fn ensure_platform(azimuth: &Path) -> Result<()> {
    if azimuth.join("fh2-mods").is_dir() && (azimuth.join("DWrite.dll").is_file() || azimuth.join("d3dcompiler_47.dll").is_file()) {
        say!("  FH2 Mod Platform found");
        return Ok(());
    }
    say!("  The FH2 Mod Platform (https://github.com/Trinculo54/fh2-mod-platform) is not installed.");
    if !ask_yes_no("  Download and install it into your Azimuth folder?", true) {
        return fail(format!("{} needs the FH2 Mod Platform. Install it, then run this again.", data::STATION_NAME));
    }
    platform::install(azimuth)
}

mod platform {
    use super::*;
    use std::io::{BufReader, Read};

    pub fn install(azimuth: &Path) -> Result<()> {
        let rel: serde_json::Value = serde_json::from_str(&net::get_text("https://api.github.com/repos/Trinculo54/fh2-mod-platform/releases/latest")?)?;
        let asset = rel["assets"].as_array().and_then(|a| a.iter().find(|x| x["name"].as_str().is_some_and(|n| n.ends_with(".zip"))));
        let (Some(url), Some(digest)) = (asset.and_then(|a| a["browser_download_url"].as_str()), asset.and_then(|a| a["digest"].as_str())) else {
            return fail("could not read the FH2 Mod Platform release; install it by hand from its GitHub page");
        };
        let sha = digest.strip_prefix("sha256:").unwrap_or(digest);
        let tmp = azimuth.join(data::MOD_DIR).join("cache").join("platform.zip");
        net::download(url, &tmp, Some(sha), "FH2 Mod Platform")?;
        let mut z = zip::ZipArchive::new(BufReader::new(std::fs::File::open(&tmp)?))?;
        std::fs::create_dir_all(azimuth.join("fh2-mods"))?;
        for (member, dest) in [
            ("DWrite.dll", "DWrite.dll"), ("d3dcompiler_47.dll", "d3dcompiler_47.dll"),
            ("LICENSE.txt", "LICENSE-FH2ModPlatform.txt"), ("LICENSE-MinHook.txt", "LICENSE-MinHook.txt"),
        ] {
            let mut data = vec![];
            z.by_name(member).map_err(|_| format!("platform zip has no {member}"))?.read_to_end(&mut data)?;
            let out = azimuth.join(dest);
            if out.is_file() && std::fs::read(&out)? != data {
                std::fs::rename(&out, azimuth.join(format!("{dest}.before-radio1-dance")))?;
            }
            std::fs::write(out, data)?;
        }
        std::fs::remove_file(tmp).ok();
        Ok(())
    }
}
