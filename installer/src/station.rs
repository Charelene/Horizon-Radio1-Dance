//! Generate the station definition and the HUD patch from the player's own game files.
//! Nothing from the game is shipped: the stock RadioInfo_*.xml and HUDRadioControl.xaml are read from the install
//! and written back, patched, as overrides that the mod platform lays over the game's files.
//!
//! The platform gives each file to one mod, and mods load in file-name order, so BBCRadio1Dance.dll gets the radio
//! files before Horizon Most Wanted and Horizon Wave Radio. When one of those is installed, its already-patched files are
//! the base instead of the stock ones, so the result holds every station (see docs/coexisting-with-horizon-wave.md).
use crate::data::{
    Station, COMPANION_DIRS, FIRST_NUMBER, ICON_NAME, LIVE_MS, MOD_DIR, SILENT_DJ, SILENT_DJ_EVENT, SILENT_MS, SILENT_STINGER,
    STATION_NAME,
};
use crate::util::{fail, Result};
use regex::Regex;
use std::collections::BTreeSet;
use std::io::Read;
use std::path::{Path, PathBuf};

fn attrs(pairs: &[(&str, String)]) -> String {
    pairs.iter().map(|(k, v)| format!("{k}=\"{}\"", esc(v))).collect::<Vec<_>>().join(" ")
}

fn esc(s: &str) -> String {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;")
}

/// The `<Entry>` for a live track: 16 minutes (see LIVE_MS). The early markers are unused (no DJ line ever plays);
/// the stinger comes 2 s and the DJ point 1 s before the end, as FH2 Universal Radio sets them.
fn live_entry(st: &Station, sound: &str) -> String {
    let l = LIVE_MS;
    format!("<Entry {}/>", attrs(&[
        ("EventName", "/Master/Radio/Track".into()), ("SoundName", sound.into()), ("SampleLength", l.to_string()),
        ("DisplayName", st.live.display_name.clone()), ("Artist", st.live.artist.clone()), ("TrackStart", "0".into()),
        ("DJDrop", "5000".into()), ("TrackDrop", "5000".into()), ("TrackBreakDown", "10000".into()),
        ("StingerStart", (l - 2000).to_string()), ("DJStart", (l - 1000).to_string()), ("End", l.to_string()),
    ]))
}

/// The <RadioStation> element for the station, with `\n` line breaks.
pub fn station_block(st: &Station, number: u32) -> String {
    let music: Vec<String> = st.live.sounds.iter().map(|s| live_entry(st, s)).collect();
    let stinger = format!("<Entry {}/>", attrs(&[
        ("EventName", "/Master/Radio/Stinger".into()), ("SoundName", SILENT_STINGER.into()), ("SampleLength", SILENT_MS.to_string()),
        // the next live track starts with the stinger: when the DLL ends a track for a new song name, the only gap is
        // FMOD starting the next one (0.1 s in left an audible pause, 2026-11-10)
        ("StartNextTrack", "0".into()), ("End", SILENT_MS.to_string()),
    ]));
    let dj = format!("<Entry {}/>", attrs(&[
        ("EventName", "/Master/Radio/DJ".into()), ("SoundName", SILENT_DJ.into()), ("SampleLength", SILENT_MS.to_string()),
        ("GameEvent", SILENT_DJ_EVENT.into()),
    ]));
    let list = |kind: &str, items: &[String]| {
        format!("      <Playlist Type=\"{kind}\">\n{}      </Playlist>\n", items.iter().map(|x| format!("        {x}\n")).collect::<String>())
    };
    format!(
        "\n    <RadioStation Name=\"{STATION_NAME}\" Number=\"{number}\" RecentlyPlayedListSize=\"-1\">\n{}{}{}{}{}    </RadioStation>\n",
        list("FreeroamTracks", &music), list("EventTracks", &music), list("Stingers", std::slice::from_ref(&stinger)),
        list("ShortStingers", std::slice::from_ref(&stinger)), list("DJ", std::slice::from_ref(&dj))
    )
}

/// Where the files we patch come from, and which station numbers are already taken.
pub struct Plan {
    /// (file name, the file to patch) for every language: the stock file, or a companion's patched one
    pub radio_info: Vec<(String, PathBuf)>,
    /// the companion's patched HUDRadioControl.xaml, when there is one
    pub hud: Option<PathBuf>,
    /// the companion mod whose files are the base (so its stations stay on the radio)
    pub companion: Option<&'static str>,
    pub used: BTreeSet<u32>,
}

fn numbers_in(xml: &[u8]) -> Vec<u32> {
    let re = Regex::new(r#"<RadioStation\b[^>]*?\bNumber\s*=\s*"(\d+)""#).unwrap();
    re.captures_iter(&String::from_utf8_lossy(xml)).filter_map(|c| c[1].parse().ok()).collect()
}

/// Look at the radios already installed: the game's own, the companions', and any other mod's `overrides/audio`.
pub fn plan(azimuth: &Path, media: &Path) -> Result<Plan> {
    let companion = COMPANION_DIRS.into_iter().find(|d| azimuth.join(d).join("overrides").join("audio").join("RadioInfo_EN.xml").is_file());
    let theirs = companion.map(|d| azimuth.join(d).join("overrides"));
    let mut radio_info = vec![];
    let mut used = BTreeSet::new();
    for entry in std::fs::read_dir(media.join("audio"))?.flatten() {
        let name = entry.file_name().to_string_lossy().into_owned();
        if !(name.starts_with("RadioInfo_") && name.ends_with(".xml")) {
            continue;
        }
        let base = theirs.as_ref().map(|t| t.join("audio").join(&name)).filter(|p| p.is_file()).unwrap_or_else(|| entry.path());
        used.extend(numbers_in(&std::fs::read(&base)?));
        radio_info.push((name, base));
    }
    if radio_info.is_empty() {
        return fail("no RadioInfo_*.xml found in the game's media/audio folder");
    }
    radio_info.sort();
    // any other mod that already replaces the radio files: its numbers are taken, but its stations cannot be merged in
    for dir in std::fs::read_dir(azimuth)?.flatten() {
        let name = dir.file_name().to_string_lossy().into_owned();
        if name == MOD_DIR || Some(name.as_str()) == companion {
            continue;
        }
        let xml = dir.path().join("overrides").join("audio").join("RadioInfo_EN.xml");
        if let Ok(data) = std::fs::read(&xml) {
            let theirs: BTreeSet<u32> = numbers_in(&data).into_iter().collect();
            let new: Vec<_> = theirs.difference(&used).copied().collect();
            if !new.is_empty() {
                say!("  note: {name} also replaces the radio files; its stations (numbers {new:?}) are not on the radio while this one is installed.");
                used.extend(new);
            }
        }
    }
    let hud = theirs.map(|t| t.join("UI").join("Controls").join("HUDRadioControl.xaml")).filter(|p| p.is_file());
    Ok(Plan { radio_info, hud, companion, used })
}

/// The first station number from FIRST_NUMBER up that nothing uses.
pub fn pick_number(used: &BTreeSet<u32>) -> u32 {
    (FIRST_NUMBER..).find(|n| !used.contains(n)).unwrap()
}

fn find(h: &[u8], n: &[u8], from: usize) -> Option<usize> {
    h.get(from..)?.windows(n.len()).position(|w| w == n).map(|i| i + from)
}

fn rfind(h: &[u8], n: &[u8]) -> Option<usize> {
    h.windows(n.len()).rposition(|w| w == n)
}

/// Write RadioInfo_<lang>.xml for every language into `out`, each with the station added before </RadioStations>.
pub fn write_radio_info(plan: &Plan, out: &Path, block: &str) -> Result<usize> {
    std::fs::create_dir_all(out)?;
    for (name, base) in &plan.radio_info {
        let data = std::fs::read(base)?;
        if find(&data, format!("Name=\"{STATION_NAME}\"").as_bytes(), 0).is_some() {
            return fail(format!("{} already has a {STATION_NAME} station (is another copy of the mod installed?)", base.display()));
        }
        let Some(i) = rfind(&data, b"</RadioStations>") else { return fail(format!("{name}: no </RadioStations>")) };
        let mut j = i;
        while j > 0 && matches!(data[j - 1], b' ' | b'\t' | b'\r' | b'\n') {
            j -= 1;
        }
        let block = if find(&data, b"\r\n", 0).is_some() { block.replace('\n', "\r\n") } else { block.to_string() };
        let mut new = data[..j].to_vec();
        new.extend_from_slice(block.as_bytes());
        new.extend_from_slice(&data[i..]);
        std::fs::write(out.join(name), new)?;
    }
    Ok(plan.radio_info.len())
}

/// Offset just past the end of the line holding `pos` (after the newline), and the indentation of the line at `start`.
fn line_end(x: &[u8], mut pos: usize) -> usize {
    while pos < x.len() && matches!(x[pos], b' ' | b'\t') {
        pos += 1;
    }
    if x.get(pos) == Some(&b'\r') {
        pos += 1;
    }
    if x.get(pos) == Some(&b'\n') {
        pos += 1;
    }
    pos
}

fn indent_at(x: &[u8], tag_start: usize) -> &[u8] {
    let mut s = tag_start;
    while s > 0 && matches!(x[s - 1], b' ' | b'\t') {
        s -= 1;
    }
    &x[s..tag_start]
}

/// HUDRadioControl.xaml with a logo trigger for the station (and the same margin rule as Horizon Pulse). The base is
/// `base` (a companion's patched copy) when given, otherwise the stock file in the game's UI.zip.
pub fn patched_hud(media: &Path, base: Option<&Path>) -> Result<Vec<u8>> {
    let mut x = vec![];
    match base {
        Some(p) => x = std::fs::read(p)?,
        None => {
            let mut z = zip::ZipArchive::new(std::io::BufReader::new(std::fs::File::open(media.join("UI.zip"))?))?;
            z.by_name("Controls/HUDRadioControl.xaml").map_err(|_| "UI.zip has no Controls/HUDRadioControl.xaml")?.read_to_end(&mut x)?;
        }
    }
    if find(&x, format!("Value=\"{STATION_NAME}\"").as_bytes(), 0).is_some() {
        return fail(format!("HUDRadioControl.xaml already has a {STATION_NAME} trigger (is another copy of the mod installed?)"));
    }
    let nl: &[u8] = if find(&x, b"\r\n", 0).is_some() { b"\r\n" } else { b"\n" };

    let anchor = br#"<DataTrigger Binding="{Binding StationName}" Value="Xbox Music">"#;
    let Some(a) = find(&x, anchor, 0) else { return fail("HUDRadioControl.xaml: Xbox Music trigger not found") };
    let Some(close) = find(&x, b"</DataTrigger>", a) else { return fail("HUDRadioControl.xaml: unterminated trigger") };
    let end = line_end(&x, close + b"</DataTrigger>".len());
    let ind = indent_at(&x, a).to_vec();
    let icon = [
        format!(r#"<DataTrigger Binding="{{Binding StationName}}" Value="{STATION_NAME}">"#),
        format!(r#"    <Setter Property="ImageSource" Value="/ForzaUI;component/Textures/Anthem/HUD/{ICON_NAME}.png" />"#),
        r#"    <Setter Property="Height" Value="104" />"#.to_string(),
        "</DataTrigger>".to_string(),
    ];
    let mut ins = vec![];
    for l in icon {
        ins.extend_from_slice(&ind);
        ins.extend_from_slice(l.as_bytes());
        ins.extend_from_slice(nl);
    }
    x.splice(end..end, ins);

    // copy Horizon Pulse's margin rule for this station
    let (open, shut, key) = (b"<MultiDataTrigger>".as_slice(), b"</MultiDataTrigger>".as_slice(), br#"Value="Horizon Pulse" />"#.as_slice());
    let mut from = 0;
    loop {
        let Some(s) = find(&x, open, from) else { return fail("HUDRadioControl.xaml: Pulse margin trigger not found") };
        let Some(e) = find(&x, shut, s) else { return fail("HUDRadioControl.xaml: unterminated trigger") };
        if find(&x[..e], key, s).is_some() {
            let end = line_end(&x, e + shut.len());
            let ind = indent_at(&x, s);
            let mut copy = ind.to_vec();
            copy.extend_from_slice(&x[s..end]);
            let copy = String::from_utf8_lossy(&copy).replace("Horizon Pulse", STATION_NAME).into_bytes();
            x.splice(end..end, copy);
            return Ok(x);
        }
        from = e;
    }
}
