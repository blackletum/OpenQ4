#!/usr/bin/env python3
"""Regression checks for the macOS Metal bridge build contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


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


def validate_meson_contract() -> None:
    options = read("meson_options.txt")
    meson = read("meson.build")
    setup_sh = read("tools/build/meson_setup.sh")
    setup_ps1 = read("tools/build/meson_setup.ps1")

    require(options, "'macos_graphics_bridge'", "Meson options")
    require(options, "choices: ['opengl', 'metal']", "Meson options")
    require(options, "Metal-ready SDL3/Cocoa integration surface", "Meson option description")

    require(meson, "macos_graphics_bridge = get_option('macos_graphics_bridge')", "Meson bridge option")
    require(meson, "macos_graphics_bridge != 'opengl'", "non-macOS bridge guard")
    require(meson, "macos_graphics_bridge == 'metal' and platform_backend_requested != 'sdl3'", "SDL3 bridge guard")
    require(meson, "use_macos_metal_bridge", "Metal bridge build predicate")
    require(meson, "modules: ['Metal', 'QuartzCore']", "Metal bridge framework dependency")
    require(meson, "-DOPENQ4_MACOS_METAL_BRIDGE=1", "Metal bridge compile define")
    require(meson, "'macOS graphics bridge': macos_graphics_bridge", "Meson summary")

    require(setup_sh, "macos_graphics_bridge", "Bash Meson wrapper option preservation")
    require(setup_ps1, '"macos_graphics_bridge"', "PowerShell Meson wrapper option preservation")


def validate_sdl3_runtime_contract() -> None:
    source = read("src/sys/sdl3/sdl3_backend.cpp")
    hints = function_body(source, "static void SDL3_SetVideoHintDefaults(void) {")
    summary = function_body(source, "static void SDL3_PrintGraphicsBridgeSummary(void) {")
    init = function_body(source, "bool GLimp_Init(glimpParms_t parms) {")

    require(source, "OPENQ4_MACOS_METAL_BRIDGE", "SDL3 Metal bridge compile guard")
    require(source, "SDL3_IsMacOSMetalBridge", "SDL3 Metal bridge predicate")
    require(source, "macOS Metal bridge (SDL3/Cocoa host, OpenGL renderer compatibility path)", "SDL3 bridge description")

    require(hints, "SDL_HINT_VIDEO_DRIVER", "macOS Metal bridge SDL video driver hint")
    require(hints, '"cocoa"', "macOS Metal bridge SDL video driver hint")
    require(hints, "SDL_HINT_RENDER_DRIVER", "macOS Metal bridge render hint")
    require(hints, "SDL_HINT_GPU_DRIVER", "macOS Metal bridge GPU hint")
    require(hints, "SDL_HINT_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "macOS Metal bridge drawable hint")

    require(summary, "no native Metal renderer rewrite is selected", "SDL3 Metal bridge log")
    require(summary, "SDL_VIDEO_METAL_AUTO_RESIZE_DRAWABLE", "SDL3 Metal bridge hint log")
    require(init, "SDL3_PrintGraphicsBridgeSummary();", "SDL3 GL initialization")


def validate_packaging_and_release_contract() -> None:
    package = read("tools/build/package_nightly.py")
    plist = read("src/sys/osx/Info.plist")
    release = read(".github/workflows/manual-release.yml")

    require(package, "--package-suffix", "release packaging variant suffix")
    require(package, "normalize_package_suffix", "release packaging variant suffix")

    for source, context in ((package, "macOS package Info.plist"), (plist, "legacy macOS Info.plist")):
        require(source, "NSHighResolutionCapable", context)
        require(source, "NSSupportsAutomaticGraphicsSwitching", context)

    require(release, "label: macOS ARM64 OpenGL", "manual release macOS OpenGL matrix")
    require(release, "macos_graphics_bridge: opengl", "manual release OpenGL bridge matrix")
    require(release, 'package_suffix: "-opengl"', "manual release OpenGL package suffix")
    require(release, "label: macOS ARM64 Metal", "manual release macOS Metal matrix")
    require(release, "macos_graphics_bridge: metal", "manual release macOS matrix")
    require(release, 'package_suffix: "-metal"', "manual release Metal package suffix")
    require(release, "-Dmacos_graphics_bridge=${{ matrix.macos_graphics_bridge }}", "manual release setup")
    require(release, "--package-suffix \"${{ matrix.package_suffix }}\"", "manual release packaging")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-opengl.tar.gz", "manual release expected assets")
    require(release, "openq4-${{ needs.metadata.outputs.version_tag }}-macos-arm64-metal.tar.gz", "manual release expected assets")


def validate_docs_and_ci_hooks() -> None:
    building = read("BUILDING.md")
    getting_started = read("docs-user/getting-started.md")
    package_readme = read("assets/release/README.html")
    release_notes = read("docs-dev/release-completion.md")
    validator = read("tools/validation/openq4_validate.py")
    push = read(".github/workflows/push-verification.yml")
    commit = read(".github/workflows/commit-validation.yml")

    require(building, "-Dmacos_graphics_bridge=metal", "build documentation")
    require(building, "without a native renderer rewrite", "build documentation")
    require(getting_started, "separate OpenGL and Metal bridge variants", "getting started guide")
    require(package_readme, "separate OpenGL and Metal bridge variants", "release package README")
    require(release_notes, "retain the OpenGL package while adding a separate Metal bridge package", "release completion notes")

    require(validator, "macos_metal_bridge.py", "validation runner")
    require(push, "tools/tests/macos_metal_bridge.py", "push verification workflow")
    require(commit, "tools/tests/macos_metal_bridge.py", "commit validation workflow")


def main() -> None:
    validate_meson_contract()
    validate_sdl3_runtime_contract()
    validate_packaging_and_release_contract()
    validate_docs_and_ci_hooks()
    print("macos_metal_bridge: ok")


if __name__ == "__main__":
    main()
