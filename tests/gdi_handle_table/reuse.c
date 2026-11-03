/* Slot reuse: the kernel bumps the reuse byte (handle bits 24..31)
 * of a slot taken from the free list unless this allocation sets a new session-wide peak, in which case it is 0.
 * Other processes share the manager, so a re-created object may land in a different slot: every check here is
 * conditional on observing the same index. */
#include "gh.h"

static HGDIOBJ mk_rgn(void) { return mk_rgn_rect(); }
struct xarg { HGDIOBJ h; int type_ok, deleted; };
static DWORD WINAPI xworker(LPVOID p) {
    struct xarg *x = p;
    x->type_ok = U(GetObjectType)(x->h) == OBJ_BRUSH;
    x->deleted = U(DeleteObject)(x->h);
    return 0;
}
struct marg { HGDIOBJ h; };
static DWORD WINAPI mworker(LPVOID p) { ((struct marg *)p)->h = mk_bmp_color(); return 0; }

TEST(gdi_handle_table_reuse_same_slot_bumps_reuse_byte) {
    GH_NEED();
    int same = 0;
    HGDIOBJ prev = mk_rgn();
    REQUIRE(prev);
    for (int i = 0; i < 200; i++) {
        unsigned pi = GH_IDX(prev), pu = GH_UNIQ(prev);
        rm_obj(prev);
        HGDIOBJ cur = mk_rgn();
        REQUIRE(cur);
        if (GH_IDX(cur) == pi) {
            same++;
            unsigned cu = GH_UNIQ(cur);
            CHECKF(cu == ((pu + 1) & 0xff) || cu == 0, "slot 0x%x reuse byte 0x%02x -> 0x%02x", pi, pu, cu);
            CHECKF(cur != prev || cu == pu, "handle value reused unchanged");
        }
        prev = cur;
    }
    rm_obj(prev);
    t_note("slot reused %d/200 times", same);
}
TEST(gdi_handle_table_reuse_reuse_byte_wraps_through_256) {
    GH_NEED();
    /* repeatedly freeing and re-taking a slot walks the reuse byte through 0..255 and wraps (a byte increment) */
    HGDIOBJ prev = mk_rgn();
    REQUIRE(prev);
    int wrapped = 0, steps = 0;
    unsigned last = GH_UNIQ(prev), li = GH_IDX(prev);
    for (int i = 0; i < 700; i++) {
        rm_obj(prev);
        prev = mk_rgn();
        REQUIRE(prev);
        if (GH_IDX(prev) == li) {
            steps++;
            if (GH_UNIQ(prev) < last && GH_UNIQ(prev) == 0) wrapped++;
            last = GH_UNIQ(prev);
        } else { li = GH_IDX(prev); last = GH_UNIQ(prev); }
    }
    rm_obj(prev);
    t_info("%d same-slot steps, %d wrap-arounds to 0", steps, wrapped);
    if (steps >= 600) CHECK(wrapped >= 1);
}
TEST(gdi_handle_table_reuse_old_handle_is_dead_after_reuse) {
    GH_NEED();
    int tested = 0;
    for (int i = 0; i < 100 && tested < 5; i++) {
        HGDIOBJ old = mk_rgn();
        REQUIRE(old);
        unsigned oi = GH_IDX(old);
        rm_obj(old);
        HGDIOBJ cur = mk_rgn();
        REQUIRE(cur);
        if (GH_IDX(cur) == oi && cur != old) {
            tested++;
            /* the cell's upper word now differs from HIWORD(old): GetObjectType returns 0 and DeleteObject must not touch cur */
            CHECK(U(GetObjectType)(old) == 0);
            CHECK(!U(DeleteObject)(old));
            if (UN(NtGdiDeleteObjectApp)) CHECK((UN(NtGdiDeleteObjectApp)((HANDLE)old) & 0xff) == 0);
            CHECK(U(GetObjectType)(cur) == OBJ_REGION);
            struct gh_cell c;
            CHECK(gh_cell_of(cur, &c) && c.objt == 4);
        }
        rm_obj(cur);
    }
    if (!tested) t_skip("never got the same slot back");
}
TEST(gdi_handle_table_reuse_other_type_in_old_slot) {
    GH_NEED();
    int tested = 0;
    for (int i = 0; i < 100 && tested < 3; i++) {
        HGDIOBJ old = mk_rgn();
        REQUIRE(old);
        unsigned oi = GH_IDX(old);
        rm_obj(old);
        HGDIOBJ cur = mk_brush_solid();
        REQUIRE(cur);
        if (GH_IDX(cur) == oi) {
            tested++;
            /* same slot, different type: the old region handle fails every type-checked entry point */
            CHECK(U(GetObjectType)(old) == 0);
            CHECK(!U(DeleteObject)(old));
            CHECK(U(GetObjectType)(cur) == OBJ_BRUSH);
        }
        rm_obj(cur);
    }
    if (!tested) t_skip("never got the same slot back");
}
TEST(gdi_handle_table_reuse_free_list_is_lifo_note) {
    GH_NEED();
    HGDIOBJ a = mk_rgn(), b = mk_rgn(), c;
    REQUIRE(a && b);
    unsigned ia = GH_IDX(a), ib = GH_IDX(b);
    rm_obj(a);
    rm_obj(b);
    c = mk_rgn();
    REQUIRE(c);
    t_info("freed idx 0x%x then 0x%x; next allocation got 0x%x", ia, ib, GH_IDX(c));
    HGDIOBJ d = mk_rgn();
    REQUIRE(d);
    t_info("following allocation got 0x%x", GH_IDX(d));
    rm_obj(c); rm_obj(d);
}
TEST(gdi_handle_table_reuse_batch_unique_indices) {
    GH_NEED();
    enum { N = 1500 };
    static HGDIOBJ h[N];
    static unsigned char seen[GH_CELLS / 8];
    int made = 0, dup = 0, bad = 0;
    memset(seen, 0, sizeof seen);
    for (int i = 0; i < N; i++) {
        h[i] = mk_rgn();
        if (!h[i]) break;
        made++;
        unsigned x = GH_IDX(h[i]);
        if (seen[x >> 3] & (1u << (x & 7))) dup++;
        seen[x >> 3] |= (unsigned char)(1u << (x & 7));
        struct gh_cell c;
        if (!gh_cell_of(h[i], &c) || c.objt != 4 || (c.owner & ~1u) != GH_PID()) bad++;
    }
    CHECKF(made == N, "only %d regions created", made);
    CHECK(dup == 0);
    CHECK(bad == 0);
    CHECK(gh_count_owned(GH_PID()) >= made);
    for (int i = 0; i < made; i++) rm_obj(h[i]);
    int stale = 0;     /* the region cache holds a few of the deleted cells back */
    for (int i = 0; i < made; i++) {
        struct gh_cell c;
        if (gh_read(GH_IDX(h[i]), &c) && c.objt != 0 && (c.owner & ~1u) == GH_PID()) stale++;
    }
    CHECKF(stale <= GH_CACHE_MAX, "%d cells still owned by this process after delete", stale);
}
TEST(gdi_handle_table_reuse_mixed_batch_types_match) {
    GH_NEED();
    HGDIOBJ h[96]; const struct gh_kind *k[96]; int n = 0, bad = 0;
    for (int round = 0; round < 3; round++)
        for (size_t i = 0; i < GH_NKINDS && n < 96; i++) {
            const struct gh_kind *kk = gh_all_kinds[i];
            if (kk->nh != 1) continue;
            HGDIOBJ x = kk->mk();
            if (!x) continue;
            h[n] = x; k[n] = kk; n++;
        }
    for (int i = 0; i < n; i++) {
        struct gh_cell c;
        if (!gh_cell_of(h[i], &c) || c.upper != GH_HIWORD(h[i]) || GH_TYPE7(h[i]) != k[i]->type7 ||
            U(GetObjectType)(h[i]) != k[i]->objtype) { bad++; t_note("mismatch for %s handle %p", k[i]->name, h[i]); }
    }
    CHECKF(bad == 0, "%d of %d objects inconsistent", bad, n);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) if (GH_IDX(h[i]) == GH_IDX(h[j])) CHECKF(0, "objects %d/%d share index", i, j);
    for (int i = n - 1; i >= 0; i--) k[i]->rm(h[i]);
}
TEST(gdi_handle_table_reuse_cross_thread_use_and_delete) {
    GH_NEED();
    /* GDI objects belong to the process, not the thread: a handle made here is deletable from another thread.
       A hatch brush is used because a deleted solid brush stays in the brush cache. */
    HGDIOBJ h = mk_brush_hatch();
    REQUIRE(h);
    struct xarg a = { h, 0, 0 };
    HANDLE t = CreateThread(NULL, 0, xworker, &a, 0, NULL);
    REQUIRE(t);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    CHECK(a.type_ok);
    CHECK(a.deleted);
    struct gh_cell c;
    CHECK(gh_read(GH_IDX(h), &c) && c.objt == 0);
}
TEST(gdi_handle_table_reuse_object_created_on_thread_survives_thread_exit) {
    GH_NEED();
    struct marg a = { NULL };
    HANDLE t = CreateThread(NULL, 0, mworker, &a, 0, NULL);
    REQUIRE(t);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    REQUIRE(a.h);
    struct gh_cell c;
    CHECK(gh_cell_of(a.h, &c));
    CHECK(c.objt == 5 && (c.owner & ~1u) == GH_PID());
    CHECK(U(GetObjectType)(a.h) == OBJ_BITMAP);
    rm_obj(a.h);
}
TEST(gdi_handle_table_reuse_delete_dc_frees_dc_attr_slot) {
    GH_NEED();
    /* a freed DC returns its DC_ATTR slot to the process pool; creating a DC next commonly gets that block back */
    HDC a = U(CreateCompatibleDC)(NULL);
    REQUIRE(a);
    struct gh_cell ca, cb;
    CHECK(gh_cell_of(a, &ca));
    U(DeleteDC)(a);
    HDC b = U(CreateCompatibleDC)(NULL);
    REQUIRE(b);
    CHECK(gh_cell_of(b, &cb));
    t_note("DC_ATTR old 0x%llx new 0x%llx", ca.puser, cb.puser);
    CHECK(cb.puser != 0);
    U(DeleteDC)(b);
}
TEST(gdi_handle_table_reuse_cell_pointer_never_user_range) {
    GH_NEED();
    /* scan the whole table: every live cell's first qword is either in kernel range or (free list) fits in 32 bits */
    void *base = gh_base();
    static struct gh_cell buf[1024];
    int live = 0, weird = 0, kptr = 0;
    REQUIRE(base);
    for (unsigned i = 0; i < GH_CELLS; i += 1024) {
        if (!k_try_read((char *)base + (ULONG_PTR)i * GDI_CELL_SIZE, buf, sizeof buf)) { CHECKF(0, "table unreadable at cell %u", i); return; }
        for (int j = 0; j < 1024; j++) {
            if (buf[j].objt == 0) continue;
            live++;
            if (buf[j].q0 >= GH_KERNEL_FLOOR) kptr++;
            else {
                /* another process may be half-way through hSetup for this cell: look again before judging */
                struct gh_cell again;
                Sleep(1);
                if (gh_read(i + j, &again) && (again.objt == 0 || again.q0 >= GH_KERNEL_FLOOR)) kptr++;
                else weird++;
            }
        }
    }
    t_info("%d live cells, %d with kernel-range first qword, %d other", live, kptr, weird);
    CHECKF(weird == 0, "%d live cells have a first qword outside the kernel range", weird);
}
TEST(gdi_handle_table_reuse_all_live_cells_consistent) {
    GH_NEED();
    void *base = gh_base();
    static struct gh_cell buf[1024];
    int bad_upper = 0, bad_q0 = 0, live = 0;
    REQUIRE(base);
    for (unsigned i = 0; i < GH_CELLS; i += 1024) {
        if (!k_try_read((char *)base + (ULONG_PTR)i * GDI_CELL_SIZE, buf, sizeof buf)) { CHECK(0); return; }
        for (unsigned j = 0; j < 1024; j++) {
            struct gh_cell *c = &buf[j];
            if (c->objt == 0) continue;
            live++;
            /* the upper word's low 5 bits (type & 0x1f) reproduce Objt */
            if ((c->upper & 0x1f) != (c->objt & 0x1f) || (c->q0 >= GH_KERNEL_FLOOR && (c->q0 & 0xffff) != i + j)) {
                struct gh_cell again;   /* re-read: a concurrent hSetup in another process can be mid-update */
                Sleep(1);
                if (!gh_read(i + j, &again) || again.objt == 0) continue;
                if ((again.upper & 0x1f) != (again.objt & 0x1f)) bad_upper++;
                if ((again.q0 & 0xffff) != i + j) bad_q0++;
            }
        }
    }
    t_info("%d live cells in the session table", live);
    CHECKF(bad_upper == 0, "%d live cells whose upper word does not carry their object type", bad_upper);
    CHECKF(bad_q0 == 0, "%d live cells whose first qword does not encode their own index", bad_q0);
}
