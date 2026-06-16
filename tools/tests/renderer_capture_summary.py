#!/usr/bin/env python3
"""Generate a RenderDoc/API capture evidence summary for renderer promotion.

The tool standardizes the capture matrix and naming convention. It can be run
before captures exist to create a blocked summary and manifest template, then
rerun with a filled manifest once .rdc/API-trace files and review notes exist.
"""

from __future__ import annotations

import argparse
import json
import platform
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


STATUS_PASS = "pass"
STATUS_NEEDS_REVIEW = "needs-review"
STATUS_MISSING = "missing"
STATUS_BLOCKED = "blocked"
STATUS_FAIL = "fail"

VALID_STATUSES = {STATUS_PASS, STATUS_NEEDS_REVIEW, STATUS_MISSING, STATUS_BLOCKED, STATUS_FAIL}

REQUIRED_METADATA_FIELDS = (
    "os",
    "gpu",
    "driverVersion",
    "glVendor",
    "glRenderer",
    "glVersion",
    "selectedTier",
    "requestedTier",
    "contextProfile",
)

RESOURCE_CHECK_DESCRIPTIONS = {
    "sceneColor": "scene-color resource is named and contains the expected final scene image",
    "depth": "depth resource is named and contains valid scene depth",
    "postProcessScratch": "post-process scratch resources are named and not self-read/write feedback loops",
    "lensFlareAccumulation": "lens-flare accumulation/composite resources are named and contain expected data",
    "lightGrid": "clustered-light or light-grid resources are named and populated",
    "gBuffer": "G-buffer resources are named and populated when modern-visible/deferred paths are enabled",
    "shadowMap": "shadow-map or stencil-shadow resources are named and contain expected contents",
    "modernExecutorBuffers": "modern executor buffers/programs/draw records are named and populated",
    "bindCountReduction": "API trace supports any low-overhead bind-count reduction claim",
    "textureBindTargets": "API trace confirms non-2D texture targets bind through the target-aware fallback",
    "graphInvalidationFinalUse": "API trace confirms framebuffer invalidation only after the final owning-pass use",
    "rollbackVisible": "capture or trace confirms explicit ARB2/legacy rollback remains visible",
    "mpHudWeaponView": "MP client capture includes HUD and weapon-view composition",
}


@dataclass(frozen=True)
class CaptureCase:
    id: str
    title: str
    mode: str
    scene: str
    path: str
    renderer_path: str
    requested_tier: str
    capture_tool: str
    cvars: dict[str, str]
    required_checks: tuple[str, ...]
    notes: str


CAPTURE_CASES = (
    CaptureCase(
        id="default-arb2-storage1",
        title="Default ARB2-compatible path",
        mode="SP",
        scene="game/storage1",
        path="spawn-static",
        renderer_path="default ARB2-compatible path",
        requested_tier="auto",
        capture_tool="api-trace",
        cvars={
            "r_renderer": "best",
            "r_glTier": "auto",
            "r_rendererModernAutoPromote": "0",
            "r_rendererMetrics": "2",
        },
        required_checks=("sceneColor", "depth", "postProcessScratch", "shadowMap"),
        notes="RenderDoc is not the primary tool for the compatibility path; use an API trace or reviewed driver trace.",
    ),
    CaptureCase(
        id="explicit-arb2-storage1",
        title="Explicit ARB2 rollback path",
        mode="SP",
        scene="game/storage1",
        path="spawn-static",
        renderer_path="explicit r_renderer arb2 rollback",
        requested_tier="auto",
        capture_tool="api-trace",
        cvars={
            "r_renderer": "arb2",
            "r_glTier": "auto",
            "r_rendererModernAutoPromote": "0",
            "r_rendererMetrics": "2",
        },
        required_checks=("sceneColor", "depth", "postProcessScratch", "rollbackVisible"),
        notes="This is the compatibility escape hatch and must remain valid after modern-path experiments.",
    ),
    CaptureCase(
        id="modern-executor-prepare-storage1",
        title="Modern executor prepare-only path",
        mode="SP",
        scene="game/storage1",
        path="spawn-static",
        renderer_path="modern executor prepare-only",
        requested_tier="gl33",
        capture_tool="renderdoc",
        cvars={
            "r_renderer": "best",
            "r_glTier": "gl33",
            "r_rendererModernExecutor": "1",
            "r_rendererModernSubmit": "0",
            "r_rendererModernVisible": "0",
            "r_rendererMetrics": "2",
        },
        required_checks=("sceneColor", "depth", "modernExecutorBuffers"),
        notes="RenderDoc capture should target the forced GL 3.3/core bring-up path.",
    ),
    CaptureCase(
        id="modern-visible-storage2",
        title="Modern-visible opt-in path",
        mode="SP",
        scene="game/storage2",
        path="spawn-static",
        renderer_path="modern-visible hybrid",
        requested_tier="gl45",
        capture_tool="renderdoc",
        cvars={
            "r_renderer": "best",
            "r_glTier": "gl45",
            "r_rendererModernExecutor": "1",
            "r_rendererModernVisible": "1",
            "r_rendererModernVisibleDepth": "1",
            "r_rendererModernOpaque": "1",
            "r_rendererModernDeferred": "1",
            "r_rendererForwardPlus": "1",
            "r_rendererMetrics": "2",
        },
        required_checks=("sceneColor", "depth", "postProcessScratch", "lightGrid", "gBuffer", "shadowMap"),
        notes="This is capture evidence for the opt-in hybrid visible frame, not default promotion.",
    ),
    CaptureCase(
        id="graph-invalidation-medlabs",
        title="Graph-invalidation armed path",
        mode="SP",
        scene="game/medlabs",
        path="spawn-static",
        renderer_path="modern graph-invalidation armed",
        requested_tier="gl45",
        capture_tool="renderdoc+api-trace",
        cvars={
            "r_renderer": "best",
            "r_glTier": "gl45",
            "r_rendererModernExecutor": "1",
            "r_rendererModernVisibleDepth": "1",
            "r_rendererModernOpaque": "1",
            "r_rendererModernDeferred": "1",
            "r_rendererForwardPlus": "1",
            "r_rendererGraphInvalidate": "1",
            "r_rendererGpuTimers": "1",
            "r_rendererMetrics": "2",
        },
        required_checks=(
            "sceneColor",
            "depth",
            "postProcessScratch",
            "lensFlareAccumulation",
            "graphInvalidationFinalUse",
        ),
        notes="API trace must confirm invalidation after final use before any default policy changes.",
    ),
    CaptureCase(
        id="low-overhead-gl45-airdefense1",
        title="Low-overhead GL 4.5 path",
        mode="SP",
        scene="game/airdefense1",
        path="spawn-static",
        renderer_path="low-overhead GL 4.5 modern submit",
        requested_tier="gl45",
        capture_tool="renderdoc+api-trace",
        cvars={
            "r_renderer": "best",
            "r_glTier": "gl45",
            "r_rendererModernExecutor": "1",
            "r_rendererModernSubmit": "1",
            "r_rendererGpuTimers": "1",
            "r_rendererMetrics": "2",
        },
        required_checks=("sceneColor", "depth", "lightGrid", "bindCountReduction", "textureBindTargets"),
        notes="API trace must back bind-count and texture target claims.",
    ),
    CaptureCase(
        id="gl33-fallback-mcc-landing",
        title="GL 3.3 fallback path",
        mode="SP",
        scene="game/mcc_landing",
        path="spawn-static",
        renderer_path="GL 3.3 fallback",
        requested_tier="gl33",
        capture_tool="renderdoc",
        cvars={
            "r_renderer": "best",
            "r_glTier": "gl33",
            "r_rendererModernExecutor": "1",
            "r_rendererMetrics": "2",
        },
        required_checks=("sceneColor", "depth", "postProcessScratch"),
        notes="Covers GUI/subview/cinematic handoff on the lowest modern tier.",
    ),
    CaptureCase(
        id="mp-q4dm1-listen-client",
        title="MP q4dm1 listen/client path",
        mode="MP",
        scene="mp/q4dm1",
        path="spawn-static listen/client",
        renderer_path="MP listen/client renderer parity",
        requested_tier="gl45",
        capture_tool="renderdoc",
        cvars={
            "r_renderer": "best",
            "r_glTier": "gl45",
            "r_rendererModernExecutor": "1",
            "r_rendererMetrics": "2",
            "si_gameType": "DM",
        },
        required_checks=("sceneColor", "depth", "mpHudWeaponView"),
        notes="Capture the local client role when tooling supports it; keep server/listen role notes with the artifact.",
    ),
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def now_string() -> str:
    if time.localtime().tm_isdst and time.daylight:
        zone = time.tzname[1]
    else:
        zone = time.tzname[0]
    return time.strftime(f"%Y-%m-%d %H:%M:%S {zone}")


def resolve_path(root: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    if not path.is_absolute():
        path = root / path
    return path.resolve()


def rel_path(root: Path, value: Path | str | None) -> str:
    if not value:
        return ""
    path = Path(value)
    try:
        return str(path.resolve().relative_to(root.resolve()))
    except ValueError:
        return str(path)


def markdown_cell(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", "<br>")


def case_to_dict(case: CaptureCase) -> dict[str, Any]:
    return {
        "id": case.id,
        "title": case.title,
        "mode": case.mode,
        "scene": case.scene,
        "path": case.path,
        "rendererPath": case.renderer_path,
        "requestedTier": case.requested_tier,
        "captureTool": case.capture_tool,
        "cvars": case.cvars,
        "requiredChecks": list(case.required_checks),
        "notes": case.notes,
    }


def expected_renderdoc_path(output_dir: Path, case: CaptureCase) -> Path:
    return output_dir / case.id / f"{case.id}.rdc"


def expected_api_trace_path(output_dir: Path, case: CaptureCase) -> Path:
    return output_dir / case.id / f"{case.id}.api-trace.json"


def expected_review_path(output_dir: Path, case: CaptureCase) -> Path:
    return output_dir / case.id / "review.json"


def load_manifest(root: Path, manifest_value: str | None) -> dict[str, dict[str, Any]]:
    manifest_path = resolve_path(root, manifest_value)
    if manifest_path is None:
        return {}
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    captures = payload.get("captures", payload)
    entries: dict[str, dict[str, Any]] = {}
    if isinstance(captures, dict):
        for case_id, value in captures.items():
            if isinstance(value, dict):
                entries[str(case_id)] = value
    elif isinstance(captures, list):
        for value in captures:
            if isinstance(value, dict) and value.get("id"):
                entries[str(value["id"])] = value
    else:
        raise ValueError("manifest must contain a capture object/list or use captures={...}")
    return entries


def path_from_entry(root: Path, output_dir: Path, case: CaptureCase, entry: dict[str, Any], key: str) -> tuple[str, bool]:
    expected = expected_renderdoc_path(output_dir, case) if key == "capturePath" else expected_api_trace_path(output_dir, case)
    value = str(entry.get(key, "")).strip()
    if value:
        path = resolve_path(root, value)
    else:
        path = expected
    assert path is not None
    return rel_path(root, path), path.exists()


def normalize_status(value: Any) -> str:
    status = str(value or "").strip().lower()
    if status in VALID_STATUSES:
        return status
    return ""


def missing_metadata(entry: dict[str, Any]) -> list[str]:
    metadata = entry.get("metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}
    return [field for field in REQUIRED_METADATA_FIELDS if not str(metadata.get(field, "")).strip()]


def missing_cvars(entry: dict[str, Any], case: CaptureCase) -> list[str]:
    observed = entry.get("cvars", {})
    if not isinstance(observed, dict):
        observed = {}
    return [key for key in case.cvars if str(observed.get(key, "")).strip() == ""]


def check_status(entry: dict[str, Any], check_id: str) -> str:
    checks = entry.get("checks", {})
    if not isinstance(checks, dict):
        return ""
    value = checks.get(check_id, "")
    if isinstance(value, dict):
        return normalize_status(value.get("status", ""))
    return normalize_status(value)


def assess_case(root: Path, output_dir: Path, case: CaptureCase, entry: dict[str, Any] | None) -> dict[str, Any]:
    if entry is None:
        entry = {}

    renderdoc_required = "renderdoc" in case.capture_tool
    api_trace_required = "api-trace" in case.capture_tool
    capture_path, capture_exists = path_from_entry(root, output_dir, case, entry, "capturePath")
    api_trace_path, api_trace_exists = path_from_entry(root, output_dir, case, entry, "apiTracePath")

    notes: list[str] = []
    missing_paths: list[str] = []
    if renderdoc_required and not capture_exists:
        missing_paths.append(capture_path)
    if api_trace_required and not api_trace_exists:
        missing_paths.append(api_trace_path)

    review_status = normalize_status(entry.get("status"))
    if review_status:
        notes.append(f"manifest status={review_status}")

    missing_meta = missing_metadata(entry)
    missing_cvar_fields = missing_cvars(entry, case)
    check_results = {check: check_status(entry, check) for check in case.required_checks}
    missing_checks = [check for check, status in check_results.items() if not status]
    failing_checks = [check for check, status in check_results.items() if status == STATUS_FAIL]
    nonpass_checks = [
        check for check, status in check_results.items()
        if status and status != STATUS_PASS
    ]

    if review_status == STATUS_FAIL or failing_checks:
        status = STATUS_FAIL
    elif review_status == STATUS_BLOCKED:
        status = STATUS_BLOCKED
    elif missing_paths:
        status = STATUS_MISSING
    elif missing_meta or missing_cvar_fields or missing_checks or nonpass_checks:
        status = STATUS_NEEDS_REVIEW
    elif review_status == STATUS_PASS:
        status = STATUS_PASS
    else:
        status = STATUS_NEEDS_REVIEW

    if missing_paths:
        notes.append("missing artifact path(s): " + ", ".join(missing_paths))
    if missing_meta:
        notes.append("missing metadata: " + ", ".join(missing_meta))
    if missing_cvar_fields:
        notes.append("missing cvar observations: " + ", ".join(missing_cvar_fields))
    if missing_checks:
        notes.append("missing check status: " + ", ".join(missing_checks))
    if nonpass_checks:
        notes.append("non-pass check status: " + ", ".join(nonpass_checks))
    if not notes:
        notes.append("all required artifacts, metadata, cvars, and checks are present")
    if entry.get("reviewedBy"):
        notes.append(f"reviewedBy={entry['reviewedBy']}")
    if entry.get("notes"):
        notes.append(str(entry["notes"]))

    return {
        **case_to_dict(case),
        "status": status,
        "capturePath": capture_path,
        "captureExists": capture_exists,
        "apiTracePath": api_trace_path,
        "apiTraceExists": api_trace_exists,
        "reviewPath": rel_path(root, expected_review_path(output_dir, case)),
        "missingMetadata": missing_meta,
        "missingCvars": missing_cvar_fields,
        "checks": check_results,
        "notes": notes,
    }


def overall_status(captures: list[dict[str, Any]]) -> str:
    if any(capture["status"] == STATUS_FAIL for capture in captures):
        return STATUS_FAIL
    if all(capture["status"] == STATUS_PASS for capture in captures):
        return STATUS_PASS
    return STATUS_BLOCKED


def status_counts(captures: list[dict[str, Any]]) -> dict[str, int]:
    counts = {STATUS_PASS: 0, STATUS_NEEDS_REVIEW: 0, STATUS_MISSING: 0, STATUS_BLOCKED: 0, STATUS_FAIL: 0}
    for capture in captures:
        counts[capture["status"]] = counts.get(capture["status"], 0) + 1
    return counts


def manifest_template(root: Path, output_dir: Path) -> dict[str, Any]:
    captures: dict[str, dict[str, Any]] = {}
    for case in CAPTURE_CASES:
        entry: dict[str, Any] = {
            "status": STATUS_MISSING,
            "capturePath": rel_path(root, expected_renderdoc_path(output_dir, case)) if "renderdoc" in case.capture_tool else "",
            "apiTracePath": rel_path(root, expected_api_trace_path(output_dir, case)) if "api-trace" in case.capture_tool else "",
            "metadata": {field: "" for field in REQUIRED_METADATA_FIELDS},
            "cvars": case.cvars,
            "checks": {check: "" for check in case.required_checks},
            "reviewedBy": "",
            "notes": "",
        }
        captures[case.id] = entry
    return {
        "metadata": {
            "generated": now_string(),
            "instructions": "Fill artifact paths, platform metadata, cvar observations, check statuses, reviewer, and notes, then rerun renderer_capture_summary.py --manifest this-file.",
        },
        "captures": captures,
    }


def build_payload(root: Path, output_dir: Path, manifest_path: str | None) -> dict[str, Any]:
    entries = load_manifest(root, manifest_path)
    captures = [assess_case(root, output_dir, case, entries.get(case.id)) for case in CAPTURE_CASES]
    return {
        "metadata": {
            "generated": now_string(),
            "host": f"{platform.system()} {platform.release()} {platform.machine()}",
            "outputDir": rel_path(root, output_dir),
            "manifest": rel_path(root, resolve_path(root, manifest_path)) if manifest_path else "",
            "captureTools": "RenderDoc .rdc for forced core-profile modern tiers; API trace JSON/text for ARB2 compatibility and bind-count claims",
        },
        "overallStatus": overall_status(captures),
        "statusCounts": status_counts(captures),
        "namingConvention": {
            "root": ".tmp/renderer-captures/<run>",
            "caseDirectory": "<case-id>/",
            "renderdocCapture": "<case-id>/<case-id>.rdc",
            "apiTrace": "<case-id>/<case-id>.api-trace.json",
            "review": "<case-id>/review.json",
            "summary": "capture_summary.md and capture_summary.json",
        },
        "requiredMetadataFields": list(REQUIRED_METADATA_FIELDS),
        "resourceCheckDescriptions": RESOURCE_CHECK_DESCRIPTIONS,
        "captureMatrix": [case_to_dict(case) for case in CAPTURE_CASES],
        "captures": captures,
    }


def write_summary(root: Path, output_dir: Path, payload: dict[str, Any], write_template: bool) -> tuple[Path, Path, Path | None]:
    output_dir.mkdir(parents=True, exist_ok=True)
    report_json = output_dir / "capture_summary.json"
    report_md = output_dir / "capture_summary.md"
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    counts = payload["statusCounts"]
    lines = [
        "# Renderer RenderDoc/API Capture Summary",
        "",
        f"- Generated: {payload['metadata']['generated']}",
        f"- Host: {payload['metadata']['host']}",
        f"- Overall status: **{payload['overallStatus']}**",
        f"- Gate counts: pass={counts.get(STATUS_PASS, 0)}, needs-review={counts.get(STATUS_NEEDS_REVIEW, 0)}, missing={counts.get(STATUS_MISSING, 0)}, blocked={counts.get(STATUS_BLOCKED, 0)}, fail={counts.get(STATUS_FAIL, 0)}",
        "",
        "This artifact standardizes the capture matrix. It does not claim RenderDoc/API evidence is complete until every row is `pass`.",
        "",
        "## Naming Convention",
        "",
        "| Item | Convention |",
        "|---|---|",
    ]
    for key, value in payload["namingConvention"].items():
        lines.append(f"| {markdown_cell(key)} | `{markdown_cell(value)}` |")

    lines += [
        "",
        "## Capture Matrix",
        "",
        "| Status | Case | Mode | Scene | Path | Tool | Tier | Required Checks | Evidence | Notes |",
        "|---|---|---|---|---|---|---|---|---|---|",
    ]
    for capture in payload["captures"]:
        evidence_parts = []
        if "renderdoc" in capture["captureTool"]:
            evidence_parts.append(f"rdc={capture['capturePath']}")
        if "api-trace" in capture["captureTool"]:
            evidence_parts.append(f"api={capture['apiTracePath']}")
        lines.append(
            f"| {capture['status']} | `{capture['id']}` | {capture['mode']} | `{capture['scene']}` | "
            f"{markdown_cell(capture['rendererPath'])} | {capture['captureTool']} | `{capture['requestedTier']}` | "
            f"{markdown_cell(', '.join(capture['requiredChecks']))} | `{markdown_cell('; '.join(evidence_parts))}` | "
            f"{markdown_cell('; '.join(capture['notes']))} |"
        )

    lines += [
        "",
        "## Required Metadata",
        "",
        "Every completed row records these fields: " + ", ".join(f"`{field}`" for field in REQUIRED_METADATA_FIELDS) + ".",
        "",
        "## Check Definitions",
        "",
        "| Check | Meaning |",
        "|---|---|",
    ]
    for check, description in RESOURCE_CHECK_DESCRIPTIONS.items():
        lines.append(f"| `{check}` | {description} |")

    lines += [
        "",
        "## Capture Commands",
        "",
        "RenderDoc remains the preferred frame-inspection tool for forced core-profile modern tiers. The ARB2 compatibility path should use an API/driver trace until compatibility-profile RenderDoc capture is proven reliable on the target machine.",
        "",
        "```powershell",
        "powershell -NoProfile -ExecutionPolicy Bypass -File .\\tools\\debug\\renderdoc_capture.ps1 -Mode SP -Map game/storage1 -AllowUnsupported",
        "python tools\\tests\\renderer_capture_summary.py --output-dir .tmp\\renderer-captures\\<run> --manifest .tmp\\renderer-captures\\<run>\\capture_manifest_template.json",
        "```",
        "",
        "## Remaining Work",
        "",
    ]
    pending = [capture for capture in payload["captures"] if capture["status"] != STATUS_PASS]
    if pending:
        for capture in pending:
            lines.append(f"- {capture['id']}: {capture['status']}.")
    else:
        lines.append("- No capture evidence rows are pending.")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")

    template_path = None
    if write_template:
        template_path = output_dir / "capture_manifest_template.json"
        template_path.write_text(json.dumps(manifest_template(root, output_dir), indent=2), encoding="utf-8")

    return report_json, report_md, template_path


def print_matrix() -> None:
    for case in CAPTURE_CASES:
        checks = ", ".join(case.required_checks)
        cvars = " ".join(f"+set {key} {value}" for key, value in case.cvars.items())
        print(f"{case.id}: {case.mode} {case.scene} tier={case.requested_tier} tool={case.capture_tool}")
        print(f"  path: {case.renderer_path}")
        print(f"  checks: {checks}")
        print(f"  cvars: {cvars}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default=".tmp/renderer-captures/capture-summary", help="Directory for capture_summary.md/json.")
    parser.add_argument("--manifest", help="Optional filled capture manifest JSON.")
    parser.add_argument("--write-template", action="store_true", help="Write capture_manifest_template.json next to the summary.")
    parser.add_argument("--print-matrix", action="store_true", help="Print the required capture matrix and exit.")
    parser.add_argument("--require-complete", action="store_true", help="Exit nonzero unless every capture row is pass.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.print_matrix:
        print_matrix()
        return 0

    root = repo_root()
    output_dir = resolve_path(root, args.output_dir)
    assert output_dir is not None
    payload = build_payload(root, output_dir, args.manifest)
    report_json, report_md, template_path = write_summary(root, output_dir, payload, args.write_template)
    print(f"Wrote {rel_path(root, report_md)}")
    print(f"Wrote {rel_path(root, report_json)}")
    if template_path is not None:
        print(f"Wrote {rel_path(root, template_path)}")
    print(f"Overall status: {payload['overallStatus']}")
    if args.require_complete and payload["overallStatus"] != STATUS_PASS:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
