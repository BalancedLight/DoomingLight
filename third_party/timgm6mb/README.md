# TimGM6mb

TimGM6mb is a compact General MIDI SoundFont by Tim Brechbill and David
Bolton, distributed under GPL-2.0. It is Dooming Light's default instrument
bank. Players can select another SF2 file or Classic OPL in Mods settings.

`tools/soundfont_assets.py` prepares the unchanged SF2 from Debian's pinned
`timgm6mb-soundfont_1.3.orig.tar.gz` source archive. Its SHA-256 matches
the archive checksum in Debian's `timgm6mb-soundfont_1.3-5.dsc` metadata:

`af8f3a00e416dfb262bcaa904a1c84df04a51b72bbc1313aed012bc754bdf99b`

The SF2 SHA-256 is:

`c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854`

The corresponding-source ZIP includes the editable `TimGM6mb.sf2` alongside
its license and notice. Builds from that ZIP reuse the included file. For
offline builds from Git, place the official archive in `build/soundfont/`.
No SoundFont is downloaded at game runtime.
