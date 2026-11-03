/* process_session_cleanup / process: what the kernel does when a whole GUI process dies.
 *
 * The child ("owner" scenario) creates GDI objects, top-level windows, a hook, a timer and a hotkey, then
 * leaves in one of four ways (return from main, ExitProcess, TerminateProcess(self), TerminateProcess by
 * the parent). win32kbase's process teardown (W32pProcessCallout / DestroyProcessInfo + GDI
 * vCleanupProcess, i.e. GdiProcessCleanup) frees every USER and GDI object the process owned.
 * The parent only ever looks at the shared tables, the user32/gdi32 entry points and GetGuiResources.
 */
#include "psc.h"

#define NEED_UI() do { REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateWindowExA)); REQUIRE(U(IsWindow)); REQUIRE(U(CreateSolidBrush)); \
    HWND probe_ = psc_mk(NULL, "probe"); if (!probe_) { t_skip("cannot create a window (no display driver)"); return; } U(DestroyWindow)(probe_); } while (0)

#define HK_MODS (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)
#define HK_VK VK_F23

/* shm layout of the "owner" scenario */
enum { V_BRUSH, V_PEN, V_RGN, V_BITMAP, V_DIB, V_DC, V_HW1, V_HW2, V_HOOK, V_NGDI, V_NUSER, V_TID, V_HKOK, V_STOCK = 16 /* 16..37 */ };
#define NSTOCK 22

static LRESULT CALLBACK hook_proc(int code, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, code, w, l); }

SCEN(owner) {
    HBRUSH br = U(CreateSolidBrush)(RGB(10, 20, 30));
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(1, 2, 3));
    HRGN rgn = U(CreateRectRgn)(0, 0, 3, 3);
    HBITMAP bmp = U(CreateBitmap)(4, 4, 1, 1, NULL);
    BITMAPINFO bi; void *bits = NULL; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 4; bi.bmiHeader.biHeight = 4;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP dib = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = U(CreateCompatibleDC)(NULL);
    CC(br && pen && rgn && bmp && dc);
    if (dc) { U(SetBkColor)(dc, 0x123456); U(SetBkMode)(dc, TRANSPARENT); }
    char t1[64], t2[64];
    sprintf(t1, "psc-title-%lu-a", GetCurrentProcessId());
    sprintf(t2, "psc-title-%lu-b", GetCurrentProcessId());
    HWND w1 = psc_mk(NULL, t1), w2 = psc_mk(NULL, t2);
    CC(w1 && w2);
    HHOOK hk = U(SetWindowsHookExA)(WH_GETMESSAGE, hook_proc, NULL, GetCurrentThreadId());
    if (w1) U(SetTimer)(w1, 3, 60000, NULL);
    BOOL hkok = w2 ? U(RegisterHotKey)(w2, 0x77, HK_MODS, HK_VK) : FALSE;
    x->s->v[V_BRUSH] = (LONGLONG)(ULONG_PTR)br; x->s->v[V_PEN] = (LONGLONG)(ULONG_PTR)pen;
    x->s->v[V_RGN] = (LONGLONG)(ULONG_PTR)rgn; x->s->v[V_BITMAP] = (LONGLONG)(ULONG_PTR)bmp;
    x->s->v[V_DIB] = (LONGLONG)(ULONG_PTR)dib; x->s->v[V_DC] = (LONGLONG)(ULONG_PTR)dc;
    x->s->v[V_HW1] = (LONGLONG)(ULONG_PTR)w1; x->s->v[V_HW2] = (LONGLONG)(ULONG_PTR)w2;
    x->s->v[V_HOOK] = (LONGLONG)(ULONG_PTR)hk;
    x->s->v[V_NGDI] = psc_gr(PSC_SELF, PSC_GDI);
    x->s->v[V_NUSER] = psc_gr(PSC_SELF, PSC_USER);
    x->s->v[V_TID] = GetCurrentThreadId();
    x->s->v[V_HKOK] = hkok;
    for (int i = 0; i < NSTOCK; i++) x->s->v[V_STOCK + i] = (LONGLONG)(ULONG_PTR)U(GetStockObject)(i);
    strcpy(x->s->s[0], t1);
    sprintf(x->s->s[1], "win32kt_psc_%lu", GetCurrentProcessId());
    if (!psc_sync_pump(x)) return;
    /* the parent may have attacked our handles: everything must still be exactly as created */
    CC(dc && U(GetBkColor)(dc) == 0x123456);
    CC(dc && U(GetBkMode)(dc) == TRANSPARENT);
    CC(U(GetObjectType)(br) == OBJ_BRUSH);
    CC(U(GetObjectType)(pen) == OBJ_PEN);
    CC(U(GetObjectType)(rgn) == OBJ_REGION);
    CC(U(GetObjectType)(bmp) == OBJ_BITMAP);
    CC(dc && U(GetObjectType)(dc) == OBJ_MEMDC);
    CC(U(IsWindow)(w1) && U(IsWindow)(w2));
    CC(psc_gr(PSC_SELF, PSC_GDI) == (DWORD)x->s->v[V_NGDI]);
    CC(psc_gr(PSC_SELF, PSC_USER) == (DWORD)x->s->v[V_NUSER]);
    { LOGBRUSH lb; CC(U(GetObjectA)(br, sizeof lb, &lb) == sizeof lb && lb.lbColor == RGB(10, 20, 30)); }
    { char tt[64] = ""; U(GetWindowTextA)(w1, tt, sizeof tt); CC(!strcmp(tt, t1)); }
    if (!strcmp(x->extra, "exit")) ExitProcess(0);
    if (!strcmp(x->extra, "killself")) TerminateProcess(GetCurrentProcess(), 88);
    /* "return" and "kill" fall out here (kill never gets this far) */
}

static void leak30(struct psc_th *t) {
    struct { HGDIOBJ h[30]; } *lk = t->user;
    for (int i = 0; i < 30; i++) lk->h[i] = (i % 3 == 0) ? (HGDIOBJ)U(CreateSolidBrush)(RGB(i, 0, 0)) : (i % 3 == 1) ? (HGDIOBJ)U(CreateRectRgn)(0, 0, 2, 2) : (HGDIOBJ)U(CreateBitmap)(2, 2, 1, 1, NULL);
    psc_park(t);
}

SCEN(leakthread) {                       /* a short-lived thread leaks GDI objects; nobody ever frees them */
    psc_warm();
    struct psc_drain dr; psc_drain_take(&dr);        /* no cache hits: the 30 leaked objects are 30 fresh handles */
    DWORD g0 = psc_gr(PSC_SELF, PSC_GDI);
    struct { HGDIOBJ h[30]; } *lk = calloc(1, sizeof *lk);
    struct psc_th t;
    if (!psc_th_start(&t, leak30, lk)) { psc_cfail(__FILE__, __LINE__, "thread start"); return; }
    psc_th_end(&t);
    Sleep(50);
    DWORD g1 = psc_gr(PSC_SELF, PSC_GDI);
    CC(g1 == g0 + 30);                   /* still counted after the creating thread is gone */
    x->s->v[0] = g0; x->s->v[1] = g1;
    for (int i = 0; i < 30; i++) x->s->v[8 + i] = (LONGLONG)(ULONG_PTR)lk->h[i];
    psc_sync(x);
}
SCEN(handlecount) {                      /* GDI/USER objects are not NT handles: the process handle count must not move */
    psc_warm();
    HBRUSH b0 = U(CreateSolidBrush)(0); HRGN r0 = U(CreateRectRgn)(0, 0, 1, 1); HBITMAP m0 = U(CreateBitmap)(1, 1, 1, 1, NULL);
    DWORD h0 = 0, h1 = 0, h2 = 0;
    CC(GetProcessHandleCount(GetCurrentProcess(), &h0));
    x->s->v[0] = h0;
    if (!psc_sync(x)) return;
    HBRUSH b[200]; HRGN r[100]; HBITMAP m[10]; HWND w[20];
    for (int i = 0; i < 200; i++) b[i] = U(CreateSolidBrush)(i);
    for (int i = 0; i < 100; i++) r[i] = U(CreateRectRgn)(0, 0, 2, 2);
    for (int i = 0; i < 10; i++) m[i] = U(CreateBitmap)(2, 2, 1, 1, NULL);
    for (int i = 0; i < 20; i++) w[i] = psc_mk(NULL, "hc");
    CC(GetProcessHandleCount(GetCurrentProcess(), &h1));
    x->s->v[1] = h1;
    if (!psc_sync(x)) return;
    for (int i = 0; i < 200; i++) U(DeleteObject)(b[i]);
    for (int i = 0; i < 100; i++) U(DeleteObject)(r[i]);
    for (int i = 0; i < 10; i++) U(DeleteObject)(m[i]);
    for (int i = 0; i < 20; i++) if (w[i]) U(DestroyWindow)(w[i]);
    CC(GetProcessHandleCount(GetCurrentProcess(), &h2));
    x->s->v[2] = h2;
    psc_sync(x);
    U(DeleteObject)(b0); U(DeleteObject)(r0); U(DeleteObject)(m0);
}

/* ---- parent: state snapshot helpers --------------------------------------------------------- */
struct owner { struct psc_ch c; HWND w1, w2; HGDIOBJ g[6]; HHOOK hk; DWORD pid; };
static int owner_start(struct owner *o, const char *mode) {
    memset(o, 0, sizeof *o);
    if (!psc_spawn(&o->c, "owner", mode)) { t_skip("spawn failed"); psc_free(&o->c); return 0; }
    if (!psc_wait(&o->c, PSC_WAIT)) { CHECKF(0, "owner child never reached its sync point: %s", o->c.s->msg); psc_free(&o->c); return 0; }
    o->pid = o->c.pid;
    o->w1 = (HWND)(ULONG_PTR)o->c.s->v[V_HW1]; o->w2 = (HWND)(ULONG_PTR)o->c.s->v[V_HW2];
    for (int i = 0; i < 6; i++) o->g[i] = (HGDIOBJ)(ULONG_PTR)o->c.s->v[V_BRUSH + i];
    o->hk = (HHOOK)(ULONG_PTR)o->c.s->v[V_HOOK];
    return 1;
}
static const char *const gname[6] = { "brush", "pen", "rgn", "bitmap", "dib", "dc" };

struct enum_ctx { HWND a, b; int found; int titled; DWORD pid; };
static BOOL CALLBACK enum_cb(HWND h, LPARAM l) {
    struct enum_ctx *e = (struct enum_ctx *)l;
    if (h == e->a || h == e->b) e->found++;
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(h, &pid);
    if (pid == e->pid) e->titled++;
    return TRUE;
}
static int enum_find(struct owner *o, int *by_pid) {
    struct enum_ctx e = { o->w1, o->w2, 0, 0, o->pid };
    U(EnumWindows)(enum_cb, (LPARAM)&e);
    if (by_pid) *by_pid = e.titled;
    return e.found;
}

/* ---- 1. process exit grid: 4 ways to die x 7 aspects ---------------------------------------- */
enum { M_RETURN, M_EXIT, M_KILLSELF, M_KILL, M_COUNT };
static const char *const mname[M_COUNT] = { "return", "exit", "killself", "kill" };
enum { A_GDI, A_WINDOWS, A_ENUM, A_DEADAPI, A_GUIRES, A_EXITCODE, A_RECYCLE, A_COUNT };

static void exit_case(int mode, int aspect) {
    NEED_UI();
    struct owner o;
    if (!owner_start(&o, mode == M_KILL ? "return" : mname[mode])) return;
    unsigned pid = o.pid;
    /* alive-state invariants for the aspect */
    int n_gdi = (int)o.c.s->v[V_NGDI];
    if (aspect == A_GDI) {
        for (int i = 0; i < 6; i++) {
            unsigned owner = 0;
            CHECKF(psc_cell_live(o.g[i], &owner) && owner == pid, "%s: cell not live/owned by child pid %u (owner %u)", gname[i], pid, owner);
            CHECKF(U(GetObjectType)(o.g[i]) == 0, "%s: foreign handle must not validate in this process", gname[i]);
        }
        { int owned = psc_count_owned(pid); DWORD cnt = psc_gr(o.c.hp, PSC_GDI);
          /* the W32PROCESS counter and the number of cells carrying the owner pid are kept by different code paths; only the lower bound is proven */
          CHECKF(owned > 0 && cnt > 0, "owned cells %d, GetGuiResources %lu", owned, cnt);
          if (owned != (int)cnt) t_note("cells owned by child %d, GetGuiResources(GDI) %lu", owned, cnt); }
        CHECK(psc_gr(o.c.hp, PSC_GDI) == (DWORD)n_gdi);
    } else if (aspect == A_WINDOWS || aspect == A_ENUM) {
        CHECK(U(IsWindow)(o.w1) && U(IsWindow)(o.w2));
        CHECK(psc_user_live(o.w1) && psc_user_live(o.w2));
        int bypid = 0;
        CHECK(enum_find(&o, &bypid) == 2);
        CHECK(bypid >= 2);
        CHECK(U(FindWindowA)(o.c.s->s[1], o.c.s->s[0]) != NULL || U(FindWindowA)(NULL, o.c.s->s[0]) != NULL);
    }
    if (mode == M_KILL) { TerminateProcess(o.c.hp, 5); }
    else psc_go(&o.c);
    CHECK(WaitForSingleObject(o.c.hp, PSC_WAIT) == WAIT_OBJECT_0);
    DWORD code = 0xFFFF;
    GetExitCodeProcess(o.c.hp, &code);
    static const DWORD want[M_COUNT] = { 0, 0, 88, 5 };
    if (aspect == A_EXITCODE) {
        CHECKF(code == want[mode], "exit code %lu want %lu", code, want[mode]);
        CHECKF(o.c.s->fails == 0 || mode == M_KILL, "child self-check failed: %s", o.c.s->msg);
    }
    switch (aspect) {
    case A_GDI: {
        /* GdiProcessCleanup: every handle owned by the pid is deleted, in the shared table and in the parent's view */
        CHECKF(PSC_POLL(psc_count_owned(pid) == 0, 5000), "%d cells still owned by the dead pid", psc_count_owned(pid));
        for (int i = 0; i < 6; i++) {
            unsigned owner = 0;
            CHECKF(!psc_cell_live(o.g[i], &owner) || owner != pid, "%s: cell still live and owned by dead pid", gname[i]);
            /* GetObjectType keeps answering from the type bits of an unreused handle value, so it cannot tell; the freed
             * object itself is gone and cannot be deleted by anybody */
            CHECKF(!(i == V_DC ? U(DeleteDC)((HDC)o.g[i]) : U(DeleteObject)(o.g[i])), "%s: dead process's object can still be deleted", gname[i]);
        }
        break;
    }
    case A_WINDOWS: {
        CHECK(PSC_POLL(!U(IsWindow)(o.w1) && !U(IsWindow)(o.w2), 5000));
        CHECK(!psc_user_live(o.w1) && !psc_user_live(o.w2));
        CHECK(k_wnd_ptr(o.w1) == NULL && k_wnd_ptr(o.w2) == NULL);
        break;
    }
    case A_ENUM: {
        int bypid = -1;
        int ok = PSC_POLL(enum_find(&o, &bypid) == 0 && bypid == 0, 5000);
        CHECKF(ok, "EnumWindows still reports %d child window(s), %d by pid", enum_find(&o, NULL), bypid);
        HWND w = NULL; int seen = 0;
        while ((w = U(FindWindowExA)(NULL, w, o.c.s->s[1], NULL)) != NULL) seen++;
        CHECKF(seen == 0, "FindWindowEx still finds %d window(s) of the dead class", seen);
        break;
    }
    case A_DEADAPI: {
        PSC_POLL(!U(IsWindow)(o.w1), 5000);
        DWORD pidout = 0x1234;
        SetLastError(PSC_MAGIC);
        CHECK(U(GetWindowThreadProcessId)(o.w1, &pidout) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SetLastError(PSC_MAGIC);
        CHECK(U(SendMessageA)(o.w1, WM_NULL, 0, 0) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SetLastError(PSC_MAGIC);
        CHECK(!U(PostMessageA)(o.w1, WM_NULL, 0, 0));
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SetLastError(PSC_MAGIC);
        CHECK(!U(DestroyWindow)(o.w2));
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);       /* handle validation fails before the cross-thread check that gives ERROR_ACCESS_DENIED */
        CHECK(!U(IsHungAppWindow)(o.w1));
        CHECK(!U(IsWindowVisible)(o.w2));
        SetLastError(PSC_MAGIC);
        CHECK(!U(UnhookWindowsHookEx)(o.hk));
        break;
    }
    case A_GUIRES: {
        DWORD v = 1;
        CHECKF(PSC_POLL((v = psc_gr(o.c.hp, PSC_GDI)) == 0, 5000), "GetGuiResources(dead) still %lu", v);
        SetLastError(PSC_MAGIC);
        CHECK(psc_gr(o.c.hp, PSC_USER) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        break;
    }
    case A_RECYCLE: {
        /* old handle values stay dead after the slot is reused: freeing the object bumps the uniqueness word */
        PSC_POLL(psc_count_owned(pid) == 0, 5000);
        HBRUSH nb[256];
        int reused = 0;
        for (int i = 0; i < 256; i++) {
            nb[i] = U(CreateSolidBrush)(RGB(i, 1, 1));
            for (int k = 0; k < 6; k++) {
                if (nb[i] && ((ULONG_PTR)nb[i] & 0xffff) == ((ULONG_PTR)o.g[k] & 0xffff)) {
                    reused++;
                    CHECKF(nb[i] != (HBRUSH)o.g[k], "slot %lx reused with the identical handle value", (unsigned long)((ULONG_PTR)nb[i] & 0xffff));
                }
            }
        }
        for (int k = 0; k < 6; k++) CHECKF(U(GetObjectType)(o.g[k]) == 0, "%s: stale handle validates after slot reuse", gname[k]);
        HWND nw[64]; int wreused = 0;
        for (int i = 0; i < 64; i++) {
            nw[i] = psc_mk(NULL, "r");
            if (nw[i] && ((ULONG_PTR)nw[i] & 0xffff) == ((ULONG_PTR)o.w1 & 0xffff)) { wreused++; CHECK(nw[i] != o.w1); }
            if (nw[i] && ((ULONG_PTR)nw[i] & 0xffff) == ((ULONG_PTR)o.w2 & 0xffff)) { wreused++; CHECK(nw[i] != o.w2); }
        }
        CHECK(!U(IsWindow)(o.w1));
        CHECK(!U(IsWindow)(o.w2));
        t_note("%s: %d GDI slot(s) and %d window slot(s) of the dead process were reused", mname[mode], reused, wreused);
        for (int i = 0; i < 256; i++) if (nb[i]) U(DeleteObject)(nb[i]);
        for (int i = 0; i < 64; i++) if (nw[i]) U(DestroyWindow)(nw[i]);
        break;
    }
    }
    psc_free(&o.c);
}
#define EX_T(m, a, mm, an) TEST(process_session_cleanup_exit_##mm##_##an) { exit_case(m, a); }
#define EX_ROW(m, mm) EX_T(m, A_GDI, mm, gdi) EX_T(m, A_WINDOWS, mm, windows) EX_T(m, A_ENUM, mm, enumwindows) EX_T(m, A_DEADAPI, mm, deadapi) \
    EX_T(m, A_GUIRES, mm, guires) EX_T(m, A_EXITCODE, mm, exitcode) EX_T(m, A_RECYCLE, mm, recycle)
EX_ROW(M_RETURN, return) EX_ROW(M_EXIT, exitprocess) EX_ROW(M_KILLSELF, terminateself) EX_ROW(M_KILL, terminated)

/* ---- 2. leaked objects of a dead thread are freed at process exit ------------------------------ */
TEST(process_session_cleanup_exit_frees_objects_leaked_by_dead_thread) {
    NEED_UI();
    struct psc_ch c;
    PSC_START(c, "leakthread", "");
    DWORD g_now = psc_gr(c.hp, PSC_GDI);
    CHECKF(g_now == (DWORD)c.s->v[1], "parent %lu child %llu", g_now, (unsigned long long)c.s->v[1]);
    CHECK(c.s->v[1] == c.s->v[0] + 30);
    int live = 0;
    for (int i = 0; i < 30; i++) { unsigned o = 0; if (psc_cell_live((HGDIOBJ)(ULONG_PTR)c.s->v[8 + i], &o) && o == c.pid) live++; }
    CHECKF(live == 30, "%d of 30 leaked cells owned by the child while it lives", live);
    PSC_END(c);
    CHECK(PSC_POLL(psc_count_owned(c.pid) == 0, 5000));
    live = 0;
    for (int i = 0; i < 30; i++) { unsigned o = 0; if (psc_cell_live((HGDIOBJ)(ULONG_PTR)c.s->v[8 + i], &o) && o == c.pid) live++; }
    CHECKF(live == 0, "%d leaked cells survive the process", live);
    psc_free(&c);
}

/* ---- 3. NT handle count is unaffected by GDI/USER objects ------------------------------------ */
TEST(process_session_cleanup_handlecount_unmoved_by_gdi_and_user_objects) {
    NEED_UI();
    struct psc_ch c;
    PSC_START(c, "handlecount", "");
    DWORD p0 = 0, p1 = 0, p2 = 0;
    CHECK(GetProcessHandleCount(c.hp, &p0));
    PSC_STEP(c);
    CHECK(GetProcessHandleCount(c.hp, &p1));
    PSC_STEP(c);
    CHECK(GetProcessHandleCount(c.hp, &p2));
    CHECKF(c.s->v[0] == c.s->v[1] && c.s->v[1] == c.s->v[2], "child saw handle counts %llu / %llu / %llu", (unsigned long long)c.s->v[0], (unsigned long long)c.s->v[1], (unsigned long long)c.s->v[2]);
    CHECKF(p0 == p1 && p1 == p2, "parent saw %lu / %lu / %lu", p0, p1, p2);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_handlecount_own_process_gdi_objects) {
    NEED_UI();
    psc_warm_process();
    HBRUSH w = U(CreateSolidBrush)(0); HRGN w2 = U(CreateRectRgn)(0, 0, 1, 1); HBITMAP w3 = U(CreateBitmap)(1, 1, 1, 1, NULL);
    DWORD a = 0, b = 0;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &a));
    HBRUSH bs[200]; HRGN rs[100];
    for (int i = 0; i < 200; i++) bs[i] = U(CreateSolidBrush)(i);
    for (int i = 0; i < 100; i++) rs[i] = U(CreateRectRgn)(0, 0, 2, 2);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &b));
    CHECKF(a == b, "handle count %lu -> %lu", a, b);
    for (int i = 0; i < 200; i++) U(DeleteObject)(bs[i]);
    for (int i = 0; i < 100; i++) U(DeleteObject)(rs[i]);
    U(DeleteObject)(w); U(DeleteObject)(w2); U(DeleteObject)(w3);
}

/* ---- 4. cross-process use of another process's handles ---------------------------------------- */
#define OWNER_ATTACK(name, ...) TEST(process_session_cleanup_xproc_##name) { NEED_UI(); struct owner o; if (!owner_start(&o, "return")) return; \
    HDC mydc = U(CreateCompatibleDC)(NULL); HBRUSH mybr = U(CreateSolidBrush)(RGB(9, 9, 9)); \
    __VA_ARGS__ \
    if (mybr) { U(DeleteObject)(mybr); } if (mydc) { U(DeleteDC)(mydc); } \
    PSC_END(o.c); psc_free(&o.c); }

OWNER_ATTACK(gdi_object_type_zero_for_every_kind, {
    for (int i = 0; i < 6; i++) CHECKF(U(GetObjectType)(o.g[i]) == 0, "%s", gname[i]);
})
OWNER_ATTACK(select_foreign_brush_into_own_dc, {
    REQUIRE(mydc);
    HGDIOBJ before = U(GetCurrentObject)(mydc, OBJ_BRUSH);
    SetLastError(PSC_MAGIC);
    HGDIOBJ r = U(SelectObject)(mydc, o.g[V_BRUSH]);
    CHECK(r == NULL);
    CHECK(U(GetCurrentObject)(mydc, OBJ_BRUSH) == before);       /* the DC's brush slot is untouched */
    t_note("SelectObject(own DC, foreign brush) last error %lu", GetLastError());
})
OWNER_ATTACK(select_foreign_pen_bitmap_rgn, {
    REQUIRE(mydc);
    HGDIOBJ pb = U(GetCurrentObject)(mydc, OBJ_PEN), bb = U(GetCurrentObject)(mydc, OBJ_BITMAP);
    CHECK(U(SelectObject)(mydc, o.g[V_PEN]) == NULL);
    CHECK(U(SelectObject)(mydc, o.g[V_BITMAP]) == NULL);
    CHECK(U(SelectObject)(mydc, o.g[V_DIB]) == NULL);
    CHECK(U(GetCurrentObject)(mydc, OBJ_PEN) == pb);
    CHECK(U(GetCurrentObject)(mydc, OBJ_BITMAP) == bb);
    CHECK(U(GetClipBox)(mydc, &(RECT){0}) >= 0);
})
OWNER_ATTACK(select_own_brush_into_foreign_dc, {
    SetLastError(PSC_MAGIC);
    CHECK(U(SelectObject)((HDC)o.g[V_DC], mybr) == NULL);
    t_note("SelectObject(foreign DC, own brush) last error %lu", GetLastError());
})
OWNER_ATTACK(delete_foreign_objects_refused, {
    for (int i = 0; i < 5; i++) {
        SetLastError(PSC_MAGIC);
        CHECKF(!U(DeleteObject)(o.g[i]), "DeleteObject(foreign %s) must fail (owner pid != caller pid)", gname[i]);
    }
    CHECK(!U(DeleteDC)((HDC)o.g[V_DC]));
    /* the child's count is untouched (the child re-checks all of it after the go) */
    CHECK(psc_gr(o.c.hp, PSC_GDI) == (DWORD)o.c.s->v[V_NGDI]);
    for (int i = 0; i < 6; i++) { unsigned ow = 0; CHECKF(psc_cell_live(o.g[i], &ow) && ow == o.pid, "%s cell no longer owned by the child", gname[i]); }
})
OWNER_ATTACK(getobject_foreign_brush, {
    LOGBRUSH lb; memset(&lb, 0x5a, sizeof lb);
    SetLastError(PSC_MAGIC);
    CHECK(U(GetObjectA)(o.g[V_BRUSH], sizeof lb, &lb) == 0);
    /* a size query on a bitmap handle is answered for a foreign bitmap too (sizeof(BITMAP)); the structures of brushes and
     * pens of another process are refused */
    CHECK(U(GetObjectA)(o.g[V_BITMAP], 0, NULL) == (int)sizeof(BITMAP));
    CHECK(U(GetObjectA)(o.g[V_PEN], sizeof(LOGPEN), &(LOGPEN){0}) == 0);
})
OWNER_ATTACK(foreign_dc_attribute_calls_rejected, {
    HDC fd = (HDC)o.g[V_DC];
    CHECK(U(GetBkMode)(fd) == 0);                      /* gdi32full!GetBkMode: owner(pid) mismatch -> 0, no kernel call */
    t_note("GetBkColor(foreign DC) = 0x%lx", (unsigned long)U(GetBkColor)(fd));
    CHECK(U(SetBkColor)(fd, 0xff) == CLR_INVALID);
    CHECK(U(SetBkMode)(fd, OPAQUE) == 0);
    CHECK(U(GetPixel)(fd, 0, 0) == CLR_INVALID);
    CHECK(U(GetDeviceCaps)(fd, HORZRES) == 0);
    CHECK(U(SaveDC)(fd) == 0);
    CHECK(!U(MoveToEx)(fd, 1, 1, NULL));
})
OWNER_ATTACK(foreign_dc_blt_rejected, {
    REQUIRE(mydc);
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 1, NULL);
    HGDIOBJ old = U(SelectObject)(mydc, bm);
    CHECK(!U(BitBlt)(mydc, 0, 0, 2, 2, (HDC)o.g[V_DC], 0, 0, SRCCOPY));
    CHECK(!U(BitBlt)((HDC)o.g[V_DC], 0, 0, 2, 2, mydc, 0, 0, SRCCOPY));
    CHECK(!U(PatBlt)((HDC)o.g[V_DC], 0, 0, 2, 2, BLACKNESS));
    U(SelectObject)(mydc, old);
    U(DeleteObject)(bm);
})
OWNER_ATTACK(foreign_dc_does_not_alias_stock_state, {
    HDC fd = (HDC)o.g[V_DC];
    CHECK(U(GetObjectType)(fd) == 0);
    CHECK(U(GetCurrentObject)(fd, OBJ_BRUSH) == NULL);
    CHECK(U(GetCurrentObject)(fd, OBJ_BITMAP) == NULL);
})
OWNER_ATTACK(stock_objects_shared_between_processes, {
    int same = 0;
    for (int i = 0; i < NSTOCK; i++) {
        HGDIOBJ mine = U(GetStockObject)(i);
        HGDIOBJ theirs = (HGDIOBJ)(ULONG_PTR)o.c.s->v[V_STOCK + i];
        if (mine == theirs) same++;
        else if (mine && theirs) t_note("stock object %d differs: %p vs %p", i, mine, theirs);
    }
    /* stock objects carry the stock bit (0x800000) and owner 0 in the shared table: one handle value session-wide */
    CHECKF(same >= 16, "only %d of %d stock handles identical across processes", same, NSTOCK);
})
OWNER_ATTACK(stock_object_of_other_process_selectable, {
    REQUIRE(mydc);
    HGDIOBJ theirs = (HGDIOBJ)(ULONG_PTR)o.c.s->v[V_STOCK + BLACK_BRUSH];
    HGDIOBJ old = U(SelectObject)(mydc, theirs);
    CHECK(old != NULL);
    CHECK(U(GetCurrentObject)(mydc, OBJ_BRUSH) == theirs);
    U(SelectObject)(mydc, old);
    t_note("DeleteObject(stock object of another process) = %d", U(DeleteObject)(theirs));   /* NtGdiDeleteObjectApp: stock bit -> returns 1 without deleting */
    CHECK(U(GetObjectType)(theirs) == OBJ_BRUSH);
})
OWNER_ATTACK(foreign_cell_fields, {
    struct psc_cell c;
    REQUIRE(psc_cell_of(o.g[V_BRUSH], &c));
    CHECK((c.owner & ~1u) == o.pid);
    CHECK(c.upper == (unsigned short)((ULONG_PTR)o.g[V_BRUSH] >> 16));
    CHECK(c.objt != 0);
    struct psc_cell d;
    REQUIRE(psc_cell_of(mybr, &d));
    CHECK((d.owner & ~1u) == GetCurrentProcessId());
    CHECK(c.objt == d.objt);                       /* both are brushes */
    CHECK(((ULONG_PTR)o.g[V_BRUSH] & 0xffff) != ((ULONG_PTR)mybr & 0xffff));
})

/* user32 side of cross-process use */
OWNER_ATTACK(window_visible_through_shared_table, {
    CHECK(U(IsWindow)(o.w1));
    CHECK(psc_user_live(o.w1));
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(o.w1, &pid);
    CHECK(pid == o.pid);
    CHECKF(tid == (DWORD)o.c.s->v[V_TID], "tid %lu, child main thread %llu", tid, (unsigned long long)o.c.s->v[V_TID]);
    ULONG_PTR ot = 0;
    CHECK(psc_user_owner_tid(o.w1, &ot) && ot == tid);
    NtUserQueryWindow_pfn q = UN(NtUserQueryWindow);
    if (q && !t_is_wine()) { CHECK(q(o.w1, 0) == o.pid); CHECK(q(o.w1, 2) == tid); CHECK(q(o.w1, 1) == o.pid); }
})
OWNER_ATTACK(class_and_text_readable_across_processes, {
    char cls[64] = "", txt[64] = "";
    CHECK(U(GetClassNameA)(o.w1, cls, sizeof cls) > 0);
    CHECK(!strcmp(cls, o.c.s->s[1]));
    CHECK(U(GetWindowTextA)(o.w1, txt, sizeof txt) > 0);
    CHECK(!strcmp(txt, o.c.s->s[0]));
})
OWNER_ATTACK(destroy_foreign_window_access_denied, {
    SetLastError(PSC_MAGIC);
    CHECK(!U(DestroyWindow)(o.w2));
    EXPECT_ERR(ERROR_ACCESS_DENIED);                  /* NtUserDestroyWindow: the caller's thread must own the window */
    CHECK(U(IsWindow)(o.w2));
    CHECK(psc_user_live(o.w2));
})
OWNER_ATTACK(subclass_foreign_window_access_denied, {
    SetLastError(PSC_MAGIC);
    LONG_PTR r = U(SetWindowLongPtrA)(o.w1, GWLP_WNDPROC, (LONG_PTR)psc_wndproc);
    CHECK(r == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);                  /* GWLP_WNDPROC of another process's window cannot be changed */
})
OWNER_ATTACK(foreign_hook_handle_can_be_unhooked, {
    SetLastError(PSC_MAGIC);
    struct k_user_entry e;
    if (k_user_entry(o.hk, &e)) CHECK(e.type == 5);   /* HMValidateHandle(h, 5): the child's hook is a hook object in our view too */
    /* Windows 10 does not tie UnhookWindowsHookEx to the owning process: the hook of the child (a thread hook of its
     * main thread) is removed, and the child then holds one USER object less than it recorded. */
    BOOL r = U(UnhookWindowsHookEx)(o.hk);
    CHECK(r);
    EXPECT_ERR(PSC_MAGIC);
    if (r) { CHECK(psc_gr(o.c.hp, PSC_USER) == (DWORD)o.c.s->v[V_NUSER] - 1); o.c.s->v[V_NUSER] -= 1; }
})
OWNER_ATTACK(foreign_hotkey_conflict_while_alive, {
    if (!o.c.s->v[V_HKOK]) { t_skip("child could not register the hotkey"); } else {
        SetLastError(PSC_MAGIC);
        CHECK(!U(RegisterHotKey)(NULL, 0x78, HK_MODS, HK_VK));
        EXPECT_ERR(ERROR_HOTKEY_ALREADY_REGISTERED);
    }
})
OWNER_ATTACK(foreign_window_not_hung_while_child_pumps, {
    CHECK(!U(IsHungAppWindow)(o.w1));
    CHECK(!U(IsHungAppWindow)(o.w2));
    DWORD_PTR res = 0;
    CHECK(U(SendMessageTimeoutA)(o.w1, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 3000, &res) != 0);
})
OWNER_ATTACK(foreign_window_receives_cross_process_message, {
    SetLastError(PSC_MAGIC);
    LRESULT r = U(SendMessageA)(o.w1, PSC_M_TID, 0, 0);
    CHECKF((DWORD)r == (DWORD)o.c.s->v[V_TID], "reply %llu, child tid %llu", (unsigned long long)r, (unsigned long long)o.c.s->v[V_TID]);
})

/* hotkey ownership ends with the owning process */
TEST(process_session_cleanup_exit_releases_hotkey) {
    NEED_UI();
    struct owner o;
    if (!owner_start(&o, "return")) return;
    if (!o.c.s->v[V_HKOK]) { t_skip("child could not register the hotkey"); PSC_END(o.c); psc_free(&o.c); return; }
    CHECK(!U(RegisterHotKey)(NULL, 0x79, HK_MODS, HK_VK));
    PSC_END(o.c);
    BOOL ok = FALSE;
    CHECKF(PSC_POLL((ok = U(RegisterHotKey)(NULL, 0x79, HK_MODS, HK_VK)) != 0, 5000), "hotkey still registered after the owner exited (error %lu)", GetLastError());
    if (ok) U(UnregisterHotKey)(NULL, 0x79);
    psc_free(&o.c);
}

/* ---- 5. session / window station / desktop of the child equal the parent's ---------------------- */
SCEN(sessinfo) {
    DWORD sid = 0xFFFF;
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    x->s->v[0] = sid;
    unsigned long peb_sid = 0xFFFF;
    k_try_read_u32((char *)k_peb() + PEB_SessionId, &peb_sid);
    x->s->v[1] = peb_sid;
    HWINSTA ws = U(GetProcessWindowStation)();
    HDESK dk = U(GetThreadDesktop)(GetCurrentThreadId());
    DWORD need = 0;
    x->s->s[0][0] = 0; x->s->s[1][0] = 0;
    CC(ws != NULL && U(GetUserObjectInformationA)(ws, UOI_NAME, x->s->s[0], sizeof x->s->s[0], &need));
    CC(dk != NULL && U(GetUserObjectInformationA)(dk, UOI_NAME, x->s->s[1], sizeof x->s->s[1], &need));
    HWND w = psc_mk(NULL, "sess");
    x->s->v[2] = (LONGLONG)(ULONG_PTR)w;
    x->s->v[3] = (LONGLONG)(ULONG_PTR)ws;
    x->s->v[4] = (LONGLONG)(ULONG_PTR)dk;
    HDESK id = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    if (id) { U(GetUserObjectInformationA)(id, UOI_NAME, x->s->s[2], sizeof x->s->s[2], &need); U(CloseDesktop)(id); }
    psc_sync_pump(x);
}
static void sess_case(int what) {
    NEED_UI();
    struct psc_ch c;
    PSC_START(c, "sessinfo", "");
    char wsn[96] = "", dkn[96] = "";
    DWORD need = 0, sid = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    unsigned long peb_sid = 0;
    k_try_read_u32((char *)k_peb() + PEB_SessionId, &peb_sid);
    HWINSTA ws = U(GetProcessWindowStation)();
    HDESK dk = U(GetThreadDesktop)(GetCurrentThreadId());
    U(GetUserObjectInformationA)(ws, UOI_NAME, wsn, sizeof wsn, &need);
    U(GetUserObjectInformationA)(dk, UOI_NAME, dkn, sizeof dkn, &need);
    switch (what) {
    case 0: CHECKF(c.s->v[0] == sid, "child session %llu parent %lu", (unsigned long long)c.s->v[0], sid); break;
    case 1: CHECKF(c.s->v[1] == peb_sid, "child PEB.SessionId %llu parent %lu", (unsigned long long)c.s->v[1], peb_sid); CHECK(peb_sid == sid); break;
    case 2: CHECKF(!strcmp(c.s->s[0], wsn), "child window station '%s' parent '%s'", c.s->s[0], wsn); CHECK(wsn[0] != 0); break;
    case 3: CHECKF(!strcmp(c.s->s[1], dkn), "child desktop '%s' parent '%s'", c.s->s[1], dkn); CHECK(dkn[0] != 0); break;
    case 4: {
        /* the child's window lives on the desktop this thread is attached to: it shows up in EnumDesktopWindows of our desktop */
        struct enum_ctx e = { (HWND)(ULONG_PTR)c.s->v[2], NULL, 0, 0, c.pid };
        U(EnumDesktopWindows)(dk, enum_cb, (LPARAM)&e);
        CHECKF(e.found == 1, "child window found %d time(s) on desktop '%s'", e.found, dkn);
        break;
    }
    case 5: {
        /* GetGuiResources only answers for processes of the caller's session (PsGetProcessSessionId == gSessionId) */
        CHECK(psc_gr(c.hp, PSC_USER) >= 1);
        break;
    }
    case 6: {
        HWND w = (HWND)(ULONG_PTR)c.s->v[2];
        CHECK(U(IsWindow)(w));
        CHECK(U(GetWindowThreadProcessId)(w, NULL) != 0);
        CHECK(U(IsWindowVisible)(w) == FALSE);          /* created without WS_VISIBLE */
        break;
    }
    case 7: {
        /* the thread desktop of the child is a different HANDLE value space: its numeric handle means nothing here */
        HDESK theirs = (HDESK)(ULONG_PTR)c.s->v[4];
        if (theirs != dk) {
            DWORD fl = 0;
            if (!GetHandleInformation(theirs, &fl)) t_note("child desktop handle %p is not open in the parent", theirs);
        }
        CHECK(c.s->v[3] != 0);
        break;
    }
    }
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_session_id_equal) { sess_case(0); }
TEST(process_session_cleanup_session_peb_session_id_equal) { sess_case(1); }
TEST(process_session_cleanup_session_window_station_name_equal) { sess_case(2); }
TEST(process_session_cleanup_session_desktop_name_equal) { sess_case(3); }
TEST(process_session_cleanup_session_child_window_on_parent_desktop) { sess_case(4); }
TEST(process_session_cleanup_session_getguiresources_same_session) { sess_case(5); }
TEST(process_session_cleanup_session_child_window_hidden_by_default) { sess_case(6); }
TEST(process_session_cleanup_session_child_handles_are_process_local) { sess_case(7); }
TEST(process_session_cleanup_session_input_desktop_name_note) {
    NEED_UI();
    struct psc_ch c;
    PSC_START(c, "sessinfo", "");
    t_note("input desktop as seen by the child: '%s'", c.s->s[2]);
    PSC_END(c); psc_free(&c);
}

/* ---- 6. TerminateProcess of a GUI child that is inside a SendMessage to the parent --------------- */
SCEN(sendtoparent) {                       /* extra = decimal HWND of a parent window */
    HWND ph = (HWND)(ULONG_PTR)_strtoui64(x->extra, NULL, 10);
    HWND mine = psc_mk(NULL, "stp");
    x->s->v[0] = (LONGLONG)(ULONG_PTR)mine;
    x->s->v[1] = GetCurrentThreadId();
    if (!psc_sync_pump(x)) return;
    /* the parent's window procedure terminates this process while the call below is pending */
    LRESULT r = U(SendMessageA)(ph, PSC_M_KILLPEER, 0, 0);
    x->s->v[2] = (LONGLONG)r;               /* never reached when the kill works */
    x->s->v[3] = 1;
    psc_sync_pump(x);
}
SCEN(relaytarget) {                         /* a window that relays a message to the parent's window from inside its window procedure */
    HWND mine = psc_mk(NULL, "rel");
    x->s->v[0] = (LONGLONG)(ULONG_PTR)mine;
    x->s->v[1] = GetCurrentThreadId();
    DWORD t0 = GetTickCount();
    psc_sync_pump(x);
    (void)t0;
}
static void with_parent_window(void (*body)(HWND, struct psc_ch *), const char *scen) {
    HWND ph = psc_mk(NULL, "parent-target");
    if (!ph) { t_skip("no window"); return; }
    char arg[32];
    sprintf(arg, "%llu", (unsigned long long)(ULONG_PTR)ph);
    struct psc_ch c;
    if (!psc_spawn(&c, scen, arg)) { t_skip("spawn"); psc_free(&c); U(DestroyWindow)(ph); return; }
    if (!psc_wait(&c, PSC_WAIT)) { CHECKF(0, "child never synced: %s", c.s->msg); psc_free(&c); U(DestroyWindow)(ph); return; }
    psc_kill_h = c.hp;
    body(ph, &c);
    psc_kill_h = NULL;
    psc_free(&c);
    U(DestroyWindow)(ph);
}
static void body_kill_inside_send(HWND ph, struct psc_ch *c) {
    psc_go(c);                                        /* child now sends to us; our window procedure kills it */
    int died = 0;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 8000) {
        k_pump(20);
        if (WaitForSingleObject(c->hp, 0) == WAIT_OBJECT_0) { died = 1; break; }
    }
    CHECKF(died, "child still alive 8 s after it was terminated from inside its SendMessage");
    DWORD code = 0;
    GetExitCodeProcess(c->hp, &code);
    CHECKF(code == 99, "exit code %lu", code);
    CHECK(c->s->v[3] == 0);                           /* the SendMessage never returned in the child */
    /* the parent's own state is intact */
    CHECK(U(IsWindow)(ph));
    CHECK(U(SendMessageA)(ph, PSC_M_TID, 0, 0) == (LRESULT)GetCurrentThreadId());
    HWND cw = (HWND)(ULONG_PTR)c->s->v[0];
    CHECK(PSC_POLL(!U(IsWindow)(cw), 5000));
    CHECK(PSC_POLL(psc_gr(c->hp, PSC_GDI) == 0, 5000));
    CHECK(PSC_POLL(psc_count_owned(c->pid) == 0, 5000));
}
TEST(process_session_cleanup_kill_child_inside_sendmessage_to_parent) {
    NEED_UI();
    with_parent_window(body_kill_inside_send, "sendtoparent");
}
static void body_sender_released(HWND ph, struct psc_ch *c) {
    /* parent sends to the child's window; the child's window procedure (class psc_wndproc) relays to our window, whose
       procedure terminates the child while the parent's original send is still pending */
    HWND cw = (HWND)(ULONG_PTR)c->s->v[0];
    DWORD t0 = GetTickCount();
    DWORD_PTR res = 0x77;
    SetLastError(PSC_MAGIC);
    LRESULT r = U(SendMessageTimeoutA)(cw, PSC_M_RELAY, 0, (LPARAM)ph, SMTO_ABORTIFHUNG, 6000, &res);
    DWORD dt = GetTickCount() - t0;
    CHECKF(dt < 7500, "sender blocked %lu ms", dt);
    t_info("SendMessageTimeout to a window whose process was terminated mid-handler: r=%lld err=%lu res=%llu after %lu ms", (long long)r, GetLastError(), (unsigned long long)res, dt);
    CHECK(WaitForSingleObject(c->hp, 5000) == WAIT_OBJECT_0);
    DWORD code = 0;
    GetExitCodeProcess(c->hp, &code);
    CHECKF(code == 99, "exit code %lu", code);
    CHECK(U(IsWindow)(ph));
    CHECK(PSC_POLL(!U(IsWindow)(cw), 5000));
}
TEST(process_session_cleanup_kill_child_while_parent_sends_to_it) {
    NEED_UI();
    with_parent_window(body_sender_released, "relaytarget");
}
static void body_child_kills_itself_in_handler(HWND ph, struct psc_ch *c) {
    HWND cw = (HWND)(ULONG_PTR)c->s->v[0];
    DWORD t0 = GetTickCount();
    DWORD_PTR res = 0x77;
    LRESULT r = U(SendMessageTimeoutA)(cw, PSC_M_KILLSELF, 0, 0, SMTO_ABORTIFHUNG, 6000, &res);
    DWORD dt = GetTickCount() - t0;
    /* the sender is released and the call counts as completed: TRUE with a zero result */
    CHECKF(r == 1, "result %lld", (long long)r);
    CHECKF(res == 0, "result slot %llu", (unsigned long long)res);
    CHECKF(dt < 7500, "sender blocked %lu ms", dt);
    CHECK(WaitForSingleObject(c->hp, 5000) == WAIT_OBJECT_0);
    DWORD code = 0;
    GetExitCodeProcess(c->hp, &code);
    CHECK(code == 88 || code == 77);
    CHECK(PSC_POLL(!U(IsWindow)(cw), 5000));
}
TEST(process_session_cleanup_sendmessage_to_process_that_terminates_itself) {
    NEED_UI();
    with_parent_window(body_child_kills_itself_in_handler, "relaytarget");
}
TEST(process_session_cleanup_sendmessage_to_exited_process_window) {
    NEED_UI();
    struct owner o;
    if (!owner_start(&o, "exit")) return;
    psc_go(&o.c);
    WaitForSingleObject(o.c.hp, PSC_WAIT);
    DWORD t0 = GetTickCount();
    SetLastError(PSC_MAGIC);
    LRESULT r = U(SendMessageA)(o.w1, PSC_M_TID, 0, 0);
    CHECK(r == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    CHECK(GetTickCount() - t0 < 3000);
    psc_free(&o.c);
}
