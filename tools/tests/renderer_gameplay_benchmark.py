#!/usr/bin/env python3
"""Run opt-in openQ4 renderer gameplay benchmark and capture cases.

Unlike renderer_validation_matrix.py, this runner enters maps. It is intended
for local, target-hardware validation where stock Quake 4 assets are available.
It launches from .install, writes isolated save/log roots under .tmp, captures
screenshots, dumps renderer benchmark metrics, and records a Markdown/JSON
report for performance triage.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shlex
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SAFE_TIERS = ("auto", "legacy", "gl33", "gl41", "gl43", "gl45", "gl46")
PRESENTATION_MAXFPS = ("0", "120", "240")
PRESENTATION_SWAP_INTERVALS = ("0", "1")
DISPLAY_MODES = ("windowed", "fullscreen")

REQUIRED_SCENES: dict[str, dict[str, Any]] = {
    "sp-storage1": {
        "mode": "SP",
        "map": "game/storage1",
        "purpose": "primary renderer performance acceptance scene, dense indoor lighting, and early-game storage visual parity",
        "path": "spawn-static",
    },
    "sp-airdefense1": {
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "stock SP baseline, outdoor lighting, terrain decals, and BSE smoke",
        "path": "spawn-static",
    },
    "sp-airdefense2": {
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "flashlight, projected shadows, animated characters, and dynamic shadow receivers",
        "path": "spawn-static",
    },
    "sp-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "indoor materials, post-process coverage, and dense local lights",
        "path": "spawn-static",
    },
    "sp-medlabs": {
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "BSE-heavy SP scene and stock scripted effects coverage",
        "path": "spawn-static",
    },
    "sp-mcc-landing": {
        "mode": "SP",
        "map": "game/mcc_landing",
        "purpose": "subviews, remote cameras, cinematic handoff, and GUI interaction",
        "path": "spawn-static",
    },
    "mp-q4dm1-listen": {
        "mode": "MP",
        "map": "mp/q4dm1",
        "purpose": "listen server plus local loopback client renderer parity",
        "path": "spawn-static",
    },
}

SHADOW_SCENES: dict[str, dict[str, Any]] = {
    "shadow-projected-airdefense2": {
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "angled projected-light caster/receiver validation",
        "path": "spawn-static",
    },
    "shadow-point-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "point-light face coverage and local-light receiver validation",
        "path": "spawn-static",
    },
    "shadow-csm-airdefense1": {
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "CSM camera sweep readiness and outdoor directional coverage",
        "path": "spawn-static",
    },
    "shadow-cutout-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "hashed-alpha cutout fence/grate caster validation at distance",
        "path": "spawn-static",
    },
    "shadow-character-airdefense2": {
        "mode": "SP",
        "map": "game/airdefense2",
        "purpose": "dynamic character shadow caster and receiver validation",
        "path": "spawn-static",
    },
    "shadow-translucent-medlabs": {
        "mode": "SP",
        "map": "game/medlabs",
        "purpose": "optional translucent moment caster coverage where the selected tier supports it",
        "path": "spawn-static",
    },
}

LENS_FLARE_SCENES: dict[str, dict[str, Any]] = {
    "lensflare-storage1": {
        "mode": "SP",
        "map": "game/storage1",
        "purpose": "stable indoor lens-flare capture at the early-game spawn with dense local lights and post-process coverage",
        "path": "spawn-static",
    },
    "lensflare-storage2": {
        "mode": "SP",
        "map": "game/storage2",
        "purpose": "indoor bright-source lens-flare capture for local lights, occlusion, and material/post interaction",
        "path": "spawn-static",
    },
    "lensflare-airdefense1": {
        "mode": "SP",
        "map": "game/airdefense1",
        "purpose": "outdoor lens-flare capture with sky/terrain visibility and root-scene post-processing",
        "path": "spawn-static",
    },
}

ALL_SCENES = {**REQUIRED_SCENES, **SHADOW_SCENES, **LENS_FLARE_SCENES}

SHADOW_PRESETS: dict[str, dict[str, str]] = {
    "default": {},
    "stencil": {
        "r_shadows": "1",
        "r_useShadowMap": "0",
    },
    "mapped": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "0",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "csm": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "0",
    },
    "translucent": {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapTranslucentMoments": "1",
    },
}

LENS_FLARE_PRESETS: dict[str, dict[str, str]] = {
    "default": {},
    "off": {
        "r_lensFlare": "0",
    },
    "corona": {
        "r_skipPostProcess": "0",
        "r_lensFlare": "1",
    },
    "high": {
        "r_skipPostProcess": "0",
        "r_lensFlare": "2",
    },
}

LENS_FLARE_SIGNOFF_MATRIX = [
    {
        "platform": "Windows x64",
        "tiers": "auto, gl41, gl45",
        "visualEvidence": "run `--profile lensflare-signoff` with approved `.tmp\\renderer-references\\lensflare-signoff\\windows-x64` references when promotion evidence is being collected",
        "performanceEvidence": "run the same profile with `--sample-msec 3000 --pacing-only --maxfps 0 --swap-intervals 0` and target-machine P95/P99 thresholds",
    },
    {
        "platform": "Linux x64/arm64",
        "tiers": "auto plus highest supported forced GL tier",
        "visualEvidence": "run `--profile lensflare-signoff` against Linux-specific approved references because drivers and desktop color paths can differ from Windows",
        "performanceEvidence": "capture uncapped pacing with `--pacing-only` under both SDL3 desktop and Steam Deck profile coverage when available",
    },
    {
        "platform": "macOS",
        "tiers": "gl41 and auto fallback",
        "visualEvidence": "run `--profile lensflare-signoff --tiers gl41,auto` with macOS-specific references; GL 4.1 is the expected portability floor",
        "performanceEvidence": "capture reduced-tier/off-vs-high pacing with `--pacing-only`; do not infer GL 4.3+ behavior from macOS runs",
    },
]

UPLOAD_PRESSURE_VARIANTS: dict[str, dict[str, Any]] = {
    "default": {
        "purpose": "current upload defaults for the selected renderer tier",
        "cvars": (),
    },
    "persistent": {
        "purpose": "explicit persistent-mapped upload request on GL 4.4+ capable drivers",
        "cvars": (
            ("r_rendererUploadPersistent", "1"),
        ),
    },
    "reduced-ring": {
        "purpose": "smaller 4 MB per-frame upload ring with the normal frame-buffer count",
        "cvars": (
            ("r_rendererUploadMegs", "4"),
            ("r_rendererUploadFrameBuffers", "4"),
            ("r_rendererUploadPersistent", "1"),
        ),
    },
    "min-frame-buffers": {
        "purpose": "minimum safe upload frame-buffer rotation depth with the default ring size",
        "cvars": (
            ("r_rendererUploadMegs", "16"),
            ("r_rendererUploadFrameBuffers", "3"),
            ("r_rendererUploadPersistent", "1"),
        ),
    },
    "map-range": {
        "purpose": "persistent mapping disabled, exercising the map-range/subdata fallback tier available on the target driver",
        "cvars": (
            ("r_rendererUploadPersistent", "0"),
        ),
    },
}

GRAPH_INVALIDATION_VARIANTS: dict[str, dict[str, Any]] = {
    "default": {
        "purpose": "current default graph invalidation policy; candidates are counted but not armed",
        "cvars": (),
    },
    "armed": {
        "purpose": "default-off graph invalidation path armed for eligibility comparison without submitting GL discard calls",
        "cvars": (
            ("r_rendererGraphInvalidate", "1"),
        ),
    },
}

PERFORMANCE_COMPARISON_VARIANTS: dict[str, dict[str, Any]] = {
    "default": {
        "purpose": "current r_renderer best default path with guarded modern promotion disabled",
        "cvars": (),
    },
    "arb2": {
        "purpose": "explicit ARB2 rollback/baseline path for ARB2-or-better comparisons",
        "cvars": (
            ("r_renderer", "arb2"),
            ("r_rendererModernExecutor", "0"),
            ("r_rendererModernSubmit", "0"),
            ("r_rendererModernVisible", "0"),
            ("r_rendererModernAutoPromote", "0"),
        ),
    },
    "executor": {
        "purpose": "modern executor preparation path enabled while visible output remains on the compatibility bridge",
        "cvars": (
            ("r_rendererModernExecutor", "1"),
            ("r_rendererModernSubmit", "0"),
            ("r_rendererModernVisible", "0"),
            ("r_rendererModernAutoPromote", "0"),
        ),
    },
}

PROFILE_LAUNCH_VARIANT_TABLES: dict[str, dict[str, dict[str, Any]]] = {
    "upload-pressure": UPLOAD_PRESSURE_VARIANTS,
    "graph-invalidation": GRAPH_INVALIDATION_VARIANTS,
    "performance-comparison": PERFORMANCE_COMPARISON_VARIANTS,
}

SHADOW_DEBUG_PRESET_MODES = (1, 2, 3, 4, 5, 6, 7, 12, 13, 14)

for debug_mode in SHADOW_DEBUG_PRESET_MODES:
    SHADOW_PRESETS[f"debug{debug_mode}"] = {
        "r_shadows": "1",
        "r_useShadowMap": "1",
        "r_shadowMapCSM": "1",
        "r_shadowMapHashedAlpha": "1",
        "r_shadowMapDebugOverlay": "1",
        "r_shadowMapDebugMode": str(debug_mode),
        "r_shadowMapTranslucentMoments": "0",
    }

PROFILE_DEFAULTS = {
    "smoke": {
        "cases": ("sp-storage1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
    },
    "required": {
        "cases": tuple(REQUIRED_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
    },
    "tiers": {
        "cases": ("sp-airdefense1",),
        "tiers": SAFE_TIERS,
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
    },
    "presentation": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": DISPLAY_MODES,
        "shadows": ("default",),
        "lensFlare": ("default",),
    },
    "shadows": {
        "cases": tuple(SHADOW_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("stencil", "mapped", "csm", "translucent", "debug1", "debug2", "debug3", "debug4", "debug5", "debug6", "debug7", "debug12", "debug13", "debug14"),
        "lensFlare": ("default",),
    },
    "shadow-regression": {
        "cases": (
            "shadow-projected-airdefense2",
            "shadow-point-storage2",
            "shadow-csm-airdefense1",
            "shadow-character-airdefense2",
            "shadow-cutout-storage2",
        ),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("csm",),
        "lensFlare": ("default",),
        "cvars": (
            ("r_shadowMapPointLights", "1"),
            ("r_shadowMapReport", "1"),
        ),
    },
    "lensflare": {
        "cases": tuple(LENS_FLARE_SCENES.keys()),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("off", "corona", "high"),
    },
    "lensflare-signoff": {
        "cases": ("lensflare-storage1", "lensflare-airdefense1"),
        "tiers": ("auto", "gl41", "gl45"),
        "maxfps": ("0",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("off", "corona", "high"),
    },
    "upload-pressure": {
        "cases": ("sp-storage1", "sp-airdefense1", "sp-medlabs"),
        "tiers": ("auto",),
        "maxfps": ("0",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "launchVariants": tuple(UPLOAD_PRESSURE_VARIANTS.keys()),
    },
    "low-overhead-state": {
        "cases": ("sp-storage1", "sp-airdefense1", "sp-medlabs"),
        "tiers": ("gl33", "gl45"),
        "maxfps": ("0",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "modernExecutor": True,
        "rendererMetricsLevel": 2,
    },
    "graph-invalidation": {
        "cases": ("lensflare-storage1", "lensflare-airdefense1", "sp-medlabs"),
        "tiers": ("auto",),
        "maxfps": ("0",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("corona",),
        "launchVariants": tuple(GRAPH_INVALIDATION_VARIANTS.keys()),
    },
    "performance-comparison": {
        "cases": ("sp-storage1", "sp-airdefense1", "sp-storage2", "sp-medlabs"),
        "tiers": ("auto",),
        "maxfps": ("0",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "launchVariants": tuple(PERFORMANCE_COMPARISON_VARIANTS.keys()),
    },
    "presentation-comparison": {
        "cases": ("sp-storage1", "sp-airdefense1", "sp-storage2", "sp-medlabs", "mp-q4dm1-listen"),
        "tiers": ("auto",),
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
    },
    "visual-comparison": {
        "cases": (
            "lensflare-airdefense1",
            "sp-medlabs",
            "sp-mcc-landing",
            "sp-storage2",
            "mp-q4dm1-listen",
        ),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("high",),
    },
    "postaa-state-poison": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "cvars": (
            ("r_postAA", "1"),
            ("r_postAAStatePoisonTest", "1"),
        ),
    },
    "postaa-high": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "cvars": (
            ("r_postAA", "2"),
        ),
    },
    "postaa-ultra": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "cvars": (
            ("r_postAA", "3"),
        ),
    },
    "postaa-color-prototype": {
        "cases": ("sp-airdefense1",),
        "tiers": ("auto",),
        "maxfps": ("240",),
        "swap": ("0",),
        "display": ("windowed",),
        "shadows": ("default",),
        "lensFlare": ("default",),
        "cvars": (
            ("r_postAA", "4"),
        ),
    },
    "full": {
        "cases": tuple(ALL_SCENES.keys()),
        "tiers": SAFE_TIERS,
        "maxfps": PRESENTATION_MAXFPS,
        "swap": PRESENTATION_SWAP_INTERVALS,
        "display": DISPLAY_MODES,
        "shadows": ("default", "stencil", "mapped", "csm", "translucent"),
        "lensFlare": ("default",),
    },
}

WARNING_PATTERNS = {
    "snPrintfOverflow": re.compile(r"idStr::snPrintf:\s*overflow", re.IGNORECASE),
    "idStrWarning": re.compile(r"WARNING:\s+idStr", re.IGNORECASE),
    "shaderCompile": re.compile(r"(shader compile|program link).*(failed|error)|failed to compile", re.IGNORECASE),
    "glError": re.compile(r"\bGL_INVALID_[A-Z_]+|OpenGL error", re.IGNORECASE),
    "fatal": re.compile(r"Fatal Error|could not initialize OpenGL|Unable to initialize OpenGL", re.IGNORECASE),
}


@dataclass(frozen=True)
class RunSpec:
    case_id: str
    mode: str
    map_name: str
    purpose: str
    path_name: str
    tier: str
    maxfps: str
    swap_interval: str
    display_mode: str
    shadow_preset: str
    lens_flare_preset: str
    renderer: str
    upload_variant: str = "default"
    variant_prefix: str = "variant"
    launch_cvars: tuple[tuple[str, str], ...] = ()

    @property
    def fullscreen(self) -> bool:
        return self.display_mode == "fullscreen"

    @property
    def id(self) -> str:
        parts = [
            self.case_id,
            self.tier,
            f"fps{self.maxfps}",
            f"vsync{self.swap_interval}",
            self.display_mode,
            self.shadow_preset,
        ]
        if self.lens_flare_preset != "default":
            parts.append(f"lf{self.lens_flare_preset}")
        if self.upload_variant != "default":
            parts.append(f"{self.variant_prefix}-{self.upload_variant}")
        parts.append(self.renderer)
        return sanitize_case_id("_".join(parts))


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


def compact_filesystem_id(case_id: str, max_length: int = 56) -> str:
    sanitized = sanitize_case_id(case_id)
    if len(sanitized) <= max_length:
        return sanitized
    digest = hashlib.sha1(sanitized.encode("utf-8")).hexdigest()[:10]
    stem_limit = max(8, max_length - len(digest) - 1)
    stem = sanitized[:stem_limit].rstrip("._-") or "case"
    return f"{stem}-{digest}"


def split_csv(value: str, defaults: tuple[str, ...]) -> tuple[str, ...]:
    if not value:
        return defaults
    return tuple(item.strip() for item in value.split(",") if item.strip())


def profile_launch_variants(defaults: dict[str, Any]) -> tuple[str, ...]:
    return tuple(defaults.get("launchVariants", ("default",)))


def profile_launch_variant_table(profile: str) -> dict[str, dict[str, Any]]:
    return PROFILE_LAUNCH_VARIANT_TABLES.get(profile, {"default": {"purpose": "current defaults", "cvars": ()}})


def profile_launch_variant_prefix(profile: str) -> str:
    if profile == "upload-pressure":
        return "upload"
    if profile == "graph-invalidation":
        return "graph"
    if profile == "performance-comparison":
        return "perf"
    return "variant"


def profile_launch_variant_cvars(profile: str, variant: str) -> tuple[tuple[str, str], ...]:
    table = profile_launch_variant_table(profile)
    if variant not in table:
        raise ValueError(f"unknown {profile} launch variant '{variant}'")
    return tuple(table[variant].get("cvars", ()))


def parse_extra_cvars(values: list[str]) -> tuple[tuple[str, str], ...]:
    parsed: list[tuple[str, str]] = []
    for raw in values:
        item = raw.strip()
        if not item:
            continue
        if "=" in item:
            name, value = item.split("=", 1)
        else:
            parts = item.split(None, 1)
            if len(parts) != 2:
                raise ValueError(f"extra cvar '{raw}' must use name=value or 'name value'")
            name, value = parts
        name = name.strip()
        value = value.strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise ValueError(f"extra cvar name '{name}' is not a valid cvar identifier")
        if not value:
            raise ValueError(f"extra cvar '{name}' needs a value")
        parsed.append((name, value))
    return tuple(parsed)


def parse_exec_commands(values: list[str]) -> tuple[str, ...]:
    commands: list[str] = []
    for raw in values:
        command = raw.strip()
        if not command:
            raise ValueError("empty --exec-command value")
        if any(ord(ch) < 32 for ch in command):
            raise ValueError(f"--exec-command contains a control character: {raw!r}")
        commands.append(command)
    return tuple(commands)


def append_set(args: list[str], name: str, value: Any) -> None:
    args += ["+set", name, str(value)]


def append_command(args: list[str], name: str, *values: Any) -> None:
    args.append("+" + name)
    args.extend(str(value) for value in values)


def common_args(
    root: Path,
    savepath: Path,
    log_name: str,
    basepath: str,
    spec: RunSpec,
    benchmark_preset: str,
    modern_executor: bool,
    show_fps_overlay: bool,
    launch_cvars: tuple[tuple[str, str], ...] = (),
    autoexec_cfg: str | None = None,
    autoexec_delay_ms: int = 1000,
) -> list[str]:
    args: list[str] = []
    append_set(args, "win_allowMultipleInstances", "1")
    append_set(args, "logFile", "2")
    append_set(args, "logFileName", f"logs/{log_name}")
    append_set(args, "developer", "1")
    append_set(args, "r_fullscreen", "1" if spec.fullscreen else "0")
    append_set(args, "r_swapInterval", spec.swap_interval)
    append_set(args, "com_maxfps", spec.maxfps)
    append_set(args, "com_showFPS", "1" if show_fps_overlay else "0")
    append_set(args, "com_skipLoadingContinue", "1")
    append_set(args, "com_loadingContinueAutoAdvance", "1")
    append_set(args, "g_autoSkipCinematics", "1")
    append_set(args, "g_autoScreenshot", "0")
    if autoexec_cfg:
        append_set(args, "g_autoExecAfterMapLoad", autoexec_cfg)
        append_set(args, "g_autoExecAfterMapLoadDelayMs", max(0, autoexec_delay_ms))
    append_set(args, "r_glTier", spec.tier)
    append_set(args, "r_renderer", spec.renderer)
    append_set(args, "r_rendererMetrics", "0")
    append_set(args, "r_rendererGpuTimers", "0")
    append_set(args, "r_rendererModernExecutor", "1" if modern_executor and spec.tier != "legacy" else "0")
    append_set(args, "r_rendererModernAutoPromote", "0")
    append_set(args, "r_rendererBenchmarkPreset", benchmark_preset)
    append_set(args, "fs_savepath", str(savepath))
    append_set(args, "fs_devpath", str(root / ".install"))
    append_set(args, "fs_game", "baseoq4")
    if basepath:
        append_set(args, "fs_basepath", basepath)

    for name, value in launch_cvars:
        append_set(args, name, value)

    for name, value in SHADOW_PRESETS[spec.shadow_preset].items():
        append_set(args, name, value)

    return args


def build_scripted_capture_lines(
    spec: RunSpec,
    role: str,
    run_id: str,
    settle_frames: int,
    sample_frames: int,
    sample_msec: int,
    quit_wait_frames: int = 5,
    extra_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
    gpu_timers: bool = False,
    renderer_metrics: bool = True,
    renderer_metrics_level: int = 1,
    capture_index: int = 0,
) -> tuple[list[str], str]:
    shot_name = f"screenshots/renderer-bench/{role}_{capture_index}.tga"
    lines: list[str] = [
        "r_rendererModernVisible 0",
        "r_rendererModernVisibleDepth 0",
        "r_rendererModernOpaque 0",
        "r_rendererModernDeferred 0",
        "r_rendererForwardPlus 0",
        "r_rendererModernSubmit 0",
        "r_rendererGpuValidation 0",
        "r_rendererBindless 0",
        "r_rendererShaderReload 0",
    ]
    for name, value in LENS_FLARE_PRESETS[spec.lens_flare_preset].items():
        lines.append(f"{name} {value}")
    for name, value in extra_cvars:
        lines.append(f"{name} {value}")
    lines += [
        f"wait {max(1, settle_frames)}",
        "god",
        "notarget",
        "getviewpos",
    ]
    lines.extend(exec_commands)
    lines.append("framePacingReset")
    sample_wait = f"waitMsec {max(1, sample_msec)}" if sample_msec > 0 else f"wait {max(1, sample_frames)}"
    if renderer_metrics:
        metrics_level = max(1, renderer_metrics_level)
        lines += [
            f"r_rendererMetrics {metrics_level}",
            f"r_rendererGpuTimers {1 if gpu_timers else 0}",
            sample_wait,
            "rendererBenchmarkCapture",
            "r_rendererMetrics 0",
        ]
    else:
        lines += [
            "r_rendererMetrics 0",
            "r_rendererGpuTimers 0",
            sample_wait,
        ]
    lines += [
        "framePacingSnapshot",
        "gfxInfo",
        f'screenshot "{shot_name}"',
        f"wait {max(1, quit_wait_frames)}",
        "quit",
    ]
    return lines, shot_name


def write_autoexec_cfg(
    savepath: Path,
    spec: RunSpec,
    role: str,
    run_id: str,
    settle_frames: int,
    sample_frames: int,
    sample_msec: int,
    quit_wait_frames: int = 5,
    extra_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
    gpu_timers: bool = False,
    renderer_metrics: bool = True,
    renderer_metrics_level: int = 1,
    capture_index: int = 0,
) -> tuple[str, str]:
    lines, shot_name = build_scripted_capture_lines(
        spec,
        role,
        run_id,
        settle_frames,
        sample_frames,
        sample_msec,
        quit_wait_frames,
        extra_cvars,
        exec_commands,
        gpu_timers,
        renderer_metrics,
        renderer_metrics_level,
        capture_index,
    )
    cfg_rel = f"renderer-bench/{role}_{capture_index}.cfg"
    payload = "\n".join(lines) + "\n"
    screenshot_rel = Path(shot_name.replace("/", os.sep))
    for game_dir in ("baseoq4", "q4base"):
        cfg_path = savepath / game_dir / Path(cfg_rel)
        cfg_path.parent.mkdir(parents=True, exist_ok=True)
        cfg_path.write_text(payload, encoding="utf-8")
        screenshot_path = savepath / game_dir / screenshot_rel
        screenshot_path.parent.mkdir(parents=True, exist_ok=True)
    return cfg_rel, shot_name


def log_candidates(savepath: Path, log_name: str) -> list[Path]:
    return [
        savepath / "baseoq4" / "logs" / log_name,
        savepath / "q4base" / "logs" / log_name,
        savepath / "logs" / log_name,
    ]


def find_log(savepath: Path, log_name: str) -> Path | None:
    candidates = log_candidates(savepath, log_name)
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def screenshot_candidates(savepath: Path, relative_name: str) -> list[Path]:
    rel = Path(relative_name.replace("/", os.sep))
    return [
        savepath / "baseoq4" / rel,
        savepath / "q4base" / rel,
        savepath / rel,
    ]


def autoexec_candidates(savepath: Path, cfg_rel: str) -> list[Path]:
    rel = Path(cfg_rel.replace("/", os.sep))
    return [
        savepath / "baseoq4" / rel,
        savepath / "q4base" / rel,
        savepath / rel,
    ]


def windows_path_length_warning(label: str, paths: list[str]) -> str:
    if os.name != "nt":
        return ""
    long_paths = [path for path in paths if len(path) >= 260]
    if not long_paths:
        return ""
    return f"{label} path length >=260 chars (max {max(len(path) for path in long_paths)})"


def find_screenshot(savepath: Path, relative_name: str) -> Path | None:
    candidates = screenshot_candidates(savepath, relative_name)
    seen: set[Path] = set()
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        if candidate.exists():
            return candidate
    return None


def format_command(command: list[str]) -> str:
    if os.name == "nt":
        return subprocess.list2cmdline(command)
    return " ".join(shlex.quote(part) for part in command)


def cvar_pair_payload(cvars: tuple[tuple[str, str], ...]) -> list[dict[str, str]]:
    return [{"name": str(name), "value": str(value)} for name, value in cvars]


def role_launch_metadata(
    root: Path,
    executable: Path,
    launch_args: list[str],
    cwd: Path,
    savepath: Path,
    basepath: str,
    role: str,
    log_name: str,
    screenshot_rel: str,
    port: int | None = None,
    autoexec_cfg: str = "",
    launch_cvars: tuple[tuple[str, str], ...] = (),
    script_cvars: tuple[tuple[str, str], ...] = (),
    exec_commands: tuple[str, ...] = (),
) -> dict[str, Any]:
    command = [str(executable)] + [str(arg) for arg in launch_args]
    metadata: dict[str, Any] = {
        "role": role,
        "executable": str(executable),
        "launchCommand": format_command(command),
        "workingDirectory": str(cwd),
        "savepath": str(savepath),
        "devpath": str(root / ".install"),
        "basepath": basepath,
        "gameDir": "baseoq4",
        "autoexecCfg": autoexec_cfg,
        "expectedAutoexecPaths": [str(path) for path in autoexec_candidates(savepath, autoexec_cfg)] if autoexec_cfg else [],
        "expectedLogPaths": [str(path) for path in log_candidates(savepath, log_name)],
        "expectedScreenshotPaths": [str(path) for path in screenshot_candidates(savepath, screenshot_rel)],
        "launchCvars": cvar_pair_payload(launch_cvars),
        "scriptCvars": cvar_pair_payload(script_cvars),
        "execCommands": list(exec_commands),
    }
    if port is not None:
        metadata["port"] = port
    return metadata


def read_text(path: Path | None) -> str:
    if path is None or not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace")


def warning_counts(text: str) -> dict[str, int]:
    return {name: len(pattern.findall(text)) for name, pattern in WARNING_PATTERNS.items()}


def extract_last_line(text: str, token: str) -> str:
    lines = [line.strip() for line in text.splitlines() if token in line]
    return lines[-1] if lines else ""


def parse_frame_pacing(line: str) -> dict[str, str]:
    if not line:
        return {}
    match = re.search(
        r"samples=(\d+).*?present=([0-9.]+) ms \(([0-9.]+) Hz\)"
        r"(?:, p50=([0-9.]+) ms, p95=([0-9.]+) ms, p99=([0-9.]+) ms, max=([0-9.]+) ms)?",
        line,
    )
    if not match:
        return {}
    samples, present_ms, present_hz, p50_ms, p95_ms, p99_ms, max_ms = match.groups()
    result = {
        "pacingSamples": samples,
        "pacingPresentMs": present_ms,
        "pacingHz": present_hz,
    }
    if p50_ms is not None:
        result.update(
            {
                "pacingP50Ms": p50_ms,
                "pacingP95Ms": p95_ms,
                "pacingP99Ms": p99_ms,
                "pacingMaxMs": max_ms,
            }
        )
    return result


def parse_benchmark_capture(line: str) -> dict[str, str]:
    if not line:
        return {}
    result: dict[str, str] = {}
    upload_match = re.search(r"work\(upload=(\d+)KB", line)
    if upload_match:
        result["benchmarkUploadKB"] = upload_match.group(1)
    return result


def parse_renderer_metrics_line(line: str) -> dict[str, str]:
    if not line:
        return {}
    result: dict[str, str] = {}
    phase_match = re.search(
        r"tier=([^\s]+) fe=(\d+)ms visibility=(\d+)ms packet=(\d+)ms graph=(\d+)ms "
        r"submit=(\d+)ms be=(\d+)ms present=(\d+)ms gpu=([^\s]+) views=(\d+) ents=(\d+) "
        r"lights=(\d+) draws=(\d+)",
        line,
    )
    if phase_match:
        (
            tier,
            front_end_ms,
            visibility_ms,
            packet_ms,
            graph_ms,
            submit_ms,
            backend_ms,
            present_ms,
            gpu_text,
            views,
            entities,
            lights,
            draws,
        ) = phase_match.groups()
        result.update(
            {
                "metricsTier": tier,
                "frontEndMs": front_end_ms,
                "visibilityMs": visibility_ms,
                "packetMs": packet_ms,
                "graphMs": graph_ms,
                "submitMs": submit_ms,
                "backendMs": backend_ms,
                "presentMs": present_ms,
                "gpuMs": gpu_text,
                "views": views,
                "visibleEntities": entities,
                "viewLights": lights,
                "draws": draws,
            }
        )
    summary_match = re.search(
        r"uploads=(\d+)KB stalls=(\d+) ring=(\d+)/(\d+)KB overflow=(\d+)KB "
        r"static=(\d+)KB/(\d+) live=(\d+)/(\d+)KB",
        line,
    )
    if summary_match:
        (
            upload_kb,
            stalls,
            ring_high_kb,
            ring_capacity_kb,
            overflow_kb,
            static_upload_kb,
            static_allocs,
            static_live,
            static_live_kb,
        ) = summary_match.groups()
        result.update(
            {
                "metricsUploadKB": upload_kb,
                "frameStalls": stalls,
                "ringHighWaterKB": ring_high_kb,
                "ringCapacityKB": ring_capacity_kb,
                "ringOverflowKB": overflow_kb,
                "staticUploadKB": static_upload_kb,
                "staticUploadAllocs": static_allocs,
                "staticLiveBuffers": static_live,
                "staticLiveKB": static_live_kb,
            }
        )
    else:
        detailed_match = re.search(
            r"uploads=(\d+) stalls=(\d+) ring=(\d+)/(\d+)KB allocs=(\d+) overflow=(\d+) "
            r"static=(\d+)KB/(\d+) live=(\d+)/(\d+)KB writes\(p=(\d+) map=(\d+) sub=(\d+)\)",
            line,
        )
        if detailed_match:
            (
                upload_bytes,
                stalls,
                ring_high_kb,
                ring_capacity_kb,
                frame_allocs,
                overflow_kb,
                static_upload_kb,
                static_allocs,
                static_live,
                static_live_kb,
                persistent_writes,
                map_writes,
                subdata_writes,
            ) = detailed_match.groups()
            result.update(
                {
                    "metricsUploadBytes": upload_bytes,
                    "frameStalls": stalls,
                    "ringHighWaterKB": ring_high_kb,
                    "ringCapacityKB": ring_capacity_kb,
                    "frameUploadAllocs": frame_allocs,
                    "ringOverflowKB": overflow_kb,
                    "staticUploadKB": static_upload_kb,
                    "staticUploadAllocs": static_allocs,
                    "staticLiveBuffers": static_live,
                    "staticLiveKB": static_live_kb,
                    "persistentWrites": persistent_writes,
                    "mapRangeWrites": map_writes,
                    "subDataWrites": subdata_writes,
                }
            )
    state_summary = re.search(r"stateCache=(\d+)/(\d+) invalid=(\d+) legacyReset=(\d+)", line)
    if state_summary:
        hits, misses, invalidations, legacy_resets = state_summary.groups()
        result.update(
            {
                "stateCacheHits": hits,
                "stateCacheMisses": misses,
                "stateCacheInvalidations": invalidations,
                "stateCacheLegacyResets": legacy_resets,
            }
        )
    else:
        state_detail = re.search(
            r"stateCache\(hits=(\d+) misses=(\d+) invalidations=(\d+) legacyResets=(\d+)",
            line,
        )
        if state_detail:
            hits, misses, invalidations, legacy_resets = state_detail.groups()
            result.update(
                {
                    "stateCacheHits": hits,
                    "stateCacheMisses": misses,
                    "stateCacheInvalidations": invalidations,
                    "stateCacheLegacyResets": legacy_resets,
                }
            )
    graph_invalidate_summary = re.search(r"graphInvalidate=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)/(\d+)", line)
    if graph_invalidate_summary:
        enabled, tagged, candidates, armed, submitted, skipped = graph_invalidate_summary.groups()
        result.update(
            {
                "graphInvalidateEnabled": enabled,
                "graphInvalidateTagged": tagged,
                "graphInvalidateCandidates": candidates,
                "graphInvalidateArmed": armed,
                "graphInvalidateSubmitted": submitted,
                "graphInvalidateSkipped": skipped,
            }
        )
    graph_invalidate_detail = re.search(
        r"graphGL\(.*?invalidate\(enabled=(\d+) tagged=(\d+) candidates=(\d+) armed=(\d+) "
        r"submitted=(\d+) skipped=(\d+) unavailable=(\d+) invalid=(\d+) imported=(\d+) "
        r"buffer=(\d+) nonTransient=(\d+) presentable=(\d+) later=(\d+) incomplete=(\d+) unsupported=(\d+)\)",
        line,
    )
    if graph_invalidate_detail:
        (
            enabled,
            tagged,
            candidates,
            armed,
            submitted,
            skipped,
            unavailable,
            invalid,
            imported,
            buffer,
            non_transient,
            presentable,
            later,
            incomplete,
            unsupported,
        ) = graph_invalidate_detail.groups()
        result.update(
            {
                "graphInvalidateEnabled": enabled,
                "graphInvalidateTagged": tagged,
                "graphInvalidateCandidates": candidates,
                "graphInvalidateArmed": armed,
                "graphInvalidateSubmitted": submitted,
                "graphInvalidateSkipped": skipped,
                "graphInvalidateSkippedUnavailable": unavailable,
                "graphInvalidateSkippedInvalid": invalid,
                "graphInvalidateSkippedImported": imported,
                "graphInvalidateSkippedBuffer": buffer,
                "graphInvalidateSkippedNonTransient": non_transient,
                "graphInvalidateSkippedPresentable": presentable,
                "graphInvalidateSkippedLater": later,
                "graphInvalidateSkippedIncomplete": incomplete,
                "graphInvalidateSkippedUnsupported": unsupported,
            }
        )
    return result


def parse_upload_manager_info(line: str) -> dict[str, str]:
    if not line:
        return {}
    match = re.search(
        r"Renderer upload manager: frameStream=([^,]+), staticAllocator=(\d+), "
        r"buffers=(\d+) index=(\d+), ring=(\d+)KB, persistent=(\d+), "
        r"mapRangeFallback=(\d+), staticLive=(\d+)/(\d+)KB, fences=(\d+)/(\d+) "
        r"waits=(\d+) timeouts=(\d+) fallbacks=(\d+), legacyBridge=(\d+)",
        line,
    )
    if not match:
        return {}
    (
        frame_stream,
        static_allocator,
        buffers,
        index,
        ring_kb,
        persistent,
        map_range,
        static_live,
        static_live_kb,
        fences_submitted,
        fences_retired,
        waits,
        timeouts,
        fallbacks,
        legacy_bridge,
    ) = match.groups()
    return {
        "uploadFrameStream": frame_stream,
        "uploadStaticAllocator": static_allocator,
        "uploadBuffers": buffers,
        "uploadBufferIndex": index,
        "uploadRingCapacityKB": ring_kb,
        "uploadPersistent": persistent,
        "uploadMapRangeFallback": map_range,
        "uploadStaticLiveBuffers": static_live,
        "uploadStaticLiveKB": static_live_kb,
        "fenceSubmitted": fences_submitted,
        "fenceRetired": fences_retired,
        "fenceWaits": waits,
        "fenceTimeouts": timeouts,
        "fenceFallbacks": fallbacks,
        "uploadLegacyBridge": legacy_bridge,
    }


def parse_state_cache_info(line: str) -> dict[str, str]:
    if not line:
        return {}
    match = re.search(
        r"Modern GL state cache: .*?hits=(\d+) misses=(\d+) .*?textureMultiBind=(\d+)"
        r"(?: textureMultiBindFallback=(\d+))? samplerMultiBind=(\d+) invalidations=(\d+) legacyResets=(\d+)",
        line,
    )
    if not match:
        return {}
    hits, misses, texture_multi_bind, texture_fallback, sampler_multi_bind, invalidations, legacy_resets = match.groups()
    return {
        "stateCacheInfoHits": hits,
        "stateCacheInfoMisses": misses,
        "stateTextureMultiBindBatches": texture_multi_bind,
        "stateTextureMultiBindFallbackBatches": texture_fallback or "0",
        "stateSamplerMultiBindBatches": sampler_multi_bind,
        "stateCacheInfoInvalidations": invalidations,
        "stateCacheInfoLegacyResets": legacy_resets,
    }


def parse_low_overhead_metrics_line(line: str) -> dict[str, str]:
    if not line:
        return {}
    result: dict[str, str] = {}
    prefix = re.search(
        r"rendererMetrics lowOverhead\(req=(\d+) ready=(\d+) dsa=(\d+) multiBind=(\d+) "
        r"bindless=(\d+)/(\d+) sampler=(\d+) dsaUpdates=(\d+) framebufferDSA=(\d+) "
        r"samplerDSA=(\d+)/(\d+) bufferMultiBind=(\d+) textureMultiBind=(\d+) "
        r"samplerMultiBind=(\d+) classicTextureBinds=(\d+) compactedBatches=(\d+) "
        r"restores=(\d+)/(\d+) textureTable=(\d+)/(\d+) tableSize=(\d+)/(\d+) "
        r"desc=(\d+) draws=(\d+) uniforms=(\d+) fallback=(\d+)",
        line,
    )
    if prefix:
        (
            requested,
            ready,
            dsa,
            multibind,
            bindless_requested,
            bindless_available,
            sampler,
            dsa_updates,
            framebuffer_dsa,
            sampler_dsa_creates,
            sampler_dsa_updates,
            buffer_multibind,
            texture_multibind,
            sampler_multibind,
            classic_texture_binds,
            compacted_batches,
            soft_restores,
            full_restores,
            texture_table_used,
            texture_table_ready,
            table_textures,
            table_capacity,
            descriptors,
            draws,
            uniforms,
            fallback,
        ) = prefix.groups()
        result.update(
            {
                "lowOverheadRequested": requested,
                "lowOverheadReady": ready,
                "lowOverheadDSA": dsa,
                "lowOverheadMultiBind": multibind,
                "lowOverheadBindlessRequested": bindless_requested,
                "lowOverheadBindlessAvailable": bindless_available,
                "lowOverheadSampler": sampler,
                "lowOverheadDSAUpdates": dsa_updates,
                "lowOverheadFramebufferDSA": framebuffer_dsa,
                "lowOverheadSamplerDSACreates": sampler_dsa_creates,
                "lowOverheadSamplerDSAUpdates": sampler_dsa_updates,
                "lowOverheadBufferMultiBind": buffer_multibind,
                "lowOverheadTextureMultiBind": texture_multibind,
                "lowOverheadSamplerMultiBind": sampler_multibind,
                "lowOverheadClassicTextureBinds": classic_texture_binds,
                "lowOverheadCompactedBatches": compacted_batches,
                "lowOverheadSoftRestores": soft_restores,
                "lowOverheadFullRestores": full_restores,
                "lowOverheadTextureTableUsed": texture_table_used,
                "lowOverheadTextureTableReady": texture_table_ready,
                "lowOverheadTextureTableTextures": table_textures,
                "lowOverheadTextureTableCapacity": table_capacity,
                "lowOverheadTextureTableDescriptors": descriptors,
                "lowOverheadTextureTableDraws": draws,
                "lowOverheadTextureTableUniforms": uniforms,
                "lowOverheadTextureTableFallbacks": fallback,
            }
        )
    graph = re.search(
        r"graphDSA\(tex=(\d+) params=(\d+) fbo=(\d+)\) graphClassic\(tex=(\d+) fbo=(\d+)\)",
        line,
    )
    if graph:
        graph_dsa_tex, graph_dsa_params, graph_dsa_fbo, graph_classic_tex, graph_classic_fbo = graph.groups()
        result.update(
            {
                "lowOverheadGraphDSATextures": graph_dsa_tex,
                "lowOverheadGraphDSAParams": graph_dsa_params,
                "lowOverheadGraphDSAFBO": graph_dsa_fbo,
                "lowOverheadGraphClassicTextures": graph_classic_tex,
                "lowOverheadGraphClassicFBO": graph_classic_fbo,
            }
        )
    upload = re.search(
        r"upload\(persistent=(\d+) default=(\d+) buffers=(\d+) index=(\d+) "
        r"fences=(\d+)/(\d+) waits=(\d+) timeouts=(\d+) fallbacks=(\d+) sync=(\d+)\)",
        line,
    )
    if upload:
        persistent, default, buffers, index, fences_submitted, fences_retired, waits, timeouts, fallbacks, sync = upload.groups()
        result.update(
            {
                "lowOverheadUploadPersistent": persistent,
                "lowOverheadUploadPersistentDefault": default,
                "lowOverheadUploadBuffers": buffers,
                "lowOverheadUploadBufferIndex": index,
                "lowOverheadFenceSubmitted": fences_submitted,
                "lowOverheadFenceRetired": fences_retired,
                "lowOverheadFenceWaits": waits,
                "lowOverheadFenceTimeouts": timeouts,
                "lowOverheadFenceFallbacks": fallbacks,
                "lowOverheadFenceSync": sync,
            }
        )
    return result


def extract_summary(text: str) -> dict[str, str]:
    metrics_line = extract_last_line(text, "rendererMetrics summary")
    if not metrics_line:
        metrics_line = extract_last_line(text, "rendererMetrics frame=")
    summary: dict[str, str] = {
        "benchmarkCapture": extract_last_line(text, "rendererBenchmark capture("),
        "benchmarkInfo": extract_last_line(text, "Renderer benchmark:"),
        "rendererMetrics": metrics_line,
        "lowOverheadMetrics": extract_last_line(text, "rendererMetrics lowOverhead("),
        "uploadManagerInfo": extract_last_line(text, "Renderer upload manager: frameStream="),
        "stateCacheInfo": extract_last_line(text, "Modern GL state cache:"),
        "framePacing": extract_last_line(text, "Frame pacing"),
        "selectedTier": extract_last_line(text, "Selected renderer tier:"),
        "tierContract": extract_last_line(text, "Renderer tier contract:"),
    }
    matches = re.findall(r"rendererBenchmark capture\(.*?samples=(\d+).*?p50=(\d+).*?p95=(\d+).*?p99=(\d+).*?pass=(\d+)", text)
    if matches:
        samples, p50, p95, p99, threshold_pass = matches[-1]
        summary.update(
            {
                "samples": samples,
                "p50": p50,
                "p95": p95,
                "p99": p99,
                "thresholdPass": threshold_pass,
            }
        )
    summary.update(parse_benchmark_capture(summary["benchmarkCapture"]))
    summary.update(parse_renderer_metrics_line(summary["rendererMetrics"]))
    summary.update(parse_upload_manager_info(summary["uploadManagerInfo"]))
    summary.update(parse_state_cache_info(summary["stateCacheInfo"]))
    summary.update(parse_low_overhead_metrics_line(summary["lowOverheadMetrics"]))
    summary.update(parse_frame_pacing(summary["framePacing"]))
    return summary


def summary_float(summary: dict[str, str], key: str) -> float | None:
    value = summary.get(key)
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def load_tga_rgb(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if len(data) < 18:
        raise ValueError("file is too small to be a TGA")
    id_length, color_map_type, image_type = data[0], data[1], data[2]
    if color_map_type != 0 or image_type not in (2, 3):
        raise ValueError(f"unsupported TGA type {image_type} with color map {color_map_type}")
    width = struct.unpack_from("<H", data, 12)[0]
    height = struct.unpack_from("<H", data, 14)[0]
    bits = data[16]
    if width <= 0 or height <= 0 or bits not in (24, 32):
        raise ValueError(f"unsupported TGA dimensions/depth {width}x{height}x{bits}")
    pixel_size = bits // 8
    pixel_count = width * height
    start = 18 + id_length
    end = start + pixel_count * pixel_size
    if len(data) < end:
        raise ValueError("truncated TGA pixel payload")
    pixels = data[start:end]
    rgb = bytearray(pixel_count * 3)
    for i in range(pixel_count):
        src = i * pixel_size
        dst = i * 3
        if image_type == 3:
            value = pixels[src]
            rgb[dst : dst + 3] = bytes((value, value, value))
        else:
            b, g, r = pixels[src], pixels[src + 1], pixels[src + 2]
            rgb[dst : dst + 3] = bytes((r, g, b))
    return width, height, bytes(rgb)


def compare_tga(actual: Path, reference: Path) -> dict[str, Any]:
    aw, ah, ap = load_tga_rgb(actual)
    rw, rh, rp = load_tga_rgb(reference)
    if (aw, ah) != (rw, rh):
        return {
            "status": "dimension-mismatch",
            "actualSize": f"{aw}x{ah}",
            "referenceSize": f"{rw}x{rh}",
        }
    total_sq = 0
    max_delta = 0
    differing = 0
    for a, r in zip(ap, rp):
        delta = abs(a - r)
        if delta:
            differing += 1
            total_sq += delta * delta
            max_delta = max(max_delta, delta)
    rms = math.sqrt(total_sq / max(1, len(ap)))
    return {
        "status": "compared",
        "actualSize": f"{aw}x{ah}",
        "referenceSize": f"{rw}x{rh}",
        "rms": round(rms, 4),
        "maxDelta": max_delta,
        "differingChannels": differing,
    }


def screenshot_reference_candidates(reference_dir: Path, screenshot: Path, savepath: Path) -> list[Path]:
    candidates = [reference_dir / screenshot.name]
    for game_dir in ("baseoq4", "q4base"):
        root = savepath / game_dir
        try:
            rel = screenshot.relative_to(root)
            candidates.insert(0, reference_dir / rel)
        except ValueError:
            pass
    return candidates


def compare_screenshot_if_requested(
    screenshot: Path | None,
    savepath: Path,
    reference_dir: Path | None,
    rms_threshold: float,
    max_threshold: int,
    require_reference: bool,
) -> dict[str, Any]:
    if screenshot is None:
        return {"status": "missing-screenshot"}
    result: dict[str, Any] = {
        "status": "not-requested",
        "actual": str(screenshot),
        "sha256": hashlib.sha256(screenshot.read_bytes()).hexdigest(),
    }
    if reference_dir is None:
        return result
    for candidate in screenshot_reference_candidates(reference_dir, screenshot, savepath):
        if candidate.exists():
            comparison = compare_tga(screenshot, candidate)
            comparison["actual"] = str(screenshot)
            comparison["reference"] = str(candidate)
            if comparison["status"] == "compared":
                comparison["pass"] = comparison["rms"] <= rms_threshold and comparison["maxDelta"] <= max_threshold
            return comparison
    result["status"] = "missing-reference" if require_reference else "reference-not-found"
    result["referenceDir"] = str(reference_dir)
    result["pass"] = not require_reference
    return result


def evaluate_role_result(
    spec: RunSpec,
    role: str,
    exit_code: int,
    timed_out: bool,
    elapsed_seconds: float,
    savepath: Path,
    log_name: str,
    stdout_path: Path,
    stderr_path: Path,
    screenshot_rel: str,
    launch_metadata: dict[str, Any],
    reference_dir: Path | None,
    rms_threshold: float,
    max_threshold: int,
    require_reference: bool,
    require_benchmark: bool = True,
    min_pacing_hz: float = 0.0,
    max_p95_ms: float = 0.0,
    max_p99_ms: float = 0.0,
) -> dict[str, Any]:
    log_path = find_log(savepath, log_name)
    log_text = read_text(log_path)
    stdout_text = read_text(stdout_path)
    stderr_text = read_text(stderr_path)
    text = log_text
    if not text:
        text = stdout_text + "\n" + stderr_text
    screenshot = find_screenshot(savepath, screenshot_rel)
    warnings = warning_counts(text)
    summary = extract_summary(text)
    image = compare_screenshot_if_requested(
        screenshot,
        savepath,
        reference_dir,
        rms_threshold,
        max_threshold,
        require_reference,
    )
    missing: list[str] = []
    if timed_out:
        missing.append("timeout")
    if log_path is None:
        missing.append("log file missing")
        warning = windows_path_length_warning("expected log", launch_metadata.get("expectedLogPaths", []))
        if warning:
            missing.append(warning)
    elif not log_text.strip():
        missing.append("log file empty")
    if not text.strip():
        missing.append("no log/stdout/stderr text")
    if "AutoExecAfterMapLoad: armed" not in text:
        missing.append("autoexec armed marker")
    if "AutoExecAfterMapLoad: waiting for first active draw" not in text:
        missing.append("autoexec waiting-for-active-draw marker")
    if "AutoExecAfterMapLoad: first active draw observed" not in text:
        missing.append("active-draw marker")
    if "AutoExecAfterMapLoad: executed" not in text:
        missing.append("autoexec executed marker")
    if "AutoExecAfterMapLoad: skipped" in text:
        missing.append(extract_last_line(text, "AutoExecAfterMapLoad: skipped"))
    if require_benchmark and "rendererBenchmark capture(" not in text:
        missing.append("renderer benchmark capture line")
    if require_benchmark and "Renderer benchmark:" not in text:
        missing.append("gfxInfo benchmark line")
    if min_pacing_hz > 0.0:
        pacing_hz = summary_float(summary, "pacingHz")
        if pacing_hz is None:
            missing.append("frame pacing Hz")
        elif pacing_hz < min_pacing_hz:
            missing.append(f"pacingHz={pacing_hz:.1f}<{min_pacing_hz:.1f}")
    if max_p95_ms > 0.0:
        p95_ms = summary_float(summary, "pacingP95Ms")
        if p95_ms is None:
            missing.append("frame pacing p95")
        elif p95_ms > max_p95_ms:
            missing.append(f"pacingP95={p95_ms:.1f}>{max_p95_ms:.1f}")
    if max_p99_ms > 0.0:
        p99_ms = summary_float(summary, "pacingP99Ms")
        if p99_ms is None:
            missing.append("frame pacing p99")
        elif p99_ms > max_p99_ms:
            missing.append(f"pacingP99={p99_ms:.1f}>{max_p99_ms:.1f}")
    if "Selected renderer tier:" not in text:
        missing.append("selected tier line")
    if screenshot is None:
        missing.append("screenshot")
        warning = windows_path_length_warning("expected screenshot", launch_metadata.get("expectedScreenshotPaths", []))
        if warning:
            missing.append(warning)
    if any(count > 0 for count in warnings.values()):
        missing += [f"{name}={count}" for name, count in warnings.items() if count > 0]
    if image.get("pass") is False:
        missing.append(f"image comparison {image.get('status')}")

    ok = exit_code == 0 and not timed_out and not missing
    return {
        "id": spec.id,
        "role": role,
        "status": "pass" if ok else "fail",
        "exitCode": exit_code,
        "timedOut": timed_out,
        "elapsedSeconds": round(elapsed_seconds, 2),
        "log": str(log_path) if log_path is not None else "",
        "stdout": str(stdout_path),
        "stderr": str(stderr_path),
        "screenshot": str(screenshot) if screenshot is not None else "",
        "screenshotRequest": screenshot_rel,
        "metadata": launch_metadata,
        "warnings": warnings,
        "missing": missing,
        "summary": summary,
        "image": image,
    }


def launch_and_wait(
    executable: Path,
    args: list[str],
    cwd: Path,
    stdout_path: Path,
    stderr_path: Path,
    timeout_seconds: int,
) -> tuple[int, bool, float]:
    started = time.time()
    timed_out = False
    with stdout_path.open("w", encoding="utf-8", errors="replace") as stdout_file, stderr_path.open("w", encoding="utf-8", errors="replace") as stderr_file:
        process = subprocess.Popen(
            [str(executable)] + args,
            cwd=str(cwd),
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
    return exit_code, timed_out, elapsed


def run_sp_spec(
    root: Path,
    executable: Path,
    output_dir: Path,
    basepath: str,
    run_id: str,
    spec: RunSpec,
    args: argparse.Namespace,
) -> dict[str, Any]:
    filesystem_id = compact_filesystem_id(spec.id)
    savepath = output_dir / "savepaths" / filesystem_id
    savepath.mkdir(parents=True, exist_ok=True)
    log_name = f"openq4_{filesystem_id}.log"
    log_path = find_log(savepath, log_name)
    if log_path is not None:
        log_path.unlink()
    stdout_path = output_dir / f"{filesystem_id}.out.txt"
    stderr_path = output_dir / f"{filesystem_id}.err.txt"
    autoexec_cfg, screenshot_rel = write_autoexec_cfg(
        savepath,
        spec,
        "sp",
        run_id,
        args.settle_frames,
        args.sample_frames,
        args.sample_msec,
        5,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
        args.renderer_metrics_level,
    )
    game_args = common_args(
        root,
        savepath,
        log_name,
        basepath,
        spec,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        spec.launch_cvars + args.launch_cvars,
        autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(game_args, "si_gameType", "singleplayer")
    append_command(game_args, "map", spec.map_name)
    cwd = root / ".install"
    sp_metadata = role_launch_metadata(
        root,
        executable,
        game_args,
        cwd,
        savepath,
        basepath,
        "sp",
        log_name,
        screenshot_rel,
        autoexec_cfg=autoexec_cfg,
        launch_cvars=spec.launch_cvars + args.launch_cvars,
        script_cvars=args.extra_cvars,
        exec_commands=args.exec_commands,
    )

    if args.dry_run:
        return {
            "id": spec.id,
            "filesystemId": filesystem_id,
            "mode": spec.mode,
            "map": spec.map_name,
            "purpose": spec.purpose,
            "tier": spec.tier,
            "maxfps": spec.maxfps,
            "swapInterval": spec.swap_interval,
            "display": spec.display_mode,
            "shadowPreset": spec.shadow_preset,
            "uploadVariant": spec.upload_variant,
            "launchCvars": list(spec.launch_cvars),
            "status": "planned",
            "args": game_args,
            "autoexecCfg": autoexec_cfg,
            "screenshotRequest": screenshot_rel,
            "lensFlarePreset": spec.lens_flare_preset,
            "renderer": spec.renderer,
            "roles": [
                {
                    "role": "sp",
                    "status": "planned",
                    "log": "",
                    "screenshot": "",
                    "screenshotRequest": screenshot_rel,
                    "metadata": sp_metadata,
                    "missing": [],
                }
            ],
        }

    exit_code, timed_out, elapsed = launch_and_wait(
        executable,
        game_args,
        cwd,
        stdout_path,
        stderr_path,
        args.timeout,
    )
    role_result = evaluate_role_result(
        spec,
        "sp",
        exit_code,
        timed_out,
        elapsed,
        savepath,
        log_name,
        stdout_path,
        stderr_path,
        screenshot_rel,
        sp_metadata,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        not args.pacing_only,
        args.min_pacing_hz,
        args.max_p95_ms,
        args.max_p99_ms,
    )
    return {
        "id": spec.id,
        "mode": spec.mode,
        "map": spec.map_name,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "lensFlarePreset": spec.lens_flare_preset,
        "renderer": spec.renderer,
        "uploadVariant": spec.upload_variant,
        "launchCvars": list(spec.launch_cvars),
        "status": role_result["status"],
        "roles": [role_result],
    }


def run_mp_spec(
    root: Path,
    executable: Path,
    output_dir: Path,
    basepath: str,
    run_id: str,
    spec: RunSpec,
    index: int,
    args: argparse.Namespace,
) -> dict[str, Any]:
    port = args.mp_port + index
    filesystem_id = compact_filesystem_id(spec.id)
    server_savepath = output_dir / "savepaths" / f"{filesystem_id}_server"
    client_savepath = output_dir / "savepaths" / f"{filesystem_id}_client"
    server_savepath.mkdir(parents=True, exist_ok=True)
    client_savepath.mkdir(parents=True, exist_ok=True)

    server_log = f"openq4_{filesystem_id}_server.log"
    client_log = f"openq4_{filesystem_id}_client.log"
    for savepath, log_name in ((server_savepath, server_log), (client_savepath, client_log)):
        log_path = find_log(savepath, log_name)
        if log_path is not None:
            log_path.unlink()

    server_stdout = output_dir / f"{filesystem_id}_server.out.txt"
    server_stderr = output_dir / f"{filesystem_id}_server.err.txt"
    client_stdout = output_dir / f"{filesystem_id}_client.out.txt"
    client_stderr = output_dir / f"{filesystem_id}_client.err.txt"

    server_autoexec_cfg, server_screenshot = write_autoexec_cfg(
        server_savepath,
        spec,
        "server",
        run_id,
        args.settle_frames + args.mp_client_delay_frames,
        args.sample_frames,
        args.sample_msec,
        args.mp_server_quit_delay_frames,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
        args.renderer_metrics_level,
    )
    server_args = common_args(
        root,
        server_savepath,
        server_log,
        basepath,
        spec,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        spec.launch_cvars + args.launch_cvars,
        server_autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(server_args, "net_serverDedicated", "0")
    append_set(server_args, "net_port", str(port))
    server_args += ["+seta", "si_pure", "0"]
    append_set(server_args, "net_serverAllowServerMod", "1")
    append_set(server_args, "sv_cheats", "1")
    append_set(server_args, "si_gameType", "DM")
    append_command(server_args, "spawnServer", spec.map_name)

    client_autoexec_cfg, client_screenshot = write_autoexec_cfg(
        client_savepath,
        spec,
        "client",
        run_id,
        args.settle_frames,
        args.sample_frames,
        args.sample_msec,
        5,
        args.extra_cvars,
        args.exec_commands,
        args.gpu_timers,
        not args.pacing_only,
        args.renderer_metrics_level,
    )
    client_args = common_args(
        root,
        client_savepath,
        client_log,
        basepath,
        spec,
        args.benchmark_preset,
        args.modern_executor,
        args.show_fps_overlay,
        spec.launch_cvars + args.launch_cvars,
        client_autoexec_cfg,
        args.autoexec_delay_ms,
    )
    append_set(client_args, "ui_name", "RendererBenchClient")
    append_command(client_args, "connect", f"127.0.0.1:{port}")
    cwd = root / ".install"
    server_metadata = role_launch_metadata(
        root,
        executable,
        server_args,
        cwd,
        server_savepath,
        basepath,
        "server",
        server_log,
        server_screenshot,
        port,
        autoexec_cfg=server_autoexec_cfg,
        launch_cvars=spec.launch_cvars + args.launch_cvars,
        script_cvars=args.extra_cvars,
        exec_commands=args.exec_commands,
    )
    client_metadata = role_launch_metadata(
        root,
        executable,
        client_args,
        cwd,
        client_savepath,
        basepath,
        "client",
        client_log,
        client_screenshot,
        port,
        autoexec_cfg=client_autoexec_cfg,
        launch_cvars=spec.launch_cvars + args.launch_cvars,
        script_cvars=args.extra_cvars,
        exec_commands=args.exec_commands,
    )

    if args.dry_run:
        return {
            "id": spec.id,
            "filesystemId": filesystem_id,
            "mode": spec.mode,
            "map": spec.map_name,
            "purpose": spec.purpose,
            "tier": spec.tier,
            "maxfps": spec.maxfps,
            "swapInterval": spec.swap_interval,
            "display": spec.display_mode,
            "shadowPreset": spec.shadow_preset,
            "uploadVariant": spec.upload_variant,
            "launchCvars": list(spec.launch_cvars),
            "status": "planned",
            "serverArgs": server_args,
            "clientArgs": client_args,
            "serverAutoexecCfg": server_autoexec_cfg,
            "clientAutoexecCfg": client_autoexec_cfg,
            "serverScreenshotRequest": server_screenshot,
            "clientScreenshotRequest": client_screenshot,
            "lensFlarePreset": spec.lens_flare_preset,
            "renderer": spec.renderer,
            "roles": [
                {
                    "role": "server",
                    "status": "planned",
                    "log": "",
                    "screenshot": "",
                    "screenshotRequest": server_screenshot,
                    "metadata": server_metadata,
                    "missing": [],
                },
                {
                    "role": "client",
                    "status": "planned",
                    "log": "",
                    "screenshot": "",
                    "screenshotRequest": client_screenshot,
                    "metadata": client_metadata,
                    "missing": [],
                },
            ],
        }

    started = time.time()
    server_timed_out = False
    client_timed_out = False
    with server_stdout.open("w", encoding="utf-8", errors="replace") as server_out, server_stderr.open("w", encoding="utf-8", errors="replace") as server_err:
        server_process = subprocess.Popen(
            [str(executable)] + server_args,
            cwd=str(cwd),
            stdout=server_out,
            stderr=server_err,
        )
    time.sleep(max(1, args.mp_client_delay))
    with client_stdout.open("w", encoding="utf-8", errors="replace") as client_out, client_stderr.open("w", encoding="utf-8", errors="replace") as client_err:
        client_process = subprocess.Popen(
            [str(executable)] + client_args,
            cwd=str(cwd),
            stdout=client_out,
            stderr=client_err,
        )

    try:
        client_exit = client_process.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        client_timed_out = True
        client_process.kill()
        client_exit = client_process.wait(timeout=10)

    remaining = max(10, args.timeout - int(time.time() - started))
    try:
        server_exit = server_process.wait(timeout=remaining)
    except subprocess.TimeoutExpired:
        server_timed_out = True
        server_process.kill()
        server_exit = server_process.wait(timeout=10)

    elapsed = time.time() - started
    server_result = evaluate_role_result(
        spec,
        "server",
        server_exit,
        server_timed_out,
        elapsed,
        server_savepath,
        server_log,
        server_stdout,
        server_stderr,
        server_screenshot,
        server_metadata,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        not args.pacing_only,
        args.min_pacing_hz,
        args.max_p95_ms,
        args.max_p99_ms,
    )
    client_result = evaluate_role_result(
        spec,
        "client",
        client_exit,
        client_timed_out,
        elapsed,
        client_savepath,
        client_log,
        client_stdout,
        client_stderr,
        client_screenshot,
        client_metadata,
        args.reference_dir_path,
        args.image_rms_threshold,
        args.image_max_threshold,
        args.require_references,
        not args.pacing_only,
        args.min_pacing_hz,
        args.max_p95_ms,
        args.max_p99_ms,
    )
    ok = server_result["status"] == "pass" and client_result["status"] == "pass"
    return {
        "id": spec.id,
        "filesystemId": filesystem_id,
        "mode": spec.mode,
        "map": spec.map_name,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "lensFlarePreset": spec.lens_flare_preset,
        "renderer": spec.renderer,
        "uploadVariant": spec.upload_variant,
        "launchCvars": list(spec.launch_cvars),
        "status": "pass" if ok else "fail",
        "port": port,
        "roles": [server_result, client_result],
    }


def harness_failure_result(spec: RunSpec, exc: Exception) -> dict[str, Any]:
    message = f"harness exception: {type(exc).__name__}: {exc}"
    role = "client" if spec.mode == "MP" else "sp"
    role_result = {
        "id": spec.id,
        "role": role,
        "status": "fail",
        "exitCode": "",
        "timedOut": False,
        "elapsedSeconds": 0.0,
        "log": "",
        "stdout": "",
        "stderr": "",
        "screenshot": "",
        "screenshotRequest": "",
        "warnings": {},
        "missing": [message],
        "summary": {},
        "image": {"status": "harness-error"},
    }
    return {
        "id": spec.id,
        "filesystemId": filesystem_id,
        "mode": spec.mode,
        "map": spec.map_name,
        "purpose": spec.purpose,
        "tier": spec.tier,
        "maxfps": spec.maxfps,
        "swapInterval": spec.swap_interval,
        "display": spec.display_mode,
        "shadowPreset": spec.shadow_preset,
        "lensFlarePreset": spec.lens_flare_preset,
        "renderer": spec.renderer,
        "uploadVariant": spec.upload_variant,
        "launchCvars": list(spec.launch_cvars),
        "status": "fail",
        "roles": [role_result],
        "harnessError": message,
    }


def build_specs(args: argparse.Namespace) -> list[RunSpec]:
    defaults = PROFILE_DEFAULTS[args.profile]
    case_ids = split_csv(args.cases, defaults["cases"])
    tiers = split_csv(args.tiers, defaults["tiers"])
    maxfps_values = split_csv(args.maxfps, defaults["maxfps"])
    swap_values = split_csv(args.swap_intervals, defaults["swap"])
    display_values = split_csv(args.display_modes, defaults["display"])
    shadow_values = split_csv(args.shadow_presets, defaults["shadows"])
    lens_flare_values = split_csv(args.lens_flare_presets, defaults["lensFlare"])
    launch_variant_values = split_csv(args.launch_variants, profile_launch_variants(defaults))

    specs: list[RunSpec] = []
    for case_id in case_ids:
        if case_id not in ALL_SCENES:
            raise ValueError(f"unknown case '{case_id}'. Use --list to inspect valid cases.")
        scene = ALL_SCENES[case_id]
        for tier in tiers:
            if tier not in SAFE_TIERS:
                raise ValueError(f"unknown r_glTier '{tier}'")
            for maxfps in maxfps_values:
                for swap in swap_values:
                    for display in display_values:
                        if display not in DISPLAY_MODES:
                            raise ValueError(f"unknown display mode '{display}'")
                        for shadow in shadow_values:
                            if shadow not in SHADOW_PRESETS:
                                raise ValueError(f"unknown shadow preset '{shadow}'")
                            for lens_flare in lens_flare_values:
                                if lens_flare not in LENS_FLARE_PRESETS:
                                    raise ValueError(f"unknown lens-flare preset '{lens_flare}'")
                                for launch_variant in launch_variant_values:
                                    specs.append(
                                        RunSpec(
                                            case_id=case_id,
                                            mode=scene["mode"],
                                            map_name=scene["map"],
                                            purpose=scene["purpose"],
                                            path_name=scene["path"],
                                            tier=tier,
                                            maxfps=maxfps,
                                            swap_interval=swap,
                                            display_mode=display,
                                            shadow_preset=shadow,
                                            lens_flare_preset=lens_flare,
                                            renderer=args.renderer,
                                            upload_variant=launch_variant,
                                            variant_prefix=profile_launch_variant_prefix(args.profile),
                                            launch_cvars=profile_launch_variant_cvars(args.profile, launch_variant),
                                        )
                                    )
    if args.limit > 0:
        specs = specs[: args.limit]
    return specs


def format_upload_amount(summary: dict[str, str], kb_key: str, bytes_key: str = "") -> str:
    if summary.get(kb_key):
        return f"{summary[kb_key]}KB"
    if bytes_key and summary.get(bytes_key):
        return f"{summary[bytes_key]}B"
    return ""


def format_ring_summary(summary: dict[str, str]) -> str:
    high = summary.get("ringHighWaterKB", "")
    capacity = summary.get("ringCapacityKB") or summary.get("uploadRingCapacityKB", "")
    if high or capacity:
        return f"{high or '?'}/{capacity or '?'}KB"
    return ""


def format_fence_summary(summary: dict[str, str]) -> str:
    submitted = summary.get("fenceSubmitted", "")
    retired = summary.get("fenceRetired", "")
    waits = summary.get("fenceWaits", "")
    timeouts = summary.get("fenceTimeouts", "")
    fallbacks = summary.get("fenceFallbacks", "")
    if submitted or retired or waits or timeouts or fallbacks:
        return f"{submitted or '?'}/{retired or '?'} wait={waits or '?'} timeout={timeouts or '?'} fallback={fallbacks or '?'}"
    return ""


def format_upload_path_summary(summary: dict[str, str]) -> str:
    stream = summary.get("uploadFrameStream", "")
    persistent = summary.get("uploadPersistent", "")
    map_range = summary.get("uploadMapRangeFallback", "")
    buffers = summary.get("uploadBuffers", "")
    parts: list[str] = []
    if stream:
        parts.append(stream)
    if persistent:
        parts.append(f"persistent={persistent}")
    if map_range:
        parts.append(f"mapRange={map_range}")
    if buffers:
        parts.append(f"buffers={buffers}")
    return ", ".join(parts)


def format_state_cache_summary(summary: dict[str, str]) -> str:
    hits = summary.get("stateCacheHits") or summary.get("stateCacheInfoHits", "")
    misses = summary.get("stateCacheMisses") or summary.get("stateCacheInfoMisses", "")
    invalidations = summary.get("stateCacheInvalidations") or summary.get("stateCacheInfoInvalidations", "")
    legacy_resets = summary.get("stateCacheLegacyResets") or summary.get("stateCacheInfoLegacyResets", "")
    if hits or misses or invalidations or legacy_resets:
        return f"{hits or '?'}/{misses or '?'} invalid={invalidations or '?'} legacyReset={legacy_resets or '?'}"
    return ""


def format_cpu_phase_summary(summary: dict[str, str]) -> str:
    keys = (
        ("fe", "frontEndMs"),
        ("vis", "visibilityMs"),
        ("pkt", "packetMs"),
        ("graph", "graphMs"),
        ("submit", "submitMs"),
        ("be", "backendMs"),
        ("present", "presentMs"),
    )
    parts = [f"{label}={summary[key]}ms" for label, key in keys if summary.get(key)]
    return " ".join(parts)


def format_pacing_summary(summary: dict[str, str]) -> str:
    if not summary.get("pacingHz"):
        return ""
    parts = [f"{summary['pacingHz']}Hz"]
    if summary.get("pacingP50Ms"):
        parts.append(f"p50={summary['pacingP50Ms']}ms")
    if summary.get("pacingP95Ms"):
        parts.append(f"p95={summary['pacingP95Ms']}ms")
    if summary.get("pacingP99Ms"):
        parts.append(f"p99={summary['pacingP99Ms']}ms")
    if summary.get("pacingMaxMs"):
        parts.append(f"max={summary['pacingMaxMs']}ms")
    return " ".join(parts)


def format_graph_invalidate_skip_summary(summary: dict[str, str]) -> str:
    skip_keys = [
        ("unavailable", "graphInvalidateSkippedUnavailable"),
        ("invalid", "graphInvalidateSkippedInvalid"),
        ("imported", "graphInvalidateSkippedImported"),
        ("buffer", "graphInvalidateSkippedBuffer"),
        ("nonTransient", "graphInvalidateSkippedNonTransient"),
        ("presentable", "graphInvalidateSkippedPresentable"),
        ("later", "graphInvalidateSkippedLater"),
        ("incomplete", "graphInvalidateSkippedIncomplete"),
        ("unsupported", "graphInvalidateSkippedUnsupported"),
    ]
    parts = [f"{label}={summary[key]}" for label, key in skip_keys if summary.get(key)]
    return " ".join(parts)


def format_low_overhead_cap_summary(summary: dict[str, str]) -> str:
    if not summary.get("lowOverheadRequested"):
        return ""
    return (
        f"req={summary.get('lowOverheadRequested', '?')} "
        f"ready={summary.get('lowOverheadReady', '?')} "
        f"dsa={summary.get('lowOverheadDSA', '?')} "
        f"multi={summary.get('lowOverheadMultiBind', '?')}"
    )


def format_low_overhead_multibind_summary(summary: dict[str, str]) -> str:
    if not any(summary.get(key) for key in ("lowOverheadBufferMultiBind", "lowOverheadTextureMultiBind", "lowOverheadSamplerMultiBind")):
        return ""
    return (
        f"buf={summary.get('lowOverheadBufferMultiBind', '?')} "
        f"tex={summary.get('lowOverheadTextureMultiBind', '?')} "
        f"samp={summary.get('lowOverheadSamplerMultiBind', '?')}"
    )


def format_low_overhead_graph_summary(summary: dict[str, str]) -> str:
    if not any(summary.get(key) for key in ("lowOverheadGraphDSATextures", "lowOverheadGraphClassicTextures")):
        return ""
    return (
        f"dsa={summary.get('lowOverheadGraphDSATextures', '?')}/"
        f"{summary.get('lowOverheadGraphDSAParams', '?')}/"
        f"{summary.get('lowOverheadGraphDSAFBO', '?')} "
        f"classic={summary.get('lowOverheadGraphClassicTextures', '?')}/"
        f"{summary.get('lowOverheadGraphClassicFBO', '?')}"
    )


def format_low_overhead_upload_summary(summary: dict[str, str]) -> str:
    if not any(summary.get(key) for key in ("lowOverheadUploadPersistent", "lowOverheadFenceSubmitted")):
        return ""
    return (
        f"persistent={summary.get('lowOverheadUploadPersistent', '?')}/"
        f"{summary.get('lowOverheadUploadPersistentDefault', '?')} "
        f"fences={summary.get('lowOverheadFenceSubmitted', '?')}/"
        f"{summary.get('lowOverheadFenceRetired', '?')} "
        f"wait={summary.get('lowOverheadFenceWaits', '?')} "
        f"timeout={summary.get('lowOverheadFenceTimeouts', '?')} "
        f"fallback={summary.get('lowOverheadFenceFallbacks', '?')}"
    )


def markdown_cell(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("\r", " ").replace("\n", " ").replace("|", "\\|")


def format_metadata_cvars(items: Any) -> str:
    if not items:
        return "none"
    parts: list[str] = []
    if isinstance(items, dict):
        iterable = [{"name": key, "value": value} for key, value in items.items()]
    else:
        iterable = items
    for item in iterable:
        if isinstance(item, dict):
            name = item.get("name", "")
            value = item.get("value", "")
        else:
            try:
                name, value = item
            except (TypeError, ValueError):
                parts.append(markdown_cell(item))
                continue
        if name:
            parts.append(f"{name}={value}")
    return markdown_cell(", ".join(parts) if parts else "none")


def format_metadata_commands(items: Any) -> str:
    if not items:
        return "none"
    return markdown_cell("; ".join(str(item) for item in items))


def write_reports(output_dir: Path, results: list[dict[str, Any]], metadata: dict[str, Any]) -> tuple[Path, Path]:
    report_json = output_dir / "renderer_gameplay_benchmark_report.json"
    report_md = output_dir / "renderer_gameplay_benchmark_report.md"
    payload = {
        "metadata": metadata,
        "requiredScenes": REQUIRED_SCENES,
        "shadowScenes": SHADOW_SCENES,
        "lensFlareScenes": LENS_FLARE_SCENES,
        "shadowPresets": SHADOW_PRESETS,
        "lensFlarePresets": LENS_FLARE_PRESETS,
        "lensFlareSignoffMatrix": LENS_FLARE_SIGNOFF_MATRIX,
        "uploadPressureVariants": UPLOAD_PRESSURE_VARIANTS,
        "graphInvalidationVariants": GRAPH_INVALIDATION_VARIANTS,
        "performanceComparisonVariants": PERFORMANCE_COMPARISON_VARIANTS,
        "results": results,
    }
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    passed = sum(1 for result in results if result["status"] == "pass")
    failed = sum(1 for result in results if result["status"] == "fail")
    planned = sum(1 for result in results if result["status"] == "planned")
    lines = [
        "# Renderer Gameplay Benchmark Report",
        "",
        f"- Generated: {metadata['generated']}",
        f"- Host: {metadata['host']}",
        f"- Executable: `{metadata['executable']}`",
        f"- Base path: `{metadata['basepath'] or 'not set'}`",
        f"- Dev path: `{metadata.get('devpath') or 'not set'}`",
        f"- Game dir: `{metadata.get('gameDir', 'baseoq4')}`",
        f"- Profile: `{metadata['profile']}`",
        f"- Launch variants: `{', '.join(metadata.get('launchVariants', [])) or 'none'}`",
        f"- Sample: `{metadata['sampleMsec']} ms`" if metadata.get("sampleMsec", 0) > 0 else f"- Sample: `{metadata['sampleFrames']} frames`",
        f"- Renderer metrics level: `{metadata.get('rendererMetricsLevel', 1)}`",
        f"- Modern executor: `{1 if metadata.get('modernExecutor', False) else 0}`",
        f"- Profile cvars: {format_metadata_cvars(metadata.get('profileCvars', {}))}",
        f"- Launch cvars: {format_metadata_cvars(metadata.get('launchCvars', {}))}",
        f"- Script cvars: {format_metadata_cvars(metadata.get('scriptCvars', []))}",
        f"- Exec commands: {format_metadata_commands(metadata.get('execCommands', []))}",
        f"- Reference dir: `{metadata.get('referenceDir') or 'not set'}`",
        f"- Require references: `{1 if metadata.get('requireReferences', False) else 0}`",
        f"- Image thresholds: RMS `{metadata.get('imageRmsThreshold', 2.0)}`, max `{metadata.get('imageMaxThreshold', 24)}`",
        f"- Cases: {passed} passed, {failed} failed, {planned} planned",
        "",
        "## Results",
        "",
        "| Status | Case | Mode | Map | Tier | FPS | VSync | Display | Shadow | Lens Flare | Variant | Pacing | Benchmark | Image | Screenshot | Log |",
        "|---|---|---|---|---|---:|---:|---|---|---|---|---|---|---|---|---|",
    ]
    for result in results:
        if result["status"] == "planned":
            lines.append(
                f"| planned | `{result['id']}` | {result['mode']} | `{result['map']}` | `{result['tier']}` | {result['maxfps']} | {result['swapInterval']} | {result['display']} | `{result['shadowPreset']}` | `{result.get('lensFlarePreset', 'default')}` | `{result.get('uploadVariant', 'default')}` | dry run | dry run | planned | `{result.get('screenshotRequest', '')}` |  |"
            )
            continue
        role = next((item for item in result.get("roles", []) if item["role"] in ("client", "sp")), result.get("roles", [{}])[0])
        summary = role.get("summary", {})
        benchmark = summary.get("benchmarkCapture", "")
        if len(benchmark) > 80:
            benchmark = benchmark[:77] + "..."
        pacing = ""
        if summary.get("pacingHz"):
            pacing = f"{summary['pacingHz']} Hz"
            if summary.get("pacingP95Ms"):
                pacing += f" / p95 {summary['pacingP95Ms']} ms"
        image = role.get("image", {}) or {}
        image_status = image.get("status", "missing")
        if image_status == "compared":
            image_status = f"compared rms={image.get('rms', '?')} max={image.get('maxDelta', '?')} pass={int(bool(image.get('pass', False)))}"
        elif image_status in ("not-requested", "reference-not-found"):
            image_status = f"{image_status} {image.get('sha256', '')[:12]}".strip()
        screenshot = role.get("screenshot", "")
        log = role.get("log", "")
        lines.append(
            f"| {result['status']} | `{result['id']}` | {result['mode']} | `{result['map']}` | `{result['tier']}` | {result['maxfps']} | {result['swapInterval']} | {result['display']} | `{result['shadowPreset']}` | `{result.get('lensFlarePreset', 'default')}` | `{result.get('uploadVariant', 'default')}` | {pacing or 'missing'} | {benchmark or 'missing'} | {image_status} | `{screenshot}` | `{log}` |"
        )
        for role_result in result.get("roles", []):
            if role_result.get("missing"):
                lines.append(
                    f"|  | `{role_result['role']}` missing |  |  |  |  |  |  |  |  |  | {'; '.join(role_result['missing'])} |  |  |  |  |"
                )

    role_rows = [
        (result, role_result)
        for result in results
        for role_result in result.get("roles", [])
    ]
    if role_rows:
        lines += [
            "",
            "## Role Reproduction Metadata",
            "",
            "Full launch commands are stored in each role's JSON metadata; failed and planned roles also print them below.",
            "",
            "| Case | Role | Status | Elapsed | Timeout | Working Directory | Save Path | Dev Path | Game | Autoexec | Launch Cvars | Script Cvars | Exec Commands | Command Recorded |",
            "|---|---|---|---:|---|---|---|---|---|---|---|---|---|---|",
        ]
        for result, role_result in role_rows:
            role_metadata = role_result.get("metadata", {}) or {}
            command_recorded = "yes" if role_metadata.get("launchCommand") else "no"
            lines.append(
                f"| `{result['id']}` | `{role_result.get('role', '')}` | {role_result.get('status', result.get('status', ''))} | "
                f"{role_result.get('elapsedSeconds', '')} | "
                f"{role_result.get('timedOut', '')} | "
                f"`{markdown_cell(role_metadata.get('workingDirectory', ''))}` | "
                f"`{markdown_cell(role_metadata.get('savepath', ''))}` | "
                f"`{markdown_cell(role_metadata.get('devpath', ''))}` | "
                f"`{markdown_cell(role_metadata.get('gameDir', ''))}` | "
                f"`{markdown_cell(role_metadata.get('autoexecCfg', ''))}` | "
                f"{format_metadata_cvars(role_metadata.get('launchCvars', []))} | "
                f"{format_metadata_cvars(role_metadata.get('scriptCvars', []))} | "
                f"{format_metadata_commands(role_metadata.get('execCommands', []))} | "
                f"{command_recorded} |"
            )

    metric_roles = [
        (result, role_result)
        for result in results
        for role_result in result.get("roles", [])
        if role_result.get("status") != "planned"
    ]
    if metric_roles:
        lines += [
            "",
            "## Upload And State Metrics",
            "",
            "| Case | Role | Variant | Benchmark Upload | Metrics Upload | Ring | Overflow | Stalls | Fences | Upload Path | State Cache |",
            "|---|---|---|---:|---:|---|---:|---:|---|---|---|",
        ]
        for result, role_result in metric_roles:
            summary = role_result.get("summary", {}) or {}
            lines.append(
                f"| `{result['id']}` | `{role_result.get('role', '')}` | `{result.get('uploadVariant', 'default')}` | "
                f"{format_upload_amount(summary, 'benchmarkUploadKB') or 'missing'} | "
                f"{format_upload_amount(summary, 'metricsUploadKB', 'metricsUploadBytes') or 'missing'} | "
                f"{format_ring_summary(summary) or 'missing'} | "
                f"{format_upload_amount(summary, 'ringOverflowKB') or 'missing'} | "
                f"{summary.get('frameStalls', 'missing')} | "
                f"{format_fence_summary(summary) or 'missing'} | "
                f"{format_upload_path_summary(summary) or 'missing'} | "
                f"{format_state_cache_summary(summary) or 'missing'} |"
            )

    presentation_roles = [
        (result, role_result)
        for result, role_result in metric_roles
        if (role_result.get("summary", {}) or {}).get("pacingHz")
    ]
    if presentation_roles:
        lines += [
            "",
            "## Presentation Pacing",
            "",
            "| Case | Role | FPS Cap | VSync | Display | Variant | Present Avg | Hz | P50 | P95 | P99 | Max |",
            "|---|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|",
        ]
        for result, role_result in presentation_roles:
            summary = role_result.get("summary", {}) or {}
            lines.append(
                f"| `{result['id']}` | `{role_result.get('role', '')}` | "
                f"{result.get('maxfps', '')} | {result.get('swapInterval', '')} | "
                f"{result.get('display', '')} | `{result.get('uploadVariant', 'default')}` | "
                f"{summary.get('pacingPresentMs', 'missing')} | "
                f"{summary.get('pacingHz', 'missing')} | "
                f"{summary.get('pacingP50Ms', 'missing')} | "
                f"{summary.get('pacingP95Ms', 'missing')} | "
                f"{summary.get('pacingP99Ms', 'missing')} | "
                f"{summary.get('pacingMaxMs', 'missing')} |"
            )

    performance_roles = [
        (result, role_result)
        for result, role_result in metric_roles
        if (role_result.get("summary", {}) or {}).get("frontEndMs")
    ]
    if performance_roles:
        lines += [
            "",
            "## Performance Phase Metrics",
            "",
            "| Case | Role | Tier | Variant | Pacing | CPU Phases | GPU | Draws | Upload | State Cache |",
            "|---|---|---|---|---|---|---|---:|---:|---|",
        ]
        for result, role_result in performance_roles:
            summary = role_result.get("summary", {}) or {}
            lines.append(
                f"| `{result['id']}` | `{role_result.get('role', '')}` | "
                f"`{summary.get('metricsTier') or result.get('tier', '')}` | "
                f"`{result.get('uploadVariant', 'default')}` | "
                f"{format_pacing_summary(summary) or 'missing'} | "
                f"{format_cpu_phase_summary(summary) or 'missing'} | "
                f"{summary.get('gpuMs', 'missing')} | "
                f"{summary.get('draws', 'missing')} | "
                f"{format_upload_amount(summary, 'metricsUploadKB', 'metricsUploadBytes') or 'missing'} | "
                f"{format_state_cache_summary(summary) or 'missing'} |"
            )

    graph_invalidate_roles = [
        (result, role_result)
        for result, role_result in metric_roles
        if (role_result.get("summary", {}) or {}).get("graphInvalidateTagged")
    ]
    if graph_invalidate_roles:
        lines += [
            "",
            "## Render Graph Invalidation",
            "",
            "| Case | Role | Tier | Variant | Enabled | Tagged | Candidates | Armed | Submitted | Skipped | Skip Breakdown |",
            "|---|---|---|---|---:|---:|---:|---:|---:|---:|---|",
        ]
        for result, role_result in graph_invalidate_roles:
            summary = role_result.get("summary", {}) or {}
            lines.append(
                f"| `{result['id']}` | `{role_result.get('role', '')}` | `{result.get('tier', '')}` | "
                f"`{result.get('uploadVariant', 'default')}` | "
                f"{summary.get('graphInvalidateEnabled', 'missing')} | "
                f"{summary.get('graphInvalidateTagged', 'missing')} | "
                f"{summary.get('graphInvalidateCandidates', 'missing')} | "
                f"{summary.get('graphInvalidateArmed', 'missing')} | "
                f"{summary.get('graphInvalidateSubmitted', 'missing')} | "
                f"{summary.get('graphInvalidateSkipped', 'missing')} | "
                f"{format_graph_invalidate_skip_summary(summary) or 'missing'} |"
            )

    low_overhead_roles = [
        (result, role_result)
        for result, role_result in metric_roles
        if (role_result.get("summary", {}) or {}).get("lowOverheadMetrics")
    ]
    if low_overhead_roles:
        lines += [
            "",
            "## Low Overhead Metrics",
            "",
            "| Case | Role | Tier | Capabilities | DSA Updates | Multibind | Classic Texture Binds | Compacted | Graph DSA/Classic | Upload/Fences | State Cache |",
            "|---|---|---|---|---:|---|---:|---:|---|---|---|",
        ]
        for result, role_result in low_overhead_roles:
            summary = role_result.get("summary", {}) or {}
            lines.append(
                f"| `{result['id']}` | `{role_result.get('role', '')}` | `{result.get('tier', '')}` | "
                f"{format_low_overhead_cap_summary(summary) or 'missing'} | "
                f"{summary.get('lowOverheadDSAUpdates', 'missing')} | "
                f"{format_low_overhead_multibind_summary(summary) or 'missing'} | "
                f"{summary.get('lowOverheadClassicTextureBinds', 'missing')} | "
                f"{summary.get('lowOverheadCompactedBatches', 'missing')} | "
                f"{format_low_overhead_graph_summary(summary) or 'missing'} | "
                f"{format_low_overhead_upload_summary(summary) or 'missing'} | "
                f"{format_state_cache_summary(summary) or 'missing'} |"
            )

    failed_roles = [
        (result, role_result)
        for result in results
        for role_result in result.get("roles", [])
        if role_result.get("status") == "fail"
    ]
    planned_roles = [
        (result, role_result)
        for result in results
        for role_result in result.get("roles", [])
        if role_result.get("status") == "planned"
    ]
    if failed_roles:
        lines += ["", "## Failed Role Details", ""]
        for result, role_result in failed_roles:
            role_metadata = role_result.get("metadata", {}) or {}
            expected_logs = role_metadata.get("expectedLogPaths", [])
            expected_shots = role_metadata.get("expectedScreenshotPaths", [])
            expected_autoexec = role_metadata.get("expectedAutoexecPaths", [])
            lines += [
                f"### `{result['id']}` / `{role_result.get('role', '')}`",
                "",
                f"- Exit: `{role_result.get('exitCode', '')}`; timed out: `{role_result.get('timedOut', '')}`; elapsed: `{role_result.get('elapsedSeconds', '')} s`",
                f"- Working directory: `{role_metadata.get('workingDirectory', '')}`",
                f"- Save path: `{role_metadata.get('savepath', '')}`",
                f"- Dev path: `{role_metadata.get('devpath', '')}`",
                f"- Base path: `{role_metadata.get('basepath') or 'not set'}`",
                f"- Game dir: `{role_metadata.get('gameDir', '')}`; port: `{role_metadata.get('port', '')}`",
                f"- Autoexec cfg: `{role_metadata.get('autoexecCfg', '')}`",
                f"- Expected autoexec: `{expected_autoexec[0] if expected_autoexec else ''}`",
                f"- Expected log: `{expected_logs[0] if expected_logs else ''}`",
                f"- Expected screenshot: `{expected_shots[0] if expected_shots else role_result.get('screenshotRequest', '')}`",
                f"- Launch cvars: {format_metadata_cvars(role_metadata.get('launchCvars', []))}",
                f"- Script cvars: {format_metadata_cvars(role_metadata.get('scriptCvars', []))}",
                f"- Exec commands: {format_metadata_commands(role_metadata.get('execCommands', []))}",
                f"- Missing: {'; '.join(role_result.get('missing', [])) or 'none'}",
                "",
                "```text",
                role_metadata.get("launchCommand", ""),
                "```",
                "",
            ]

    if planned_roles:
        lines += ["", "## Planned Role Details", ""]
        for result, role_result in planned_roles:
            role_metadata = role_result.get("metadata", {}) or {}
            expected_autoexec = role_metadata.get("expectedAutoexecPaths", [])
            lines += [
                f"### `{result['id']}` / `{role_result.get('role', '')}`",
                "",
                f"- Working directory: `{role_metadata.get('workingDirectory', '')}`",
                f"- Save path: `{role_metadata.get('savepath', '')}`",
                f"- Game dir: `{role_metadata.get('gameDir', '')}`; port: `{role_metadata.get('port', '')}`",
                f"- Autoexec cfg: `{role_metadata.get('autoexecCfg', '')}`",
                f"- Expected autoexec: `{expected_autoexec[0] if expected_autoexec else ''}`",
                f"- Launch cvars: {format_metadata_cvars(role_metadata.get('launchCvars', []))}",
                f"- Script cvars: {format_metadata_cvars(role_metadata.get('scriptCvars', []))}",
                f"- Exec commands: {format_metadata_commands(role_metadata.get('execCommands', []))}",
                "",
                "```text",
                role_metadata.get("launchCommand", ""),
                "```",
                "",
            ]

    lines += [
        "",
        "## Required Scene Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in REQUIRED_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Shadow Correctness Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in SHADOW_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Lens Flare Capture Coverage",
        "",
        "| Case | Mode | Map | Purpose |",
        "|---|---|---|---|",
    ]
    for case_id, scene in LENS_FLARE_SCENES.items():
        lines.append(f"| `{case_id}` | {scene['mode']} | `{scene['map']}` | {scene['purpose']} |")

    lines += [
        "",
        "## Lens Flare Presets",
        "",
        "| Preset | Cvars |",
        "|---|---|",
    ]
    for preset, cvars in LENS_FLARE_PRESETS.items():
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars.items()) or "stock/default cvars"
        lines.append(f"| `{preset}` | {cvar_text} |")

    lines += [
        "",
        "## Upload Pressure Variants",
        "",
        "| Variant | Launch Cvars | Purpose |",
        "|---|---|---|",
    ]
    for variant, item in UPLOAD_PRESSURE_VARIANTS.items():
        cvars = item.get("cvars", ())
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars) or "current defaults"
        lines.append(f"| `{variant}` | {cvar_text} | {item['purpose']} |")

    lines += [
        "",
        "## Graph Invalidation Variants",
        "",
        "| Variant | Launch Cvars | Purpose |",
        "|---|---|---|",
    ]
    for variant, item in GRAPH_INVALIDATION_VARIANTS.items():
        cvars = item.get("cvars", ())
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars) or "current defaults"
        lines.append(f"| `{variant}` | {cvar_text} | {item['purpose']} |")

    lines += [
        "",
        "## Performance Comparison Variants",
        "",
        "| Variant | Launch Cvars | Purpose |",
        "|---|---|---|",
    ]
    for variant, item in PERFORMANCE_COMPARISON_VARIANTS.items():
        cvars = item.get("cvars", ())
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars) or "current defaults"
        lines.append(f"| `{variant}` | {cvar_text} | {item['purpose']} |")

    lines += [
        "",
        "## Lens Flare Sign-Off Matrix",
        "",
        "| Platform | Required Tiers | Visual Evidence | Performance Evidence |",
        "|---|---|---|---|",
    ]
    for item in LENS_FLARE_SIGNOFF_MATRIX:
        lines.append(
            f"| {item['platform']} | {item['tiers']} | {item['visualEvidence']} | {item['performanceEvidence']} |"
        )

    lines += [
        "",
        "## Shadow Presets",
        "",
        "| Preset | Cvars |",
        "|---|---|",
    ]
    for preset, cvars in SHADOW_PRESETS.items():
        cvar_text = ", ".join(f"`{key} {value}`" for key, value in cvars.items()) or "stock defaults"
        lines.append(f"| `{preset}` | {cvar_text} |")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=tuple(PROFILE_DEFAULTS.keys()), default="smoke", help="Preset case/dimension profile.")
    parser.add_argument("--cases", default="", help="Comma-separated case ids. Overrides profile cases.")
    parser.add_argument("--tiers", default="", help="Comma-separated r_glTier values. Overrides profile tiers.")
    parser.add_argument("--maxfps", default="", help="Comma-separated com_maxfps values. Overrides profile values.")
    parser.add_argument("--swap-intervals", default="", help="Comma-separated r_swapInterval values. Overrides profile values.")
    parser.add_argument("--display-modes", default="", help="Comma-separated display modes: windowed,fullscreen.")
    parser.add_argument("--shadow-presets", default="", help="Comma-separated shadow presets. Use --list to inspect values.")
    parser.add_argument("--lens-flare-presets", default="", help="Comma-separated lens-flare presets: default,off,corona,high.")
    parser.add_argument("--launch-variants", default="", help="Comma-separated profile launch variants. Use --list to inspect values for profiles such as upload-pressure.")
    parser.add_argument("--renderer", default="best", help="Value for r_renderer, usually best or arb2.")
    parser.add_argument("--benchmark-preset", default="baseline", help="Value for r_rendererBenchmarkPreset.")
    parser.add_argument("--modern-executor", action="store_true", help="Opt into r_rendererModernExecutor for gameplay benchmarking. Defaults off so ARB2/high-FPS baselines are not polluted by side-path work.")
    parser.add_argument("--gpu-timers", action="store_true", help="Enable GL timer queries during the sampled benchmark window. Defaults off for acceptance FPS runs because timer queries can perturb frame pacing.")
    parser.add_argument("--show-fps-overlay", action="store_true", help="Draw the in-game FPS overlay during the run. Defaults off so acceptance timings measure renderer/gameplay cost, not diagnostic text drawing.")
    parser.add_argument("--pacing-only", action="store_true", help="Measure frame pacing without enabling r_rendererMetrics or rendererBenchmarkCapture. Use this for high-FPS acceptance runs after diagnostic captures are clean.")
    parser.add_argument("--renderer-metrics-level", type=int, default=-1, help="Renderer metrics detail level during the sampled window. Defaults to the selected profile's level, usually 1; use 2 for detailed low-overhead/state lines.")
    parser.add_argument("--min-pacing-hz", type=float, default=0.0, help="Fail when the parsed frame-pacing snapshot falls below this average presentation rate.")
    parser.add_argument("--max-p95-ms", type=float, default=0.0, help="Fail when the parsed frame-pacing P95 exceeds this millisecond budget. Use 0 to disable.")
    parser.add_argument("--max-p99-ms", type=float, default=0.0, help="Fail when the parsed frame-pacing P99 exceeds this millisecond budget. Use 0 to disable.")
    parser.add_argument("--set-cvar", action="append", default=[], metavar="NAME=VALUE", help="Extra post-map cvar written into the generated benchmark cfg. Repeat for A/B diagnostics without extending the launch command line.")
    parser.add_argument("--set-launch-cvar", action="append", default=[], metavar="NAME=VALUE", help="Extra cvar applied on the openQ4 launch command line before the map loads. Use for load-time renderer knobs such as vertex/index buffer caching.")
    parser.add_argument("--exec-command", action="append", default=[], metavar="COMMAND", help="Extra post-map console command written into the generated benchmark cfg. Repeat for targeted diagnostics such as flashlight impulses.")
    parser.add_argument("--autoexec-delay-ms", type=int, default=1000, help="Delay after active map draw before executing the generated benchmark cfg.")
    parser.add_argument("--settle-frames", type=int, default=360, help="Frames to wait after map/connect before sampling.")
    parser.add_argument("--sample-frames", type=int, default=600, help="Frames to sample before dumping metrics and screenshots.")
    parser.add_argument("--sample-msec", type=int, default=0, help="Real milliseconds to sample before dumping metrics and screenshots. Overrides --sample-frames when positive.")
    parser.add_argument("--timeout", type=int, default=180, help="Per-case process timeout in seconds.")
    parser.add_argument("--basepath", default=default_basepath(), help="Quake 4 install/base path. Omit or set empty to skip fs_basepath.")
    parser.add_argument("--output-dir", default="", help="Report/output directory. Defaults to <repo>/.tmp/renderer-gameplay/<timestamp>.")
    parser.add_argument("--reference-dir", default="", help="Optional TGA reference screenshot root for deterministic image comparison.")
    parser.add_argument("--require-references", action="store_true", help="Fail captures when --reference-dir has no matching reference image.")
    parser.add_argument("--image-rms-threshold", type=float, default=2.0, help="Allowed RMS channel delta for TGA comparisons.")
    parser.add_argument("--image-max-threshold", type=int, default=24, help="Allowed maximum channel delta for TGA comparisons.")
    parser.add_argument("--mp-port", type=int, default=28110, help="Base listen-server port for MP runs.")
    parser.add_argument("--mp-client-delay", type=int, default=12, help="Seconds to wait before launching the MP loopback client.")
    parser.add_argument("--mp-client-delay-frames", type=int, default=480, help="Extra server frames before server-side capture in MP runs.")
    parser.add_argument("--mp-server-quit-delay-frames", type=int, default=1800, help="Extra server frames to wait after MP server capture before quitting, so the loopback client can finish its capture.")
    parser.add_argument("--limit", type=int, default=0, help="Limit generated specs, useful for bounded local smoke runs.")
    parser.add_argument("--dry-run", action="store_true", help="Write the planned command lines without launching openQ4.")
    parser.add_argument("--list", action="store_true", help="List profiles, cases, and shadow presets without running.")
    parsed = parser.parse_args(argv)
    try:
        profile_defaults = PROFILE_DEFAULTS[parsed.profile]
        profile_cvars = tuple(profile_defaults.get("cvars", ()))
        parsed.extra_cvars = profile_cvars + parse_extra_cvars(parsed.set_cvar)
        parsed.launch_cvars = parse_extra_cvars(parsed.set_launch_cvar)
        parsed.exec_commands = parse_exec_commands(parsed.exec_command)
        if parsed.renderer_metrics_level < 0:
            parsed.renderer_metrics_level = int(profile_defaults.get("rendererMetricsLevel", 1))
        parsed.renderer_metrics_level = max(0, parsed.renderer_metrics_level)
        parsed.modern_executor = parsed.modern_executor or bool(profile_defaults.get("modernExecutor", False))
    except ValueError as exc:
        parser.error(str(exc))
    parsed.reference_dir_path = Path(parsed.reference_dir).resolve() if parsed.reference_dir else None
    return parsed


def print_list() -> None:
    print("Profiles:")
    for profile, defaults in PROFILE_DEFAULTS.items():
        launch_variants = profile_launch_variants(defaults)
        count = (
            len(defaults["cases"])
            * len(defaults["tiers"])
            * len(defaults["maxfps"])
            * len(defaults["swap"])
            * len(defaults["display"])
            * len(defaults["shadows"])
            * len(defaults["lensFlare"])
            * len(launch_variants)
        )
        profile_cvars = defaults.get("cvars", ())
        cvar_text = " " + ", ".join(f"{key}={value}" for key, value in profile_cvars) if profile_cvars else ""
        variant_text = "" if launch_variants == ("default",) else " variants=" + ",".join(launch_variants)
        metrics_text = ""
        if int(defaults.get("rendererMetricsLevel", 1)) != 1:
            metrics_text = f" metrics={defaults['rendererMetricsLevel']}"
        modern_text = " modernExecutor=1" if defaults.get("modernExecutor", False) else ""
        print(f"  {profile}: {count} generated case(s){cvar_text}{variant_text}{metrics_text}{modern_text}")
    print("\nRequired gameplay cases:")
    for case_id, scene in REQUIRED_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nShadow correctness cases:")
    for case_id, scene in SHADOW_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nLens flare capture cases:")
    for case_id, scene in LENS_FLARE_SCENES.items():
        print(f"  {case_id}: {scene['mode']} {scene['map']} - {scene['purpose']}")
    print("\nShadow presets:")
    for preset, cvars in SHADOW_PRESETS.items():
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars.items()) or "stock defaults"
        print(f"  {preset}: {cvar_text}")
    print("\nLens flare presets:")
    for preset, cvars in LENS_FLARE_PRESETS.items():
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars.items()) or "stock/default cvars"
        print(f"  {preset}: {cvar_text}")
    print("\nLens flare sign-off matrix:")
    for item in LENS_FLARE_SIGNOFF_MATRIX:
        print(f"  {item['platform']}: {item['tiers']} - {item['visualEvidence']}; {item['performanceEvidence']}")
    print("\nUpload pressure variants:")
    for variant, item in UPLOAD_PRESSURE_VARIANTS.items():
        cvars = item.get("cvars", ())
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars) or "current defaults"
        print(f"  {variant}: {cvar_text} - {item['purpose']}")
    print("\nGraph invalidation variants:")
    for variant, item in GRAPH_INVALIDATION_VARIANTS.items():
        cvars = item.get("cvars", ())
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars) or "current defaults"
        print(f"  {variant}: {cvar_text} - {item['purpose']}")
    print("\nPerformance comparison variants:")
    for variant, item in PERFORMANCE_COMPARISON_VARIANTS.items():
        cvars = item.get("cvars", ())
        cvar_text = ", ".join(f"{key}={value}" for key, value in cvars) or "current defaults"
        print(f"  {variant}: {cvar_text} - {item['purpose']}")


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list:
        print_list()
        return 0

    root = repo_root()
    executable = find_client_executable(root)
    basepath = args.basepath
    if basepath and not Path(basepath).exists():
        print(f"warning: basepath does not exist, omitting fs_basepath: {basepath}", file=sys.stderr)
        basepath = ""
    if args.reference_dir_path is not None and not args.reference_dir_path.exists():
        raise FileNotFoundError(f"reference directory does not exist: {args.reference_dir_path}")

    specs = build_specs(args)
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    output_dir = Path(args.output_dir).resolve() if args.output_dir else root / ".tmp" / "renderer-gameplay" / timestamp
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = output_dir.name

    results: list[dict[str, Any]] = []
    for index, spec in enumerate(specs):
        print(f"running {spec.id} ({spec.mode} {spec.map_name})...", flush=True)
        try:
            if spec.mode == "MP":
                result = run_mp_spec(root, executable, output_dir, basepath, run_id, spec, index, args)
            else:
                result = run_sp_spec(root, executable, output_dir, basepath, run_id, spec, args)
        except Exception as exc:
            result = harness_failure_result(spec, exc)
            print(f"  fail ({type(exc).__name__}: {exc})", file=sys.stderr, flush=True)
        else:
            print(f"  {result['status']}", flush=True)
        results.append(result)

    selected_launch_variants = list(dict.fromkeys(spec.upload_variant for spec in specs))
    metadata = {
        "generated": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "host": f"{platform.system()} {platform.release()} {platform.machine()}",
        "executable": str(executable),
        "basepath": basepath,
        "devpath": str(root / ".install"),
        "gameDir": "baseoq4",
        "profile": args.profile,
        "dryRun": args.dry_run,
        "autoexecDelayMs": args.autoexec_delay_ms,
        "settleFrames": args.settle_frames,
        "sampleFrames": args.sample_frames,
        "sampleMsec": args.sample_msec,
        "rendererMetricsLevel": args.renderer_metrics_level,
        "modernExecutor": args.modern_executor,
        "minPacingHz": args.min_pacing_hz,
        "maxP95Ms": args.max_p95_ms,
        "maxP99Ms": args.max_p99_ms,
        "referenceDir": str(args.reference_dir_path) if args.reference_dir_path is not None else "",
        "requireReferences": args.require_references,
        "imageRmsThreshold": args.image_rms_threshold,
        "imageMaxThreshold": args.image_max_threshold,
        "profileCvars": dict(PROFILE_DEFAULTS[args.profile].get("cvars", ())),
        "profileLaunchVariants": list(profile_launch_variants(PROFILE_DEFAULTS[args.profile])),
        "launchVariants": selected_launch_variants,
        "launchVariantFilter": args.launch_variants,
        "launchCvars": dict(args.launch_cvars),
        "scriptCvars": cvar_pair_payload(args.extra_cvars),
        "execCommands": list(args.exec_commands),
    }
    report_json, report_md = write_reports(output_dir, results, metadata)
    print(f"wrote {report_md}")
    print(f"wrote {report_json}")

    if args.dry_run:
        return 0
    return 0 if all(result["status"] == "pass" for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
