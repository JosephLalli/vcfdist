#!/usr/bin/env python3
"""Compare timed vcfdist benchmark runs.

This helper turns the ad hoc benchmark checks used during performance work into
a reusable tool.  It compares the stable vcfdist output files from one run
against another, parses `/usr/bin/time -v` output when `time.txt` is present,
and prints the headline precision/recall and phasing metrics that must remain
unchanged across runtime optimizations.

Example:

    python3 tools/compare_vcfdist_runs.py \
        out/opencode-runtime/profile_threads_64 \
        out/opencode-runtime/branch_threads_64_v1 \
        --prefix vcfdist.

The script exits nonzero if any stable output differs. Runtime/RSS deltas are
reported for review but are not treated as pass/fail by this tool.
"""

from __future__ import annotations

import argparse
import csv
import difflib
import json
import re
import sys
from pathlib import Path
from typing import Any


STABLE_SUFFIXES = [
    "phase-blocks.tsv",
    "phasing-summary.tsv",
    "precision-recall-summary.tsv",
    "precision-recall.tsv",
    "query.tsv",
    "superclusters.tsv",
    "switchflips.tsv",
    "truth.tsv",
]

SUMMARY_VCF_SUFFIX = "summary.vcf"
VOLATILE_VCF_HEADERS = ("##fileDate=", "##CL=")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare timed vcfdist output trees and report runtime deltas."
    )
    parser.add_argument(
        "baseline_dir",
        type=Path,
        help="Baseline run directory containing vcfdist outputs and optional time.txt.",
    )
    parser.add_argument(
        "run_dir",
        type=Path,
        help="Candidate/branch run directory containing vcfdist outputs and optional time.txt.",
    )
    parser.add_argument(
        "--prefix",
        default="vcfdist.",
        help="Filename prefix before vcfdist output suffixes (default: vcfdist.). Use '' for demo-style outputs.",
    )
    parser.add_argument(
        "--baseline-time",
        type=Path,
        default=None,
        help="Optional explicit baseline /usr/bin/time -v file. Defaults to BASELINE_DIR/time.txt.",
    )
    parser.add_argument(
        "--run-time",
        type=Path,
        default=None,
        help="Optional explicit candidate /usr/bin/time -v file. Defaults to RUN_DIR/time.txt.",
    )
    parser.add_argument(
        "--max-diff-lines",
        type=int,
        default=80,
        help="Maximum unified diff lines to print for the first differing text output (default: 80).",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit a machine-readable JSON summary instead of human-readable text.",
    )
    return parser.parse_args()


def output_path(run_dir: Path, prefix: str, suffix: str) -> Path:
    return run_dir / f"{prefix}{suffix}"


def normalized_summary_vcf(path: Path) -> list[str]:
    lines = []
    for line in path.read_text().splitlines():
        if line.startswith(VOLATILE_VCF_HEADERS):
            continue
        lines.append(line)
    return lines


def first_unified_diff(
        baseline_path: Path,
        run_path: Path,
        max_lines: int,
        normalize_vcf: bool = False) -> list[str]:
    if normalize_vcf:
        baseline_lines = normalized_summary_vcf(baseline_path)
        run_lines = normalized_summary_vcf(run_path)
    else:
        baseline_lines = baseline_path.read_text().splitlines()
        run_lines = run_path.read_text().splitlines()

    diff = difflib.unified_diff(
        baseline_lines,
        run_lines,
        fromfile=str(baseline_path),
        tofile=str(run_path),
        lineterm="",
    )
    return list(diff)[:max_lines]


def compare_outputs(
        baseline_dir: Path,
        run_dir: Path,
        prefix: str,
        max_diff_lines: int) -> dict[str, Any]:
    files: list[dict[str, Any]] = []
    pass_outputs = True

    for suffix in STABLE_SUFFIXES:
        baseline_path = output_path(baseline_dir, prefix, suffix)
        run_path = output_path(run_dir, prefix, suffix)
        result: dict[str, Any] = {"file": f"{prefix}{suffix}"}

        if not baseline_path.exists() or not run_path.exists():
            result["status"] = "missing"
            result["baseline_exists"] = baseline_path.exists()
            result["run_exists"] = run_path.exists()
            pass_outputs = False
        elif baseline_path.read_bytes() == run_path.read_bytes():
            result["status"] = "same"
        else:
            result["status"] = "different"
            result["diff"] = first_unified_diff(
                    baseline_path, run_path, max_diff_lines)
            pass_outputs = False
        files.append(result)

    baseline_vcf = output_path(baseline_dir, prefix, SUMMARY_VCF_SUFFIX)
    run_vcf = output_path(run_dir, prefix, SUMMARY_VCF_SUFFIX)
    vcf_result: dict[str, Any] = {"file": f"{prefix}{SUMMARY_VCF_SUFFIX}"}
    if not baseline_vcf.exists() or not run_vcf.exists():
        vcf_result["status"] = "missing"
        vcf_result["baseline_exists"] = baseline_vcf.exists()
        vcf_result["run_exists"] = run_vcf.exists()
        pass_outputs = False
    else:
        baseline_lines = normalized_summary_vcf(baseline_vcf)
        run_lines = normalized_summary_vcf(run_vcf)
        if baseline_lines == run_lines:
            vcf_result["status"] = "same_normalized"
            vcf_result["normalized_headers"] = list(VOLATILE_VCF_HEADERS)
        else:
            vcf_result["status"] = "different_normalized"
            vcf_result["normalized_headers"] = list(VOLATILE_VCF_HEADERS)
            vcf_result["diff"] = first_unified_diff(
                    baseline_vcf, run_vcf, max_diff_lines, normalize_vcf=True)
            pass_outputs = False
    files.append(vcf_result)

    return {"pass": pass_outputs, "files": files}


def parse_elapsed_seconds(value: str) -> float | None:
    parts = value.strip().split(":")
    try:
        if len(parts) == 3:
            hours = int(parts[0])
            minutes = int(parts[1])
            seconds = float(parts[2])
            return hours * 3600 + minutes * 60 + seconds
        if len(parts) == 2:
            minutes = int(parts[0])
            seconds = float(parts[1])
            return minutes * 60 + seconds
        if len(parts) == 1:
            return float(parts[0])
    except ValueError:
        return None
    return None


def parse_percent(value: str) -> float | None:
    match = re.match(r"([0-9.]+)%", value.strip())
    if match is None:
        return None
    return float(match.group(1))


def parse_time_file(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None

    result: dict[str, Any] = {"path": str(path)}
    for raw_line in path.read_text().splitlines():
        line = raw_line.strip()
        if ": " not in line:
            continue
        key, value = line.split(": ", 1)
        if key == "Command being timed":
            result["command"] = value.strip().strip('"')
        elif key == "User time (seconds)":
            result["user_seconds"] = float(value)
        elif key == "System time (seconds)":
            result["system_seconds"] = float(value)
        elif key == "Percent of CPU this job got":
            result["cpu_percent"] = parse_percent(value)
        elif key.startswith("Elapsed (wall clock) time"):
            result["elapsed"] = value
            result["elapsed_seconds"] = parse_elapsed_seconds(value)
        elif key == "Maximum resident set size (kbytes)":
            result["max_rss_kb"] = int(value)
        elif key == "Exit status":
            result["exit_status"] = int(value)
    return result


def summarize_time_delta(
        baseline_time: dict[str, Any] | None,
        run_time: dict[str, Any] | None) -> dict[str, Any]:
    if baseline_time is None or run_time is None:
        return {
            "available": False,
            "baseline_time_available": baseline_time is not None,
            "run_time_available": run_time is not None,
        }

    summary: dict[str, Any] = {
        "available": True,
        "baseline": baseline_time,
        "run": run_time,
    }

    baseline_elapsed = baseline_time.get("elapsed_seconds")
    run_elapsed = run_time.get("elapsed_seconds")
    if baseline_elapsed is not None and run_elapsed is not None:
        summary["elapsed_delta_seconds"] = run_elapsed - baseline_elapsed
        summary["elapsed_speedup"] = baseline_elapsed / run_elapsed if run_elapsed else None
        summary["elapsed_change_percent"] = (
            100.0 * (run_elapsed - baseline_elapsed) / baseline_elapsed
            if baseline_elapsed else None
        )

    baseline_rss = baseline_time.get("max_rss_kb")
    run_rss = run_time.get("max_rss_kb")
    if baseline_rss is not None and run_rss is not None:
        summary["rss_delta_kb"] = run_rss - baseline_rss
        summary["rss_ratio"] = run_rss / baseline_rss if baseline_rss else None
        summary["rss_change_percent"] = (
            100.0 * (run_rss - baseline_rss) / baseline_rss
            if baseline_rss else None
        )

    return summary


def load_tsv_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle, delimiter="\t"))


def headline_metrics(run_dir: Path, prefix: str) -> dict[str, Any]:
    metrics: dict[str, Any] = {}

    pr_path = output_path(run_dir, prefix, "precision-recall-summary.tsv")
    if pr_path.exists():
        rows = load_tsv_rows(pr_path)
        all_none = next(
            (
                row for row in rows
                if row.get("VAR_TYPE") == "ALL" and row.get("THRESHOLD") == "NONE"
            ),
            None,
        )
        if all_none is not None:
            metrics["all_none"] = {
                "truth_tp": int(all_none["TRUTH_TP"]),
                "query_tp": int(all_none["QUERY_TP"]),
                "truth_fn": int(all_none["TRUTH_FN"]),
                "query_fp": int(all_none["QUERY_FP"]),
                "precision": float(all_none["PREC"]),
                "recall": float(all_none["RECALL"]),
                "f1_score": float(all_none["F1_SCORE"]),
                "f1_qscore": float(all_none["F1_QSCORE"]),
            }

    phase_path = output_path(run_dir, prefix, "phasing-summary.tsv")
    if phase_path.exists():
        rows = load_tsv_rows(phase_path)
        if rows:
            row = rows[0]
            metrics["phasing"] = {
                "phase_blocks": int(row["PHASE_BLOCKS"]),
                "switch_errors": int(row["SWITCH_ERRORS"]),
                "flip_errors": int(row["FLIP_ERRORS"]),
            }

    superclusters_path = output_path(run_dir, prefix, "superclusters.tsv")
    if superclusters_path.exists():
        with superclusters_path.open() as handle:
            metrics["superclusters"] = max(0, sum(1 for _ in handle) - 1)

    return metrics


def render_human(summary: dict[str, Any]) -> None:
    print("vcfdist timed run comparison")
    print(f"  baseline: {summary['baseline_dir']}")
    print(f"  run     : {summary['run_dir']}")
    print(f"  prefix  : {summary['prefix']!r}")

    time_summary = summary["time"]
    if time_summary["available"]:
        baseline = time_summary["baseline"]
        run = time_summary["run"]
        print("\nRuntime:")
        if "elapsed_seconds" in baseline and "elapsed_seconds" in run:
            print(
                "  elapsed: "
                f"baseline={baseline['elapsed']} ({baseline['elapsed_seconds']:.3f}s), "
                f"run={run['elapsed']} ({run['elapsed_seconds']:.3f}s), "
                f"delta={time_summary['elapsed_delta_seconds']:+.3f}s, "
                f"speedup={time_summary['elapsed_speedup']:.3f}x"
            )
        if "max_rss_kb" in baseline and "max_rss_kb" in run:
            print(
                "  max RSS: "
                f"baseline={baseline['max_rss_kb']} KB, "
                f"run={run['max_rss_kb']} KB, "
                f"delta={time_summary['rss_delta_kb']:+d} KB, "
                f"ratio={time_summary['rss_ratio']:.3f}x"
            )
    else:
        print("\nRuntime: unavailable; expected time.txt in each run directory or explicit --*-time paths")

    print("\nHeadline metrics from run:")
    metrics = summary["metrics"]
    all_none = metrics.get("all_none")
    if all_none:
        print(
            "  ALL/NONE: "
            f"truth_tp={all_none['truth_tp']}, "
            f"query_tp={all_none['query_tp']}, "
            f"truth_fn={all_none['truth_fn']}, "
            f"query_fp={all_none['query_fp']}, "
            f"f1={all_none['f1_score']:.6f}"
        )
    phase = metrics.get("phasing")
    if phase:
        print(
            "  phasing: "
            f"phase_blocks={phase['phase_blocks']}, "
            f"switch_errors={phase['switch_errors']}, "
            f"flip_errors={phase['flip_errors']}"
        )
    if "superclusters" in metrics:
        print(f"  superclusters: {metrics['superclusters']}")

    print("\nOutput comparison:")
    output = summary["outputs"]
    print(f"  verdict: {'PASS' if output['pass'] else 'FAIL'}")
    for file_result in output["files"]:
        status = file_result["status"]
        print(f"  {status:>20}  {file_result['file']}")
        if "diff" in file_result:
            for line in file_result["diff"]:
                print(f"    {line}")
            break


def main() -> int:
    args = parse_args()
    baseline_time_path = args.baseline_time or (args.baseline_dir / "time.txt")
    run_time_path = args.run_time or (args.run_dir / "time.txt")

    outputs = compare_outputs(
            args.baseline_dir, args.run_dir, args.prefix, args.max_diff_lines)
    baseline_time = parse_time_file(baseline_time_path)
    run_time = parse_time_file(run_time_path)
    summary = {
        "baseline_dir": str(args.baseline_dir),
        "run_dir": str(args.run_dir),
        "prefix": args.prefix,
        "outputs": outputs,
        "time": summarize_time_delta(baseline_time, run_time),
        "metrics": headline_metrics(args.run_dir, args.prefix),
    }

    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        render_human(summary)

    return 0 if outputs["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
