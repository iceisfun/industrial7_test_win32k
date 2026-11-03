/* common/k.h - kernel-state helper layer for the win32k.sys / win32kbase.sys / win32kfull.sys
 * conformance corpus.
 *
 * This corpus cannot load or call the kernel. The only entry point into win32k is the syscall
 * interface exported by win32u.dll (NtUserXxx / NtGdiXxx / ... stubs executing SYSCALL with service
 * numbers 0x1000+). Tests therefore do one or both of:
 *   (a) call a win32u.dll Nt* stub directly (GetProcAddress, UDECL+UN) for a pinpoint kernel check
 *   (b) go through the normal user32/gdi32/gdi32full path to make the kernel do work
 * and then inspect LOCAL PROCESS STATE that the kernel produced/maintains: the TEB/PEB win32
 * fields, the GDI shared handle table, the USER shared handle table (desktop heap), the kernel
 * callback table, etc. This header is that inspection layer. Use it; do not reinvent it.
 *
 * Everything here is C99, no external libs, and works whether or not user32/gdi32 have been
 * loaded yet (most accessors take an explicit pointer/value rather than reaching out on their
 * own, so a test controls exactly when a module gets loaded - see the PRISTINE-STATE rule in
 * docs/KERNEL_TESTING.md).
 *
 * Field offsets are verified against the public ntdll.dll symbols for
 * _TEB/_PEB (build 19041.2965) where a struct exists; fields with no PDB struct (the GDI/USER
 * handle tables, _CLIENTINFO's raw layout) are derived from observing the real
 * callers (user32!IsWindow, user32!HMValidateHandle, gdi32full!GdiGetEntry, gdi32full!GetBkMode,
 * etc.) - each such constant says which function it was read from in a comment. Treat those as
 * "verified by observation", not "documented by Microsoft": re-check on the target build if a test
 * using them behaves unexpectedly on real hardware.
 */
#ifndef K_H
#define K_H
#include "t.h"
#include <stdbool.h>

/* =======================================================================
 * TEB / PEB offsets (ntdll.dll!type:_TEB, ntdll.dll!type:_PEB, 19041.2965)
 * ======================================================================= */

/* _TEB, size 0x1838 */
#define TEB_Win32ThreadInfo        0x078   /* void* - PTHREADINFO (win32kbase per-thread struct); NULL until the thread's first win32k syscall converts it to a GUI thread */
#define TEB_User32Reserved         0x080   /* ULONG[26] - scratch used by user32 (e.g. [0]=hWinSta-ish bookkeeping on some paths) */
#define TEB_WOW32Reserved          0x100   /* void* - wow64 only */
#define TEB_GdiTebBatch            0x2F0   /* _GDI_TEB_BATCH, size 0x4E8 (see GDI_TEB_BATCH_* below) */
#define TEB_GdiCachedProcessHandle 0x7E8   /* void* - HANDLE, process handle gdi32 caches for batch flush */
#define TEB_GdiClientPID           0x7F0   /* ULONG */
#define TEB_GdiClientTID           0x7F4   /* ULONG */
#define TEB_GdiThreadLocalInfo     0x7F8   /* void* */
#define TEB_Win32ClientInfo        0x800   /* ULONG64[62], size 0x1F0 - opaque to the PDB; this is _CLIENTINFO. Sub-offsets below are read from analysis, not a PDB type. */
#define TEB_glDispatchTable        0x9F0
#define TEB_glSectionInfo          0x1228
#define TEB_glSection              0x1230
#define TEB_glTable                0x1238
#define TEB_glCurrentRC            0x1240
#define TEB_glContext              0x1248
#define TEB_GdiBatchCount          0x1740  /* ULONG - entries queued in GdiTebBatch, flushed by NtGdiFlush/GdiFlush at 0x136 */

/* _CLIENTINFO sub-offsets, relative to TEB_Win32ClientInfo (0x800). No PDB type ships these names;
 * read from user32.dll!IsWindow / IsWindowVisible / HMValidateHandle / GetWindowLongW analysis
 * (19041.2965): all four index where Self == TEB+0x800 region start. */
#define CI_pDeskHeapBaseGuard  0x020  /* void** - if *(void**)(CI+0x20)==NULL, or its target != the handle-entry's cached heap-base word, the fast path is skipped and NtUserCallOneParam(.., FNID 0x38 / "GETWNDPTR") is used instead (see HMValidateHandle) */
#define CI_pDeskHeapClientBase 0x028  /* void* - client-side base address the desktop heap is mapped at; window pointer = *(void**)(CI+0x28) + <handle-entry Delta> */
#define CI_hWndCacheHandle     0x040  /* HWND  - last-validated window handle (fast-path cache slot) */
#define CI_hWndCachePtr        0x048  /* void* - window pointer for CI_hWndCacheHandle; IsWindow/GetWindowLongW/HMValidateHandle all special-case "hWnd == cache" to skip the handle-table walk entirely */

/* _GDI_TEB_BATCH (PDB-backed, e.g. aclayers.dll!type:_GDI_TEB_BATCH, size 1256 = 0x4E8) */
#define GDI_TEB_BATCH_Offset_HasRenderingCommand 0x000 /* bitfield: bits0-30 Offset, bit31 HasRenderingCommand */
#define GDI_TEB_BATCH_HDC                        0x008
#define GDI_TEB_BATCH_Buffer                      0x010 /* UCHAR[1224] batched GDI command stream */

/* _PEB, size 0x7C8 */
#define PEB_ImageBaseAddress     0x010
#define PEB_ProcessHeap          0x030
#define PEB_KernelCallbackTable  0x058  /* void* - PKERNEL_CALLBACK_TABLE; set by user32!ClientThreadSetup during the first user32 call */
#define PEB_UserSharedInfoPtr    0x058  /* same offset as KernelCallbackTable in the PDB (anonymous union): some builds alias user32's gSharedInfo pointer through here. Treat PEB+0x58 as "KernelCallbackTable once a GUI thread exists"; use user32!gSharedInfo directly (k_shared_info) rather than this field for SHAREDINFO access. */
#define PEB_GdiSharedHandleTable 0x0F8  /* void* - base of the per-process GDI handle table (gdi32full caches this as pGdiSharedHandleTable); entries are GDI_CELL_SIZE bytes, see k_gdi_cell() */
#define PEB_GdiDCAttributeList   0x108  /* ULONG */
#define PEB_GdiHandleBuffer      0x140  /* ULONG[60] - legacy pre-batch handle buffer, size 0xF0 */
#define PEB_SessionId            0x2C0

/* =======================================================================
 * Safe memory read - every pointer chase through the structures above
 * MUST go through this. Never dereference a chased pointer directly.
 * ======================================================================= */

/* Copies `size` bytes from `addr` (anywhere in this process, including bogus/kernel-range
 * addresses) into `out`. Returns TRUE on success, FALSE without crashing on failure.
 * Implemented with ReadProcessMemory(GetCurrentProcess(), ...), which ntdll services through
 * NtReadVirtualMemory and reports an unmapped/guard/kernel-range page as a normal failure
 * instead of raising - this is the documented, swap-safe way to probe a maybe-bad pointer in
 * your own process (no VEH races, no risk of skipping a real bug by catching its exception). */
bool k_try_read(const void *addr, void *out, size_t size);
/* Convenience wrappers for the common sizes. */
bool k_try_read_ptr(const void *addr, void **out);
bool k_try_read_u32(const void *addr, unsigned long *out);
bool k_try_read_u16(const void *addr, unsigned short *out);
bool k_try_read_u8(const void *addr, unsigned char *out);

/* TEB/PEB of the calling thread/process (NtCurrentTeb() reimplemented without pulling in the
 * NT_TIB macros; valid in any thread, before or after any win32k syscall). */
void *k_teb(void);
void *k_peb(void);

/* =======================================================================
 * win32u.dll syscall stub inspection
 * ======================================================================= */

/* The compiled shape of every win32u Nt* export on 19041 x64 is exactly:
 *   4C 8B D1            mov r10, rcx
 *   B8 xx xx xx xx      mov eax, <service number>
 *   F6 04 25 08 03 FE 7F 01   test byte [0x7FFE0308], 1      ; KUSER_SHARED_DATA->SystemCall... flag
 *   75 03               jne +3
 *   0F 05               syscall
 *   C3                  ret
 *   CD 2E               int 0x2E      ; legacy fallback target of the jne
 *   C3                  ret
 * (verified against win32u.dll!NtGdiGetEntry /asm and several others). k_stub_valid() checks the
 * fixed prologue bytes; k_stub_number() reads the eax immediate. */
bool   k_stub_valid(FARPROC stub);
long   k_stub_number(FARPROC stub);          /* -1 if k_stub_valid() is false */
size_t k_stub_bytes(FARPROC stub, unsigned char *out, size_t cap); /* raw bytes copied, <= cap */

/* Issues a raw SYSCALL with the given service number and up to 8 ULONG_PTR args, bypassing
 * win32u.dll entirely (useful once you already have the number from k_stub_number() or
 * tools/win32u_syscalls.tsv and want to call it without a resolved export, e.g. to probe an
 * invalid service number). NOT thread-safe (one shared trampoline buffer, patched in place);
 * call only from the test thread, never from two threads concurrently. */
typedef long long K_NTSTATUS;
K_NTSTATUS k_raw_syscall(unsigned long number,
                          ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4,
                          ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8);

/* =======================================================================
 * GDI shared handle table (PEB->GdiSharedHandleTable)
 * ======================================================================= */
#define GDI_CELL_SIZE 0x18  /* verified: gdi32full!HANDLE_TO_INDEX / GdiGetEntry index by (handle&0xffff)*0x18 */

struct k_gdicell {
    void *object;     /* cell+0x00: opaque kernel object pointer/tag, read by GdiGetEntry as its first qword; never dereference, compare only */
    unsigned long long field8; /* cell+0x08: second qword GdiGetEntry copies out (object-type-specific; e.g. holds the owning PID/flags word for a DC) */
    unsigned char type_check;  /* cell+0x0d: top byte of the handle (HIBYTE(HIWORD)) is compared against this byte by HANDLE_TO_INDEX/GdiGetEntry to detect a stale/reused handle */
    void *field10;    /* cell+0x10: third qword (e.g. pointer-sized attribute, DC_ATTR-adjacent on DC cells) */
};

/* Decodes the handle table cell for `obj` (any HGDIOBJ/HDC/HBITMAP/...). Returns false if
 * PEB->GdiSharedHandleTable is NULL (process not yet connected to the GDI table - see the
 * pristine-state rule) or the handle's low 16 bits are >= the table's cell count is not checkable
 * without the table size exported, so this only range-checks against a safe read of the cell. */
bool k_gdi_cell(void *obj, struct k_gdicell *out);
unsigned k_gdi_index(void *obj);        /* HANDLE_TO_INDEX: obj & 0xffff, with the type_check fallback applied */
unsigned char k_gdi_type_byte(void *obj); /* the stored type_check byte for obj's cell, or 0 if unreadable */

/* =======================================================================
 * USER shared info / handle table (desktop heap)
 * ======================================================================= */

/* gSharedInfo's layout has no public PDB type; fields below are read at the offsets the real
 * user32 code (IsWindow/HMValidateHandle) uses relative to gSharedInfo.psi and to the
 * cached table-base/entry-size globals those functions read once at module init. We read the
 * live cached values out of user32.dll's data rather than re-deriving them, so this works
 * regardless of which internal globals a given build caches them in. */
struct k_user_shared_info {
    void *psi;            /* SERVERINFO* - gSharedInfo+0x00 */
    void *aheList;         /* HANDLEENTRY* - base of the USER handle table, gSharedInfo+0x08 */
    unsigned long heEntrySize; /* gSharedInfo+0x10 (expected USER_HANDLE_ENTRY_SIZE, 0x18) */
};
/* GetProcAddress(user32.dll, "gSharedInfo"); NULL if user32 is not loaded. Do not call this from
 * a "before first GUI call" test - resolving it loads user32 like any other U(). */
void *k_shared_info_ptr(void);
bool  k_shared_info(struct k_user_shared_info *out);

#define USER_HANDLE_SIZE 0x18 /* verified via gSharedInfo.HeEntrySize at runtime; matches IsWindow's indexing */

struct k_user_entry {
    void *delta;      /* entry+0x00: NOT a pointer - added to the desktop-heap client base (CI_pDeskHeapClientBase) to get the object pointer (ulClientDelta pattern); verified via IsWindow/HMValidateHandle */
    void *field10;     /* entry+0x10: compared against *CI_pDeskHeapBaseGuard to decide whether the client-side desktop heap mapping is current */
    unsigned char type;  /* entry+0x18: object type tag (1=TYPE_WINDOW per IsWindow; see k_handle_type enum below) */
    unsigned char flags;  /* entry+0x19: bit0 observed set once the object is destroyed/freed (HMValidateHandle treats bit0!=0 as invalid) */
    unsigned short uniq;   /* entry+0x1a: compared against HIWORD(handle) (or -1 wildcard) to reject a stale/reused handle */
};
enum k_handle_type { K_TYPE_FREE = 0, K_TYPE_WINDOW = 1 };

/* Decodes the USER handle-table entry for a HWND/HMENU/HACCEL/... Returns false if gSharedInfo
 * is unavailable, the index is out of range of the handle-table entry count worth of entries, or the
 * entry cannot be safely read. */
bool k_user_entry(HANDLE h, struct k_user_entry *out);
/* Resolves the desktop-heap client pointer for a handle the way IsWindow's fast path does:
 * CI_pDeskHeapClientBase + entry.delta, gated on CI_pDeskHeapBaseGuard matching entry.field10
 * exactly as the real code checks it. Returns NULL (and leaves *stale untouched) if the fast
 * path is not valid for this handle right now - callers that need the pointer unconditionally
 * should fall back to the real API (e.g. GetWindowLongPtrW) which asks the kernel instead. */
void *k_wnd_ptr(HWND hwnd);

/* =======================================================================
 * Kernel callback table (PEB->KernelCallbackTable)
 * ======================================================================= */
#define K_CB_MAX 160  /* generous upper bound; the real apfnDispatch table on 19041 has 129 non-NULL
                       * entries (user32.dll!apfnDispatch, verified by reading the raw
                       * table bytes and resolving each qword as a function RVA against user32.dll) -
                       * was 128, which silently truncated k_cb_count()'s scan one slot short of the
                       * real table and made index 128 (__xxxClientCallDefWindowProc) unreachable by
                       * k_cb_hook(); bumped past 129 with margin, we only read what k_cb_count() finds */

void   *k_cb_table(void);          /* PEB->KernelCallbackTable, or NULL if not yet set (pre-GUI-thread) */
/* There is no PDB-published entry count; this returns the number of consecutive non-NULL
 * pointer-sized slots from the table base up to K_CB_MAX, which is how many a test can safely
 * index/hook. Treat the exact count as t_note()-worthy (version-dependent), never CHECK it
 * against a hardcoded number. */
unsigned k_cb_count(void);

/* Installs a counting trampoline over kernel-callback-table slot `index`: calls increment a
 * counter and record the raw args before chaining to the original entry, then restore it.
 * Uses VirtualProtect to make the table page writable and guarantees the original bytes are
 * restored (atexit-registered as a backstop in addition to k_cb_unhook_all()). Returns false if
 * the slot is out of range or already hooked. */
bool k_cb_hook(unsigned index);
void k_cb_unhook_all(void);
unsigned long k_cb_hit_count(unsigned index);          /* 0 if never hit or not hooked */
void *k_cb_original(unsigned index);                    /* original entry pointer, or NULL */
/* Raw arguments of the most recent call through hooked slot `index` (up to 4; most kernel
 * callbacks take a single struct pointer, which is what argv[0] will hold). */
bool k_cb_last_args(unsigned index, ULONG_PTR argv[4]);

/* =======================================================================
 * Windows/UI helpers
 * ======================================================================= */

/* A tiny, invisible, non-activating, off-screen window of a private window class, suitable for
 * "a real HWND to probe" without disturbing the interactive desktop. Destroy with DestroyWindow.
 * Requires user32 (loads it like any U() call - do not use from a pristine-state test). */
HWND k_make_window(const char *title);

/* Pumps this thread's message queue for up to ms milliseconds (PeekMessage+Dispatch, no
 * GetMessage block), so queued kernel callbacks (WM_*, hook callouts) get delivered without
 * risking an unbounded wait. Returns the number of messages dispatched. */
int k_pump(unsigned ms);

/* Runs fn(arg) on a new thread that pumps its own message loop until fn returns; joins and
 * returns fn's return value. Useful for testing per-thread GUI state (Win32ThreadInfo, message
 * queues) in isolation from the calling thread's. */
typedef DWORD (*k_thread_fn)(void *arg);
DWORD k_run_gui_thread(k_thread_fn fn, void *arg);

/* =======================================================================
 * Child-process helpers (pristine-state tests, per-process quota tests, mitigation-policy tests)
 * ======================================================================= */

/* Child reporting convention: a child launched through t_run_child()/k_child_run() uses the same
 * CHECK/EXPECT_* macros as a normal TEST() body (they just print to the child's stdout) and
 * returns its own failure count as the process exit code; 0 = all good. k_child_run() captures
 * the child's stdout through an inheritable pipe, relays every line to the parent's own stdout
 * prefixed "  CHILD <name>: ", and returns the child's exit code. The PARENT test should
 * CHECK(k_child_run(...) == 0) so a child failure fails the parent test too. */
struct k_child_result {
    DWORD exit_code;     /* 0xDEAD0000 spawn failed, 0xDEAD0001 timeout (killed) */
    int   relayed_lines; /* number of lines captured from the child's stdout */
};
struct k_child_result k_child_run(const char *name, const char *extra_args, DWORD timeout_ms);

/* Same, but lets the caller pass a STARTUPINFOEX with an extended attribute list already filled
 * in (e.g. PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY) - process-wide/irreversible settings belong
 * only in a child, never in the main test exe. `siex` must have siex->StartupInfo.cb set by the
 * caller; this function adds STARTF_USESTDHANDLES/pipe wiring on top. */
struct k_child_result k_child_run_ex(const char *name, const char *extra_args, DWORD timeout_ms, LPSTARTUPINFOEXA siex);

/* =======================================================================
 * Misc environment helpers
 * ======================================================================= */

const char *k_status_name(K_NTSTATUS st);  /* "STATUS_SUCCESS" etc for the common ones this corpus checks; hex string if unknown (returned buffer is a small static ring - copy out before the next call if you need two at once) */

void k_win_build(unsigned long *build, unsigned long *ubr); /* RtlGetVersion-based */
bool k_is_19041(void);                      /* build == 19041; t_note+true-passthrough is typical usage */

unsigned long k_integrity_level(void);      /* SECURITY_MANDATORY_*_RID, via GetTokenInformation(TokenIntegrityLevel) */
bool k_is_elevated(void);                   /* GetTokenInformation(TokenElevation) */
unsigned long k_session_id(void);           /* ProcessIdToSessionId(current) */
bool k_has_interactive_desktop(void);       /* OpenInputDesktop succeeds and is not a Winlogon/secure desktop */

/* =======================================================================
 * Complete _TEB / _PEB field offsets (ntdll.dll!type:_TEB size 0x1838, ntdll.dll!type:_PEB size 0x7C8,
 * 19041.2965 PDB). Every PDB member that is not a
 * bitfield or padding gets TEB_<member> / PEB_<member>; each define is guarded so the earlier
 * hand-written names above (and area-local headers) keep their value. Added by the
 * teb_peb_win32_fields area.
 * ======================================================================= */
#ifndef TEB_SIZE
#define TEB_SIZE 0x1838
#endif
#ifndef PEB_SIZE
#define PEB_SIZE 0x7C8
#endif
#ifndef TEB_NtTib
#define TEB_NtTib 0x0
#endif
#ifndef TEB_EnvironmentPointer
#define TEB_EnvironmentPointer 0x38
#endif
#ifndef TEB_ClientId
#define TEB_ClientId 0x40
#endif
#ifndef TEB_ActiveRpcHandle
#define TEB_ActiveRpcHandle 0x50
#endif
#ifndef TEB_ThreadLocalStoragePointer
#define TEB_ThreadLocalStoragePointer 0x58
#endif
#ifndef TEB_ProcessEnvironmentBlock
#define TEB_ProcessEnvironmentBlock 0x60
#endif
#ifndef TEB_LastErrorValue
#define TEB_LastErrorValue 0x68
#endif
#ifndef TEB_CountOfOwnedCriticalSections
#define TEB_CountOfOwnedCriticalSections 0x6C
#endif
#ifndef TEB_CsrClientThread
#define TEB_CsrClientThread 0x70
#endif
#ifndef TEB_Win32ThreadInfo
#define TEB_Win32ThreadInfo 0x78
#endif
#ifndef TEB_User32Reserved
#define TEB_User32Reserved 0x80
#endif
#ifndef TEB_UserReserved
#define TEB_UserReserved 0xE8
#endif
#ifndef TEB_WOW32Reserved
#define TEB_WOW32Reserved 0x100
#endif
#ifndef TEB_CurrentLocale
#define TEB_CurrentLocale 0x108
#endif
#ifndef TEB_FpSoftwareStatusRegister
#define TEB_FpSoftwareStatusRegister 0x10C
#endif
#ifndef TEB_ReservedForDebuggerInstrumentation
#define TEB_ReservedForDebuggerInstrumentation 0x110
#endif
#ifndef TEB_SystemReserved1
#define TEB_SystemReserved1 0x190
#endif
#ifndef TEB_PlaceholderCompatibilityMode
#define TEB_PlaceholderCompatibilityMode 0x280
#endif
#ifndef TEB_PlaceholderHydrationAlwaysExplicit
#define TEB_PlaceholderHydrationAlwaysExplicit 0x281
#endif
#ifndef TEB_PlaceholderReserved
#define TEB_PlaceholderReserved 0x282
#endif
#ifndef TEB_ProxiedProcessId
#define TEB_ProxiedProcessId 0x28C
#endif
#ifndef TEB_ActivationStack
#define TEB_ActivationStack 0x290
#endif
#ifndef TEB_WorkingOnBehalfTicket
#define TEB_WorkingOnBehalfTicket 0x2B8
#endif
#ifndef TEB_ExceptionCode
#define TEB_ExceptionCode 0x2C0
#endif
#ifndef TEB_ActivationContextStackPointer
#define TEB_ActivationContextStackPointer 0x2C8
#endif
#ifndef TEB_InstrumentationCallbackSp
#define TEB_InstrumentationCallbackSp 0x2D0
#endif
#ifndef TEB_InstrumentationCallbackPreviousPc
#define TEB_InstrumentationCallbackPreviousPc 0x2D8
#endif
#ifndef TEB_InstrumentationCallbackPreviousSp
#define TEB_InstrumentationCallbackPreviousSp 0x2E0
#endif
#ifndef TEB_TxFsContext
#define TEB_TxFsContext 0x2E8
#endif
#ifndef TEB_InstrumentationCallbackDisabled
#define TEB_InstrumentationCallbackDisabled 0x2EC
#endif
#ifndef TEB_UnalignedLoadStoreExceptions
#define TEB_UnalignedLoadStoreExceptions 0x2ED
#endif
#ifndef TEB_GdiTebBatch
#define TEB_GdiTebBatch 0x2F0
#endif
#ifndef TEB_RealClientId
#define TEB_RealClientId 0x7D8
#endif
#ifndef TEB_GdiCachedProcessHandle
#define TEB_GdiCachedProcessHandle 0x7E8
#endif
#ifndef TEB_GdiClientPID
#define TEB_GdiClientPID 0x7F0
#endif
#ifndef TEB_GdiClientTID
#define TEB_GdiClientTID 0x7F4
#endif
#ifndef TEB_GdiThreadLocalInfo
#define TEB_GdiThreadLocalInfo 0x7F8
#endif
#ifndef TEB_Win32ClientInfo
#define TEB_Win32ClientInfo 0x800
#endif
#ifndef TEB_glDispatchTable
#define TEB_glDispatchTable 0x9F0
#endif
#ifndef TEB_glReserved1
#define TEB_glReserved1 0x1138
#endif
#ifndef TEB_glReserved2
#define TEB_glReserved2 0x1220
#endif
#ifndef TEB_glSectionInfo
#define TEB_glSectionInfo 0x1228
#endif
#ifndef TEB_glSection
#define TEB_glSection 0x1230
#endif
#ifndef TEB_glTable
#define TEB_glTable 0x1238
#endif
#ifndef TEB_glCurrentRC
#define TEB_glCurrentRC 0x1240
#endif
#ifndef TEB_glContext
#define TEB_glContext 0x1248
#endif
#ifndef TEB_LastStatusValue
#define TEB_LastStatusValue 0x1250
#endif
#ifndef TEB_StaticUnicodeString
#define TEB_StaticUnicodeString 0x1258
#endif
#ifndef TEB_StaticUnicodeBuffer
#define TEB_StaticUnicodeBuffer 0x1268
#endif
#ifndef TEB_DeallocationStack
#define TEB_DeallocationStack 0x1478
#endif
#ifndef TEB_TlsSlots
#define TEB_TlsSlots 0x1480
#endif
#ifndef TEB_TlsLinks
#define TEB_TlsLinks 0x1680
#endif
#ifndef TEB_Vdm
#define TEB_Vdm 0x1690
#endif
#ifndef TEB_ReservedForNtRpc
#define TEB_ReservedForNtRpc 0x1698
#endif
#ifndef TEB_DbgSsReserved
#define TEB_DbgSsReserved 0x16A0
#endif
#ifndef TEB_HardErrorMode
#define TEB_HardErrorMode 0x16B0
#endif
#ifndef TEB_Instrumentation
#define TEB_Instrumentation 0x16B8
#endif
#ifndef TEB_ActivityId
#define TEB_ActivityId 0x1710
#endif
#ifndef TEB_SubProcessTag
#define TEB_SubProcessTag 0x1720
#endif
#ifndef TEB_PerflibData
#define TEB_PerflibData 0x1728
#endif
#ifndef TEB_EtwTraceData
#define TEB_EtwTraceData 0x1730
#endif
#ifndef TEB_WinSockData
#define TEB_WinSockData 0x1738
#endif
#ifndef TEB_GdiBatchCount
#define TEB_GdiBatchCount 0x1740
#endif
#ifndef TEB_CurrentIdealProcessor
#define TEB_CurrentIdealProcessor 0x1744
#endif
#ifndef TEB_IdealProcessorValue
#define TEB_IdealProcessorValue 0x1744
#endif
#ifndef TEB_ReservedPad0
#define TEB_ReservedPad0 0x1744
#endif
#ifndef TEB_ReservedPad1
#define TEB_ReservedPad1 0x1745
#endif
#ifndef TEB_ReservedPad2
#define TEB_ReservedPad2 0x1746
#endif
#ifndef TEB_IdealProcessor
#define TEB_IdealProcessor 0x1747
#endif
#ifndef TEB_GuaranteedStackBytes
#define TEB_GuaranteedStackBytes 0x1748
#endif
#ifndef TEB_ReservedForPerf
#define TEB_ReservedForPerf 0x1750
#endif
#ifndef TEB_ReservedForOle
#define TEB_ReservedForOle 0x1758
#endif
#ifndef TEB_WaitingOnLoaderLock
#define TEB_WaitingOnLoaderLock 0x1760
#endif
#ifndef TEB_SavedPriorityState
#define TEB_SavedPriorityState 0x1768
#endif
#ifndef TEB_ReservedForCodeCoverage
#define TEB_ReservedForCodeCoverage 0x1770
#endif
#ifndef TEB_ThreadPoolData
#define TEB_ThreadPoolData 0x1778
#endif
#ifndef TEB_TlsExpansionSlots
#define TEB_TlsExpansionSlots 0x1780
#endif
#ifndef TEB_DeallocationBStore
#define TEB_DeallocationBStore 0x1788
#endif
#ifndef TEB_BStoreLimit
#define TEB_BStoreLimit 0x1790
#endif
#ifndef TEB_MuiGeneration
#define TEB_MuiGeneration 0x1798
#endif
#ifndef TEB_IsImpersonating
#define TEB_IsImpersonating 0x179C
#endif
#ifndef TEB_NlsCache
#define TEB_NlsCache 0x17A0
#endif
#ifndef TEB_pShimData
#define TEB_pShimData 0x17A8
#endif
#ifndef TEB_HeapData
#define TEB_HeapData 0x17B0
#endif
#ifndef TEB_CurrentTransactionHandle
#define TEB_CurrentTransactionHandle 0x17B8
#endif
#ifndef TEB_ActiveFrame
#define TEB_ActiveFrame 0x17C0
#endif
#ifndef TEB_FlsData
#define TEB_FlsData 0x17C8
#endif
#ifndef TEB_PreferredLanguages
#define TEB_PreferredLanguages 0x17D0
#endif
#ifndef TEB_UserPrefLanguages
#define TEB_UserPrefLanguages 0x17D8
#endif
#ifndef TEB_MergedPrefLanguages
#define TEB_MergedPrefLanguages 0x17E0
#endif
#ifndef TEB_MuiImpersonation
#define TEB_MuiImpersonation 0x17E8
#endif
#ifndef TEB_CrossTebFlags
#define TEB_CrossTebFlags 0x17EC
#endif
#ifndef TEB_SameTebFlags
#define TEB_SameTebFlags 0x17EE
#endif
#ifndef TEB_TxnScopeEnterCallback
#define TEB_TxnScopeEnterCallback 0x17F0
#endif
#ifndef TEB_TxnScopeExitCallback
#define TEB_TxnScopeExitCallback 0x17F8
#endif
#ifndef TEB_TxnScopeContext
#define TEB_TxnScopeContext 0x1800
#endif
#ifndef TEB_LockCount
#define TEB_LockCount 0x1808
#endif
#ifndef TEB_WowTebOffset
#define TEB_WowTebOffset 0x180C
#endif
#ifndef TEB_ResourceRetValue
#define TEB_ResourceRetValue 0x1810
#endif
#ifndef TEB_ReservedForWdf
#define TEB_ReservedForWdf 0x1818
#endif
#ifndef TEB_ReservedForCrt
#define TEB_ReservedForCrt 0x1820
#endif
#ifndef TEB_EffectiveContainerId
#define TEB_EffectiveContainerId 0x1828
#endif
#ifndef PEB_InheritedAddressSpace
#define PEB_InheritedAddressSpace 0x0
#endif
#ifndef PEB_ReadImageFileExecOptions
#define PEB_ReadImageFileExecOptions 0x1
#endif
#ifndef PEB_BeingDebugged
#define PEB_BeingDebugged 0x2
#endif
#ifndef PEB_BitField
#define PEB_BitField 0x3
#endif
#ifndef PEB_Mutant
#define PEB_Mutant 0x8
#endif
#ifndef PEB_ImageBaseAddress
#define PEB_ImageBaseAddress 0x10
#endif
#ifndef PEB_Ldr
#define PEB_Ldr 0x18
#endif
#ifndef PEB_ProcessParameters
#define PEB_ProcessParameters 0x20
#endif
#ifndef PEB_SubSystemData
#define PEB_SubSystemData 0x28
#endif
#ifndef PEB_ProcessHeap
#define PEB_ProcessHeap 0x30
#endif
#ifndef PEB_FastPebLock
#define PEB_FastPebLock 0x38
#endif
#ifndef PEB_AtlThunkSListPtr
#define PEB_AtlThunkSListPtr 0x40
#endif
#ifndef PEB_IFEOKey
#define PEB_IFEOKey 0x48
#endif
#ifndef PEB_CrossProcessFlags
#define PEB_CrossProcessFlags 0x50
#endif
#ifndef PEB_KernelCallbackTable
#define PEB_KernelCallbackTable 0x58
#endif
#ifndef PEB_UserSharedInfoPtr
#define PEB_UserSharedInfoPtr 0x58
#endif
#ifndef PEB_SystemReserved
#define PEB_SystemReserved 0x60
#endif
#ifndef PEB_AtlThunkSListPtr32
#define PEB_AtlThunkSListPtr32 0x64
#endif
#ifndef PEB_ApiSetMap
#define PEB_ApiSetMap 0x68
#endif
#ifndef PEB_TlsExpansionCounter
#define PEB_TlsExpansionCounter 0x70
#endif
#ifndef PEB_TlsBitmap
#define PEB_TlsBitmap 0x78
#endif
#ifndef PEB_TlsBitmapBits
#define PEB_TlsBitmapBits 0x80
#endif
#ifndef PEB_ReadOnlySharedMemoryBase
#define PEB_ReadOnlySharedMemoryBase 0x88
#endif
#ifndef PEB_SharedData
#define PEB_SharedData 0x90
#endif
#ifndef PEB_ReadOnlyStaticServerData
#define PEB_ReadOnlyStaticServerData 0x98
#endif
#ifndef PEB_AnsiCodePageData
#define PEB_AnsiCodePageData 0xA0
#endif
#ifndef PEB_OemCodePageData
#define PEB_OemCodePageData 0xA8
#endif
#ifndef PEB_UnicodeCaseTableData
#define PEB_UnicodeCaseTableData 0xB0
#endif
#ifndef PEB_NumberOfProcessors
#define PEB_NumberOfProcessors 0xB8
#endif
#ifndef PEB_NtGlobalFlag
#define PEB_NtGlobalFlag 0xBC
#endif
#ifndef PEB_CriticalSectionTimeout
#define PEB_CriticalSectionTimeout 0xC0
#endif
#ifndef PEB_HeapSegmentReserve
#define PEB_HeapSegmentReserve 0xC8
#endif
#ifndef PEB_HeapSegmentCommit
#define PEB_HeapSegmentCommit 0xD0
#endif
#ifndef PEB_HeapDeCommitTotalFreeThreshold
#define PEB_HeapDeCommitTotalFreeThreshold 0xD8
#endif
#ifndef PEB_HeapDeCommitFreeBlockThreshold
#define PEB_HeapDeCommitFreeBlockThreshold 0xE0
#endif
#ifndef PEB_NumberOfHeaps
#define PEB_NumberOfHeaps 0xE8
#endif
#ifndef PEB_MaximumNumberOfHeaps
#define PEB_MaximumNumberOfHeaps 0xEC
#endif
#ifndef PEB_ProcessHeaps
#define PEB_ProcessHeaps 0xF0
#endif
#ifndef PEB_GdiSharedHandleTable
#define PEB_GdiSharedHandleTable 0xF8
#endif
#ifndef PEB_ProcessStarterHelper
#define PEB_ProcessStarterHelper 0x100
#endif
#ifndef PEB_GdiDCAttributeList
#define PEB_GdiDCAttributeList 0x108
#endif
#ifndef PEB_LoaderLock
#define PEB_LoaderLock 0x110
#endif
#ifndef PEB_OSMajorVersion
#define PEB_OSMajorVersion 0x118
#endif
#ifndef PEB_OSMinorVersion
#define PEB_OSMinorVersion 0x11C
#endif
#ifndef PEB_OSBuildNumber
#define PEB_OSBuildNumber 0x120
#endif
#ifndef PEB_OSCSDVersion
#define PEB_OSCSDVersion 0x122
#endif
#ifndef PEB_OSPlatformId
#define PEB_OSPlatformId 0x124
#endif
#ifndef PEB_ImageSubsystem
#define PEB_ImageSubsystem 0x128
#endif
#ifndef PEB_ImageSubsystemMajorVersion
#define PEB_ImageSubsystemMajorVersion 0x12C
#endif
#ifndef PEB_ImageSubsystemMinorVersion
#define PEB_ImageSubsystemMinorVersion 0x130
#endif
#ifndef PEB_ActiveProcessAffinityMask
#define PEB_ActiveProcessAffinityMask 0x138
#endif
#ifndef PEB_GdiHandleBuffer
#define PEB_GdiHandleBuffer 0x140
#endif
#ifndef PEB_PostProcessInitRoutine
#define PEB_PostProcessInitRoutine 0x230
#endif
#ifndef PEB_TlsExpansionBitmap
#define PEB_TlsExpansionBitmap 0x238
#endif
#ifndef PEB_TlsExpansionBitmapBits
#define PEB_TlsExpansionBitmapBits 0x240
#endif
#ifndef PEB_SessionId
#define PEB_SessionId 0x2C0
#endif
#ifndef PEB_AppCompatFlags
#define PEB_AppCompatFlags 0x2C8
#endif
#ifndef PEB_AppCompatFlagsUser
#define PEB_AppCompatFlagsUser 0x2D0
#endif
#ifndef PEB_pShimData
#define PEB_pShimData 0x2D8
#endif
#ifndef PEB_AppCompatInfo
#define PEB_AppCompatInfo 0x2E0
#endif
#ifndef PEB_CSDVersion
#define PEB_CSDVersion 0x2E8
#endif
#ifndef PEB_ActivationContextData
#define PEB_ActivationContextData 0x2F8
#endif
#ifndef PEB_ProcessAssemblyStorageMap
#define PEB_ProcessAssemblyStorageMap 0x300
#endif
#ifndef PEB_SystemDefaultActivationContextData
#define PEB_SystemDefaultActivationContextData 0x308
#endif
#ifndef PEB_SystemAssemblyStorageMap
#define PEB_SystemAssemblyStorageMap 0x310
#endif
#ifndef PEB_MinimumStackCommit
#define PEB_MinimumStackCommit 0x318
#endif
#ifndef PEB_SparePointers
#define PEB_SparePointers 0x320
#endif
#ifndef PEB_SpareUlongs
#define PEB_SpareUlongs 0x340
#endif
#ifndef PEB_WerRegistrationData
#define PEB_WerRegistrationData 0x358
#endif
#ifndef PEB_WerShipAssertPtr
#define PEB_WerShipAssertPtr 0x360
#endif
#ifndef PEB_pUnused
#define PEB_pUnused 0x368
#endif
#ifndef PEB_pImageHeaderHash
#define PEB_pImageHeaderHash 0x370
#endif
#ifndef PEB_TracingFlags
#define PEB_TracingFlags 0x378
#endif
#ifndef PEB_CsrServerReadOnlySharedMemoryBase
#define PEB_CsrServerReadOnlySharedMemoryBase 0x380
#endif
#ifndef PEB_TppWorkerpListLock
#define PEB_TppWorkerpListLock 0x388
#endif
#ifndef PEB_TppWorkerpList
#define PEB_TppWorkerpList 0x390
#endif
#ifndef PEB_WaitOnAddressHashTable
#define PEB_WaitOnAddressHashTable 0x3A0
#endif
#ifndef PEB_TelemetryCoverageHeader
#define PEB_TelemetryCoverageHeader 0x7A0
#endif
#ifndef PEB_CloudFileFlags
#define PEB_CloudFileFlags 0x7A8
#endif
#ifndef PEB_CloudFileDiagFlags
#define PEB_CloudFileDiagFlags 0x7AC
#endif
#ifndef PEB_PlaceholderCompatibilityMode
#define PEB_PlaceholderCompatibilityMode 0x7B0
#endif
#ifndef PEB_PlaceholderCompatibilityModeReserved
#define PEB_PlaceholderCompatibilityModeReserved 0x7B1
#endif
#ifndef PEB_LeapSecondData
#define PEB_LeapSecondData 0x7B8
#endif
#ifndef PEB_LeapSecondFlags
#define PEB_LeapSecondFlags 0x7C0
#endif
#ifndef PEB_NtGlobalFlag2
#define PEB_NtGlobalFlag2 0x7C4
#endif

/* _NT_TIB members (TEB+0x00, ntdll.dll!type:_NT_TIB) */
#ifndef TEB_StackBase
#define TEB_StackBase 0x08
#endif
#ifndef TEB_StackLimit
#define TEB_StackLimit 0x10
#endif
#ifndef TEB_Self
#define TEB_Self 0x30
#endif

/* Values kernel win32k writes into the process (win32kbase!GdiProcessCallout: PEB->GdiDCAttributeList = 0x14,
 * memset(PEB+0x140, 0, 0xF0)); GDI shared-memory layout (gdi32full!GetStockObject / GdiProcessSetup). */
#ifndef K_GDI_DC_ATTRIBUTE_LIST_INIT
#define K_GDI_DC_ATTRIBUTE_LIST_INIT 0x14
#endif
#ifndef K_GDI_SHARED_STOCK_OBJECTS
#define K_GDI_SHARED_STOCK_OBJECTS 0x1800B0  /* HGDIOBJ[22], index = GetStockObject() argument (gdi32full!GetStockObject) */
#endif
#ifndef K_GDI_SHARED_DEVCAPS
#define K_GDI_SHARED_DEVCAPS 0x180000        /* pGdiDevCaps = pGdiSharedMemory + 0x180000 (gdi32full!GdiProcessSetup) */
#endif
#ifndef K_GDI_SHARED_STOCK_DEFGUIFONT
#define K_GDI_SHARED_STOCK_DEFGUIFONT 0x180138
#endif
#ifndef K_GDI_TEB_BATCH_RECORDS
#define K_GDI_TEB_BATCH_RECORDS 0x300        /* first batch record, == TEB_GdiTebBatch + GDI_TEB_BATCH_Buffer */
#endif
#ifndef K_GDI_TEB_BATCH_LIMIT
#define K_GDI_TEB_BATCH_LIMIT 0x7D8          /* end of the record area (TEB+0x7D8) */
#endif

#endif
