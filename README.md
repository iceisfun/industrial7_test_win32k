# win32k kernel-side conformance corpus

> ## STATUS: verified on genuine Windows 10 22H2
>
> 104 areas. The 86 non-`dx_` areas (16211 tests per mode) ran on a genuine Windows 10 22H2 x64 guest (build 19045, interactive
> session) in adm, std and user (real standard account) mode, with 0 failed tests in the committed run:
>
> | Mode | Pass | Fail | Skip |
> |---|---|---|---|
> | adm (elevated Administrator) | 16172 | 0 | 39 |
> | std (restricted-admin token) | 16110 | 0 | 101 |
> | user (real standard account) | 16110 | 0 | 101 |
>
> Skips are conditional and carry a printed reason (`docs/WIN10_NOTES.md`). A second whole-corpus run differed in
> three timing-dependent tests (`results/win10/DETERMINISM.md`); determinism over three runs is not yet shown for this build. The 18 `dx_` areas change system state and
> have not been run on Windows 10. Statistics: `results/STATS.md`; syscall coverage at three levels of evidence
> (direct, statically mapped route, witnessed kernel entry): `COVERAGE.md`; how to pack, run and parse:
> `HANDOFF.md`.

Black-box, kernel-side conformance tests for the Windows 10 22H2 (build 19041) x64 Win32 subsystem
driver stack: `win32k.sys` (service-table thunks), `win32kbase.sys` and `win32kfull.sys`. The
kernel cannot be loaded or called directly, so tests reach it the way every real program does -
through the syscall interface exported by `win32u.dll` (~1300 `Nt*` stubs executing `SYSCALL` with
service numbers >= 0x1000) - either by resolving a `win32u.dll` stub directly for a pinpoint
check, or through the normal `user32`/`gdi32`/`gdi32full` path, and then **validating local
process state** the kernel produced or maintains (TEB/PEB win32 fields, the GDI and USER shared
handle tables, the kernel callback table, `NtQueryObject`, etc.). No DLL under test is ever linked;
every call goes `LoadLibrary -> GetProcAddress -> invoke`.

See `docs/KERNEL_TESTING.md` for how this differs from a plain user-mode DLL corpus, the
pristine-state rule, the `common/k.h` helper API, and how to find an `Nt*` syscall's signature
(none are in any SDK header).

Modules searched by `U()`: `win32u.dll user32.dll gdi32.dll gdi32full.dll ntdll.dll kernel32.dll`
(in that order - `win32u.dll` first, since it is itself the module under test and is also where
most syscall stubs live).

    make                         # cross-compile build/<area>.exe (mingw-w64)
    build\<area>.exe [substr]    # on Windows; optional test-name filter
    tools\run_all.bat            # everything, build\results\<area>.txt (add /destructive for dx_* areas)
    python tools\summarize.py build\results [--failures]
    python tools/coverage.py .  # syscall coverage -> COVERAGE.md

Output: `RUN/OK/FAIL/SKIP` per test, `  FAIL file:line expr (detail)` per failed check, `SUMMARY`
last; exit code 1 if any test failed. `NOTE` lines record observed, version-dependent behaviour.

Tests that need a second process re-launch the exe (`--child <name>`); see `common/k.h`'s
`k_child_run()`/`k_child_run_ex()` for the capture/relay convention, and `docs/KERNEL_TESTING.md`
for when a test must be a child (pristine-state, process-wide/irreversible settings, per-process
quota). Scratch files live in `%TEMP%\win32kt_<pid>\` and are removed on exit.
`tools/*_exports.txt` list the named exports of each module; `tools/win32u_syscalls.tsv` lists
every win32u service number and name.

Headless Wine (`tools/wine_run.sh build/<area>.exe [filter]`) is a compile/hang/crash validator
only - Wine has its own `win32u.dll` and does not implement `win32k.sys`, so a check failing there
is never, by itself, evidence the test is wrong. Real Windows 10 x64 is the authority: a test that fails
there is a bug in the test. `build/<area>.exe --list` prints the registered test names without running anything.

## The method: userland path, then local state

The kernel is only reachable through syscalls, so a test does one or both of:

1. Call a `win32u.dll` stub directly (`UDECL(ret, NtUserFoo, (args))`, `UN(NtUserFoo)`) to check
   kernel-side parameter validation: NULL or stale handles, bad flag bits, wrong object type, access and
   ownership rules, buffer-size protocols. Signatures are derived from the user32/gdi32full call sites
   and probed on Windows 10, never guessed.
2. Go through the normal user32/gdi32/gdi32full API so the kernel does real work, then inspect what the
   kernel left in the process: TEB/PEB win32 fields, `GdiSharedHandleTable` cells and `DC_ATTR`/`BRUSHATTR`,
   the user32 `gSharedInfo` handle table and `SERVERINFO`, read-only desktop-heap structures through
   `ulClientDelta`, DIB-section memory, `GetGuiResources` counts, `NtQueryObject` names/types,
   `KernelCallbackTable` arrivals, `KUSER_SHARED_DATA`, return values, last error and NTSTATUS.

Only behavior observed on Windows 10 (or fixed by public documentation or PDB types) is `CHECK`ed; build, locale, SKU, hardware,
DPI and theme dependent observations are `t_note`d, and missing prerequisites `t_skip`.
Memory read through kernel-maintained pointers goes through the safe-read helpers in `common/k.h`.

## Safety

Tests run in the interactive session on the default desktop of WinSta0 and are good guests: hidden
off-screen tool windows, no modal loops, no foreground stealing, bounded waits, <= 256 threads,
< 1 GB, <= 12000 objects per test. Process-global irreversible state (DPI awareness, mitigation
policy, window station assignment, process hooks) is only changed in child processes. Everything that
injects input or changes system state (SendInput, desktop switching, SPI writes with restore, clipboard
content, display affinity) lives in `dx_*` areas that `run_all.bat` runs only with `/destructive`.
`kprobe_user_pointers` is the one area that passes NULL/kernel-range pointers, and only to syscalls that guard the copy-in
(observed on Windows 10: the call fails with an error instead of crashing). Nothing calls MessageBox, TrackPopupMenu
(only its early-exit argument checks), ExitWindows, LockWorkStation or shutdown APIs.

## Areas

Test counts are the registered tests of the built exe. Win10 columns are the skipped tests on genuine Windows 10 22H2 in the elevated (adm) and unprivileged (std) mode; every area has 0 failures there (`results/win10/`). `dx_` areas change system state and have not been run on Windows 10. Reasons for every skip: `docs/WIN10_NOTES.md`.

| Area | Purpose | Safety | Tests | Win10 skipped adm | Win10 skipped std |
|---|---|---|---|---|---|
| class_registration | RegisterClass/atoms, class structures, CallProc handles (NtUserRegisterClassExWOW, GetClassInfo, SetClassLong) | safe (non-destructive) | 147 | 0 | 0 |
| clipboard_dde | clipboard ownership/formats/sequence, DDE, per-winsta isolation | safe (non-destructive) | 209 | 0 | 0 |
| core_k_helpers | self-test of common/k.c helpers | safe (non-destructive) | 9 | 0 | 0 |
| cursor_icon_menu | cursors, icons, menus, ClipCursor state | safe (non-destructive) | 273 | 0 | 0 |
| dcomp_flip_dxgk_neg | DComposition / FlipObject / Dxgk / OneCore stubs: stub layout and early-exit negatives | safe (non-destructive) | 166 | 0 | 0 |
| display_monitors_dpi | monitors, DisplayConfig, DPI awareness/scaling, monitor shared heap | safe (non-destructive) | 247 | 1 | 1 |
| dx_desktop_switch | desktop/winsta switching, input desktop isolation (guarded) | destructive: run with /destructive | 104 | not run | not run |
| dx_gap_00 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 4 | not run | not run |
| dx_gap_01 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 5 | not run | not run |
| dx_gap_02 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 2 | not run | not run |
| dx_gap_03 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 10 | not run | not run |
| dx_gap_04 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 5 | not run | not run |
| dx_gap_05 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 6 | not run | not run |
| dx_gap_06 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 5 | not run | not run |
| dx_gap_07 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 12 | not run | not run |
| dx_gap_08 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 10 | not run | not run |
| dx_gap_09 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 6 | not run | not run |
| dx_gap_10 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 22 | not run | not run |
| dx_gap_11 | coverage gap-fill: syscalls left over by the feature areas (state-changing part) | destructive: run with /destructive | 6 | not run | not run |
| dx_input_inject | SendInput, cursor, hooks, queue state (input injection) | destructive: run with /destructive | 88 | not run | not run |
| dx_process_session_cleanup | abnormal process exit cleanup of win32k objects | destructive: run with /destructive | 6 | not run | not run |
| dx_r2_input_pointer_rim_state | RIM/pointer input state with injection | destructive: run with /destructive | 7 | not run | not run |
| dx_sysparams_colors_metrics | SystemParametersInfo/colors/metrics writes with restore | destructive: run with /destructive | 48 | not run | not run |
| dx_user_serverinfo_shared | SERVERINFO writes visible in shared info, restored | destructive: run with /destructive | 34 | not run | not run |
| focus_capture_attach | focus/capture/activation, AttachThreadInput | safe (non-destructive) | 215 | 1 | 1 |
| gap_00 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 104 | 0 | 0 |
| gap_01 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 174 | 0 | 0 |
| gap_02 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 104 | 0 | 0 |
| gap_03 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 117 | 0 | 0 |
| gap_04 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 107 | 0 | 0 |
| gap_05 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 111 | 0 | 0 |
| gap_06 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 112 | 0 | 0 |
| gap_07 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 147 | 3 | 3 |
| gap_08 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 117 | 0 | 0 |
| gap_09 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 141 | 0 | 0 |
| gap_10 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 134 | 0 | 0 |
| gap_11 | coverage gap-fill: syscalls left over by the feature areas | safe (non-destructive) | 66 | 1 | 1 |
| gdi_bitmaps_dib | bitmaps and DIB sections (section memory, DIBSECTION fields) | safe (non-destructive) | 430 | 0 | 0 |
| gdi_blit_raster | BitBlt/StretchBlt/raster ops results | safe (non-destructive) | 258 | 0 | 0 |
| gdi_d3dkmt_dd | D3DKMT thunks, DirectDraw/DxEng syscalls | safe (non-destructive) | 215 | 0 | 0 |
| gdi_dc_objects | DC creation/selection/save/restore, object types | safe (non-destructive) | 169 | 0 | 0 |
| gdi_init_order | process GDI/USER initialisation state before and after the first win32k service (fixture exe, relations only) | safe (non-destructive) | 5 | - | - |
| gdi_dcattr_batching | DC_ATTR pages, GDI TEB batch, flushing | safe (non-destructive) | 143 | 0 | 0 |
| gdi_font_enum_resources | font enumeration and private font resources | safe (non-destructive) | 187 | 0 | 0 |
| gdi_font_metrics_text | font creation, text metrics, glyph/text syscalls | safe (non-destructive) | 230 | 1 | 1 |
| gdi_handle_table | GdiSharedHandleTable cells: index/uniq/type, lifetime | safe (non-destructive) | 479 | 8 | 8 |
| gdi_metafile_print_neg | metafile/printing syscall negatives | safe (non-destructive) | 204 | 3 | 3 |
| gdi_paths_xform_draw | paths, transforms, drawing results | safe (non-destructive) | 321 | 0 | 0 |
| gdi_pens_brushes_palettes | pens, brushes, palettes, BRUSHATTR | safe (non-destructive) | 182 | 0 | 0 |
| gdi_regions_clip | regions and clipping | safe (non-destructive) | 141 | 0 | 0 |
| gui_thread_conversion | first-GUI-call thread conversion, TEB/PEB win32 fields (child processes) | safe (non-destructive); child processes | 123 | 0 | 0 |
| hooks_winevents | SetWindowsHookEx / WinEvent hooks and callbacks | safe (non-destructive) | 96 | 0 | 0 |
| ime_hotkeys_misc | IME, hotkeys, assorted NtUser queries | safe (non-destructive) | 333 | 0 | 0 |
| kbd_layouts_state | keyboard layouts, key state, ToUnicode | safe (non-destructive) | 139 | 1 | 3 |
| kcb_kernel_callbacks | PEB KernelCallbackTable and callbacks into user mode | safe (non-destructive) | 72 | 0 | 0 |
| kprobe_user_pointers | NULL / kernel-range pointers to guarded copy-in syscalls (only area allowed to) | safe (non-destructive); deliberate bad pointers | 66 | 0 | 0 |
| msg_queue_post_peek | message queues, post/peek, thread state | safe (non-destructive) | 158 | 0 | 0 |
| msg_send_cross_thread | cross-thread SendMessage, reply, nesting | safe (non-destructive) | 149 | 0 | 0 |
| msg_timers_atoms | timers, window messages, atom tables | safe (non-destructive) | 205 | 0 | 0 |
| paint_dc_cache_regions | BeginPaint, update regions, DC cache | safe (non-destructive) | 272 | 0 | 0 |
| process_session_cleanup | process/session object cleanup, handle counts | safe (non-destructive) | 214 | 0 | 0 |
| quota_limits | per-process GUI object quotas (child processes) | safe (non-destructive); child processes | 144 | 0 | 0 |
| r2_composition_surface_channel | composition surface/channel syscalls | safe (non-destructive) | 81 | 0 | 0 |
| r2_display_opm_gamma_dpi | OPM, gamma ramps, DPI syscalls | safe (non-destructive) | 95 | 0 | 0 |
| r2_gdi_cell_per_type | GDI cell fields for every object type | safe (non-destructive) | 333 | 0 | 0 |
| r2_gdi_internal_eng_glue | Eng*/internal GDI glue syscalls | safe (non-destructive) | 232 | 0 | 0 |
| r2_input_pointer_rim_state | RIM/pointer state, injection gates | safe (non-destructive) | 121 | 3 | 3 |
| r2_kernel_callback_table_full | every KernelCallbackTable slot | safe (non-destructive) | 93 | 0 | 0 |
| r2_ntusercall_codes_matrix | NtUserCall* code matrix | safe (non-destructive) | 129 | 0 | 0 |
| r2_teb_peb_field_matrix | TEB/PEB field matrix | safe (non-destructive) | 159 | 1 | 1 |
| r2_user_handle_types_entries | USER handle types and entries | safe (non-destructive) | 95 | 0 | 0 |
| r2_win32k_process_lifecycle_quotas | W32 process lifecycle and quotas | safe (non-destructive) | 104 | 0 | 0 |
| r2_winsta_activation_group_session | winsta activation, groups, sessions | safe (non-destructive) | 188 | 0 | 15 |
| r2_wnd_cls_heap_fields | WND/CLS desktop-heap fields | safe (non-destructive) | 136 | 0 | 0 |
| raw_input_touch_pointer | raw input, touch, pointer APIs (read-only side) | safe (non-destructive) | 424 | 6 | 6 |
| sc_callxxx_dispatch | NtUserCallOneParam/TwoParam/HwndXxx dispatch codes | safe (non-destructive) | 74 | 0 | 0 |
| sc_raw_syscall | raw SYSCALL entry, number bounds, GUI conversion | safe (non-destructive) | 19 | 0 | 0 |
| sc_stub_layout | win32u stub bytes and service numbers for all 1316 syscalls | safe (non-destructive) | 91 | 0 | 0 |
| scroll_info_structs | scroll bars and struct marshalling | safe (non-destructive) | 171 | 0 | 0 |
| sysparams_colors_metrics | SPI validation, colors, metrics (read, validation) | safe (non-destructive) | 196 | 0 | 0 |
| teb_peb_win32_fields | TEB/PEB win32 fields and last error/status | safe (non-destructive) | 70 | 0 | 0 |
| user_handle_table | USER shared handle table entries | safe (non-destructive) | 272 | 0 | 0 |
| user_serverinfo_shared | SERVERINFO / shared info read-only invariants | safe (non-destructive) | 439 | 0 | 0 |
| user_wnd_desktop_heap | window structures in the read-only desktop heap | safe (non-destructive) | 269 | 1 | 1 |
| win32k_mitigation | win32k mitigation policy and syscall filtering (child processes) | safe (non-destructive); child processes | 284 | 13 | 13 |
| win32k_modules_sysinfo | loaded win32k modules, NtQuerySystemInformation | safe (non-destructive) | 329 | 0 | 9 |
| win_create_destroy | window create/destroy via NtUserCreateWindowEx | safe (non-destructive) | 192 | 0 | 0 |
| win_position_zorder | positioning, Z order, visibility | safe (non-destructive) | 306 | 0 | 0 |
| win_props_styles_queries | window properties, styles, queries | safe (non-destructive) | 192 | 0 | 0 |
| winsta_desktop_objects | window station / desktop objects, NtQueryObject, access | safe (non-destructive) | 163 | 0 | 36 |
| x_clipboard_state_matrix | the clipboard state syscalls cross-checked on the win32u stub and the user32 wrapper: sequence number, format count against enumeration, availability, priority selection, not-open errors, a second thread's access, viewer chain link/forward/unlink; only relations are asserted, the clipboard content the test places is emptied again | safe (non-destructive); worker thread | 226 | 0 | 0 |
| x_cursor_display_config_read | per-queue current cursor (get, set, hide, invalid and wrong-type handles, a second thread's independent queue), cursor size table, DPI cursor link refusals, cursor contents move semantics, CCD buffer-size flag matrix and unwritable outputs, DisplayConfigSetDeviceInfo and SetDisplayConfig rejection and validate-only forms (never applied; monitor list checked unchanged), monitor enumeration against SM_CMONITORS, clip rectangles, DC kinds and callback contract, monitor paint and display mapping guards, work-area relations, stub identity of the thin syscalls | safe (non-destructive); worker threads | 203 | 0 | 0 |
| x_d3dkmt_badhandle_matrix_a | keyed mutex lifecycle (create, open, acquire, release, destroy, private data, NT-handle open), and argument/handle validation of the keyed mutex, synchronization object and swap chain D3DKMT thunks on both the win32u and gdi32 routes, with no adapter | safe (non-destructive); worker thread | 304 | 6 | 6 |
| x_d3dkmt_badhandle_matrix_b | argument/handle validation of 36 more D3DKMT thunks (contexts, hardware queues, GPU virtual addresses, offer/reclaim, Lock2/Unlock2, presents, shared resources, bundle and protected-session) on both routes, NT-handle type checks with real kernel handles, no adapter | safe (non-destructive); worker thread | 235 | 0 | 0 |
| x_d3dkmt_process_global_queries | per-process and global D3DKMT queries that need no adapter handle: the gated thunks (scheduling-priority band and class, yield percentage, memory budget target, FSE block, process list) and their fixed answers, process device-removal support get and the one-shot setter (child processes), process offer-info and trim-commitment handle/selector/flag matrices, EnumAdapters/2/3 count and LUID relations and the v3 filter, QueryClockCalibration and DDisplayEnum, on both win32u and gdi32 routes | safe (non-destructive); child processes | 250 | 0 | 0 |
| x_dxgk_tracked_workload_vail_neg | the 18 NtDxgk tracked-workload, display-manager, hardware-queue present, pinning and Vail thunks: fixed rejection statuses for zero, readable, unreadable and filled argument shapes, first-block size thresholds, the display-manager handle-type matrix, the Vail registration/promotion/connect denials, privilege-dependent display-port gate, gdi32 route, fresh thread and child process | safe (non-destructive); child process | 245 | 0 | 0 |
| x_gdi_brush_palette_dib_neg | parameter matrices and consistency for DIB and pattern brushes, nearest palette index, system palette use, colour-correct palette, halftone palettes, StretchDIBits, MonoBitmap and glyph indices | safe (non-destructive) | 162 | 0 | 0 |
| x_gdi_font_charset_embed | charset per font across GetTextCharset, GetTextCharsetInfo, NtGdiGetCharSet, LOGFONT and FONTSIGNATURE; embedded-font counter; EUDC and font-link switches; remote/merge font attach; EngComputeGlyphSet; PolyTextOutW against ExtTextOutW; direct NtGdiHfontCreate | safe (non-destructive); child processes | 250 | 0 | 0 |
| x_gdi_region_clip_neg | return-code matrices for the thin clip, elliptic/round-rect region, bounds, unrealize, window-extent and MoveTo syscalls | safe (non-destructive) | 250 | 0 | 0 |
| x_menu_accel_hotkey_lifetime | ownership and lifetime of accelerator tables, menus and hotkeys: create/copy/destroy of tables from other threads and processes (handle forms, buffers, wrong object kinds), EndMenu on and off the menu-owning thread, SetMenuFlagRtoL on menus of other threads, processes and desktops and after window/parent destruction, RegisterHotKey ownership between threads, hotkey release on thread and process end, IME hotkey table shared across threads and processes, MNDragOver/MNDragLeave with and without a tracked drag-drop menu; popup menus opened off a timer, no input sent | safe (non-destructive); child processes, worker threads, an extra desktop | 169 | 0 | 0 |
| x_opm_ddcci_neg | rejection matrices for the OPM protected-output, certificate, DDC/CI monitor-control and gamma-capability syscalls: exact statuses for bogus handles, buffer sizes, device names and pointers; physical-monitor handle lifetime; dxva2 error values | safe (non-destructive); never creates a protected output or writes a monitor setting | 241 | 0 | 0 |
| x_pointer_touch_inertia_neg | argument screening of the pointer-info list, proprietary-id, frame-discard and pointer-injection syscalls (kind/size table, id, count and overflow limits, unwritable pointers, synthetic touch and pen devices in child processes, no input injected); touch-window capability round trip with per-window isolation and other-thread refusals; touch validation status; compositor-only manipulation and inertia entry points and the ReportInertia screening order; mouse-in-pointer promotion, touch pad registration, himetric scale factors, raw-mouse marking, mouse input state | safe (non-destructive); child processes, worker threads | 246 | 0 | 0 |
| x_thread_desktop_heap_multi | per-thread versus per-process win32k state: queue change/wake bits (NtUserGetQueueStatusReadonly against GetQueueStatus) across worker threads, RealWaitMessageEx and RealInternalGetMessage on the caller's own queue, message extra info, focus/active/capture/caret per thread and not inherited, NavigateFocus and ClearForeground denials, CheckProcessSession, CheckAccessForIntegrityLevel and NotifyProcessCreate from threads and child processes, DisableProcessWindowFiltering in children, per-window resize layout synchronisation flag (Begin/Completed, Enable/Is, ConfirmResizeCommit), thread desktop and DPI context, and what is gone when a thread or child process ends (windows, hooks, carets, user-object counts) versus what stays (GDI objects, menus, classes) | safe (non-destructive); child processes, worker threads | 163 | 0 | 0 |
| x_window_chrome_info_queries | thin window-information syscalls on hidden windows: scroll bar info (object ids, cbSize, bar and thumb geometry against client/window rectangles, state array, hidden/absent bars, scroll bar controls), title bar info (button states against style bits, system menu, geometry), combo box info (handles, rectangles, styles), caret position, internal window placement, minimize rectangle, top-level window, thread/desktop check, desktop id, shared window data, control brush and colour messages, window fill, caption and menu bar drawing into memory DCs, style alteration (changeable bit set, mask semantics) | safe (non-destructive); worker threads, a private desktop in a helper thread | 305 | 0 | 0 |
