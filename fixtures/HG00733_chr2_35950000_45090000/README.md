# HG00733 chr2 vcfdist regression fixture

This is a GRCh38 vcfdist regression/performance fixture built from real phased
1KGP query data and HPRC v2.0 truth data for sample `HG00733`. The inputs are
pre-subset to `chr2:35,950,000-45,090,000`, a 9.14 Mb window chosen because it
contains several expensive natural superclusters rather than one isolated hot
supercluster.

## Files

- `query.1kgp.bcf`: HG00733 query callset over `chr2:35,950,000-45,090,000`.
- `truth.hprc.bcf`: HG00733 HPRC v2.0 truth callset over the same window.
- `*.bcf.csi`: CSI indexes for the query and truth BCFs.
- `region.bed`: 0-based BED interval for the fixture window
  (`chr2\t35949999\t45090000`).
- `vcfdist.*`: archived output files from the current 1-thread baseline run.
  Validate timed output trees against these files before recording performance
  results.

Original local sources:

- Query source: `HG00733.chr2.query.bcf`
- Truth source: `HG00733.chr2.truth.bcf`
- Reference: `/mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta`

## Why This Test Exists

The earlier 55 kb chr2 fixture had one dominant supercluster and therefore did
not scale with `--max-threads`: one worker handled nearly all precision/recall
work while the other workers finished tiny superclusters quickly. This expanded
fixture keeps the original hot supercluster near `chr2:45,079,682-45,084,505`
and adds nearby high-cost superclusters near:

- `chr2:36,181,890-36,184,941` (`3051` bases, `122` variants),
- `chr2:41,504,183-41,507,559` (`3376` bases, `38` variants),
- `chr2:38,710,998-38,713,442` (`2444` bases, `26` variants),
- `chr2:35,956,798-35,957,814` (`1016` bases, `112` variants), and
- `chr2:44,588,230-44,589,282` (`1052` bases, `42` variants).

The fixture exercises:

- full-reference FASTA loading with pre-subset BCF inputs;
- `biwfa` clustering/reclustering and realignment;
- precision/recall work that can be distributed across multiple costly
  superclusters;
- deterministic flip-error reporting.

## Run Command

From the vcfdist repository root:

```bash
THREADS=64
OUT_ROOT=out/opencode-runtime/HG00733_chr2_35950000_45090000_current_threads/threads_${THREADS}
rm -rf "${OUT_ROOT}"
mkdir -p "${OUT_ROOT}"

/usr/bin/time -v -o "${OUT_ROOT}/time.txt" \
  src/vcfdist \
  fixtures/HG00733_chr2_35950000_45090000/query.1kgp.bcf \
  fixtures/HG00733_chr2_35950000_45090000/truth.hprc.bcf \
  /mnt/ssd/lalli/phasing_T2T/GRCh38_full_analysis_set_plus_decoy_hla.uppercase.fasta \
  --max-threads "${THREADS}" \
  --credit-threshold 1 \
  --max-supercluster-size 10000 \
  --realign-query \
  --realign-truth \
  --cluster biwfa \
  -p "${OUT_ROOT}/vcfdist." \
  > "${OUT_ROOT}/vcfdist.stdout" \
  2> "${OUT_ROOT}/vcfdist.stderr"
```

The command intentionally uses the already-subset BCF inputs and does not pass
`-b`; changing this command changes the measured reference-loading behavior and
requires recording a new baseline.

## Current Timings

Measured on commit `2344986` with native `src/vcfdist` version `2.6.4` on a
2-socket AMD EPYC 7713 host (`256` logical CPUs, `2` NUMA nodes). The timed
output trees are under
`out/opencode-runtime/HG00733_chr2_35950000_45090000_current_threads/`.

| Threads | Wall clock | User s | System s | CPU | Max RSS KB | PR timer s |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 4:01.90 | 229.79 | 12.04 | 99% | 3393536 | 151.881 |
| 2 | 2:35.06 | 235.01 | 12.83 | 159% | 3391488 | 88.807 |
| 8 | 1:53.30 | 240.98 | 14.49 | 225% | 3548160 | 47.697 |
| 32 | 1:52.69 | 239.66 | 15.50 | 226% | 3604552 | 47.020 |
| 64 | 1:53.75 | 240.36 | 16.52 | 225% | 3564292 | 47.572 |

Each run matched the archived fixture outputs. Validation logs are stored as
`compare_to_fixture.txt` in each timed output directory. The 1-thread to 8-thread
improvement confirms that this fixture exposes parallel precision/recall work;
8, 32, and 64 threads are similar because only a small number of superclusters
dominate this window.

## Expected Results

Expected precision/recall summary for `NONE Q >= 0`:

```text
TYPE   TRUTH_TP  QUERY_TP  TRUTH_FN  QUERY_FP  PREC      RECALL    F1_SCORE
SNP    18307     18307     865       148       0.991980  0.954882  0.973078
INDEL  3401      3401      1218      119       0.966193  0.736307  0.835729
SV     0         0         65        2         0.000000  0.000000  0.000000
ALL    21708     21708     2148      269       0.987760  0.909960  0.947265
```

Expected phasing summary:

```text
PHASE_BLOCKS=1
SWITCH_ERRORS=0
FLIP_ERRORS=5
```

Expected supercluster behavior:

- `12692` total superclusters.
- Largest supercluster has `4823` bases.
- Largest supercluster has `122` variants.
- Average supercluster size is `5.905` bases and `3.611` variants.

Expected warnings:

- `PS` tag is missing from both QUERY and TRUTH headers, so vcfdist assumes one
  phase set per contig.
- `2` QUERY overlapping variants are skipped.
- `14` QUERY CPX variants are split into INS + DEL.
- `33` TRUTH variants with unknown alleles are skipped.
- `2` TRUTH large variants are skipped.
- `129` TRUTH overlapping variants are skipped.
- `191` TRUTH CPX variants are split into INS + DEL.
