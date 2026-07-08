#!/usr/bin/env python3
"""Regression checks for shared SDL3 multi-display and window management."""

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


def require_before(haystack: str, first: str, second: str, context: str) -> None:
    first_index = haystack.find(first)
    second_index = haystack.find(second)
    if first_index == -1 or second_index == -1 or first_index >= second_index:
        raise AssertionError(f"Expected {first!r} before {second!r} in {context}")


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


def validate_shared_backend_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    resolve_display = function_body(source, "static sdl3DisplaySelection_t SDL3_ResolveTargetDisplay(bool warnOnInvalidScreenIndex) {")
    query_desktop = function_body(source, "static bool SDL3_QueryDesktopResolution(int *width, int *height, const char *platformName) {")
    display_list = function_body(source, "static void SDL3_PrintDisplayList(void) {")
    display_modes = function_body(source, "static void SDL3_ListDisplayModes_f(const idCmdArgs &args) {")
    virtual_bounds = function_body(source, "static bool SDL3_GetVirtualDisplayBounds(SDL_Rect &bounds) {")
    placement_bounds = function_body(source, "static bool SDL3_GetDisplayWindowedPlacementBounds(SDL_DisplayID display, SDL_Rect &bounds) {")
    position_compat = function_body(source, "static bool SDL3_SetWindowPositionCompat(int x, int y, SDL_DisplayID display, bool centerOnWayland, const char *description) {")
    viewport_display = function_body(source, "static SDL_DisplayID SDL3_ResolveViewportDisplay(void) {")
    clamp_viewport_pixel = function_body(source, "static int SDL3_ClampViewportPixel(double value, int minValue, int maxValue) {")
    display_viewport = function_body(source, "static void SDL3_UpdateDisplayViewport(SDL_DisplayID display, int windowX, int windowY, int windowWidth, int windowHeight, int pixelWidth, int pixelHeight) {")
    refresh_placement = function_body(source, "static void SDL3_RefreshWindowPlacement(void) {")
    screen_parms = function_body(source, "static bool SDL3_ApplyScreenParms(glimpParms_t parms) {")
    leave_fullscreen = function_body(source, "static bool SDL3_LeaveFullscreenAndRestoreDesktopMode(void) {")
    window_event = function_body(source, "static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")

    require(source, 'static idCVar r_screen("r_screen"', "SDL3 monitor selection cvar")
    require(source, 'static idCVar r_multiScreen("r_multiScreen"', "SDL3 multi-screen span cvar")

    require(resolve_display, "SDL_GetDisplays(&displayCount)", "SDL3 display enumeration")
    require(resolve_display, "requestedScreen >= 0", "SDL3 explicit display selection")
    require(resolve_display, "SDL_GetDisplayForWindow(s_sdlWindow)", "SDL3 current-window display fallback")
    require(resolve_display, "SDL_GetPrimaryDisplay()", "SDL3 primary display fallback")

    require(display_list, "SDL_GetDisplayBounds(display, &bounds)", "SDL3 display diagnostics bounds")
    require(display_list, "SDL_GetDisplayContentScale(display)", "SDL3 display diagnostics scale")
    require(display_list, "SDL_GetCurrentDisplayOrientation(display)", "SDL3 display diagnostics orientation")
    require(display_modes, "SDL_GetFullscreenDisplayModes(display, &modeCount)", "SDL3 mode diagnostics")
    require(init, 'cmdSystem->AddCommand("listDisplays"', "SDL3 display diagnostic command")
    require(init, 'cmdSystem->AddCommand("listDisplayModes"', "SDL3 mode diagnostic command")

    require(query_desktop, "SDL_GetDesktopDisplayMode(display)", "shared desktop-mode query")
    require(query_desktop, "SDL_GetCurrentDisplayMode(display)", "shared current-mode desktop fallback")
    require(query_desktop, "SDL_GetDisplayBounds(display, &bounds)", "shared display-bounds desktop fallback")
    require(query_desktop, "desktop display mode unavailable", "shared desktop fallback log")

    require(placement_bounds, "SDL_GetDisplayUsableBounds(display, &bounds)", "window placement usable bounds")
    require(placement_bounds, "SDL_GetDisplayBounds(display, &bounds)", "window placement display bounds fallback")
    require(virtual_bounds, "SDL_GetDisplays(&displayCount)", "virtual desktop enumeration")
    require(virtual_bounds, "bounds.w = right - left;", "virtual desktop width union")
    require(virtual_bounds, "bounds.h = bottom - top;", "virtual desktop height union")

    require(position_compat, "SDL3_IsNativeWaylandVideoDriver()", "Wayland-aware window placement")
    require(position_compat, "SDL_WINDOWPOS_CENTERED_DISPLAY(display)", "Wayland selected-display centering")
    require(position_compat, "SDL_SetWindowPosition(s_sdlWindow, x, y)", "absolute window placement")
    require(viewport_display, "r_screen.GetInteger() >= 0", "explicit selected-display UI viewport")
    require(viewport_display, "SDL3_ResolveTargetDisplay(false)", "selected viewport display resolution")
    require(viewport_display, "SDL_GetPrimaryDisplay()", "auto-display UI viewport fallback")
    require(clamp_viewport_pixel, "if (!std::isfinite(value)", "selected-display viewport finite clamp")
    require(clamp_viewport_pixel, "return static_cast<int>(value);", "selected-display viewport bounded int cast")
    require(display_viewport, "SDL_GetDisplayBounds(display, &displayBounds)", "selected-display viewport bounds")
    require(display_viewport, "const int64_t windowRight", "selected-display viewport signed-overflow guard")
    require(display_viewport, "const int64_t displayRight", "selected-display viewport signed-overflow guard")
    require(display_viewport, "SDL3_ClampViewportPixel(std::floor", "selected-display viewport floor clamp")
    require(display_viewport, "SDL3_ClampViewportPixel(std::ceil", "selected-display viewport ceil clamp")
    require(display_viewport, "glConfig.uiViewportX", "selected-display viewport x")
    require(display_viewport, "glConfig.uiViewportWidth", "selected-display viewport width")

    require(screen_parms, "SDL3_SnapshotCurrentWindowedPlacement();", "windowed placement preservation before fullscreen")
    require(screen_parms, "r_multiScreen.GetInteger() == 1", "multi-screen span request")
    require(screen_parms, "SDL3_GetVirtualDisplayBounds(bounds)", "multi-screen virtual desktop bounds")
    require(screen_parms, "native Wayland does not expose absolute multi-display window placement", "Wayland span fallback")
    require(screen_parms, "SDL_SetWindowFullscreenMode(s_sdlWindow, NULL)", "desktop fullscreen mode")
    require(screen_parms, "SDL_GetClosestFullscreenDisplayMode", "exclusive fullscreen mode selection")
    require(screen_parms, "SDL_SetWindowFullscreen(s_sdlWindow, true)", "fullscreen transition")
    require(screen_parms, "SDL_SetWindowBordered(s_sdlWindow, !useBorderlessWindow)", "borderless window transition")
    require(screen_parms, "SDL3_ConstrainWindowRectToBounds", "window restore bounds constraint")
    require(screen_parms, "SDL3_SyncWindowAfterScreenChange", "post-transition compositor sync")
    require_before(screen_parms, "SDL_SetWindowFullscreen(s_sdlWindow, true)", "SDL_ShowWindow(s_sdlWindow)", "fullscreen startup shows window only after mode transition")
    require_before(screen_parms, "Sys_DestroySplash();", "SDL_ShowWindow(s_sdlWindow)", "startup splash is destroyed at render-window handoff")
    require(leave_fullscreen, "SDL_SetWindowFullscreen(s_sdlWindow, false)", "fullscreen exit")

    require(refresh_placement, "win32.win_xpos.SetInteger(x)", "window position persistence")
    require(refresh_placement, "r_windowWidth.SetInteger(width)", "window width persistence")
    require(refresh_placement, "SDL_GetWindowSizeInPixels(s_sdlWindow, &pixelWidth, &pixelHeight)", "high-DPI drawable refresh")
    require(refresh_placement, "SDL3_UpdateDisplayViewport(SDL3_ResolveViewportDisplay()", "selected-display UI viewport for spanned windows")
    require(refresh_placement, "SDL3_UpdateFullWindowViewport", "full-window UI viewport fallback")

    require(window_event, "case SDL_EVENT_WINDOW_MOVED:", "window move handling")
    require(window_event, "case SDL_EVENT_WINDOW_DISPLAY_CHANGED:", "display migration handling")
    require(window_event, "case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:", "display scale handling")
    require(window_event, "case SDL_EVENT_WINDOW_RESIZED:", "window resize handling")

    require(init, "SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN", "hidden SDL3 render-window startup")
    reject(init, "Sys_DestroySplash();", "SDL3 startup splash must survive until render window handoff")


def validate_platform_wrappers() -> None:
    meson_sources = read("tools/build/meson_sources.py")
    win_main = read("src/sys/win32/win_main.cpp")
    win = read("src/sys/win32/win_sdl3.cpp")
    linux = read("src/sys/linux/linux_sdl3.cpp")
    macos = read("src/sys/osx/macosx_sdl3.cpp")

    require(meson_sources, '"src/sys/win32/win_sdl3.cpp"', "Windows SDL3 source selection")
    require(meson_sources, '"sys/linux/linux_sdl3.cpp"', "Linux SDL3 source selection")
    require(meson_sources, '"sys/osx/macosx_sdl3.cpp"', "macOS SDL3 source selection")
    require(meson_sources, '"sys/osx/macosx_sdl3_main.cpp"', "macOS SDL3 main source selection")

    for source, context in (
        (win, "Windows SDL3 wrapper"),
        (linux, "Linux SDL3 wrapper"),
        (macos, "macOS SDL3 wrapper"),
    ):
        require(source, '#include "../sdl3/sdl3_backend.cpp"', context)

    require(linux, 'SDL3_QueryDesktopResolution(width, height, "SDL3 Linux")', "Linux desktop-resolution parity")
    require(macos, 'SDL3_QueryDesktopResolution(width, height, "SDL3 macOS")', "macOS desktop-resolution parity")
    require(win_main, "SetProcessDpiAwarenessContext", "Windows SDL3 early DPI awareness")
    require_before(win_main, 'GetProcAddress( shcore, "SetProcessDpiAwareness" )', 'GetProcAddress( user32, "SetProcessDPIAware" )', "Windows SDL3 per-monitor DPI fallback before legacy DPI fallback")
    require_before(win_main, "Sys_SetProcessDpiAwarenessForEarlyWindows();", "Sys_CreateConsole();", "Windows SDL3 DPI awareness before splash")

    sys_display = function_body(macos, "CGDirectDisplayID Sys_DisplayToUse(void) {")
    require(sys_display, "r_screen.GetInteger()", "macOS r_screen compatibility selection")
    require(sys_display, "CGGetActiveDisplayList", "macOS active display enumeration")
    require(sys_display, "displays[requestedScreen]", "macOS selected CoreGraphics display")
    require(sys_display, "CGMainDisplayID()", "macOS primary display fallback")


def validate_ci_hooks() -> None:
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")
    renderer = read("tools/tests/renderer_validation_matrix.py")

    for source, context in (
        (validator, "validation runner"),
        (push, "push verification workflow"),
        (commit, "commit validation workflow"),
    ):
        require(source, "sdl3_multidisplay_windowing.py", context)

    require(renderer, '"id": "sdl3-wayland-display-diagnostics"', "Wayland display diagnostics runtime case")
    require(renderer, '"id": "sdl3-wayland-window-stress"', "Wayland window stress runtime case")
    require(renderer, "native Wayland SDL3 repeated window/fullscreen transition stress", "Wayland window stress runtime case")
    require(renderer, '"id": "sdl3-x11-display-diagnostics"', "X11 display diagnostics runtime case")
    require(renderer, '"+listDisplays"', "Wayland display diagnostics runtime command")
    require(renderer, '"r_windowWidth",\n                "1280"', "Wayland window stress size change")
    require(commit, "sdl3-wayland-window-stress", "Wayland CI window stress case")
    require(commit, "sdl3-wayland-display-diagnostics", "Wayland CI display diagnostics case")
    require(commit, "sdl3-x11-display-diagnostics", "X11 CI display diagnostics case")


def main() -> None:
    validate_shared_backend_contract()
    validate_platform_wrappers()
    validate_ci_hooks()
    print("sdl3_multidisplay_windowing: ok")


if __name__ == "__main__":
    main()
