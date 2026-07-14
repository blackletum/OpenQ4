#!/usr/bin/env python3
"""Regression checks for openQ4-game source staging."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
STAGE_SCRIPT = ROOT / "tools" / "build" / "stage_gamelibs.py"
MANIFEST_NAME = "openq4_gamelibs_stage_manifest.json"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def run_stage(project_root: Path, gamelibs_root: Path, stage_root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(STAGE_SCRIPT), str(project_root), str(gamelibs_root), str(stage_root)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def write_file(path: Path, data: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def make_minimal_workspace(work: Path) -> tuple[Path, Path, Path]:
    project_root = work / "openQ4"
    gamelibs_root = work / "openQ4-game"
    stage_root = project_root / ".tmp" / "gamelibs_stage"

    write_file(project_root / "src" / "idlib" / "idlib_public.h", "// idlib\n")
    write_file(project_root / "src" / "renderer" / "RenderWorld.h", "// renderer\n")
    write_file(gamelibs_root / "src" / "game" / "Game_local.cpp", "// game\n")
    write_file(gamelibs_root / "src" / "game" / "gamesys" / "SysCvar.cpp", "// cvar\n")
    write_file(gamelibs_root / "src" / "mpgame" / "Game_local.cpp", "// mpgame\n")
    write_file(gamelibs_root / "src" / "mpgame" / "gamesys" / "SysCvar.cpp", "// mp cvar\n")
    return project_root, gamelibs_root, stage_root


def validate_manifest(stage_root: Path) -> None:
    manifest_path = stage_root / MANIFEST_NAME
    if not manifest_path.is_file():
        raise AssertionError("stage manifest was not written")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    files = manifest.get("files")
    if manifest.get("format") != 1:
        raise AssertionError("unexpected stage manifest format")
    if not isinstance(files, list) or manifest.get("fileCount") != len(files):
        raise AssertionError("stage manifest file count mismatch")

    paths = {entry["path"]: entry["sha256"] for entry in files}
    for rel in (
        "src/game/Game_local.cpp",
        "src/game/gamesys/SysCvar.cpp",
        "src/mpgame/Game_local.cpp",
        "src/mpgame/gamesys/SysCvar.cpp",
        "src/idlib/idlib_public.h",
        "src/renderer/RenderWorld.h",
    ):
        staged = stage_root / rel
        if not staged.is_file():
            raise AssertionError(f"missing staged file: {rel}")
        if paths.get(rel) != sha256(staged):
            raise AssertionError(f"manifest hash mismatch for {rel}")


def validate_successful_stage(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode != 0:
        raise AssertionError(f"stage_gamelibs.py failed unexpectedly: {result.stderr}")
    if result.stdout.strip() != stage_root.resolve().as_posix():
        raise AssertionError("stage_gamelibs.py stdout should be the resolved stage root only")
    validate_manifest(stage_root)


def validate_symlink_rejection(work: Path) -> None:
    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    target = gamelibs_root / "src" / "game" / "Game_local.cpp"
    link = gamelibs_root / "src" / "game" / "linked.cpp"
    try:
        os.symlink(target, link)
    except (OSError, NotImplementedError):
        return

    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode == 0:
        raise AssertionError("stage_gamelibs.py accepted a symlink source")
    if "refusing to stage symlink" not in result.stderr:
        raise AssertionError(f"unexpected symlink rejection message: {result.stderr}")


def validate_stage_root_guard(work: Path) -> None:
    project_root, gamelibs_root, _stage_root = make_minimal_workspace(work)
    result = run_stage(project_root, gamelibs_root, work / "outside-stage")
    if result.returncode == 0:
        raise AssertionError("stage_gamelibs.py accepted a stage root outside openQ4/.tmp")
    if "stage root must be under openQ4 .tmp" not in result.stderr:
        raise AssertionError(f"unexpected stage-root guard message: {result.stderr}")


def validate_posix_wrapper_refresh(work: Path) -> None:
    bash = shutil.which("bash")
    if os.name != "posix" or bash is None:
        return

    project_root, gamelibs_root, stage_root = make_minimal_workspace(work)
    wrapper = project_root / "tools" / "build" / "meson_setup.sh"
    wrapper.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "tools" / "build" / "meson_setup.sh", wrapper)

    baseline_mtime_ns = 1_700_000_000_000_000_000
    for module_name in ("game", "mpgame"):
        for source in (gamelibs_root / "src" / module_name).rglob("*"):
            if source.is_file():
                os.utime(source, ns=(baseline_mtime_ns, baseline_mtime_ns))

    result = run_stage(project_root, gamelibs_root, stage_root)
    if result.returncode != 0:
        raise AssertionError(f"initial GameLibs stage failed: {result.stderr}")

    build_dir = project_root / "builddir"
    write_file(build_dir / "meson-private" / "coredata.dat", "test\n")
    write_file(build_dir / "build.ninja", "# test\n")
    write_file(
        build_dir / "meson-info" / "intro-buildoptions.json",
        json.dumps(
            [
                {"name": "build_engine", "value": True},
                {"name": "build_games", "value": True},
            ]
        ),
    )

    meson_log = work / "meson.log"
    fake_meson = work / "fake-meson"
    write_file(
        fake_meson,
        "#!/usr/bin/env bash\n"
        "printf '%s\\n' \"$*\" >> \"${OPENQ4_FAKE_MESON_LOG}\"\n",
    )
    fake_meson.chmod(0o755)

    env = os.environ.copy()
    env.update(
        {
            "OPENQ4_MESON": str(fake_meson),
            "OPENQ4_SKIP_ICON_SYNC": "1",
            "OPENQ4_FAKE_MESON_LOG": str(meson_log),
        }
    )

    def run_wrapper() -> list[str]:
        meson_log.unlink(missing_ok=True)
        completed = subprocess.run(
            [bash, str(wrapper), "compile", "-C", str(build_dir)],
            cwd=project_root,
            env=env,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(
                f"meson_setup.sh failed during GameLibs refresh test:\n"
                f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
            )
        return meson_log.read_text(encoding="utf-8").splitlines()

    if any(line.startswith("setup --reconfigure ") for line in run_wrapper()):
        raise AssertionError("fresh GameLibs stage caused an unnecessary Meson reconfigure")

    for module_name in ("game", "mpgame"):
        staged_files = [
            path
            for staged_module in ("game", "mpgame")
            for path in (stage_root / "src" / staged_module).rglob("*")
            if path.is_file()
        ]
        rounded_now_ns = (time.time_ns() // 1_000_000_000) * 1_000_000_000
        for staged_file in staged_files:
            os.utime(staged_file, ns=(rounded_now_ns, rounded_now_ns))

        source = gamelibs_root / "src" / module_name / "Game_local.cpp"
        original = source.read_text(encoding="utf-8")
        write_file(source, original.replace(module_name, module_name.upper(), 1))

        invocations = run_wrapper()
        if not any(line.startswith("setup --reconfigure ") for line in invocations):
            raise AssertionError(f"{module_name} source edit did not trigger a Meson reconfigure")

        result = run_stage(project_root, gamelibs_root, stage_root)
        if result.returncode != 0:
            raise AssertionError(f"GameLibs restage failed after {module_name} edit: {result.stderr}")

    (gamelibs_root / "src" / "mpgame" / "gamesys" / "SysCvar.cpp").unlink()
    invocations = run_wrapper()
    if not any(line.startswith("setup --reconfigure ") for line in invocations):
        raise AssertionError("mpgame source deletion did not trigger a Meson reconfigure")


def validate_source_contracts() -> None:
    script = STAGE_SCRIPT.read_text(encoding="utf-8")
    shell_wrapper = (ROOT / "tools" / "build" / "meson_setup.sh").read_text(encoding="utf-8")
    meson = (ROOT / "meson.build").read_text(encoding="utf-8")
    game_targets = (ROOT / "content" / "baseoq4" / "meson.build").read_text(encoding="utf-8")
    aas_file = (ROOT / "src" / "aas" / "AASFile.h").read_text(encoding="utf-8")
    precompiled = (ROOT / "src" / "idlib" / "precompiled.h").read_text(encoding="utf-8")
    validator = (ROOT / "tools" / "validation" / "openq4_validate.py").read_text(encoding="utf-8")
    building = (ROOT / "BUILDING.md").read_text(encoding="utf-8")

    required_script_tokens = (
        "MANIFEST_NAME",
        "openq4_gamelibs_stage_manifest.json",
        "refusing to stage symlink",
        "refusing to stage non-regular file",
        "stage root must be under openQ4 .tmp",
        "sha256",
        "gameLibsGitCommit",
        "gameLibsGitDirty",
        "validate_stage_manifest",
        '"mpgame": gamelibs_root / "src" / "mpgame"',
    )
    for token in required_script_tokens:
        if token not in script:
            raise AssertionError(f"missing staging script token: {token}")

    for token in (
        "test_gamelibs_stage_refresh_needed",
        '"${gamelibs_repo}/src/game"',
        '"${gamelibs_repo}/src/mpgame"',
        '"${repo_root}/.tmp/gamelibs_stage/src/game"',
        '"${repo_root}/.tmp/gamelibs_stage/src/mpgame"',
        "latest_file_mtime_ns",
        "source_latest > staged_latest",
        "run_meson setup --reconfigure",
    ):
        if token not in shell_wrapper:
            raise AssertionError(f"missing POSIX GameLibs refresh token: {token}")

    for token in (
        "openq4_gamelibs_stage_manifest.json",
        "Staged openQ4-game source manifest not found",
        "game_sp_sources = files(game_sp_absolute_paths)",
        "game_mp_sources = files(game_mp_absolute_paths)",
        "game_sources = game_sp_sources + game_mp_sources",
        "game_sp_module_defs_file",
        "game_mp_module_defs_file",
        "game_target_override_options = ['cpp_std=c++17']",
    ):
        if token not in meson:
            raise AssertionError(f"missing Meson staging contract token: {token}")

    sp_marker = "if build_games and build_game_sp"
    mp_marker = "if build_games and build_game_mp"
    if sp_marker not in game_targets or mp_marker not in game_targets:
        raise AssertionError("missing SP/MP game target blocks")
    sp_target_block, mp_target_block = game_targets.split(mp_marker, 1)
    sp_target_block = sp_target_block.split(sp_marker, 1)[1]
    for token in ("game_sp_sources", "game_sp_module_defs_file", "game_target_override_options"):
        if token not in sp_target_block:
            raise AssertionError(f"missing SP target binding: {token}")
    for token in ("game_mp_sources", "game_mp_module_defs_file"):
        if token in sp_target_block:
            raise AssertionError(f"SP target incorrectly references MP binding: {token}")
    for token in ("game_mp_sources", "game_mp_module_defs_file", "game_target_override_options", "-DGAME_MPAPI"):
        if token not in mp_target_block:
            raise AssertionError(f"missing MP target binding: {token}")
    for token in ("game_sp_sources", "game_sp_module_defs_file"):
        if token in mp_target_block:
            raise AssertionError(f"MP target incorrectly references SP binding: {token}")

    for token in (
        "#ifdef GAME_MPAPI",
        '#include "../mpgame/Game_local.h"',
        '#include "../game/Game_local.h"',
    ):
        if token not in precompiled:
            raise AssertionError(f"missing SP/MP precompiled-header routing token: {token}")

    for token in (
        "aasArea_t& GetArea(int index) { return areas[index]; }",
        "const aasArea_t& GetArea(int index) const { return areas[index]; }",
    ):
        if token not in aas_file:
            raise AssertionError(f"missing SDK-compatible AAS area accessor: {token}")

    if "gamelibs_staging.py" not in validator:
        raise AssertionError("validation runner does not include gamelibs_staging.py")
    if "source-input repository" not in building:
        raise AssertionError("BUILDING.md does not document the GameLibs source-input role")


def main() -> None:
    work = ROOT / ".tmp" / "gamelibs-staging-test"
    shutil.rmtree(work, ignore_errors=True)
    try:
        validate_successful_stage(work / "success")
        validate_symlink_rejection(work / "symlink")
        validate_stage_root_guard(work / "stage-root")
        validate_posix_wrapper_refresh(work / "posix-refresh")
        validate_source_contracts()
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("gamelibs_staging: ok")


if __name__ == "__main__":
    main()
