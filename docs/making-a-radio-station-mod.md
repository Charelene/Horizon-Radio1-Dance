# Add your own station to Forza Horizon 2

This guide builds a new station, "My Station", from scratch. It uses this repo's DLL ([`mod/src/horizon_wave.c`](../mod/src/horizon_wave.c)) as the worked example. For the reasons behind each step, see [How FH2's radio works](how-fh2-radio-works.md).

A station has two halves:

- **Data:** a station block in the game's `RadioInfo_*.xml`, a HUD trigger and an icon texture.
- **A small DLL** that:
  1. loads that data through the Mod Platform,
  2. serves your audio files,
  3. tells the radio your station may be selected.

## 0. What you need

- **The game:** FH2, Xbox One build **1.0.0.64**, running through Azimuth, with the [FH2 Mod Platform](https://github.com/Trinculo54/fh2-mod-platform) installed. Hooks need platform API 3 or later; this repo is built against API 9 (platform 2.8.0). The addresses in this guide are for that exact build.
- **A compiler:** mingw-w64 (`x86_64-w64-mingw32-gcc`).
- **Audio:** your audio as **OGG Vorbis or WAV** files (both verified). MP3 and FLAC are looked up too, but they're untested.
- **An icon:** a 196x96 or 196x104 icon, converted to the game's texture format (see [step 4](#4-make-the-icon)).

Work from the player's own game files. Patched copies of `RadioInfo_*.xml` and `HUDRadioControl.xaml` are still the game's files, so generate them on the player's machine instead of shipping them.

## 1. Pick a name, a number and a sound prefix

| Thing | Example | Rule |
|---|---|---|
| Station `Name` | `My Station` | Must match exactly (case included) in the XML, the DLL and the HUD trigger. |
| `Number` | `10` | Must be unique. A duplicate silently **replaces** that stock station. Stock stations use 1 to 7, and 8 and 9 look reserved. 10 is verified; higher numbers are untested. Stations cycle in number order. |
| Sound prefix | `MS_` | Every `SoundName` of yours starts with it, so the DLL can tell your sounds from the game's. |

## 2. Write the station block

Insert this block just before `</RadioStations>` in **all ten** `media/audio/RadioInfo_<lang>.xml` files (BR, DE, EN, ES, FR, IT, JP, MX, PL, RU). Edit them as text: the stock files contain raw `&` characters, so strict XML parsers reject them. Keep each file's line endings.

```xml
    <RadioStation Name="My Station" Number="10" RecentlyPlayedListSize="-1">
      <Playlist Type="FreeroamTracks">
        <Entry EventName="/Master/Radio/Track" SoundName="MS_Track_01" SampleLength="200000" DisplayName="First Song" Artist="Some Artist" TrackStart="0" DJDrop="15000" TrackDrop="15000" TrackBreakDown="120000" StingerStart="197500" DJStart="198500" End="200000"/>
        <Entry EventName="/Master/Radio/Track" SoundName="MS_Track_02" SampleLength="185000" DisplayName="Second Song" Artist="Another Artist" TrackStart="0" DJDrop="12000" TrackDrop="20000" TrackBreakDown="110000" StingerStart="182500" DJStart="183500" End="185000"/>
      </Playlist>
      <Playlist Type="EventTracks">
        <Entry EventName="/Master/Radio/Track" SoundName="MS_Track_01" SampleLength="200000" DisplayName="First Song" Artist="Some Artist" TrackStart="0" DJDrop="15000" TrackDrop="15000" TrackBreakDown="120000" StingerStart="197500" DJStart="198500" End="200000"/>
        <Entry EventName="/Master/Radio/Track" SoundName="MS_Track_02" SampleLength="185000" DisplayName="Second Song" Artist="Another Artist" TrackStart="0" DJDrop="12000" TrackDrop="20000" TrackBreakDown="110000" StingerStart="182500" DJStart="183500" End="185000"/>
      </Playlist>
      <Playlist Type="Stingers">
        <Entry EventName="/Master/Radio/Stinger" SoundName="MS_Stinger_01" SampleLength="8000" StartNextTrack="6500" End="8000"/>
      </Playlist>
      <Playlist Type="ShortStingers">
        <Entry EventName="/Master/Radio/Stinger" SoundName="MS_Stinger_01" SampleLength="8000" StartNextTrack="6500" End="8000"/>
      </Playlist>
      <Playlist Type="DJ">
        <Entry EventName="/Master/Radio/DJ" SoundName="MS_DJ_01" SampleLength="12000" GameEvent="BarnFind1Seen"/>
      </Playlist>
    </RadioStation>
```

Tips for the numbers (all values are milliseconds):

- **`SampleLength` and `End`** are the length of the audio file.
- **Track markers:** `TrackStart`, `DJDrop`, `TrackDrop`, `TrackBreakDown`, `StingerStart`, `DJStart`, `End`. If you don't know a track's structure, estimates are fine. Use the stock data as a guide:
  - `StingerStart` sits about 1.5 to 3.5 s before `End`.
  - `DJStart` is always `StingerStart + 1000`.
- **Stingers:** `StartNextTrack` sits about 1 to 4 s before the stinger's `End`.
- **Trim trailing silence** from your files before you measure them. Silence at the end of a track shows up as a gap before the stinger.
- **`GameEvent`:** pick names from the stock files; the English file uses 122 distinct ones. DJ lines go through the same audio path as tracks, but none has been confirmed firing in game yet.
- **All five playlists:** fill every one. Every stock station does, and an empty playlist is untested.
- **Loudness:** match the stock stations, or yours will stand out. By ear, about −11 LUFS integrated sat right next to them; −12.5 sounded slightly quiet. Measure with `ffmpeg -i in -af ebur128 -f null -` and apply a fixed gain plus a limiter (leave headroom: Vorbis can overshoot the limiter by about 1 dB). A fixed gain is much cheaper than `loudnorm` and cannot pump.

The [attribute table](how-fh2-radio-works.md#entry-attributes) covers what each field is thought to mean.

## 3. Add a HUD trigger

Without this step the station still works, but its icon is a white box. Open `Controls/HUDRadioControl.xaml` from `media/UI.zip`. Inside the `A_Style_FZMaterial_StationIcon` style's `<Style.Triggers>`, add a trigger for your name. Horizon Wave puts its trigger right after the stock "Xbox Music" trigger:

```xml
<DataTrigger Binding="{Binding StationName}" Value="My Station">
    <Setter Property="ImageSource" Value="/ForzaUI;component/Textures/Anthem/HUD/HUD_RadioIcon_MyStation.png" />
    <Setter Property="Height" Value="104" />
</DataTrigger>
```

- **`Height`:** keep the setter for a 196x104 icon, and drop it for 196x96 (the style's default).
- **The `.png`:** the path says `.png`, but the game loads `HUD_RadioIcon_MyStation.swatchbin` from `media/ui/textures/Anthem.zip`.
- **The margin trigger:** each stock station also has a `MultiDataTrigger` that sets its left-aligned margin. Copy the Horizon Pulse one (it sets `Margin="-7,0,0,0"`) and change its `Value` to your station name.

The text under the icon is the track slot. It reads "Radio Off" whenever no track name is set, for example while a stinger plays right after you switch stations. That's the game's normal behavior; stock stations do it too.

## 4. Make the icon

The icon is a 196x96 or 196x104 image, stored as a BC7 `.swatchbin`:

- a `0x53`-byte header,
- a 256x128 padded surface in a swizzled block order,
- 32851 bytes in total.

This repo has no converter. The [texture format notes](how-fh2-radio-works.md#icon-texture-format-verified-against-all-nine-stock-icons) give the header fields and the block order, which is enough to write one. To check your converter, run a stock icon through it: the repacked file should be byte-identical.

## 5. Lay out the files

The DLL treats one folder next to `azimuth.exe` as the station. Everything under `overrides\` mirrors the game's `media` tree, with a zip's name standing in for the zip:

```
<azimuth>\MyStation\
  overrides\
    audio\RadioInfo_BR.xml ... RadioInfo_RU.xml               (all ten, patched)
    UI\Controls\HUDRadioControl.xaml                          -> member of media\UI.zip
    UI\Textures\Anthem\HUD\HUD_RadioIcon_MyStation.swatchbin  -> new member of media\ui\textures\Anthem.zip
  audio\
    MS_Track_01.ogg
    MS_Track_02.ogg
    MS_Stinger_01.ogg
    MS_DJ_01.ogg
<azimuth>\fh2-mods\
  MyStation.dll
```

Each audio file is named after its `SoundName`, with `.ogg`, `.wav`, `.mp3` or `.flac` added. Keep names under 96 characters, because the DLL's lookup key is 96 bytes.

## 6. Adapt the DLL

Copy `horizon_wave.c` and change four things.

**1. The station name and sound prefix:**

```c
#define STATION_NAME "Horizon Wave"
#define SOUND_PREFIX "HW_"
```

**2. The folder name** (in `fh2mod_init`):

```c
    swprintf(g_dir, MAX_PATH, L"%ls\\HorizonWave", api->azimuth_dir);
```

**3. The mod's identity** (in `fh2mod_init`):

```c
    info.name = "Horizon Wave Radio";
    info.version = "1.0.0";
    info.platform_version = FH2MOD_PLATFORM_VERSION;
    info.description = "Adds the Horizon Wave radio station.";
```

**4. `build.sh`:** it names the source and output files (`src/horizon_wave.c`, `build/HorizonWave.dll`). Change them to yours.

Nothing else needs to change. Here is what each part does, so you can trust it or rework it.

### Overrides: `set_file` per file

`load_overrides` walks `overrides\` recursively and builds the media path from the relative path, for example `"audio/RadioInfo_EN.xml"` or `"UI/Controls/HUDRadioControl.xaml"`. It hands each file to the platform:

```c
        if (A->set_file(M, sub, data, n)) count++; else say("override %s was refused", sub);
```

The platform copies the data, so the buffer is freed straight after. A refusal usually means another mod already replaces that file, because the platform allows **one mod per file**. If nothing was loaded, `fh2mod_init` logs it and returns without hooking anything.

### Hooks: from a thread, after `wait_for_code`

Game code can't be hooked until Azimuth has mapped the game and Extra Options has patched it. So `fh2mod_init` starts a thread, and that thread waits for the runtime base and hooks `base + RVA`:

```c
static DWORD WINAPI hook_thread(LPVOID unused) {
    (void)unused;
    unsigned char* base = (unsigned char*)A->wait_for_code(300000);   /* the game is unpacked/relocated at runtime */
    if (!base) { say("wait_for_code timed out; hooks not installed"); return 1; }
    FH2Hook* h[4] = {
        A->hook_function(M, "Studio::System::getSoundInfo", base + RVA_GETSOUNDINFO, d_getSoundInfo, (void**)&o_getSoundInfo),
        A->hook_function(M, "System::createSound", base + RVA_CREATESOUND, d_createSound, (void**)&o_createSound),
        A->hook_function(M, "Sound::getSubSound", base + RVA_GETSUBSOUND, d_getSubSound, (void**)&o_getSubSound),
        A->hook_function(M, "RadioModule::IsStationUnlocked", base + RVA_ISUNLOCKED, d_isUnlocked, (void**)&o_isUnlocked),
    };
    if (!h[0] || !h[1] || !h[2] || !h[3]) { say("hook creation failed; the game build does not match"); return 1; }
    say("hooks enabled: %d", A->enable_hooks(M, NULL));
    return 0;
}
```

The example doesn't check `api->api_version`. If you might run on an older platform, check it first (hooks need API 3 or later).

### Unlock: return 0 for your name

`IsStationUnlocked` returns **1 to skip** a station and **0 to allow it**. A station the game never unlocked is skipped, so the hook says 0 for yours and passes every other name through:

```c
static char d_isUnlocked(void* mod, const void* name) {
    if (name && string_is(name, STATION_NAME)) return 0;
    return o_isUnlocked(mod, name);
}
```

### Audio: three hooks

**`getSoundInfo`** handles your prefix only. It loads the file into memory once (`mem_for`), then describes it as an in-memory sound:

```c
static int d_getSoundInfo(void* sys, const char* key, StudioSoundInfo* info) {
    if (key && !strncmp(key, SOUND_PREFIX, sizeof SOUND_PREFIX - 1)) {
        Mem* m = mem_for(key);
        if (m) {
            memset(info, 0, sizeof *info);
            info->name_or_data = (const char*)m->data;
            info->mode = 0x00000009 | 0x10000000;      /* FMOD_LOOP_OFF | FMOD_2D | FMOD_OPENMEMORY_POINT */
            *(int*)info->exinfo = 216;                 /* cbsize */
            *(unsigned*)(info->exinfo + 4) = m->size;  /* length */
            return 0;
        }
        say("no audio file for %s in %s", key, g_audio_dir);
    }
    return o_getSoundInfo(sys, key, info);
}
```

**`createSound`** remembers which handles were opened from your buffers. **`getSubSound`** hands back the sound itself when FMOD says a plain file has no subsound (error 34):

```c
static int d_createSound(void* sys, const char* name, unsigned mode, void* exinfo, void** out) {
    int ours = name && mem_owns(name);
    int r = o_createSound(sys, name, mode, exinfo, out);
    if (out && *out) set_ours(*out, ours && r == 0);
    return r;
}

/* The game asks for subsound 0 of the opened sound. A plain file has none (error 34), so it is its own subsound. */
static int d_getSubSound(void* snd, int idx, void** out) {
    int r = o_getSubSound(snd, idx, out);
    if (r != 0 && is_ours(snd)) { *out = snd; return 0; }
    return r;
}
```

Three details carry this design:

- **The struct layout:** `exinfo` starts at `0x10`.
- **Memory, not paths:** the game's FMOD file layer can't open your files by path.
- **Buffers never freed:** FMOD streams straight from them.

The [research notes](how-fh2-radio-works.md#2-audio-how-a-soundname-becomes-sound) explain all three.

## 7. Build and install

```sh
./mod/build.sh
cp mod/build/MyStation.dll <azimuth>/fh2-mods/
```

## 8. Test it

Start the game, load into free roam and cycle the radio. Your station should come after Innovative Leisure if its number is 10.

**Read the log.** `<azimuth>\fh2-mods\platform.log` is the first place to look. The platform also keeps per-session copies in `fh2-mods\platform-logs\`. A healthy start of Horizon Wave looks like this:

```
[Horizon Wave Radio] 1.0.0 registered
[Horizon Wave Radio] replaces media\audio\RadioInfo_EN.xml (222909 bytes)
[Horizon Wave Radio] replaces media\UI\Controls\HUDRadioControl.xaml (23265 bytes)
[Horizon Wave Radio] replaces media\UI\Textures\Anthem\HUD\HUD_RadioIcon_Wave.swatchbin (32851 bytes)
[Horizon Wave Radio] 12 overrides loaded
[Horizon Wave Radio] hooked function game+0x1A98920 (Studio::System::getSoundInfo)
[Horizon Wave Radio] hooked function game+0x1997740 (System::createSound)
[Horizon Wave Radio] hooked function game+0x199F510 (Sound::getSubSound)
[Horizon Wave Radio] hooked function game+0x668810 (RadioModule::IsStationUnlocked)
[Horizon Wave Radio] enabled 4 hooks
[Horizon Wave Radio] hooks enabled: 1
```

**Restart quickly.** Every override and hook is set up at startup, so each change needs a game restart. Kill the game by PID:

```sh
pgrep -f '[Z]:.*azimuth.exe'   # the [Z] keeps pgrep from matching its own command line
kill <pid>
```

Don't use `pkill -f azimuth`: the pattern also matches the shell running the command, and kills it.

During development the game was started like this. Adjust the paths to your own setup:

```sh
STEAM_COMPAT_DATA_PATH=<your compatdata prefix> \
WINEDLLOVERRIDES="winmm,dwrite,d3dcompiler_47=n,b" \
<proton> run azimuth.exe --skip-launcher
```

`WINEDLLOVERRIDES` makes Wine load the platform's `DWrite.dll` and `d3dcompiler_47.dll` and the `winmm.dll` next to `azimuth.exe`, instead of its own built-in copies.

### Symptoms and causes

| What you see | Likely cause |
|---|---|
| A stock station disappeared | Your `Number` duplicates it. |
| Your station never comes up when cycling | Check these in order: the unlock hook is missing or returns 1 (the polarity is inverted); the name in the DLL doesn't match the XML exactly; the log lacks `hooks enabled: 1`. |
| `override ... was refused` | Another mod already replaces that file (one mod per file). |
| `nothing installed in ...` | The `overrides\` folder is empty or missing; no hooks are installed. |
| `hook creation failed; the game build does not match` or `wait_for_code timed out` | A different game build, or the platform couldn't hook. The addresses are build-specific. |
| `no audio file for MS_... in ...` | The file is missing, or its name or extension doesn't match the `SoundName`. |
| HUD shows your track, but there's silence, and the sound goes to OPENSTATE ERROR | The `FMOD_STUDIO_SOUND_INFO` layout is off (`exinfo` at `0xC` instead of `0x10`), or FMOD got a file path instead of memory. |
| The sound reaches READY but is silent | `getSubSound` isn't falling back to the sound itself (error 34). |
| The game crashes some time after your sounds play | FMOD was called from your own thread on a `Sound*` the game had already released or recycled. |
| A white box where the icon should be | No `DataTrigger` matches `StationName` exactly, or the texture path or name is wrong. |
| A blank or garbled icon | Probably a wrong header or block order in the texture (unverified; the working icon never showed this). |
| "Radio Off" while a stinger plays | Normal. The track slot is empty until the next track. |
| A gap between a track's end and the stinger | Trailing silence in the audio file, or `StingerStart`/`End` measured on an untrimmed file. |

## Limitations

- **The addresses are build-specific.** The four RVAs are for FH2 Xbox One 1.0.0.64 under Azimuth. Another build needs new ones; see [Finding the addresses](finding-addresses.md).
- **Audio stays in memory for the whole session.** Each file is read once and never freed, so memory grows with the size of your station.
- **Station mods collide.** Two station mods built this way both want `RadioInfo_*.xml` and `HUDRadioControl.xaml`, and the platform gives each file to one mod. Two stations would have to come from one mod (or from mods that coordinate).
- **The example's tables are fixed-size:** at most 256 distinct audio files (`MAX_MEM`), and 64 live sound handles tracked for the subsound fallback (`MAX_OURS`).
- **Several pieces are untested:** DJ lines in game, numbers above 10, empty playlists, and MP3/FLAC. The [open questions](how-fh2-radio-works.md#open-questions) list them all.
