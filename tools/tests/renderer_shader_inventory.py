#!/usr/bin/env python3
"""Audit the internal modern GL shader-library source contract.

This is a static companion to the runtime shader self-tests. It keeps the
long-term renderer roadmap honest about shader source ownership: generated
reports live under .tmp, while runtime shader source remains compiled into the
engine unless the project deliberately changes that packaging model.
"""

from __future__ import annotations

import argparse
import json
import platform
import re
import sys
import time
from pathlib import Path
from typing import Any


EXPECTED_GLSL_VERSIONS = (330, 410, 430, 450)
EXPECTED_SHADER_TIER_CASES = {
    "shader-lensflare-gl33": "gl33",
    "shader-lensflare-gl41": "gl41",
    "shader-lensflare-gl43": "gl43",
    "shader-lensflare-gl45": "gl45",
    "shader-lensflare-gl46": "gl46",
}
EXPECTED_BUILDERS = (
    "R_ModernGLShaderLibrary_BuildVersionList",
    "R_ModernGLShaderLibrary_BuildVertexSource",
    "R_ModernGLShaderLibrary_BuildFragmentSource",
)
EXPECTED_DRAW_RECORD_GUARDS = (
    "uniform uint uDrawRecordCount;",
    "drawRecordIndex < uDrawRecordCount",
    "uDrawRecords.records[int(drawRecordIndex)]",
)
EXPECTED_LENS_FLARE_SELFTEST_TOKENS = (
    "const int lensFlareVersions[4] = { 330, 410, 430, 450 };",
    "stats.lensFlareProgramCount",
    "stats.lensFlareAccumulationProgramCount",
    "stats.lensFlareCompositeProgramCount",
    "stats.lensFlareReflectedSamplerCount",
    "stats.validatedGLSLVersionCount",
)
LOOSE_SHADER_FILE_TOKENS = (
    "fileSystem->",
    "ReadFile(",
    "OpenFileRead(",
    "FindFile(",
    '".glsl"',
    '".vert"',
    '".frag"',
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def parse_enum_kinds(header_text: str) -> list[str]:
    match = re.search(
        r"enum\s+modernGLShaderProgramKind_t\s*\{(?P<body>.*?)\};",
        header_text,
        re.DOTALL,
    )
    if not match:
        return []

    kinds: list[str] = []
    for raw_line in match.group("body").splitlines():
        line = raw_line.split("//", 1)[0].strip().rstrip(",")
        if not line:
            continue
        name = line.split("=", 1)[0].strip()
        if name == "MODERN_GL_SHADER_PROGRAM_KIND_COUNT":
            continue
        if name.startswith("MODERN_GL_SHADER_"):
            kinds.append(name)
    return kinds


def parse_descriptors(cpp_text: str) -> list[dict[str, Any]]:
    match = re.search(
        r"rg_modernGLShaderProgramDescriptors\s*\[\s*MODERN_GL_SHADER_PROGRAM_KIND_COUNT\s*\]\s*=\s*\{(?P<body>.*?)\n\};",
        cpp_text,
        re.DOTALL,
    )
    if not match:
        return []

    descriptors: list[dict[str, Any]] = []
    for line_number, raw_line in enumerate(match.group("body").splitlines(), start=1):
        line = raw_line.strip()
        if not line.startswith("{"):
            continue
        desc_match = re.search(
            r"\{\s*(MODERN_GL_SHADER_[A-Z0-9_]+)\s*,.*\"([A-Za-z0-9_]+)\"\s*\}",
            line,
        )
        if desc_match:
            descriptors.append(
                {
                    "kind": desc_match.group(1),
                    "name": desc_match.group(2),
                    "lineInTable": line_number,
                }
            )
    return descriptors


def parse_kind_names(cpp_text: str) -> dict[str, str]:
    match = re.search(
        r"const\s+char\s+\*ModernGLShaderProgramKind_Name\s*\(.*?switch\s*\(.*?\)\s*\{(?P<body>.*?)\n\tdefault:",
        cpp_text,
        re.DOTALL,
    )
    if not match:
        return {}

    names: dict[str, str] = {}
    for case_match in re.finditer(
        r"case\s+(MODERN_GL_SHADER_[A-Z0-9_]+):\s*return\s+\"([^\"]+)\";",
        match.group("body"),
        re.DOTALL,
    ):
        names[case_match.group(1)] = case_match.group(2)
    return names


def parse_max_programs(header_text: str) -> int | None:
    match = re.search(r"MODERN_GL_SHADER_MAX_PROGRAMS\s*=\s*(\d+)", header_text)
    if not match:
        return None
    return int(match.group(1))


def check_entry(check_id: str, title: str, passed: bool, detail: str) -> dict[str, Any]:
    return {
        "id": check_id,
        "title": title,
        "status": "pass" if passed else "fail",
        "detail": detail,
    }


def build_inventory(root: Path) -> dict[str, Any]:
    header_path = root / "src" / "renderer" / "ModernGLShaderLibrary.h"
    cpp_path = root / "src" / "renderer" / "ModernGLShaderLibrary.cpp"
    render_system_path = root / "src" / "renderer" / "RenderSystem_init.cpp"
    validation_matrix_path = root / "tools" / "tests" / "renderer_validation_matrix.py"

    header_text = read_text(header_path)
    cpp_text = read_text(cpp_path)
    render_system_text = read_text(render_system_path)
    validation_matrix_text = read_text(validation_matrix_path)

    enum_kinds = parse_enum_kinds(header_text)
    descriptors = parse_descriptors(cpp_text)
    descriptor_by_kind = {item["kind"]: item for item in descriptors}
    kind_names = parse_kind_names(cpp_text)
    max_programs = parse_max_programs(header_text)
    expected_program_slots = len(enum_kinds) * len(EXPECTED_GLSL_VERSIONS)

    checks: list[dict[str, Any]] = []

    checks.append(
        check_entry(
            "enum-descriptor-count",
            "Enum and descriptor counts match",
            len(enum_kinds) > 0 and len(enum_kinds) == len(descriptors),
            f"{len(enum_kinds)} enum kinds, {len(descriptors)} descriptors",
        )
    )

    missing_descriptors = [kind for kind in enum_kinds if kind not in descriptor_by_kind]
    extra_descriptors = [item["kind"] for item in descriptors if item["kind"] not in enum_kinds]
    descriptor_order = [item["kind"] for item in descriptors]
    checks.append(
        check_entry(
            "enum-descriptor-coverage",
            "Every enum kind has one descriptor in enum order",
            not missing_descriptors and not extra_descriptors and descriptor_order == enum_kinds,
            "missing="
            + ",".join(missing_descriptors or ["none"])
            + f"; extra={','.join(extra_descriptors or ['none'])}; order={'pass' if descriptor_order == enum_kinds else 'fail'}",
        )
    )

    name_mismatches = []
    for kind in enum_kinds:
        descriptor_name = descriptor_by_kind.get(kind, {}).get("name")
        function_name = kind_names.get(kind)
        if descriptor_name != function_name:
            name_mismatches.append(f"{kind}:{descriptor_name}/{function_name}")
    checks.append(
        check_entry(
            "kind-name-contract",
            "Descriptor names match ModernGLShaderProgramKind_Name",
            not name_mismatches and len(kind_names) == len(enum_kinds),
            "mismatches=" + ",".join(name_mismatches or ["none"]),
        )
    )

    checks.append(
        check_entry(
            "shader-program-capacity",
            "Max program slots cover all GLSL variants",
            max_programs is not None and expected_program_slots <= max_programs,
            f"{len(enum_kinds)} kinds * {len(EXPECTED_GLSL_VERSIONS)} GLSL versions = {expected_program_slots}; max={max_programs}",
        )
    )

    missing_versions = [
        version
        for version in EXPECTED_GLSL_VERSIONS
        if f"versions[count++] = {version};" not in cpp_text
    ]
    missing_stat_tails = [
        version for version in EXPECTED_GLSL_VERSIONS if f"glsl{version}ProgramCount" not in header_text + cpp_text
    ]
    checks.append(
        check_entry(
            "glsl-tier-source-coverage",
            "GLSL 330/410/430/450 tiers are generated and reported",
            not missing_versions and not missing_stat_tails,
            "missingBuildVersions="
            + ",".join(str(item) for item in missing_versions or ["none"])
            + "; missingStats="
            + ",".join(str(item) for item in missing_stat_tails or ["none"]),
        )
    )

    missing_builders = [name for name in EXPECTED_BUILDERS if name not in cpp_text]
    checks.append(
        check_entry(
            "source-builders",
            "Shader sources are generated by internal C++ builders",
            not missing_builders,
            "missing=" + ",".join(missing_builders or ["none"]),
        )
    )

    loose_file_hits = [token for token in LOOSE_SHADER_FILE_TOKENS if token in cpp_text + header_text]
    checks.append(
        check_entry(
            "runtime-source-packaging",
            "Runtime shader source does not depend on loose shader files",
            not loose_file_hits,
            "looseFileTokens=" + ",".join(loose_file_hits or ["none"]),
        )
    )

    missing_draw_record_guards = [token for token in EXPECTED_DRAW_RECORD_GUARDS if token not in cpp_text]
    checks.append(
        check_entry(
            "draw-record-bounds-guard",
            "SSBO draw-record fetch path is bounds guarded",
            not missing_draw_record_guards,
            "missing=" + ",".join(missing_draw_record_guards or ["none"]),
        )
    )

    missing_lens_flare_tokens = [token for token in EXPECTED_LENS_FLARE_SELFTEST_TOKENS if token not in cpp_text]
    checks.append(
        check_entry(
            "runtime-tier-selftest",
            "Runtime self-test cross-checks lens-flare shader variants per GLSL tier",
            not missing_lens_flare_tokens,
            "missing=" + ",".join(missing_lens_flare_tokens or ["none"]),
        )
    )

    missing_validation_cases = [
        case_id
        for case_id, tier in EXPECTED_SHADER_TIER_CASES.items()
        if case_id not in validation_matrix_text or f'"tier": "{tier}"' not in validation_matrix_text
    ]
    checks.append(
        check_entry(
            "validation-matrix-tier-cases",
            "Validation matrix keeps forced GL shader-tier probes",
            not missing_validation_cases,
            "missing=" + ",".join(missing_validation_cases or ["none"]),
        )
    )

    reload_checks = {
        "cvar": 'r_rendererShaderReload( "r_rendererShaderReload", "0"' in render_system_text,
        "reloadGate": "!r_rendererShaderReload.GetBool()" in cpp_text,
        "command": 'rendererShaderLibraryReload' in render_system_text,
        "planInvalidate": "R_ModernGLExecutor_InvalidatePlans()" in render_system_text,
    }
    checks.append(
        check_entry(
            "reload-debug-opt-in",
            "Shader reload/debug path remains opt-in and invalidates cached plans",
            all(reload_checks.values()),
            ", ".join(f"{key}={'pass' if value else 'fail'}" for key, value in reload_checks.items()),
        )
    )

    all_passed = all(check["status"] == "pass" for check in checks)
    return {
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "status": "pass" if all_passed else "fail",
        "policy": {
            "runtimeShaderSource": "internal C++ string builders",
            "generatedArtifacts": "validation-only reports under .tmp; no loose runtime shader files",
            "shaderReload": "opt-in with r_rendererShaderReload 1 and rendererShaderLibraryReload",
        },
        "paths": {
            "header": str(header_path.relative_to(root)).replace("\\", "/"),
            "implementation": str(cpp_path.relative_to(root)).replace("\\", "/"),
            "renderSystem": str(render_system_path.relative_to(root)).replace("\\", "/"),
            "validationMatrix": str(validation_matrix_path.relative_to(root)).replace("\\", "/"),
        },
        "summary": {
            "programKinds": len(enum_kinds),
            "descriptorCount": len(descriptors),
            "glslVersions": list(EXPECTED_GLSL_VERSIONS),
            "maxPrograms": max_programs,
            "expectedProgramSlots": expected_program_slots,
        },
        "shaderPrograms": [
            {
                "index": index,
                "kind": kind,
                "descriptorName": descriptor_by_kind.get(kind, {}).get("name", ""),
                "nameFunction": kind_names.get(kind, ""),
            }
            for index, kind in enumerate(enum_kinds)
        ],
        "checks": checks,
    }


def write_markdown(report: dict[str, Any], output_path: Path) -> None:
    lines = [
        "# Modern GL Shader Source Inventory",
        "",
        f"- Status: `{report['status']}`",
        f"- Generated: `{report['generatedAt']}`",
        f"- Runtime shader source policy: `{report['policy']['runtimeShaderSource']}`",
        f"- Generated artifact policy: `{report['policy']['generatedArtifacts']}`",
        f"- Shader reload policy: `{report['policy']['shaderReload']}`",
        "",
        "## Summary",
        "",
        "| Field | Value |",
        "| --- | --- |",
        f"| Program kinds | {report['summary']['programKinds']} |",
        f"| Descriptors | {report['summary']['descriptorCount']} |",
        f"| GLSL versions | {', '.join(str(item) for item in report['summary']['glslVersions'])} |",
        f"| Expected program slots | {report['summary']['expectedProgramSlots']} |",
        f"| Max program slots | {report['summary']['maxPrograms']} |",
        "",
        "## Checks",
        "",
        "| Check | Status | Detail |",
        "| --- | --- | --- |",
    ]
    for check in report["checks"]:
        lines.append(f"| {check['title']} | `{check['status']}` | {check['detail']} |")

    lines.extend(
        [
            "",
            "## Shader Program Inventory",
            "",
            "| Index | Kind | Descriptor | Name Function |",
            "| ---: | --- | --- | --- |",
        ]
    )
    for program in report["shaderPrograms"]:
        lines.append(
            f"| {program['index']} | `{program['kind']}` | `{program['descriptorName']}` | `{program['nameFunction']}` |"
        )

    lines.extend(
        [
            "",
            "## Developer Notes",
            "",
            "- Keep runtime shader sources internal unless a future project decision changes packaging.",
            "- Validation-generated inventories belong under `.tmp/` and are not staged into `.install/`.",
            "- The runtime `rendererShaderLibrarySelfTest` remains the GL compile/reflection authority; this report guards source inventory drift before runtime.",
            "",
        ]
    )
    output_path.write_text("\n".join(lines), encoding="utf-8")


def write_json(report: dict[str, Any], output_path: Path) -> None:
    output_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        default=".tmp/renderer-validation/shader-source-inventory",
        help="Directory for shader_source_inventory.md/json.",
    )
    parser.add_argument("--require-clean", action="store_true", help="Return non-zero if any audit check fails.")
    parser.add_argument("--print-summary", action="store_true", help="Print a compact text summary.")
    args = parser.parse_args(argv)

    root = repo_root()
    report = build_inventory(root)

    output_dir = (root / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    markdown_path = output_dir / "shader_source_inventory.md"
    json_path = output_dir / "shader_source_inventory.json"
    write_markdown(report, markdown_path)
    write_json(report, json_path)

    if args.print_summary:
        print(
            f"shader source inventory: {report['status']} "
            f"({report['summary']['programKinds']} kinds, "
            f"{report['summary']['expectedProgramSlots']}/{report['summary']['maxPrograms']} slots)"
        )
        print(f"markdown: {markdown_path}")
        print(f"json: {json_path}")

    if args.require_clean and report["status"] != "pass":
        failed = [check for check in report["checks"] if check["status"] != "pass"]
        for check in failed:
            print(f"{check['id']}: {check['detail']}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
