#!/usr/bin/env python3
"""Stage and package the Dooming Light native mod."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import zipfile
from pathlib import Path

from dluma_mod_package import PackageError, create_package, validate_lock, write_lock

ROOT = Path(__file__).resolve().parents[1]
MOD_RELATIVE = Path("mods/native/global/doom.mode")


def validate_configuration(path: Path) -> None:
    """Validate the DLUMA native configuration v1 contract before staging."""
    # Limits match the DLUMA native configuration v1 parser bundled with this SDK.
    raw = path.read_bytes()
    if not raw or len(raw) > 64 * 1024:
        raise PackageError("Configuration schema must be at most 64 KiB")

    def object_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result = {}
        for key, value in pairs:
            if key in result:
                raise PackageError(f"Duplicate configuration field: {key}")
            result[key] = value
        return result

    def invalid_constant(value: str) -> None:
        raise PackageError(f"Invalid configuration number: {value}")

    try:
        schema = json.loads(raw.decode("utf-8"), object_pairs_hook=object_pairs,
                            parse_constant=invalid_constant)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageError(f"Invalid configuration JSON: {error}") from error

    def fields(value: object, required: set[str], optional: set[str], label: str) -> dict:
        if not isinstance(value, dict) or not required <= value.keys() or value.keys() - required - optional:
            raise PackageError(f"Invalid {label} fields")
        return value

    def identifier(value: object, label: str) -> str:
        if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", value):
            raise PackageError(f"Invalid {label}")
        base = value.split(".", 1)[0].upper()
        if value.endswith(".") or base in {"CON", "PRN", "AUX", "NUL"} or re.fullmatch(r"(?:COM|LPT)[1-9]", base):
            raise PackageError(f"Invalid {label}")
        return value

    def text(value: object, label: str, byte_limit: int, char_limit: int, empty: bool = False) -> None:
        if not isinstance(value, str) or (not value and not empty):
            raise PackageError(f"Invalid {label}")
        try:
            byte_count = len(value.encode("utf-8"))
        except UnicodeEncodeError as error:
            raise PackageError(f"Invalid {label}") from error
        if byte_count > byte_limit or len(value) > char_limit or any(ord(c) < 32 or ord(c) == 127 for c in value):
            raise PackageError(f"Invalid {label}")

    def integer(value: object, label: str) -> int:
        if type(value) is not int or not -(2**31) <= value <= 2**31 - 1:
            raise PackageError(f"Invalid {label}")
        return value

    root = fields(schema, {"schemaVersion", "categories"}, set(), "configuration")
    if type(root["schemaVersion"]) is not int or root["schemaVersion"] != 1:
        raise PackageError("Unsupported configuration schema version")
    categories = root["categories"]
    if not isinstance(categories, list) or not 1 <= len(categories) <= 8:
        raise PackageError("Configuration requires 1 to 8 categories")
    category_ids, setting_keys = set(), set()
    for category in categories:
        category = fields(category, {"id", "name", "settings"}, set(), "category")
        category_id = identifier(category["id"], "category id")
        if category_id in category_ids:
            raise PackageError(f"Duplicate configuration category: {category_id}")
        category_ids.add(category_id)
        text(category["name"], "category name", 96, 96)
        settings = category["settings"]
        if not isinstance(settings, list) or not 1 <= len(settings) <= 9:
            raise PackageError(f"Configuration category {category_id} requires 1 to 9 settings")
        for setting in settings:
            setting = fields(setting, {"key", "name", "description", "type", "default"},
                             {"min", "max", "step", "maxLength", "options"}, "setting")
            key = identifier(setting["key"], "setting key")
            if key in setting_keys:
                raise PackageError(f"Duplicate configuration setting: {key}")
            setting_keys.add(key)
            if len(setting_keys) > 48:
                raise PackageError("Configuration allows at most 48 settings")
            text(setting["name"], "setting name", 96, 96)
            text(setting["description"], "setting description", 256, 256)
            kind, default = setting["type"], setting["default"]
            extras = setting.keys() - {"key", "name", "description", "type", "default"}
            if kind == "boolean":
                if type(default) is not bool or extras:
                    raise PackageError(f"Invalid boolean setting: {key}")
            elif kind == "integer":
                if extras != {"min", "max", "step"}:
                    raise PackageError(f"Invalid integer setting: {key}")
                minimum, maximum = integer(setting["min"], key), integer(setting["max"], key)
                step, number = integer(setting["step"], key), integer(default, key)
                if step <= 0 or minimum > maximum or not minimum <= number <= maximum or (maximum-minimum) % step or (number-minimum) % step:
                    raise PackageError(f"Invalid integer range or default: {key}")
            elif kind == "keybind":
                if not extras <= {"min", "max", "step"} or not 1 <= integer(default, key) <= 254:
                    raise PackageError(f"Invalid keybind setting: {key}")
                if any(integer(setting[field], key) != expected for field, expected in (("min", 1), ("max", 254), ("step", 1)) if field in setting):
                    raise PackageError(f"Invalid keybind limits: {key}")
            elif kind == "enum":
                options = setting.get("options")
                if extras != {"options"} or not isinstance(options, list) or not 2 <= len(options) <= 16:
                    raise PackageError(f"Invalid enum options: {key}")
                values = set()
                for option in options:
                    option = fields(option, {"value", "name"}, set(), "enum option")
                    value = identifier(option["value"], "enum value")
                    if value in values:
                        raise PackageError(f"Duplicate enum value: {value}")
                    values.add(value)
                    text(option["name"], "enum option name", 96, 96)
                if not isinstance(default, str) or default not in values:
                    raise PackageError(f"Invalid enum default: {key}")
            elif kind == "text":
                if extras != {"maxLength"}:
                    raise PackageError(f"Invalid text setting: {key}")
                maximum = integer(setting["maxLength"], key)
                if not 1 <= maximum <= 1024:
                    raise PackageError(f"Invalid text length: {key}")
                text(default, "text default", 4096, maximum, empty=True)
            else:
                raise PackageError(f"Unsupported configuration setting type: {kind}")


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
        validate_configuration(source / "config.dluma.json")
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
        shutil.copyfile(ROOT / "third_party/tsf/LICENSE", destination / "LICENSE.TSF.txt")
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
