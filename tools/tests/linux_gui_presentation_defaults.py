#!/usr/bin/env python3
"""Regression checks for Linux GUI presentation defaults."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


def require_int_at_least(source: str, pattern: str, minimum: int, context: str) -> int:
    match = re.search(pattern, source)
    if match is None:
        raise AssertionError(f"Missing integer pattern in {context}: {pattern}")
    value = int(match.group(1))
    if value < minimum:
        raise AssertionError(f"{context} should be at least {minimum}, got {value}")
    return value


def validate_linux_video_ram_defaults() -> None:
    linux_shared_h = read("src/sys/linux/linux_shared.h")
    unknown_vram = require_int_at_least(
        linux_shared_h,
        r"OPENQ4_LINUX_UNKNOWN_VIDEO_RAM_MB\s*=\s*(\d+)",
        128,
        "Linux unknown VRAM fallback",
    )
    max_configured = require_int_at_least(
        linux_shared_h,
        r"OPENQ4_LINUX_MAX_CONFIGURED_VIDEO_RAM_MB\s*=\s*(\d+)",
        unknown_vram,
        "Linux configurable VRAM ceiling",
    )
    require(read("src/sys/linux/local.h"), '#include "linux_shared.h"', "native Linux shared defaults")

    for relative_path in ("src/sys/linux/glimp.cpp", "src/sys/linux/linux_sdl3.cpp"):
        source = read(relative_path)
        require(source, "OPENQ4_LINUX_MAX_CONFIGURED_VIDEO_RAM_MB", f"{relative_path} sys_videoRam range")
        require(source, "OPENQ4_LINUX_UNKNOWN_VIDEO_RAM_MB", f"{relative_path} unknown VRAM fallback")
        require(source, "conservative modern VRAM setting", f"{relative_path} fallback log")
        reject(source, "default low-end VRAM setting", f"{relative_path} fallback log")
        reject(source, "run_once = 64", f"{relative_path} fallback assignment")
        reject(source, "cachedVideoRam = 64", f"{relative_path} fallback assignment")


def validate_linux_sdl3_x11_helpers_are_optional() -> None:
    linux_sdl3 = read("src/sys/linux/linux_sdl3.cpp")
    meson_sources = read("tools/build/meson_sources.py")
    meson_build = read("meson.build")
    sdl3_backend = read("src/sys/sdl3/sdl3_backend.cpp")
    linux_main = read("src/sys/linux/main.cpp")

    require(linux_sdl3, "OPENQ4_HAVE_X11_HELPERS", "SDL3 Linux optional X11 helper guard")
    require(linux_sdl3, '#include "linux_shared.h"', "SDL3 Linux shared defaults")
    reject(linux_sdl3, '#include "local.h"', "SDL3 Linux local X11 header dependency")
    reject(sdl3_backend, '../linux/local.h', "SDL3 backend local X11 header dependency")
    reject(linux_main, '#include "local.h"', "shared Linux main X11 header dependency")

    require(meson_sources, "LINUX_X11_HELPER_SOURCES", "optional Linux X11 helper sources")
    require(meson_sources, "--linux-x11-helpers", "optional Linux X11 helper source switch")
    require(meson_build, "dependency('x11', required: false)", "optional SDL3 X11 dependency")
    require(meson_build, "dependency('xext', required: false)", "optional SDL3 Xext dependency")
    require(meson_build, "-DOPENQ4_HAVE_X11_HELPERS=1", "optional X11 helper define")


def validate_gui_images_skip_downsize() -> None:
    source = read("src/renderer/ImageManager.cpp")
    require(source, "R_IsQ4PresentationImageNamespace", "presentation image namespace helper")
    require(source, '"gfx/guis/"', "GUI image namespace")
    require(source, '"fonts"', "font image namespace")
    require(source, '"newfonts"', "new font image namespace")

    call = "allowDownSize = R_AllowImageDownSizeForName( name.c_str(), allowDownSize );"
    count = source.count(call)
    if count != 3:
        raise AssertionError(f"Image manager should normalize presentation downsize policy in 3 lookup paths, got {count}")


def validate_linux_legacy_config_migration() -> None:
    source = read("src/framework/Common.cpp")
    require(source, "Common_MigrateLinuxLegacyLowVRamTexturePreset", "Linux legacy VRAM migration")
    require(source, "com_videoRam.GetInteger()", "Linux legacy VRAM migration")
    require(source, "image_downSizeLimit\" ) == 256", "legacy texture-cap signature")
    require(source, "detectedVideoRam = Sys_GetVideoRam()", "current Linux VRAM re-query")
    require(source, "com_videoRam.SetInteger( detectedVideoRam )", "Linux VRAM archive refresh")
    require(source, "image_downSizeLimit\", 0", "legacy texture-cap removal")


def validate_release_note() -> None:
    release_notes = read("docs-dev/release-completion.md")
    require(release_notes, "GUI/font presentation atlases are protected from downsizing", "release completion notes")


def main() -> None:
    validate_linux_video_ram_defaults()
    validate_linux_sdl3_x11_helpers_are_optional()
    validate_gui_images_skip_downsize()
    validate_linux_legacy_config_migration()
    validate_release_note()
    print("linux_gui_presentation_defaults: ok")


if __name__ == "__main__":
    main()
