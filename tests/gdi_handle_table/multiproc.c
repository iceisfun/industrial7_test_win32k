/* The handle table as seen across processes and process lifetimes. The table is one session-wide section mapped
 * read-only into every process; ownership lives in cell+8. All child state is exchanged through a named mapping. */
#include "gh.h"

static HANDLE gh_event(unsigned ppid, const char *what, int create) {
    char nm[96];
    gh_shm_name(nm, sizeof nm, ppid, what);
    return create ? CreateEventA(NULL, TRUE, FALSE, nm) : OpenEventA(EVENT_ALL_ACCESS, FALSE, nm);
}

/* ---- peer child: another process looks at, and pokes, the parent's objects ---- */
CHILD(gh_peer) {
    gh_child_fails = 0;
    unsigned ppid = (unsigned)atoi(argv[1]);
    HANDLE hm; struct gh_shm *s = gh_shm_open(ppid, 0, &hm);
    CC(s != NULL);
    if (!s) return 100;
    if (!gh_connect()) { printf("  SKIP no GDI table in child\n"); return 0; }
    unsigned me = GetCurrentProcessId();
    CC(me != ppid);
    for (unsigned i = 0; i < s->nph; i++) {
        HGDIOBJ h = (HGDIOBJ)(ULONG_PTR)s->ph[i];
        struct gh_cell c;
        CCF(gh_cell_of(h, &c), "parent handle %p unreadable in child", h);
        if (!gh_cell_of(h, &c)) continue;
        /* same session table: the parent's entry is visible with the parent's pid in the owner word */
        CCF(c.objt == (s->ph_type[i] & 0x1f) || c.objt == 6, "handle %p objt 0x%x type 0x%x", h, c.objt, s->ph_type[i]);
        CCF((c.owner & ~1u) == ppid, "handle %p owner 0x%x parent %u", h, c.owner, ppid);
        CCF(c.upper == GH_HIWORD(h), "handle %p upper 0x%04x", h, c.upper);
        /* gdi32full!GetObjectType rejects entries whose owner is neither this pid nor 0 */
        CCF(U(GetObjectType)(h) == 0, "GetObjectType(parent object %p) = %lu", h, U(GetObjectType)(h));
    }
    /* stock objects are public: identical handle values and a working GetObjectType here */
    for (int i = 0; i < 20; i++) {
        if (!s->stock[i]) continue;
        HGDIOBJ h = U(GetStockObject)(i);
        CCF((ULONG_PTR)h == s->stock[i], "stock %d child %p parent %llx", i, h, s->stock[i]);
        CCF(U(GetObjectType)(h) != 0, "GetObjectType(stock %d) = 0 in child", i);
    }
    /* own objects */
    HRGN r = U(CreateRectRgn)(0, 0, 5, 5);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CC(r && b && dc);
    HGDIOBJ mine[3] = { r, b, dc };
    for (int k = 0; k < 3; k++) {
        struct gh_cell c;
        CC(gh_cell_of(mine[k], &c));
        CCF((c.owner & ~1u) == me, "own object %p owner 0x%x", mine[k], c.owner);
        for (unsigned i = 0; i < s->nph; i++) CCF(GH_IDX(mine[k]) != GH_IDX((HGDIOBJ)(ULONG_PTR)s->ph[i]), "child object shares index with parent object");
    }
    CCF(gh_count_owned(me) == (int)U(GetGuiResources)(GetCurrentProcess(), 0), "own count %d vs GR %lu", gh_count_owned(me), U(GetGuiResources)(GetCurrentProcess(), 0));
    CC(gh_count_owned(ppid) >= (int)s->nph);
    /* try to destroy / use the parent's objects */
    for (unsigned i = 0; i < s->nph; i++) {
        HGDIOBJ h = (HGDIOBJ)(ULONG_PTR)s->ph[i];
        ULONG_PTR rv = UN(NtGdiDeleteObjectApp) ? UN(NtGdiDeleteObjectApp)((HANDLE)h) : 0;
        BOOL dv = U(DeleteObject)(h);
        printf("  NOTE foreign delete type 0x%x: NtGdiDeleteObjectApp=0x%llx DeleteObject=%d\n", s->ph_type[i], (unsigned long long)rv, dv);
        struct gh_cell c;
        CCF(gh_cell_of(h, &c) && c.objt != 0 && (c.owner & ~1u) == ppid, "foreign delete freed parent object %p", h);
        if (s->ph_type[i] == 0x01 && UN(NtGdiGetDCObject)) CCF(UN(NtGdiGetDCObject)((HANDLE)h, 0x100000) == NULL, "NtGdiGetDCObject on a foreign DC returned a handle");
    }
    s->child_pid = me;
    s->child_gr = U(GetGuiResources)(GetCurrentProcess(), 0);
    U(DeleteDC)(dc); U(DeleteObject)(b); U(DeleteObject)(r);
    CC(gh_count_owned(me) == (int)U(GetGuiResources)(GetCurrentProcess(), 0));
    return gh_child_fails > 100 ? 100 : gh_child_fails;
}

static int make_parent_objects(struct gh_shm *s, HGDIOBJ *keep, const struct gh_kind **kinds, int cap) {
    static const struct gh_kind *want[] = { &gk_rgn_rect, &gk_bmp_color, &gk_brush_solid, &gk_pen_solid, &gk_font_indirect,
                                            &gk_pal_logical, &gk_dc_memory, &gk_bmp_dib24 };
    int n = 0;
    for (size_t i = 0; i < sizeof want / sizeof want[0] && n < cap; i++) {
        HGDIOBJ h = want[i]->mk();
        if (!h) continue;
        keep[n] = h; kinds[n] = want[i];
        s->ph[n] = (unsigned long long)(ULONG_PTR)h;
        s->ph_type[n] = GH_TYPE7(h);
        n++;
    }
    s->nph = n;
    for (int i = 0; i < 20; i++) s->stock[i] = (unsigned long long)(ULONG_PTR)U(GetStockObject)(i);
    return n;
}

TEST(gdi_handle_table_mp_peer_sees_parent_cells_and_cannot_free_them) {
    GH_NEED();
    HANDLE hm; HGDIOBJ keep[8]; const struct gh_kind *kinds[8];
    struct gh_shm *s = gh_shm_open(GH_PID(), 1, &hm);
    REQUIRE(s);
    memset(s, 0, sizeof *s);
    s->parent_pid = GH_PID();
    int n = make_parent_objects(s, keep, kinds, 8);
    int before = gh_count_owned(GH_PID());
    DWORD gr0 = U(GetGuiResources)(GetCurrentProcess(), 0);
    char arg[32]; snprintf(arg, sizeof arg, "%u", GH_PID());
    struct k_child_result r = k_child_run("gh_peer", arg, 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", r.exit_code);
    /* every parent object is still alive and unchanged */
    for (int i = 0; i < n; i++) {
        struct gh_cell c;
        CHECKF(U(GetObjectType)(keep[i]) == kinds[i]->objtype, "%s no longer valid after the peer's attempts", kinds[i]->name);
        CHECK(gh_cell_of(keep[i], &c) && c.objt != 0 && (c.owner & ~1u) == GH_PID());
    }
    CHECK(gh_count_owned(GH_PID()) == before);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), 0) == gr0);
    if (s->child_pid) CHECK(s->child_pid != GH_PID());
    for (int i = n - 1; i >= 0; i--) kinds[i]->rm(keep[i]);
    UnmapViewOfFile(s); CloseHandle(hm);
}

/* ---- the child leaks objects; the kernel frees them at process exit ---- */
CHILD(gh_leaker) {
    unsigned ppid = (unsigned)atoi(argv[1]);
    HANDLE hm; struct gh_shm *s = gh_shm_open(ppid, 0, &hm);
    if (!s) return 100;
    if (!gh_connect()) return 0;
    unsigned n = 0;
    for (int round = 0; round < 2; round++)
        for (size_t i = 0; i < GH_NKINDS && n < 60; i++) {
            const struct gh_kind *k = gh_all_kinds[i];
            if (k->nh != 1) continue;
            HGDIOBJ h = k->mk();
            if (h) s->ch[n++] = (unsigned long long)(ULONG_PTR)h;
        }
    s->nch = n;
    s->child_pid = GetCurrentProcessId();
    s->child_gr = U(GetGuiResources)(GetCurrentProcess(), 0);
    printf("  NOTE leaking %u objects\n", n);
    return 0;
}
TEST(gdi_handle_table_mp_exit_releases_cells) {
    GH_NEED();
    HANDLE hm;
    struct gh_shm *s = gh_shm_open(GH_PID() + 1, 1, &hm);   /* distinct mapping name from the peer test */
    REQUIRE(s);
    memset(s, 0, sizeof *s);
    char arg[32]; snprintf(arg, sizeof arg, "%u", GH_PID() + 1);
    struct k_child_result r = k_child_run("gh_leaker", arg, 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", r.exit_code);
    if (!s->nch) { t_skip("child made nothing"); return; }
    t_note("child leaked %u objects, GR_GDIOBJECTS it saw: %u", s->nch, s->child_gr);
    CHECK(s->child_gr >= s->nch);
    /* GdiProcessCallout(delete) -> NtGdiCloseProcess frees every object owned by the pid; poll briefly for the worker */
    int live = 0;
    for (int tries = 0; tries < 30; tries++) {
        live = 0;
        for (unsigned i = 0; i < s->nch; i++) {
            struct gh_cell c;
            if (gh_cell_of((HGDIOBJ)(ULONG_PTR)s->ch[i], &c) && c.objt != 0 && (c.owner & ~1u) == (s->child_pid & ~3u)) live++;
        }
        if (!live) break;
        Sleep(100);
    }
    CHECKF(live == 0, "%d of %u cells still owned by the exited process", live, s->nch);
    CHECK(gh_count_owned(s->child_pid) == 0);
    UnmapViewOfFile(s); CloseHandle(hm);
}

/* ---- another process queried through its process handle ---- */
CHILD(gh_gui_idle) {
    unsigned ppid = (unsigned)atoi(argv[1]);
    HANDLE hm; struct gh_shm *s = gh_shm_open(ppid, 0, &hm);
    HANDLE evGo = gh_event(ppid, "go", 0);
    if (!s || !evGo) return 100;
    if (!gh_connect()) return 0;
    HRGN r[7];
    for (int i = 0; i < 7; i++) r[i] = U(CreateRectRgn)(0, 0, 2, 2);
    s->child_gr = U(GetGuiResources)(GetCurrentProcess(), 0);
    InterlockedExchange(&s->ready, 1);
    WaitForSingleObject(evGo, 8000);
    for (int i = 0; i < 7; i++) if (r[i]) U(DeleteObject)(r[i]);
    return 0;
}
TEST(gdi_handle_table_mp_getguiresources_other_process) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    unsigned key = GH_PID() + 2;
    HANDLE hm, evGo = gh_event(key, "go", 1);
    struct gh_shm *s = gh_shm_open(key, 1, &hm);
    REQUIRE(s && evGo);
    memset(s, 0, sizeof *s);
    char arg[32]; snprintf(arg, sizeof arg, "%u", key);
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("gh_gui_idle", arg, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    REQUIRE(hp);
    for (int i = 0; i < 100 && !s->ready; i++) Sleep(50);
    if (!s->ready) { t_skip("child never became ready"); TerminateProcess(hp, 1); CloseHandle(hp); CloseHandle(pi.hThread); return; }
    /* the count the child sees for itself is what the kernel reports for its process handle */
    DWORD g = GHN(NtUserGetGuiResources)(hp, 0);
    CHECKF(g == s->child_gr, "GetGuiResources(child) = %lu, child saw %u", g, s->child_gr);
    CHECK(U(GetGuiResources)(hp, 0) == s->child_gr);
    CHECK(GHN(NtUserGetGuiResources)(hp, 2) >= g);
    CHECK(gh_count_owned(pi.dwProcessId) == (int)g);
    CHECK(g >= 7);
    SetEvent(evGo);
    WaitForSingleObject(hp, 8000);
    CloseHandle(hp); CloseHandle(pi.hThread); CloseHandle(evGo);
    UnmapViewOfFile(s); CloseHandle(hm);
}
CHILD(gh_pristine_idle) { Sleep(6000); return 0; }
TEST(gdi_handle_table_mp_getguiresources_process_without_gui) {
    REQUIRE(U(GetGuiResources));
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("gh_pristine_idle", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    REQUIRE(hp);
    Sleep(300);
    SetLastError(0);
    /* the child never made a win32k call, so PsGetProcessWin32Process is NULL: result 0, ERROR_INVALID_PARAMETER */
    DWORD g = U(GetGuiResources)(hp, 0);
    DWORD e = GetLastError();
    CHECKF(g == 0, "count %lu", g);
    t_note("GetGuiResources(process without win32k state) error %lu", e);
    TerminateProcess(hp, 0);
    WaitForSingleObject(hp, 5000);
    CloseHandle(hp); CloseHandle(pi.hThread);
}

/* ---- a fresh process connects to GDI on its first win32k call ---- */
CHILD(gh_pristine) {
    gh_child_fails = 0;
    void *before = NULL;
    k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &before);
    printf("  NOTE PEB.GdiSharedHandleTable before any GUI call: %p\n", before);
    printf("  NOTE gdi32 loaded before first GUI call: %d\n", GetModuleHandleA("gdi32.dll") != NULL);
    HDC dc = U(CreateCompatibleDC)(NULL);
    CC(dc != NULL);
    void *base = gh_base();
    CC(base != NULL);
    unsigned long dcal = 0;
    k_try_read_u32((char *)k_peb() + PEB_GdiDCAttributeList, &dcal);
    CCF(dcal == 0x14, "PEB+0x108 = 0x%lx", dcal);
    MEMORY_BASIC_INFORMATION mbi;
    if (base && VirtualQuery(base, &mbi, sizeof mbi)) CC(mbi.Type == MEM_MAPPED && mbi.Protect == PAGE_READONLY);
    struct gh_cell c;
    CC(dc && gh_cell_of(dc, &c) && (c.owner & ~1u) == GetCurrentProcessId());
    unsigned char hb[0xf0]; int nz = 0;
    k_try_read((char *)k_peb() + PEB_GdiHandleBuffer, hb, sizeof hb);
    for (int i = 0; i < 0xf0; i++) nz += hb[i] != 0;
    printf("  NOTE GdiHandleBuffer non-zero bytes: %d\n", nz);
    int cells = gh_count_owned(GetCurrentProcessId());
    DWORD gr = U(GetGuiResources)(GetCurrentProcess(), 0);
    CCF((int)gr == cells, "GR_GDIOBJECTS %lu vs owned cells %d", gr, cells);
    CCF(gr >= 1, "fresh process owns %lu GDI objects after creating a DC", gr);
    printf("  NOTE fresh process owns %lu GDI objects after one DC\n", gr);
    U(DeleteDC)(dc);
    CC(gh_count_owned(GetCurrentProcessId()) == cells - 1);
    CC((int)U(GetGuiResources)(GetCurrentProcess(), 0) == cells - 1);
    return gh_child_fails;
}
TEST(gdi_handle_table_mp_fresh_process_connect) {
    struct k_child_result r = k_child_run("gh_pristine", NULL, 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", r.exit_code);
}

/* ---- per-process handle quota (fail once count >= the process handle quota) ---- */
CHILD(gh_quota) {
    gh_child_fails = 0;
    DWORD q = 10000, sz = sizeof q;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", "GDIProcessHandleQuota",
                     RRF_RT_REG_DWORD, NULL, &q, &sz) != ERROR_SUCCESS) q = 10000;
    printf("  NOTE GDIProcessHandleQuota = %lu\n", q);
    if (q < 256 || q > 11500) { printf("  SKIP quota %lu outside the range this test allocates\n", q); return 0; }
    if (!gh_connect()) return 0;
    static HRGN h[12000];
    int n = 0;
    DWORD base = U(GetGuiResources)(GetCurrentProcess(), 0);
    for (; n < 12000; n++) { h[n] = U(CreateRectRgn)(0, 0, 1, 1); if (!h[n]) break; }
    DWORD last_err = GetLastError();
    DWORD gr = U(GetGuiResources)(GetCurrentProcess(), 0);
    printf("  NOTE allocation stopped after %d regions (baseline %lu), GR=%lu, error %lu\n", n, base, gr, last_err);
    CCF(n < 12000, "no failure within 12000 allocations");
    CCF(gr == q, "GR_GDIOBJECTS at failure %lu quota %lu", gr, q);
    CCF((DWORD)n + base == q, "created %d + baseline %lu != quota %lu", n, base, q);
    CC(U(GetGuiResources)(GetCurrentProcess(), 2) == q);
    /* every other GDI allocation is refused too */
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    CC(b == NULL);
    HBITMAP bm = U(CreateBitmap)(1, 1, 1, 1, NULL);
    CC(bm == NULL);
    HDC dc = U(CreateCompatibleDC)(NULL);
    CC(dc == NULL);
    /* Freed rectangular regions first go to a small cache that keeps their handles counted, so free a batch: what is
       released for real is the batch minus the cache, and exactly that many further allocations get through. */
    int freed = n > 40 ? 40 : n;
    for (int i = 0; i < freed; i++) CC(U(DeleteObject)(h[--n]));
    DWORD now = U(GetGuiResources)(GetCurrentProcess(), 0);
    int room = (int)(q - now);
    printf("  NOTE freed %d regions, %d slots became free\n", freed, room);
    CCF(room >= freed - 16 && room <= freed, "room %d after freeing %d", room, freed);
    HGDIOBJ got[64]; int ng = 0;
    for (; ng < 64; ng++) { got[ng] = U(CreateHatchBrush)(HS_CROSS, RGB(1, 2, 3)); if (!got[ng]) break; }
    CCF(ng == room, "%d hatch brushes created, %d slots free", ng, room);
    for (int i = 0; i < ng; i++) U(DeleteObject)(got[i]);
    for (int i = 0; i < n; i++) U(DeleteObject)(h[i]);
    DWORD end = U(GetGuiResources)(GetCurrentProcess(), 0);
    CCF(end >= base && end - base <= 16, "GR_GDIOBJECTS %lu after deleting everything, baseline %lu", end, base);
    return gh_child_fails;
}
TEST(gdi_handle_table_mp_process_handle_quota) {
    if (t_is_wine()) { t_skip("Wine has no GDI handle quota"); return; }
    struct k_child_result r = k_child_run("gh_quota", NULL, 60000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", r.exit_code);
}
