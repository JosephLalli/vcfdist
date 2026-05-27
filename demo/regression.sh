#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

VCFDIST=${VCFDIST:-"${REPO_ROOT}/src/vcfdist"}
EXPECTED_DIR=${EXPECTED_DIR:-"${SCRIPT_DIR}/results"}

if [[ -z "${OUT_DIR:-}" ]]; then
    OUT_DIR=$(mktemp -d "${TMPDIR:-/tmp}/vcfdist-demo-regression.XXXXXX")
    CLEAN_OUT=1
else
    mkdir -p "${OUT_DIR}"
    CLEAN_OUT=0
fi

cleanup() {
    if [[ "${CLEAN_OUT}" == "1" && "${KEEP_OUT:-0}" != "1" ]]; then
        rm -rf "${OUT_DIR}"
    fi
}
trap cleanup EXIT

if [[ ! -x "${VCFDIST}" ]]; then
    echo "vcfdist executable not found or not executable: ${VCFDIST}" >&2
    echo "Build it first with: make -C src -j24" >&2
    exit 2
fi

rm -rf "${OUT_DIR:?}/"*

(
    cd "${SCRIPT_DIR}"
    "${VCFDIST}" \
        query.vcf \
        nist-v4.2.1_chr1_5Mb.vcf.gz \
        GRCh38_chr1_5Mb.fa \
        -b nist-v4.2.1_chr1_5Mb.bed \
        -p "${OUT_DIR}/" \
        -v 0 \
        > "${OUT_DIR}/stdout.log" \
        2> "${OUT_DIR}/stderr.log"
)

stable_files=(
    phase-blocks.tsv
    phasing-summary.tsv
    precision-recall-summary.tsv
    precision-recall.tsv
    query.tsv
    superclusters.tsv
    switchflips.tsv
    truth.tsv
)

for file in "${stable_files[@]}"; do
    diff -u "${EXPECTED_DIR}/${file}" "${OUT_DIR}/${file}"
done

python3 - "${EXPECTED_DIR}/summary.vcf" "${OUT_DIR}/summary.vcf" <<'PY'
import sys
from pathlib import Path

def normalized(path: str) -> list[str]:
    lines = []
    for line in Path(path).read_text().splitlines():
        if line.startswith("##fileDate=") or line.startswith("##CL="):
            continue
        lines.append(line)
    return lines

expected_path, actual_path = sys.argv[1:3]
expected = normalized(expected_path)
actual = normalized(actual_path)
if expected != actual:
    print("summary.vcf differs after removing volatile ##fileDate and ##CL headers", file=sys.stderr)
    for i, (exp, act) in enumerate(zip(expected, actual), start=1):
        if exp != act:
            print(f"first difference at normalized line {i}", file=sys.stderr)
            print(f"expected: {exp}", file=sys.stderr)
            print(f"actual:   {act}", file=sys.stderr)
            break
    if len(expected) != len(actual):
        print(f"line count differs: expected {len(expected)}, actual {len(actual)}", file=sys.stderr)
    sys.exit(1)
PY

python3 - "${OUT_DIR}" <<'PY'
import csv
import sys
from pathlib import Path

out = Path(sys.argv[1])
with (out / "precision-recall-summary.tsv").open() as fh:
    pr_rows = list(csv.DictReader(fh, delimiter="\t"))
all_none = next(r for r in pr_rows if r["VAR_TYPE"] == "ALL" and r["THRESHOLD"] == "NONE")
with (out / "phasing-summary.tsv").open() as fh:
    phase = next(csv.DictReader(fh, delimiter="\t"))
with (out / "superclusters.tsv").open() as fh:
    superclusters = sum(1 for _ in fh) - 1

print(
    "PASS demo regression: "
    f"superclusters={superclusters}, "
    f"ALL_TP={all_none['TRUTH_TP']}, "
    f"ALL_FN={all_none['TRUTH_FN']}, "
    f"ALL_FP={all_none['QUERY_FP']}, "
    f"switch_errors={phase['SWITCH_ERRORS']}, "
    f"flip_errors={phase['FLIP_ERRORS']}"
)
PY
