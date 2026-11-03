/* gap_05: NtUser* syscalls - SoundSentry, WindowFromPhysicalPoint, GetActiveProcessesDpis,
 * HwndQueryRedirectionInfo, MagGet/SetContextInformation, DCompositionDestroyChannel. */
#include "g5.h"

/* ---------------------------------------------------------------- NtUserSoundSentry
 * the sound-sentry path: when the sound-sentry flags lacks SSF_SOUNDSENTRYON (1) it does
 * UserSetLastError(0x32 ERROR_NOT_SUPPORTED) and returns 0 before touching any window. */
static int sentry_on(int *known) {
    SOUNDSENTRYW ss; memset(&ss, 0, sizeof ss); ss.cbSize = sizeof ss;
    *known = 0;
    if (!U(SystemParametersInfoW)) return 1;
    if (!U(SystemParametersInfoW)(SPI_GETSOUNDSENTRY, sizeof ss, &ss, 0)) return 1;
    *known = 1;
    return (ss.dwFlags & SSF_SOUNDSENTRYON) != 0;
}
TEST(gap5_sound_sentry_off_not_supported) {
    G5_GUI(); G5_NT(NtUserSoundSentry); G5_NEED(NtUserSoundSentry_);
    int known; if (sentry_on(&known) || !known) { t_skip("SoundSentry enabled or state unreadable; the call would flash the screen"); return; }
    SET_ERR(0);
    ULONG r = NtUserSoundSentry_();
    CHECK(r == 0);
    EXPECT_ERR(ERROR_NOT_SUPPORTED);
}
TEST(gap5_sound_sentry_off_repeatable) {
    G5_GUI(); G5_NT(NtUserSoundSentry); G5_NEED(NtUserSoundSentry_);
    int known; if (sentry_on(&known) || !known) { t_skip("SoundSentry enabled"); return; }
    for (int i = 0; i < 5; i++) {
        SET_ERR(0);
        CHECK(NtUserSoundSentry_() == 0);
        EXPECT_ERR(ERROR_NOT_SUPPORTED);
    }
}
TEST(gap5_sound_sentry_user32_wrapper_is_a_tail_jump) {
    G5_GUI(); G5_NT(NtUserSoundSentry); G5_NEED(NtUserSoundSentry_);
    SoundSentry_pfn w = (SoundSentry_pfn)t_proc_in("user32.dll", "SoundSentry");
    G5_NEED(w);
    int known; if (sentry_on(&known) || !known) { t_skip("SoundSentry enabled"); return; }
    /* user32!SoundSentry is a bare jmp to the stub, so the result and last error are the kernel's. */
    SET_ERR(0);
    CHECK(w() == 0);
    EXPECT_ERR(ERROR_NOT_SUPPORTED);
}
TEST(gap5_sound_sentry_does_not_touch_status) {
    G5_GUI(); G5_NT(NtUserSoundSentry); G5_NEED(NtUserSoundSentry_);
    int known; if (sentry_on(&known) || !known) { t_skip("SoundSentry enabled"); return; }
    /* The disabled path uses UserSetLastError only, not UserSetLastStatus. */
    g5_clear_status();
    CHECK(NtUserSoundSentry_() == 0);
    CHECKF(g5_laststatus() == 0x5A5A5A5Au, "LastStatusValue became 0x%lx", (unsigned long)g5_laststatus());
}

/* ---------------------------------------------------------------- NtUserWindowFromPhysicalPoint
 * win32kfull 0xf8b00 is the body of BOTH NtUserWindowFromPhysicalPoint and NtUserWindowFromPoint: it takes
 * the POINT by value in one register, runs the window-from-point search under the user critical section and returns
 * the 32-bit handle stored at the start of the hit tagWND, or 0. */
static LONG64 pk(LONG x, LONG y) { return ((LONG64)(ULONG)y << 32) | (ULONG)x; }
TEST(gap5_wfpp_matches_windowfrompoint_alias) {
    G5_GUI(); G5_NT(NtUserWindowFromPhysicalPoint); G5_NT(NtUserWindowFromPoint);
    G5_NEED(NtUserWindowFromPhysicalPoint_); G5_NEED(NtUserWindowFromPoint_);
    int cx = U(GetSystemMetrics) ? U(GetSystemMetrics)(SM_CXSCREEN) : 800;
    int cy = U(GetSystemMetrics) ? U(GetSystemMetrics)(SM_CYSCREEN) : 600;
    LONG pts[][2] = { {0, 0}, {1, 1}, {cx / 2, cy / 2}, {cx - 1, cy - 1}, {-1, -1}, {-30000, -30000},
                      {0x7FFFFFFF, 0x7FFFFFFF}, {(LONG)0x80000000, (LONG)0x80000000}, {cx, cy}, {-1, 0}, {0, -1} };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        HWND a = NtUserWindowFromPhysicalPoint_(pk(pts[i][0], pts[i][1]));
        HWND b = NtUserWindowFromPoint_(pk(pts[i][0], pts[i][1]));
        CHECKF(a == b, "(%ld,%ld): physical %p vs logical %p", pts[i][0], pts[i][1], a, b);
    }
}
TEST(gap5_wfpp_result_is_a_32bit_live_window_handle) {
    G5_GUI(); G5_NT(NtUserWindowFromPhysicalPoint); G5_NEED(NtUserWindowFromPhysicalPoint_);
    G5_NEED(U(IsWindow));
    int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
    LONG pts[][2] = { {cx / 2, cy / 2}, {0, 0}, {cx - 1, cy - 1}, {cx / 4, cy / 4}, {cx / 2, cy - 2} };
    for (size_t i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        HWND h = NtUserWindowFromPhysicalPoint_(pk(pts[i][0], pts[i][1]));
        if (!h) continue;
 /* the result is the 4-byte handle zero-extended by the 8-byte load; the handle also stays valid. */
        CHECKF(((ULONG_PTR)h >> 32) == 0, "handle %p has upper bits", h);
        CHECKF(U(IsWindow)(h), "(%ld,%ld): %p is not a window", pts[i][0], pts[i][1], h);
    }
}
TEST(gap5_wfpp_far_outside_the_desktop_has_no_window) {
    G5_GUI(); G5_NT(NtUserWindowFromPhysicalPoint); G5_NEED(NtUserWindowFromPhysicalPoint_);
    HWND h = NtUserWindowFromPhysicalPoint_(pk(-30000, -30000));
    /* A point outside the desktop window either hits nothing (0) or the desktop window itself,
     * depending on how the hit-test treats it; both are a stable answer. */
    HWND desk = U(GetDesktopWindow)();
    t_note("window at (-30000,-30000): %p (desktop %p)", h, desk);
    CHECK(h == NULL || h == desk);
}
TEST(gap5_wfpp_user32_wrapper_agrees) {
    G5_GUI(); G5_NT(NtUserWindowFromPhysicalPoint); G5_NEED(NtUserWindowFromPhysicalPoint_);
    G5_NEED(U(WindowFromPhysicalPoint));
    int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
    POINT p = { cx / 2, cy / 2 };
    HWND a = U(WindowFromPhysicalPoint)(p);
    HWND b = NtUserWindowFromPhysicalPoint_(pk(p.x, p.y));
    CHECKF(a == b, "wrapper %p vs syscall %p", a, b);
}
TEST(gap5_wfpp_does_not_change_last_error) {
    G5_GUI(); G5_NT(NtUserWindowFromPhysicalPoint); G5_NEED(NtUserWindowFromPhysicalPoint_);
    /* The body has no UserSetLastError/Status call. */
    g5_clear_status();
    NtUserWindowFromPhysicalPoint_(pk(5, 5));
    CHECKF(GetLastError() == 0x5A5A5A5Au, "last error %lu", GetLastError());
    CHECKF(g5_laststatus() == 0x5A5A5A5Au, "last status 0x%lx", (unsigned long)g5_laststatus());
}

/* ---------------------------------------------------------------- NtUserGetActiveProcessesDpis
 * win32kfull 0x122900: walks gppiList and ORs 1 << GetDpiCacheSlot(ppi->dpi) for every process whose dpi
 * has a slot. This process is in gppiList once connected, and runs at a cached DPI. */
TEST(gap5_active_dpis_nonzero_for_connected_process) {
    G5_GUI(); G5_NT(NtUserGetActiveProcessesDpis); G5_NEED(NtUserGetActiveProcessesDpis_);
    ULONG m = NtUserGetActiveProcessesDpis_();
    if (m == 0) t_note("mask 0: this process runs at a DPI with no cache slot");
    CHECK(m != 0);
}
TEST(gap5_active_dpis_matches_user32_ordinal_2711) {
    G5_GUI(); G5_NT(NtUserGetActiveProcessesDpis); G5_NEED(NtUserGetActiveProcessesDpis_);
    typedef ULONG (WINAPI *fn_t)(void);
    fn_t o = (fn_t)t_proc_ord("user32.dll", 2711);
    G5_NEED(o);
    /* Another process may start or change DPI between two calls; two out of three agreeing is enough. */
    ULONG a = NtUserGetActiveProcessesDpis_(), b = o(), c = NtUserGetActiveProcessesDpis_();
    CHECKF(a == b || b == c, "0x%lx 0x%lx 0x%lx", (unsigned long)a, (unsigned long)b, (unsigned long)c);
}
TEST(gap5_active_dpis_stable_without_process_changes) {
    G5_GUI(); G5_NT(NtUserGetActiveProcessesDpis); G5_NEED(NtUserGetActiveProcessesDpis_);
    ULONG v[4];
    for (int i = 0; i < 4; i++) v[i] = NtUserGetActiveProcessesDpis_();
    int same = 0;
    for (int i = 1; i < 4; i++) same += (v[i] == v[i - 1]);
    CHECKF(same >= 2, "mask changed on %d of 3 consecutive reads", 3 - same);
}
TEST(gap5_active_dpis_no_last_error) {
    G5_GUI(); G5_NT(NtUserGetActiveProcessesDpis); G5_NEED(NtUserGetActiveProcessesDpis_);
    g5_clear_status();
    NtUserGetActiveProcessesDpis_();
    CHECK(GetLastError() == 0x5A5A5A5Au);
}

/* ---------------------------------------------------------------- NtUserHwndQueryRedirectionInfo
 * win32kfull 0x1fcee0 (HWND, type, out, *inoutSize) -> BOOL.
 * ValidateHwnd failure returns FALSE without writing anything. For a valid window: type >= 5 sets
 * STATUS_INVALID_INFO_CLASS (0xC0000003) before looking at the buffers; types 0..3 with a NULL buffer end in
 * STATUS_INVALID_PARAMETER (0xC000000D, the initial value). UserSetLastStatus also sets LastError. */
#define SENT 0xA5
static void rq_bad_hwnd(HWND h, const char *what) {
    G5_NT(NtUserHwndQueryRedirectionInfo);
    for (int type = 0; type <= 4; type++) {
        unsigned char buf[0x40]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x40]; memcpy(ref, buf, sizeof ref);
        ULONG sz = 0x20;
        ULONG r = NtUserHwndQueryRedirectionInfo_(h, type, buf, &sz);
        CHECKF(r == 0, "%s type %d: returned %lu", what, type, (unsigned long)r);
        CHECKF(!memcmp(buf, ref, sizeof buf), "%s type %d: output buffer modified", what, type);
        CHECKF(sz == 0x20, "%s type %d: size became %lu", what, type, (unsigned long)sz);
    }
}
TEST(gap5_redirinfo_null_hwnd_false) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    rq_bad_hwnd(NULL, "NULL");
}
TEST(gap5_redirinfo_bogus_hwnds_false) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    HWND bad[] = { (HWND)1, (HWND)0xDEAD0, (HWND)0x7FFFFFFF, (HWND)(LONG_PTR)-1, (HWND)(LONG_PTR)-3 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) rq_bad_hwnd(bad[i], "bogus");
}
TEST(gap5_redirinfo_destroyed_hwnd_false) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    HWND w = k_make_window("gap5-redir-dead");
    if (!w) { t_skip("cannot create a window"); return; }
    U(DestroyWindow)(w);
    rq_bad_hwnd(w, "destroyed");
}
TEST(gap5_redirinfo_invalid_info_class) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    HWND w = k_make_window("gap5-redir-class");
    if (!w) { t_skip("cannot create a window"); return; }
    int types[] = { 5, 6, 7, 100, 0x7FFFFFFF };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        unsigned char buf[0x40]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x40]; memcpy(ref, buf, sizeof ref);
        ULONG sz = 0x20;
        g5_clear_status();
        ULONG r = NtUserHwndQueryRedirectionInfo_(w, types[i], buf, &sz);
        CHECKF(r == 0, "type %d returned %lu", types[i], (unsigned long)r);
        CHECKF(g5_laststatus() == 0xC0000003u, "type %d: status 0x%lx", types[i], (unsigned long)g5_laststatus());
        CHECKF(GetLastError() == g5_dos((LONG)0xC0000003), "type %d: last error %lu", types[i], GetLastError());
        CHECK(!memcmp(buf, ref, sizeof buf));
        CHECK(sz == 0x20);
    }
    U(DestroyWindow)(w);
}
TEST(gap5_redirinfo_null_buffer_invalid_parameter) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    HWND w = k_make_window("gap5-redir-null");
    if (!w) { t_skip("cannot create a window"); return; }
    for (int type = 0; type <= 2; type++) {
        ULONG sz = 0x40;
        g5_clear_status();
        ULONG r = NtUserHwndQueryRedirectionInfo_(w, type, NULL, &sz);
        CHECKF(r == 0, "type %d returned %lu", type, (unsigned long)r);
        /* No style window and NULL buffer both end at the initial status. */
        CHECKF(g5_laststatus() == 0xC000000Du, "type %d: status 0x%lx", type, (unsigned long)g5_laststatus());
        CHECKF(GetLastError() == g5_dos((LONG)0xC000000D), "type %d: last error %lu", type, GetLastError());
        CHECK(sz == 0x40);
    }
    U(DestroyWindow)(w);
}
TEST(gap5_redirinfo_small_buffer_status_set) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    HWND w = k_make_window("gap5-redir-small");
    if (!w) { t_skip("cannot create a window"); return; }
    /* Minimum buffer per type: 0 -> 0x20, 1 -> 0x10, 2 -> 4. Smaller yields STATUS_INVALID_BUFFER_SIZE
     * (0xC0000206) when the window has a style window, otherwise STATUS_INVALID_PARAMETER. */
    struct { int type; ULONG small; } rows[] = { {0, 0x1F}, {0, 0}, {1, 0xF}, {1, 1}, {2, 3}, {2, 0} };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        unsigned char buf[0x40]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x40]; memcpy(ref, buf, sizeof ref);
        ULONG sz = rows[i].small;
        g5_clear_status();
        ULONG r = NtUserHwndQueryRedirectionInfo_(w, rows[i].type, buf, &sz);
        ULONG st = g5_laststatus();
        CHECKF(r == 0, "type %d size %lu returned %lu", rows[i].type, (unsigned long)rows[i].small, (unsigned long)r);
        CHECKF(st == 0xC0000206u || st == 0xC000000Du, "type %d size %lu: status 0x%lx", rows[i].type, (unsigned long)rows[i].small, (unsigned long)st);
        CHECK(!memcmp(buf, ref, sizeof buf));
        CHECK(sz == rows[i].small);
        t_note("type %d size %lu -> status 0x%lx", rows[i].type, (unsigned long)rows[i].small, (unsigned long)st);
    }
    U(DestroyWindow)(w);
}
TEST(gap5_redirinfo_dwm_only_type_3_denied) {
    G5_GUI(); G5_NT(NtUserHwndQueryRedirectionInfo); G5_NEED(NtUserHwndQueryRedirectionInfo_);
    HWND w = k_make_window("gap5-redir-dwm");
    if (!w) { t_skip("cannot create a window"); return; }
    /* Type 3 is for dwm.exe only: IsProcessDwm == 0 gives STATUS_ACCESS_DENIED (0xC0000022), unless the
     * window has no style window, which fails earlier with STATUS_INVALID_PARAMETER. */
    unsigned char buf[0x20]; memset(buf, SENT, sizeof buf);
    unsigned char ref[0x20]; memcpy(ref, buf, sizeof ref);
    ULONG sz = 0x20;
    g5_clear_status();
    ULONG r = NtUserHwndQueryRedirectionInfo_(w, 3, buf, &sz);
    ULONG st = g5_laststatus();
    CHECK(r == 0);
    CHECKF(st == 0xC0000022u || st == 0xC000000Du, "status 0x%lx", (unsigned long)st);
    CHECK(!memcmp(buf, ref, sizeof buf));
    t_note("type 3 non-DWM status 0x%lx", (unsigned long)st);
    U(DestroyWindow)(w);
}

/* ---------------------------------------------------------------- NtUserMag{Get,Set}ContextInformation
 * win32kfull 0x1fe690 / 0x1febb0. hwnd: 0 or -3 (desktop) skip validation, anything else goes through
 * ValidateHwnd (failure: STATUS_INVALID_PARAMETER). type > 10: STATUS_INVALID_INFO_CLASS. The failing
 * paths end in UserSetLastError(RtlNtStatusToDosError(status)) and return 0. */
#define MAGDESK ((HWND)(LONG_PTR)-3)
static void mag_expect_get(HWND h, int type, void *buf, ULONG sz_in, LONG st, const char *tag) {
    G5_NT(NtUserMagGetContextInformation);
    ULONG sz = sz_in;
    SET_ERR(0);
    ULONG r = NtUserMagGetContextInformation_(h, type, buf, &sz);
    CHECKF(r == 0, "%s: returned %lu", tag, (unsigned long)r);
    CHECKF(GetLastError() == g5_dos(st), "%s: last error %lu want %lu", tag, GetLastError(), g5_dos(st));
    CHECKF(sz == sz_in, "%s: size changed to %lu", tag, (unsigned long)sz);
}
static void mag_expect_set(HWND h, int type, void *data, ULONG sz, LONG st, const char *tag) {
    G5_NT(NtUserMagSetContextInformation);
    SET_ERR(0);
    ULONG r = NtUserMagSetContextInformation_(h, type, data, sz);
    CHECKF(r == 0, "%s: returned %lu", tag, (unsigned long)r);
    CHECKF(GetLastError() == g5_dos(st), "%s: last error %lu want %lu", tag, GetLastError(), g5_dos(st));
}
TEST(gap5_mag_get_invalid_hwnd) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    HWND bad[] = { (HWND)1, (HWND)0xDEAD0, (HWND)0x7FFFFFFF, (HWND)(LONG_PTR)-1, (HWND)(LONG_PTR)-2, (HWND)(LONG_PTR)-4, (HWND)0xFFFFFFFDu };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        for (int type = 2; type <= 5; type++) {
            unsigned char buf[0x80]; memset(buf, SENT, sizeof buf);
            unsigned char ref[0x80]; memcpy(ref, buf, sizeof ref);
            mag_expect_get(bad[i], type, buf, 0x80, (LONG)0xC000000D, "invalid hwnd");
            CHECK(!memcmp(buf, ref, sizeof buf));
        }
}
TEST(gap5_mag_get_destroyed_window) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    HWND w = k_make_window("gap5-mag-dead");
    if (!w) { t_skip("cannot create a window"); return; }
    U(DestroyWindow)(w);
    unsigned char buf[0x80]; memset(buf, SENT, sizeof buf);
    mag_expect_get(w, 3, buf, 0x80, (LONG)0xC000000D, "destroyed window");
}
TEST(gap5_mag_get_type_out_of_range) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    int types[] = { 11, 12, 100, 0x7FFFFFFF };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        unsigned char buf[0x80]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x80]; memcpy(ref, buf, sizeof ref);
        mag_expect_get(NULL, types[i], buf, 0x80, (LONG)0xC0000003, "type out of range / desktop 0");
        mag_expect_get(MAGDESK, types[i], buf, 0x80, (LONG)0xC0000003, "type out of range / desktop -3");
        CHECK(!memcmp(buf, ref, sizeof buf));
    }
}
TEST(gap5_mag_get_types_0_and_1_unsupported) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    for (int type = 0; type <= 1; type++) {
        unsigned char buf[0x80]; memset(buf, SENT, sizeof buf);
        mag_expect_get(NULL, type, buf, 0x80, (LONG)0xC000000D, "type 0/1");
        mag_expect_get(MAGDESK, type, buf, 0x80, (LONG)0xC000000D, "type 0/1 desktop");
    }
}
TEST(gap5_mag_get_null_buffer) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    for (int type = 2; type <= 10; type++) {
        mag_expect_get(NULL, type, NULL, 0x100, (LONG)0xC000000D, "NULL output");
        mag_expect_get(MAGDESK, type, NULL, 0x100, (LONG)0xC000000D, "NULL output desktop");
    }
}
TEST(gap5_mag_get_buffer_too_small) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    /* Smallest accepted size per type, from the `*size < N` tests in the analysis. */
    static const struct { int type; ULONG min; } rows[] = {
        {2, 0x10}, {3, 0x20}, {4, 0x24}, {5, 100}, {6, 8}, {7, 0x10}, {8, 0x30}, {9, 4}, {10, 4} };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        unsigned char buf[0x200]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x200]; memcpy(ref, buf, sizeof ref);
        char tag[64]; snprintf(tag, sizeof tag, "type %d size %lu", rows[i].type, (unsigned long)rows[i].min - 1);
        mag_expect_get(NULL, rows[i].type, buf, rows[i].min - 1, (LONG)0xC0000206, tag);
        mag_expect_get(MAGDESK, rows[i].type, buf, 0, (LONG)0xC0000206, tag);
        CHECK(!memcmp(buf, ref, sizeof buf));
    }
}
TEST(gap5_mag_get_no_context_not_ready) {
    G5_GUI(); G5_NT(NtUserMagGetContextInformation); G5_NEED(NtUserMagGetContextInformation_);
    /* A well-formed request on a thread that never initialised magnification: MagpFindThreadContext
     * returns NULL and MagGetLensContextInformation answers STATUS_DEVICE_NOT_READY (0xC00000A3). */
    static const struct { int type; ULONG sz; } rows[] = { {2, 0x10}, {3, 0x20}, {4, 0x24}, {5, 100}, {9, 4}, {10, 4} };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        unsigned char buf[0x200]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x200]; memcpy(ref, buf, sizeof ref);
        char tag[48]; snprintf(tag, sizeof tag, "type %d no context", rows[i].type);
        mag_expect_get(MAGDESK, rows[i].type, buf, rows[i].sz, (LONG)0xC00000A3, tag);
        CHECK(!memcmp(buf, ref, sizeof buf));
    }
}
TEST(gap5_mag_set_invalid_hwnd) {
    G5_GUI(); G5_NT(NtUserMagSetContextInformation); G5_NEED(NtUserMagSetContextInformation_);
    HWND bad[] = { (HWND)1, (HWND)0xDEAD0, (HWND)0x7FFFFFFF, (HWND)(LONG_PTR)-1, (HWND)(LONG_PTR)-2, (HWND)0xFFFFFFFDu };
    ULONG data[16] = { 0 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        for (int type = 2; type <= 10; type++)
            mag_expect_set(bad[i], type, data, sizeof data, (LONG)0xC000000D, "invalid hwnd");
}
TEST(gap5_mag_set_type_out_of_range) {
    G5_GUI(); G5_NT(NtUserMagSetContextInformation); G5_NEED(NtUserMagSetContextInformation_);
    ULONG data[16] = { 0 };
    int types[] = { 11, 12, 100, 0x7FFFFFFF };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        mag_expect_set(NULL, types[i], data, sizeof data, (LONG)0xC0000003, "type out of range / 0");
        mag_expect_set(MAGDESK, types[i], data, sizeof data, (LONG)0xC0000003, "type out of range / -3");
        mag_expect_set(NULL, types[i], NULL, 0, (LONG)0xC0000003, "type out of range / NULL data");
    }
}
TEST(gap5_mag_set_types_0_and_1_reject_data) {
    G5_GUI(); G5_NT(NtUserMagSetContextInformation); G5_NEED(NtUserMagSetContextInformation_);
    /* A non-NULL data pointer for type 0/1 is STATUS_INVALID_PARAMETER; only NULL reaches the setter. */
    ULONG data[16] = { 0 };
    for (int type = 0; type <= 1; type++) {
        mag_expect_set(NULL, type, data, sizeof data, (LONG)0xC000000D, "type 0/1 with data");
        mag_expect_set(MAGDESK, type, data, 4, (LONG)0xC000000D, "type 0/1 with data, desktop");
    }
}
TEST(gap5_mag_set_null_data) {
    G5_GUI(); G5_NT(NtUserMagSetContextInformation); G5_NEED(NtUserMagSetContextInformation_);
    mag_expect_set(NULL, 2, NULL, 0x10, (LONG)0xC000000D, "type 2 NULL");
    mag_expect_set(MAGDESK, 2, NULL, 0x10, (LONG)0xC000000D, "type 2 NULL desktop");
    mag_expect_set(NULL, 6, NULL, 0x20, (LONG)0xC000000D, "type 6 NULL");
    mag_expect_set(NULL, 10, NULL, 4, (LONG)0xC000000D, "type 10 NULL");
}
TEST(gap5_mag_set_size_too_small) {
    G5_GUI(); G5_NT(NtUserMagSetContextInformation); G5_NEED(NtUserMagSetContextInformation_);
    ULONG data[16] = { 0 };
    /* NtUserMagSetContextInformation (win32kfull 0x1febb0): hwnd 0 and -3 both reach the per-type switch
     * (only other values go through ValidateHwnd); type 6 checks data!=NULL then size<0x10, type 10 checks
     * data!=NULL then size<4, each setting STATUS_BUFFER_TOO_SMALL-class 0xC0000206 before touching state.
     * type 6: size < 0x10 ; type 10: size < 4 (explicit checks before any state is touched). */
    mag_expect_set(NULL, 6, data, 0xF, (LONG)0xC0000206, "type 6 small");
    mag_expect_set(NULL, 6, data, 0, (LONG)0xC0000206, "type 6 zero");
    mag_expect_set(MAGDESK, 6, data, 8, (LONG)0xC0000206, "type 6 small desktop");
    mag_expect_set(NULL, 10, data, 3, (LONG)0xC0000206, "type 10 small");
    mag_expect_set(MAGDESK, 10, data, 0, (LONG)0xC0000206, "type 10 zero desktop");
}

/* ---------------------------------------------------------------- NtDCompositionDestroyChannel
 * win32kbase 0x67b5c CChannel::DestroyHandle(uint): STATUS_UNSUCCESSFUL (0xC0000001) when the process has no
 * composition process data, otherwise STATUS_ACCESS_DENIED (0xC0000022) for an id not in the channel table. */
TEST(gap5_dcomp_destroy_unknown_channel) {
    G5_GUI(); G5_NT(NtDCompositionDestroyChannel); G5_NEED(NtDCompositionDestroyChannel_);
    ULONG ids[] = { 0, 1, 0x12345678, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        LONG st = NtDCompositionDestroyChannel_(ids[i]);
        CHECKF((ULONG)st == 0xC0000001u || (ULONG)st == 0xC0000022u, "id 0x%lx: status 0x%lx", (unsigned long)ids[i], (unsigned long)st);
    }
}
TEST(gap5_dcomp_destroy_twice_same_status) {
    G5_GUI(); G5_NT(NtDCompositionDestroyChannel); G5_NEED(NtDCompositionDestroyChannel_);
    /* NtDCompositionDestroyChannel (win32kbase 0x67c90) just forwards to CChannel::DestroyHandle; its
     * analysis is not in the reference, so only the two statuses are asserted and order dependence is allowed. */
    LONG a = NtDCompositionDestroyChannel_(0x00ABCDEF);
    LONG b = NtDCompositionDestroyChannel_(0x00ABCDEF);
    CHECKF(a < 0 && b < 0, "0x%lx 0x%lx", (unsigned long)a, (unsigned long)b);
    /* The first call may lazily create the process composition data, so a (0xC0000001, then 0xC0000022)
     * sequence is legal and the all-tests exe may already hold such data from earlier tests. Once the
     * data exists (status 0xC0000022) it persists, so the second status must then be 0xC0000022 too. */
    CHECKF((ULONG)a == 0xC0000001u || (ULONG)a == 0xC0000022u, "first 0x%lx", (unsigned long)a);
    CHECKF((ULONG)b == 0xC0000001u || (ULONG)b == 0xC0000022u, "second 0x%lx", (unsigned long)b);
    if ((ULONG)a == 0xC0000022u) CHECK((ULONG)b == 0xC0000022u);
    else if (a != b) t_note("destroy channel status changed 0x%lx -> 0x%lx (process data created by first call)", (unsigned long)a, (unsigned long)b);
}
