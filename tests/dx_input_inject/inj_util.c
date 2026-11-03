#include "inj.h"

/* ---- environment -------------------------------------------------------------------------------------------------- */
static int g_env = -1;
static void wname(HANDLE h, WCHAR *out, DWORD cap) {
    DWORD n = 0; out[0] = 0;
    if (U(GetUserObjectInformationW)) U(GetUserObjectInformationW)(h, UOI_NAME, out, cap * 2, &n);
}
int inj_env(void) {
    if (g_env < 0) {
        g_env = 1;
        if (!U(SendInput) || !U(GetCursorPos)) g_env = 2;
        else if (!t_is_wine()) {
            if (k_session_id() == 0) g_env = 3;
            else if (!k_has_interactive_desktop()) g_env = 4;
            else {
                WCHAR a[64], b[64];
                HDESK in = U(OpenInputDesktop) ? U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS) : NULL;
                HDESK me = U(GetThreadDesktop)(GetCurrentThreadId());
                if (!in) g_env = 4;
                else {
                    wname(in, a, 64); wname(me, b, 64);
                    if (lstrcmpiW(a, b) || lstrcmpiW(a, L"Default")) g_env = 5;
                    U(CloseDesktop)(in);
                }
            }
        }
    }
    switch (g_env) {
    case 1: return 1;
    case 2: t_skip("SendInput/GetCursorPos not exported"); break;
    case 3: t_skip("session 0 has no interactive window station"); break;
    case 4: t_skip("no interactive input desktop"); break;
    default: t_skip("input desktop is not the process's default desktop"); break;
    }
    return 0;
}

/* ---- snapshot / restore ------------------------------------------------------------------------------------------- */
static struct { int armed; POINT pt; RECT clip; int clipfull; int swap; int tog[3]; } g_s;
static const WORD g_togvk[3] = { VK_CAPITAL, VK_NUMLOCK, VK_SCROLL };
static const WORD g_relvk[] = { 0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80, 0x7F, 0x7E, 0x7D, 0x7C, VK_SHIFT, VK_LSHIFT, VK_RSHIFT,
    VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_SPACE, VK_RETURN, VK_BACK, VK_TAB, VK_ESCAPE, VK_PACKET_, 0 };
void inj_screen(RECT *prim, RECT *virt) {
    int (WINAPI *gsm)(int) = U(GetSystemMetrics);
    if (prim) { prim->left = prim->top = 0; prim->right = gsm(SM_CXSCREEN); prim->bottom = gsm(SM_CYSCREEN); }
    if (virt) {
        virt->left = gsm(SM_XVIRTUALSCREEN); virt->top = gsm(SM_YVIRTUALSCREEN);
        virt->right = virt->left + gsm(SM_CXVIRTUALSCREEN); virt->bottom = virt->top + gsm(SM_CYVIRTUALSCREEN);
    }
}
void inj_restore(void) {
    if (!g_s.armed) return;
    if (U(BlockInput)) U(BlockInput)(FALSE);
    inj_hook_end();
    for (int i = 0; g_relvk[i]; i++) {
        if (U(GetAsyncKeyState)(g_relvk[i]) & 0x8000) {
            INPUT in; inj_key(&in, g_relvk[i], 0, KEYEVENTF_KEYUP, 0); inj_send(1, &in);
        }
    }
    inj_pump(30);   /* let the queue process the releases above before reading GetKeyState toggles */
    for (int i = 0; i < 3; i++) {
        int now = U(GetKeyState)(g_togvk[i]) & 1;
        if (now != g_s.tog[i]) inj_tap(g_togvk[i], 0, 0);
    }
    if (U(SwapMouseButton)) U(SwapMouseButton)(g_s.swap);
    if (U(ClipCursor)) U(ClipCursor)(g_s.clipfull ? NULL : &g_s.clip);
    if (U(SetCursorPos)) U(SetCursorPos)(g_s.pt.x, g_s.pt.y);
    inj_pump(20);
    /* the toggle keys are re-read after the queue processed our restoring taps */
}
static void backstop(void) { inj_restore(); }
void inj_arm(void) {
    if (g_s.armed) return;
    U(GetCursorPos)(&g_s.pt);
    RECT v; inj_screen(NULL, &v);
    memset(&g_s.clip, 0, sizeof g_s.clip);
    g_s.clipfull = 1;
    if (U(GetClipCursor) && U(GetClipCursor)(&g_s.clip)) {
        g_s.clipfull = (g_s.clip.left <= v.left && g_s.clip.top <= v.top && g_s.clip.right >= v.right && g_s.clip.bottom >= v.bottom);
    }
    g_s.swap = U(GetSystemMetrics)(SM_SWAPBUTTON) != 0;
    for (int i = 0; i < 3; i++) g_s.tog[i] = U(GetKeyState)(g_togvk[i]) & 1;
    g_s.armed = 1;
    atexit(backstop);
}

ULONG_PTR inj_tag(void) { static unsigned n; return (ULONG_PTR)0x57490000u + (++n); }
void inj_pump(unsigned ms) {
    DWORD t0 = GetTickCount(); MSG m;
    do {
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        Sleep(4);
    } while (GetTickCount() - t0 < ms);
}

/* ---- INPUT builders ----------------------------------------------------------------------------------------------- */
void inj_key(INPUT *in, WORD vk, WORD scan, DWORD flags, ULONG_PTR extra) {
    memset(in, 0, sizeof *in); in->type = INPUT_KEYBOARD;
    in->ki.wVk = vk; in->ki.wScan = scan; in->ki.dwFlags = flags; in->ki.dwExtraInfo = extra;
}
void inj_mouse(INPUT *in, LONG dx, LONG dy, DWORD data, DWORD flags, ULONG_PTR extra) {
    memset(in, 0, sizeof *in); in->type = INPUT_MOUSE;
    in->mi.dx = dx; in->mi.dy = dy; in->mi.mouseData = data; in->mi.dwFlags = flags; in->mi.dwExtraInfo = extra;
}
UINT inj_send(UINT n, INPUT *in) { return U(SendInput)(n, in, sizeof(INPUT)); }
UINT inj_keydown(WORD vk, DWORD flags, ULONG_PTR extra) { INPUT i; inj_key(&i, vk, 0, flags, extra); return inj_send(1, &i); }
UINT inj_keyup(WORD vk, DWORD flags, ULONG_PTR extra) { INPUT i; inj_key(&i, vk, 0, flags | KEYEVENTF_KEYUP, extra); return inj_send(1, &i); }
UINT inj_tap(WORD vk, DWORD flags, ULONG_PTR extra) { return inj_keydown(vk, flags, extra) + inj_keyup(vk, flags, extra); }
UINT inj_mouse1(LONG dx, LONG dy, DWORD data, DWORD flags, ULONG_PTR extra) { INPUT i; inj_mouse(&i, dx, dy, data, flags, extra); return inj_send(1, &i); }
LONG inj_abs(int px, int total) { return (LONG)(((long long)px * 65536 + total - 1) / total); }
int inj_near(LONG a, LONG b, LONG tol) { LONG d = a - b; return d < 0 ? -d <= tol : d <= tol; }

/* ---- LL hook recorder --------------------------------------------------------------------------------------------- */
#define MAXEV 512
static struct inj_ev g_ev[MAXEV]; static int g_nev;
static HHOOK g_hkb, g_hms;
DWORD inj_block_vk; WPARAM inj_block_mouse_msg;
int inj_ev_n(void) { return g_nev; }
struct inj_ev *inj_ev(int i) { return &g_ev[i]; }
void inj_ev_clear(void) { g_nev = 0; }
static LRESULT CALLBACK kbproc(int c, WPARAM w, LPARAM l) {
    if (c == HC_ACTION && g_nev < MAXEV) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)l; struct inj_ev *e = &g_ev[g_nev++];
        memset(e, 0, sizeof *e); e->wp = w; e->vk = k->vkCode; e->scan = k->scanCode; e->flags = k->flags; e->time = k->time; e->extra = k->dwExtraInfo;
        if (inj_block_vk && k->vkCode == inj_block_vk && (k->flags & LLKHF_INJECTED)) return 1;
    }
    return U(CallNextHookEx)(g_hkb, c, w, l);
}
static LRESULT CALLBACK msproc(int c, WPARAM w, LPARAM l) {
    if (c == HC_ACTION && g_nev < MAXEV) {
        MSLLHOOKSTRUCT *m = (MSLLHOOKSTRUCT *)l; struct inj_ev *e = &g_ev[g_nev++];
        memset(e, 0, sizeof *e); e->mouse = 1; e->wp = w; e->x = m->pt.x; e->y = m->pt.y; e->mdata = m->mouseData; e->flags = m->flags; e->time = m->time; e->extra = m->dwExtraInfo;
        if (inj_block_mouse_msg && w == inj_block_mouse_msg && (m->flags & LLMHF_INJECTED)) return 1;
    }
    return U(CallNextHookEx)(g_hms, c, w, l);
}
HHOOK inj_hook_install(int id, HOOKPROC p) { return U(SetWindowsHookExW)(id, p, GetModuleHandleW(NULL), 0); }
void inj_hook_remove(HHOOK h) { if (h) U(UnhookWindowsHookEx)(h); }
int inj_hook_begin(int kb, int mouse) {
    int ok = 1;
    inj_ev_clear(); inj_block_vk = 0; inj_block_mouse_msg = 0;
    if (kb && !g_hkb) { g_hkb = inj_hook_install(WH_KEYBOARD_LL, kbproc); if (!g_hkb) ok = 0; }
    if (mouse && !g_hms) { g_hms = inj_hook_install(WH_MOUSE_LL, msproc); if (!g_hms) ok = 0; }
    return ok;
}
void inj_hook_end(void) {
    if (g_hkb) { inj_hook_remove(g_hkb); g_hkb = NULL; }
    if (g_hms) { inj_hook_remove(g_hms); g_hms = NULL; }
    inj_block_vk = 0; inj_block_mouse_msg = 0;
}
int inj_ev_find(int mouse, ULONG_PTR extra, int from) {
    for (int i = from < 0 ? 0 : from; i < g_nev; i++) if (g_ev[i].mouse == mouse && g_ev[i].extra == extra) return i;
    return -1;
}
int inj_ev_find_vk(DWORD vk, int from) {
    for (int i = from < 0 ? 0 : from; i < g_nev; i++) if (!g_ev[i].mouse && g_ev[i].vk == vk && (g_ev[i].flags & LLKHF_INJECTED)) return i;
    return -1;
}

/* ---- test window -------------------------------------------------------------------------------------------------- */
#define MAXMSG 512
static struct inj_msg g_msg[MAXMSG]; static int g_nmsg;
static HWND g_prevfg;
int inj_msg_n(void) { return g_nmsg; }
struct inj_msg *inj_msg(int i) { return &g_msg[i]; }
void inj_msg_clear(void) { g_nmsg = 0; }
int inj_msg_find(UINT msg, int from) { for (int i = from < 0 ? 0 : from; i < g_nmsg; i++) if (g_msg[i].msg == msg) return i; return -1; }
int inj_msg_count(UINT msg) { int n = 0; for (int i = 0; i < g_nmsg; i++) if (g_msg[i].msg == msg) n++; return n; }

static LRESULT CALLBACK wproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    int logit = (m >= WM_KEYFIRST && m <= WM_KEYLAST) || (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_MOUSEHWHEEL;
    if (logit && g_nmsg < MAXMSG) {
        struct inj_msg *e = &g_msg[g_nmsg++];
        e->msg = m; e->wp = w; e->lp = l; e->extra = U(GetMessageExtraInfo)(); e->time = (DWORD)U(GetMessageTime)();
    }
    switch (m) {
    case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR: return 0;      /* never enter menu mode on a bare Alt tap */
    case WM_SYSCOMMAND: if ((w & 0xFFF0) == SC_KEYMENU) return 0; break;
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    }
    return U(DefWindowProcW)(h, m, w, l);
}
static ATOM g_atom;
HWND inj_win_create(int x, int y, int w, int h) {
    if (!g_atom) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.style = CS_DBLCLKS; wc.lpfnWndProc = wproc; wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = NULL; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszClassName = L"win32kt_inj";
        g_atom = U(RegisterClassW)(&wc);
        if (!g_atom) return NULL;
    }
    HWND hw = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"win32kt_inj", L"win32kt inject", WS_POPUP | WS_VISIBLE,
                                 x, y, w, h, NULL, NULL, GetModuleHandleW(NULL), NULL);
    return hw;
}
void inj_win_destroy(HWND h) {
    if (h) U(DestroyWindow)(h);
    inj_pump(10);
    if (g_prevfg && U(IsWindow)(g_prevfg)) U(SetForegroundWindow)(g_prevfg);
    g_prevfg = NULL;
}
int inj_win_focus(HWND h) {
    DWORD me = GetCurrentThreadId();
    for (int i = 0; i < 5; i++) {
        HWND fg = U(GetForegroundWindow)();
        if (fg == h && U(GetFocus)() == h) return 1;
        DWORD ft = fg ? U(GetWindowThreadProcessId)(fg, NULL) : 0;
        int att = ft && ft != me && U(AttachThreadInput)(me, ft, TRUE);
        U(SetForegroundWindow)(h); U(BringWindowToTop)(h); U(SetFocus)(h);
        if (att) U(AttachThreadInput)(me, ft, FALSE);
        inj_pump(40);
    }
    return U(GetForegroundWindow)() == h && U(GetFocus)() == h;
}
HWND inj_fg_begin(void) {
    inj_arm();
    g_prevfg = U(GetForegroundWindow)();
    HWND h = inj_win_create(8, 8, 160, 100);
    if (!h) { t_skip("cannot create the injection target window"); return NULL; }
    inj_pump(20);
    if (!inj_win_focus(h)) {
        t_skip("cannot become the foreground/focus window (foreground lock)");
        inj_win_destroy(h);
        return NULL;
    }
    inj_msg_clear();
    return h;
}
void inj_fg_end(HWND h) { inj_hook_end(); inj_restore(); inj_win_destroy(h); }
void inj_win_center(HWND h, POINT *pt) {
    RECT r; U(GetWindowRect)(h, &r); pt->x = (r.left + r.right) / 2; pt->y = (r.top + r.bottom) / 2;
}
int inj_cursor_into(HWND h) {
    POINT c; inj_win_center(h, &c);
    U(SetCursorPos)(c.x, c.y);
    inj_pump(20);
    return U(WindowFromPoint)(c) == h;
}
