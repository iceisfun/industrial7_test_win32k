# Determinism of the corpus on Windows 10 22H2 (19045)

## Method
The corpus was run on fresh Windows 10 22H2 x64 guests, adm mode first and std mode second in the same guest, three times with
the final build, and the logs were compared with `tools/win10_compare.py <log> <log> <log> --lines`: per-test results and every
printed line (OK/FAIL/SKIP detail, NOTE, SUMMARY without the check total) must be identical. The areas were run in three payloads
(the 70 short areas, `quota_limits` alone because it takes about three minutes, and re-runs of areas fixed late); the final
three comparisons report 0 differences.

## What varied and how it was fixed
- Hex addresses and handles in notes and failure messages: scrubbed in the harness (`t_scrub`); `W32K_RAW=1` shows them.
- Machine-varying decimals (process and thread ids, atoms, counters, timings, rectangles, slot indexes): printed with `t_info`,
  which prints nothing unless `W32K_INFO=1`.
- `r2_win32k_process_lifecycle_quotas`: compared the GDI cell type byte with the sign-extended top of the handle (fails when the
  uniqueness byte has its top bit set); the recycled slot of a dead process may validate to nothing or to a bitmap.
- `r2_gdi_cell_per_type`: the slot reuse byte advances once per allocation in the slot, more when another allocator takes it in
  between (the halftone palette always allocates twice); the check now accepts an advance of 1 to 4.
- `gdi_handle_table`: a deleted client object's cell may already be reused by another allocation; the check accepts a changed upper word.
- `sc_stub_layout`: which import thunks are bound into win32u when the sweep runs varies; every bound thunk must land on a
  win32u export entry (not only on the services in the known table).
- `cursor_icon_menu`: `SetMenuInfo` with the brush handle 0x1234 succeeded or failed depending on whether that slot of the
  session GDI table held a live object; the test now uses a handle far above any slot in use and expects the refusal
  (ERROR_INVALID_PARAMETER, nothing stored).
- `r2_kernel_callback_table_full`: a hook returned the shared system question icon as a class small icon; the class destroys
  its small icon with the class, which invalidated the icon for every later process of the session (class_registration and
  cursor_icon_menu then failed in std mode, after the adm pass). The test now hands over a private copy; the class takes it in both
  modes (the earlier belief that a medium-integrity process is refused was an artifact of the already invalidated icon).
- Check totals (`checks_pass`) vary by one or two in areas that enumerate system objects (window stations, modules); they are
  not compared.

## Tests that skip identically in every run
All are conditional skips with an explicit reason; the reason text is in failing_tests.tsv.

| Area | Mode | Reason |
|---|---|---|
| display_monitors_dpi | both | the guest has one monitor |
| gap_07, raw_input_touch_pointer | both | no pointer, touch or HID devices; no HID device with preparsed data |
| kbd_layouts_state | both | one keyboard layout installed; std also: GetAsyncKeyState is refused for the restricted token |
| gdi_metafile_print_neg | both | display driver without OpenGL pixel formats; one export the image does not have |
| gdi_font_metrics_text | both | the Wingdings face is not mapped on this image |
| focus_capture_attach | both | the test process is not the foreground process of the interactive session |
| gap_11 | both | nothing to query in this configuration |
| gap_09 | both | rooted/absolute opens of the window station directory are denied (std also at the directory open) |
| gdi_handle_table | both | slot reuse not observable in one case; handle counts not asserted for metafile and pattern-brush kinds; a regional-rect cell held by the handle cache |
| r2_input_pointer_rim_state | both | the denied path of input injection is unreachable because the session token holds the right |
| r2_teb_peb_field_matrix | both | DC attribute batching is not active |
| user_wnd_desktop_heap | both | HWND_TOP does not move a window owning a default IME window |
| win32k_mitigation | both | the audit creation attribute is rejected by CreateProcess (error 87) on this build: 13 tests |
| winsta_desktop_objects | std | object creation needs a high-integrity administrator token: 36 tests |
| r2_winsta_activation_group_session | std | CreateWindowStation refused for the unprivileged token: 14 tests, plus one unreadable directory |
| win32k_modules_sysinfo | std | object directories and window station creation denied for the token: 9 tests |

No test fails in any run in either mode.

## The x_ areas
The 14 x_ areas (3249 tests) were packed as one payload and run three times on fresh guests, adm mode first and std mode second in
the same guest. `tools/win10_compare.py <log> <log> <log> --lines` reports 0 differences. No area needed a fix after its first
Windows 10 run. The only varying number is the check total (`checks_pass` differs by one between runs in `x_d3dkmt_badhandle_matrix_a` and `x_menu_accel_hotkey_lifetime`), which is not
compared. The one skip group is `x_d3dkmt_badhandle_matrix_a`: 6 tests that skip in both modes because the adapter-less thunk
creates a real keyed mutex for a zero, filled or garbage block (see docs/WIN10_NOTES.md).
