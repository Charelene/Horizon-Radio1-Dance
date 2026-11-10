# BBC Radio 1 Dance for Forza Horizon 2

One more station on the dial, and this one is live.

```
 Off · Pulse · Bass Arena · XS · Levante · Hospital · Ninja Tune · Leisure · ... · BBC Radio 1 Dance
                                                                                    you are here ^
```

This mod adds **BBC Radio 1 Dance** to Forza Horizon 2's radio as a new station. It plays what is on air right now, not a playlist. Every stock station stays as it was. It targets the Xbox One build of the game (1.0.0.64) running on PC through [Azimuth](#credits), and it loads through the [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform).

It is an unofficial fan mod. It is not made by, endorsed by or connected to the BBC.

## What you get

- **Live radio in the game.** Tune in and you hear the station as it broadcasts, a few seconds behind like any web stream.
- **Nothing downloads in the background.** The stream starts once the station has played for a second and stops 3 seconds after you switch station, turn the radio off or pause the game. While you listen, it uses about 145 MB per hour (320 kb/s).
- **Song names in the radio HUD.** The mod reads the BBC's "now playing" feed while you listen and shows the song you are hearing whenever the radio HUD appears: when you tune in, switch stations, or every 16 minutes. Between songs it reads "Live · BBC Radio 1 Dance".
- **It gets along with the other radio mods.** Horizon Wave Radio and Horizon Most Wanted stay on the dial (see [below](#next-to-horizon-wave-radio-and-horizon-most-wanted)).

## Requirements

- Forza Horizon 2 (Xbox One build 1.0.0.64) through Azimuth, on Windows. Linux (Proton/Wine) should work but has not been tested yet.
- The FH2 Mod Platform. The installer installs it if you don't have it.
- FH2 Extra Options with media overrides on (`EnableMediaOverrides=1` in `FH2_ExtraOptions.ini`). The Mod Platform delivers the station's files through Extra Options' override folder.
- An internet connection while you listen.

## Installing

- **Windows:** download `BBCRadio1Dance-Setup.exe` from the [latest release](../../releases/latest) and run it. Pick your Azimuth folder (the one with `azimuth.exe`; it is found by itself when it can be). A console window shows the progress; when it closes, you're done. It shows up in Add/Remove Programs, so uninstalling is the usual way.
- **Linux (Proton/Wine), untested:** `curl -fsSL https://github.com/Charelene/Horizon-Radio1-Dance/releases/latest/download/install.sh | bash`

Then start the game and cycle the radio until you reach BBC Radio 1 Dance. Give it a few seconds the first time: the stream is starting.

The installer:

1. finds Azimuth and your Forza Horizon 2 files, and installs the FH2 Mod Platform if it is missing;
2. downloads ffmpeg (the Windows build from [BtbN's FFmpeg builds](https://github.com/BtbN/FFmpeg-Builds), about 175 MB, checked against the SHA-256 that project publishes) into `<azimuth>\Radio1Dance\tools`;
3. builds the station from your own game files: the radio list in all ten languages, the HUD layout and the station logo;
4. writes `<azimuth>\Radio1Dance\stream.ini` and puts `BBCRadio1Dance.dll` into `<azimuth>\fh2-mods`.

The command-line installer takes options (`r1d-installer --help`); `r1d-installer --uninstall` removes everything it installed.

### Next to Horizon Wave Radio and Horizon Most Wanted

The Mod Platform lets only one mod replace a given game file, and every station mod wants the radio list and the HUD layout. Mods load in file-name order, so `BBCRadio1Dance.dll` gets those files first. The installer builds them on top of the files Horizon Most Wanted or Horizon Wave Radio already made, so every station is in them. The other mods' DLLs still play their own stations.

**Run this installer again after you install, update or remove Horizon Wave Radio or Horizon Most Wanted**, so the files stay in step.

## Settings: stream.ini

`<azimuth>\Radio1Dance\stream.ini` says where the audio and the song names come from:

```ini
[stream]
url=http://as-hls-ww-live.akamaized.net/pool_62063831/live/ww/bbc_radio_one_dance/bbc_radio_one_dance.isml/bbc_radio_one_dance-audio%3d320000.norewind.m3u8
service=bbc_radio_one_dance
```

- `url` is the BBC's own worldwide stream. If the BBC moves it, put the new address here. The 96 kb/s stream (`audio%3d96000`) uses less data.
- `service` is the station's id in the BBC's "now playing" feed.
- Optional: `name_delay=5` (seconds, may be negative) shows song names later or earlier, if they don't line up with the music. `song_popup=1` makes the radio pop up every new song's name, at the price of a tiny break in the music at each song change.

Running the installer again keeps your `stream.ini`. Delete it first to get the defaults back.

## If something is wrong

- **Silence:** look in `fh2-mods\platform.log` for the lines from "BBC Radio 1 Dance", and in `<azimuth>\Radio1Dance\ffmpeg.log` for what ffmpeg said.
- **"Radio Off" under the logo for a moment when you switch station:** the stock stations do the same until the next song starts.
- **The station is missing from the dial:** check that `EnableMediaOverrides=1` is set in `FH2_ExtraOptions.ini`, and that `platform.log` doesn't say an override was refused.

## How it works

- **The station is data.** A new `<RadioStation>` in the game's `RadioInfo_*.xml`, a patched HUD layout and an icon texture, all delivered as Mod Platform file overrides. The game's own files are never touched. The playlist holds two 16-minute "tracks", plus silent placeholders where the stingers and DJ lines go.
- **A small DLL** ([`mod/src/radio1_dance.c`](mod/src/radio1_dance.c)) does what data can't:
  1. **Loads the overrides** through the Mod Platform.
  2. **Plays the stream.** The game opens each track through FMOD, and the DLL makes it an FMOD *user stream*: FMOD asks the DLL for audio as it plays, and gets what ffmpeg decodes from the BBC's live stream (the way FH2 Universal Radio feeds its stations). ffmpeg runs as a separate process only while the station is heard, and it is closed together with the game. The radio starts a track partway through, where the station's clock says it is; for a live track the position changes nothing, and what you hear is the broadcast either way.
  3. **Serves the placeholders** as one second of silence, so no stingers or DJ lines interrupt the broadcast.
  4. **Makes the station selectable.** Without it, the game skips a station it has never heard of when you cycle the radio.
  5. **Reads "now playing"** from the BBC's feed while the station is heard.
- The research behind the station, the HUD and the audio hooks comes from Horizon Wave Radio: see [docs/](docs/). The FMOD details specific to this mod (the user stream and the struct layout in this build) are in the comment at the top of the DLL's source.

## What's not in this repo

No Forza Horizon 2 files, no music and no recordings. The station files are generated on your machine from your own game files, and the audio is the BBC's public live stream, played on your machine while you listen, like any internet radio player.

## Building

You need mingw-w64 and Rust with the `x86_64-pc-windows-gnu` target.

```sh
./installer/build.sh    # DLL + installers -> installer/dist/
./mod/build.sh          # just the DLL -> mod/build/BBCRadio1Dance.dll
```

On Windows with MSYS2, put `C:\msys64\mingw64\bin` on `PATH` and run `CC=gcc ./mod/build.sh`. The installer embeds the DLL, so build the DLL first (`installer/build.sh` does).

The HUD icon is made from [`assets/logo.png`](assets/logo.png), a render of [`assets/logo-white.svg`](assets/logo-white.svg):

```sh
pip install pillow etcpak texture2ddecoder
python tools/make_icon.py assets/logo.png installer/data/HUD_RadioIcon_R1Dance.swatchbin --preview icon-preview.png
```

## Releasing

GitHub Actions ([`release.yml`](.github/workflows/release.yml)) builds and publishes a release when a version tag is pushed: `BBCRadio1Dance-Setup.exe`, `r1d-installer-linux-x86_64`, `install.sh` and `SHA256SUMS`.

```sh
git tag v0.1.0 && git push origin v0.1.0
```

## Layout

```
mod/
  build.sh                      builds the DLL with mingw-w64
  src/radio1_dance.c            the whole mod
  third_party/fh2mod.h          FH2 Mod Platform API header (MIT, Trinculo54)
installer/
  src/                          the installer (Rust)
  data/station.json             the stream, the feed and what the HUD shows
  data/HUD_RadioIcon_R1Dance.swatchbin   the HUD icon, compiled into the installer
  install.sh                    Linux one-liner: fetches, verifies and runs the installer
  windows/radio1-dance.iss      Inno Setup script: the Windows wizard around the installer
  build.sh                      builds the DLL and both installers
assets/                         the station logo (SVG and PNG)
tools/make_icon.py              logo PNG -> BC7 .swatchbin
.github/workflows/              release.yml: builds and publishes releases when a v* tag is pushed
docs/                           radio research notes from Horizon Wave Radio and Horizon Most Wanted
```

## Credits

- [Horizon Most Wanted](https://github.com/Charelene/Horizon-Most-Wanted) and [Horizon Wave Radio](https://github.com/Charelene/Horizon-Wave-Radio), which this is a fork of: the mod, the installer and the radio research.
- [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform) by Trinculo54: file overrides, hooks and logging. `fh2mod.h` is MIT licensed.
- [FH2 Universal Radio](https://github.com/Trinculo54/FH2-Universal-Radio) by Trinculo54 (GPL-3.0), the Forza Horizon 2 port of [FH6 Universal Radio](https://github.com/g0ldyy/fh6-universal-radio) by [g0ldyy](https://github.com/g0ldyy) and [its contributors](https://github.com/g0ldyy/fh6-universal-radio/graphs/contributors). The way this mod feeds its station follows the FH2 port: an FMOD user stream with a small decode buffer, only the newest stream fed, 16-minute tracks so the radio's clock never seeks the track event past what it can reach, and song names written into the radio's own track info. No code was copied from either; the approach was reimplemented here.
- [FFmpeg](https://ffmpeg.org), in [BtbN's Windows builds](https://github.com/BtbN/FFmpeg-Builds), decodes the stream. It is downloaded by the installer, not shipped here.
- Azimuth runs the Xbox One build of FH2 on PC.
- The audio and the song information are the BBC's, streamed live from its public services while you listen.

## License

GPL-3.0: see [LICENSE](LICENSE). [`mod/third_party/fh2mod.h`](mod/third_party/fh2mod.h), the FH2 Mod Platform's API header, keeps its own MIT license ([mod/third_party/LICENSE](mod/third_party/LICENSE)).

## Disclaimer

This is an unofficial fan project. It is not affiliated with or endorsed by the BBC, Microsoft, Playground Games or Turn 10 Studios. BBC, Radio 1 and the BBC Radio 1 Dance logo are trademarks of the British Broadcasting Corporation; the logo, the only BBC material in this repo, is used only to label the station in the game's HUD. Forza Horizon is a trademark of Microsoft Corporation. No game files and no music are distributed: the audio is the BBC's public stream, played on your machine while you listen.
