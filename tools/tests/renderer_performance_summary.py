#!/usr/bin/env python3
"""Summarize ARB2-or-better renderer performance evidence.

The gameplay benchmark records raw performance runs. This tool turns the
`performance-comparison` profile into a promotion-review artifact by comparing a
candidate variant against explicit ARB2 on the agreed scene set and by keeping
the result blocked until the review metadata says the comparison is approved.
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

REQUIRED_VARIANTS = ("default", "arb2", "executor")
DEFAULT_CANDIDATE = "executor"
DEFAULT_BASELINE = "arb2"
DEFAULT_SECONDARY_BASELINE = "default"

REQUIRED_REVIEW_METADATA = (
    "platform",
    "os",
    "gpu",
    "driverVersion",
    "glVendor",
    "glRenderer",
    "glVersion",
)

PRIMARY_METRICS = (
    "pacingP50Ms",
    "pacingP95Ms",
    "pacingP99Ms",
    "pacingMaxMs",
)


@dataclass(frozen=True)
class PerformanceCase:
    id: str
    title: str
    scene: str
    focus: str


PERFORMANCE_CASES = (
    PerformanceCase(
        id="sp-storage1",
        title="Storage1 primary acceptance scene",
        scene="game/storage1",
        focus="dense indoor storage baseline and early-game renderer performance",
    ),
    PerformanceCase(
        id="sp-airdefense1",
        title="Airdefense1 outdoor/BSE baseline",
        scene="game/airdefense1",
        focus="outdoor terrain, local effects, decals, and BSE smoke",
    ),
    PerformanceCase(
        id="sp-storage2",
        title="Storage2 dense-light baseline",
        scene="game/storage2",
        focus="indoor material, post-process, and dense local-light performance",
    ),
    PerformanceCase(
        id="sp-medlabs",
        title="Medlabs BSE-heavy baseline",
        scene="game/medlabs",
        focus="BSE-heavy scripted effects and stock gameplay performance",
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


def case_to_dict(case: PerformanceCase) -> dict[str, str]:
    return {
        "id": case.id,
        "title": case.title,
        "scene": case.scene,
        "focus": case.focus,
    }


def default_review_manifest(output_dir: Path) -> dict[str, Any]:
    return {
        "schema": "openq4.renderer.performanceReview.v1",
        "review": {
            "id": output_dir.name,
            "approvalStatus": STATUS_NEEDS_REVIEW,
            "approvedBy": "",
            "approvedDate": "",
            "decision": STATUS_NEEDS_REVIEW,
            "candidateVariant": DEFAULT_CANDIDATE,
            "baselineVariant": DEFAULT_BASELINE,
            "secondaryBaselineVariant": DEFAULT_SECONDARY_BASELINE,
            "sourceReport": ".tmp/renderer-gameplay/mid-term-performance-comparison-r1/renderer_gameplay_benchmark_report.json",
            "platform": "Windows x64",
            "os": "",
            "gpu": "",
            "driverVersion": "",
            "glVendor": "",
            "glRenderer": "",
            "glVersion": "",
            "notes": "",
        },
        "thresholds": {
            "p50TolerancePercent": 10.0,
            "p50ToleranceMs": 1.0,
            "p95TolerancePercent": 10.0,
            "p95ToleranceMs": 2.0,
            "p99TolerancePercent": 10.0,
            "p99ToleranceMs": 3.0,
            "maxTolerancePercent": 10.0,
            "maxToleranceMs": 5.0,
            "minHzRatio": 0.90,
            "requireZeroWarnings": True,
        },
        "caseReviews": [
            {
                "id": case.id,
                "decision": STATUS_NEEDS_REVIEW,
                "notes": "",
            }
            for case in PERFORMANCE_CASES
        ],
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def write_template(output_dir: Path) -> Path:
    path = output_dir / "performance_review_template.json"
    write_json(path, default_review_manifest(output_dir))
    return path


def print_matrix() -> None:
    for case in PERFORMANCE_CASES:
        print(f"{case.id}: {case.scene} - {case.focus}")
    print(f"variants: {', '.join(REQUIRED_VARIANTS)}; candidate={DEFAULT_CANDIDATE}; baseline={DEFAULT_BASELINE}")


def float_value(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def sum_warnings(role: dict[str, Any]) -> int:
    warnings = role.get("warnings", {})
    if not isinstance(warnings, dict):
        return 0
    total = 0
    for value in warnings.values():
        try:
            total += int(value)
        except (TypeError, ValueError):
            continue
    return total


def first_role(result: dict[str, Any]) -> dict[str, Any] | None:
    roles = result.get("roles", [])
    if not isinstance(roles, list) or not roles:
        return None
    for role in roles:
        if isinstance(role, dict) and role.get("role") in ("sp", "client"):
            return role
    return roles[0] if isinstance(roles[0], dict) else None


def result_variant(result: dict[str, Any]) -> str:
    variant = str(result.get("uploadVariant", "")).strip()
    if variant:
        return variant
    result_id = str(result.get("id", ""))
    if "perf-arb2" in result_id:
        return "arb2"
    if "perf-executor" in result_id:
        return "executor"
    return "default"


def find_result(payload: dict[str, Any] | None, case: PerformanceCase, variant: str) -> dict[str, Any] | None:
    if payload is None:
        return None
    results = payload.get("results", [])
    if not isinstance(results, list):
        return None
    for result in results:
        if not isinstance(result, dict):
            continue
        if str(result.get("id", "")).startswith(case.id) and result_variant(result) == variant:
            return result
    return None


def metric_threshold(metric: str, thresholds: dict[str, Any]) -> tuple[float, float]:
    if metric == "pacingP50Ms":
        return float_value(thresholds.get("p50TolerancePercent")) or 0.0, float_value(thresholds.get("p50ToleranceMs")) or 0.0
    if metric == "pacingP95Ms":
        return float_value(thresholds.get("p95TolerancePercent")) or 0.0, float_value(thresholds.get("p95ToleranceMs")) or 0.0
    if metric == "pacingP99Ms":
        return float_value(thresholds.get("p99TolerancePercent")) or 0.0, float_value(thresholds.get("p99ToleranceMs")) or 0.0
    return float_value(thresholds.get("maxTolerancePercent")) or 0.0, float_value(thresholds.get("maxToleranceMs")) or 0.0


def candidate_allowed_value(baseline: float, metric: str, thresholds: dict[str, Any]) -> float:
    percent, absolute = metric_threshold(metric, thresholds)
    return baseline * (1.0 + percent / 100.0) + absolute


def compare_lower_is_better(candidate: float | None, baseline: float | None, metric: str, thresholds: dict[str, Any]) -> dict[str, Any]:
    if candidate is None or baseline is None:
        return {
            "status": STATUS_MISSING,
            "candidate": candidate,
            "baseline": baseline,
            "allowed": "",
            "delta": "",
            "ratio": "",
        }
    allowed = candidate_allowed_value(baseline, metric, thresholds)
    ratio = candidate / baseline if baseline > 0.0 else 999999.0
    return {
        "status": STATUS_PASS if candidate <= allowed else STATUS_FAIL,
        "candidate": candidate,
        "baseline": baseline,
        "allowed": round(allowed, 4),
        "delta": round(candidate - baseline, 4),
        "ratio": round(ratio, 4),
    }


def compare_higher_is_better(candidate: float | None, baseline: float | None, thresholds: dict[str, Any]) -> dict[str, Any]:
    min_ratio = float_value(thresholds.get("minHzRatio")) or 0.0
    if candidate is None or baseline is None:
        return {
            "status": STATUS_MISSING,
            "candidate": candidate,
            "baseline": baseline,
            "allowed": "",
            "delta": "",
            "ratio": "",
        }
    ratio = candidate / baseline if baseline > 0.0 else 0.0
    return {
        "status": STATUS_PASS if ratio >= min_ratio else STATUS_FAIL,
        "candidate": candidate,
        "baseline": baseline,
        "allowed": round(baseline * min_ratio, 4),
        "delta": round(candidate - baseline, 4),
        "ratio": round(ratio, 4),
    }


def extract_variant_summary(result: dict[str, Any] | None) -> dict[str, Any]:
    if result is None:
        return {
            "present": False,
            "status": STATUS_MISSING,
            "summary": {},
            "warnings": 0,
            "missing": ["result"],
        }
    role = first_role(result)
    if role is None:
        return {
            "present": True,
            "status": STATUS_MISSING,
            "summary": {},
            "warnings": 0,
            "missing": ["role"],
        }
    return {
        "present": True,
        "status": role.get("status", result.get("status", "")),
        "summary": role.get("summary", {}) if isinstance(role.get("summary"), dict) else {},
        "warnings": sum_warnings(role),
        "missing": role.get("missing", []) if isinstance(role.get("missing"), list) else [],
        "resultId": result.get("id", ""),
        "log": role.get("log", ""),
    }


def case_review_decision(review_manifest: dict[str, Any] | None, case_id: str) -> str:
    if review_manifest is None:
        return STATUS_NEEDS_REVIEW
    items = review_manifest.get("caseReviews", [])
    if not isinstance(items, list):
        return STATUS_NEEDS_REVIEW
    for item in items:
        if isinstance(item, dict) and item.get("id") == case_id:
            return str(item.get("decision", STATUS_NEEDS_REVIEW)).strip().lower()
    return STATUS_NEEDS_REVIEW


def evaluate_case(
    case: PerformanceCase,
    payload: dict[str, Any] | None,
    thresholds: dict[str, Any],
    review_manifest: dict[str, Any] | None,
    candidate_variant: str,
    baseline_variant: str,
    secondary_variant: str,
) -> dict[str, Any]:
    variant_summaries = {
        variant: extract_variant_summary(find_result(payload, case, variant))
        for variant in REQUIRED_VARIANTS
    }
    candidate = variant_summaries.get(candidate_variant, {})
    baseline = variant_summaries.get(baseline_variant, {})
    secondary = variant_summaries.get(secondary_variant, {})
    notes: list[str] = []
    metric_rows: dict[str, dict[str, Any]] = {}

    for variant, summary in variant_summaries.items():
        if summary.get("status") != STATUS_PASS:
            notes.append(f"{variant} status={summary.get('status')}")
        if summary.get("missing"):
            notes.append(f"{variant} missing={','.join(str(item) for item in summary.get('missing', []))}")

    require_zero_warnings = bool(thresholds.get("requireZeroWarnings", True))
    if require_zero_warnings:
        for variant, summary in variant_summaries.items():
            if int(summary.get("warnings", 0)) != 0:
                notes.append(f"{variant} warnings={summary.get('warnings')}")

    candidate_summary = candidate.get("summary", {}) if isinstance(candidate.get("summary"), dict) else {}
    baseline_summary = baseline.get("summary", {}) if isinstance(baseline.get("summary"), dict) else {}
    secondary_summary = secondary.get("summary", {}) if isinstance(secondary.get("summary"), dict) else {}

    metric_statuses: list[str] = []
    for metric in PRIMARY_METRICS:
        row = compare_lower_is_better(
            float_value(candidate_summary.get(metric)),
            float_value(baseline_summary.get(metric)),
            metric,
            thresholds,
        )
        secondary_value = float_value(secondary_summary.get(metric))
        row["secondaryBaseline"] = secondary_value
        row["secondaryDelta"] = (
            round(float(row["candidate"]) - secondary_value, 4)
            if row["candidate"] not in (None, "") and secondary_value is not None
            else ""
        )
        metric_rows[metric] = row
        metric_statuses.append(str(row["status"]))

    hz_row = compare_higher_is_better(
        float_value(candidate_summary.get("pacingHz")),
        float_value(baseline_summary.get("pacingHz")),
        thresholds,
    )
    hz_row["secondaryBaseline"] = float_value(secondary_summary.get("pacingHz"))
    metric_rows["pacingHz"] = hz_row
    metric_statuses.append(str(hz_row["status"]))

    if any(status == STATUS_FAIL for status in metric_statuses):
        comparison_status = STATUS_FAIL
    elif any(status == STATUS_MISSING for status in metric_statuses):
        comparison_status = STATUS_MISSING
    else:
        comparison_status = STATUS_PASS

    review_decision = case_review_decision(review_manifest, case.id)
    status = comparison_status
    if status == STATUS_PASS and review_decision != STATUS_PASS:
        status = STATUS_NEEDS_REVIEW
        notes.append(f"case review decision is {review_decision}")

    return {
        "id": case.id,
        "title": case.title,
        "scene": case.scene,
        "focus": case.focus,
        "status": status,
        "comparisonStatus": comparison_status,
        "reviewDecision": review_decision,
        "candidateVariant": candidate_variant,
        "baselineVariant": baseline_variant,
        "secondaryBaselineVariant": secondary_variant,
        "variants": variant_summaries,
        "metrics": metric_rows,
        "notes": notes,
    }


def evaluate_checks(
    root: Path,
    review_manifest: dict[str, Any] | None,
    review_path: Path | None,
    payload: dict[str, Any] | None,
    report_path: Path | None,
) -> list[dict[str, Any]]:
    checks: list[dict[str, Any]] = []

    def add(check_id: str, title: str, status: str, detail: str) -> None:
        checks.append({"id": check_id, "title": title, "status": status, "detail": detail})

    if review_manifest is None:
        add("review-manifest", "Performance review manifest is present", STATUS_MISSING, rel_path(root, review_path))
    else:
        add("review-manifest", "Performance review manifest is present", STATUS_PASS, rel_path(root, review_path))
        review = review_manifest.get("review", {})
        if not isinstance(review, dict):
            review = {}
        approval_status = str(review.get("approvalStatus", "")).strip().lower()
        approved_by = str(review.get("approvedBy", "")).strip()
        approved_date = str(review.get("approvedDate", "")).strip()
        approval_pass = approval_status == STATUS_PASS and bool(approved_by) and bool(approved_date)
        add(
            "review-approval",
            "ARB2-or-better review is explicitly approved",
            STATUS_PASS if approval_pass else STATUS_NEEDS_REVIEW,
            f"approvalStatus={approval_status or 'missing'} approvedBy={approved_by or 'missing'} approvedDate={approved_date or 'missing'}",
        )
        missing_metadata = [field for field in REQUIRED_REVIEW_METADATA if not str(review.get(field, "")).strip()]
        add(
            "platform-metadata",
            "Performance platform and GL metadata are recorded",
            STATUS_PASS if not missing_metadata else STATUS_NEEDS_REVIEW,
            "missing=" + ",".join(missing_metadata or ["none"]),
        )

    if payload is None:
        add("performance-report", "Performance comparison report is present", STATUS_MISSING, rel_path(root, report_path))
        return checks

    add("performance-report", "Performance comparison report is present", STATUS_PASS, rel_path(root, report_path))
    metadata = payload.get("metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}
    profile = str(metadata.get("profile", ""))
    add(
        "profile",
        "Gameplay report used performance-comparison profile",
        STATUS_PASS if profile == "performance-comparison" else STATUS_FAIL,
        f"profile={profile or 'missing'}",
    )
    launch_variants = metadata.get("launchVariants", [])
    if not isinstance(launch_variants, list):
        launch_variants = []
    missing_variants = [variant for variant in REQUIRED_VARIANTS if variant not in launch_variants]
    add(
        "launch-variants",
        "Default, explicit ARB2, and executor variants are present",
        STATUS_PASS if not missing_variants else STATUS_MISSING,
        "missing=" + ",".join(missing_variants or ["none"]),
    )
    sample_msec = int(metadata.get("sampleMsec", 0) or 0)
    add(
        "sample-window",
        "Report uses a wall-clock sample window",
        STATUS_PASS if sample_msec > 0 else STATUS_NEEDS_REVIEW,
        f"sampleMsec={sample_msec}",
    )
    return checks


def review_config(review_manifest: dict[str, Any] | None) -> tuple[dict[str, Any], dict[str, Any]]:
    if review_manifest is None:
        manifest = default_review_manifest(Path("performance-review"))
    else:
        manifest = review_manifest
    review = manifest.get("review", {})
    thresholds = manifest.get("thresholds", {})
    if not isinstance(review, dict):
        review = {}
    if not isinstance(thresholds, dict):
        thresholds = {}
    default_thresholds = default_review_manifest(Path("performance-review"))["thresholds"]
    merged_thresholds = dict(default_thresholds)
    merged_thresholds.update(thresholds)
    return review, merged_thresholds


def build_summary(
    root: Path,
    output_dir: Path,
    review_path: Path | None,
    report_path: Path | None,
) -> dict[str, Any]:
    review_manifest: dict[str, Any] | None = None
    review_error = ""
    if review_path is not None and review_path.exists():
        review_manifest, review_error = read_json(review_path)
    elif review_path is not None:
        review_error = "review manifest path does not exist"

    review, thresholds = review_config(review_manifest)
    if report_path is None:
        report_path = resolve_path(root, str(review.get("sourceReport", "")))

    payload: dict[str, Any] | None = None
    report_error = ""
    if report_path is not None and report_path.exists():
        payload, report_error = read_json(report_path)
    elif report_path is not None:
        report_error = "performance report path does not exist"

    candidate_variant = str(review.get("candidateVariant", DEFAULT_CANDIDATE) or DEFAULT_CANDIDATE)
    baseline_variant = str(review.get("baselineVariant", DEFAULT_BASELINE) or DEFAULT_BASELINE)
    secondary_variant = str(review.get("secondaryBaselineVariant", DEFAULT_SECONDARY_BASELINE) or DEFAULT_SECONDARY_BASELINE)

    results = [
        evaluate_case(case, payload, thresholds, review_manifest, candidate_variant, baseline_variant, secondary_variant)
        for case in PERFORMANCE_CASES
    ]
    checks = evaluate_checks(root, review_manifest, review_path, payload, report_path)
    if review_error:
        status = STATUS_MISSING if "does not exist" in review_error else STATUS_FAIL
        checks.append({"id": "review-parse", "title": "Review manifest JSON parses", "status": status, "detail": review_error})
    if report_error:
        status = STATUS_MISSING if "does not exist" in report_error else STATUS_FAIL
        checks.append({"id": "report-parse", "title": "Performance report JSON parses", "status": status, "detail": report_error})

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

    metadata = payload.get("metadata", {}) if isinstance(payload, dict) else {}
    if not isinstance(metadata, dict):
        metadata = {}
    return {
        "schema": "openq4.renderer.performanceSummary.v1",
        "overallStatus": overall,
        "generated": now_string(),
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "metadata": {
            "profile": "performance-comparison",
            "reviewManifest": rel_path(root, review_path),
            "sourceReport": rel_path(root, report_path),
            "candidateVariant": candidate_variant,
            "baselineVariant": baseline_variant,
            "secondaryBaselineVariant": secondary_variant,
            "thresholds": thresholds,
            "sourceHost": metadata.get("host", ""),
            "policy": "Candidate must be ARB2-or-better on the agreed scenes before perf=arb2-or-better can be reviewed as pass.",
        },
        "matrix": [case_to_dict(case) for case in PERFORMANCE_CASES],
        "checks": checks,
        "results": results,
    }


def write_markdown(summary: dict[str, Any], path: Path) -> None:
    lines = [
        "# Renderer Performance Summary",
        "",
        f"- Status: `{summary['overallStatus']}`",
        f"- Generated: `{summary['generated']}`",
        f"- Source report: `{summary['metadata'].get('sourceReport') or 'not supplied'}`",
        f"- Review manifest: `{summary['metadata'].get('reviewManifest') or 'not supplied'}`",
        f"- Candidate: `{summary['metadata'].get('candidateVariant')}`",
        f"- Baseline: `{summary['metadata'].get('baselineVariant')}`",
        f"- Secondary baseline: `{summary['metadata'].get('secondaryBaselineVariant')}`",
        f"- Source host: `{summary['metadata'].get('sourceHost') or 'not recorded'}`",
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
            "## Scene Comparisons",
            "",
            "| Scene | Status | Candidate vs ARB2 P95 | P99 | Max | Hz | Notes |",
            "| --- | --- | ---: | ---: | ---: | ---: | --- |",
        ]
    )
    for result in summary["results"]:
        metrics = result.get("metrics", {})
        p95 = metrics.get("pacingP95Ms", {})
        p99 = metrics.get("pacingP99Ms", {})
        max_metric = metrics.get("pacingMaxMs", {})
        hz = metrics.get("pacingHz", {})
        notes = "; ".join(result.get("notes", []))
        lines.append(
            f"| `{result['id']}` | `{result['status']}` | "
            f"{p95.get('candidate', '')}/{p95.get('baseline', '')} `{p95.get('status', '')}` | "
            f"{p99.get('candidate', '')}/{p99.get('baseline', '')} `{p99.get('status', '')}` | "
            f"{max_metric.get('candidate', '')}/{max_metric.get('baseline', '')} `{max_metric.get('status', '')}` | "
            f"{hz.get('candidate', '')}/{hz.get('baseline', '')} `{hz.get('status', '')}` | "
            f"{markdown_cell(notes)} |"
        )

    lines.extend(
        [
            "",
            "## Variant Metrics",
            "",
            "| Scene | Variant | Status | P50 | P95 | P99 | Max | Hz | FE | BE | GPU | Warnings |",
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |",
        ]
    )
    for result in summary["results"]:
        for variant, variant_summary in result.get("variants", {}).items():
            value = variant_summary.get("summary", {}) if isinstance(variant_summary.get("summary"), dict) else {}
            lines.append(
                f"| `{result['id']}` | `{variant}` | `{variant_summary.get('status', '')}` | "
                f"{value.get('pacingP50Ms', '')} | {value.get('pacingP95Ms', '')} | "
                f"{value.get('pacingP99Ms', '')} | {value.get('pacingMaxMs', '')} | "
                f"{value.get('pacingHz', '')} | {value.get('frontEndMs', '')} | "
                f"{value.get('backendMs', '')} | {markdown_cell(value.get('gpuMs', ''))} | "
                f"{variant_summary.get('warnings', '')} |"
            )

    lines.extend(
        [
            "",
            "## Matrix",
            "",
            "| Case | Scene | Focus |",
            "| --- | --- | --- |",
        ]
    )
    for item in summary["matrix"]:
        lines.append(f"| `{item['id']}` | `{item['scene']}` | {markdown_cell(item['focus'])} |")
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        default=".tmp/renderer-validation/performance-summary",
        help="Directory for performance_summary.md/json and optional review template.",
    )
    parser.add_argument("--review-manifest", default="", help="Performance review manifest JSON to evaluate.")
    parser.add_argument("--gameplay-report", default="", help="performance-comparison renderer_gameplay_benchmark_report.json to evaluate.")
    parser.add_argument("--write-template", action="store_true", help="Write performance_review_template.json.")
    parser.add_argument("--print-matrix", action="store_true", help="Print the performance comparison matrix and exit.")
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
        template_path = write_template(output_dir)
    else:
        template_path = None

    review_path = resolve_path(root, args.review_manifest)
    report_path = resolve_path(root, args.gameplay_report)
    summary = build_summary(root, output_dir, review_path, report_path)
    write_json(output_dir / "performance_summary.json", summary)
    write_markdown(summary, output_dir / "performance_summary.md")

    print(f"performance summary: {summary['overallStatus']}")
    print(f"markdown: {output_dir / 'performance_summary.md'}")
    print(f"json: {output_dir / 'performance_summary.json'}")
    if template_path is not None:
        print(f"template: {template_path}")

    if args.require_complete and summary["overallStatus"] != STATUS_PASS:
        print(f"performance summary is {summary['overallStatus']}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
