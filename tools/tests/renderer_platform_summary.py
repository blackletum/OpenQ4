#!/usr/bin/env python3
"""Summarize cross-platform renderer validation evidence.

Promotion and portability claims need platform-specific proof. This tool turns
safe validation, gameplay, visual comparison, and presentation reports into one
platform ledger so Windows evidence cannot implicitly satisfy Linux or macOS
requirements.
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

REQUIRED_PLATFORM_METADATA = (
    "executablePath",
    "os",
    "gpu",
    "driverVersion",
    "glVendor",
    "glRenderer",
    "glVersion",
    "selectedTier",
    "requestedTier",
    "contextProfile",
    "debugContextAvailable",
    "timerQueryAvailable",
    "uploadManagerMode",
    "persistentMap",
    "mapRangeSubdataFallback",
    "dsaAvailable",
    "multibindAvailable",
    "gpuDrivenAvailable",
)

METADATA_DESCRIPTIONS = {
    "executablePath": "client executable used for the report",
    "os": "OS version and architecture",
    "gpu": "GPU model or reviewed adapter name",
    "driverVersion": "driver version from the target machine",
    "glVendor": "OpenGL vendor string",
    "glRenderer": "OpenGL renderer string",
    "glVersion": "OpenGL version/context string",
    "selectedTier": "renderer-selected GL tier",
    "requestedTier": "requested GL tier or auto/forced tier",
    "contextProfile": "OpenGL context profile",
    "debugContextAvailable": "debug context request/actual availability",
    "timerQueryAvailable": "timer-query availability",
    "uploadManagerMode": "upload manager mode and ring/buffer summary",
    "persistentMap": "persistent-map support or fallback state",
    "mapRangeSubdataFallback": "map-range/subdata fallback support",
    "dsaAvailable": "DSA availability",
    "multibindAvailable": "multibind availability",
    "gpuDrivenAvailable": "GPU-driven/modern executor capability availability",
}


@dataclass(frozen=True)
class ReportRequirement:
    key: str
    title: str
    required: bool
    expected_profile: str
    require_references: bool = False
    require_zero_warnings: bool = True


@dataclass(frozen=True)
class PlatformRequirement:
    id: str
    title: str
    host_tokens: tuple[str, ...]
    arch_tokens: tuple[str, ...]
    tier_tokens: tuple[str, ...]
    notes: str


REPORT_REQUIREMENTS = (
    ReportRequirement(
        key="safeReport",
        title="safe renderer validation",
        required=True,
        expected_profile="",
    ),
    ReportRequirement(
        key="requiredGameplayReport",
        title="required SP/MP gameplay",
        required=True,
        expected_profile="required",
    ),
    ReportRequirement(
        key="visualComparisonReport",
        title="reference-backed visual comparison",
        required=True,
        expected_profile="visual-comparison",
        require_references=True,
    ),
    ReportRequirement(
        key="presentationReport",
        title="presentation pacing comparison",
        required=True,
        expected_profile="presentation-comparison",
    ),
    ReportRequirement(
        key="platformNotesReport",
        title="platform capability notes",
        required=False,
        expected_profile="",
    ),
)


PLATFORM_REQUIREMENTS = (
    PlatformRequirement(
        id="windows-x64",
        title="Windows x64",
        host_tokens=("windows",),
        arch_tokens=("amd64", "x86_64", "x64"),
        tier_tokens=(),
        notes="Windows x64 baseline evidence; it does not satisfy Linux or macOS readiness.",
    ),
    PlatformRequirement(
        id="linux-x64",
        title="Linux x64",
        host_tokens=("linux",),
        arch_tokens=("amd64", "x86_64", "x64"),
        tier_tokens=(),
        notes="Linux x64 evidence must come from a Linux GL runner or target machine.",
    ),
    PlatformRequirement(
        id="macos-gl41",
        title="macOS GL 4.1",
        host_tokens=("darwin", "macos", "mac os"),
        arch_tokens=(),
        tier_tokens=("gl41", "topgl41", "4.1"),
        notes="macOS evidence must prove the GL 4.1/fallback path; GL 4.3+ is not expected on Apple OpenGL.",
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


def markdown_cell(value: Any) -> str:
    text = "" if value is None else str(value)
    return text.replace("|", "\\|").replace("\n", "<br>")


def read_json(path: Path) -> tuple[dict[str, Any] | None, str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return None, str(exc)
    if not isinstance(payload, dict):
        return None, "top-level JSON value is not an object"
    return payload, ""


def find_json_companion(path: Path) -> Path | None:
    if path.suffix.lower() == ".json":
        return path
    companion = path.with_suffix(".json")
    if companion.exists():
        return companion
    return None


def normalize_status(value: Any) -> str:
    status = str(value or "").strip().lower()
    if status in VALID_STATUSES:
        return status
    return ""


def warning_count_from_dict(value: Any) -> int:
    if not isinstance(value, dict):
        return 0
    total = 0
    for count in value.values():
        try:
            total += int(count)
        except (TypeError, ValueError):
            continue
    return total


def warning_count(result: dict[str, Any]) -> int:
    total = warning_count_from_dict(result.get("warningSignatures"))
    roles = result.get("roles", [])
    if isinstance(roles, list):
        for role in roles:
            if isinstance(role, dict):
                total += warning_count_from_dict(role.get("warnings"))
    return total


def default_report_paths(platform_id: str) -> dict[str, str]:
    if platform_id == "windows-x64":
        return {
            "safeReport": ".tmp/renderer-validation/mid-term-safe-matrix-r1/renderer_validation_report.json",
            "requiredGameplayReport": ".tmp/renderer-gameplay/mid-term-required-gameplay-r1/renderer_gameplay_benchmark_report.json",
            "visualComparisonReport": ".tmp/renderer-gameplay/mid-term-visual-comparison-r1/renderer_gameplay_benchmark_report.json",
            "presentationReport": ".tmp/renderer-gameplay/mid-term-presentation-comparison-r1/renderer_gameplay_benchmark_report.json",
            "platformNotesReport": ".tmp/renderer-validation/mid-term-platform-notes-r1/renderer_validation_report.json",
        }
    if platform_id == "linux-x64":
        return {
            "safeReport": ".tmp/renderer-validation/long-term-linux-x64/renderer_validation_report.json",
            "requiredGameplayReport": ".tmp/renderer-gameplay/long-term-linux-x64-required/renderer_gameplay_benchmark_report.json",
            "visualComparisonReport": ".tmp/renderer-gameplay/long-term-linux-x64-visual/renderer_gameplay_benchmark_report.json",
            "presentationReport": ".tmp/renderer-gameplay/long-term-linux-x64-presentation/renderer_gameplay_benchmark_report.json",
            "platformNotesReport": ".tmp/renderer-validation/long-term-linux-x64-platform-notes/renderer_validation_report.json",
        }
    if platform_id == "macos-gl41":
        return {
            "safeReport": ".tmp/renderer-validation/long-term-macos-gl41/renderer_validation_report.json",
            "requiredGameplayReport": ".tmp/renderer-gameplay/long-term-macos-gl41-required/renderer_gameplay_benchmark_report.json",
            "visualComparisonReport": ".tmp/renderer-gameplay/long-term-macos-gl41-visual/renderer_gameplay_benchmark_report.json",
            "presentationReport": ".tmp/renderer-gameplay/long-term-macos-gl41-presentation/renderer_gameplay_benchmark_report.json",
            "platformNotesReport": ".tmp/renderer-validation/long-term-macos-gl41-platform-notes/renderer_validation_report.json",
        }
    return {}


def manifest_template() -> dict[str, Any]:
    platforms: dict[str, dict[str, Any]] = {}
    for requirement in PLATFORM_REQUIREMENTS:
        platforms[requirement.id] = {
            "reviewStatus": STATUS_NEEDS_REVIEW,
            "reviewedBy": "",
            "reviewedDate": "",
            "reports": default_report_paths(requirement.id),
            "metadata": {field: "" for field in REQUIRED_PLATFORM_METADATA},
            "notes": "",
        }
    return {
        "schema": "openq4.renderer.platformEvidence.v1",
        "metadata": {
            "generated": now_string(),
            "instructions": "Fill report paths, platform metadata, reviewer, and notes, then rerun renderer_platform_summary.py --manifest this-file.",
            "policy": "Do not infer Linux or macOS readiness from Windows x64 evidence.",
        },
        "platforms": platforms,
    }


def load_manifest(root: Path, manifest_value: str | None) -> dict[str, dict[str, Any]]:
    if not manifest_value:
        platforms = manifest_template()["platforms"]
        assert isinstance(platforms, dict)
        return {str(key): value for key, value in platforms.items() if isinstance(value, dict)}

    manifest_path = resolve_path(root, manifest_value)
    assert manifest_path is not None
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    platforms = payload.get("platforms", payload)
    entries: dict[str, dict[str, Any]] = {}
    if isinstance(platforms, dict):
        for platform_id, value in platforms.items():
            if isinstance(value, dict):
                entries[str(platform_id)] = value
    elif isinstance(platforms, list):
        for value in platforms:
            if isinstance(value, dict) and value.get("id"):
                entries[str(value["id"])] = value
    else:
        raise ValueError("manifest must contain platforms={...}, a platform list, or be a platform object map")
    return entries


def report_path_for(root: Path, platform_id: str, entry: dict[str, Any], requirement: ReportRequirement) -> str:
    defaults = default_report_paths(platform_id)
    reports = entry.get("reports", {})
    if not isinstance(reports, dict):
        reports = {}
    value = str(reports.get(requirement.key, "")).strip()
    if not value:
        value = defaults.get(requirement.key, "")
    path = resolve_path(root, value)
    return rel_path(root, path) if path is not None else ""


def assess_report(root: Path, requirement: ReportRequirement, path_value: str) -> dict[str, Any]:
    path = resolve_path(root, path_value)
    if path is None:
        return {
            "key": requirement.key,
            "title": requirement.title,
            "required": requirement.required,
            "path": "",
            "present": False,
            "jsonPresent": False,
            "status": STATUS_MISSING,
            "resultCount": 0,
            "passCount": 0,
            "failCount": 0,
            "warningTotal": 0,
            "metadata": {},
            "results": [],
            "notes": ["no artifact path supplied"],
        }

    if not path.exists():
        return {
            "key": requirement.key,
            "title": requirement.title,
            "required": requirement.required,
            "path": rel_path(root, path),
            "present": False,
            "jsonPresent": False,
            "status": STATUS_MISSING,
            "resultCount": 0,
            "passCount": 0,
            "failCount": 0,
            "warningTotal": 0,
            "metadata": {},
            "results": [],
            "notes": ["artifact path does not exist"],
        }

    json_path = find_json_companion(path)
    if json_path is None or not json_path.exists():
        return {
            "key": requirement.key,
            "title": requirement.title,
            "required": requirement.required,
            "path": rel_path(root, path),
            "present": True,
            "jsonPresent": False,
            "status": STATUS_NEEDS_REVIEW,
            "resultCount": 0,
            "passCount": 0,
            "failCount": 0,
            "warningTotal": 0,
            "metadata": {},
            "results": [],
            "notes": ["artifact exists but no JSON report was found for automated status checks"],
        }

    payload, error = read_json(json_path)
    if payload is None:
        return {
            "key": requirement.key,
            "title": requirement.title,
            "required": requirement.required,
            "path": rel_path(root, json_path),
            "present": True,
            "jsonPresent": True,
            "status": STATUS_FAIL,
            "resultCount": 0,
            "passCount": 0,
            "failCount": 1,
            "warningTotal": 0,
            "metadata": {},
            "results": [],
            "notes": [f"could not parse JSON report: {error}"],
        }

    results = payload.get("results", [])
    if not isinstance(results, list):
        results = []
    typed_results = [result for result in results if isinstance(result, dict)]
    pass_count = sum(1 for result in typed_results if result.get("status") == STATUS_PASS)
    non_pass = [
        f"{result.get('id', 'unknown')}:{result.get('status', 'unknown')}"
        for result in typed_results
        if result.get("status") != STATUS_PASS
    ]
    explicit_fail = [item for item in non_pass if item.endswith(f":{STATUS_FAIL}")]
    warning_total = sum(warning_count(result) for result in typed_results)

    overall = str(payload.get("overallStatus", "")).strip().lower()
    notes: list[str] = []
    if typed_results:
        if non_pass:
            status = STATUS_FAIL if explicit_fail else STATUS_NEEDS_REVIEW
            notes.append(f"{len(non_pass)} result(s) are not pass")
            if explicit_fail:
                notes.append(f"{len(explicit_fail)} result(s) are explicit fail")
        else:
            status = STATUS_PASS
            notes.append(f"{pass_count} result(s) passed")
    elif overall == STATUS_PASS:
        status = STATUS_PASS
        notes.append("summary artifact reports overallStatus=pass")
    elif overall == STATUS_FAIL:
        status = STATUS_FAIL
        notes.append("summary artifact reports overallStatus=fail")
    elif overall in (STATUS_BLOCKED, STATUS_MISSING, STATUS_NEEDS_REVIEW):
        status = STATUS_NEEDS_REVIEW
        notes.append(f"summary artifact reports overallStatus={overall}")
    else:
        status = STATUS_NEEDS_REVIEW
        notes.append("JSON artifact has no top-level results array")

    metadata = payload.get("metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}

    if status == STATUS_PASS and requirement.require_zero_warnings and warning_total:
        status = STATUS_FAIL
        notes.append(f"warning signature total is {warning_total}")
    elif status == STATUS_PASS and requirement.require_zero_warnings:
        notes.append("warning signature total is 0")

    if status == STATUS_PASS and requirement.expected_profile:
        profile = str(metadata.get("profile", "")).strip()
        if profile != requirement.expected_profile:
            status = STATUS_NEEDS_REVIEW
            notes.append(f"expected profile {requirement.expected_profile}, found {profile or '<unset>'}")
        else:
            notes.append(f"profile={profile}")

    if status == STATUS_PASS and requirement.require_references:
        require_refs = bool(metadata.get("requireReferences"))
        reference_dir = str(metadata.get("referenceDir", "")).strip()
        if not require_refs or not reference_dir:
            status = STATUS_NEEDS_REVIEW
            notes.append("visual comparison did not require approved references")
        else:
            notes.append(f"approved references required from {reference_dir}")

    return {
        "key": requirement.key,
        "title": requirement.title,
        "required": requirement.required,
        "path": rel_path(root, json_path),
        "present": True,
        "jsonPresent": True,
        "status": status,
        "resultCount": len(typed_results),
        "passCount": pass_count,
        "failCount": len(non_pass),
        "warningTotal": warning_total,
        "metadata": metadata,
        "resultIds": [str(result.get("id", "")) for result in typed_results],
        "results": typed_results,
        "notes": notes,
    }


def first_summary(report: dict[str, Any]) -> dict[str, Any]:
    for result in report.get("results", []):
        if isinstance(result, dict):
            summary = result.get("summary", {})
            if isinstance(summary, dict) and summary:
                return summary
    return {}


def first_captured_summary(report: dict[str, Any]) -> dict[str, Any]:
    results = report.get("results", [])
    if results:
        for result in results:
            if isinstance(result, dict):
                payload = result.get("summary", {})
                if isinstance(payload, dict) and payload:
                    return payload
    return {}


def value_from_summary(summaries: list[dict[str, Any]], *keys: str) -> str:
    for summary in summaries:
        for key in keys:
            value = summary.get(key)
            if value is not None and str(value).strip():
                return str(value).strip()
    return ""


def requested_tier_from_results(reports: dict[str, dict[str, Any]]) -> str:
    for key in ("platformNotesReport", "safeReport"):
        report = reports.get(key, {})
        for result_id in report.get("resultIds", []):
            text = str(result_id)
            if text.startswith("tier-"):
                return text.removeprefix("tier-")
    return ""


def extract_auto_metadata(reports: dict[str, dict[str, Any]]) -> dict[str, str]:
    safe = reports.get("safeReport", {})
    notes = reports.get("platformNotesReport", {})
    primary = notes if notes.get("status") != STATUS_MISSING else safe
    summaries = [first_captured_summary(primary), first_summary(safe), first_summary(notes)]

    safe_metadata = safe.get("metadata", {})
    if not isinstance(safe_metadata, dict):
        safe_metadata = {}
    primary_metadata = primary.get("metadata", {})
    if not isinstance(primary_metadata, dict):
        primary_metadata = {}

    host = str(primary_metadata.get("host") or safe_metadata.get("host") or "").strip()
    executable = str(primary_metadata.get("executable") or safe_metadata.get("executable") or "").strip()
    selected_tier = value_from_summary(summaries, "selectedTier")
    context = value_from_summary(summaries, "context", "contextProfile")

    return {
        "executablePath": executable,
        "os": host,
        "gpu": "",
        "driverVersion": "",
        "glVendor": "",
        "glRenderer": "",
        "glVersion": context,
        "selectedTier": selected_tier,
        "requestedTier": requested_tier_from_results(reports) or value_from_summary(summaries, "contextRequest"),
        "contextProfile": value_from_summary(summaries, "contextProfile"),
        "debugContextAvailable": value_from_summary(summaries, "actualDebug", "requestedDebug"),
        "timerQueryAvailable": value_from_summary(summaries, "timerQuery", "gpuTimers", "gpuTimerStatus"),
        "uploadManagerMode": value_from_summary(summaries, "uploadManager", "uploadFrameStream"),
        "persistentMap": value_from_summary(summaries, "uploadPersistent"),
        "mapRangeSubdataFallback": value_from_summary(summaries, "uploadMapRangeFallback", "capMapRange"),
        "dsaAvailable": value_from_summary(summaries, "executorDSA", "capDSA"),
        "multibindAvailable": value_from_summary(summaries, "executorMultiBind", "capMultiBind"),
        "gpuDrivenAvailable": value_from_summary(summaries, "executorGpuDriven", "executorAvailable"),
    }


def merged_platform_metadata(entry: dict[str, Any], reports: dict[str, dict[str, Any]]) -> dict[str, str]:
    manual = entry.get("metadata", {})
    if not isinstance(manual, dict):
        manual = {}
    auto = extract_auto_metadata(reports)
    merged: dict[str, str] = {}
    for field in REQUIRED_PLATFORM_METADATA:
        value = str(manual.get(field, "")).strip()
        if not value:
            value = str(auto.get(field, "")).strip()
        merged[field] = value
    return merged


def report_hosts(reports: dict[str, dict[str, Any]]) -> list[str]:
    hosts: list[str] = []
    seen: set[str] = set()
    for report in reports.values():
        if not report.get("jsonPresent"):
            continue
        metadata = report.get("metadata", {})
        if isinstance(metadata, dict):
            host = str(metadata.get("host", "")).strip()
            if host and host not in seen:
                seen.add(host)
                hosts.append(host)
    return hosts


def host_matches(requirement: PlatformRequirement, hosts: list[str]) -> tuple[bool, str]:
    if not hosts:
        return False, "no host metadata found in supplied JSON reports"
    lowered = " ".join(hosts).lower()
    if requirement.host_tokens and not any(token in lowered for token in requirement.host_tokens):
        return False, f"host metadata does not contain one of: {', '.join(requirement.host_tokens)}"
    if requirement.arch_tokens and not any(token in lowered for token in requirement.arch_tokens):
        return False, f"host metadata does not contain one of: {', '.join(requirement.arch_tokens)}"
    return True, "; ".join(hosts)


def tier_matches(requirement: PlatformRequirement, metadata: dict[str, str]) -> tuple[bool, str]:
    if not requirement.tier_tokens:
        return True, ""
    tier_text = " ".join(
        metadata.get(field, "")
        for field in ("selectedTier", "requestedTier", "contextProfile", "glVersion")
    ).lower()
    if not tier_text.strip():
        return False, "no selected/requested tier metadata recorded"
    if any(token in tier_text for token in requirement.tier_tokens):
        return True, tier_text
    return False, f"tier metadata does not contain one of: {', '.join(requirement.tier_tokens)}"


def assess_platform(root: Path, requirement: PlatformRequirement, entry: dict[str, Any]) -> dict[str, Any]:
    reports: dict[str, dict[str, Any]] = {}
    for report_requirement in REPORT_REQUIREMENTS:
        path = report_path_for(root, requirement.id, entry, report_requirement)
        reports[report_requirement.key] = assess_report(root, report_requirement, path)

    metadata = merged_platform_metadata(entry, reports)
    missing_metadata = [field for field in REQUIRED_PLATFORM_METADATA if not metadata.get(field)]
    review_status = normalize_status(entry.get("reviewStatus") or entry.get("status")) or STATUS_NEEDS_REVIEW
    reviewed_by = str(entry.get("reviewedBy", "")).strip()
    reviewed_date = str(entry.get("reviewedDate", "")).strip()

    notes: list[str] = [requirement.notes]
    required_reports = [report for report in reports.values() if report["required"]]
    optional_reports = [report for report in reports.values() if not report["required"]]

    status = STATUS_PASS
    if any(report["status"] == STATUS_FAIL for report in reports.values()):
        status = STATUS_FAIL
        notes.append("one or more reports failed")
    elif any(report["required"] and report["status"] == STATUS_MISSING for report in reports.values()):
        status = STATUS_MISSING
        notes.append("one or more required reports are missing")
    elif any(report["status"] in (STATUS_NEEDS_REVIEW, STATUS_BLOCKED) for report in required_reports):
        status = STATUS_NEEDS_REVIEW
        notes.append("one or more required reports need review")
    elif any(report["status"] == STATUS_FAIL for report in optional_reports):
        status = STATUS_FAIL
        notes.append("optional platform notes failed")
    elif any(report["status"] in (STATUS_NEEDS_REVIEW, STATUS_BLOCKED) for report in optional_reports if report["present"]):
        status = STATUS_NEEDS_REVIEW
        notes.append("optional platform notes need review")

    hosts = report_hosts(reports)
    if status not in (STATUS_MISSING, STATUS_FAIL):
        host_ok, host_note = host_matches(requirement, hosts)
        if host_ok:
            notes.append(f"host metadata: {host_note}")
        else:
            status = STATUS_FAIL
            notes.append(host_note)

    if status not in (STATUS_MISSING, STATUS_FAIL):
        tier_ok, tier_note = tier_matches(requirement, metadata)
        if tier_ok and tier_note:
            notes.append(f"tier metadata: {tier_note}")
        elif not tier_ok:
            status = STATUS_FAIL
            notes.append(tier_note)

    if status == STATUS_PASS and missing_metadata:
        status = STATUS_NEEDS_REVIEW
        notes.append("missing platform metadata fields: " + ", ".join(missing_metadata))

    if status == STATUS_PASS and review_status != STATUS_PASS:
        status = STATUS_NEEDS_REVIEW
        notes.append(f"reviewStatus={review_status}")
    if status == STATUS_PASS and (not reviewed_by or not reviewed_date):
        status = STATUS_NEEDS_REVIEW
        notes.append("reviewedBy and reviewedDate are required")

    if reviewed_by:
        notes.append(f"reviewedBy={reviewed_by}")
    if reviewed_date:
        notes.append(f"reviewedDate={reviewed_date}")
    if entry.get("notes"):
        notes.append(str(entry["notes"]))

    compact_reports = {
        key: {
            "title": value["title"],
            "required": value["required"],
            "path": value["path"],
            "present": value["present"],
            "jsonPresent": value["jsonPresent"],
            "status": value["status"],
            "resultCount": value["resultCount"],
            "passCount": value["passCount"],
            "failCount": value["failCount"],
            "warningTotal": value["warningTotal"],
            "resultIds": value.get("resultIds", []),
            "notes": value["notes"],
        }
        for key, value in reports.items()
    }

    return {
        "id": requirement.id,
        "title": requirement.title,
        "status": status,
        "reviewStatus": review_status,
        "reviewedBy": reviewed_by,
        "reviewedDate": reviewed_date,
        "metadata": metadata,
        "missingMetadataFields": missing_metadata,
        "hostMetadata": hosts,
        "reports": compact_reports,
        "notes": notes,
    }


def overall_status(results: list[dict[str, Any]]) -> str:
    if any(result["status"] == STATUS_FAIL for result in results):
        return STATUS_FAIL
    if all(result["status"] == STATUS_PASS for result in results):
        return STATUS_PASS
    return STATUS_BLOCKED


def status_counts(results: list[dict[str, Any]]) -> dict[str, int]:
    counts = {STATUS_PASS: 0, STATUS_NEEDS_REVIEW: 0, STATUS_MISSING: 0, STATUS_BLOCKED: 0, STATUS_FAIL: 0}
    for result in results:
        counts[result["status"]] = counts.get(result["status"], 0) + 1
    return counts


def build_payload(root: Path, output_dir: Path, manifest_path: str | None) -> dict[str, Any]:
    entries = load_manifest(root, manifest_path)
    results = [
        assess_platform(root, requirement, entries.get(requirement.id, {}))
        for requirement in PLATFORM_REQUIREMENTS
    ]
    return {
        "metadata": {
            "generated": now_string(),
            "host": f"{platform.system()} {platform.release()} {platform.machine()}",
            "outputDir": rel_path(root, output_dir),
            "manifest": rel_path(root, resolve_path(root, manifest_path)) if manifest_path else "",
            "policy": "Do not infer Linux or macOS readiness from Windows x64 evidence.",
        },
        "overallStatus": overall_status(results),
        "statusCounts": status_counts(results),
        "requiredMetadataFields": list(REQUIRED_PLATFORM_METADATA),
        "metadataDescriptions": METADATA_DESCRIPTIONS,
        "reportRequirements": [
            {
                "key": requirement.key,
                "title": requirement.title,
                "required": requirement.required,
                "expectedProfile": requirement.expected_profile,
                "requireReferences": requirement.require_references,
                "requireZeroWarnings": requirement.require_zero_warnings,
            }
            for requirement in REPORT_REQUIREMENTS
        ],
        "platformRequirements": [
            {
                "id": requirement.id,
                "title": requirement.title,
                "hostTokens": list(requirement.host_tokens),
                "archTokens": list(requirement.arch_tokens),
                "tierTokens": list(requirement.tier_tokens),
                "notes": requirement.notes,
            }
            for requirement in PLATFORM_REQUIREMENTS
        ],
        "results": results,
    }


def write_summary(root: Path, output_dir: Path, payload: dict[str, Any], write_template: bool) -> tuple[Path, Path, Path | None]:
    output_dir.mkdir(parents=True, exist_ok=True)
    report_json = output_dir / "platform_summary.json"
    report_md = output_dir / "platform_summary.md"
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    counts = payload["statusCounts"]
    lines = [
        "# Renderer Cross-Platform Evidence Summary",
        "",
        f"- Generated: {payload['metadata']['generated']}",
        f"- Host: {payload['metadata']['host']}",
        f"- Overall status: **{payload['overallStatus']}**",
        f"- Platform counts: pass={counts.get(STATUS_PASS, 0)}, needs-review={counts.get(STATUS_NEEDS_REVIEW, 0)}, missing={counts.get(STATUS_MISSING, 0)}, blocked={counts.get(STATUS_BLOCKED, 0)}, fail={counts.get(STATUS_FAIL, 0)}",
        f"- Policy: {payload['metadata']['policy']}",
        "",
        "Every completed platform row needs safe validation, required SP/MP gameplay, reference-backed visual comparison, presentation pacing, complete hardware/GL metadata, and an explicit review.",
        "",
        "## Platform Summary",
        "",
        "| Status | Platform | Required Reports | Missing Metadata | Review | Evidence | Notes |",
        "|---|---|---|---|---|---|---|",
    ]

    for result in payload["results"]:
        report_summary = []
        evidence = []
        for report in result["reports"].values():
            if report["required"]:
                report_summary.append(f"{report['title']}={report['status']}")
                evidence.append(f"{report['title']}:{report['path']}")
        review = result["reviewStatus"]
        if result["reviewedBy"]:
            review += f" by {result['reviewedBy']}"
        if result["reviewedDate"]:
            review += f" on {result['reviewedDate']}"
        missing = ", ".join(result["missingMetadataFields"]) if result["missingMetadataFields"] else "none"
        lines.append(
            f"| {result['status']} | `{result['id']}` | {markdown_cell('; '.join(report_summary))} | "
            f"{markdown_cell(missing)} | {markdown_cell(review)} | `{markdown_cell('; '.join(evidence))}` | "
            f"{markdown_cell('; '.join(result['notes']))} |"
        )

    lines += [
        "",
        "## Recorded Metadata",
        "",
        "| Field | Meaning | " + " | ".join(f"`{result['id']}`" for result in payload["results"]) + " |",
        "|---|---|" + "---|" * len(payload["results"]),
    ]
    for field in payload["requiredMetadataFields"]:
        values = [markdown_cell(result["metadata"].get(field, "")) for result in payload["results"]]
        lines.append(f"| `{field}` | {markdown_cell(payload['metadataDescriptions'].get(field, ''))} | " + " | ".join(values) + " |")

    lines += [
        "",
        "## Report Requirements",
        "",
        "| Report | Required | Expected Profile | Reference Gate | Warning Gate |",
        "|---|---|---|---|---|",
    ]
    for requirement in payload["reportRequirements"]:
        lines.append(
            f"| `{requirement['key']}` {markdown_cell(requirement['title'])} | {requirement['required']} | "
            f"`{markdown_cell(requirement['expectedProfile'])}` | {requirement['requireReferences']} | {requirement['requireZeroWarnings']} |"
        )

    lines += [
        "",
        "## Collection Commands",
        "",
        "Run the same evidence profile on each target platform and update `platform_evidence_manifest_template.json` with the report paths and reviewed hardware metadata.",
        "",
        "```powershell",
        "python tools\\tests\\renderer_validation_matrix.py --output-dir .tmp\\renderer-validation\\<platform-safe-run>",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile required --output-dir .tmp\\renderer-gameplay\\<platform-required-run>",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile visual-comparison --reference-dir .tmp\\renderer-references\\<platform> --require-references --output-dir .tmp\\renderer-gameplay\\<platform-visual-run>",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile presentation-comparison --pacing-only --output-dir .tmp\\renderer-gameplay\\<platform-presentation-run>",
        "python tools\\tests\\renderer_platform_summary.py --output-dir .tmp\\renderer-validation\\<platform-summary-run> --manifest .tmp\\renderer-validation\\<platform-summary-run>\\platform_evidence_manifest_template.json",
        "```",
        "",
        "## Remaining Work",
        "",
    ]
    pending = [result for result in payload["results"] if result["status"] != STATUS_PASS]
    if pending:
        for result in pending:
            lines.append(f"- {result['title']}: {result['status']}.")
    else:
        lines.append("- No platform evidence rows are pending.")

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")

    template_path = None
    if write_template:
        template_path = output_dir / "platform_evidence_manifest_template.json"
        template_path.write_text(json.dumps(manifest_template(), indent=2), encoding="utf-8")

    return report_json, report_md, template_path


def print_matrix() -> None:
    for requirement in PLATFORM_REQUIREMENTS:
        print(f"{requirement.id}: {requirement.title}")
        print(f"  host tokens: {', '.join(requirement.host_tokens) or '<none>'}")
        print(f"  arch tokens: {', '.join(requirement.arch_tokens) or '<none>'}")
        if requirement.tier_tokens:
            print(f"  tier tokens: {', '.join(requirement.tier_tokens)}")
        paths = default_report_paths(requirement.id)
        for report in REPORT_REQUIREMENTS:
            print(f"  {report.key}: {paths.get(report.key, '')}")
        print(f"  notes: {requirement.notes}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default=".tmp/renderer-validation/platform-summary", help="Directory for platform_summary.md/json.")
    parser.add_argument("--manifest", help="Optional filled platform evidence manifest JSON.")
    parser.add_argument("--write-template", action="store_true", help="Write platform_evidence_manifest_template.json next to the summary.")
    parser.add_argument("--print-matrix", action="store_true", help="Print platform evidence requirements and exit.")
    parser.add_argument("--require-complete", action="store_true", help="Exit nonzero unless every platform row is pass.")
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
