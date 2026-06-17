#!/usr/bin/env python3
"""Validation profiles for openQ4 local pushes and pull requests."""

from __future__ import annotations

import argparse
import fnmatch
import os
import platform
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


PROFILE_DEFAULTS = {
    "push": {
        "build_dir": "builddir",
        "buildtype": "debug",
        "clean": False,
        "install": False,
    },
    "pr": {
        "build_dir": ".tmp/validation/pr-builddir",
        "buildtype": "debug",
        "clean": True,
        "install": True,
    },
}

STAGED_REQUIRED_GAME_FILES = (
    "default.cfg",
    "mod.json",
    "openq4_defaults.cfg",
    "openq4_profile_steamdeck.cfg",
)

NON_RUNTIME_PATTERNS = (
    "*.exp",
    "*.ilk",
    "*.lib",
    "*.map",
    "*.zip",
)


class ValidationError(RuntimeError):
    pass


def repo_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def host_is_windows() -> bool:
    return platform.system().lower() == "windows"


def host_is_linux() -> bool:
    return platform.system().lower() == "linux"


def host_is_macos() -> bool:
    return platform.system().lower() == "darwin"


def format_command(command: list[str]) -> str:
    if host_is_windows():
        return subprocess.list2cmdline(command)
    return " ".join(shlex.quote(part) for part in command)


def rel(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path)


def section(title: str) -> None:
    print(f"\n==> {title}", flush=True)


def run_command(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str],
    title: str,
    dry_run: bool,
) -> None:
    section(title)
    print(format_command(command), flush=True)
    if dry_run:
        return

    result = subprocess.run(command, cwd=str(cwd), env=env)
    if result.returncode != 0:
        raise ValidationError(f"{title} failed with exit code {result.returncode}.")


def capture_command(command: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=str(cwd),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def resolve_meson_wrapper(root: Path) -> list[str]:
    if host_is_windows():
        wrapper = root / "tools" / "build" / "meson_setup.ps1"
        if not wrapper.is_file():
            raise ValidationError(f"openQ4 Meson wrapper not found: {wrapper}")
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(wrapper)]

    wrapper = root / "tools" / "build" / "meson_setup.sh"
    if not wrapper.is_file():
        raise ValidationError(f"openQ4 Meson wrapper not found: {wrapper}")
    return ["bash", str(wrapper)]


def is_meson_build_dir(path: Path) -> bool:
    return (path / "meson-private" / "coredata.dat").is_file() and (path / "build.ninja").is_file()


def setup_args(args: argparse.Namespace, root: Path, build_dir: Path) -> list[str]:
    base = ["setup"]
    if args.clean or not is_meson_build_dir(build_dir):
        base += ["--wipe", str(build_dir), str(root)]
    else:
        base += ["--reconfigure", str(build_dir), str(root)]

    base += [
        "--backend",
        "ninja",
        f"--buildtype={args.buildtype}",
        "--wrap-mode=forcefallback",
    ]

    if args.platform_backend:
        base.append(f"-Dplatform_backend={args.platform_backend}")

    base.extend(args.extra_setup_arg or [])
    return base


def compile_args(args: argparse.Namespace, build_dir: Path) -> list[str]:
    command = ["compile", "-C", str(build_dir)]
    if args.jobs is not None:
        command += ["-j", str(args.jobs)]
    command.extend(args.extra_compile_arg or [])
    return command


def install_args(build_dir: Path) -> list[str]:
    return ["install", "-C", str(build_dir), "--no-rebuild", "--skip-subprojects"]


def validation_env(args: argparse.Namespace, root: Path) -> dict[str, str]:
    env = os.environ.copy()
    if args.game_libs_repo:
        env["OPENQ4_GAMELIBS_REPO"] = str(Path(args.game_libs_repo).resolve())
    elif "OPENQ4_GAMELIBS_REPO" not in env:
        default_game_libs = (root / ".." / "openQ4-GameLibs").resolve()
        env["OPENQ4_GAMELIBS_REPO"] = str(default_game_libs)

    if args.build_gamelibs:
        env["OPENQ4_BUILD_GAMELIBS"] = "1"

    if args.skip_icon_sync:
        env["OPENQ4_SKIP_ICON_SYNC"] = "1"

    return env


def run_python_tests(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    tests = [
        root / "tools" / "tests" / "hdr_postprocess_math.py",
        root / "tools" / "tests" / "linux_highdpi_mouse.py",
        root / "tools" / "tests" / "linux_vsync_support.py",
        root / "tools" / "tests" / "loading_continue_input.py",
        root / "tools" / "tests" / "macos_renderer_startup_guard.py",
        root / "tools" / "tests" / "macos_metal_bridge.py",
        root / "tools" / "tests" / "preprocessor_macro_safety.py",
        root / "tools" / "tests" / "posix_memory_management.py",
        root / "tools" / "tests" / "sdl3_input_parity.py",
        root / "tools" / "tests" / "sdl3_multidisplay_windowing.py",
        root / "tools" / "tests" / "steam_deck_support.py",
    ]
    for test_script in tests:
        if not test_script.is_file():
            raise ValidationError(f"Python validation test not found: {test_script}")
        run_command(
            [sys.executable, str(test_script)],
            cwd=root,
            env=env,
            title=f"Python check: {rel(test_script, root)}",
            dry_run=args.dry_run,
        )


def ensure_game_libs_repo(env: dict[str, str]) -> None:
    game_libs_repo = Path(env["OPENQ4_GAMELIBS_REPO"])
    expected = game_libs_repo / "src" / "game"
    if not expected.is_dir():
        raise ValidationError(
            "openQ4-GameLibs source directory was not found. "
            f"Expected: {expected}"
        )


def find_any(root: Path, patterns: tuple[str, ...]) -> list[Path]:
    matches: list[Path] = []
    for pattern in patterns:
        matches.extend(sorted(root.glob(pattern)))
    return matches


def find_engine_executables(install_root: Path, stem: str) -> list[Path]:
    suffix = ".exe" if host_is_windows() else ""
    return [
        path
        for path in sorted(install_root.glob(f"{stem}_*{suffix}"))
        if path.is_file()
    ]


def is_posix_executable(path: Path) -> bool:
    return path.is_file() and os.access(path, os.X_OK)


def require_posix_executable(path: Path, root: Path, label: str) -> None:
    if not is_posix_executable(path):
        raise ValidationError(f"{label} is not executable: {rel(path, root)}")


def validate_linux_steamdeck_launcher(path: Path, root: Path, client_candidates: list[Path]) -> None:
    try:
        launcher = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValidationError(f"Linux Steam Deck launcher is unreadable: {rel(path, root)}") from exc

    required_tokens = (
        "OPENQ4_STEAMDECK",
        "OPENQ4_FORCE_X11",
        "SDL_VIDEO_DRIVER=x11",
        "SDL_VIDEODRIVER=x11",
        "+set com_platformProfile steamdeck",
    )
    for token in required_tokens:
        if token not in launcher:
            raise ValidationError(f"Linux Steam Deck launcher is missing {token!r}: {rel(path, root)}")

    if "WAYLAND_DISPLAY" in launcher:
        raise ValidationError(f"Linux Steam Deck launcher still forces X11 from Wayland session state: {rel(path, root)}")

    client_names = {candidate.name for candidate in client_candidates}
    if not any(client_name in launcher for client_name in client_names):
        expected = ", ".join(sorted(client_names))
        raise ValidationError(
            f"Linux Steam Deck launcher does not exec a staged client binary. "
            f"Expected one of: {expected}"
        )


def desktop_entry_exec(path: Path, root: Path) -> str:
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("Exec="):
                return line[5:].strip()
    except OSError as exc:
        raise ValidationError(f"Linux desktop entry is unreadable: {rel(path, root)}") from exc
    return ""


def desktop_exec_command(exec_line: str) -> str:
    if not exec_line:
        return ""
    try:
        parts = shlex.split(exec_line, posix=True)
    except ValueError:
        parts = exec_line.split()
    return parts[0] if parts else ""


def validate_linux_launch_metadata(root: Path, install_root: Path, client_candidates: list[Path]) -> None:
    steamdeck_launcher = install_root / "openQ4-steamdeck"
    if not steamdeck_launcher.is_file():
        raise ValidationError("Linux staged payload is missing openQ4-steamdeck.")
    require_posix_executable(steamdeck_launcher, root, "Linux Steam Deck launcher")
    validate_linux_steamdeck_launcher(steamdeck_launcher, root, client_candidates)

    desktop_dir = install_root / "share" / "applications"
    desktop_entries = {
        "openq4.desktop": {path.name for path in client_candidates},
        "openq4-steamdeck.desktop": {steamdeck_launcher.name},
    }
    for filename, allowed_commands in desktop_entries.items():
        desktop_path = desktop_dir / filename
        if not desktop_path.is_file():
            raise ValidationError(f"Linux staged payload is missing desktop entry: {rel(desktop_path, root)}")

        exec_line = desktop_entry_exec(desktop_path, root)
        exec_command = desktop_exec_command(exec_line)
        if exec_command not in allowed_commands:
            allowed = ", ".join(sorted(allowed_commands))
            raise ValidationError(
                f"Linux desktop entry {rel(desktop_path, root)} points at {exec_command or '<empty>'!r}; "
                f"expected one of: {allowed}"
            )


def macos_binary_arch(path: Path, stem: str) -> str | None:
    match = re.fullmatch(rf"{re.escape(stem)}_([A-Za-z0-9_]+)", path.name)
    if match is None:
        return None
    return match.group(1)


def validate_macos_staged_metadata(
    root: Path,
    install_root: Path,
    game_dir: Path,
    client_candidates: list[Path],
    dedicated_candidates: list[Path],
) -> None:
    icon_path = install_root / "openQ4.icns"
    splash_path = install_root / "assets" / "splash" / "quake4_rt_bitmap_4001.bmp"

    if not icon_path.is_file():
        raise ValidationError(f"macOS staged payload is missing app icon: {rel(icon_path, root)}")
    if not splash_path.is_file():
        raise ValidationError(f"macOS staged payload is missing startup splash asset: {rel(splash_path, root)}")

    client_arches: set[str] = set()
    for client in client_candidates:
        arch = macos_binary_arch(client, "openQ4-client")
        if arch is None:
            raise ValidationError(f"Unexpected macOS client binary name: {rel(client, root)}")
        client_arches.add(arch)

    dedicated_arches: set[str] = set()
    for dedicated in dedicated_candidates:
        arch = macos_binary_arch(dedicated, "openQ4-ded")
        if arch is None:
            raise ValidationError(f"Unexpected macOS dedicated-server binary name: {rel(dedicated, root)}")
        dedicated_arches.add(arch)

    if client_arches != dedicated_arches:
        raise ValidationError(
            "macOS staged engine binary architecture mismatch: "
            f"clients={sorted(client_arches)} dedicated={sorted(dedicated_arches)}"
        )

    bad_game_modules = sorted(
        path
        for pattern in ("game-sp_*.dll", "game-sp_*.so", "game-mp_*.dll", "game-mp_*.so")
        for path in game_dir.glob(pattern)
    )
    if bad_game_modules:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in bad_game_modules)
        raise ValidationError(f"macOS staged payload contains non-dylib game modules:\n{formatted}")

    missing_game_modules: list[Path] = []
    for arch in sorted(client_arches):
        for module_name in (f"game-sp_{arch}.dylib", f"game-mp_{arch}.dylib"):
            module_path = game_dir / module_name
            if not module_path.is_file():
                missing_game_modules.append(module_path)

    if missing_game_modules:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in missing_game_modules)
        raise ValidationError(f"macOS staged payload is missing architecture-matched game modules:\n{formatted}")


def validate_staged_payload(root: Path, *, dry_run: bool) -> None:
    section("Validate staged .install payload")
    if dry_run:
        print("Staged payload checks skipped during dry run.", flush=True)
        return

    install_root = root / ".install"
    game_dir = install_root / "baseoq4"
    if not install_root.is_dir():
        raise ValidationError(f"Install root is missing: {install_root}")
    if not game_dir.is_dir():
        raise ValidationError(f"Staged game directory is missing: {game_dir}")

    client_candidates = find_engine_executables(install_root, "openQ4-client")
    dedicated_candidates = find_engine_executables(install_root, "openQ4-ded")
    if not client_candidates:
        raise ValidationError("Staged client executable was not found under .install/.")
    if not dedicated_candidates:
        raise ValidationError("Staged dedicated-server executable was not found under .install/.")
    if not host_is_windows():
        require_posix_executable(client_candidates[0], root, "Staged client executable")
        require_posix_executable(dedicated_candidates[0], root, "Staged dedicated-server executable")
    if host_is_linux():
        validate_linux_launch_metadata(root, install_root, client_candidates)
    if host_is_macos():
        validate_macos_staged_metadata(
            root,
            install_root,
            game_dir,
            client_candidates,
            dedicated_candidates,
        )

    sp_modules = find_any(game_dir, ("game-sp_*.dll", "game-sp_*.so", "game-sp_*.dylib"))
    mp_modules = find_any(game_dir, ("game-mp_*.dll", "game-mp_*.so", "game-mp_*.dylib"))
    if not sp_modules:
        raise ValidationError("Staged single-player game module was not found under .install/baseoq4/.")
    if not mp_modules:
        raise ValidationError("Staged multiplayer game module was not found under .install/baseoq4/.")

    for relative_name in STAGED_REQUIRED_GAME_FILES:
        required_file = game_dir / relative_name
        if not required_file.is_file():
            raise ValidationError(f"Required staged game file is missing: {rel(required_file, root)}")

    if host_is_windows():
        if not (install_root / "OpenAL32.dll").is_file():
            raise ValidationError("Windows staged payload is missing OpenAL32.dll.")

        required_symbols = (
            sorted(install_root.glob("openQ4-client_*.pdb")),
            sorted(install_root.glob("openQ4-ded_*.pdb")),
            sorted(game_dir.glob("game-sp_*.pdb")),
            sorted(game_dir.glob("game-mp_*.pdb")),
        )
        if any(len(matches) == 0 for matches in required_symbols):
            raise ValidationError("Windows staged payload is missing one or more required diagnostic PDB files.")

    bad_artifacts: list[Path] = []
    for directory in (install_root, game_dir):
        for path in directory.iterdir():
            if not path.is_file():
                continue
            if any(fnmatch.fnmatch(path.name.lower(), pattern.lower()) for pattern in NON_RUNTIME_PATTERNS):
                bad_artifacts.append(path)

    if bad_artifacts:
        formatted = "\n".join(f"  - {rel(path, root)}" for path in bad_artifacts)
        raise ValidationError(f"Non-runtime artifacts remain staged:\n{formatted}")

    print(f"Client: {rel(client_candidates[0], root)}", flush=True)
    print(f"Dedicated server: {rel(dedicated_candidates[0], root)}", flush=True)
    print(f"SP module: {rel(sp_modules[0], root)}", flush=True)
    print(f"MP module: {rel(mp_modules[0], root)}", flush=True)


def run_runtime_matrix(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    matrix_script = root / "tools" / "tests" / "renderer_validation_matrix.py"
    if not matrix_script.is_file():
        raise ValidationError(f"Renderer validation matrix not found: {matrix_script}")

    command = [
        sys.executable,
        str(matrix_script),
        "--tiers",
        args.runtime_tiers,
        "--timeout",
        str(args.runtime_timeout),
    ]
    if args.runtime_cases:
        command += ["--cases", args.runtime_cases]
    if args.runtime_basepath is not None:
        command += ["--basepath", args.runtime_basepath]
    if args.runtime_skip_official_pak_validation:
        command += ["--skip-official-pak-validation"]

    run_command(
        command,
        cwd=root,
        env=env,
        title="Optional renderer startup validation matrix",
        dry_run=args.dry_run,
    )


def check_dirty_worktree(args: argparse.Namespace, root: Path, env: dict[str, str]) -> None:
    if not args.fail_on_dirty:
        return

    result = capture_command(["git", "status", "--short"], cwd=root, env=env)
    if result.returncode != 0:
        raise ValidationError(result.stderr.strip() or "git status failed.")
    if result.stdout.strip():
        raise ValidationError("Working tree is dirty; commit, stash, or rerun without --fail-on-dirty.")


def git_value(root: Path, env: dict[str, str], *git_args: str) -> str:
    result = capture_command(["git", *git_args], cwd=root, env=env)
    if result.returncode != 0:
        return "unavailable"
    return result.stdout.strip() or "unavailable"


def describe_git_revision(root: Path, env: dict[str, str]) -> str:
    short_sha = git_value(root, env, "rev-parse", "--short", "HEAD")
    branch = git_value(root, env, "rev-parse", "--abbrev-ref", "HEAD")
    status = capture_command(["git", "status", "--short"], cwd=root, env=env)
    dirty_state = "dirty" if status.returncode == 0 and status.stdout.strip() else "clean"

    github_sha = env.get("GITHUB_SHA", "").strip()
    if github_sha:
        return f"{short_sha} ({branch}, {dirty_state}, GitHub SHA {github_sha[:12]})"

    return f"{short_sha} ({branch}, {dirty_state})"


def apply_profile_defaults(args: argparse.Namespace, root: Path) -> None:
    defaults = PROFILE_DEFAULTS[args.profile]
    if args.build_dir is None:
        args.build_dir = str(root / defaults["build_dir"])
    if args.buildtype is None:
        args.buildtype = defaults["buildtype"]
    if args.clean is None:
        args.clean = defaults["clean"]
    if args.install is None:
        args.install = defaults["install"]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=sorted(PROFILE_DEFAULTS), help="Validation profile to run.")
    parser.add_argument("--source-root", default="", help="openQ4 source root. Defaults to this script's repository.")
    parser.add_argument("--build-dir", default=None, help="Meson build directory for this validation run.")
    parser.add_argument("--buildtype", default=None, help="Meson buildtype. Profile default: push=debug, pr=debug.")
    parser.add_argument("--platform-backend", default="", help="Optional Meson platform_backend override.")
    parser.add_argument("--clean", dest="clean", action="store_true", default=None, help="Use Meson --wipe for setup.")
    parser.add_argument("--no-clean", dest="clean", action="store_false", help="Reconfigure/reuse an existing build directory.")
    parser.add_argument("--install", dest="install", action="store_true", default=None, help="Run Meson install and staged payload checks.")
    parser.add_argument("--no-install", dest="install", action="store_false", help="Skip Meson install and staged payload checks.")
    parser.add_argument("--skip-python-tests", action="store_true", help="Skip lightweight Python validation tests.")
    parser.add_argument("--skip-build", action="store_true", help="Skip Meson setup/compile/install steps.")
    parser.add_argument("--build-gamelibs", action="store_true", help="Ask the Windows Meson wrapper to build openQ4-GameLibs during compile.")
    parser.add_argument("--game-libs-repo", default="", help="Override the openQ4-GameLibs companion repository path.")
    parser.add_argument("--skip-icon-sync", action="store_true", help="Set OPENQ4_SKIP_ICON_SYNC=1 for this run.")
    parser.add_argument("--jobs", "-j", type=int, default=None, help="Parallel compile job count passed to Meson.")
    parser.add_argument("--extra-setup-arg", action="append", default=[], help="Additional argument appended to Meson setup.")
    parser.add_argument("--extra-compile-arg", action="append", default=[], help="Additional argument appended to Meson compile.")
    parser.add_argument("--runtime", action="store_true", help="Also run the safe renderer startup validation matrix after install.")
    parser.add_argument("--runtime-cases", default="", help="Comma-separated renderer validation case ids.")
    parser.add_argument("--runtime-tiers", default="auto,legacy", help="Renderer tiers for --runtime. Defaults to auto,legacy.")
    parser.add_argument("--runtime-timeout", type=int, default=60, help="Per-case renderer validation timeout.")
    parser.add_argument("--runtime-basepath", default=None, help="Quake 4 base path override for renderer validation.")
    parser.add_argument(
        "--runtime-skip-official-pak-validation",
        action="store_true",
        help="Disable official q4base PK4 validation for assetless renderer startup smoke checks.",
    )
    parser.add_argument("--fail-on-dirty", action="store_true", help="Fail when the openQ4 working tree has uncommitted changes.")
    parser.add_argument("--dry-run", action="store_true", help="Print the selected commands without executing them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = Path(args.source_root).resolve() if args.source_root else repo_root_from_script()
    apply_profile_defaults(args, root)
    build_dir = Path(args.build_dir).resolve()
    build_dir.parent.mkdir(parents=True, exist_ok=True)
    env = validation_env(args, root)
    wrapper = resolve_meson_wrapper(root)

    started = time.monotonic()
    print(f"openQ4 {args.profile} validation", flush=True)
    print(f"Source root: {root}", flush=True)
    print(f"Git revision: {describe_git_revision(root, env)}", flush=True)
    print(f"Build dir: {build_dir}", flush=True)
    print(f"Build type: {args.buildtype}", flush=True)
    print(f"GameLibs repo: {env['OPENQ4_GAMELIBS_REPO']}", flush=True)

    try:
        check_dirty_worktree(args, root, env)
        ensure_game_libs_repo(env)

        if not args.skip_python_tests:
            run_python_tests(args, root, env)

        if not args.skip_build:
            run_command(
                wrapper + setup_args(args, root, build_dir),
                cwd=root,
                env=env,
                title="Meson setup",
                dry_run=args.dry_run,
            )
            run_command(
                wrapper + compile_args(args, build_dir),
                cwd=root,
                env=env,
                title="Meson compile",
                dry_run=args.dry_run,
            )
            if args.install:
                run_command(
                    wrapper + install_args(build_dir),
                    cwd=root,
                    env=env,
                    title="Meson install",
                    dry_run=args.dry_run,
                )
                validate_staged_payload(root, dry_run=args.dry_run)

        if args.runtime:
            if not args.install and not args.dry_run:
                print("warning: --runtime uses the current .install tree because --install is disabled.", file=sys.stderr)
            run_runtime_matrix(args, root, env)

    except ValidationError as exc:
        print(f"\nvalidation failed: {exc}", file=sys.stderr, flush=True)
        return 1

    elapsed = time.monotonic() - started
    section("Validation complete")
    print(f"{args.profile} validation passed in {elapsed:.1f}s.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
