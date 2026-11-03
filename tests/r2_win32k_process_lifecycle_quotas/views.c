/* views: the memory a GUI process shares with win32k: the GDI shared handle table (PEB+0xF8), the DC_ATTR
 * pages (cell+0x10 = pUser), the desktop-heap view holding tagWND objects, and the USER handle table.
 * Expectations: client views of session sections are MEM_MAPPED and committed; the tables and the desktop heap are
 * PAGE_READONLY for the client (it only ever writes through syscalls); DC_ATTR must be writable (gdi32 batches
 * attribute changes into it without a syscall).
 */
#include "r2.h"

static void *gdi_table(void) { void *p = NULL; k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &p); return p; }

TEST(r2_win32k_process_lifecycle_quotas_views_gdi_table_mapped_readonly) {
    R2_NEED_WIN();
    void *t = gdi_table(); CHECK(t); REQUIRE(t);
    MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(t, &m));
    CHECK(m.State == MEM_COMMIT);
    CHECK(m.Type == MEM_MAPPED);
    CHECK(m.Protect == PAGE_READONLY);
}
TEST(r2_win32k_process_lifecycle_quotas_views_gdi_table_size_covers_cells) {
    R2_NEED_WIN();
    void *t = gdi_table(); REQUIRE(t);
    MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(t, &m));
    /* the table has 0x10000 cells of 0x18 bytes on 19041; at least the cells of any handle we can hold must be inside the view */
    SIZE_T avail = m.RegionSize - ((char *)t - (char *)m.BaseAddress);
    CHECKF(avail >= (SIZE_T)0x18 * 0x1000, "only 0x%llx bytes", (unsigned long long)avail);
    t_note("GDI table region 0x%llx bytes from table start, alloc base %p, table %p", (unsigned long long)avail, m.AllocationBase, t);
}
TEST(r2_win32k_process_lifecycle_quotas_views_gdi_table_protect_attempt_restores) {
    R2_NEED_WIN();
    void *t = gdi_table(); REQUIRE(t);
    /* whether the section allows a writable alias is not established by the analysis: observe, always restore */
    DWORD old = 0; SetLastError(0);
    BOOL ok = VirtualProtect(t, 0x1000, PAGE_READWRITE, &old);
    DWORD err = GetLastError();
    if (ok) { DWORD o2; VirtualProtect(t, 0x1000, old, &o2); }
    t_note("VirtualProtect(GDI table, RW) -> %d (error %lu), previous protect 0x%lx", ok, ok ? 0 : err, old);
    MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(t, &m)); CHECK(m.Protect == PAGE_READONLY);
}
TEST(r2_win32k_process_lifecycle_quotas_views_gdi_table_cell_of_new_brush) {
    R2_NEED_WIN();
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3)); REQUIRE(b);
    struct k_gdicell c;
    CHECK(k_gdi_cell(b, &c));
    /* HANDLE_TO_INDEX / GdiGetEntry compare the top byte of the handle with the cell byte at +0x0d */
    CHECK(c.type_check == (((ULONG_PTR)b >> 24) & 0xFF));
    unsigned char lo = 0; k_try_read_u8((char *)gdi_table() + ((ULONG_PTR)b & 0xFFFF) * GDI_CELL_SIZE + 0x0c, &lo);
    CHECK(lo == (((ULONG_PTR)b >> 16) & 0xFF));         /* low half of the handle's upper word is the object type */
    CHECK((ULONG_PTR)c.object >= 0xFFFF800000000000ULL);  /* pKernelAddress is a session-space pointer */
    U(DeleteObject)(b);
}
TEST(r2_win32k_process_lifecycle_quotas_views_gdi_cells_of_distinct_objects_distinct) {
    R2_NEED_WIN();
    HBRUSH b[8]; struct k_gdicell c[8];
    int n = 0;
    for (; n < 8; n++) { b[n] = U(CreateSolidBrush)(RGB(n, n, n)); if (!b[n]) break; }
    CHECK(n == 8);
    for (int i = 0; i < n; i++) CHECK(k_gdi_cell(b[i], &c[i]));
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) { CHECK(c[i].object != c[j].object); CHECK(((ULONG_PTR)b[i] & 0xFFFF) != ((ULONG_PTR)b[j] & 0xFFFF)); }
    for (int i = 0; i < n; i++) U(DeleteObject)(b[i]);
}
TEST(r2_win32k_process_lifecycle_quotas_views_dcattr_pointer_writable_user_page) {
    R2_NEED_WIN();
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    struct k_gdicell c; CHECK(k_gdi_cell(dc, &c));
    CHECK(c.field10 != NULL);
    if (c.field10) {
        MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(c.field10, &m));
        CHECK((ULONG_PTR)c.field10 < 0x7FFFFFFF0000ULL);
        CHECK(m.State == MEM_COMMIT);
        CHECK(m.Protect == PAGE_READWRITE);             /* gdi32 writes DC_ATTR directly when batching */
        t_note("DC_ATTR page: type 0x%lx region 0x%llx", (unsigned long)m.Type, (unsigned long long)m.RegionSize);
    }
    U(DeleteDC)(dc);
}
TEST(r2_win32k_process_lifecycle_quotas_views_dcattr_pointers_distinct_per_dc) {
    R2_NEED_WIN();
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(NULL); REQUIRE(a && b);
    struct k_gdicell ca, cb; CHECK(k_gdi_cell(a, &ca)); CHECK(k_gdi_cell(b, &cb));
    CHECK(ca.field10 && cb.field10 && ca.field10 != cb.field10);
    U(DeleteDC)(a); U(DeleteDC)(b);
}
TEST(r2_win32k_process_lifecycle_quotas_views_dcattr_roundtrip_through_pointer) {
    R2_NEED_WIN();
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    struct k_gdicell c; CHECK(k_gdi_cell(dc, &c));
    /* SetTextColor(dc, x) returns the previous color; the new color is then visible through DC_ATTR without a syscall */
    U(SetTextColor)(dc, RGB(0x11, 0x22, 0x33));
    CHECK(U(GetTextColor)(dc) == RGB(0x11, 0x22, 0x33));
    if (c.field10) {
        int hits = 0;
        for (unsigned off = 0; off < 0x300; off += 4) { unsigned long v = 0; if (k_try_read_u32((char *)c.field10 + off, &v) && v == RGB(0x11, 0x22, 0x33)) hits++; }
        CHECKF(hits >= 1, "text color not found in the first 0x300 bytes of DC_ATTR");
    }
    U(DeleteDC)(dc);
}
TEST(r2_win32k_process_lifecycle_quotas_views_desktop_heap_wnd_read_only_mapped) {
    R2_NEED_WIN();
    HWND w = r2_window(); REQUIRE(w);
    void *p = k_wnd_ptr(w);
    if (!p) { t_note("fast path unavailable"); U(DestroyWindow)(w); return; }
    MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(p, &m));
    CHECK(m.State == MEM_COMMIT); CHECK(m.Type == MEM_MAPPED);
    CHECK(m.Protect == PAGE_READONLY);                  /* the client view of the desktop heap is read-only */
    U(DestroyWindow)(w);
}
TEST(r2_win32k_process_lifecycle_quotas_views_desktop_heap_windows_share_one_view) {
    R2_NEED_WIN();
    HWND a = r2_window(), b = r2_window(); REQUIRE(a && b);
    void *pa = k_wnd_ptr(a), *pb = k_wnd_ptr(b);
    if (pa && pb) {
        MEMORY_BASIC_INFORMATION ma, mb; CHECK(r2_vq(pa, &ma)); CHECK(r2_vq(pb, &mb));
        CHECK(ma.AllocationBase == mb.AllocationBase);  /* same desktop -> same heap mapping */
        CHECK(pa != pb);
    } else t_note("fast path unavailable");
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}
TEST(r2_win32k_process_lifecycle_quotas_views_desktop_heap_head_handle) {
    R2_NEED_WIN();
    HWND w = r2_window(); REQUIRE(w);
    void *p = k_wnd_ptr(w);
    if (p) {
        void *h = NULL; CHECK(k_try_read_ptr(p, &h));
        CHECK(h == (void *)w);                          /* THRDESKHEAD.h is the first field of every USER object */
    }
    U(DestroyWindow)(w);
}
TEST(r2_win32k_process_lifecycle_quotas_views_user_handle_entry_matches_window) {
    R2_NEED_WIN();
    HWND w = r2_window(); REQUIRE(w);
    struct k_user_entry e;
    CHECK(k_user_entry(w, &e));
    CHECK(e.type == K_TYPE_WINDOW);
    CHECK(e.uniq == (((ULONG_PTR)w >> 16) & 0xFFFF));
    U(DestroyWindow)(w);
    CHECK(k_user_entry(w, &e));
    CHECK(e.type == K_TYPE_FREE || e.uniq != (((ULONG_PTR)w >> 16) & 0xFFFF) || (e.flags & 1));
}
TEST(r2_win32k_process_lifecycle_quotas_views_shared_view_addresses_stable_over_object_churn) {
    R2_NEED_WIN();
    struct k_user_shared_info a, b;
    void *g0 = gdi_table();
    CHECK(k_shared_info(&a));
    for (int i = 0; i < 40; i++) { HBRUSH x = U(CreateSolidBrush)(i); HWND w = r2_window(); if (x) U(DeleteObject)(x); if (w) U(DestroyWindow)(w); }
    CHECK(k_shared_info(&b));
    CHECK(a.psi == b.psi); CHECK(a.aheList == b.aheList); CHECK(g0 == gdi_table());
}
