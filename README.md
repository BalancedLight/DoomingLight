<p align="center">
  <img
    src="https://github.com/user-attachments/assets/40dd9126-4a99-4897-bc28-5aeda323c000"
    alt="DOOM Logo"
    width="1280"
  />
</p>

<p align="center">
Dooming Light brings Doom's weapons, status bar, sounds and music to Dying
Light Player maps through DLUMA. Dying Light supplies the maps, enemies,
objectives, and interactions.
</p>

<hr>

## Beta compatibility

Version 0.1.0-beta.1 supports single-player Player maps on the following x64
Player build:

| Module | SHA-256 |
| --- | --- |
| `DyingLightPlayer.exe` | `61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A` |
| `gamedll_x64_rwdi.dll` | `CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95` |
| `engine_x64_rwdi.dll` | `A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C` |

Other Player builds, the main campaign executable, multiplayer, Doom maps and
PWAD modifications are outside this beta. Do not enable another combat or HUD
replacement at the same time. A game update may require a new mod build.

Keyboard, mouse and XInput controller combat are supported. Doom's ground
movement and parts of death, respawn and player-voice presentation are still
being developed. Movement uses Dying Light's
normal parkour controls, including jumping, climbing and free look.

## Requirements and installation

Install DLUMA and an original Doom, Ultimate Doom, or Doom II IWAD from your
own installation. Only the selected game's IWAD is required. Doom II is chosen
automatically when both games are found in Steam libraries; Mods configuration
also accepts explicit WAD paths. Game and WAD changes apply on the next map.

1. Extract the package into `mods/native/global/doom.mode` in your DLUMA
   installation.
2. Enable Dooming Light and configure the WAD path in Mods if discovery did
   not find it.
3. Restart Player to load the native mod.

Disabling a native mod also requires restarting Player. To uninstall, disable
the mod, restart Player, and remove the `doom.mode` folder. If the mod does
not activate, check the Player build above, the selected IWAD, and DLUMA's
runtime log.

## Playing

Each map and respawn starts with 100 health, no armor, all available weapons,
and full standard ammunition. Doom II adds the super shotgun. Doom inventory
is separate from Dying Light's inventory. Supply drops appear after hostile
kills; pickup text appears at the top left. Supported weapon, ammunition and medkit pickups
convert into Doom supplies. Excess converted supplies remain as Doom pickups.
Some native pickup paths and ordinary ground-item visuals are not yet replaced. Quest items,
crafting materials, cash and unrecognized items retain their normal behavior.
Some native container capacity checks still apply before conversion.

Left mouse and the configurable extra fire key (Left Ctrl by default) fire.
Weapon keys 1–7 select the classic slots; 1 cycles melee weapons and 3 cycles
the shotgun and super shotgun when available. Native attacks and equipped
throwables are disabled. Inventory, map, quest and skill-menu hotkeys are also
disabled during Doom gameplay; Escape opens the pause menu and Mods settings.
Computer Area Map restores access to the native map for the current level.
Carried objects use native throw controls; Doom weapons require a fresh fire
press after the object is thrown or dropped.
New skill XP is suppressed while the mode is active.

On a controller, right trigger fires and D-pad Left/Right select the previous
and next weapon. X interacts; jump, parkour, flashlight and the other native
movement controls retain their bindings. Mods settings can move fire to the
left trigger or reverse weapon cycling. The first connected XInput controller
is used.

The status bar fits the screen width by default. Its scale and the weapon
sprite scale are separate. Doom graphics use point filtering by default;
**Bilinear Doom textures** smooths the HUD, weapons, effects and pickups.
**Projectile lights** illuminates plasma and BFG shots and their impacts,
BFG tracer bursts, and rocket explosions.

Music settings choose Doom music, Dying Light music, or silence. Default track
1 selects E1M1 in Doom or MAP01 in Doom II. Doom II uses track numbers 1–35;
Doom also accepts its original `IDMUS` numbers 11–45, and settings 1–9 select
E1M1–E1M9 while 10 selects E2M1. The `IDMUS` cheat uses the original game
numbering. Select **SoundFont (SF2)** and provide your own
SF2 file to use different instruments; Classic OPL is used if it cannot be
loaded. Sound effects include `DSNOWAY`, Doom's failed-interaction sound.

Cheats include `IDDQD`, `IDFA`, `IDKFA`, `IDCHOPPERS`, `IDCLIP` and
`IDMUSnn`, plus `IDBEHOLD` followed by V (invulnerability), S (berserk),
I (partial invisibility), R (radiation suit), A (computer map), or L (light
amplification). The radiation suit protects against acid and fire. Partial
invisibility uses native camouflage; light amplification uses a light around
the camera. Computer Area Map reveals fog visually without changing saved
exploration. Doom keycards do not unlock Dying Light quest doors. `IDCLIP` uses
Player's developer fly mode. Doom weapons are unavailable while flying;
normal parkour controls remain available after leaving noclip.

## Build

Install Python 3.11 or newer and Visual Studio C++ x64 tools with a Windows SDK.
From this directory run:

```bat
build_doom_mode_msvc.bat Release
```

The source archive includes its SDK snapshot, package tools and OPL emulator.
Output is written to `build/Release` and `dist/`. The versioned ZIP includes
the native DLL, configuration, licenses and corresponding source. Building does
not install the mod or launch the game.

For engine integration work, `build_doom_mode_msvc.bat Debug Lab` builds a
diagnostic DLL under `build/Lab`. Stage it with `--lab --output-dir` in a
separate directory.

## Source and assets

The C core adapts Doom's gameplay values and rules. Music decoding and
instrument handling use the Doom and Chocolate Doom formats with Nuked OPL3
synthesis. See `THIRD_PARTY_NOTICES.md` and the included licenses.

Doom graphics, sounds and music are loaded from the user's local IWAD.
SoundFont music uses the separately selected SF2 file. The distribution
contains no Doom game data or SoundFonts.

## Disclaimer
Some additions to this application were created with assistance from AI

> "In here..? **Doom.**"
