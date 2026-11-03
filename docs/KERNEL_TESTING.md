# Kernel-side testing

The usual user-mode recipe targets a single DLL:
`LoadLibrary -> GetProcAddress -> invoke -> check`. This corpus targets the **win32k kernel
stack** - `win32k.sys` (service-table thunks), `win32kbase.sys` and `win32kfull.sys` - which
cannot be loaded or called directly from a test. The only door into it is the syscall interface:
`win32u.dll` exports ~1300 `Nt*` stubs (`NtUser*`, `NtGdi*`, `NtDComposition*`, `NtFlipObject*`,
`NtDxgk*`, `NtModerncore*`, ...) that execute `SYSCALL` with a service number >= 0x1000
(`KiSystemCall64 -> win32k.sys -> win32kbase.sys/win32kfull.sys`).

## Two ways in, one way to check

1. **Pinpoint syscall test.** Resolve a win32u.dll `Nt*` stub directly with `GetProcAddress`
   (`U()`/`UIN("win32u.dll", ...)` for an SDK-declared one; `UDECL` + `UN` for one that is not in
   any mingw header - which is most of them) and call it. This exercises exactly one kernel entry
   point with exactly the arguments you chose, so it is the sharpest tool for parameter
   validation (`NULL`/invalid handle, bad flags, wrong object type, access checks).
2. **Userland-path test.** Call the normal `user32`/`gdi32`/`gdi32full` API. It resolves to one or
   more syscalls internally (see "finding a syscall's signature" below), and is how most state
   actually gets created (windows, DCs, GDI objects).

Either way, **the check is local state inspection**, not just a return value: after the kernel did
something, read back the TEB/PEB win32 fields, the GDI shared handle table, the USER shared
handle table (desktop heap), the kernel callback table, mapped section memory, `NtQueryObject`,
etc., and assert what a run on Windows 10 shows or a PDB struct layout proves. This is the
*weird but valuable* shape of this corpus: call through the expected userland path, then go
digging in your own process's memory for the fingerprints the kernel left behind.

## The pristine-state rule

The harness (`common/t.c`) loads **only win32u.dll** before running any test. `user32.dll` and
`gdi32.dll`/`gdi32full.dll` are loaded lazily, by the first `U()` call that needs them - which also
runs `NtUserProcessConnect` and converts the calling thread into a GUI thread on its first win32k
syscall. **Any test that wants to observe "before first GUI call" or "fresh process" state must
run in a `CHILD()`**, launched before the parent's own test loop has touched user32/gdi32 in that
child (the child entry path in `common/t.c` dispatches straight to the registered child function
and never runs the module list). Never rely on test registration order to get a "fresh" state -
tests in one exe share a process and run in registration order, so cross-test ordering is not a
substitute for a child process.

## Helper API (`common/k.h` / `common/k.c`)

Compiled into every test exe automatically (the Makefile rule for `build/<area>.exe` includes
`common/k.c`). Full documented API is in `common/k.h`; summary:

- **Safe read**: `k_try_read()`/`k_try_read_ptr/u32/u16/u8()` - `ReadProcessMemory` on
  `GetCurrentProcess()`, returns `false` instead of crashing on any bad/kernel-range pointer.
  **Every** pointer chase through the structures below must go through this, never a raw `*p`.
- **TEB/PEB**: `k_teb()`/`k_peb()`, plus named offset constants (`TEB_Win32ThreadInfo`,
  `TEB_GdiTebBatch`, `TEB_GdiBatchCount`, `TEB_Win32ClientInfo` + its `CI_*` sub-offsets,
  `PEB_KernelCallbackTable`, `PEB_GdiSharedHandleTable`, `PEB_GdiHandleBuffer`, `PEB_SessionId`,
  ...), verified against the PDB types for `_TEB`/`_PEB` (19041.2965) where a
  PDB struct exists, or by experiment on Windows 10 where it does not.
- **Syscall stubs**: `k_stub_valid()`/`k_stub_number()`/`k_stub_bytes()` decode a win32u.dll
  export's fixed prologue to get its service number without consulting the .tsv at runtime;
  `k_raw_syscall(number, a1..a8)` issues a raw `SYSCALL` bypassing win32u.dll entirely (one
  shared, self-modified trampoline - not thread-safe, single test thread only).
- **GDI handle table**: `k_gdi_cell()`/`k_gdi_index()`/`k_gdi_type_byte()` decode
  `PEB->GdiSharedHandleTable` cells the way `gdi32full!HANDLE_TO_INDEX`/`GdiGetEntry` do.
- **USER handle table**: `k_shared_info()` reads `user32!gSharedInfo`; `k_user_entry()` decodes a
  handle's desktop-heap table entry and `k_wnd_ptr()` resolves its client-side object pointer,
  following the exact fast-path logic in `user32!IsWindow`/`HMValidateHandle`.
- **Kernel callback table**: `k_cb_table()`/`k_cb_count()`, and `k_cb_hook()`/`k_cb_unhook_all()`
  to install a counting trampoline over one slot (restores the original on unhook, and at exit as
  a backstop) plus `k_cb_hit_count()`/`k_cb_last_args()`.
- **UI helpers**: `k_make_window()` (hidden, off-screen, non-activating, private class),
  `k_pump()` (bounded message pump), `k_run_gui_thread()` (per-thread GUI state in isolation).
- **Children**: `k_child_run()`/`k_child_run_ex()` launch `--child <name>`, capture its stdout
  through an inheritable pipe, relay every line to the parent's own stdout as
  `  CHILD <name>: <line>`, and return `{exit_code, relayed_lines}`. Convention: a child prints
  normal `CHECK`/`FAIL`/`NOTE` lines and **returns its own failure count as the process exit
  code** (0 = clean); the parent test does `CHECK(k_child_run(...).exit_code == 0)`.
  `k_child_run_ex()` additionally takes a `STARTUPINFOEXA` with an extended attribute list already
  filled in, for process-wide/irreversible settings (mitigation policy, etc.) that must only ever
  be set in a child.
- **Environment**: `k_status_name()`, `k_win_build()`/`k_is_19041()`, `k_integrity_level()`,
  `k_is_elevated()`, `k_session_id()`, `k_has_interactive_desktop()`.

Self-test: `tests/core_k_helpers/` exercises stub decoding, GDI cell decoding, USER entry
decoding, the callback table, safe-read-of-a-bad-pointer, and child capture/relay. Run it first
when something in `k.h` looks wrong.

## Finding a syscall's signature

`NtUser*`/`NtGdi*` signatures are **not** in any SDK header. Derive them before calling one
directly - a wrong signature can corrupt process or kernel state:

1. `tools/win32u_syscalls.tsv` gives the service number and name; `tools/win32u_syscalls.json` the same data as JSON.
2. The user-mode callers in `user32.dll`, `gdi32full.dll` and `win32u.dll` pass the argument list the
   kernel expects; read how the real caller builds it (count, widths, which arguments are pointers).
3. Public documentation of the user-mode API the syscall backs (parameter meaning, flag values) and PDB-backed
   types for `_TEB`/`_PEB`. Most `tagWND`/`_CLIENTINFO`/handle-table structures have no public layout; their
   offsets in `common/k.h` were established by experiment and are exercised by the tests.
4. The authority is a run on real Windows 10: start from the smallest probe (invalid handle, zero flags),
   see what is refused and with which error, and only then assert it. A check that fails there is a wrong test.

## Adding a test

1. Read `common/t.h` and `common/k.h` fully before writing anything; this corpus's whole point is
   not reinventing either.
2. Decide: pinpoint syscall, userland-path + state check, or both.
3. `REQUIRE(ptr)` every resolved export before calling it (missing export -> `t_skip`, not a
   crash).
4. Prefer many small `TEST()`s, table-driven for parameter vectors, 60-300 per area.
5. `CHECK` only what you verified on Windows 10 or in public documentation or PDB types; `t_note()` anything locale/SKU/hardware/
   DPI/theme/version-dependent; `t_skip()` a missing prerequisite.
6. If a test needs "before first GUI call" state or process-wide/irreversible state, write it as
   a `CHILD()` and drive it with `k_child_run()`.
7. Comments say *why* an expectation holds (the observed Windows 10 behavior, or which PDB type) -
   never where the test idea came from.

## The child reporting convention

A `CHILD(name) { ... }` body runs the same `TEST()`-style `CHECK`/`EXPECT_*` macros as a normal
test (they print directly, independent of the harness's own test loop) and returns the number of
failures it detected as its process exit code (`return 0` = all good). `k_child_run()` /
`k_child_run_ex()` capture the child's stdout, relay each line, and hand back `exit_code` for the
parent to `CHECK(... == 0)` against - a failing child fails the parent test, visibly, with the
child's own `FAIL`/`NOTE` lines inline in the parent's output.

## Wine caveats (compile/hang validation only, never the authority)

`tools/wine_run.sh build/<area>.exe [filter]` runs an area in a throw-away, headless Wine prefix
(no `DISPLAY`, hard timeout, auto cleanup) to catch crashes/hangs/build rot before anyone touches
real hardware. Wine does **not** implement `win32k.sys`: it has its own `win32u.dll` with its own
syscall numbers, its own (or absent) GDI/USER shared tables, and no PEB `GdiSharedHandleTable`/
`KernelCallbackTable` guarantees match real Windows. A check failing under Wine is **not**
evidence the test is wrong - decide from a Windows 10 run, keep the Windows-correct assertion, and guard
only the parts that would crash or hang under Wine with `t_is_wine()` + `t_skip()`/`t_note()` (see
`core_k_helpers_stub_decode_known_syscalls` and `core_k_helpers_user_entry_decode_window` for the
pattern: the syscall *numbers* are version-specific so they're `t_note()`d under Wine, and
`k_make_window()` failing under the null display driver is a clean `t_skip()`, not a crash).
Validate a pointer with `k_try_read()` before following it when chasing a Wine-specific layout
that might not exist at all - a fault must never take the whole run down.

## Scope vs the other corpora

`test_user32` and `test_gdi32_gdifull` already cover the user-mode API surface of
`user32`/`gdi32`/`gdi32full` (buffer protocols, A/W parity, flag combinations, handle lifetime as
seen from the API). **This corpus does not duplicate that.** It aims at the syscall boundary
itself and at kernel-maintained local state: parameter validation visible only at the
kernel boundary, handle-table/cell layout invariants, kernel callback delivery, and
cross-checks between "what the userland wrapper claims" and "what the kernel actually wrote into
this process". Existing tests in those corpora may be skimmed for fixtures/idioms, never for
API-level coverage to copy here.
