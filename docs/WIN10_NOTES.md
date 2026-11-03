# Windows 10 notes: skips and environment dependence

The authority is genuine Windows 10 22H2 x64 (build 19045). The reference guest is a Windows 10 **Home** image
(operating system SKU 101), one interactive session, one virtual display, one monitor, no network, a PS/2 keyboard and mouse plus one USB tablet in the raw
input list, no pointer (touch or pen) device and no other HID device, one keyboard layout, no OpenGL-capable display driver. The tests ran in three modes (the third, user, was added later and has been run on a representative subset, see the last section):

- **adm**: the session's own token, an elevated local Administrator (High integrity, Administrators enabled, 24 privileges).
- **std** (restricted-admin, not a real standard account): a restricted copy of that token in the same interactive session (Medium integrity, only the change-notify
  privilege, Administrators deny-only), see `HANDOFF.md`. `TokenIsElevated` still reads 1 on that token, so no test decides
  on it; tests decide on integrity level, privileges or the observed refusal.
- **user**: a real throwaway local standard account created by the runner inside the guest (Users group only, own SID,
  Medium integrity, `TokenIsElevated` 0, five standard privileges), started with the secondary logon service in the same
  interactive session on `WinSta0\Default`.

Every test either asserts what Windows does or skips with a printed reason. A skip never counts as a pass: it is listed
per mode in `results/win10/*/failing_tests.tsv`. All skips below were identical in all three determinism runs. The
numbers are tests (the area header comments say what each measures).

## Skips that depend on the hardware or configuration of the guest

These are the same in both modes. A machine with the missing hardware or setting runs the test instead.

| Area | Tests | Reason (printed) | Dependence |
|---|---|---|---|
| display_monitors_dpi | 1 | needs two monitors, the system has one and a virtual second display cannot be created without a display driver | the guest has one display adapter with one monitor (`EnumDisplayDevices` lists one adapter and one monitor); the test compares monitor-from-rect choices between two. A second monitor needs a display driver or hardware, which user mode cannot add, and a duplicated or mirrored topology still enumerates one monitor |
| raw_input_touch_pointer | 1 | devices present | the converse test (an empty raw input list) needs a machine with no raw input device at all; the guest lists a PS/2 keyboard and mouse and a tablet, and a device cannot be removed from user mode. It runs where the list is empty |
| gdi_metafile_print_neg | 3 | display driver without OpenGL pixel formats; one export the build lacks | the basic display driver reports no pixel formats, so SetPixelFormat is accepted but nothing is retained |
| gdi_font_metrics_text | 1 | face wingdings not mapped (not installed) | font set of the image |
| r2_teb_peb_field_matrix | 1 | no batching | GDI attribute batching is not active for the test's DC in this configuration, so the batch-dependent field cannot be exercised |
| user_wnd_desktop_heap | 1 | HWND_TOP does not move a window that owns a default IME window | a window that owns a default IME window is not moved by HWND_TOP on Windows 10; the check that needs the move is skipped |
| win32k_mitigation | 13 | no audit attribute | CreateProcess rejects the audit-only mitigation creation attribute (error 87) on this build, so the audit-mode tests cannot create their child |

## Skips that depend on the session or on what else is running

| Area | Tests | Reason (printed) | Dependence |
|---|---|---|---|
| focus_capture_attach | 1 | not foreground | the test process is not the foreground process of the interactive session; Windows decides this and the test must not steal the foreground |
| gdi_handle_table | 8 | handle count not asserted for emf, emf_dc, wmf, wmf_dc, brush_pattern, brush_dibpat; never got the same slot back; the deleted region cell is still held by the handle cache | GDI slot reuse and the per-process handle cache are timing and history dependent; only relations that hold for every outcome are asserted, the rest is skipped |
| r2_input_pointer_rim_state | 3 | input injection is granted to this process, the denied path is not reachable | the session token holds the right to inject input, so the refusal paths cannot be reached. A token without the right would run them |
| clipboard_dde | 0 | no window station/display, no window, cannot spawn child | the `renderlife` tests (WM_RENDERFORMAT / WM_RENDERALLFORMATS lifecycle, including the replaced-by-another-process and owner-process-exit arms) need a window and, for two of them, a second process. They run on every interactive Windows 10 guest (adm and std, no skip observed) and skip only without a window station or when the helper cannot be started |

## Dependences that replaced skips (synthetic input, extra layouts)

These tests used to skip on the reference guest and now run, in both modes. They build the missing precondition themselves
and put the session back as they found it.

| Area | Tests | What the test builds | Observed on Windows 10 22H2 |
|---|---|---|---|
| gap_07 | 3 | a synthetic touch device (`CreateSyntheticPointerDevice`, a second synthetic pen device where two entries are needed) when the system lists no pointer device | the device is listed by `GetPointerDevices` at once; the full read, the per-device query, the cursors and the rects work on it |
| gap_11 | 1 | the same | the enumerated-device consistency test runs on the synthetic device |
| raw_input_touch_pointer | 5 | the same, for the pointer device tests; for the two HID tests a synthetic touch device, which is a HID digitizer (usage page 0x0d, usage 4) with preparsed data in the raw input list | `GetRawInputDeviceInfo(RIDI_PREPARSEDDATA)` returns the size and the `HidP KDR` data |
| kbd_layouts_state | 1 | a second keyboard layout, loaded with `LoadKeyboardLayout` and unloaded again by the test (`kls_cleanup`) | `kbd_layouts_state_load_then_unload_restores_the_list` checks that the list is back as it was |

`common/synth.h` creates the devices. After `DestroySyntheticPointerDevice` the device can stay listed for a short time
(queries on it fail with 0x3bc3), so the helper waits until the count is back to its starting value before the next test
runs. A device name carries a counter and is never printed. Both modes can create the devices and inject (the restricted
token holds the injection right, see `r2_input_pointer_rim_state`).

New tests built on the same devices: the shape of a synthetic touch device (type, contacts, 10 properties, one cursor per
contact, a HID digitizer in the raw list), of a synthetic pen (integrated pen, 12 properties, pen and eraser cursors), the
device leaving the list after destruction, and `InjectSyntheticPointerInput` touch and pen down, update and up reaching a topmost
test window as one `WM_POINTERDOWN`, at least one `WM_POINTERUPDATE` and one `WM_POINTERUP`.

## Privilege dependence (std mode only, or tests that branch on the token)

In adm mode these run; in std mode the refusal is expected, observed and the dependent part is skipped with the reason.

| Area | Tests (std) | Reason (printed) | Dependence |
|---|---|---|---|
| winsta_desktop_objects | 36 | object creation denied (error 5): needs a high-integrity administrator token | creating window stations and desktops needs a High integrity token |
| r2_winsta_activation_group_session | 14 + 1 | CreateWindowStation refused for the unprivileged token; directory not readable | same, plus one object directory that a Medium token may not read |
| win32k_modules_sysinfo | 9 | CreateWindowStation failed (5); object directories denied for this token | `\ObjectTypes`, `\Sessions\<n>`, window station directories need administrator access |
| kbd_layouts_state | 2 (plus the layout skip) | GetAsyncKeyState is refused with ERROR_ACCESS_DENIED for this (restricted) token | the restricted token cannot read the async key state; the result is the same on Windows, only the token differs; the real standard account (user mode) is refused too |

Raw `NtUserOpenWindowStation` (gap_09): the former two skips are now passes in both modes. The cause was the test, not the token:
the name `\Windows\WindowStations\...` resolves in the system (session 0) namespace, where the interactive station is refused with
access denied for every token and every access mask (asserted as a denial: `gap9_winsta_direct_absolute_global_path_denied`,
`gap9_winsta_direct_rooted_global_directory_denied`). The session's own directory `\Sessions\<n>\Windows\WindowStations` opens
the station absolute or rooted (`gap9_winsta_direct_absolute_path`, `gap9_winsta_direct_rooted_path`; the rooted case opens the
directory with traverse access only, which both tokens are granted). A denial from the station's own ACL is built in the test
by impersonating a restricted token (`gap9_winsta_direct_open_denied_for_restricted_impersonation`, restricting SID Everyone and an
unrelated SID, absolute, rooted and user32 paths all give error 5 and succeed again after the revert), so it needs no second
account. `gap9_winsta_direct_directory_query_access_depends_on_token` branches on integrity: query access to the two window station
directories is granted at High and refused (STATUS_ACCESS_DENIED) at Medium.

Foreign-window checks use a real second process: `clipboard_dde` (t_foreign.c) and `r2_winsta_activation_group_session`
(`cwmfex_window_of_other_process_is_denied_helper_window`, which replaces the old shell-window test and its "no foreign shell window"
skip) re-launch the test exe as a helper that creates a hidden window and, for the clipboard cases, takes or holds the clipboard.
Dependence: the guest must allow a second process in the same window station (it does in both modes); each test waits up to ten
seconds for the helper window and fails (does not skip) if it never appears. These tests never touch the desktop or shell windows.

Other tests branch on integrity or privilege without skipping: they assert the Windows 10 outcome for the token they run under
(for example the window station directory query test). Where adm and std give
different outcomes the test names both.

## User mode: a real standard account

`--mode user` (HANDOFF.md) runs every exe as a genuine local standard user. The runner creates the account in the guest
(random password, Users group only), starts the exe with the secondary logon service (`CreateProcessWithLogonW`, profile
loaded, no console window) and checks with `w32k_probe.exe` that the account is a separate one, not elevated, in the
interactive window station before it trusts the mode (`W32K userkind real-user`). Facts observed on Windows 10 22H2:

- The child lands in the caller's session on `WinSta0\Default` when `lpDesktop` is left NULL. Naming `WinSta0\Default`
  explicitly makes the child hang at start-up, and `CreateProcessWithLogonW` fails with error 87 if the standard handles
  it is given are not valid. A console child gets its own visible console window unless `CREATE_NO_WINDOW` is used; that
  window is the foreground window of the process and changes the focus, foreground and handle-table tests, so user mode
  uses `CREATE_NO_WINDOW` and the process has no window of its own, as in adm and std.
- Token: Medium integrity, `TokenIsElevated` 0, elevation type default, no restriction flag, Users enabled and
  Administrators absent from the token (std has it deny-only), privileges change-notify, shutdown, undock, increase
  working set and time zone (std: change-notify only, adm: 24).
- Results on the sampled areas (29 areas, 6263 tests; see HANDOFF.md for the list): user behaves exactly like std in
  every one of them: same skips for the same reasons (winsta_desktop_objects 36, r2_winsta_activation_group_session 15,
  win32k_modules_sysinfo 9, kbd_layouts_state 2 plus the layout skip), and adm differs only in those. Input injection
  (`r2_input_pointer_rim_state`) is granted to the standard account as well, so its three denied-path skips are the same
  in all three modes. The `Create/Open WindowStation` refusal (error 5), the object directory refusals and the
  `GetAsyncKeyState` refusal are therefore properties of "not an administrator", not of the restricted token.
- What the real account adds over std that no sampled test decides on: its own user SID and profile, no Administrators
  membership at all, `TokenIsElevated` 0. A test that depends on them would branch on `CheckTokenMembership` or the
  elevation flag.

## Unconditional skips by design

`x_d3dkmt_badhandle_matrix_a` skips 6 tests in both modes (`CreateKeyedMutex` and `CreateKeyedMutex2`, zero block, filled
block and garbage block): on Windows a block shaped like that creates a real keyed mutex object (the adapter-less thunk
accepts it), so the matrix cannot assert a rejection. The creation path is covered by the keyed mutex lifecycle tests of
the same area. They are reported as skipped, not passed.

## Unreadable pointer arguments (x_dxgk_tracked_workload_vail_neg)

- The unreadable-pointer kinds of the matrix are literal addresses (address 1, a kernel address, all ones, a released
  region, the top of the user range, 0xDEADBEEF, 0x10000, 0xFFFFFFFF). A literal is only unreadable because nothing is
  mapped there, so the area also runs every thunk with a committed PAGE_NOACCESS page owned by the test (checked with
  VirtualQuery before use; a failed fixture is a failure, never a pass). On Windows 10 the thunks answer that page exactly
  as they answer the literal unreadable pointers.
- Layout dependence: `x_literal_unreadable_addresses_record_their_mapping_state` queries the mapping state of each literal
  and prints it through `t_info`. If a literal turns out to be readable memory on some machine, the matching matrix cases
  skip that kind (with a `t_info` line) and the test is reported as skipped with the count. On the guests used so far no
  literal is mapped, so nothing is skipped. The kernel-range, all-ones and top-of-user-range literals are asserted to be
  unusable on every machine.
- A synchronize-only event handle (duplicated with SYNCHRONIZE) is a valid handle of the event type: the duplicate is
  created, signalling through it fails with access denied, and the display-manager thunk answers it like any other event
  handle (type mismatch).

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
- Initialisation order (`gdi_init_order`): the fixture `w32k_gdiinit.exe` imports kernel32 and user32 only (the area checks
  its import table), so gdi32 is loaded through user32's dependency. On Windows 10 the process is already connected before
  the entry point (GDI shared handle table view, kernel callback table, win32 thread of the initial thread all present,
  GdiDCAttributeList 0x14, no GDI object yet) and a new thread has no win32 thread until its first win32k service. The
  area asserts relations (same table and callback table on every thread, object count is process wide) and never prints
  addresses or handles. If the fixture executable is not next to the test (a bare build without the tools folder) the
  tests skip with "fixture executable not available next to the test"; in a packed payload they always run.
- Mitigation policy attribute (`win32k_mitigation`, tests `attrsize_*`): the accepted payload sizes (4, 8, 16 bytes; any other size fails with
  ERROR_BAD_LENGTH) and the second-word flags used (downgrade of the dynamic code policy, indirect branch prediction restriction,
  speculative store bypass disable) are the behaviour of build 19045. The second-word flags are read back through the process
  mitigation policy API, which on 22H2 reports them regardless of CPU support; no skip was added, a later build that reads them
  differently would need these expectations revisited. Undefined second-word flags are rejected with ERROR_INVALID_PARAMETER on this build.
- `dx_` areas are guarded, change system state and have not been run on Windows 10, so they have no Windows 10 skips or
  reasons recorded yet.

## Check counts and the thunk sweep (environment dependence that is not a skip)

- Check totals are never an expectation, but they should not move for no reason. Three sources were found and removed:
  `win_position_zorder` asserted once per entry of an enumerated desktop window list (three tests), so its total followed the
  number of top-level windows on the desktop at that moment (it varied between boots and between runs of one boot by a few
  dozen); each of those loops is now one aggregate check. `clipboard_dde_listener_foreign_process_window_denied` ran one check
  more in the restricted mode (the call is refused there and the error is verified) than in the administrator mode (the call is
  accepted); it now runs one check on either outcome. Both areas now report the same total in every run and in both modes.
- Intrinsic dependence that remains: an area that asserts per enumerated system object (window stations, loaded modules) still
  has a total that follows that set; no such area is compared by total.
- `sc_stub_layout_thunk_sweep_user32_gdi32` failed once in both modes of one boot and never again (more than fifty runs of the
  whole area, first after logon, after other areas, in both modes, did not reproduce it). Not reproduced, so the root cause is
  NOT confirmed. Most likely cause: the sweep took "a thunk target inside win32u" to mean "within 0x40000 bytes above the
  win32u base", but the win32u image is only 0x22000 bytes long. System images get a base that is chosen once per boot and
  shared by both modes of that boot, so on a boot that places another module (for example KERNELBASE or msvcrt) within the
  0x1e000 bytes after win32u, a thunk into that module is judged to land in win32u and is not a win32u export entry. That fits
  "both modes failed in the same boot, later boots passed". The sweep now uses the image's own SizeOfImage, and a new test
  (`sc_stub_layout_thunk_sweep_owner_module_agrees_with_image_range`) compares that range classification with the loader's owner
  module of every thunk target.

## Rules for adding a skip

1. The reason string states the condition in plain words and nothing machine specific (no handles, ids, paths).
2. The condition must be detected, not assumed: probe the capability (device present, privilege held, error returned) and
   skip only when it fails. A skip that always fires is listed above as unconditional and needs a stated reason.
3. A check whose result varies from run to run is not a skip: assert the relation that always holds, or print the value
   with `t_info` (shown only with `W32K_INFO=1`).

## Timing-sensitive cross-thread send tests

On a loaded guest the calling thread can be descheduled for longer than a receiver's sleep. Two tests are
therefore tolerant of that: the 100 ms SendMessageTimeout test retries (bounded) when the send completed
because the caller was stalled, and the hung-receiver test skips (conditional, with a plain reason) when the
caller was stalled until the receiver resumed pumping. An un-stalled attempt is still asserted exactly.

## EnableWindow lifecycle (focus_capture_attach, fc_enable_life.c)

No skips and no environment dependence were added; the capture tests skip only when the process cannot take capture. Observed on
Windows 10 22H2 and asserted exactly: the result is the previous disabled state (0, nonzero, nonzero, 0 for disable, disable,
enable, enable); every disable, including a repeated one, sends WM_CANCELMODE before the style bit changes; WM_ENABLE is sent only
on a real change; with capture the order is WM_CANCELMODE, WM_CAPTURECHANGED, WM_ENABLE(FALSE) and capture is gone afterwards;
IsWindowEnabled reads only the window's own WS_DISABLED bit, so a child of a disabled parent still reports enabled and receives no
WM_ENABLE, and enabling the parent does not enable a separately disabled child. Both modes give identical logs.
