/* kcb_kernel_callbacks: a recording "echo" window on its own pumping thread.
 *
 * A cross-thread SendMessage is the public path that makes the kernel call back into user mode
 * through PEB->KernelCallbackTable on the RECEIVING thread (the kernel queues the
 * message, the receiver's next GetMessage/PeekMessage runs the receive path ->
 * the client-callback path -> KeUserModeCallback(fnid slot) -> KiUserCallbackDispatcher ->
 * user32!__fn* -> window procedure -> NtCallbackReturn). The echo window records exactly what its
 * window procedure saw (thread id, message, wParam, lParam, a deep copy of the pointer payloads), so a
 * test can validate the marshaling end to end without depending on which __fn* slot carried it.
 * The window is message-only (HWND_MESSAGE) so system broadcasts never reach it.
 */
#ifndef KCB_ECHO_H
#define KCB_ECHO_H
#include "t.h"
#include "k.h"
#include "kcb_api.h"

#define KCB_SENTINEL_RET ((LRESULT)0x1122334455667788LL)

struct kcb_echo {
    HANDLE thread, ready;
    volatile LONG stop;
    HWND hwnd;
    BOOL wide;                 /* register a Unicode (RegisterClassW) class instead of ANSI */
    BOOL main_thread;          /* created on the calling thread instead of a worker */
    char cls[64];
    WCHAR clsw[64];
    DWORD tid;                 /* owning thread */
    volatile LONG armed;       /* message id whose payload the proc should modify (0 = none) */
    HWND forward_to;           /* WM_USER+7 makes the proc SendMessage(forward_to, WM_USER+8, 1, 2) */
    volatile LONG calls;       /* WM_USER..WM_USER+0xFFFF family deliveries */
    volatile LONG any;         /* deliveries of the specific message under test */
    UINT last_msg; WPARAM last_wp; LPARAM last_lp; DWORD last_tid;
    char text[128]; WCHAR wtext[128];
    NMHDR nm;
    HELPINFO hi;
    DRAWITEMSTRUCT dis; int dis_seen;
    DELETEITEMSTRUCT del;
    COPYDATASTRUCT cds; BYTE cdata[512]; int cd_seen; WPARAM cd_wp;
    int create_n; UINT create_msg[4]; CREATESTRUCTA create_cs[4]; HWND create_hwnd[4];
    RECT rect_in, rect_out;
    MINMAXINFO mmi_in;
    int pbt_seen;
};

static struct kcb_echo *g_kcb_echo[4];
static struct kcb_echo *g_kcb_creating;

static struct kcb_echo *kcb_echo_find(HWND h) {
    for (int i = 0; i < 4; i++) if (g_kcb_echo[i] && g_kcb_echo[i]->hwnd == h) return g_kcb_echo[i];
    return g_kcb_creating;
}

static LRESULT kcb_echo_core(HWND h, UINT m, WPARAM w, LPARAM l, BOOL wide) {
    struct kcb_echo *e = kcb_echo_find(h);
    if (e) {
        e->last_tid = GetCurrentThreadId();
        switch (m) {
        case WM_NCCREATE: case WM_CREATE:
            if (e->create_n < 4) {
                CREATESTRUCTA *cs = (CREATESTRUCTA *)l;
                e->create_msg[e->create_n] = m;
                e->create_cs[e->create_n] = *cs;
                e->create_hwnd[e->create_n] = h;
                e->create_n++;
            }
            break;
        case WM_SETTEXT:
            if (wide) { lstrcpynW(e->wtext, (LPCWSTR)l, 128); }
            else lstrcpynA(e->text, (LPCSTR)l, 128);
            e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
            return TRUE;
        case WM_GETTEXT: {
            const char *s = "kcb-echo-text";
            size_t n = strlen(s);
            if (w == 0) return 0;
            if (wide) { WCHAR *d = (WCHAR *)l; size_t i; for (i = 0; i < n && i + 1 < w; i++) d[i] = s[i]; d[i] = 0; e->any++; return (LRESULT)i; }
            char *d = (char *)l; size_t i; for (i = 0; i < n && i + 1 < w; i++) d[i] = s[i]; d[i] = 0;
            e->any++;
            return (LRESULT)i;
        }
        case WM_GETTEXTLENGTH:
            e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
            return 1234;
        case WM_SIZING:
            if (e->armed == WM_SIZING) {
                RECT *r = (RECT *)l;
                e->rect_in = *r; r->left += 5; r->right -= 7; r->top += 11; r->bottom -= 13; e->rect_out = *r;
                e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
                return TRUE;
            }
            break;
        case WM_GETMINMAXINFO:
            if (e->armed == WM_GETMINMAXINFO) {
                MINMAXINFO *mm = (MINMAXINFO *)l;
                e->mmi_in = *mm; mm->ptMaxSize.x = 1111; mm->ptMaxSize.y = 2222;
                mm->ptMinTrackSize.x = 33; mm->ptMinTrackSize.y = 44;
                e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
                return 0;
            }
            break;
        case WM_NCCALCSIZE:
            if (e->armed == WM_NCCALCSIZE && w == 0) {
                RECT *r = (RECT *)l;
                e->rect_in = *r; r->left += 2; r->top += 3; r->right -= 4; r->bottom -= 5; e->rect_out = *r;
                e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
                return 0;
            }
            break;
        case WM_HELP:
            if (e->armed == WM_HELP) { e->hi = *(HELPINFO *)l; e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l; return TRUE; }
            break;
        case WM_NOTIFY:
            if (e->armed == WM_NOTIFY) { e->nm = *(NMHDR *)l; e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l; return 0x77; }
            break;
        case WM_DRAWITEM:
            if (e->armed == WM_DRAWITEM) { e->dis = *(DRAWITEMSTRUCT *)l; e->dis_seen++; e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l; return TRUE; }
            break;
        case WM_DELETEITEM:
            if (e->armed == WM_DELETEITEM) { e->del = *(DELETEITEMSTRUCT *)l; e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l; return TRUE; }
            break;
        case WM_COPYDATA:
            if (e->armed == WM_COPYDATA) {
                COPYDATASTRUCT *c = (COPYDATASTRUCT *)l;
                e->cds = *c; e->cd_wp = w; e->cd_seen++;
                if (c->lpData && c->cbData) memcpy(e->cdata, c->lpData, c->cbData < sizeof e->cdata ? c->cbData : sizeof e->cdata);
                e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
                return TRUE;
            }
            break;
        case WM_POWERBROADCAST:
            if (e->armed == WM_POWERBROADCAST) { e->pbt_seen++; e->any++; e->last_msg = m; e->last_wp = w; e->last_lp = l; return TRUE; }
            break;
        default:
            if (m >= WM_USER && m < 0x10000) {
                e->calls++; e->last_msg = m; e->last_wp = w; e->last_lp = l;
                if (m == WM_USER + 7 && e->forward_to)
                    SendMessageW(e->forward_to, WM_USER + 8, 1, 2);
                return KCB_SENTINEL_RET;
            }
            break;
        }
    }
    return wide ? DefWindowProcW(h, m, w, l) : DefWindowProcA(h, m, w, l);
}
static LRESULT CALLBACK kcb_echo_procA(HWND h, UINT m, WPARAM w, LPARAM l) { return kcb_echo_core(h, m, w, l, FALSE); }
static LRESULT CALLBACK kcb_echo_procW(HWND h, UINT m, WPARAM w, LPARAM l) { return kcb_echo_core(h, m, w, l, TRUE); }

static BOOL kcb_echo_create_window(struct kcb_echo *e) {
    HINSTANCE hi = GetModuleHandleA(NULL);
    g_kcb_creating = e;
    if (e->wide) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = kcb_echo_procW; wc.hInstance = hi; wc.lpszClassName = e->clsw;
        RegisterClassW(&wc);
        e->hwnd = CreateWindowExW(0, e->clsw, L"kcb_echo", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, hi, NULL);
    } else {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = kcb_echo_procA; wc.hInstance = hi; wc.lpszClassName = e->cls;
        RegisterClassA(&wc);
        e->hwnd = CreateWindowExA(0, e->cls, "kcb_echo", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, hi, NULL);
    }
    g_kcb_creating = NULL;
    e->tid = GetCurrentThreadId();
    return e->hwnd != NULL;
}
static void kcb_echo_destroy_window(struct kcb_echo *e) {
    if (e->hwnd) DestroyWindow(e->hwnd);
    if (e->wide) UnregisterClassW(e->clsw, GetModuleHandleA(NULL));
    else UnregisterClassA(e->cls, GetModuleHandleA(NULL));
    e->hwnd = NULL;
}

static DWORD WINAPI kcb_echo_thread(LPVOID arg) {
    struct kcb_echo *e = (struct kcb_echo *)arg;
    kcb_echo_create_window(e);
    SetEvent(e->ready);
    if (!e->hwnd) return 1;
    /* Bounded pump (<= ~8 s): the receiving thread must be inside GetMessage/PeekMessage for the
     * kernel to deliver a pending sent message. */
    for (int i = 0; i < 400 && !e->stop; i++) k_pump(20);
    kcb_echo_destroy_window(e);
    return 0;
}

/* slot: index into g_kcb_echo (0 = worker under test, 1 = a second window, usually on the main thread) */
static BOOL kcb_echo_start(struct kcb_echo *e, int slot, const char *tag, BOOL wide, BOOL on_main) {
    memset(e, 0, sizeof *e);
    e->wide = wide; e->main_thread = on_main;
    snprintf(e->cls, sizeof e->cls, "win32ktKcbE%s_%lu", tag, GetCurrentProcessId());
    for (int i = 0; i < 64; i++) e->clsw[i] = (WCHAR)(unsigned char)e->cls[i];
    g_kcb_echo[slot] = e;
    if (on_main) {
        if (!kcb_echo_create_window(e)) { g_kcb_echo[slot] = NULL; return FALSE; }
        return TRUE;
    }
    e->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!e->ready) { g_kcb_echo[slot] = NULL; return FALSE; }
    e->thread = CreateThread(NULL, 0, kcb_echo_thread, e, 0, &e->tid);
    if (!e->thread) { CloseHandle(e->ready); g_kcb_echo[slot] = NULL; return FALSE; }
    WaitForSingleObject(e->ready, 5000);
    if (!e->hwnd) {
        InterlockedExchange(&e->stop, 1);
        WaitForSingleObject(e->thread, 5000);
        CloseHandle(e->thread); CloseHandle(e->ready); g_kcb_echo[slot] = NULL;
        return FALSE;
    }
    return TRUE;
}
static void kcb_echo_stop(struct kcb_echo *e, int slot) {
    if (e->main_thread) { kcb_echo_destroy_window(e); }
    else if (e->thread) {
        InterlockedExchange(&e->stop, 1);
        WaitForSingleObject(e->thread, 5000);
        CloseHandle(e->thread); CloseHandle(e->ready);
        e->thread = NULL;
    }
    g_kcb_echo[slot] = NULL;
}

/* ---- slot-set helpers ---------------------------------------------------------------------- */
static unsigned long kcb_sum_hits(const unsigned *slots, unsigned n) {
    unsigned long s = 0;
    for (unsigned i = 0; i < n; i++) s += k_cb_hit_count(slots[i]);
    return s;
}
/* Hooks every populated slot; returns how many were hooked. */
static unsigned kcb_hook_all(void) {
    unsigned n = k_cb_count(), ok = 0;
    for (unsigned i = 0; i < n; i++) if (k_cb_hook(i)) ok++;
    return ok;
}
/* Hooks the listed slots (skipping ones beyond the table). */
static unsigned kcb_hook_set(const unsigned *slots, unsigned n) {
    unsigned cnt = k_cb_count(), ok = 0;
    for (unsigned i = 0; i < n; i++) if (slots[i] < cnt && k_cb_hook(slots[i])) ok++;
    return ok;
}

/* Decodes the first three scalar arguments out of the argument block a hooked slot received.
 * Layouts come from the __fn* analysis: "flat" thunks read hwnd/message/wParam from qwords 0/1/2;
 * "capture-buffer" thunks (_CAPTUREBUF) read them from +0x28/+0x30/+0x38. Returns FALSE for slots
 * whose layout has not been derived. */
static BOOL kcb_slot_is_flat(unsigned s) {
    switch (s) { case 2: case 6: case 96: case 12: case 17: case 20: case 39: case 126: case 117: return TRUE; }
    return FALSE;
}
static BOOL kcb_slot_is_capture(unsigned s) {
    switch (s) { case 0: case 10: case 26: case 27: case 29: case 7: case 35: return TRUE; }
    return FALSE;
}
static BOOL kcb_decode(unsigned slot, ULONG_PTR a0, HWND *h, UINT *m, WPARAM *w) {
    ULONG_PTR hv = 0, wv = 0; DWORD mv = 0;
    size_t oh, om, ow;
    if (kcb_slot_is_flat(slot)) { oh = 0; om = 8; ow = 16; }
    else if (kcb_slot_is_capture(slot)) { oh = 0x28; om = 0x30; ow = 0x38; }
    else return FALSE;
    if (!k_try_read((char *)a0 + oh, &hv, 8) || !k_try_read((char *)a0 + om, &mv, 4) || !k_try_read((char *)a0 + ow, &wv, 8)) return FALSE;
    *h = kcb_pwnd_hwnd(hv); *m = mv; *w = wv;
    return TRUE;
}
#endif
