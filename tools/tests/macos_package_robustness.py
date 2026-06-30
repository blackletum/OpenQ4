#!/usr/bin/env python3
"""Static checks for macOS package robustness without Finder/runtime testing."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_runtime_startup_error() -> None:
    compat = read("src/sys/osx/macosx_compat.mm")

    for token in (
        "Sys_ErrorIfMacOSAppBundlePackageRootIncomplete",
        "Sys_GetAppBundlePackageRootFromExecutableDirectory",
        "Sys_LocalizedMacOSPackageRootString",
        "localizedStringForKey",
        'table:@"OpenQ4PackageRoot"',
        "OpenQ4PackageRootMissingTitle",
        "OpenQ4PackageRootMissingBody",
        "openQ4.app adjacent package root is incomplete",
        "Keep openQ4.app, baseoq4/, openQ4-client_<arch>, and openQ4-ded_<arch> together",
        "Moving only openQ4.app to /Applications is not supported yet.",
        "Expected adjacent package-root contract: openQ4.app, loose binaries, and baseoq4/ together",
        "Package root: %s",
        "App path: %s",
        "Missing or unusable entries: %s",
        "openQ4.app",
        "BASE_GAMEDIR",
        "openQ4-client_%s",
        "openQ4-ded_%s",
        "%s/game-sp_%s.dylib",
        "%s/game-mp_%s.dylib",
        "Sys_ExecutableFileExists",
        "Sys_RequireMacOSPackageRootExecutable",
        "Sys_RequireMacOSPackageRootDirectory",
    ):
        require(compat, token, "macOS adjacent package-root startup diagnostic")


def validate_package_metadata_and_archive_guards() -> None:
    package = read("tools/build/package_nightly.py")

    for token in (
        "MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME",
        "OpenQ4PackageRoot.strings",
        "MACOS_PACKAGE_ROOT_ERROR_STRINGS",
        "English",
        "French",
        "write_macos_package_root_error_strings",
        "validate_macos_package_root_error_bytes",
        "macOS archive missing {locale} localized package-root error strings",
        'name.endswith(f".lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}")',
        "Contents/Resources/English.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
        "Contents/Resources/French.lproj/{MACOS_PACKAGE_ROOT_ERROR_STRINGS_NAME}",
    ):
        require(package, token, "macOS app localized startup-error package metadata")

    for token in (
        '".DS_Store"',
        '"__MACOSX"',
        '"._"',
        '".dSYM"',
        "validate_no_package_symlinks",
        "validate_no_macos_metadata_artifacts",
        "validate_no_macos_casefold_path_collisions",
        "validate_macos_archive_metadata_member_size",
        "macOS archive contains symlink entry",
        "macOS archive contains case-insensitive duplicate entries",
    ):
        require(package, token, "macOS archive hygiene guards")


def validate_release_path_policy() -> None:
    manual_release = read(".github/workflows/manual-release.yml")
    package_policy = read("docs/dev/macos-package-layout-and-release-policy.md")
    building = read("BUILDING.md")
    platform_support = read("docs/dev/platform-support.md")

    for token in (
        "First-class macOS releases require signed/notarized DMGs",
        "macos_support_tier",
        "first-class",
        "Experimental macOS unsigned/unnotarized tar.gz release artifacts enabled as fallback output",
    ):
        require(manual_release, token, "manual release first-class macOS DMG gate")

    for source, context in (
        (package_policy, "macOS package layout and release policy"),
        (building, "build documentation"),
        (platform_support, "platform support documentation"),
    ):
        require(source, "signed/notarized DMGs", context)
        require(source, "`-unsigned.tar.gz`", context)
        require(source, "experimental", context)


def validate_support_info_path_resolution() -> None:
    collector = read("tools/macos/collect_macos_support_info.sh")
    support_doc = read("docs/user/macos-support-data.md")

    for token in (
        "package/path-resolution.txt",
        "Package root: %s",
        "App path: %s",
        "App executable path: %s",
        "Expected loose client path: %s",
        "Expected loose dedicated-server path: %s",
        "Expected game directory path: %s",
        "Expected log keys: fs_basepath, fs_cdpath, fs_savepath",
        "grep -E 'fs_(basepath|cdpath|savepath)",
        "No openq4.log files were found. fs_basepath, fs_cdpath, and fs_savepath values could not be copied without launching openQ4.",
        "does not launch openQ4",
    ):
        require(collector, token, "macOS support collector path-resolution report")

    for token in (
        "`package/path-resolution.txt`",
        "package root, app path, expected loose runtime paths",
        "`fs_basepath`, `fs_cdpath`, and `fs_savepath`",
        "without launching openQ4",
    ):
        require(support_doc, token, "macOS support data path-resolution documentation")


def validate_docs_plan_and_release_notes() -> None:
    plan = read("docs/dev/plans/2026-06-30-apple-support-no-macos-access.md")
    package_policy = read("docs/dev/macos-package-layout-and-release-policy.md")
    release_completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.6.5.md")

    for token in (
        "- [x] Add a localized, clear startup error when adjacent runtime files are",
        "- [x] Make the error name the expected adjacent package-root contract:",
        "- [x] Add static tests for package layout docs, app metadata, and error text",
        "- [x] Keep symlink, AppleDouble, `.DS_Store`, `__MACOSX`, debug bundle, and",
        "- [x] Keep signed/notarized DMG as the only first-class release path.",
        "- [x] Keep unsigned tarballs clearly named and documented as experimental",
        "- [x] Add support-info output showing package root, app path, `fs_basepath`,",
        "Phase 5 implementation status",
        "tools/tests/macos_package_robustness.py",
        "No macOS platform testing is required or claimed for Phase 5.",
    ):
        require(plan, token, "Phase 5 macOS no-platform-test implementation plan")

    for token in (
        "openQ4.app adjacent package root is incomplete",
        "Expected adjacent package-root contract: `openQ4.app`, loose binaries, and `baseoq4/` together",
        "Moving only `openQ4.app` to `/Applications` is not supported yet",
        "`package/path-resolution.txt`",
    ):
        require(package_policy, token, "macOS package layout policy startup error and support path report")

    for source, context in (
        (release_completion, "release completion notes"),
        (release_notes, "curated release notes"),
    ):
        require(source, "localized macOS startup error", context)
        require(source, "package/path-resolution.txt", context)


def validate_ci_wiring() -> None:
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
        require(source, "macos_package_robustness.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_package_robustness.py", context)


def main() -> None:
    validate_runtime_startup_error()
    validate_package_metadata_and_archive_guards()
    validate_release_path_policy()
    validate_support_info_path_resolution()
    validate_docs_plan_and_release_notes()
    validate_ci_wiring()
    print("macos_package_robustness: ok")


if __name__ == "__main__":
    main()
