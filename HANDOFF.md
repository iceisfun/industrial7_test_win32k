# HANDOFF: running the win32k kernel-side corpus on Windows 10

How to build the corpus, run it on a genuine Windows 10 22H2 x64 machine in both token modes, and turn the output
into per-area and per-test results. The tooling described here was verified end to end on a Windows 10 22H2 guest
(build 19045).

## Where things stand

- 103 areas, 16511 tests (`results/STATS.md`). The 18 `dx_` areas change system state and are only run on purpose.
- The 85 other areas (16131 tests) were run on Windows 10 22H2 in both modes:

  | Mode | Areas | Tests | Pass | Fail | Skip |
  |---|---|---|---|---|---|
  | adm | 85 | 16131 | 16082 | 0 | 49 |
  | std | 85 | 16131 | 16020 | 0 | 111 |

- Every skip is conditional, has a printed reason and is stable (`docs/WIN10_NOTES.md`).
- Three runs of the same build gave identical per-test results and printed lines (`results/win10/DETERMINISM.md`).
- A test that fails on genuine Windows 10 is a bug in the test (wrong expectation, environment dependence or flakiness).
  Fix the test so that it asserts what Windows does; do not weaken a check into meaninglessness.

## Prerequisites

- Linux build host with mingw-w64 (`x86_64-w64-mingw32-gcc`), GNU make and Python 3.
- A disposable Windows 10 22H2 x64 guest with: an interactive desktop session that is logged on (not locked, no
  service session), a display attached (a virtual display is fine), a local Administrator account, no domain, COM1
  that the host can capture, PowerShell 5. Network is not needed. The guest must be thrown away after each run: some
  areas touch session-wide state, and a clean guest is what makes the runs comparable.
- Something on the host that boots the guest, runs the payload's `run.cmd` at logon and keeps COM1 as `serial.log`.

## Build

    make -j8          # build/<area>.exe (static), plus build/tools/w32k_runas.exe and w32k_probe.exe

A broken build is a failed change. Every exe also accepts `--list`, which prints the registered test names without
running any of them, and a test-name substring as its only argument to run a subset.

## Pack a payload

    python3 tools/win10_pack.py <payload_dir> --all --mode both              # every non-dx_ area, adm then std
    python3 tools/win10_pack.py <payload_dir> --exes a,b,c --mode std --timeout 120
    python3 tools/win10_pack.py <payload_dir> --exes x_clipboard_state_matrix --filter keyed --mode adm

Options: `--mode std|adm|both` (default adm), `--timeout SECS` per exe (default 300; the process tree is killed and
the exe reported as TIMEOUT), `--filter SUBSTR` test-name substring passed to every exe, `--full` also send the
`RUN` lines, `--destructive` allow `dx_*` areas (refused otherwise). The payload holds `bin/` (the exes plus the two
helper exes), `list.txt`, `runner.ps1` and `run.cmd`.

Keep a payload under about ten minutes of test time, because a boot costs about a minute and a half and a hung guest
should not hold a slot for long. The standard split used for the full corpus is three payloads: the short areas,
`quota_limits` alone (about three minutes), and re-runs of anything fixed late. The slowest other areas take under a
minute.

## Run

Boot the guest with the payload available, run `run.cmd` at logon, and capture COM1 into `serial.log`.
`run.cmd` sets COM1 to 115200,n,8,1 and drives `tools/guest/runner.ps1`, which copies the exes to a local folder,
runs each (per mode) with the timeout, and frames every output line on COM1:

    W32K begin ...   W32K os ...   W32K mode <adm|std>
    W32K start <exe>   W32K out <exe> | <line>   W32K rc <exe> <code|TIMEOUT|MISSING> <secs>
    W32K done

The payload counts as finished only when `W32K done` appears; a failing or timed-out test does not change that.
Per-test output also stays in the guest; COM1 carries OK/FAIL/SKIP/NOTE/SUMMARY lines.

## Parse

    python3 tools/win10_results.py serial.log                  # one row per (mode, exe): tests failed skipped checks rc secs status
    python3 tools/win10_results.py serial.log --tests          # one row per test: mode exe test OK|FAIL|SKIP
    python3 tools/win10_results.py serial.log --failures       # the FAIL/SKIP detail lines
    python3 tools/win10_results.py serial.log --format json -o run.json
    python3 tools/test_win10_results.py                        # unit test of the parser

Status is OK (summary seen, exe returned 0 or 1), TIMEOUT, NOSUMMARY (crash), HANG (log stops), MISSING. The exit code
is 0 when the run finished and every exe is OK.

## The two modes and the restricted-admin token

- **adm** is the interactive session's own token. The built-in Administrator is not filtered by UAC there: High
  integrity, Administrators enabled, 24 privileges including the debug privilege.
- **std** runs the exe through `build/tools/w32k_runas.exe` (source `tools/guest/w32k_runas.c`). The helper takes the
  session token and derives a restricted copy with `CreateRestrictedToken`: every privilege except the change-notify
  privilege removed, Administrators and the other powerful built-in aliases set to deny-only, integrity lowered to
  Medium, owner and default DACL reset to the user SID. It starts the child with `CreateProcessAsUser` on
  `WinSta0\Default` in the same session, inside a kill-on-close job. No logon API is used, so the window station and
  desktop are the interactive ones. This is a restricted-admin token, not a real standard account: it keeps the
  administrator's user SID and profile, so behavior that depends on being a genuinely separate unprivileged account
  (own profile, own SID, no admin group membership at all) is not covered by std mode.
- Verified with `build/tools/w32k_probe.exe`, which prints the identity it runs with (session, window station and
  desktop, integrity, elevation flag, privileges, Administrators group attributes): session 1 and `WinSta0\Default` in
  both modes; std has integrity 0x2000, only the change-notify privilege, Administrators deny-only, and
  `CheckTokenMembership(Administrators)` false; adm has integrity 0x3000 and Administrators enabled.
- Known limit: the std token keeps the account's user SID and `TokenIsElevated` still reads 1 (the flag cannot be
  cleared on a derived token). No test decides on it; decide on integrity, privileges or group state.

## Summarize results (the only thing committed)

    python3 tools/win10_summarize.py results/win10 serial.log [more.log ...]

Writes `results/win10/<mode>/summary.tsv` (tests failed skipped checks rc status per area), `failing_tests.tsv`
(every test that did not pass, with the detail line printed before its result) and `failures.tsv`. When several logs are
given a later log replaces the rows of the earlier ones, so a full run can be combined with re-runs. Seconds are left
out because they vary. Lines are cut at 400 characters and checked for host identifying text. **Raw logs are never
committed**; check a summary for computer names, account names and paths before committing anything else from a run.
The `results/` folder is ignored by git for everything else, so add files there deliberately.

## Determinism

A run must give the same OK/FAIL/SKIP per test and the same printed lines every time on a fresh guest.

    python3 tools/win10_compare.py run1/serial.log run2/serial.log run3/serial.log --lines

It reports tests whose result differs between the logs and, with `--lines`, every printed line that is not present the
same number of times in all logs (exit code 0 means identical; check totals and seconds are ignored). Rules that keep
tests deterministic:

- Addresses, handles and other hexadecimal machine values are scrubbed from every line printed through `t_check`,
  `t_note`, `t_skip` and relayed child output (`t_scrub` in `common/t.c`). `W32K_RAW=1` shows the raw text.
- A value that varies per run and is not hexadecimal (process and thread ids, atoms, counters, timings, rectangles,
  slot indexes) is printed with `t_info`, which prints nothing unless `W32K_INFO=1`. Use `t_note` only for text that is
  the same on every run. If a value is machine-varying, print and assert relations over it, never the value.
- A check never depends on what else allocates in the session: slot reuse and cell-release checks accept another
  allocator taking the slot in between, and handles meant to be invalid are chosen far above any slot in use.
- A test leaves the session's shared objects as it found them. A stock icon handed to a class is destroyed with the
  class, so tests use a private copy.
- Totals of checks are never an expectation; they vary with the objects an area enumerates.

## Statistics and coverage

    python3 tools/stats.py          # rewrites results/STATS.md (and COVERAGE.md)
    python3 tools/coverage.py .     # COVERAGE.md only

`COVERAGE.md` separates three levels of evidence: direct (a test calls the `Nt*` stub), statically mapped route (a test
calls a user-mode export that reaches the syscall), and witnessed kernel entry (the syscall was seen executing; not yet
measured; note that gdi32 calls are batched until `GdiFlush`, so a routed GDI call may not enter the kernel at all).

## Triage a failure

1. Re-run only the area or test: `build\<area>.exe <name-substring>`; the failing check prints `file:line expr`.
2. Classify it. Wrong expectation: fix the test, keep the check specific. Environment, SKU, privilege, session or
   hardware dependence: make the dependent part a conditional `t_skip` with a plain reason (nothing machine specific);
   flakiness: apply the determinism rules above and run three times.
3. Record every skip and dependence in `docs/WIN10_NOTES.md`.
4. Commit the fix with a short message. The authority is always the Windows 10 result; a pass under Wine proves
   nothing about the kernel.

## Running by hand on a Windows machine

    build\<area>.exe [substr]       # one area, optional test-name filter
    tools\run_all.bat               # everything non-destructive into build\results\<area>.txt
    tools\run_all.bat /destructive  # also the dx_ areas
    python tools\summarize.py build\results --failures

Use this on a disposable machine only; the `dx_` areas switch desktops, inject input and write system parameters
(with restore), and have not been run on Windows 10 yet.
