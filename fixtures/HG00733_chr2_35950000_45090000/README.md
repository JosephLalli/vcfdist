# HG00733 chr2 35.95-45.09 Mb regression fixture

A 9.14 Mb GRCh38 windowed slice of phased 1KGP query and HPRC v2.0 truth data for sample `HG00733`. The window is chosen because it contains several expensive natural superclusters (notably near `chr2:45,079,682-45,084,505`), which makes it useful for checking that independent superclusters can be processed in parallel while preserving deterministic flip-error reporting.

Inputs: `query.1kgp.bcf`, `truth.hprc.bcf` (with CSI indexes); `region.bed` is provenance only.

Run command, parameters, baseline timings, expected outputs, and the validation protocol live in `testing.md § HG00733 chr2 regression timing fixture`. Do not duplicate them here.
