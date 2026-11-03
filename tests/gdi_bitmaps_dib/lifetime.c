/* Lifetime of bitmaps and their pixel memory.
 *  - Deleting a DIB section tears down the SURFACE, which releases its user-mode view (ZwFreeVirtualMemory / unmap in the owner's context);
 *    the address range must no longer be committed afterwards. A view of a caller-supplied section goes away too, the section survives.
 *  - GDI drawing may sit in the TEB batch (GdiTebBatch / GdiBatchCount) until GdiFlush -> NtGdiFlush; bits are only guaranteed current after it.
 *  - Bitmaps belong to the process, not the creating thread. */
#include "bd.h"

static int range_gone(const void *p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!bd_query(p, &mbi)) return 1;
    return mbi.State != MEM_COMMIT || mbi.BaseAddress != p;
}

TEST(bd_life_delete_releases_private_view) {
    BD_NEED();
    void *bits = NULL; HBITMAP hb = bd_dib(64, 64, 32, &bits); REQUIRE(hb && bits);
    MEMORY_BASIC_INFORMATION mbi; CHECK(bd_query(bits, &mbi) && mbi.State == MEM_COMMIT);
    CHECK(U(DeleteObject)(hb));
    CHECKF(range_gone(bits), "address %p still committed after DeleteObject", bits);
}
TEST(bd_life_delete_releases_each_of_many) {
    BD_NEED();
    enum { N = 24 };
    void *b[N]; HBITMAP h[N];
    for (int i = 0; i < N; i++) h[i] = bd_dib(16 + i, 8, 24, &b[i]);
    for (int i = 0; i < N; i++) { CHECK(h[i] && b[i]); }
    for (int i = 0; i < N; i++) if (h[i]) U(DeleteObject)(h[i]);
    int still = 0; for (int i = 0; i < N; i++) if (b[i] && !range_gone(b[i])) still++;
    CHECKF(still == 0, "%d views survived", still);
}
TEST(bd_life_delete_releases_mapped_view_section_survives) {
    BD_NEED();
    HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x10000, NULL); REQUIRE(sec);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 16, 16, 32);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    if (!hb || !bits) { if (hb) U(DeleteObject)(hb); CloseHandle(sec); t_skip("no section DIB"); return; }
    ((DWORD *)bits)[7] = 0x600dcafe;
    U(DeleteObject)(hb);
    CHECK(range_gone(bits));
    DWORD *v = MapViewOfFile(sec, FILE_MAP_READ, 0, 0, 0x10000);
    CHECK(v != NULL);
    if (v) { CHECK(v[7] == 0x600dcafe); UnmapViewOfFile(v); }
    CloseHandle(sec);
}
TEST(bd_life_delete_twice) {
    BD_NEED();
    HBITMAP hb = bd_dib(8, 8, 32, NULL); REQUIRE(hb);
    CHECK(U(DeleteObject)(hb));
    CHECK(!U(DeleteObject)(hb));
}
TEST(bd_life_getobject_after_delete_fails) {
    BD_NEED();
    HBITMAP hb = bd_dib(8, 8, 32, NULL); REQUIRE(hb);
    U(DeleteObject)(hb);
    DIBSECTION ds;
    CHECK(U(GetObjectW)(hb, sizeof ds, &ds) == 0);
    /* GetObjectType does not read the cell for a dead bitmap: it decodes the type bits of the handle (OBJ_BITMAP). */
    struct bd_cell c;
    if (bd_cell_read(hb, &c)) CHECKF(c.objt == 0, "objt after delete 0x%x", c.objt);
    t_note("GetObjectType of a deleted DIB section handle: %lu", U(GetObjectType)(hb));
}
TEST(bd_life_delete_selected_then_unselect) {
    BD_NEED(); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = bd_dib(8, 8, 32, &bits); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    BOOL r = U(DeleteObject)(hb);
    MEMORY_BASIC_INFORMATION mbi; int alive = bd_query(bits, &mbi) && mbi.State == MEM_COMMIT && mbi.BaseAddress == bits;
    t_note("DeleteObject(selected DIB) -> %d, view %s", r, alive ? "still committed" : "gone");
    ((DWORD *)bits)[0] = alive ? 1 : 0; /* only write while the note above says the memory is live */
    U(SelectObject)(dc, old);
    if (U(GetObjectType)(hb)) U(DeleteObject)(hb);
    CHECK(range_gone(bits));
    U(DeleteDC)(dc);
}
TEST(bd_life_delete_dc_then_bitmap_releases) {
    BD_NEED(); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = bd_dib(8, 8, 32, &bits); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    U(SelectObject)(dc, hb);
    U(DeleteDC)(dc);
    CHECK(U(DeleteObject)(hb));
    CHECK(range_gone(bits));
}
TEST(bd_life_gui_resource_count) {
    BD_NEED(); REQUIRE(U(GetGuiResources));
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    HBITMAP h[6];
    for (int i = 0; i < 6; i++) h[i] = bd_dib(4, 4, i < 3 ? 32 : 8, NULL);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a + 6);
    for (int i = 0; i < 6; i++) if (h[i]) U(DeleteObject)(h[i]);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a);
}
TEST(bd_life_cell_type_and_owner) {
    BD_NEED();
    HBITMAP hb = bd_dib(8, 8, 32, NULL); REQUIRE(hb);
    struct bd_cell c;
    if (!bd_cell_read(hb, &c)) { U(DeleteObject)(hb); t_skip("GDI shared table unreadable"); return; }
    CHECKF(c.objt == 5, "objt 0x%x", c.objt);
    CHECK(BD_TYPE7(hb) == 5);
    CHECKF((c.owner & ~1u) == GetCurrentProcessId(), "owner 0x%x", c.owner);
    CHECKF(c.q0 >= 0xFFFF800000000000ull, "kernel object pointer 0x%llx not in kernel range", c.q0);
    t_note("DIB section cell: pUser 0x%llx flags 0x%02x q0 0x%llx", c.puser, c.flags, c.q0);
    if (c.puser) CHECK(c.puser < 0x800000000000ull);
    U(DeleteObject)(hb);
}
TEST(bd_life_slot_reuse_bumps_uniqueness) {
    BD_NEED();
    HBITMAP a = bd_dib(4, 4, 32, NULL); REQUIRE(a);
    unsigned idx = BD_IDX(a);
    U(DeleteObject)(a);
    HBITMAP b = bd_dib(4, 4, 32, NULL); REQUIRE(b);
    if (BD_IDX(b) == idx) {
        CHECK(b != a); /* a reused slot never reproduces the old handle value: the reuse byte advanced */
        CHECK(U(GetObjectType)(a) == 0); /* the stale handle now meets a live cell of another generation */
    }
    DIBSECTION ds;
    CHECK(U(GetObjectW)(a, sizeof ds, &ds) == 0);
    U(DeleteObject)(b);
}
TEST(bd_life_flush_makes_bits_current) {
    BD_NEED(); REQUIRE(U(SelectObject)); REQUIRE(U(PatBlt)); REQUIRE(U(GdiFlush));
    void *bits = NULL; HBITMAP hb = bd_dib(32, 32, 32, &bits); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    unsigned long before = 0, after = 0;
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &before);
    U(PatBlt)(dc, 0, 0, 32, 32, WHITENESS);
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &after);
    t_note("GdiBatchCount %lu -> %lu around PatBlt", before, after);
    CHECK(U(GdiFlush)());
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &after);
    CHECKF(after == 0, "GdiBatchCount %lu after GdiFlush", after);
    int bad = 0; const DWORD *p = bits;
    for (int i = 0; i < 32 * 32; i++) if ((p[i] & 0xffffff) != 0xffffff) bad++;
    CHECKF(bad == 0, "%d pixels not white after GdiFlush", bad);
    U(PatBlt)(dc, 0, 0, 32, 32, BLACKNESS);
    U(GdiFlush)();
    bad = 0; for (int i = 0; i < 32 * 32; i++) if (p[i] & 0xffffff) bad++;
    CHECKF(bad == 0, "%d pixels not black after GdiFlush", bad);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_life_ntgdiflush_direct) {
    BD_NEED(); BD_NEED_NT(NtGdiFlush); REQUIRE(U(SelectObject)); REQUIRE(U(PatBlt));
    void *bits = NULL; HBITMAP hb = bd_dib(8, 8, 32, &bits); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    U(PatBlt)(dc, 0, 0, 8, 8, WHITENESS);
    /* gdi32full!GdiFlush ends in NtGdiFlush, which drains the TEB batch inside the kernel */
    UN(NtGdiFlush)();
    unsigned long cnt = 99; k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &cnt);
    t_note("GdiBatchCount %lu after a bare NtGdiFlush (the user32 side may not have reset it)", cnt);
    int bad = 0; const DWORD *p = bits; for (int i = 0; i < 64; i++) if ((p[i] & 0xffffff) != 0xffffff) bad++;
    CHECKF(bad == 0, "%d pixels stale after NtGdiFlush", bad);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
struct th_ctx1 { HBITMAP hb; void *bits; };
static DWORD WINAPI th_create(void *arg) {
    struct th_ctx1 *x = arg;
    x->hb = bd_dib(8, 8, 32, &x->bits);
    if (x->bits) ((DWORD *)x->bits)[3] = 0x13572468;
    return 0;
}
TEST(bd_life_bitmap_outlives_creating_thread) {
    BD_NEED();
    struct th_ctx1 c = { 0 };
    HANDLE t = CreateThread(NULL, 0, th_create, &c, 0, NULL);
    REQUIRE(t);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    REQUIRE(c.hb && c.bits);
    /* the surface is owned by the process: the thread's exit does not free it */
    DIBSECTION ds;
    CHECK(bd_getdib(c.hb, &ds) == (int)sizeof ds);
    CHECK(ds.dsBm.bmBits == c.bits);
    CHECK(((DWORD *)c.bits)[3] == 0x13572468);
    CHECK(U(DeleteObject)(c.hb));
    CHECK(range_gone(c.bits));
}
struct th_ctx2 { HBITMAP hb; HGDIOBJ old; HDC dc; int ok; };
static DWORD WINAPI th_select(void *arg) {
    struct th_ctx2 *x = arg;
    x->dc = bd_mem_dc();
    if (x->dc) {
        x->old = U(SelectObject)(x->dc, x->hb);
        x->ok = x->old != NULL;
        if (x->ok) U(SelectObject)(x->dc, x->old);
        U(DeleteDC)(x->dc);
    }
    return 0;
}
TEST(bd_life_select_on_other_thread_dc) {
    BD_NEED(); REQUIRE(U(SelectObject));
    struct th_ctx2 c = { 0 };
    c.hb = bd_dib(8, 8, 32, NULL); REQUIRE(c.hb);
    HANDLE t = CreateThread(NULL, 0, th_select, &c, 0, NULL);
    if (!t) { U(DeleteObject)(c.hb); t_skip("no thread"); return; }
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    /* a DC from another thread of the same process can hold the bitmap; GDI objects have process, not thread, affinity */
    CHECK(c.ok);
    U(DeleteObject)(c.hb);
}

/* ---- child process: private objects and exit cleanup ------------------------------------------------------------------------- */
CHILD(bd_dib_release) {
    int fails = 0;
    if (!U(CreateDIBSection) || !U(DeleteObject)) { printf("NOTE no gdi32\n"); return 0; }
    for (int i = 0; i < 40; i++) {
        void *bits = NULL; HBITMAP hb = bd_dib(32 + i, 16, (i & 1) ? 24 : 32, &bits);
        if (!hb || !bits) { printf("FAIL create %d\n", i); fails++; continue; }
        MEMORY_BASIC_INFORMATION mbi; bd_query(bits, &mbi);
        if (mbi.State != MEM_COMMIT) { printf("FAIL %d not committed\n", i); fails++; }
        U(DeleteObject)(hb);
        if (!range_gone(bits)) { printf("FAIL %d still committed after delete (%p)\n", i, bits); fails++; }
    }
    return fails;
}
TEST(bd_life_child_dib_release_cycle) {
    BD_NEED();
    struct k_child_result r = k_child_run("bd_dib_release", "", 30000);
    CHECKF(r.exit_code == 0, "child exit %d", (int)r.exit_code);
}

CHILD(bd_foreign_handle) {
    /* argv[1]: a bitmap handle that belongs to the parent process */
    int fails = 0;
    ULONG_PTR h = argc > 1 ? (ULONG_PTR)strtoull(argv[1], NULL, 16) : 0;
    if (!U(GetObjectW) || !U(GetObjectType)) return 0;
    BITMAP bm; memset(&bm, 0, sizeof bm);
    int n = U(GetObjectW)((HBITMAP)h, sizeof bm, &bm);
    DWORD t = U(GetObjectType)((HGDIOBJ)h);
    printf("NOTE foreign bitmap %p: GetObject=%d GetObjectType=%lu\n", (void *)h, n, t);
    /* nothing the child does with the parent's private handle may succeed in deleting it */
    U(DeleteObject)((HGDIOBJ)h);
    return fails;
}
TEST(bd_life_child_foreign_handle_survives) {
    BD_NEED();
    HBITMAP hb = bd_dib(8, 8, 32, NULL); REQUIRE(hb);
    char arg[64]; snprintf(arg, sizeof arg, "%llx", (unsigned long long)(ULONG_PTR)hb);
    struct k_child_result r = k_child_run("bd_foreign_handle", arg, 30000);
    CHECKF(r.exit_code == 0, "child exit %d", (int)r.exit_code);
    /* another process cannot delete a private bitmap of ours (its cell owner pid does not match) */
    DIBSECTION ds; CHECK(bd_getdib(hb, &ds) == (int)sizeof ds);
    CHECK(U(GetObjectType)(hb) == OBJ_BITMAP);
    U(DeleteObject)(hb);
}
