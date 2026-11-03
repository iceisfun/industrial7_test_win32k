/* A table of user32/kernel window calls applied to handles the entry check must reject: same index but
 * another uniqueness, uniq 0 (native), an index at the table edge, a free entry, an object of another
 * type and NULL. user32!HMValidateHandle / GetWindowLong & co. report ERROR_INVALID_WINDOW_HANDLE (0x578). */
#include "r2h.h"

typedef int (*op_fn)(HWND h);   /* 1 when the call failed the way an invalid handle must fail */
struct op { const char *name; op_fn run; };

static int op_getwindowlong(HWND h)  { return U(GetWindowLongW)(h, GWL_STYLE) == 0; }
static int op_getwindowlongptr(HWND h){ return U(GetWindowLongPtrW)(h, GWLP_WNDPROC) == 0; }
static int op_getwindowrect(HWND h)  { RECT r; return !U(GetWindowRect)(h, &r); }
static int op_getclientrect(HWND h)  { RECT r; return !U(GetClientRect)(h, &r); }
static int op_isvisible(HWND h)      { return !U(IsWindowVisible)(h); }
static int op_getparent(HWND h)      { return U(GetParent)(h) == NULL; }
static int op_getwindow(HWND h)      { return U(GetWindow)(h, GW_HWNDNEXT) == NULL; }
static int op_isiconic(HWND h)       { return !U(IsIconic)(h); }
static int op_iszoomed(HWND h)       { return !U(IsZoomed)(h); }
static int op_getclasslong(HWND h)   { return U(GetClassLongW)(h, GCL_STYLE) == 0; }
static int op_gwtpid(HWND h)         { DWORD p = 99; return U(GetWindowThreadProcessId)(h, &p) == 0; }
static int op_showwindow(HWND h)     { U(ShowWindow)(h, SW_HIDE); return 1; }
static int op_enablewindow(HWND h)   { return !U(EnableWindow)(h, TRUE); }
static int op_setwindowpos(HWND h)   { return !U(SetWindowPos)(h, NULL, 0, 0, 1, 1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
static int op_destroywindow(HWND h)  { return !U(DestroyWindow)(h); }
static const struct op ops[] = {
    { "GetWindowLongW", op_getwindowlong }, { "GetWindowLongPtrW", op_getwindowlongptr },
    { "GetWindowRect", op_getwindowrect }, { "GetClientRect", op_getclientrect },
    { "IsWindowVisible", op_isvisible }, { "GetParent", op_getparent }, { "GetWindow", op_getwindow },
    { "IsIconic", op_isiconic }, { "IsZoomed", op_iszoomed }, { "GetClassLongW", op_getclasslong },
    { "GetWindowThreadProcessId", op_gwtpid }, { "ShowWindow", op_showwindow },
    { "EnableWindow", op_enablewindow }, { "SetWindowPos", op_setwindowpos }, { "DestroyWindow", op_destroywindow },
};
#define NOPS (sizeof ops / sizeof ops[0])

static void run_variant(HWND bad, HWND live, const char *vname) {
    r2h_entry_t e0, e1;
    CHECK(r2h_read_h(live, &e0));
    for (unsigned i = 0; i < NOPS; i++) {
        SetLastError(R2H_SENTINEL);
        int failed = ops[i].run(bad);
        DWORD e = GetLastError();
        CHECKF(failed, "%s(%s) did not fail", ops[i].name, vname);
        CHECKF(e == 0x578, "%s(%s) last error %lu want 1400", ops[i].name, vname, e);
    }
    CHECK(r2h_read_h(live, &e1));
    EXPECT_EQ(e1.type, e0.type); EXPECT_EQ(e1.uniq, e0.uniq); EXPECT_EQ(e1.flags, e0.flags);
    CHECK(U(IsWindow)(live));
}
static unsigned other_uniq(unsigned u) { unsigned n = (u + 1) & 0xffff; if (n == 0 || n == 0xffff) n = u > 2 ? u - 1 : 2; return n; }

R2(stale_matrix_forged_uniq) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    run_variant((HWND)r2h_forge(w, other_uniq(R2H_UNIQ(w))), w, "other uniq");
    U(DestroyWindow)(w);
}
R2(stale_matrix_zero_uniq) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    run_variant((HWND)r2h_forge(w, 0), w, "uniq 0");
    U(DestroyWindow)(w);
}
R2(stale_matrix_index_at_table_edge) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    run_variant((HWND)(ULONG_PTR)(0xffffu | (1u << 16)), w, "index 0xFFFF");
    U(DestroyWindow)(w);
}
R2(stale_matrix_free_entry) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    unsigned idx, uq;
    if (!r2h_free_slot(&idx, &uq, 1)) { U(DestroyWindow)(w); t_skip("no free entry"); return; }
    run_variant((HWND)(ULONG_PTR)(idx | (uq << 16)), w, "free entry");
    U(DestroyWindow)(w);
}
R2(stale_matrix_menu_handle) {
    R2H_NEED();
    HWND w = r2h_make_top(); HMENU m = U(CreateMenu)(); REQUIRE(w); REQUIRE(m);
    run_variant((HWND)m, w, "menu handle");
    U(DestroyMenu)(m); U(DestroyWindow)(w);
}
R2(stale_matrix_cursor_handle) {
    R2H_NEED();
    HWND w = r2h_make_top(); HANDLE c = r2h_d_icon.create(); REQUIRE(w); REQUIRE(c);
    run_variant((HWND)c, w, "cursor handle");
    r2h_d_icon.destroy(c); U(DestroyWindow)(w);
}
R2(stale_matrix_null_handle) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    run_variant(NULL, w, "NULL");
    U(DestroyWindow)(w);
}
/* The 0xFFFF wildcard resolves to the live entry in the HMValidateHandle based calls: results equal those of the real handle. */
R2(wildcard_uniq_results_equal_real_handle) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    HWND wc = (HWND)r2h_forge(w, 0xffff);
    CHECK(U(GetWindowLongW)(wc, GWL_STYLE) == U(GetWindowLongW)(w, GWL_STYLE));
    CHECK(U(GetWindowLongPtrW)(wc, GWLP_WNDPROC) == U(GetWindowLongPtrW)(w, GWLP_WNDPROC));
    RECT a, b; memset(&a, 1, sizeof a); memset(&b, 2, sizeof b);
    CHECK(U(GetWindowRect)(wc, &a) && U(GetWindowRect)(w, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK(U(GetClassLongW)(wc, GCL_STYLE) == U(GetClassLongW)(w, GCL_STYLE));
    CHECK(U(GetParent)(wc) == U(GetParent)(w));
    CHECK(U(IsWindowVisible)(wc) == U(IsWindowVisible)(w));
    CHECK(U(IsIconic)(wc) == U(IsIconic)(w));
    /* user32!GetWindowThreadProcessId reads the entry itself and compares the uniq exactly (no 0xFFFF
     * wildcard there): the wildcard form fails with 0x578 and leaves the pid untouched. */
    DWORD p1 = 1, p2 = 2; DWORD tid;
    SetLastError(R2H_SENTINEL);
    tid = U(GetWindowThreadProcessId)(wc, &p1);
    EXPECT_EQ(tid, 0); EXPECT_ERR(0x578); EXPECT_EQ(p1, 1);
    EXPECT_EQ(U(GetWindowThreadProcessId)(w, &p2), GetCurrentThreadId());
    EXPECT_EQ(p2, GetCurrentProcessId());
    U(DestroyWindow)(w);
}
/* A slot recycled for a new window: the old handle (same index, older uniq) fails, the new one works. */
R2(recycled_slot_old_handle_rejected_new_handle_valid) {
    R2H_NEED();
    HWND w1 = r2h_make_top(); REQUIRE(w1);
    unsigned idx = R2H_IDX(w1);
    U(DestroyWindow)(w1);
    HWND keep[8]; int n = 0; HWND w2 = NULL;
    for (; n < 8; n++) { keep[n] = r2h_make_top(); if (!keep[n]) break; if (R2H_IDX(keep[n]) == idx) { w2 = keep[n]; n++; break; } }
    if (w2) {
        EXPECT_EQ(R2H_UNIQ(w2), r2h_next_uniq((unsigned short)R2H_UNIQ(w1)));
        CHECK(w2 != w1);
        CHECK(!U(IsWindow)(w1));
        CHECK(U(IsWindow)(w2));
        SetLastError(R2H_SENTINEL);
        EXPECT_EQ(U(GetWindowLongW)(w1, GWL_STYLE), 0);
        EXPECT_ERR(0x578);
        CHECK(U(GetWindowLongW)(w2, GWL_STYLE) != 0);   /* WS_POPUP */
        DWORD pid = 0;
        EXPECT_EQ(U(GetWindowThreadProcessId)(w2, &pid), GetCurrentThreadId());
    } else t_note("slot %u not recycled within 8 creations", idx);
    for (int i = 0; i < n; i++) if (keep[i]) U(DestroyWindow)(keep[i]);
}
R2(session_id_peb_matches_process_query) {
    DWORD s = 0xdead;
    REQUIRE(U(ProcessIdToSessionId));
    CHECK(U(ProcessIdToSessionId)(GetCurrentProcessId(), &s));
    unsigned long ps = 0xbeef;
    CHECK(k_try_read_u32((char *)k_peb() + PEB_SessionId, &ps));
    EXPECT_EQ(ps, s);
    EXPECT_EQ(k_session_id(), s);
}
