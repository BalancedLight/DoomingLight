"""Prepare the pinned GPL-2.0 TimGM6mb SoundFont for distribution."""

from __future__ import annotations

import hashlib
import os
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ARCHIVE = "timgm6mb-soundfont_1.3.orig.tar.gz"
URL = f"https://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/{ARCHIVE}"
ARCHIVE_SHA256 = "af8f3a00e416dfb262bcaa904a1c84df04a51b72bbc1313aed012bc754bdf99b"
FONT_SHA256 = "c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854"
MAX_BYTES = 16 * 1024 * 1024


def matches(path: Path, expected: str) -> bool:
    if not path.is_file() or path.stat().st_size > MAX_BYTES:
        return False
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest() == expected


def prepare_soundfont(root: Path) -> Path:
    # Corresponding-source ZIPs contain the editable SF2 itself.
    source_font = root / "third_party/timgm6mb/TimGM6mb.sf2"
    if matches(source_font, FONT_SHA256):
        return source_font
    cache = root / "build/soundfont"
    cache.mkdir(parents=True, exist_ok=True)
    font = cache / "TimGM6mb.sf2"
    if matches(font, FONT_SHA256):
        return font
    archive = cache / ARCHIVE
    if not matches(archive, ARCHIVE_SHA256):
        descriptor, temporary = tempfile.mkstemp(prefix="soundfont-", suffix=".tar.gz", dir=cache)
        try:
            with os.fdopen(descriptor, "wb") as target:
                with urllib.request.urlopen(URL, timeout=30) as response:
                    total = 0
                    while block := response.read(1024 * 1024):
                        total += len(block)
                        if total > MAX_BYTES:
                            raise ValueError("SoundFont download exceeds its size limit")
                        target.write(block)
            if not matches(Path(temporary), ARCHIVE_SHA256):
                raise ValueError("SoundFont archive checksum mismatch")
            os.replace(temporary, archive)
        finally:
            Path(temporary).unlink(missing_ok=True)
    with tarfile.open(archive, "r:gz") as package:
        member = package.getmember("timgm6mb-soundfont_1.3/TimGM6mb.sf2")
        if not member.isfile() or member.size > MAX_BYTES:
            raise ValueError("Invalid bundled SoundFont archive member")
        with package.extractfile(member) as source:
            data = source.read(MAX_BYTES + 1)
        if hashlib.sha256(data).hexdigest() != FONT_SHA256:
            raise ValueError("Bundled SoundFont checksum mismatch")
        font.write_bytes(data)
    return font
