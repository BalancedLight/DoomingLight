# Dooming Light

A DLUMA native mod bringing Doom's player mechanics and presentation to Dying Light Player maps.

## Development build

Dooming Light is under development. The standard development build leaves
normal gameplay in control while the player integration is being completed.

The supported target is single-player Dying Light Player, including Player
launched from Developer Tools. The main campaign executable and multiplayer
are outside this version's target.

## Requirements and configuration

Use the current DLUMA build and an original registered Doom, Ultimate Doom or
Doom II IWAD from your own installation. Doom II is selected automatically
when both games are found in Steam libraries. The Mods configuration also
accepts explicit WAD paths. Game and WAD changes apply on the next map.

The planned mode starts each map and respawn with all available weapons,
full standard ammunition, 100 health and no armor. Doom II adds the super
shotgun. The Traversal movement preset retains jumping, climbing and free look;
Classic uses horizontal aiming and disables jumping and climbing.

Music settings choose the Doom soundtrack, normal Dying Light music, or silence.
Default track numbers match `IDMUS`: `11` for Doom's E1M1, `01` for Doom II's
MAP01. Zero selects the first track automatically. Sound effects include
`DSNOWAY`, Doom's failed-interaction sound.
In Mods, select **SoundFont (SF2)** as the music synthesizer and set
**SoundFont file** to an SF2 from your own collection. Font changes prepare
the current track before switching playback. If the font cannot be loaded,
the track uses Classic OPL. No SoundFont is bundled.

The player simulation recognizes `IDDQD`, `IDFA`, `IDKFA`, `IDCHOPPERS` and `IDMUSnn`.
Doom keycards do not unlock Dying Light quest doors.

Left mouse and the configurable extra fire key (Left Ctrl by default) fire.
Weapon keys 1–7 select the classic slots; 1 cycles melee weapons and 3 cycles
the shotgun and super shotgun when available.
Inventory, map, quest and skill-menu hotkeys are disabled during Doom gameplay.
Escape still opens the pause menu and Mods settings.

The status bar fits the screen width by default. Its scale and weapon sprite
scale are separate. Doom graphics use point filtering; enable **Bilinear Doom
textures** to smooth the HUD, weapons, effects and pickups. These presentation
settings apply immediately.

Weapon sprites bob with movement. **Projectile lights** lets plasma and BFG
shots illuminate nearby surfaces. BFG spray hits show Doom's target-impact
animation.

## Build

Install Python 3.11 or newer and Visual Studio C++ x64 tools with a Windows SDK.
From this directory run:

```bat
build_doom_mode_msvc.bat Release
```

The source archive includes its SDK snapshot, package tools and OPL emulator.
Builds use paths relative to this folder. Output is written to `build/Release`
and `dist/`. The versioned development ZIP includes the native DLL, configuration,
licenses and corresponding source. No installation or game launch occurs.

For engine integration work, `build_doom_mode_msvc.bat Debug Lab` builds a
separate diagnostic DLL under `build/Lab`. Stage it with `--lab --output-dir`
in a separate private directory. The lab variant exercises the implemented
bindings while the normal build retains its complete-mode activation check.

To install the package, extract its files into
`mods/native/global/doom.mode` in your DLUMA installation. Enable and configure
it in Mods, then restart Player. Disabling native mods also requires restarting
Player. To uninstall, disable the mod, restart Player, and remove the
`doom.mode` folder.

## Source and assets

The C core adapts Doom's gameplay values and rules. Music decoding and instrument
handling use the Doom and Chocolate Doom formats with Nuked OPL3 synthesis.
See THIRD_PARTY_NOTICES.md and the included licenses.

All graphics, sounds, instruments and music are loaded from the user's local
IWAD. The distribution contains no Doom game data.



> "In here..? **Doom.**"
