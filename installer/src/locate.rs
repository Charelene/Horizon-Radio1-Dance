//! Find the Azimuth folder and the player's own Forza Horizon 2 media folder.
use crate::util::{ask_line, can_ask, fail, Result};
use std::path::{Path, PathBuf};

const EXE: &str = "azimuth.exe";
/// Forza Horizon 2's section in Azimuth's data/azimuth.ini.
const FH2_TITLE: &str = "title-1a96545b";

/// The Azimuth folder a path names: the folder itself, or the azimuth.exe in it.
fn azimuth_at(p: &Path) -> Option<PathBuf> {
    if p.join(EXE).is_file() {
        return Some(p.to_path_buf());
    }
    if p.is_file() && p.file_name().is_some_and(|n| n.eq_ignore_ascii_case(EXE)) {
        return Some(match p.parent() {
            Some(dir) if !dir.as_os_str().is_empty() => dir.to_path_buf(),
            _ => PathBuf::from("."),
        });
    }
    None
}

/// A folder's subfolders by name, without the hidden ones. At most 300: this is for quick looks, not searches.
fn subdirs(dir: &Path) -> Vec<PathBuf> {
    let Ok(entries) = std::fs::read_dir(dir) else { return vec![] };
    let mut dirs: Vec<PathBuf> = entries
        .flatten()
        .filter(|e| !e.file_name().to_string_lossy().starts_with('.'))
        .map(|e| e.path())
        .filter(|p| p.is_dir())
        .take(300)
        .collect();
    dirs.sort();
    dirs
}

/// Azimuth folders in the places people unpack it, the likeliest first. Its zip unpacks as "azimuth-windows" and
/// people rename it, so the name says nothing: look at each place, its subfolders, and theirs where games are kept.
pub fn find_azimuth() -> Vec<PathBuf> {
    let mut places = vec![(std::env::current_dir().unwrap_or_default(), 1)];
    if let Ok(exe) = std::env::current_exe() {
        places.extend(exe.parent().map(|dir| (dir.to_path_buf(), 1)));
    }
    if let Some(home) = std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")).map(PathBuf::from) {
        places.extend(["Games", "Downloads", "Desktop", "Documents"].map(|s| (home.join(s), 2)));
        places.push((home, 1));
    }
    if cfg!(windows) {
        places.extend(["C:\\Games", "C:\\Azimuth"].map(|s| (PathBuf::from(s), 2)));
    }
    azimuths_in(places)
}

/// The Azimuth folders in each place, looking `depth` levels of subfolders into it. Each folder is listed once.
fn azimuths_in(places: Vec<(PathBuf, usize)>) -> Vec<PathBuf> {
    let (mut found, mut seen) = (vec![], std::collections::HashSet::new());
    for (place, depth) in places {
        let mut level = vec![place];
        for d in 0..=depth {
            for dir in &level {
                if dir.join(EXE).is_file() && seen.insert(std::fs::canonicalize(dir).unwrap_or_else(|_| dir.clone())) {
                    found.push(dir.clone());
                }
            }
            if d < depth {
                level = level.iter().flat_map(|dir| subdirs(dir)).collect();
            }
        }
    }
    found
}

pub fn azimuth_dir(arg: Option<&Path>) -> Result<PathBuf> {
    if let Some(p) = arg {
        return azimuth_at(p).ok_or_else(|| format!("{} has no azimuth.exe", p.display()).into());
    }
    let mut found = with_the_game(find_azimuth());
    if found.len() == 1 || (found.len() > 1 && !can_ask()) {
        return Ok(found.swap_remove(0));
    }
    let question = if found.is_empty() {
        say!("Could not find Azimuth by itself.");
        "Path to your Azimuth folder (the one with azimuth.exe), or Enter to quit:"
    } else {
        say!("Found more than one Azimuth:");
        for (i, p) in found.iter().enumerate() {
            say!("  {}. {}", i + 1, p.display());
        }
        "The number of the one to install into, or the path to another Azimuth folder (Enter to quit):"
    };
    while let Some(line) = ask_line(question) {
        if let Some(p) = line.parse::<usize>().ok().and_then(|n| found.get(n.wrapping_sub(1))) {
            return Ok(p.clone());
        }
        if let Some(p) = azimuth_at(Path::new(&line)) {
            return Ok(p);
        }
        say!("  no azimuth.exe in {line}");
    }
    fail("No Azimuth folder was chosen. Run again with --azimuth <folder>.")
}

/// With several Azimuth copies around, this is for the ones that run Forza Horizon 2, when any of them does.
fn with_the_game(found: Vec<PathBuf>) -> Vec<PathBuf> {
    let with_game: Vec<PathBuf> = found.iter().filter(|a| find_media(a).is_some()).cloned().collect();
    if with_game.is_empty() { found } else { with_game }
}

fn is_media(media: &Path) -> bool {
    media.join("audio").join("RadioInfo_EN.xml").is_file() && media.join("UI.zip").is_file()
}

/// Forza Horizon 3 keeps the same two files, so a folder nobody named for Forza Horizon 2 has to show its radio.
fn is_fh2_media(media: &Path) -> bool {
    std::fs::read(media.join("audio").join("RadioInfo_EN.xml")).is_ok_and(|xml| String::from_utf8_lossy(&xml).contains("Radio Levante"))
}

/// The media folder (the one holding `audio/` and `UI.zip`) of the game folder a path names. The extracted package
/// keeps it in Mount/. Be forgiving about which level was named: the game folder, Mount, media, something inside
/// them (the game's exe, media/audio), or the folder the game folder is in.
fn media_in(path: &Path) -> Option<PathBuf> {
    let shapes = |f: &Path| [f.join("Mount").join("media"), f.join("media"), f.to_path_buf()];
    let start = if path.is_file() { path.parent()? } else { path };
    shapes(start)
        .into_iter()
        .chain(start.ancestors().skip(1).take(2).map(Path::to_path_buf))
        .chain(subdirs(start).iter().flat_map(|dir| shapes(dir)))
        .find(|media| is_media(media))
}

/// The game's media folder, from what Azimuth knows: Forza Horizon 2's own folder in its settings, or else another
/// folder listed there, or one inside the Azimuth folder (some keep the game in it), that holds Forza Horizon 2.
fn find_media(azimuth: &Path) -> Option<PathBuf> {
    let listed = game_folders(azimuth);
    let named = listed.iter().filter(|(fh2, _)| *fh2).find_map(|(_, folder)| media_in(folder));
    named.or_else(|| {
        let others = listed.iter().filter(|(fh2, _)| !fh2).map(|(_, folder)| folder.as_path());
        others.chain([azimuth]).filter_map(media_in).find(|media| is_fh2_media(media))
    })
}

/// The folder holding `audio/`, `UI.zip` and the rest of the game's media.
pub fn media_dir(azimuth: &Path, arg: Option<&Path>) -> Result<PathBuf> {
    const NEEDS: &str = "media/audio/RadioInfo_EN.xml and media/UI.zip";
    if let Some(p) = arg {
        return media_in(p).ok_or_else(|| format!("{} does not look like the Forza Horizon 2 folder: no {NEEDS} in it", p.display()).into());
    }
    if let Some(media) = find_media(azimuth) {
        return Ok(media);
    }
    say!("Could not find your Forza Horizon 2 files by itself.");
    let listed = game_folders(azimuth);
    if listed.is_empty() {
        say!("  Azimuth's settings ({}) name no game folder.", azimuth.join("data").join("azimuth.ini").display());
    }
    for (_, folder) in &listed {
        let why = if folder.is_dir() { format!("has no {NEEDS}") } else { "does not exist".to_string() };
        say!("  {} (from Azimuth's settings) {why}", folder.display());
    }
    while let Some(line) = ask_line("Path to your Forza Horizon 2 folder (the one you picked in Azimuth), or Enter to quit:") {
        if let Some(media) = media_in(Path::new(&line)) {
            return Ok(media);
        }
        say!("  no {NEEDS} in {line}");
    }
    fail("Could not find your Forza Horizon 2 files. Run again with --game <your Forza Horizon 2 folder>.")
}

/// Game folders listed in Azimuth's data/azimuth.ini, each with whether it is Forza Horizon 2's own: that one
/// first, then the title selected in the launcher.
fn game_folders(azimuth: &Path) -> Vec<(bool, PathBuf)> {
    let Ok(raw) = std::fs::read(azimuth.join("data").join("azimuth.ini")) else { return vec![] };
    let text = if raw.starts_with(&[0xFF, 0xFE]) {
        String::from_utf16_lossy(&raw[2..].chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect::<Vec<_>>())
    } else {
        String::from_utf8_lossy(&raw).into_owned()
    };
    let (mut section, mut selected) = (String::new(), String::new());
    let mut folders: Vec<(String, String)> = vec![];
    for line in text.trim_start_matches('\u{feff}').lines().map(str::trim) {
        if let Some(s) = line.strip_prefix('[').and_then(|l| l.strip_suffix(']')) {
            section = s.to_ascii_lowercase();
        } else if let Some((k, v)) = line.split_once('=') {
            let v = v.trim().trim_matches('"');
            match (k.trim(), section.as_str()) {
                ("game", "launcher") => selected = format!("title-{}", v.to_ascii_lowercase()),
                ("folder", s) if s.starts_with("title-") && !v.is_empty() => folders.push((s.to_string(), v.to_string())),
                _ => {}
            }
        }
    }
    folders.sort_by_key(|(s, _)| (s != FH2_TITLE, *s != selected));
    folders.into_iter().map(|(s, f)| (s == FH2_TITLE, native_path(&f, azimuth))).collect()
}

/// azimuth.ini stores Windows paths. On Linux (Wine/Proton) map Z:\ to / and C:\ into the prefix Azimuth runs in.
/// A path with no drive is taken from the Azimuth folder.
fn native_path(win: &str, azimuth: &Path) -> PathBuf {
    if cfg!(windows) {
        return azimuth.join(win);
    }
    if win.as_bytes().get(1) != Some(&b':') {
        return azimuth.join(win.replace('\\', "/"));
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    /// A fresh folder for one test under the system's temp folder, removed when the test is over.
    struct Scratch(PathBuf);

    impl Drop for Scratch {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    fn scratch(name: &str) -> Scratch {
        let dir = std::env::temp_dir().join(format!("r1d-locate-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        Scratch(dir)
    }

    fn touch(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn game(media: &Path, station: &str) {
        touch(&media.join("audio").join("RadioInfo_EN.xml"), &format!("<RadioStation Name=\"{station}\" Number=\"4\">"));
        touch(&media.join("UI.zip"), "");
    }

    fn ini(azimuth: &Path, text: &str) {
        let mut raw = vec![0xFF, 0xFE];
        raw.extend(text.encode_utf16().flat_map(u16::to_le_bytes));
        fs::create_dir_all(azimuth.join("data")).unwrap();
        fs::write(azimuth.join("data").join("azimuth.ini"), raw).unwrap();
    }

    #[test]
    fn azimuth_is_the_folder_or_the_exe_in_it() {
        let tmp = scratch("exe");
        let az = tmp.0.clone();
        touch(&az.join(EXE), "");
        assert_eq!(azimuth_at(&az), Some(az.clone()));
        assert_eq!(azimuth_at(&az.join(EXE)), Some(az.clone()));
        assert_eq!(azimuth_at(&az.join("data")), None);
        assert_eq!(azimuth_dir(Some(&az.join(EXE))).unwrap(), az);
        assert!(azimuth_dir(Some(&az.join("nope"))).is_err());
    }

    #[test]
    fn azimuth_is_found_under_any_name_and_the_copy_with_the_game_is_preferred() {
        let tmp = scratch("find");
        let home = tmp.0.clone();
        let (plain, efk, deep) = (home.join("Desktop").join("Azimuth"), home.join("Games").join("azimuth-windows"), home.join("Games").join("emu").join("az (2)"));
        for az in [&plain, &efk, &deep] {
            touch(&az.join(EXE), "");
        }
        touch(&home.join("Games").join("a").join("b").join("too deep").join(EXE), "");
        let places = || vec![(efk.clone(), 1), (home.join("Desktop"), 2), (home.join("Games"), 2), (home.join("nowhere"), 2)];
        assert_eq!(azimuths_in(places()), [efk.clone(), plain.clone(), deep.clone()]);
        assert_eq!(azimuths_in(vec![(home.join("Games"), 1)]), [efk.clone()]);
        // none of them has the game: all stay, in order
        assert_eq!(with_the_game(azimuths_in(places())), [efk.clone(), plain.clone(), deep.clone()]);
        game(&deep.join("FH2").join("Mount").join("media"), "Radio Levante");
        assert_eq!(with_the_game(azimuths_in(places())), [deep.clone()]);
    }

    #[test]
    fn media_is_found_at_any_level_of_the_game_folder() {
        let tmp = scratch("levels");
        let root = tmp.0.clone();
        let media = root.join("FH2").join("Mount").join("media");
        game(&media, "Radio Levante");
        touch(&root.join("FH2").join("Mount").join("forza_durango_release_final.exe"), "");
        for named in ["FH2", "FH2/Mount", "FH2/Mount/media", "FH2/Mount/media/audio", "FH2/Mount/forza_durango_release_final.exe", "."] {
            assert_eq!(media_in(&root.join(named)), Some(media.clone()), "from {named}");
        }
        assert_eq!(media_in(&root.join("FH2").join("EmbeddedXvd")), None);
    }

    #[test]
    fn the_game_can_live_inside_the_azimuth_folder() {
        let tmp = scratch("inside");
        let az = tmp.0.clone();
        // no folder in the settings at all, and the package's content is the folder itself (no Mount)
        ini(&az, "[launcher]\r\ngame=1a96545b\r\n");
        let media = az.join("Anthem_1.0.0.64_x64__8wekyb3d8bbwe").join("media");
        game(&media, "Radio Levante");
        assert_eq!(media_dir(&az, None).unwrap(), media);
    }

    #[test]
    fn a_folder_without_a_drive_is_taken_from_the_azimuth_folder() {
        let tmp = scratch("relative");
        let az = tmp.0.clone();
        ini(&az, "[title-1A96545B]\r\nfolder=games\\FH2\r\n");
        let media = az.join("games").join("FH2").join("Mount").join("media");
        game(&media, "Radio Levante");
        assert_eq!(media_dir(&az, None).unwrap(), media);
    }

    #[test]
    fn forza_horizon_2_wins_over_the_selected_title() {
        let tmp = (scratch("titles"), scratch("titles-games"));
        let (az, games) = (tmp.0.0.clone(), tmp.1.0.clone());
        let (fh3, fh2) = (games.join("FH3").join("Mount").join("media"), games.join("FH2").join("Mount").join("media"));
        game(&fh3, "Horizon Block Party");
        game(&fh2, "Radio Levante");
        let section = |id: &str, dir: &str| format!("[title-{id}]\r\nfolder={}\r\n", games.join(dir).display());
        ini(&az, &format!("[launcher]\r\ngame=4ce1dfab\r\n{}{}", section("4ce1dfab", "FH3"), section("1a96545b", "FH2")));
        assert_eq!(media_dir(&az, None).unwrap(), fh2);
        // with no section of its own, Forza Horizon 2 is told from Forza Horizon 3 by its radio
        ini(&az, &format!("[launcher]\r\ngame=4ce1dfab\r\n{}{}", section("4ce1dfab", "FH3"), section("00000000", "FH2")));
        assert_eq!(media_dir(&az, None).unwrap(), fh2);
        ini(&az, &format!("[launcher]\r\ngame=4ce1dfab\r\n{}", section("4ce1dfab", "FH3")));
        assert!(media_dir(&az, None).is_err());
    }
}
