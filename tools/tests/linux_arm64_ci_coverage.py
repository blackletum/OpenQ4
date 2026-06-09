#!/usr/bin/env python3
"""Regression checks for Linux ARM64 CI coverage."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def require_count(haystack: str, needle: str, expected: int, context: str) -> None:
    actual = haystack.count(needle)
    if actual != expected:
        raise AssertionError(f"Expected {expected} occurrence(s) of {needle!r} in {context}, found {actual}")


def validate_push_workflow() -> None:
    source = read(".github/workflows/push-verification.yml")

    require(source, "Linux ARM64 Push Verification", "push verification workflow")
    require(source, "os: ubuntu-24.04-arm", "push verification workflow")
    require(source, "artifact_name: linux-arm64", "push verification workflow")
    require(source, "runtime_smoke: true", "push verification workflow")
    require(source, "startsWith(matrix.os, 'ubuntu-')", "push verification Linux dependency gate")
    require(source, "xvfb", "push verification runtime display dependency")
    require(source, "xvfb-run -a bash tools/validation/validate_push.sh", "push verification runtime smoke")
    require(source, "--runtime-cases renderer-default-safety-selftest", "push verification runtime smoke")
    require(source, "--runtime-skip-official-pak-validation", "push verification assetless runtime smoke")
    require(source, "SDL_VIDEO_DRIVER=x11 SDL_VIDEODRIVER=x11", "push verification runtime display override")
    require(source, "python tools/tests/linux_arm64_ci_coverage.py", "push script-smoke regression check")


def validate_commit_workflow() -> None:
    source = read(".github/workflows/commit-validation.yml")

    require(source, "Linux ARM64 Commit Validation", "commit validation workflow")
    require(source, "runs-on: ubuntu-24.04-arm", "commit validation workflow")
    require(source, "xvfb", "commit validation runtime display dependency")
    require(source, "xvfb-run -a bash tools/validation/validate_pr.sh", "commit validation runtime smoke")
    require(source, "--runtime-cases renderer-default-safety-selftest", "commit validation runtime smoke")
    require(source, "--runtime-skip-official-pak-validation", "commit validation assetless runtime smoke")
    require(source, "SDL_VIDEO_DRIVER=x11 SDL_VIDEODRIVER=x11", "commit validation runtime display override")
    require(source, "python tools/tests/linux_arm64_ci_coverage.py", "commit script-smoke regression check")


def validate_runtime_flags() -> None:
    renderer = read("tools/tests/renderer_validation_matrix.py")
    runner = read("tools/validation/openq4_validate.py")

    require(renderer, "--skip-official-pak-validation", "renderer validation matrix assetless option")
    require(renderer, '"fs_validateOfficialPaks", "0"', "renderer validation matrix startup cvar")
    require(renderer, "skipOfficialPakValidation", "renderer validation matrix report metadata")
    require(runner, "--runtime-skip-official-pak-validation", "validation profile assetless option")
    require(runner, '"--skip-official-pak-validation"', "validation profile renderer handoff")


def validate_release_note() -> None:
    source = read("docs-dev/release-completion.md")

    require(source, "Linux ARM64 is now covered by normal CI", "release completion notes")
    require(source, "assetless renderer startup smoke", "release completion notes")


def validate_no_duplicate_jobs() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require_count(push, "windows-build:", 1, "push verification workflow")
    require_count(push, "Linux ARM64 Push Verification", 1, "push verification workflow")
    require_count(commit, "Linux ARM64 Commit Validation", 1, "commit validation workflow")


def main() -> None:
    validate_push_workflow()
    validate_commit_workflow()
    validate_runtime_flags()
    validate_release_note()
    validate_no_duplicate_jobs()
    print("linux_arm64_ci_coverage: ok")


if __name__ == "__main__":
    main()
