# HG00733 chr22 phaseflip vcfdist test set

This is a small GRCh38 vcfdist regression/stress test built from real phased
1KGP and HPRC data, with a controlled phase perturbation added to the query so
that vcfdist reports switch errors deterministically.

## Files

- `query.1kgp.phaseflip.bcf`: query callset. Derived from phased 1KGP HG00733
  chr22 calls over `chr22:32,000,000-37,000,000`, with phased heterozygous GTs
  flipped in three dense subintervals.
- `truth.hprc.bcf`: HPRC v2.0 GRCh38 HG00733 truth callset over the same region.
- `region.bed`: 0-based BED interval for the test region.
- `vcfdist.*`: archived output files for this fixture. For recorded performance
  work, validate the output tree written by the timed run against the archived
  baseline tree named in `docs/benchmark-progress.json` / `testing.md`.

Original local sources:

- Query source: `/mnt/ssd/lalli/phasing_T2T/1kGP_high_coverage_Illumina.chr22.filtered.SNV_INDEL_SV_phased_panel.vcf.gz`
- Truth source: `/mnt/ssd/lalli/phasing_T2T/hprc-v2.0-mc-grch38.wave.vcf.gz`
- Reference: `/mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta`

## Why This Test Exists

The unmodified HG00733 1KGP-vs-HPRC slice is already a useful fast test for:

- Dense SNP and indel comparison.
- SV-sized false negatives.
- Genotyping/representation differences.
- Forced supercluster splitting.

However, vcfdist reported zero natural switch errors in the sampled regions.
To exercise the switch-error path, the query has controlled phase flips in:

- `chr22:32,290,000-32,305,000`
- `chr22:34,395,000-34,400,000`
- `chr22:35,693,000-35,701,000`

## Run Command

From the vcfdist repository root:

```bash
THREADS=4
OUT_ROOT=/tmp/opencode/HG00733_chr22_32000000_37000000_phaseflip-rerun/threads_${THREADS}
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
  --cluster size 100 \
  --max-supercluster-size 1000 \
  --largest-variant 500 \
  --realign-query \
  --realign-truth \
  -v 0 \
  > "${OUT_ROOT}/vcfdist.stdout" \
  2> "${OUT_ROOT}/vcfdist.stderr"
```

The timed command must be the invocation that creates the output tree used for
validation. Do not time one run and validate a separate rerun.

## Benchmark-output validation

After `/usr/bin/time` exits, compare `${OUT_ROOT}/vcfdist.*` against the
archived baseline output tree for the same fixture, command, and matching thread
count when available. Validation time is outside the timed interval. A
performance record is incomplete unless it records both the `/usr/bin/time -v`
wall-clock/RSS values and an output-diff verdict for the exact timed output
tree.

Normalize only documented volatile VCF headers such as `##fileDate` and `##CL`.
Treat `time.txt`, stdout/stderr logs, and command/path fields in
`parameters.txt` as provenance unless the slice design explicitly makes them
part of the stable diff.

## Expected Results

Runtime on this machine was about 23 seconds with 4 threads. For new benchmark
records, copy elapsed time and maximum resident set size directly from
`/usr/bin/time -v` output; do not use inferred or placeholder values.

```text
elapsed=0:22.73 maxrss_kb=3391488
```

Expected phasing summary:

```text
Total switch errors: 4
Total flip errors: 0
Supercluster switch error rate: 0.083735%
```

Expected switch-error locations:

```text
CONTIG  START     STOP      SWITCH_TYPE  SUPERCLUSTER  PHASE_BLOCK
chr22   32289237  32290068  SWITCH_ERR   171           0
chr22   32304851  32305664  SWITCH_ERR   194           0
chr22   35691240  35699089  SWITCH_ERR   3660          0
chr22   35700531  35700872  SWITCH_ERR   3663          0
```

Expected precision/recall summary for `NONE Q >= 0`:

```text
TYPE   TRUTH_TP  QUERY_TP  TRUTH_FN  QUERY_FP  PREC      RECALL    F1_SCORE
SNP    9143      9141      730       154       0.983432  0.926061  0.953885
INDEL  1820      1818      853       107       0.944416  0.680883  0.791284
SV     1         1         54        0         1.000000  0.018182  0.035714
ALL    10964     10960     1637      261       0.976740  0.870090  0.920335
```

Expected supercluster behavior:

- `4777` total superclusters.
- Largest supercluster: `962` bases and `117` variants.
- Multiple warnings of the form `Max supercluster size (1000) exceeded ... breaking up into ... superclusters`.

This test intentionally uses `--largest-variant 500` because vcfdist rejects
`--max-supercluster-size 1000` with the default `--largest-variant 5000`.
