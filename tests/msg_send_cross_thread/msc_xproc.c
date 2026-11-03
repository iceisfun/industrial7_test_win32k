/* msg_send_cross_thread: sends across a process boundary and UIPI.
 *
 * A server child (re-launch of this exe, --child msc_server) owns a set of windows whose procedures are
 * configured to return checksums / apply edits, so the parent can verify what the kernel delivered
 * into the other address space purely from SendMessage results and its own buffers. */
#include "msc.h"

#define BIGW ((WPARAM)0x0123456789ABCDEFULL)
#define BIGL ((LPARAM)0xFEDCBA9876543210ULL)

/* window indices created by the server */
enum { XW_PLAIN, XW_ANSI, XW_MMI, XW_WPCHG, XW_SIZING, XW_NCCALC, XW_COPY, XW_NOTIFY, XW_GETSEL, XW_MEASURE, XW_COUNT };

static void xtitle(WCHAR *out, DWORD pid, int idx) { swprintf(out, 64, L"win32ktMscX_%lu_%d", pid, idx); }

CHILD(msc_server) {
    int bad = 0; (void)argc;
    DWORD ppid = argc > 1 ? (DWORD)strtoul(argv[1], NULL, 10) : 0;
    if (!msc_have_gui()) return 0x100;
    DWORD me = GetCurrentProcessId();
    struct msc_beh *b[XW_COUNT]; HWND w[XW_COUNT];
    for (int i = 0; i < XW_COUNT; i++) {
        WCHAR t[64]; xtitle(t, me, i);
        struct msc_beh tmpl; msc_beh_default(&tmpl);
        switch (i) {
        case XW_MMI:     tmpl.cap_msg = WM_GETMINMAXINFO; tmpl.cap_len = sizeof(MINMAXINFO); tmpl.cap_ret = -4; tmpl.mut_on = 1; tmpl.mut_off = offsetof(MINMAXINFO, ptMaxSize.x); tmpl.mut_val = 0x1111; break;
        case XW_WPCHG:   tmpl.cap_msg = WM_WINDOWPOSCHANGING; tmpl.cap_len = sizeof(WINDOWPOS); tmpl.cap_ret = -4; tmpl.mut_on = 1; tmpl.mut_off = offsetof(WINDOWPOS, cx); tmpl.mut_val = 0x2222; break;
        case XW_SIZING:  tmpl.cap_msg = WM_SIZING; tmpl.cap_len = sizeof(RECT); tmpl.cap_ret = -4; tmpl.mut_on = 1; tmpl.mut_off = offsetof(RECT, left); tmpl.mut_val = 0x44; break;
        case XW_NCCALC:  tmpl.cap_msg = WM_NCCALCSIZE; tmpl.mut_on = 1; tmpl.mut_val = 7; tmpl.cap_ret = 0x1234; break;
        case XW_COPY:    tmpl.cd_ret = -2; break;
        case XW_NOTIFY:  tmpl.cap_msg = WM_NOTIFY; tmpl.cap_len = 0; tmpl.cap_ret = -3; break;
        case XW_GETSEL:  tmpl.cap_msg = EM_GETSEL; tmpl.cap_ret = 0x00090003; tmpl.wr_wl = 1; tmpl.wr_w_val = 3; tmpl.wr_l_val = 9; break;
        case XW_MEASURE: tmpl.cap_msg = WM_MEASUREITEM; tmpl.cap_len = sizeof(MEASUREITEMSTRUCT); tmpl.cap_ret = -4; tmpl.mut_on = 1; tmpl.mut_off = offsetof(MEASUREITEMSTRUCT, itemHeight); tmpl.mut_val = 0x33; break;
        default: break;
        }
        w[i] = msc_create_ex(&tmpl, &b[i], i == XW_ANSI, NULL, t);
        if (!w[i]) { printf("  FAIL cannot create server window %d\n", i); return 0x101; }
    }
    HANDLE hp = ppid ? OpenProcess(SYNCHRONIZE, FALSE, ppid) : NULL;
    LONGLONG t0 = msc_qpc();
    while (!msc_quit_flag && msc_ms_since(t0) < 60000) {
        msc_pump(20);
        if (hp && WaitForSingleObject(hp, 0) == WAIT_OBJECT_0) break;   /* orphan guard */
    }
    return bad;
}

struct xs { HANDLE proc; HANDLE thr; DWORD pid; HWND w[XW_COUNT]; };
static HWND xs_find(struct xs *s, int idx) {
    if (s->w[idx]) return s->w[idx];
    WCHAR t[64]; xtitle(t, s->pid, idx);
    WCHAR cls[64]; swprintf(cls, 64, idx == XW_ANSI ? L"win32ktMscA_%lu" : L"win32ktMscW_%lu", s->pid);
    LONGLONG t0 = msc_qpc();
    while (msc_ms_since(t0) < 8000) {
        HWND h = FindWindowW(cls, t);
        if (h) { s->w[idx] = h; return h; }
        Sleep(20);
    }
    return NULL;
}
static BOOL xs_start(struct xs *s) {
    memset(s, 0, sizeof *s);
    if (!msc_have_gui()) return FALSE;
    PROCESS_INFORMATION pi; char args[32]; snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    s->proc = t_spawn_child("msc_server", args, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!s->proc) return FALSE;
    s->pid = pi.dwProcessId;
    if (!xs_find(s, XW_COUNT - 1)) { TerminateProcess(s->proc, 1); CloseHandle(s->proc); s->proc = NULL; return FALSE; }
    return TRUE;
}
static DWORD xs_stop(struct xs *s) {
    DWORD code = 0xFFFF;
    if (!s->proc) return code;
    ULONG_PTR r;
    xs_find(s, XW_PLAIN);
    if (s->w[XW_PLAIN]) SendMessageTimeoutW(s->w[XW_PLAIN], MSC_QUIT, 0, 0, SMTO_ABORTIFHUNG, 2000, &r);
    if (WaitForSingleObject(s->proc, 6000) != WAIT_OBJECT_0) { TerminateProcess(s->proc, 1); WaitForSingleObject(s->proc, 3000); t_note("server had to be terminated"); }
    GetExitCodeProcess(s->proc, &code);
    CloseHandle(s->proc); s->proc = NULL;
    return code;
}
#define NEED_SERVER(s) do { if (!xs_start(&(s))) { t_skip("could not start the cross-process server"); return; } } while (0)

TEST(msc_xproc_windows_belong_to_other_process) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    for (int i = 0; i < XW_COUNT; i++) {
        HWND h = xs_find(&s, i);
        CHECKF(h != NULL, "window %d", i);
        if (!h) continue;
        DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
        CHECK(tid != 0);
        EQ(pid, s.pid);
        CHECK(pid != GetCurrentProcessId());
        CHECK(IsWindow(h));
    }
    CHECK(IsWindowUnicode(xs_find(&s, XW_PLAIN)));
    CHECK(!IsWindowUnicode(xs_find(&s, XW_ANSI)));
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_echo_full_64bit_values) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN), a = xs_find(&s, XW_ANSI);
    static const struct { WPARAM w; LPARAM l; } v[] = { { 0, 0 }, { BIGW, BIGL }, { (WPARAM)-1, (LPARAM)-1 }, { 0x100000000ULL, 0x7FFFFFFF } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        EQ(SendMessageW(w, MSC_ECHO, v[i].w, v[i].l), msc_echo_ret(v[i].w, v[i].l));
        EQ(SendMessageA(a, MSC_ECHO, v[i].w, v[i].l), msc_echo_ret(v[i].w, v[i].l));
    }
    EQ(SendMessageW(w, MSC_QUERY, 0, 0), 8);          /* the server logged each of them */
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_text_roundtrip_across_charsets) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN), a = xs_find(&s, XW_ANSI);
    WCHAR wb[64]; char ab[64];
    EQ(SendMessageA(w, WM_SETTEXT, 0, (LPARAM)"from ansi parent"), TRUE);
    memset(wb, 0, sizeof wb);
    EQ(SendMessageW(w, WM_GETTEXT, 64, (LPARAM)wb), 16); EXPECT_WSTR(wb, L"from ansi parent");
    EQ(SendMessageW(w, WM_SETTEXT, 0, (LPARAM)L"from wide parent"), TRUE);
    memset(ab, 0, sizeof ab);
    EQ(SendMessageA(w, WM_GETTEXT, 64, (LPARAM)ab), 16); EXPECT_STR(ab, "from wide parent");
    EQ(SendMessageW(a, WM_SETTEXT, 0, (LPARAM)L"ansi window, wide sender"), TRUE);
    memset(ab, 0, sizeof ab);
    EQ(SendMessageA(a, WM_GETTEXT, 64, (LPARAM)ab), 24); EXPECT_STR(ab, "ansi window, wide sender");
    EQ(SendMessageW(a, WM_GETTEXTLENGTH, 0, 0), 24);
    memset(wb, 0, sizeof wb);
    EQ(SendMessageW(a, WM_GETTEXT, 64, (LPARAM)wb), 24); EXPECT_WSTR(wb, L"ansi window, wide sender");
    /* NULL text clears; empty text stays empty */
    EQ(SendMessageW(w, WM_SETTEXT, 0, 0), TRUE);
    EQ(SendMessageW(w, WM_GETTEXTLENGTH, 0, 0), 0);
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_gettext_buffer_protocol) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    SendMessageW(w, WM_SETTEXT, 0, (LPARAM)L"xproc-protocol");   /* 14 chars */
    static const int cch[] = { 1, 2, 7, 14, 15, 16, 200 };
    for (unsigned i = 0; i < sizeof cch / sizeof *cch; i++) {
        WCHAR b[256]; memset(b, 0, sizeof b);
        LRESULT r = SendMessageW(w, WM_GETTEXT, cch[i], (LPARAM)b);
        int want = cch[i] - 1 < 14 ? cch[i] - 1 : 14;
        CHECKF(r == want && wcsncmp(b, L"xproc-protocol", want) == 0 && b[want] == 0, "cch %d: r=%lld", cch[i], (long long)r);
    }
    EQ(SendMessageW(w, WM_GETTEXT, 0, (LPARAM)L""), 0);
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_copydata_checksums) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_COPY);
    static const size_t sz[] = { 0, 1, 3, 8, 255, 4096, 65536, 262144 };
    static const ULONG_PTR dw[] = { 0, 0x12345678, 0xFFFFFFFFFFFFFFFFULL };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        size_t n = sz[i];
        unsigned char *p = (unsigned char *)malloc(n ? n : 1), *keep = (unsigned char *)malloc(n ? n : 1);
        if (!p || !keep) { free(p); free(keep); CHECK(0); continue; }
        for (size_t k = 0; k < n; k++) p[k] = (unsigned char)(k * 13 + i);
        memcpy(keep, p, n);
        COPYDATASTRUCT cds = { dw[i % 3], (DWORD)n, n ? p : NULL };
        LRESULT want = (LRESULT)(msc_fnv(p, n, MSC_FNV_SEED) ^ ((ULONGLONG)n * 0x9E37) ^ (ULONGLONG)cds.dwData);
        LRESULT r = SendMessageW(w, WM_COPYDATA, (WPARAM)GetDesktopWindow(), (LPARAM)&cds);
        CHECKF(r == want, "size %zu: checksum 0x%llx want 0x%llx", n, (unsigned long long)r, (unsigned long long)want);
        CHECKF(memcmp(p, keep, n) == 0, "size %zu: sender buffer changed", n);
        free(p); free(keep);
    }
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_inout_structs_copy_in_and_out) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    /* the server returns the FNV of the bytes it saw and applies a known edit */
    {
        HWND w = xs_find(&s, XW_MMI);
        MINMAXINFO m; memset(&m, 0, sizeof m);
        m.ptMaxSize.x = 800; m.ptMaxSize.y = 600; m.ptMinTrackSize.x = 100; m.ptMaxTrackSize.x = 1600; m.ptMaxPosition.y = 4;
        MINMAXINFO o = m;
        LRESULT r = SendMessageW(w, WM_GETMINMAXINFO, 0, (LPARAM)&m);
        EQ(r, (LRESULT)msc_fnv(&o, sizeof o, MSC_FNV_SEED));
        EQ(m.ptMaxSize.x, 0x1111); EQ(m.ptMaxSize.y, 600); EQ(m.ptMinTrackSize.x, 100);
    }
    {
        HWND w = xs_find(&s, XW_WPCHG);
        WINDOWPOS p; memset(&p, 0, sizeof p);
        p.hwnd = w; p.x = 5; p.y = 6; p.cx = 7; p.cy = 8; p.flags = SWP_NOZORDER;
        WINDOWPOS o = p;
        LRESULT r = SendMessageW(w, WM_WINDOWPOSCHANGING, 0, (LPARAM)&p);
        EQ(r, (LRESULT)msc_fnv(&o, sizeof o, MSC_FNV_SEED));
        EQ(p.cx, 0x2222); EQ(p.cy, 8); EQ(p.x, 5); EQ(p.hwnd, w);
    }
    {
        HWND w = xs_find(&s, XW_SIZING);
        RECT rc = { 10, 20, 310, 220 }, o = rc;
        LRESULT r = SendMessageW(w, WM_SIZING, WMSZ_LEFT, (LPARAM)&rc);
        EQ(r, (LRESULT)msc_fnv(&o, sizeof o, MSC_FNV_SEED));
        EQ(rc.left, 0x44); EQ(rc.top, 20); EQ(rc.right, 310);
    }
    {
        HWND w = xs_find(&s, XW_MEASURE);
        MEASUREITEMSTRUCT mi; memset(&mi, 0, sizeof mi);
        mi.CtlType = ODT_LISTBOX; mi.CtlID = 9; mi.itemID = 4; mi.itemWidth = 50; mi.itemHeight = 15; mi.itemData = 0x777;
        MEASUREITEMSTRUCT o = mi;
        LRESULT r = SendMessageW(w, WM_MEASUREITEM, 9, (LPARAM)&mi);
        EQ(r, (LRESULT)msc_fnv(&o, sizeof o, MSC_FNV_SEED));
        EQ(mi.itemHeight, 0x33); EQ(mi.itemWidth, 50); EQ(mi.itemData, 0x777);
    }
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_nccalcsize_both_forms) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_NCCALC);
    NCCALCSIZE_PARAMS np; WINDOWPOS wp;
    memset(&np, 0, sizeof np); memset(&wp, 0, sizeof wp);
    SetRect(&np.rgrc[0], 100, 110, 500, 510); SetRect(&np.rgrc[1], 1, 2, 3, 4); SetRect(&np.rgrc[2], 5, 6, 7, 8);
    wp.hwnd = w; wp.x = 100; wp.y = 110; wp.cx = 400; wp.cy = 400; np.lppos = &wp;
    NCCALCSIZE_PARAMS o = np; WINDOWPOS owp = wp;
    EQ(SendMessageW(w, WM_NCCALCSIZE, TRUE, (LPARAM)&np), 0x1234);
    EQ(np.rgrc[0].left, o.rgrc[0].left + 7); EQ(np.rgrc[0].right, o.rgrc[0].right - 7);
    CHECK(memcmp(&np.rgrc[1], &o.rgrc[1], sizeof(RECT)) == 0);
    CHECK(memcmp(&np.rgrc[2], &o.rgrc[2], sizeof(RECT)) == 0);
    EQ(np.lppos, &wp);
    CHECK(memcmp(&wp, &owp, sizeof wp) == 0);
    RECT rc = { 10, 20, 410, 320 };
    EQ(SendMessageW(w, WM_NCCALCSIZE, FALSE, (LPARAM)&rc), 0x1234);
    EQ(rc.left, 17); EQ(rc.bottom, 313); EQ(rc.top, 20); EQ(rc.right, 410);
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_wm_notify_refused_across_processes) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_NOTIFY);
    NMHDR nm = { w, 5, 6 };
    /* WM_NOTIFY carries a pointer that cannot be marshaled between processes: the send is refused with access denied and
     * never reaches the other process's window procedure. */
    SetLastError(0);
    LRESULT zr = SendMessageW(w, WM_NOTIFY, 5, (LPARAM)&nm);
    DWORD err = GetLastError();
    EQ(zr, 0); EQ(err, ERROR_ACCESS_DENIED);
    ULONG_PTR res = 0xAA;
    SetLastError(0);
    zr = SendMessageTimeoutW(w, WM_NOTIFY, 5, (LPARAM)&nm, SMTO_NORMAL, 1000, &res);
    err = GetLastError();
    EQ(zr, 0); EQ(err, ERROR_ACCESS_DENIED);
    EQ(SendMessageW(w, MSC_COUNT, WM_NOTIFY, 0), 0);      /* the server never saw one */
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_optional_dword_out_params) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_GETSEL);
    DWORD a = 0xAAAA, b = 0xBBBB;
    EQ(SendMessageW(w, EM_GETSEL, (WPARAM)&a, (LPARAM)&b), 0x00090003);
    CHECKF(a == 3 && b == 9, "%x %x", a, b);
    a = b = 0xCCCC;
    SendMessageW(w, EM_GETSEL, (WPARAM)&a, 0); CHECKF(a == 3 && b == 0xCCCC, "%x %x", a, b);
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_timeout_then_recovery) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    ULONG_PTR res = 0;
    SetLastError(0);
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(w, MSC_SLEEP, 900, 0, SMTO_NORMAL, 250, &res);
    double ms = msc_ms_since(t0);
    EQ(r, 0); EQ(GetLastError(), ERROR_TIMEOUT);
    CHECKF(ms >= 220.0 && ms < 850.0, "timeout after %.0f ms", ms);
    /* queued behind the still-sleeping handler, then served */
    EQ(SendMessageW(w, MSC_ECHO, 1, 2), msc_echo_ret(1, 2));
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_notify_does_not_block_sender) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    LONGLONG t0 = msc_qpc();
    CHECK(SendNotifyMessageW(w, MSC_SLEEP, 700, 0));
    double ms = msc_ms_since(t0);
    CHECKF(ms < 500.0, "SendNotifyMessage blocked %.0f ms behind a 700 ms handler", ms);
    /* the notify is still delivered: a later synchronous send is served after it */
    EQ(SendMessageW(w, MSC_COUNT, MSC_SLEEP, 0), 1);
    EQ(xs_stop(&s), 0);
}

static volatile LONG g_xcb_hits;
static HWND g_xcb_hwnd; static UINT g_xcb_msg; static ULONG_PTR g_xcb_data; static LRESULT g_xcb_res;
static void CALLBACK xcb(HWND h, UINT m, ULONG_PTR d, LRESULT r) { g_xcb_hwnd = h; g_xcb_msg = m; g_xcb_data = d; g_xcb_res = r; InterlockedIncrement(&g_xcb_hits); }
TEST(msc_xproc_callback_delivers_result_to_sender) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    g_xcb_hits = 0;
    CHECK(SendMessageCallbackW(w, MSC_ECHO, BIGW, BIGL, xcb, 0xFACE));
    CHECKF(msc_pump_until(&g_xcb_hits, 5000), "callback never arrived");
    EQ(g_xcb_hits, 1);
    EQ(g_xcb_hwnd, w); EQ(g_xcb_msg, MSC_ECHO); EQ(g_xcb_data, 0xFACE);
    EQ(g_xcb_res, msc_echo_ret(BIGW, BIGL));
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_reply_releases_sender_early) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageW(w, MSC_REPLY, 600, 0);
    double ms = msc_ms_since(t0);
    EQ(r, (LRESULT)0x1234567890ABCDEFLL);
    CHECKF(ms < 450.0, "sender waited %.0f ms after an immediate ReplyMessage", ms);
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_private_broadcast_reaches_server_windows) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    WCHAR nm[64]; swprintf(nm, 64, L"win32ktMscXbc_%lu", GetCurrentProcessId());
    UINT priv = RegisterWindowMessageW(nm);
    ULONG_PTR res = 0;
    SendMessageTimeoutW(HWND_BROADCAST, priv, 1, 2, SMTO_ABORTIFHUNG, 1500, &res);
    HWND w = xs_find(&s, XW_PLAIN);
    /* the server registers no such message itself; the count is of what its windows received */
    LRESULT n = SendMessageW(w, MSC_COUNT, priv, 0);
    CHECKF(n == XW_COUNT, "server windows that saw the broadcast: %lld of %d", (long long)n, XW_COUNT);
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_destroywindow_on_foreign_window_denied) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    SetLastError(0);
    CHECK(!DestroyWindow(w));
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    CHECK(IsWindow(w));
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_remote_destroy_in_handler_then_stale) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_ANSI), other = xs_find(&s, XW_PLAIN);
    ULONG_PTR res = 0;
    SetLastError(0);
    LRESULT r = SendMessageTimeoutA(w, MSC_SELFDESTROY, 0, 0, SMTO_ERRORONEXIT, 3000, &res);
    /* the handler returns normally after destroying its window: the send completed with the handler's result */
    CHECK(r != 0); EQ(res, 5); EQ(GetLastError(), 0);
    CHECK(!IsWindow(w));
    SetLastError(0); EQ(SendMessageW(w, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    EQ(SendMessageW(other, MSC_ECHO, 1, 2), msc_echo_ret(1, 2));
    EQ(xs_stop(&s), 0);
}
TEST(msc_xproc_receiver_process_exit_releases_sender) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    ULONG_PTR res = 0xAA;
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(w, MSC_EXITPROC, 0, 0, SMTO_NORMAL, 6000, &res);
    double ms = msc_ms_since(t0);
    CHECKF(ms < 4500.0, "sender still blocked %.0f ms after the receiver process exited", ms);
    t_info("process exit mid-send: r=%lld err=%lu after %.0f ms", (long long)r, GetLastError(), ms);
    CHECK(WaitForSingleObject(s.proc, 5000) == WAIT_OBJECT_0);
    DWORD code = 0; GetExitCodeProcess(s.proc, &code);
    EQ(code, 3);
    CHECK(!IsWindow(w));
    SetLastError(0); EQ(SendMessageW(w, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    CloseHandle(s.proc);
}
TEST(msc_xproc_process_exit_with_erroronexit_reports_1400) {
    MSC_NEED_GUI();
    struct xs s; NEED_SERVER(s);
    HWND w = xs_find(&s, XW_PLAIN);
    ULONG_PTR res = 0;
    SetLastError(0);
    LRESULT r = SendMessageTimeoutW(w, MSC_EXITPROC, 0, 0, SMTO_ERRORONEXIT, 6000, &res);
    DWORD err = GetLastError();
    EQ(r, 0); EQ(err, ERROR_INVALID_WINDOW_HANDLE);
    WaitForSingleObject(s.proc, 5000);
    CloseHandle(s.proc);
}

/* ---- UIPI: a low-integrity process cannot send to higher-integrity windows ------------------- */
CHILD(msc_lowclient) {
    int bad = 0; (void)argc;
    DWORD ppid = argc > 1 ? (DWORD)strtoul(argv[1], NULL, 10) : 0;
    if (!msc_have_gui()) return 0x100;
    /* a distinctive title: msc_have_gui() above made and destroyed a transient window of the same class, and the parent
     * must not pick that one up while it looks for the window to serve */
    HWND mine = msc_create_ex(NULL, NULL, 0, NULL, L"win32ktMscLowServed");
    /* The parent created two windows with titles ..._0 (default filter) and ..._1 (MSC_ECHO allowed). */
    WCHAR cls[64], t0[64], t1[64];
    swprintf(cls, 64, L"win32ktMscW_%lu", ppid); xtitle(t0, ppid, 0); xtitle(t1, ppid, 1);
    HWND blocked = NULL, allowed = NULL;
    LONGLONG t = msc_qpc();
    while ((!blocked || !allowed) && msc_ms_since(t) < 8000) { blocked = FindWindowW(cls, t0); allowed = FindWindowW(cls, t1); Sleep(20); }
    if (!blocked || !allowed || !mine) return 0x102;
    ULONG_PTR res = 0; SetLastError(0);
    LRESULT r = SendMessageTimeoutW(blocked, MSC_ECHO, 1, 2, SMTO_ABORTIFHUNG, 2000, &res);
    if (!(r == 0 && GetLastError() == 5)) bad |= 1;          /* UIPI: ERROR_ACCESS_DENIED */
    COPYDATASTRUCT cds = { 1, 4, &res };
    SetLastError(0);
    r = SendMessageTimeoutW(blocked, WM_COPYDATA, (WPARAM)mine, (LPARAM)&cds, SMTO_ABORTIFHUNG, 2000, &res);
    if (!(r == 0 && GetLastError() == 5)) bad |= 2;
    SetLastError(0);
    r = SendMessageTimeoutW(allowed, MSC_ECHO, 3, 4, SMTO_ABORTIFHUNG, 2000, &res);
    if (!(r != 0 && res == (ULONG_PTR)msc_echo_ret(3, 4))) bad |= 4;   /* explicitly allowed message passes */
    SetLastError(0);
    r = SendMessageTimeoutW(allowed, MSC_REPLY, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
    if (!(r == 0 && GetLastError() == 5)) bad |= 8;          /* other messages to the same window stay blocked */
    SetLastError(0);
    BOOL nok = SendNotifyMessageW(blocked, MSC_ECHO, 5, 6);
    if (nok || GetLastError() != 5) bad |= 16;
    /* serve our own window until told to quit: higher IL -> lower IL is allowed */
    LONGLONG t2 = msc_qpc();
    while (!msc_quit_flag && msc_ms_since(t2) < 20000) msc_pump(20);
    return bad;
}
static BOOL make_low_token(HANDLE *out) {
    HANDLE tok = NULL, dup = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_ASSIGN_PRIMARY, &tok)) return FALSE;
    BOOL ok = DuplicateTokenEx(tok, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup);
    CloseHandle(tok);
    if (!ok) return FALSE;
    SID_IDENTIFIER_AUTHORITY ia = SECURITY_MANDATORY_LABEL_AUTHORITY;
    PSID sid = NULL;
    if (!AllocateAndInitializeSid(&ia, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &sid)) { CloseHandle(dup); return FALSE; }
    TOKEN_MANDATORY_LABEL tml; tml.Label.Sid = sid; tml.Label.Attributes = SE_GROUP_INTEGRITY;
    ok = SetTokenInformation(dup, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(sid));
    FreeSid(sid);
    if (!ok) { CloseHandle(dup); return FALSE; }
    *out = dup;
    return TRUE;
}
TEST(msc_xproc_uipi_low_integrity_sender_is_denied) {
    MSC_NEED_GUI();
    if (t_is_wine()) { t_skip("no integrity levels under Wine"); return; }
    unsigned long il = k_integrity_level();
    if (il < SECURITY_MANDATORY_MEDIUM_RID) { t_skip("test process is not at medium integrity or above (IL 0x%lx)", il); return; }
    struct msc_beh *b0, *b1;
    WCHAR t0[64], t1[64]; xtitle(t0, GetCurrentProcessId(), 0); xtitle(t1, GetCurrentProcessId(), 1);
    HWND w0 = msc_create_ex(NULL, &b0, 0, NULL, t0), w1 = msc_create_ex(NULL, &b1, 0, NULL, t1);
    if (!w0 || !w1) { if (w0) DestroyWindow(w0); if (w1) DestroyWindow(w1); t_skip("window"); return; }
    BOOL fok = ChangeWindowMessageFilterEx(w1, MSC_ECHO, MSGFLT_ALLOW, NULL);
    HANDLE low = NULL;
    if (!make_low_token(&low)) { t_skip("cannot derive a low-integrity token (error %lu)", GetLastError()); DestroyWindow(w0); DestroyWindow(w1); return; }
    char cmd[1024]; snprintf(cmd, sizeof cmd, "\"%s\" --child msc_lowclient %lu", t_self_path(), GetCurrentProcessId());
    WCHAR wcmd[1024]; MultiByteToWideChar(CP_ACP, 0, cmd, -1, wcmd, 1024);
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); memset(&pi, 0, sizeof pi); si.cb = sizeof si;
    if (!CreateProcessAsUserW(low, NULL, wcmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DWORD e = GetLastError();
        CloseHandle(low); DestroyWindow(w0); DestroyWindow(w1);
        t_skip("CreateProcessAsUser with a low-integrity token failed (%lu)", e);
        return;
    }
    CloseHandle(low); CloseHandle(pi.hThread);
    CHECK(fok);
    /* serve both windows while the low-integrity client works */
    LONGLONG t = msc_qpc();
    BOOL ready = FALSE; HWND lw = NULL;
    WCHAR lcls[64]; swprintf(lcls, 64, L"win32ktMscW_%lu", pi.dwProcessId);
    while (msc_ms_since(t) < 10000) {
        msc_pump(30);
        if (!lw) lw = FindWindowW(lcls, L"win32ktMscLowServed");
        if (lw) { ready = TRUE; break; }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
    }
    if (ready) {
        /* the child had to finish its UIPI probes before opening... it serves its window only afterwards */
        ULONG_PTR res = 0;
        LRESULT r = SendMessageTimeoutW(lw, MSC_ECHO, 7, 8, SMTO_ABORTIFHUNG, 3000, &res);
        CHECKF(r != 0 && res == (ULONG_PTR)msc_echo_ret(7, 8), "medium -> low send failed (r=%lld err=%lu)", (long long)r, GetLastError());
        SendMessageTimeoutW(lw, MSC_QUIT, 0, 0, SMTO_ABORTIFHUNG, 2000, &res);
    } else t_note("low-integrity client never showed its window");
    LONGLONG t3 = msc_qpc();
    while (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0 && msc_ms_since(t3) < 8000) msc_pump(30);
    DWORD code = 0xFFFF;
    if (WaitForSingleObject(pi.hProcess, 0) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); t_note("low client terminated"); }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code & 16) t_note("SendNotifyMessage from the low-integrity client was not denied with ERROR_ACCESS_DENIED");
    CHECKF((code & ~16UL) == 0, "low-integrity client reported failures 0x%lx (bit0 echo not denied, 1 copydata not denied, 2 allowed echo failed, 3 other msg not denied)", code);
    CHECK(msc_log_count(MSC_ECHO) == 1);            /* only the explicitly allowed send reached a procedure */
    CHECK(msc_log_count(WM_COPYDATA) == 0);
    DestroyWindow(w0); DestroyWindow(w1);
}

