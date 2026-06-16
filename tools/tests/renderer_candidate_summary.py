#!/usr/bin/env python3
"""Summarize modern renderer promotion-candidate readiness.

The renderer promotion bundle answers whether a default change is allowed. This
tool answers the earlier question: which modern path, if any, is currently a
credible candidate for that bundle. It records the candidate matrix, acceptance
thresholds, and the evidence that keeps the current sampled candidate blocked or
disqualified.
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

FINAL_DECISIONS = ("needs-review", "blocked", "candidate-selected", "no-current-candidate")

DEFAULT_PERFORMANCE_SUMMARY = ".tmp/renderer-validation/long-term-performance-summary-r1/performance_summary.json"
DEFAULT_PROMOTION_SUMMARY = ".tmp/renderer-validation/long-term-promotion-evidence-r5-platform-summary/promotion_evidence_summary.json"


@dataclass(frozen=True)
class Candidate:
    id: str
    title: str
    path: str
    performance_variant: str
    description: str
    required_evidence: tuple[str, ...]


CANDIDATES = (
    Candidate(
        id="executor-prepare",
        title="Modern executor prepare-only",
        path="r_rendererModernExecutor 1, submit/visible paths off",
        performance_variant="executor",
        description="Existing sampled candidate: prepare modern draw plans while final visible output remains on the compatibility bridge.",
        required_evidence=(
            "performance",
            "safe-validation",
            "required-gameplay",
            "visual-references",
            "renderdoc-api-captures",
            "rollback",
            "platform-summary",
        ),
    ),
    Candidate(
        id="modern-visible-hybrid",
        title="Modern-visible hybrid",
        path="r_rendererModernVisible 1 with compatibility bridge rollback",
        performance_variant="",
        description="Hybrid visible-frame path; needs its own performance variant and reference-backed visual evidence before it can be selected.",
        required_evidence=(
            "performance",
            "visual-references",
            "renderdoc-api-captures",
            "rollback",
            "platform-summary",
        ),
    ),
    Candidate(
        id="deferred-lite",
        title="Deferred-lite",
        path="modern visible depth/G-buffer/deferred resolve subset",
        performance_variant="",
        description="Deferred-lighting subset; not a measured performance-comparison variant yet.",
        required_evidence=(
            "performance",
            "visual-references",
            "renderdoc-api-captures",
            "rollback",
            "platform-summary",
        ),
    ),
    Candidate(
        id="forward-plus",
        title="Forward+",
        path="modern visible depth plus Forward+ light assignment",
        performance_variant="",
        description="Forward+ path; needs dedicated performance, capture, and visual evidence before selection.",
        required_evidence=(
            "performance",
            "visual-references",
            "renderdoc-api-captures",
            "rollback",
            "platform-summary",
        ),
    ),
    Candidate(
        id="gpu-driven-submit",
        title="GPU-driven submit",
        path="modern executor with GPU-driven draw submission",
        performance_variant="",
        description="Most aggressive low-overhead path; requires API traces, fallback proof, and cross-platform capability review.",
        required_evidence=(
            "performance",
            "visual-references",
            "renderdoc-api-captures",
            "rollback",
            "platform-summary",
        ),
    ),
)

PROMOTION_GATE_IDS = {
    "safe-validation": "safe-validation",
    "required-sp-gameplay": "required-sp-gameplay",
    "required-mp-gameplay": "required-mp-gameplay",
    "visual-references": "visual-references",
    "renderdoc-api-captures": "renderdoc-api-captures",
    "rollback": "rollback-coverage",
    "default-safety": "debug-off-defaults",
    "platform-summary": "cross-platform-summary",
}

FALLBACK_COUNTERS = (
    "fenceWaits",
    "fenceTimeouts",
    "fenceFallbacks",
    "frameStalls",
    "ringOverflowKB",
    "stateTextureMultiBindFallbackBatches",
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


def normalize_status(value: Any) -> str:
    status = str(value or "").strip().lower()
    if status in VALID_STATUSES:
        return status
    return ""


def float_value(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def int_value(value: Any) -> int | None:
    if value is None or value == "":
        return None
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return None


def default_thresholds() -> dict[str, Any]:
    return {
        "baselineVariant": "arb2",
        "frameAverageTolerancePercent": 10.0,
        "frameAverageToleranceMs": 2.0,
        "p50TolerancePercent": 10.0,
        "p50ToleranceMs": 1.0,
        "p95TolerancePercent": 10.0,
        "p95ToleranceMs": 2.0,
        "p99TolerancePercent": 10.0,
        "p99ToleranceMs": 3.0,
        "maxTolerancePercent": 10.0,
        "maxToleranceMs": 5.0,
        "minHzRatio": 0.90,
        "maxWarnings": 0,
        "maxFallbackCounter": 0,
        "imageRmsThreshold": 2.0,
        "imageMaxThreshold": 24,
        "requireSafeValidationPass": True,
        "requireRequiredGameplayPass": True,
        "requireVisualReferencesPass": True,
        "requireCaptureSummaryPass": True,
        "requireRollbackPass": True,
        "requireDefaultSafetyPass": True,
        "requirePlatformSummaryPass": True,
    }


def default_manifest(output_dir: Path) -> dict[str, Any]:
    return {
        "schema": "openq4.renderer.candidateReview.v1",
        "review": {
            "id": output_dir.name,
            "approvalStatus": STATUS_NEEDS_REVIEW,
            "decision": "needs-review",
            "selectedCandidate": "executor-prepare",
            "reviewedBy": "",
            "reviewedDate": "",
            "notes": "The currently measured candidate is executor-prepare; it must clear every gate before default-promotion work can continue.",
        },
        "sourceReports": {
            "performanceSummary": DEFAULT_PERFORMANCE_SUMMARY,
            "promotionSummary": DEFAULT_PROMOTION_SUMMARY,
        },
        "thresholds": default_thresholds(),
        "candidateReviews": {
            candidate.id: {
                "decision": STATUS_NEEDS_REVIEW,
                "notes": "",
            }
            for candidate in CANDIDATES
        },
    }


def load_manifest(root: Path, output_dir: Path, manifest_value: str | None) -> tuple[dict[str, Any], str]:
    if not manifest_value:
        return default_manifest(output_dir), ""
    manifest_path = resolve_path(root, manifest_value)
    assert manifest_path is not None
    if not manifest_path.exists():
        return default_manifest(output_dir), "manifest path does not exist"
    payload, error = read_json(manifest_path)
    if payload is None:
        return default_manifest(output_dir), error
    return payload, ""


def merge_thresholds(manifest: dict[str, Any]) -> dict[str, Any]:
    thresholds = default_thresholds()
    supplied = manifest.get("thresholds", {})
    if isinstance(supplied, dict):
        thresholds.update(supplied)
    return thresholds


def source_report_path(root: Path, manifest: dict[str, Any], key: str, fallback: str) -> Path | None:
    reports = manifest.get("sourceReports", {})
    if not isinstance(reports, dict):
        reports = {}
    value = str(reports.get(key, "") or fallback)
    return resolve_path(root, value)


def load_report(root: Path, manifest: dict[str, Any], key: str, fallback: str) -> dict[str, Any]:
    path = source_report_path(root, manifest, key, fallback)
    if path is None:
        return {
            "path": "",
            "present": False,
            "status": STATUS_MISSING,
            "payload": None,
            "notes": ["no report path supplied"],
        }
    if not path.exists():
        return {
            "path": rel_path(root, path),
            "present": False,
            "status": STATUS_MISSING,
            "payload": None,
            "notes": ["report path does not exist"],
        }
    payload, error = read_json(path)
    if payload is None:
        return {
            "path": rel_path(root, path),
            "present": True,
            "status": STATUS_FAIL,
            "payload": None,
            "notes": [f"could not parse JSON report: {error}"],
        }
    overall = normalize_status(payload.get("overallStatus")) or STATUS_NEEDS_REVIEW
    return {
        "path": rel_path(root, path),
        "present": True,
        "status": overall,
        "payload": payload,
        "notes": [f"overallStatus={overall}"],
    }


def combine_status(statuses: list[str]) -> str:
    if any(status == STATUS_FAIL for status in statuses):
        return STATUS_FAIL
    if any(status == STATUS_MISSING for status in statuses):
        return STATUS_MISSING
    if any(status in (STATUS_BLOCKED, STATUS_NEEDS_REVIEW) for status in statuses):
        return STATUS_NEEDS_REVIEW
    if statuses and all(status == STATUS_PASS for status in statuses):
        return STATUS_PASS
    return STATUS_BLOCKED


def lower_allowed(baseline: float, percent: float, absolute: float) -> float:
    return baseline * (1.0 + percent / 100.0) + absolute


def compare_lower(candidate: float | None, baseline: float | None, percent: float, absolute: float) -> dict[str, Any]:
    if candidate is None or baseline is None:
        return {"status": STATUS_MISSING, "candidate": candidate, "baseline": baseline, "allowed": None}
    allowed = lower_allowed(baseline, percent, absolute)
    return {
        "status": STATUS_PASS if candidate <= allowed else STATUS_FAIL,
        "candidate": round(candidate, 4),
        "baseline": round(baseline, 4),
        "allowed": round(allowed, 4),
        "delta": round(candidate - baseline, 4),
        "ratio": round(candidate / baseline, 4) if baseline else None,
    }


def compare_hz(candidate: float | None, baseline: float | None, min_ratio: float) -> dict[str, Any]:
    if candidate is None or baseline is None:
        return {"status": STATUS_MISSING, "candidate": candidate, "baseline": baseline, "allowed": None}
    allowed = baseline * min_ratio
    return {
        "status": STATUS_PASS if candidate >= allowed else STATUS_FAIL,
        "candidate": round(candidate, 4),
        "baseline": round(baseline, 4),
        "allowed": round(allowed, 4),
        "delta": round(candidate - baseline, 4),
        "ratio": round(candidate / baseline, 4) if baseline else None,
    }


def variant_summary(result: dict[str, Any], variant: str) -> dict[str, Any] | None:
    variants = result.get("variants", {})
    if not isinstance(variants, dict):
        return None
    value = variants.get(variant)
    if isinstance(value, dict):
        return value
    return None


def variant_metric(variant: dict[str, Any] | None, key: str) -> float | None:
    if not variant:
        return None
    summary = variant.get("summary", {})
    if not isinstance(summary, dict):
        return None
    return float_value(summary.get(key))


def variant_counter(variant: dict[str, Any] | None, key: str) -> int | None:
    if not variant:
        return None
    summary = variant.get("summary", {})
    if not isinstance(summary, dict):
        return None
    return int_value(summary.get(key))


def evaluate_scene(
    result: dict[str, Any],
    candidate_variant: str,
    baseline_variant: str,
    thresholds: dict[str, Any],
) -> dict[str, Any]:
    candidate = variant_summary(result, candidate_variant)
    baseline = variant_summary(result, baseline_variant)
    notes: list[str] = []
    metric_checks: dict[str, dict[str, Any]] = {}

    avg_candidate_hz = variant_metric(candidate, "pacingHz")
    avg_baseline_hz = variant_metric(baseline, "pacingHz")
    avg_candidate_ms = 1000.0 / avg_candidate_hz if avg_candidate_hz else None
    avg_baseline_ms = 1000.0 / avg_baseline_hz if avg_baseline_hz else None
    metric_checks["frameAverageMs"] = compare_lower(
        avg_candidate_ms,
        avg_baseline_ms,
        float(thresholds["frameAverageTolerancePercent"]),
        float(thresholds["frameAverageToleranceMs"]),
    )
    metric_checks["pacingP50Ms"] = compare_lower(
        variant_metric(candidate, "pacingP50Ms"),
        variant_metric(baseline, "pacingP50Ms"),
        float(thresholds["p50TolerancePercent"]),
        float(thresholds["p50ToleranceMs"]),
    )
    metric_checks["pacingP95Ms"] = compare_lower(
        variant_metric(candidate, "pacingP95Ms"),
        variant_metric(baseline, "pacingP95Ms"),
        float(thresholds["p95TolerancePercent"]),
        float(thresholds["p95ToleranceMs"]),
    )
    metric_checks["pacingP99Ms"] = compare_lower(
        variant_metric(candidate, "pacingP99Ms"),
        variant_metric(baseline, "pacingP99Ms"),
        float(thresholds["p99TolerancePercent"]),
        float(thresholds["p99ToleranceMs"]),
    )
    metric_checks["pacingMaxMs"] = compare_lower(
        variant_metric(candidate, "pacingMaxMs"),
        variant_metric(baseline, "pacingMaxMs"),
        float(thresholds["maxTolerancePercent"]),
        float(thresholds["maxToleranceMs"]),
    )
    metric_checks["pacingHz"] = compare_hz(
        avg_candidate_hz,
        avg_baseline_hz,
        float(thresholds["minHzRatio"]),
    )

    warnings = int_value(candidate.get("warnings") if candidate else None)
    warning_status = STATUS_PASS
    if warnings is None:
        warning_status = STATUS_MISSING
        notes.append("candidate warning count is missing")
    elif warnings > int(thresholds["maxWarnings"]):
        warning_status = STATUS_FAIL
        notes.append(f"warning count {warnings} exceeds {thresholds['maxWarnings']}")

    fallback_checks: dict[str, dict[str, Any]] = {}
    for counter in FALLBACK_COUNTERS:
        value = variant_counter(candidate, counter)
        if value is None:
            fallback_checks[counter] = {"status": STATUS_MISSING, "value": None, "allowed": thresholds["maxFallbackCounter"]}
        else:
            fallback_checks[counter] = {
                "status": STATUS_PASS if value <= int(thresholds["maxFallbackCounter"]) else STATUS_FAIL,
                "value": value,
                "allowed": thresholds["maxFallbackCounter"],
            }

    statuses = [check["status"] for check in metric_checks.values()]
    statuses += [check["status"] for check in fallback_checks.values()]
    statuses.append(warning_status)
    if candidate is None:
        statuses.append(STATUS_MISSING)
        notes.append(f"candidate variant '{candidate_variant}' is missing")
    if baseline is None:
        statuses.append(STATUS_MISSING)
        notes.append(f"baseline variant '{baseline_variant}' is missing")

    failed_metrics = [key for key, check in metric_checks.items() if check["status"] == STATUS_FAIL]
    if failed_metrics:
        notes.append("failed metrics: " + ", ".join(failed_metrics))
    failed_fallbacks = [key for key, check in fallback_checks.items() if check["status"] == STATUS_FAIL]
    if failed_fallbacks:
        notes.append("failed fallback counters: " + ", ".join(failed_fallbacks))

    return {
        "id": str(result.get("id", "unknown")),
        "scene": str(result.get("scene", result.get("id", ""))),
        "status": combine_status(statuses),
        "resultStatus": normalize_status(result.get("status")) or str(result.get("status", "")),
        "candidateVariant": candidate_variant,
        "baselineVariant": baseline_variant,
        "metrics": metric_checks,
        "warningCount": warnings,
        "warningStatus": warning_status,
        "fallbackChecks": fallback_checks,
        "notes": notes,
    }


def evaluate_performance(
    candidate: Candidate,
    performance_report: dict[str, Any],
    thresholds: dict[str, Any],
) -> dict[str, Any]:
    if not candidate.performance_variant:
        return {
            "status": STATUS_MISSING,
            "variant": "",
            "baselineVariant": thresholds["baselineVariant"],
            "sceneResults": [],
            "notes": ["candidate has no performance-comparison variant yet"],
        }
    payload = performance_report.get("payload")
    if not isinstance(payload, dict):
        return {
            "status": performance_report["status"],
            "variant": candidate.performance_variant,
            "baselineVariant": thresholds["baselineVariant"],
            "sceneResults": [],
            "notes": list(performance_report["notes"]),
        }
    results = payload.get("results", [])
    if not isinstance(results, list):
        results = []
    scene_results = [
        evaluate_scene(result, candidate.performance_variant, str(thresholds["baselineVariant"]), thresholds)
        for result in results
        if isinstance(result, dict)
    ]
    notes: list[str] = []
    if not scene_results:
        notes.append("performance summary has no scene results")
    failed = [result["id"] for result in scene_results if result["status"] == STATUS_FAIL]
    missing = [result["id"] for result in scene_results if result["status"] == STATUS_MISSING]
    needs_review = [result["id"] for result in scene_results if result["status"] == STATUS_NEEDS_REVIEW]
    if failed:
        notes.append("failed scenes: " + ", ".join(failed))
    if missing:
        notes.append("missing scenes: " + ", ".join(missing))
    if needs_review:
        notes.append("needs-review scenes: " + ", ".join(needs_review))

    statuses = [result["status"] for result in scene_results]
    if not statuses:
        statuses.append(STATUS_MISSING)
    return {
        "status": combine_status(statuses),
        "variant": candidate.performance_variant,
        "baselineVariant": thresholds["baselineVariant"],
        "sceneResults": scene_results,
        "notes": notes,
    }


def promotion_gate_status(promotion_report: dict[str, Any], short_id: str) -> dict[str, Any]:
    gate_id = PROMOTION_GATE_IDS[short_id]
    payload = promotion_report.get("payload")
    if not isinstance(payload, dict):
        return {
            "id": short_id,
            "gateId": gate_id,
            "status": promotion_report["status"],
            "evidence": promotion_report["path"],
            "notes": list(promotion_report["notes"]),
        }
    gates = payload.get("gates", [])
    if not isinstance(gates, list):
        gates = []
    for gate in gates:
        if isinstance(gate, dict) and gate.get("id") == gate_id:
            return {
                "id": short_id,
                "gateId": gate_id,
                "status": normalize_status(gate.get("status")) or STATUS_NEEDS_REVIEW,
                "evidence": str(gate.get("evidence", "")),
                "notes": [str(item) for item in gate.get("notes", []) if item],
            }
    return {
        "id": short_id,
        "gateId": gate_id,
        "status": STATUS_MISSING,
        "evidence": promotion_report["path"],
        "notes": [f"gate '{gate_id}' not found in promotion summary"],
    }


def required_global_gates(thresholds: dict[str, Any]) -> list[str]:
    gates: list[str] = []
    if thresholds.get("requireSafeValidationPass"):
        gates.append("safe-validation")
    if thresholds.get("requireRequiredGameplayPass"):
        gates.extend(["required-sp-gameplay", "required-mp-gameplay"])
    if thresholds.get("requireVisualReferencesPass"):
        gates.append("visual-references")
    if thresholds.get("requireCaptureSummaryPass"):
        gates.append("renderdoc-api-captures")
    if thresholds.get("requireRollbackPass"):
        gates.append("rollback")
    if thresholds.get("requireDefaultSafetyPass"):
        gates.append("default-safety")
    if thresholds.get("requirePlatformSummaryPass"):
        gates.append("platform-summary")
    return gates


def candidate_review(manifest: dict[str, Any], candidate_id: str) -> dict[str, Any]:
    reviews = manifest.get("candidateReviews", {})
    if not isinstance(reviews, dict):
        return {}
    review = reviews.get(candidate_id, {})
    return review if isinstance(review, dict) else {}


def evaluate_candidate(
    candidate: Candidate,
    selected_id: str,
    manifest: dict[str, Any],
    thresholds: dict[str, Any],
    performance_report: dict[str, Any],
    global_gates: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    performance = evaluate_performance(candidate, performance_report, thresholds)
    review = candidate_review(manifest, candidate.id)
    review_decision = normalize_status(review.get("decision")) or str(review.get("decision", STATUS_NEEDS_REVIEW))
    selected = candidate.id == selected_id
    notes = [candidate.description]
    if not selected:
        notes.append("not selected for current promotion-candidate review")

    required_gate_statuses = []
    for gate_name in required_global_gates(thresholds):
        required_gate_statuses.append(global_gates[gate_name]["status"])

    statuses = [performance["status"]] + required_gate_statuses
    if selected and review_decision != STATUS_PASS:
        statuses.append(STATUS_NEEDS_REVIEW)
        notes.append(f"candidate review decision is {review_decision}")
    if not selected:
        status = STATUS_BLOCKED
    else:
        status = combine_status(statuses)

    if selected and performance["status"] == STATUS_FAIL:
        notes.append("selected candidate is disqualified by performance evidence")
    elif selected and status == STATUS_PASS:
        notes.append("selected candidate satisfies current automated gates")

    if review.get("notes"):
        notes.append(str(review["notes"]))

    return {
        "id": candidate.id,
        "title": candidate.title,
        "path": candidate.path,
        "selected": selected,
        "status": status,
        "performanceStatus": performance["status"],
        "performanceVariant": performance["variant"],
        "baselineVariant": performance["baselineVariant"],
        "reviewDecision": review_decision,
        "requiredEvidence": list(candidate.required_evidence),
        "performance": performance,
        "notes": notes,
    }


def review_result(manifest: dict[str, Any], selected_id: str) -> dict[str, Any]:
    review = manifest.get("review", {})
    if not isinstance(review, dict):
        review = {}
    approval = normalize_status(review.get("approvalStatus")) or STATUS_NEEDS_REVIEW
    decision = str(review.get("decision", "needs-review")).strip()
    notes: list[str] = []
    if decision not in FINAL_DECISIONS:
        return {
            "id": "candidate-review-decision",
            "title": "Candidate review decision",
            "status": STATUS_FAIL,
            "evidence": "",
            "notes": [f"invalid decision '{decision}'"],
        }
    if approval != STATUS_PASS:
        notes.append(f"approvalStatus={approval}")
    if decision != "candidate-selected":
        notes.append(f"decision={decision}")
    if decision == "candidate-selected" and not selected_id:
        notes.append("decision is candidate-selected but selectedCandidate is empty")
        status = STATUS_FAIL
    elif decision == "candidate-selected" and approval == STATUS_PASS:
        status = STATUS_PASS
    elif decision == "no-current-candidate" and approval == STATUS_PASS:
        status = STATUS_BLOCKED
        notes.append("review explicitly selected no current promotion candidate")
    else:
        status = STATUS_NEEDS_REVIEW
    if review.get("reviewedBy"):
        notes.append(f"reviewedBy={review['reviewedBy']}")
    if review.get("reviewedDate"):
        notes.append(f"reviewedDate={review['reviewedDate']}")
    if review.get("notes"):
        notes.append(str(review["notes"]))
    return {
        "id": "candidate-review-decision",
        "title": "Candidate review decision",
        "status": status,
        "evidence": "",
        "notes": notes,
    }


def build_summary(root: Path, output_dir: Path, manifest_path: str | None) -> dict[str, Any]:
    manifest, manifest_error = load_manifest(root, output_dir, manifest_path)
    thresholds = merge_thresholds(manifest)
    review = manifest.get("review", {})
    if not isinstance(review, dict):
        review = {}
    selected_id = str(review.get("selectedCandidate", "")).strip()

    performance_report = load_report(root, manifest, "performanceSummary", DEFAULT_PERFORMANCE_SUMMARY)
    promotion_report = load_report(root, manifest, "promotionSummary", DEFAULT_PROMOTION_SUMMARY)
    global_gates = {
        gate_name: promotion_gate_status(promotion_report, gate_name)
        for gate_name in PROMOTION_GATE_IDS
    }

    candidates = [
        evaluate_candidate(candidate, selected_id, manifest, thresholds, performance_report, global_gates)
        for candidate in CANDIDATES
    ]
    selected = next((candidate for candidate in candidates if candidate["selected"]), None)

    results: list[dict[str, Any]] = []
    if selected is None:
        results.append(
            {
                "id": "selected-candidate",
                "title": "Selected candidate exists",
                "status": STATUS_MISSING,
                "evidence": "",
                "notes": [f"selectedCandidate '{selected_id}' is not in the candidate matrix"],
            }
        )
    else:
        results.append(
            {
                "id": "selected-candidate",
                "title": "Selected candidate readiness",
                "status": selected["status"],
                "evidence": performance_report["path"],
                "notes": selected["notes"],
            }
        )

    for gate_name in required_global_gates(thresholds):
        gate = global_gates[gate_name]
        results.append(
            {
                "id": gate_name,
                "title": gate_name.replace("-", " ").title(),
                "status": gate["status"],
                "evidence": gate["evidence"],
                "notes": gate["notes"],
            }
        )
    results.append(review_result(manifest, selected_id))
    if manifest_error:
        status = STATUS_MISSING if "does not exist" in manifest_error else STATUS_FAIL
        results.append(
            {
                "id": "manifest",
                "title": "Candidate manifest",
                "status": status,
                "evidence": rel_path(root, resolve_path(root, manifest_path)),
                "notes": [manifest_error],
            }
        )

    statuses = [result["status"] for result in results]
    overall = STATUS_FAIL if any(status == STATUS_FAIL for status in statuses) else combine_status(statuses)
    recommendation = "no-current-candidate"
    if selected is not None and selected["status"] == STATUS_PASS:
        recommendation = selected["id"]
    elif selected is not None and selected["status"] == STATUS_FAIL:
        recommendation = f"{selected['id']}-disqualified"

    return {
        "schema": "openq4.renderer.candidateSummary.v1",
        "overallStatus": overall,
        "generated": now_string(),
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "metadata": {
            "manifest": rel_path(root, resolve_path(root, manifest_path)) if manifest_path else "",
            "outputDir": rel_path(root, output_dir),
            "performanceSummary": performance_report["path"],
            "promotionSummary": promotion_report["path"],
            "selectedCandidate": selected_id,
            "recommendation": recommendation,
            "policy": "No renderer path may become a default-promotion candidate unless the selected candidate row and every required gate are pass.",
        },
        "thresholds": thresholds,
        "results": results,
        "candidateMatrix": candidates,
    }


def status_counts(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts = {STATUS_PASS: 0, STATUS_NEEDS_REVIEW: 0, STATUS_MISSING: 0, STATUS_BLOCKED: 0, STATUS_FAIL: 0}
    for row in rows:
        status = row.get("status", "")
        counts[status] = counts.get(status, 0) + 1
    return counts


def write_summary(root: Path, output_dir: Path, summary: dict[str, Any], write_template: bool) -> tuple[Path, Path, Path | None]:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "candidate_summary.json"
    md_path = output_dir / "candidate_summary.md"
    json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    counts = status_counts(summary["results"])
    lines = [
        "# Renderer Promotion Candidate Summary",
        "",
        f"- Status: `{summary['overallStatus']}`",
        f"- Generated: `{summary['generated']}`",
        f"- Selected candidate: `{summary['metadata']['selectedCandidate'] or 'none'}`",
        f"- Recommendation: `{summary['metadata']['recommendation']}`",
        f"- Performance summary: `{summary['metadata']['performanceSummary'] or 'not supplied'}`",
        f"- Promotion summary: `{summary['metadata']['promotionSummary'] or 'not supplied'}`",
        f"- Gate counts: pass={counts.get(STATUS_PASS, 0)}, needs-review={counts.get(STATUS_NEEDS_REVIEW, 0)}, missing={counts.get(STATUS_MISSING, 0)}, blocked={counts.get(STATUS_BLOCKED, 0)}, fail={counts.get(STATUS_FAIL, 0)}",
        "",
        summary["metadata"]["policy"],
        "",
        "## Candidate Matrix",
        "",
        "| Status | Candidate | Selected | Performance Variant | Path | Notes |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for candidate in summary["candidateMatrix"]:
        lines.append(
            f"| `{candidate['status']}` | `{candidate['id']}` | {candidate['selected']} | "
            f"`{candidate['performanceVariant'] or 'missing'}` | {markdown_cell(candidate['path'])} | "
            f"{markdown_cell('; '.join(candidate['notes']))} |"
        )

    lines.extend(
        [
            "",
            "## Gate Summary",
            "",
            "| Gate | Status | Evidence | Notes |",
            "| --- | --- | --- | --- |",
        ]
    )
    for result in summary["results"]:
        lines.append(
            f"| `{result['id']}` | `{result['status']}` | `{markdown_cell(result.get('evidence', ''))}` | "
            f"{markdown_cell('; '.join(result.get('notes', [])))} |"
        )

    lines.extend(
        [
            "",
            "## Acceptance Thresholds",
            "",
            "| Threshold | Value |",
            "| --- | --- |",
        ]
    )
    for key, value in summary["thresholds"].items():
        lines.append(f"| `{key}` | `{markdown_cell(value)}` |")

    lines.extend(
        [
            "",
            "## Selected Candidate Scene Evidence",
            "",
            "| Scene | Status | Avg ms | P50 | P95 | P99 | Max | Hz | Warnings | Fallbacks | Notes |",
            "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |",
        ]
    )
    selected = next((candidate for candidate in summary["candidateMatrix"] if candidate["selected"]), None)
    if selected:
        for scene in selected.get("performance", {}).get("sceneResults", []):
            metrics = scene.get("metrics", {})
            fallbacks = "; ".join(
                f"{key}={value.get('value')}/{value.get('allowed')} {value.get('status')}"
                for key, value in scene.get("fallbackChecks", {}).items()
            )

            def metric_cell(key: str) -> str:
                item = metrics.get(key, {})
                return f"{item.get('candidate', '')}/{item.get('baseline', '')} `{item.get('status', '')}`"

            lines.append(
                f"| `{scene['id']}` | `{scene['status']}` | {metric_cell('frameAverageMs')} | "
                f"{metric_cell('pacingP50Ms')} | {metric_cell('pacingP95Ms')} | "
                f"{metric_cell('pacingP99Ms')} | {metric_cell('pacingMaxMs')} | "
                f"{metric_cell('pacingHz')} | {scene.get('warningCount', '')} | "
                f"{markdown_cell(fallbacks)} | {markdown_cell('; '.join(scene.get('notes', [])))} |"
            )
    else:
        lines.append("| missing | `no selected candidate` |  |  |  |  |  |  |  |  |  |")

    lines.extend(
        [
            "",
            "## Collection Commands",
            "",
            "```powershell",
            "python tools\\tests\\renderer_gameplay_benchmark.py --profile performance-comparison --output-dir .tmp\\renderer-gameplay\\<performance-run>",
            "python tools\\tests\\renderer_performance_summary.py --source-report .tmp\\renderer-gameplay\\<performance-run>\\renderer_gameplay_benchmark_report.json --output-dir .tmp\\renderer-validation\\<performance-summary-run>",
            "python tools\\tests\\renderer_candidate_summary.py --output-dir .tmp\\renderer-validation\\<candidate-run> --manifest .tmp\\renderer-validation\\<candidate-run>\\candidate_manifest_template.json",
            "```",
            "",
        ]
    )
    md_path.write_text("\n".join(lines), encoding="utf-8")

    template_path = None
    if write_template:
        template_path = output_dir / "candidate_manifest_template.json"
        template_path.write_text(json.dumps(default_manifest(output_dir), indent=2), encoding="utf-8")
    return json_path, md_path, template_path


def print_matrix() -> None:
    for candidate in CANDIDATES:
        print(f"{candidate.id}: {candidate.title}")
        print(f"  path: {candidate.path}")
        print(f"  performance variant: {candidate.performance_variant or '<missing>'}")
        print(f"  required evidence: {', '.join(candidate.required_evidence)}")
        print(f"  notes: {candidate.description}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default=".tmp/renderer-validation/candidate-summary", help="Directory for candidate_summary.md/json.")
    parser.add_argument("--manifest", help="Optional filled candidate manifest JSON.")
    parser.add_argument("--write-template", action="store_true", help="Write candidate_manifest_template.json next to the summary.")
    parser.add_argument("--print-matrix", action="store_true", help="Print the candidate matrix and exit.")
    parser.add_argument("--require-complete", action="store_true", help="Exit nonzero unless the selected candidate and all gates are pass.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.print_matrix:
        print_matrix()
        return 0

    root = repo_root()
    output_dir = resolve_path(root, args.output_dir)
    assert output_dir is not None
    summary = build_summary(root, output_dir, args.manifest)
    json_path, md_path, template_path = write_summary(root, output_dir, summary, args.write_template)
    print(f"Wrote {rel_path(root, md_path)}")
    print(f"Wrote {rel_path(root, json_path)}")
    if template_path is not None:
        print(f"Wrote {rel_path(root, template_path)}")
    print(f"candidate summary: {summary['overallStatus']}")
    if args.require_complete and summary["overallStatus"] != STATUS_PASS:
        print(f"candidate summary is {summary['overallStatus']}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
