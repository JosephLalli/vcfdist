# Testing and Benchmarking

This file defines the correctness fixture and benchmark protocol used by the performance refactoring harness. The canonical execution environment is Docker.

## Fixture

The primary real-data fixture is derived from the `GRCh38_chr21_debugging` fixture in `phasing_T2T`. It is built around the HPRC sample `HG03784` on `chr21`, so it exercises the phased-comparison path.

This bundled fixture is the canonical correctness fixture and the initial benchmark tier. If it is too small to keep 64 cores busy for a proposed scaling claim, add or reference a larger chr21 benchmark tier in `docs/benchmark-progress.json` before making that claim.

Inputs:

- `fixtures/hg03784_chr21_grch38/query.vcf.gz`
- `fixtures/hg03784_chr21_grch38/truth.bcf`
- `fixtures/hg03784_chr21_grch38/reference.fa`
- `fixtures/hg03784_chr21_grch38/region.bed`

Indexes are included for the query and truth files, and the reference FASTA has its `.fai` alongside it.

Origin:

- Sample: `HG03784`
- Genome build: `GRCh38`
- Contig: `chr21`
- Debug window: `chr21:45090000-45091000`
- Reference source: `chr21.grch38.fasta`
- Truth source: `chr21.HG03784.HPRC.bcf`
- Query source: `sample.vcf.gz`

## Baseline

Authoritative baseline version: `v2.6.4`.

Docker image for release-baseline comparisons:

```text
timd1/vcfdist:v2.6.4
```

If a local branch is benchmarked through Docker, build or tag that branch image explicitly and record the tag or image digest in `docs/benchmark-progress.json`.

## Canonical chr21 command

Run from the repository root. Set `THREADS` to the value being measured.

```bash
THREADS=64
OUT_ROOT=test_unphased_gt_testing/results/vcfdist
rm -rf "${OUT_ROOT}"
mkdir -p "${OUT_ROOT}"

/usr/bin/time -v docker run --rm \
    -v "${PWD}:/work" \
    -w /work \
    timd1/vcfdist:v2.6.4 \
    vcfdist \
    fixtures/hg03784_chr21_grch38/query.vcf.gz \
    fixtures/hg03784_chr21_grch38/truth.bcf \
    fixtures/hg03784_chr21_grch38/reference.fa \
    -b fixtures/hg03784_chr21_grch38/region.bed \
    -p "${OUT_ROOT}/" \
    -t "${THREADS}" \
    --credit-threshold 1 \
    --max-supercluster-size 10000 \
    --realign-query \
    --realign-truth \
    --cluster biwfa \
    -v 0
```

For branch measurements, replace the Docker image with the branch image under test. Keep the command, inputs, output root, and thread count identical between baseline and branch runs.

## Correctness checks

The chr21 fixture is the canonical correctness check for performance work.

Required checks:

1. The command exits successfully.
2. The output tree matches the baseline output tree byte-for-byte, or numerically under existing project tolerances where byte identity is not expected.
3. The measured genotyping error and switch error rates are unchanged.
4. Any stdout/stderr differences are explained in the verification note. Informational timing/log ordering changes are acceptable only if output files and reported metrics are unchanged.

The expected output root is:

```text
test_unphased_gt_testing/results/vcfdist/
```

Archive baseline and branch output trees separately before diffing. Do not overwrite the only copy of a baseline result.

## Performance checks

Record the `/usr/bin/time -v` wall-clock runtime and maximum resident set size for each benchmark run.

Preferred thread sweep on capable hosts:

```text
1, 8, 16, 32, 64
```

Optional NUMA stretch sweep:

```text
128, 256
```

The near-term target is efficient 64-core use. Stretch measurements help characterize NUMA behavior but are not required for every slice.

For short runs, use the median of three or more runs. The chr21 fixture can be slow; a single baseline/branch pair at the relevant thread count is acceptable for an intermediate slice, but final performance claims should repeat near-threshold results.

Per-core memory growth must stay below 30% relative to baseline at the same thread count unless the design was explicitly approved for a larger increase. Compare `(branch peak RSS / threads)` to `(baseline peak RSS / threads)`; at equal thread counts this is the same as requiring branch peak RSS below `1.3x` baseline peak RSS.

## Smaller checks

Smaller checks are encouraged during implementation. They are not substitutes for the chr21 correctness fixture.

Useful intermediate checks:

- `make -C src -j24`
- `demo/demo.sh` from the `demo/` directory
- one or more small local fixtures chosen by the slice owner

The demo fixture is fast enough for smoke testing. The chr21 fixture is the canonical correctness fixture and initial performance tier for this harness.

## Benchmark tracker

Update `docs/benchmark-progress.json` when a baseline, branch run, or slice verdict is produced.

The tracker should record:

- baseline tag or image;
- branch name and commit;
- Docker image tag or digest;
- host CPU/core count and NUMA notes;
- thread count;
- wall-clock runtime;
- maximum resident set size;
- output-diff verdict;
- genotyping/switch error verdict;
- whether the slice is pure refactor, performance refactor, optimization, or port/rewrite experiment.
