/* msg_send_cross_thread: what the kernel does to message payloads on the way to another thread.
 *
 * The per-message handler is chosen by win32kfull's MessageTable (message id -> NtUserfn*): DWORD,
 * INSTRING, OUTSTRING, COPYDATA, INOUTLPWINDOWPOS, INOUTNCCALCSIZE, ... Each handler copies the
 * payload into a capture buffer, lets the receiver's __fn* thunk (user32 apfnDispatch slot) call the
 * window procedure on that copy, and copies OUT/INOUT data back. A/W window procedures also get their
 * strings and characters converted. The window procedures record what they saw (msc_common.c). */
#include "msc.h"

#define BIGW ((WPARAM)0x0123456789ABCDEFULL)

static HWND mk_main(void) { return msc_create(NULL, NULL, 0, NULL); }
static BOOL acp_single_byte(void) { CPINFO ci; return GetCPInfo(CP_ACP, &ci) && ci.MaxCharSize == 1; }

/* ---- WM_SETTEXT ---------------------------------------------------------------------------- */
TEST(msc_marshal_settext_w_to_w_window) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const WCHAR t[] = L"cross thread W text";
    wk.beh->has_str = 0;
    LRESULT r = SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)t);
    CHECK(r != 0);
    CHECK(wk.beh->has_str);
    EQ(wk.beh->str_msg, WM_SETTEXT);
    EXPECT_WSTR(wk.beh->wstr, t);
    WCHAR got[64] = L"";
    GetWindowTextW(wk.w, got, 64);
    EXPECT_WSTR(got, t);
    if (wk.beh->str_ptr == (LPARAM)t) t_note("receiver saw the sender's own string pointer");
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_a_to_w_window_converts) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const char t[] = "ansi sender text";
    LRESULT r = SendMessageA(wk.w, WM_SETTEXT, 0, (LPARAM)t);
    CHECK(r != 0);
    CHECK(wk.beh->has_str);
    EXPECT_WSTR(wk.beh->wstr, L"ansi sender text");
    WCHAR got[64] = L"";
    GetWindowTextW(wk.w, got, 64);
    EXPECT_WSTR(got, L"ansi sender text");
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_w_to_a_window_converts) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    CHECK(!IsWindowUnicode(wk.wa));
    LRESULT r = SendMessageW(wk.wa, WM_SETTEXT, 0, (LPARAM)L"unicode sender text");
    CHECK(r != 0);
    CHECK(wk.beha->has_str);
    EXPECT_STR(wk.beha->astr, "unicode sender text");
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_a_to_a_window) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    LRESULT r = SendMessageA(wk.wa, WM_SETTEXT, 0, (LPARAM)"ansi to ansi");
    CHECK(r != 0);
    CHECK(wk.beha->has_str);
    EXPECT_STR(wk.beha->astr, "ansi to ansi");
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_nonascii_follows_acp_conversion) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    /* The kernel/user32 conversion uses the ANSI code page; the expectation is computed with the same
     * Win32 conversion APIs, so this holds on any locale. */
    static const char ansi[] = "caf\xE9 \xE0\xFC";
    static const WCHAR uni[] = L"café Ж中";
    WCHAR expw[64]; char expa[64];
    msc_w_of_ansi(ansi, expw, 64);
    SendMessageA(wk.w, WM_SETTEXT, 0, (LPARAM)ansi);
    EXPECT_WSTR(wk.beh->wstr, expw);
    msc_ansi_of_w(uni, expa, 64);
    SendMessageW(wk.wa, WM_SETTEXT, 0, (LPARAM)uni);
    EXPECT_STR(wk.beha->astr, expa);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_null_lparam_clears_text) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"something");
    /* NtUserfnINSTRINGNULL (MessageTable word for WM_SETTEXT) lets a NULL pointer through. */
    wk.beh->has_str = 0; wk.beh->str_ptr = 1;
    LRESULT r = SendMessageW(wk.w, WM_SETTEXT, 0, 0);
    CHECK(r != 0);
    EQ(wk.beh->str_ptr, 0);
    CHECK(!wk.beh->has_str);
    WCHAR got[16] = L"x";
    GetWindowTextW(wk.w, got, 16);
    EXPECT_WSTR(got, L"");
    wk.beha->str_ptr = 1;
    SendMessageA(wk.wa, WM_SETTEXT, 0, 0);
    EQ(wk.beha->str_ptr, 0);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_empty_string) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"before");
    LRESULT r = SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"");
    CHECK(r != 0);
    CHECK(wk.beh->has_str);
    EXPECT_WSTR(wk.beh->wstr, L"");
    EQ(SendMessageW(wk.w, WM_GETTEXTLENGTH, 0, 0), 0);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_long_strings_arrive_whole) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const int lens[] = { 255, 256, 3000, 20000 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++) {
        int n = lens[i];
        WCHAR *buf = (WCHAR *)malloc((n + 1) * sizeof(WCHAR));
        WCHAR *out = (WCHAR *)malloc((n + 2) * sizeof(WCHAR));
        if (!buf || !out) { free(buf); free(out); t_skip("oom"); break; }
        for (int k = 0; k < n; k++) buf[k] = L'a' + (k % 26);
        buf[n] = 0;
        LRESULT r = SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)buf);
        CHECKF(r != 0, "len %d", n);
        LRESULT tl = SendMessageW(wk.w, WM_GETTEXTLENGTH, 0, 0);
        CHECKF(tl == n, "len %d: WM_GETTEXTLENGTH = %lld", n, (long long)tl);
        LRESULT g = SendMessageW(wk.w, WM_GETTEXT, n + 1, (LPARAM)out);
        CHECKF(g == n, "len %d: WM_GETTEXT = %lld", n, (long long)g);
        CHECKF(g == n && memcmp(out, buf, n * sizeof(WCHAR)) == 0, "len %d: content differs", n);
        CHECKF(wcsncmp(wk.beh->wstr, buf, n < 255 ? n : 255) == 0, "len %d: receiver's prefix differs", n);
        free(buf); free(out);
    }
    msc_worker_stop(&wk);
}

/* ---- WM_GETTEXT / WM_GETTEXTLENGTH --------------------------------------------------------- */
TEST(msc_marshal_gettext_buffer_protocol_w) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const WCHAR t[] = L"gettext-protocol";         /* 16 chars */
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)t);
    static const int cch[] = { 1, 2, 5, 15, 16, 17, 18, 100, 4096 };
    for (unsigned i = 0; i < sizeof cch / sizeof *cch; i++) {
        WCHAR buf[4200]; for (int k = 0; k < 4200; k++) buf[k] = 0x7777;
        LRESULT r = SendMessageW(wk.w, WM_GETTEXT, cch[i], (LPARAM)buf);
        int want = cch[i] - 1 < 16 ? cch[i] - 1 : 16;
        CHECKF(r == want, "cch %d: returned %lld want %d", cch[i], (long long)r, want);
        CHECKF(wcsncmp(buf, t, want) == 0 && buf[want] == 0, "cch %d: bad content/terminator", cch[i]);
    }
    msc_worker_stop(&wk);
}
TEST(msc_marshal_gettext_buffer_protocol_a_sender) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"gettext-protocol");
    static const int cch[] = { 1, 2, 5, 16, 17, 100 };
    for (unsigned i = 0; i < sizeof cch / sizeof *cch; i++) {
        char buf[200]; memset(buf, 0x55, sizeof buf);
        LRESULT r = SendMessageA(wk.w, WM_GETTEXT, cch[i], (LPARAM)buf);
        int want = cch[i] - 1 < 16 ? cch[i] - 1 : 16;
        CHECKF(r == want, "cch %d: returned %lld want %d", cch[i], (long long)r, want);
        CHECKF(strncmp(buf, "gettext-protocol", want) == 0 && buf[want] == 0, "cch %d: bad content", cch[i]);
    }
    msc_worker_stop(&wk);
}
TEST(msc_marshal_gettext_zero_cch_returns_zero) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"abc");
    WCHAR buf[8] = { 0x7777, 0x7777 };
    EQ(SendMessageW(wk.w, WM_GETTEXT, 0, (LPARAM)buf), 0);
    if (buf[0] != 0x7777) t_note("WM_GETTEXT with cch 0 touched the buffer (0x%x)", buf[0]);
    char abuf[8] = { 0x55 };
    EQ(SendMessageA(wk.w, WM_GETTEXT, 0, (LPARAM)abuf), 0);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_gettext_between_ansi_and_unicode_windows) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SendMessageA(wk.wa, WM_SETTEXT, 0, (LPARAM)"ansi window title");
    char ab[64] = ""; WCHAR wb[64] = L"";
    EQ(SendMessageA(wk.wa, WM_GETTEXT, 64, (LPARAM)ab), 17); EXPECT_STR(ab, "ansi window title");
    EQ(SendMessageW(wk.wa, WM_GETTEXT, 64, (LPARAM)wb), 17); EXPECT_WSTR(wb, L"ansi window title");
    EQ(SendMessageW(wk.wa, WM_GETTEXTLENGTH, 0, 0), 17);
    EQ(SendMessageA(wk.wa, WM_GETTEXTLENGTH, 0, 0), 17);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_gettextlength_nonascii_counts_in_caller_charset) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const WCHAR uni[] = L"café Ж中!";
    char exp[64]; int bytes = msc_ansi_of_w(uni, exp, 64) - 1;
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)uni);
    EQ(SendMessageW(wk.w, WM_GETTEXTLENGTH, 0, 0), (int)wcslen(uni));
    /* NtUserfnGETDBCSTEXTLENGTHS: the ANSI caller gets the DBCS-aware byte length. */
    EQ(SendMessageA(wk.w, WM_GETTEXTLENGTH, 0, 0), bytes);
    char got[64]; memset(got, 0, sizeof got);
    EQ(SendMessageA(wk.w, WM_GETTEXT, 64, (LPARAM)got), bytes);
    EXPECT_STR(got, exp);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_gettext_huge_buffer_request) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"short title");
    int n = 100000;
    WCHAR *buf = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (!buf) { msc_worker_stop(&wk); t_skip("oom"); return; }
    EQ(SendMessageW(wk.w, WM_GETTEXT, n, (LPARAM)buf), 11);
    EXPECT_WSTR(buf, L"short title");
    free(buf);
    msc_worker_stop(&wk);
}

/* ---- WM_COPYDATA --------------------------------------------------------------------------- */
static void fill_pat(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(i * 31 + 7); }
static void copydata_case(struct msc_worker *wk, HWND target, struct msc_beh *b, size_t n, ULONG_PTR dw, WPARAM wparam, LRESULT want_ret) {
    unsigned char *src = (unsigned char *)malloc(n ? n : 1);
    unsigned char *keep = (unsigned char *)malloc(n ? n : 1);
    if (!src || !keep) { free(src); free(keep); CHECK(0); return; }
    fill_pat(src, n); memcpy(keep, src, n);
    COPYDATASTRUCT cds = { dw, (DWORD)n, n ? src : NULL };
    b->cd_ret = want_ret; b->cd_ok = FALSE; b->cd_cb = 0xFFFFFFFF; b->cd_data = 0;
    LRESULT r = SendMessageW(target, WM_COPYDATA, wparam, (LPARAM)&cds);
    CHECKF(r == want_ret, "size %zu: SendMessage returned %lld want %lld", n, (long long)r, (long long)want_ret);
    EQ(b->cd_data, dw);
    EQ(b->cd_cb, n);
    EQ(b->cd_w, wparam);
    CHECK(b->cd_ok);
    size_t cmp = n < sizeof b->cd_bytes ? n : sizeof b->cd_bytes;
    CHECKF(memcmp(b->cd_bytes, keep, cmp) == 0, "size %zu: payload differs", n);
    CHECKF(memcmp(src, keep, n) == 0, "size %zu: sender's buffer was modified", n);
    if (n && b->cd_lp == src) t_note("size %zu: receiver saw the sender's own lpData pointer", n);
    free(src); free(keep);
}
TEST(msc_marshal_copydata_sizes_to_unicode_window) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HWND me = mk_main();
    static const size_t sz[] = { 0, 1, 2, 7, 8, 9, 63, 64, 511, 512, 513, 4096, 65536, 262144 };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) copydata_case(&wk, wk.w, wk.beh, sz[i], 0x1000 + i, (WPARAM)me, 1);
    if (me) DestroyWindow(me);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_copydata_sizes_to_ansi_window) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const size_t sz[] = { 0, 1, 33, 1024, 70000 };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) copydata_case(&wk, wk.wa, wk.beha, sz[i], 0xA0 + i, 0x99, 1);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_copydata_return_value_and_wparam_roundtrip) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const LRESULT rv[] = { 0, 1, -1, 12345, (LRESULT)0x100000000LL };
    for (unsigned i = 0; i < sizeof rv / sizeof *rv; i++) copydata_case(&wk, wk.w, wk.beh, 16, (ULONG_PTR)BIGW, BIGW, rv[i]);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_copydata_dwdata_64bit) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const ULONG_PTR dw[] = { 0, 1, 0xFFFFFFFFULL, 0x100000000ULL, 0xFFFFFFFFFFFFFFFFULL, 0x8000000000000000ULL };
    for (unsigned i = 0; i < sizeof dw / sizeof *dw; i++) copydata_case(&wk, wk.w, wk.beh, 4, dw[i], 5, 1);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_copydata_receiver_writes_stay_local) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    wk.beh->cd_scribble = 1;
    copydata_case(&wk, wk.w, wk.beh, 300, 7, 8, 1);   /* copydata_case verifies the sender's buffer afterwards */
    wk.beh->cd_scribble = 0;
    msc_worker_stop(&wk);
}
TEST(msc_marshal_copydata_oversized_cbdata_is_refused) {
    MSC_NEED_GUI();
    if (t_is_wine()) { t_skip("Wine would try to copy 4 GB"); return; }
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static unsigned char dummy[16];
    static const DWORD big[] = { 0xFFFFFFE8u, 0xFFFFFFF0u, 0xFFFFFFFFu };
    /* the cross-thread send path, case 0x4A: cbData + 0x18 wrapping a 32-bit add => return 0 before any copy. */
    for (unsigned i = 0; i < 3; i++) {
        COPYDATASTRUCT cds = { 1, big[i], dummy };
        wk.beh->cd_cb = 0xAAAA; wk.beh->cd_ok = FALSE;
        LRESULT r = SendMessageW(wk.w, WM_COPYDATA, 0, (LPARAM)&cds);
        CHECKF(r == 0, "cbData %08x: returned %lld", big[i], (long long)r);
        CHECKF(wk.beh->cd_cb == 0xAAAA, "cbData %08x: procedure was entered", big[i]);
    }
    msc_worker_stop(&wk);
}
#define SLOT_COPYDATA 0
#define SLOT_DWORD 2
#define SLOT_INSTRING 26         /* LB_ADDSTRING and the other string-in messages */
#define SLOT_INSTRINGNULL 27     /* WM_SETTEXT (the string may be NULL) */
#define SLOT_OUTSTRING 35
TEST(msc_marshal_copydata_dispatches_fnCOPYDATA_slot) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (k_cb_count() <= SLOT_OUTSTRING) { msc_worker_stop(&wk); t_skip("no callback table"); return; }
    CHECK(k_cb_hook(SLOT_COPYDATA));
    unsigned char d[32]; fill_pat(d, 32);
    COPYDATASTRUCT cds = { 5, 32, d };
    SendMessageW(wk.w, WM_COPYDATA, 0, (LPARAM)&cds);
    unsigned long hits = k_cb_hit_count(SLOT_COPYDATA);
    if (hits == 0 && t_is_wine()) t_note("no kernel callbacks under Wine"); else CHECKF(hits >= 1, "__fnCOPYDATA hits %lu", hits);
    k_cb_unhook_all();
    msc_worker_stop(&wk);
}
TEST(msc_marshal_settext_gettext_dispatch_instring_outstring_slots) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (k_cb_count() <= SLOT_OUTSTRING) { msc_worker_stop(&wk); t_skip("no callback table"); return; }
    CHECK(k_cb_hook(SLOT_INSTRINGNULL)); CHECK(k_cb_hook(SLOT_OUTSTRING));
    SendMessageW(wk.w, WM_SETTEXT, 0, (LPARAM)L"slots");
    unsigned long in1 = k_cb_hit_count(SLOT_INSTRINGNULL), out1 = k_cb_hit_count(SLOT_OUTSTRING);
    WCHAR b[16]; SendMessageW(wk.w, WM_GETTEXT, 16, (LPARAM)b);
    unsigned long in2 = k_cb_hit_count(SLOT_INSTRINGNULL), out2 = k_cb_hit_count(SLOT_OUTSTRING);
    if (in1 == 0 && t_is_wine()) t_note("no kernel callbacks under Wine");
    else {
        CHECKF(in1 >= 1, "SETTEXT: string-in callback hits %lu", in1);
        CHECKF(out2 > out1, "GETTEXT: __fnOUTSTRING hits %lu -> %lu", out1, out2);
        CHECKF(in2 == in1, "GETTEXT bumped the string-in callback (%lu -> %lu)", in1, in2);
    }
    k_cb_unhook_all();
    msc_worker_stop(&wk);
}
TEST(msc_marshal_report_dispatch_slots_per_message_kind) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    unsigned n = k_cb_count();
    if (n < 40 || t_is_wine()) { msc_worker_stop(&wk); t_skip("no callback table"); return; }
    struct { const char *name; UINT msg; } k[] = { { "WM_USER", MSC_ECHO }, { "WM_GETMINMAXINFO", WM_GETMINMAXINFO },
        { "WM_WINDOWPOSCHANGING", WM_WINDOWPOSCHANGING }, { "WM_WINDOWPOSCHANGED", WM_WINDOWPOSCHANGED },
        { "WM_NCCALCSIZE", WM_NCCALCSIZE }, { "WM_STYLECHANGING", WM_STYLECHANGING }, { "WM_SIZING", WM_SIZING },
        { "WM_MEASUREITEM", WM_MEASUREITEM }, { "WM_DRAWITEM", WM_DRAWITEM }, { "EM_GETRECT", EM_GETRECT } };
    for (unsigned i = 0; i < sizeof k / sizeof *k; i++) {
        unsigned char buf[256]; memset(buf, 0, sizeof buf);
        if (k[i].msg == WM_WINDOWPOSCHANGING || k[i].msg == WM_WINDOWPOSCHANGED) ((WINDOWPOS *)buf)->hwnd = wk.w;
        if (k[i].msg == WM_DRAWITEM) ((DRAWITEMSTRUCT *)buf)->hwndItem = wk.w;
        for (unsigned s = 0; s < n && s < K_CB_MAX; s++) k_cb_hook(s);
        wk.beh->cap_msg = k[i].msg; wk.beh->cap_len = 0; wk.beh->mut_on = 0; wk.beh->cap_ret = 0;
        if (k[i].msg == WM_NCCALCSIZE) { NCCALCSIZE_PARAMS np; memset(&np, 0, sizeof np); WINDOWPOS wp; memset(&wp, 0, sizeof wp); wp.hwnd = wk.w; np.lppos = &wp; SendMessageW(wk.w, k[i].msg, TRUE, (LPARAM)&np); }
        else SendMessageW(wk.w, k[i].msg, 0, (LPARAM)buf);
        char line[400] = ""; int off = 0;
        for (unsigned s = 0; s < n && s < K_CB_MAX; s++) { unsigned long h = k_cb_hit_count(s); if (h) off += snprintf(line + off, sizeof line - off, " [%u]x%lu", s, h); }
        t_note("%s -> callback slots hit:%s", k[i].name, line);
        k_cb_unhook_all();
        wk.beh->cap_msg = 0;
    }
    msc_worker_stop(&wk);
}

/* ---- struct messages: IN / INOUT / OUT -------------------------------------------------------- */
struct scase {
    const char *name; UINT msg; WPARAM w; size_t size; int inout; size_t mut_off; DWORD mut_val;
    void (*init)(unsigned char *buf, HWND target);
};
static void init_wp(unsigned char *b, HWND t) { WINDOWPOS *p = (WINDOWPOS *)b; p->hwnd = t; p->hwndInsertAfter = NULL; p->x = 11; p->y = -22; p->cx = 333; p->cy = 444; p->flags = SWP_NOMOVE | SWP_NOZORDER; }
/* WM_DRAWITEM is only delivered when the structure carries a DC that exists: a NULL hDC is dropped by the kernel without
 * reaching the window procedure. The screen DC taken here is released by run_scase() after the send. */
static void init_drawitem(unsigned char *b, HWND t) { DRAWITEMSTRUCT *p = (DRAWITEMSTRUCT *)b; p->hwndItem = t; p->hDC = GetDC(NULL); p->CtlType = ODT_LISTBOX; p->CtlID = 9; p->itemID = 3; p->itemAction = ODA_DRAWENTIRE; p->itemState = ODS_SELECTED; SetRect(&p->rcItem, 1, 2, 3, 4); p->itemData = 0x123456789ULL; }
static void init_delitem(unsigned char *b, HWND t) { DELETEITEMSTRUCT *p = (DELETEITEMSTRUCT *)b; p->CtlType = ODT_LISTBOX; p->CtlID = 9; p->itemID = 2; p->hwndItem = t; p->itemData = 0xABCDEF012ULL; }
static void init_cmpitem(unsigned char *b, HWND t) { COMPAREITEMSTRUCT *p = (COMPAREITEMSTRUCT *)b; p->CtlType = ODT_LISTBOX; p->CtlID = 9; p->hwndItem = t; p->itemID1 = 1; p->itemData1 = 0x1111; p->itemID2 = 2; p->itemData2 = 0x2222; p->dwLocaleId = 0x409; }
static void init_help(unsigned char *b, HWND t) { HELPINFO *p = (HELPINFO *)b; p->cbSize = sizeof *p; p->iContextType = HELPINFO_WINDOW; p->iCtrlId = 12; p->hItemHandle = t; p->dwContextId = 99; p->MousePos.x = 5; p->MousePos.y = 6; }
static void init_msg(unsigned char *b, HWND t) { MSG *p = (MSG *)b; p->hwnd = t; p->message = WM_KEYDOWN; p->wParam = 'X'; p->lParam = 0x1234; p->time = 77; p->pt.x = 8; p->pt.y = 9; }
static void init_scroll(unsigned char *b, HWND t) { SCROLLINFO *p = (SCROLLINFO *)b; p->cbSize = sizeof *p; p->fMask = SIF_ALL; p->nMin = 0; p->nMax = 100; p->nPage = 10; p->nPos = 20; p->nTrackPos = 30; }
static void init_measure(unsigned char *b, HWND t) { MEASUREITEMSTRUCT *p = (MEASUREITEMSTRUCT *)b; p->CtlType = ODT_LISTBOX; p->CtlID = 9; p->itemID = 4; p->itemWidth = 50; p->itemHeight = 15; p->itemData = 0x777; }
static void init_rect(unsigned char *b, HWND t) { SetRect((RECT *)b, 10, 20, 310, 220); }
static void init_style(unsigned char *b, HWND t) { STYLESTRUCT *p = (STYLESTRUCT *)b; p->styleOld = WS_POPUP; p->styleNew = WS_POPUP | WS_BORDER; }
static void init_mmi(unsigned char *b, HWND t) { MINMAXINFO *p = (MINMAXINFO *)b; p->ptReserved.x = 1; p->ptReserved.y = 2; p->ptMaxSize.x = 800; p->ptMaxSize.y = 600; p->ptMaxPosition.x = 3; p->ptMaxPosition.y = 4; p->ptMinTrackSize.x = 100; p->ptMinTrackSize.y = 50; p->ptMaxTrackSize.x = 1600; p->ptMaxTrackSize.y = 1200; }

/* Cases marked inout are NtUserfnINOUT* handlers in the MessageTable decode: the receiver's edits
 * must be visible in the sender's buffer after SendMessage returns. */
static const struct scase g_inout[] = {
    { "WM_GETMINMAXINFO",     WM_GETMINMAXINFO,     0,           sizeof(MINMAXINFO),         1, offsetof(MINMAXINFO, ptMaxSize.x), 0x1111, init_mmi },
    { "WM_WINDOWPOSCHANGING", WM_WINDOWPOSCHANGING, 0,           sizeof(WINDOWPOS),          1, offsetof(WINDOWPOS, cx), 0x2222, init_wp },
    { "WM_MEASUREITEM",       WM_MEASUREITEM,       9,           sizeof(MEASUREITEMSTRUCT),  1, offsetof(MEASUREITEMSTRUCT, itemHeight), 0x33, init_measure },
    { "WM_SIZING",            WM_SIZING,            WMSZ_LEFT,   sizeof(RECT),               1, offsetof(RECT, left), 0x44, init_rect },
    { "WM_MOVING",            WM_MOVING,            0,           sizeof(RECT),               1, offsetof(RECT, top), 0x55, init_rect },
    { "EM_SETRECT",           EM_SETRECT,           0,           sizeof(RECT),               1, offsetof(RECT, right), 0x66, init_rect },
    { "WM_STYLECHANGING",     WM_STYLECHANGING,     (WPARAM)GWL_STYLE, sizeof(STYLESTRUCT),  1, offsetof(STYLESTRUCT, styleNew), 0x20000000, init_style },
    { "SBM_GETSCROLLINFO",    SBM_GETSCROLLINFO,    0,           sizeof(SCROLLINFO),         1, offsetof(SCROLLINFO, nPos), 0x77, init_scroll },
    { "SBM_SETSCROLLINFO",    SBM_SETSCROLLINFO,    1,           sizeof(SCROLLINFO),         1, offsetof(SCROLLINFO, nPage), 0x88, init_scroll },
};
static const struct scase g_in[] = {
    { "WM_WINDOWPOSCHANGED", WM_WINDOWPOSCHANGED, 0, sizeof(WINDOWPOS),         0, offsetof(WINDOWPOS, x), 0x99, init_wp },
    { "WM_DRAWITEM",         WM_DRAWITEM,         9, sizeof(DRAWITEMSTRUCT),    0, offsetof(DRAWITEMSTRUCT, itemID), 0x98, init_drawitem },
    { "WM_DELETEITEM",       WM_DELETEITEM,       9, sizeof(DELETEITEMSTRUCT),  0, offsetof(DELETEITEMSTRUCT, itemID), 0x97, init_delitem },
    { "WM_COMPAREITEM",      WM_COMPAREITEM,      9, sizeof(COMPAREITEMSTRUCT), 0, offsetof(COMPAREITEMSTRUCT, itemID1), 0x96, init_cmpitem },
    { "WM_HELP",             WM_HELP,             0, sizeof(HELPINFO),          0, offsetof(HELPINFO, iCtrlId), 0x95, init_help },
    { "WM_GETDLGCODE",       WM_GETDLGCODE,       'X', sizeof(MSG),             0, offsetof(MSG, time), 0x94, init_msg },
    { "WM_STYLECHANGED",     WM_STYLECHANGED,     (WPARAM)GWL_STYLE, sizeof(STYLESTRUCT), 0, offsetof(STYLESTRUCT, styleNew), 0x93, init_style },
};
static void run_scase(struct msc_worker *wk, HWND target, struct msc_beh *b, int ansi_sender, const struct scase *c) {
    unsigned char buf[512], orig[512], expect[512];
    memset(buf, 0xA7, sizeof buf);
    memset(buf, 0, c->size);       /* padding inside the structure is not carried across the thread boundary: the receiver sees zeros there */
    c->init(buf, target);
    memcpy(orig, buf, sizeof buf);
    b->cap_msg = c->msg; b->cap_len = c->size; b->mut_on = 1; b->mut_off = c->mut_off; b->mut_val = c->mut_val;
    b->cap_ret = 0x4321; b->cap_hits = 0; b->cap_ok = FALSE;
    LRESULT r = ansi_sender ? SendMessageA(target, c->msg, c->w, (LPARAM)buf) : SendMessageW(target, c->msg, c->w, (LPARAM)buf);
    if (c->msg == WM_DRAWITEM && ((DRAWITEMSTRUCT *)orig)->hDC) ReleaseDC(NULL, ((DRAWITEMSTRUCT *)orig)->hDC);
    CHECKF(r == 0x4321, "%s: returned 0x%llx", c->name, (unsigned long long)r);
    CHECKF(b->cap_hits == 1, "%s: procedure entered %ld times", c->name, b->cap_hits);
    CHECKF(b->cap_ok, "%s: payload pointer unreadable in receiver", c->name);
    CHECKF(b->cap_w == c->w, "%s: wParam 0x%llx != 0x%llx", c->name, (unsigned long long)b->cap_w, (unsigned long long)c->w);
    CHECKF(memcmp(b->cap, orig, c->size) == 0, "%s: receiver saw different bytes than the sender built", c->name);
    memcpy(expect, orig, sizeof expect);
    if (c->inout) {
        *(DWORD *)(expect + c->mut_off) = c->mut_val;
        CHECKF(memcmp(buf, expect, c->size) == 0, "%s: INOUT edit (+0x%zx=0x%x) not visible in sender (now 0x%x)", c->name, c->mut_off, c->mut_val, *(DWORD *)(buf + c->mut_off));
    } else {
        if (memcmp(buf, expect, c->size) != 0) t_note("%s: IN-only struct was modified in the sender's buffer", c->name);
    }
    CHECKF(memcmp(buf + c->size, orig + c->size, sizeof buf - c->size) == 0, "%s: bytes beyond the struct were touched", c->name);
    if ((LPARAM)buf == b->cap_ptr) t_note("%s: receiver saw the sender's own pointer", c->name);
    b->cap_msg = 0; b->mut_on = 0;
}
TEST(msc_marshal_inout_structs_copy_back_to_unicode_sender) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    for (unsigned i = 0; i < sizeof g_inout / sizeof *g_inout; i++) run_scase(&wk, wk.w, wk.beh, 0, &g_inout[i]);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_inout_structs_copy_back_to_ansi_window_from_ansi_sender) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    for (unsigned i = 0; i < sizeof g_inout / sizeof *g_inout; i++) run_scase(&wk, wk.wa, wk.beha, 1, &g_inout[i]);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_inout_structs_across_character_sets) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    for (unsigned i = 0; i < sizeof g_inout / sizeof *g_inout; i++) { run_scase(&wk, wk.wa, wk.beha, 0, &g_inout[i]); run_scase(&wk, wk.w, wk.beh, 1, &g_inout[i]); }
    msc_worker_stop(&wk);
}
TEST(msc_marshal_in_structs_arrive_intact) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    for (unsigned i = 0; i < sizeof g_in / sizeof *g_in; i++) { run_scase(&wk, wk.w, wk.beh, 0, &g_in[i]); run_scase(&wk, wk.wa, wk.beha, 1, &g_in[i]); }
    msc_worker_stop(&wk);
}
TEST(msc_marshal_getdlgcode_null_msg_pointer) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    wk.beh->cap_msg = WM_GETDLGCODE; wk.beh->cap_len = 0; wk.beh->cap_ret = DLGC_WANTALLKEYS; wk.beh->cap_hits = 0;
    /* NtUserfnDWORDOPTINLPMSG: the MSG pointer is optional. */
    EQ(SendMessageW(wk.w, WM_GETDLGCODE, 0, 0), DLGC_WANTALLKEYS);
    EQ(wk.beh->cap_ptr, 0);
    EQ(wk.beh->cap_hits, 1);
    wk.beh->cap_msg = 0;
    msc_worker_stop(&wk);
}
TEST(msc_marshal_em_getrect_out_rect) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    /* NtUserfnOUTLPRECT: the receiver fills a kernel temp RECT which is copied to the sender. */
    wk.beh->cap_msg = EM_GETRECT; wk.beh->cap_len = 0; wk.beh->cap_ret = 5;
    wk.beh->mut_on = 1; wk.beh->mut_off = offsetof(RECT, right); wk.beh->mut_val = 4321;
    RECT rc; memset(&rc, 0x11, sizeof rc);
    EQ(SendMessageW(wk.w, EM_GETRECT, 0, (LPARAM)&rc), 5);
    EQ(rc.right, 4321);
    wk.beh->mut_on = 0; wk.beh->cap_msg = 0;
    msc_worker_stop(&wk);
}
TEST(msc_marshal_optional_dword_out_params_em_getsel_sbm_getrange) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const UINT ms[] = { EM_GETSEL, SBM_GETRANGE };
    for (unsigned i = 0; i < 2; i++) {
        wk.beh->cap_msg = ms[i]; wk.beh->cap_len = 0; wk.beh->cap_ret = 0x00090003; wk.beh->wr_wl = 1; wk.beh->wr_w_val = 3; wk.beh->wr_l_val = 9;
        DWORD a = 0xAAAAAAAA, b = 0xBBBBBBBB;
        EQ(SendMessageW(wk.w, ms[i], (WPARAM)&a, (LPARAM)&b), 0x00090003);
        CHECKF(a == 3 && b == 9, "msg %x: out values %x %x", ms[i], a, b);
        /* either pointer may be NULL (OPTOUT) */
        a = 0xAAAAAAAA; b = 0xBBBBBBBB;
        SendMessageW(wk.w, ms[i], (WPARAM)&a, 0);
        CHECKF(a == 3 && b == 0xBBBBBBBB, "msg %x: NULL lParam: %x %x", ms[i], a, b);
        a = 0xAAAAAAAA;
        SendMessageW(wk.w, ms[i], 0, (LPARAM)&b);
        CHECKF(a == 0xAAAAAAAA && b == 9, "msg %x: NULL wParam: %x %x", ms[i], a, b);
        EQ(SendMessageW(wk.w, ms[i], 0, 0), 0x00090003);
        wk.beh->wr_wl = 0; wk.beh->cap_msg = 0;
    }
    msc_worker_stop(&wk);
}

/* ---- WM_NCCALCSIZE ----------------------------------------------------------------------- */
TEST(msc_marshal_nccalcsize_true_params_and_windowpos) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *b = wk.beh;
    NCCALCSIZE_PARAMS np; WINDOWPOS wp;
    memset(&np, 0, sizeof np); memset(&wp, 0, sizeof wp);
    SetRect(&np.rgrc[0], 100, 110, 500, 510); SetRect(&np.rgrc[1], 1, 2, 3, 4); SetRect(&np.rgrc[2], 5, 6, 7, 8);
    wp.hwnd = wk.w; wp.x = 100; wp.y = 110; wp.cx = 400; wp.cy = 400; wp.flags = SWP_NOZORDER;
    np.lppos = &wp;
    NCCALCSIZE_PARAMS o = np; WINDOWPOS owp = wp;
    b->cap_msg = WM_NCCALCSIZE; b->mut_on = 1; b->mut_val = 7; b->cap_ret = 0x1234; b->nc_hits = 0;
    LRESULT r = SendMessageW(wk.w, WM_NCCALCSIZE, TRUE, (LPARAM)&np);
    EQ(r, 0x1234);
    EQ(b->nc_hits, 1);
    EQ(b->nc_w, TRUE);
    CHECK(memcmp(b->nc_rect, o.rgrc, sizeof o.rgrc) == 0);
    CHECK(b->nc_wp_ok);
    CHECK(b->nc_wp_ok && memcmp(&b->nc_wp, &owp, sizeof owp) == 0);
    EQ(np.rgrc[0].left, o.rgrc[0].left + 7);
    EQ(np.rgrc[0].right, o.rgrc[0].right - 7);
    EQ(np.rgrc[0].top, o.rgrc[0].top); EQ(np.rgrc[0].bottom, o.rgrc[0].bottom);
    CHECK(memcmp(&np.rgrc[1], &o.rgrc[1], sizeof(RECT)) == 0);
    CHECK(memcmp(&np.rgrc[2], &o.rgrc[2], sizeof(RECT)) == 0);
    EQ(np.lppos, &wp);                       /* the sender's lppos pointer is restored */
    CHECK(memcmp(&wp, &owp, sizeof wp) == 0);
    b->cap_msg = 0; b->mut_on = 0;
    msc_worker_stop(&wk);
}
TEST(msc_marshal_nccalcsize_false_single_rect) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *b = wk.beh;
    RECT rc = { 10, 20, 410, 320 };
    b->cap_msg = WM_NCCALCSIZE; b->mut_on = 1; b->mut_val = 9; b->cap_ret = 0; b->nc_hits = 0;
    SendMessageW(wk.w, WM_NCCALCSIZE, FALSE, (LPARAM)&rc);
    EQ(b->nc_hits, 1); EQ(b->nc_w, FALSE);
    EQ(b->nc_rect[0].left, 10); EQ(b->nc_rect[0].top, 20); EQ(b->nc_rect[0].right, 410); EQ(b->nc_rect[0].bottom, 320);
    EQ(rc.left, 19); EQ(rc.bottom, 311);
    EQ(rc.top, 20); EQ(rc.right, 410);
    b->cap_msg = 0; b->mut_on = 0;
    msc_worker_stop(&wk);
}

/* ---- messages that cannot cross threads / are passed through untouched ------------------------ */
TEST(msc_marshal_wm_create_and_nccreate_refused_across_threads) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    CREATESTRUCTW cs; memset(&cs, 0, sizeof cs);
    cs.lpszName = L"cs-name"; cs.lpszClass = L"cs-class"; cs.cx = 5; cs.cy = 6;
    wk.beh->cap_msg = WM_CREATE; wk.beh->cap_len = sizeof cs; wk.beh->cap_hits = 0;
    /* the cross-thread send path, case 1 / 0x81: UserSetLastError(ERROR_ACCESS_DENIED), return 0. */
    static const UINT ms[] = { WM_CREATE, WM_NCCREATE };
    for (unsigned i = 0; i < 2; i++) {
        SetLastError(0); EQ(SendMessageW(wk.w, ms[i], 0, (LPARAM)&cs), 0); EXPECT_ERR(5);
        SetLastError(0); EQ(SendMessageA(wk.wa, ms[i], 0, (LPARAM)&cs), 0); EXPECT_ERR(5);
    }
    EQ(wk.beh->cap_hits, 0);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_wm_create_same_thread_reaches_procedure) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = msc_create(NULL, &b, 0, NULL);
    if (!h) { t_skip("window"); return; }
    CREATESTRUCTW cs; memset(&cs, 0, sizeof cs);
    cs.lpszName = L"cs-name-same"; cs.cx = 5; cs.cy = 6; cs.x = 7; cs.y = 8; cs.style = 0x1234;
    b->cap_msg = WM_CREATE; b->cap_len = sizeof cs;
    /* same-thread send: no kernel crossing, so the 1/0x81 restriction does not apply */
    SetLastError(0);
    SendMessageW(h, WM_CREATE, 0, (LPARAM)&cs);
    EQ(b->cap_hits, 1);
    CHECK(b->has_str); EXPECT_WSTR(b->wstr, L"cs-name-same");
    CREATESTRUCTW got; memcpy(&got, b->cap, sizeof got);
    EQ(got.cx, 5); EQ(got.cy, 6); EQ(got.x, 7); EQ(got.y, 8); EQ(got.style, 0x1234);
    DestroyWindow(h);
}
TEST(msc_marshal_wm_notify_pointer_is_passed_through) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    NMHDR nm = { wk.w, 77, 0xFFFFFFF9u };
    wk.beh->cap_msg = WM_NOTIFY; wk.beh->cap_len = sizeof nm; wk.beh->cap_ret = 0x4e; wk.beh->cap_hits = 0;
    /* WM_NOTIFY is NtUserfnDWORD in the MessageTable: lParam is delivered as the raw pointer value. */
    EQ(SendMessageW(wk.w, WM_NOTIFY, 77, (LPARAM)&nm), 0x4e);
    EQ(wk.beh->cap_ptr, (LPARAM)&nm);
    EQ(wk.beh->cap_w, 77);
    CHECK(wk.beh->cap_ok);
    CHECK(memcmp(wk.beh->cap, &nm, sizeof nm) == 0);
    wk.beh->cap_msg = 0;
    msc_worker_stop(&wk);
}

/* ---- character messages ------------------------------------------------------------------- */
TEST(msc_marshal_wm_char_family_a_to_w_and_w_to_a) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const UINT ms[] = { WM_CHAR, WM_DEADCHAR, WM_SYSCHAR, WM_SYSDEADCHAR, WM_MENUCHAR, WM_IME_CHAR };
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        UINT m = ms[i];
        /* ASCII passes unchanged in both directions */
        wk.beh->chr_msg = m; wk.beha->chr_msg = m; wk.beh->chr_hits = wk.beha->chr_hits = 0;
        SendMessageA(wk.w, m, 'A', 0x00010001);
        CHECKF(wk.beh->chr_hits == 1 && wk.beh->chr_w == 'A', "msg %x A->W 'A': hits %ld w=%llx", m, wk.beh->chr_hits, (unsigned long long)wk.beh->chr_w);
        SendMessageW(wk.wa, m, L'z', 0x00010001);
        CHECKF(wk.beha->chr_hits == 1 && wk.beha->chr_w == 'z', "msg %x W->A 'z': hits %ld w=%llx", m, wk.beha->chr_hits, (unsigned long long)wk.beha->chr_w);
        if (m != WM_IME_CHAR && m != WM_MENUCHAR && acp_single_byte()) {
            /* Latin-1 letter: converted through the ANSI code page both ways */
            WCHAR w1 = 0; MultiByteToWideChar(CP_ACP, 0, "\xE9", 1, &w1, 1);
            SendMessageA(wk.w, m, 0xE9, 0);
            CHECKF(wk.beh->chr_w == w1, "msg %x A->W 0xE9: got %llx want %x", m, (unsigned long long)wk.beh->chr_w, w1);
            char a1[4] = { 0 }; int nb = WideCharToMultiByte(CP_ACP, 0, L"é", 1, a1, 4, NULL, NULL);
            SendMessageW(wk.wa, m, 0x00E9, 0);
            if (nb == 1) CHECKF(wk.beha->chr_w == (unsigned char)a1[0], "msg %x W->A U+00E9: got %llx want %x", m, (unsigned long long)wk.beha->chr_w, (unsigned char)a1[0]);
        }
    }
    wk.beh->chr_msg = wk.beha->chr_msg = 0;
    msc_worker_stop(&wk);
}
TEST(msc_marshal_wm_char_lparam_untouched) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    SendMessageA(wk.w, WM_CHAR, 'q', 0x4000FFFF);
    SendMessageW(wk.wa, WM_CHAR, L'q', 0x2001ABCD);
    struct msc_ev e;
    CHECK(msc_log_find(WM_CHAR, 0, &e)); EQ(e.l, 0x4000FFFF);
    CHECK(msc_log_find(WM_CHAR, 1, &e)); EQ(e.l, 0x2001ABCD);
    msc_worker_stop(&wk);
}

/* ---- list/combo string messages on a plain window and on real controls ----------------------- */
TEST(msc_marshal_lbcb_string_messages_reach_plain_window_converted) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const UINT ms[] = { LB_ADDSTRING, LB_INSERTSTRING, LB_FINDSTRING, LB_FINDSTRINGEXACT, LB_SELECTSTRING,
                               CB_ADDSTRING, CB_INSERTSTRING, CB_FINDSTRING, CB_FINDSTRINGEXACT, CB_SELECTSTRING };
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        wk.beh->has_str = 0; wk.beha->has_str = 0;
        LRESULT r = SendMessageW(wk.w, ms[i], 1, (LPARAM)L"item-w");
        CHECKF(r == 0x55 && wk.beh->has_str && wcscmp(wk.beh->wstr, L"item-w") == 0 && wk.beh->str_msg == ms[i], "msg %x W->W: r=%llx has=%d", ms[i], (long long)r, wk.beh->has_str);
        r = SendMessageA(wk.w, ms[i], 1, (LPARAM)"item-a");
        CHECKF(r == 0x55 && wk.beh->has_str && wcscmp(wk.beh->wstr, L"item-a") == 0, "msg %x A->W", ms[i]);
        r = SendMessageW(wk.wa, ms[i], 1, (LPARAM)L"item-w");
        CHECKF(r == 0x55 && wk.beha->has_str && strcmp(wk.beha->astr, "item-w") == 0, "msg %x W->A", ms[i]);
        r = SendMessageA(wk.wa, ms[i], 1, (LPARAM)"item-a");
        CHECKF(r == 0x55 && wk.beha->has_str && strcmp(wk.beha->astr, "item-a") == 0, "msg %x A->A", ms[i]);
    }
    msc_worker_stop(&wk);
}
TEST(msc_marshal_real_listbox_roundtrip_across_threads) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_CONTROLS, 0);
    if (!wk.lb) { msc_worker_stop(&wk); t_skip("no listbox"); return; }
    HWND lb = wk.lb;
    EQ(SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"alpha"), 0);
    EQ(SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"gamma"), 1);
    EQ(SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)"delta"), 2);
    EQ(SendMessageW(lb, LB_INSERTSTRING, 1, (LPARAM)L"beta"), 1);
    EQ(SendMessageW(lb, LB_GETCOUNT, 0, 0), 4);
    EQ(SendMessageW(lb, LB_GETTEXTLEN, 1, 0), 4);
    WCHAR wb[32] = L""; char ab[32] = "";
    EQ(SendMessageW(lb, LB_GETTEXT, 1, (LPARAM)wb), 4); EXPECT_WSTR(wb, L"beta");
    EQ(SendMessageA(lb, LB_GETTEXT, 3, (LPARAM)ab), 5); EXPECT_STR(ab, "delta");
    EQ(SendMessageW(lb, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)L"gamma"), 2);
    EQ(SendMessageA(lb, LB_FINDSTRING, (WPARAM)-1, (LPARAM)"de"), 3);
    EQ(SendMessageW(lb, LB_GETTEXTLEN, 99, 0), LB_ERR);
    EQ(SendMessageW(lb, LB_GETTEXT, 99, (LPARAM)wb), LB_ERR);
    /* item data is a full pointer-sized value */
    CHECK(SendMessageW(lb, LB_SETITEMDATA, 2, (LPARAM)0x1122334455667788LL) != LB_ERR);
    EQ(SendMessageW(lb, LB_GETITEMDATA, 2, 0), 0x1122334455667788LL);
    EQ(SendMessageW(lb, LB_SETCURSEL, 3, 0), 3);
    EQ(SendMessageW(lb, LB_GETCURSEL, 0, 0), 3);
    EQ(SendMessageW(lb, LB_DELETESTRING, 0, 0), 3);
    EQ(SendMessageW(lb, LB_GETCOUNT, 0, 0), 3);
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    EQ(SendMessageW(lb, LB_GETCOUNT, 0, 0), 0);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_real_combobox_roundtrip_across_threads) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_CONTROLS, 0);
    if (!wk.cb) { msc_worker_stop(&wk); t_skip("no combobox"); return; }
    HWND cb = wk.cb;
    EQ(SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"one"), 0);
    EQ(SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)"two"), 1);
    EQ(SendMessageW(cb, CB_INSERTSTRING, 1, (LPARAM)L"one-and-half"), 1);
    EQ(SendMessageW(cb, CB_GETCOUNT, 0, 0), 3);
    WCHAR wb[32] = L""; char ab[32] = "";
    EQ(SendMessageW(cb, CB_GETLBTEXTLEN, 1, 0), 12);
    EQ(SendMessageW(cb, CB_GETLBTEXT, 1, (LPARAM)wb), 12); EXPECT_WSTR(wb, L"one-and-half");
    EQ(SendMessageA(cb, CB_GETLBTEXT, 2, (LPARAM)ab), 3); EXPECT_STR(ab, "two");
    EQ(SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)L"two"), 2);
    CHECK(SendMessageW(cb, CB_SETITEMDATA, 0, (LPARAM)0x7766554433221100LL) != CB_ERR);
    EQ(SendMessageW(cb, CB_GETITEMDATA, 0, 0), 0x7766554433221100LL);
    EQ(SendMessageW(cb, CB_SETCURSEL, 2, 0), 2);
    EQ(SendMessageW(cb, CB_GETCURSEL, 0, 0), 2);
    EQ(SendMessageW(cb, CB_GETLBTEXTLEN, 77, 0), CB_ERR);
    msc_worker_stop(&wk);
}
TEST(msc_marshal_real_edit_roundtrip_across_threads) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_CONTROLS, 0);
    if (!wk.ed) { msc_worker_stop(&wk); t_skip("no edit"); return; }
    HWND ed = wk.ed;
    EQ(SendMessageW(ed, WM_SETTEXT, 0, (LPARAM)L"hello world"), TRUE);
    EQ(SendMessageW(ed, WM_GETTEXTLENGTH, 0, 0), 11);
    SendMessageW(ed, EM_SETSEL, 2, 5);
    DWORD s = 0xAAAA, e = 0xBBBB;
    LRESULT r = SendMessageW(ed, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    CHECKF(s == 2 && e == 5, "EM_GETSEL out values %u %u", s, e);
    EQ(r, MAKELONG(2, 5));
    s = e = 0xCCCC;
    SendMessageA(ed, EM_GETSEL, (WPARAM)&s, 0); EQ(s, 2); EQ(e, 0xCCCC);
    EQ(SendMessageW(ed, EM_GETSEL, 0, 0), MAKELONG(2, 5));
    /* EM_GETLINE: first WORD of the buffer carries the buffer size (NtUserfnINCNTOUTSTRING) */
    WCHAR wl[40]; memset(wl, 0, sizeof wl); wl[0] = 39;
    EQ(SendMessageW(ed, EM_GETLINE, 0, (LPARAM)wl), 11); CHECK(wcsncmp(wl, L"hello world", 11) == 0);
    char al[40]; memset(al, 0, sizeof al); *(WORD *)al = 39;
    EQ(SendMessageA(ed, EM_GETLINE, 0, (LPARAM)al), 11); CHECK(strncmp(al, "hello world", 11) == 0);
    EQ(SendMessageW(ed, EM_GETLINECOUNT, 0, 0), 1);
    RECT rc = { 0, 0, 0, 0 };
    SendMessageW(ed, EM_GETRECT, 0, (LPARAM)&rc);
    CHECKF(rc.right > rc.left && rc.bottom > rc.top, "EM_GETRECT %d,%d,%d,%d", rc.left, rc.top, rc.right, rc.bottom);
    SendMessageW(ed, EM_REPLACESEL, TRUE, (LPARAM)L"BIG");
    WCHAR out[40] = L""; GetWindowTextW(ed, out, 40);
    EXPECT_WSTR(out, L"heBIG world");
    SendMessageW(ed, EM_SETLIMITTEXT, 7, 0);
    EQ(SendMessageW(ed, EM_GETLIMITTEXT, 0, 0), 7);
    SendMessageW(ed, EM_SETPASSWORDCHAR, L'*', 0);
    EQ(SendMessageW(ed, EM_GETPASSWORDCHAR, 0, 0), L'*');
    msc_worker_stop(&wk);
}
