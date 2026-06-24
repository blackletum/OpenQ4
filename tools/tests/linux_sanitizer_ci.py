#!/usr/bin/env python3
"""Regression checks for Linux sanitizer validation coverage."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def validate_commit_workflow() -> None:
    source = read(".github/workflows/commit-validation.yml")

    require(source, "linux-sanitizer:", "commit validation sanitizer job")
    require(source, "Linux x64 ASan+UBSan Commit Validation", "commit validation sanitizer job")
    require(source, "runs-on: ubuntu-24.04", "commit validation sanitizer runner")
    require(source, "ASAN_OPTIONS: halt_on_error=1:detect_leaks=0", "commit validation ASan options")
    require(source, "UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1", "commit validation UBSan options")
    require(source, "bash tools/validation/validate_pr.sh", "commit validation sanitizer runner")
    require(source, "--skip-python-tests", "commit validation sanitizer focus")
    require(source, "--no-install", "commit validation sanitizer compile-only lane")
    require(source, "--build-dir .tmp/validation/linux-sanitizer-builddir", "commit validation sanitizer builddir")
    require(source, "--buildtype debugoptimized", "commit validation sanitizer buildtype")
    require(source, "--jobs 2", "commit validation sanitizer parallelism")
    require(source, "--extra-setup-arg=-Duse_pch=false", "commit validation sanitizer PCH opt-out")
    require(source, "--extra-setup-arg=-Db_sanitize=address,undefined", "commit validation ASan+UBSan setup")
    require(source, "commit-linux-sanitizer-build-logs", "commit validation sanitizer artifact")
    require(source, ".tmp/validation/linux-sanitizer-builddir/meson-logs", "commit validation sanitizer logs")
    require(source, ".tmp/gamelibs_stage/openq4_gamelibs_stage_manifest.json", "commit validation sanitizer staging manifest")
    require(source, "python tools/tests/linux_sanitizer_ci.py", "commit script-smoke sanitizer guard")

    for dependency in (
        "libdecor-0-dev",
        "libdrm-dev",
        "libegl1-mesa-dev",
        "libgl1-mesa-dev",
        "libopenal-dev",
        "libwayland-dev",
        "libx11-dev",
        "libxxf86vm-dev",
    ):
        require(source, dependency, "commit validation sanitizer dependencies")


def validate_push_workflow() -> None:
    source = read(".github/workflows/push-verification.yml")

    require(source, "python tools/tests/linux_sanitizer_ci.py", "push script-smoke sanitizer guard")
    require(source, "tools/tests/linux_sanitizer_ci.py", "push script-smoke sanitizer py_compile")


def validate_validation_runner() -> None:
    source = read("tools/validation/openq4_validate.py")

    require(source, "linux_sanitizer_ci.py", "validation runner sanitizer guard")
    require(source, "--extra-setup-arg", "validation runner sanitizer Meson passthrough")
    require(source, "args.extra_setup_arg", "validation runner sanitizer Meson passthrough")
    require(source, "--no-install", "validation runner compile-only support")


def validate_documentation() -> None:
    source = read("BUILDING.md")

    require(source, "Linux Sanitizer Validation", "BUILDING sanitizer section")
    require(source, "ASan+UBSan", "BUILDING sanitizer section")
    require(source, "-Db_sanitize=address,undefined", "BUILDING sanitizer command")
    require(source, "-Duse_pch=false", "BUILDING sanitizer command")
    require(source, ".tmp/validation/linux-sanitizer-builddir", "BUILDING sanitizer builddir")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Linux sanitizer coverage now catches compile-time regressions", "release completion notes")
    require(source, "ASan+UBSan", "release completion notes")
    require(source, "openQ4-game staging manifest", "release completion notes")


def main() -> None:
    validate_commit_workflow()
    validate_push_workflow()
    validate_validation_runner()
    validate_documentation()
    validate_release_note()
    print("linux_sanitizer_ci: ok")


if __name__ == "__main__":
    main()
