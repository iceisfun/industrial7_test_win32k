/* x_dxgk_tracked_workload_vail_neg / generic: the same shape matrix applied to each of the eighteen thunks.
 *
 * Measures, per thunk, on an ordinary interactive process that is no Vail, display-manager or compositor client:
 *   - the win32u export is a well-formed syscall stub with the documented service number;
 *   - the status for an all-zero argument vector, a single readable block, readable blocks in every argument
 *     slot, unreadable pointers (address 1, kernel address, all ones, a released region, the first inaccessible
 *     user page, small garbage values) in the first slot, in later slots and in all slots, and for blocks filled
 *     with 0xFF, 0x41 or 0x01 bytes;
 *   - the status for a readable first block of every size from 1 byte to 0x120 bytes (the thunks that read a
 *     structure answer the same until the block is large enough);
 *   - the answer is repeatable, the argument blocks are never written, GetLastError is untouched, a thread
 *     that never made a win32k call before answers the same, the gdi32 export answers the same as win32u, and
 *     a deterministic sweep of random argument vectors never produces a success.
 * Only symbolic status names are printed; no address, handle or size from the machine is printed.
 */
#include "x.h"

#define NWILD X_NWILD

/* The status a readable first-argument block of n bytes (ending at the end of the committed page) gets. */
static LONG size_model(int sc, unsigned n) {
    LONG w; const char *r;
    switch (sc) {
    case X_DISPMGR:   return n < 32 ? XS_INVALID_PARAMETER : XS_INVALID_HANDLE;
    case X_REGVAIL:   return n < 16 ? XS_INVALID_PARAMETER : XS_GFX_C01E0011;
    case X_PROMOTE:   return XS_INVALID_PARAMETER;      /* the second argument is NULL */
    default: x_model(sc, XS_BLOCK0, &w, &r); return w;
    }
}

static void t_stub(int sc) {
    XRESOLVE(f, sc, 0);
    CHECKF(k_stub_valid((FARPROC)f), "%s: not a syscall stub", xsc_tab[sc].nt);
    EXPECT_EQ(k_stub_number((FARPROC)f), xsc_tab[sc].num);
}
static void t_null_single(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    XMODEL(sc, XS_ZERO, w0); XMODEL(sc, XS_BLOCK0, w1); XMODEL(sc, XS_BLOCKS_ALL, w2);
    x_args(XS_ZERO, 0, 0, a);   X_ST(x_call(sc, 0, a), w0, "%s(all zero)", xsc_tab[sc].nt);
    x_args(XS_BLOCK0, 0, 0, a); X_ST(x_call(sc, 0, a), w1, "%s(one readable block)", xsc_tab[sc].nt);
    x_args(XS_BLOCKS_ALL, 0, 0, a); X_ST(x_call(sc, 0, a), w2, "%s(a block in every argument)", xsc_tab[sc].nt);
}
static void t_block_each_pos(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    XMODEL(sc, XS_BLOCKN, w);
    for (int pos = 1; pos < 8; pos++) { x_args(XS_BLOCKN, pos, 0, a); X_ST(x_call(sc, 0, a), w, "%s(block in argument %d only)", xsc_tab[sc].nt, pos); }
}
static void t_wild_pointers(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    XMODEL(sc, XS_WILD0, w0); XMODEL(sc, XS_WILD_ALL, w1);
    for (int v = 0; v < NWILD; v++) {
        if (x_wild_is_readable(v)) { t_info("kind %d is readable memory here, not tested", v); continue; }
        x_args(XS_WILD0, 0, v, a);    X_ST(x_call(sc, 0, a), w0, "%s(unreadable first argument, kind %d)", xsc_tab[sc].nt, v);
        x_args(XS_WILD_ALL, 0, v, a); X_ST(x_call(sc, 0, a), w1, "%s(unreadable value in every argument, kind %d)", xsc_tab[sc].nt, v);
    }
}
static void t_wild_later(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    XMODEL(sc, XS_WILDN, w);
    for (int pos = 1; pos < 8; pos++) for (int v = 0; v < NWILD; v += 3) {
        if (x_wild_is_readable(v)) { t_info("kind %d is readable memory here, not tested (argument %d)", v, pos); continue; }
        x_args(XS_WILDN, pos, v, a);
        X_ST(x_call(sc, 0, a), w, "%s(unreadable argument %d, kind %d)", xsc_tab[sc].nt, pos, v);
    }
}
static void t_fills(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    static const enum xshape sh[] = { XS_FILL_FF, XS_FILL_41, XS_FILL_01 };
    static const char *const nm[] = { "0xFF", "0x41", "0x01" };
    for (int i = 0; i < 3; i++) {
        XMODEL(sc, sh[i], w);
        x_args(sh[i], 0, 0, a); X_ST(x_call(sc, 0, a), w, "%s(blocks filled with %s)", xsc_tab[sc].nt, nm[i]);
    }
}
static void t_sizes(int sc) {
    XRESOLVE(f, sc, 0);
    {   LONG w; const char *r; if (!x_model(sc, XS_BLOCK0, &w, &r)) { t_skip("%s", r); return; } }
    for (unsigned n = 1; n <= 0x120; n++) {
        ULONG_PTR a[8] = { 0 };
        a[0] = (ULONG_PTR)x_edge(n < 0x1000 ? n : 0x1000);
        X_ST(x_call(sc, 0, a), size_model(sc, n), "%s(first block of %u readable bytes)", xsc_tab[sc].nt, n);
    }
}
static void t_repeat(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8];
    XMODEL(sc, XS_ZERO, w0); XMODEL(sc, XS_BLOCKS_ALL, w1);
    for (int i = 0; i < 16; i++) {
        x_args(XS_ZERO, 0, 0, a);       X_ST(x_call(sc, 0, a), w0, "%s(all zero) call %d", xsc_tab[sc].nt, i);
        x_args(XS_BLOCKS_ALL, 0, 0, a); X_ST(x_call(sc, 0, a), w1, "%s(blocks) call %d", xsc_tab[sc].nt, i);
    }
}
static void t_not_written(int sc) {
    XRESOLVE(f, sc, 0); ULONG_PTR a[8]; unsigned char before[8][0x100];
    XMODEL(sc, XS_BLOCKS_ALL, w);
    x_args(XS_BLOCKS_ALL, 0, 0, a);
    for (int k = 0; k < 8; k++) { unsigned char *p = (unsigned char *)a[k]; for (int i = 0; i < 0x100; i++) p[i] = (unsigned char)(0x5A ^ i ^ k); memcpy(before[k], p, 0x100); }
    LONG st = x_call(sc, 0, a);
    for (int k = 0; k < 8; k++) {
        /* the promotion thunk clears the first eight bytes of its second argument (an output slot) even when it fails */
        size_t from = (sc == X_PROMOTE && k == 1) ? 8 : 0;
        CHECKF(!memcmp(before[k] + from, (unsigned char *)a[k] + from, 0x100 - from), "%s wrote into the block of argument %d (status %s)", xsc_tab[sc].nt, k, xs_name(st));
    }
    (void)w;
}
struct thr { int sc; LONG zero, blocks, last; };
static DWORD WINAPI thr_proc(LPVOID p) {
    struct thr *t = p; ULONG_PTR a[8];
    SetLastError(0x1234);
    x_args(XS_ZERO, 0, 0, a);       t->zero = x_call(t->sc, 0, a);
    x_args(XS_BLOCKS_ALL, 0, 0, a); t->blocks = x_call(t->sc, 0, a);
    t->last = GetLastError();
    return 0;
}
static void t_fresh_thread(int sc) {
    XRESOLVE(f, sc, 0);
    XMODEL(sc, XS_ZERO, w0); XMODEL(sc, XS_BLOCKS_ALL, w1);
    struct thr t = { sc, 0, 0, 0 };
    HANDLE h = CreateThread(NULL, 0, thr_proc, &t, 0, NULL);
    if (!h) { CHECKF(0, "CreateThread failed"); return; }
    CHECK(WaitForSingleObject(h, 20000) == WAIT_OBJECT_0);
    CloseHandle(h);
    X_ST(t.zero, w0, "%s(all zero) on a new thread", xsc_tab[sc].nt);
    X_ST(t.blocks, w1, "%s(blocks) on a new thread", xsc_tab[sc].nt);
    EXPECT_EQ(t.last, 0x1234);
    SetLastError(0x4321);
    ULONG_PTR a[8]; x_args(XS_BLOCKS_ALL, 0, 0, a); x_call(sc, 0, a);
    EXPECT_EQ(GetLastError(), 0x4321);
}
static void t_gdi_route(int sc) {
    XRESOLVE(f, sc, 0); XRESOLVE(g, sc, 1);
    ULONG_PTR a[8];
    if (k_stub_valid((FARPROC)g)) EXPECT_EQ(k_stub_number((FARPROC)g), xsc_tab[sc].num);
    static const enum xshape sh[] = { XS_ZERO, XS_BLOCK0, XS_BLOCKS_ALL, XS_WILD_ALL, XS_FILL_FF };
    for (unsigned i = 0; i < XARR(sh); i++) {
        XMODEL(sc, sh[i], w);
        x_args(sh[i], 0, 1, a);
        X_ST(x_call(sc, 1, a), w, "gdi32 %s, shape %u", xsc_tab[sc].gdi, i);
    }
}
static void t_prng(int sc) {
    XRESOLVE(f, sc, 0);
    {   LONG w; const char *r; if (!x_model(sc, XS_ZERO, &w, &r)) { t_skip("%s", r); return; } }
    unsigned long long s = 0x9E3779B97F4A7C15ull ^ (unsigned long long)sc;
    int ok = 0, bad = 0; LONG first_bad = 0;
    for (int i = 0; i < 400; i++) {
        ULONG_PTR a[8];
        for (int k = 0; k < 8; k++) {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            a[k] = (ULONG_PTR)(s ^ (s >> 29));
            if (k & 1) a[k] |= 0x8000000000000000ull;   /* never a readable user pointer */
            if (i % 5 == 0) a[k] &= 0xFFFFFFFF;
        }
        LONG st = x_call(sc, 0, a), want;
        if (sc == X_DISPMGR || sc == X_REGVAIL || sc == X_PROMOTE) {
            /* random values are never readable pointers: the first slot decides */
            if (sc == X_PROMOTE) want = (a[0] && a[1]) ? XS_ACCESS_VIOLATION : XS_INVALID_PARAMETER;
            else want = XS_INVALID_PARAMETER;
        } else { const char *r; x_model(sc, XS_ZERO, &want, &r); }
        if (st == want) ok++; else { bad++; if (!first_bad) first_bad = st; }
        CHECKF(st < 0, "%s returned success for random arguments", xsc_tab[sc].nt);
    }
    CHECKF(bad == 0, "%s: %d of 400 random vectors did not get the modelled status (first %s)", xsc_tab[sc].nt, bad, xs_name(first_bad));
}

#define GEN(tag, SC) \
TEST(x_##tag##_stub_is_well_formed)        { t_stub(SC); } \
TEST(x_##tag##_zero_single_and_all_blocks)      { t_null_single(SC); } \
TEST(x_##tag##_block_in_each_slot)         { t_block_each_pos(SC); } \
TEST(x_##tag##_unreadable_pointers)        { t_wild_pointers(SC); } \
TEST(x_##tag##_unreadable_later_slots)     { t_wild_later(SC); } \
TEST(x_##tag##_filled_blocks)              { t_fills(SC); } \
TEST(x_##tag##_first_block_sizes)          { t_sizes(SC); } \
TEST(x_##tag##_repeat_is_stable)           { t_repeat(SC); } \
TEST(x_##tag##_blocks_are_not_written)     { t_not_written(SC); } \
TEST(x_##tag##_fresh_thread_and_lasterror) { t_fresh_thread(SC); } \
TEST(x_##tag##_gdi32_route_matches)        { t_gdi_route(SC); } \
TEST(x_##tag##_random_vectors_never_succeed) { t_prng(SC); }

GEN(create,   X_CREATE)
GEN(update,   X_UPDATE)
GEN(destroy,  X_DESTROY)
GEN(reset,    X_RESET)
GEN(getidx,   X_GETIDX)
GEN(getstat,  X_GETSTAT)
GEN(dispmgr,  X_DISPMGR)
GEN(dport,    X_DPORT)
GEN(dup,      X_DUP)
GEN(odupl,    X_ODUPL)
GEN(blt,      X_BLT)
GEN(present,  X_PRESENT)
GEN(pin,      X_PIN)
GEN(unpin,    X_UNPIN)
GEN(regvail,  X_REGVAIL)
GEN(vconn,    X_VCONN)
GEN(vdisc,    X_VDISC)
GEN(promote,  X_PROMOTE)
