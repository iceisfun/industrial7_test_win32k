#include "pd.h"
#include <stdlib.h>

unsigned pd_beh;
struct pd_ev pd_log[PD_LOG_MAX];
int pd_log_n;
struct pd_paint_obs pd_obs;
struct pd_erase_obs pd_eobs;
WPARAM pd_ncpaint_wparam;
int pd_ncpaint_seen;
int pd_erase_result;

static HWND g_tops[64];
static int g_ntops;
static ATOM g_atoms[PDC_COUNT];
static char g_names[PDC_COUNT][48];
static int g_have = -1;
static HBRUSH g_brush;

HINSTANCE pd_hinst(void) { return GetModuleHandleA(NULL); }

void pd_log_clear(void) { pd_log_n = 0; }
int pd_count(HWND h, UINT msg) {
    int c = 0;
    for (int i = 0; i < pd_log_n; i++) if (pd_log[i].msg == msg && (!h || pd_log[i].h == h)) c++;
    return c;
}
int pd_find(HWND h, UINT msg, int from) {
    for (int i = from < 0 ? 0 : from; i < pd_log_n; i++) if (pd_log[i].msg == msg && (!h || pd_log[i].h == h)) return i;
    return -1;
}
static void pd_rec(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (pd_log_n >= PD_LOG_MAX) return;
    struct pd_ev *e = &pd_log[pd_log_n];
    e->h = h; e->msg = m; e->w = w; e->l = l; e->seq = pd_log_n;
    pd_log_n++;
}

/* ---- the shared window procedure ---- */
static LRESULT CALLBACK pd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCPAINT:
        pd_rec(h, m, w, l);
        pd_ncpaint_wparam = w; pd_ncpaint_seen++;
        if (pd_beh & PB_NCPAINT_NONE) return 0;
        break;
    case WM_ERASEBKGND: {
        HDC dc = (HDC)w;
        pd_rec(h, m, w, l);
        memset(&pd_eobs, 0, sizeof pd_eobs);
        pd_eobs.valid = 1; pd_eobs.hdc = dc;
        if (UN(NtUserWindowFromDC)) pd_eobs.wfd = UN(NtUserWindowFromDC)(dc);
        pd_eobs.objtype = U(GetObjectType) ? U(GetObjectType)(dc) : 0;
        pd_eobs.cliptype = U(GetClipBox) ? U(GetClipBox)(dc, &pd_eobs.clip) : 0;
        if (pd_beh & PB_ERASE_ZERO) return 0;
        if (pd_beh & PB_ERASE_FILL) {
            RECT r;
            HBRUSH b = U(CreateSolidBrush)(PD_FILL);
            U(GetClientRect)(h, &r);
            U(FillRect)(dc, &r, b);
            U(DeleteObject)(b);
            return 1;
        }
        pd_erase_result = (int)U(DefWindowProcA)(h, m, w, l);
        return pd_erase_result;
    }
    case WM_PAINT:
        pd_rec(h, m, w, l);
        if (pd_beh & PB_PAINT_NONE) return 0;
        if (pd_beh & PB_PAINT_VALIDATE) { U(ValidateRect)(h, NULL); return 0; }
        if (pd_beh & PB_PAINT_BP) {
            struct pd_paint_obs *o = &pd_obs;
            memset(o, 0, sizeof *o);
            o->valid = 1;
            o->upd_before = U(GetUpdateRect)(h, &o->upd_rect_before, FALSE);
            memset(&o->ps, 0xCC, sizeof o->ps);
            o->hdc_ret = U(BeginPaint)(h, &o->ps);
            o->upd_after = U(GetUpdateRect)(h, NULL, FALSE);
            o->cliptype = U(GetClipBox)(o->hdc_ret, &o->clipbox);
            o->wfd = UN(NtUserWindowFromDC) ? UN(NtUserWindowFromDC)(o->hdc_ret) : NULL;
            o->objtype = U(GetObjectType)(o->hdc_ret);
            o->cell_ok = pd_cell(o->hdc_ret, &o->cell);
            o->end_ret = U(EndPaint)(h, &o->ps);
            return 0;
        }
        break;
    case WM_PRINT: case WM_PRINTCLIENT: case WM_SYNCPAINT:
        pd_rec(h, m, w, l);
        break;
    }
    return U(DefWindowProcA)(h, m, w, l);
}

static const UINT pd_cs[PDC_COUNT] = { 0, 0, CS_OWNDC, CS_CLASSDC, CS_PARENTDC, CS_HREDRAW | CS_VREDRAW };

static void pd_register(void) {
    if (g_atoms[0]) return;
    if (!U(RegisterClassExA) || !U(CreateSolidBrush)) return;
    g_brush = U(CreateSolidBrush)(PD_FILL);
    for (int i = 0; i < PDC_COUNT; i++) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.style = pd_cs[i]; wc.lpfnWndProc = pd_proc; wc.hInstance = pd_hinst();
        wc.hbrBackground = (i == PDC_NOBG) ? NULL : g_brush;
        snprintf(g_names[i], sizeof g_names[i], "win32ktPD%d_%lu", i, GetCurrentProcessId());
        wc.lpszClassName = g_names[i];
        g_atoms[i] = U(RegisterClassExA)(&wc);
    }
}
const char *pd_cls(enum pd_class c) { pd_register(); return g_names[c]; }

int pd_have_gui(void) {
    if (g_have >= 0) return g_have;
    g_have = 0;
    if (!U(CreateWindowExA) || !U(DestroyWindow) || !U(GetDC) || !U(ReleaseDC) || !U(GetClipBox) || !U(DefWindowProcA)
        || !U(BeginPaint) || !U(EndPaint) || !U(GetUpdateRect) || !U(ValidateRect) || !U(GetObjectType) || !U(FillRect)
        || !U(SetLayeredWindowAttributes) || !U(ShowWindow) || !U(PeekMessageA) || !U(DispatchMessageA)) return 0;
    pd_register();
    if (!g_atoms[0]) return 0;
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_names[0], "probe", WS_POPUP, PD_X, PD_Y, 8, 8, NULL, NULL, pd_hinst(), NULL);
    if (!h) return 0;
    U(DestroyWindow)(h);
    g_have = 1;
    return 1;
}

int pd_pump(unsigned ms, int maxmsg) {
    DWORD start = GetTickCount();
    int n = 0;
    MSG m;
    if (!U(PeekMessageA) || !U(DispatchMessageA)) { Sleep(ms); return 0; }
    while (GetTickCount() - start < ms && n < maxmsg) {
        if (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) {
            if (U(TranslateMessage)) U(TranslateMessage)(&m);
            U(DispatchMessageA)(&m);
            n++;
        } else Sleep(1);
    }
    return n;
}

void pd_settle(HWND h) {
    unsigned save = pd_beh;
    pd_beh = 0;
    /* a freshly shown window receives WM_NCPAINT/WM_ERASEBKGND/WM_PAINT; drain and validate until nothing is pending */
    for (int round = 0; round < 4; round++) {
        pd_pump(15, 40);
        if (h && U(IsWindow) && U(IsWindow)(h) && U(RedrawWindow))
            U(RedrawWindow)(h, NULL, NULL, RDW_VALIDATE | RDW_NOERASE | RDW_NOFRAME | RDW_NOINTERNALPAINT | RDW_ALLCHILDREN);
        pd_pump(5, 20);
        if (!(h && U(IsWindow) && U(IsWindow)(h) && U(GetUpdateRect))) break;
        if (!U(GetUpdateRect)(h, NULL, FALSE) && !pd_peek_paint(NULL, NULL, 0)) break;
    }
    pd_beh = save;
    pd_log_clear();
    pd_ncpaint_seen = 0;
    memset(&pd_obs, 0, sizeof pd_obs);
    memset(&pd_eobs, 0, sizeof pd_eobs);
}

void pd_kill(HWND h) {
    if (h && U(IsWindow) && U(IsWindow)(h)) U(DestroyWindow)(h);
}

void pd_reset(void) {
    if (U(LockWindowUpdate)) U(LockWindowUpdate)(NULL);     /* a failed test must not leave the system-wide lock behind */
    for (int i = g_ntops - 1; i >= 0; i--) pd_kill(g_tops[i]);
    g_ntops = 0;
    pd_beh = 0;
    pd_log_clear();
    pd_ncpaint_seen = 0; pd_ncpaint_wparam = 0;
    memset(&pd_obs, 0, sizeof pd_obs);
    memset(&pd_eobs, 0, sizeof pd_eobs);
    if (pd_have_gui()) pd_pump(2, 50);
}

HWND pd_make(enum pd_class c, HWND parent, DWORD style, DWORD ex, int x, int y, int w, int h) {
    HWND r;
    pd_register();
    if (!g_atoms[c]) return NULL;
    if (parent) {
        r = U(CreateWindowExA)(ex, g_names[c], "pd", WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, NULL, pd_hinst(), NULL);
        return r;
    }
    DWORD st = WS_POPUP | style;
    DWORD ex2 = ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOPMOST;
    RECT rc = pd_rc(0, 0, w, h);
    if (U(AdjustWindowRectEx)) U(AdjustWindowRectEx)(&rc, st & ~WS_VISIBLE, FALSE, ex2);
    r = U(CreateWindowExA)(ex2, g_names[c], "pd", st & ~WS_VISIBLE, x, y, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, pd_hinst(), NULL);
    if (!r) return NULL;
    U(SetLayeredWindowAttributes)(r, 0, 0, LWA_ALPHA);   /* fully transparent: invisible to the user, still a normal painted window */
    U(ShowWindow)(r, SW_SHOWNOACTIVATE);
    if (g_ntops < 64) g_tops[g_ntops++] = r;
    return r;
}
HWND pd_top_style(enum pd_class c, DWORD style) {
    HWND h = pd_make(c, NULL, style, 0, PD_X, PD_Y, PD_CW, PD_CH);
    if (h) pd_settle(h);
    return h;
}
HWND pd_top(enum pd_class c) { return pd_top_style(c, 0); }
HWND pd_child(HWND parent, enum pd_class c, DWORD style, int x, int y, int w, int h) {
    HWND r = pd_make(c, parent, style, 0, x, y, w, h);
    if (r) pd_settle(parent);
    return r;
}

int pd_exposed(HWND h) {
    RECT c, b;
    if (!U(GetDC) || !U(GetClientRect)) return 0;
    U(GetClientRect)(h, &c);
    HDC dc = U(GetDC)(h);
    if (!dc) return 0;
    int t = U(GetClipBox)(dc, &b);
    U(ReleaseDC)(h, dc);
    return t == SIMPLEREGION && pd_eq(&b, &c);
}

RECT pd_rc(int l, int t, int r, int b) { RECT x; x.left = l; x.top = t; x.right = r; x.bottom = b; return x; }
int pd_eq(const RECT *a, const RECT *b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
int pd_rgn_box(HRGN r, RECT *box) { return U(GetRgnBox)(r, box); }
HRGN pd_rgn(int l, int t, int r, int b) { return U(CreateRectRgn)(l, t, r, b); }
HRGN pd_rgn_screen(HWND h, int l, int t, int r, int b) {
    POINT o = {0, 0};
    U(ClientToScreen)(h, &o);
    return U(CreateRectRgn)(l + o.x, t + o.y, r + o.x, b + o.y);
}
void pd_rgn_free(HRGN r) { if (r) U(DeleteObject)(r); }

const unsigned char *pd_wnd(HWND h) {
    const unsigned char *p = (const unsigned char *)k_wnd_ptr(h);
    ULONG_PTR v = 0;
    if (p && k_try_read(p, &v, 8) && v == (ULONG_PTR)h) return p;
    if (t_is_wine() || !UN(NtUserCallOneParam)) return NULL;
    p = (const unsigned char *)UN(NtUserCallOneParam)((ULONG_PTR)h, 0x38);   /* user32!HMValidateHandle slow path */
    if (p && k_try_read(p, &v, 8) && v == (ULONG_PTR)h) return p;
    return NULL;
}
int pd_wnd_q(HWND h, size_t off, ULONG_PTR *out) {
    const unsigned char *p = pd_wnd(h);
    return p && k_try_read(p + off, out, sizeof *out);
}
int pd_hrgn_update(HWND h, ULONG_PTR *out) { return pd_wnd_q(h, PD_WND_HRGN_UPDATE, out); }
int pd_hrgn_clip(HWND h, ULONG_PTR *out) { return pd_wnd_q(h, PD_WND_HRGN_CLIP, out); }

int pd_cell(void *h, struct pd_cell *c) {
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) return 0;
    unsigned idx = (unsigned)((ULONG_PTR)h & 0xffff);
    unsigned char raw[24];
    if (!k_try_read((char *)base + (ULONG_PTR)idx * GDI_CELL_SIZE, raw, sizeof raw)) return 0;
    memcpy(&c->q0, raw, 8);
    memcpy(&c->owner, raw + 8, 4);
    memcpy(&c->upper, raw + 12, 2);
    c->objt = raw[14]; c->flags = raw[15];
    memcpy(&c->puser, raw + 16, 8);
    return 1;
}

unsigned pd_qs_paint(void) {
    if (!U(GetQueueStatus)) return 0;
    return (U(GetQueueStatus)(QS_PAINT) >> 16) & QS_PAINT;
}
int pd_peek_paint(HWND h, MSG *m, int remove) {
    MSG tmp;
    if (!m) m = &tmp;
    return U(PeekMessageA)(m, h, WM_PAINT, WM_PAINT, remove ? PM_REMOVE : PM_NOREMOVE) != 0;
}

int pd_dib_make(struct pd_dib *d, int w, int h) {
    BITMAPINFO bi;
    void *bits = NULL;
    memset(d, 0, sizeof *d);
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject)) return 0;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    d->bmp = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d->bmp || !bits) { U(DeleteDC)(d->dc); memset(d, 0, sizeof *d); return 0; }
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bmp);
    d->bits = (unsigned *)bits; d->w = w; d->h = h;
    return 1;
}
void pd_dib_free(struct pd_dib *d) {
    if (!d->dc) return;
    U(SelectObject)(d->dc, d->old);
    U(DeleteObject)(d->bmp);
    U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}
void pd_dib_fill(struct pd_dib *d, unsigned v) {
    for (int i = 0; i < d->w * d->h; i++) d->bits[i] = v;
}
unsigned pd_dib_px(struct pd_dib *d, int x, int y) {
    if (U(GdiFlush)) U(GdiFlush)();
    return d->bits[y * d->w + x] & 0x00FFFFFFu;
}

int pd_nt_release(HDC dc) {
    if (!UN(NtUserReleaseDC)) return 0;
    if (t_is_wine()) {
        typedef int (WINAPI *rel2_t)(HWND, HDC);
        HWND w = UN(NtUserWindowFromDC) ? UN(NtUserWindowFromDC)(dc) : NULL;
        return ((rel2_t)UN(NtUserReleaseDC))(w, dc);
    }
    return UN(NtUserReleaseDC)(dc);
}
