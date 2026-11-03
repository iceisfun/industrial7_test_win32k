# Windows 10 notes: skips and environment dependence

The authority is genuine Windows 10 22H2 x64 (build 19045). The reference guest is a Windows 10 **Home** image
(operating system SKU 101), one interactive session, one virtual display, one monitor, no network, no pointer, touch or
HID devices, one keyboard layout, no OpenGL-capable display driver. The tests ran in two modes:

- **adm**: the session's own token, an elevated local Administrator (High integrity, Administrators enabled, 24 privileges).
- **std** (restricted-admin, not a real standard account): a restricted copy of that token in the same interactive session (Medium integrity, only the change-notify
  privilege, Administrators deny-only), see `HANDOFF.md`. `TokenIsElevated` still reads 1 on that token, so no test decides
  on it; tests decide on integrity level, privileges or the observed refusal.

Every test either asserts what Windows does or skips with a printed reason. A skip never counts as a pass: it is listed
per mode in `results/win10/*/failing_tests.tsv`. All skips below were identical in all three determinism runs. The
numbers are tests (the area header comments say what each measures).

## Skips that depend on the hardware or configuration of the guest

These are the same in both modes. A machine with the missing hardware or setting runs the test instead.

| Area | Tests | Reason (printed) | Dependence |
|---|---|---|---|
| display_monitors_dpi | 1 | needs two monitors | the guest has one monitor; the test compares monitor-from-rect choices between two |
| gap_07, raw_input_touch_pointer | 3 + 6 | no pointer devices; no HID device; no HID device with preparsed data | no mouse, touch, pen or HID device is attached; one raw-input test is the converse and skips when devices are present |
| kbd_layouts_state | 1 | only 1 layout | one keyboard layout installed; the layout-switching test needs two |
| gdi_metafile_print_neg | 3 | display driver without OpenGL pixel formats; one export the build lacks | the basic display driver reports no pixel formats, so SetPixelFormat is accepted but nothing is retained |
| gdi_font_metrics_text | 1 | face wingdings not mapped (not installed) | font set of the image |
| gap_11 | 1 | none to query | nothing to enumerate in this configuration |
| r2_teb_peb_field_matrix | 1 | no batching | GDI attribute batching is not active for the test's DC in this configuration, so the batch-dependent field cannot be exercised |
| user_wnd_desktop_heap | 1 | HWND_TOP does not move a window that owns a default IME window | a window that owns a default IME window is not moved by HWND_TOP on Windows 10; the check that needs the move is skipped |
| win32k_mitigation | 13 | no audit attribute | CreateProcess rejects the audit-only mitigation creation attribute (error 87) on this build, so the audit-mode tests cannot create their child |

## Skips that depend on the session or on what else is running

| Area | Tests | Reason (printed) | Dependence |
|---|---|---|---|
| focus_capture_attach | 1 | not foreground | the test process is not the foreground process of the interactive session; Windows decides this and the test must not steal the foreground |
| gdi_handle_table | 8 | handle count not asserted for emf, emf_dc, wmf, wmf_dc, brush_pattern, brush_dibpat; never got the same slot back; the deleted region cell is still held by the handle cache | GDI slot reuse and the per-process handle cache are timing and history dependent; only relations that hold for every outcome are asserted, the rest is skipped |
| r2_input_pointer_rim_state | 3 | input injection is granted to this process, the denied path is not reachable | the session token holds the right to inject input, so the refusal paths cannot be reached. A token without the right would run them |

## Privilege dependence (std mode only, or tests that branch on the token)

In adm mode these run; in std mode the refusal is expected, observed and the dependent part is skipped with the reason.

| Area | Tests (std) | Reason (printed) | Dependence |
|---|---|---|---|
| winsta_desktop_objects | 36 | object creation denied (error 5): needs a high-integrity administrator token | creating window stations and desktops needs a High integrity token |
| r2_winsta_activation_group_session | 14 + 1 | CreateWindowStation refused for the unprivileged token; directory not readable | same, plus one object directory that a Medium token may not read |
| win32k_modules_sysinfo | 9 | CreateWindowStation failed (5); object directories denied for this token | `\ObjectTypes`, `\Sessions\<n>`, window station directories need administrator access |
| kbd_layouts_state | 2 (plus the layout skip) | GetAsyncKeyState is refused with ERROR_ACCESS_DENIED for this (restricted) token | the restricted token cannot read the async key state; the result is the same on Windows, only the token differs |
| gap_09 | 2 in adm, 2 in std | absolute open denied (error 5); rooted open denied (error 5) in adm; cannot open the window station directory (access denied status) in std | opens of the window station directory by absolute or rooted name are refused with access denied for both tokens, and the std token is refused the directory itself |

Other tests branch on integrity or privilege without skipping: they assert the Windows 10 outcome for the token they run under
(for example the clipboard-owner and foreign-window checks pick the expectation from the integrity level). Where adm and std give
different outcomes the test names both.

## Unconditional skips by design

`x_d3dkmt_badhandle_matrix_a` skips 6 tests in both modes (`CreateKeyedMutex` and `CreateKeyedMutex2`, zero block, filled
block and garbage block): on Windows a block shaped like that creates a real keyed mutex object (the adapter-less thunk
accepts it), so the matrix cannot assert a rejection. The creation path is covered by the keyed mutex lifecycle tests of
the same area. They are reported as skipped, not passed.

## SKU, build and version dependence

- The guest is a Home SKU of 22H2 (build 19045). The tests make no Pro/Enterprise feature assumption, but they have not
  been run on another SKU; a Pro or Enterprise guest can differ in group policy defaults and installed fonts. Where the
  expectations were SKU or build dependent they print the value (as a relation, never the machine value) and compare with
  relations only.
- Build-dependent service numbers: `sc_stub_layout`, `sc_raw_syscall` and `core_k_helpers` read service numbers from the
  win32u stubs. Numbers differ between Windows builds; the tests compare against the table of this build
  (`tools/win32u_syscalls.tsv`) and must be regenerated for another build.
- Loading order: the first win32k syscall of a process without user32 loaded is fatal on Windows 10 (the callback table is
  missing and the process ends with a fatal user-callback exception). Tests that need to observe a fresh process run in
  child processes that load user32 first or install a callback table; this is a property of the build, not of a SKU.
- Mitigation policy attribute (`win32k_mitigation`, tests `attrsize_*`): the accepted payload sizes (4, 8, 16 bytes; any other size fails with
  ERROR_BAD_LENGTH) and the second-word flags used (downgrade of the dynamic code policy, indirect branch prediction restriction,
  speculative store bypass disable) are the behaviour of build 19045. The second-word flags are read back through the process
  mitigation policy API, which on 22H2 reports them regardless of CPU support; no skip was added, a later build that reads them
  differently would need these expectations revisited. Undefined second-word flags are rejected with ERROR_INVALID_PARAMETER on this build.
- `dx_` areas are guarded, change system state and have not been run on Windows 10, so they have no Windows 10 skips or
  reasons recorded yet.

## Rules for adding a skip

1. The reason string states the condition in plain words and nothing machine specific (no handles, ids, paths).
2. The condition must be detected, not assumed: probe the capability (device present, privilege held, error returned) and
   skip only when it fails. A skip that always fires is listed above as unconditional and needs a stated reason.
3. A check whose result varies from run to run is not a skip: assert the relation that always holds, or print the value
   with `t_info` (shown only with `W32K_INFO=1`).
