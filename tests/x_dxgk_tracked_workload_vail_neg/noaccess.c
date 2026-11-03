/* x_dxgk_tracked_workload_vail_neg / noaccess: the unreadable-pointer kinds, made deliberate.
 *
 * The matrix in generic.c passes several literal addresses as "unreadable" (address 1, a kernel address, all ones,
 * a released region, the probe boundary, 0xDEADBEEF, 0xFFFFFFFF and 0x10000). Most of them are unreadable only because
 * nothing happens to be mapped there. This file adds
 *   - a companion fixture: a committed PAGE_NOACCESS page that this process owns and holds for the whole call, checked
 *     with VirtualQuery right before the call. An allocation or query that does not show a committed no-access page
 *     is a fixture failure, never a pass;
 *   - a test that records the mapping state of every literal address and skips the literal-address cases for a literal
 *     that turns out to be readable memory (the literal kinds are then not unreadable and say nothing).
 * A guard page (PAGE_GUARD) is a different state and is not covered here.
 * The thunks answer the no-access page exactly as they answer any other unreadable pointer.
 * Determinism: the mapping state of a literal address is machine dependent and goes through t_info only.
 */
#include "x.h"

static void t_noaccess(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    XMODEL(sc, XS_WILD0, w0); XMODEL(sc, XS_WILD_ALL, w1); XMODEL(sc, XS_WILDN, wn);
    if (!x_noaccess_begin()) { CHECKF(0, "fixture: no committed PAGE_NOACCESS page"); return; }
    CHECK(x_noaccess_ptr() != NULL && !x_addr_usable((ULONG_PTR)x_noaccess_ptr()));
    x_args(XS_WILD0, 0, X_WILD_NOACCESS, a);    X_ST(x_call(sc, 0, a), w0, "%s(no-access page as first argument)", xsc_tab[sc].nt);
    x_args(XS_WILD_ALL, 0, X_WILD_NOACCESS, a); X_ST(x_call(sc, 0, a), w1, "%s(no-access page in every argument)", xsc_tab[sc].nt);
    for (int pos = 1; pos < 8; pos++) {
        x_args(XS_WILDN, pos, X_WILD_NOACCESS, a);
        X_ST(x_call(sc, 0, a), wn, "%s(no-access page as argument %d)", xsc_tab[sc].nt, pos);
    }
    /* the page is still the fixture it was, and was never made readable or written by the call */
    MEMORY_BASIC_INFORMATION m;
    CHECK(VirtualQuery(x_noaccess_ptr(), &m, sizeof m) == sizeof m && m.State == MEM_COMMIT && m.Protect == PAGE_NOACCESS);
    x_noaccess_end();
}

#define NA(tag, SC) TEST(x_##tag##_noaccess_page_pointer) { t_noaccess(SC); }
NA(create, X_CREATE) NA(update, X_UPDATE) NA(destroy, X_DESTROY) NA(reset, X_RESET) NA(getidx, X_GETIDX)
NA(getstat, X_GETSTAT) NA(dispmgr, X_DISPMGR) NA(dport, X_DPORT) NA(dup, X_DUP) NA(odupl, X_ODUPL)
NA(blt, X_BLT) NA(present, X_PRESENT) NA(pin, X_PIN) NA(unpin, X_UNPIN) NA(regvail, X_REGVAIL)
NA(vconn, X_VCONN) NA(vdisc, X_VDISC) NA(promote, X_PROMOTE)

TEST(x_noaccess_fixture_is_a_committed_noaccess_page) {
    MEMORY_BASIC_INFORMATION m;
    if (!x_noaccess_begin()) { CHECKF(0, "fixture: no committed PAGE_NOACCESS page"); return; }
    CHECK(VirtualQuery(x_noaccess_ptr(), &m, sizeof m) == sizeof m);
    EXPECT_EQ(m.State, MEM_COMMIT);
    EXPECT_EQ(m.Protect, PAGE_NOACCESS);
    CHECK(m.RegionSize >= 0x1000);
    CHECK(!x_addr_usable((ULONG_PTR)x_noaccess_ptr()));
    void *p = x_noaccess_ptr();
    x_noaccess_end();
    /* after release the same address is no longer committed: the fixture is not leaked */
    CHECK(x_noaccess_ptr() == NULL);
    CHECK(VirtualQuery(p, &m, sizeof m) == sizeof m && m.State == MEM_FREE);
}

/* The two argument-specific thunks that read their pointers, with the no-access page where they read. */
TEST(x_noaccess_regvail_first_argument_is_invalid_parameter) {
    XRESOLVE(f, X_REGVAIL, 0);
    if (!x_noaccess_begin()) { CHECKF(0, "fixture: no committed PAGE_NOACCESS page"); return; }
    ULONG_PTR a[8] = { (ULONG_PTR)x_noaccess_ptr() };
    X_ST(x_call(X_REGVAIL, 0, a), XS_INVALID_PARAMETER, "no-access page");
    a[0] = (ULONG_PTR)x_noaccess_ptr() + 0x1000 - 16;   /* a 16-byte block inside the page: still unreadable */
    X_ST(x_call(X_REGVAIL, 0, a), XS_INVALID_PARAMETER, "16 bytes at the end of the no-access page");
    x_noaccess_end();
}
TEST(x_noaccess_promote_second_argument_is_access_violation_status) {
    XRESOLVE(f, X_PROMOTE, 0);
    if (!x_noaccess_begin()) { CHECKF(0, "fixture: no committed PAGE_NOACCESS page"); return; }
    ULONG_PTR a[8] = { (ULONG_PTR)x_blk(0, 0x40, 0), (ULONG_PTR)x_noaccess_ptr() };
    X_ST(x_call(X_PROMOTE, 0, a), XS_ACCESS_VIOLATION, "no-access page as the output");
    x_noaccess_end();
}
TEST(x_noaccess_dispmgr_block_is_invalid_parameter) {
    XRESOLVE(f, X_DISPMGR, 0);
    if (!x_noaccess_begin()) { CHECKF(0, "fixture: no committed PAGE_NOACCESS page"); return; }
    ULONG_PTR a[8] = { (ULONG_PTR)x_noaccess_ptr() };
    X_ST(x_call(X_DISPMGR, 0, a), XS_INVALID_PARAMETER, "no-access page as the block");
    x_noaccess_end();
}

/* ---- the literal addresses: record how they are mapped ------------------------------------------------- */
TEST(x_literal_unreadable_addresses_record_their_mapping_state) {
    static const char *const nm[X_NWILD] = { "address 1", "kernel address", "all ones", "released region", "probe boundary",
                                             "0xDEADBEEF", "literal 0x10000", "0xFFFFFFFF" };
    int usable = 0;
    for (int k = 0; k < X_NWILD; k++) {
        ULONG_PTR v = x_wild_value(k);
        int u = x_addr_usable(v);
        t_info("%s: %s", nm[k], x_addr_state(v));
        if (u) { usable++; t_info("%s is readable memory here; the literal-address cases for it prove nothing", nm[k]); }
        /* a kernel-range or all-ones value is never user memory, whatever else is mapped */
        if (k == 1 || k == 2 || k == 4) CHECKF(!u, "%s is usable memory", nm[k]);
    }
    if (usable) { t_skip("%d literal unreadable address(es) are mapped readable on this machine", usable); return; }
}
