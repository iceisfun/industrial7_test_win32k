/* kprobe_user_pointers/kprobe_nullreq.c
 *
 * NULL for a required OUT pointer, called directly through the win32u.dll stub (never through
 * user32/gdi32full - see the note in each test for why the wrapper itself is not safe to drive
 * with a bad pointer here). Every syscall below has a win32kfull/win32kbase analysis branch that
 * dereferences the pointer unconditionally once a short-circuit condition is false; for NULL that
 * condition is false (NULL is numerically "in range" of every clamp this
 * corpus has seen, so NULL is never clamped - it reaches the real dereference/ProbeForWrite call
 * exactly like a real buffer address would, just at address 0). Expected on real Windows: the
 * syscall's own documented failure sentinel, no user-mode exception (kp_veh_hits() stays 0 - the
 * fault, if the kernel's own access even faults instead of failing some other way, happens on the
 * kernel side and is turned into that return value before ntdll's syscall-return code resumes).
 *
 * Two syscalls in this area (NtGdiGetBitmapBits, NtGdiExtGetObjectW) treat a NULL OUT pointer as
 * a documented "just tell me the size" query mode instead of an error - those are NOT here, they
 * are the positive buffer-size-protocol tests in kprobe_guardpage.c.
 */
#include "kprobe.h"

/* ---- NtUserInternalGetWindowText(HWND, LPWSTR, int) ----------------------------------------
 * A non-zero cch always reaches the probe; NULL buf is the address it probes.
 * NOT tested through user32!GetWindowTextW/InternalGetWindowText: that wrapper does
 *  AFTER the syscall - i.e. it writes to OUR pointer argument
 * itself on a failed call, which would crash with a NULL buffer regardless of what the kernel
 * did. That is a real, separate footgun in the wrapper, not the kernel guard this area targets,
 * so it is deliberately avoided here by calling the win32u stub directly. */
TEST(kprobe_user_pointers_internalgetwindowtext_null_buffer) {
    typedef int (WINAPI *fn_t)(HWND, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserInternalGetWindowText");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    kp_veh_reset();
    int r = p(hwnd, NULL, 8);
    CHECKF(r == 0, "NtUserInternalGetWindowText(valid hwnd, NULL, 8) got %d want 0 (failure)", r);
    CHECKF(kp_veh_hits() == 0, "NULL buffer probe must fail inside the kernel, not raise a "
           "user-mode exception in this process (hits=%u)", kp_veh_hits());
    U(DestroyWindow)(hwnd);
}
/* cch == 0 skips the probe branch entirely (), so a NULL buffer with cch == 0
 * is harmless by the same analysis line - documented protocol, not a probe-failure case. */
TEST(kprobe_user_pointers_internalgetwindowtext_null_buffer_zero_count_is_harmless) {
    typedef int (WINAPI *fn_t)(HWND, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserInternalGetWindowText");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    kp_veh_reset();
    int r = p(hwnd, NULL, 0);
    CHECKF(kp_veh_hits() == 0, "cch==0 must never touch the buffer pointer at all (hits=%u)", kp_veh_hits());
    t_note("InternalGetWindowText(hwnd, NULL, 0) returned %d (analysis: argument 3==0 skips the probe "
           "branch and returns 0 unconditionally before it)", r);
    CHECK(r == 0);
    U(DestroyWindow)(hwnd);
}

/* ---- NtUserGetClassName(HWND, int, struct kp_strbuf *) --------------------------------------
 * the OUTER descriptor pointer is only
 * clamped when it is >= MmUserProbeAddress; NULL is not, so  dereferences address 0
 * directly. */
TEST(kprobe_user_pointers_getclassname_null_descriptor) {
    typedef BOOL (WINAPI *fn_t)(HWND, int, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClassName");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    kp_veh_reset();
    BOOL r = p(hwnd, 0, NULL);
    CHECKF(!r, "NtUserGetClassName(valid hwnd, 0, NULL) got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "NULL descriptor must fail cleanly, not raise (hits=%u)", kp_veh_hits());
    U(DestroyWindow)(hwnd);
}
/* Descriptor itself valid (real stack struct) but its inner buffer pointer is NULL: analysis then
 * does ProbeForWrite with == NULL - the inner probe. */
TEST(kprobe_user_pointers_getclassname_null_inner_buffer) {
    typedef BOOL (WINAPI *fn_t)(HWND, int, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClassName");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    struct kp_strbuf d = { 0, 64, 0, NULL };
    kp_veh_reset();
    BOOL r = p(hwnd, 0, &d);
    CHECKF(!r, "NtUserGetClassName with a NULL inner buffer pointer got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "inner-buffer probe must fail cleanly, not raise (hits=%u)", kp_veh_hits());
    U(DestroyWindow)(hwnd);
}

/* ---- NtUserGetAtomName(ATOM, struct kp_strbuf *) --------------------------------------------
 * win32kfull analysis: identical shape to GetClassName's descriptor handling, one
 * fewer unused leading argument. */
TEST(kprobe_user_pointers_getatomname_null_descriptor) {
    typedef BOOL (WINAPI *fn_t)(ATOM, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetAtomName");
    REQUIRE(p);
    ATOM a = kp_fixture_atom(L"win32kt_kprobe_atom");
    REQUIRE(a);
    kp_veh_reset();
    BOOL r = p(a, NULL);
    CHECKF(!r, "NtUserGetAtomName(valid atom, NULL) got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    U(GlobalDeleteAtom)(a);
}
TEST(kprobe_user_pointers_getatomname_null_inner_buffer) {
    typedef BOOL (WINAPI *fn_t)(ATOM, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetAtomName");
    REQUIRE(p);
    ATOM a = kp_fixture_atom(L"win32kt_kprobe_atom2");
    REQUIRE(a);
    struct kp_strbuf d = { 0, 64, 0, NULL };
    kp_veh_reset();
    BOOL r = p(a, &d);
    CHECKF(!r, "got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    U(GlobalDeleteAtom)(a);
}

/* ---- NtUserGetKeyboardLayoutName(struct kp_strbuf *) ----------------------------------------
 * win32kbase analysis: same descriptor shape; additionally requires a non-NULL
 * current keyboard layout, which
 * every interactive desktop session has, so the descriptor path is reached. */
TEST(kprobe_user_pointers_getkeyboardlayoutname_null_descriptor) {
    typedef BOOL (WINAPI *fn_t)(struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardLayoutName");
    REQUIRE(p);
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    kp_veh_reset();
    BOOL r = p(NULL);
    CHECKF(!r, "got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
}
TEST(kprobe_user_pointers_getkeyboardlayoutname_null_inner_buffer) {
    typedef BOOL (WINAPI *fn_t)(struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardLayoutName");
    REQUIRE(p);
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    struct kp_strbuf d = { 0, 18, 0, NULL }; /* KL_NAMELENGTH*2 bytes, per user32!GetKeyboardLayoutNameW */
    kp_veh_reset();
    BOOL r = p(&d);
    CHECKF(!r, "got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
}

/* ---- NtUserGetClipboardFormatName(UINT, LPWSTR, int) ----------------------------------------
 * win32kfull analysis:  unconditionally,
 * before even checking the format id - the probe fires regardless of whether the format exists. */
TEST(kprobe_user_pointers_getclipboardformatname_null_buffer) {
    typedef BOOL (WINAPI *fn_t)(UINT, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClipboardFormatName");
    REQUIRE(p);
    UINT fmt = kp_fixture_clip_format();
    REQUIRE(fmt);
    kp_veh_reset();
    BOOL r = p(fmt, NULL, 32);
    CHECKF(!r, "got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
}

/* ---- NtUserGetKeyboardState(BYTE[256]) -------------------------------------------------------
 * the kernel touches the first and last byte of the buffer - NULL is
 * below the user probe limit so it is used as-is: a direct read-modify-write of address 0 and
 * address 0xff. No size parameter exists for this one (always exactly 256 bytes, matching
 * winuser.h's GetKeyboardState contract). */
TEST(kprobe_user_pointers_getkeyboardstate_null_buffer) {
    typedef BOOL (WINAPI *fn_t)(BYTE *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardState");
    REQUIRE(p);
    kp_veh_reset();
    BOOL r = p(NULL);
    CHECKF(!r, "NtUserGetKeyboardState(NULL) got %d want FALSE", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
}

/* ---- NtGdiGetTextMetricsW(HDC, TEXTMETRICW*, UINT) -------------------------------------------
 * win32kfull analysis: the clamp+self-touch only fires when the OUT-OF-RANGE/
 * misalignment condition is true; for NULL none of
 * (wraparound) or are true, so execution falls through to the real copy
 * (), i.e. NULL still faults, just on the first real store rather than
 * on this function's own extra probe - same expected outcome (clean failure, no user exception),
 * different analysis line, which is worth keeping as its own test rather than assuming it behaves
 * like the kernel-range case in kprobe_kernelrange.c. Requires cb > 0x43 (68) to even reach the
 * copy, per the  guard - 0x44 is the minimum real-build TEXTMETRICW size. */
TEST(kprobe_user_pointers_gettextmetricsw_null_buffer) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    if (t_is_wine()) {
        /* Observed hang under headless Wine: cb=0x44 is past Wine's own equivalent of the
 * early-return, so Wine's NtGdiGetTextMetricsW reaches its own NULL-
         * buffer write with no probe-conversion machinery equivalent to real Windows' - that is
         * an unhandled fault inside Wine's win32u that this corpus cannot safely drive headless.
         * Real Windows is the only authority for this test; see the file header. */
        t_skip("Wine's NtGdiGetTextMetricsW NULL-buffer write past the size guard is not known "
               "safe headless (observed hang) - real Windows only, see file header");
        return;
    }
    kp_veh_reset();
    int r = p(dc, NULL, 0x44);
    CHECKF(r == 0, "NtGdiGetTextMetricsW(valid dc, NULL, 0x44) got %d want 0 (failure)", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    U(DeleteDC)(dc);
}
/* cb <= 0x43 returns 0 before ever looking at the pointer ()
 * - a NULL buffer with a too-small cb is the SAME harmless early-return as any other pointer. */
TEST(kprobe_user_pointers_gettextmetricsw_null_buffer_undersized_cb_is_harmless) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    if (t_is_wine()) {
        /* Observed under headless Wine: this call does NOT early-return the way the real-
 * Windows analysis's guard does - it proceeds to a NULL write,
         * which comes back as STATUS_ACCESS_VIOLATION cast to int (-1073741819) rather than 0,
         * AND leaves Wine's own GDI lock stuck so every GDI call afterward in this process hangs
         * forever (observed: the very next test's CreateCompatibleDC never returned). Skipping
         * the call entirely under Wine, not just the assertion, to protect every later test in
         * this run - real Windows is the only authority here regardless. */
        t_skip("Wine's NtGdiGetTextMetricsW does not share this build's early-return threshold "
               "and driving it with a NULL buffer leaves a stuck GDI lock headless - real "
               "Windows only, see file header");
        U(DeleteDC)(dc);
        return;
    }
    kp_veh_reset();
    int r = p(dc, NULL, 0x43); /* exactly one byte short of the 0x43 < cb guard */
    CHECK(r == 0);
    CHECK(kp_veh_hits() == 0);
    U(DeleteDC)(dc);
}

/* ---- NtGdiGetPath(HDC, POINT*, BYTE*, int) ----------------------------------------------------
 * win32kfull analysis: cpt==0 queries the point count only and never probes
 * (); cpt>0
 * reaches both probes with NULL pts/types once a real path with >=1 point exists on the DC. */
TEST(kprobe_user_pointers_getpath_null_buffers) {
    typedef int (WINAPI *fn_t)(HDC, POINT *, BYTE *, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_fixture_dc_with_path();
    if (!dc) { t_skip("could not build a DC with an open/closed path"); return; }
    int count = p(dc, NULL, NULL, 0);
    if (count <= 0) { t_skip("path has no points to query (count=%d)", count); U(DeleteDC)(dc); return; }
    kp_veh_reset();
    int r = p(dc, NULL, NULL, count);
    CHECKF(r != count, "NtGdiGetPath with NULL pts/types must not report success (got %d, "
           "same as the real point count %d)", r, count);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    U(DeleteDC)(dc);
}

/* ---- out-of-scope reminder: NtGdiGetBitmapBits and NtGdiExtGetObjectW's NULL-out-pointer cases
 * are documented success paths (query the required size), not probe-failure cases - see the
 * buffer-size-protocol tests in kprobe_guardpage.c for those, so this file does not duplicate
 * them as "expected failure" tests. */
TEST(kprobe_user_pointers_null_is_not_universally_a_failure_case) {
    t_note("NtGdiGetBitmapBits(obj, cb, NULL) and NtGdiExtGetObjectW(obj, cb, NULL) both treat a "
           "NULL out pointer as a documented size-query, not a probed failure (see analysis in "
           "kprobe_guardpage.c) - intentionally not tested here as a failure case");
}
