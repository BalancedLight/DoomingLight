# Freedoom game data

Dooming Light bundles Freedoom Phase 1 and Phase 2 version 0.13.0 as an
optional fallback. Retail Doom assets are never distributed with the mod.

- Project: https://freedoom.github.io/
- Release: https://github.com/freedoom/freedoom/releases/tag/v0.13.0
- Source: https://github.com/freedoom/freedoom/tree/v0.13.0
- License: `COPYING.txt` (BSD 3-Clause). Contributor and music credits are
  reproduced unchanged in `CREDITS.txt` and `CREDITS-MUSIC.txt`.

`tools/freedoom_assets.py` downloads the official release during packaging
and verifies its SHA-256 against the published release checksum. It also
verifies each WAD before staging. The WADs and download cache are build
artifacts; source archives contain this helper and the license/credits.

For offline builds, place the official `freedoom-0.13.0.zip` in
`build/freedoom/` before building. The SHA-256 must be:

`3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59`
