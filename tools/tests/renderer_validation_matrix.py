#!/usr/bin/env python3
"""Run and report the openQ4 renderer validation matrix.

The default matrix is intentionally safe: it starts the staged client, runs
renderer self-tests and tier/startup probes, prints gfxInfo, then quits. Gameplay
map loads are listed in the generated report but are not launched unless a human
chooses to run them separately.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


SAFE_TIERS = ("auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46")

# Keep in sync with MAX_CONSOLE_LINES in src/framework/Common.cpp. The engine
# silently ignores any "+command" beyond this limit, which would drop "+quit"
# and leave the case running until the timeout.
ENGINE_MAX_STARTUP_COMMANDS = 64

SELFTEST_CHECKS = [
    ["RendererContextLadder self-test passed"],
    ["RendererTierSelect self-test passed"],
    ["RendererTierContract self-test passed"],
    ["RendererUpload self-test passed"],
    ["fences="],
    ["waits="],
    ["timeouts="],
    ["fallbacks="],
    ["RendererGpuTimer self-test passed", "RendererGpuTimer self-test skipped"],
    ["RendererLensFlareSettings self-test passed"],
    ["RendererLensFlareRuntime self-test passed"],
    ["RendererScenePacket self-test passed"],
    ["RendererRenderGraph self-test passed"],
    ["RendererRenderGraphResource self-test passed", "RendererRenderGraphResource self-test skipped"],
    ["RendererMaterialResourceTable self-test passed", "RendererMaterialResourceTable self-test skipped"],
    ["RendererGeometryResource self-test passed"],
    ["RendererGLStateCache self-test passed", "RendererGLStateCache self-test skipped"],
    ["textureMultiBindFallback=", "RendererGLStateCache self-test skipped"],
    ["mixedTargetFallback=1", "RendererGLStateCache self-test skipped"],
    ["RendererModernGLShaderLibrary self-test passed"],
    ["RendererModernGLDrawPlan self-test passed"],
    ["RendererModernGLSubmitPlan self-test passed"],
    ["RendererModernGLExecutor self-test passed"],
    ["noZeroUnbind=1", "RendererModernGLExecutor buffer-helper self-test skipped"],
    ["RendererModernVisibility self-test passed"],
    ["RendererShadowPlanner self-test passed"],
]

STARTUP_CHECKS = [
    ["created OpenGL context"],
    ["Selected renderer tier:"],
    ["GL context profile:"],
    ["GL context request:"],
    ["Renderer caps:"],
    ["Renderer tier contract:"],
]

MANUAL_GAMEPLAY_MATRIX = [
    {
        "id": "sp-airdefense1",
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "stock SP baseline, outdoor lighting and BSE smoke",
    },
    {
        "id": "sp-airdefense2",
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "stock SP flashlight, projected shadows, animated characters",
    },
    {
        "id": "sp-storage2",
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "indoor SP material and post-process coverage",
    },
    {
        "id": "sp-bse-heavy",
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "stress BSE effects while preserving stock assets",
    },
    {
        "id": "sp-cinematic-subview",
        "mode": "SP",
        "map": "game/mcc_landing",
        "purpose": "subviews, remote cameras, cinematic and GUI interaction",
    },
    {
        "id": "mp-q4dm1-listen",
        "mode": "MP",
        "map": "mp/q4dm1",
        "purpose": "listen-server and local-client MP renderer parity",
    },
]

DETERMINISTIC_CAPTURE_MATRIX = [
    {
        "id": "capture-startup-mainmenu",
        "mode": "SP",
        "scene": "main menu after logo skip",
        "purpose": "deterministic GUI composition, font/material atlas, and widescreen expansion",
    },
    {
        "id": "capture-renderer-visible-selftest",
        "mode": "safe startup",
        "scene": "rendererModernVisibleSelfTest",
        "purpose": "synthetic modern-visible depth/G-buffer/deferred/forward+/hybrid-scene/present composition with shadow-policy handoff",
    },
    {
        "id": "capture-renderer-compatibility-selftest",
        "mode": "safe startup",
        "scene": "rendererModernCompatibilitySelfTest",
        "purpose": "known fallback inventory for GUI/post/subview/render-demo/BSE categories",
    },
    {
        "id": "capture-sp-airdefense1-static",
        "mode": "SP",
        "scene": "game/airdefense1 fixed spawn, no input for 3 seconds",
        "purpose": "outdoor lighting, terrain decals, BSE smoke, and stock material parity",
    },
    {
        "id": "capture-lensflare-storage1-off",
        "mode": "SP",
        "scene": "game/storage1 fixed spawn with `r_lensFlare 0`",
        "purpose": "baseline image for lens-flare screenshot comparisons without flare contribution",
    },
    {
        "id": "capture-lensflare-storage1-corona",
        "mode": "SP",
        "scene": "game/storage1 fixed spawn with `r_lensFlare 1`",
        "purpose": "corona-tier lens-flare screenshot comparison against the no-flare baseline and references",
    },
    {
        "id": "capture-lensflare-storage1-high",
        "mode": "SP",
        "scene": "game/storage1 fixed spawn with `r_lensFlare 2`",
        "purpose": "high-tier corona/ghost/streak lens-flare screenshot comparison against references",
    },
]

RENDERDOC_TIER_MATRIX = [
    {
        "tier": "gl33",
        "focus": "VAO/VBO/UBO baseline, graph resources, visible-depth/G-buffer/forward+ passes",
    },
    {
        "tier": "gl41",
        "focus": "macOS-class GLSL path and GL 4.1 context fallback behavior",
    },
    {
        "tier": "gl43",
        "focus": "SSBO scene records, compute validation dispatch, indirect-command generation",
    },
    {
        "tier": "gl45",
        "focus": "DSA texture/FBO updates, persistent upload defaults, and multi-bind groups",
    },
    {
        "tier": "gl46",
        "focus": "top-tier selection plus GL SPIR-V/bindless availability reporting without default use",
    },
]

SHADER_LIBRARY_TIER_MATRIX = [
    {
        "id": "shader-lensflare-gl33",
        "tier": "gl33",
        "coverage": "GLSL 330 lens-flare accumulation/composite compile, link, exact-version lookup, and sampler reflection",
    },
    {
        "id": "shader-lensflare-gl41",
        "tier": "gl41",
        "coverage": "GLSL 330/410 lens-flare coverage for the macOS-class GL 4.1 portability floor",
    },
    {
        "id": "shader-lensflare-gl43",
        "tier": "gl43",
        "coverage": "GLSL 330/410/430 lens-flare coverage alongside GPU-driven SSBO-capable tiers",
    },
    {
        "id": "shader-lensflare-gl45",
        "tier": "gl45",
        "coverage": "GLSL 330/410/430/450 lens-flare coverage alongside low-overhead DSA-capable tiers",
    },
    {
        "id": "shader-lensflare-gl46",
        "tier": "gl46",
        "coverage": "top-tier lens-flare shader coverage with the highest selected GLSL variant and all reflected sampler bindings",
    },
]

LENS_FLARE_SIGNOFF_MATRIX = [
    {
        "platform": "Windows x64",
        "shaderCoverage": "shader-lensflare-gl33/gl41/gl43/gl45/gl46 safe cases plus the `lensflare-signoff` gameplay profile under `auto`, `gl41`, and `gl45`",
        "visualEvidence": "approved Windows reference comparisons from `.tmp\\renderer-references\\lensflare-signoff\\windows-x64` or an explicitly reviewed replacement reference bundle",
        "performanceEvidence": "`lensflare-signoff` pacing-only run with `com_maxfps 0`, `r_swapInterval 0`, wall-clock sampling, and target-machine P95/P99 thresholds",
    },
    {
        "platform": "Linux x64/arm64",
        "shaderCoverage": "assetless shader-tier coverage on supported GL tiers plus SDL3 gameplay capture on `auto` and the highest supported forced tier",
        "visualEvidence": "Linux-specific reference comparisons because Mesa/proprietary-driver output and desktop color paths are allowed to differ from Windows",
        "performanceEvidence": "uncapped pacing evidence for desktop Linux and Steam Deck profile coverage when a Deck/SteamOS target is in scope",
    },
    {
        "platform": "macOS",
        "shaderCoverage": "`shader-lensflare-gl41` and `auto` fallback coverage; GL 4.3+ cases are not expected on Apple OpenGL",
        "visualEvidence": "macOS-specific reference comparisons from `lensflare-signoff --tiers gl41,auto`",
        "performanceEvidence": "off/corona/high pacing evidence on the GL 4.1 path, with platform notes for any reduced-quality fallback",
    },
]

LONG_RUN_VALIDATION_MATRIX = [
    {
        "id": "longrun-vid-restart-10x",
        "mode": "SP",
        "purpose": "repeat `vid_restart` ten times under `r_glTier auto`, `gl33`, and the highest supported forced tier; inspect logs after each cycle",
    },
    {
        "id": "longrun-map-transition-sp",
        "mode": "SP",
        "purpose": "transition between `game/airdefense1`, `game/storage2`, and `game/medlabs` without restarting the process",
    },
    {
        "id": "longrun-mp-listen-reconnect",
        "mode": "MP",
        "purpose": "`mp/q4dm1` listen server with local client connect, disconnect, reconnect, then map restart",
    },
]

CROSS_PLATFORM_VALIDATION_MATRIX = [
    {
        "platform": "Windows x64",
        "status": "first validation target",
        "required": "safe renderer matrix, required SP/MP gameplay, upload-pressure, low-overhead-state, performance-comparison, presentation-comparison, and visual-comparison artifacts before any promotion claim",
    },
    {
        "platform": "Linux x64",
        "status": "pending after Windows artifacts are green",
        "required": "safe renderer matrix plus required gameplay on SDL3/Linux with selected tier, context profile, upload mode, timer-query availability, DSA, and multibind notes recorded",
    },
    {
        "platform": "macOS GL 4.1",
        "status": "pending before portability claims",
        "required": "assetless GL 4.1/auto validation and approved gameplay/visual evidence on the Apple OpenGL compatibility floor; GL 4.3+ and GL 4.5+ tiers are not expected",
    },
]

GAMEPLAY_BENCHMARK_HARNESS = [
    {
        "profile": "smoke",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile smoke",
        "coverage": "bounded SP gameplay smoke with screenshot, rendererBenchmarkCapture, framePacingSnapshot, gfxInfo, and zero-warning log gates",
    },
    {
        "profile": "required",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile required",
        "coverage": "all required SP maps plus the MP q4dm1 listen-server/local-client case using the selected tier and presentation settings",
    },
    {
        "profile": "tiers",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile tiers",
        "coverage": "forced auto/legacy/gl33/gl41/gl43/gl45/gl46 gameplay probes that either reach gameplay or fail closed with logged tier-contract reasons",
    },
    {
        "profile": "presentation",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile presentation",
        "coverage": "windowed/fullscreen coverage for r_swapInterval 0/1 and com_maxfps 0/120/240 while preserving uncapped high-refresh presentation behavior",
    },
    {
        "profile": "shadows",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile shadows",
        "coverage": "shadow-map correctness scenes with stencil, mapped, CSM, translucent, and debug-overlay/debug-mode presets",
    },
    {
        "profile": "shadow-regression",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile shadow-regression --reference-dir .tmp\\renderer-references\\shadow-regression\\windows-x64",
        "coverage": "bounded five-scene CSM-enabled projected, point, character/skinned, and alpha-tested shadow-map captures with optional TGA reference comparison and screenshot hashes",
    },
    {
        "profile": "lensflare",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile lensflare",
        "coverage": "lens-flare screenshot captures over stable SP scenes with off/corona/high presets and optional TGA reference comparison",
    },
    {
        "profile": "lensflare-signoff",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile lensflare-signoff",
        "coverage": "cross-platform lens-flare sign-off profile covering storage/outdoor scenes, off/corona/high presets, and auto/macOS-floor/low-overhead GL tiers for visual and pacing evidence",
    },
    {
        "profile": "upload-pressure",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile upload-pressure --sample-msec 3000",
        "coverage": "mid-term upload-pressure matrix for storage1, airdefense1, and medlabs across default, persistent, reduced-ring, minimum-frame-buffer, and map-range/subdata fallback upload variants",
    },
    {
        "profile": "low-overhead-state",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile low-overhead-state --sample-msec 3000",
        "coverage": "mid-term GL 3.3 versus GL 4.5 low-overhead state/bind comparison with detailed renderer metrics, modern executor preparation enabled, and state-cache/DSA/multibind counters promoted into reports",
    },
    {
        "profile": "graph-invalidation",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile graph-invalidation --sample-msec 3000",
        "coverage": "mid-term render-graph invalidation A/B profile for post/lens-flare/BSE-heavy scenes across default and r_rendererGraphInvalidate-armed launch variants",
    },
    {
        "profile": "performance-comparison",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile performance-comparison --sample-msec 3000",
        "coverage": "mid-term ARB2-or-better comparison profile for storage, airdefense, storage2, and medlabs scenes across default, explicit ARB2, and modern-executor-prep launch variants with CPU phase and pacing metrics promoted into reports",
    },
    {
        "profile": "presentation-comparison",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile presentation-comparison --sample-msec 3000 --pacing-only",
        "coverage": "mid-term presentation pacing profile for storage, airdefense, storage2, medlabs, and MP q4dm1 listen/client scenes across com_maxfps 0/120/240 and r_swapInterval 0/1 with avg/P50/P95/P99/max pacing promoted into reports",
    },
    {
        "profile": "visual-comparison",
        "command": "python tools\\tests\\renderer_gameplay_benchmark.py --profile visual-comparison --sample-msec 3000",
        "coverage": "mid-term screenshot comparison set for post/lens-flare, BSE-heavy, GUI/subview, dense local-light, and MP listen/client scenes with optional TGA reference comparison",
    },
]

SHADOW_CORRECTNESS_MATRIX = [
    {
        "id": "shadow-projected-airdefense2",
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "angled projected-light caster/receiver validation",
    },
    {
        "id": "shadow-point-storage2",
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "point-light face coverage and local-light receiver validation",
    },
    {
        "id": "shadow-csm-airdefense1",
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "CSM camera sweep readiness and outdoor directional coverage",
    },
    {
        "id": "shadow-cutout-storage2",
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "hashed-alpha cutout fence/grate caster validation at distance",
    },
    {
        "id": "shadow-character-airdefense2",
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "dynamic character shadow caster and receiver validation",
    },
    {
        "id": "shadow-translucent-medlabs",
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "optional translucent moment caster coverage where the selected tier supports it",
    },
]

HUMAN_REVIEW_CHECKLIST = [
    {
        "case": "sp-bse-heavy",
        "focus": "BSE-heavy effects in `game/medlabs`",
        "checks": "effect sprites/trails animate at the expected cadence, no black quads, no missing additive passes, no warning spam",
    },
    {
        "case": "sp-cinematic-subview",
        "focus": "cinematic/subview flow in `game/mcc_landing`",
        "checks": "remote-camera/subview content is visible, GUI overlays composite in the right order, cinematic handoff keeps frame pacing stable",
    },
    {
        "case": "mp-q4dm1-listen",
        "focus": "local MP listen server plus loopback client",
        "checks": "client reaches the map, player/world lighting matches host expectations, frame pacing remains uncapped when requested",
    },
]

PERF_REGRESSION_THRESHOLDS = [
    {
        "preset": "low",
        "p95Ms": 33,
        "p99Ms": 50,
        "budget": "75% screen-percentage experiment, 4x3x8 cluster-grid budget, 512 shadow-map budget, post quality 0",
    },
    {
        "preset": "baseline",
        "p95Ms": 20,
        "p99Ms": 28,
        "budget": "fixed 100% screen, 6x4x12 cluster-grid budget, 1024 shadow-map budget, post quality 1",
    },
    {
        "preset": "modern",
        "p95Ms": 16,
        "p99Ms": 24,
        "budget": "fixed 100% screen, 8x6x16 cluster-grid budget, 1024 shadow-map budget, post quality 2",
    },
    {
        "preset": "high-end",
        "p95Ms": 12,
        "p99Ms": 18,
        "budget": "fixed 100% screen, 8x6x16 cluster-grid budget, 2048 shadow-map budget, post quality 3",
    },
]

PROMOTION_EVIDENCE_REQUIRED_TOKENS = [
    "phase8=complete",
    "warnings=0",
    "visual=pass",
    "gameplay=pass",
    "renderdoc=pass",
    "perf=arb2-or-better",
    "presentation=pass",
    "rollback=pass",
    "debug=off",
]

PROMOTION_EVIDENCE_TOKEN = ";".join(PROMOTION_EVIDENCE_REQUIRED_TOKENS)

DEFAULT_PROMOTION_CRITERIA = [
    {
        "criterion": "tier",
        "required": "`r_glTier auto` selects a modern GL 3.3+ tier after driver quirks and compatibility gates are applied",
    },
    {
        "criterion": "renderer escape",
        "required": "`r_renderer best` leaves promotion available; explicit `r_renderer arb2` keeps the ARB2 bridge",
    },
    {
        "criterion": "compatibility gates",
        "required": "modern baseline features, UBOs, MRT, render graph, scene packets, and shader library readiness are available",
    },
    {
        "criterion": "fallback escape",
        "required": "the ARB2 compatibility bridge remains available for rollback and explicit user selection",
    },
    {
        "criterion": "conservative defaults",
        "required": "`r_renderer best` or explicit `r_renderer arb2` keeps ARB2 visible; modern executor, submit, visible, side-path, debug, GPU-validation, bindless, shader-reload, and auto-promotion cvars remain off in a clean startup",
    },
    {
        "criterion": "validation evidence",
        "required": "`r_rendererPromotionEvidence` contains the Phase 8 evidence token after zero-warning deterministic visual checks, required SP/MP gameplay, RenderDoc tier captures, ARB2-or-better performance, presentation, rollback, and debug-off checks pass",
    },
    {
        "criterion": "manual sign-off",
        "required": "`r_rendererModernAutoPromote 1` is set only together with a complete `r_rendererPromotionEvidence` token",
    },
]

PROMOTION_READINESS_MATRIX = [
    {
        "id": "promotion-missing-evidence-block",
        "coverage": "`r_rendererModernAutoPromote 1` without `r_rendererPromotionEvidence`",
        "expected": "default promotion remains inactive with evidenceReady=0",
    },
    {
        "id": "promotion-incomplete-evidence-block",
        "coverage": "`r_rendererModernAutoPromote 1` with a partial Phase 8 evidence token",
        "expected": "default promotion remains inactive and reports the missing evidence categories",
    },
    {
        "id": "promotion-explicit-arb2-rollback",
        "coverage": "explicit `r_renderer arb2` with a complete evidence token and auto-promote requested",
        "expected": "ARB2 remains the visible rollback path and promotion is blocked by the explicit renderer escape",
    },
    {
        "id": "promotion-debug-off-defaults",
        "coverage": "clean `r_renderer best` startup with debug/validation/experimental side paths off",
        "expected": "default safety reports conservative=1, rollback=available, and issues=none",
    },
]

WARNING_PATTERNS = {
    "snPrintfOverflow": re.compile(r"idStr::snPrintf:\s*overflow", re.IGNORECASE),
    "idStrWarning": re.compile(r"WARNING:\s+idStr", re.IGNORECASE),
    "shaderCompileOrLink": re.compile(r"(shader compile|program link).*(failed|error)|failed to compile", re.IGNORECASE),
    "glError": re.compile(r"\bGL_INVALID_[A-Z_]+|OpenGL error", re.IGNORECASE),
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("amd64", "x86_64"):
        return "x64"
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86", "i386", "i686"):
        return "x86"
    return machine


def find_client_executable(root: Path) -> Path:
    install_dir = root / ".install"
    suffix = ".exe" if os.name == "nt" else ""
    candidate_prefixes = ("openQ4-client", "openQ4-client")
    for prefix in candidate_prefixes:
        preferred = install_dir / f"{prefix}_{host_arch()}{suffix}"
        if preferred.exists():
            return preferred

    candidates: list[Path] = []
    seen: set[Path] = set()
    for prefix in candidate_prefixes:
        for candidate in sorted(install_dir.glob(f"{prefix}_*{suffix}")):
            if candidate not in seen:
                candidates.append(candidate)
                seen.add(candidate)

    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    raise FileNotFoundError(f"openQ4 client executable not found under {install_dir}")


def default_basepath() -> str:
    if os.name == "nt":
        return r"C:\Program Files (x86)\Steam\steamapps\common\Quake 4"
    return ""


def sanitize_case_id(case_id: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", case_id)


def common_args(
    root: Path,
    case_id: str,
    basepath: str,
    savepath: Path,
    skip_official_pak_validation: bool,
) -> list[str]:
    log_name = f"openq4_validation_{sanitize_case_id(case_id)}.log"
    args = [
        "+set",
        "win_allowMultipleInstances",
        "1",
        "+set",
        "logFile",
        "2",
        "+set",
        "logFileName",
        f"logs/{log_name}",
        "+set",
        "developer",
        "1",
        "+set",
        "r_fullscreen",
        "0",
        "+set",
        "g_autoScreenshot",
        "0",
        "+set",
        "r_glTier",
        "auto",
        "+set",
        "r_glDebugContext",
        "0",
        "+set",
        "r_rendererModernSubmit",
        "0",
        "+set",
        "fs_savepath",
        str(savepath),
        "+set",
        "fs_devpath",
        str(root / ".install"),
        "+set",
        "fs_game",
        "baseoq4",
    ]
    if skip_official_pak_validation:
        args += [
            "+set",
            "fs_validateOfficialPaks",
            "0",
            "+set",
            "g_allowAssetlessStartup",
            "1",
        ]
    if basepath:
        args += ["+set", "fs_basepath", basepath]
    return args


def build_safe_cases(tiers: tuple[str, ...]) -> list[dict[str, Any]]:
    selftest_commands = [
        "+set",
        "r_rendererMetrics",
        "2",
        "+set",
        "r_rendererModernExecutor",
        "1",
        "+set",
        "r_rendererModernSubmit",
        "0",
        "+rendererContextLadderSelfTest",
        "+rendererTierSelfTest",
        "+rendererTierContractSelfTest",
        "+uiFontParitySelfTest",
        "+rendererUploadSelfTest",
        "+rendererGpuTimerSelfTest",
        "+rendererLensFlareSettingsSelfTest",
        "+rendererLensFlareRuntimeSelfTest",
        "+rendererScenePacketSelfTest",
        "+rendererRenderGraphSelfTest",
        "+rendererRenderGraphResourceSelfTest",
        "+rendererMaterialResourceTableSelfTest",
        "+rendererGeometryResourceSelfTest",
        "+rendererGLStateCacheSelfTest",
        "+rendererModernGLExecutorSelfTest",
        "+rendererModernVisibilitySelfTest",
        "+rendererShadowPlannerSelfTest",
        "+gfxInfo",
    ]

    cases: list[dict[str, Any]] = [
        {
            "id": "renderer-foundation-selftests",
            "category": "selftest",
            "description": "Renderer foundation, upload, metrics, packet, graph, material, geometry, shader, draw, submit, and executor self-tests.",
            "args": selftest_commands,
            "checks": SELFTEST_CHECKS + [["Selected renderer tier:"], ["GL context request:"]],
        },
        {
            "id": "renderer-visible-depth-selftest",
            "category": "selftest",
            "description": "Opt-in graph-backed visible modern depth and shadow-depth self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisibleDepth",
                "1",
                "+rendererVisiblePathSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererVisiblePath self-test passed"],
                ["sceneDepth=1"],
                ["shadowMap=1"],
                ["overlay=1"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-gbuffer-selftest",
            "category": "selftest",
            "description": "Opt-in graph-backed opaque G-buffer self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernOpaque",
                "1",
                "+rendererGBufferSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererGBuffer self-test passed"],
                ["mrt=1"],
                ["albedo=1"],
                ["normal=1"],
                ["material=1"],
                ["emissive=1"],
                ["overlay=1"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-cluster-grid-selftest",
            "category": "selftest",
            "description": "Modern clustered light CPU binning and UBO fallback self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererClusterDebug",
                "1",
                "+rendererClusterGridSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererClusterGrid self-test passed"],
                ["lights=6"],
                ["shadowDesc="],
                ["shadowBuffer=1"],
                ["uploadedShadow="],
                ["overflow="],
                ["ubo=1"],
                ["overlay=1"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-shadow-planner-selftest",
            "category": "selftest",
            "description": "Modern shadow planner policy, budget, fallback, and clustered descriptor integration self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisibleDepth",
                "1",
                "+set",
                "r_useShadowMap",
                "1",
                "+set",
                "r_shadowMapCSM",
                "1",
                "+rendererVisiblePathSelfTest",
                "+rendererShadowPlannerSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererVisiblePath self-test passed"],
                ["ShadowMap caster admission self-test passed"],
                ["ShadowMap LOD admission self-test passed"],
                ["RendererShadowPlanner self-test passed"],
                ["RendererShadowPlanner regression coverage:"],
                ["projected=1"],
                ["point=1"],
                ["csm=1"],
                ["budgetFallback=1"],
                ["cacheReuse=1"],
                ["fairness=1"],
                ["throttleHistory=1"],
                ["casterAdmission=1"],
                ["receiverFallback=1"],
                ["arb2Parity="],
                ["projectedGate(on="],
                ["projectedTransform(pad="],
                ["sampleValidation(samples="],
                ["lod="],
                ["shadowMap=1"],
                ["projectedCSM="],
                ["mapped="],
                ["fallback="],
                ["skipped="],
                ["Modern shadow plan:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-shadow-projected-diagnostic",
            "category": "selftest",
            "description": "Synthetic flashlight/projected-light diagnostic scene logging planner classification, ARB2 cascade/atlas/clip state, and receiver shader inputs.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisibleDepth",
                "1",
                "+set",
                "r_useShadowMap",
                "1",
                "+set",
                "r_shadowMapCSM",
                "1",
                "+set",
                "r_shadowMapProjectedCSM",
                "1",
                "+rendererVisiblePathSelfTest",
                "+rendererShadowProjectedDiagnosticSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererVisiblePath self-test passed"],
                ["SM projected-diagnostic scene=synthetic-flashlight"],
                ["SM projected-diagnostic fallbackValidation("],
                ["classification=projected"],
                ["planner(map=cascade"],
                ["arb2(cascades=3"],
                ["projectedTransform(pad="],
                ["sampleValidation(samples="],
                ["fallbackValidation(reason=mixed-w-signs"],
                ["clipPlane0="],
                ["atlas0="],
                ["split0="],
                ["SM projected-diagnostic receiverInputs("],
                ["RendererShadowProjectedDiagnostic self-test passed"],
                ["projectedGate="],
                ["projectedCSM="],
                ["Modern shadow plan:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-deferred-resolve-selftest",
            "category": "selftest",
            "description": "Opt-in deferred-lite resolve over graph-backed G-buffer and clustered-light UBOs.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernDeferred",
                "1",
                "+set",
                "r_rendererModernDeferredDebug",
                "3",
                "+rendererDeferredResolveSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererDeferredResolve self-test passed"],
                ["program=1"],
                ["output=1"],
                ["resources=1"],
                ["cluster=1"],
                ["shadowTextures=1/1"],
                ["pixels="],
                ["reads="],
                ["overlay=1"],
                ["Modern GL executor:"],
                ["Modern shadow textures:"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-forward-plus-selftest",
            "category": "selftest",
            "description": "Opt-in clustered forward+ opaque, alpha-test, and transparent side-path self-test.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererForwardPlus",
                "1",
                "+rendererForwardPlusSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererForwardPlus self-test passed"],
                ["programs=1"],
                ["alphaProgram=1"],
                ["resources=1"],
                ["scene=1"],
                ["depth=1"],
                ["cluster=1"],
                ["shadowTextures=1/1"],
                ["draws="],
                ["opaque="],
                ["transparent="],
                ["reads="],
                ["Modern forward+:"],
                ["Modern shadow textures:"],
                ["modernForwardPlus req=1", "Modern forward+: cvar=1, req=1"],
                ["rendererMetrics forwardPlus(req=1", "Modern forward+: cvar=1, req=1"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-modern-visible-selftest",
            "category": "selftest",
            "description": "Opt-in hybrid visible-frame composition over modern depth, deferred-lite, forward+, HDR/post handoff, and present passes.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisible",
                "1",
                "+set",
                "r_useShadowMap",
                "1",
                "+set",
                "r_shadowMapCSM",
                "1",
                "+rendererModernVisibleSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererModernVisible self-test passed"],
                ["program=1"],
                ["resources=1"],
                ["source=1"],
                ["hybrid=1"],
                ["backBuffer=1"],
                ["shadow=1"],
                ["hdr="],
                ["postHandoff=1"],
                ["blocked=0"],
                ["composed=1"],
                ["copies=1"],
                ["postComposed=1"],
                ["depthCopies=1"],
                ["deferred=1", "deferred=0"],
                ["forward=1", "forward=0"],
                ["present=1"],
                ["Modern visible frame:"],
                # the self-test validates shadow readiness internally (shadow=1 in its
                # pass line); the post-test status line reads executor stats that the
                # self-test now resets on exit, so accept the clean state too
                ["shadowReady=1", "shadowReady=0"],
                ["shadow(mapped="],
                ["modernVisible req=1"],
                ["rendererMetrics modernVisible(req=1"],
                ["Modern forward+:"],
                ["Modern clustered lighting:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-modern-compatibility-selftest",
            "category": "selftest",
            "description": "Phase 14 command-category ownership inventory with modern fullscreen GUI readiness and explicit post/subview/render-demo/BSE fallbacks.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererModernVisible",
                "1",
                "+rendererModernCompatibilitySelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererModernCompatibility self-test passed"],
                ["inventory="],
                ["gui=1/1"],
                ["post="],
                ["subview="],
                ["demo="],
                ["bse="],
                ["blocked=1"],
                ["Modern compatibility:"],
                ["modernCompatibility ready=1"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-compatibility-gates-selftest",
            "category": "selftest",
            "description": "Phase 15 driver-quirk table and fallback-gate coverage for missing UBO, broken MRT, missing timer query, missing buffer storage, and rejected debug context.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+rendererCompatibilityGatesSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererCompatibilityGates self-test passed"],
                ["Renderer driver quirks:"],
                ["Renderer compatibility gates:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-default-promotion-selftest",
            "category": "selftest",
            "description": "Phase 8 evidence-gated default-promotion coverage for r_glTier auto, explicit ARB2 escapes, compatibility gates, legacy fallback availability, missing/incomplete/complete r_rendererPromotionEvidence, and auto-promote sign-off control.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+rendererDefaultPromotionSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererDefaultPromotion self-test passed"],
                ["Renderer default promotion:"],
                ["Renderer compatibility gates:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-default-safety-selftest",
            "category": "selftest",
            "description": "Phase 13 conservative-default safety gate for ARB2 default visibility, rollback escape, and default-off modern diagnostic side paths.",
            "args": [
                "+rendererDefaultSafetySelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererDefaultSafety self-test passed"],
                ["Renderer default safety:", "conservative=1", "rollback=available", "issues=none"],
                ["Renderer default promotion:", "active=0"],
                ["Renderer bootstrap:", "defaultVisible=ARB2"],
            ],
        },
        {
            "id": "promotion-missing-evidence-block",
            "category": "promotion-readiness",
            "description": "Auto-promotion request remains blocked when no Phase 8 evidence token is present.",
            "assetless": True,
            "args": [
                "+set",
                "r_glTier",
                "auto",
                "+set",
                "r_renderer",
                "best",
                "+set",
                "r_rendererModernAutoPromote",
                "1",
                "+set",
                "r_rendererPromotionEvidence",
                "",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS
            + [
                ["Renderer default promotion:"],
                ["cvar=1"],
                ["active=0"],
                ["evidence=0"],
                ["evidenceReady=0"],
            ],
        },
        {
            "id": "promotion-incomplete-evidence-block",
            "category": "promotion-readiness",
            "description": "Auto-promotion request remains blocked and reports missing categories when Phase 8 evidence is incomplete.",
            "assetless": True,
            "args": [
                "+set",
                "r_glTier",
                "auto",
                "+set",
                "r_renderer",
                "best",
                "+set",
                "r_rendererModernAutoPromote",
                "1",
                "+set",
                "r_rendererPromotionEvidence",
                "phase8=complete;warnings=0;visual=pass;gameplay=pass",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS
            + [
                ["Renderer default promotion:"],
                ["cvar=1"],
                ["active=0"],
                ["evidence=1"],
                ["evidenceReady=0"],
                ["evidenceMissing="],
                ["renderdoc"],
                ["perf"],
                ["presentation"],
                ["rollback"],
                ["debug"],
            ],
        },
        {
            "id": "promotion-explicit-arb2-rollback",
            "category": "promotion-readiness",
            "description": "Explicit ARB2 renderer selection remains a rollback escape even when complete evidence and auto-promotion are requested.",
            "assetless": True,
            "args": [
                "+set",
                "r_glTier",
                "auto",
                "+set",
                "r_renderer",
                "arb2",
                "+set",
                "r_rendererModernAutoPromote",
                "1",
                "+set",
                "r_rendererPromotionEvidence",
                PROMOTION_EVIDENCE_TOKEN,
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS
            + [
                ["Renderer default promotion:"],
                ["cvar=1"],
                ["active=0"],
                ["evidence=1"],
                ["evidenceReady=1"],
                ["rendererAllows=0"],
                ["reason=explicit-renderer-escape"],
                ["Renderer bootstrap:"],
                ["defaultVisible=ARB2"],
            ],
        },
        {
            "id": "promotion-debug-off-defaults",
            "category": "promotion-readiness",
            "description": "Clean default startup keeps debug, validation, bindless, shader-reload, and modern visible side paths off.",
            "assetless": True,
            "args": [
                "+set",
                "r_renderer",
                "best",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS
            + [
                ["Renderer default safety:"],
                ["renderer=best"],
                ["executor=0"],
                ["submit=0"],
                ["autoPromote=0"],
                ["promotionEvidence=0"],
                ["visible=0"],
                ["visibleDepth=0"],
                ["gpuValidation=0"],
                ["bindless=0"],
                ["shaderReload=0"],
                ["rollback=available"],
                ["issues=none"],
                ["Renderer default promotion:"],
                ["active=0"],
            ],
        },
        {
            "id": "renderer-benchmark-selftest",
            "category": "selftest",
            "description": "Phase 16 benchmark capture format, frame-time percentile, preset budget, and regression-threshold coverage.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+rendererBenchmarkSelfTest",
                "+rendererBenchmarkCapture",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererBenchmark self-test passed"],
                ["rendererBenchmark capture("],
                ["Renderer benchmark:"],
                ["Performance regression thresholds:"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-gpu-driven-selftest",
            "category": "selftest",
            "description": "GL 4.3 GPU-driven compute culling, compacted indirect command generation, CPU-reference validation, and masked multi-draw-indirect execution.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_glTier",
                "gl43",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+set",
                "r_rendererGpuValidation",
                "1",
                "+rendererGpuDrivenSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererGpuDriven self-test passed"],
                ["resources=1"],
                ["compute=1"],
                ["generated="],
                ["culled="],
                ["clusters="],
                ["mismatches=0"],
                ["readbacks=1"],
                ["indirect=1"],
                ["multiDraw="],
                ["dispatches="],
                ["rendererMetrics gpuDriven(req=1"],
                ["Modern GL executor:"],
                ["Modern visibility:"],
                ["gpuValidation=1"],
                ["Requested GL tier: gl43"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
        {
            "id": "renderer-low-overhead-selftest",
            "category": "selftest",
            "description": "GL 4.5 DSA resource allocation, persistent upload diagnostics, multi-bind texture/sampler groups, and low-overhead batch compaction.",
            "args": [
                "+set",
                "r_rendererMetrics",
                "2",
                "+set",
                "r_glTier",
                "gl45",
                "+set",
                "r_rendererModernExecutor",
                "1",
                "+rendererLowOverheadSelfTest",
                "+gfxInfo",
            ],
            "checks": [
                ["RendererLowOverhead self-test passed"],
                ["dsa=1"],
                ["multiBind=1"],
                ["textureDSA="],
                ["framebufferDSA="],
                ["textureMultiBind="],
                ["samplerMultiBind="],
                ["compactedBatches="],
                ["waits="],
                ["timeouts="],
                ["fallbacks="],
                ["rendererMetrics lowOverhead(req=1"],
                ["Modern GL low-overhead:"],
                ["Renderer graph resources:"],
                ["Requested GL tier: gl45"],
                ["Selected renderer tier:"],
                ["GL context request:"],
            ],
        },
    ]

    for shader_tier in SHADER_LIBRARY_TIER_MATRIX:
        tier = shader_tier["tier"]
        cases.append(
            {
                "id": shader_tier["id"],
                "category": "shader-tier",
                "description": shader_tier["coverage"],
                "assetless": True,
                "args": [
                    "+set",
                    "r_rendererMetrics",
                    "2",
                    "+set",
                    "r_glTier",
                    tier,
                    "+set",
                    "r_rendererModernExecutor",
                    "1",
                    "+rendererShaderLibrarySelfTest",
                    "+gfxInfo",
                ],
                "checks": [
                    ["RendererModernGLShaderLibrary self-test passed"],
                    ["Modern GL shader library: available"],
                    ["lensFlare=1/1"],
                    ["lensFlare(programs="],
                    ["lensFlarePrograms="],
                    ["lensFlareVersions="],
                    ["lensFlareSamplers="],
                    ["accum="],
                    ["composite="],
                    ["samplers="],
                    [f"Requested GL tier: {tier}"],
                    ["Selected renderer tier:"],
                    ["GL context request:"],
                ],
            }
        )

    for tier in tiers:
        case_args = [
            "+set",
            "r_glTier",
            tier,
            "+set",
            "r_rendererModernExecutor",
            "1" if tier not in ("legacy",) else "0",
            "+gfxInfo",
        ]
        cases.append(
            {
                "id": f"tier-{tier}",
                "category": "tier-startup",
                "description": f"Startup and gfxInfo probe for r_glTier {tier}.",
                "args": case_args,
                "checks": STARTUP_CHECKS + [[f"Requested GL tier: {tier}"]],
            }
        )

    cases += [
        {
            "id": "renderer-debug-output-debug-context",
            "category": "debug-startup",
            "description": "Opt-in debug-context probe that requires an actual debug context and debug-output callback registration.",
            "default": False,
            "args": [
                "+set",
                "r_glTier",
                "auto",
                "+set",
                "r_glDebugContext",
                "1",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS
            + [
                ["Renderer default safety:"],
                ["Requested GL tier: auto"],
                ["requestedDebug=1"],
                ["actualDebug=1"],
                ["OpenGL debug output callback enabled", "OpenGL ARB debug output callback enabled"],
            ],
        },
        {
            "id": "renderer-debug-output-vid-restart",
            "category": "debug-restart",
            "description": "Opt-in debug-output probe that requires callback registration before and after vid_restart.",
            "default": False,
            "args": [
                "+set",
                "r_glTier",
                "auto",
                "+set",
                "r_glDebugContext",
                "1",
                "+set",
                "r_glDebugOutput",
                "1",
                "+gfxInfo",
                "+vid_restart",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS
            + [
                ["Renderer default safety:"],
                ["Requested GL tier: auto"],
                ["requestedDebug=1"],
                ["actualDebug=1"],
                ["OpenGL debug output callback enabled", "OpenGL ARB debug output callback enabled"],
            ],
            "countChecks": [
                {
                    "label": "debug-output callback registration across vid_restart",
                    "patterns": ["OpenGL debug output callback enabled", "OpenGL ARB debug output callback enabled"],
                    "min": 2,
                },
            ],
        },
        {
            "id": "tier-gl33-debug-context",
            "category": "context-startup",
            "description": "Debug-context request path with non-debug fallback available in the ladder.",
            "args": [
                "+set",
                "r_glTier",
                "gl33",
                "+set",
                "r_glDebugContext",
                "1",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS + [["Requested GL tier: gl33"], ["requestedDebug=1"]],
        },
        {
            "id": "present-vsync0-fps0",
            "category": "presentation-startup",
            "description": "Unlocked presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "0",
                "+set",
                "com_maxfps",
                "0",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
        {
            "id": "present-vsync1-fps240",
            "category": "presentation-startup",
            "description": "High-refresh capped presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "1",
                "+set",
                "com_maxfps",
                "240",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
        {
            "id": "present-vsync1-fps120",
            "category": "presentation-startup",
            "description": "120 FPS capped presentation startup probe.",
            "args": [
                "+set",
                "r_swapInterval",
                "1",
                "+set",
                "com_maxfps",
                "120",
                "+gfxInfo",
            ],
            "checks": STARTUP_CHECKS,
        },
    ]

    return cases


def find_log(savepath: Path, log_name: str) -> Path | None:
    candidates = [
        savepath / "baseoq4" / "logs" / log_name,
        savepath / "q4base" / "logs" / log_name,
        savepath / "logs" / log_name,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def count_warning_signatures(text: str) -> dict[str, int]:
    return {name: len(pattern.findall(text)) for name, pattern in WARNING_PATTERNS.items()}


def format_warning_signatures(warnings: dict[str, int]) -> str:
    active = [f"{name}={count}" for name, count in sorted(warnings.items()) if count > 0]
    return ", ".join(active) if active else "0"


def evaluate_checks(
    text: str,
    checks: list[list[str]],
    warnings: dict[str, int],
    count_checks: list[dict[str, Any]] | None = None,
) -> tuple[bool, list[str]]:
    missing: list[str] = []
    for alternatives in checks:
        if not any(pattern in text for pattern in alternatives):
            missing.append(" or ".join(alternatives))
    for check in count_checks or []:
        patterns = check.get("patterns")
        if patterns is None:
            pattern = check.get("pattern", "")
            patterns = [pattern] if pattern else []
        count = sum(text.count(pattern) for pattern in patterns)
        min_count = int(check.get("min", 1))
        label = check.get("label", " or ".join(patterns))
        if count < min_count:
            missing.append(f"{label} >= {min_count} (found {count})")
    failed_markers = [
        "self-test failed",
        "Fatal Error",
        "could not initialize OpenGL",
        "Unable to initialize OpenGL",
    ]
    for marker in failed_markers:
        if marker in text:
            missing.append(f"unexpected marker: {marker}")
    missing += [f"warning signature: {name}={count}" for name, count in sorted(warnings.items()) if count > 0]
    return len(missing) == 0, missing


def extract_last_prefixed_value(text: str, prefix: str) -> str:
    value = ""
    for line in text.splitlines():
        if line.startswith(prefix):
            value = line[len(prefix) :].strip()
    return value


def extract_named_value(text: str, name: str) -> str:
    match = re.search(rf"(?:^|[\s,\(]){re.escape(name)}[:=]([^\s,\)]+)", text)
    return match.group(1) if match else ""


def summarize_bool_field(value: str) -> str:
    if value in ("0", "1"):
        return "yes" if value == "1" else "no"
    return value


def extract_summary(text: str) -> dict[str, str]:
    summary: dict[str, str] = {}
    for key, pattern in {
        "context": r"created OpenGL context ([^\r\n]+)",
        "selectedTier": r"Selected renderer tier:\s*([^\r\n]+)",
        "contextProfile": r"GL context profile:\s*([^\r\n]+)",
        "contextRequest": r"GL context request:\s*([^\r\n]+)",
    }.items():
        match = re.search(pattern, text)
        if match:
            summary[key] = match.group(1).strip()

    for key, prefix in {
        "rendererCaps": "Renderer caps:",
        "compatibilityGates": "Renderer compatibility gates:",
        "gpuTimers": "Renderer GPU timers:",
        "uploadManager": "Renderer upload manager:",
        "modernExecutor": "Modern GL executor:",
        "stateCache": "Modern GL state cache:",
    }.items():
        value = extract_last_prefixed_value(text, prefix)
        if value:
            summary[key] = value

    context_request = summary.get("contextRequest", "")
    summary["requestedDebug"] = extract_named_value(context_request, "requestedDebug")
    summary["actualDebug"] = extract_named_value(context_request, "actualDebug")

    renderer_caps = summary.get("rendererCaps", "")
    for field, source in {
        "capVBO": "VBO",
        "capUBO": "UBO",
        "capVAO": "VAO",
        "capMRT": "MRT",
        "capFBO": "FBO",
        "capMapRange": "map_range",
        "capSync": "sync",
        "capCompute": "compute",
        "capSSBO": "SSBO",
        "capBufferStorage": "buffer_storage",
        "capDSA": "DSA",
        "capMultiBind": "multi_bind",
        "capBindless": "bindless",
    }.items():
        value = extract_named_value(renderer_caps, source)
        if value:
            summary[field] = value

    compatibility_gates = summary.get("compatibilityGates", "")
    for field, source in {
        "gateBaseline": "baseline",
        "gateUBO": "UBO",
        "gateMRT": "MRT",
        "gateTimerQuery": "timerQuery",
        "gateBufferStorage": "bufferStorage",
        "gateLowOverhead": "lowOverhead",
        "gateDebugFallback": "debugFallback",
        "gateForcedTierSupported": "forcedTierSupported",
    }.items():
        value = extract_named_value(compatibility_gates, source)
        if value:
            summary[field] = value

    gpu_timers = summary.get("gpuTimers", "")
    if gpu_timers:
        summary["gpuTimerStatus"] = gpu_timers.split(",", 1)[0].strip()
        timer_query = extract_named_value(gpu_timers, "timerQuery")
        if timer_query:
            summary["timerQuery"] = timer_query

    upload_manager = summary.get("uploadManager", "")
    for field, source in {
        "uploadFrameStream": "frameStream",
        "uploadStaticAllocator": "staticAllocator",
        "uploadBuffers": "buffers",
        "uploadRingKB": "ring",
        "uploadPersistent": "persistent",
        "uploadMapRangeFallback": "mapRangeFallback",
        "uploadFenceWaits": "waits",
        "uploadFenceTimeouts": "timeouts",
        "uploadFenceFallbacks": "fallbacks",
        "uploadLegacyBridge": "legacyBridge",
    }.items():
        value = extract_named_value(upload_manager, source)
        if value:
            summary[field] = value

    modern_executor = summary.get("modernExecutor", "")
    for field, source in {
        "executorAvailable": "available",
        "executorGpuDriven": "gpuDriven",
        "executorSSBO": "ssbo",
        "executorLowOverhead": "lowOverhead",
        "executorDSA": "dsa",
        "executorMultiBind": "multiBind",
    }.items():
        if source == "available":
            if modern_executor:
                summary[field] = "1" if modern_executor.startswith("available") else "0"
            continue
        value = extract_named_value(modern_executor, source)
        if value:
            summary[field] = value

    return summary


def markdown_cell(value: Any) -> str:
    return str(value or "").replace("|", "\\|").replace("\r", " ").replace("\n", " ")


def summarize_capability_row(summary: dict[str, str]) -> dict[str, str]:
    return {
        "context": summary.get("contextProfile") or summary.get("context", ""),
        "request": summary.get("contextRequest", ""),
        "debug": f"{summary.get('requestedDebug', '?')}/{summary.get('actualDebug', '?')}",
        "tier": summary.get("selectedTier", ""),
        "timer": f"{summary.get('gpuTimerStatus', 'missing')} tq={summary.get('timerQuery') or summary.get('gateTimerQuery', '?')}",
        "upload": (
            f"{summary.get('uploadFrameStream', 'missing')} "
            f"ring={summary.get('uploadRingKB', '?')} "
            f"persistent={summary.get('uploadPersistent', '?')} "
            f"mapRangeFallback={summary.get('uploadMapRangeFallback', '?')}"
        ),
        "lowOverhead": (
            f"ready={summary.get('gateLowOverhead') or summary.get('executorLowOverhead', '?')} "
            f"DSA={summary.get('executorDSA') or summary.get('capDSA', '?')} "
            f"multiBind={summary.get('executorMultiBind') or summary.get('capMultiBind', '?')}"
        ),
    }


def build_platform_summaries(results: list[dict[str, Any]], metadata: dict[str, Any]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for result in results:
        summary = result.get("summary", {})
        if not summary:
            continue
        row = summarize_capability_row(summary)
        row.update(
            {
                "host": metadata.get("host", ""),
                "case": result.get("id", ""),
                "status": result.get("status", ""),
                "log": result.get("log") or result.get("stdout", ""),
            }
        )
        rows.append(row)
    return rows


def print_failure_details(result: dict[str, Any]) -> None:
    print(f"  exitCode={result['exitCode']} timedOut={int(result['timedOut'])}")
    if result["log"]:
        print(f"  log: {result['log']}")
    print(f"  stdout: {result['stdout']}")
    print(f"  stderr: {result['stderr']}")
    if result["missing"]:
        print("  missing:")
        for missing in result["missing"]:
            print(f"    - {missing}")
    tail_source = result["log"] or result["stdout"]
    if tail_source:
        tail_path = Path(tail_source)
        if tail_path.is_file():
            lines = tail_path.read_text(encoding="utf-8", errors="replace").splitlines()
            if lines:
                print("  tail:")
                for line in lines[-25:]:
                    print(f"    {line}")


def run_case(
    root: Path,
    executable: Path,
    output_dir: Path,
    savepath: Path,
    basepath: str,
    case: dict[str, Any],
    timeout_seconds: int,
    skip_official_pak_validation: bool,
) -> dict[str, Any]:
    case_id = case["id"]
    log_name = f"openq4_validation_{sanitize_case_id(case_id)}.log"
    stdout_path = output_dir / f"{sanitize_case_id(case_id)}.out.txt"
    stderr_path = output_dir / f"{sanitize_case_id(case_id)}.err.txt"
    log_path_guess = find_log(savepath, log_name)
    if log_path_guess is not None:
        log_path_guess.unlink()

    case_assetless = bool(case.get("assetless", False))
    case_basepath = "" if case_assetless else basepath
    case_skip_official_pak_validation = skip_official_pak_validation or case_assetless
    args = common_args(root, case_id, case_basepath, savepath, case_skip_official_pak_validation) + case["args"] + ["+quit"]
    startup_commands = sum(1 for arg in args if arg.startswith("+"))
    if startup_commands > ENGINE_MAX_STARTUP_COMMANDS:
        raise RuntimeError(
            f"case {case_id} passes {startup_commands} '+' startup commands; the engine keeps only the "
            f"first {ENGINE_MAX_STARTUP_COMMANDS} (MAX_CONSOLE_LINES) and would silently drop '+quit'"
        )
    started = time.time()
    timed_out = False
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            [str(executable)] + args,
            cwd=str(root / ".install"),
            stdout=stdout_file,
            stderr=stderr_file,
        )
        try:
            exit_code = process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            exit_code = process.wait(timeout=10)

    elapsed = time.time() - started
    log_path = find_log(savepath, log_name)
    log_text = ""
    case_log_path = output_dir / f"{sanitize_case_id(case_id)}.log"
    if log_path is not None:
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        case_log_path.write_text(log_text, encoding="utf-8")
    else:
        if stdout_path.exists():
            log_text += stdout_path.read_text(encoding="utf-8", errors="replace")
        if stderr_path.exists():
            log_text += "\n" + stderr_path.read_text(encoding="utf-8", errors="replace")

    warning_signatures = count_warning_signatures(log_text)
    checks_ok, missing = evaluate_checks(log_text, case["checks"], warning_signatures, case.get("countChecks"))
    ok = exit_code == 0 and not timed_out and log_path is not None and checks_ok
    return {
        "id": case_id,
        "category": case["category"],
        "description": case["description"],
        "assetless": case_assetless,
        "status": "pass" if ok else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed, 2),
        "log": str(case_log_path) if log_path is not None else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "missing": missing,
        "warningSignatures": warning_signatures,
        "summary": extract_summary(log_text),
    }


def write_reports(output_dir: Path, results: list[dict[str, Any]], metadata: dict[str, Any]) -> tuple[Path, Path]:
    report_json = output_dir / "renderer_validation_report.json"
    report_md = output_dir / "renderer_validation_report.md"

    payload = {
        "metadata": metadata,
        "results": results,
        "manualGameplayMatrix": MANUAL_GAMEPLAY_MATRIX,
        "gameplayBenchmarkHarness": GAMEPLAY_BENCHMARK_HARNESS,
        "shadowCorrectnessMatrix": SHADOW_CORRECTNESS_MATRIX,
        "humanReviewChecklist": HUMAN_REVIEW_CHECKLIST,
        "deterministicCaptureMatrix": DETERMINISTIC_CAPTURE_MATRIX,
        "renderDocTierMatrix": RENDERDOC_TIER_MATRIX,
        "shaderLibraryTierMatrix": SHADER_LIBRARY_TIER_MATRIX,
        "lensFlareSignoffMatrix": LENS_FLARE_SIGNOFF_MATRIX,
        "longRunValidationMatrix": LONG_RUN_VALIDATION_MATRIX,
        "crossPlatformValidationMatrix": CROSS_PLATFORM_VALIDATION_MATRIX,
        "capturedPlatformSummaries": build_platform_summaries(results, metadata),
        "perfRegressionThresholds": PERF_REGRESSION_THRESHOLDS,
        "promotionEvidenceGate": {
            "cvar": "r_rendererPromotionEvidence",
            "requiredTokens": PROMOTION_EVIDENCE_REQUIRED_TOKENS,
            "completeToken": PROMOTION_EVIDENCE_TOKEN,
            "autoPromoteCvar": "r_rendererModernAutoPromote",
            "status": "blocked-until-manual-evidence",
        },
        "defaultPromotionCriteria": DEFAULT_PROMOTION_CRITERIA,
        "promotionReadinessMatrix": PROMOTION_READINESS_MATRIX,
    }
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    passed = sum(1 for result in results if result["status"] == "pass")
    failed = sum(1 for result in results if result["status"] != "pass")
    lines = [
        "# Renderer Validation Matrix Report",
        "",
        f"- Generated: {metadata['generated']}",
        f"- Host: {metadata['host']}",
        f"- Executable: `{metadata['executable']}`",
        f"- Save path: `{metadata['savepath']}`",
        f"- Base path: `{metadata['basepath'] or 'not set'}`",
        f"- Automated cases: {passed} passed, {failed} failed",
        "",
        "## Automated Safe Cases",
        "",
        "| Status | Case | Category | Context | Selected Tier | Warning Signatures | Log |",
        "|---|---|---|---|---|---|---|",
    ]
    for result in results:
        summary = result["summary"]
        context = summary.get("context", summary.get("contextProfile", ""))
        selected = summary.get("selectedTier", "")
        warnings = format_warning_signatures(result.get("warningSignatures", {}))
        log = result["log"] or result["stdout"]
        lines.append(
            f"| {result['status']} | `{result['id']}` | {result['category']} | {context} | {selected} | {warnings} | `{log}` |"
        )
        if result["missing"]:
            lines.append(f"|  | missing |  | {'; '.join(result['missing'])} |  |  |  |")

    lines += [
        "",
        "## Manual Gameplay Matrix",
        "",
        "These cases are required for renderer release sign-off, but this runner does not launch them by default because map startup is currently freeze-prone in local validation.",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for manual in MANUAL_GAMEPLAY_MATRIX:
        lines.append(f"| `{manual['id']}` | {manual['mode']} | `{manual['map']}` | {manual['purpose']} |")

    lines += [
        "",
        "## Gameplay Benchmark Harness",
        "",
        "`tools/tests/renderer_gameplay_benchmark.py` is the opt-in map-loading runner for Phase 12 evidence. It launches from `.install`, enters SP maps or an MP listen server plus loopback client, waits for streaming, records `rendererBenchmarkCapture`, captures screenshots, optionally compares TGA references, and fails on renderer overflow warnings.",
        "",
        "| Profile | Command | Coverage |",
        "|---|---|---|",
    ]
    for item in GAMEPLAY_BENCHMARK_HARNESS:
        lines.append(f"| `{item['profile']}` | `{item['command']}` | {item['coverage']} |")

    lines += [
        "",
        "## Shadow Correctness Matrix",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for item in SHADOW_CORRECTNESS_MATRIX:
        lines.append(f"| `{item['id']}` | {item['mode']} | `{item['map']}` | {item['purpose']} |")

    lines += [
        "",
        "## Human Review Checklist",
        "",
        "| Case | Focus | Checks |",
        "|---|---|---|",
    ]
    for item in HUMAN_REVIEW_CHECKLIST:
        lines.append(f"| `{item['case']}` | {item['focus']} | {item['checks']} |")

    lines += [
        "",
        "## Deterministic Capture Matrix",
        "",
        "| Case | Mode | Scene | Purpose |",
        "|---|---|---|---|",
    ]
    for capture in DETERMINISTIC_CAPTURE_MATRIX:
        lines.append(f"| `{capture['id']}` | {capture['mode']} | {capture['scene']} | {capture['purpose']} |")

    lines += [
        "",
        "## RenderDoc Tier Matrix",
        "",
        "| Forced Tier | Capture Focus |",
        "|---|---|",
    ]
    for item in RENDERDOC_TIER_MATRIX:
        lines.append(f"| `r_glTier {item['tier']}` | {item['focus']} |")

    lines += [
        "",
        "## Shader Library Tier Matrix",
        "",
        "| Case | Forced Tier | Coverage |",
        "|---|---|---|",
    ]
    for item in SHADER_LIBRARY_TIER_MATRIX:
        lines.append(f"| `{item['id']}` | `r_glTier {item['tier']}` | {item['coverage']} |")

    lines += [
        "",
        "## Lens Flare Cross-Platform Sign-Off",
        "",
        "`tools/tests/renderer_gameplay_benchmark.py --profile lensflare-signoff` is the repeatable map-loading profile for final lens-flare visual and pacing evidence. It must be run on each target platform with platform-specific screenshot references before release notes claim cross-platform visual parity.",
        "",
        "| Platform | Shader Coverage | Visual Evidence | Performance Evidence |",
        "|---|---|---|---|",
    ]
    for item in LENS_FLARE_SIGNOFF_MATRIX:
        lines.append(
            f"| {item['platform']} | {item['shaderCoverage']} | {item['visualEvidence']} | {item['performanceEvidence']} |"
        )

    lines += [
        "",
        "## Long-Run Matrix",
        "",
        "| Case | Mode | Purpose |",
        "|---|---|---|",
    ]
    for item in LONG_RUN_VALIDATION_MATRIX:
        lines.append(f"| `{item['id']}` | {item['mode']} | {item['purpose']} |")

    platform_summaries = build_platform_summaries(results, metadata)
    lines += [
        "",
        "## Captured Platform Capability Summary",
        "",
        "Rows are extracted from `gfxInfo` output in completed automated startup cases. Use this table as the first cross-platform note bundle; pending target platforms still require their own runs.",
        "",
        "| Case | Status | Host | Context | Debug Req/Actual | Tier | Timer | Upload | Low-Overhead | Log |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    if platform_summaries:
        for row in platform_summaries:
            lines.append(
                f"| `{row['case']}` | {row['status']} | {markdown_cell(row['host'])} | "
                f"{markdown_cell(row['context'])} | {markdown_cell(row['debug'])} | "
                f"`{markdown_cell(row['tier'])}` | {markdown_cell(row['timer'])} | "
                f"{markdown_cell(row['upload'])} | {markdown_cell(row['lowOverhead'])} | "
                f"`{markdown_cell(row['log'])}` |"
            )
    else:
        lines.append("| not-run | not-run |  |  |  |  |  |  |  |  |")

    lines += [
        "",
        "## Cross-Platform Validation Status",
        "",
        "| Platform | Status | Required Evidence Before Promotion Claims |",
        "|---|---|---|",
    ]
    for item in CROSS_PLATFORM_VALIDATION_MATRIX:
        lines.append(f"| {item['platform']} | {item['status']} | {item['required']} |")

    lines += [
        "",
        "## Performance Regression Thresholds",
        "",
        "| Preset | P95 Budget | P99 Budget | Budget Shape |",
        "|---|---:|---:|---|",
    ]
    for item in PERF_REGRESSION_THRESHOLDS:
        lines.append(f"| `{item['preset']}` | {item['p95Ms']} ms | {item['p99Ms']} ms | {item['budget']} |")

    lines += [
        "",
        "## Promotion Evidence Gate",
        "",
        "`r_rendererModernAutoPromote 1` is ignored by the engine unless `r_rendererPromotionEvidence` carries a complete Phase 8 evidence token.",
        "",
        "Required token:",
        "",
        f"`{PROMOTION_EVIDENCE_TOKEN}`",
        "",
        "| Token | Meaning |",
        "|---|---|",
        "| `phase8=complete` | The Phase 8 evidence bundle has been reviewed as a single promotion candidate. |",
        "| `warnings=0` | Renderer validation, gameplay, and benchmark logs are free of renderer warning/fatal/signature failures. |",
        "| `visual=pass` | Deterministic captures and human visual checks pass for materials, characters, GUI, post, fog/blend, BSE, and shadow cases. |",
        "| `gameplay=pass` | Required SP maps and the MP q4dm1 listen/local-client case reach gameplay and pass log/screenshot gates. |",
        "| `renderdoc=pass` | Required GL-tier RenderDoc captures show named resources and expected pass contents. |",
        "| `perf=arb2-or-better` | Modern candidate P95/P99 frame time is ARB2-or-better for target scenes and presets. |",
        "| `presentation=pass` | High-refresh and vsync presentation cases preserve uncapped rendering with 60 Hz simulation. |",
        "| `rollback=pass` | `r_renderer arb2`, `r_glTier legacy`, and modern-disable rollback commands work after modern-visible frames. |",
        "| `debug=off` | Promotion does not depend on debug-only overlays, validation readbacks, bindless experiments, or shader reload. |",
    ]

    results_by_id = {result["id"]: result for result in results}
    lines += [
        "",
        "## Promotion Readiness Probes",
        "",
        "| Case | Status | Coverage | Expected Signal | Log |",
        "|---|---|---|---|---|",
    ]
    for item in PROMOTION_READINESS_MATRIX:
        result = results_by_id.get(item["id"])
        if result is None:
            status = "not-run"
            log = ""
        else:
            status = result["status"]
            log = result["log"] or result["stdout"]
        lines.append(f"| `{item['id']}` | {status} | {item['coverage']} | {item['expected']} | `{log}` |")

    lines += [
        "",
        "## Default Promotion Criteria",
        "",
        "| Criterion | Required Evidence |",
        "|---|---|",
    ]
    for item in DEFAULT_PROMOTION_CRITERIA:
        lines.append(f"| {item['criterion']} | {item['required']} |")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tiers", default=",".join(SAFE_TIERS), help="Comma-separated r_glTier startup probes.")
    parser.add_argument("--cases", default="", help="Comma-separated automated safe case ids to run. Defaults to all cases.")
    parser.add_argument("--timeout", type=int, default=60, help="Per-case timeout in seconds.")
    parser.add_argument("--basepath", default=default_basepath(), help="Quake 4 install/base path. Omit or set empty to skip fs_basepath.")
    parser.add_argument("--savepath", default="", help="Save path root. Defaults to <repo>/.home.")
    parser.add_argument("--output-dir", default="", help="Report/output directory. Defaults to <repo>/.tmp/renderer-validation/<timestamp>.")
    parser.add_argument(
        "--skip-official-pak-validation",
        action="store_true",
        help="Disable official q4base PK4 validation for assetless engine-startup smoke checks.",
    )
    parser.add_argument("--list", action="store_true", help="List automated and manual cases without running them.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    root = repo_root()
    tiers = tuple(tier.strip() for tier in args.tiers.split(",") if tier.strip())
    safe_cases = build_safe_cases(tiers)
    requested_cases = tuple(case_id.strip() for case_id in args.cases.split(",") if case_id.strip())
    if requested_cases:
        available = {case["id"] for case in safe_cases}
        missing_cases = sorted(case_id for case_id in requested_cases if case_id not in available)
        if missing_cases:
            print(f"Unknown automated safe case id(s): {', '.join(missing_cases)}", file=sys.stderr)
            print("Use --list to see available case ids.", file=sys.stderr)
            return 2
        requested = set(requested_cases)
        safe_cases = [case for case in safe_cases if case["id"] in requested]
    elif not args.list:
        safe_cases = [case for case in safe_cases if case.get("default", True)]

    if args.list:
        print("Automated safe cases:")
        for case in safe_cases:
            default_label = "" if case.get("default", True) else " [opt-in]"
            print(f"  {case['id']}{default_label}: {case['description']}")
        print("\nManual gameplay cases:")
        for case in MANUAL_GAMEPLAY_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['map']} - {case['purpose']}")
        print("\nGameplay benchmark harness profiles:")
        for case in GAMEPLAY_BENCHMARK_HARNESS:
            print(f"  {case['profile']}: {case['command']} - {case['coverage']}")
        print("\nShadow correctness cases:")
        for case in SHADOW_CORRECTNESS_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['map']} - {case['purpose']}")
        print("\nHuman review checklist:")
        for case in HUMAN_REVIEW_CHECKLIST:
            print(f"  {case['case']}: {case['focus']} - {case['checks']}")
        print("\nDeterministic capture cases:")
        for case in DETERMINISTIC_CAPTURE_MATRIX:
            print(f"  {case['id']}: {case['mode']} {case['scene']} - {case['purpose']}")
        print("\nRenderDoc tier cases:")
        for case in RENDERDOC_TIER_MATRIX:
            print(f"  r_glTier {case['tier']}: {case['focus']}")
        print("\nShader library tier cases:")
        for case in SHADER_LIBRARY_TIER_MATRIX:
            print(f"  {case['id']}: r_glTier {case['tier']} - {case['coverage']}")
        print("\nLens flare cross-platform sign-off:")
        for case in LENS_FLARE_SIGNOFF_MATRIX:
            print(
                f"  {case['platform']}: {case['shaderCoverage']} - "
                f"{case['visualEvidence']}; {case['performanceEvidence']}"
            )
        print("\nLong-run cases:")
        for case in LONG_RUN_VALIDATION_MATRIX:
            print(f"  {case['id']}: {case['mode']} - {case['purpose']}")
        print("\nCross-platform validation targets:")
        for item in CROSS_PLATFORM_VALIDATION_MATRIX:
            print(f"  {item['platform']}: {item['status']} - {item['required']}")
        print("\nPerformance regression thresholds:")
        for item in PERF_REGRESSION_THRESHOLDS:
            print(f"  {item['preset']}: P95 <= {item['p95Ms']} ms, P99 <= {item['p99Ms']} ms - {item['budget']}")
        print("\nPromotion readiness probes:")
        for item in PROMOTION_READINESS_MATRIX:
            print(f"  {item['id']}: {item['coverage']} - {item['expected']}")
        print("\nDefault promotion criteria:")
        for item in DEFAULT_PROMOTION_CRITERIA:
            print(f"  {item['criterion']}: {item['required']}")
        return 0

    executable = find_client_executable(root)
    savepath = Path(args.savepath).resolve() if args.savepath else root / ".home"
    savepath.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / ".tmp" / "renderer-validation" / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)

    basepath = args.basepath
    if basepath and not Path(basepath).exists():
        print(f"warning: basepath does not exist, omitting fs_basepath: {basepath}", file=sys.stderr)
        basepath = ""

    results = []
    for case in safe_cases:
        print(f"running {case['id']}...")
        result = run_case(
            root,
            executable,
            output_dir,
            savepath,
            basepath,
            case,
            args.timeout,
            args.skip_official_pak_validation,
        )
        print(f"  {result['status']} ({result['elapsedSeconds']}s)")
        if result["status"] != "pass":
            print_failure_details(result)
        results.append(result)

    metadata = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "executable": str(executable),
        "savepath": str(savepath),
        "basepath": basepath,
        "skipOfficialPakValidation": args.skip_official_pak_validation,
    }
    report_json, report_md = write_reports(output_dir, results, metadata)
    print(f"wrote {report_md}")
    print(f"wrote {report_json}")

    return 0 if all(result["status"] == "pass" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
