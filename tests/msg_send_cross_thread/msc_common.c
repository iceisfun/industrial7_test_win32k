#include "msc.h"

/* ---- time ------------------------------------------------------------------------------ */
LONGLONG msc_qpc(void) { LARGE_INTEGER v; QueryPerformanceCounter(&v); return v.QuadPart; }
double msc_ms_since(LONGLONG start) {
    LARGE_INTEGER f, v; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&v);
    return (double)(v.QuadPart - start) * 1000.0 / (double)f.QuadPart;
}

/* ---- event log ------------------------------------------------------------------------- */
static struct msc_ev g_log[MSC_LOG_MAX];
static volatile LONG g_nlog, g_seq;
static CRITICAL_SECTION g_logcs;
static volatile LONG g_logcs_init;
static void logcs(void) {
    if (g_logcs_init == 2) return;
    if (InterlockedCompareExchange(&g_logcs_init, 1, 0) == 0) { InitializeCriticalSection(&g_logcs); g_logcs_init = 2; }
    else while (g_logcs_init != 2) Sleep(1);
}
static void log_add(HWND h, UINT m, WPARAM w, LPARAM l, int ansi) {
    logcs();
    EnterCriticalSection(&g_logcs);
    if (g_nlog < MSC_LOG_MAX) {
        struct msc_ev *e = &g_log[g_nlog++];
        e->msg = m; e->w = w; e->l = l; e->hwnd = h; e->tid = GetCurrentThreadId();
        e->ismex = InSendMessageEx(NULL); e->insend = InSendMessage(); e->ansi = ansi;
        { void *p = NULL; unsigned long v = 0; e->ci_ok = k_try_read_ptr((char *)k_teb() + 0x860, &p) && p && k_try_read_u32(p, &v); e->ciflags = v; }
        e->seq = InterlockedIncrement(&g_seq); e->qpc = msc_qpc();
    }
    LeaveCriticalSection(&g_logcs);
}
void msc_log_reset(void) { logcs(); EnterCriticalSection(&g_logcs); g_nlog = 0; LeaveCriticalSection(&g_logcs); }
int msc_log_total(void) { return g_nlog; }
int msc_log_count(UINT msg) {
    logcs(); int n = 0;
    EnterCriticalSection(&g_logcs);
    for (int i = 0; i < g_nlog; i++) if (g_log[i].msg == msg) n++;
    LeaveCriticalSection(&g_logcs);
    return n;
}
BOOL msc_log_find(UINT msg, int nth, struct msc_ev *out) {
    logcs(); BOOL ok = FALSE;
    EnterCriticalSection(&g_logcs);
    for (int i = 0; i < g_nlog; i++) if (g_log[i].msg == msg && nth-- == 0) { if (out) *out = g_log[i]; ok = TRUE; break; }
    LeaveCriticalSection(&g_logcs);
    return ok;
}
BOOL msc_log_wait(UINT msg, int count, DWORD timeout_ms) {
    LONGLONG t0 = msc_qpc();
    while (msc_log_count(msg) < count) { if (msc_ms_since(t0) > timeout_ms) return FALSE; Sleep(2); }
    return TRUE;
}

/* ---- strings --------------------------------------------------------------------------- */
void msc_utf8_to_w(const char *s, WCHAR *out, int cap) { MultiByteToWideChar(CP_UTF8, 0, s, -1, out, cap); }
int msc_ansi_of_w(const WCHAR *w, char *out, int cap) { return WideCharToMultiByte(CP_ACP, 0, w, -1, out, cap, NULL, NULL); }
int msc_w_of_ansi(const char *a, WCHAR *out, int cap) { return MultiByteToWideChar(CP_ACP, 0, a, -1, out, cap); }

static void safe_wstr(LPARAM p, WCHAR *out, int cap) {
    int i = 0; out[0] = 0;
    for (; i < cap - 1; i++) {
        WCHAR c;
        if (!k_try_read((const WCHAR *)p + i, &c, sizeof c)) break;
        out[i] = c;
        if (!c) return;
    }
    out[i] = 0;
}
static void safe_astr(LPARAM p, char *out, int cap) {
    int i = 0; out[0] = 0;
    for (; i < cap - 1; i++) {
        char c;
        if (!k_try_read((const char *)p + i, &c, 1)) break;
        out[i] = c;
        if (!c) return;
    }
    out[i] = 0;
}

/* ---- window class + procedures --------------------------------------------------------- */
static WCHAR g_cls_w[48], g_cls_a[48];
static volatile LONG g_reg_state;
static BOOL g_reg_ok;
const WCHAR *msc_class_w(void) { msc_register(); return g_cls_w; }
const WCHAR *msc_class_a(void) { msc_register(); return g_cls_a; }

void msc_beh_default(struct msc_beh *b) {
    memset(b, 0, sizeof *b);
    b->logging = 1; b->def_text = 1;
}
struct msc_beh *msc_beh_of(HWND h) { return (struct msc_beh *)GetWindowLongPtrW(h, GWLP_USERDATA); }

static BOOL wr_dword(LPARAM p, size_t off, DWORD v) {
    SIZE_T n = 0;
    return WriteProcessMemory(GetCurrentProcess(), (void *)(p + off), &v, sizeof v, &n) && n == sizeof v;
}

static LRESULT msc_core(HWND h, UINT m, WPARAM w, LPARAM l, int ansi) {
    #define DEFPROC() (ansi ? DefWindowProcA(h, m, w, l) : DefWindowProcW(h, m, w, l))
    if (m == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;   /* lpCreateParams sits at the same offset in the A layout */
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        struct msc_beh *nb = (struct msc_beh *)cs->lpCreateParams;
        if (nb) nb->ansi = ansi;
        return DEFPROC();
    }
    struct msc_beh *b = msc_beh_of(h);
    if (!b) return DEFPROC();
    if (b->logging) log_add(h, m, w, l, ansi);

    switch (m) {
    case MSC_ECHO: return msc_echo_ret(w, l);
    case MSC_SLEEP: Sleep((DWORD)w); if (b->done) SetEvent(b->done); return 7;
    case MSC_QUERY: return msc_log_count(MSC_ECHO);
    case MSC_QSM: {
        struct msc_api_t *ap = msc_api();
        memset(&b->qsm_msg, 0xCC, sizeof b->qsm_msg);
        SetLastError(0);
        b->qsm_ret = ap->QuerySendMessage ? ap->QuerySendMessage(&b->qsm_msg) : FALSE;
        b->qsm_err = GetLastError();
        b->qsm_null_ret = ap->QuerySendMessage ? ap->QuerySendMessage(NULL) : FALSE;
        b->gts8 = ap->NtUserGetThreadState ? ap->NtUserGetThreadState(8) : 0xFFFF;
        return 0x51;
    }
    case MSC_COUNT: return msc_log_count((UINT)w);
    case MSC_QUIT: InterlockedExchange(&msc_quit_flag, 1); return 0;
    case MSC_EXITPROC: ExitProcess(3); return 0;
    case MSC_REPLY:
        b->ismex_before = InSendMessageEx(NULL);
        b->rep1 = ReplyMessage((LRESULT)0x1234567890ABCDEFLL);
        b->ismex_after = InSendMessageEx(NULL);
        b->insend_after = InSendMessage();
        SetLastError(0);
        b->rep2 = ReplyMessage(0x42);
        b->rep_err = GetLastError();
        Sleep((DWORD)w);
        if (b->done) SetEvent(b->done);
        return 0x9999;
    case MSC_NEST: {
        SetLastError(0);
        LRESULT r = SendMessageW(b->peer, MSC_ECHO, w, 0xBEEF);
        b->nest_ret = r; b->nest_err = GetLastError(); InterlockedIncrement(&b->nest_hits);
        return r;
    }
    case MSC_DEPTH: {
        if (w == 0) return 1000;
        LRESULT r = SendMessageW((HWND)l, MSC_DEPTH, w - 1, (LPARAM)h);
        return r + 1;
    }
    case MSC_PROBE: {
        ULONG_PTR res = 0xDEAD;
        SetLastError(0);
        b->probe_ret = SendMessageTimeoutW(b->peer, MSC_ECHO, 0, 0, (UINT)w, (UINT)l, &res);
        b->probe_err = GetLastError(); b->probe_res = res;
        if (b->done) SetEvent(b->done);
        return 0;
    }
    case MSC_SELFDESTROY: DestroyWindow(h); return 5;
    case MSC_EXITTHREAD: ExitThread(0); return 0;
    case MSC_PREPOST:
        PostMessageW(h, MSC_POSTED, 1, 2);
        if (b->done) SetEvent(b->done);
        if (b->go) WaitForSingleObject(b->go, 3000);
        return 3;
    case WM_SETTEXT: case LB_ADDSTRING: case LB_INSERTSTRING: case LB_FINDSTRING: case LB_FINDSTRINGEXACT: case LB_SELECTSTRING:
    case CB_ADDSTRING: case CB_INSERTSTRING: case CB_FINDSTRING: case CB_FINDSTRINGEXACT: case CB_SELECTSTRING:
        b->str_msg = m; b->str_ptr = l; b->has_str = 0;
        if (l) {
            if (ansi) safe_astr(l, b->astr, sizeof b->astr); else safe_wstr(l, b->wstr, 256);
            b->has_str = 1;
        }
        if (m == WM_SETTEXT) return b->def_text ? DEFPROC() : 1;
        return 0x55;
    case WM_GETTEXT: case WM_GETTEXTLENGTH:
        return DEFPROC();
    case WM_COPYDATA: {
        COPYDATASTRUCT cds;
        b->cd_w = w; b->cd_ok = FALSE; b->cd_cb = 0; b->cd_lp = NULL; b->cd_data = 0;
        if (l && k_try_read((void *)l, &cds, sizeof cds)) {
            b->cd_data = cds.dwData; b->cd_cb = cds.cbData; b->cd_lp = cds.lpData;
            DWORD n = cds.cbData < sizeof b->cd_bytes ? cds.cbData : sizeof b->cd_bytes;
            b->cd_ok = n == 0 ? TRUE : k_try_read(cds.lpData, b->cd_bytes, n);
            if (b->cd_scribble && cds.lpData && n) {
                SIZE_T x; unsigned char junk[64]; memset(junk, 0xEE, sizeof junk);
                for (DWORD o = 0; o < n; o += 64) WriteProcessMemory(GetCurrentProcess(), (char *)cds.lpData + o, junk, n - o < 64 ? n - o : 64, &x);
            }
        }
        if (b->cd_ret == (LRESULT)-2 && b->cd_ok) {   /* checksum mode: report what was received */
            unsigned char *tmp = (unsigned char *)malloc(b->cd_cb ? b->cd_cb : 1);
            ULONGLONG hsh = MSC_FNV_SEED;
            if (tmp && (b->cd_cb == 0 || k_try_read(b->cd_lp, tmp, b->cd_cb))) hsh = msc_fnv(tmp, b->cd_cb, MSC_FNV_SEED);
            free(tmp);
            return (LRESULT)(hsh ^ ((ULONGLONG)b->cd_cb * 0x9E37) ^ (ULONGLONG)b->cd_data);
        }
        return b->cd_ret;
    }
    case WM_NCCALCSIZE:
        if (b->cap_msg != WM_NCCALCSIZE) return DEFPROC();
        InterlockedIncrement(&b->nc_hits);
        b->nc_w = w; b->nc_wp_ok = FALSE;
        if (!l) return 0;
        if (w) {
            NCCALCSIZE_PARAMS np;
            if (!k_try_read((void *)l, &np, sizeof np)) return 0;
            memcpy(b->nc_rect, np.rgrc, sizeof np.rgrc);
            if (np.lppos && k_try_read(np.lppos, &b->nc_wp, sizeof b->nc_wp)) b->nc_wp_ok = TRUE;
            if (b->mut_on) {
                wr_dword(l, offsetof(NCCALCSIZE_PARAMS, rgrc[0].left), np.rgrc[0].left + b->mut_val);
                wr_dword(l, offsetof(NCCALCSIZE_PARAMS, rgrc[0].right), np.rgrc[0].right - b->mut_val);
            }
        } else {
            if (!k_try_read((void *)l, &b->nc_rect[0], sizeof(RECT))) return 0;
            if (b->mut_on) {
                wr_dword(l, offsetof(RECT, left), b->nc_rect[0].left + b->mut_val);
                wr_dword(l, offsetof(RECT, bottom), b->nc_rect[0].bottom - b->mut_val);
            }
        }
        return b->cap_ret;
    default: break;
    }
    if (b->cap_msg && b->cap_msg == m) {
        InterlockedIncrement(&b->cap_hits);
        b->cap_w = w; b->cap_ptr = l; b->cap_ok = FALSE;
        if (l && b->cap_len) b->cap_ok = k_try_read((void *)l, b->cap, b->cap_len > sizeof b->cap ? sizeof b->cap : b->cap_len);
        if (m == WM_CREATE && b->cap_ok) {
            CREATESTRUCTW cs;
            if (k_try_read((void *)l, &cs, sizeof cs) && (ULONG_PTR)cs.lpszName > 0xFFFF) {
                if (ansi) safe_astr((LPARAM)cs.lpszName, b->astr, sizeof b->astr); else safe_wstr((LPARAM)cs.lpszName, b->wstr, 256);
                b->has_str = 1; b->str_msg = m;
            }
        }
        if (l && b->mut_on) wr_dword(l, b->mut_off, b->mut_val);
        if (b->wr_wl) {
            if (w) wr_dword(w, 0, b->wr_w_val);
            if (l) wr_dword(l, 0, b->wr_l_val);
        }
        if (b->cap_ret == (LRESULT)-3) return l;                 /* return the raw lParam */
        if (b->cap_ret == (LRESULT)-4) return (LRESULT)msc_fnv(b->cap, b->cap_len > sizeof b->cap ? sizeof b->cap : b->cap_len, MSC_FNV_SEED);
        return b->cap_ret;
    }
    if (b->chr_msg && b->chr_msg == m) { b->chr_w = w; InterlockedIncrement(&b->chr_hits); return 0xCC; }
    return DEFPROC();
    #undef DEFPROC
}
LRESULT CALLBACK msc_wndproc_w(HWND h, UINT m, WPARAM w, LPARAM l) { return msc_core(h, m, w, l, 0); }
LRESULT CALLBACK msc_wndproc_a(HWND h, UINT m, WPARAM w, LPARAM l) { return msc_core(h, m, w, l, 1); }

volatile LONG msc_quit_flag;
ULONGLONG msc_fnv(const void *p, size_t n, ULONGLONG seed) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) { seed ^= b[i]; seed *= 1099511628211ULL; }
    return seed;
}

BOOL msc_register(void) {
    if (g_reg_state == 2) return g_reg_ok;
    if (InterlockedCompareExchange(&g_reg_state, 1, 0) == 0) {
        swprintf(g_cls_w, 48, L"win32ktMscW_%lu", GetCurrentProcessId());
        swprintf(g_cls_a, 48, L"win32ktMscA_%lu", GetCurrentProcessId());
        BOOL ok = msc_api()->ok;
        if (ok) {
            WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
            wc.cbSize = sizeof wc; wc.lpfnWndProc = msc_wndproc_w; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = g_cls_w;
            ok = RegisterClassExW(&wc) != 0;
            WNDCLASSEXA wa; memset(&wa, 0, sizeof wa);
            char an[48]; WideCharToMultiByte(CP_ACP, 0, g_cls_a, -1, an, sizeof an, NULL, NULL);
            wa.cbSize = sizeof wa; wa.lpfnWndProc = msc_wndproc_a; wa.hInstance = GetModuleHandleW(NULL); wa.lpszClassName = an;
            ok = ok && RegisterClassExA(&wa) != 0;
        }
        g_reg_ok = ok;
        g_reg_state = 2;
    } else while (g_reg_state != 2) Sleep(1);
    return g_reg_ok;
}

HWND msc_create(const struct msc_beh *tmpl, struct msc_beh **out_beh, int ansi, HWND parent) {
    return msc_create_ex(tmpl, out_beh, ansi, parent, NULL);
}
HWND msc_create_ex(const struct msc_beh *tmpl, struct msc_beh **out_beh, int ansi, HWND parent, const WCHAR *title) {
    if (!msc_register()) return NULL;
    struct msc_beh *b = (struct msc_beh *)calloc(1, sizeof *b);
    if (tmpl) *b = *tmpl; else msc_beh_default(b);
    HWND h;
    DWORD ex = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (ansi) {
        char an[48]; WideCharToMultiByte(CP_ACP, 0, g_cls_a, -1, an, sizeof an, NULL, NULL);
        char at[128]; WideCharToMultiByte(CP_ACP, 0, title ? title : L"win32kt_msc", -1, at, sizeof at, NULL, NULL);
        h = CreateWindowExA(ex, an, at, WS_POPUP, -32000, -32000, 100, 60, parent, NULL, GetModuleHandleW(NULL), b);
    } else {
        h = CreateWindowExW(ex, g_cls_w, title ? title : L"win32kt_msc", WS_POPUP, -32000, -32000, 100, 60, parent, NULL, GetModuleHandleW(NULL), b);
    }
    if (!h) { free(b); return NULL; }
    if (out_beh) *out_beh = b;
    return h;
}

BOOL msc_have_gui(void) {
    static int state = -1;
    if (state >= 0) return state;
    state = 0;
    if (!msc_api()->ok || !msc_register()) return 0;
    HWND h = msc_create(NULL, NULL, 0, NULL);
    if (h) { state = 1; DestroyWindow(h); }
    return state;
}

/* ---- pumping --------------------------------------------------------------------------- */
void msc_pump(unsigned ms) {
    LONGLONG t0 = msc_qpc();
    MSG msg;
    do {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        MsgWaitForMultipleObjectsEx(0, NULL, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } while (msc_ms_since(t0) < ms);
}
int msc_pump_until(volatile LONG *flag, unsigned timeout_ms) {
    LONGLONG t0 = msc_qpc();
    MSG msg;
    while (!*flag && msc_ms_since(t0) < timeout_ms) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        if (*flag) break;
        MsgWaitForMultipleObjectsEx(0, NULL, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    return *flag != 0;
}

/* ---- worker thread --------------------------------------------------------------------- */
static DWORD WINAPI worker_main(LPVOID p) {
    struct msc_worker *wk = (struct msc_worker *)p;
    MSG msg;
    wk->tid = GetCurrentThreadId();
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);          /* create the thread's queue, stamp its last-read time */
    wk->w = msc_create(NULL, &wk->beh, 0, NULL);
    wk->wa = msc_create(NULL, &wk->beha, 1, NULL);
    wk->wm = msc_create(NULL, &wk->behm, 0, (HWND)(LONG_PTR)-3);   /* HWND_MESSAGE */
    if (wk->w && (wk->flags & MSCW_CONTROLS)) {
        HINSTANCE hi = GetModuleHandleW(NULL);
        wk->lb = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | LBS_HASSTRINGS | LBS_NOTIFY, 0, 0, 60, 60, wk->w, (HMENU)(UINT_PTR)101, hi, NULL);
        wk->cb = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | CBS_DROPDOWN | CBS_HASSTRINGS, 0, 0, 60, 120, wk->w, (HMENU)(UINT_PTR)102, hi, NULL);
        wk->ed = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | ES_LEFT | ES_AUTOHSCROLL, 0, 0, 60, 20, wk->w, (HMENU)(UINT_PTR)103, hi, NULL);
    }
    wk->created = wk->w != NULL;
    SetEvent(wk->ready);
    if (!wk->created) return 1;
    if (wk->flags & MSCW_PAUSE) WaitForSingleObject(wk->go, 8000);
    if (wk->flags & MSCW_NOPUMP) Sleep(wk->nopump_ms);
    wk->pump_start_qpc = msc_qpc();
    InterlockedExchange(&wk->pumping, 1);
    while (!wk->quit && msc_ms_since(wk->pump_start_qpc) < 30000) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { wk->quit = 1; break; }
            DispatchMessageW(&msg);
        }
        if (wk->quit) break;
        MsgWaitForMultipleObjectsEx(0, NULL, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    HWND all[] = { wk->ed, wk->cb, wk->lb, wk->wm, wk->wa, wk->w };
    for (unsigned i = 0; i < sizeof all / sizeof *all; i++) if (all[i] && IsWindow(all[i])) DestroyWindow(all[i]);
    return 0;
}

BOOL msc_worker_start(struct msc_worker *wk, unsigned flags, DWORD nopump_ms) {
    memset(wk, 0, sizeof *wk);
    if (!msc_have_gui()) return FALSE;
    wk->flags = flags; wk->nopump_ms = nopump_ms;
    wk->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    wk->go = CreateEventW(NULL, TRUE, FALSE, NULL);
    wk->th = CreateThread(NULL, 0, worker_main, wk, 0, NULL);
    if (!wk->th) { CloseHandle(wk->ready); CloseHandle(wk->go); memset(wk, 0, sizeof *wk); return FALSE; }
    WaitForSingleObject(wk->ready, 8000);
    if (!wk->created) { msc_worker_stop(wk); return FALSE; }
    return TRUE;
}
void msc_worker_stop(struct msc_worker *wk) {
    if (!wk->th) return;
    InterlockedExchange(&wk->quit, 1);
    SetEvent(wk->go);
    PostThreadMessageW(wk->tid, WM_NULL, 0, 0);
    if (WaitForSingleObject(wk->th, 8000) != WAIT_OBJECT_0) t_note("worker thread %lu did not exit in time (leaked)", wk->tid);
    CloseHandle(wk->th); CloseHandle(wk->ready); CloseHandle(wk->go);
    memset(wk, 0, sizeof *wk);
}

/* ---- one-shot sender thread ------------------------------------------------------------ */
static DWORD WINAPI sender_main(LPVOID p) {
    struct msc_sender *s = (struct msc_sender *)p;
    if (s->delay_ms) Sleep(s->delay_ms);
    InterlockedExchange(&s->started, 1);
    s->t_enter = msc_qpc();
    SetLastError(0);
    s->ret = s->notify ? (LRESULT)SendNotifyMessageW(s->target, s->msg, s->w, s->l) : SendMessageW(s->target, s->msg, s->w, s->l);
    s->err = GetLastError();
    s->t_leave = msc_qpc();
    InterlockedExchange(&s->finished, 1);
    return 0;
}
BOOL msc_sender_start(struct msc_sender *s) {
    s->started = s->finished = 0; s->ret = 0; s->err = 0;
    s->th = CreateThread(NULL, 0, sender_main, s, 0, NULL);
    return s->th != NULL;
}
BOOL msc_sender_join(struct msc_sender *s, DWORD ms) {
    if (!s->th) return TRUE;
    BOOL ok = WaitForSingleObject(s->th, ms) == WAIT_OBJECT_0;
    if (ok) { CloseHandle(s->th); s->th = NULL; }
    return ok;
}
