#!/usr/bin/env python3
"""Regression checks for macOS crash-report intake and support artifact collection."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_PATH = "docs/dev/plans/2026-06-30-apple-support-no-macos-access.md"


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_issue_template() -> None:
    template = read(".github/ISSUE_TEMPLATE/macos-crash-report.yml")

    for token in (
        "Experimental macOS crash report",
        "issue #73",
        "macOS support is experimental",
        "full terminal output as text, not only a screenshot",
        "openQ4 version",
        "Package artifact name",
        "OpenGL",
        "Metal bridge",
        "openQ4.app",
        "openQ4-client_arm64 from Terminal",
        "macOS version and build",
        "Hardware model",
        "Full terminal output",
        "Saved openq4.log",
        "~/Library/Application Support/openQ4/baseoq4/logs/openq4.log",
        "macOS crash report",
        "~/Library/Logs/DiagnosticReports",
        "I kept `openQ4.app`, `baseoq4/`, the loose client/dedicated binaries, and runtime support files together.",
        "collect_macos_support_info.sh",
        "openq4-macos-support-*.tar.gz",
    ):
        require(template, token, "macOS crash issue template")


def validate_support_collector() -> None:
    script = read("tools/macos/collect_macos_support_info.sh")

    for token in (
        "#!/bin/sh",
        "OPENQ4_PACKAGE_ROOT",
        "openq4-macos-support-${STAMP}",
        "redact_text()",
        "/Users/[^/[:space:]]+",
        "<email>",
        "sw_vers",
        "uname -a",
        "system_profiler SPHardwareDataType",
        "system_profiler SPDisplaysDataType",
        "xcodebuild -version",
        "xcrun --sdk macosx --show-sdk-version",
        "openQ4.app",
        "baseoq4",
        "openQ4-client_arm64",
        "collect_macos_support_info.sh",
        "openq4.log",
        "${HOME}/Library/Application Support/openQ4/baseoq4/logs/openq4.log",
        "~/Library/Logs/DiagnosticReports",
        "-name 'openQ4*.ips'",
        "-name 'openQ4*.crash'",
        "tar -czf",
        "does not dump the environment",
        "does not launch openQ4",
        "does not copy retail q4base PK4 assets",
    ):
        require(script, token, "macOS support collector")

    for token in (
        "printenv",
        "env >",
        "set >",
        "openQ4-client_arm64 >",
        "openQ4-client_arm64 2>",
    ):
        reject(script, token, "macOS support collector privacy/no-launch guard")


def validate_user_docs() -> None:
    support_doc = read("docs/user/macos-support-data.md")
    readme = read("README.md")
    getting_started = read("docs/user/getting-started.md")
    package_readme = read("assets/release/README.html")
    release_notes = read("docs/dev/releases/v0.6.5.md")

    for token in (
        "# Experimental macOS Support Data",
        "GitHub issue #73",
        "Full terminal output as text, not only a screenshot",
        "~/Library/Application Support/openQ4/baseoq4/logs/openq4.log",
        "~/Library/Logs/DiagnosticReports",
        "collect_macos_support_info.sh",
        "openq4-macos-support-YYYYMMDD-HHMMSSZ.tar.gz",
        "does not dump the environment",
        "does not launch openQ4",
        "does not copy retail `q4base` PK4 assets",
        "Moving only `openQ4.app` to `/Applications` is not supported yet.",
    ):
        require(support_doc, token, "macOS support-data guide")

    require(readme, "docs/user/macos-support-data.md", "README macOS support-data link")
    require(getting_started, "macos-support-data.md", "getting started macOS support-data link")
    require(package_readme, "docs/docs/user/macos-support-data.html", "packaged README macOS support-data link")
    require(release_notes, "collect_macos_support_info.sh", "curated release notes macOS collector request")
    require(release_notes, "../../user/macos-support-data.md", "curated release notes macOS support-data link")


def validate_packaging_contract() -> None:
    packaging = read("tools/build/package_nightly.py")

    for token in (
        'MACOS_SUPPORT_INFO_SCRIPT_PATH = Path("tools") / "macos" / "collect_macos_support_info.sh"',
        'MACOS_SUPPORT_INFO_SCRIPT_NAME = "collect_macos_support_info.sh"',
        "def copy_release_collateral(source_root: Path, package_root: Path, platform: str) -> None:",
        'if platform == "macos":',
        "source_root / MACOS_SUPPORT_INFO_SCRIPT_PATH",
        "package_root / MACOS_SUPPORT_INFO_SCRIPT_NAME",
        "ensure_posix_executable(destination)",
        "Path(MACOS_SUPPORT_INFO_SCRIPT_NAME)",
        'f"{package_prefix}{MACOS_SUPPORT_INFO_SCRIPT_NAME}"',
        "copy_release_collateral(source_root, package_root, args.platform)",
    ):
        require(packaging, token, "macOS collector packaging contract")


def validate_phase1_plan_status() -> None:
    plan = read(PLAN_PATH)

    for token in (
        "## Phase 1: Improve Issue #73 Triage Without Running macOS",
        "- [x] Add a macOS issue/support template requesting:",
        "- [x] Add a short packaged `collect_macos_support_info.sh` helper that gathers",
        "- [x] Make the helper redact obvious user secrets from paths or environment",
        "- [x] Add a docs page explaining how users can provide crash data without",
        "- [x] Link the support-data request from issue #73 and future macOS release",
        "Phase 1 implementation status",
        ".github/ISSUE_TEMPLATE/macos-crash-report.yml",
        "docs/user/macos-support-data.md",
        "tools/macos/collect_macos_support_info.sh",
        "tools/tests/macos_support_intake.py",
    ):
        require(plan, token, "Phase 1 Apple support plan")


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
        require(source, "macos_support_intake.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
        (macos_debug, "macOS debug workflow"),
    ):
        require(source, "python tools/tests/macos_support_intake.py", context)


def main() -> None:
    validate_issue_template()
    validate_support_collector()
    validate_user_docs()
    validate_packaging_contract()
    validate_phase1_plan_status()
    validate_ci_and_local_wiring()
    print("macos_support_intake: ok")


if __name__ == "__main__":
    main()
