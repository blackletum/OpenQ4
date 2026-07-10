#!/usr/bin/env python3
"""Regression checks for Linux ARM64 CI coverage."""

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GAME_LIBS_ROOT = Path(os.environ.get("OPENQ4_GAMELIBS_REPO", ROOT.parent / "openQ4-game")).resolve()


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def read_game_libs(relative_path: str) -> str:
    return (GAME_LIBS_ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


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
    require(source, "binutils", "push verification readelf dependency")
    require(source, "xvfb", "push verification runtime display dependency")
    require(source, "libgl1-mesa-dri", "push verification software GL runtime dependency")
    require(source, "libglx-mesa0", "push verification GLX runtime dependency")
    require(source, "xvfb-run -a bash tools/validation/validate_push.sh", "push verification runtime smoke")
    require(source, "--runtime-cases renderer-default-safety-selftest", "push verification runtime smoke")
    require(source, "sdl3-x11-display-diagnostics", "push verification X11 display diagnostics case")
    require(source, "OPENQ4_FORCE_X11=1 xvfb-run -a python tools/tests/renderer_validation_matrix.py", "push verification OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--cases sdl3-force-x11-display-diagnostics", "push verification OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--runtime-skip-official-pak-validation", "push verification assetless runtime smoke")
    require(source, "LIBGL_ALWAYS_SOFTWARE=1", "push verification software GL runtime smoke")
    require(source, "SDL_VIDEO_DRIVER=x11 SDL_VIDEODRIVER=x11", "push verification runtime display override")
    require(source, "push-${{ matrix.artifact_name }}-renderer-validation", "push verification renderer report artifact")
    require(source, "path: .tmp/renderer-validation", "push verification renderer report artifact")
    require(source, "include-hidden-files: true", "push verification hidden staging/report artifacts")
    require(source, "python tools/tests/linux_arm64_ci_coverage.py", "push script-smoke regression check")


def validate_commit_workflow() -> None:
    source = read(".github/workflows/commit-validation.yml")

    require(source, "Linux ARM64 Commit Validation", "commit validation workflow")
    require(source, "runs-on: ubuntu-24.04-arm", "commit validation workflow")
    require(source, "binutils", "commit validation readelf dependency")
    require(source, "xvfb", "commit validation runtime display dependency")
    require(source, "libgl1-mesa-dri", "commit validation software GL runtime dependency")
    require(source, "libglx-mesa0", "commit validation GLX runtime dependency")
    require(source, "xvfb-run -a bash tools/validation/validate_pr.sh", "commit validation runtime smoke")
    require(source, "--runtime-cases renderer-default-safety-selftest", "commit validation runtime smoke")
    require(source, "sdl3-x11-display-diagnostics", "commit validation X11 display diagnostics case")
    require(source, "OPENQ4_FORCE_X11=1 xvfb-run -a python tools/tests/renderer_validation_matrix.py", "commit validation OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--cases sdl3-force-x11-display-diagnostics", "commit validation OPENQ4_FORCE_X11 runtime smoke")
    require(source, "--runtime-skip-official-pak-validation", "commit validation assetless runtime smoke")
    require(source, "LIBGL_ALWAYS_SOFTWARE=1", "commit validation software GL runtime smoke")
    require(source, "SDL_VIDEO_DRIVER=x11 SDL_VIDEODRIVER=x11", "commit validation runtime display override")
    require(source, "commit-linux-arm64-renderer-validation", "commit validation renderer report artifact")
    require(source, "path: .tmp/renderer-validation", "commit validation renderer report artifact")
    require(source, "include-hidden-files: true", "commit validation hidden report artifact")
    require(source, "python tools/tests/linux_arm64_ci_coverage.py", "commit script-smoke regression check")
    require(source, "linux-wayland:", "commit validation native Wayland job")
    require(source, "Linux Wayland ${{ matrix.libdecor_label }} Commit Validation", "commit validation native Wayland job")
    require(source, "weston --backend=headless-backend.so", "commit validation native Wayland compositor")
    require(source, "SDL_VIDEO_DRIVER=wayland", "commit validation native Wayland video driver")
    require(source, "SDL_VIDEODRIVER=wayland", "commit validation native Wayland legacy video driver")
    require(source, "OPENQ4_WAYLAND_DISABLE_LIBDECOR=1", "commit validation libdecor opt-out matrix")
    require(source, "OPENQ4_WAYLAND_SYNC_WINDOW_OPS=1", "commit validation sync-window matrix")
    require(source, "sdl3-wayland-window-lifecycle", "commit validation Wayland window lifecycle case")
    require(source, "sdl3-wayland-window-stress", "commit validation Wayland window stress case")
    require(source, "sdl3-wayland-mouse-capture", "commit validation Wayland mouse capture case")
    require(source, "sdl3-wayland-mouse-capture-stress", "commit validation Wayland mouse capture stress case")
    require(source, "sdl3-wayland-display-diagnostics", "commit validation Wayland display diagnostics case")
    require(source, "commit-linux-wayland-${{ matrix.artifact_suffix }}-renderer-validation", "commit validation Wayland renderer report artifact")


def validate_linux_hardening_contract() -> None:
    meson = read("meson.build")
    validator = read("tools/validation/openq4_validate.py")

    require(meson, "linux_compile_hardening_args", "Linux compile hardening")
    require(meson, "-fstack-protector-strong", "Linux compile hardening")
    require(meson, "-D_FORTIFY_SOURCE=2", "Linux compile hardening")
    require(meson, "linux_link_hardening_args", "Linux link hardening")
    require(meson, "-Wl,-z,relro", "Linux link hardening")
    require(meson, "-Wl,-z,now", "Linux link hardening")
    require(meson, "-Wl,-z,noexecstack", "Linux link hardening")
    require(meson, "linux_executable_pie", "Linux executable PIE")
    require(meson, "pie: linux_executable_pie", "Linux executable PIE")
    require(meson, "'Linux executable PIE': linux_executable_pie", "Meson hardening summary")

    require(validator, "validate_linux_binary_hardening", "Linux staged hardening validation")
    require(validator, "readelf", "Linux staged hardening validation")
    require(validator, "GNU_RELRO", "Linux staged hardening validation")
    require(validator, "GNU_STACK", "Linux staged hardening validation")
    require(validator, "BIND_NOW", "Linux staged hardening validation")
    require(validator, "Linux binary is not PIE/ET_DYN", "Linux staged hardening validation")


def validate_runtime_flags() -> None:
    renderer = read("tools/tests/renderer_validation_matrix.py")
    runner = read("tools/validation/openq4_validate.py")

    require(renderer, '"id": "sdl3-wayland-window-lifecycle"', "renderer validation Wayland window lifecycle case")
    require(renderer, "native Wayland SDL3 window lifecycle smoke", "renderer validation Wayland window lifecycle case")
    require(renderer, '"SDL3: current video driver: wayland"', "renderer validation Wayland driver assertion")
    require(renderer, '"SDL3: native Wayland window state after fullscreen change"', "renderer validation Wayland fullscreen assertion")
    require(renderer, '"SDL3: native Wayland window state after windowed change"', "renderer validation Wayland windowed assertion")
    require(renderer, '"+vid_restart"', "renderer validation window lifecycle restart command")
    require(renderer, '"id": "sdl3-wayland-window-stress"', "renderer validation Wayland window stress case")
    require(renderer, "native Wayland SDL3 repeated window/fullscreen transition stress", "renderer validation Wayland window stress case")
    require(renderer, '"r_windowWidth",\n                "1280"', "renderer validation Wayland window stress width change")
    require(renderer, '"id": "sdl3-wayland-mouse-capture"', "renderer validation Wayland mouse capture case")
    require(renderer, "native Wayland SDL3 relative mouse capture smoke", "renderer validation Wayland mouse capture case")
    require(renderer, '"+sdl3MouseCaptureDiagnostics"', "renderer validation mouse capture command")
    require(renderer, '"SDL3 mouse capture diagnostics after activate:"', "renderer validation mouse capture activation assertion")
    require(renderer, '"relative=on"', "renderer validation relative mouse assertion")
    require(renderer, '"captured=yes"', "renderer validation relative mouse assertion")
    require(renderer, '"id": "sdl3-wayland-mouse-capture-stress"', "renderer validation Wayland mouse capture stress case")
    require(renderer, "native Wayland SDL3 repeated relative mouse capture stress", "renderer validation Wayland mouse capture stress case")
    require(renderer, '"SDL3 mouse capture diagnostics: begin repeat=4"', "renderer validation mouse capture repeat assertion")
    require(renderer, '"SDL3 mouse capture diagnostics: iteration 4/4"', "renderer validation mouse capture repeat assertion")
    require(renderer, '"id": "sdl3-wayland-display-diagnostics"', "renderer validation Wayland display diagnostics case")
    require(renderer, "native Wayland SDL3 display diagnostics smoke", "renderer validation Wayland display diagnostics case")
    require(renderer, '"+listDisplays"', "renderer validation display diagnostics command")
    require(renderer, '"SDL3: detected"', "renderer validation display enumeration assertion")
    require(renderer, '"contentScale"', "renderer validation display scale assertion")
    require(renderer, '"orientation"', "renderer validation display orientation assertion")
    require(renderer, '"selected display"', "renderer validation selected-display assertion")
    require(renderer, '"id": "sdl3-x11-display-diagnostics"', "renderer validation X11 display diagnostics case")
    require(renderer, "SDL3 X11/Xvfb fallback display diagnostics smoke", "renderer validation X11 display diagnostics case")
    require(renderer, '"SDL3: current video driver: x11"', "renderer validation X11 driver assertion")
    require(renderer, '"id": "sdl3-force-x11-display-diagnostics"', "renderer validation OPENQ4_FORCE_X11 display diagnostics case")
    require(renderer, "openQ4 XWayland fallback diagnostics smoke", "renderer validation OPENQ4_FORCE_X11 display diagnostics case")
    require(renderer, '"OPENQ4_FORCE_X11=1"', "renderer validation OPENQ4_FORCE_X11 environment assertion")
    require(renderer, '"videoDriver": "x11"', "renderer validation X11 driver-specific metadata")
    require(renderer, '"videoDriver": "wayland"', "renderer validation Wayland driver-specific metadata")
    require(renderer, "filter_driver_specific_cases", "renderer validation default driver-specific filter")
    require(renderer, "--skip-official-pak-validation", "renderer validation matrix assetless option")
    require(renderer, '"fs_validateOfficialPaks"', "renderer validation matrix startup cvar")
    require(renderer, '"g_allowAssetlessStartup"', "renderer validation matrix assetless game guard")
    require(renderer, "skipOfficialPakValidation", "renderer validation matrix report metadata")
    require(runner, "--runtime-skip-official-pak-validation", "validation profile assetless option")
    require(runner, '"--skip-official-pak-validation"', "validation profile renderer handoff")


def validate_renderer_selftest_object_lifetime() -> None:
    planner = read("src/renderer/ModernShadowPlanner.cpp")
    executor = read("src/renderer/ModernGLExecutor.cpp")

    for source, context in (
        (planner, "modern shadow planner self-tests"),
        (executor, "modern GL executor self-tests"),
    ):
        reject(source, "memset( lightDefs, 0, sizeof( lightDefs ) );", context)
    reject(planner, "memset( &lightDef, 0, sizeof( lightDef ) );", "projected shadow diagnostic self-test")


def validate_assetless_renderer_bootstrap() -> None:
    source = read("src/renderer/RenderSystem_init.cpp")

    require(source, 'FindMaterial( "_default", false )', "renderer default material stock lookup")
    require(source, "using generated internal fallback", "renderer default material assetless fallback")
    require(source, 'FindMaterial( "_default" )', "renderer default material generated fallback lookup")
    require(source, "_default material fallback not available", "renderer default material fallback fatal guard")


def validate_assetless_game_bootstrap() -> None:
    for module in ("game", "mpgame"):
        game_local = read_game_libs(f"src/{module}/Game_local.cpp")
        sys_cvar = read_game_libs(f"src/{module}/gamesys/SysCvar.cpp")
        sys_cvar_header = read_game_libs(f"src/{module}/gamesys/SysCvar.h")

        require(game_local, 'FindEntityDefDict( "aas_types", false )', f"{module} stock AAS lookup")
        require(game_local, "g_allowAssetlessStartup.GetBool()", f"{module} assetless AAS guard")
        require(game_local, "continuing without AAS because g_allowAssetlessStartup is enabled", f"{module} assetless AAS log")
        require(sys_cvar, '"g_allowAssetlessStartup"', f"{module} assetless startup cvar")
        require(sys_cvar_header, "g_allowAssetlessStartup", f"{module} assetless startup cvar declaration")


def validate_release_note() -> None:
    source = read("docs/dev/release-completion.md")

    require(source, "Linux builds and Wayland validation are harder to regress", "release completion notes")
    require(source, "OPENQ4_WAYLAND_DISABLE_LIBDECOR=1", "release completion notes")
    require(source, "Linux ARM64 is now covered by normal CI", "release completion notes")
    require(source, "assetless renderer startup smoke", "release completion notes")
    require(source, "AAS declarations", "release completion notes")


def validate_no_duplicate_jobs() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require_count(push, "windows-build:", 1, "push verification workflow")
    require_count(push, "Linux ARM64 Push Verification", 1, "push verification workflow")
    require_count(commit, "Linux ARM64 Commit Validation", 1, "commit validation workflow")
    require_count(commit, "linux-wayland:", 1, "commit validation workflow")


def main() -> None:
    validate_push_workflow()
    validate_commit_workflow()
    validate_linux_hardening_contract()
    validate_runtime_flags()
    validate_renderer_selftest_object_lifetime()
    validate_assetless_renderer_bootstrap()
    validate_assetless_game_bootstrap()
    validate_release_note()
    validate_no_duplicate_jobs()
    print("linux_arm64_ci_coverage: ok")


if __name__ == "__main__":
    main()
