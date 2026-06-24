#!/usr/bin/env python3
"""Regression checks for release/version/docs tooling safety."""

from __future__ import annotations

import importlib.util
import argparse
import os
import shutil
import subprocess
import sys
import uuid
from pathlib import Path
from types import ModuleType


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "tools" / "build"
WORK_BASE = ROOT / ".tmp" / "release-tooling-safety-test"


def make_work_root() -> Path:
    return WORK_BASE / f"{os.getpid()}-{uuid.uuid4().hex}"


WORK = make_work_root()


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
SYNC_ICONS = load_module("sync_icons_under_test", BUILD_DIR / "sync_icons.py")
VERSION = load_module("openq4_version_under_test", BUILD_DIR / "openq4_version.py")
DOC_LINKS = load_module("docs_link_integrity_under_test", ROOT / "tools" / "tests" / "docs_link_integrity.py")


def write_file(path: Path, text: str = "data\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def make_symlink(target: Path, link: Path, *, target_is_directory: bool = False) -> bool:
    try:
        os.symlink(target, link, target_is_directory=target_is_directory)
    except (OSError, NotImplementedError):
        return False
    return True


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
        lambda: CHANGELOG.validate_optional_https_url("https://example.invalid/run) injected", "workflow run URL"),
        "unsafe in generated Markdown",
        "workflow URL markdown injection validation",
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


def validate_changelog_output_and_override_guards() -> None:
    output_root = WORK / "changelog-output"
    outside = WORK / "changelog-outside.md"
    write_file(outside, "outside\n")
    output_link = output_root / "release.md"
    output_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, output_link):
        expect_runtime_error(
            lambda: CHANGELOG.validate_output_path(output_link),
            "through symlink",
            "release changelog output symlink guard",
        )

    parent_target = WORK / "changelog-parent-target"
    parent_target.mkdir(parents=True, exist_ok=True)
    parent_link = WORK / "changelog-parent-link"
    if make_symlink(parent_target, parent_link, target_is_directory=True):
        expect_runtime_error(
            lambda: CHANGELOG.validate_output_path(parent_link / "release.md"),
            "symlinked directory",
            "release changelog output parent symlink guard",
        )

    notes_root = WORK / "changelog-notes-source"
    release_notes_target = WORK / "external-release-notes.md"
    write_file(release_notes_target, "# external\n")
    release_notes_link = notes_root / "docs" / "dev" / "releases" / "v0.1.010.md"
    release_notes_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(release_notes_target, release_notes_link):
        expect_runtime_error(
            lambda: CHANGELOG.find_tracked_release_notes(notes_root, "v0.1.010", "0.1.010"),
            "must not be a symlink",
            "tracked release notes symlink guard",
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

    category, weight, is_major = RELEASE_VERSION.classify_path("docs/user/getting-started.md")
    if (category, weight, is_major) != ("docs", 0, False):
        raise AssertionError(f"docs/user path was not classified as docs-only: {(category, weight, is_major)!r}")
    category, weight, is_major = RELEASE_VERSION.classify_path("assets/docs/img/banner.png")
    if (category, weight, is_major) != ("docs", 0, False):
        raise AssertionError(f"docs asset path was not classified as docs-only: {(category, weight, is_major)!r}")
    category, weight, is_major = RELEASE_VERSION.classify_path("assets/icons/quake4.ico")
    if (category, weight, is_major) != ("packaging", 2, False):
        raise AssertionError(f"packaged icon path was not classified as packaging: {(category, weight, is_major)!r}")
    category, weight, is_major = RELEASE_VERSION.classify_path("assets/linux/openQ4-steamdeck.in")
    if (category, weight, is_major) != ("platform", 2, False):
        raise AssertionError(f"Linux launcher asset path was not classified as platform: {(category, weight, is_major)!r}")

    scale, reason, score = RELEASE_VERSION.analyze_change_scale(
        ["docs/user/getting-started.md"],
        commit_count=1,
        additions=10,
        deletions=0,
    )
    if (scale, score) != ("patch", 0) or "Only documentation" not in reason:
        raise AssertionError(f"docs/user-only change should stay docs-only, got {(scale, reason, score)!r}")

    scale, reason, score = RELEASE_VERSION.analyze_change_scale(
        ["assets/icons/quake4.ico"],
        commit_count=1,
        additions=1,
        deletions=1,
    )
    if (scale, score) != ("patch", 2) or "Only documentation" in reason:
        raise AssertionError(f"packaged asset change should not be docs-only, got {(scale, reason, score)!r}")


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
        lambda: DOCS.validate_output_root(source_root, source_root / "docs/user"),
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

    output_target = source_root / ".tmp" / "docs-target"
    output_target.mkdir(parents=True, exist_ok=True)
    output_link = source_root / ".tmp" / "docs-link"
    if make_symlink(output_target, output_link, target_is_directory=True):
        expect_runtime_error(
            lambda: DOCS.validate_output_root(source_root, output_link),
            "symlinked output",
            "docs symlinked output guard",
        )

    source_target = WORK / "docs-source-target"
    source_target.mkdir(parents=True, exist_ok=True)
    source_link = WORK / "docs-source-link"
    if make_symlink(source_target, source_link, target_is_directory=True):
        expect_runtime_error(
            lambda: DOCS.validate_source_root(source_link),
            "source root must not be a symlink",
            "docs symlinked source root guard",
        )


def validate_release_docs_layout() -> None:
    source_root = WORK / "docs-layout-source"
    legacy_user_root = "docs" + "-user"
    legacy_dev_root = "docs" + "-dev"
    write_file(source_root / "README.md", "# Docs\n\nProject docs.\n")
    write_file(source_root / "docs" / "user" / "getting-started.md", "# Getting Started\n\nUser guide.\n")
    write_file(source_root / "docs" / "dev" / "platform-support.md", "# Platform Support\n\nDeveloper guide.\n")
    write_file(source_root / "docs" / "dev" / "proposals" / "renderer.md", "# Renderer Proposal\n\nResearch.\n")
    write_file(source_root / legacy_user_root / "legacy.md", "# Old User Path\n\nIgnored.\n")
    write_file(source_root / legacy_dev_root / "legacy.md", "# Old Dev Path\n\nIgnored.\n")

    sources = {relative.as_posix() for relative in DOCS.collect_doc_sources(source_root)}
    expected = {
        "README.md",
        "docs/user/getting-started.md",
        "docs/dev/platform-support.md",
        "docs/dev/proposals/renderer.md",
    }
    missing = expected - sources
    if missing:
        raise AssertionError(f"release docs did not collect new docs layout entries: {sorted(missing)!r}")
    unexpected_legacy = {f"{legacy_user_root}/legacy.md", f"{legacy_dev_root}/legacy.md"} & sources
    if unexpected_legacy:
        raise AssertionError(f"release docs still collect legacy docs layout entries: {sorted(unexpected_legacy)!r}")

    classifications = {
        "README.md": "Project",
        "docs/user/getting-started.md": "User Guides",
        "docs/dev/platform-support.md": "Developer Guides",
        "docs/dev/proposals/renderer.md": "Research and Proposals",
    }
    for relative, expected_group in classifications.items():
        group = DOCS.classify_group(Path(relative))
        if group != expected_group:
            raise AssertionError(f"{relative} classified as {group!r}, expected {expected_group!r}")


def validate_release_docs_symlink_and_link_guards() -> None:
    source_root = WORK / "docs-symlink-source"
    target = WORK / "docs-symlink-target.md"
    write_file(target, "# Leaked\n\nOutside content.\n")
    readme_link = source_root / "README.md"
    readme_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(target, readme_link):
        expect_runtime_error(
            lambda: DOCS.collect_doc_sources(source_root),
            "symlinked documentation source",
            "root documentation source symlink guard",
        )

    nested_root = WORK / "docs-nested-symlink-source"
    write_file(nested_root / "README.md", "# Docs\n\nBody\n")
    nested_link = nested_root / "docs" / "user" / "linked.md"
    nested_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(target, nested_link):
        expect_runtime_error(
            lambda: DOCS.collect_doc_sources(nested_root),
            "symlinked documentation source",
            "nested documentation source symlink guard",
        )

    asset_root = WORK / "docs-assets" / "img"
    asset_dest = WORK / "docs-assets-out"
    write_file(asset_root / "safe.png", "not really an image\n")
    asset_link = asset_root / "leak.png"
    if make_symlink(target, asset_link):
        expect_runtime_error(
            lambda: DOCS.copy_docs_asset_tree(asset_root, asset_dest),
            "symlinked documentation asset",
            "documentation asset symlink guard",
        )

    auxiliary_root = WORK / "docs-auxiliary-source"
    auxiliary_link = auxiliary_root / "LICENSE"
    auxiliary_link.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(target, auxiliary_link):
        expect_runtime_error(
            lambda: DOCS.copy_docs_auxiliary_file(auxiliary_root, Path("LICENSE"), WORK / "docs-auxiliary-out" / "LICENSE"),
            "symlinked documentation auxiliary file",
            "documentation auxiliary symlink guard",
        )

    prepared = DOCS.prepare_markdown(
        "[bad](javascript:alert(1)) "
        "![inline](data:text/html,evil) "
        "[file](file:///tmp/leak) "
        "[ok](https://example.invalid/docs)"
    )
    for unsafe in ("javascript:", "data:text", "file:///"):
        if unsafe in prepared:
            raise AssertionError(f"unsafe generated-docs URI scheme survived rewrite: {prepared!r}")
    if "[ok](https://example.invalid/docs)" not in prepared:
        raise AssertionError(f"safe HTTPS link was not preserved: {prepared!r}")


def validate_docs_link_integrity_work_roots() -> None:
    first = DOC_LINKS.make_work_root()
    second = DOC_LINKS.make_work_root()
    expected_parent = ROOT / ".tmp" / "docs-link-integrity"
    if first == second:
        raise AssertionError(f"docs link integrity work roots are not unique: {first}")
    if first.parent != expected_parent or second.parent != expected_parent:
        raise AssertionError(f"docs link integrity work roots are outside the expected temp root: {first}, {second}")
    if str(os.getpid()) not in first.name or str(os.getpid()) not in second.name:
        raise AssertionError(f"docs link integrity work roots do not include the process id: {first}, {second}")


def validate_docs_link_integrity_local_link_guards() -> None:
    source_root = WORK / "docs-link-integrity-source"
    outside = WORK / "outside-doc.md"
    write_file(outside, "# Outside\n")
    write_file(source_root / "README.md", "[outside](../outside-doc.md)\n")

    expect_exception(
        lambda: DOC_LINKS.validate_markdown_links_for_sources([source_root / "README.md"], source_root),
        AssertionError,
        "escapes documentation root",
        "docs link integrity escaped local Markdown link",
    )

    symlink_source_root = WORK / "docs-link-integrity-symlink-source"
    write_file(symlink_source_root / "README.md", "# Root\n")
    symlinked_doc = symlink_source_root / "docs" / "user" / "linked.md"
    symlinked_doc.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, symlinked_doc):
        expect_exception(
            lambda: DOC_LINKS.markdown_sources(symlink_source_root),
            AssertionError,
            "symlinked Markdown source",
            "docs link integrity symlinked Markdown source",
        )

    symlinked_text = symlink_source_root / "docs" / "user" / "linked.txt"
    if make_symlink(outside, symlinked_text):
        expect_exception(
            lambda: DOC_LINKS.iter_text_files(symlink_source_root),
            AssertionError,
            "must not be a symlink",
            "docs link integrity symlinked text source",
        )


def validate_icon_sync_symlink_guards() -> None:
    icon_root = WORK / "icon-sync"
    target = icon_root / "real.ico"
    link = icon_root / "quake4.ico"
    write_file(target, "ico\n")
    if make_symlink(target, link):
        expect_file_not_found(
            lambda: SYNC_ICONS.ensure_file(link, "ICO icon"),
            "must not be a symlink",
            "sync_icons required file symlink guard",
        )

    png_target = icon_root / "real.png"
    png_link = icon_root / "quake4_1024.png"
    write_file(png_target, "png\n")
    if make_symlink(png_target, png_link):
        expect_file_not_found(
            lambda: SYNC_ICONS.highest_png_source(icon_root),
            "must not be a symlink",
            "sync_icons PNG source symlink guard",
        )

    output_target = icon_root / "output-target.png"
    output_link = icon_root / "quake4_16.png"
    write_file(output_target, "outside\n")
    if make_symlink(output_target, output_link):
        expect_runtime_error(
            lambda: SYNC_ICONS.ensure_writable_icon_output(output_link),
            "must not be a symlink",
            "sync_icons generated output symlink guard",
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


def validate_windows_installer_symlink_guards() -> None:
    package_dir = WORK / "installer-symlink-package"
    required = [
        "openQ4-client_x64.exe",
        "openQ4-client_x64.pdb",
        "openQ4-ded_x64.exe",
        "openQ4-ded_x64.pdb",
        "OpenAL32.dll",
        "README.html",
        "LICENSE",
        "docs/index.html",
        "baseoq4/mod.json",
        "baseoq4/pak0.pk4",
        "baseoq4/pak1.pk4",
        "baseoq4/game-sp_x64.dll",
        "baseoq4/game-sp_x64.pdb",
        "baseoq4/game-mp_x64.dll",
        "baseoq4/game-mp_x64.pdb",
    ]
    for relative in required:
        write_file(package_dir / relative)

    real_client = package_dir / "real-client.exe"
    write_file(real_client, "client\n")
    (package_dir / "openQ4-client_x64.exe").unlink()
    if make_symlink(real_client, package_dir / "openQ4-client_x64.exe"):
        expect_file_not_found(
            lambda: INSTALLER.validate_package_dir(package_dir, "x64"),
            "must not be symlinks",
            "installer payload symlink guard",
        )

    output_dir = WORK / "installer-output"
    script_path = output_dir / "openq4-0.1.010-windows-x64-setup.iss"
    installer_path = output_dir / "openq4-0.1.010-windows-x64-setup.exe"
    outside = WORK / "installer-output-outside.iss"
    write_file(outside, "outside\n")
    script_path.parent.mkdir(parents=True, exist_ok=True)
    if make_symlink(outside, script_path):
        expect_runtime_error(
            lambda: INSTALLER.validate_output_paths(output_dir, script_path, installer_path),
            "through symlink",
            "installer script output symlink guard",
        )

    output_target = WORK / "installer-output-target"
    output_target.mkdir(parents=True, exist_ok=True)
    output_link = WORK / "installer-output-link"
    if make_symlink(output_target, output_link, target_is_directory=True):
        expect_runtime_error(
            lambda: INSTALLER.validate_output_paths(
                output_link,
                output_link / "openq4-0.1.010-windows-x64-setup.iss",
                output_link / "openq4-0.1.010-windows-x64-setup.exe",
            ),
            "must not be a symlink",
            "installer output directory symlink guard",
        )

    package_target = WORK / "installer-package-target"
    package_target.mkdir(parents=True, exist_ok=True)
    package_link = WORK / "installer-package-link"
    if make_symlink(package_target, package_link, target_is_directory=True):
        expect_file_not_found(
            lambda: INSTALLER.require_directory_arg(package_link, "package directory", must_exist=True),
            "must not be a symlink",
            "installer raw package directory symlink guard",
        )

    asset_target = WORK / "installer-template-target.iss"
    asset_link = WORK / "installer-template-link.iss"
    write_file(asset_target, "; template\n")
    if make_symlink(asset_target, asset_link):
        expect_file_not_found(
            lambda: INSTALLER.require_regular_file(asset_link, "installer template"),
            "must not be a symlink",
            "installer template symlink guard",
        )


def validate_windows_installer_input_escaping() -> None:
    expect_exception(
        lambda: INSTALLER.validate_version("0.1/bad"),
        ValueError,
        "dotted numeric",
        "installer version validation",
    )
    expect_exception(
        lambda: INSTALLER.validate_filename_token("../escape", "version tag"),
        ValueError,
        "file-name-safe",
        "installer version tag validation",
    )

    rendered = INSTALLER.render_installer_script(
        '#define PackageSource "@@PACKAGE_SOURCE@@"\n@@OUTPUT_BASENAME@@\n',
        package_dir=Path('C:/release/openq4 "quoted"'),
        output_dir=Path("C:/out"),
        version="0.1.010",
        version_tag="0.1.010",
        arch="x64",
        setup_icon_file=Path("C:/icons/openQ4.ico"),
    )
    expected_source = INSTALLER.inno_string_contents(Path('C:/release/openq4 "quoted"'))
    if f'#define PackageSource "{expected_source}"' not in rendered:
        raise AssertionError(f"installer PackageSource was not escaped for Inno Setup:\n{rendered}")


def validate_discord_release_download_suffixes() -> None:
    announcer = (ROOT / ".github" / "scripts" / "announce-release-discord.mjs").read_text(encoding="utf-8")
    for suffix in (
        "macos-arm64-opengl.dmg",
        "macos-arm64-metal.dmg",
        "macos-arm64-opengl-unsigned.tar.gz",
        "macos-arm64-metal-unsigned.tar.gz",
    ):
        if suffix not in announcer:
            raise AssertionError(f"Discord release announcer is missing macOS download suffix {suffix!r}")
    for token in (
        "validateDiscordWebhookUrl",
        "validateGitHubRepoSlug",
        "validateHttpsUrl",
        "DISCORD_WEBHOOK_URL must point at a Discord webhook API path",
        "contains characters that are unsafe in Discord Markdown",
        "new Set([\"discord.com\", \"discordapp.com\"])",
        "new Set([\"github.com\"])",
    ):
        if token not in announcer:
            raise AssertionError(f"Discord release announcer is missing validation token {token!r}")


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
        if "docs_link_integrity.py" not in text:
            raise AssertionError(f"docs_link_integrity.py is not wired into {context}")


def main() -> None:
    shutil.rmtree(WORK, ignore_errors=True)
    try:
        validate_changelog_git_context_and_limits()
        validate_changelog_input_and_markdown_safety()
        validate_changelog_output_and_override_guards()
        validate_openq4_version_iteration()
        validate_release_version_floor_and_docs_classification()
        validate_release_docs_output_guard()
        validate_release_docs_layout()
        validate_release_docs_symlink_and_link_guards()
        validate_docs_link_integrity_work_roots()
        validate_docs_link_integrity_local_link_guards()
        validate_icon_sync_symlink_guards()
        validate_windows_installer_payload_requirements()
        validate_windows_installer_symlink_guards()
        validate_windows_installer_input_escaping()
        validate_discord_release_download_suffixes()
        validate_validation_wiring()
    finally:
        shutil.rmtree(WORK, ignore_errors=True)
    print("release_tooling_safety: ok")


if __name__ == "__main__":
    main()
