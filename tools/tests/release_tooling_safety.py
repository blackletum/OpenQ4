#!/usr/bin/env python3
"""Regression checks for release/version/docs tooling safety."""

from __future__ import annotations

import importlib.util
import argparse
import shutil
import subprocess
import sys
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "tools" / "build"
WORK = ROOT / ".tmp" / "release-tooling-safety-test"


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


CHANGELOG = load_module("release_changelog_under_test", BUILD_DIR / "generate_release_changelog.py")
DOCS = load_module("release_docs_under_test", BUILD_DIR / "generate_release_docs.py")
INSTALLER = load_module("windows_installer_under_test", BUILD_DIR / "build_windows_installer.py")
RELEASE_VERSION = load_module("release_version_under_test", BUILD_DIR / "openq4_release_version.py")
VERSION = load_module("openq4_version_under_test", BUILD_DIR / "openq4_version.py")


def write_file(path: Path, text: str = "data\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def run(command: list[str], cwd: Path) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed.stdout.strip()


def git(repo: Path, *args: str) -> str:
    return run(["git", *args], repo)


def make_git_repo(path: Path) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    git(path, "init", "-q")
    git(path, "config", "user.email", "openq4-tests@example.invalid")
    git(path, "config", "user.name", "openQ4 Tests")
    return path


def commit_file(repo: Path, name: str, text: str, subject: str) -> None:
    write_file(repo / name, text)
    git(repo, "add", name)
    git(repo, "commit", "-q", "-m", subject)


def expect_runtime_error(callback, text: str, label: str) -> None:
    try:
        callback()
    except RuntimeError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_system_exit(callback, text: str, label: str) -> None:
    try:
        callback()
    except SystemExit as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_file_not_found(callback, text: str, label: str) -> None:
    try:
        callback()
    except FileNotFoundError as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def expect_exception(callback, exception_type: type[BaseException], text: str, label: str) -> None:
    try:
        callback()
    except exception_type as exc:
        if text not in str(exc):
            raise AssertionError(f"{label} raised {exc!r}, expected text {text!r}") from exc
        return
    raise AssertionError(f"{label} unexpectedly succeeded")


def validate_changelog_git_context_and_limits() -> None:
    repo = make_git_repo(WORK / "release-repo")
    commit_file(repo, "file.txt", "one\n", "first")
    git(repo, "tag", "v0.1.001")
    commit_file(repo, "file.txt", "two\n", "second")
    git(repo, "tag", "v0.1.003")
    commit_file(repo, "file.txt", "three\n", "third")
    git(repo, "tag", "v0.2.000")
    commit_file(repo, "file.txt", "four\n", "fourth")

    top_level = CHANGELOG.run_git(repo, ["rev-parse", "--show-toplevel"])
    if Path(top_level).resolve() != repo.resolve():
        raise AssertionError("release changelog git helper ignored source_root")

    previous = CHANGELOG.find_previous_release_tag(repo, "v0.1.003")
    if previous != "v0.1.001":
        raise AssertionError(f"previous release tag should ignore newer tags, got {previous!r}")

    commits = CHANGELOG.collect_commits(repo, "v0.1.001..HEAD", 1)
    if len(commits) != 1:
        raise AssertionError(f"collect_commits did not enforce max_count for ranges: {len(commits)}")

    if CHANGELOG.collect_commits(repo, "v0.1.001..HEAD", 0):
        raise AssertionError("collect_commits should return no commits for max_count=0")


def validate_changelog_input_and_markdown_safety() -> None:
    expect_runtime_error(
        lambda: CHANGELOG.validate_repo_slug("owner/repo/extra"),
        "GitHub repository slug",
        "repository slug validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_optional_https_url("http://example.invalid/run", "workflow run URL"),
        "https URL",
        "workflow URL validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_release_tag("release|bad"),
        "release tag",
        "release tag validation",
    )
    expect_runtime_error(
        lambda: CHANGELOG.validate_version_tag("0.1.010/bad"),
        "version tag",
        "version tag validation",
    )

    escaped = CHANGELOG.escape_markdown_inline("[link](javascript:alert(1)) | *bold*")
    for token in ("\\[link\\]", "\\(javascript:alert\\(1\\)\\)", "\\|", "\\*bold\\*"):
        if token not in escaped:
            raise AssertionError(f"markdown escape missed {token!r}: {escaped!r}")

    header = CHANGELOG.build_release_header(
        version="0.1.010",
        version_tag="0.1.010",
        release_tag="v0.1.010",
        release_scale="patch",
        release_reason="safe | injected\nnext row",
        repo_url="https://github.com/themuffinator/openQ4",
        head_sha="0123456789abcdef",
        generated_at="2026-06-23 00:00 UTC",
        run_id="run | id",
        run_url="https://github.com/themuffinator/openQ4/actions/runs/1",
        previous_tag="v0.1.009",
    )
    rendered = "\n".join(header)
    if "safe | injected" in rendered or "run | id" in rendered:
        raise AssertionError(f"release header did not escape table/link text:\n{rendered}")
    if "safe \\| injected next row" not in rendered or "run \\| id" not in rendered:
        raise AssertionError(f"release header lost escaped text:\n{rendered}")

    expect_exception(
        lambda: CHANGELOG.non_negative_int("-1"),
        argparse.ArgumentTypeError,
        "non-negative integer",
        "negative max commits",
    )
    expect_exception(
        lambda: CHANGELOG.non_negative_int("-1"),
        argparse.ArgumentTypeError,
        "non-negative integer",
        "negative max highlights",
    )


def validate_openq4_version_iteration() -> None:
    expect_system_exit(
        lambda: VERSION.resolve_auto_iteration_date("20260231"),
        "real UTC calendar date",
        "invalid auto iteration date",
    )

    original_run_git = VERSION.run_git
    VERSION.run_git = lambda _root, *_args: "\n".join(
        [
            "dev-0.1.010-dev.20260623.1",
            "dev-0.1.010-dev.20260623.10",
            "dev-0.1.010-dev.20260623.2",
            "dev-0.1.010-dev.20260623.preview",
        ]
    )
    try:
        iteration = VERSION.compute_auto_iteration(Path("."), "0.1.010", "dev", "20260623")
    finally:
        VERSION.run_git = original_run_git

    if iteration != "20260623.11":
        raise AssertionError(f"auto iteration should follow the highest suffix, got {iteration!r}")


def validate_release_version_floor_and_docs_classification() -> None:
    current = RELEASE_VERSION.parse_version("0.1.011")
    lower = RELEASE_VERSION.parse_version("0.1.010")
    latest = ("v0.1.012", RELEASE_VERSION.parse_version("0.1.012"))

    expect_system_exit(
        lambda: RELEASE_VERSION.validate_version_override(lower, current, None),
        "must not be lower than the configured project version",
        "version override current floor",
    )
    expect_system_exit(
        lambda: RELEASE_VERSION.validate_version_override(current, current, latest),
        "must not be lower than the latest published release",
        "version override latest floor",
    )

    category, weight, is_major = RELEASE_VERSION.classify_path("docs-user/getting-started.md")
    if (category, weight, is_major) != ("docs", 0, False):
        raise AssertionError(f"docs-user path was not classified as docs-only: {(category, weight, is_major)!r}")

    scale, reason, score = RELEASE_VERSION.analyze_change_scale(
        ["docs-user/getting-started.md"],
        commit_count=1,
        additions=10,
        deletions=0,
    )
    if (scale, score) != ("patch", 0) or "Only documentation" not in reason:
        raise AssertionError(f"docs-user-only change should stay docs-only, got {(scale, reason, score)!r}")


def validate_release_docs_output_guard() -> None:
    source_root = WORK / "docs-source"
    write_file(source_root / "README.md", "# Docs\n\nBody\n")

    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, source_root),
        "source root",
        "docs output source root guard",
    )
    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, source_root.parent),
        "parent directories",
        "docs output parent guard",
    )
    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, source_root / "docs-user"),
        "source-controlled",
        "docs output source-controlled guard",
    )

    DOCS.validate_output_root(source_root, source_root / ".tmp" / "docs")
    DOCS.validate_output_root(source_root, source_root / "builddir-ci-linux" / "docs")

    output_file = source_root / ".tmp" / "docs-file"
    write_file(output_file, "not a directory\n")
    expect_runtime_error(
        lambda: DOCS.validate_output_root(source_root, output_file),
        "not a directory",
        "docs output file guard",
    )


def validate_windows_installer_payload_requirements() -> None:
    package_dir = WORK / "installer-package"
    required = [
        "openQ4-client_x64.exe",
        "openQ4-client_x64.pdb",
        "openQ4-ded_x64.exe",
        "openQ4-ded_x64.pdb",
        "README.html",
        "LICENSE",
        "docs/index.html",
        "baseoq4/mod.json",
        "baseoq4/pak0.pk4",
        "baseoq4/pak1.pk4",
        "baseoq4/game-sp_x64.pdb",
        "baseoq4/game-mp_x64.pdb",
    ]
    for relative in required:
        write_file(package_dir / relative)

    expect_file_not_found(
        lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
        "OpenAL32.dll",
        "installer missing OpenAL runtime",
    )
    write_file(package_dir / "OpenAL32.dll")
    expect_file_not_found(
        lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
        "game-sp_x64.dll",
        "installer missing game module DLLs",
    )
    write_file(package_dir / "baseoq4" / "game-sp_x64.dll")
    expect_file_not_found(
        lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
        "game-mp_x64.dll",
        "installer missing multiplayer module DLL",
    )
    write_file(package_dir / "baseoq4" / "game-mp_x64.dll")
    INSTALLER.validate_package_dir(package_dir, "x64")


def validate_validation_wiring() -> None:
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    push = (ROOT / ".github" / "workflows" / "push-verification.yml").read_text(encoding="utf-8")
    commit = (ROOT / ".github" / "workflows" / "commit-validation.yml").read_text(encoding="utf-8")
    for context, text in (
        ("validation runner", validator),
        ("push workflow", push),
        ("commit workflow", commit),
    ):
        if "release_tooling_safety.py" not in text:
            raise AssertionError(f"release_tooling_safety.py is not wired into {context}")


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_changelog_git_context_and_limits()
        validate_changelog_input_and_markdown_safety()
        validate_openq4_version_iteration()
        validate_release_version_floor_and_docs_classification()
        validate_release_docs_output_guard()
        validate_windows_installer_payload_requirements()
        validate_validation_wiring()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("release_tooling_safety: ok")


if __name__ == "__main__":
    main()
