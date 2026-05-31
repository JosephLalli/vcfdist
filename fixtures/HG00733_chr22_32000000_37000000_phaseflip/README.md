# HG00733 chr22 phaseflip smoke fixture

A small GRCh38 fixture (`chr22:32,000,000-37,000,000`) built from real phased 1KGP and HPRC v2.0 data for sample `HG00733`, with controlled phase flips inserted into the query so vcfdist reports switch errors deterministically. Used as a quick performance smoke fixture, not as the correctness gate.

Inputs: `query.1kgp.phaseflip.bcf`, `truth.hprc.bcf`, `region.bed`.

Run command, parameters, expected outputs, and the validation protocol live in `testing.md § Performance smoke fixture` and `testing.md § Provisional chr22 performance command`. Do not duplicate them here.
