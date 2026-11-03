/* gap_03 / user: win32kfull NtUser* syscalls that no other area calls through the win32u stub.
 *
 * NtUserTrackMouseEvent(TRACKMOUSEEVENT*)  cbSize != 0x18 -> ERROR_INVALID_PARAMETER; flags & 0x3fffffec -> ERROR_INVALID_FLAGS
 *                                          (1004); TME_QUERY(0x40000000) -> QueryTrackMouseEvent rewrites the whole 0x18 bytes
 *                                          (cbSize=0x18, other fields 0 unless this thread's window is the tracked one);
 *                                          TME_LEAVE on a window that is not the desktop's tracked window posts
 *                                          WM_MOUSELEAVE (WM_NCMOUSELEAVE with TME_NONCLIENT).
 * NtUserTranslateAccelerator(hwnd, haccel, MSG*)  ValidateHwnd + HMValidateHandle(haccel, TYPE_ACCELTABLE=8), then
 *                                          accelerator matching: WM_KEYDOWN/WM_SYSKEYDOWN match FVIRTKEY entries, WM_CHAR/
 *                                          WM_SYSCHAR the character entries; a hit SENDS WM_COMMAND(wParam = cmd | 0x10000, 0).
 * NtUserSBGetParms(hwnd, bar, SBDATA*, SCROLLINFO*)  overlays SBDATA {min,max,page,pos} on the SCROLLINFO per fMask bit
 *                                          (RANGE/PAGE/POS/TRACKPOS), returns (fMask & 0x17) != 0.
 * NtUserFlashWindowEx(FLASHWINFO*)         cbSize must be 0x20, hwnd valid, (dwFlags & 0xfffffff0) == 0, else 0 + 0x57.
 * NtUserGetAltTabInfo                      hwnd must be NULL or the alt-tab window (else 0 + 0x578); cbSize must be 0x28.
 * NtUserInheritWindowMonitor               desktop/switch-window FNIDs return 0 silently; foreign process -> ERROR_ACCESS_DENIED.
 * Get/SetWindowCompositionAttribute        26 attributes with a per-attribute minimum size (table at win32kfull 0x2e9858).
 * NtUserSetWindowCompositionTransition     hwnd validation then pointer-consistency / top-level checks.
 */
#include "gap3.h"

static char g_cls[64];
static int  g_cmd_count;
static WPARAM g_cmd_wp;
static LPARAM g_cmd_lp;
static HWND g_cmd_hwnd;

static LRESULT CALLBACK gap3_wp(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COMMAND) { g_cmd_count++; g_cmd_wp = w; g_cmd_lp = l; g_cmd_hwnd = h; return 0; }
    return U(DefWindowProcA)(h, m, w, l);
}

static int reg_class(void)
{
    static int done;
    if (done) return 1;
    wsprintfA(g_cls, "win32kt_gap3_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = (WNDPROC)gap3_wp;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = g_cls;
    if (!U(RegisterClassA)(&wc)) return 0;
    done = 1;
    return 1;
}

/* Hidden, off-screen, non-activating popup of our class. */
static HWND mk_top(void)
{
    if (!reg_class()) { t_note("RegisterClassA failed: %lu", GetLastError()); return NULL; }
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_cls, "gap3", WS_POPUP,
                                -32000, -32000, 40, 40, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!h) t_note("CreateWindowExA failed: %lu", GetLastError());
    return h;
}
static HWND mk_child(HWND parent)
{
    return U(CreateWindowExA)(0, g_cls, "gap3c", WS_CHILD, 0, 0, 10, 10, parent, (HMENU)1, GetModuleHandleA(NULL), NULL);
}
#define TOP_OR_SKIP(h) HWND h = mk_top(); if (!h) { t_skip("cannot create hidden window"); return; }

static const ULONG_PTR BAD_HWNDS[] = { 0, 1, 0x1234, 0xdeadbeef, 0x7fffffff, (ULONG_PTR)-1 };
#define NBAD (sizeof BAD_HWNDS / sizeof *BAD_HWNDS)

/* ---- NtUserTrackMouseEvent ---------------------------------------------- */

TEST(gap3_tme_bad_cbsize_invalid_parameter) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    static const DWORD sizes[] = { 0, 1, 0x14, 0x17, 0x19, 0x1c, 0x20, 0x28, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
        t.cbSize = sizes[i]; t.dwFlags = TME_LEAVE; t.hwndTrack = w;
        PLANT();
        EXPECT_EQ(tme(&t), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_tme_invalid_flag_bits_error_1004) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    /* allowed set is HOVER|LEAVE|NONCLIENT|QUERY|CANCEL; every bit of 0x3fffffec outside it is rejected. */
    static const DWORD bad[] = { 0x4, 0x8, 0x20, 0x40, 0x80, 0x100, 0x10000, 0x20000000, 0x3fffffec, 0x5, 0x40000004 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
        t.cbSize = sizeof t; t.dwFlags = bad[i]; t.hwndTrack = w;
        PLANT();
        EXPECT_EQ(tme(&t), 0);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_tme_cbsize_checked_before_flags) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
    t.cbSize = 8; t.dwFlags = 0x3fffffec;
    PLANT();
    EXPECT_EQ(tme(&t), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gap3_tme_invalid_hwnd_fails) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    for (unsigned i = 0; i < NBAD; i++) {
        TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
        t.cbSize = sizeof t; t.dwFlags = TME_LEAVE; t.hwndTrack = (HWND)BAD_HWNDS[i];
        EXPECT_EQ(tme(&t), 0);               /* ValidateHwnd fails -> return 0 */
    }
}

TEST(gap3_tme_query_without_tracking_rewrites_struct) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    TRACKMOUSEEVENT t;
    memset(&t, 0xCC, sizeof t);
    t.cbSize = sizeof t; t.dwFlags = TME_QUERY;
    EXPECT_EQ(tme(&t), 1);
    /* QueryTrackMouseEvent zeroes all 0x18 bytes, then stores cbSize = 0x18; nothing is tracked for this thread. */
    EXPECT_EQ(t.cbSize, 0x18);
    EXPECT_EQ(t.dwFlags, 0);
    EXPECT_EQ(t.hwndTrack, 0);
    EXPECT_EQ(t.dwHoverTime, 0);
    U(DestroyWindow)(w);
}

TEST(gap3_tme_query_ignores_input_hwnd) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
    t.cbSize = sizeof t; t.dwFlags = TME_QUERY; t.hwndTrack = (HWND)0xdeadbeef;
    EXPECT_EQ(tme(&t), 1);                   /* the query path never validates hwndTrack */
    EXPECT_EQ(t.hwndTrack, 0);
}

TEST(gap3_tme_leave_posts_mouseleave) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    MSG m;
    while (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE)) ;
    TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
    t.cbSize = sizeof t; t.dwFlags = TME_LEAVE; t.hwndTrack = w;
    EXPECT_EQ(tme(&t), 1);
    /* A hidden off-screen window is never the desktop's tracked window, so TrackMouseEvent posts the leave at once. */
    BOOL got = U(PeekMessageW)(&m, w, 0x2a3, 0x2a3, PM_REMOVE);
    CHECK(got);
    if (got) { EXPECT_EQ(m.message, 0x2a3); EXPECT_EQ(m.hwnd, w); }
    CHECK(!U(PeekMessageW)(&m, w, 0x2a3, 0x2a3, PM_REMOVE));   /* exactly one */
    U(DestroyWindow)(w);
}

TEST(gap3_tme_nonclient_leave_posts_ncmouseleave) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    MSG m;
    while (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE)) ;
    TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
    t.cbSize = sizeof t; t.dwFlags = TME_LEAVE | TME_NONCLIENT; t.hwndTrack = w;
    EXPECT_EQ(tme(&t), 1);
    /* (~(flags >> 4) & 1) | 0x2a2: with TME_NONCLIENT set the id is 0x2a2 (WM_NCMOUSELEAVE). */
    BOOL got = U(PeekMessageW)(&m, w, 0x2a2, 0x2a3, PM_REMOVE);
    CHECK(got);
    if (got) EXPECT_EQ(m.message, 0x2a2);
    U(DestroyWindow)(w);
}

TEST(gap3_tme_hover_alone_and_cancel_post_nothing) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    static const DWORD fl[] = { TME_HOVER, TME_HOVER | TME_NONCLIENT, TME_CANCEL | TME_LEAVE, TME_CANCEL | TME_HOVER, TME_NONCLIENT, 0 };
    MSG m;
    while (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE)) ;
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
        t.cbSize = sizeof t; t.dwFlags = fl[i]; t.hwndTrack = w; t.dwHoverTime = HOVER_DEFAULT;
        EXPECT_EQ(tme(&t), 1);
        CHECKF(!U(PeekMessageW)(&m, w, 0x2a0, 0x2a3, PM_REMOVE), "flags 0x%lx posted 0x%x", (unsigned long)fl[i], m.message);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_tme_leave_hover_combined_posts_leave) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    MSG m;
    while (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE)) ;
    TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
    t.cbSize = sizeof t; t.dwFlags = TME_LEAVE | TME_HOVER; t.hwndTrack = w;
    EXPECT_EQ(tme(&t), 1);
    CHECK(U(PeekMessageW)(&m, w, 0x2a3, 0x2a3, PM_REMOVE));
    U(DestroyWindow)(w);
}

TEST(gap3_tme_does_not_leave_tracking_state) {
    BOOL (WINAPI *tme)(TRACKMOUSEEVENT *) = (void *)UNX(NtUserTrackMouseEvent);
    REQUIRE(tme);
    TOP_OR_SKIP(w);
    TRACKMOUSEEVENT t; memset(&t, 0, sizeof t);
    t.cbSize = sizeof t; t.dwFlags = TME_HOVER | TME_LEAVE; t.hwndTrack = w;
    tme(&t);
    memset(&t, 0xCC, sizeof t); t.cbSize = sizeof t; t.dwFlags = TME_QUERY;
    EXPECT_EQ(tme(&t), 1);
    EXPECT_EQ(t.dwFlags, 0);                 /* the window was not the tracked one, so no tracking flag is recorded */
    EXPECT_EQ(t.hwndTrack, 0);
    MSG m; while (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE)) ;
    U(DestroyWindow)(w);
}

/* ---- NtUserTranslateAccelerator ---------------------------------------- */

static HACCEL mk_accel(BYTE virt, WORD key, WORD cmd)
{
    /* The table is built from a zeroed buffer with room after the entry: with an uninitialised single ACCEL on the stack,
     * CreateAcceleratorTableW on Windows 10 fails (NULL, ERROR_NOACCESS) depending on the bytes around the 6-byte struct. */
    ACCEL a[2]; memset(a, 0, sizeof a);
    a[0].fVirt = virt; a[0].key = key; a[0].cmd = cmd;
    HACCEL h = U(CreateAcceleratorTableW)(a, 1);
    CHECKF(h != NULL, "CreateAcceleratorTableW failed, error %lu", GetLastError());
    return h;
}

static MSG mk_msg(HWND h, UINT m, WPARAM w)
{
    MSG s; memset(&s, 0, sizeof s);
    s.hwnd = h; s.message = m; s.wParam = w; s.time = GetTickCount();
    return s;
}

TEST(gap3_ta_bad_hwnd_or_accel_returns_zero) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F24, 0x1234);
    CHECK(ac != NULL);
    MSG m = mk_msg(w, WM_KEYDOWN, VK_F24);
    g_cmd_count = 0;
    for (unsigned i = 0; i < NBAD; i++) {
        EXPECT_EQ(ta((HWND)BAD_HWNDS[i], ac, &m), 0);          /* ValidateHwnd fails */
        EXPECT_EQ(ta(w, (HACCEL)BAD_HWNDS[i], &m), 0);         /* HMValidateHandle(.., 8) fails */
    }
    HMENU menu = U(CreatePopupMenu)();
    EXPECT_EQ(ta(w, (HACCEL)menu, &m), 0);                     /* a valid handle of the wrong type */
    EXPECT_EQ(ta(w, (HACCEL)w, &m), 0);
    EXPECT_EQ(g_cmd_count, 0);
    U(DestroyMenu)(menu); U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_virtkey_match_sends_wm_command) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F24, 0x1234);
    MSG m = mk_msg(w, WM_KEYDOWN, VK_F24);
    g_cmd_count = 0; g_cmd_wp = 0; g_cmd_lp = 1; g_cmd_hwnd = NULL;
    EXPECT_EQ(ta(w, ac, &m), 1);
    EXPECT_EQ(g_cmd_count, 1);               /* WM_COMMAND is sent (synchronously) before the call returns */
    EXPECT_EQ(g_cmd_wp, 0x11234);            /* cmd | 0x10000: HIWORD 1 marks an accelerator */
    EXPECT_EQ(g_cmd_lp, 0);
    EXPECT_EQ(g_cmd_hwnd, w);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_syskeydown_also_matches_virtkey) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F23, 0x0042);
    MSG m = mk_msg(w, WM_SYSKEYDOWN, VK_F23);    /* SystoChar maps 0x104 to the virtual-key class */
    g_cmd_count = 0;
    EXPECT_EQ(ta(w, ac, &m), 1);
    EXPECT_EQ(g_cmd_count, 1);
    EXPECT_EQ(g_cmd_wp, 0x10042);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_char_entry_matches_wm_char_only) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(0, 'x', 0x0777);        /* fVirt == 0: character accelerator */
    MSG ch = mk_msg(w, WM_CHAR, 'x');
    MSG kd = mk_msg(w, WM_KEYDOWN, 'X');
    g_cmd_count = 0;
    EXPECT_EQ(ta(w, ac, &kd), 0);
    EXPECT_EQ(g_cmd_count, 0);
    EXPECT_EQ(ta(w, ac, &ch), 1);
    EXPECT_EQ(g_cmd_count, 1);
    EXPECT_EQ(g_cmd_wp, 0x10777);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_virtkey_entry_ignores_wm_char) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, 'Y', 0x0555);
    MSG ch = mk_msg(w, WM_CHAR, 'Y');
    g_cmd_count = 0;
    EXPECT_EQ(ta(w, ac, &ch), 0);
    EXPECT_EQ(g_cmd_count, 0);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_non_key_messages_return_zero) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F24, 0x1234);
    static const UINT msgs[] = { WM_NULL, WM_KEYUP, WM_SYSKEYUP, WM_MOUSEMOVE, WM_PAINT, WM_COMMAND, WM_TIMER, WM_DEADCHAR, WM_USER };
    g_cmd_count = 0;
    for (unsigned i = 0; i < sizeof msgs / sizeof *msgs; i++) {
        MSG m = mk_msg(w, msgs[i], VK_F24);
        CHECKF(ta(w, ac, &m) == 0, "message 0x%x translated", msgs[i]);   /* SystoChar != 0x100/0x102/0x104/0x106 -> return 0 */
    }
    EXPECT_EQ(g_cmd_count, 0);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_wrong_key_returns_zero) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F24, 0x1234);
    g_cmd_count = 0;
    for (WPARAM k = VK_F13; k <= VK_F22; k++) {
        MSG m = mk_msg(w, WM_KEYDOWN, k);
        CHECK(ta(w, ac, &m) == 0);
    }
    EXPECT_EQ(g_cmd_count, 0);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_modifier_entry_does_not_match_plain_key) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
 /* Entries requiring Ctrl / Shift / Alt compare against GetKeyState of this thread's queue ( & 0x1c). */
    static const BYTE mods[] = { FCONTROL, FSHIFT, FALT, FCONTROL | FSHIFT };
    g_cmd_count = 0;
    for (unsigned i = 0; i < sizeof mods; i++) {
        HACCEL ac = mk_accel(FVIRTKEY | mods[i], VK_F24, 0x0100 + i);
        MSG m = mk_msg(w, WM_KEYDOWN, VK_F24);
        CHECKF(ta(w, ac, &m) == 0, "modifier 0x%x matched without the key held", mods[i]);
        U(DestroyAcceleratorTable)(ac);
    }
    EXPECT_EQ(g_cmd_count, 0);
    U(DestroyWindow)(w);
}

TEST(gap3_ta_first_matching_entry_wins) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    ACCEL a[3] = { {FVIRTKEY, VK_F19, 0x10}, {FVIRTKEY, VK_F20, 0x20}, {FVIRTKEY, VK_F20, 0x30} };
    HACCEL ac = U(CreateAcceleratorTableW)(a, 3);
    MSG m = mk_msg(w, WM_KEYDOWN, VK_F20);
    g_cmd_count = 0;
    EXPECT_EQ(ta(w, ac, &m), 1);
    EXPECT_EQ(g_cmd_count, 1);
    EXPECT_EQ(g_cmd_wp, 0x10020);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

TEST(gap3_ta_destroyed_accel_table_rejected) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F24, 0x1234);
    U(DestroyAcceleratorTable)(ac);
    MSG m = mk_msg(w, WM_KEYDOWN, VK_F24);
    g_cmd_count = 0;
    EXPECT_EQ(ta(w, ac, &m), 0);
    EXPECT_EQ(g_cmd_count, 0);
    U(DestroyWindow)(w);
}

TEST(gap3_ta_user32_wrapper_agrees) {
    BOOL (WINAPI *ta)(HWND, HACCEL, MSG *) = (void *)UNX(NtUserTranslateAccelerator);
    REQUIRE(ta);
    TOP_OR_SKIP(w);
    HACCEL ac = mk_accel(FVIRTKEY, VK_F24, 0x4321);
    MSG m = mk_msg(w, WM_KEYDOWN, VK_F24);
    g_cmd_count = 0;
    int viaapi = U(TranslateAcceleratorW)(w, ac, &m);
    EXPECT_EQ(viaapi != 0, 1);
    EXPECT_EQ(g_cmd_count, 1);
    EXPECT_EQ(g_cmd_wp, 0x14321);
    EXPECT_EQ(ta(w, ac, &m), 1);
    EXPECT_EQ(g_cmd_count, 2);
    U(DestroyAcceleratorTable)(ac); U(DestroyWindow)(w);
}

/* ---- NtUserSBGetParms --------------------------------------------------- */

static void fill_si(SCROLLINFO *si, DWORD mask)
{
    si->cbSize = sizeof *si; si->fMask = mask;
    si->nMin = 111; si->nMax = 222; si->nPage = 333; si->nPos = 444; si->nTrackPos = 555;
}

TEST(gap3_sbgetparms_invalid_hwnd_returns_zero_untouched) {
    BOOL (WINAPI *sb)(HWND, int, int *, SCROLLINFO *) = (void *)UNX(NtUserSBGetParms);
    REQUIRE(sb);
    for (unsigned i = 0; i < NBAD; i++) {
        int sbd[4] = { 1, 2, 3, 4 };
        SCROLLINFO si; fill_si(&si, SIF_ALL);
        SCROLLINFO ref = si;
        PLANT();
        EXPECT_EQ(sb((HWND)BAD_HWNDS[i], SB_VERT, sbd, &si), 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        CHECK(!memcmp(&si, &ref, sizeof si));
    }
}

TEST(gap3_sbgetparms_mask_table) {
    BOOL (WINAPI *sb)(HWND, int, int *, SCROLLINFO *) = (void *)UNX(NtUserSBGetParms);
    REQUIRE(sb);
    TOP_OR_SKIP(w);
    for (DWORD mask = 0; mask < 0x20; mask++) {
        int sbd[4] = { -7, 700, 25, 60 };          /* SBDATA: min, max, page, pos */
        SCROLLINFO si; fill_si(&si, mask);
        BOOL r = sb(w, SB_VERT, sbd, &si);
        EXPECT_EQ(r != 0, (mask & 0x17) != 0);   /* returns (fMask & (RANGE|PAGE|POS|TRACKPOS)) != 0 */
        EXPECT_EQ(si.cbSize, sizeof si);
        EXPECT_EQ(si.fMask, mask);
        EXPECT_EQ(si.nMin, (mask & SIF_RANGE) ? -7 : 111);
        EXPECT_EQ(si.nMax, (mask & SIF_RANGE) ? 700 : 222);
        EXPECT_EQ(si.nPage, (mask & SIF_PAGE) ? 25 : 333);
        EXPECT_EQ(si.nPos, (mask & SIF_POS) ? 60 : 444);
        /* TRACKPOS without an active scrollbar drag takes SBDATA's last dword (the position). */
        EXPECT_EQ(si.nTrackPos, (mask & SIF_TRACKPOS) ? 60 : 555);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_sbgetparms_sbdata_values_pass_through) {
    BOOL (WINAPI *sb)(HWND, int, int *, SCROLLINFO *) = (void *)UNX(NtUserSBGetParms);
    REQUIRE(sb);
    TOP_OR_SKIP(w);
    static const int vals[][4] = { {0,0,0,0}, {0,100,10,5}, {-100,100,0,-100}, {INT_MIN,INT_MAX,-1,INT_MAX}, {5,5,1,5} };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        int sbd[4]; memcpy(sbd, vals[i], sizeof sbd);
        SCROLLINFO si; fill_si(&si, SIF_RANGE | SIF_PAGE | SIF_POS);
        EXPECT_EQ(sb(w, SB_HORZ, sbd, &si), 1);
        EXPECT_EQ(si.nMin, vals[i][0]); EXPECT_EQ(si.nMax, vals[i][1]);
        EXPECT_EQ((int)si.nPage, vals[i][2]); EXPECT_EQ(si.nPos, vals[i][3]);
        CHECK(!memcmp(sbd, vals[i], sizeof sbd));          /* the input block is read-only */
    }
    U(DestroyWindow)(w);
}

TEST(gap3_sbgetparms_bar_selector_not_validated_without_tracking) {
    BOOL (WINAPI *sb)(HWND, int, int *, SCROLLINFO *) = (void *)UNX(NtUserSBGetParms);
    REQUIRE(sb);
    TOP_OR_SKIP(w);
    for (int bar = -1; bar <= 4; bar++) {
        int sbd[4] = { 1, 2, 3, 4 };
        SCROLLINFO si; fill_si(&si, SIF_PAGE);
        EXPECT_EQ(sb(w, bar, sbd, &si), 1);      /* nBar is compared only to an active track state */
        EXPECT_EQ(si.nPage, 3);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_sbgetparms_matches_getscrollinfo_for_set_values) {
    BOOL (WINAPI *sb)(HWND, int, int *, SCROLLINFO *) = (void *)UNX(NtUserSBGetParms);
    REQUIRE(sb);
    TOP_OR_SKIP(w);
    SCROLLINFO set; memset(&set, 0, sizeof set);
    set.cbSize = sizeof set; set.fMask = SIF_RANGE | SIF_PAGE | SIF_POS; set.nMin = 3; set.nMax = 90; set.nPage = 12; set.nPos = 40;
    U(SetScrollInfo)(w, SB_VERT, &set, FALSE);
    SCROLLINFO got; memset(&got, 0, sizeof got); got.cbSize = sizeof got; got.fMask = SIF_ALL;
    CHECK(U(GetScrollInfo)(w, SB_VERT, &got));
    EXPECT_EQ(got.nMin, 3); EXPECT_EQ(got.nMax, 90); EXPECT_EQ(got.nPage, 12); EXPECT_EQ(got.nPos, 40);
    U(DestroyWindow)(w);
}

/* ---- NtUserFlashWindowEx ------------------------------------------------ */

TEST(gap3_flash_bad_cbsize_invalid_parameter) {
    BOOL (WINAPI *fl)(FLASHWINFO *) = (void *)UNX(NtUserFlashWindowEx);
    REQUIRE(fl);
    TOP_OR_SKIP(w);
    CHECK(sizeof(FLASHWINFO) == 0x20);
    static const DWORD sizes[] = { 0, 4, 0x18, 0x1c, 0x1f, 0x21, 0x24, 0x28, 0x40, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        FLASHWINFO fi; memset(&fi, 0, sizeof fi);
        fi.cbSize = sizes[i]; fi.hwnd = w; fi.dwFlags = FLASHW_STOP;
        PLANT();
        EXPECT_EQ(fl(&fi), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_flash_flag_bits_above_low_nibble_rejected) {
    BOOL (WINAPI *fl)(FLASHWINFO *) = (void *)UNX(NtUserFlashWindowEx);
    REQUIRE(fl);
    TOP_OR_SKIP(w);
    static const DWORD bad[] = { 0x10, 0x20, 0x40, 0x80, 0x100, 0x10000, 0x80000000u, 0xfffffff0u, 0x13 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        FLASHWINFO fi; memset(&fi, 0, sizeof fi);
        fi.cbSize = sizeof fi; fi.hwnd = w; fi.dwFlags = bad[i]; fi.uCount = 1;
        PLANT();
        EXPECT_EQ(fl(&fi), 0);               /* (dwFlags & 0xfffffff0) != 0 */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_flash_invalid_hwnd_is_invalid_parameter) {
    BOOL (WINAPI *fl)(FLASHWINFO *) = (void *)UNX(NtUserFlashWindowEx);
    REQUIRE(fl);
    for (unsigned i = 0; i < NBAD; i++) {
        FLASHWINFO fi; memset(&fi, 0, sizeof fi);
        fi.cbSize = sizeof fi; fi.hwnd = (HWND)BAD_HWNDS[i]; fi.dwFlags = FLASHW_STOP;
        PLANT();
        EXPECT_EQ(fl(&fi), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* ValidateHWNDND failure falls into the same 0x57 branch */
    }
}

TEST(gap3_flash_stop_on_hidden_window_is_accepted) {
    BOOL (WINAPI *fl)(FLASHWINFO *) = (void *)UNX(NtUserFlashWindowEx);
    REQUIRE(fl);
    TOP_OR_SKIP(w);
    FLASHWINFO fi; memset(&fi, 0, sizeof fi);
    fi.cbSize = sizeof fi; fi.hwnd = w; fi.dwFlags = FLASHW_STOP;
    PLANT();
    BOOL r = fl(&fi);
    t_note("last error after FlashWindowEx(STOP): %lu", GetLastError());
    t_note("FlashWindowEx(STOP) on hidden window returned %d", r);
    /* The user32 wrapper reaches the same syscall with the same arguments. */
    BOOL r2 = U(FlashWindowEx)(&fi);
    t_note("user32 FlashWindowEx(STOP) returned %d", r2);
    U(DestroyWindow)(w);
}

TEST(gap3_flash_user32_wrapper_rejects_same_inputs) {
    FLASHWINFO fi; memset(&fi, 0, sizeof fi);
    fi.cbSize = sizeof fi; fi.hwnd = (HWND)0xdeadbeef; fi.dwFlags = FLASHW_STOP;
    PLANT();
    EXPECT_EQ(U(FlashWindowEx)(&fi), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---- NtUserGetAltTabInfo ------------------------------------------------ */

TEST(gap3_alttab_foreign_hwnd_is_invalid_window_handle_0x578) {
    BOOL (WINAPI *at)(HWND, int, ALTTABINFO *, void *, UINT, BOOL) = (void *)UNX(NtUserGetAltTabInfo);
    REQUIRE(at);
    TOP_OR_SKIP(w);
    ALTTABINFO ati; memset(&ati, 0xCC, sizeof ati); ati.cbSize = sizeof ati;
    ALTTABINFO ref = ati;
    WCHAR txt[8];
    PLANT();
    /* A valid HWND that is not gspwndAltTab: UserSetLastError(0x578) before any size check. */
    EXPECT_EQ(at(w, 0, &ati, txt, 8, TRUE), 0);
    EXPECT_ERR(0x578);
    CHECK(!memcmp(&ati, &ref, sizeof ati));
    U(DestroyWindow)(w);
}

TEST(gap3_alttab_invalid_hwnd_fails) {
    BOOL (WINAPI *at)(HWND, int, ALTTABINFO *, void *, UINT, BOOL) = (void *)UNX(NtUserGetAltTabInfo);
    REQUIRE(at);
    for (unsigned i = 1; i < NBAD; i++) {
        ALTTABINFO ati; memset(&ati, 0, sizeof ati); ati.cbSize = sizeof ati;
        WCHAR txt[8];
        EXPECT_EQ(at((HWND)BAD_HWNDS[i], 0, &ati, txt, 8, TRUE), 0);
    }
}

TEST(gap3_alttab_bad_cbsize_invalid_parameter) {
    BOOL (WINAPI *at)(HWND, int, ALTTABINFO *, void *, UINT, BOOL) = (void *)UNX(NtUserGetAltTabInfo);
    REQUIRE(at);
    static const DWORD sizes[] = { 0, 4, 0x24, 0x27, 0x29, 0x2c, 0x50, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        ALTTABINFO ati; memset(&ati, 0xCC, sizeof ati); ati.cbSize = sizes[i];
        ALTTABINFO ref = ati;
        WCHAR txt[8];
        PLANT();
        EXPECT_EQ(at(NULL, 0, &ati, txt, 8, TRUE), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECK(!memcmp(&ati, &ref, sizeof ati));    /* nothing is written back on failure */
    }
}

TEST(gap3_alttab_no_alttab_window_active_leaves_struct_alone) {
    BOOL (WINAPI *at)(HWND, int, ALTTABINFO *, void *, UINT, BOOL) = (void *)UNX(NtUserGetAltTabInfo);
    REQUIRE(at);
    ALTTABINFO ati; memset(&ati, 0xCC, sizeof ati); ati.cbSize = sizeof ati;
    ALTTABINFO ref = ati;
    WCHAR txt[8];
    BOOL r = at(NULL, 0, &ati, txt, 8, TRUE);
    t_note("GetAltTabInfo with no Alt-Tab UI returned %d", r);
    if (!r) CHECK(!memcmp(&ati, &ref, sizeof ati));   /* the struct is copied out only when _GetAltTabInfo succeeds */
    else CHECK(ati.cbSize == 0x28);
}

TEST(gap3_alttab_user32_wrapper_agrees_on_validation) {
    TOP_OR_SKIP(w);
    ALTTABINFO ati; memset(&ati, 0, sizeof ati); ati.cbSize = sizeof ati;
    WCHAR txt[8];
    PLANT();
    EXPECT_EQ(U(GetAltTabInfoW)(w, 0, &ati, txt, 8), 0);
    EXPECT_ERR(0x578);
    U(DestroyWindow)(w);
}

/* ---- NtUserInheritWindowMonitor ----------------------------------------- */

TEST(gap3_inheritmonitor_invalid_hwnd_returns_zero) {
    BOOL (WINAPI *im)(HWND, HWND) = (void *)UNX(NtUserInheritWindowMonitor);
    REQUIRE(im);
    for (unsigned i = 0; i < NBAD; i++) EXPECT_EQ(im((HWND)BAD_HWNDS[i], NULL), 0);
}

TEST(gap3_inheritmonitor_desktop_window_silently_refused) {
    BOOL (WINAPI *im)(HWND, HWND) = (void *)UNX(NtUserInheritWindowMonitor);
    REQUIRE(im);
    TOP_OR_SKIP(w);
    HWND dt = U(GetDesktopWindow)();
    PLANT();
    /* FNID_DESKTOP (0x29d) windows hit the early "return 0" before any ownership test, so no error is set. */
    EXPECT_EQ(im(dt, w), 0);
    EXPECT_EQ(GetLastError(), SENTINEL_ERR);
    U(DestroyWindow)(w);
}

TEST(gap3_inheritmonitor_foreign_process_window_access_denied) {
    BOOL (WINAPI *im)(HWND, HWND) = (void *)UNX(NtUserInheritWindowMonitor);
    REQUIRE(im);
    HWND tray = U(FindWindowA)("Shell_TrayWnd", NULL);
    if (!tray) { t_skip("no Shell_TrayWnd on this desktop"); return; }
    DWORD pid = 0; U(GetWindowThreadProcessId)(tray, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("taskbar owned by this process"); return; }
    PLANT();
    EXPECT_EQ(im(tray, NULL), 0);            /* W32PROCESS of the window's thread differs -> UserSetLastError(5) */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}

TEST(gap3_inheritmonitor_invalid_inherit_source_fails) {
    BOOL (WINAPI *im)(HWND, HWND) = (void *)UNX(NtUserInheritWindowMonitor);
    REQUIRE(im);
    TOP_OR_SKIP(w);
    for (unsigned i = 1; i < NBAD; i++) EXPECT_EQ(im(w, (HWND)BAD_HWNDS[i]), 0);   /* non-NULL source must validate */
    U(DestroyWindow)(w);
}

TEST(gap3_inheritmonitor_own_windows_ok_or_noted) {
    BOOL (WINAPI *im)(HWND, HWND) = (void *)UNX(NtUserInheritWindowMonitor);
    REQUIRE(im);
    TOP_OR_SKIP(a);
    HWND b = mk_top();
    HMONITOR before = U(MonitorFromWindow)(a, MONITOR_DEFAULTTONULL);
    BOOL r1 = im(a, NULL);
    BOOL r2 = im(a, b);
    HMONITOR after = U(MonitorFromWindow)(a, MONITOR_DEFAULTTONULL);
    t_note("InheritWindowMonitor(a,NULL)=%d (a,b)=%d monitor before %p after %p", r1, r2, (void *)before, (void *)after);
    U(DestroyWindow)(a); if (b) U(DestroyWindow)(b);
}

/* ---- Get/SetWindowCompositionAttribute --------------------------------- */

/* Minimum cbData per attribute 1..26, read from the table at win32kfull (16-byte entries, size first). */
static const SIZE_T WCA_MIN[27] = { 0, 4,4,4,4, 0x10, 4,4, 0x10, 4,4,4, 0x14, 4,4,4,4,4,4, 0x10, 4,4, 8, 4,4,4,4 };

TEST(gap3_wca_get_unknown_attribute_invalid_parameter) {
    BOOL (WINAPI *g)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserGetWindowCompositionAttribute);
    REQUIRE(g);
    static const DWORD attrs[] = { 0, 27, 28, 0x40, 0xff, 0x100, 0x7fffffff, 0xffffffffu };
    for (unsigned i = 0; i < sizeof attrs / sizeof *attrs; i++) {
        DWORD buf[16]; memset(buf, 0xCC, sizeof buf);
        GAP3_WCAD d = { attrs[i], buf, sizeof buf };
        PLANT();
        EXPECT_EQ(g(NULL, &d), 0);           /* attrib - 1 >= 0x1a: STATUS_INVALID_PARAMETER -> ERROR_INVALID_PARAMETER */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(buf[0], 0xCCCCCCCC);
    }
}

TEST(gap3_wca_get_buffer_too_small_for_every_attribute) {
    BOOL (WINAPI *g)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserGetWindowCompositionAttribute);
    REQUIRE(g);
    TOP_OR_SKIP(w);
    for (DWORD a = 1; a <= 26; a++) {
        DWORD buf[16]; memset(buf, 0xCC, sizeof buf);
        for (int pass = 0; pass < 2; pass++) {
            GAP3_WCAD d = { a, buf, pass ? WCA_MIN[a] - 1 : 0 };
            PLANT();
            CHECKF(g(w, &d) == 0, "attr %lu pass %d", (unsigned long)a, pass);
            EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);   /* STATUS_BUFFER_TOO_SMALL (0xC0000023) */
        }
        EXPECT_EQ(buf[0], 0xCCCCCCCC);
    }
    U(DestroyWindow)(w);
}

TEST(gap3_wca_get_invalid_hwnd_is_invalid_handle) {
    BOOL (WINAPI *g)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserGetWindowCompositionAttribute);
    REQUIRE(g);
    for (DWORD a = 1; a <= 26; a++) {
        DWORD buf[16]; memset(buf, 0xCC, sizeof buf);
        for (unsigned i = 0; i < 3; i++) {
            GAP3_WCAD d = { a, buf, WCA_MIN[a] };
            PLANT();
            CHECKF(g((HWND)BAD_HWNDS[i], &d) == 0, "attr %lu", (unsigned long)a);
            EXPECT_ERR(ERROR_INVALID_HANDLE);        /* STATUS_INVALID_HANDLE (0xC0000008) from the failed ValidateHwnd */
        }
        EXPECT_EQ(buf[0], 0xCCCCCCCC);
    }
}

TEST(gap3_wca_get_flag_attributes_default_zero) {
    BOOL (WINAPI *g)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserGetWindowCompositionAttribute);
    REQUIRE(g);
    TOP_OR_SKIP(w);
    /* 0x18 EXCLUDED_FROM_DDA, 0x19 PASSIVEUPDATEMODE, 0x1a DARKMODE_COLORS are read from the DWM window property,
     * which a fresh window does not have: value 0, call succeeds. */
    for (DWORD a = 0x18; a <= 0x1a; a++) {
        DWORD buf[4]; memset(buf, 0xCC, sizeof buf);
        GAP3_WCAD d = { a, buf, 4 };
        EXPECT_EQ(g(w, &d), 1);
        EXPECT_EQ(buf[0], 0);
        EXPECT_EQ(buf[1], 0xCCCCCCCC);       /* exactly the table's minimum size is copied out */
    }
    U(DestroyWindow)(w);
}

TEST(gap3_wca_get_copies_only_table_size) {
    BOOL (WINAPI *g)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserGetWindowCompositionAttribute);
    REQUIRE(g);
    TOP_OR_SKIP(w);
    DWORD buf[16]; memset(buf, 0xCC, sizeof buf);
    GAP3_WCAD d = { 0x19, buf, sizeof buf };
    EXPECT_EQ(g(w, &d), 1);
    EXPECT_EQ(buf[0], 0);
    for (int i = 1; i < 16; i++) CHECKF(buf[i] == 0xCCCCCCCC, "buf[%d] written", i);
    U(DestroyWindow)(w);
}

TEST(gap3_wca_set_unknown_attribute_invalid_parameter) {
    BOOL (WINAPI *s)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserSetWindowCompositionAttribute);
    REQUIRE(s);
    static const DWORD attrs[] = { 0, 27, 28, 0x40, 0xff, 0x100, 0x7fffffff, 0xffffffffu };
    for (unsigned i = 0; i < sizeof attrs / sizeof *attrs; i++) {
        DWORD buf[16] = { 0 };
        GAP3_WCAD d = { attrs[i], buf, sizeof buf };
        PLANT();
        EXPECT_EQ(s(NULL, &d), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(gap3_wca_set_buffer_too_small_for_every_attribute) {
    BOOL (WINAPI *s)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserSetWindowCompositionAttribute);
    REQUIRE(s);
    TOP_OR_SKIP(w);
    for (DWORD a = 1; a <= 26; a++) {
        DWORD buf[16] = { 0 };
        for (int pass = 0; pass < 2; pass++) {
            GAP3_WCAD d = { a, buf, pass ? WCA_MIN[a] - 1 : 0 };
            PLANT();
            CHECKF(s(w, &d) == 0, "attr %lu pass %d", (unsigned long)a, pass);
            EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        }
    }
    U(DestroyWindow)(w);
}

TEST(gap3_wca_set_invalid_hwnd_is_invalid_handle) {
    BOOL (WINAPI *s)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserSetWindowCompositionAttribute);
    REQUIRE(s);
    for (DWORD a = 1; a <= 26; a++)
        for (unsigned i = 0; i < 3; i++) {
            DWORD buf[16] = { 0 };
            GAP3_WCAD d = { a, buf, WCA_MIN[a] };
            PLANT();
            CHECKF(s((HWND)BAD_HWNDS[i], &d) == 0, "attr %lu", (unsigned long)a);
            EXPECT_ERR(ERROR_INVALID_HANDLE);
        }
}

TEST(gap3_wca_set_child_window_is_invalid_handle_for_every_attribute) {
    BOOL (WINAPI *s)(HWND, GAP3_WCAD *) = (void *)UNX(NtUserSetWindowCompositionAttribute);
    REQUIRE(s);
    TOP_OR_SKIP(p);
    HWND c = mk_child(p);
    if (!c) { U(DestroyWindow)(p); t_skip("cannot create child"); return; }
    /* A plain (non-layered) WS_CHILD is not a top-level window: every attribute, including 0x11 and 0x14, takes the
     * "not top level" exit (status 0xC0000008) before any attribute handler runs, so nothing is modified. */
    for (DWORD a = 1; a <= 26; a++) {
        DWORD buf[16] = { 0 };
        GAP3_WCAD d = { a, buf, WCA_MIN[a] };
        PLANT();
        CHECKF(s(c, &d) == 0, "attr %lu", (unsigned long)a);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
    U(DestroyWindow)(p);
}

TEST(gap3_wca_user32_wrappers_reach_same_checks) {
    BOOL (WINAPI *g)(HWND, GAP3_WCAD *) = (void *)t_proc_in("user32.dll", "GetWindowCompositionAttribute");
    BOOL (WINAPI *s)(HWND, GAP3_WCAD *) = (void *)t_proc_in("user32.dll", "SetWindowCompositionAttribute");
    REQUIRE(g); REQUIRE(s);
    DWORD buf[16] = { 0 };
    GAP3_WCAD d = { 0, buf, sizeof buf };
    PLANT(); EXPECT_EQ(g(NULL, &d), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PLANT(); EXPECT_EQ(s(NULL, &d), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---- NtUserSetWindowCompositionTransition ------------------------------- */

TEST(gap3_wct_invalid_hwnd_returns_zero) {
    BOOL (WINAPI *t)(HWND, DWORD, void *, void *, void *, void *, void *) = (void *)UNX(NtUserSetWindowCompositionTransition);
    REQUIRE(t);
    for (unsigned i = 0; i < NBAD; i++) EXPECT_EQ(t((HWND)BAD_HWNDS[i], 0, NULL, NULL, NULL, NULL, NULL), 0);
}

TEST(gap3_wct_pointer_consistency_invalid_parameter) {
    BOOL (WINAPI *t)(HWND, DWORD, void *, void *, void *, void *, void *) = (void *)UNX(NtUserSetWindowCompositionTransition);
    REQUIRE(t);
    TOP_OR_SKIP(w);
    UINT64 blob[2] = { 0, 0 };
    /* p4 requires p3, and p6 requires p5; violating either is STATUS_INVALID_PARAMETER -> ERROR_INVALID_PARAMETER. */
    PLANT(); EXPECT_EQ(t(w, 0, NULL, blob, NULL, NULL, NULL), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PLANT(); EXPECT_EQ(t(w, 0, NULL, NULL, NULL, blob, NULL), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PLANT(); EXPECT_EQ(t(w, 7, NULL, blob, NULL, blob, blob), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(DestroyWindow)(w);
}

TEST(gap3_wct_child_window_is_invalid_handle) {
    BOOL (WINAPI *t)(HWND, DWORD, void *, void *, void *, void *, void *) = (void *)UNX(NtUserSetWindowCompositionTransition);
    REQUIRE(t);
    TOP_OR_SKIP(p);
    HWND c = mk_child(p);
    if (!c) { U(DestroyWindow)(p); t_skip("cannot create child"); return; }
    PLANT();
    EXPECT_EQ(t(c, 0, NULL, NULL, NULL, NULL, NULL), 0);   /* _IsTopLevelWindow false -> STATUS_INVALID_HANDLE */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    UINT64 blob[2] = { 0, 0 };
    PLANT();
    EXPECT_EQ(t(c, 0, blob, blob, blob, blob, blob), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    U(DestroyWindow)(p);
}
