# Testing and Benchmarking

This file defines the correctness regression gate and benchmark protocol used by the performance refactoring harness. The demo fixture remains the mandatory correctness gate because it has superclusters and nonzero error counts. Every recorded performance benchmark must also validate the exact output tree produced by the timed `vcfdist` invocation, so a speed measurement is never accepted without a same-run output-validity verdict.

## Correctness regression gate

The mandatory correctness gate is the bundled demo regression:

```bash
make -C src -j24
bash demo/regression.sh
```

`demo/regression.sh` runs the local `src/vcfdist` binary on the chr1 5 Mb demo inputs and compares the generated report files against the checked-in `demo/results/` baseline. It compares the stable TSV outputs byte-for-byte and compares `summary.vcf` after removing volatile `##fileDate` and `##CL` header lines. It does not compare `parameters.txt`, `pr_plot.png`, or timestamped stderr logs.

Demo inputs:

- `demo/query.vcf`
- `demo/nist-v4.2.1_chr1_5Mb.vcf.gz`
- `demo/GRCh38_chr1_5Mb.fa`
- `demo/nist-v4.2.1_chr1_5Mb.bed`

Expected baseline coverage from `demo/results/`:

- Superclusters: 6037
- SNPs: 8222 TP, 1 FN, 2 FP
- INDELs: 874 TP, 51 FN, 12 FP
- SVs: 0 TP, 0 FN, 0 FP
- All variants: 9096 TP, 52 FN, 14 FP
- Phase blocks: 1
- Switch errors: 3
- Flip errors: 13

This fixture is short, but it is the best bundled regression check because it exercises supercluster reporting, genotype error counting, switch error counting, and flip error counting.

## Performance smoke fixture

The bundled chr22 fixture is derived from the test set `HG00733_chr22_32000000_37000000_phaseflip`. It is built around sample `HG00733` on `chr22` / `GRCh38` and is retained as a provisional performance smoke benchmark only.

The query and truth files are pre-subset to the region `chr22:32,000,000-37,000,000` because `vcfdist -b/--bed` filters variants after input parsing rather than using the VCF index to seek by region. The span includes the intentional phase flips added to exercise switch-error detection. This chr22 fixture is not the correctness gate: it has baseline genotype, switch, and flip errors (see expected metrics below) and does exercise error-count preservation.

If this fixture is too small to keep the target core count busy, add or reference a larger non-pathological benchmark tier in `docs/benchmark-progress.json` before making high-core scaling claims.

Inputs:

- `fixtures/HG00733_chr22_32000000_37000000_phaseflip/query.1kgp.phaseflip.bcf`
- `fixtures/HG00733_chr22_32000000_37000000_phaseflip/truth.hprc.bcf`
- Reference FASTA: `/mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta` (must be available at this path)
- `fixtures/HG00733_chr22_32000000_37000000_phaseflip/region.bed`

Indexes are included for the query and truth files, and the reference FASTA has its `.fai` alongside it (if needed, index it with `samtools faidx`).

Origin:

- Sample: `HG00733`
- Genome build: `GRCh38`
- Contig: `chr22`
- Subset window: `chr22:32,000,000-37,000,000`
- BED interval: `chr22 31999999 37000000` (0-based, half-open)
- Bundled query records: 8309
- Bundled truth records: 9074
- Reference scope: full `GRCh38` FASTA retained so absolute coordinates do not need rebasing
- Reference source: `GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta`
- Truth source: `truth.hprc.bcf` (HPRC v2.0)
- Query source: `query.1kgp.phaseflip.bcf` (1KGP with controlled phase flips)

## HG00733 chr2 regression timing fixture

The larger bundled chr2 fixture is `HG00733_chr2_35950000_45090000`. It is built
around sample `HG00733` on `chr2` / `GRCh38` and is the current repo-local
regression timing fixture for runs that should expose parallel precision/recall
work across multiple natural superclusters.

The query and truth files are pre-subset to the region
`chr2:35,950,000-45,090,000`. The selected window includes the original
expensive natural supercluster near `chr2:45,079,682-45,084,505` plus other
high-cost superclusters near `chr2:36,181,890-36,184,941`,
`chr2:41,504,183-41,507,559`, and `chr2:38,710,998-38,713,442`. This makes the
fixture useful for checking that independent superclusters can be processed in
parallel while preserving deterministic flip-error reporting.

Inputs:

- `fixtures/HG00733_chr2_35950000_45090000/query.1kgp.bcf`
- `fixtures/HG00733_chr2_35950000_45090000/truth.hprc.bcf`
- Reference FASTA: `/mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta` (must be available at this path)
- `fixtures/HG00733_chr2_35950000_45090000/region.bed` for provenance; the recorded command uses the already-subset BCFs and does not pass `-b`

Indexes are included for the query and truth files, and the reference FASTA has
its `.fai` alongside it. The archived `vcfdist.*` files in the fixture directory
are the expected output baseline for the command documented in the fixture README.
A `time.txt` from the canonical threads=64 run is also bundled; `compare_vcfdist_runs.py`
uses it to report runtime and RSS deltas when validating new runs against the fixture.

Origin:

- Sample: `HG00733`
- Genome build: `GRCh38`
- Contig: `chr2`
- Subset window: `chr2:35,950,000-45,090,000`
- BED interval: `chr2\t35949999\t45090000` (0-based, half-open)
- Bundled query records: 266435
- Bundled truth records: 146335
- Reference scope: full `GRCh38` FASTA retained so absolute coordinates do not need rebasing
- Reference source: `GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta`
- Truth source: `truth.hprc.bcf` (HPRC v2.0)
- Query source: `query.1kgp.bcf` (1KGP)

Current recorded native timings for thread counts `1, 2, 8, 32, 64` live in
`docs/benchmark-progress.json`. The output artifact root for the current sweep
is `out/opencode-runtime/HG00733_chr2_35950000_45090000_current_threads/`.
Each timed output tree was validated against the archived fixture outputs with
`tools/compare_vcfdist_runs.py --prefix vcfdist.`.

## HG00733 full-chr2 canonical performance benchmark

**This is the canonical fixture for measuring wall-clock speedup.** Use it for
every performance-optimization slice, not the windowed chr2 or chr22 fixtures.
The windowed fixtures are too short to saturate high core counts and do not
expose phases (clustering, realigning) that dominate full-chromosome runs.

The longest-running bundled fixture is `HG00733_chr2_full`. It covers the complete
`chr2` arm for sample `HG00733` on `GRCh38` and is the repo-local fixture for
evaluating vcfdist performance under production-scale input sizes and non-default
algorithm parameters.

The query and truth files are the full per-sample chr2 callsets with no region
subsetting. The selected parameter set (`--credit-threshold 0.7`,
`--max-supercluster-size 20000`, `--largest-variant 800`) differs materially from
the other chr2 fixtures and exercises a qualitatively different workload: lower
credit threshold permits more credit assignments to propagate, larger supercluster
cap allows more variants to be processed in a single cluster, and a higher variant
size limit increases the number of SVs evaluated.

Inputs:

- `fixtures/HG00733_chr2_full/query.1kgp.bcf`
- `fixtures/HG00733_chr2_full/truth.hprc.bcf`
- Reference FASTA: `/mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta` (must be available at this path)
- `fixtures/HG00733_chr2_full/region.bed` for provenance; the recorded command uses the already-subset BCFs and does not pass `-b`

Indexes are included for the query and truth files, and the reference FASTA has
its `.fai` alongside it. The archived `vcfdist.*` files in the fixture directory
are the expected output baseline for the command documented in the fixture README.
A `time.txt` from the canonical threads=64 run is also bundled; `compare_vcfdist_runs.py`
uses it to report runtime and RSS deltas when validating new runs against the fixture.

Origin:

- Sample: `HG00733`
- Genome build: `GRCh38`
- Contig: `chr2`
- Subset window: full chr2 (`chr2:10,000-242,183,311`)
- BED interval: `chr2\t0\t242193529` (0-based, GRCh38 chr2 length; provenance only)
- Bundled query records: 6088598
- Bundled truth records: 3403777
- Reference scope: full `GRCh38` FASTA
- Reference source: `GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta`
- Truth source: `truth.hprc.bcf` (HPRC v2.0)
- Query source: `query.1kgp.bcf` (1KGP)

The baseline timing for threads=64 and all current recorded runs live in
`docs/benchmark-progress.json` under the `HG00733_chr2_full` fixture entry. The
output artifact root is `out/opencode-runtime/HG00733_chr2_full_threads_<N>/`.
Each timed output tree must be validated against the archived fixture outputs with
`tools/compare_vcfdist_runs.py --prefix vcfdist.` before recording performance
results.

### Canonical timed command

```bash
THREADS=64
OUT=out/opencode-runtime/HG00733_chr2_full_threads_${THREADS}
mkdir -p "$OUT"

/usr/bin/time -v -o "$OUT/time.txt" \
    ./src/vcfdist \
    fixtures/HG00733_chr2_full/query.1kgp.bcf \
    fixtures/HG00733_chr2_full/truth.hprc.bcf \
    /mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta \
    -t "${THREADS}" \
    --credit-threshold 0.7 \
    --max-supercluster-size 20000 \
    --largest-variant 800 \
    --realign-query \
    --realign-truth \
    -p "$OUT/vcfdist." \
    -v 0

python3 tools/compare_vcfdist_runs.py \
    fixtures/HG00733_chr2_full "$OUT" --prefix vcfdist.
```

## VCF/BCF inspection

`bcftools` is available in the benchmark environment. Use `bcftools view` for read-only inspection (headers, samples, contigs, region counts). `vcfdist` evaluates one sample at a time, so multi-sample panels must be inspected for sample names and then subset to one sample before they are used as benchmark inputs:

```bash
bcftools view -h input.bcf
bcftools query -l input.bcf
bcftools view -H -r chr22 input.bcf | wc -l
```

Do not run commands that write to or rewrite fixture files (`-o`, `index`, `norm`, `sort`, `reheader`, etc.). New artifacts must land under the repository artifact tree, never overwrite checked-in inputs.

## Full-chr22 stress candidate

For a larger benchmark tier that stresses the supercluster/precision-recall path
more than the bundled 5 Mb smoke fixture, use sample `HG00733` on full `chr22` in
the local CHM13 dataset under `/mnt/ssd/lalli/phasing_T2T`. The source panels are
multi-sample, so create single-sample benchmark inputs under ignored `out/`
before invoking `vcfdist`.

Source inputs:

- Query panel: `/mnt/ssd/lalli/phasing_T2T/testing_phasing_params2/phased_CHM13v2.0_panel_true_false_0.05_false_1_CHM13v2.0/1KGP.CHM13v2.0.chr22.recalibrated.snp_indel.pass.phased.native_maps.3202.bcf`
- Truth panel: `/mnt/ssd/lalli/phasing_T2T/hprc.hgsvc.chr22.norm.0.05missingcutoff.annotated.sorted.atomized.no_svs.bcf`
- Reference FASTA: `/mnt/ssd/lalli/phasing_T2T/chm13v2.0_maskedY_rCRS.uppercase.fasta`
- Reference index: `/mnt/ssd/lalli/phasing_T2T/chm13v2.0_maskedY_rCRS.uppercase.fasta.fai`
- Full-chr22 BED interval: `chr22 0 51324926`

Prepare repo-local single-sample inputs:

```bash
mkdir -p out/opencode-runtime/full_chr22_chm13_hg00733/inputs
python3 - <<'PY'
from pathlib import Path
base = Path('out/opencode-runtime/full_chr22_chm13_hg00733/inputs')
(base / 'chr22.full.bed').write_text('chr22\t0\t51324926\n')
PY

bcftools view --threads 4 -r chr22 -s HG00733 -c 1:nref -a -Ob \
    -o out/opencode-runtime/full_chr22_chm13_hg00733/inputs/query.1kgp.HG00733.chr22.bcf \
    /mnt/ssd/lalli/phasing_T2T/testing_phasing_params2/phased_CHM13v2.0_panel_true_false_0.05_false_1_CHM13v2.0/1KGP.CHM13v2.0.chr22.recalibrated.snp_indel.pass.phased.native_maps.3202.bcf
bcftools index -f out/opencode-runtime/full_chr22_chm13_hg00733/inputs/query.1kgp.HG00733.chr22.bcf

bcftools view --threads 4 -r chr22 -s HG00733 -c 1:nref -a -Ob \
    -o out/opencode-runtime/full_chr22_chm13_hg00733/inputs/truth.hprc.HG00733.chr22.bcf \
    /mnt/ssd/lalli/phasing_T2T/hprc.hgsvc.chr22.norm.0.05missingcutoff.annotated.sorted.atomized.no_svs.bcf
bcftools index -f out/opencode-runtime/full_chr22_chm13_hg00733/inputs/truth.hprc.HG00733.chr22.bcf

bcftools reheader -f /mnt/ssd/lalli/phasing_T2T/chm13v2.0_maskedY_rCRS.uppercase.fasta.fai \
    -o out/opencode-runtime/full_chr22_chm13_hg00733/inputs/query.1kgp.HG00733.chr22.reheader.bcf \
    out/opencode-runtime/full_chr22_chm13_hg00733/inputs/query.1kgp.HG00733.chr22.bcf
bcftools index -f out/opencode-runtime/full_chr22_chm13_hg00733/inputs/query.1kgp.HG00733.chr22.reheader.bcf

bcftools reheader -f /mnt/ssd/lalli/phasing_T2T/chm13v2.0_maskedY_rCRS.uppercase.fasta.fai \
    -o out/opencode-runtime/full_chr22_chm13_hg00733/inputs/truth.hprc.HG00733.chr22.reheader.bcf \
    out/opencode-runtime/full_chr22_chm13_hg00733/inputs/truth.hprc.HG00733.chr22.bcf
bcftools index -f out/opencode-runtime/full_chr22_chm13_hg00733/inputs/truth.hprc.HG00733.chr22.reheader.bcf
```

The `reheader` step is required because the phased query panel's `chr22` contig
header does not include a `length`; `vcfdist` requires contig lengths in the VCF
header. A 64-thread smoke run of this prepared dataset completed in `0:08.18`
with peak RSS `1,270,620 KB`, `34,501` superclusters, ALL/NONE `59,426` truth TP,
`59,428` query TP, `22,781` FN, `5,748` FP, `2` switch errors, and `8` flip
errors. This is useful for development smoke testing, but it is still short;
record a baseline thread sweep before using it for final scaling claims.

## Baseline

Authoritative performance baseline version: `v2.6.4`.

Release image for provenance checks:

```text
timd1/vcfdist:v2.6.4
```

Recorded time and RSS for the chr22 fixture come from timing the `vcfdist`
process directly with native `/usr/bin/time -v -o ... ./src/vcfdist ...`. Do
not record RSS from host `/usr/bin/time -v docker run ...`; that measures the
Docker client rather than `vcfdist`. A Docker benchmark is valid only if the
timing tool runs around `vcfdist` inside the container, or if another mechanism
directly reports the `vcfdist` process RSS.

The timed `vcfdist` process must write a complete output tree. That same tree is
the one validated against the archived baseline output tree for the same
fixture, command, and thread count. Do not time one invocation and validate a
second invocation.

## Provisional chr22 performance command

Run from the repository root. Set `THREADS` to the value being measured. The
command below is the only timed interval. Keep the `/usr/bin/time -v` output in
a file and copy only the reported elapsed time and maximum resident set size
into `docs/benchmark-progress.json`.

```bash
THREADS=64
OUT_ROOT=/tmp/opencode/vcfdist-baseline-chr22-v2.6.4-native-time-run/threads_${THREADS}
rm -rf "${OUT_ROOT}"
mkdir -p "${OUT_ROOT}"

/usr/bin/time -v -o "${OUT_ROOT}/time.txt" \
    ./src/vcfdist \
    fixtures/HG00733_chr22_32000000_37000000_phaseflip/query.1kgp.phaseflip.bcf \
    fixtures/HG00733_chr22_32000000_37000000_phaseflip/truth.hprc.bcf \
    /mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta \
    -b fixtures/HG00733_chr22_32000000_37000000_phaseflip/region.bed \
    -p "${OUT_ROOT}/vcfdist." \
    -t "${THREADS}" \
    --credit-threshold 0.700000 \
    --max-supercluster-size 1000 \
    --largest-variant 500 \
    --realign-query \
    --realign-truth \
    --cluster size 100 \
    -v 0 \
    > "${OUT_ROOT}/vcfdist.stdout" \
    2> "${OUT_ROOT}/vcfdist.stderr"
```

Note: The command uses the parameters from the test set's `vcfdist.parameters.txt` (credit-threshold 0.7, max-supercluster-size 1000, largest-variant 500, cluster size 100). Do not adjust these parameters for baseline/branch comparisons unless the design note explicitly says so.

For branch measurements, replace `./src/vcfdist` with the branch binary under test. Keep the command, inputs, output-root pattern, and thread count identical between baseline and branch runs.

## Timed-output validation

A recorded performance benchmark is the pair of:

1. one `vcfdist` invocation timed by `/usr/bin/time -v`; and
2. validation of the output files written by that exact invocation.

Run output validation immediately after `/usr/bin/time` exits. The validation
runtime is not part of the timed interval and must not be folded into the
wall-clock value copied from `time.txt`.

For baseline-versus-branch comparisons, compare `${OUT_ROOT}/vcfdist.*` from the
branch run against an archived baseline output tree for the same fixture,
command, and matching thread count when available. The current `v2.6.4` baseline
artifact directories are recorded in `docs/benchmark-progress.json` and are the
comparison source for branch runs unless a design note records an approved
alternative. If establishing a new baseline, archive its timed output tree
before measuring the branch.

The default pass/fail scope is the stable report outputs: precision/recall TSVs,
query/truth TSVs, superclusters, phase-blocks, phasing summary, switch/flip
records, and emitted VCF report files. Normalize only documented volatile VCF
headers such as `##fileDate` and `##CL`. Treat `time.txt`, stdout/stderr logs,
and command/path fields in `parameters.txt` as provenance unless the design note
explicitly makes them part of the stable diff. Any ignored or normalized fields
must be listed in the verification note.

Do not use the demo regression, an untimed rerun, stdout summaries, or extracted
metrics alone as a substitute for validating the exact timed performance output
tree. A performance record is incomplete unless it has an output-diff verdict
for that tree.

### Saved run-comparison tool

Use `tools/compare_vcfdist_runs.py` for the repeated baseline-versus-branch
checks that pair output validation with runtime/RSS review. The tool compares
the stable vcfdist output files, normalizes only `summary.vcf` headers already
treated as volatile by the demo regression (`##fileDate` and `##CL`), parses
`time.txt` files produced by `/usr/bin/time -v`, and prints the headline
genotyping/phasing metrics that must remain unchanged.

Example with repo-local artifact directories:

```bash
python3 tools/compare_vcfdist_runs.py \
    out/opencode-runtime/profile_threads_64 \
    out/opencode-runtime/branch_threads_64_v1 \
    --prefix vcfdist.
```

The command exits nonzero if any stable output differs. Runtime and maximum RSS
deltas are reported for the verification note and benchmark tracker; policy
decisions such as whether a runtime change is sufficient still come from the
refactoring plan and slice design.

## Correctness checks

The demo regression is the canonical correctness check for performance work. It
does not replace timed-output validation for the chr22 performance benchmark.

Required checks:

1. `make -C src -j24` exits successfully.
2. `bash demo/regression.sh` exits successfully.
3. The demo regression output files match the checked-in `demo/results/` baseline under the script's comparison rules.
4. The measured genotype, switch, and flip error counts are unchanged.

The regression script writes to a temporary directory by default. To keep outputs for inspection:

```bash
KEEP_OUT=1 OUT_DIR=/tmp/vcfdist-demo-regression bash demo/regression.sh
```

When write access is intentionally limited to the repository tree, set `OUT_DIR`
under ignored `out/`, for example
`OUT_DIR=$PWD/out/opencode-runtime/demo-regression`.

Archive baseline and branch output trees separately before diffing. Do not overwrite the only copy of a baseline result.

## Performance checks

Record the `/usr/bin/time -v` wall-clock runtime, maximum resident set size, and
same-run output-diff verdict for each benchmark run. Time/RSS values in
`docs/benchmark-progress.json` must be copied directly from `/usr/bin/time -v`
output, not inferred from stdout, shell arithmetic, Docker client RSS, or
placeholder examples.

Preferred thread sweep on capable hosts:

```text
1, 2, 8, 16, 32, 64
```

Optional NUMA stretch sweep:

```text
128, 256
```

The near-term target is efficient 64-core use. Stretch measurements help characterize NUMA behavior but are not required for every slice.

For short runs, use the median of three or more runs. The bundled chr22 fixture runs in roughly 20-30 seconds on the recorded baseline host and is not enough by itself for final 64-core scaling claims. If a run is unexpectedly under 20 seconds, stop and decide whether the fixture, command, or documentation is stale before recording scaling evidence. A single baseline/branch pair at the relevant thread count is acceptable for an intermediate smoke check, but final performance claims should repeat near-threshold results and use a benchmark tier large enough to exercise the target core count.

Per-core memory growth must stay below `3.0x` relative to baseline at the same thread count unless the design was explicitly approved for a larger increase. Compare `(branch peak RSS / threads)` to `(baseline peak RSS / threads)`; at equal thread counts this is the same as requiring branch peak RSS below `3.0x` baseline peak RSS. The previous `1.3x` cap was relaxed on 2026-05-28 in exchange for higher wall-clock parallelism on the `HG00733_chr2_full` benchmark; on that fixture the relaxed cap is `~29 GB` against the `9.68 GB` baseline.

## Smaller checks

Smaller checks are encouraged during implementation. They are not substitutes for the demo regression gate.

Useful intermediate checks:

- `make -C src -j24`
- `bash demo/regression.sh`
- `demo/demo.sh` from the `demo/` directory when manually refreshing the demo plot or visual output
- one or more small local fixtures chosen by the slice owner

The demo regression is the correctness gate. The chr22 fixture is the provisional performance smoke tier.

## Benchmark tracker

Update `docs/benchmark-progress.json` when a baseline, branch run, or slice verdict is produced.

The tracker should record:

- baseline tag and measured binary path, image tag, or digest;
- branch name and commit;
- execution environment and binary path, image tag, or digest used for the measured `vcfdist` process;
- host CPU/core count and NUMA notes;
- thread count;
- wall-clock runtime;
- maximum resident set size;
- baseline output artifact directory used for comparison;
- measured output artifact directory produced by the timed run;
- output validation command/script and log path;
- output-diff verdict;
- genotyping/switch/flip error verdict;
- whether the slice is pure refactor, performance refactor, optimization, or port/rewrite experiment.
