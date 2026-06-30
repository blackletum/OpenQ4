#!/usr/bin/env python3
"""Static and fixture checks for macOS symbolication packaging policy."""

from __future__ import annotations

import importlib.util
import os
import shutil
import sys
import tarfile
import uuid
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
WORK_BASE = ROOT / ".tmp" / "macos-symbolication-policy-test"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def load_module(name: str, path: Path) -> ModuleType:
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


PACKAGE = load_module("package_nightly_symbolication_under_test", ROOT / "tools" / "build" / "package_nightly.py")


def make_work_root() -> Path:
    return WORK_BASE / f"{os.getpid()}-{uuid.uuid4().hex}"


def add_tar_bytes(archive: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size = len(data)
    info.mode = 0o644
    info.uid = 0
    info.gid = 0
    info.uname = ""
    info.gname = ""
    info.mtime = 0
    archive.addfile(info, fileobj=__import__("io").BytesIO(data))


def symbol_manifest_text() -> str:
    return "\n".join(
        [
            "openQ4 macOS symbols",
            "format=1",
            "version=0.6.5",
            "version_tag=0.6.5",
            "platform=macos",
            "arch=arm64",
            "package_suffix=-opengl",
            "runtime_archive=openq4-0.6.5-macos-arm64-opengl.dmg",
            "symbol_archive=openq4-0.6.5-macos-arm64-opengl-symbols.tar.xz",
            "",
            "binaries:",
            "- path=openQ4.app/Contents/MacOS/openQ4",
            "  sha256=" + "0" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000001 (arm64) openQ4",
            "  dsym=dSYMs/openQ4.app.dSYM",
            "- path=openQ4-client_arm64",
            "  sha256=" + "1" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000002 (arm64) openQ4-client_arm64",
            "  dsym=dSYMs/openQ4-client_arm64.dSYM",
            "- path=openQ4-ded_arm64",
            "  sha256=" + "2" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000003 (arm64) openQ4-ded_arm64",
            "  dsym=dSYMs/openQ4-ded_arm64.dSYM",
            "- path=baseoq4/game-sp_arm64.dylib",
            "  sha256=" + "3" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000004 (arm64) game-sp_arm64.dylib",
            "  dsym=dSYMs/game-sp_arm64.dylib.dSYM",
            "- path=baseoq4/game-mp_arm64.dylib",
            "  sha256=" + "4" * 64,
            "  size=1",
            "  macho_uuid=UUID: 00000000-0000-0000-0000-000000000005 (arm64) game-mp_arm64.dylib",
            "  dsym=dSYMs/game-mp_arm64.dylib.dSYM",
            "",
        ]
    )


def create_symbol_archive(path: Path, *, leak_runtime: bool = False) -> str:
    root_name = "openq4-0.6.5-macos-arm64-opengl-symbols"
    entries = {
        f"{root_name}/SYMBOLS.txt": symbol_manifest_text().encode("utf-8"),
        f"{root_name}/dSYMs/openQ4.app.dSYM/Contents/Resources/DWARF/openQ4": b"dsym\n",
        f"{root_name}/dSYMs/openQ4-client_arm64.dSYM/Contents/Resources/DWARF/openQ4-client_arm64": b"dsym\n",
        f"{root_name}/dSYMs/openQ4-ded_arm64.dSYM/Contents/Resources/DWARF/openQ4-ded_arm64": b"dsym\n",
        f"{root_name}/dSYMs/game-sp_arm64.dylib.dSYM/Contents/Resources/DWARF/game-sp_arm64.dylib": b"dsym\n",
        f"{root_name}/dSYMs/game-mp_arm64.dylib.dSYM/Contents/Resources/DWARF/game-mp_arm64.dylib": b"dsym\n",
    }
    if leak_runtime:
        entries[f"{root_name}/openQ4-client_arm64"] = b"runtime\n"

    with tarfile.open(path, "w:xz") as archive:
        for name, data in sorted(entries.items()):
            add_tar_bytes(archive, name, data)
    return root_name


def validate_packager_contract() -> None:
    packaging = read("tools/build/package_nightly.py")

    for token in (
        'MACOS_SYMBOL_MANIFEST_NAME = "SYMBOLS.txt"',
        'MACOS_SYMBOL_ARCHIVE_SUFFIX = ".tar.xz"',
        "def macos_symbol_archive_stem(",
        "def macos_symbol_targets(package_root: Path, arch: str)",
        'Path("openQ4.app") / "Contents" / "MacOS" / "openQ4"',
        f'Path(f"{{PRODUCT_NAME}}-client_{{arch}}")',
        f'Path(f"{{PRODUCT_NAME}}-ded_{{arch}}")',
        'Path(GAME_DIR_NAME) / f"game-sp_{arch}.dylib"',
        'Path(GAME_DIR_NAME) / f"game-mp_{arch}.dylib"',
        "dwarfdump",
        "dsymutil",
        "create_macos_dsym_bundle",
        "write_macos_symbol_manifest",
        "validate_macos_symbol_archive_contents",
        "forbidden_runtime_entries",
        "macos_symbol_archive_path = create_macos_symbol_archive",
        "runtime_archive_name=archive_path.name",
        f"f\"{{package_prefix}}{{MACOS_SYMBOL_MANIFEST_NAME}}\"",
        "validate_no_macos_metadata_artifacts(package_root)",
        '".dSYM",',
    ):
        require(packaging, token, "macOS symbolication packaging contract")


def validate_symbol_archive_fixture() -> None:
    work = make_work_root()
    shutil.rmtree(work, ignore_errors=True)
    try:
        work.mkdir(parents=True, exist_ok=True)
        good_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols.tar.xz"
        root_name = create_symbol_archive(good_archive)
        PACKAGE.validate_macos_symbol_archive_contents(
            good_archive,
            root_name,
            version="0.6.5",
            version_tag="0.6.5",
            arch="arm64",
            package_suffix="-opengl",
            runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
        )

        bad_archive = work / "openq4-0.6.5-macos-arm64-opengl-symbols-bad.tar.xz"
        bad_root_name = create_symbol_archive(bad_archive, leak_runtime=True)
        try:
            PACKAGE.validate_macos_symbol_archive_contents(
                bad_archive,
                bad_root_name,
                version="0.6.5",
                version_tag="0.6.5",
                arch="arm64",
                package_suffix="-opengl",
                runtime_archive_name="openq4-0.6.5-macos-arm64-opengl.dmg",
            )
        except RuntimeError as exc:
            if "runtime payload entries" not in str(exc):
                raise AssertionError(f"unexpected symbol archive rejection: {exc}") from exc
        else:
            raise AssertionError("symbol archive validator accepted leaked runtime binaries")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_release_workflow() -> None:
    workflow = read(".github/workflows/manual-release.yml")

    for token in (
        "Runtime package archive name must not look like a symbol/debug artifact",
        "Expected macOS dSYM symbol archive not found",
        "symbols_archive_path=",
        "Missing macOS symbolication manifest",
        "macOS runtime package directory must not contain .dSYM bundles",
        "Upload macOS dSYM symbols",
        "steps.package.outputs.symbols_archive_path",
        "openq4-release-${{ needs.metadata.outputs.version_tag }}-macos-${{ matrix.binary_arch }}${{ matrix.package_suffix }}-symbols",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl-symbols.tar.xz",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal-symbols.tar.xz",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl-unsigned-symbols.tar.xz",
        "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal-unsigned-symbols.tar.xz",
    ):
        require(workflow, token, "manual release macOS symbol workflow")


def validate_docs_and_support_collector() -> None:
    symbol_doc = read("docs/dev/macos-symbolication.md")
    support_doc = read("docs/user/macos-support-data.md")
    package_policy = read("docs/dev/macos-package-layout-and-release-policy.md")
    support_collector = read("tools/macos/collect_macos_support_info.sh")
    evidence = read("docs/dev/macos-signoff-evidence.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")

    for token in (
        "# macOS Symbolication Workflow",
        "`openq4-<version>-macos-arm64-opengl-symbols.tar.xz`",
        "`openq4-<version>-macos-arm64-metal-symbols.tar.xz`",
        "`openQ4.app/Contents/MacOS/openQ4`",
        "`openQ4-client_arm64`",
        "`openQ4-ded_arm64`",
        "`baseoq4/game-sp_arm64.dylib`",
        "`baseoq4/game-mp_arm64.dylib`",
        "`SYMBOLS.txt`",
        "SHA-256",
        "`dwarfdump --uuid`",
        "support collector copies it into `package/SYMBOLS.txt`",
        "without requiring any macOS platform test",
    ):
        require(symbol_doc, token, "macOS symbolication documentation")

    for source, context in (
        (support_doc, "macOS support-data guide"),
        (package_policy, "macOS package policy"),
        (evidence, "macOS signoff evidence"),
        (release_completion, "release completion"),
        (release_notes, "curated release notes"),
    ):
        require(source, "SYMBOLS.txt", context)
        require(source, "dSYM", context)

    for token in (
        '"SYMBOLS.txt"',
        'copy_text_if_present "${PACKAGE_ROOT}/SYMBOLS.txt" "package/SYMBOLS.txt"',
        "package/SYMBOLS.txt",
    ):
        require(support_collector, token, "macOS support collector symbol manifest")


def validate_plan_status() -> None:
    plan = read("docs/dev/plans/2026-06-30-apple-support-no-macos-access.md")

    for token in (
        "## Phase 4: Make Symbolication Possible",
        "- [x] Add a macOS release/debug symbol artifact that is separate from the",
        "- [x] Keep `.dSYM` bundles out of runtime DMG/tarball packages.",
        "- [x] Ensure symbol artifacts cover:",
        "`openQ4.app/Contents/MacOS/openQ4`",
        "loose `openQ4-client_arm64`",
        "loose `openQ4-ded_arm64`",
        "`baseoq4/game-sp_arm64.dylib`",
        "`baseoq4/game-mp_arm64.dylib`",
        "- [x] Document how to pair a user `.ips` report with the correct symbol archive.",
        "- [x] Add package-script tests that symbol archives cannot be mistaken for",
        "Phase 4 implementation status",
        "tools/tests/macos_symbolication_policy.py",
    ):
        require(plan, token, "Phase 4 Apple support plan")


def validate_ci_and_local_wiring() -> None:
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")
    macos_debug = read(".github/workflows/macos-debug.yml")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "macos_symbolication_policy.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_symbolication_policy.py", context)


def validate_discord_downloads_do_not_promote_symbols() -> None:
    announcer = read(".github/scripts/announce-release-discord.mjs")
    reject(announcer, "symbols.tar.xz\", \"macOS", "Discord download links should not promote debug symbols as runtime downloads")


def main() -> None:
    validate_packager_contract()
    validate_symbol_archive_fixture()
    validate_release_workflow()
    validate_docs_and_support_collector()
    validate_plan_status()
    validate_ci_and_local_wiring()
    validate_discord_downloads_do_not_promote_symbols()
    print("macos_symbolication_policy: ok")


if __name__ == "__main__":
    main()
