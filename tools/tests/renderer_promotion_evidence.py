#!/usr/bin/env python3
"""Build an auditable renderer promotion evidence summary.

This tool does not collect new renderer evidence and it does not bless default
promotion by itself. It gathers existing validation artifacts into one
review-oriented bundle and keeps the result blocked until every automated,
manual, and platform gate is satisfied.
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

STATUS_PASS = "pass"
STATUS_NEEDS_REVIEW = "needs-review"
STATUS_MISSING = "missing"
STATUS_BLOCKED = "blocked"
STATUS_FAIL = "fail"

FINAL_DECISIONS = ("", "blocked", "no-promotion", "promote")

DEFAULT_SAFETY_CASES = {
    "renderer-default-promotion-selftest",
    "renderer-default-safety-selftest",
    "promotion-debug-off-defaults",
}

REQUIRED_SP_SCENE_COUNT = 6
REQUIRED_MP_SCENE_COUNT = 1


@dataclass
class ReportAssessment:
    label: str
    requested_path: str
    path: str
    present: bool
    json_present: bool
    status: str
    result_count: int
    pass_count: int
    fail_count: int
    warning_total: int
    failed_results: list[str]
    metadata: dict[str, Any]
    results: list[dict[str, Any]]
    notes: list[str]


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


def find_json_companion(path: Path) -> Path | None:
    if path.suffix.lower() == ".json":
        return path
    companion = path.with_suffix(".json")
    if companion.exists():
        return companion
    return None


def collect_warning_total(results: list[dict[str, Any]]) -> int:
    total = 0
    for result in results:
        signatures = result.get("warningSignatures", {})
        if not isinstance(signatures, dict):
            continue
        for value in signatures.values():
            try:
                total += int(value)
            except (TypeError, ValueError):
                continue
    return total


def assess_report(label: str, root: Path, requested: str | None) -> ReportAssessment:
    if not requested:
        return ReportAssessment(
            label=label,
            requested_path="",
            path="",
            present=False,
            json_present=False,
            status=STATUS_MISSING,
            result_count=0,
            pass_count=0,
            fail_count=0,
            warning_total=0,
            failed_results=[],
            metadata={},
            results=[],
            notes=["no artifact path supplied"],
        )

    requested_path = resolve_path(root, requested)
    assert requested_path is not None
    if not requested_path.exists():
        return ReportAssessment(
            label=label,
            requested_path=rel_path(root, requested_path),
            path=rel_path(root, requested_path),
            present=False,
            json_present=False,
            status=STATUS_MISSING,
            result_count=0,
            pass_count=0,
            fail_count=0,
            warning_total=0,
            failed_results=[],
            metadata={},
            results=[],
            notes=["artifact path does not exist"],
        )

    json_path = find_json_companion(requested_path)
    if json_path is None or not json_path.exists():
        return ReportAssessment(
            label=label,
            requested_path=rel_path(root, requested_path),
            path=rel_path(root, requested_path),
            present=True,
            json_present=False,
            status=STATUS_NEEDS_REVIEW,
            result_count=0,
            pass_count=0,
            fail_count=0,
            warning_total=0,
            failed_results=[],
            metadata={},
            results=[],
            notes=["artifact exists but no JSON report was found for automated status checks"],
        )

    try:
        payload = json.loads(json_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return ReportAssessment(
            label=label,
            requested_path=rel_path(root, requested_path),
            path=rel_path(root, json_path),
            present=True,
            json_present=True,
            status=STATUS_FAIL,
            result_count=0,
            pass_count=0,
            fail_count=1,
            warning_total=0,
            failed_results=[f"invalid-json:{exc}"],
            metadata={},
            results=[],
            notes=[f"could not parse JSON report: {exc}"],
        )

    overall_status = str(payload.get("overallStatus", "")).strip().lower()
    results = payload.get("results", [])
    if not isinstance(results, list):
        results = []
    typed_results = [result for result in results if isinstance(result, dict)]
    failed = [
        f"{result.get('id', 'unknown')}:{result.get('status', 'unknown')}"
        for result in typed_results
        if result.get("status") != STATUS_PASS
    ]
    hard_failed = [
        f"{result.get('id', 'unknown')}:{result.get('status', 'unknown')}"
        for result in typed_results
        if result.get("status") == STATUS_FAIL
    ]
    pass_count = sum(1 for result in typed_results if result.get("status") == STATUS_PASS)
    fail_count = len(failed)
    warning_total = collect_warning_total(typed_results)

    notes: list[str] = []
    if not typed_results:
        if overall_status == STATUS_PASS:
            status = STATUS_PASS
            notes.append("summary artifact reports overallStatus=pass")
        elif overall_status == STATUS_FAIL:
            status = STATUS_FAIL
            notes.append("summary artifact reports overallStatus=fail")
        elif overall_status in (STATUS_BLOCKED, STATUS_MISSING, STATUS_NEEDS_REVIEW):
            status = STATUS_NEEDS_REVIEW
            notes.append(f"summary artifact reports overallStatus={overall_status}")
        else:
            status = STATUS_NEEDS_REVIEW
            notes.append("JSON artifact has no top-level results array")
    elif fail_count:
        if hard_failed or overall_status not in (STATUS_BLOCKED, STATUS_MISSING, STATUS_NEEDS_REVIEW):
            status = STATUS_FAIL
        else:
            status = STATUS_NEEDS_REVIEW
        notes.append(f"{fail_count} result(s) are not pass")
        if hard_failed:
            notes.append(f"{len(hard_failed)} result(s) are explicit fail")
    else:
        status = STATUS_PASS
        notes.append(f"{pass_count} result(s) passed")

    metadata = payload.get("metadata", {})
    if not isinstance(metadata, dict):
        metadata = {}

    return ReportAssessment(
        label=label,
        requested_path=rel_path(root, requested_path),
        path=rel_path(root, json_path),
        present=True,
        json_present=True,
        status=status,
        result_count=len(typed_results),
        pass_count=pass_count,
        fail_count=fail_count,
        warning_total=warning_total,
        failed_results=failed,
        metadata=metadata,
        results=typed_results,
        notes=notes,
    )


def load_reviews(root: Path, path_value: str | None) -> dict[str, dict[str, Any]]:
    path = resolve_path(root, path_value)
    if path is None:
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    gates = payload.get("gates", payload)
    if not isinstance(gates, dict):
        raise ValueError("review file must contain a top-level object or a 'gates' object")
    reviews: dict[str, dict[str, Any]] = {}
    for gate_id, review in gates.items():
        if isinstance(review, dict):
            reviews[str(gate_id)] = review
    return reviews


def make_gate(
    gate_id: str,
    title: str,
    token: str,
    required: str,
    status: str,
    evidence: str,
    notes: list[str],
) -> dict[str, Any]:
    return {
        "id": gate_id,
        "title": title,
        "token": token,
        "required": required,
        "status": status,
        "evidence": evidence,
        "notes": notes,
    }


def apply_review(gate: dict[str, Any], reviews: dict[str, dict[str, Any]]) -> dict[str, Any]:
    review = reviews.get(gate["id"])
    if not review:
        return gate

    status = str(review.get("status", "")).strip()
    if status in {STATUS_PASS, STATUS_NEEDS_REVIEW, STATUS_MISSING, STATUS_BLOCKED, STATUS_FAIL}:
        gate["status"] = status
    if review.get("reviewedBy"):
        gate["notes"].append(f"reviewedBy={review['reviewedBy']}")
    if review.get("decision"):
        gate["notes"].append(f"decision={review['decision']}")
    if review.get("notes"):
        gate["notes"].append(str(review["notes"]))
    return gate


def automated_gate(
    gate_id: str,
    title: str,
    token: str,
    required: str,
    report: ReportAssessment,
    reviews: dict[str, dict[str, Any]],
    *,
    require_zero_warnings: bool = False,
    review_after_pass: bool = False,
) -> dict[str, Any]:
    status = report.status
    notes = list(report.notes)
    if report.status == STATUS_PASS and require_zero_warnings:
        if report.warning_total == 0:
            notes.append("warning signature total is 0")
        else:
            status = STATUS_FAIL
            notes.append(f"warning signature total is {report.warning_total}")
    if status == STATUS_PASS and review_after_pass:
        status = STATUS_NEEDS_REVIEW
        notes.append("automated report passed; manual review is still required for this gate")
    if report.failed_results:
        notes.append("failed results: " + ", ".join(report.failed_results[:8]))
    return apply_review(make_gate(gate_id, title, token, required, status, report.path or report.requested_path, notes), reviews)


def visual_gate(report: ReportAssessment, reviews: dict[str, dict[str, Any]]) -> dict[str, Any]:
    status = report.status
    notes = list(report.notes)
    require_references = bool(report.metadata.get("requireReferences"))
    reference_dir = str(report.metadata.get("referenceDir", ""))
    if status == STATUS_PASS:
        if require_references and reference_dir:
            notes.append(f"approved references were required from {reference_dir}")
        else:
            status = STATUS_NEEDS_REVIEW
            notes.append("report did not require approved references; rerun with --require-references before promotion")
    return apply_review(
        make_gate(
            "visual-references",
            "Approved deterministic visual references",
            "visual=pass",
            "Reference-backed visual comparison with RMS/max thresholds and reviewed scene exceptions.",
            status,
            report.path or report.requested_path,
            notes,
        ),
        reviews,
    )


def manual_gate(
    gate_id: str,
    title: str,
    token: str,
    required: str,
    report: ReportAssessment,
    reviews: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    status = report.status
    notes = list(report.notes)
    if report.present and status == STATUS_PASS:
        status = STATUS_NEEDS_REVIEW
        notes.append("automated JSON exists, but this gate requires an explicit review decision")
    elif report.present and status == STATUS_NEEDS_REVIEW:
        notes.append("manual artifact is present; mark this gate pass through a review file after review")
    return apply_review(make_gate(gate_id, title, token, required, status, report.path or report.requested_path, notes), reviews)


def scene_gate(
    gate_id: str,
    title: str,
    token: str,
    required: str,
    report: ReportAssessment,
    reviews: dict[str, dict[str, Any]],
    mode: str,
    minimum_count: int,
) -> dict[str, Any]:
    status = report.status
    notes = list(report.notes)
    passed = [
        result
        for result in report.results
        if result.get("status") == STATUS_PASS and str(result.get("mode", "")).upper() == mode
    ]
    if status == STATUS_PASS:
        if len(passed) >= minimum_count:
            notes.append(f"{len(passed)} {mode} result(s) passed")
        else:
            status = STATUS_FAIL
            notes.append(f"expected at least {minimum_count} {mode} result(s), found {len(passed)}")
    return apply_review(make_gate(gate_id, title, token, required, status, report.path or report.requested_path, notes), reviews)


def default_safety_gate(report: ReportAssessment, reviews: dict[str, dict[str, Any]]) -> dict[str, Any]:
    status = report.status
    notes = list(report.notes)
    if status == STATUS_PASS:
        result_ids = {str(result.get("id", "")) for result in report.results}
        missing = sorted(DEFAULT_SAFETY_CASES - result_ids)
        if missing:
            status = STATUS_NEEDS_REVIEW
            notes.append("default-safety cases not all present: " + ", ".join(missing))
        else:
            notes.append("default promotion and default safety cases are present")
    return apply_review(
        make_gate(
            "debug-off-defaults",
            "Debug-off and experimental-path-off defaults",
            "debug=off",
            "Default startup keeps debug, validation, bindless, shader reload, modern-visible, and auto-promotion side paths off.",
            status,
            report.path or report.requested_path,
            notes,
        ),
        reviews,
    )


def platform_gate(
    gate_id: str,
    title: str,
    platform_token: str,
    safe_report: ReportAssessment,
    gameplay_report: ReportAssessment,
    reviews: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    notes: list[str] = []
    evidence_parts = [item for item in (safe_report.path, gameplay_report.path) if item]
    if safe_report.status == STATUS_MISSING or gameplay_report.status == STATUS_MISSING:
        status = STATUS_MISSING
        notes.append("safe validation and required gameplay reports are both required")
    elif safe_report.status != STATUS_PASS or gameplay_report.status != STATUS_PASS:
        status = STATUS_FAIL
        notes.append(f"safe={safe_report.status}, gameplay={gameplay_report.status}")
    else:
        safe_host = str(safe_report.metadata.get("host", ""))
        gameplay_host = str(gameplay_report.metadata.get("host", ""))
        host_text = f"{safe_host} {gameplay_host}".lower()
        if platform_token not in host_text:
            status = STATUS_FAIL
            notes.append(f"host metadata does not match expected platform token '{platform_token}'")
        else:
            status = STATUS_PASS
            notes.append(f"safe host={safe_host}; gameplay host={gameplay_host}")
    return apply_review(
        make_gate(
            gate_id,
            title,
            "phase8=complete",
            "Platform-specific safe validation plus required SP/MP gameplay evidence.",
            status,
            "; ".join(evidence_parts),
            notes,
        ),
        reviews,
    )


def final_decision_gate(decision: str, reviews: dict[str, dict[str, Any]]) -> dict[str, Any]:
    status = STATUS_BLOCKED
    notes = ["no promotion/no-promotion review decision recorded"]
    if decision in ("promote", "no-promotion"):
        status = STATUS_PASS
        notes = [f"decision={decision}"]
    elif decision == "blocked":
        notes = ["decision=blocked"]
    return apply_review(
        make_gate(
            "review-decision",
            "Reviewed promotion decision",
            "phase8=complete",
            "A reviewer records promote or no-promotion after every evidence row is pass.",
            status,
            "",
            notes,
        ),
        reviews,
    )


def overall_status(gates: list[dict[str, Any]]) -> str:
    if any(gate["status"] == STATUS_FAIL for gate in gates):
        return STATUS_FAIL
    if all(gate["status"] == STATUS_PASS for gate in gates):
        return STATUS_PASS
    return STATUS_BLOCKED


def status_counts(gates: list[dict[str, Any]]) -> dict[str, int]:
    counts = {STATUS_PASS: 0, STATUS_NEEDS_REVIEW: 0, STATUS_MISSING: 0, STATUS_BLOCKED: 0, STATUS_FAIL: 0}
    for gate in gates:
        counts[gate["status"]] = counts.get(gate["status"], 0) + 1
    return counts


def report_summary(report: ReportAssessment) -> dict[str, Any]:
    return {
        "label": report.label,
        "requestedPath": report.requested_path,
        "path": report.path,
        "present": report.present,
        "jsonPresent": report.json_present,
        "status": report.status,
        "resultCount": report.result_count,
        "passCount": report.pass_count,
        "failCount": report.fail_count,
        "warningTotal": report.warning_total,
        "failedResults": report.failed_results,
        "metadata": report.metadata,
        "resultIds": [str(result.get("id", "")) for result in report.results],
        "notes": report.notes,
    }


def write_summary(root: Path, output_dir: Path, payload: dict[str, Any]) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    report_json = output_dir / "promotion_evidence_summary.json"
    report_md = output_dir / "promotion_evidence_summary.md"
    report_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    metadata = payload["metadata"]
    gates = payload["gates"]
    counts = payload["statusCounts"]
    lines = [
        "# Renderer Promotion Evidence Summary",
        "",
        f"- Generated: {metadata['generated']}",
        f"- Host: {metadata['host']}",
        f"- Overall status: **{payload['overallStatus']}**",
        f"- Required evidence token: `{PROMOTION_EVIDENCE_TOKEN}`",
        f"- Gate counts: pass={counts.get(STATUS_PASS, 0)}, needs-review={counts.get(STATUS_NEEDS_REVIEW, 0)}, missing={counts.get(STATUS_MISSING, 0)}, blocked={counts.get(STATUS_BLOCKED, 0)}, fail={counts.get(STATUS_FAIL, 0)}",
        "",
        "`r_rendererModernAutoPromote 1` must remain out of default paths until every row below is `pass` and the final review decision is recorded.",
        "",
        "## Gate Summary",
        "",
        "| Status | Gate | Token | Evidence | Notes |",
        "|---|---|---|---|---|",
    ]
    for gate in gates:
        evidence = gate.get("evidence", "")
        notes = "; ".join(gate.get("notes", []))
        lines.append(
            f"| {gate['status']} | {markdown_cell(gate['title'])} | `{markdown_cell(gate['token'])}` | "
            f"`{markdown_cell(evidence)}` | {markdown_cell(notes)} |"
        )

    lines += [
        "",
        "## Required Evidence Token",
        "",
        "| Token | Evidence represented |",
        "|---|---|",
        "| `phase8=complete` | A single reviewed promotion bundle exists, including platform evidence and the final promote/no-promotion decision. |",
        "| `warnings=0` | Safe validation and gameplay/benchmark logs have zero renderer warning signatures. |",
        "| `visual=pass` | Approved deterministic references were compared with `--require-references` and reviewed. |",
        "| `gameplay=pass` | Required SP and MP gameplay coverage reached gameplay and passed log/screenshot gates. |",
        "| `renderdoc=pass` | RenderDoc/API capture summary reviewed named resources, pass contents, and bind-count/invalidation claims. |",
        "| `perf=arb2-or-better` | Candidate path meets or beats default/explicit ARB2 on the agreed frame-time thresholds. |",
        "| `presentation=pass` | Capped, uncapped, and VSync presentation coverage passed. |",
        "| `rollback=pass` | Explicit ARB2, legacy tier, and modern-disable rollback commands were validated after modern-path usage. |",
        "| `debug=off` | Clean defaults keep debug, validation, bindless, shader reload, and experimental side paths off. |",
        "",
        "## Remaining Work",
        "",
    ]

    pending = [gate for gate in gates if gate["status"] != STATUS_PASS]
    if pending:
        for gate in pending:
            lines.append(f"- {gate['title']}: {gate['status']}.")
    else:
        lines.append("- No remaining evidence gates are pending.")

    lines += [
        "",
        "## Collection Commands",
        "",
        "```powershell",
        "python tools\\tests\\renderer_validation_matrix.py --output-dir .tmp\\renderer-validation\\<safe-run>",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile required --output-dir .tmp\\renderer-gameplay\\<required-gameplay-run>",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile visual-comparison --reference-dir .tmp\\renderer-references\\mid-term-visual\\windows-x64 --require-references --output-dir .tmp\\renderer-gameplay\\<visual-run>",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile performance-comparison --output-dir .tmp\\renderer-gameplay\\<performance-run>",
        "python tools\\tests\\renderer_candidate_summary.py --output-dir .tmp\\renderer-validation\\<candidate-run> --manifest .tmp\\renderer-validation\\<candidate-run>\\candidate_manifest_template.json",
        "python tools\\tests\\renderer_gameplay_benchmark.py --profile presentation-comparison --pacing-only --output-dir .tmp\\renderer-gameplay\\<presentation-run>",
        "python tools\\tests\\renderer_platform_summary.py --output-dir .tmp\\renderer-validation\\<platform-summary-run> --manifest .tmp\\renderer-validation\\<platform-summary-run>\\platform_evidence_manifest_template.json",
        "```",
        "",
    ]

    report_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return report_json, report_md


def build_payload(args: argparse.Namespace, root: Path) -> dict[str, Any]:
    reviews = load_reviews(root, args.review_file)

    safe_report = assess_report("safe renderer validation", root, args.safe_report)
    required_gameplay_report = assess_report("required gameplay", root, args.required_gameplay_report)
    visual_report = assess_report("visual references", root, args.visual_report)
    capture_report = assess_report("RenderDoc/API captures", root, args.capture_summary)
    performance_report = assess_report("performance comparison", root, args.performance_report)
    candidate_summary = assess_report("modern renderer candidate summary", root, args.candidate_summary)
    presentation_report = assess_report("presentation comparison", root, args.presentation_report)
    rollback_report = assess_report("rollback coverage", root, args.rollback_report)
    default_report = assess_report("default safety", root, args.default_report or args.safe_report)
    platform_summary = assess_report("cross-platform evidence summary", root, args.platform_summary)

    windows_safe = assess_report("Windows safe validation", root, args.windows_safe_report or args.safe_report)
    windows_gameplay = assess_report("Windows gameplay", root, args.windows_gameplay_report or args.required_gameplay_report)
    linux_safe = assess_report("Linux safe validation", root, args.linux_safe_report)
    linux_gameplay = assess_report("Linux gameplay", root, args.linux_gameplay_report)
    macos_safe = assess_report("macOS safe validation", root, args.macos_safe_report)
    macos_gameplay = assess_report("macOS gameplay", root, args.macos_gameplay_report)

    gates = [
        automated_gate(
            "safe-validation",
            "Zero-warning safe renderer validation",
            "warnings=0",
            "Broad safe validation passes and reports no renderer warning signatures.",
            safe_report,
            reviews,
            require_zero_warnings=True,
        ),
        scene_gate(
            "required-sp-gameplay",
            "Required SP gameplay coverage",
            "gameplay=pass",
            "All required SP gameplay scenes pass under the required profile.",
            required_gameplay_report,
            reviews,
            "SP",
            REQUIRED_SP_SCENE_COUNT,
        ),
        scene_gate(
            "required-mp-gameplay",
            "Required MP listen/client gameplay coverage",
            "gameplay=pass",
            "The MP q4dm1 listen/local-client gameplay case passes.",
            required_gameplay_report,
            reviews,
            "MP",
            REQUIRED_MP_SCENE_COUNT,
        ),
        visual_gate(visual_report, reviews),
        manual_gate(
            "renderdoc-api-captures",
            "RenderDoc/API capture summary",
            "renderdoc=pass",
            "Capture summary records file locations, driver/GPU/OS/GL metadata, named resources, pass contents, bind-count claims, and invalidation behavior.",
            capture_report,
            reviews,
        ),
        manual_gate(
            "performance-arb2-or-better",
            "ARB2-or-better performance comparison",
            "perf=arb2-or-better",
            "Performance comparison is reviewed against the selected candidate, default, and explicit ARB2 thresholds.",
            performance_report,
            reviews,
        ),
        automated_gate(
            "modern-promotion-candidate",
            "Modern renderer promotion candidate",
            "phase8=complete",
            "A selected modern candidate exists and satisfies the candidate-readiness matrix before any default promotion is considered.",
            candidate_summary,
            reviews,
        )
        if args.candidate_summary
        else None,
        automated_gate(
            "presentation-coverage",
            "Capped, uncapped, and VSync presentation coverage",
            "presentation=pass",
            "Presentation comparison profile passes for capped, uncapped, and VSync variants.",
            presentation_report,
            reviews,
        ),
        automated_gate(
            "rollback-coverage",
            "Rollback command coverage",
            "rollback=pass",
            "ARB2, legacy-tier, and modern-disable rollback commands are validated after modern-path usage.",
            rollback_report,
            reviews,
        ),
        default_safety_gate(default_report, reviews),
        platform_gate("windows-x64-platform", "Windows x64 platform evidence", "windows", windows_safe, windows_gameplay, reviews),
        platform_gate("linux-x64-platform", "Linux x64 platform evidence", "linux", linux_safe, linux_gameplay, reviews),
        platform_gate("macos-gl41-platform", "macOS GL 4.1 platform evidence", "darwin", macos_safe, macos_gameplay, reviews),
        final_decision_gate(args.decision, reviews),
    ]
    gates = [gate for gate in gates if gate is not None]
    if args.platform_summary:
        gates.insert(
            -1,
            automated_gate(
                "cross-platform-summary",
                "Reviewed cross-platform evidence summary",
                "phase8=complete",
                "Platform ledger records safe, gameplay, visual, presentation, hardware/GL metadata, and review status for Windows x64, Linux x64, and macOS GL 4.1.",
                platform_summary,
                reviews,
            ),
        )

    overall = overall_status(gates)
    reports = {
        "safe": report_summary(safe_report),
        "requiredGameplay": report_summary(required_gameplay_report),
        "visual": report_summary(visual_report),
        "capture": report_summary(capture_report),
        "performance": report_summary(performance_report),
        "presentation": report_summary(presentation_report),
        "rollback": report_summary(rollback_report),
        "defaultSafety": report_summary(default_report),
        "windowsSafe": report_summary(windows_safe),
        "windowsGameplay": report_summary(windows_gameplay),
        "linuxSafe": report_summary(linux_safe),
        "linuxGameplay": report_summary(linux_gameplay),
        "macosSafe": report_summary(macos_safe),
        "macosGameplay": report_summary(macos_gameplay),
    }
    if args.platform_summary:
        reports["platformSummary"] = report_summary(platform_summary)
    if args.candidate_summary:
        reports["candidateSummary"] = report_summary(candidate_summary)

    return {
        "metadata": {
            "generated": now_string(),
            "host": f"{platform.system()} {platform.release()} {platform.machine()}",
            "requiredEvidenceToken": PROMOTION_EVIDENCE_TOKEN,
            "autoPromoteCvar": "r_rendererModernAutoPromote",
            "promotionEvidenceCvar": "r_rendererPromotionEvidence",
            "outputDir": rel_path(root, resolve_path(root, args.output_dir) or root),
        },
        "overallStatus": overall,
        "statusCounts": status_counts(gates),
        "gates": gates,
        "reports": reports,
    }


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default=".tmp/renderer-validation/promotion-evidence", help="Directory for promotion_evidence_summary.md/json.")
    parser.add_argument("--safe-report", help="Safe renderer validation Markdown or JSON report.")
    parser.add_argument("--required-gameplay-report", help="Required SP/MP gameplay Markdown or JSON report.")
    parser.add_argument("--visual-report", help="Reference-backed visual comparison Markdown or JSON report.")
    parser.add_argument("--capture-summary", help="RenderDoc/API capture summary Markdown or JSON artifact.")
    parser.add_argument("--performance-report", help="Performance comparison Markdown or JSON report.")
    parser.add_argument("--candidate-summary", help="Modern renderer promotion candidate summary Markdown or JSON report.")
    parser.add_argument("--platform-summary", help="Cross-platform platform evidence summary Markdown or JSON report.")
    parser.add_argument("--presentation-report", help="Presentation comparison Markdown or JSON report.")
    parser.add_argument("--rollback-report", help="Rollback gameplay Markdown or JSON report.")
    parser.add_argument("--default-report", help="Default-promotion/default-safety validation report. Defaults to --safe-report.")
    parser.add_argument("--windows-safe-report", help="Windows x64 safe validation report. Defaults to --safe-report.")
    parser.add_argument("--windows-gameplay-report", help="Windows x64 required gameplay report. Defaults to --required-gameplay-report.")
    parser.add_argument("--linux-safe-report", help="Linux x64 safe validation report.")
    parser.add_argument("--linux-gameplay-report", help="Linux x64 required gameplay report.")
    parser.add_argument("--macos-safe-report", help="macOS GL 4.1 safe validation report.")
    parser.add_argument("--macos-gameplay-report", help="macOS GL 4.1 required gameplay report.")
    parser.add_argument("--review-file", help="Optional JSON file with gate review overrides keyed by gate id.")
    parser.add_argument("--decision", default="", choices=FINAL_DECISIONS, help="Final reviewed decision once every gate is pass.")
    parser.add_argument("--print-token", action="store_true", help="Print the required r_rendererPromotionEvidence token and exit.")
    parser.add_argument("--require-complete", action="store_true", help="Exit nonzero unless every promotion evidence gate is pass.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.print_token:
        print(PROMOTION_EVIDENCE_TOKEN)
        return 0

    root = repo_root()
    output_dir = resolve_path(root, args.output_dir)
    assert output_dir is not None
    payload = build_payload(args, root)
    report_json, report_md = write_summary(root, output_dir, payload)
    print(f"Wrote {rel_path(root, report_md)}")
    print(f"Wrote {rel_path(root, report_json)}")
    print(f"Overall status: {payload['overallStatus']}")
    if args.require_complete and payload["overallStatus"] != STATUS_PASS:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
