#!/usr/bin/env python3
"""Regression checks for the VS Code fast default build path."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def find_task(tasks: list[dict[str, object]], label: str) -> dict[str, object]:
    for task in tasks:
        if task.get("label") == label:
            return task
    raise AssertionError(f"Missing VS Code task {label!r}")


def validate_tasks() -> None:
    tasks = json.loads(read(".vscode/tasks.json"))["tasks"]
    default_tasks = [
        task
        for task in tasks
        if isinstance(task.get("group"), dict) and task["group"].get("kind") == "build" and task["group"].get("isDefault") is True
    ]
    if len(default_tasks) != 1:
        raise AssertionError(f"Expected exactly one default build task, found {len(default_tasks)}")

    fast_build = find_task(tasks, "Build openQ4 (Meson Optimized)")
    if fast_build is not default_tasks[0]:
        raise AssertionError("Build openQ4 (Meson Optimized) must be the default VS Code build task")
    if fast_build.get("dependsOn"):
        raise AssertionError("Fast default build must not depend on configure or full install tasks")
    if "fastbuild" not in fast_build.get("args", []):
        raise AssertionError("Fast default build must invoke meson-task.ps1 fastbuild")

    full_build = find_task(tasks, "Full Build and Stage openQ4 (Meson Optimized)")
    if full_build.get("dependsOrder") != "sequence":
        raise AssertionError("Full build task must keep ordered configure, compile, install steps")
    for label in (
        "Configure openQ4 (Meson Optimized)",
        "Compile openQ4 (Meson Optimized)",
        "Stage openQ4 Install Tree (Meson Optimized)",
    ):
        if label not in full_build.get("dependsOn", []):
            raise AssertionError(f"Full build task is missing dependency {label!r}")


def validate_wrapper() -> None:
    wrapper = read(".vscode/meson-task.ps1")
    require(wrapper, "[ValidateSet('setup', 'compile', 'install', 'fastbuild')]", "VS Code Meson wrapper actions")
    require(wrapper, "stage_fast_install.py", "VS Code fast build staging script")
    # The launch configurations run whatever this build stages into .install, so it
    # has to be optimized. Configuring it as a plain debug build measured 146.5 Hz
    # against 315.7 Hz for the same source on game/airdefense1.
    require(wrapper, "'debugoptimized'", "VS Code build must configure an optimized buildtype")
    reject(wrapper, "'debug',", "VS Code build must not configure an unoptimized buildtype")
    require(wrapper, "check_staged_content_edits.py", "VS Code fast build staged content guard")
    require(wrapper, "'compile',", "VS Code fast build compiles through Meson")
    require(wrapper, "'--install-dir',", "VS Code fast build stages .install incrementally")

    stager = read("tools/build/stage_fast_install.py")
    require(stager, "copy_file_if_changed", "fast install copy-if-changed behavior")
    require(
        stager,
        "from windows_runtime import cleanup_windows_stage_target, is_windows_host",
        "shared Windows stage hygiene helper",
    )
    require(
        stager,
        "cleanup_windows_stage_target(install_dir)",
        "fast-build Windows stale-runtime cleanup",
    )
    require(stager, '"renderer-gl_*.dll"', "fast install renderer staging")
    require(stager, '"--temporary-runtime"', "isolated compatibility runtime staging")
    require(stager, 'source_root / ".tmp" / "stock-runtime"', "temporary runtime containment")
    require(stager, '"pak0.pk4"', "fast install stages pak0")
    require(stager, '"pak1.pk4"', "fast install stages pak1")
    reject(stager, '"*.lib",\n    "pak0.pk4"', "fast install must not copy linker artifacts as runtime content")

    windows_runtime = read("tools/build/windows_runtime.py")
    require(
        windows_runtime,
        "WINDOWS_STALE_STAGE_FILE_MANIFEST",
        "narrow Windows stale-runtime cleanup manifest",
    )
    require(
        windows_runtime,
        '"baseoq4/skins"',
        "known empty Windows stage directory manifest",
    )
    require(
        windows_runtime,
        "cleanup_results[str(target)] = cleanup_windows_stage_target(target)",
        "full-install Windows stale-runtime cleanup",
    )


def validate_launch_configs() -> None:
    launch = json.loads(read(".vscode/launch.json"))
    mp_configs = []
    for config in launch.get("configurations", []):
        if "preLaunchTask" in config:
            raise AssertionError(f"Launch config {config.get('name')!r} must not define preLaunchTask")
        if "(MP)" in str(config.get("name", "")):
            mp_configs.append(config)
            args = config.get("args", [])
            values = [
                str(args[index + 2])
                for index, token in enumerate(args[:-2])
                if token in ("+set", "+seta") and args[index + 1] == "ui_autoJoin"
            ]
            # The interactive configurations start at the join screen, which is the
            # shipped default; the automated MP profiles below still pin it to 1.
            if values != ["0"]:
                raise AssertionError(
                    f"MP launch config {config.get('name')!r} must set ui_autoJoin exactly once to 0"
                )
    if not mp_configs:
        raise AssertionError("Expected at least one VS Code MP launch configuration")


def validate_mp_autojoin_policy() -> None:
    listen_script = read("tools/debug/start_listen_server_client.ps1")
    if listen_script.count('"+set", "ui_autoJoin", "1"') != 2:
        raise AssertionError("MP listen-server helper must enable auto-join for host and client")

    renderdoc_script = read("tools/debug/renderdoc_capture.ps1")
    if renderdoc_script.count('"+set", "ui_autoJoin", "1"') != 1:
        raise AssertionError("MP RenderDoc helper must enable auto-join for its listen host")

    benchmark = read("tools/tests/renderer_gameplay_benchmark.py")
    for target in ("server_args", "client_args"):
        require(
            benchmark,
            f'append_set({target}, "ui_autoJoin", "1")',
            f"MP renderer benchmark {target}",
        )

    baseline = read("tools/validation/stock_asset_baseline.py")
    require(
        baseline,
        'args[restart_index:restart_index] = ("+set", "ui_autoJoin", "1")',
        "stock baseline MP roles",
    )
    require(
        baseline,
        'launch_contract["ui_autoJoin"] = "1"',
        "recorded stock baseline MP contract",
    )

    guide = read("AGENTS.md")
    require(
        guide,
        "keep an explicit `+set ui_autoJoin 1`",
        "agent MP test policy",
    )
    require(
        guide,
        "pass `+set ui_autoJoin 0`",
        "interactive join-screen launch policy",
    )


def validate_validation_coverage() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    for haystack, context in (
        (validator, "validation runner"),
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(haystack, "vscode_fast_build.py", context)


def main() -> None:
    validate_tasks()
    validate_wrapper()
    validate_launch_configs()
    validate_mp_autojoin_policy()
    validate_validation_coverage()
    print("vscode_fast_build: ok")


if __name__ == "__main__":
    main()
