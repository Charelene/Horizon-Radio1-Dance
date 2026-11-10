//! Data compiled into the installer: the station definition, the HUD icon and the mod DLL.
use serde::Deserialize;

#[derive(Deserialize)]
pub struct Station {
    pub stream: Stream,
    pub live: Live,
}

/// Where the audio and the song names come from.
#[derive(Deserialize)]
pub struct Stream {
    pub url: String,
    pub service: String,
}

/// The playlist entries the DLL serves as the live stream, and what the HUD shows for them.
#[derive(Deserialize)]
pub struct Live {
    pub sounds: Vec<String>,
    pub display_name: String,
    pub artist: String,
}

pub fn station() -> Station {
    serde_json::from_str(include_str!("../data/station.json")).expect("station.json is valid")
}

pub static MOD_DLL: &[u8] = include_bytes!("../../mod/build/BBCRadio1Dance.dll");
pub const MOD_FILE: &str = "BBCRadio1Dance.dll";
/// The HUD icon (196x104 BC7), made from assets/logo.png by tools/make_icon.py.
pub static ICON: &[u8] = include_bytes!("../data/HUD_RadioIcon_R1Dance.swatchbin");
pub const ICON_NAME: &str = "HUD_RadioIcon_R1Dance";

pub const STATION_NAME: &str = "BBC Radio 1 Dance";
/// The folder under Azimuth that holds this mod's overrides, stream.ini and ffmpeg.
pub const MOD_DIR: &str = "Radio1Dance";
/// Other station mods built the same way. The first one installed is the base for the patched files, so its stations
/// stay on the radio (Horizon Most Wanted's files already hold Horizon Wave's station when both are installed).
pub const COMPANION_DIRS: [&str; 2] = ["HorizonMostWanted", "HorizonWave"];
/// Station numbers start here: stock stations use 1-7 and 8-9 look reserved (docs/how-fh2-radio-works.md).
pub const FIRST_NUMBER: u32 = 10;
/// Length of each live track. The radio keeps the "/Master/Radio/Track" event within 5 s of the station's clock by
/// seeking it, and that event cannot be seeked much past the longest stock track (977656 ms); FH2 Universal Radio
/// found 16 minutes safe. With 1-hour tracks the stream went silent whenever the clock was past that (2026-11-10).
/// At the end the radio plays the silent stinger and opens the other live track, which picks up the stream again.
pub const LIVE_MS: i64 = 16 * 60 * 1000;
/// The placeholders the DLL serves as one second of silence (SILENT_PREFIX in the DLL). An empty playlist is untested
/// in the game, so the stinger and DJ lists each hold one.
pub const SILENT_STINGER: &str = "R1D_Silent_Stinger";
pub const SILENT_DJ: &str = "R1D_Silent_DJ";
pub const SILENT_MS: i64 = 1000;
/// A game event that never happens, so the radio never picks the silent DJ line. With a real one (BarnFind1Seen,
/// true in a save that has seen it) the radio took its DJ path at track changes and ducked the live audio under a
/// DJ who says nothing (2026-11-10).
pub const SILENT_DJ_EVENT: &str = "Radio1DanceNeverFires";
