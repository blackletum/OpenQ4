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
    transform = function_body(source, "static bool SDL3_BuildGuiMouseTransform(sdl3GuiMouseTransform_t &transform) {")
    refresh = function_body(source, "static void SDL3_RefreshWindowPlacement(void) {")
    consume = function_body(source, "static int SDL3_ConsumeMouseDelta(float delta, float &remainder) {")
    routed_delta = function_body(source, "static bool SDL3_UpdateRoutedMouseDelta(float menuMouseX, float menuMouseY, int &dx, int &dy) {")
    window_event = function_body(source, "static void SDL3_HandleWindowEvent(const SDL_WindowEvent &event, int eventTime) {")
    pump = function_body(source, "bool Sys_SDL_PumpEvents(void) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")

    require(hints, 'SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0"', "SDL3 unscaled relative mouse hint")
    require(hints, 'SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0"', "SDL3 relative warp filtering hint")
    require(video_hints, 'SDL_HINT_VIDEO_WAYLAND_SCALE_TO_DISPLAY, "0"', "SDL3 Wayland logical-size high-DPI hint")
    require(init, "SDL_WINDOW_HIGH_PIXEL_DENSITY", "SDL3 high pixel density window creation")

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
