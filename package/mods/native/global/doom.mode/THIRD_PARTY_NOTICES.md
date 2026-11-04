# Third-party software

Dooming Light's gameplay adaptation is distributed under GPL-2.0-or-later.
See LICENSE and the notices in the source files.

- Doom source release: Copyright id Software, GPL-2.0-or-later.
  https://github.com/id-Software/DOOM
- Music format and playback references: Chocolate Doom, GPL-2.0-or-later.
  Its `src/i_oplmusic.c` credits id Software and Simon Howard; `src/mus2mid.c`
  also credits Ben Ryves. The project uses its own MUS/MIDI parser and
  GENMIDI sequencer. The reference revision examined is
  37ee739de526e08717970ba1faa24cd78f81da08:
  https://github.com/chocolate-doom/chocolate-doom/tree/37ee739de526e08717970ba1faa24cd78f81da08
- Nuked OPL3 and Nuked-OPL3-fast: Copyright Nuke.YKT and Tony Gies,
  LGPL-2.1-or-later. Source, license and pinned blob identities are in
  third_party/opl.
- DLUMA C SDK, setup and package tools: MIT, see LICENSE.DLUMA.
- TinySoundFont 0.9: Copyright Bernhard Schelling and Steve Folta, MIT;
  see LICENSE.TSF.txt and third_party/tsf for the source and pinned revision.
- TimGM6mb General MIDI SoundFont: Copyright 2004 Tim Brechbill and
  2010 David Bolton, GPL-2.0. See LICENSE.TIMGM6MB.txt and NOTICE.TIMGM6MB.txt.
  The editable SF2 is included in the corresponding-source archive.
  Source and checksum provenance: third_party/timgm6mb/README.md.
- MinHook 1.3.4: Copyright Tsuda Kageyu and contributors, BSD 2-Clause;
  see LICENSE.MINHOOK.txt and third_party/minhook.

- Freedoom Phase 1 and Phase 2 0.13.0: Copyright 2001-2024 Contributors to
  the Freedoom project, BSD 3-Clause. The official release WADs are included
  as a fallback. See LICENSE.FREEDOOM.txt, CREDITS.FREEDOOM.txt and
  CREDITS.FREEDOOM-MUSIC.txt; source and release provenance are documented
  in third_party/freedoom/README.md.
  https://github.com/freedoom/freedoom/releases/tag/v0.13.0

Retail Doom graphics, sound effects, instruments and music are read from
the user's IWAD. Retail game data is not included in this distribution.

The included secret notification cue is original audio created and supplied
for Dooming Light by its maintainer.
