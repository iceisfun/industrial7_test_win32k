/* Per-object-type completeness of the GDI shared handle table cell (_ENTRY, 0x18 bytes):
 * lock bit / owner, transfer flag, reuse-byte lifetime, delete-or-cache outcome, per-process population
 * scan and attribute-slot uniqueness, generated for every kind in r2.h. */
#include "r2.h"

static int is_cached_kind(const struct gh_kind *k) { return k == &gk_brush_solid || k == &gk_rgn_rect; }
static int cache_type_of(const struct gh_kind *k) { return k == &gk_rgn_rect ? R2_T_REGION : R2_T_BRUSH; }

/* Bit 0 of the owner word is the lock bit and the pid half equals GetCurrentProcessId() for a user-mode owned object.
 * On Windows 10 a quiescent live cell has the lock bit clear for most kinds, but fonts, non-rectangular regions and the
 * metafile client objects keep it set for as long as they live (observed at creation, nothing else running), so the bit is
 * asserted per kind. Flags bit 0x20 is set only on a DC, once it is prepared for transfer. */
static int lock_bit_expected(const struct gh_kind *k) {
    static const char *const set[] = { "font_w", "font_indirect", "rgn_ellip", "rgn_round", "rgn_poly",
                                       "wmf", "wmf_dc", "emf", "cobj_meta", "cobj_emf", "cobj_metadc" };
    for (size_t i = 0; i < sizeof set / sizeof *set; i++) if (!strcmp(k->name, set[i])) return 1;
    return 0;
}
static void k_owner_flags(const struct gh_kind *k) {
    GH_NEED();
    GH_MAKE(h, k);
    struct gh_cell c;
    if (!gh_cell_of(h, &c)) { CHECK(0); k->rm(h); return; }
    CHECKF((c.owner & 1u) == (unsigned)lock_bit_expected(k), "%s: lock bit %u on an idle cell, expected %d, owner=0x%x", k->name, c.owner & 1u, lock_bit_expected(k), c.owner);
    CHECKF((c.owner & ~1u) == GH_PID(), "%s: owner=0x%x pid=%u", k->name, c.owner, GH_PID());
    CHECKF((c.flags & 0x20) == 0, "%s: xferable flag set on a fresh object, flags=0x%02x", k->name, c.flags);
    CHECKF(c.objt != 0, "%s: live cell has Objt 0", k->name);
    t_note("%s: cell flags=0x%02x objt=0x%02x", k->name, c.flags, c.objt);
    k->rm(h);
}

/* create / delete / create: when the new object lands in the old slot its reuse byte is the old one + 1
 * (object insertion bumps it; caching the handle in the PEB bumps it and the cache hands that value back),
 * q0 carries index | reuse<<16 under the 0xffffffffff000000 tag, and the old handle value no longer validates. */
static void k_recreate(const struct gh_kind *k) {
    GH_NEED();
    int same = 0;
    HGDIOBJ prev = k->mk();
    if (!prev) { t_skip("cannot create %s", k->name); return; }
    for (int i = 0; i < 6; i++) {
        unsigned pi = GH_IDX(prev), pu = GH_UNIQ(prev);
        k->rm(prev);
        HGDIOBJ cur = k->mk();
        if (!cur) { t_skip("cannot recreate %s", k->name); return; }
        if (GH_IDX(cur) == pi) {
            struct gh_cell c;
            same++;
            /* The reuse byte of a slot advances once per allocation in it. Any other allocator on the machine (and the
             * halftone palette's own internal second allocation) may take and free the slot between our delete and create,
             * which adds further steps; so the advance is a small positive number, one when nothing interposed. */
            unsigned adv = (GH_UNIQ(cur) - pu) & 0xff;
            CHECKF(adv >= 1 && adv <= 4, "%s: slot reuse byte advanced by %u", k->name, adv);
            CHECKF(r2_lo32(cur) != r2_lo32(prev), "%s: handle value unchanged across delete+create", k->name);
            if (gh_cell_of(cur, &c))
                CHECKF(c.q0 == (0xffffffffff000000ull | ((unsigned long long)GH_UNIQ(cur) << 16) | pi), "%s: q0=0x%llx", k->name, c.q0);
            /* GetObjectType (gdi32full) rejects a handle whose HIWORD differs from the cell's */
            CHECKF(U(GetObjectType)(prev) == 0, "%s: GetObjectType(old handle %p)=%lu", k->name, prev, U(GetObjectType)(prev));
        }
        prev = cur;
    }
    k->rm(prev);
    t_info("%s: same slot reused %d/6", k->name, same);
}

/* After the delete the cell is either released (Objt 0, pUser 0, owner 0 - HANDLELOCK::vUnlockAndRelease) or, for
 * solid brushes and rectangular regions that carry an object attribute, parked in the PEB handle cache (a cache of freed handles):
 * cell still live and owned, reuse byte already bumped, AttrFlags bit 0 set, handle listed in PEB+0x158. */
static void k_delete_state(const struct gh_kind *k) {
    GH_NEED();
    HGDIOBJ held[40];
    int nheld = 0;
    if (is_cached_kind(k)) nheld = r2_drain(cache_type_of(k), held, 40);
    GH_MAKE(h, k);
    struct gh_cell before, after;
    if (!gh_cell_of(h, &before)) { CHECK(0); k->rm(h); r2_release(held, nheld); return; }
    unsigned idx = GH_IDX(h), u = GH_UNIQ(h);
    k->rm(h);
    U(GdiFlush)();      /* DeleteObject of a solid brush or rectangular region is batched: the cell is parked or freed at the flush */
    if (!gh_read(idx, &after)) { CHECK(0); r2_release(held, nheld); return; }
    struct r2_cache cc;
    int have = r2_cache_read(&cc);
    int slot = have ? r2_cache_find(&cc, idx) : -1;
    if (after.objt == 0) {
        CHECKF(!is_cached_kind(k), "%s: expected the handle to be parked in the PEB cache but the cell was released", k->name);
        CHECKF(slot < 0, "%s: released cell 0x%x is also listed in the PEB cache", k->name, idx);
        CHECKF(after.puser == 0 && (after.owner & ~1u) == 0, "%s: released cell keeps pUser 0x%llx owner 0x%x", k->name, after.puser, after.owner);
    } else {
        unsigned af = 0;
        CHECKF(slot >= 0, "%s: live cell 0x%x after delete but not in the PEB cache (objt 0x%x)", k->name, idx, after.objt);
        CHECK((after.owner & ~1u) == GH_PID());
        CHECK(after.objt == before.objt);
        CHECKF(after.puser == before.puser, "%s: pUser moved 0x%llx -> 0x%llx", k->name, before.puser, after.puser);
        CHECKF((after.upper >> 8) == ((u + 1) & 0xff), "%s: reuse byte 0x%02x after caching, handle had 0x%02x", k->name, after.upper >> 8, u);
        CHECKF(r2_attr(&after, 0, &af) && (af & 1), "%s: AttrFlags=0x%x lacks the cached bit", k->name, af);
        if (slot >= 0)
            CHECKF((unsigned)cc.slot[slot] == (((unsigned)after.upper << 16) | idx), "%s: cache slot 0x%llx cell upper 0x%04x", k->name, cc.slot[slot], after.upper);
        CHECKF(U(GetObjectType)(h) == 0, "%s: GetObjectType(parked handle)=%lu", k->name, U(GetObjectType)(h));
        CHECK(cc.lock == 0);
    }
    r2_release(held, nheld);
}

/* The per-process population seen by scanning the whole table must equal GetGuiResources, and creating one
 * object adds at least one cell of the right Objt. */
static void k_scan(const struct gh_kind *k) {
    GH_NEED();
    REQUIRE(U(GetGuiResources));
    int c0[32], c1[32], c2[32];
    HGDIOBJ held[40];
    /* a parked solid brush or rectangular region would be taken over by the create below and add no cell: empty that cache first */
    int nheld = is_cached_kind(k) ? r2_drain(cache_type_of(k), held, 40) : 0;
    int t0 = r2_scan(GH_PID(), c0);
    CHECK(t0 >= 0);
    HGDIOBJ h = k->mk();
    if (!h) { r2_release(held, nheld); t_skip("cannot create %s", k->name); return; }
    int t1 = r2_scan(GH_PID(), c1);
    int g1 = r2_gr(0);
    CHECKF(t1 == g1, "%s: scan=%d GR_GDIOBJECTS=%d", k->name, t1, g1);
    CHECKF(t1 > t0, "%s: population %d -> %d", k->name, t0, t1);
    if (k->objt >= 0) CHECKF(c1[k->objt & 0x1f] > c0[k->objt & 0x1f], "%s: Objt 0x%x cells %d -> %d", k->name, k->objt, c0[k->objt & 0x1f], c1[k->objt & 0x1f]);
    k->rm(h);
    U(GdiFlush)();
    int t2 = r2_scan(GH_PID(), c2);
    int g2 = r2_gr(0);
    CHECKF(t2 == g2, "%s: after delete scan=%d GR_GDIOBJECTS=%d", k->name, t2, g2);
    CHECKF(t2 <= t1, "%s: delete grew the population %d -> %d", k->name, t1, t2);
    /* brushes, pens and regions may legitimately stay parked in the PEB cache; every other type is released at once */
    if (k->nh >= 0 && (k->type7 == 0x01 || k->type7 == 0x05 || k->type7 == 0x08 || k->type7 == 0x09 || k->type7 == 0x0a))
        CHECKF(t2 == t0, "%s: population %d before, %d after create+delete", k->name, t0, t2);
    r2_release(held, nheld);
}

/* Live objects of one kind never share an attribute slot: DC_ATTR slots are 0x160-aligned in their page,
 * object attribute slots (regions, brushes) are 0x18-aligned. */
static void k_puser(const struct gh_kind *k) {
    GH_NEED();
    enum { N = 8 };
    HGDIOBJ hs[N]; unsigned long long pu[N];
    int n = 0;
    for (; n < N; n++) { hs[n] = k->mk(); if (!hs[n]) break; }
    if (n < 2) { for (int i = 0; i < n; i++) k->rm(hs[i]); t_skip("cannot create several %s", k->name); return; }
    for (int i = 0; i < n; i++) {
        struct gh_cell c;
        pu[i] = 0;
        if (!gh_cell_of(hs[i], &c)) { CHECK(0); continue; }
        pu[i] = c.puser;
        if (!pu[i]) continue;
        if (k->type7 == 0x01)
            CHECKF((pu[i] & 0xfff) % 0x160 == 0 && (pu[i] & 0xfff) <= 0xdc0, "%s: DC_ATTR 0x%llx", k->name, pu[i]);
        else if (k->type7 == 0x04 || k->type7 == 0x10)
            CHECKF((pu[i] & 0xfff) % 0x18 == 0 && (pu[i] & 0xfff) <= 0xfd8, "%s: object attribute 0x%llx", k->name, pu[i]);
        CHECKF(pu[i] < GH_KERNEL_FLOOR, "%s: pUser 0x%llx in kernel range", k->name, pu[i]);
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (pu[i] && pu[j]) CHECKF(pu[i] != pu[j], "%s: objects %d and %d share pUser 0x%llx", k->name, i, j, pu[i]);
    for (int i = 0; i < n; i++) k->rm(hs[i]);
}

/* The handle's type bits, the cell's Objt, and the table index all describe the same object: the table
 * entry found through the index has the reuse byte of the handle and a type consistent with Objt. */
static void k_roundtrip(const struct gh_kind *k) {
    GH_NEED();
    GH_MAKE(h, k);
    struct gh_cell c;
    if (!gh_cell_of(h, &c)) { CHECK(0); k->rm(h); return; }
    /* compared on 32 bits: a handle handed back from the PEB cache is zero-extended, a freshly created one sign-extended */
    unsigned rebuilt = ((unsigned)c.upper << 16) | GH_IDX(h);
    CHECKF(rebuilt == r2_lo32(h), "%s: handle %p rebuilt from the cell as 0x%x", k->name, h, rebuilt);
    CHECKF((c.upper & 0x80) == 0, "%s: stock bit in the cell of a created object", k->name);
    CHECKF((c.upper & 0x1f) == (k->type7 & 0x1f), "%s: cell type byte 0x%02x", k->name, c.upper & 0xff);
    k->rm(h);
}

#define GEN(id) \
    TEST(r2_gdi_cell_per_type_##id##_owner_and_flags) { k_owner_flags(&gk_##id); } \
    TEST(r2_gdi_cell_per_type_##id##_recreate_cycle)  { k_recreate(&gk_##id); } \
    TEST(r2_gdi_cell_per_type_##id##_delete_state)    { k_delete_state(&gk_##id); } \
    TEST(r2_gdi_cell_per_type_##id##_population_scan) { k_scan(&gk_##id); } \
    TEST(r2_gdi_cell_per_type_##id##_attr_slots)      { k_puser(&gk_##id); } \
    TEST(r2_gdi_cell_per_type_##id##_handle_roundtrip) { k_roundtrip(&gk_##id); }
R2_KINDS(GEN)
