/* kprobe_user_pointers/kprobe.h
 *
 * Shared declarations for the ONE area in this corpus allowed to pass bad user-mode pointers to
 * win32k syscalls (project rule: "nowhere else"). Every syscall exercised here was picked because
 * its win32kbase/win32kfull analysis shows an
 * explicit user-pointer guard - either a literal ProbeForWrite()/ProbeForRead() call, or the
 * equivalent manually-inlined pattern win32k uses in several places:
 *
 *     if ptr = *MmUserProbeAddress_exref;
 *     *ptr = *ptr;                      // (or a single-byte poke at ptr/ptr+size-1)
 *
 * which is ProbeForWrite's own logic inlined (clamp any address at/above the user/kernel split to
 * the one-page-wide "probe region" at MmUserProbeAddress, 0x7FFFFFFF0000 on x64, then touch it so
 * a genuinely bad/kernel-range caller address turns into a fault INSIDE that known-unmapped probe
 * page instead of walking off into whatever the raw pointer bits happened to decode to). Either
 * form means: a bad pointer here must come back as a documented failure return (0/FALSE/-1 per
 * the analysis's own early-return value) and MUST NOT raise a user-mode exception in this process -
 * the fault, if any, happens on the kernel side of the syscall and is turned into that return
 * value before control ever reaches ntdll's syscall-return path. See kp_veh_hit() below: every
 * test in this area wraps its calls in kp_veh_reset()/kp_veh_hit() specifically to make that
 * "no user-mode exception" expectation an explicit, counted CHECK, not just "the harness's own
 * crash-catcher didn't fire".
 *
 * Signatures below are NOT in any SDK header (none of these Nt* exports are) and were derived by
 * reading win32kfull.sys's/win32kbase.sys's own analysed body (parameter use + probe calls) and
 * cross-checking against the real user32.dll/gdi32full.dll caller's argument-building code -
 * never guessed. The exact source (function + address) is in the file header of whichever .c
 * calls each one.
 */
#ifndef KPROBE_H
#define KPROBE_H
#include "t.h"
#include "k.h"

/* ---- pointer values this area is allowed to pass (project exception, this area only) -------- */
/* Ordinary kernel VA range (any current x64 Windows build: top of canonical kernel space). */
#define KP_ADDR_KERNEL_GENERIC   ((void *)0xFFFF800000000000ULL)
/* KUSER_SHARED_DATA's *kernel-mode* alias (the user alias every process maps read-only at
 * 0x7FFE0000 is the SAME page; this is the kernel-VA mapping of it - still a kernel address, not
 * usable as a destination for a user-mode probe regardless of what it maps). */
#define KP_ADDR_KUSER_ALIAS      ((void *)0xFFFFF78000000000ULL)
/* MmUserProbeAddress itself on x64 19041 (confirmed live in this corpus: msg_send_cross_thread's
 * NtUserQuerySendMessage test and sc_callxxx_dispatch's dispatch_msgfilter.c both observe the
 * kernel clamping an out-of-range pointer to exactly this value before touching it). Passing it
 * directly is "one past the real top of user VA" - the first byte of the kernel's own one-page
 * probe-boundary region, never a valid destination either. */
#define KP_ADDR_BOUNDARY         ((void *)0x7FFFFFFF0000ULL)

/* ---- the 16-byte "length + buffer pointer" descriptor several NtUser* string getters take in
 * place of a bare (buffer,count) pair - user32.dll builds one on its own stack and passes its
 * address; the kernel dereferences the FIRST 4 bytes directly (no probe - the descriptor address
 * itself is always a real stack address from a real caller) to recover a WORD byte-count at
 * offset 2 and a PVOID buffer pointer at offset 8, which IS then probed. Verified identically in
 * win32kfull!NtUserGetClassName, !NtUserGetAtomName and !NtUserGetKeyboardLayoutName behavior
 * and in the stack layout user32 uses for GetClassNameW / GetKeyboardLayoutNameW (a pad, then the
 * count, then the buffer pointer at the next 8-byte slot). */
struct kp_strbuf { unsigned short unused0; unsigned short cb; unsigned int pad; void *buf; };

/* ---- syscalls exercised, declared (none are in any mingw/SDK header) -------------------------
 * user32!InternalGetWindowText / !GetClassNameW / !GetKeyboardLayoutNameW pass HWND, an unused
 * int, or a kp_strbuf* respectively in RDX/R8 exactly as shown in their analysis above the call. */
UDECL(int,  NtUserInternalGetWindowText, (HWND hwnd, LPWSTR buf, int cch));
UDECL(BOOL, NtUserGetClassName,          (HWND hwnd, int unused0, struct kp_strbuf *out));
UDECL(BOOL, NtUserGetAtomName,           (ATOM atom, struct kp_strbuf *out));
UDECL(BOOL, NtUserGetKeyboardLayoutName, (struct kp_strbuf *out));
UDECL(BOOL, NtUserGetClipboardFormatName,(UINT format, LPWSTR buf, int cch));
UDECL(BOOL, NtUserGetKeyboardState,      (BYTE *state256));
UDECL(BOOL, NtGdiGetTextMetricsW,        (HDC hdc, void *tm, UINT cb));
UDECL(DWORD,NtGdiGetBitmapBits_,         (HBITMAP hbmp, DWORD cb, LPVOID buf));
UDECL(int,  NtGdiGetPath_,               (HDC hdc, POINT *pts, BYTE *types, int cpt));
UDECL(UINT, NtGdiExtGetObjectW_,         (HGDIOBJ obj, int cb, LPVOID out));

/* ---- a VEH layered in FRONT of the harness's own (installed later == called first by
 * AddVectoredExceptionHandler's LIFO order) purely to make "no user-mode exception happened"
 * an explicit counted fact for every test in this area, instead of relying on the harness's
 * own crash-catcher (which would also longjmp and fail the test anyway, but does not by itself
 * tell a later CHECK how many hits there were). Always returns CONTINUE_SEARCH so the harness's
 * handler still runs and still protects the test loop if something truly crashes. */
void kp_veh_install(void);     /* idempotent; call once (done lazily by kp_veh_reset) */
void kp_veh_reset(void);       /* clears the hit counter before a probe call */
unsigned kp_veh_hits(void);    /* hit counter since the last reset */

/* ---- guard-page buffer: one committed, writable page of `usable` bytes immediately followed by
 * one RESERVED-but-not-committed page (task wording: "VirtualAlloc MEM_RESERVE / PAGE_NOACCESS
 * with a guard page at the exact end of a committed buffer"). A size-exact probe touches only the
 * committed page; a probe that reads/writes one byte past `usable` faults on the reserved page.
 * `buf` points at the START of the usable region (i.e. at base + (page_size - usable) so that
 * buf+usable lands exactly on the page boundary - every byte of slack before `buf` is itself
 * PAGE_NOACCESS too, so an off-by-a-few-bytes write is just as visible as an off-by-one). */
struct kp_guard { void *base /* 2-page MEM_RESERVE region */, *buf /* usable region start */; size_t usable; };
bool kp_guard_alloc(size_t usable, struct kp_guard *out);
void kp_guard_free(struct kp_guard *g);
/* Fills [buf, buf+usable) with a fixed, recognizable byte so a syscall that writes fewer bytes
 * than documented leaves a detectable tail, and a syscall that writes more (if it somehow could
 * without touching the guard page, e.g. through a size the kernel itself miscomputed) is still
 * bounded by the allocation's own PAGE_NOACCESS page regardless. */
void kp_guard_fill(struct kp_guard *g, unsigned char pattern);
/* True if every byte in [off, off+len) still equals `pattern` - i.e. the syscall wrote nothing
 * there. */
bool kp_guard_untouched(struct kp_guard *g, size_t off, size_t len, unsigned char pattern);

/* ---- small fixtures shared across this area's files, built from REQUIRE'd user-mode APIs only
 * (never from the Nt* stubs under test - a fixture must not depend on the thing it feeds). Each
 * returns a zero/NULL sentinel on failure; callers REQUIRE/ t_skip accordingly. */
HWND   kp_fixture_window(void);                 /* hidden, off-screen, non-activating (k_make_window) */
ATOM   kp_fixture_atom(const wchar_t *name);    /* AddAtomW; caller GlobalDeleteAtom when done */
UINT   kp_fixture_clip_format(void);            /* RegisterClipboardFormatW of a unique name */
HDC    kp_fixture_dc_with_path(void);           /* memory DC + BeginPath/LineTo/EndPath; DeleteDC when done */
HBITMAP kp_fixture_bitmap(int w, int h);        /* CreateBitmap(w,h,1,1,NULL); DeleteObject when done */
DWORD  kp_fixture_bitmap_bits_size(HBITMAP bmp);/* GetBitmapBits(bmp,0,NULL) - the real required size */

#endif
