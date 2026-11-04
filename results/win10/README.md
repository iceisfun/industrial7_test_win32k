Corpus results on genuine Windows 10 22H2 (build 19045), all 86 areas except the 18 dx_* areas (complete run f2a).
Modes: adm = elevated Administrator token, std = unprivileged (restricted) token in the same interactive session, user = a real
standard account. Layout: `adm/`, `std/`, `user/` each hold summary.tsv (per-area counts and status), failing_tests.tsv (every
test that did not pass, with the detail line printed before its result), failures.tsv (the FAIL/SKIP detail lines). Raw logs are not committed.

Totals per mode: 86 areas, 16211 tests. adm 16172 pass, 0 fail, 39 skip; std 16110 pass, 0 fail, 101 skip; user 16110 pass,
0 fail, 101 skip. Every skip has a stated reason in failing_tests.tsv (see docs/WIN10_NOTES.md). DETERMINISM.md has the two-run
comparison (three timing-dependent differences in a second run) and determinism.tsv lists the compared groups. results/STATS.md
has the corpus-wide numbers. Regenerate with tools/win10_summarize.py from the COM1 logs of a run.
