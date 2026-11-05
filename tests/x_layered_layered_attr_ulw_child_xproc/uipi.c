/* Integrity-level (UIPI) ownership of layered attribute calls between two processes of the same user. The test process starts
 * a helper whose token is a copy of its own lowered to a lower mandatory integrity level (low, or medium when the test process
 * runs above medium). Two directions are measured on every window shape (fresh layered, attributes set, plain, image driven):
 *  - the lower-integrity helper acts on a window of this process: SetLayeredWindowAttributes, GetLayeredWindowAttributes,
 *    UpdateLayeredWindow, SetWindowLong, SetWindowPos and DestroyWindow are all refused with access denied (5), even the
 *    read-only Get, and the window is left exactly as it was; reading the window's identity and styles stays allowed;
 *  - this process acts on a window of the lower-integrity helper: everything that works between equal-integrity processes
 *    still works (Set, Get, UpdateLayeredWindow, SetWindowLong), except DestroyWindow which is refused as for any foreign window.
 * Tests are skipped when the process cannot derive a lowered token or is not above the helper's level. A started helper is
 * always terminated and waited for at the end of each test. */
#include "xl.h"

#define UIPI_LOW SECURITY_MANDATORY_LOW_RID
#define UIPI_MED SECURITY_MANDATORY_MEDIUM_RID

/* ---- actor: performs one operation on a window handle given by the parent and prints "RES r=<> err=<> v=<hex>" ----------------- */
CHILD(uipi_actor) {
    unsigned long long h = 0; char op[32] = "";
    if (argc < 3 || sscanf(argv[1], "%llx", &h) != 1) { printf("FAIL args\n"); return 2; }
    snprintf(op, sizeof op, "%s", argv[2]);
    HWND w = (HWND)(ULONG_PTR)h; struct xl_attr a; struct xl_img im; BOOL r = 0; unsigned long v = 0;
    if (!UN(NtUserSetLayeredWindowAttributes) || !U(CreateWindowExA)) { printf("FAIL exports\n"); return 3; }
    xl_attr_clear(&a); SET_ERR(XL_ERR_SENT);
    if (!strcmp(op, "get")) { r = xl_getall(w, &a); v = a.flags; }
    else if (!strcmp(op, "set")) r = xl_set(w, 0x0a0b0c, 55, 3);
    else if (!strcmp(op, "setflags0")) r = xl_set(w, 0, 0, 0);
    else if (!strcmp(op, "ulw")) { if (!xl_img_make(&im, 8, 8)) { printf("FAIL image\n"); return 4; } SET_ERR(XL_ERR_SENT); r = xl_ulw(w, &im, ULW_ALPHA); }
    else if (!strcmp(op, "swl")) { LONG ex = xl_exstyle(w); SET_ERR(XL_ERR_SENT); v = (unsigned long)U(SetWindowLongA)(w, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT); r = v != 0; }
    else if (!strcmp(op, "swp")) r = U(SetWindowPos)(w, NULL, -20000, -20000, 30, 30, SWP_NOZORDER | SWP_NOACTIVATE);
    else if (!strcmp(op, "destroy")) r = U(DestroyWindow)(w);
    else if (!strcmp(op, "read")) {
        DWORD pid = 0; DWORD tid = U(GetWindowThreadProcessId)(w, &pid);
        r = U(IsWindow)(w) && tid != 0 && pid != 0 && pid != GetCurrentProcessId(); v = (unsigned long)xl_exstyle(w); SET_ERR(XL_ERR_SENT);
    }
    else { printf("FAIL op\n"); return 5; }
    { DWORD err = GetLastError(); printf("RES r=%d err=%lu v=%lx il=%lx\n", r ? 1 : 0, (unsigned long)err, v, k_integrity_level()); }
    return 0;
}
struct ures { int ok; int r; unsigned long err, v, il; };
/* Run the actor at integrity rid against window w; returns ok=0 when the helper could not run or reported nothing. */
static struct ures act(HWND w, const char *op, DWORD rid) {
    struct ures u; memset(&u, 0, sizeof u); struct xl_proc p; char args[96], line[300];
    snprintf(args, sizeof args, "%llx %s", (unsigned long long)(ULONG_PTR)w, op);
    if (!xl_spawn_il(&p, t_self_path(), "uipi_actor", args, rid)) return u;
    while (xl_readline_pump(&p, line, sizeof line, 30000)) {
        if (!strncmp(line, "RES ", 4)) { if (sscanf(line, "RES r=%d err=%lu v=%lx il=%lx", &u.r, &u.err, &u.v, &u.il) == 4) u.ok = 1; break; }
        printf("  ACTOR %s\n", line);
    }
    WaitForSingleObject(p.hproc, 10000); xl_kill_proc(&p);
    return u;
}
/* The level the actor runs at must be strictly below ours. Returns 0 (and skips) otherwise. */
static int below(DWORD rid) {
    unsigned long il = k_integrity_level();
    if (il <= rid) { t_skip("test process integrity 0x%lx is not above 0x%lx", il, (unsigned long)rid); return 0; }
    HANDLE t = xl_il_token(rid);
    if (!t) { t_skip("cannot derive a token at integrity 0x%lx (error %lu)", (unsigned long)rid, GetLastError()); return 0; }
    CloseHandle(t); return 1;
}
static HWND mk_kind(char kind, struct xl_img *im) {
    HWND w = xl_mk(WS_POPUP, kind == 'P' ? 0 : XL_EXL, NULL); if (!w) return NULL;
    if (kind == 'S') xl_set(w, 0x112233, 100, 3);
    if (kind == 'U') xl_ulw(w, im, ULW_ALPHA);
    return w;
}
/* State of a window as seen from this process: Get result plus the extended style. */
struct snap { int r; unsigned long key, alpha, flags, ex; };
static struct snap snap_of(HWND w) { struct snap s; struct xl_attr a; xl_attr_clear(&a); s.r = xl_getall(w, &a); s.key = a.key; s.alpha = a.alpha; s.flags = a.flags; s.ex = xl_exstyle(w); return s; }
static int same_snap(struct snap a, struct snap b) { return a.r == b.r && a.key == b.key && a.alpha == b.alpha && a.flags == b.flags && a.ex == b.ex; }

/* ---- a lower-integrity actor on our windows: every operation is refused with access denied ----------------------------------- */
#define DENY(nm, kind, op, rid) \
    TEST(uipi_##nm) { XL_NEED(); if (!below(rid)) return; struct xl_img im; if (!xl_img_make(&im, 8, 8)) { t_skip("image"); return; } \
        HWND w = mk_kind(kind, &im); if (!w) { xl_img_free(&im); t_skip("window"); return; } struct snap before = snap_of(w); \
        struct ures u = act(w, op, rid); CHECK(u.ok); if (u.ok) { EXPECT_EQ(u.r, 0); EXPECT_EQ(u.err, ERROR_ACCESS_DENIED); EXPECT_EQ(u.il, rid); } \
        CHECK(same_snap(before, snap_of(w))); CHECK(U(IsWindow)(w)); xl_kill(w); xl_img_free(&im); }
#define DENY_ALL(pfx, rid) \
    DENY(pfx##_fresh_get, 'L', "get", rid) DENY(pfx##_fresh_set, 'L', "set", rid) DENY(pfx##_fresh_ulw, 'L', "ulw", rid) \
    DENY(pfx##_fresh_swl, 'L', "swl", rid) DENY(pfx##_fresh_swp, 'L', "swp", rid) DENY(pfx##_fresh_destroy, 'L', "destroy", rid) \
    DENY(pfx##_set_get, 'S', "get", rid) DENY(pfx##_set_set, 'S', "set", rid) DENY(pfx##_set_ulw, 'S', "ulw", rid) \
    DENY(pfx##_set_swl, 'S', "swl", rid) DENY(pfx##_set_setflags0, 'S', "setflags0", rid) \
    DENY(pfx##_plain_get, 'P', "get", rid) DENY(pfx##_plain_set, 'P', "set", rid) DENY(pfx##_plain_ulw, 'P', "ulw", rid) \
    DENY(pfx##_plain_swl, 'P', "swl", rid) \
    DENY(pfx##_img_get, 'U', "get", rid) DENY(pfx##_img_set, 'U', "set", rid) DENY(pfx##_img_ulw, 'U', "ulw", rid) \
    DENY(pfx##_img_swl, 'U', "swl", rid)
DENY_ALL(low_actor, UIPI_LOW)
DENY(med_actor_fresh_set, 'L', "set", UIPI_MED) DENY(med_actor_fresh_get, 'L', "get", UIPI_MED) DENY(med_actor_set_ulw, 'S', "ulw", UIPI_MED)
DENY(med_actor_fresh_swl, 'L', "swl", UIPI_MED) DENY(med_actor_plain_set, 'P', "set", UIPI_MED)

/* Identity and style reads stay allowed. */
#define READ_OK(nm, kind) \
    TEST(uipi_low_actor_read_##nm) { XL_NEED(); if (!below(UIPI_LOW)) return; struct xl_img im; if (!xl_img_make(&im, 8, 8)) { t_skip("image"); return; } \
        HWND w = mk_kind(kind, &im); if (!w) { xl_img_free(&im); t_skip("window"); return; } \
        struct ures u = act(w, "read", UIPI_LOW); CHECK(u.ok); if (u.ok) { EXPECT_EQ(u.r, 1); EXPECT_EQ(u.v, (unsigned long)xl_exstyle(w)); } \
        xl_kill(w); xl_img_free(&im); }
READ_OK(fresh, 'L') READ_OK(plain, 'P')

/* A refused actor leaves no mark: after a refused Set the next Get still fails the way a fresh window does. */
TEST(uipi_low_actor_refused_set_then_own_get_fresh) {
    XL_NEED(); if (!below(UIPI_LOW)) return; HWND w = xl_lay(); if (!w) { t_skip("window"); return; }
    struct ures u = act(w, "set", UIPI_LOW); CHECK(u.ok); if (u.ok) EXPECT_EQ(u.err, ERROR_ACCESS_DENIED);
    struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(w, &a)); EXPECT_ERR(XL_ERR_SENT); EXPECT_EQ(a.flags, 0xfeedf00d);
    CHECK(xl_set(w, 1, 2, 3)); xl_kill(w);
}
TEST(uipi_low_actor_refused_ulw_then_own_ulw_ok) {
    XL_NEED(); if (!below(UIPI_LOW)) return; struct xl_img im; if (!xl_img_make(&im, 8, 8)) { t_skip("image"); return; } HWND w = xl_lay(); if (!w) { t_skip("window"); return; }
    struct ures u = act(w, "ulw", UIPI_LOW); CHECK(u.ok); if (u.ok) EXPECT_EQ(u.err, ERROR_ACCESS_DENIED);
    SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(w, &im, ULW_ALPHA)); xl_kill(w); xl_img_free(&im);
}
TEST(uipi_low_actor_refused_swl_keeps_layered_bit) {
    XL_NEED(); if (!below(UIPI_LOW)) return; HWND w = xl_lay(); if (!w) { t_skip("window"); return; } xl_set(w, 7, 8, 3); LONG ex = xl_exstyle(w);
    struct ures u = act(w, "swl", UIPI_LOW); CHECK(u.ok); if (u.ok) EXPECT_EQ(u.err, ERROR_ACCESS_DENIED);
    EXPECT_EQ(xl_exstyle(w), ex); struct xl_attr a; CHECK(xl_getall(w, &a)); EXPECT_EQ(a.alpha, 8); xl_kill(w);
}

/* ---- this process acts on a window of a lower-integrity helper ---------------------------------------------------------------- */
struct lo { struct xl_proc p; HWND w; int open; unsigned long il; };
static int lo_open(struct lo *o, const char *kind, DWORD rid) {
    char line[256]; unsigned long long h = 0; unsigned long pid = 0, il = 0; memset(o, 0, sizeof *o);
    if (!xl_spawn_il(&o->p, t_self_path(), "xwin", kind, rid)) return 0;
    if (!xl_readline_pump(&o->p, line, sizeof line, 30000) || sscanf(line, "READY %llx %lu", &h, &pid) != 2) { xl_kill_proc(&o->p); return 0; }
    o->w = (HWND)(ULONG_PTR)h; o->open = 1; (void)il; return 1;
}
static void lo_close(struct lo *o) { if (o->open) { xl_kill_proc(&o->p); o->open = 0; } }
#define LO(kind, rid, o) XL_NEED(); if (!below(rid)) return; struct lo o; if (!lo_open(&o, kind, rid)) { t_skip("cannot start the lowered helper"); return; }
static unsigned long owner_il(struct lo *o) {            /* the owner process's integrity level, read from its own token */
    HANDLE t = NULL; unsigned long level = 0; if (!OpenProcessToken(o->p.hproc, TOKEN_QUERY, &t)) { if (OpenProcessToken(o->p.hproc, 0, &t)) CloseHandle(t); return 0; }
    DWORD need = 0; GetTokenInformation(t, TokenIntegrityLevel, NULL, 0, &need);
    if (need) { TOKEN_MANDATORY_LABEL *l = malloc(need); if (l && GetTokenInformation(t, TokenIntegrityLevel, l, need, &need)) level = *GetSidSubAuthority(l->Label.Sid, *GetSidSubAuthorityCount(l->Label.Sid) - 1); free(l); }
    CloseHandle(t); return level;
}

#define LOWNER(pfx, rid) \
TEST(uipi_##pfx##_owner_runs_lowered) { LO("L", rid, o); unsigned long il = owner_il(&o); if (il) EXPECT_EQ(il, rid); else t_note("owner token not readable"); lo_close(&o); } \
TEST(uipi_##pfx##_owner_fresh_get_untouched) { LO("L", rid, o); struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(o.w, &a)); EXPECT_ERR(XL_ERR_SENT); EXPECT_EQ(a.flags, 0xfeedf00d); lo_close(&o); } \
TEST(uipi_##pfx##_owner_set_get_roundtrip) { LO("L", rid, o); SET_ERR(XL_ERR_SENT); CHECK(xl_set(o.w, 0x0a0b0c, 55, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); \
    struct xl_attr a; CHECK(xl_getall(o.w, &a)); EXPECT_EQ(a.key, 0x0a0b0c); EXPECT_EQ(a.alpha, 55); EXPECT_EQ(a.flags, 3); lo_close(&o); } \
TEST(uipi_##pfx##_owner_preset_readable) { LO("S", rid, o); struct xl_attr a; CHECK(xl_getall(o.w, &a)); EXPECT_EQ(a.key, 0x112233); EXPECT_EQ(a.alpha, 100); lo_close(&o); } \
TEST(uipi_##pfx##_owner_set_then_ulw_87) { LO("L", rid, o); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { lo_close(&o); t_skip("image"); return; } \
    CHECK(xl_set(o.w, 1, 2, 3)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(o.w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); xl_img_free(&im); lo_close(&o); } \
TEST(uipi_##pfx##_owner_fresh_ulw_ok) { LO("L", rid, o); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { lo_close(&o); t_skip("image"); return; } \
    SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(o.w, &im, ULW_ALPHA)); xl_img_free(&im); lo_close(&o); } \
TEST(uipi_##pfx##_owner_plain_window_87) { LO("P", rid, o); struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(o.w, 1, 2, 3), ERROR_INVALID_PARAMETER); \
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(o.w, &a), ERROR_INVALID_PARAMETER); lo_close(&o); } \
TEST(uipi_##pfx##_owner_swl_transparent) { LO("L", rid, o); LONG ex = xl_exstyle(o.w); SET_ERR(XL_ERR_SENT); LONG old = U(SetWindowLongA)(o.w, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT); \
    EXPECT_EQ(old, ex); EXPECT_EQ(xl_exstyle(o.w), ex | WS_EX_TRANSPARENT); lo_close(&o); } \
TEST(uipi_##pfx##_owner_swl_clear_layered_drops_attributes) { LO("S", rid, o); LONG ex = xl_exstyle(o.w); EXPECT_EQ(U(SetWindowLongA)(o.w, GWL_EXSTYLE, ex & ~XL_EXL), ex); \
    struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(o.w, &a), ERROR_INVALID_PARAMETER); lo_close(&o); } \
TEST(uipi_##pfx##_owner_destroy_denied_5) { LO("L", rid, o); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!U(DestroyWindow)(o.w), ERROR_ACCESS_DENIED); CHECK(U(IsWindow)(o.w)); lo_close(&o); } \
TEST(uipi_##pfx##_owner_killed_1400) { LO("S", rid, o); HWND w = o.w; lo_close(&o); struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(w, 1, 2, 3), ERROR_INVALID_WINDOW_HANDLE); \
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(w, &a), ERROR_INVALID_WINDOW_HANDLE); }
LOWNER(low, UIPI_LOW)
LOWNER(med, UIPI_MED)

/* The integrity state the area ran under (informational; every check above is conditional on it). */
TEST(uipi_integrity_state_note) {
    HANDLE t = NULL; TOKEN_ELEVATION e; DWORD n = 0; memset(&e, 0, sizeof e);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) { GetTokenInformation(t, TokenElevation, &e, sizeof e, &n); CloseHandle(t); }
    t_note("test process integrity level 0x%lx, token elevated=%lu", k_integrity_level(), (unsigned long)e.TokenIsElevated);
}
