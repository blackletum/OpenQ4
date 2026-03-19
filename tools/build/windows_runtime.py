#!/usr/bin/env python3
"""Helpers for staging self-contained Windows runtime payloads for OpenQ4."""

from __future__ import annotations

import os
import re
import shutil
import struct
import subprocess
from enum import Enum
from pathlib import Path


PRODUCT_NAME = "OpenQ4"
OPENAL_RUNTIME_RELATIVE = Path("src/external/openal-soft/bin/win64/OpenAL32.dll")
WINDOWS_ROOT_RUNTIME_PATTERNS = (
    "OpenAL32.dll",
    "api-ms-win-crt-*.dll",
    "concrt*.dll",
    "msvcp*.dll",
    "ucrtbase.dll",
    "ucrtbased.dll",
    "vccorlib*.dll",
    "vcruntime*.dll",
)

RELEASE_IMPORT_TOKENS = (
    b"ucrtbase.dll",
    b"vcruntime140.dll",
    b"vcruntime140_1.dll",
    b"msvcp140.dll",
)
DEBUG_IMPORT_TOKENS = (
    b"ucrtbased.dll",
    b"vcruntime140d.dll",
    b"vcruntime140_1d.dll",
    b"msvcp140d.dll",
)

REQUIRED_RELEASE_RUNTIME_FILES = (
    "vcruntime140.dll",
    "vcruntime140_1.dll",
    "msvcp140.dll",
    "ucrtbase.dll",
)
REQUIRED_DEBUG_RUNTIME_FILES = (
    "vcruntime140d.dll",
    "vcruntime140_1d.dll",
    "msvcp140d.dll",
    "ucrtbased.dll",
)


class RuntimeFlavor(str, Enum):
    NONE = "none"
    RELEASE = "release"
    DEBUG = "debug"


def is_windows_host() -> bool:
    return os.name == "nt"


def _read_binary(path: Path) -> bytes:
    return path.read_bytes().lower()


def _rva_to_file_offset(rva: int, sections: list[tuple[int, int, int, int]]) -> int | None:
    for virtual_address, virtual_size, raw_size, raw_pointer in sections:
        section_size = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + section_size:
            return raw_pointer + (rva - virtual_address)
    return None


def _read_c_string(data: bytes, offset: int) -> bytes:
    if offset < 0 or offset >= len(data):
        return b""
    end = data.find(b"\0", offset)
    if end == -1:
        end = len(data)
    return data[offset:end]


def _read_pe_import_names(binary_path: Path) -> set[str]:
    data = binary_path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        return set()

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 4 > len(data) or data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        return set()

    file_header_offset = e_lfanew + 4
    number_of_sections = struct.unpack_from("<H", data, file_header_offset + 2)[0]
    size_of_optional_header = struct.unpack_from("<H", data, file_header_offset + 16)[0]
    optional_header_offset = file_header_offset + 20
    magic = struct.unpack_from("<H", data, optional_header_offset)[0]

    if magic == 0x10B:
        data_directory_offset = optional_header_offset + 96
    elif magic == 0x20B:
        data_directory_offset = optional_header_offset + 112
    else:
        return set()

    import_rva, _import_size = struct.unpack_from("<II", data, data_directory_offset + 8)
    if import_rva == 0:
        return set()

    section_offset = optional_header_offset + size_of_optional_header
    sections: list[tuple[int, int, int, int]] = []
    for index in range(number_of_sections):
        header_offset = section_offset + (index * 40)
        if header_offset + 40 > len(data):
            return set()
        virtual_size = struct.unpack_from("<I", data, header_offset + 8)[0]
        virtual_address = struct.unpack_from("<I", data, header_offset + 12)[0]
        raw_size = struct.unpack_from("<I", data, header_offset + 16)[0]
        raw_pointer = struct.unpack_from("<I", data, header_offset + 20)[0]
        sections.append((virtual_address, virtual_size, raw_size, raw_pointer))

    import_offset = _rva_to_file_offset(import_rva, sections)
    if import_offset is None:
        return set()

    import_names: set[str] = set()
    descriptor_size = 20
    while import_offset + descriptor_size <= len(data):
        descriptor = struct.unpack_from("<IIIII", data, import_offset)
        if descriptor == (0, 0, 0, 0, 0):
            break

        name_rva = descriptor[3]
        name_offset = _rva_to_file_offset(name_rva, sections)
        if name_offset is not None:
            import_name = _read_c_string(data, name_offset).decode("ascii", errors="ignore").lower()
            if import_name:
                import_names.add(import_name)

        import_offset += descriptor_size

    return import_names


def scan_runtime_imports(binary_path: Path) -> set[str]:
    import_names = _read_pe_import_names(binary_path)
    imports: set[str] = set()
    for token in RELEASE_IMPORT_TOKENS + DEBUG_IMPORT_TOKENS:
        if token.decode("ascii") in import_names:
            imports.add(token.decode("ascii"))
    return imports


def collect_openq4_windows_binaries(root_dir: Path) -> list[Path]:
    patterns = (
        f"{PRODUCT_NAME}-client_*.exe",
        f"{PRODUCT_NAME}-ded_*.exe",
        "openq4/game-sp_*.dll",
        "openq4/game-mp_*.dll",
    )
    binaries: list[Path] = []
    for pattern in patterns:
        binaries.extend(path for path in root_dir.glob(pattern) if path.is_file())
    return sorted(set(binaries))


def infer_runtime_flavor(root_dir: Path) -> RuntimeFlavor:
    has_release = False
    has_debug = False

    for binary_path in collect_openq4_windows_binaries(root_dir):
        imports = scan_runtime_imports(binary_path)
        if any(token.decode("ascii") in imports for token in RELEASE_IMPORT_TOKENS):
            has_release = True
        if any(token.decode("ascii") in imports for token in DEBUG_IMPORT_TOKENS):
            has_debug = True

    if has_release and has_debug:
        raise RuntimeError(
            f"Mixed MSVC CRT flavors detected under '{root_dir}'. "
            "OpenQ4 runtime binaries must all use the same CRT flavor."
        )
    if has_debug:
        return RuntimeFlavor.DEBUG
    if has_release:
        return RuntimeFlavor.RELEASE
    return RuntimeFlavor.NONE


def detect_binary_arch(root_dir: Path) -> str:
    patterns = (
        f"{PRODUCT_NAME}-client_*.exe",
        f"{PRODUCT_NAME}-ded_*.exe",
        "openq4/game-sp_*.dll",
        "openq4/game-mp_*.dll",
    )
    for pattern in patterns:
        for path in sorted(root_dir.glob(pattern)):
            stem = path.stem
            if "_" not in stem:
                continue
            return stem.rsplit("_", 1)[-1]
    raise RuntimeError(f"Could not determine OpenQ4 binary architecture from '{root_dir}'.")


def list_staged_runtime_files(root_dir: Path) -> list[Path]:
    files_by_name: dict[str, Path] = {}
    for pattern in WINDOWS_ROOT_RUNTIME_PATTERNS:
        for path in root_dir.glob(pattern):
            if path.is_file():
                files_by_name[path.name.lower()] = path
    return sorted(files_by_name.values(), key=lambda path: path.name.lower())


def clear_staged_runtime_files(root_dir: Path) -> None:
    for path in list_staged_runtime_files(root_dir):
        path.unlink()


def required_runtime_file_names(flavor: RuntimeFlavor) -> tuple[str, ...]:
    if flavor == RuntimeFlavor.RELEASE:
        return REQUIRED_RELEASE_RUNTIME_FILES
    if flavor == RuntimeFlavor.DEBUG:
        return REQUIRED_DEBUG_RUNTIME_FILES
    return ()


def _numeric_parts(value: str) -> tuple[int, ...]:
    parts = re.findall(r"\d+", value)
    return tuple(int(part) for part in parts)


def _path_version_key(path: Path, marker: str) -> tuple[int, tuple[int, ...], str]:
    marker_lower = marker.lower()
    parts = list(path.parts)
    version_text = ""
    for index, part in enumerate(parts):
        if part.lower() == marker_lower and index + 1 < len(parts):
            version_text = parts[index + 1]
            break
    numeric = _numeric_parts(version_text)
    return (1 if version_text[:1].isdigit() else 0, numeric, version_text)


def _discover_vs_install_root() -> Path | None:
    configured = os.environ.get("VSINSTALLDIR", "").strip()
    if configured:
        root = Path(configured)
        if root.is_dir():
            return root

    program_files_x86 = os.environ.get("ProgramFiles(x86)", "").strip()
    if not program_files_x86:
        return None

    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None

    queries = (
        ("-latest", "-prerelease", "-version", "[18.0,19.0)"),
        ("-latest", "-prerelease"),
    )
    for extra_args in queries:
        result = subprocess.run(
            [
                str(vswhere),
                *extra_args,
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        install_path = result.stdout.strip()
        if install_path:
            root = Path(install_path)
            if root.is_dir():
                return root

    return None


def _find_vc_runtime_dir(arch: str, flavor: RuntimeFlavor) -> Path:
    env_name = (
        "OPENQ4_MSVC_DEBUGCRT_DIR"
        if flavor == RuntimeFlavor.DEBUG
        else "OPENQ4_MSVC_REDIST_DIR"
    )
    configured = os.environ.get(env_name, "").strip()
    if configured:
        path = Path(configured)
        if path.is_dir():
            return path
        raise FileNotFoundError(f"{env_name} does not point to a directory: {path}")

    vs_root = _discover_vs_install_root()
    if vs_root is None:
        raise FileNotFoundError("Could not locate a Visual Studio installation with VC tools.")

    redist_root = vs_root / "VC" / "Redist" / "MSVC"
    if not redist_root.is_dir():
        raise FileNotFoundError(f"VC redist root not found: {redist_root}")

    candidates: list[Path] = []
    for path in redist_root.rglob("Microsoft.VC*"):
        if not path.is_dir():
            continue
        lower_parts = {part.lower() for part in path.parts}
        if arch.lower() not in lower_parts:
            continue
        if "onecore" in lower_parts:
            continue

        if flavor == RuntimeFlavor.DEBUG:
            if "debug_nonredist" not in lower_parts:
                continue
            if not path.name.endswith("DebugCRT"):
                continue
        else:
            if "debug_nonredist" in lower_parts:
                continue
            if not path.name.endswith(".CRT"):
                continue

        candidates.append(path)

    if not candidates:
        raise FileNotFoundError(
            f"Could not find an MSVC {flavor.value} CRT directory for arch '{arch}' under '{redist_root}'."
        )

    return max(candidates, key=lambda path: _path_version_key(path, "MSVC"))


def _find_release_ucrt_dir(arch: str) -> Path:
    configured = os.environ.get("OPENQ4_UCRT_REDIST_DIR", "").strip()
    if configured:
        path = Path(configured)
        if path.is_dir():
            return path
        raise FileNotFoundError(f"OPENQ4_UCRT_REDIST_DIR does not point to a directory: {path}")

    program_files_x86 = os.environ.get("ProgramFiles(x86)", "").strip()
    if not program_files_x86:
        raise FileNotFoundError("ProgramFiles(x86) is not set; cannot locate the Windows SDK.")

    redist_root = Path(program_files_x86) / "Windows Kits" / "10" / "Redist"
    candidates: list[Path] = []

    direct = redist_root / "ucrt" / "DLLs" / arch
    if direct.is_dir():
        candidates.append(direct)

    for path in redist_root.glob(f"*/ucrt/DLLs/{arch}"):
        if path.is_dir():
            candidates.append(path)

    if not candidates:
        raise FileNotFoundError(f"Could not locate a Windows SDK UCRT redist directory for arch '{arch}'.")

    return max(candidates, key=lambda path: _path_version_key(path, "Redist"))


def _find_debug_ucrt_file(arch: str) -> Path:
    configured = os.environ.get("OPENQ4_UCRT_DEBUG_DLL", "").strip()
    if configured:
        path = Path(configured)
        if path.is_file():
            return path
        raise FileNotFoundError(f"OPENQ4_UCRT_DEBUG_DLL does not point to a file: {path}")

    program_files_x86 = os.environ.get("ProgramFiles(x86)", "").strip()
    if not program_files_x86:
        raise FileNotFoundError("ProgramFiles(x86) is not set; cannot locate the Windows SDK.")

    bin_root = Path(program_files_x86) / "Windows Kits" / "10" / "bin"
    candidates = [
        path
        for path in bin_root.glob(f"*/{arch}/ucrt/ucrtbased.dll")
        if path.is_file()
    ]
    if not candidates:
        raise FileNotFoundError(f"Could not locate ucrtbased.dll for arch '{arch}'.")

    return max(candidates, key=lambda path: _path_version_key(path, "bin"))


def _copy_directory_dlls(source_dir: Path, target_dir: Path) -> list[Path]:
    copied: list[Path] = []
    for source_path in sorted(source_dir.glob("*.dll")):
        destination = target_dir / source_path.name
        shutil.copy2(source_path, destination)
        copied.append(destination)
    return copied


def _copy_file(source_path: Path, target_dir: Path) -> Path:
    destination = target_dir / source_path.name
    shutil.copy2(source_path, destination)
    return destination


def stage_runtime_payloads(
    source_root: Path,
    build_root: Path,
    targets: list[Path],
) -> dict[str, object]:
    build_root = build_root.resolve()
    source_root = source_root.resolve()

    binaries = collect_openq4_windows_binaries(build_root)
    if not binaries:
        return {
            "arch": None,
            "runtime_flavor": RuntimeFlavor.NONE.value,
            "targets": [str(target) for target in targets],
            "copied_files": [],
        }

    arch = detect_binary_arch(build_root)
    flavor = infer_runtime_flavor(build_root)

    copied_files: list[str] = []
    for target in targets:
        target.mkdir(parents=True, exist_ok=True)
        clear_staged_runtime_files(target)

        if arch == "x64":
            openal_dll = source_root / OPENAL_RUNTIME_RELATIVE
            if openal_dll.is_file():
                copied_files.append(str(_copy_file(openal_dll, target)))
            else:
                raise FileNotFoundError(f"Bundled OpenAL runtime not found: {openal_dll}")

        if flavor == RuntimeFlavor.RELEASE:
            vc_dir = _find_vc_runtime_dir(arch, flavor)
            copied_files.extend(str(path) for path in _copy_directory_dlls(vc_dir, target))
            ucrt_dir = _find_release_ucrt_dir(arch)
            copied_files.extend(str(path) for path in _copy_directory_dlls(ucrt_dir, target))
        elif flavor == RuntimeFlavor.DEBUG:
            vc_dir = _find_vc_runtime_dir(arch, flavor)
            copied_files.extend(str(path) for path in _copy_directory_dlls(vc_dir, target))
            ucrt_debug = _find_debug_ucrt_file(arch)
            copied_files.append(str(_copy_file(ucrt_debug, target)))

    return {
        "arch": arch,
        "runtime_flavor": flavor.value,
        "targets": [str(target) for target in targets],
        "copied_files": sorted(set(copied_files)),
    }
