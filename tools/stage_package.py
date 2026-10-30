#!/usr/bin/env python3
"""Stage and package the Dooming Light native mod."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import zipfile
from pathlib import Path

from dluma_mod_package import PackageError, create_package, validate_lock, write_lock

ROOT = Path(__file__).resolve().parents[1]
MOD_RELATIVE = Path("mods/native/global/doom.mode")


def inspect_module(path: Path) -> dict[str, object]:
    image = path.read_bytes()
    if image[:2] != b"MZ" or len(image) < 64:
        raise PackageError("The native module is not a PE image")
    pe = struct.unpack_from("<I", image, 60)[0]
    if pe + 264 > len(image) or image[pe:pe + 4] != b"PE\0\0":
        raise PackageError("The native module has an invalid PE header")
    machine, count = struct.unpack_from("<HH", image, pe + 4)
    optional_size = struct.unpack_from("<H", image, pe + 20)[0]
    optional = pe + 24
    if machine != 0x8664 or struct.unpack_from("<H", image, optional)[0] != 0x20B:
        raise PackageError("Dooming Light requires an x64 PE32+ module")
    sections = []
    for index in range(count):
        section = optional + optional_size + index * 40
        if section + 40 > len(image):
            raise PackageError("The native module has an incomplete section table")
        virtual_size, rva, size, offset = struct.unpack_from("<IIII", image, section + 8)
        sections.append((rva, size, offset))

    def file_offset(rva: int, size: int) -> int:
        for start, length, offset in sections:
            if start <= rva and rva + size <= start + length:
                result = offset + rva - start
                if result + size <= len(image):
                    return result
        raise PackageError("The native module has an invalid export address")

    export_rva, export_size = struct.unpack_from("<II", image, optional + 112)
    if export_size < 40:
        raise PackageError("The native module has no export table")
    export = file_offset(export_rva, 40)
    names_count = struct.unpack_from("<I", image, export + 24)[0]
    names = struct.unpack_from("<I", image, export + 32)[0]
    names_offset = file_offset(names, names_count * 4)
    exports = []
    for index in range(names_count):
        name_rva = struct.unpack_from("<I", image, names_offset + 4 * index)[0]
        name_offset = file_offset(name_rva, 1)
        end = image.find(b"\0", name_offset, name_offset + 512)
        if end < 0:
            raise PackageError("The native module has an invalid export name")
        exports.append(image[name_offset:end].decode("ascii"))
    if exports != ["DLUMANativeMod_GetApiV2"]:
        raise PackageError("The native module must export only DLUMANativeMod_GetApiV2")
    return {"architecture": "x64", "exports": exports}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    parser.add_argument("--lab", action="store_true", help="Stage a separate diagnostic mod")
    args = parser.parse_args()
    try:
        module = args.module.resolve(strict=True)
        report = inspect_module(module)
        source = ROOT / "package" / MOD_RELATIVE
        output = args.output_dir.resolve()
        if args.lab and output == (ROOT / "dist").resolve():
            raise PackageError("Lab packages require a separate output directory")
        relative = Path("mods/native/global/doom.mode.lab") if args.lab else MOD_RELATIVE
        destination = output / relative
        destination.mkdir(parents=True, exist_ok=True)
        for filename in ("mod.dluma.json", "config.dluma.json", "README.txt", "LICENSE", "THIRD_PARTY_NOTICES.md"):
            shutil.copyfile(source / filename, destination / filename)
        with zipfile.ZipFile(destination / "DoomMode.pak", "w", compression=zipfile.ZIP_STORED) as pack:
            info = zipfile.ZipInfo("data/menu/scr/DLUMA_ModHud.xui", date_time=(2000, 1, 1, 0, 0, 0))
            info.external_attr = 0o100644 << 16
            pack.writestr(info, (ROOT / "assets/DLUMA_ModHud.xui").read_bytes())
        shutil.copyfile(ROOT / "LICENSE.DLUMA", destination / "LICENSE.DLUMA")
        shutil.copyfile(ROOT / "third_party/opl/LICENSE", destination / "LICENSE.OPL")
        shutil.copyfile(ROOT / "third_party/opl/NOTICE.txt", destination / "NOTICE.OPL.txt")
        shutil.copyfile(ROOT / "third_party/minhook/LICENSE.txt", destination / "LICENSE.MINHOOK.txt")
        shutil.copyfile(module, destination / "DoomMode.dll")
        manifest = json.loads((destination / "mod.dluma.json").read_text(encoding="utf-8"))
        if args.lab:
            manifest.update(id="doom.mode.lab", name="Dooming Light Lab", version="0.1.0-lab")
            (destination / "mod.dluma.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        write_lock(destination)
        lock = validate_lock(destination)
        version = manifest["version"]
        source_archive = output / f"DoomingLight-{version}-source.zip"
        with zipfile.ZipFile(source_archive, "w", zipfile.ZIP_DEFLATED) as archive:
            for directory in ("src", "sdk", "tools", "third_party", "package", "assets"):
                for item in sorted((ROOT / directory).rglob("*")):
                    if item.is_file() and "__pycache__" not in item.parts:
                        archive.write(item, item.relative_to(ROOT))
            for name in ("LICENSE", "LICENSE.DLUMA", "THIRD_PARTY_NOTICES.md", "README.md", "build_doom_mode_msvc.bat"):
                archive.write(ROOT / name, name)
        # Include corresponding source with every binary distribution.
        shutil.copyfile(source_archive, destination / "source.zip")
        write_lock(destination)
        lock = validate_lock(destination)
        package = create_package(destination, output / f"DoomingLight-{version}.zip")
        print(json.dumps({"module": report, "artifacts": len(lock["artifacts"]), "package": package}, indent=2))
        return 0
    except (OSError, ValueError, struct.error, PackageError) as error:
        parser.exit(1, f"Dooming Light package error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
