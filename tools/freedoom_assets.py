"""Fetch and verify the pinned, freely redistributable Freedoom game data."""

from __future__ import annotations

import hashlib
import os
import tempfile
import urllib.request
import zipfile
from pathlib import Path

VERSION = "0.13.0"
ARCHIVE = f"freedoom-{VERSION}.zip"
URL = f"https://github.com/freedoom/freedoom/releases/download/v{VERSION}/{ARCHIVE}"
# Published in the release's signed freedoom-0.13.0-CHECKSUM file.
SHA256 = "3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59"
WADS = {
    "freedoom1.wad": "7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d",
    "freedoom2.wad": "a8772e088847032510d97ba2312406a6998f21cbab44d4ff10696faa9c0ecd4b",
}
MAX_BYTES = 64 * 1024 * 1024


def digest(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def prepare_freedoom(cache: Path) -> list[Path]:
    """Return verified Phase 1/2 WADs, downloading only on an empty build cache."""
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ARCHIVE
    if not archive.is_file() or archive.stat().st_size > MAX_BYTES or digest(archive) != SHA256:
        descriptor, temporary = tempfile.mkstemp(prefix="freedoom-", suffix=".zip", dir=cache)
        try:
            with os.fdopen(descriptor, "wb") as target:
                with urllib.request.urlopen(URL, timeout=30) as response:
                    total = 0
                    while block := response.read(1024 * 1024):
                        total += len(block)
                        if total > MAX_BYTES:
                            raise ValueError("Freedoom download exceeds its size limit")
                        target.write(block)
            if digest(Path(temporary)) != SHA256:
                raise ValueError("Freedoom archive checksum mismatch")
            os.replace(temporary, archive)
        finally:
            Path(temporary).unlink(missing_ok=True)
    result = []
    with zipfile.ZipFile(archive) as package:
        for name, expected in WADS.items():
            member = package.getinfo(f"freedoom-{VERSION}/{name}")
            if member.file_size > MAX_BYTES:
                raise ValueError("Freedoom WAD exceeds its size limit")
            data = package.read(member)
            if hashlib.sha256(data).hexdigest() != expected or data[:4] != b"IWAD":
                raise ValueError(f"Freedoom WAD checksum mismatch: {name}")
            output = cache / name
            output.write_bytes(data)
            result.append(output)
    return result


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    for wad in prepare_freedoom(root / "build/freedoom"):
        print(f"Verified Freedoom {VERSION}: {wad.name}")
