/* Per-object-type lifecycle tests: create through the normal user32 path, then validate the
 * client-visible USER handle table entry and the per-process USER object count the kernel keeps. */
#include "uh.h"

typedef void (*uh_body)(const struct uh_desc *);
static void uh_run(const struct uh_desc *d, uh_body fn) {
    UH_NEED();
    if (d->setup) d->setup();
    fn(d);
    if (d->teardown) d->teardown();
}
#define MKH(d, h) HANDLE h = (d)->create(); if (!h) { t_skip("cannot create %s (err %lu)", (d)->name, GetLastError()); return; }

/* the handle allocator: *_Dst = (int)(uniq << 16 | index); entry.uniq is the same uniq. */
static void b_type_uniq(const struct uh_desc *d) {
    MKH(d, h);
    uh_entry_t e;
    CHECK(UH_IDX(h) < uh_n());
    CHECK(uh_read_h(h, &e));
    EXPECT_EQ(e.type, d->type);
    CHECK((e.flags & 1) == 0);
    EXPECT_EQ(e.uniq, UH_UNIQ(h));
    CHECK(e.uniq != 0 && e.uniq != 0xffff);
    d->destroy(h);
}
/* the handle allocator: the owner id is the thread id (flag bit0) / PsGetProcessId (flag bit1). */
static void b_owner(const struct uh_desc *d) {
    MKH(d, h);
    uh_entry_t e;
    CHECK(uh_read_h(h, &e));
    switch (d->owner) {
    case UH_OWN_THREAD:  EXPECT_EQ(e.owner, GetCurrentThreadId()); break;
    case UH_OWN_PROCESS: EXPECT_EQ(e.owner, GetCurrentProcessId()); break;
    default:             EXPECT_EQ(e.owner, 0); break;
    }
    d->destroy(h);
}
/* the handle allocator: desktop-heap types store the shared part's heap offset and the desktop base
 * word (**(pdesk+8)); pool types store 0 in both. */
static void b_heap(const struct uh_desc *d) {
    MKH(d, h);
    uh_entry_t e;
    CHECK(uh_read_h(h, &e));
    if (d->desk) {
        CHECK(e.delta != 0);
        CHECK(e.f10 != 0);
        void *guard = NULL, *gv = NULL;
        if (k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + CI_pDeskHeapBaseGuard, &guard) && guard && k_try_read_ptr(guard, &gv))
            EXPECT_EQ(e.f10, gv); /* the user32 fast path in IsWindow/HMValidateHandle relies on this */
    } else {
        EXPECT_EQ(e.delta, 0);
        EXPECT_EQ(e.f10, 0);
    }
    d->destroy(h);
}
/* The returned handle is the sign-extended int stored in the head; windows always come from the
 * even-index free list (the handle allocator: type 1 always takes the lowest free slot, indices are pushed to
 * the list matching their parity in the handle free / table grow paths). */
static void b_format(const struct uh_desc *d) {
    MKH(d, h);
    long long v = (long long)(ULONG_PTR)h;
    EXPECT_EQ(v, (long long)(int)(unsigned)(ULONG_PTR)h);
    CHECK(UH_UNIQ(h) != 0);
    if (d->type == UT_WINDOW) CHECKF((UH_IDX(h) & 1) == 0, "window index %u is odd", UH_IDX(h));
    d->destroy(h);
}
/* The first qword of the client part is the handle, the second is the object's own heap offset
 *. */
static void b_client_head(const struct uh_desc *d) {
    MKH(d, h);
    uh_entry_t e;
    CHECK(uh_read_h(h, &e));
    void *p = uh_client_ptr(h);
    if (!p) { t_skip("client desktop heap view not resolvable via fast path"); d->destroy(h); return; }
    unsigned long long q[2] = {0, 0};
    CHECK(k_try_read(p, q, sizeof q));
    EXPECT_EQ((unsigned)q[0], (unsigned)(ULONG_PTR)h);
    EXPECT_EQ(q[1], e.delta);
    d->destroy(h);
}
static void b_gui_count(const struct uh_desc *d) {
    DWORD before = uh_gr(GR_USEROBJECTS);
    MKH(d, h);
    DWORD after = uh_gr(GR_USEROBJECTS);
    if (d->exact) EXPECT_EQ(after, before + 1); else CHECK(after >= before + 1);
    DWORD peak = uh_gr(4 /* GR_USEROBJECTS_PEAK */);
    CHECK(peak >= after);
    d->destroy(h);
    DWORD fin = uh_gr(GR_USEROBJECTS);
    if (d->exact) EXPECT_EQ(fin, before); else if (fin != before) t_note("%s: count %lu -> %lu -> %lu (implicit helper objects)", d->name, before, after, fin);
}
/* The per-process counter (PROCESSINFO+0x44) is bumped by the handle allocator and dropped by the handle free path,
 * so it tracks the table entries owned by this process/threads. */
static void b_scan(const struct uh_desc *d) {
    unsigned n0 = uh_scan_owned(d->type);
    MKH(d, h);
    unsigned n1 = uh_scan_owned(d->type);
    if (d->exact) EXPECT_EQ(n1, n0 + 1); else CHECK(n1 >= n0 + 1);
    d->destroy(h);
    unsigned n2 = uh_scan_owned(d->type);
    if (d->exact) EXPECT_EQ(n2, n0); else CHECK(n2 <= n1 - 1);
}
/* the handle free path zeroes delta/owner/f10/type/flags and stores the successor uniq. */
static void b_destroy_frees(const struct uh_desc *d) {
    MKH(d, h);
    uh_entry_t e0, e1;
    CHECK(uh_read_h(h, &e0));
    BOOL r = d->destroy(h);
    if (d->destroy_ok) CHECK(r);
    CHECK(uh_read_h(h, &e1));
    if (e1.type == 0) {
        EXPECT_EQ(e1.delta, 0); EXPECT_EQ(e1.owner, 0); EXPECT_EQ(e1.f10, 0); EXPECT_EQ(e1.flags, 0);
        EXPECT_EQ(e1.uniq, uh_next_uniq(e0.uniq));
    } else if (e1.uniq == e0.uniq) {
        /* object still locked: marking it for destruction sets the destroyed flag */
        CHECK(e1.flags & 1);
        t_note("%s entry kept after destroy (flags 0x%02x)", d->name, e1.flags);
    }
    if (d->valid) CHECK(!d->valid(h));
}
static void b_double_destroy(const struct uh_desc *d) {
    MKH(d, h);
    d->destroy(h);
    BOOL r; UH_CALL_ERR(r, d->destroy(h));
    CHECK(!r);
    if (d->err) EXPECT_ERR(d->err);
}
/* Uniq successor: re-creating after a free hands out the same index (LIFO free list) with uniq+1,
 * unless another process grabbed the slot first. */
static void b_reuse(const struct uh_desc *d) {
    HANDLE prev = NULL; unsigned reuse = 0;
    uh_entry_t pe;
    for (int i = 0; i < 8; i++) {
        MKH(d, h);
        uh_entry_t e;
        CHECK(uh_read_h(h, &e));
        if (prev) {
            CHECK(h != prev);
            if (UH_IDX(h) == UH_IDX(prev)) {
                reuse++;
                EXPECT_EQ(e.uniq, uh_next_uniq(pe.uniq));
            }
        }
        pe = e; prev = h;
        d->destroy(h);
    }
    if (!reuse) t_note("%s: index never reused within 8 create/destroy cycles", d->name);
}
static unsigned pick_other_uniq(unsigned u) { unsigned n = (u + 1) & 0xffff; if (n == 0 || n == 0xffff) n = (u > 2) ? u - 1 : 2; return n; }
#define NOFORGE(d) if (!(d)->forge_ok) { t_skip("forged-handle error mapping not verified for %s", (d)->name); return; }
static void forged_common(const struct uh_desc *d, int mode) {
    MKH(d, h);
    HANDLE hf = mode == 2 ? (HANDLE)(ULONG_PTR)(0xffffu | (1u << 16)) : uh_forge(h, mode == 1 ? 0 : pick_other_uniq(UH_UNIQ(h)));
    uh_entry_t e0, e1; CHECK(uh_read_h(h, &e0));
    if (d->valid) CHECK(!d->valid(hf));
    BOOL r; UH_CALL_ERR(r, d->destroy(hf));
    CHECK(!r);
    if (d->err) EXPECT_ERR(d->err);
    CHECK(uh_read_h(h, &e1));
    EXPECT_EQ(e1.type, d->type); EXPECT_EQ(e1.uniq, e0.uniq); CHECK((e1.flags & 1) == 0);
    if (d->valid) CHECK(d->valid(h));
    BOOL ok = d->destroy(h);
    if (d->destroy_ok) CHECK(ok);
}
static void b_forged_uniq(const struct uh_desc *d) { NOFORGE(d); forged_common(d, 0); }
static void b_zero_uniq(const struct uh_desc *d) { NOFORGE(d); forged_common(d, 1); } /* x64 native: HIWORD 0 only matches in WOW64 (HMValidateHandle) */
static void b_oob_index(const struct uh_desc *d) { NOFORGE(d); forged_common(d, 2); } /* index 0xFFFF >= cHandleEntries (max 0xFFFE) */
static void b_free_entry(const struct uh_desc *d) {
    NOFORGE(d);
    unsigned n; uh_entry_t *t = uh_snap(&n);
    if (!t) { t_skip("table snapshot failed"); return; }
    unsigned idx = 0;
    for (unsigned i = n; i-- > 2;) if (t[i].type == 0 && (d->type != UT_WINDOW || !(i & 1))) { idx = i; break; }
    unsigned short uq = idx ? t[idx].uniq : 0;
    free(t);
    if (!idx) { t_skip("no free entry"); return; }
    HANDLE hf = (HANDLE)(ULONG_PTR)(idx | ((unsigned)uq << 16));
    if (d->valid) CHECK(!d->valid(hf));
    BOOL r; UH_CALL_ERR(r, d->destroy(hf)); /* HMValidateHandle: entry type != expected type */
    CHECK(!r);
    if (d->err) EXPECT_ERR(d->err);
    uh_entry_t e;
    CHECK(uh_read(idx, &e));
    if (e.type == 0) { EXPECT_EQ(e.uniq, uq); EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.owner, 0); }
}
/* HMValidateHandle / user32 HMValidateHandle accept HIWORD 0xFFFF as "any uniqueness". */
static void b_wildcard(const struct uh_desc *d) {
    NOFORGE(d);
    MKH(d, h);
    HANDLE hw = uh_forge(h, 0xffff);
    if (d->valid) CHECK(d->valid(hw));
    uh_entry_t e0, e1; CHECK(uh_read_h(h, &e0));
    BOOL r = d->destroy(hw);
    if (d->destroy_ok) CHECK(r);
    CHECK(uh_read_h(h, &e1));
    CHECK(e1.type == 0 || (e1.flags & 1));
    if (d->valid) CHECK(!d->valid(h));
}

#define PT(tag, d) \
    TEST(user_handle_table_##tag##_type_and_uniq)      { uh_run(&d, b_type_uniq); } \
    TEST(user_handle_table_##tag##_owner_id)           { uh_run(&d, b_owner); } \
    TEST(user_handle_table_##tag##_heap_fields)        { uh_run(&d, b_heap); } \
    TEST(user_handle_table_##tag##_handle_format)      { uh_run(&d, b_format); } \
    TEST(user_handle_table_##tag##_gui_object_count)   { uh_run(&d, b_gui_count); } \
    TEST(user_handle_table_##tag##_owned_entry_scan)   { uh_run(&d, b_scan); } \
    TEST(user_handle_table_##tag##_destroy_frees_entry){ uh_run(&d, b_destroy_frees); } \
    TEST(user_handle_table_##tag##_double_destroy)     { uh_run(&d, b_double_destroy); } \
    TEST(user_handle_table_##tag##_reuse_bumps_uniq)   { uh_run(&d, b_reuse); } \
    TEST(user_handle_table_##tag##_forged_uniq)        { uh_run(&d, b_forged_uniq); } \
    TEST(user_handle_table_##tag##_zero_uniq)          { uh_run(&d, b_zero_uniq); } \
    TEST(user_handle_table_##tag##_oob_index)          { uh_run(&d, b_oob_index); } \
    TEST(user_handle_table_##tag##_free_entry)         { uh_run(&d, b_free_entry); } \
    TEST(user_handle_table_##tag##_wildcard_uniq)      { uh_run(&d, b_wildcard); }
#define PT_DESK(tag, d) TEST(user_handle_table_##tag##_client_head) { uh_run(&d, b_client_head); }

PT(window, uh_d_wtop)   PT_DESK(window, uh_d_wtop)
PT(child, uh_d_wchild)  PT_DESK(child, uh_d_wchild)
PT(menu, uh_d_menu)     PT_DESK(menu, uh_d_menu)
PT(popup, uh_d_popup)   PT_DESK(popup, uh_d_popup)
PT(icon, uh_d_icon)
PT(hdwp, uh_d_hdwp)
PT(hook, uh_d_hook)
PT(accel, uh_d_accel)
PT(winevent, uh_d_winevent)
PT(imc, uh_d_imc)
