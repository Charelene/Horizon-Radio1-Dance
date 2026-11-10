# Finding the addresses

The DLL hooks four functions by address. Those addresses only hold for FH2 Xbox One build 1.0.0.64 as run by Azimuth (see the [address table](how-fh2-radio-works.md#addresses)). These notes record how each function was found, so you can find it again in another build or check it in this one. They cover only what was actually done. The Ghidra helper scripts used during the research aren't part of this repo.

## Setup

- **The executable:** `forza_durango_release_final.exe`, from the game's `Mount` folder, loaded in [Ghidra](https://ghidra-sre.org/). The research ran Ghidra headless (`analyzeHeadless`), reusing an already-analyzed project with `-noanalysis -readOnly` and postScripts that print what's needed.
- **Addresses:** Ghidra shows absolute addresses at the preferred image base, `0x140000000`. **RVA = address − `0x140000000`.** That RVA is what the DLL adds to the base it gets from `wait_for_code`. The platform logs hooks as `game+0x...`, so you can check them against your table.
- **A second opinion:** `x86_64-w64-mingw32-objdump -d -M intel --start-address=... --stop-address=...` on the exe is a quick way to see the raw instructions when the decompiler output looks odd.

## FMOD Core: look up the export names

The exe exports the FMOD Core API under its MSVC-mangled names, so Ghidra labels those functions for you:

| Function | Export (ordinal) | RVA |
|---|---|---|
| `FMOD::System::createSound` | `?createSound@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDIPEAUFMOD_CREATESOUNDEXINFO@@PEAPEAVSound@2@@Z` (22) | `0x1997740` |
| `FMOD::Sound::getSubSound` | `?getSubSound@Sound@FMOD@@QEAA?AW4FMOD_RESULT@@HPEAPEAV12@@Z` (164) | `0x199f510` |

In another build, search the exports or symbol tree for `createSound@System@FMOD` and `getSubSound@Sound@FMOD`.

## FMOD Studio `getSoundInfo`: follow its error string

`FMOD::Studio::System::getSoundInfo` has no export name. FMOD's API entry points report failures under their own names, so the string **`System::getSoundInfo`** leads to it: the function that uses that string in its error path is the entry point, at RVA `0x1a98920`.

Ghidra's decompiler shows only that error-reporting part, which looks like a stub. The disassembly shows the function keeping its three arguments (`rcx`, `rdx` and `r8`: the system, the key and the info pointer) and calling on. Hooking its entry works.

**Confirm it in game:** log the `key` argument from a hook. Radio tracks and stingers pass through it with their `SoundName` from `RadioInfo_*.xml` (verified). DJ lines are expected to as well, but that hasn't been seen in game. Seeing your own keys arrive is the proof.

## The radio module: start from the stateflow states

Ghidra recovers class names for the game's stateflow states. Their vtables lead into the radio code. The research dumped the vtables of every class named `States::Radio*`, among them:

- `RadioIsStationUnlocked`,
- `RadioSetStationUnlocked`,
- `RadioSetStation`,
- `RadioChangeStation`,
- `RadioSetTrackPlaylist`.

It also dumped the vtable of `EntityModel::RadioModule`.

- **`IsStationUnlocked`, RVA `0x668810`:** the handler of the `States::RadioIsStationUnlocked` state (RVA `0x8fc850`) calls this function. So does `ChangeStation` (RVA `0x660e50`), the code that runs when you cycle the radio. Two callers with those names identify it.
- **`SetStation`, RVA `0x65b310`:** called from the `States::RadioSetStation` handler. It isn't on the cycling path.

### String references help too

These strings in the exe have code references that land in the radio code:

- **Stateflow node names:** `RadioStationUnlocked`, `RadioSetStationUnlocked`.
- **RadioInfo attribute names:** `StartNextTrack`, `DJDrop`, `GameEvent`.
- **Bank name format strings:** `%sHZ2_MX_Radio_%02d_Tracks.bank`.

Look up their cross-references to land near the code that uses them.

## Check semantics in game, not just in the decompiler

Two of the findings that cost the most time were about meaning, not address.

**The unlock check's polarity.** The decompiled `ChangeStation` didn't make it clear whether `IsStationUnlocked` returning 1 means "stop here" or "skip". A guess of 1 hid the station. What settled it was a diagnostic build that left the check alone and **logged the station table with the game's own answer for each station**. The stock stations got 0 and the new one got 1. Before you force a return value, log what the game returns for things you know work.

**Struct layouts.** `FMOD_STUDIO_SOUND_INFO` has padding before `exinfo`, which starts at `0x10` (see [Gotcha 1](how-fh2-radio-works.md#gotcha-1-the-fmod_studio_sound_info-layout-verified)). Check every offset against FMOD's public headers, mind the alignment, and pin the layout with `_Static_assert` so the compiler catches a slip. This build's `FMOD_CREATESOUNDEXINFO` is 216 bytes (its `cbsize`). A different FMOD version may differ.

## Porting checklist

1. **Find the four functions** in the new build:
   - the two FMOD exports, by name;
   - `getSoundInfo`, by its error string;
   - `IsStationUnlocked`, from the `States::RadioIsStationUnlocked` handler.
2. **Recompute the RVAs** and update the four `RVA_*` defines in `horizon_wave.c`.
3. **Check the signatures and struct offsets** against the decompiled callers, and keep the `_Static_assert`s.
4. **Check the log:** confirm each `hooked function game+0x...` line, and log the keys passing through `getSoundInfo` before you rely on them.
5. **Check the polarity:** log `IsStationUnlocked`'s answer for a stock station and for yours.
