# Windows 10 notes: skips and environment dependence

The authority is genuine Windows 10 22H2 x64 (build 19045). The reference guest is a Windows 10 **Home** image
(operating system SKU 101), one interactive session, one virtual display, one monitor, no network, a PS/2 keyboard and mouse plus one USB tablet in the raw
input list, no pointer (touch or pen) device and no other HID device, one keyboard layout, no OpenGL-capable display driver. The tests ran in three modes
(the third, user, was added later and has been run on a representative subset, see the last section):

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
| kbd_layouts_state | 2 (plus the layout skip) | GetAsyncKeyState is refused with ERROR_ACCESS_DENIED for this unprivileged token | the restricted token cannot read the async key state; the result is the same on Windows, only the token differs; the real standard account (user mode) is refused too |
| gap_09 | 2 in adm, 2 in std | absolute open denied (error 5); rooted open denied (error 5) in adm; cannot open the window station directory (access denied status) in std | opens of the window station directory by absolute or rooted name are refused with access denied for both tokens, and the std token is refused the directory itself |

Other tests branch on integrity or privilege without skipping: they assert the Windows 10 outcome for the token they run under
(for example the clipboard-owner and foreign-window checks pick the expectation from the integrity level). Where adm and std give
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
- `dx_` areas are guarded, change system state and have not been run on Windows 10, so they have no Windows 10 skips or
  reasons recorded yet.

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
