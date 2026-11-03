/* kprobe_user_pointers/kprobe_kernelrange.c
 *
 * Kernel-range and non-canonical-boundary pointers, table-driven across every syscall in this
 * area that clamps the pointer to the user-mode limit (see kprobe.h's file header for the full pattern and why it is the inlined form
 * of ProbeForWrite: clamp then self-touch). All three addresses below are genuinely >= the real
 * MmUserProbeAddress (0x7FFFFFFF0000 on x64 19041) so every one of them takes the clamp branch:
 *   - KP_ADDR_KERNEL_GENERIC (0xFFFF800000000000): an ordinary kernel VA.
 *   - KP_ADDR_KUSER_ALIAS    (0xFFFFF78000000000): KUSER_SHARED_DATA's *kernel* mapping - chosen
 *     specifically because it IS a real, mapped, readable page in kernel space, to rule out
 *     "any unmapped address clamps and fails" being the only thing under test; a mapped-but-
 *     wrong-privilege-level address must clamp exactly the same way.
 *   - KP_ADDR_BOUNDARY       (0x7FFFFFFF0000): MmUserProbeAddress itself, i.e. "one past the
 *     real top of user VA" - the first byte of the kernel's own probe-boundary page.
 * Expected for all three, on every syscall below: documented failure return, kp_veh_hits()==0.
 */
#include "kprobe.h"

enum kp_kind { KP_KERNEL_GENERIC, KP_KUSER_ALIAS, KP_BOUNDARY, KP_KIND_COUNT };
static void *kp_kind_addr(enum kp_kind k) {
    switch (k) {
        case KP_KERNEL_GENERIC: return KP_ADDR_KERNEL_GENERIC;
        case KP_KUSER_ALIAS:    return KP_ADDR_KUSER_ALIAS;
        case KP_BOUNDARY:       return KP_ADDR_BOUNDARY;
        default: return NULL;
    }
}
static const char *kp_kind_name(enum kp_kind k) {
    switch (k) {
        case KP_KERNEL_GENERIC: return "kernel-generic(0xFFFF8000...)";
        case KP_KUSER_ALIAS:    return "KUSER-kernel-alias(0xFFFFF78000000000)";
        case KP_BOUNDARY:       return "MmUserProbeAddress(0x7FFFFFFF0000)";
        default: return "?";
    }
}

/* ---- NtUserInternalGetWindowText: ProbeForWrite(buf, cch*2, 2) --------------------------------- */
TEST(kprobe_user_pointers_internalgetwindowtext_kernel_range) {
    typedef int (WINAPI *fn_t)(HWND, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserInternalGetWindowText");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        int r = p(hwnd, (LPWSTR)kp_kind_addr(k), 8);
        CHECKF(r == 0, "%s: got %d want 0", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(DestroyWindow)(hwnd);
}

/* ---- NtUserGetClassName: outer descriptor pointer clamp, then inner buffer probe ------------- */
TEST(kprobe_user_pointers_getclassname_kernel_range_descriptor) {
    typedef BOOL (WINAPI *fn_t)(HWND, int, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClassName");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        BOOL r = p(hwnd, 0, (struct kp_strbuf *)kp_kind_addr(k));
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(DestroyWindow)(hwnd);
}
TEST(kprobe_user_pointers_getclassname_kernel_range_inner_buffer) {
    typedef BOOL (WINAPI *fn_t)(HWND, int, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClassName");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        struct kp_strbuf d = { 0, 64, 0, kp_kind_addr(k) };
        kp_veh_reset();
        BOOL r = p(hwnd, 0, &d); /* descriptor itself is a valid local stack struct */
        CHECKF(!r, "%s inner buffer: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s inner buffer: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(DestroyWindow)(hwnd);
}

/* ---- NtUserGetAtomName: same descriptor shape as GetClassName -------------------------------- */
TEST(kprobe_user_pointers_getatomname_kernel_range_descriptor) {
    typedef BOOL (WINAPI *fn_t)(ATOM, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetAtomName");
    REQUIRE(p);
    ATOM a = kp_fixture_atom(L"win32kt_kprobe_kr_atom");
    REQUIRE(a);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        BOOL r = p(a, (struct kp_strbuf *)kp_kind_addr(k));
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(GlobalDeleteAtom)(a);
}
TEST(kprobe_user_pointers_getatomname_kernel_range_inner_buffer) {
    typedef BOOL (WINAPI *fn_t)(ATOM, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetAtomName");
    REQUIRE(p);
    ATOM a = kp_fixture_atom(L"win32kt_kprobe_kr_atom2");
    REQUIRE(a);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        struct kp_strbuf d = { 0, 64, 0, kp_kind_addr(k) };
        kp_veh_reset();
        BOOL r = p(a, &d);
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(GlobalDeleteAtom)(a);
}

/* ---- NtUserGetKeyboardLayoutName: same descriptor shape again --------------------------------- */
TEST(kprobe_user_pointers_getkeyboardlayoutname_kernel_range_descriptor) {
    typedef BOOL (WINAPI *fn_t)(struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardLayoutName");
    REQUIRE(p);
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        BOOL r = p((struct kp_strbuf *)kp_kind_addr(k));
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
}
TEST(kprobe_user_pointers_getkeyboardlayoutname_kernel_range_inner_buffer) {
    typedef BOOL (WINAPI *fn_t)(struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardLayoutName");
    REQUIRE(p);
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        struct kp_strbuf d = { 0, 18, 0, kp_kind_addr(k) };
        kp_veh_reset();
        BOOL r = p(&d);
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
}

/* ---- NtUserGetClipboardFormatName: ProbeForWrite(buf, cch*2, 2) unconditionally --------------- */
TEST(kprobe_user_pointers_getclipboardformatname_kernel_range) {
    typedef BOOL (WINAPI *fn_t)(UINT, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClipboardFormatName");
    REQUIRE(p);
    UINT fmt = kp_fixture_clip_format();
    REQUIRE(fmt);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        BOOL r = p(fmt, (LPWSTR)kp_kind_addr(k), 32);
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
}

/* ---- NtUserGetKeyboardState: clamp + self read-modify-write at [0] and [0xff] ------------------ */
TEST(kprobe_user_pointers_getkeyboardstate_kernel_range) {
    typedef BOOL (WINAPI *fn_t)(BYTE *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardState");
    REQUIRE(p);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        BOOL r = p((BYTE *)kp_kind_addr(k));
        CHECKF(!r, "%s: got %d want FALSE", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
}

/* ---- NtGdiGetTextMetricsW: explicit bounds+alignment check, then self-touch at MmUserProbeAddress
 * itself () rather than at the caller's own pointer - still
 * must fail cleanly for any pointer whose [ptr, ptr+0x44) range crosses or sits in kernel space. */
TEST(kprobe_user_pointers_gettextmetricsw_kernel_range) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    if (t_is_wine()) {
        /* kprobe_nullreq.c's file-local note (same function) applies here too: a bad pointer
         * into this specific syscall was observed to leave Wine's own GDI lock stuck, hanging
         * every later GDI call in the process - skip the call entirely, not just the CHECK. */
        t_skip("NtGdiGetTextMetricsW with a bad pointer is not known safe headless under Wine "
               "(observed stuck GDI lock) - real Windows only");
        U(DeleteDC)(dc);
        return;
    }
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        int r = p(dc, kp_kind_addr(k), 0x44);
        CHECKF(r == 0, "%s: got %d want 0", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(DeleteDC)(dc);
}
/* Unaligned (odd DWORD alignment) pointer, same analysis's arm of
 * the OR - takes the self-touch-at-probe-address branch exactly like a kernel-range pointer,
 * even though the address itself is ordinary, mapped, writable user stack memory. A real
 * TEXTMETRICW is never required to be 4-byte aligned by any public contract, so this is purely
 * this syscall's own extra defense-in-depth check, not a general alignment requirement. */
TEST(kprobe_user_pointers_gettextmetricsw_unaligned_buffer) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    if (t_is_wine()) {
        t_skip("NtGdiGetTextMetricsW with a misaligned pointer is not known safe headless under "
               "Wine (see the stuck-GDI-lock note in kprobe_nullreq.c) - real Windows only");
        U(DeleteDC)(dc);
        return;
    }
    unsigned char buf[128 + 8];
    for (int off = 1; off < 4; off++) {
        kp_veh_reset();
        int r = p(dc, buf + off, 0x44);
        CHECKF(r == 0, "offset %d (unaligned): got %d want 0", off, r);
        CHECKF(kp_veh_hits() == 0, "offset %d: hits=%u", off, kp_veh_hits());
    }
    /* offset 0 and 4 are both 4-byte aligned and must succeed, as a control - otherwise the
     * failures above could just be "this syscall never works", not an alignment check. */
    kp_veh_reset();
    int r0 = p(dc, buf, 0x44);
    CHECKF(r0 != 0, "4-byte-aligned buffer (offset 0) must succeed, got %d", r0);
    CHECK(kp_veh_hits() == 0);
    U(DeleteDC)(dc);
}

/* ---- NtGdiGetPath: both output buffers probed when cpt != 0 ----------------------------------- */
TEST(kprobe_user_pointers_getpath_kernel_range) {
    typedef int (WINAPI *fn_t)(HDC, POINT *, BYTE *, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_fixture_dc_with_path();
    if (!dc) { t_skip("could not build a DC with a path"); return; }
    int count = p(dc, NULL, NULL, 0);
    if (count <= 0) { t_skip("path has no points (count=%d)", count); U(DeleteDC)(dc); return; }
    unsigned char types[64];
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        int r = p(dc, (POINT *)kp_kind_addr(k), types, count); /* bad pts, valid types */
        CHECKF(r != count, "%s pts: got %d, must not equal real count %d", kp_kind_name(k), r, count);
        CHECKF(kp_veh_hits() == 0, "%s pts: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    POINT pts[64];
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        int r = p(dc, pts, (BYTE *)kp_kind_addr(k), count); /* valid pts, bad types */
        CHECKF(r != count, "%s types: got %d, must not equal real count %d", kp_kind_name(k), r, count);
        CHECKF(kp_veh_hits() == 0, "%s types: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(DeleteDC)(dc);
}

/* ---- NtGdiExtGetObjectW: clamp+self-touch at MmUserProbeAddress, plus a WORD-alignment check --- */
TEST(kprobe_user_pointers_extgetobjectw_kernel_range) {
    typedef UINT (WINAPI *fn_t)(HGDIOBJ, int, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiExtGetObjectW");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(4, 4);
    REQUIRE(bmp);
    for (enum kp_kind k = 0; k < KP_KIND_COUNT; k++) {
        kp_veh_reset();
        UINT r = p(bmp, sizeof(BITMAP), kp_kind_addr(k));
 /* analysis: the clamp/self-touch happens only -
 * the real object size is non-zero for a real bitmap, and every address here is
         * non-NULL, so the branch is reached and must fault/clamp, not report the struct size. */
        CHECKF(r == 0, "%s: got %u want 0 (clean failure, not the struct size)", kp_kind_name(k), r);
        CHECKF(kp_veh_hits() == 0, "%s: hits=%u", kp_kind_name(k), kp_veh_hits());
    }
    U(DeleteObject)(bmp);
}
TEST(kprobe_user_pointers_extgetobjectw_unaligned_buffer) {
    typedef UINT (WINAPI *fn_t)(HGDIOBJ, int, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiExtGetObjectW");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(4, 4);
    REQUIRE(bmp);
    unsigned char buf[sizeof(BITMAP) + 8];
 /* analysis's alignment arm is - WORD alignment, not DWORD. */
    kp_veh_reset();
    UINT r1 = p(bmp, sizeof(BITMAP), buf + 1);
    CHECKF(r1 == 0, "odd-aligned buffer: got %u want 0", r1);
    CHECK(kp_veh_hits() == 0);
    kp_veh_reset();
    UINT r0 = p(bmp, sizeof(BITMAP), buf); /* even-aligned control, must succeed */
    CHECKF(r0 != 0, "even-aligned buffer must succeed, got %u", r0);
    CHECK(kp_veh_hits() == 0);
    U(DeleteObject)(bmp);
}
