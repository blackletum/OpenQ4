#!/usr/bin/env python3
"""Regression checks for the macOS package UX and release policy contract."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_package_layout_policy_doc() -> None:
    policy = read("docs/dev/macos-package-layout-and-release-policy.md")

    for token in (
        "# macOS Package Layout And Release Policy",
        "adjacent package root, not a",
        "self-contained drag-only app bundle",
        "`openQ4.app`",
        "`baseoq4/`",
        "The SP/MP game modules must live under `baseoq4/`",
        "Do not copy openQ4 game\nmodules into `q4base/`",
        "app-bundle startup preflight treats `q4base/game-*.dylib` as misplaced",
        "loose runtime support files",
        "Double-click `openQ4.app` from the mounted signed/notarized DMG payload.",
        "Copy the whole package payload to a user-writable folder",
        "Launch from Terminal with the working directory at the package root.",
        "Moving only `openQ4.app` away from its package root.",
        "unsupported until a self-contained bundle migration is implemented",
        "`fs_basepath`, `fs_cdpath`, and `fs_savepath`",
        "Gatekeeper behavior",
        "First-class macOS releases require signed and notarized DMGs",
        "Unsigned `-unsigned.tar.gz` archives are allowed only for experimental or",
        "tools/build/package_nightly.py",
    ):
        require(policy, token, "macOS package layout and release policy doc")


def validate_signoff_evidence_contract() -> None:
    workflow_doc = read("docs/dev/macos-vm-testing-workflow.md")
    guest = read("tools/macos/guest/openq4-macos-sync-build-test.sh")
    validator = read("tools/macos/validate_signoff_archive.py")
    evidence = read("docs/dev/macos-signoff-evidence.md")
    recorder = read("tools/macos/record_signoff_evidence.py")
    release_completion = read("docs/dev/release-completion.md")

    for token in (
        "docs/dev/macos-package-layout-and-release-policy.md",
        "mounted signed/notarized DMG",
        "copy the whole package",
        "app-only move",
        "`fs_basepath`, `fs_cdpath`, and",
        "Gatekeeper behavior",
    ):
        require(workflow_doc, token, "macOS workflow package UX documentation")

    for token in (
        "Package layout contract is adjacent package root",
        "mounted signed/notarized DMG",
        "whole package payload",
        "Move only openQ4.app",
        "package root as the working directory",
        "fs_basepath",
        "fs_cdpath",
        "fs_savepath",
        "Gatekeeper assessment",
    ):
        require(guest, token, "macOS guest signoff package UX checklist")
        require(validator, token, "macOS signoff archive package UX validation")

    for token in (
        "Package layout contract",
        "Mounted DMG launch coverage",
        "Copied package launch coverage",
        "App-only move behavior",
        "Path resolution log coverage",
        "Gatekeeper assessment",
        "First-class macOS release artifacts are signed/notarized DMGs",
    ):
        require(evidence, token, "macOS signoff evidence package UX fields")
        require(recorder, token, "macOS signoff evidence recorder package UX output")

    for token in (
        "macOS Package Layout And Release Policy",
        "First-class macOS release jobs used signed/notarized DMGs",
        "`fs_basepath`, `fs_cdpath`, and `fs_savepath`",
        "Gatekeeper assessment passed",
    ):
        require(release_completion, token, "release completion macOS package UX gate")


def validate_release_workflow_gate() -> None:
    workflow = read(".github/workflows/manual-release.yml")
    building = read("BUILDING.md")
    platform_support = read("docs/dev/platform-support.md")

    for token in (
        "macos_support_tier",
        "macOS release support tier",
        "first-class",
        "OPENQ4_MACOS_SUPPORT_TIER",
        "First-class macOS releases require signed/notarized DMGs",
        "missing Apple signing/notary repository secrets",
        "Experimental macOS unsigned/unnotarized tar.gz release artifacts enabled as fallback output",
        "macos_support_tier=",
    ):
        require(workflow, token, "manual release macOS first-class gate")

    for source, context in (
        (building, "build documentation"),
        (platform_support, "platform support documentation"),
    ):
        require(source, "macos_support_tier=experimental", context)
        require(source, "macos_support_tier=first-class", context)
        require(source, "signed/notarized DMGs", context)
        require(source, "`-unsigned.tar.gz`", context)


def validate_user_docs_and_package_readme() -> None:
    getting_started = read("docs/user/getting-started.md")
    package_readme = read("assets/release/README.html")
    platform_support = read("docs/dev/platform-support.md")

    require(
        getting_started,
        "Keep `openQ4.app`, `baseoq4/`, and the loose runtime files together as one adjacent package root",
        "getting started macOS package layout",
    )
    require(
        getting_started,
        "moving only `openQ4.app` to `/Applications` is not supported yet",
        "getting started app-only move limitation",
    )
    require(
        package_readme,
        "Keep <code>openQ4.app</code>, <code>baseoq4/</code>, and the loose runtime files together as one adjacent package root",
        "packaged README macOS package layout",
    )
    require(
        package_readme,
        "moving only <code>openQ4.app</code> to <code>/Applications</code> is not supported yet",
        "packaged README app-only move limitation",
    )
    require(
        platform_support,
        "The current supported package contract is an adjacent package root",
        "platform support adjacent package contract",
    )
    require(
        platform_support,
        "moving only `openQ4.app` is unsupported",
        "platform support app-only limitation",
    )


def validate_plan_status_and_ci_wiring() -> None:
    plan = read("docs/dev/plan/2026-06-30-macos-compatibility-support.md")
    local_runner = read("tools/validation/openq4_validate.py")
    commit = read(".github/workflows/commit-validation.yml")
    push = read(".github/workflows/push-verification.yml")

    for token in (
        "Phase 3: Package UX and release policy",
        "Phase 3 implementation status",
        "`docs/dev/macos-package-layout-and-release-policy.md`",
        "macos_support_tier=first-class",
        "mounted-DMG, copied-package, app-only move, path-resolution, and Gatekeeper results",
    ):
        require(plan, token, "macOS compatibility/support plan Phase 3 status")

    for source, context in (
        (local_runner, "local validation runner"),
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "macos_package_policy.py", context)

    for source, context in (
        (commit, "commit validation workflow"),
        (push, "push verification workflow"),
    ):
        require(source, "python tools/tests/macos_package_policy.py", context)


def main() -> None:
    validate_package_layout_policy_doc()
    validate_signoff_evidence_contract()
    validate_release_workflow_gate()
    validate_user_docs_and_package_readme()
    validate_plan_status_and_ci_wiring()
    print("macos_package_policy: ok")


if __name__ == "__main__":
    main()
