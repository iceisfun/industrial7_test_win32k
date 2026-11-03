/* HMONITOR handles in the USER shared handle table and the tagMONITOR objects in the shared heap.
 *
 * HMONITORs are TYPE_MONITOR (12) entries of gSharedInfo.aheList: user32!GetMonitorInfoW checks
 *, the uniq word, the "destroyed" bit (entry+0x19 bit 0) and `entry+0x18 == 12`
 * before rebasing entry+0 (a kernel address) with gSharedInfo.ulSharedDelta. The list head lives in the client
 * DISPLAYINFO at gSharedInfo.pDispInfo (+0x10 first monitor, +8 primary, links rebased with the same delta).
 */
#include "dm.h"

#define K_TYPE_MONITOR 12

static int heap_or_skip(struct dm_heap *h) {
    if (t_is_wine()) { t_skip("Wine has no USER shared monitor list"); return 0; }
    if (!dm_heap_read(h) || h->n == 0) { t_skip("monitor shared heap not readable"); return 0; }
    return 1;
}

TEST(display_monitors_dpi_heap_hmonitor_is_type_monitor_entry) {
    NEED_GUI();
    if (t_is_wine()) { t_skip("Wine has no USER handle table"); return; }
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        struct k_user_entry ue;
        if (!k_user_entry(e.m[i].h, &ue)) { t_skip("USER handle table not readable"); return; }
        CHECKF(ue.type == K_TYPE_MONITOR, "monitor %d handle %p entry type %u", i, (void *)e.m[i].h, ue.type);
        CHECKF((ue.flags & 1) == 0, "monitor %d entry flags 0x%x (bit0 = destroyed)", i, ue.flags);
        CHECKF(ue.uniq == (unsigned short)((ULONG_PTR)e.m[i].h >> 16), "uniq 0x%x vs HIWORD 0x%x", ue.uniq, (unsigned)((ULONG_PTR)e.m[i].h >> 16));
    }
}

TEST(display_monitors_dpi_heap_hmonitor_index_is_below_table_capacity) {
    NEED_GUI();
    if (t_is_wine()) { t_skip("Wine has no USER handle table"); return; }
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("gSharedInfo unavailable"); return; }
    ULONG_PTR cap = 0;
    if (!k_try_read((char *)si.psi + 8, &cap, sizeof cap)) { t_skip("server info not readable"); return; }
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) CHECKF(((ULONG_PTR)e.m[i].h & 0xffff) < cap, "index %lu vs capacity %lu", (unsigned long)((ULONG_PTR)e.m[i].h & 0xffff), (unsigned long)cap);
}

TEST(display_monitors_dpi_heap_entry_pointer_rebases_to_the_monitor_object) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        struct k_user_entry ue;
        if (!k_user_entry(e.m[i].h, &ue)) { t_skip("USER handle table not readable"); return; }
        ULONG_PTR obj = (ULONG_PTR)ue.delta + h.delta;
        ULONG_PTR head = 0;
        CHECKF(k_try_read((void *)obj, &head, sizeof head), "monitor %d object %p unreadable", i, (void *)obj);
        CHECKF((HMONITOR)head == e.m[i].h, "object head %p vs handle %p", (void *)head, (void *)e.m[i].h);   /* MonitorFromWindow returns *(HMONITOR*)tagMONITOR */
    }
}

TEST(display_monitors_dpi_heap_list_equals_enumeration) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    int active = 0;
    for (int i = 0; i < h.n; i++) {
        if (!dm_heap_is_active(&h.m[i])) continue;
        active++;
        int found = 0; for (int j = 0; j < e.n; j++) if (e.m[j].h == h.m[i].h) found = 1;
        CHECKF(found, "active heap monitor %p not reported by EnumDisplayMonitors", (void *)h.m[i].h);
    }
    CHECKF(active == e.total, "%d active in heap, %d enumerated", active, e.total);
    for (int j = 0; j < e.n; j++) {
        int found = 0; for (int i = 0; i < h.n; i++) if (h.m[i].h == e.m[j].h) found = 1;
        CHECKF(found, "enumerated monitor %p missing from the heap list", (void *)e.m[j].h);
    }
}

TEST(display_monitors_dpi_heap_enum_order_is_list_order) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    int k = 0;
    for (int i = 0; i < h.n && k < e.n; i++) {
        if (!dm_heap_is_active(&h.m[i])) continue;
        CHECKF(h.m[i].h == e.m[k].h, "position %d: heap %p enum %p", k, (void *)h.m[i].h, (void *)e.m[k].h);   /* enumeration walks the same monitor list */
        k++;
    }
}

TEST(display_monitors_dpi_heap_primary_pointer_is_the_primary_monitor) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    CHECK(h.primary >= 0);
    if (h.primary >= 0) {
        CHECK(h.m[h.primary].h == dm_primary());
        MONITORINFO mi = { sizeof mi };
        U(GetMonitorInfoW)(h.m[h.primary].h, &mi);
        CHECK(mi.dwFlags == MONITORINFOF_PRIMARY);
        CHECK(dm_heap_is_active(&h.m[h.primary]));
    }
}

TEST(display_monitors_dpi_heap_rects_match_pmv2_monitorinfo) {
    NEED_GUI(); REQUIRE(U(SetThreadDpiAwarenessContext));
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (int i = 0; i < h.n; i++) {
        if (!dm_heap_is_active(&h.m[i])) continue;
        MONITORINFO mi = { sizeof mi };
        if (!U(GetMonitorInfoW)(h.m[i].h, &mi)) { CHECKF(0, "GetMonitorInfoW(%p)", (void *)h.m[i].h); continue; }
        CHECKF(dm_rect_eq(&h.m[i].mon, &mi.rcMonitor), "monitor %d heap rect differs from rcMonitor", i);
        CHECKF(dm_rect_eq(&h.m[i].work, &mi.rcWork), "monitor %d heap work rect differs from rcWork", i);
    }
}

TEST(display_monitors_dpi_heap_work_rect_inside_monitor_rect) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    for (int i = 0; i < h.n; i++) {
        if (!dm_heap_is_active(&h.m[i])) continue;
        CHECK(h.m[i].mon.right > h.m[i].mon.left && h.m[i].mon.bottom > h.m[i].mon.top);
        CHECK(dm_rect_in(&h.m[i].work, &h.m[i].mon));
    }
}

TEST(display_monitors_dpi_heap_monitor_dpi_field_is_plausible) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    for (int i = 0; i < h.n; i++) {
        if (!dm_heap_is_active(&h.m[i])) continue;
        if (h.m[i].dpi < 96 || h.m[i].dpi > 960) t_note("monitor %d dpi field %u outside the usual 96..960 range", i, h.m[i].dpi);
        CHECKF(h.m[i].dpi != 0, "monitor %d dpi field is 0 (ScaleDPIRect would divide by it)", i);
    }
}

TEST(display_monitors_dpi_heap_single_monitor_flag_implies_primary_fallback) {
    NEED_GUI(); REQUIRE(U(MonitorFromPoint));
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    t_note("pDispInfo single-monitor flag = %ld, %d monitors in list", h.single, h.n);
    if (h.single == 1) {
        /* _MonitorFromPoint/_MonitorFromRect/_MonitorFromWindowInternal returns the primary at once */
        POINT far_pt = { -100000, 100000 };
        HMONITOR p = dm_primary();
        CHECK(U(MonitorFromPoint)(far_pt, MONITOR_DEFAULTTOPRIMARY) == p);
        CHECK(U(MonitorFromPoint)(far_pt, MONITOR_DEFAULTTONEAREST) == p);
        CHECK(U(MonitorFromPoint)(far_pt, MONITOR_DEFAULTTONULL) == NULL);
    }
}

TEST(display_monitors_dpi_heap_list_has_no_cycles_and_is_bounded) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    CHECK(h.n < DM_MAXMON);
    for (int i = 0; i < h.n; i++) for (int j = i + 1; j < h.n; j++) CHECK(h.m[i].kptr != h.m[j].kptr);
}

TEST(display_monitors_dpi_heap_monitor_objects_are_in_the_shared_mapping) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    /* tagMONITOR lives in the shared heap, a read-only view mapped once per session; every monitor object must be
     * readable and lie in the same allocation as the DISPLAYINFO pointer rebased the same way. */
    MEMORY_BASIC_INFORMATION mbi0;
    void *si = k_shared_info_ptr(); void *pdisp = NULL;
    if (!si || !k_try_read_ptr((char *)si + 0x18, &pdisp) || !pdisp) { t_skip("pDispInfo unavailable"); return; }
    if (!VirtualQuery(pdisp, &mbi0, sizeof mbi0)) { t_skip("VirtualQuery failed"); return; }
    for (int i = 0; i < h.n; i++) {
        MEMORY_BASIC_INFORMATION mbi;
        CHECK(VirtualQuery(h.m[i].cptr, &mbi, sizeof mbi) == sizeof mbi);
        CHECKF(mbi.State == MEM_COMMIT, "monitor %d object memory state 0x%lx", i, mbi.State);
        CHECKF(mbi.Type == MEM_MAPPED, "monitor %d object memory type 0x%lx (shared section view)", i, mbi.Type);
        CHECKF(mbi.AllocationBase == mbi0.AllocationBase, "monitor %d in a different mapping than pDispInfo", i);
    }
}

TEST(display_monitors_dpi_heap_monitor_memory_is_read_only_for_user_mode) {
    NEED_GUI();
    struct dm_heap h; if (!heap_or_skip(&h)) return;
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery(h.m[0].cptr, &mbi, sizeof mbi) == sizeof mbi);
    CHECKF((mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0, "shared heap view protect 0x%lx", mbi.Protect);
}

TEST(display_monitors_dpi_heap_destroyed_flag_clear_on_live_handles_after_repeated_enum) {
    NEED_GUI();
    if (t_is_wine()) { t_skip("Wine has no USER handle table"); return; }
    struct dm_enum a; dm_enum_all(&a, NULL, NULL);
    for (int rep = 0; rep < 20; rep++) { struct dm_enum b; dm_enum_all(&b, NULL, NULL); CHECK(b.total == a.total); }
    for (int i = 0; i < a.n; i++) {
        struct k_user_entry ue;
        if (!k_user_entry(a.m[i].h, &ue)) { t_skip("not readable"); return; }
        CHECK((ue.flags & 1) == 0 && ue.type == K_TYPE_MONITOR);
    }
}
