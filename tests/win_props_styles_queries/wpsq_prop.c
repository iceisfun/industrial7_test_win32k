/* Window property syscalls: NtUserSetProp / NtUserGetProp / NtUserRemoveProp / NtUserBuildPropList
 * (win32kfull, backed by win32kbase RealInternalSetProp / RealGetProp / RealInternalRemoveProp and
 * _BuildPropList). user32 maps string keys to global atoms (SetPropW: GlobalAddAtomW, key|0x10000 =
 * "this prop owns a reference to the atom"; GetPropW: GlobalFindAtomW; RemovePropW: GlobalFindAtomW,
 * key|0x10000) and enumerates through NtUserBuildPropList (InternalEnumProps). */
#include "wpsq.h"

static void mkkey(WCHAR *out, const char *tag) {
    char tmp[96]; snprintf(tmp, sizeof tmp, "win32ktP%lu_%s", GetCurrentProcessId(), tag);
    int i = 0; for (; tmp[i]; i++) out[i] = (unsigned char)tmp[i];
    out[i] = 0;
}
static void mkkeyA(char *out, const char *tag) { snprintf(out, 96, "win32ktP%lu_%s", GetCurrentProcessId(), tag); }

/* key: a WCHAR string or MAKEINTATOM(atom) */
static BOOL  sp(HWND h, const void *k, ULONG_PTR v) { return U(SetPropW)(h, (LPCWSTR)k, (HANDLE)v); }
static ULONG_PTR gp(HWND h, const void *k) { return (ULONG_PTR)U(GetPropW)(h, (LPCWSTR)k); }
static ULONG_PTR rp(HWND h, const void *k) { return (ULONG_PTR)U(RemovePropW)(h, (LPCWSTR)k); }
static ATOM  fa(const WCHAR *k) { return U(GlobalFindAtomW)(k); }

#define NEED_PROP_API() do { REQUIRE(U(SetPropW)); REQUIRE(U(GetPropW)); REQUIRE(U(RemovePropW)); REQUIRE(U(GlobalFindAtomW)); REQUIRE(U(CreateWindowExW)); } while (0)

/* ---- enumeration collector ---- */
struct pe { WCHAR name[96]; ULONG_PTR key; HANDLE data; int is_atom; };
struct pcol { struct pe e[600]; int n; int stop_at; int ret; int remove_self; HWND h; };
static BOOL CALLBACK col_cb(HWND h, LPWSTR s, HANDLE d, ULONG_PTR u) {
    struct pcol *c = (struct pcol *)u;
    if (c->n < 600) {
        struct pe *e = &c->e[c->n];
        e->key = (ULONG_PTR)s; e->data = d;
        e->is_atom = ((ULONG_PTR)s >> 16) == 0;
        e->name[0] = 0;
        if (!e->is_atom) { int i = 0; for (; s[i] && i < 95; i++) e->name[i] = s[i]; e->name[i] = 0; }
        c->n++;
    }
    if (c->remove_self && c->h) U(RemovePropW)(c->h, s);
    if (c->stop_at && c->n >= c->stop_at) return 0;
    return c->ret ? c->ret : 1;
}
static int enum_all(HWND h, struct pcol *c) {
    memset(c, 0, sizeof *c); c->h = h;
    return U(EnumPropsExW)(h, col_cb, (LPARAM)c);
}
static int find_name(struct pcol *c, const WCHAR *n) {
    for (int i = 0; i < c->n; i++) if (!c->e[i].is_atom && !lstrcmpiW(c->e[i].name, n)) return i;
    return -1;
}
static int find_atom(struct pcol *c, ATOM a) {
    for (int i = 0; i < c->n; i++) if (c->e[i].is_atom && (ATOM)c->e[i].key == a) return i;
    return -1;
}
static ULONG prop_count(HWND h) {
    ULONG cnt = 0xFFFFFFFF; struct wpsq_prop buf[1];
    LONG st = UN(NtUserBuildPropList)(h, 1, buf, &cnt);
    (void)st;
    return cnt;
}

/* ---------------- basic set / get / remove ------------------------------------------------ */

WT(prop_string_roundtrip) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "rt");
    CHECK(sp(h, k, 0x1234));
    WEQ(gp(h, k), 0x1234);
    WEQ(rp(h, k), 0x1234);
    WEQ(gp(h, k), 0);
    U(DestroyWindow)(h);
}

WT(prop_values_are_pointer_wide) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "wide");
    static const ULONG_PTR v[] = { 1, 0xFFFFFFFFull, 0x100000000ull, 0x1122334455667788ull, 0xFFFF800000000000ull, ~(ULONG_PTR)0 };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        CHECK(sp(h, k, v[i]));
        WEQ(gp(h, k), v[i]);
    }
    WEQ(rp(h, k), ~(ULONG_PTR)0);
    U(DestroyWindow)(h);
}

WT(prop_key_is_case_insensitive) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96], u[96]; mkkey(k, "CaseKey");
    lstrcpyW(u, k); CharUpperW(u);
    CHECK(sp(h, k, 7));
    /* string keys go through the (case-insensitive) global atom table, so both spellings are one atom */
    WEQ(gp(h, u), 7);
    CHECK(sp(h, u, 8));
    WEQ(gp(h, k), 8);
    WEQ(rp(h, u), 8);
    WEQ(gp(h, k), 0);
    U(DestroyWindow)(h);
}

WT(prop_ansi_and_wide_keys_are_one_atom) {
    NEED_PROP_API(); REQUIRE(U(SetPropA)); REQUIRE(U(GetPropA)); REQUIRE(U(RemovePropA));
    NEED_WND(h, 0);
    char ka[96]; WCHAR kw[96]; mkkeyA(ka, "ansi"); mkkey(kw, "ansi");
    CHECK(U(SetPropA)(h, ka, (HANDLE)0x41));
    WEQ(gp(h, kw), 0x41);
    CHECK(sp(h, kw, 0x42));
    WEQ((ULONG_PTR)U(GetPropA)(h, ka), 0x42);
    WEQ((ULONG_PTR)U(RemovePropA)(h, ka), 0x42);
    WEQ(gp(h, kw), 0);
    U(DestroyWindow)(h);
}

WT(prop_string_key_creates_global_atom) {
    NEED_PROP_API(); REQUIRE(U(GlobalGetAtomNameW)); NEED_WND(h, 0);
    WCHAR k[96], back[128]; mkkey(k, "atomcreate");
    WEQ(fa(k), 0);                          /* unique per process: not present yet */
    CHECK(sp(h, k, 1));
    ATOM a = fa(k);
    CHECK(a != 0);
    CHECKF(a >= 0xC000, "string atom %04x below 0xC000", a);     /* GlobalAddAtom string atoms live in 0xC000..0xFFFF */
    UINT n = U(GlobalGetAtomNameW)(a, back, 128);
    WEQ(n, lstrlenW(k));
    CHECK(!lstrcmpW(back, k));
    rp(h, k);
    U(DestroyWindow)(h);
}

WT(prop_remove_string_releases_the_atom) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "relatom");
    CHECK(sp(h, k, 1));
    CHECK(fa(k) != 0);
    /* RemovePropW passes atom|0x10000; RealInternalRemoveProp then drops the atom reference SetPropW took */
    WEQ(rp(h, k), 1);
    WEQ(fa(k), 0);
    U(DestroyWindow)(h);
}

WT(prop_atom_key_interchangeable_with_string) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "interchange");
    CHECK(sp(h, k, 0xA1));
    ATOM a = fa(k);
    CHECK(a != 0);
    /* RealGetProp matches on the 16-bit atom and flag bit 0 only (bit 1, "owns atom", is not compared) */
    WEQ(gp(h, MAKEINTATOM(a)), 0xA1);
    CHECK(sp(h, MAKEINTATOM(a), 0xA2));
    WEQ(gp(h, k), 0xA2);
    ULONG c = prop_count(h);
    (void)c;
    WEQ(rp(h, k), 0xA2);
    U(DestroyWindow)(h);
}

WT(prop_integer_atom_key_without_atom_table_entry) {
    NEED_PROP_API(); NEED_WND(h, 0);
    /* RealInternalSetProp never validates the atom, only that it is non-zero */
    static const ATOM at[] = { 1, 2, 5, 0x10, 0x100, 0x1234, 0x7FFF, 0xBFFE, 0xBFFF };
    for (unsigned i = 0; i < sizeof at / sizeof *at; i++)
        CHECKF(sp(h, MAKEINTATOM(at[i]), 0x1000 + at[i]), "atom %04x", at[i]);
    for (unsigned i = 0; i < sizeof at / sizeof *at; i++)
        WEQ(gp(h, MAKEINTATOM(at[i])), 0x1000u + at[i]);
    for (unsigned i = 0; i < sizeof at / sizeof *at; i++)
        WEQ(rp(h, MAKEINTATOM(at[i])), 0x1000u + at[i]);
    U(DestroyWindow)(h);
}

WT(prop_replace_keeps_single_entry) {
    NEED_PROP_API(); REQUIRE(UN(NtUserBuildPropList)); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "replace");
    ULONG base = prop_count(h);
    CHECK(sp(h, k, 1));
    ULONG one = prop_count(h);
    CHECK(sp(h, k, 2));
    CHECK(sp(h, k, 3));
    WEQ(prop_count(h), one);               /* RealInternalSetProp finds the existing PROP via _FindProp and overwrites it */
    WEQ(one, base + 1);
    WEQ(rp(h, k), 3);
    WEQ(prop_count(h), base);
    U(DestroyWindow)(h);
}

WT(prop_null_data_is_stored_not_removed) {
    NEED_PROP_API(); REQUIRE(UN(NtUserBuildPropList)); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "nulldata");
    ULONG base = prop_count(h);
    CHECK(sp(h, k, 0));                       /* RealInternalSetProp stores the zero and returns 1 */
    WEQ(gp(h, k), 0);
    WEQ(prop_count(h), base + 1);
    struct pcol c; enum_all(h, &c);
    CHECK(find_name(&c, k) >= 0);
    rp(h, k);
    WEQ(prop_count(h), base);
    U(DestroyWindow)(h);
}

WT(prop_remove_missing_returns_zero) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "missing");
    WEQ(rp(h, k), 0);                         /* GlobalFindAtomW fails -> RemovePropW returns 0 without entering the kernel */
    WCHAR k2[96]; mkkey(k2, "missing2");
    CHECK(sp(h, k2, 5));
    WEQ(rp(h, MAKEINTATOM(0x1234)), 0);        /* _FindProp miss */
    WEQ(rp(h, k2), 5);
    WEQ(rp(h, k2), 0);                         /* second removal: atom is gone (or prop is) */
    U(DestroyWindow)(h);
}

WT(prop_get_unknown_and_null_key) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "neverset");
    WEQ(gp(h, k), 0);
    WEQ(gp(h, NULL), 0);
    WEQ(gp(h, MAKEINTATOM(0xBEEF)), 0);
    U(DestroyWindow)(h);
}

WT(prop_set_null_key_fails_invalid_parameter) {
    NEED_PROP_API(); NEED_WND(h, 0);
    SetLastError(0);
    /* SetPropW(NULL key) -> NtUserSetProp(hwnd, 0, data) -> RealInternalSetProp: UserSetLastError(0x57), 0 */
    WEQ(sp(h, NULL, 5), 0);
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}

WT(prop_set_empty_string_fails) {
    NEED_PROP_API(); NEED_WND(h, 0);
    SetLastError(0);
    WEQ(sp(h, L"", 5), 0);                     /* GlobalAddAtomW(L"") fails, SetPropW returns 0 before the syscall */
    U(DestroyWindow)(h);
}

WT(prop_string_key_length_limit_255) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[300];
    wcscpy(k, L"L");
    int base = (int)wcslen(k);
    for (int i = base; i < 255; i++) k[i] = L'a' + (i % 20);
    k[255] = 0;
    CHECK(sp(h, k, 1));                        /* 255 characters is the longest atom string */
    WEQ(gp(h, k), 1);
    WEQ(rp(h, k), 1);
    k[255] = L'z'; k[256] = 0;
    SetLastError(0);
    WEQ(sp(h, k, 1), 0);                       /* 256 characters: GlobalAddAtomW fails */
    WEQ(gp(h, k), 0);
    U(DestroyWindow)(h);
}

WT(prop_independent_between_windows) {
    NEED_PROP_API(); NEED_WND(a, 0);
    HWND b = wpsq_mk(0, "b");
    if (!b) { t_skip("no 2nd window"); U(DestroyWindow)(a); return; }
    WCHAR k[96]; mkkey(k, "indep");
    CHECK(sp(a, k, 10)); CHECK(sp(b, k, 20));
    WEQ(gp(a, k), 10); WEQ(gp(b, k), 20);
    WEQ(rp(a, k), 10);
    WEQ(gp(b, k), 20);                         /* the atom stays alive: b's prop still references it */
    CHECK(fa(k) != 0);
    WEQ(rp(b, k), 20);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

WT(prop_many_properties_roundtrip) {
    NEED_PROP_API(); REQUIRE(UN(NtUserBuildPropList)); NEED_WND(h, 0);
    enum { N = 200 };
    WCHAR k[N][96];
    ULONG base = prop_count(h);
    for (int i = 0; i < N; i++) { char t[32]; snprintf(t, sizeof t, "many%03d", i); mkkey(k[i], t); CHECKF(sp(h, k[i], 0x100 + i), "set %d", i); }
    WEQ(prop_count(h), base + N);
    for (int i = 0; i < N; i++) WEQ(gp(h, k[i]), 0x100u + i);
    struct pcol *c = (struct pcol *)calloc(1, sizeof *c);
    int r = enum_all(h, c);
    WEQ(c->n, base + N);
    for (int i = 0; i < N; i++) {
        int at = find_name(c, k[i]);
        CHECKF(at >= 0, "prop %d missing from enumeration", i);
        if (at >= 0) WEQ(c->e[at].data, 0x100u + i);
    }
    (void)r;
    for (int i = 0; i < N; i++) WEQ(rp(h, k[i]), 0x100u + i);
    WEQ(prop_count(h), base);
    for (int i = 0; i < N; i += 37) WEQ(fa(k[i]), 0);   /* every owned atom reference was released */
    free(c);
    U(DestroyWindow)(h);
}

WT(prop_removal_swaps_last_into_hole) {
    NEED_PROP_API(); REQUIRE(UN(NtUserBuildPropList)); NEED_WND(h, 0);
    if (prop_count(h) != 0) { t_skip("window starts with system properties"); U(DestroyWindow)(h); return; }
    WCHAR a[96], b[96], c[96]; mkkey(a, "ordA"); mkkey(b, "ordB"); mkkey(c, "ordC");
    sp(h, a, 1); sp(h, b, 2); sp(h, c, 3);
    struct pcol col; enum_all(h, &col);
    WEQ(col.n, 3);
    ATOM aa = fa(a), ab = fa(b), ac = fa(c);
    if (col.n == 3) {
        t_info("enumeration order atoms: %04x %04x %04x (set order a,b,c = %04x %04x %04x)", (unsigned)col.e[0].key, (unsigned)col.e[1].key, (unsigned)col.e[2].key, aa, ab, ac);
        WEQ(col.e[0].data, 1);       /* props are appended in insertion order */
    }
    rp(h, a);
    /* RealInternalRemoveProp copies the last entry over the removed one */
    enum_all(h, &col);
    WEQ(col.n, 2);
    if (col.n == 2) { WEQ(col.e[0].data, 3); WEQ(col.e[1].data, 2); }
    rp(h, b); rp(h, c);
    U(DestroyWindow)(h);
}

WT(prop_survive_other_window_destruction) {
    NEED_PROP_API(); NEED_WND(a, 0);
    HWND b = wpsq_mk(0, "b");
    if (!b) { t_skip("no 2nd window"); U(DestroyWindow)(a); return; }
    WCHAR k[96]; mkkey(k, "survive");
    sp(a, k, 1); sp(b, k, 2);
    U(DestroyWindow)(a);
    WEQ(gp(b, k), 2);
    CHECK(fa(k) != 0);                         /* b still holds a reference */
    rp(b, k);
    U(DestroyWindow)(b);
}

WT(prop_destroy_window_with_props_keeps_other_atoms) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "destroyatom");
    sp(h, k, 1);
    U(DestroyWindow)(h);
    /* the window's property list is freed with the window; whether the atom reference is dropped is not
     * observable through user32 alone, so record it */
    t_note("string atom after DestroyWindow: %s", fa(k) ? "still present" : "released");
    ATOM a = fa(k); if (a) U(GlobalDeleteAtom)(a);
}

WT(prop_invalid_hwnd_set_get_remove) {
    NEED_PROP_API();
    WCHAR k[96]; mkkey(k, "invalidhwnd");
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        WEQ(gp(bad[i], k), 0);
        WEQ(gp(bad[i], MAKEINTATOM(5)), 0);
        SetLastError(0);
        WEQ(sp(bad[i], k, 5), 0);
        SetLastError(0);
        WEQ(sp(bad[i], MAKEINTATOM(5), 5), 0);
        WEQ(rp(bad[i], MAKEINTATOM(5)), 0);
    }
 /* SetPropW added the atom before the syscall failed and deleted it again */
    WEQ(fa(k), 0);
}

WT(prop_get_invalid_hwnd_last_error) {
    NEED_PROP_API();
    SetLastError(0);
    WEQ(gp(GARBAGE_HWND, MAKEINTATOM(7)), 0);
    EXPECT_ERR(E_INVALID_HWND);            /* GetPropW: validation fails -> RtlSetLastWin32Error(0x578) */
    SetLastError(0);
    WEQ(gp(NULL, MAKEINTATOM(7)), 0);
    EXPECT_ERR(E_INVALID_HWND);
}

WT(prop_destroyed_window_rejects_props) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "stale");
    sp(h, k, 1);
    U(DestroyWindow)(h);
    WEQ(gp(h, k), 0);
    WEQ(sp(h, k, 2), 0);
    WEQ(rp(h, k), 0);
    WEQ(fa(k), 0);
}

WT(prop_set_from_other_thread_same_process) {
    NEED_PROP_API(); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "po")) { t_skip("owner failed"); return; }
    WCHAR k[96]; mkkey(k, "xthread");
    CHECK(sp(o.hwnd, k, 0x77));               /* no thread-ownership rule for properties */
    WEQ(gp(o.hwnd, k), 0x77);
    struct pcol c; enum_all(o.hwnd, &c);
    CHECK(find_name(&c, k) >= 0);
    WEQ(rp(o.hwnd, k), 0x77);
    wpsq_owner_stop(&o);
}

static struct { int hit; char want[96]; } g_ae;
static BOOL CALLBACK ae_cb(HWND hh, LPSTR s, HANDLE d, ULONG_PTR u) {
    if (((ULONG_PTR)s >> 16) != 0 && !lstrcmpiA(s, g_ae.want)) g_ae.hit++;
    return TRUE;
}
WT(prop_ansi_enumeration_gives_ansi_names) {
    NEED_PROP_API(); REQUIRE(U(EnumPropsExA)); REQUIRE(U(SetPropA)); NEED_WND(h, 0);
    char ka[96]; mkkeyA(ka, "enumansi");
    strcpy(g_ae.want, ka); g_ae.hit = 0;
    CHECK(U(SetPropA)(h, ka, (HANDLE)9));
    U(EnumPropsExA)(h, ae_cb, 0);
    WEQ(g_ae.hit, 1);                           /* InternalEnumProps(flags&1==0) uses GlobalGetAtomNameA for atoms >= 0xC000 */
    U(RemovePropA)(h, ka);
    U(DestroyWindow)(h);
}

/* ---------------- enumeration ------------------------------------------------------------- */

WT(prop_enum_empty_returns_minus_one) {
    NEED_PROP_API(); REQUIRE(UN(NtUserBuildPropList)); NEED_WND(h, 0);
    if (prop_count(h) != 0) { t_skip("window starts with properties"); U(DestroyWindow)(h); return; }
    struct pcol c;
    int r = enum_all(h, &c);
    WEQ(c.n, 0);
 /* InternalEnumProps leaves == -1 when BuildPropList reports 0 entries */
    WEQ(r, -1);
    U(DestroyWindow)(h);
}

WT(prop_enum_invalid_hwnd_returns_minus_one) {
    NEED_PROP_API(); struct pcol c;
    WEQ(enum_all(GARBAGE_HWND, &c), -1);       /* NtUserBuildPropList -> STATUS_INVALID_HANDLE (negative) */
    WEQ(c.n, 0);
    WEQ(enum_all(NULL, &c), -1);
    WEQ(c.n, 0);
}

WT(prop_enum_lists_strings_and_atoms) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k1[96], k2[96]; mkkey(k1, "enum1"); mkkey(k2, "enum2");
    sp(h, k1, 0x11); sp(h, k2, 0x22); sp(h, MAKEINTATOM(0x2A), 0x33);
    struct pcol c; enum_all(h, &c);
    int i1 = find_name(&c, k1), i2 = find_name(&c, k2), i3 = find_atom(&c, 0x2A);
    CHECK(i1 >= 0); CHECK(i2 >= 0);
    CHECK(i3 >= 0);                            /* atoms below 0xC000 are passed to the callback as integer atoms */
    if (i1 >= 0) WEQ(c.e[i1].data, 0x11);
    if (i2 >= 0) WEQ(c.e[i2].data, 0x22);
    if (i3 >= 0) { WEQ(c.e[i3].data, 0x33); WEQ(c.e[i3].key, 0x2A); }
    rp(h, k1); rp(h, k2); rp(h, MAKEINTATOM(0x2A));
    U(DestroyWindow)(h);
}

WT(prop_enum_stops_when_callback_returns_zero) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[5][96];
    for (int i = 0; i < 5; i++) { char t[16]; snprintf(t, sizeof t, "stop%d", i); mkkey(k[i], t); sp(h, k[i], i + 1); }
    struct pcol c; memset(&c, 0, sizeof c); c.stop_at = 2;
    int r = U(EnumPropsExW)(h, col_cb, (LPARAM)&c);
    WEQ(c.n, 2);
    WEQ(r, 0);
    for (int i = 0; i < 5; i++) rp(h, k[i]);
    U(DestroyWindow)(h);
}

WT(prop_enum_returns_last_callback_value) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[3][96];
    for (int i = 0; i < 3; i++) { char t[16]; snprintf(t, sizeof t, "ret%d", i); mkkey(k[i], t); sp(h, k[i], i + 1); }
    ULONG total = prop_count(h);
    struct pcol c; memset(&c, 0, sizeof c); c.ret = 77;
    WEQ(U(EnumPropsExW)(h, col_cb, (LPARAM)&c), 77);
    WEQ(c.n, total);
    for (int i = 0; i < 3; i++) rp(h, k[i]);
    U(DestroyWindow)(h);
}

WT(prop_enum_is_a_snapshot) {
    NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[6][96];
    ULONG base = 0;
    for (int i = 0; i < 6; i++) { char t[16]; snprintf(t, sizeof t, "snap%d", i); mkkey(k[i], t); sp(h, k[i], i + 1); }
    struct pcol c; memset(&c, 0, sizeof c); c.remove_self = 1; c.h = h;
    U(EnumPropsExW)(h, col_cb, (LPARAM)&c);
    /* BuildPropList copies the list before the first callback, so removing props mid-walk does not skip any */
    CHECK(c.n >= 6);
    for (int i = 0; i < 6; i++) WEQ(gp(h, k[i]), 0);
    (void)base;
    U(DestroyWindow)(h);
}

static struct { int hit; ULONG_PTR seen; WCHAR want[96]; } g_ne;
static BOOL CALLBACK ne_cb(HWND hh, LPCWSTR s, HANDLE d) {
    if (((ULONG_PTR)s >> 16) != 0 && !lstrcmpiW(s, g_ne.want)) { g_ne.hit++; g_ne.seen = (ULONG_PTR)d; }
    return TRUE;
}
WT(prop_enum_nonex_callback_signature) {
    NEED_PROP_API(); REQUIRE(U(EnumPropsW)); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "plainenum");
    sp(h, k, 0x5A);
    lstrcpyW(g_ne.want, k); g_ne.hit = 0; g_ne.seen = 0;
    U(EnumPropsW)(h, ne_cb);
    WEQ(g_ne.hit, 1);
    WEQ(g_ne.seen, 0x5A);
    rp(h, k);
    U(DestroyWindow)(h);
}

/* ---------------- direct syscalls --------------------------------------------------------- */

WT(prop_direct_buildproplist_exact_and_roomy_buffers) {
    REQUIRE(UN(NtUserBuildPropList)); NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[4][96];
    for (int i = 0; i < 4; i++) { char t[16]; snprintf(t, sizeof t, "bpl%d", i); mkkey(k[i], t); sp(h, k[i], 0x10 + i); }
    ULONG base = prop_count(h) - 4;
    struct wpsq_prop buf[64]; memset(buf, 0xEE, sizeof buf);
    ULONG cnt = 0xFFFFFFFF;
    LONG st = UN(NtUserBuildPropList)(h, 64, buf, &cnt);
    WEQ(st, 0);
    WEQ(cnt, base + 4);
    ULONG n = 0;
    for (ULONG i = 0; i < cnt; i++) {
        for (int j = 0; j < 4; j++) if (buf[i].atom == fa(k[j])) { WEQ(buf[i].data, 0x10 + j); n++; }
    }
    WEQ(n, 4);
    /* exactly cnt entries: still success */
    cnt = 0xFFFFFFFF;
    st = UN(NtUserBuildPropList)(h, base + 4, buf, &cnt);
    WEQ(st, 0);
    WEQ(cnt, base + 4);
    for (int i = 0; i < 4; i++) rp(h, k[i]);
    U(DestroyWindow)(h);
}

WT(prop_direct_buildproplist_small_buffer_reports_needed_count) {
    REQUIRE(UN(NtUserBuildPropList)); NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[5][96];
    for (int i = 0; i < 5; i++) { char t[16]; snprintf(t, sizeof t, "small%d", i); mkkey(k[i], t); sp(h, k[i], 1 + i); }
    ULONG total = prop_count(h);
    CHECK(total >= 5);
    struct wpsq_prop buf[2]; ULONG cnt = 0;
    LONG st = UN(NtUserBuildPropList)(h, 2, buf, &cnt);
    /* _BuildPropList clamps stores to the buffer, returns STATUS_BUFFER_TOO_SMALL and still reports the full count;
     * user32!InternalEnumProps relies on this to re-allocate */
    WEQ(st, NTST_BUFFER_TOO_SMALL);
    WEQ(cnt, total);
    for (int i = 0; i < 5; i++) rp(h, k[i]);
    U(DestroyWindow)(h);
}

WT(prop_direct_buildproplist_invalid_hwnd) {
    REQUIRE(UN(NtUserBuildPropList)); NEED_PROP_API();
    struct wpsq_prop buf[2]; ULONG cnt = 0x1234;
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        cnt = 0x1234;
        WEQ(UN(NtUserBuildPropList)(bad[i], 2, buf, &cnt), NTST_INVALID_HANDLE);
        WEQ(cnt, 0x1234);                         /* untouched on validation failure */
    }
}

WT(prop_direct_buildproplist_skips_nothing_for_user_props) {
    REQUIRE(UN(NtUserBuildPropList)); NEED_PROP_API(); NEED_WND(h, 0);
    ULONG base = prop_count(h);
    for (int i = 0; i < 10; i++) sp(h, MAKEINTATOM(0x100 + i), 1000 + i);
    WEQ(prop_count(h), base + 10);
    struct wpsq_prop buf[64]; ULONG cnt = 0;
    WEQ(UN(NtUserBuildPropList)(h, 64, buf, &cnt), 0);
    int found = 0;
    for (ULONG i = 0; i < cnt; i++)
        if (buf[i].atom >= 0x100 && buf[i].atom < 0x10A) { WEQ(buf[i].data, 1000u + (buf[i].atom - 0x100)); found++; }
    WEQ(found, 10);
    for (int i = 0; i < 10; i++) rp(h, MAKEINTATOM(0x100 + i));
    U(DestroyWindow)(h);
}

WT(prop_direct_setprop_atom_zero) {
    REQUIRE(UN(NtUserSetProp)); REQUIRE(U(CreateWindowExW)); NEED_WND(h, 0);
    SetLastError(0);
    WEQ(UN(NtUserSetProp)(h, 0, (HANDLE)1), 0);
    EXPECT_ERR(E_INVALID_PARAM);
    SetLastError(0);
    WEQ(UN(NtUserSetProp)(h, 0x10000, (HANDLE)1), 0);   /* low 16 bits are the atom: still zero */
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}

WT(prop_direct_setprop_invalid_hwnd) {
    REQUIRE(UN(NtUserSetProp)); REQUIRE(UN(NtUserGetProp)); REQUIRE(UN(NtUserRemoveProp)); REQUIRE(U(CreateWindowExW));
    U(GetWindowLongW);
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        WEQ(UN(NtUserSetProp)(bad[i], 0x77, (HANDLE)1), 0);
        WEQ(UN(NtUserGetProp)(bad[i], 0x77), 0);
        WEQ(UN(NtUserRemoveProp)(bad[i], 0x77), 0);
    }
}

WT(prop_direct_atom_reference_ownership) {
    REQUIRE(UN(NtUserSetProp)); REQUIRE(UN(NtUserRemoveProp)); REQUIRE(UN(NtUserGetProp)); NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "ownership");
    ATOM a = U(GlobalAddAtomW)(k);                         /* refcount 1 held by this test */
    CHECK(a >= 0xC000);
    /* high word 1 = the property takes over the reference: RealInternalRemoveProp(flags&2 || key>>16 == 1) deletes it */
    CHECK(UN(NtUserSetProp)(h, a | 0x10000, (HANDLE)0x31));
    WEQ(UN(NtUserGetProp)(h, a), 0x31);                    /* get ignores the high word */
    WEQ(UN(NtUserRemoveProp)(h, a | 0x10000), 0x31);
    WEQ(fa(k), 0);                                         /* the single reference was dropped by the removal */
    U(DestroyWindow)(h);
}

WT(prop_direct_remove_without_flag_keeps_atom) {
    REQUIRE(UN(NtUserSetProp)); REQUIRE(UN(NtUserRemoveProp)); NEED_PROP_API(); NEED_WND(h, 0);
    WCHAR k[96]; mkkey(k, "noflag");
    ATOM a = U(GlobalAddAtomW)(k);
    CHECK(UN(NtUserSetProp)(h, a, (HANDLE)0x32));          /* no ownership flag */
    WEQ(UN(NtUserRemoveProp)(h, a), 0x32);
    WEQ(fa(k), a);                                         /* atom untouched */
    U(GlobalDeleteAtom)(a);
    WEQ(fa(k), 0);
    U(DestroyWindow)(h);
}

WT(prop_direct_get_ignores_flag_bit_of_storage) {
    REQUIRE(UN(NtUserSetProp)); REQUIRE(UN(NtUserGetProp)); REQUIRE(UN(NtUserRemoveProp)); REQUIRE(U(CreateWindowExW)); NEED_WND(h, 0);
    CHECK(UN(NtUserSetProp)(h, 0x4321, (HANDLE)0x44));
    WEQ(UN(NtUserGetProp)(h, 0x4321), 0x44);
    WEQ(gp(h, MAKEINTATOM(0x4321)), 0x44);
    WEQ(UN(NtUserRemoveProp)(h, 0x4321), 0x44);
    WEQ(UN(NtUserGetProp)(h, 0x4321), 0);
    U(DestroyWindow)(h);
}

WT(prop_direct_remove_uses_low_word_only) {
    REQUIRE(UN(NtUserSetProp)); REQUIRE(UN(NtUserRemoveProp)); REQUIRE(U(CreateWindowExW)); NEED_WND(h, 0);
    CHECK(UN(NtUserSetProp)(h, 0x4322, (HANDLE)0x45));
 /* RealInternalRemoveProp looks the prop up by */
    WEQ(UN(NtUserRemoveProp)(h, 0x20000 | 0x4322), 0x45);
    WEQ(UN(NtUserGetProp)(h, 0x4322), 0);
    U(DestroyWindow)(h);
}

WT(prop_direct_get_atom_zero_and_unknown) {
    REQUIRE(UN(NtUserGetProp)); REQUIRE(U(CreateWindowExW)); NEED_WND(h, 0);
    WEQ(UN(NtUserGetProp)(h, 0), 0);
    WEQ(UN(NtUserGetProp)(h, 0xFFFF), 0);
    WEQ(UN(NtUserGetProp)(h, 1), 0);
    U(DestroyWindow)(h);
}

WT(prop_direct_viewid_property_slot) {
    /* NtUserSetProp special-cases the atom of "Microsoft.Windows.WindowFactory.ViewId": it also stores the value in
     * the window's own field (+0x138 of the shared window struct) while still creating an ordinary property. Only
     * the ordinary side is observable here. */
    NEED_PROP_API(); NEED_WND(h, 0);
    const WCHAR *k = L"Microsoft.Windows.WindowFactory.ViewId";
    CHECK(sp(h, k, 0x99));
    WEQ(gp(h, k), 0x99);
    WEQ(rp(h, k), 0x99);
    WEQ(gp(h, k), 0);
    U(DestroyWindow)(h);
}
