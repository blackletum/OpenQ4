#!/usr/bin/env python3
"""rendererVkProbe must not reload the Vulkan module while it is the active renderer.

The console command loads renderer-vk, calls its GetRenderAPI, runs the
bring-up probe, then calls the module's Shutdown and unloads it. While Vulkan is
the active renderer that load returns the live module instance -- LoadLibrary
and dlopen only raise its reference count -- so the sequence would re-run
idLib::Init under the running renderer, then free its SIMD processor and clear
its dict string pools while the renderer still uses both, and let the probe's
throwaway instance and device repoint volk's function pointers away from the
live device. The loader has to refuse before it loads anything.
"""

from __future__ import annotations

from pathlib import Path
from copy import deepcopy
from unittest.mock import Mock, patch

import renderer_validation_matrix as matrix
import renderer_vulkan_hdr_gameplay as hdr_gameplay


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def braced_block(source: str, marker: str) -> str:
    """The marker through the end of the first brace-balanced block after it."""
    start = source.find(marker)
    if start == -1:
        raise AssertionError(f"Missing {marker!r}")

    depth = 0
    for index in range(source.index("{", start), len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find the end of the block after {marker!r}")


def validate_probe_refuses_active_vulkan() -> None:
    loader = read("src/renderer/RendererModule.cpp")
    context = "R_RendererModule_RunVulkanProbe"
    probe = braced_block(loader, "bool R_RendererModule_RunVulkanProbe( bool verbose ) {")

    guard = braced_block(
        probe,
        "if ( rm_state.interfacesPublished && rm_state.status.activeApi == RENDER_MODULE_API_VULKAN ) {",
    )
    require(guard, "rendererVkProbe: the Vulkan renderer is active", f"{context} active-Vulkan refusal")
    require(guard, "return false;", f"{context} active-Vulkan refusal")

    # The load itself hands back the live instance, and GetRenderAPI and
    # Shutdown then run against it, so every one of them has to sit behind
    # the refusal.
    guard_end = probe.index(guard) + len(guard)
    for token in (
        "Sys_DLL_Load( modulePath )",
        "GetRenderAPI( &moduleImport )",
        "moduleExport->Shutdown();",
        "RM_UnloadModuleBinary( handle, completions );",
    ):
        position = probe.find(token)
        if position == -1:
            raise AssertionError(f"Missing {token!r} in {context}")
        if position < guard_end:
            raise AssertionError(f"{token!r} runs before the active-Vulkan refusal in {context}")


def validate_runtime_gate() -> None:
    """Required Vulkan coverage cannot pass by omission or disabled layers."""
    cases = matrix.build_safe_cases(("auto",))
    startup = next(case for case in cases if case["id"] == "renderer-vk-clear-startup")
    fallback = next(case for case in cases if case["id"] == "renderer-vk-device-fallback-drill")
    runtime = ROOT / ".tmp" / "unused-vulkan-probe-test"
    suffix = ".exe" if matrix.os.name == "nt" else ""
    expected_client = runtime / f"openQ4-client_{matrix.host_arch()}{suffix}"
    with patch.object(Path, "exists", autospec=True, side_effect=lambda path: path == expected_client):
        if matrix.find_client_executable(runtime) != expected_client:
            raise AssertionError("runtime override selected a client from another package")
    savepath = ROOT / ".tmp" / "unused-vulkan-probe-evidence"
    args = matrix.common_args(runtime, "probe", "", savepath, True)
    if args[args.index("fs_devpath") + 1] != str(savepath):
        raise AssertionError("runtime checks write generated files into the tested package")
    module = Mock()
    with patch.object(matrix, "vk_module_path", return_value=module):
        module.is_file.return_value = True
        with patch.object(matrix.sys, "platform", "linux"):
            if matrix.filter_vulkan_module_cases([startup, fallback], runtime) != [startup, fallback]:
                raise AssertionError("Linux Vulkan cases were silently excluded")
            matrix.validate_case_prerequisites([startup, fallback], runtime)
        module.is_file.return_value = False
        try:
            matrix.validate_case_prerequisites([startup], runtime)
        except ValueError as exc:
            if "required Vulkan module is missing" not in str(exc):
                raise
        else:
            raise AssertionError("a required missing Vulkan module did not fail the gate")

    try:
        matrix.validate_case_prerequisites([], runtime)
    except ValueError:
        pass
    else:
        raise AssertionError("an empty runtime suite passed")

    complete_log = "\n".join(alternatives[0] for alternatives in startup["checks"])
    if not matrix.evaluate_checks(complete_log, startup["checks"], {})[0]:
        raise AssertionError("complete Vulkan startup evidence was rejected")
    validated = "Vulkan: validation enabled (VK_LAYER_KHRONOS_validation, debug messenger active)"
    without_layers = complete_log.replace(validated, "Vulkan: requested validation is not active")
    if matrix.evaluate_checks(without_layers, startup["checks"], {})[0]:
        raise AssertionError("startup without active validation passed as validated")
    mrt_marker = "Vulkan MRT self-test passed (draws, mixed formats, blend masks, load, cube faces, MSAA resolves, resize and rejection)"
    if [mrt_marker] not in startup["checks"]:
        raise AssertionError("Vulkan startup no longer requires the GPU MRT exercise")
    if matrix.evaluate_checks(complete_log.replace(mrt_marker, ""), startup["checks"], {})[0]:
        raise AssertionError("startup without MRT evidence passed")

    hdr = next(case for case in cases if case["id"] == "renderer-vk-hdr-selftest")
    hdr_log = "\n".join(alternatives[0] for alternatives in hdr["checks"])
    if not matrix.evaluate_checks(hdr_log, hdr["checks"], {})[0]:
        raise AssertionError("complete HDR runtime evidence was rejected")
    for marker in (validated, hdr["checks"][-1][0]):
        if matrix.evaluate_checks(hdr_log.replace(marker, ""), hdr["checks"], {})[0]:
            raise AssertionError("HDR without real GPU fixture/validation evidence passed")
    for workflow in ("push-verification.yml", "commit-validation.yml"):
        require(read(f".github/workflows/{workflow}"), hdr["id"], f"{workflow} required HDR coverage")
    restarted = "\n".join(hdr["orderedLogChecks"])
    if matrix.evaluate_ordered_log_checks(restarted, hdr["orderedLogChecks"]):
        raise AssertionError("complete HDR device-restart sequence rejected")
    if not matrix.evaluate_ordered_log_checks(hdr["orderedLogChecks"][0], hdr["orderedLogChecks"]):
        raise AssertionError("HDR without post-restart GPU evidence passed")


def validate_recovery_gate() -> None:
    cases = {case["id"]: case for case in matrix.build_safe_cases(("auto",))}
    for stage in ("window", "surface", "swapchain", "resources"):
        case_id = f"renderer-vk-{stage}-recovery"
        case = cases[case_id]
        markers = case["orderedLogChecks"]
        complete = "\n".join(markers)
        if matrix.evaluate_ordered_log_checks(complete, markers):
            raise AssertionError(f"complete recovery sequence rejected: {case_id}")
        for missing in markers:
            if not matrix.evaluate_ordered_log_checks(complete.replace(missing, ""), markers):
                raise AssertionError(f"missing recovery evidence accepted: {case_id}: {missing}")
        premature_gl = markers.copy()
        premature_gl[1], premature_gl[4] = premature_gl[4], premature_gl[1]
        if not matrix.evaluate_ordered_log_checks("\n".join(premature_gl), markers):
            raise AssertionError(f"OpenGL before teardown accepted: {case_id}")
        if not case["requiresVulkanModule"] or not case["preservesConfig"]:
            raise AssertionError(f"recovery drill lost isolation/prerequisites: {case_id}")
        for workflow in ("push-verification.yml", "commit-validation.yml"):
            source = read(f".github/workflows/{workflow}")
            require(source, case_id, f"{workflow} required recovery coverage")


def validate_hdr_gameplay_gate() -> None:
    # Regression cases from real runs: requested MP MSAA stayed single-sample,
    # a cheat-protected readback toggle never took effect, and hidden restarts
    # silently used the desktop extent. A clean exit cannot qualify these.
    states = {}
    for name, fmt, extent, generation in (
        ("initial", "RGBA16F", "1280x720", 1),
        ("after_capture", "RGBA16F", "1280x720", 1),
        ("ldr", "LDR", "1280x720", 2),
        ("scaled", "RGBA16F", "960x540", 3),
        ("synchronous", "RGBA16F", "960x540", 3),
        ("resized", "RGBA16F", "1024x640", 1),
        ("partial", "RGBA16F", "960x600", 2),
        ("manual", "RGBA16F", "960x600", 3),
        ("disabled", "LDR", "960x600", 4),
    ):
        active = name not in ("manual", "disabled")
        states[name] = {"sceneFormat": fmt, "extent": extent, "samples": "4",
                        "generation": str(generation), "async": "0" if name == "synchronous" else "1",
                        "autoExposure": str(int(active)), "initialized": str(int(active)),
                        "average": "0.125", "target": "1.44", "exposure": "1.4",
                        "queued": "30", "completed": "29"}
    if hdr_gameplay.validate_states(states, 4):
        raise AssertionError("complete HDR gameplay sequence rejected")
    for checkpoint, field, value in (
        ("initial", "samples", "0"),
        ("disabled", "samples", "0"),
        ("synchronous", "async", "1"),
        ("resized", "extent", "2560x1440"),
        ("partial", "generation", "1"),
        ("after_capture", "exposure", "1.5"),
        ("initial", "average", "nan"),
        ("scaled", "completed", "0"),
    ):
        broken = deepcopy(states)
        broken[checkpoint][field] = value
        if not hdr_gameplay.validate_states(broken, 4):
            raise AssertionError(f"invalid HDR gameplay evidence accepted: {checkpoint}.{field}={value}")
    del states["manual"]
    if not hdr_gameplay.validate_states(states, 4):
        raise AssertionError("missing HDR gameplay checkpoint accepted")


def main() -> None:
    validate_probe_refuses_active_vulkan()
    validate_runtime_gate()
    validate_recovery_gate()
    validate_hdr_gameplay_gate()
    print("renderer_vulkan_probe_safety: ok")


if __name__ == "__main__":
    main()
