/* kprobe_user_pointers/kprobe_guardpage.c
 *
 * Buffer-size-exactness: a committed buffer of exactly the documented size, immediately followed
 * by a reserved-but-uncommitted guard page (kp_guard_alloc, see kprobe.h). An exact-size call must
 * succeed without faulting into the guard page, and must write exactly the documented number of
 * bytes - checked with a canary pattern filled before the call (kp_guard_fill /
 * kp_guard_untouched) so an over- or under-write is directly visible, not just "didn't crash".
 * A size one byte too large must fail cleanly (cross into the guard page) instead of either
 * succeeding past the documented size or raising a user-mode exception.
 *
 * Also: the two syscalls in this area (NtGdiGetBitmapBits, NtGdiExtGetObjectW) whose analysis treats
 * a NULL out pointer as "just tell me the required size" instead of an error - the positive half
 * of the buffer-size protocol kprobe_nullreq.c deliberately did not test as a failure case.
 */
#include "kprobe.h"

/* ---- NtUserInternalGetWindowText: writes exactly `min(real_len, cch-1)` chars + a NUL, per the
 * analysis's  - for a window titled with >= cch-1 characters,
 * exactly cch WCHARs (2*cch bytes) get touched: the guard buffer is sized to exactly that. ------- */
TEST(kprobe_user_pointers_internalgetwindowtext_exact_size_succeeds) {
    typedef int (WINAPI *fn_t)(HWND, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserInternalGetWindowText");
    REQUIRE(p);
    HWND hwnd = U(CreateWindowExW) ? k_make_window("kprobe_exact_title_longer_than_8_chars") : NULL;
    REQUIRE(hwnd);
    const int cch = 4; /* deliberately shorter than the real title, so TextCopy truncates+NULs */
    struct kp_guard g;
    if (!kp_guard_alloc((size_t)cch * 2, &g)) { t_skip("guard alloc failed"); U(DestroyWindow)(hwnd); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    int r = p(hwnd, (LPWSTR)g.buf, cch);
    CHECKF(r != 0, "exact-size buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
    /* every one of the cch*2 bytes is inside the committed page; nothing past it was touched
     * (guaranteed by the allocator's own guard page - re-verified here for documentation). */
    MEMORY_BASIC_INFORMATION mbi;
    CHECKF(VirtualQuery((char *)g.buf + g.usable, &mbi, sizeof mbi) != 0 && mbi.State == MEM_RESERVE,
           "byte after the buffer must still be the unmapped guard page");
    kp_guard_free(&g);
    U(DestroyWindow)(hwnd);
}
TEST(kprobe_user_pointers_internalgetwindowtext_one_byte_over_fails) {
    typedef int (WINAPI *fn_t)(HWND, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserInternalGetWindowText");
    REQUIRE(p);
    HWND hwnd = k_make_window("kprobe_exact_title_longer_than_8_chars");
    REQUIRE(hwnd);
    SYSTEM_INFO si; GetSystemInfo(&si);
    /* cch sized so cch*2 bytes lands exactly one byte past the committed page - ProbeForWrite
     * must see the range crossing into the guard page and fail before writing anything. */
    size_t one_page_wchars = si.dwPageSize / 2;
    struct kp_guard g;
    if (!kp_guard_alloc(si.dwPageSize, &g)) { t_skip("guard alloc failed"); U(DestroyWindow)(hwnd); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    int r = p(hwnd, (LPWSTR)g.buf, (int)one_page_wchars + 1); /* 2 bytes past the committed page */
    CHECKF(r == 0, "size crossing into the guard page must fail, got %d", r);
    CHECKF(kp_veh_hits() == 0, "must fail cleanly, not raise (hits=%u)", kp_veh_hits());
    CHECKF(kp_guard_untouched(&g, 0, g.usable, 0xCC),
           "a probe that fails must not have partially written the buffer first");
    kp_guard_free(&g);
    U(DestroyWindow)(hwnd);
}

/* ---- NtUserGetKeyboardState: always exactly 256 bytes, no size parameter at all --------------- */
TEST(kprobe_user_pointers_getkeyboardstate_exact_256_succeeds_and_writes_exactly_256) {
    typedef BOOL (WINAPI *fn_t)(BYTE *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardState");
    REQUIRE(p);
    struct kp_guard g;
    if (!kp_guard_alloc(256, &g)) { t_skip("guard alloc failed"); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    BOOL r = p((BYTE *)g.buf);
    CHECKF(r, "NtUserGetKeyboardState with an exact 256-byte buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
 /* analysis's loop body ( 0..0xff inclusive) writes every one of the 256 bytes - none
     * should still read the 0xCC canary (every key is either 0, 0x80, 0x01 or 0x81; 0xCC is none
     * of those on any real keyboard-state byte). */
    int all_written = 1;
    for (int i = 0; i < 256; i++) if (((unsigned char *)g.buf)[i] == 0xCC) { all_written = 0; break; }
    CHECKF(all_written, "every one of the 256 bytes must be written by the kernel, a 0xCC canary "
           "survived - either the loop did not run to 0xff or this build's layout changed");
    kp_guard_free(&g);
}
/* A 255-byte buffer (one short): the analysis's self-touch explicitly reads/writes 
 * (the 256th byte, index 255) BEFORE the fill loop even starts - a buffer one byte short crosses
 * into the guard page on exactly that touch. */
TEST(kprobe_user_pointers_getkeyboardstate_255_bytes_fails) {
    typedef BOOL (WINAPI *fn_t)(BYTE *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardState");
    REQUIRE(p);
    struct kp_guard g;
    if (!kp_guard_alloc(256, &g)) { t_skip("guard alloc failed"); return; }
    void *short_buf = (char *)g.buf + 1; /* now only 255 bytes before the guard page */
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    BOOL r = p((BYTE *)short_buf);
    CHECKF(!r, "255-byte buffer (index 255 lands on the guard page) must fail, got %d", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    kp_guard_free(&g);
}

/* ---- NtGdiGetTextMetricsW: fixed 0x44-byte real-TEXTMETRICW copy ------------------------------- */
TEST(kprobe_user_pointers_gettextmetricsw_exact_size_succeeds_and_writes_0x44_bytes) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    struct kp_guard g;
    if (!kp_guard_alloc(0x44, &g)) { t_skip("guard alloc failed"); U(DeleteDC)(dc); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    int r = p(dc, g.buf, 0x44);
    CHECKF(r != 0, "exact 0x44-byte buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
 /* analysis copies exactly 0x44 bytes - every byte
     * of the canary must be gone. */
    CHECKF(!kp_guard_untouched(&g, 0, 0x44, 0xCC), "the full 0x44-byte TEXTMETRICW must be written");
    kp_guard_free(&g);
    U(DeleteDC)(dc);
}
TEST(kprobe_user_pointers_gettextmetricsw_one_byte_short_buffer_fails) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    if (t_is_wine()) {
        t_skip("NtGdiGetTextMetricsW with a bad pointer is not known safe headless under Wine "
               "(see the stuck-GDI-lock note in kprobe_nullreq.c) - real Windows only");
        U(DeleteDC)(dc);
        return;
    }
    struct kp_guard g;
    if (!kp_guard_alloc(0x44, &g)) { t_skip("guard alloc failed"); U(DeleteDC)(dc); return; }
    void *short_buf = (char *)g.buf + 1; /* 0x43 usable bytes before the guard page */
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    /* cb itself is still 0x44 (the documented structure size the caller asks for) - only the
     * actual memory window is one byte short, which the bounds/alignment self-touch condition
     * () does not directly see (it is not one of
     * those three specific guard conditions - the only way this fails here is the subsequent
     * real stores crossing into the reserved guard page, same as the NULL case in
     * kprobe_nullreq.c). Also deliberately unaligned (+1), so this doubles as an alignment case:
     * both the unaligned-pointer self-touch AND the short-buffer store would fail independently. */
    int r = p(dc, short_buf, 0x44);
    CHECKF(r == 0, "one-byte-short+unaligned buffer must fail, got %d", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    kp_guard_free(&g);
    U(DeleteDC)(dc);
}

/* ---- NtGdiGetBitmapBits: NULL out pointer is a documented size query, not a probe failure ------ */
TEST(kprobe_user_pointers_getbitmapbits_null_buffer_is_a_size_query) {
    typedef DWORD (WINAPI *fn_t)(HBITMAP, DWORD, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetBitmapBits");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(8, 8); /* 1 bpp, 1 plane: 8*8 bits -> 1 byte/row, dword-padded */
    REQUIRE(bmp);
    kp_veh_reset();
 /* analysis: is simply skipped when the output
     * pointer is NULL/0 - the function still computes and return the
     * real required size, never dereferencing anything. */
    DWORD size = p(bmp, 0, NULL);
    CHECKF(size != 0, "NtGdiGetBitmapBits(bmp, 0, NULL) must return the real required size, got 0");
    CHECKF(kp_veh_hits() == 0, "a documented size-query must never touch memory (hits=%u)", kp_veh_hits());
    U(DeleteObject)(bmp);
}
TEST(kprobe_user_pointers_getbitmapbits_exact_size_succeeds_and_writes_exact_bytes) {
    typedef DWORD (WINAPI *fn_t)(HBITMAP, DWORD, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetBitmapBits");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(8, 8);
    REQUIRE(bmp);
    DWORD want = kp_fixture_bitmap_bits_size(bmp);
    REQUIRE(want > 0 && want <= 4096);
    struct kp_guard g;
    if (!kp_guard_alloc(want, &g)) { t_skip("guard alloc failed"); U(DeleteObject)(bmp); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    DWORD got = p(bmp, want, g.buf);
    CHECKF(got == want, "GetBitmapBits with the exact real size must copy all of it: got %lu want %lu", got, want);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
    U(DeleteObject)(bmp);
}
TEST(kprobe_user_pointers_getbitmapbits_one_byte_over_real_size_fails_cleanly) {
    typedef DWORD (WINAPI *fn_t)(HBITMAP, DWORD, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetBitmapBits");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(8, 8);
    REQUIRE(bmp);
    DWORD want = kp_fixture_bitmap_bits_size(bmp);
    REQUIRE(want > 0 && want <= 4095);
    /* analysis clamps the requested `cb` down to the real size BEFORE probing
 * (), so asking for one byte MORE than the real size
     * does not itself fail - it is clamped to `want` and that many bytes are copied/probed. This
     * test documents that clamp rather than asserting a failure: a guard page placed at exactly
     * `want+1` must stay untouched even though we asked for `want+1`. */
    struct kp_guard g;
    if (!kp_guard_alloc(want + 1, &g)) { t_skip("guard alloc failed"); U(DeleteObject)(bmp); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    DWORD got = p(bmp, want + 1, g.buf);
    CHECKF(got == want, "requesting want+1 must be clamped to the real size %lu, got %lu", want, got);
    CHECK(kp_veh_hits() == 0);
    CHECKF(kp_guard_untouched(&g, want, 1, 0xCC),
           "the clamp means byte [want] must never be written even though cb asked for want+1");
    kp_guard_free(&g);
    U(DeleteObject)(bmp);
}

/* ---- NtGdiExtGetObjectW: NULL out pointer is also a documented size query ---------------------- */
TEST(kprobe_user_pointers_extgetobjectw_null_buffer_is_a_size_query) {
    typedef UINT (WINAPI *fn_t)(HGDIOBJ, int, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiExtGetObjectW");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(4, 4);
    REQUIRE(bmp);
    kp_veh_reset();
    UINT size = p(bmp, 0, NULL);
    CHECKF(size != 0, "NtGdiExtGetObjectW(bmp, 0, NULL) must return the real structure size, got 0");
    CHECKF(kp_veh_hits() == 0, "size query must not touch memory (hits=%u)", kp_veh_hits());
    U(DeleteObject)(bmp);
}
TEST(kprobe_user_pointers_extgetobjectw_exact_size_succeeds) {
    typedef UINT (WINAPI *fn_t)(HGDIOBJ, int, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiExtGetObjectW");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(4, 4);
    REQUIRE(bmp);
    struct kp_guard g;
    if (!kp_guard_alloc(sizeof(BITMAP), &g)) { t_skip("guard alloc failed"); U(DeleteObject)(bmp); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    UINT r = p(bmp, sizeof(BITMAP), g.buf);
    CHECKF(r == sizeof(BITMAP), "exact sizeof(BITMAP) buffer must succeed and return it, got %u", r);
    CHECK(kp_veh_hits() == 0);
    CHECKF(!kp_guard_untouched(&g, 0, sizeof(BITMAP), 0xCC), "the full BITMAP struct must be written");
    kp_guard_free(&g);
    U(DeleteObject)(bmp);
}

/* ---- NtUserGetClassName / NtUserGetAtomName / NtUserGetKeyboardLayoutName: inner-buffer exactness
 * via the shared kp_strbuf descriptor - one representative test per function, same pattern. ----- */
TEST(kprobe_user_pointers_getclassname_exact_inner_buffer_succeeds) {
    typedef BOOL (WINAPI *fn_t)(HWND, int, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClassName");
    REQUIRE(p);
    HWND hwnd = kp_fixture_window();
    REQUIRE(hwnd);
    struct kp_guard g;
    if (!kp_guard_alloc(64, &g)) { t_skip("guard alloc failed"); U(DestroyWindow)(hwnd); return; }
    kp_guard_fill(&g, 0xCC);
    struct kp_strbuf d = { 0, 64, 0, g.buf };
    kp_veh_reset();
    BOOL r = p(hwnd, 0, &d);
    CHECKF(r, "exact-size inner buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
    U(DestroyWindow)(hwnd);
}
TEST(kprobe_user_pointers_getatomname_exact_inner_buffer_succeeds) {
    typedef BOOL (WINAPI *fn_t)(ATOM, struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetAtomName");
    REQUIRE(p);
    ATOM a = kp_fixture_atom(L"win32kt_kprobe_guard_atom");
    REQUIRE(a);
    struct kp_guard g;
    if (!kp_guard_alloc(64, &g)) { t_skip("guard alloc failed"); U(GlobalDeleteAtom)(a); return; }
    kp_guard_fill(&g, 0xCC);
    struct kp_strbuf d = { 0, 64, 0, g.buf };
    kp_veh_reset();
    BOOL r = p(a, &d);
    CHECKF(r, "exact-size inner buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
    U(GlobalDeleteAtom)(a);
}
TEST(kprobe_user_pointers_getkeyboardlayoutname_exact_inner_buffer_succeeds) {
    typedef BOOL (WINAPI *fn_t)(struct kp_strbuf *);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetKeyboardLayoutName");
    REQUIRE(p);
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    struct kp_guard g;
    if (!kp_guard_alloc(18, &g)) { t_skip("guard alloc failed"); return; } /* KL_NAMELENGTH*2 */
    kp_guard_fill(&g, 0xCC);
    struct kp_strbuf d = { 0, 18, 0, g.buf };
    kp_veh_reset();
    BOOL r = p(&d);
    CHECKF(r, "exact-size inner buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
}

/* ---- NtUserGetClipboardFormatName: exactness on the raw (buf,cch) pair ------------------------- */
TEST(kprobe_user_pointers_getclipboardformatname_exact_size_succeeds) {
    typedef BOOL (WINAPI *fn_t)(UINT, LPWSTR, int);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtUserGetClipboardFormatName");
    REQUIRE(p);
    UINT fmt = kp_fixture_clip_format();
    REQUIRE(fmt);
    const int cch = 48;
    struct kp_guard g;
    if (!kp_guard_alloc((size_t)cch * 2, &g)) { t_skip("guard alloc failed"); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    BOOL r = p(fmt, (LPWSTR)g.buf, cch);
    CHECKF(r, "exact-size buffer must succeed for a real registered format, got %d", r);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
}
