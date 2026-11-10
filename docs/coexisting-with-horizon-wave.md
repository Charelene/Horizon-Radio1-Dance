# Coexisting with Horizon Wave Radio

Horizon Most Wanted and [Horizon Wave Radio](https://github.com/Charelene/Horizon-Wave-Radio) are two mods that each add a station. They need the same game files, so they cannot just sit side by side. This page says what the two do about it, and which parts have been seen working and which have not.

## The problem

Both mods deliver their station through file overrides, and both replace the same two files:

- `media\audio\RadioInfo_<lang>.xml` (all ten languages): the station list.
- `media\UI\Controls\HUDRadioControl.xaml`: the HUD, with one icon trigger per station.

The Mod Platform allows [one mod per file](how-fh2-radio-works.md#5-what-the-mod-platform-provides). A second mod's `set_file` for a file that is already replaced returns 0 and the file stays the first mod's. Left alone, the mod that loads second would lose its station.

Both mods also hook the same four game functions, and both unlock their station by name.

## What the two do

1. **Load order decides who owns the files.** The platform loads mods in file-name order (seen in `platform.log`: `FH2MP`, `HonkHonk`, `Horizon Wave Radio`). `HorizonMostWanted.dll` sorts before `HorizonWave.dll`, so this mod registers first and gets the files.
2. **The installer builds on top of Horizon Wave's files.** For each `RadioInfo_<lang>.xml` it takes `<azimuth>\HorizonWave\overrides\audio\RadioInfo_<lang>.xml` when that exists (the game's own file otherwise) and adds its station. It does the same for `HUDRadioControl.xaml`. The result holds both stations.
3. **The DLL serves both stations.** Besides its own `HMW_*` sounds, it serves `HW_*` sounds from `<azimuth>\HorizonWave\audio\` and says "unlocked" for the station named `Horizon Wave`, when that audio folder exists. So Horizon Wave's own DLL has nothing left to do.
4. **The station number is the first free one from 10.** The installer reads the `Number` of every station in the files it builds on, and in any other mod's `<azimuth>\<mod>\overrides\audio\RadioInfo_EN.xml`. With Horizon Wave (10) installed this station gets 11.

## What you have to do

**Run the Most Wanted installer again after you install, update or remove Horizon Wave Radio.**

- Horizon Wave's installer writes its own folder and knows nothing about this mod. Its new files do not reach the game while this mod owns the radio files.
- If you remove Horizon Wave and do not re-run, the station list still names `Horizon Wave`. The DLL no longer serves its audio, so that station is silent. Re-running rebuilds the files from the game's own.

`hmw-installer --uninstall` removes this mod's DLL and folder. Horizon Wave takes its files back the next time the game starts.

## What has been checked, and what has not

Checked (on a Windows machine, against real game files and Horizon Wave's real installed files, but without starting the game):

- the installer picks 11 when Horizon Wave's station 10 is in the files it builds on, and 10 when it is not;
- it adds the station to all ten language files and to the HUD on top of Horizon Wave's patched copies, without disturbing Horizon Wave's station or its HUD triggers;
- the DLL compiles without warnings.

**Not checked in the game:**

- That a second mod's `hook_function` on an address another mod already hooked either works or fails cleanly. If it fails, Horizon Wave's DLL logs `hook creation failed; the game build does not match` and does nothing, which is the intended outcome here: this DLL does the work for both. If the platform chains the two hooks instead, both DLLs would serve `HW_*`, which is also harmless. Look in `fh2-mods\platform.log` for those lines.
- That station number 11 works. The original verified 10 only. Numbers above 10 were untried.
- That an empty-looking station (a single silent stinger and a single silent DJ line) behaves like a stock one.

## If you add a third station mod

This arrangement is hard-wired for two. The installer only builds on Horizon Wave's files, and the DLL only knows `HW_`. The installer prints a note when it finds another mod's radio overrides and keeps that mod's numbers free, but it cannot merge its stations. Sharing the radio between more mods needs one mod to own the files and serve everyone's audio, which is what this DLL does for Horizon Wave. Add a row to `g_st` in [`horizon_most_wanted.c`](../mod/src/horizon_most_wanted.c) and teach `plan` in [`station.rs`](../installer/src/station.rs) about the other mod's folder.
