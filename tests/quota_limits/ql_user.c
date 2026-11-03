/* USER object quota (win32kbase). Owned object types (thread or process owned, i.e.
 * type flags & 3 != 0 in win32kbase's per-type table) are refused with ERROR_NO_MORE_USER_HANDLES (1158)
 * once the per-process USER handle count (GetGuiResources(GR_USEROBJECTS)) has reached
 * the per-process USER handle quota. Freeing decrements it. */
#include "ql.h"

/* ---- creators ---- */
static void *mk_menu(int i) { return U(CreateMenu)(); }
static BOOL del_menu(void *h) { return U(DestroyMenu)((HMENU)h); }

static void *mk_accel(int i) {
    ACCEL a QL_ACCEL_ALIGN; memset(&a, 0, sizeof a);
    a.fVirt = FVIRTKEY | FCONTROL; a.key = 'A' + (i % 26); a.cmd = (WORD)(1000 + i % 1000);
    return U(CreateAcceleratorTableW)(&a, 1);
}
static BOOL del_accel(void *h) { return U(DestroyAcceleratorTable)((HACCEL)h); }

static void *mk_timer(int i) { return (void *)U(SetTimer)(NULL, 0, 0x7fffffff, NULL); }
static BOOL del_timer(void *h) { return U(KillTimer)(NULL, (UINT_PTR)h); }

static void *mk_win(int i) { return ql_msg_window(); }
static BOOL del_win(void *h) { return U(DestroyWindow)((HWND)h); }

static HWND g_parent;
static void setup_child(void) { g_parent = ql_msg_window(); }
static void teardown_child(void) { if (g_parent) U(DestroyWindow)(g_parent); g_parent = NULL; }
static void *mk_child(int i) {
    const WCHAR *c = ql_class();
    if (!c || !g_parent) return NULL;
    return U(CreateWindowExW)(0, c, L"c", WS_CHILD, 0, 0, 1, 1, g_parent, NULL, GetModuleHandleW(NULL), NULL);
}

static LRESULT CALLBACK hookproc(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }
static void *mk_hook(int i) { return U(SetWindowsHookExW)(WH_GETMESSAGE, hookproc, NULL, GetCurrentThreadId()); }
static BOOL del_hook(void *h) { return U(UnhookWindowsHookEx)((HHOOK)h); }

static void *mk_icon(int i) {
    static BYTE andb[32], xorb[32];
    memset(andb, 0xff, sizeof andb);
    return U(CreateIcon)(GetModuleHandleW(NULL), 16, 16, 1, 1, andb, xorb);
}
static BOOL del_icon(void *h) { return U(DestroyIcon)((HICON)h); }

static void CALLBACK weproc(HWINEVENTHOOK h, DWORD ev, HWND w, LONG o, LONG c, DWORD t, DWORD tm) {}
static void *mk_we(int i) {
    /* out-of-context and skipping our own process: no event is ever delivered to the callback */
    return U(SetWinEventHook)(0x8012, 0x8012, NULL, weproc, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}
static BOOL del_we(void *h) { return U(UnhookWinEvent)((HWINEVENTHOOK)h); }

const struct ql_ukind uk_menu     = { "menu",     1, 0, mk_menu,  del_menu,  NULL, NULL };
const struct ql_ukind uk_accel    = { "accel",    1, 0, mk_accel, del_accel, NULL, NULL };
const struct ql_ukind uk_timer    = { "timer",    1, 0, mk_timer, del_timer, NULL, NULL };
const struct ql_ukind uk_window   = { "window",   0, 0, mk_win,   del_win,   NULL, NULL };
const struct ql_ukind uk_childwin = { "childwin", 0, 0, mk_child, del_win,   setup_child, teardown_child };
const struct ql_ukind uk_hook     = { "hook",     1, 0, mk_hook,  del_hook,  NULL, NULL };
const struct ql_ukind uk_icon     = { "icon",     1, 1, mk_icon,  del_icon,  NULL, NULL };
const struct ql_ukind uk_winevent = { "winevent", 1, 0, mk_we,    del_we,    NULL, NULL };

/* ---- fill machinery ---- */
static unsigned user_prep(const struct ql_ukind *k, struct ql_quota *qq) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return 0; }
    *qq = ql_user_quota();
    if (!qq->std) t_note("USERProcessHandleQuota configured as %u: equality checks are relaxed", qq->q);
    if (qq->q < 200) { t_skip("USER quota %u too small", qq->q); return 0; }
    if (qq->q + QL_SLACK > QL_MAX_OBJ) { t_skip("USER quota %u exceeds the %d object test budget", qq->q, QL_MAX_OBJ); return 0; }
    if (!ql_class()) { t_skip("cannot register the private window class"); return 0; }
    if (k->setup) k->setup();
    return qq->q;
}

static void user_fill(const struct ql_ukind *k, unsigned q, DWORD base, int checkpoints) {
    DWORD prev = ql_gur(GR_USER_CUR);
    int cap = (int)q + QL_SLACK;
    ql_f.n = 0; ql_f.failed = 0; ql_f.err = 0;
    for (int i = 0; i < cap; i++) {
        SetLastError(0);
        void *h = k->mk(i);
        if (!h) {
            ql_f.err = GetLastError();
            ql_f.failed = 1;
            ql_f.user_at_fail = ql_gur(GR_USER_CUR);
            ql_f.gdi_at_fail = ql_gur(GR_GDI_CUR);
            break;
        }
        ql_f.h[ql_f.n++] = h;
        if (checkpoints && (i % 61) == 0) {
            DWORD cur = ql_gur(GR_USER_CUR);
            QCF(cur >= prev, "USER count went down while creating: %lu -> %lu", prev, cur);
            if (k->exact && !k->uses_gdi) QCF(cur == base + (DWORD)ql_f.n, "USER count %lu after %d creations (base %lu)", cur, ql_f.n, base);
            prev = cur;
        }
    }
}

static void user_free(const struct ql_ukind *k, int order) {
    int n = ql_f.n;
    if (n == 0) return;
    if (order == 0) { for (int i = 0; i < n; i++) QC(k->del(ql_f.h[i])); }
    else if (order == 1) { for (int i = n - 1; i >= 0; i--) QC(k->del(ql_f.h[i])); }
    else {
        static unsigned char seen[QL_MAX_OBJ];
        memset(seen, 0, sizeof seen);
        int stride = 4093 % n ? 4093 % n : 1, pos = 0, done = 0;
        while (done < n) {
            while (seen[pos]) pos = (pos + 1) % n;
            seen[pos] = 1; QC(k->del(ql_f.h[pos])); done++;
            pos = (pos + stride) % n;
        }
    }
    ql_f.n = 0;
}

/* After the deletes: the count must be back (windows are destroyed synchronously on their own thread). */
static void user_expect_base(DWORD base, const char *what) {
    k_pump(0);
    QCF(ql_gur(GR_USER_CUR) == base, "%s: USER count %lu, base %lu", what, ql_gur(GR_USER_CUR), base);
}

static void user_boundary(const struct ql_ukind *k) {
    struct ql_quota qq;
    unsigned q = user_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_USER_CUR), peak0 = ql_gur(GR_USER_PEAK), gbase = ql_gur(GR_GDI_CUR);
    user_fill(k, q, base, 1);
    if (!ql_f.failed) {
        if (qq.std) QCF(t_is_wine(), "%s: no refusal after %d creations, quota %u", k->name, ql_f.n, q);
        else t_note("%s: no refusal within %d objects for configured quota %u", k->name, ql_f.n, q);
        user_free(k, 0);
        if (k->teardown) k->teardown();
        return;
    }
    DWORD U_ = ql_f.user_at_fail, G = ql_f.gdi_at_fail;
    t_note("%s: refused after %d creations, USER count %lu GDI count %lu, GetLastError %lu", k->name, ql_f.n, U_, G, ql_f.err);
    int user_hit = (U_ >= q - 2 && U_ <= q);
    if (k->uses_gdi) {
        struct ql_quota gq = ql_gdi_quota();
        int gdi_hit = (G + 2 >= gq.q && G <= gq.q);
        QCF(user_hit || gdi_hit, "%s refused with USER %lu/%u and GDI %lu/%u: neither quota reached", k->name, U_, q, G, gq.q);
        t_note("%s: stopped by the %s quota", k->name, user_hit ? "USER" : "GDI");
        if (user_hit && qq.std) QCF(ql_f.err == ERROR_NO_MORE_USER_HANDLES, "last error %lu", ql_f.err);
    } else if (qq.std) {
        if (k->exact) QCF(U_ == q, "%s: USER count at refusal %lu, quota %u (refuse when count >= quota)", k->name, U_, q);
        else QCF(user_hit, "%s: USER count at refusal %lu, quota %u", k->name, U_, q);
        if (k->exact) QCF((DWORD)ql_f.n == q - base, "%s: created %d, expected %lu", k->name, ql_f.n, (DWORD)q - base);
        if (q == 10000) QCF(U_ >= 9900 && U_ <= 10010, "USER count at refusal %lu outside [9900,10010]", U_);
        /* the allocator sets 1158 as its last act and nothing on the accel/timer paths overwrites it */
        if (k == &uk_accel || k == &uk_timer) QCF(ql_f.err == ERROR_NO_MORE_USER_HANDLES, "%s: last error %lu want 1158", k->name, ql_f.err);
    } else {
        QCF(U_ <= q, "USER count at refusal %lu above the configured quota %u", U_, q);
    }
    /* a refused creation neither leaks a handle nor wedges the thread */
    for (int r = 0; r < 3; r++) {
        void *h = k->mk(r);
        QCF(h == NULL, "%s: creation succeeded at the quota", k->name);
        if (h) k->del(h);
        QCF(ql_gur(GR_USER_CUR) == U_, "%s: refused creation changed USER count %lu -> %lu", k->name, U_, ql_gur(GR_USER_CUR));
    }
    DWORD peak = ql_gur(GR_USER_PEAK);
    QCF(peak >= U_, "USER peak %lu < count %lu", peak, U_);
    QCF(peak >= peak0, "USER peak went down %lu -> %lu", peak0, peak);
    user_free(k, 0);
    user_expect_base(base, k->name);
    if (k->teardown) { k->teardown(); user_expect_base(base - (k == &uk_childwin ? 1 : 0), "teardown"); }
    QCF(ql_gur(GR_USER_PEAK) == peak, "USER peak changed by frees: %lu -> %lu", peak, ql_gur(GR_USER_PEAK));
    if (k->uses_gdi) QCF(ql_gur(GR_GDI_CUR) == gbase, "%s: GDI count %lu after cleanup, base %lu", k->name, ql_gur(GR_GDI_CUR), gbase);
}

static void user_recycle(const struct ql_ukind *k) {
    struct ql_quota qq;
    unsigned q = user_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_USER_CUR);
    user_fill(k, q, base, 0);
    if (!ql_f.failed) { if (qq.std) QCF(t_is_wine(), "%s: no refusal", k->name); user_free(k, 0); if (k->teardown) k->teardown(); return; }
    DWORD F = ql_f.user_at_fail;
    static const int steps[] = { 1, 2, 10, 50 };
    for (unsigned s = 0; s < sizeof steps / sizeof steps[0]; s++) {
        int st = steps[s];
        for (int j = 0; j < st; j++) { QC(k->del(ql_f.h[--ql_f.n])); }
        DWORD cur = ql_gur(GR_USER_CUR);
        if (k->exact && !k->uses_gdi) QCF(cur == F - st, "%s: after freeing %d: %lu (was %lu)", k->name, st, cur, F);
        else QCF(cur < F, "%s: count did not drop after freeing %d", k->name, st);
        int again = 0;
        for (int j = 0; j < st + 5; j++) {
            void *h = k->mk(j);
            if (!h) break;
            ql_f.h[ql_f.n++] = h; again++;
        }
        if (k->exact && !k->uses_gdi) QCF(again == st, "%s: re-created %d after freeing %d", k->name, again, st);
        else QCF(again >= 1 && again <= st + 2, "%s: re-created %d after freeing %d", k->name, again, st);
        F = ql_gur(GR_USER_CUR);
        { void *x = k->mk(0); QC(x == NULL || !qq.std); if (x) ql_f.h[ql_f.n++] = x; }
    }
    user_free(k, 1);
    user_expect_base(base, k->name);
    if (k->teardown) k->teardown();
}

static void user_cleanup(const struct ql_ukind *k, int order) {
    struct ql_quota qq;
    unsigned q = user_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_USER_CUR);
    user_fill(k, q, base, 0);
    int n1 = ql_f.n;
    user_free(k, order);
    user_expect_base(base, k->name);
    user_fill(k, q, base, 0);
    QCF(ql_f.n == n1 || t_is_wine(), "%s: second fill created %d, first %d", k->name, ql_f.n, n1);
    user_free(k, order == 0 ? 1 : 0);
    user_expect_base(base, k->name);
    if (k->teardown) k->teardown();
}

#define UK_FACETS(K) \
    QL_SCENARIO(quota_limits_user_##K##_boundary, user_boundary(&uk_##K)) \
    QL_SCENARIO(quota_limits_user_##K##_recycle, user_recycle(&uk_##K)) \
    QL_SCENARIO(quota_limits_user_##K##_cleanup_fwd, user_cleanup(&uk_##K, 0)) \
    QL_SCENARIO(quota_limits_user_##K##_cleanup_rev, user_cleanup(&uk_##K, 1)) \
    QL_SCENARIO(quota_limits_user_##K##_cleanup_stride, user_cleanup(&uk_##K, 2))
UK_FACETS(menu)
UK_FACETS(accel)
UK_FACETS(timer)
UK_FACETS(window)
UK_FACETS(childwin)
UK_FACETS(hook)
UK_FACETS(icon)
UK_FACETS(winevent)

/* ---- the two quotas are independent counters ---- */
static void user_full_gdi_free(void) {
    struct ql_quota qq;
    unsigned q = user_prep(&uk_menu, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_USER_CUR), gbase = ql_gur(GR_GDI_CUR);
    user_fill(&uk_menu, q, base, 0);
    if (!ql_f.failed) { if (qq.std) QCF(t_is_wine(), "no refusal"); user_free(&uk_menu, 0); return; }
    /* USER quota exhausted: GDI allocation is governed by the separate GDI counter */
    HPEN b = U(CreatePen)(PS_SOLID, 1, RGB(9, 9, 9));      /* a pen: one fresh handle, no cache involved */
    QCF(b != NULL, "GDI pen refused although only the USER quota is exhausted");
    QCF(ql_gur(GR_GDI_CUR) == gbase + (b ? 1 : 0), "GDI count %lu", ql_gur(GR_GDI_CUR));
    if (b) QC(U(DeleteObject)(b));
    HRGN r = U(CreateRectRgn)(0, 0, 3, 3);
    QC(r != NULL);
    if (r) U(DeleteObject)(r);
    user_free(&uk_menu, 2);
    user_expect_base(base, "menu");
}
QL_SCENARIO(quota_limits_user_full_does_not_block_gdi, user_full_gdi_free())

static void gdi_full_user_free(void) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return; }
    struct ql_quota gq = ql_gdi_quota();
    if (gq.q < 256 || gq.q + QL_SLACK > QL_MAX_OBJ) { t_skip("GDI quota %u unsuitable", gq.q); return; }
    DWORD gbase = ql_gur(GR_GDI_CUR), ubase = ql_gur(GR_USER_CUR);
    int n = 0, failed = 0;
    for (int i = 0; i < (int)gq.q + QL_SLACK; i++) {
        HPEN b = U(CreatePen)(PS_SOLID, 1, RGB(i & 255, 0, 0));       /* pens: deleted pens free their slot at once */
        if (!b) { failed = 1; break; }
        ql_f.h[n++] = b;
    }
    if (!failed) { QCF(t_is_wine(), "no GDI refusal"); for (int i = 0; i < n; i++) U(DeleteObject)(ql_f.h[i]); return; }
    /* GDI quota exhausted: USER objects that need no GDI object are still created */
    HMENU m = U(CreateMenu)();
    QCF(m != NULL, "menu refused although only the GDI quota is exhausted");
    HACCEL a = (HACCEL)mk_accel(0);
    QCF(a != NULL, "accelerator table refused although only the GDI quota is exhausted");
    UINT_PTR t = U(SetTimer)(NULL, 0, 0x7fffffff, NULL);
    QCF(t != 0, "timer refused although only the GDI quota is exhausted");
    HWND w = ql_msg_window();
    QCF(w != NULL, "window refused although only the GDI quota is exhausted");
    QCF(ql_gur(GR_USER_CUR) == ubase + (m ? 1 : 0) + (a ? 1 : 0) + (t ? 1 : 0) + (w ? 1 : 0), "USER count %lu", ql_gur(GR_USER_CUR));
    if (m) U(DestroyMenu)(m);
    if (a) U(DestroyAcceleratorTable)(a);
    if (t) U(KillTimer)(NULL, t);
    if (w) U(DestroyWindow)(w);
    for (int i = 0; i < n; i++) QC(U(DeleteObject)(ql_f.h[i]));
    QCF(ql_gur(GR_GDI_CUR) == gbase, "GDI count %lu base %lu", ql_gur(GR_GDI_CUR), gbase);
    user_expect_base(ubase, "after");
}
QL_SCENARIO(quota_limits_gdi_full_does_not_block_user, gdi_full_user_free())

/* ---- different USER types share one count ---- */
static void user_mixed(void) {
    static const struct ql_ukind *const ks[] = { &uk_menu, &uk_accel, &uk_timer, &uk_window, &uk_hook };
    static const struct ql_ukind *of[QL_MAX_OBJ];
    struct ql_quota qq;
    unsigned q = user_prep(&uk_menu, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_USER_CUR);
    int n = 0, failed = 0;
    for (int i = 0; i < (int)q + QL_SLACK; i++) {
        const struct ql_ukind *k = ks[i % 5];
        void *h = k->mk(i);
        if (!h) { failed = 1; break; }
        ql_f.h[n] = h; of[n] = k; n++;
    }
    if (!failed) { if (qq.std) QCF(t_is_wine(), "mixed fill never refused"); }
    else {
        DWORD U_ = ql_gur(GR_USER_CUR);
        if (qq.std) QCF(U_ + 2 >= q && U_ <= q, "mixed USER count at refusal %lu quota %u", U_, q);
        for (int j = 0; j < 5; j++) {
            void *h = ks[j]->mk(j);
            QCF(h == NULL || !qq.std, "type %s still allocates at the quota", ks[j]->name);
            if (h) { ql_f.h[n] = h; of[n] = ks[j]; n++; }
        }
        /* one freed slot of any type is reusable by any other type */
        for (int j = 0; j < 5 && qq.std; j++) {
            n--; QC(of[n]->del(ql_f.h[n]));
            void *h = ks[j]->mk(j);
            QCF(h != NULL, "type %s refused although a slot is free", ks[j]->name);
            if (h) { ql_f.h[n] = h; of[n] = ks[j]; n++; }
        }
    }
    for (int i = 0; i < n; i++) QC(of[i]->del(ql_f.h[i]));
    user_expect_base(base, "mixed");
}
QL_SCENARIO(quota_limits_user_mixed_types, user_mixed())

/* ---- peak counter ---- */
static void user_peak(void) {
    struct ql_quota qq;
    unsigned q = user_prep(&uk_menu, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_USER_CUR), p0 = ql_gur(GR_USER_PEAK);
    QC(p0 >= base);
    HMENU m[200];
    for (int i = 0; i < 200; i++) m[i] = U(CreateMenu)();
    DWORD p1 = ql_gur(GR_USER_PEAK);
    QCF(p1 >= base + 200 && p1 >= p0, "peak %lu after 200 menus (base %lu)", p1, base);
    for (int i = 0; i < 200; i++) QC(U(DestroyMenu)(m[i]));
    QCF(ql_gur(GR_USER_PEAK) == p1, "peak dropped after frees: %lu -> %lu", p1, ql_gur(GR_USER_PEAK));
    QCF(ql_gur(GR_USER_CUR) == base, "count %lu base %lu", ql_gur(GR_USER_CUR), base);
    /* the peak only moves up when the count exceeds it */
    HMENU one = U(CreateMenu)();
    QC(ql_gur(GR_USER_PEAK) == p1);
    QC(U(DestroyMenu)(one));
}
QL_SCENARIO(quota_limits_user_peak_tracks_maximum, user_peak())
