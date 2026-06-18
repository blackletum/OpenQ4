#!/usr/bin/env python3
"""Regression checks for the macOS Metal bridge build contract."""

import importlib.util
import io
import os
import plistlib
import shutil
import tarfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def expect_runtime_error(fragment: str, callback, context: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if fragment not in str(exc):
            raise AssertionError(f"Unexpected error for {context}: {exc}") from exc
        return
    raise AssertionError(f"Expected RuntimeError containing {fragment!r} for {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def load_package_module():
    package_path = ROOT / "tools" / "build" / "package_nightly.py"
    spec = importlib.util.spec_from_file_location("package_nightly_for_macos_test", package_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import package helper: {package_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_validation_module():
    validation_path = ROOT / "tools" / "validation" / "openq4_validate.py"
    spec = importlib.util.spec_from_file_location("openq4_validate_for_macos_test", validation_path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"Unable to import validation helper: {validation_path}")

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_macos_plist_bytes(package, version: str) -> bytes:
    plist = dict(package.MACOS_EXPECTED_PLIST_VALUES)
    plist.update(
        {
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "NSHighResolutionCapable": True,
            "NSSupportsAutomaticGraphicsSwitching": True,
        }
    )
    return plistlib.dumps(plist)


def make_macos_archive_entries(
    package,
    package_name: str,
    arch: str,
    plist_bytes: bytes,
    *,
    client_mode: int = 0o755,
    dedicated_mode: int = 0o755,
    app_exec_mode: int = 0o755,
    extra_entries: dict[str, tuple[bytes, int]] | None = None,
) -> dict[str, tuple[bytes, int]]:
    prefix = package_name + "/"
    client_bytes = b"client-binary\n"
    entries = {
        f"{prefix}openQ4-client_{arch}": (client_bytes, client_mode),
        f"{prefix}openQ4-ded_{arch}": (b"dedicated-binary\n", dedicated_mode),
        f"{prefix}{package.GAME_DIR_NAME}/mod.json": (b'{"version":"0.2.000"}\n', 0o644),
        f"{prefix}{package.GAME_DIR_NAME}/pak0.pk4": (b"pk4\n", 0o644),
        f"{prefix}openQ4.app/Contents/Info.plist": (plist_bytes, 0o644),
        f"{prefix}openQ4.app/Contents/MacOS/openQ4": (client_bytes, app_exec_mode),
        f"{prefix}openQ4.app/Contents/Resources/openQ4.icns": (b"icns\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/VERSION.txt": (b"openQ4\n", 0o644),
        f"{prefix}openQ4.app/Contents/Resources/English.lproj/InfoPlist.strings": (
            b'CFBundleName = "openQ4";\n',
            0o644,
        ),
        f"{prefix}openQ4.app/Contents/Resources/French.lproj/InfoPlist.strings": (
            b'CFBundleName = "openQ4";\n',
            0o644,
        ),
    }
    if extra_entries:
        entries.update(extra_entries)
    return entries


def write_test_targz_archive(archive_path: Path, entries: dict[str, tuple[bytes, int]]) -> None:
    with tarfile.open(archive_path, "w:gz") as archive:
        for name, (data, mode) in entries.items():
            member = tarfile.TarInfo(name)
            member.mode = mode
            member.size = len(data)
            member.mtime = 0
            archive.addfile(member, io.BytesIO(data))


def write_test_zip_archive(archive_path: Path, entries: dict[str, tuple[bytes, int]]) -> None:
    with ZipFile(archive_path, "w", compression=ZIP_DEFLATED) as archive:
        for name, (data, mode) in entries.items():
            info = ZipInfo(name)
            info.create_system = 3
            info.compress_type = ZIP_DEFLATED
            info.external_attr = (mode & 0o777) << 16
            archive.writestr(info, data)


def write_test_file(path: Path, data: bytes, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    os.chmod(path, mode)


def validate_macos_app_bundle_validator_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-app-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    app_root = package_root / "openQ4.app"
    app_contents = app_root / "Contents"
    arch = "arm64"
    version = "0.2.000"

    shutil.rmtree(work, ignore_errors=True)
    try:
        client_bytes = b"client-binary\n"
        write_test_file(package_root / f"openQ4-client_{arch}", client_bytes, 0o755)
        (package_root / package.GAME_DIR_NAME).mkdir(parents=True, exist_ok=True)
        write_test_file(app_contents / "Info.plist", make_macos_plist_bytes(package, version))
        write_test_file(app_contents / "MacOS" / "openQ4", client_bytes, 0o755)
        write_test_file(app_contents / "Resources" / "openQ4.icns", b"icns\n")
        write_test_file(app_contents / "Resources" / "VERSION.txt", b"openQ4\n")

        package.validate_macos_app_bundle(package_root, app_root, arch, version)

        write_test_file(app_contents / "MacOS" / "openQ4", b"other-binary\n", 0o755)
        expect_runtime_error(
            "macOS app executable does not match packaged client binary",
            lambda: package.validate_macos_app_bundle(package_root, app_root, arch, version),
            "macOS app executable drift from packaged client",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_macos_archive_validator_runtime() -> None:
    package = load_package_module()
    work = ROOT / ".tmp" / "macos-archive-contract"
    package_root = work / "openq4-v0.2.000-macos-arm64-opengl"
    arch = "arm64"
    version = "0.2.000"
    plist_bytes = make_macos_plist_bytes(package, version)

    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)
    try:
        entries = make_macos_archive_entries(package, package_root.name, arch, plist_bytes)
        for archive_format, archive_name, writer in (
            ("tar.gz", "good.tar.gz", write_test_targz_archive),
            ("zip", "good.zip", write_test_zip_archive),
        ):
            archive_path = work / archive_name
            writer(archive_path, entries)
            package.validate_macos_archive_contents(
                package_root,
                archive_path,
                archive_format,
                arch,
                version,
            )

        bad_exec_archive = work / "bad-exec.tar.gz"
        write_test_targz_archive(
            bad_exec_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                client_mode=0o644,
            ),
        )
        expect_runtime_error(
            "macOS archive entry is not executable",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_exec_archive,
                "tar.gz",
                arch,
                version,
            ),
            "non-executable macOS archive client",
        )

        bad_plist_archive = work / "bad-plist-version.tar.gz"
        write_test_targz_archive(
            bad_plist_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                make_macos_plist_bytes(package, "0.1.000"),
            ),
        )
        expect_runtime_error(
            "CFBundleShortVersionString",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_plist_archive,
                "tar.gz",
                arch,
                version,
            ),
            "mismatched macOS archive plist version",
        )

        bad_app_exec_archive = work / "bad-app-exec.tar.gz"
        write_test_targz_archive(
            bad_app_exec_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={
                    f"{package_root.name}/openQ4.app/Contents/MacOS/openQ4": (
                        b"other-binary\n",
                        0o755,
                    )
                },
            ),
        )
        expect_runtime_error(
            "macOS archive app executable does not match packaged client binary",
            lambda: package.validate_macos_archive_contents(
                package_root,
                bad_app_exec_archive,
                "tar.gz",
                arch,
                version,
            ),
            "mismatched macOS archive app executable",
        )

        unsafe_archive = work / "unsafe.tar.gz"
        write_test_targz_archive(
            unsafe_archive,
            make_macos_archive_entries(
                package,
                package_root.name,
                arch,
                plist_bytes,
                extra_entries={"other-package/escaped": (b"bad\n", 0o644)},
            ),
        )
        expect_runtime_error(
            "macOS archive contains unsafe or out-of-package paths",
            lambda: package.validate_macos_archive_contents(
                package_root,
                unsafe_archive,
                "tar.gz",
                arch,
                version,
            ),
            "out-of-package macOS archive entry",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_legacy_macos_plist_runtime() -> None:
    package = load_package_module()
    info_plist_path = ROOT / "src" / "sys" / "osx" / "Info.plist"
    version_plist_path = ROOT / "src" / "sys" / "osx" / "version.plist"

    with info_plist_path.open("rb") as handle:
        info_plist = plistlib.load(handle)
    with version_plist_path.open("rb") as handle:
        version_plist = plistlib.load(handle)

    version = version_plist["CFBundleVersion"]
    if version_plist.get("CFBundleShortVersionString") != version:
        raise AssertionError("legacy macOS version.plist short version must match CFBundleVersion")

    package.validate_macos_plist_values(info_plist, "legacy macOS Info.plist", version)


def validate_macos_staged_payload_validator_runtime() -> None:
    validator = load_validation_module()
    work = ROOT / ".tmp" / "macos-staged-contract"
    install_root = work / ".install"
    game_dir = install_root / "baseoq4"
    arch = "arm64"

    shutil.rmtree(work, ignore_errors=True)
    try:
        client = install_root / f"openQ4-client_{arch}"
        dedicated = install_root / f"openQ4-ded_{arch}"
        write_test_file(install_root / "openQ4.icns", b"icns\n")
        write_test_file(install_root / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp", b"bmp\n")
        write_test_file(client, b"client\n", 0o755)
        write_test_file(dedicated, b"ded\n", 0o755)
        write_test_file(game_dir / f"game-sp_{arch}.dylib", b"sp\n")
        write_test_file(game_dir / f"game-mp_{arch}.dylib", b"mp\n")

        validator.validate_macos_staged_metadata(
            ROOT,
            install_root,
            game_dir,
            [client],
            [dedicated],
        )

        bad_module = game_dir / f"game-sp_{arch}.so"
        write_test_file(bad_module, b"wrong\n")
        expect_runtime_error(
            "non-dylib game modules",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload with Linux game module",
        )
        bad_module.unlink()

        (game_dir / f"game-mp_{arch}.dylib").unlink()
        expect_runtime_error(
            "architecture-matched game modules",
            lambda: validator.validate_macos_staged_metadata(
                ROOT,
                install_root,
                game_dir,
                [client],
                [dedicated],
            ),
            "macOS staged payload missing matched MP module",
        )
    finally:
        shutil.rmtree(work, ignore_errors=True)


def validate_meson_contract() -> None:
    options = read("meson_options.txt")
    meson = read("meson.build")
    baseoq4_meson = read("content/baseoq4/meson.build")
    setup_sh = read("tools/build/meson_setup.sh")
    setup_ps1 = read("tools/build/meson_setup.ps1")

    require(options, "'macos_graphics_bridge'", "Meson options")
    require(options, "choices: ['opengl', 'metal']", "Meson options")
    require(options, "Metal-ready SDL3/Cocoa integration surface", "Meson option description")

    require(meson, "macos_graphics_bridge = get_option('macos_graphics_bridge')", "Meson bridge option")
    require(meson, "macos_graphics_bridge != 'opengl'", "non-macOS bridge guard")
    require(meson, "macos_graphics_bridge == 'metal' and platform_backend_requested != 'sdl3'", "SDL3 bridge guard")
    require(meson, "use_macos_metal_bridge", "Metal bridge build predicate")
    require(meson, "modules: ['Metal', 'QuartzCore']", "Metal bridge framework dependency")
    require(meson, "-DOPENQ4_MACOS_METAL_BRIDGE=1", "Metal bridge compile define")
    require(meson, "'macOS graphics bridge': macos_graphics_bridge", "Meson summary")

    require(baseoq4_meson, "elif host_system == 'darwin'", "macOS game module source branch")
    require(baseoq4_meson, "name_suffix: 'dylib'", "macOS game module dylib suffix")

    require(setup_sh, "macos_graphics_bridge", "Bash Meson wrapper option preservation")
    require(setup_ps1, '"macos_graphics_bridge"', "PowerShell Meson wrapper option preservation")


def validate_sdl3_runtime_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    summary = function_body(source, "static void SDL3_PrintGraphicsBridgeSummary(void) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")

    require(source, "OPENQ4_MACOS_METAL_BRIDGE", "SDL3 Metal bridge compile guard")
    require(source, "SDL3_IsMacOSMetalBridge", "SDL3 Metal bridge predicate")
    require(source, "macOS Metal bridge (SDL3/Cocoa host, OpenGL renderer compatibility path)", "SDL3 bridge description")

    require(hints, "SDL_HINT_VIDEO_DRIVER", "macOS Metal bridge SDL video driver hint")
    require(hints, '"cocoa"', "macOS Metal bridge SDL video driver hint")
    require(hints, "SDL_HINT_RENDER_DRIVER", "macOS Metal bridge render hint")
    require(hints, "SDL_HINT_GPU_DRIVER", "macOS Metal bridge GPU hint")
    require(hints, "SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "macOS Metal bridge drawable hint")

    require(summary, "no native Metal renderer rewrite is selected", "SDL3 Metal bridge log")
    require(summary, "SDL_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "SDL3 Metal bridge hint log")
    require(init, "SDL3_PrintGraphicsBridgeSummary();", "SDL3 GL initialization")


def validate_packaging_and_release_contract() -> None:
    package = read("tools/build/package_nightly.py")
    plist = read("src/sys/osx/Info.plist")
    release = read(".github/workflows/manual-release.yml")
    compat = read("src/sys/osx/macosx_compat.mm")
    main = read("src/sys/osx/macosx_sdl3_main.cpp")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    require(package, "--package-suffix", "release packaging variant suffix")
    require(package, "normalize_package_suffix", "release packaging variant suffix")
    require(package, "import filecmp", "macOS app executable comparison")
    require(package, "import subprocess", "macOS binary dependency validation")
    require(package, "filecmp.cmp(client_binary, app_executable, shallow=False)", "macOS app executable comparison")
    require(package, "shutil.copy2(client_binary, app_executable)", "macOS app executable creation")
    require(package, "macOS app executable does not match packaged client binary", "macOS app executable validation")
    require(package, "get_package_executable_archive_paths", "POSIX archive executable mode preservation")
    require(package, "ZipInfo.from_file", "POSIX archive executable mode preservation")
    require(package, "info.mode = 0o755", "POSIX archive executable mode preservation")
    require(package, "MACOS_ALLOWED_RUNTIME_DEPENDENCY_PREFIXES", "macOS binary dependency validation")
    require(package, "macos_otool_dependencies", "macOS binary dependency validation")
    require(package, "validate_macos_binary_dependencies", "macOS binary dependency validation")
    require(package, "otool_path, \"-L\"", "macOS binary dependency validation")
    require(package, "macOS binary has unbundled non-system dependencies", "macOS binary dependency validation")
    require(package, "MACOS_EXPECTED_PLIST_VALUES", "macOS package Info.plist validation")
    require(package, "validate_macos_plist_values", "macOS package Info.plist validation")
    require(package, "validate_macos_app_bundle", "macOS package app validation")
    require(package, "validate_macos_archive_contents", "macOS package archive validation")
    require(package, '"CFBundleIconFile": "openQ4.icns"', "macOS package Info.plist validation")
    require(package, '"CFBundleName": "openQ4"', "macOS package Info.plist validation")
    require(package, '("CFBundleShortVersionString", "CFBundleVersion")', "macOS package Info.plist validation")
    require(package, "openQ4-ded_{arch}", "macOS package archive validation")
    require(package, "English.lproj/InfoPlist.strings", "macOS package archive validation")
    require(package, "French.lproj/InfoPlist.strings", "macOS package archive validation")
    require(package, "macOS archive entry is not executable", "macOS package archive validation")
    require(package, "macOS archive app executable does not match packaged client binary", "macOS package archive validation")
    require(package, "macOS archive contains unsafe or out-of-package paths", "macOS package archive validation")
    require(package, "macOS archive Info.plist", "macOS package archive validation")
    require(package, "plistlib.loads", "macOS package Info.plist validation")
    require(package, "NSSupportsAutomaticGraphicsSwitching", "macOS package Info.plist validation")
    require(package, '"LSMinimumSystemVersion": "11.0"', "macOS package compatibility floor")
    require(package, '"NSPrincipalClass": "NSApplication"', "macOS package Cocoa app metadata")

    require(compat, "_NSGetExecutablePath", "macOS executable path resolution")
    require(compat, "Sys_CopyPathIfFits", "macOS executable path truncation guard")
    require(compat, "Sys_CopyExecutablePath", "macOS executable path resolution")
    require(compat, "malloc( bufferSize )", "macOS executable path long-buffer handling")
    require(compat, "realpath( pathBuffer, resolvedPath )", "macOS executable path canonicalization")
    require(compat, "Sys_DirectoryContainsGameDir", "macOS base path validation")
    require(compat, "BASE_GAMEDIR", "macOS base path validation")
    require(compat, "Sys_UseAppBundleParentBasePathCandidate", "macOS app bundle base path validation")
    require(compat, "\"/Contents/MacOS\"", "macOS app bundle base path validation")
    require(compat, "\"app parent\"", "macOS app bundle base path validation")
    require(main, "SDL_MAIN_HANDLED", "macOS SDL3 launch initialization")
    require(main, "static int SDLCALL OpenQ4_Main", "macOS SDL3 launch initialization")
    require(main, "SDL_RunApp(argc, argv, OpenQ4_Main, NULL)", "macOS SDL3 launch initialization")
    require(validator, "host_is_macos", "macOS staged payload validation")
    require(validator, "macos_binary_arch", "macOS staged payload validation")
    require(validator, "validate_macos_staged_metadata", "macOS staged payload validation")
    require(validator, "openQ4.icns", "macOS staged payload validation")
    require(validator, "non-dylib game modules", "macOS staged payload validation")
    require(validator, "architecture-matched game modules", "macOS staged payload validation")
    require(commit, "macOS ARM64 ${{ matrix.bridge_label }} Commit Validation", "commit validation macOS job")
    require(commit, "macos_graphics_bridge: opengl", "commit validation macOS OpenGL job")
    require(commit, "macos_graphics_bridge: metal", "commit validation macOS Metal job")
    require(commit, "--extra-setup-arg=-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "commit validation macOS bridge setup")
    require(commit, "runs-on: macos-15", "commit validation macOS job")
    require(commit, "bash tools/validation/validate_pr.sh \\", "commit validation macOS job")
    require(commit, "--fail-on-dirty \\", "commit validation macOS job")
    require(push, "macOS OpenGL Push Verification", "push verification macOS OpenGL job")
    require(push, "macOS Metal Push Verification", "push verification macOS Metal job")
    require(push, "macos-opengl", "push verification macOS OpenGL artifact")
    require(push, "macos-metal", "push verification macOS Metal artifact")
    require(push, "--extra-setup-arg=-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "push verification macOS bridge setup")

    for source, context in ((package, "macOS package Info.plist"), (plist, "legacy macOS Info.plist")):
        require(source, "CFBundleName", context)
        require(source, "NSHighResolutionCapable", context)
        require(source, "NSPrincipalClass", context)
        require(source, "NSSupportsAutomaticGraphicsSwitching", context)
        require(source, "LSMinimumSystemVersion", context)
        require(source, "11.0", context)

    require(plist, "<string>openQ4.icns</string>", "legacy macOS Info.plist icon")
    require(plist, "CFBundleShortVersionString", "legacy macOS Info.plist version")

    require(release, "label: macOS ARM64 OpenGL", "manual release macOS OpenGL matrix")
    require(release, "macos_graphics_bridge: opengl", "manual release OpenGL bridge matrix")
    require(release, 'package_suffix: "-opengl"', "manual release OpenGL package suffix")
    require(release, "label: macOS ARM64 Metal", "manual release macOS Metal matrix")
    require(release, "macos_graphics_bridge: metal", "manual release macOS matrix")
    require(release, 'package_suffix: "-metal"', "manual release Metal package suffix")
    require(release, "-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "manual release setup")
    require(release, "--package-suffix=\"${{ matrix.package_suffix }}\"", "manual release packaging")
    require(release, 'cmp -s "${app_exec}" "${client_binary}"', "manual release macOS app validation")
    require(release, "macOS app executable does not match the packaged client binary", "manual release macOS app validation")
    require(release, "Missing or non-executable macOS dedicated binary", "manual release macOS app validation")
    require(release, "sp_module=", "manual release macOS dependency validation")
    require(release, "check_macos_binary_dependencies", "manual release macOS dependency validation")
    require(release, "otool -L", "manual release macOS dependency validation")
    require(release, "unbundled non-system dependency", "manual release macOS dependency validation")
    require(release, "Missing macOS staged payload file", "manual release macOS staged validation")
    require(release, ".install/baseoq4/game-sp_${{ matrix.binary_arch }}.dylib", "manual release macOS staged validation")
    require(release, ".install/baseoq4/game-mp_${{ matrix.binary_arch }}.dylib", "manual release macOS staged validation")
    require(release, "macOS staged payload contains non-dylib game modules", "manual release macOS staged validation")
    require(release, "check_plist_value CFBundleIconFile openQ4.icns", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleName openQ4", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleShortVersionString", "manual release macOS app validation")
    require(release, "check_plist_value CFBundleVersion", "manual release macOS app validation")
    require(release, "check_plist_value LSMinimumSystemVersion 11.0", "manual release macOS app validation")
    require(release, "check_plist_value NSPrincipalClass NSApplication", "manual release macOS app validation")
    require(release, "check_plist_value NSSupportsAutomaticGraphicsSwitching true", "manual release macOS app validation")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl.tar.gz", "manual release expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal.tar.gz", "manual release expected assets")


def validate_macos_shell_entrypoints() -> None:
    for relative_path in (
        "tools/build/meson_setup.sh",
        "tools/validation/validate_pr.sh",
        "tools/validation/validate_push.sh",
    ):
        source = read(relative_path)
        reject(source, "dirname --", relative_path)
        require(source, 'case "${BASH_SOURCE[0]}" in', relative_path)
        require(source, 'script_dir="$(CDPATH= cd "${script_dir}" && pwd)"', relative_path)


def validate_docs_and_ci_hooks() -> None:
    building = read("BUILDING.md")
    platform_support = read("docs-dev/platform-support.md")
    migration = read("docs-dev/sdl3-linux-macos-migration.md")
    getting_started = read("docs-user/getting-started.md")
    package_readme = read("assets/release/README.html")
    release_notes = read("docs-dev/release-completion.md")
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require(building, "-Dmacos_graphics_bridge=metal", "build documentation")
    require(building, "without a native renderer rewrite", "build documentation")
    require(getting_started, "separate OpenGL and Metal bridge variants", "getting started guide")
    require(getting_started, "xattr -dr com.apple.quarantine", "getting started guide")
    require(package_readme, "separate OpenGL and Metal bridge variants", "release package README")
    require(package_readme, "xattr -dr com.apple.quarantine", "release package README")
    require(release_notes, "retain the OpenGL package while adding a separate Metal bridge package", "release completion notes")
    require(release_notes, "Final macOS release archive", "release completion notes")
    require(platform_support, "Linux and macOS now use the shared SDL3 runtime path", "platform support roadmap")
    require(platform_support, "macOS SDL3 builds select `src/sys/osx/macosx_sdl3.cpp`", "platform support roadmap")
    require(migration, "macOS CI covers OpenGL and Metal bridge configure/build/install/package validation", "SDL3 migration plan")

    require(validator, "macos_metal_bridge.py", "validation runner")
    require(push, "tools/tests/macos_metal_bridge.py", "push verification workflow")
    require(commit, "tools/tests/macos_metal_bridge.py", "commit validation workflow")


def main() -> None:
    validate_meson_contract()
    validate_sdl3_runtime_contract()
    validate_packaging_and_release_contract()
    validate_macos_shell_entrypoints()
    validate_macos_app_bundle_validator_runtime()
    validate_macos_archive_validator_runtime()
    validate_legacy_macos_plist_runtime()
    validate_macos_staged_payload_validator_runtime()
    validate_docs_and_ci_hooks()
    print("macos_metal_bridge: ok")


if __name__ == "__main__":
    main()
