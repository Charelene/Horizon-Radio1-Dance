# How Forza Horizon 2's radio works

These are research notes from building [Horizon Wave Radio](../README.md). They cover how the radio system in FH2 fits together, what can be done with data alone, and where code is needed. For a hands-on walkthrough, see [Making a radio station mod](making-a-radio-station-mod.md).

**The build these notes apply to:** the Xbox One build of FH2 (version 1.0.0.64, `forza_durango_release_final.exe`, the 24 Nov 2015 executable), run on PC through Azimuth, with the FH2 Mod Platform 2.8.0 (API 9). Every address below is a relative virtual address (RVA): an offset from the game's image base. The preferred base is `0x140000000`, but Azimuth maps the game somewhere else at runtime.

Each claim carries one of these labels:

- **Verified:** seen working in game, or seen in the platform log.
- **From code:** read in decompiled code, but never exercised.
- **Inferred:** reasoned from names or from patterns in the stock data.
- **Unverified:** an open question.

---

## The pieces at a glance

```
media/audio/RadioInfo_<lang>.xml        the station list and every playlist (plain text)
            |
            v
   radio module  -- map<Number, Name> --> cycling --> IsStationUnlocked(name)?  1 = skip, 0 = stop here
            |
            v  plays an entry: EventName (an FMOD Studio event) + SoundName (a key)
   FMOD Studio:  getSoundInfo(key) -> createSound(...) -> getSubSound(...) -> playback
            |                         audio lives in media/audio/fmodanthem/durango/HZ2_MX_Radio_NN_*.bank
            v
   HUD: media/UI.zip/Controls/HUDRadioControl.xaml
        StationName -> DataTrigger -> icon texture (media/ui/textures/Anthem.zip/HUD/*.swatchbin)
        TrackName   -> text, or "Radio Off" when empty
```

Adding a station touches every row of that diagram. The station list, the HUD layout and the icon are files, so the Mod Platform can override them. Two pieces live in code: the eligibility check and the bank lookup. Those need hooks.

---

## 1. Stations are XML

The game reads one `RadioInfo` file per language from `media/audio/`: `RadioInfo_BR.xml`, `_DE`, `_EN`, `_ES`, `_FR`, `_IT`, `_JP`, `_MX`, `_PL` and `_RU`. A new station has to go into all ten. The overall shape looks like this (attribute values trimmed):

```xml
<Radio>
  <RadioStations>
    <RadioStation Name="Horizon Pulse" Number="1" RecentlyPlayedListSize="-1">
      <Playlist Type="FreeroamTracks">
        <Entry EventName="/Master/Radio/Track" SoundName="..." SampleLength="..." DisplayName="..." Artist="..."
               TrackStart="..." DJDrop="..." TrackDrop="..." TrackBreakDown="..." StingerStart="..." DJStart="..." End="..."/>
      </Playlist>
      <Playlist Type="EventTracks">   ...same Entry shape as FreeroamTracks...   </Playlist>
      <Playlist Type="Stingers">
        <Entry EventName="/Master/Radio/Stinger" SoundName="..." SampleLength="8690" StartNextTrack="7129" End="8690"/>
      </Playlist>
      <Playlist Type="ShortStingers"> ...same Entry shape as Stingers... </Playlist>
      <Playlist Type="DJ">
        <Entry EventName="/Master/Radio/DJ" SoundName="..." SampleLength="25845" GameEvent="BarnFind10Available"/>
      </Playlist>
    </RadioStation>
    ...
  </RadioStations>
</Radio>
```

The stock stations:

| Number | Name |
|---|---|
| 1 | Horizon Pulse |
| 2 | Bass Arena |
| 3 | Horizon XS |
| 4 | Radio Levante |
| 5 | Hospital Records |
| 6 | Ninja Tune |
| 7 | Innovative Leisure |

### `Number` must be unique (verified)

The radio module keeps its stations in a `map<int, string>` keyed by `Number`. A second station with an existing number doesn't add anything. It **silently replaces** the station that had that number. The first attempt used `Number="1"`, and Horizon Pulse disappeared.

- **8 and 9 look reserved.** The HUD has `StationName` triggers for "Xbox Music" and "E3", and the game has matching strings. This is inferred, and those numbers weren't tried.
- **10 works** (verified). Numbers above 10 haven't been tried (unverified).

### Cycling order follows the number (verified)

Stations come in key order: StationOff (0), then 1 to 7, then 10. Horizon Wave, at Number 10, comes right after Innovative Leisure.

### `Name` is the station's identity

The unlock check (section 3) and the HUD (section 4) both look the station up by its `Name` string. So the same exact string has to appear in the XML, in the DLL's unlock hook and in the HUD triggers.

### Edit these files as text

The stock files aren't strict XML. They contain unescaped `&` characters in display names, so a strict XML parser rejects them. The game copes. Insert your `<RadioStation>` block as text, just before `</RadioStations>`, and keep the file's line endings. Whether the game decodes `&amp;` in your own values is unverified, so avoid `&` in names.

### `RadioStation` attributes

| Attribute | Meaning |
|---|---|
| `Name` | Station name: the key for the unlock check and the HUD. |
| `Number` | Unique integer: the map key and the cycling order. |
| `RecentlyPlayedListSize` | Every stock station uses `-1`. Presumably a no-repeat history size; unverified. |

### Playlists

| `Type` | Entries | Notes from the stock data |
|---|---|---|
| `FreeroamTracks` | Tracks | The main rotation. |
| `EventTracks` | Tracks | In the stock data, always a subset of `FreeroamTracks`. Presumably what plays during events and races (inferred). |
| `Stingers` | Stingers | Station idents played between tracks. Stingers play in game (verified). |
| `ShortStingers` | Stingers | In the stock data, identical to `Stingers` on every station. When the game picks this list is unverified. |
| `DJ` | DJ lines | Each entry is tied to one `GameEvent`. |

Every stock station fills all five playlists. Whether an empty playlist is safe is unverified.

### `Entry` attributes

All times are **milliseconds from the start of that sound**. `SampleLength` matches the length of the audio, and in the stock data `End` sits within a few ms of `SampleLength`.

| Attribute | Used in | Meaning |
|---|---|---|
| `EventName` | all | The FMOD Studio event the sound plays through. Stock values: `/Master/Radio/Track`, `/Master/Radio/Stinger` and `/Master/Radio/DJ`. Radio Levante has its own `_Levante` variants. A new station can reuse the generic three (verified for tracks and stingers). |
| `SoundName` | all | The key the game resolves to audio (section 2). |
| `SampleLength` | all | Length of the sound in ms. |
| `DisplayName`, `Artist` | tracks | Shown in the HUD's track slot (verified). |
| `TrackStart` | tracks | Where the music starts. Stock values run from 0 to a few hundred ms. |
| `DJDrop` | tracks | Inferred: a point early in the track where a DJ line may play over it. Often equal to `TrackDrop`, but not always before it. |
| `TrackDrop` | tracks | Inferred: the drop, where the track kicks in. |
| `TrackBreakDown` | tracks | Inferred: the breakdown, later in the track. |
| `StingerStart` | tracks | Inferred: where the stinger for the next transition starts, overlapping the outro. Stock values sit about 1.5 to 3.5 s before `End`. |
| `DJStart` | tracks | In every stock entry, exactly `StingerStart + 1000`. |
| `End` | tracks, stingers | End of the sound. |
| `StartNextTrack` | stingers | Inferred: where in the stinger the next track starts. Stock values sit about 1 to 4 s before the stinger's `End`. |
| `GameEvent` | DJ | The game situation that triggers the line. The stock English file uses 122 distinct names. Use names from the stock files; none of them looks like a generic "chatter" slot (inferred). |

The meanings above come from the names and from how the stock values line up. They weren't traced in code. Approximate values do work in practice: many of Horizon Wave's markers are estimates, and the station plays fine. One finding is practical: the timing is measured against the file, so **trailing silence in an audio file shows up as a pause** before the stinger. Trim the silence and measure the trimmed file.

---

## 2. Audio: how a `SoundName` becomes sound

The stock station audio lives in FMOD Studio banks under `media/audio/fmodanthem/durango/`, named by station number. The exe holds format strings such as `%sHZ2_MX_Radio_%02d_Tracks.bank`, `..._TracksLFE.bank`, `..._DJ_%s.bank`, `..._Expansion_DJ_%s.bank` and `..._Stingers_%s.bank`. No banks exist for Number 10, and the station plays anyway. What the game does about the missing banks is unverified, but nothing visibly breaks.

To play an entry, the game resolves its `SoundName` through this chain (verified by logging each call):

1. **`FMOD::Studio::System::getSoundInfo(system, key, FMOD_STUDIO_SOUND_INFO* info)`**, RVA `0x1a98920`. `key` is the entry's `SoundName`. For bank content, this fills `info` with where the sound lives.
2. **`FMOD::System::createSound(system, name_or_data, mode, exinfo, &sound)`**, RVA `0x1997740`. The game passes on what `getSoundInfo` returned, and ORs `0x10080` (`FMOD_NONBLOCKING | FMOD_CREATESTREAM`) into `mode`.
3. Once the sound is ready, **`FMOD::Sound::getSubSound(sound, index, &sub)`**, RVA `0x199f510`. The game calls this only after the sound reaches READY, and asks it for a subsound (index 0 for the sounds this mod describes).

To serve audio the banks don't contain, hook step 1 for your own keys and describe a sound of your own. Three things bite.

### Gotcha 1: the `FMOD_STUDIO_SOUND_INFO` layout (verified)

| Offset | Field |
|---|---|
| `0x00` | `const char* name_or_data` |
| `0x08` | `FMOD_MODE mode` (4 bytes) |
| `0x0C` | padding |
| `0x10` | `FMOD_CREATESOUNDEXINFO exinfo` (216 bytes in this build, `cbsize` first, then `length`) |
| `0xE8` | `int subsoundIndex` |

`FMOD_CREATESOUNDEXINFO` is 8-byte aligned, so it starts at `0x10`. A naive struct puts it at `0xC`. Then every field lands 4 bytes off and `cbsize` reads as 0. The non-blocking `createSound` still "succeeds", but the sound then goes to **OPENSTATE ERROR**. If your sounds open and then fail like that, check this layout first. The DLL pins the layout with `_Static_assert`s:

```c
typedef struct {
    const char* name_or_data;
    unsigned mode;
    unsigned pad_;                 /* FMOD_CREATESOUNDEXINFO is 8-aligned: exinfo starts at +0x10, not +0xC */
    unsigned char exinfo[216];     /* FMOD_CREATESOUNDEXINFO: int cbsize; unsigned length; ... */
    int subsoundIndex;
} StudioSoundInfo;
_Static_assert(__builtin_offsetof(StudioSoundInfo, exinfo) == 0x10, "exinfo offset");
_Static_assert(__builtin_offsetof(StudioSoundInfo, subsoundIndex) == 0xE8, "subsoundIndex offset");
```

### Gotcha 2: hand FMOD memory, not a path (verified)

The game runs FMOD behind its own file layer. Pointing FMOD at a loose file outside the game's media failed to open (OPENSTATE ERROR) when tested under Proton/Wine. The game's own Studio sounds are opened from memory, so do the same:

- Read the file into a buffer.
- Set `mode |= FMOD_OPENMEMORY_POINT` (`0x10000000`), and set `exinfo.length` to the buffer size.
- **Keep the buffer alive for the life of the process.** With `OPENMEMORY_POINT` and streaming, FMOD reads straight from your buffer for as long as the sound exists.

OGG Vorbis and WAV both decode in the game's FMOD (verified). The DLL also looks for `.mp3` and `.flac`, but those are untested.

### Gotcha 3: a plain file has no subsound (verified)

A bank sound is a container, and the game asks it for one subsound. A plain OGG or WAV sound has none, so `getSubSound` fails with **error 34**. The fix is to hook `getSubSound`: when it fails on a sound you created, return the sound itself as its own subsound. To recognize your sounds, also hook `createSound` and record each handle that was created from one of your buffers.

### Don't call FMOD from your own threads (verified, the hard way)

A diagnostic thread that polled the open state of sounds crashed the game. The game releases and recycles `Sound*` handles, so a handle you saved earlier can point at something else, or at nothing. Only touch FMOD objects inside hooks, on the game's own calls. The same recycling is why the DLL clears its "this handle is mine" flag whenever `createSound` returns that address for a sound that isn't yours.

---

## 3. The unlock check: why a new station is skipped

Cycling the radio runs `ChangeStation` (RVA `0x660e50`), which asks **`RadioModule::IsStationUnlocked(this, const std::string* name)`** (RVA `0x668810`) about each candidate station.

- **The polarity is inverted (verified):** it returns **1 for "skip / locked"** and **0 for "eligible"**, the opposite of what its name suggests. The decompiled code alone didn't settle the question. An early build forced 1 and hid the station. The answer came from logging the station table along with the game's own answer for each station.
- **A station nobody unlocked defaults to 1 (verified),** so a brand-new station is in the table but always skipped.
- **Stock stations are unlocked by name** through stateflow script nodes such as `global.set_radio_station_unlocked station_name="Horizon Pulse"` in the free-roam start scripts. The unlocked set is presumably kept in the save as well (from code and scripts; inferred). This mod doesn't patch any stateflow scripts. Whether patching them alone would be enough is unverified.

The fix is a hook that returns 0 for your station's name and passes every other name through:

```c
static char d_isUnlocked(void* mod, const void* name) {
    if (name && string_is(name, STATION_NAME)) return 0;
    return o_isUnlocked(mod, name);
}
```

`name` is an MSVC `std::string`. Short strings (15 characters or fewer) are stored inline at `+0`; longer ones are behind a heap pointer at `+0`. The size is at `+0x10` and the capacity at `+0x18`. `string_is` in [`horizon_wave.c`](../mod/src/horizon_wave.c) reads that layout.

`ChangeStation` applies the change through a virtual call, not through `SetStation` (RVA `0x65b310`). So a hook on `SetStation` won't see radio cycling (from code).

---

## 4. The HUD

The radio HUD is `Controls/HUDRadioControl.xaml` inside `media/UI.zip`. Two things matter for a new station.

### The icon: a `DataTrigger` per station name (verified)

The style `A_Style_FZMaterial_StationIcon` has a default size of `Width="196" Height="96"`. One `DataTrigger` on `StationName` per station sets the image:

```xml
<DataTrigger Binding="{Binding StationName}" Value="Horizon Pulse">
    <Setter Property="ImageSource" Value="/ForzaUI;component/Textures/Anthem/HUD/HUD_RadioIcon_Pulse.png" />
</DataTrigger>
```

- **A name with no trigger gets a white box.** The station still works.
- **The `.png` in the XAML maps to a `.swatchbin` texture.** `/ForzaUI;component/Textures/Anthem/HUD/HUD_RadioIcon_Pulse.png` is the member `HUD/HUD_RadioIcon_Pulse.swatchbin` of `media/ui/textures/Anthem.zip`. A new icon can be added there as a new member through the Mod Platform (verified: the Horizon Wave icon shows).
- **Each stock station also has a margin trigger.** A `MultiDataTrigger` per station sets a `Margin` when the HUD is left-aligned. This mod copies Horizon Pulse's trigger for the new name.
- The "Xbox Music" trigger also sets `Height="104"`, so icons come in 196x96 and 196x104 versions.

### The text: the track slot shows "Radio Off" when empty (verified)

The HUD text under the icon is the **track** slot, bound to `TrackName`. When `TrackName` is empty, it shows the localized `IDS_RadioOff` ("Radio Off") instead. So if you switch to a station while a stinger plays, the HUD reads "Radio Off" until the next track starts. That isn't a bug in your station. Horizon Pulse does the same.

### Icon texture format (verified against all nine stock icons)

The stock HUD icons are Xbox One "burG" swatchbin textures:

- **Size:** every file is **32851 bytes**: a `0x53`-byte header, then 32768 bytes of BC7 data.
- **Surface:** the BC7 data covers a **256x128 padded surface** (64x32 blocks of 4x4 pixels, 16 bytes per block). The icon occupies the top-left 196x96 or 196x104, and the padding blocks are zero.
- **Header:** the only difference between stock icons is the width and height fields.

| Offset | Content |
|---|---|
| `0x00` | `'burG'` |
| `0x04` | u32 `0x00010001` |
| `0x08` | u32 header size, `0x53` |
| `0x0C` | u32 file size, `32851` |
| `0x10` | `'BCXT'` chunk: `0x14` u32 `0x10002`, `0x18` u32 `0x28`, `0x1C` u32 data offset (`0x53`), `0x20` u32 data size (`0x8000`), `0x24` u32 data size again |
| `0x28` | `'HCXT'` chunk: `0x2C` u32 `0x80230` |
| `0x40` | u16 width, u16 height, u16 mip count (1) |
| `0x46` | the same three values again |
| `0x4C` | `01 01 00`, then the format byte at `0x4F`: `0x2E` = BC7 |
| `0x50` | `01 00 B8` |

The blocks are stored in a swizzled order: stored block index `i` (0 to 2047) is block `(x, y)` on the 64x32 grid. In the formula below, `b[k]` means bit `k` of `i`:

```
x = b0 | b2<<1 | b9<<2 | (b4^b5^b7^b8)<<3 | (b5^b7)<<4 | b6<<5
y = b1 | b3<<1 | b10<<2 | b8<<3 | b7<<4
```

The lowest four bits form a 4x4 Morton (Z-order) tile, and the upper bits are XOR-swizzled. This mapping was derived empirically from the stock icons. Repacking each one through it reproduces the file byte for byte.

---

## 5. What the Mod Platform provides

The mod uses only a handful of [Mod Platform](https://github.com/Trinculo54/fh2-mod-platform) calls (see [`fh2mod.h`](../mod/third_party/fh2mod.h)):

| Call | API | Use here |
|---|---|---|
| `register_mod(&info)` | 1 | Registers the mod. Call it first in `fh2mod_init`. |
| `log(mod, line)` | 1 | Writes a line to `fh2-mods\platform.log`, prefixed with the mod's name. |
| `set_file(mod, media_path, data, size)` | 2 | Replaces (or adds) a file under `game:\media`. A zip's name stands in for the zip, so `"UI/Controls/HUDRadioControl.xaml"` means that member of `media\UI.zip`. Files are written to the Extra Options `mediaoverride` folder, which the game reads before the original media. |
| `hook_function(mod, name, target, detour, &original)` | 3 | Creates an inline hook, switched off. `name` shows up in the log and in crash reports. |
| `enable_hooks(mod, NULL)` | 3 | Switches on all of the mod's hooks in one go. |
| `wait_for_code(timeout_ms)` | 3 | Waits until the game is mapped and Extra Options has finished patching it, then returns the runtime image base. Call it from your own thread. |

- **One mod per file.** `set_file` refuses a file that another mod already replaces. Two station mods built this way would collide on `RadioInfo_*.xml` and `HUDRadioControl.xaml`, and the second one loses. Sharing a station list between mods would need coordination this repo doesn't attempt (see [Limitations](making-a-radio-station-mod.md#limitations)).
- **Hook addresses are runtime.** Azimuth unpacks and relocates the game at runtime, so hook `base + RVA`, where `base` comes from `wait_for_code`.

---

## Addresses

These RVAs are for **FH2 Xbox One build 1.0.0.64 as run by Azimuth** only, and are relative to the image base. They are almost certainly wrong for any other build. The [address-finding notes](finding-addresses.md) cover how they were located.

| RVA | Function | Signature (x64) | Role | Hooked |
|---|---|---|---|---|
| `0x668810` | `RadioModule::IsStationUnlocked` | `char (void* this, const std::string* name)` | Eligibility check while cycling. 1 = skip, 0 = eligible. | yes |
| `0x1a98920` | `FMOD::Studio::System::getSoundInfo` | `int (void* system, const char* key, FMOD_STUDIO_SOUND_INFO* info)` | Resolves a `SoundName` to sound data. | yes |
| `0x1997740` | `FMOD::System::createSound` | `int (void* system, const char* name_or_data, unsigned mode, FMOD_CREATESOUNDEXINFO* exinfo, FMOD::Sound** out)` | Opens the sound. The game adds `0x10080` to `mode`. | yes |
| `0x199f510` | `FMOD::Sound::getSubSound` | `int (void* sound, int index, FMOD::Sound** out)` | Picks the subsound out of the opened sound. | yes |
| `0x660e50` | `ChangeStation` | (not needed) | Cycling. Calls `IsStationUnlocked`, then applies the change through a virtual call. | no |
| `0x65b310` | `SetStation` | (not needed) | Not on the cycling path. | no |

The FMOD names are the exe's own export names. The radio-module names are labels given during the research, after the stateflow states that call those functions (`States::RadioIsStationUnlocked`, `States::RadioSetStation` and so on).

---

## Open questions

- What `RecentlyPlayedListSize` does, and whether values other than `-1` matter.
- When exactly `EventTracks` and `ShortStingers` are used.
- Whether the game tries to load `HZ2_MX_Radio_10_*` banks for a new station, and what happens when they're missing.
- Station numbers above 10, and whether 8 and 9 are really reserved.
- Whether an empty playlist is safe.
- Whether DJ lines served this way play on their `GameEvent`. Tracks and stingers are verified in game; DJ lines go through the same audio path, but nobody has heard one fire in game yet.
- MP3 and FLAC through the same path.
