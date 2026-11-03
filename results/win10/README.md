Corpus results on genuine Windows 10 22H2 (build 19045), all 85 areas except the 18 dx_* areas.
Modes: adm = elevated Administrator token, std = unprivileged token in the same interactive session.
Layout: `adm/` and `std/` hold the 70 original areas after the determinism pass; `x_areas/adm` and `x_areas/std` hold the
14 x_* areas. Per mode: summary.tsv (per-area counts and status), failing_tests.tsv (every test that did not pass, with the
detail line printed before its result), failures.tsv (the FAIL/SKIP detail lines). Raw logs are not committed.

Totals per mode: 85 areas, 16121 tests, 0 failed. Skipped: adm 51, std 113 (original areas 45 and 107, x_ areas 6 and 6).
Every skip is stable across runs and has a stated reason in failing_tests.tsv (see docs/WIN10_NOTES.md); DETERMINISM.md says
how the three-run comparison was made and determinism.tsv lists the compared groups. results/STATS.md has the corpus-wide numbers.
Regenerate with tools/win10_summarize.py from the COM1 logs of a run.
