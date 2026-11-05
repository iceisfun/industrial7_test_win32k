# NtQueryInformationProcess and layered window attributes

This note describes the two surfaces covered by the `x_ntqip_*` and `x_layered_*` areas, in the corpus authors' own words, and records the Windows 10 22H2 x64 (build 19041/19045) result of each area. The authority for every hard check is a genuine Windows 10 guest. Values that vary per machine appear only as relations, and behaviour that depends on token privilege is a conditional skip with a printed reason.

## Areas

All eight areas pass with 0 failures on Windows 10 22H2 x64 in both modes (administrator and the restricted standard mode), and the results were identical across three consecutive fresh-guest runs. A skip is conditional and prints its reason.

| Area | Tests | Admin (skips) | Standard (skips) |
|---|---|---|---|
| x_ntqip_qip_basic_quota_handles | 113 | 0 failed (0) | 0 failed (1: working set size needs a privilege the restricted token lacks) |
| x_ntqip_qip_image_session_peb | 171 | 0 failed (0) | 0 failed (5) |
| x_ntqip_qip_sched_security | 145 | 0 failed (0) | 0 failed (1: the session-0 service process cannot be opened by the restricted token) |
| x_ntqip_qip_mitigation_policy_job_energy | 114 | 0 failed (0) | 0 failed (5: coverage blob refused for the restricted token, test process already inside a job) |
| x_ntqip_qip_debug_protocol_invalid | 224 | 0 failed (0) | 0 failed (0) |
| x_layered_layered_attr_validation | 142 | 0 failed (0) | 0 failed (0) |
| x_layered_layered_attr_state_style | 113 | 0 failed (0) | 0 failed (0) |
| x_layered_layered_attr_ulw_child_xproc | 301 | 0 failed (0) | 0 failed (16: helpers that need a medium-integrity child, skipped because the standard token is itself medium) |

Total: 1323 tests per mode.

Class coverage of the process areas: basic_quota_handles covers classes 0-4, 14, 15, 20, 38, 42, 51, 54, 55, 58, 71; image_session_peb covers 19, 21, 23, 24, 26, 27, 37, 43, 44, 47, 49, 50, 60, 66, 67, 89, 97 and 28; sched_security covers 10, 12, 18, 22, 29, 33, 34, 36, 39, 45, 46, 59, 61, 74, 87, 92, 94, 96; mitigation_policy_job_energy covers 52, 64, 65, 69, 70, 72, 73, 75-77, 79, 81, 82, 84, 85, 88; debug_protocol_invalid covers 7, 30, 31, 32 plus the sweep over every invalid id, handle kind, length, alignment and return-length pattern.

## NtQueryInformationProcess: protocol

Rights: LIM = PROCESS_QUERY_LIMITED_INFORMATION (0x1000), QRY = PROCESS_QUERY_INFORMATION (0x400), VMR = PROCESS_VM_READ (0x10). The pseudo handle -1 always passes the access check. RL = ReturnLength, len = ProcessInformationLength.

1. Buffer alignment is checked first when len is not zero: 4 bytes for nearly every class (even those with 8-byte fields), 8 for classes 65, 92 and 94, none for classes 61, 70, 74 and 87. A misaligned buffer returns STATUS_DATATYPE_MISALIGNMENT (0x80000002) before the class or handle is examined, so it also beats an invalid class.
2. A class that does not exist (see the invalid list below, any value above about 100, 0x80000000, 0xFFFFFFFF) returns STATUS_INVALID_INFO_CLASS (0xC0000003) before the handle is looked at.
3. For most classes the length is checked before the handle (wrong length with an invalid handle gives STATUS_INFO_LENGTH_MISMATCH 0xC0000004); a few classes reference the handle first (27, 43, 60, 61, 64, 76 and others, see the table and the area comments).
4. Handle errors: zero or closed handle STATUS_INVALID_HANDLE (0xC0000008); thread pseudo handle or non-process object STATUS_OBJECT_TYPE_MISMATCH (0xC0000024); missing access right STATUS_ACCESS_DENIED (0xC0000022).
5. On a length mismatch ReturnLength is usually not written; the variable-size classes (name, command line, window information, watch, group, leap-second information) do write the needed size. The areas pre-fill ReturnLength with a sentinel to pin this per class.
6. On success most fixed-size classes write ReturnLength; a handful (54, 55, 59, and 61 partly) leave it untouched.
7. There is almost no privilege test. Standard-user versus administrator matters through which other processes OpenProcess can open and with which rights, and through three refusals for restricted callers (the working set watch classes 15 and 42, and handle tracing 32). Class 81 refuses every user-mode caller; class 89 and 97 accept only the current-process pseudo handle.

Names: only classes 0, 7, 26, 27, 29 and 75 have public names; the others are described by neutral labels.

## Class table (71 classes valid on build 19041)

| id | label | length rule | right | layout and notes |
|---|---|---|---|---|
| 0 (0x00) | basic information (public) | exactly 0x30, or 0x40 (extended form; first qword = size 0x40, tail has a flags dword: bits protected/wow64/frozen etc.); other lens C0000004 | LIM | ExitStatus (STILL_ACTIVE 0x103 when running), PebBaseAddress, AffinityMask, BasePriority(int, 8 for NORMAL), UniqueProcessId, InheritedFromUniqueProcessId. Extended form: Size at +0, then the 0x30 body at +8, flags dword at +0x38. len check before handle. |
| 1 (0x01) | quota limits | exactly 0x30 or 0x58 (extended); else C0000004 | LIM | PagedPool/NonPaged/MinWS/MaxWS/Pagefile limits, TimeLimit; no pagefile limit semantics fixed; machine-varying values. |
| 2 (0x02) | io counters | exactly 0x30 | LIM | six ULONGLONG counters (read/write/other ops and bytes); monotone. |
| 3 (0x03) | vm counters | len in {0x58, 0x60, 0x70} only (0x68 and others are C0000004) | LIM | PeakVirtualSize, VirtualSize, PageFaultCount, PeakWS, WS, quota peaks/usage, Pagefile usage, peak pagefile, private usage etc.; relations only (Peak>=cur). |
| 4 (0x04) | times | exactly 0x20 | LIM | CreateTime, ExitTime (0 while running), KernelTime, UserTime (100 ns). |
| 7 (0x07) | debug port (public) | exactly 8 | QRY | 0 when not debugged, -1 (all ones) if a debug object attached; (this is the CheckRemoteDebugger path). |
| 10 (0x0a) | ldt information | not a fixed size | QRY | VMR/x64 returns a fixed refusal status after the rights check; exploratory only. |
| 12 (0x0c) | default hard error mode | exactly 4 | LIM | mirrors SetErrorMode flags of the process (SEM_*). |
| 14 (0x0e) | pooled usage and limits | exactly 0x48 | LIM | nine ULONG_PTR: paged/nonpaged peak/usage/limit... relations. |
| 15 (0x0f) | working set watch | 0x10-byte records, last record is terminator + base | QRY; restricted callers 0xC0000022 | watch must have been started (set side): else STATUS_UNSUCCESSFUL 0xC0000001; no events: STATUS_NO_MORE_ENTRIES 0x8000001A; too small: STATUS_BUFFER_TOO_SMALL 0xC0000023 with RL = needed. |
| 18 (0x12) | priority class | exactly 2 | LIM | {BOOLEAN Foreground; UCHAR PriorityClass 1..6 as PROCESS_PRIORITY_CLASS_*}; normal = 2. |
| 19 (0x13) | wow64-x86 flag | exactly 4 | QRY | native x64: 0 (bit 0 of a flags byte). |
| 20 (0x14) | handle count | 4 or 8 | LIM | ULONG count (+ ULONG peak when 8); count >= 1; peak >= count. RL = 4 even when 8 passed. |
| 21 (0x15) | affinity | 8 or 0x10 | LIM | ULONGLONG mask (+ group USHORT at +8 when 0x10). multi-group process: INVALID_PARAMETER. |
| 22 (0x16) | priority boost disabled | exactly 4 | LIM | 0/1 (inverse of boost flag; default 0 = boost enabled). |
| 23 (0x17) | device map | 0x24 or 0x30; else 0xC0000004 | QRY | DriveMap bitmask + 32 drive-type bytes; the 0x30 form takes a Flags dword at +0x28 as INPUT: bits other than 1 -> STATUS_INVALID_PARAMETER 0xC000000D. len < 0x24 -> 0xC0000004. |
| 24 (0x18) | session information | exactly 4 | LIM | SessionId ULONG; equals PEB SessionId (+0x2C0) for self (existing corpus: session_info.c). |
| 26 (0x1a) | wow64 information (public) | exactly 8 | LIM | PEB32 pointer of a WOW64 process; 0 for native x64. |
| 27 (0x1b) | image file name, NT path (public) | variable; len < 0x10 behaves as "too small"; layout UNICODE_STRING (0x10 bytes) + chars | LIM | name = NT device path of the image (\Device\HarddiskVolumeN\...). Too small: C0000004 with RL = needed. Buffer pointer in the UNICODE_STRING points inside the caller buffer. Handle referenced first. |
| 28 (0x1c) | LUID device maps enabled | exactly 4 | none: handle is NOT referenced; any handle value (even invalid) succeeds | always 1 on this build. |
| 29 (0x1d) | break on termination (public) | exactly 4 | LIM | 0/1; set side needs privilege (not in this scope). |
| 30 (0x1e) | debug object handle | exactly 8 | QRY | not debugged: STATUS_PORT_NOT_SET 0xC0000353 and output 0; debugged: new handle (must be closed). |
| 31 (0x1f) | debug flags | exactly 4 | QRY | 1 normally (inherit debug); 0 after NoDebugInherit set. |
| 32 (0x20) | handle tracing | variable, >= 0x10 | QRY; restricted caller 0xC0000022 | tracing not enabled -> STATUS_INVALID_PARAMETER 0xC000000D; len < 0x10 -> 0xC0000004. |
| 33 (0x21) | io priority | exactly 4 | LIM | 0..4 (default Normal=2); bits 27..29 of flags. |
| 34 (0x22) | execute flags (DEP) | exactly 4 | -1 special: pseudo handle references nothing; else QRY | MEM_EXECUTE_OPTION_*; on x64 native the kernel answers for the process (all permanent-on). |
| 36 (0x24) | cookie | exactly 4 | -1 uses current process; else a handle with 0x20 (VM_WRITE) | per-process 32-bit random, never changes within the process, nonzero after first query. Foreign handles are allowed when the 0x20 right is granted. |
| 37 (0x25) | image information | exactly 0x40 | LIM | SECTION_IMAGE_INFORMATION: TransferAddress, ZeroBits, MaxStack, CommitStack, SubSystem(2/3), versions, characteristics, Machine 0x8664, flags. For self: STATUS_PROCESS_IS_TERMINATING 0xC000010A style when image gone; no image -> INVALID_PARAMETER. |
| 38 (0x26) | cycle time | exactly 0x10 | LIM | AccumulatedCycles, CurrentCycleCount (both monotone). |
| 39 (0x27) | page priority | exactly 4 | LIM | 1..5 (default 5), bits 12..14 of process flags. |
| 42 (0x2a) | working set watch (extended) | multiples of 0x20 only (else 0xC0000004) | QRY; restricted caller 0xC0000022 | like 15 with 0x20-byte records. |
| 43 (0x2b) | image file name, DOS path (Win32) | variable: UNICODE_STRING + chars, needs 0x10 + bytes | LIM | DOS path (C:\...), derived from the file object; too small -> C0000004 with RL needed. No restriction to self on Win10. |
| 44 (0x2c) | image file mapping | exactly 8 | QRY | INPUT is a file handle in the first qword (needs FILE_READ_DATA/SYNCHRONIZE-ish 0x100020 on the file); success (RL=0) only if the file is the process's image; mismatch -> STATUS_UNSUCCESSFUL 0xC0000001. |
| 45 (0x2d) | affinity update mode | exactly 4 | LIM | bit0 = enable-auto-update, bit1 = permanent. |
| 46 (0x2e) | memory allocation mode | exactly 4 | LIM | bit0 = bottom-up / top-down randomisation flag. |
| 47 (0x2f) | group information | even len; RL must be non-NULL (NULL or odd len -> 0xC000000D) | LIM | array of USHORT group numbers; small buffer -> STATUS_BUFFER_TOO_SMALL 0xC0000023 with RL=needed (2 per group). |
| 49 (0x31) | console host process id | exactly 8 | LIM | ULONG_PTR pid (0 if none). |
| 50 (0x32) | window information | variable, >= 6 | LIM; kernel-mode callers get C0000001 | {ULONG WindowFlags; USHORT TitleLength; WCHAR Title[]}; reads from the process's STARTUPINFO; too small: C0000004 with RL = TitleLength + 6. |
| 51 (0x33) | handle information | >= 0x10 | QRY | PROCESS_HANDLE_SNAPSHOT_INFORMATION: {ULONG_PTR NumberOfHandles; ULONG_PTR Reserved; entries of 0x28 bytes: HandleValue, HandleCount, PointerCount, GrantedAccess, ObjectTypeIndex, HandleAttributes, Reserved}. Truncation -> C0000004, RL full size; len < 0x10 -> C0000004 and RL = 0x10. |
| 52 (0x34) | mitigation policy | exactly 8 | LIM; -1 special | INPUT policy id in first dword (1=ASLR, 2=dynamic code, 3=strict handle checks, 4=syscall disable, 6=extension points, 7=CFG, 8=signature, 9=font disable, 10=image load, 11=win32k filter bits, 12=payload restriction, 13=child process, 14=side channel isolation, 15=user shadow stack, 16=redirection trust); OUTPUT flags dword at +4. ids 0, 5 and >16 -> STATUS_NOT_SUPPORTED 0xC00000BB. Existing corpus (win32k_mitigation, r2 mitig.c) covers policy contents; this regime covers only the front door. |
| 54 (0x36) | handle checking mode | exactly 4 | QRY | 0/1 (strict invalid-handle checking). |
| 55 (0x37) | keep-alive count | exactly 8 | QRY | two ULONGs (counts); leaves RL untouched. |
| 58 (0x3a) | handle table (array of handle values) | >= 4 | QRY | DUP_HANDLE (0x440)/array of ULONG handle numbers, RL = 4 * count; buffer locked/probed; sparse results machine-varying. |
| 59 (0x3b) | check stack extents mode | exactly 4 | QRY | 0/1; no RL. |
| 60 (0x3c) | command line | variable | LIM | UNICODE_STRING + chars (0x10 header); handle first; too small: C0000004 with RL needed; process with no usable params -> STATUS_NOT_FOUND 0xC0000225; terminating -> 0xC000010A. |
| 61 (0x3d) | protection information | >=1 (byte aligned) | LIM | 1 byte PS_PROTECTION: 0 for ordinary processes; len 0 -> C0000004. |
| 64 (0x40) | telemetry id | variable | LIM | telemetry provider blob; machine-varying; exploratory only. |
| 65 (0x41) | commit release information | exactly 0x20; 8-byte aligned buffer | LIM | INPUT first dword Version must be 3 (else 0xC0000059 STATUS_REVISION_MISMATCH), second dword < 8 and 8..15 zero (else C000000D); output: flags + 3 qword sizes. |
| 66 (0x42) | default cpu sets | len % 8 == 0 and <= 0xa0 | LIM | ULONG64 array of CPU set ids; RL = 8*count (RL is written even when len smaller; output truncated, status success). |
| 67 (0x43) | allowed cpu sets | same as 66 | LIM | same. |
| 69 (0x45) | job memory information | exactly 0x28 | LIM | process not in a job: STATUS_PROCESS_NOT_IN_JOB 0xC00001AE; else job counters. |
| 70 (0x46) | in-private | exactly 1 (byte aligned) | LIM | BOOLEAN. |
| 71 (0x47) | raise exception on invalid handle close | exactly 4 | LIM | 0/1. |
| 72 (0x48) | IUM challenge response | variable | - | handled by a secure-kernel helper; expect a fixed refusal on non-IUM machines; exploratory only. |
| 73 (0x49) | child process information | exactly 3 | -1 needs no lookup; foreign handle QRY | three BOOLEANs {NoChildProcessCreation, AllowSecureProcess, ...}; RL = 3. |
| 74 (0x4a) | high graphics priority | exactly 1 | LIM | BOOLEAN. |
| 75 (0x4b) | subsystem information (public) | exactly 4 | LIM | 0 = Win32/none; non-zero (e.g. 1) only for pico/WSL processes. |
| 76 (0x4c) | energy values | any length | LIM | body 0x1b0 bytes; copies min(len, 0x1b0); RL always 0x1b0; NEVER a length error (len 0 succeeds). |
| 77 (0x4d) | activity throttle state | exactly 0xc | LIM | present only if the throttling extension is loaded: otherwise STATUS_NOT_IMPLEMENTED 0xC0000002. machine-varying. |
| 79 (0x4f) | win32k syscall filter | exactly 8 | -1 special: QRY | {ULONG FilterState; ULONG FilterSet}. |
| 81 (0x51) | wake information | exactly 0x30 | LIM | every USER-mode caller gets STATUS_ACCESS_DENIED 0xC0000022 before the length is looked at (kernel callers only). |
| 82 (0x52) | energy tracking state | >= 8 | LIM | copies min(len, 0x90); RL = copied count. |
| 84 (0x54) | trustlet live dump | exactly 4 | QRY | VM_OPERATION (0x418)/non-secure process -> STATUS_ACCESS_DENIED 0xC0000022 after the right check. |
| 85 (0x55) | telemetry coverage | variable | LIM | machine-varying; exploratory only. |
| 87 (0x57) | read/write VM logging | any len != 0 (byte aligned) | LIM | one byte, bits 0..1; len 0 -> C0000004. |
| 88 (0x58) | uptime / freeze statistics | any length | LIM | 0x38-byte structure; copies min(len, 0x38) (len 0 succeeds); RL = copied count. |
| 89 (0x59) | image section handle | exactly 8 | handle MUST be -1 and caller = target else STATUS_ACCESS_DENIED 0xC0000022 | returns a NEW handle to the process image section in a qword (caller closes it). |
| 92 (0x5c) | sequence number | exactly 8 (8-byte aligned) | LIM | process start sequence number (qword); stable; unique across processes. |
| 94 (0x5e) | security domain | exactly 8 (8-byte aligned) | LIM | no security domain assigned: STATUS_NO_TOKEN 0xC000007C. |
| 96 (0x60) | enable logging | >= 4 | LIM | dword combining bit flags (read/write VM logging bits; bits 2,3 from the second flags word); len < 4 -> C0000004. |
| 97 (0x61) | leap second information | exactly 8 | handle MUST be -1 (anything else STATUS_INVALID_PARAMETER 0xC000000D, even for a real self handle); no right needed | {ULONG Flags (bit0 = leap seconds enabled); ULONG Reserved}; wrong len: C0000004 AND RL = 8 (the only class where RL is written on mismatch via the common tail). |

Invalid ids (STATUS_INVALID_INFO_CLASS for a query, whatever the handle and length): 5, 6, 8, 9, 11, 13, 16, 17, 25, 35, 40, 41, 48, 53, 56, 57, 62, 63, 68, 78, 80, 83, 86, 90, 91, 93, 95, 98, 99, 100-127 and larger values. Several of them are set-only classes.

## Layered window attributes: contract

Surfaces: SetLayeredWindowAttributes (hwnd, key, alpha, flags), GetLayeredWindowAttributes (hwnd, out key, out alpha, out flags) and UpdateLayeredWindow with ten parameters in the win32u form. In user32 the first two are direct forwards to the win32u services; UpdateLayeredWindow is a thin wrapper that adds a NULL dirty rectangle.

## Set: validation order
1. Resolve hwnd. Invalid/NULL/destroyed handle -> FALSE, last error 1400 (invalid window handle), nothing else touched.
2. (dwFlags & ~3) != 0 -> FALSE, error 87. This test is BEFORE the layered-style test, so it fires for non-layered windows too.
3. If window is composed on the desktop compositor and an internal state bit (0x20) is set -> FALSE, error 87 (not reproducible from user mode; ignore).
4. Inner worker: window must have WS_EX_LAYERED in its extended style, else error 87, FALSE. flags 0..3 on a non-layered window all fail the same way.
5. If the window has no redirection surface yet, one is created (side effect: the window becomes "attribute-layered"). If it already has one that was NOT created by SetLayeredWindowAttributes (i.e. it was driven by UpdateLayeredWindow) the call fails (the exact error is pinned in x_layered_layered_attr_ulw_child_xproc).
6. Stored: crKey, bAlpha, flags (LWA_COLORKEY 1, LWA_ALPHA 2). No range checks on crKey; bAlpha any byte. flags 0 is accepted on a layered window (the read-back is pinned in x_layered_layered_attr_state_style).
7. If LWA_ALPHA set and the window has a shadow (class CS_DROPSHADOW), the shadow alpha is updated (invisible to tests).
- Cross-thread use inside one process works; cross-process behaviour, including the integrity-level cases, is pinned in x_layered_layered_attr_ulw_child_xproc.
- Return value is BOOL 0/1 (a failing internal status becomes the last error).

## Get: validation order
1. Resolve hwnd; invalid -> FALSE, 1400.
2. Compositor-state test as above (87), not user-reproducible.
3. Window not WS_EX_LAYERED -> FALSE, 87.
4. Window layered but never given attributes through Set (no redirection state) or driven by UpdateLayeredWindow -> FALSE (the last error observed is pinned in x_layered_layered_attr_validation).
5. On success each of the three out pointers is optional (NULL skipped); outputs written only on success; *flags is masked with 3; alpha byte is the stored one (an unused alpha reads back as the stored byte). Out-pointers at/above the user probe limit are clamped (so are not faults, but are not testable validly).

## UpdateLayeredWindow relevant rules
- dwFlags & 0xffffffd0 != 0 -> error 87 (valid: ULW_COLORKEY 1, ULW_ALPHA 2, ULW_OPAQUE 4, ULW_EX_NORESIZE 8, plus 0x20 accepted by the mask; 0x10 and 0x40+ rejected).
- psize negative component, or prcDirty negative left/top -> 87.
- Invalid hwnd -> 1400. Non-layered window -> 87 (the documented layered requirement).
- A layered CHILD with pptDst/psize given: allowed only when the child-layered rules hold (top-level, or a manifested process); otherwise a status failure.
- Exclusivity: once Set made the window attribute-layered, UpdateLayeredWindow fails 87; once UpdateLayeredWindow gave an image, Set fails (exact codes pinned in the area).

## Who sets WS_EX_LAYERED
CreateWindowEx(WS_EX_LAYERED) or SetWindowLong(GWL_EXSTYLE) adding the bit. Removing the bit drops the attribute state; re-adding it requires a new Set call (Get fails again until then). Win8+ allows WS_EX_LAYERED on child windows (needs a manifest-compatible app; processes without the compatibility manifest keep the older rule; the child-window tests run both variants).


## Method

Every hard check was observed on Windows 10 first with an exploratory probe that only printed the behaviour matrix, then turned into assertions, then run in both modes on fresh guests until two or more consecutive runs were identical, including the printed lines. Internal-state orderings that cannot be reproduced from user mode were left out.
