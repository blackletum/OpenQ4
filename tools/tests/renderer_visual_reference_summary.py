#!/usr/bin/env python3
"""Generate an approved visual-reference evidence summary.

The gameplay benchmark can compare TGA screenshots against a reference
directory, but promotion needs one reviewed bundle that says which references
were approved, which human-review checks were performed, and whether the
reference-backed run actually passed. This tool standardizes that manifest and
keeps the gate blocked until the binary references remain external/manual but
the review evidence is explicit.
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

APPROVED_REFERENCE_STATUSES = {"approved", STATUS_PASS}

REQUIRED_BUNDLE_METADATA = (
    "platform",
    "os",
    "gpu",
    "driverVersion",
    "glVendor",
    "glRenderer",
    "glVersion",
)

REVIEW_CHECK_DESCRIPTIONS = {
    "blackFramesAbsent": "screenshots were inspected for black or empty frames",
    "postProcessPresent": "post-process output was visible where expected",
    "guiSubviewCompositionPresent": "GUI, cinematic, and subview composition was inspected",
    "lensFlareReviewed": "lens-flare accumulation/composite output was inspected",
    "shadowStencilFallbackReviewed": "shadow-map and stencil-fallback behavior was inspected",
    "bseEffectsVisible": "BSE effect visibility was inspected",
    "mpHudWeaponViewReviewed": "MP client HUD and weapon-view composition was inspected",
}


@dataclass(frozen=True)
class VisualReferenceCase:
    id: str
    title: str
    mode: str
    scene: str
    tier: str
    maxfps: str
    swap_interval: str
    display: str
    lens_flare: str
    required_roles: tuple[str, ...]
    review_checks: tuple[str, ...]
    focus: str


VISUAL_REFERENCE_CASES = (
    VisualReferenceCase(
        id="lensflare-airdefense1",
        title="Airdefense lens-flare/post-process reference",
        mode="SP",
        scene="game/airdefense1",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display="windowed",
        lens_flare="high",
        required_roles=("sp",),
        review_checks=("blackFramesAbsent", "postProcessPresent", "lensFlareReviewed", "shadowStencilFallbackReviewed"),
        focus="outdoor lighting, sky/terrain visibility, and high-quality lens-flare/post output",
    ),
    VisualReferenceCase(
        id="sp-medlabs",
        title="Medlabs BSE-heavy reference",
        mode="SP",
        scene="game/medlabs",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display="windowed",
        lens_flare="high",
        required_roles=("sp",),
        review_checks=("blackFramesAbsent", "bseEffectsVisible", "postProcessPresent"),
        focus="BSE-heavy stock scripted effects and post-process composition",
    ),
    VisualReferenceCase(
        id="sp-mcc-landing",
        title="MCC Landing GUI/subview reference",
        mode="SP",
        scene="game/mcc_landing",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display="windowed",
        lens_flare="high",
        required_roles=("sp",),
        review_checks=("blackFramesAbsent", "guiSubviewCompositionPresent", "postProcessPresent"),
        focus="cinematic handoff, GUI, subview, and remote-camera composition",
    ),
    VisualReferenceCase(
        id="sp-storage2",
        title="Storage2 dense-light/post reference",
        mode="SP",
        scene="game/storage2",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display="windowed",
        lens_flare="high",
        required_roles=("sp",),
        review_checks=("blackFramesAbsent", "postProcessPresent", "shadowStencilFallbackReviewed"),
        focus="indoor materials, dense local lights, post-process, and shadow fallback behavior",
    ),
    VisualReferenceCase(
        id="mp-q4dm1-listen",
        title="MP q4dm1 listen/client reference",
        mode="MP",
        scene="mp/q4dm1",
        tier="auto",
        maxfps="240",
        swap_interval="0",
        display="windowed",
        lens_flare="high",
        required_roles=("server", "client"),
        review_checks=("blackFramesAbsent", "mpHudWeaponViewReviewed"),
        focus="listen-server plus loopback-client renderer parity, HUD, and weapon-view composition",
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
        return str(path.resolve().relative_to(root.resolve())).replace("\\", "/")
    except ValueError:
        return str(path)


def read_json(path: Path) -> tuple[dict[str, Any] | None, str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return None, str(exc)
    if not isinstance(payload, dict):
        return None, "top-level JSON value is not an object"
    return payload, ""


def markdown_cell(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", "<br>")


def case_to_dict(case: VisualReferenceCase) -> dict[str, Any]:
    return {
        "id": case.id,
        "title": case.title,
        "mode": case.mode,
        "scene": case.scene,
        "tier": case.tier,
        "maxfps": case.maxfps,
        "swapInterval": case.swap_interval,
        "display": case.display,
        "lensFlare": case.lens_flare,
        "requiredRoles": list(case.required_roles),
        "reviewChecks": list(case.review_checks),
        "focus": case.focus,
    }


def default_manifest(output_dir: Path) -> dict[str, Any]:
    return {
        "schema": "openq4.renderer.visualReferences.v1",
        "bundle": {
            "id": output_dir.name,
            "approvalStatus": STATUS_NEEDS_REVIEW,
            "approvedBy": "",
            "approvedDate": "",
            "platform": "Windows x64",
            "os": "",
            "gpu": "",
            "driverVersion": "",
            "glVendor": "",
            "glRenderer": "",
            "glVersion": "",
            "referenceDir": ".tmp/renderer-references/mid-term-visual/windows-x64",
            "sourceReport": ".tmp/renderer-gameplay/visual-run/renderer_gameplay_benchmark_report.json",
            "imageRmsThreshold": 2.0,
            "imageMaxThreshold": 24,
            "notes": "",
        },
        "reviewChecklist": {key: STATUS_NEEDS_REVIEW for key in REVIEW_CHECK_DESCRIPTIONS},
        "references": [
            {
                "id": case.id,
                "title": case.title,
                "mode": case.mode,
                "scene": case.scene,
                "requiredRoles": list(case.required_roles),
                "focus": case.focus,
                "roles": {
                    role: {
                        "status": STATUS_MISSING,
                        "reference": "",
                        "sha256": "",
                        "notes": "",
                    }
                    for role in case.required_roles
                },
            }
            for case in VISUAL_REFERENCE_CASES
        ],
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def print_matrix() -> None:
    for case in VISUAL_REFERENCE_CASES:
        roles = ",".join(case.required_roles)
        checks = ",".join(case.review_checks)
        print(f"{case.id}: {case.mode} {case.scene} roles={roles} checks={checks} - {case.focus}")


def manifest_reference_entries(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    entries = manifest.get("references", [])
    if not isinstance(entries, list):
        return {}
    result: dict[str, dict[str, Any]] = {}
    for item in entries:
        if not isinstance(item, dict):
            continue
        case_id = str(item.get("id", ""))
        if case_id:
            result[case_id] = item
    return result


def role_entry(case_entry: dict[str, Any] | None, role: str) -> dict[str, Any]:
    if not case_entry:
        return {}
    roles = case_entry.get("roles", {})
    if isinstance(roles, dict):
        item = roles.get(role, {})
        return item if isinstance(item, dict) else {}
    if isinstance(roles, list):
        for item in roles:
            if isinstance(item, dict) and item.get("role") == role:
                return item
    return {}


def value_is_approved(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in APPROVED_REFERENCE_STATUSES


def path_is_under(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def find_case_result(gameplay_payload: dict[str, Any] | None, case: VisualReferenceCase) -> dict[str, Any] | None:
    if gameplay_payload is None:
        return None
    results = gameplay_payload.get("results", [])
    if not isinstance(results, list):
        return None
    candidates = []
    for item in results:
        if not isinstance(item, dict):
            continue
        result_id = str(item.get("id", ""))
        if not result_id.startswith(case.id):
            continue
        if item.get("map") and item.get("map") != case.scene:
            continue
        candidates.append(item)
    if not candidates:
        return None

    def score(item: dict[str, Any]) -> int:
        points = 0
        points += 1 if str(item.get("tier", "")) == case.tier else 0
        points += 1 if str(item.get("maxfps", "")) == case.maxfps else 0
        points += 1 if str(item.get("swapInterval", "")) == case.swap_interval else 0
        points += 1 if str(item.get("display", "")) == case.display else 0
        points += 1 if str(item.get("lensFlarePreset", "")) == case.lens_flare else 0
        return points

    return sorted(candidates, key=score, reverse=True)[0]


def find_role_result(case_result: dict[str, Any] | None, role: str) -> dict[str, Any] | None:
    if case_result is None:
        return None
    roles = case_result.get("roles", [])
    if not isinstance(roles, list):
        return None
    for item in roles:
        if isinstance(item, dict) and item.get("role") == role:
            return item
    return None


def status_from_notes(notes: list[str], role_statuses: list[str]) -> str:
    if any(status == STATUS_FAIL for status in role_statuses):
        return STATUS_FAIL
    if any(status == STATUS_MISSING for status in role_statuses):
        return STATUS_MISSING
    if any(status == STATUS_NEEDS_REVIEW for status in role_statuses):
        return STATUS_NEEDS_REVIEW
    if notes:
        return STATUS_NEEDS_REVIEW
    return STATUS_PASS


def evaluate_case(
    root: Path,
    case: VisualReferenceCase,
    manifest: dict[str, Any] | None,
    gameplay_payload: dict[str, Any] | None,
    reference_dir: Path | None,
    effective_rms_threshold: float,
    effective_max_threshold: int,
) -> dict[str, Any]:
    notes: list[str] = []
    role_summaries: list[dict[str, Any]] = []
    role_statuses: list[str] = []
    manifest_entries = manifest_reference_entries(manifest or {})
    case_entry = manifest_entries.get(case.id)
    case_result = find_case_result(gameplay_payload, case)

    if manifest is None:
        notes.append("manifest is missing")
    elif case_entry is None:
        notes.append("case is missing from manifest")

    if gameplay_payload is None:
        notes.append("reference-backed gameplay report is missing")
    elif case_result is None:
        notes.append("case is missing from gameplay report")

    for role in case.required_roles:
        status = STATUS_PASS
        role_notes: list[str] = []
        manifest_role = role_entry(case_entry, role)
        report_role = find_role_result(case_result, role)
        image = report_role.get("image", {}) if isinstance(report_role, dict) else {}
        if not isinstance(image, dict):
            image = {}

        if not manifest_role:
            status = STATUS_MISSING
            role_notes.append("role is missing from manifest")
        elif not value_is_approved(manifest_role.get("status")) and not value_is_approved(manifest_role.get("approved")):
            status = STATUS_NEEDS_REVIEW
            role_notes.append("manifest role is not approved")

        if report_role is None:
            status = STATUS_MISSING
            role_notes.append("role is missing from gameplay report")
        elif report_role.get("status") != STATUS_PASS:
            status = STATUS_FAIL
            role_notes.append(f"gameplay role status is {report_role.get('status')}")

        if image.get("status") != "compared":
            if image.get("status"):
                role_notes.append(f"image status is {image.get('status')}")
            else:
                role_notes.append("image comparison is missing")
            status = STATUS_FAIL if image.get("status") == "dimension-mismatch" else max_status(status, STATUS_MISSING)
        elif not bool(image.get("pass", False)):
            status = STATUS_FAIL
            role_notes.append(f"image comparison failed rms={image.get('rms')} maxDelta={image.get('maxDelta')}")
        else:
            try:
                rms = float(image.get("rms", 0.0))
            except (TypeError, ValueError):
                rms = effective_rms_threshold + 1.0
            try:
                max_delta = int(image.get("maxDelta", 999999))
            except (TypeError, ValueError):
                max_delta = effective_max_threshold + 1
            if rms > effective_rms_threshold or max_delta > effective_max_threshold:
                status = STATUS_FAIL
                role_notes.append(f"comparison exceeds effective thresholds rms={rms} maxDelta={max_delta}")

        reference_path_text = str(image.get("reference") or manifest_role.get("reference") or "")
        reference_path = resolve_path(root, reference_path_text) if reference_path_text else None
        if reference_path is None:
            status = max_status(status, STATUS_MISSING)
            role_notes.append("approved reference path is missing")
        elif not reference_path.exists():
            status = max_status(status, STATUS_MISSING)
            role_notes.append(f"approved reference does not exist: {rel_path(root, reference_path)}")
        elif reference_dir is not None and not path_is_under(reference_path, reference_dir):
            status = STATUS_FAIL
            role_notes.append("approved reference is outside referenceDir")

        manifest_reference = str(manifest_role.get("reference", ""))
        if manifest_reference and image.get("reference"):
            manifest_path = resolve_path(root, manifest_reference)
            image_path = resolve_path(root, str(image.get("reference")))
            if manifest_path is not None and image_path is not None and manifest_path != image_path:
                status = STATUS_FAIL
                role_notes.append("manifest reference path does not match gameplay comparison reference")

        role_statuses.append(status)
        role_summaries.append(
            {
                "role": role,
                "status": status,
                "reference": rel_path(root, reference_path) if reference_path else "",
                "actual": rel_path(root, image.get("actual", "")) if image.get("actual") else "",
                "imageStatus": image.get("status", ""),
                "rms": image.get("rms", ""),
                "maxDelta": image.get("maxDelta", ""),
                "sha256": image.get("sha256") or manifest_role.get("sha256", ""),
                "notes": role_notes,
            }
        )

    status = status_from_notes([], role_statuses)
    if status == STATUS_PASS and notes:
        status = STATUS_NEEDS_REVIEW
    elif status == STATUS_PASS:
        missing_checks = [
            check
            for check in case.review_checks
            if manifest is None or str((manifest.get("reviewChecklist", {}) or {}).get(check, "")).strip().lower() != STATUS_PASS
        ]
        if missing_checks:
            status = STATUS_NEEDS_REVIEW
            notes.append("review checks not pass: " + ", ".join(missing_checks))

    return {
        "id": case.id,
        "title": case.title,
        "status": status,
        "mode": case.mode,
        "scene": case.scene,
        "requiredRoles": list(case.required_roles),
        "reviewChecks": list(case.review_checks),
        "roles": role_summaries,
        "notes": notes,
    }


def max_status(current: str, candidate: str) -> str:
    order = {
        STATUS_PASS: 0,
        STATUS_NEEDS_REVIEW: 1,
        STATUS_MISSING: 2,
        STATUS_BLOCKED: 3,
        STATUS_FAIL: 4,
    }
    return candidate if order[candidate] > order[current] else current


def evaluate_bundle_checks(
    root: Path,
    manifest: dict[str, Any] | None,
    manifest_path: Path | None,
    gameplay_payload: dict[str, Any] | None,
    gameplay_path: Path | None,
    reference_dir: Path | None,
) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []

    def add(check_id: str, title: str, status: str, detail: str) -> None:
        checks.append({"id": check_id, "title": title, "status": status, "detail": detail})

    if manifest is None:
        add("manifest-present", "Approved reference manifest is present", STATUS_MISSING, "no manifest supplied")
        return checks

    add("manifest-present", "Approved reference manifest is present", STATUS_PASS, rel_path(root, manifest_path))
    bundle = manifest.get("bundle", {})
    if not isinstance(bundle, dict):
        add("bundle-object", "Manifest bundle metadata is present", STATUS_FAIL, "bundle is missing or not an object")
        bundle = {}
    else:
        add("bundle-object", "Manifest bundle metadata is present", STATUS_PASS, str(bundle.get("id", "")))

    approval_status = str(bundle.get("approvalStatus", "")).strip().lower()
    approved_by = str(bundle.get("approvedBy", "")).strip()
    approved_date = str(bundle.get("approvedDate", "")).strip()
    approval_pass = approval_status == STATUS_PASS and bool(approved_by) and bool(approved_date)
    add(
        "approval-status",
        "Reference bundle has explicit approval",
        STATUS_PASS if approval_pass else STATUS_NEEDS_REVIEW,
        f"approvalStatus={approval_status or 'missing'} approvedBy={approved_by or 'missing'} approvedDate={approved_date or 'missing'}",
    )

    missing_metadata = [field for field in REQUIRED_BUNDLE_METADATA if not str(bundle.get(field, "")).strip()]
    add(
        "platform-metadata",
        "Reference platform and GL metadata are recorded",
        STATUS_PASS if not missing_metadata else STATUS_NEEDS_REVIEW,
        "missing=" + ",".join(missing_metadata or ["none"]),
    )

    if reference_dir is None:
        add("reference-dir", "Reference directory is declared and exists", STATUS_MISSING, "referenceDir is missing")
    elif reference_dir.exists():
        add("reference-dir", "Reference directory is declared and exists", STATUS_PASS, rel_path(root, reference_dir))
    else:
        add("reference-dir", "Reference directory is declared and exists", STATUS_MISSING, rel_path(root, reference_dir))

    review_checklist = manifest.get("reviewChecklist", {})
    if not isinstance(review_checklist, dict):
        review_checklist = {}
    missing_checks = [
        key for key in REVIEW_CHECK_DESCRIPTIONS if str(review_checklist.get(key, "")).strip().lower() != STATUS_PASS
    ]
    add(
        "review-checklist",
        "Human visual-review checklist is complete",
        STATUS_PASS if not missing_checks else STATUS_NEEDS_REVIEW,
        "missing=" + ",".join(missing_checks or ["none"]),
    )

    if gameplay_payload is None:
        add("gameplay-report", "Reference-backed gameplay report is present", STATUS_MISSING, rel_path(root, gameplay_path))
        return checks

    add("gameplay-report", "Reference-backed gameplay report is present", STATUS_PASS, rel_path(root, gameplay_path))
    metadata = gameplay_payload.get("metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}
    profile = str(metadata.get("profile", ""))
    add(
        "visual-profile",
        "Gameplay report used the visual-comparison profile",
        STATUS_PASS if profile == "visual-comparison" else STATUS_NEEDS_REVIEW,
        f"profile={profile or 'missing'}",
    )
    require_references = bool(metadata.get("requireReferences"))
    report_reference_dir = str(metadata.get("referenceDir", ""))
    add(
        "require-references",
        "Gameplay report required approved references",
        STATUS_PASS if require_references and report_reference_dir else STATUS_FAIL,
        f"requireReferences={int(require_references)} referenceDir={report_reference_dir or 'missing'}",
    )
    return checks


def build_summary(
    root: Path,
    output_dir: Path,
    manifest_path: Path | None,
    gameplay_report_path: Path | None,
) -> dict[str, Any]:
    manifest: dict[str, Any] | None = None
    manifest_error = ""
    if manifest_path is not None and manifest_path.exists():
        manifest, manifest_error = read_json(manifest_path)
    elif manifest_path is not None:
        manifest_error = "manifest path does not exist"

    bundle = manifest.get("bundle", {}) if isinstance(manifest, dict) else {}
    if not isinstance(bundle, dict):
        bundle = {}

    if gameplay_report_path is None:
        gameplay_report_path = resolve_path(root, str(bundle.get("sourceReport", "")))

    gameplay_payload: dict[str, Any] | None = None
    gameplay_error = ""
    if gameplay_report_path is not None and gameplay_report_path.exists():
        gameplay_payload, gameplay_error = read_json(gameplay_report_path)
    elif gameplay_report_path is not None:
        gameplay_error = "gameplay report path does not exist"

    reference_dir = resolve_path(root, str(bundle.get("referenceDir", "")))
    report_metadata = gameplay_payload.get("metadata", {}) if isinstance(gameplay_payload, dict) else {}
    if not isinstance(report_metadata, dict):
        report_metadata = {}

    try:
        effective_rms = float(bundle.get("imageRmsThreshold", report_metadata.get("imageRmsThreshold", 2.0)))
    except (TypeError, ValueError):
        effective_rms = 2.0
    try:
        effective_max = int(bundle.get("imageMaxThreshold", report_metadata.get("imageMaxThreshold", 24)))
    except (TypeError, ValueError):
        effective_max = 24

    results = [
        evaluate_case(root, case, manifest, gameplay_payload, reference_dir, effective_rms, effective_max)
        for case in VISUAL_REFERENCE_CASES
    ]
    checks = evaluate_bundle_checks(root, manifest, manifest_path, gameplay_payload, gameplay_report_path, reference_dir)
    if manifest_error:
        manifest_status = STATUS_MISSING if "does not exist" in manifest_error else STATUS_FAIL
        checks.append({"id": "manifest-parse", "title": "Manifest JSON parses", "status": manifest_status, "detail": manifest_error})
    if gameplay_error:
        gameplay_status = STATUS_MISSING if "does not exist" in gameplay_error else STATUS_FAIL
        checks.append({"id": "gameplay-parse", "title": "Gameplay report JSON parses", "status": gameplay_status, "detail": gameplay_error})

    failed_results = [result for result in results if result["status"] == STATUS_FAIL]
    non_pass_results = [result for result in results if result["status"] != STATUS_PASS]
    failed_checks = [check for check in checks if check["status"] == STATUS_FAIL]
    non_pass_checks = [check for check in checks if check["status"] != STATUS_PASS]
    if failed_results or failed_checks:
        overall = STATUS_FAIL
    elif non_pass_results or non_pass_checks:
        overall = STATUS_BLOCKED
    else:
        overall = STATUS_PASS

    return {
        "schema": "openq4.renderer.visualReferenceSummary.v1",
        "overallStatus": overall,
        "generated": now_string(),
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "metadata": {
            "profile": "visual-comparison",
            "manifest": rel_path(root, manifest_path),
            "sourceReport": rel_path(root, gameplay_report_path),
            "referenceDir": rel_path(root, reference_dir),
            "requireReferences": bool(report_metadata.get("requireReferences")),
            "imageRmsThreshold": effective_rms,
            "imageMaxThreshold": effective_max,
            "policy": "Reference images stay external/manual under .tmp; this summary and manifest template describe the approved bundle.",
        },
        "matrix": [case_to_dict(case) for case in VISUAL_REFERENCE_CASES],
        "checks": checks,
        "results": results,
    }


def write_markdown(summary: dict[str, Any], path: Path) -> None:
    lines = [
        "# Renderer Visual Reference Summary",
        "",
        f"- Status: `{summary['overallStatus']}`",
        f"- Generated: `{summary['generated']}`",
        f"- Manifest: `{summary['metadata'].get('manifest') or 'not supplied'}`",
        f"- Source report: `{summary['metadata'].get('sourceReport') or 'not supplied'}`",
        f"- Reference dir: `{summary['metadata'].get('referenceDir') or 'not supplied'}`",
        f"- Require references: `{1 if summary['metadata'].get('requireReferences') else 0}`",
        f"- Image thresholds: RMS `{summary['metadata'].get('imageRmsThreshold')}`, max `{summary['metadata'].get('imageMaxThreshold')}`",
        f"- Policy: {summary['metadata'].get('policy')}",
        "",
        "## Bundle Checks",
        "",
        "| Check | Status | Detail |",
        "| --- | --- | --- |",
    ]
    for check in summary["checks"]:
        lines.append(f"| {markdown_cell(check['title'])} | `{check['status']}` | {markdown_cell(check['detail'])} |")

    lines.extend(
        [
            "",
            "## Visual Cases",
            "",
            "| Case | Status | Scene | Roles | Review Checks | Notes |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
    )
    for result in summary["results"]:
        notes = "; ".join(result.get("notes", []))
        lines.append(
            f"| `{result['id']}` | `{result['status']}` | `{result['scene']}` | "
            f"{', '.join(result.get('requiredRoles', []))} | {', '.join(result.get('reviewChecks', []))} | {markdown_cell(notes)} |"
        )
        for role in result.get("roles", []):
            role_notes = "; ".join(role.get("notes", []))
            lines.append(
                f"| `{result['id']}:{role.get('role')}` | `{role.get('status')}` |  | "
                f"`{markdown_cell(role.get('reference', ''))}` | "
                f"image={role.get('imageStatus', '')} rms={role.get('rms', '')} max={role.get('maxDelta', '')} | "
                f"{markdown_cell(role_notes)} |"
            )

    lines.extend(
        [
            "",
            "## Matrix",
            "",
            "| Case | Mode | Scene | Focus |",
            "| --- | --- | --- | --- |",
        ]
    )
    for item in summary["matrix"]:
        lines.append(f"| `{item['id']}` | {item['mode']} | `{item['scene']}` | {markdown_cell(item['focus'])} |")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        default=".tmp/renderer-references/visual-reference-summary",
        help="Directory for visual_reference_summary.md/json and optional manifest template.",
    )
    parser.add_argument("--manifest", default="", help="Approved visual-reference manifest JSON to evaluate.")
    parser.add_argument("--gameplay-report", default="", help="Reference-backed renderer_gameplay_benchmark_report.json to evaluate.")
    parser.add_argument("--write-template", action="store_true", help="Write visual_reference_manifest_template.json.")
    parser.add_argument("--print-matrix", action="store_true", help="Print the visual reference matrix and exit.")
    parser.add_argument("--require-complete", action="store_true", help="Return non-zero unless the summary is pass.")
    args = parser.parse_args(argv)

    if args.print_matrix:
        print_matrix()
        return 0

    root = repo_root()
    output_dir = resolve_path(root, args.output_dir)
    assert output_dir is not None
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.write_template:
        write_json(output_dir / "visual_reference_manifest_template.json", default_manifest(output_dir))

    manifest_path = resolve_path(root, args.manifest)
    gameplay_report_path = resolve_path(root, args.gameplay_report)
    summary = build_summary(root, output_dir, manifest_path, gameplay_report_path)
    write_json(output_dir / "visual_reference_summary.json", summary)
    write_markdown(summary, output_dir / "visual_reference_summary.md")

    print(f"visual reference summary: {summary['overallStatus']}")
    print(f"markdown: {output_dir / 'visual_reference_summary.md'}")
    print(f"json: {output_dir / 'visual_reference_summary.json'}")
    if args.write_template:
        print(f"template: {output_dir / 'visual_reference_manifest_template.json'}")

    if args.require_complete and summary["overallStatus"] != STATUS_PASS:
        print(f"visual reference summary is {summary['overallStatus']}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
