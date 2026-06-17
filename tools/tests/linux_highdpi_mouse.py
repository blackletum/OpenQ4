#!/usr/bin/env python3
"""Regression checks for Linux SDL3 high-DPI mouse handling."""

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


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def validate_sdl3_highdpi_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    hints = function_body(source, "static void SDL3_SetMouseHintDefaults(void) {")
    video_hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    display_list = function_body(source, "static void SDL3_PrintDisplayList(void) {")
    display_orientation = function_body(source, "static const char *SDL3_DisplayOrientationName(SDL_DisplayOrientation orientation) {")
    display_mode = function_body(source, "static void SDL3_FormatDisplayMode(const SDL_DisplayMode *mode, char *buffer, int bufferSize) {")
    display_modes_command = function_body(source, "static void SDL3_ListDisplayModes_f(const idCmdArgs &args) {")
    summary = function_body(source, "static void SDL3_PrintVideoDriverSummary(void) {")
    transform = function_body(source, "static bool SDL3_BuildGuiMouseTransform(sdl3GuiMouseTransform_t &transform) {")
    refresh = function_body(source, "static void SDL3_RefreshWindowPlacement(void) {")
    consume = function_body(source, "static int SDL3_ConsumeMouseDelta(float delta, float &remainder) {")
    routed_delta = function_body(source, "static bool SDL3_UpdateRoutedMouseDelta(float menuMouseX, float menuMouseY, int &dx, int &dy) {")
    window_event = function_body(source, "static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {")
    sync_window = function_body(source, "static void SDL3_SyncWindowAfterScreenChange(const char *description) {")
    window_state = function_body(source, "static void SDL3_PrintWaylandWindowState(const char *description) {")
    screen_parms = function_body(source, "static bool SDL3_ApplyScreenParms(glimpParms_t parms) {")
    activate_mouse = function_body(source, "void IN_ActivateMouse(void) {")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")

    require(hints, 'SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0"', "SDL3 unscaled relative mouse hint")
    require(hints, 'SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0"', "SDL3 relative warp filtering hint")
    require(source, 'static bool SDL3_EnvFlagEnabled(const char *name) {', "SDL3 Linux environment flag parsing")
    require(source, 'static bool SDL3_EnvHasValue(const char *name) {', "SDL3 Linux environment override parsing")
    require(video_hints, 'SDL3_EnvFlagEnabled("OPENQ4_FORCE_X11")', "SDL3 direct XWayland fallback flag")
    require(video_hints, 'SDL_HINT_VIDEO_DRIVER, "x11"', "SDL3 direct XWayland fallback hint")
    require(video_hints, 'SDL3_EnvFlagEnabled("OPENQ4_WAYLAND_PREFER_LIBDECOR")', "SDL3 Wayland libdecor preference flag")
    require(video_hints, 'SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR, "1"', "SDL3 Wayland libdecor preference hint")
    require(video_hints, 'SDL3_EnvFlagEnabled("OPENQ4_WAYLAND_SYNC_WINDOW_OPS")', "SDL3 Wayland sync window ops flag")
    require(video_hints, 'SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS, "1"', "SDL3 Wayland sync window ops hint")
    require(video_hints, '!SDL3_EnvHasValue("SDL_VIDEO_DRIVER")', "SDL3 explicit video driver override preservation")
    require(video_hints, '!SDL3_EnvHasValue("SDL_VIDEODRIVER")', "SDL3 legacy video driver override preservation")
    require(video_hints, 'SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "0"', "SDL3 Wayland logical-size high-DPI hint")
    require(summary, "OPENQ4_FORCE_X11=%s OPENQ4_WAYLAND_PREFER_LIBDECOR=%s OPENQ4_WAYLAND_SYNC_WINDOW_OPS=%s", "SDL3 Linux video environment summary")
    require(summary, "SDL3: Wayland hints:", "SDL3 Wayland hint diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR", "SDL3 Wayland libdecor hint diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_PREFER_LIBDECOR", "SDL3 Wayland libdecor preference diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_MODE_EMULATION", "SDL3 Wayland mode emulation diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_MODE_SCALING", "SDL3 Wayland mode scaling diagnostics")
    require(summary, "SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY", "SDL3 Wayland scale diagnostics")
    require(summary, "SDL_HINT_VIDEO_SYNC_WINDOW_OPERATIONS", "SDL3 window sync hint diagnostics")
    require(display_orientation, "SDL_ORIENTATION_PORTRAIT", "SDL3 display orientation diagnostics")
    require(display_mode, "mode->pixel_density", "SDL3 display mode pixel density diagnostics")
    require(display_mode, "mode->refresh_rate_numerator", "SDL3 exact refresh numerator diagnostics")
    require(display_mode, "mode->refresh_rate_denominator", "SDL3 exact refresh denominator diagnostics")
    require(display_list, "SDL_GetDisplayContentScale(display)", "SDL3 display content scale diagnostics")
    require(display_list, "SDL_GetNaturalDisplayOrientation(display)", "SDL3 natural display orientation diagnostics")
    require(display_list, "SDL_GetCurrentDisplayOrientation(display)", "SDL3 current display orientation diagnostics")
    require(display_list, "SDL_GetDesktopDisplayMode(display)", "SDL3 desktop mode diagnostics")
    require(display_list, "SDL_GetCurrentDisplayMode(display)", "SDL3 current mode diagnostics")
    require(display_list, "contentScale %.2f", "SDL3 display scale log")
    require(display_list, "orientation %s/%s", "SDL3 display orientation log")
    require(display_modes_command, "SDL_GetDisplayContentScale(display)", "SDL3 listDisplayModes display scale diagnostics")
    require(display_modes_command, "SDL3_FormatDisplayMode(mode, modeText, sizeof(modeText))", "SDL3 listDisplayModes pixel density diagnostics")
    require(init, "SDL_WINDOW_HIGH_PIXEL_DENSITY", "SDL3 high pixel density window creation")
    require(sync_window, "SDL_SyncWindow(s_sdlWindow)", "SDL3 compositor window synchronization")
    require(window_state, "SDL_GetWindowPixelDensity(s_sdlWindow)", "SDL3 Wayland window pixel density diagnostics")
    require(window_state, "SDL_GetWindowDisplayScale(s_sdlWindow)", "SDL3 Wayland window display scale diagnostics")
    require(window_state, "native Wayland window state after %s", "SDL3 Wayland window state log")
    require(screen_parms, "SDL3_SyncWindowAfterScreenChange(parms.fullScreen ? \"fullscreen change\" : \"windowed change\")", "SDL3 screen parm compositor synchronization")
    require(screen_parms, "SDL3_RefreshWindowPlacement();", "SDL3 post-sync placement refresh")
    require(screen_parms, "SDL3_PrintWaylandWindowState(parms.fullScreen ? \"fullscreen change\" : \"windowed change\")", "SDL3 post-sync Wayland window state diagnostics")
    require(activate_mouse, "if (SDL3_IsNativeWaylandVideoDriver()) {", "SDL3 Wayland mouse capture path")
    require(activate_mouse, "SDL_SetWindowRelativeMouseMode(s_sdlWindow, true)", "SDL3 Wayland relative mouse priority")
    require(activate_mouse, "native Wayland could not confine mouse pointer; continuing with relative mouse mode", "SDL3 Wayland best-effort pointer confinement")

    require(transform, "SDL_GetWindowSize(s_sdlWindow, &windowWidth, &windowHeight)", "SDL3 GUI mouse transform")
    require(transform, "SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)", "SDL3 GUI mouse transform")
    require(transform, "transform.windowToPixelX", "SDL3 window-to-pixel mouse scale")
    require(refresh, "SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)", "SDL3 placement refresh")
    require(refresh, "glConfig.vidWidth = pixelWidth;", "SDL3 framebuffer width refresh")
    require(refresh, "glConfig.vidHeight = pixelHeight;", "SDL3 framebuffer height refresh")

    require(consume, "const float accumulated = delta + remainder;", "SDL3 fractional mouse accumulator")
    require(consume, "const int whole = static_cast<int>(accumulated);", "SDL3 fractional mouse accumulator")
    require(consume, "remainder = accumulated - static_cast<float>(whole);", "SDL3 fractional mouse accumulator")
    reject(source, "SDL3_RoundToInt", "SDL3 high-DPI mouse path")

    require(routed_delta, "SDL3_ConsumeMouseDelta(menuMouseX - previousX, s_menuMouseRemainderX)", "SDL3 routed mouse delta")
    require(routed_delta, "SDL3_ConsumeMouseDelta(menuMouseY - previousY, s_menuMouseRemainderY)", "SDL3 routed mouse delta")
    require(pump, "SDL3_ConsumeMouseDelta(event.motion.xrel, s_sdlRelativeMouseRemainderX)", "SDL3 captured relative mouse delta")
    require(pump, "SDL3_ConsumeMouseDelta(event.motion.yrel, s_sdlRelativeMouseRemainderY)", "SDL3 captured relative mouse delta")

    require(window_event, "case SDL_EVENT_WINDOW_DISPLAY_CHANGED:", "SDL3 display migration handling")
    require(window_event, "case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:", "SDL3 display scale handling")
    require(window_event, "SDL3_RefreshWindowPlacement();", "SDL3 display scale placement refresh")
    require(window_event, "SDL3_InvalidateMenuMouseRouting();", "SDL3 display scale mouse routing reset")


def validate_ci_smoke() -> None:
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    runner = read("tools/validation/openq4_validate.py")

    require(push, "tools/tests/linux_highdpi_mouse.py", "push verification workflow")
    require(push, "python tools/tests/linux_highdpi_mouse.py", "push script-smoke regression check")
    require(commit, "tools/tests/linux_highdpi_mouse.py", "commit validation workflow")
    require(commit, "python tools/tests/linux_highdpi_mouse.py", "commit script-smoke regression check")
    require(runner, "linux_highdpi_mouse.py", "validation Python tests")


def main() -> None:
    validate_sdl3_highdpi_contract()
    validate_ci_smoke()
    print("linux_highdpi_mouse: ok")


if __name__ == "__main__":
    main()
