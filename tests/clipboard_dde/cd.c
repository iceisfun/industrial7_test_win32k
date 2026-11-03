#include "cd.h"
#include <stdlib.h>
#include <wchar.h>

struct cd_msg cd_log[CD_LOG_MAX];
int cd_log_n;
void cd_log_reset(void) { cd_log_n = 0; }

static struct cd_ctx s_pool[16];
static int s_used[16];
static WCHAR s_cls[64];
static WCHAR *s_saved_text;
static int s_registered_exit;

#define NMSG(m) ((m) >= 0x305 && (m) <= 0x30D)

static LRESULT CALLBACK cd_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    struct cd_ctx *c;
    if (m == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
        U(SetWindowLongPtrW)(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return U(DefWindowProcW)(h, m, w, l);
    }
    c = (struct cd_ctx *)U(GetWindowLongPtrW)(h, GWLP_USERDATA);
    if (c && (NMSG(m) || m == CD_WM_CLIPBOARDUPDATE)) {
        c->cnt[m - 0x300]++;
        c->last_w[m - 0x300] = w;
        c->last_l[m - 0x300] = l;
        if (cd_log_n < CD_LOG_MAX) {
            cd_log[cd_log_n].msg = m; cd_log[cd_log_n].w = w; cd_log[cd_log_n].l = l; cd_log[cd_log_n].ctx_id = c->id;
            cd_log_n++;
        }
        switch (m) {
        case CD_WM_RENDERFORMAT: {
            int i;
            c->seen_open_owner_ok = (U(GetClipboardOwner)() == h);
            c->seen_open_window = U(GetOpenClipboardWindow)();
            c->seen_fmt_avail = U(IsClipboardFormatAvailable)((UINT)w);
            for (i = 0; i < c->nrender; i++) {
                if (c->render[i].fmt == (UINT)w && !c->render[i].refuse) {
                    HGLOBAL g = cd_gmem(c->render[i].data, c->render[i].len);
                    if (g) U(SetClipboardData)((UINT)w, g);
                }
            }
            return 0;
        }
        case CD_WM_RENDERALLFORMATS:
            if (c->render_all && U(OpenClipboard)(NULL)) {
                int i;
                for (i = 0; i < c->nrender; i++) {
                    HGLOBAL g;
                    if (c->render[i].refuse) continue;
                    g = cd_gmem(c->render[i].data, c->render[i].len);
                    if (g) U(SetClipboardData)(c->render[i].fmt, g);
                }
                U(CloseClipboard)();
            }
            return 0;
        case CD_WM_DRAWCLIPBOARD:
            if (c->forward && c->next_viewer && U(IsWindow)(c->next_viewer))
                U(SendMessageW)(c->next_viewer, m, w, l);
            return 0;
        case CD_WM_CHANGECBCHAIN:
            if ((HWND)w == c->next_viewer) c->next_viewer = (HWND)l;
            else if (c->forward && c->next_viewer && U(IsWindow)(c->next_viewer))
                U(SendMessageW)(c->next_viewer, m, w, l);
            return c->chain_ret;
        default:
            return 0;
        }
    }
    return U(DefWindowProcW)(h, m, w, l);
}

static int cd_class(void) {
    WNDCLASSEXW wc;
    if (s_cls[0]) return 1;
    memset(&wc, 0, sizeof wc);
    wsprintfW(s_cls, L"win32kt_cd_%lu", GetCurrentProcessId());
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = cd_wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = s_cls;
    if (!U(RegisterClassExW)(&wc)) { t_note("RegisterClassExW failed err %lu", GetLastError()); s_cls[0] = 0; return 0; }
    return 1;
}

struct cd_ctx *cd_new(void) {
    int i;
    if (!cd_class()) return NULL;
    for (i = 0; i < 16; i++) if (!s_used[i]) break;
    if (i == 16) return NULL;
    memset(&s_pool[i], 0, sizeof s_pool[i]);
    s_used[i] = 1;
    s_pool[i].id = i + 1;
    s_pool[i].hwnd = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, s_cls, L"cd", WS_POPUP,
                                        -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), &s_pool[i]);
    if (!s_pool[i].hwnd) { t_note("CreateWindowExW failed err %lu", GetLastError()); s_used[i] = 0; return NULL; }
    return &s_pool[i];
}

void cd_destroy(struct cd_ctx *c) {
    if (!c) return;
    if (c->hwnd && U(IsWindow)(c->hwnd)) {
        if (c->listener) { U(RemoveClipboardFormatListener)(c->hwnd); c->listener = 0; }
        if (c->in_chain) { U(ChangeClipboardChain)(c->hwnd, c->next_viewer); c->in_chain = 0; }
        U(DestroyWindow)(c->hwnd);
    }
    c->hwnd = NULL;
    s_used[c->id - 1] = 0;
}

/* ---- memory / clipboard helpers --------------------------------------------------------- */
HGLOBAL cd_gmem(const void *p, size_t n) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n ? n : 1);
    if (g && p && n) {
        void *d = GlobalLock(g);
        if (!d) { GlobalFree(g); return NULL; }
        memcpy(d, p, n);
        GlobalUnlock(g);
    }
    return g;
}
HGLOBAL cd_gstr_w(const WCHAR *s) { return cd_gmem(s, (lstrlenW(s) + 1) * sizeof(WCHAR)); }
HGLOBAL cd_gstr_a(const char *s) { return cd_gmem(s, strlen(s) + 1); }

BOOL cd_open(HWND h) {
    int i;
    for (i = 0; i < 20; i++) {
        if (U(OpenClipboard)(h)) return TRUE;
        Sleep(50);
    }
    return FALSE;
}
BOOL cd_open_empty(HWND h) {
    if (!cd_open(h)) return FALSE;
    if (!U(EmptyClipboard)()) { U(CloseClipboard)(); return FALSE; }
    return TRUE;
}
BOOL cd_put(HWND owner, UINT fmt, const void *p, size_t n) {
    BOOL ok = TRUE;
    if (!cd_open_empty(owner)) return FALSE;
    if (p || n) {
        HGLOBAL g = cd_gmem(p, n);
        if (!g || !U(SetClipboardData)(fmt, g)) ok = FALSE;
    } else {
        U(SetClipboardData)(fmt, NULL);
    }
    U(CloseClipboard)();
    return ok;
}
BOOL cd_put_text(HWND owner, const WCHAR *s) { return cd_put(owner, CF_UNICODETEXT, s, (lstrlenW(s) + 1) * sizeof(WCHAR)); }

size_t cd_get_bytes(UINT fmt, void *out, size_t cap) {
    HANDLE h = U(GetClipboardData)(fmt);
    size_t sz;
    void *p;
    if (!h) return 0;
    sz = GlobalSize(h);
    if (!sz) return 0;
    p = GlobalLock(h);
    if (!p) return 0;
    if (out) memcpy(out, p, sz < cap ? sz : cap);
    GlobalUnlock(h);
    return sz;
}
int cd_formats(UINT *out, int cap) {
    int n = 0;
    UINT f = U(EnumClipboardFormats)(0);
    while (f && n < cap) { out[n++] = f; f = U(EnumClipboardFormats)(f); }
    return n;
}
int cd_formats_closed(UINT *out, int cap, HWND opener) {
    int n;
    if (!cd_open(opener)) return -1;
    n = cd_formats(out, cap);
    U(CloseClipboard)();
    return n;
}
int cd_fmt_index(const UINT *f, int n, UINT fmt) {
    int i;
    for (i = 0; i < n; i++) if (f[i] == fmt) return i;
    return -1;
}
UINT cd_reg(const char *tag) {
    WCHAR nm[96];
    wsprintfW(nm, L"win32kt_cd_%lu_%S", GetCurrentProcessId(), tag);
    return U(RegisterClipboardFormatW)(nm);
}
int cd_freed(HGLOBAL h) { return GlobalSize(h) == 0 || GlobalFlags(h) == GMEM_INVALID_HANDLE; }

struct thr { LPTHREAD_START_ROUTINE fn; void *arg; };
DWORD cd_thread(LPTHREAD_START_ROUTINE fn, void *arg) {
    HANDLE t = CreateThread(NULL, 0, fn, arg, 0, NULL);
    DWORD code = 0xDEAD0000;
    if (!t) return code;
    if (WaitForSingleObject(t, 5000) != WAIT_OBJECT_0) code = 0xDEAD0001;
    else GetExitCodeThread(t, &code);
    CloseHandle(t);
    return code;
}

HANDLE cd_convert(const void *p, DWORD n) {
    NtUserConvertMemHandle_pfn f = UN(NtUserConvertMemHandle);
    return f ? f((PVOID)p, n) : NULL;
}
void cd_destroy_clipdata(HANDLE h) {
    NtUserSetClipboardData_pfn set = UN(NtUserSetClipboardData);
    CD_SETCLIPBDATA sp = { 1, 1 };
    UINT fmt;
    if (!h || !set) return;
    fmt = cd_reg("park");
    if (!fmt) return;
    if (!cd_open_empty(NULL)) return;
    set(fmt, h, &sp);
    U(EmptyClipboard)();     /* the clipboard data object is freed */
    U(CloseClipboard)();
}

/* ---- session setup / teardown ------------------------------------------------------------ */
static void cd_restore(void) {
    if (!U(OpenClipboard)) return;
    if (!cd_open(NULL)) return;
    U(EmptyClipboard)();
    if (s_saved_text) {
        HGLOBAL g = cd_gstr_w(s_saved_text);
        if (g) U(SetClipboardData)(CF_UNICODETEXT, g);
    }
    U(CloseClipboard)();
}

static void cd_reset_all(void) {
    int i;
    for (i = 0; i < 3; i++) if (!U(CloseClipboard)()) break;   /* never leave it open */
    for (i = 0; i < 16; i++) if (s_used[i]) cd_destroy(&s_pool[i]);
}

void cd_guard_end(struct cd_guard *g) { (void)g; cd_reset_all(); }

int cd_begin(struct cd_guard *g) {
    static int state;
    static DWORD probe_err;
    (void)g;
    if (!U(OpenClipboard) || !U(CloseClipboard) || !U(EmptyClipboard) || !U(SetClipboardData) ||
        !U(GetClipboardData) || !U(EnumClipboardFormats) || !U(CountClipboardFormats) ||
        !U(IsClipboardFormatAvailable) || !U(GetClipboardOwner) || !U(GetOpenClipboardWindow) ||
        !U(RegisterClipboardFormatW) || !U(CreateWindowExW) || !U(DestroyWindow) ||
        !U(GetWindowLongPtrW) || !U(SetWindowLongPtrW) || !U(DefWindowProcW) || !U(IsWindow) ||
        !U(RegisterClassExW) || !U(SendMessageW) || !U(GetClipboardSequenceNumber)) {
        t_skip("missing user32 clipboard exports");
        return 0;
    }
    cd_reset_all();
    cd_log_reset();
    if (state == 0) {
        state = -1;
        if (U(OpenClipboard)(NULL)) {
            if (U(IsClipboardFormatAvailable)(CF_UNICODETEXT)) {
                HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
                if (h && GlobalSize(h) >= 2 && GlobalSize(h) < (1u << 24)) {
                    WCHAR *p = GlobalLock(h);
                    if (p) {
                        size_t n = GlobalSize(h);
                        s_saved_text = malloc(n + 2);
                        if (s_saved_text) { memcpy(s_saved_text, p, n); s_saved_text[n / 2] = 0; }
                        GlobalUnlock(h);
                    }
                }
            }
            U(CloseClipboard)();
            state = 1;
            if (!s_registered_exit) { s_registered_exit = 1; atexit(cd_restore); }
        } else probe_err = GetLastError();
    }
    if (state < 0) { t_skip("clipboard not reachable from this session (OpenClipboard error %lu)", probe_err); return 0; }
    return 1;
}

/* Cross-process scenarios need a real window station/display; a headless emulator cannot create
 * windows at all (CreateWindowExW fails), and its clipboard then never completes cross-process
 * requests, so those tests skip instead of waiting for the child timeouts. */
int cd_have_window(void) {
    static int state;
    if (state == 0) {
        struct cd_ctx *c = cd_new();
        state = c ? 1 : -1;
        if (c) cd_destroy(c);
    }
    return state > 0;
}
