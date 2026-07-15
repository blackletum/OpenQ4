#!/usr/bin/env python3
"""Static contracts for the non-publishing macOS universal2 candidate lane."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(source: str, needle: str, context: str) -> None:
    if needle not in source:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(source: str, needle: str, context: str) -> None:
    if needle in source:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def validate_candidate_workflow() -> None:
    workflow = read(".github/workflows/macos-universal2-candidate.yml")

    for token in (
        "name: macOS Universal2 Release Candidate",
        "workflow_dispatch:",
        "openq4_game_ref:",
        "graphics_bridge:",
        "macos_signing_mode:",
        "default: ad-hoc",
        "contents: read",
        "source_sha:",
        "game_sha:",
        "thin_matrix:",
        "bridge_matrix:",
        "git status --porcelain --untracked-files=all",
        "openq4_version.py",
        "macos-15-intel",
        "macos-15",
        "Verify requested native architecture",
        "expected_arch=\"x86_64\"",
        "Fetch pinned openQ4-game source",
        "--expected-project-commit",
        "--expected-gamelibs-commit",
        "--buildtype=debugoptimized",
        "assemble_macos_universal2.py record",
        "Archive thin universal2 candidate payload with modes",
        "tar -C .install -czf",
        "Restore mode-preserving thin payloads",
        "tar -xzf",
        "assemble_macos_universal2.py assemble",
        "--arch universal2",
        "package_release.py",
        "--macos-signing-mode ad-hoc",
        "--macos-signing-mode developer-id",
        "--macos-notarize",
        "lipo -archs",
        "Expected exact arm64 x86_64 slices",
        "otool -arch",
        "codesign --verify",
        "xcrun stapler validate",
        "spctl --assess",
        "Finder-style directory",
        "game-sp_universal2.dylib",
        "retention-days: 90",
    ):
        require(workflow, token, "macOS universal2 candidate workflow")

    reject(workflow, "contents: write", "macOS universal2 candidate workflow permissions")
    reject(workflow, "gh release", "macOS universal2 candidate workflow publication")
    reject(workflow, "softprops/action-gh-release", "macOS universal2 candidate workflow publication")
    reject(workflow, "path: .install", "macOS universal2 candidate thin artifact transfer")


def validate_docs_and_wiring() -> None:
    design = read("docs/dev/macos-universal2-design.md")
    plan = read("docs/dev/plan/2026-06-30-macos-compatibility-support.md")
    completion = read("docs/dev/release-completion.md")
    release_notes = read("docs/dev/releases/v0.8.1.md")
    validator = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for source, context in (
        (design, "universal2 design"),
        (plan, "macOS compatibility plan"),
        (completion, "release completion"),
        (release_notes, "curated release notes"),
    ):
        require(source, "non-publishing", context)
        require(source, "arm64-only", context)

    for source, context in (
        (validator, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_universal2_release_candidate.py", context)

    commit_macos_start = commit.index("  macos-arm64:")
    commit_universal_start = commit.index("  macos-universal2:", commit_macos_start)
    commit_thin_jobs = commit[commit_macos_start:commit_universal_start]
    for token in (
        "Archive macOS ARM64 staged payload with modes",
        "Archive macOS Intel staged payload with modes",
        "tar -C .install -czf",
    ):
        require(commit_thin_jobs, token, "commit-validation thin payload transfer")
    reject(commit_thin_jobs, "path: .install", "commit-validation thin payload transfer")

    commit_universal = commit[commit_universal_start:]
    require(commit_universal, "Restore mode-preserving thin payloads", "commit-validation universal2 restore")
    require(commit_universal, "tar -xzf", "commit-validation universal2 restore")


def main() -> None:
    validate_candidate_workflow()
    validate_docs_and_wiring()
    print("macos_universal2_release_candidate: ok")


if __name__ == "__main__":
    main()
