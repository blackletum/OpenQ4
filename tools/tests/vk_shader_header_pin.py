#!/usr/bin/env python3
"""Pin the committed Vulkan shader header to its GLSL sources.

The renderer-vk module embeds SPIR-V from a COMMITTED generated header so
builds never require glslang (CI runners carry no Vulkan SDK). When
glslangValidator IS available, this test regenerates the header and
byte-compares; when it is not, the test skips cleanly.
"""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
GENERATOR = REPO_ROOT / "tools" / "build" / "spirv_to_header.py"
COMMITTED = REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gui_shaders_spv.h"
SHADERS = [
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gui.vert",
    REPO_ROOT / "src" / "renderer" / "Vulkan" / "shaders" / "gui.frag",
]


def main() -> int:
    sys.path.insert(0, str(GENERATOR.parent))
    import spirv_to_header  # noqa: E402

    if spirv_to_header.find_glslang(None) is None:
        print("vk_shader_header_pin: skipped (glslangValidator not available)")
        return 0

    if not COMMITTED.is_file():
        print(f"vk_shader_header_pin: missing committed header {COMMITTED}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        regenerated = pathlib.Path(tmp) / "gui_shaders_spv.h"
        result = subprocess.run(
            [
                sys.executable,
                str(GENERATOR),
                "--header-out",
                str(regenerated),
                *[str(s) for s in SHADERS],
            ],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            print(f"vk_shader_header_pin: regeneration failed:\n{result.stdout}\n{result.stderr}", file=sys.stderr)
            return 1
        if regenerated.read_bytes() != COMMITTED.read_bytes():
            print(
                "vk_shader_header_pin: committed header is stale — regenerate with:\n"
                "  python tools/build/spirv_to_header.py --header-out src/renderer/Vulkan/shaders/gui_shaders_spv.h "
                "src/renderer/Vulkan/shaders/gui.vert src/renderer/Vulkan/shaders/gui.frag",
                file=sys.stderr,
            )
            return 1

    print("vk_shader_header_pin: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
