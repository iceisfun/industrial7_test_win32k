/* Self-test of common/k.h - the kernel-state helper layer. Every area writer depends on this
 * layer being correct, so these checks run before any area that uses it. Keep this area small
 * and fast; it is not itself a coverage area. */
#include "t.h"
#include "k.h"

/* ---- 1. syscall stub decoding, 3 known numbers (tools/win32u_syscalls.tsv) ---- */
TEST(core_k_helpers_stub_decode_known_syscalls) {
    /* Numbers are from tools/win32u_syscalls.tsv (real Windows 10 19041 service table). Wine
     * ships its own win32u with its own numbering, so only the stub SHAPE is checked there. */
    struct { const char *name; long expect; } v[] = {
        { "NtUserGetThreadState", 4096 },
        { "NtUserCallOneParam",   4098 },
        { "NtGdiFlush",           4114 },
    };
    for (int i = 0; i < 3; i++) {
        FARPROC p = t_proc_in("win32u.dll", v[i].name);
        if (!p) { t_skip("win32u.dll missing export %s", v[i].name); continue; }
        CHECK(k_stub_valid(p));
        if (t_is_wine()) { t_note("%s number=%ld (Wine's own table, not comparable to 19041)", v[i].name, k_stub_number(p)); continue; }
        CHECKF(k_stub_number(p) == v[i].expect, "%s got %ld want %ld", v[i].name, k_stub_number(p), v[i].expect);
    }
}
TEST(core_k_helpers_stub_decode_rejects_non_stub) {
    /* kernel32!GetTickCount is not a win32u syscall stub; the fixed byte pattern must not match
     * an arbitrary function (otherwise k_stub_number would silently return garbage numbers). */
    FARPROC p = t_proc_in("kernel32.dll", "GetTickCount");
    REQUIRE(p);
    CHECK(!k_stub_valid(p));
    CHECK(k_stub_number(p) == -1);
}

/* ---- 2. GDI cell decode of a freshly created DC/bitmap ---- */
TEST(core_k_helpers_gdi_cell_decode_dc) {
    REQUIRE(U(CreateCompatibleDC));
    HDC hdc = U(CreateCompatibleDC)(NULL);
    REQUIRE(hdc);
    struct k_gdicell cell;
    if (!k_gdi_cell(hdc, &cell)) { t_skip("PEB->GdiSharedHandleTable unavailable (not yet connected to GDI?)"); U(DeleteDC)(hdc); return; }
    /* HANDLE_TO_INDEX (gdi32full.dll) compares this byte against the handle's top byte to
     * detect a stale/reused slot; a handle this process just received must match. */
    CHECKF(cell.type_check == (unsigned char)((ULONG_PTR)hdc >> 24),
           "type_check=0x%02x top_byte=0x%02x", cell.type_check, (unsigned char)((ULONG_PTR)hdc >> 24));
    EXPECT_NE(cell.object, NULL);
    U(DeleteDC)(hdc);
}
TEST(core_k_helpers_gdi_cell_decode_bitmap) {
    REQUIRE(U(CreateBitmap));
    HBITMAP hbm = U(CreateBitmap)(1, 1, 1, 1, NULL);
    REQUIRE(hbm);
    struct k_gdicell cell;
    if (k_gdi_cell(hbm, &cell)) {
        CHECKF(cell.type_check == (unsigned char)((ULONG_PTR)hbm >> 24),
               "type_check=0x%02x top_byte=0x%02x", cell.type_check, (unsigned char)((ULONG_PTR)hbm >> 24));
    } else t_skip("GDI shared handle table unavailable");
    U(DeleteObject)(hbm);
}

/* ---- 3. USER handle entry decode of a created window ---- */
TEST(core_k_helpers_user_entry_decode_window) {
    HWND h = k_make_window("k_helpers_selftest");
    if (!h) { t_skip("k_make_window failed (err %lu) - no display driver under headless Wine", GetLastError()); return; }
    struct k_user_entry e;
    if (!k_shared_info_ptr()) { t_skip("user32!gSharedInfo not resolvable"); DestroyWindow(h); return; }
    if (!k_user_entry(h, &e)) { t_skip("USER handle table unavailable for this handle"); DestroyWindow(h); return; }
    /* IsWindow (user32.dll) treats type byte 1 as a window object; a window this process just
     * created and still owns must decode to that type with the "deleted" flag bit clear. */
    CHECKF(e.type == K_TYPE_WINDOW, "type=0x%02x", e.type);
    CHECK((e.flags & 1) == 0);
    DestroyWindow(h);
}

/* ---- 4. kernel callback table non-NULL after user32 loads ---- */
TEST(core_k_helpers_callback_table_present_after_user32) {
    REQUIRE(U(IsWindow)); /* forces user32 to be loaded and the thread to connect, if not already */
    void *tbl = k_cb_table();
    EXPECT_NE(tbl, NULL);
    unsigned n = k_cb_count();
    t_note("kernel callback table at %p, %u consecutive non-NULL slots", tbl, n);
    CHECK(n > 0);
}

/* ---- 5. safe-read of a bad pointer returns false, does not crash the run ---- */
TEST(core_k_helpers_safe_read_bad_pointer) {
    unsigned char buf[8];
    CHECK(!k_try_read(NULL, buf, sizeof buf));
    CHECK(!k_try_read((const void *)(ULONG_PTR)1, buf, sizeof buf));
    /* kernel-range address: unmapped/inaccessible from user mode, must fail cleanly */
    CHECK(!k_try_read((const void *)(ULONG_PTR)0xFFFF800000000000ULL, buf, sizeof buf));
    /* a real, valid pointer must still succeed */
    int x = 0x1234;
    int y = 0;
    CHECK(k_try_read(&x, &y, sizeof x) && y == x);
}

/* ---- 6. child capture relays a failure to the parent ---- */
CHILD(k_helpers_fail_demo) {
    (void)argc; (void)argv;
    printf("FAIL k_helpers_fail_demo: deliberate failure for relay self-test\n");
    return 1;
}
TEST(core_k_helpers_child_capture_relays_failure) {
    struct k_child_result r = k_child_run("k_helpers_fail_demo", NULL, 10000);
    EXPECT_EQ(r.exit_code, 1);
    CHECK(r.relayed_lines >= 1);
}
CHILD(k_helpers_ok_demo) { (void)argc; (void)argv; return 0; }
TEST(core_k_helpers_child_capture_relays_success) {
    struct k_child_result r = k_child_run("k_helpers_ok_demo", NULL, 10000);
    EXPECT_EQ(r.exit_code, 0);
}
