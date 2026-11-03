/* NtUserMinMaximize (negatives), NtUserChangeWindowMessageFilterEx, NtUserGetCPD (+GetClassWord), NtUserGetProcessUIContextInformation,
 * NtUserInjectGesture (rejection paths only), NtUserSetImeOwnerWindow (rejection paths only). */
#include "gap.h"

#define WM_GAP (WM_APP + 0x70)

/* ---- NtUserMinMaximize(HWND, int cmd, BOOL fKeepHidden) ----------------------------------------------
 * win32kfull: ValidateHwnd failure -> 0 with 0x578; windows whose function id (low 12 bits of the fnid word at tagWND+0x2a,
 * ignoring 0x2000) is 0x29d (desktop) or 0x29f (switch window) are skipped and 0 is returned without an error. */
TEST(gap1_minmax_invalid_window) {
    GAP_BEGIN(); NtUserMinMaximize_pfn mm = UN(NtUserMinMaximize); REQUIRE(mm);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xdeadbeef, (HWND)(ULONG_PTR)0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(mm(bad[i], SW_SHOWMINNOACTIVE, TRUE), 0);
        EXPECT_ERR(0x578);
    }
}

TEST(gap1_minmax_destroyed_window) {
    GAP_BEGIN(); NtUserMinMaximize_pfn mm = UN(NtUserMinMaximize); REQUIRE(mm);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    U(DestroyWindow)(w);
    GAP_POISON();
    EXPECT_EQ(mm(w, SW_SHOWMINNOACTIVE, TRUE), 0);
    EXPECT_ERR(0x578);
}

TEST(gap1_minmax_desktop_window_ignored) {
    GAP_BEGIN(); NtUserMinMaximize_pfn mm = UN(NtUserMinMaximize); REQUIRE(mm);
    HWND dw = U(GetDesktopWindow)();
    int cmds[] = { SW_SHOWMINNOACTIVE, SW_SHOWMAXIMIZED, SW_RESTORE, SW_HIDE };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(mm(dw, cmds[i], TRUE), 0);
        EXPECT_EQ(GetLastError(), 0xBEEF);                     /* skipped by the fnid test, no error raised */
    }
    CHECK(!U(IsIconic)(dw));
}

TEST(gap1_minmax_desktop_fnid_in_window_struct) {
    GAP_BEGIN();
    HWND dw = U(GetDesktopWindow)();
    void *p = k_wnd_ptr(dw);
    unsigned short fnid = 0;
    if (!p || !k_try_read_u16((char *)p + 0x2a, &fnid)) { t_skip("desktop window structure not readable"); return; }
    t_note("desktop fnid word 0x%04x", fnid);
    EXPECT_EQ(fnid & 0x2fff, 0x29d);                           /* the value NtUserMinMaximize tests against */
}

TEST(gap1_minmax_ordinary_window_fnid) {
    GAP_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    void *p = k_wnd_ptr(w);
    unsigned short fnid = 0;
    if (!p || !k_try_read_u16((char *)p + 0x2a, &fnid)) { t_skip("window structure not readable"); U(DestroyWindow)(w); return; }
    CHECK((fnid & 0x2fff) != 0x29d && (fnid & 0x2fff) != 0x29f);   /* so it is eligible for min/maximize */
    U(DestroyWindow)(w);
}

/* ---- NtUserChangeWindowMessageFilterEx(HWND, UINT msg, DWORD action, CHANGEFILTERSTRUCT *) ----------
 * win32kfull: invalid window -> 0 / 0x578; window of another process -> 0 / ERROR_ACCESS_DENIED (5); action must be
 * 0 (reset, with msg == 0), 1 or 2 (with msg != 0), else 0 / 0x57; pChangeFilterStruct may be NULL, otherwise its cbSize
 * must be 8 (else 0 / 0x57) and it is written back (ExtStatus) on success. */
#define CWF_BEGIN() \
    GAP_BEGIN(); NtUserChangeWindowMessageFilterEx_pfn cwf = UN(NtUserChangeWindowMessageFilterEx); REQUIRE(cwf); \
    HWND w = gap_win(0, GAP_STYLE_SYS); if (!w) { t_skip("no window"); return; }

TEST(gap1_msgfilter_invalid_window) {
    GAP_BEGIN(); NtUserChangeWindowMessageFilterEx_pfn cwf = UN(NtUserChangeWindowMessageFilterEx); REQUIRE(cwf);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(cwf(bad[i], WM_GAP, MSGFLT_ALLOW, NULL), 0);
        EXPECT_ERR(0x578);
    }
}

TEST(gap1_msgfilter_other_process_window) {
    GAP_BEGIN(); NtUserChangeWindowMessageFilterEx_pfn cwf = UN(NtUserChangeWindowMessageFilterEx); REQUIRE(cwf);
    HWND dw = U(GetDesktopWindow)();                           /* owned by a system process' thread */
    GAP_POISON();
    EXPECT_EQ(cwf(dw, WM_GAP, MSGFLT_ALLOW, NULL), 0);
    EXPECT_ERR(5);
    GAP_POISON();
    EXPECT_EQ(cwf(dw, 0, MSGFLT_RESET, NULL), 0);
    EXPECT_ERR(5);
}

TEST(gap1_msgfilter_bad_action) {
    CWF_BEGIN();
    DWORD acts[] = { 3, 4, 0x10, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof acts / sizeof acts[0]; i++) {
        CHANGEFILTERSTRUCT cf; cf.cbSize = 8; cf.ExtStatus = 0x77;
        GAP_POISON();
        EXPECT_EQ(cwf(w, WM_GAP, acts[i], &cf), 0);
        EXPECT_ERR(0x57);
        EXPECT_EQ(cf.ExtStatus, 0x77);                         /* nothing written back on failure */
    }
    U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_message_action_pairing) {
    CWF_BEGIN();
    /* reset takes no message; allow/disallow need one */
    GAP_POISON(); EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_RESET, NULL), 0); EXPECT_ERR(0x57);
    GAP_POISON(); EXPECT_EQ(cwf(w, 0, MSGFLT_ALLOW, NULL), 0);      EXPECT_ERR(0x57);
    GAP_POISON(); EXPECT_EQ(cwf(w, 0, MSGFLT_DISALLOW, NULL), 0);   EXPECT_ERR(0x57);
    U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_bad_struct_size) {
    CWF_BEGIN();
    DWORD sizes[] = { 0, 1, 4, 7, 9, 12, 16, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        CHANGEFILTERSTRUCT cf; cf.cbSize = sizes[i]; cf.ExtStatus = 0x77;
        GAP_POISON();
        EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_ALLOW, &cf), 0);
        EXPECT_ERR(0x57);
        EXPECT_EQ(cf.ExtStatus, 0x77);
        EXPECT_EQ(cf.cbSize, sizes[i]);
    }
    U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_null_struct_accepted) {
    CWF_BEGIN();
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_ALLOW, NULL), 1);
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_DISALLOW, NULL), 1);
    EXPECT_EQ(cwf(w, 0, MSGFLT_RESET, NULL), 1);
    U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_extstatus_sequence) {
    CWF_BEGIN();
    CHANGEFILTERSTRUCT cf;
    cf.cbSize = 8; cf.ExtStatus = 0x77;
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_ALLOW, &cf), 1);
    EXPECT_EQ(cf.cbSize, 8);
    DWORD first = cf.ExtStatus;
    CHECK(first <= MSGFLTINFO_ALLOWED_HIGHER);
    cf.ExtStatus = 0x77;
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_ALLOW, &cf), 1);
    DWORD second = cf.ExtStatus;
    /* win32kfull!_ChangeWindowMessageFilterEx: adding a message already in the window's filter reports 1 (already allowed) */
    if (first == MSGFLTINFO_NONE) EXPECT_EQ(second, MSGFLTINFO_ALREADYALLOWED_FORWND);
    else t_note("UIPI filter not enforced or message pre-allowed: first %lu second %lu", (unsigned long)first, (unsigned long)second);
    cf.ExtStatus = 0x77;
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_DISALLOW, &cf), 1);
    CHECK(cf.ExtStatus <= MSGFLTINFO_ALLOWED_HIGHER);
    t_note("disallow ExtStatus %lu", (unsigned long)cf.ExtStatus);
    cf.ExtStatus = 0x77;
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_DISALLOW, &cf), 1);        /* removing again: not in the filter */
    t_note("second disallow ExtStatus %lu", (unsigned long)cf.ExtStatus);
    EXPECT_EQ(cwf(w, 0, MSGFLT_RESET, NULL), 1);
    U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_matches_user_export) {
    CWF_BEGIN();
    typedef BOOL (WINAPI *cwf_t)(HWND, UINT, DWORD, PCHANGEFILTERSTRUCT);
    cwf_t u = (cwf_t)t_proc_in("user32.dll", "ChangeWindowMessageFilterEx");
    REQUIRE(u);
    CHANGEFILTERSTRUCT cf; cf.cbSize = 8; cf.ExtStatus = 0;
    EXPECT_EQ(u(w, WM_GAP + 1, MSGFLT_ALLOW, &cf), 1);        /* the export is a bare jump to the syscall stub */
    GAP_POISON();
    EXPECT_EQ(u(w, WM_GAP + 1, 3, &cf), 0);
    EXPECT_ERR(0x57);
    EXPECT_EQ(cwf(w, 0, MSGFLT_RESET, NULL), 1);
    U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_per_window) {
    CWF_BEGIN();
    HWND w2 = gap_win(0, GAP_STYLE_SYS);
    if (!w2) { U(DestroyWindow)(w); t_skip("second window"); return; }
    CHANGEFILTERSTRUCT a, b;
    a.cbSize = b.cbSize = 8; a.ExtStatus = b.ExtStatus = 0x77;
    EXPECT_EQ(cwf(w, WM_GAP + 2, MSGFLT_ALLOW, &a), 1);
    EXPECT_EQ(cwf(w2, WM_GAP + 2, MSGFLT_ALLOW, &b), 1);
    t_note("ExtStatus first window %lu, second window %lu", (unsigned long)a.ExtStatus, (unsigned long)b.ExtStatus);
    if (a.ExtStatus == 0) EXPECT_EQ(b.ExtStatus, 0);           /* filters are kept per window */
    cwf(w, 0, MSGFLT_RESET, NULL); cwf(w2, 0, MSGFLT_RESET, NULL);
    U(DestroyWindow)(w2); U(DestroyWindow)(w);
}

TEST(gap1_msgfilter_destroyed_window) {
    CWF_BEGIN();
    U(DestroyWindow)(w);
    GAP_POISON();
    EXPECT_EQ(cwf(w, WM_GAP, MSGFLT_ALLOW, NULL), 0);
    EXPECT_ERR(0x578);
}

TEST(gap1_msgfilter_many_messages) {
    CWF_BEGIN();
    int ok = 1;
    for (UINT m = WM_GAP; m < WM_GAP + 40; m++) {
        CHANGEFILTERSTRUCT cf; cf.cbSize = 8;
        if (!cwf(w, m, MSGFLT_ALLOW, &cf)) ok = 0;
    }
    CHECK(ok);
    EXPECT_EQ(cwf(w, 0, MSGFLT_RESET, NULL), 1);               /* a reset frees the whole per-window table */
    U(DestroyWindow)(w);
}

/* ---- NtUserGetCPD(HWND, DWORD flags, ULONG_PTR proc) -------------------------------------------------
 * win32kfull: ValidateHwnd failure -> 0 / 0x578; (flags & ~3) must be 0x20, 0x40 or 0x80 else 0 with no error; user32
 * calls it with 0x42 - bAnsi (so 0x41 / 0x42) from _GetWindowLongPtr(GWLP_WNDPROC) when the caller's A/W flavour differs
 * from the window procedure's, to obtain a call-procedure-data handle. */
TEST(gap1_cpd_invalid_window) {
    GAP_BEGIN(); NtUserGetCPD_pfn cpd = UN(NtUserGetCPD); REQUIRE(cpd);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(cpd(bad[i], 0x41, 0), 0);
        EXPECT_ERR(0x578);
    }
}

TEST(gap1_cpd_bad_flags) {
    GAP_BEGIN(); NtUserGetCPD_pfn cpd = UN(NtUserGetCPD); REQUIRE(cpd);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    DWORD flags[] = { 0, 1, 2, 3, 4, 0x10, 0x1c, 0x1f, 0x24, 0x44, 0x84, 0x100, 0x1000, 0x10020 };
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(cpd(w, flags[i], 0), 0);
        EXPECT_EQ(GetLastError(), 0xBEEF);                     /* the window validated; the flag test fails silently */
    }
    U(DestroyWindow)(w);
}

TEST(gap1_cpd_wndproc_flavours_ansi_window) {
    GAP_BEGIN(); NtUserGetCPD_pfn cpd = UN(NtUserGetCPD); REQUIRE(cpd);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    LONG_PTR a = U(GetWindowLongPtrA)(w, GWLP_WNDPROC), u = U(GetWindowLongPtrW)(w, GWLP_WNDPROC);
    CHECK(a != 0 && u != 0);
    CHECK(a != u);                                             /* the W view of an ANSI procedure is a CPD handle */
    LONG_PTR u2 = U(GetWindowLongPtrW)(w, GWLP_WNDPROC);
    EXPECT_EQ(u, u2);                                          /* CPDs are cached per (procedure, flavour) */
    /* flags 0x22: the Unicode view of an ANSI procedure; the CPD is the same object GetWindowLongPtrW hands out */
    ULONG_PTR direct = cpd(w, 0x22, (ULONG_PTR)a);
    EXPECT_EQ(direct, u);
    EXPECT_EQ(cpd(w, 0x22, (ULONG_PTR)a), direct);
    U(DestroyWindow)(w);
}

TEST(gap1_cpd_wndproc_flavours_unicode_window) {
    GAP_BEGIN(); NtUserGetCPD_pfn cpd = UN(NtUserGetCPD); REQUIRE(cpd);
    HWND w = gap_win(1, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    LONG_PTR a = U(GetWindowLongPtrA)(w, GWLP_WNDPROC), u = U(GetWindowLongPtrW)(w, GWLP_WNDPROC);
    CHECK(a != 0 && u != 0);
    CHECK(a != u);                                             /* here the A view is the CPD handle */
    ULONG_PTR direct = cpd(w, 0x21, (ULONG_PTR)u);              /* flags 0x21: the ANSI view of a Unicode procedure */
    EXPECT_EQ(direct, a);
    U(DestroyWindow)(w);
}

TEST(gap1_cpd_handle_is_user_object) {
    GAP_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    LONG_PTR u = U(GetWindowLongPtrW)(w, GWLP_WNDPROC), a = U(GetWindowLongPtrA)(w, GWLP_WNDPROC);
    struct k_user_entry e;
    if (u != a && k_user_entry((HANDLE)u, &e)) {
        t_note("CPD handle %p entry type %u flags %02x", (void *)u, e.type, e.flags);
        CHECK(e.type != 0);
        EXPECT_EQ(e.flags & 1, 0);
        CHECK(e.type != 1 && e.type != 2 && e.type != 3);      /* not a window, menu or cursor */
    } else t_skip("no readable CPD handle entry");
    U(DestroyWindow)(w);
}

TEST(gap1_cpd_same_flavour_returns_real_proc) {
    GAP_BEGIN();
    HWND wa = gap_win(0, GAP_STYLE_SYS), ww = gap_win(1, GAP_STYLE_SYS);
    if (!wa || !ww) { t_skip("no windows"); if (wa) U(DestroyWindow)(wa); if (ww) U(DestroyWindow)(ww); return; }
    /* matching flavour: the real client pointer comes back untouched (user32 does not call NtUserGetCPD) */
    LONG_PTR pa = U(GetWindowLongPtrA)(wa, GWLP_WNDPROC), pw = U(GetWindowLongPtrW)(ww, GWLP_WNDPROC);
    CHECK(pa > 0x10000 && pw > 0x10000);
    CHECK(pa != pw);
    U(DestroyWindow)(wa); U(DestroyWindow)(ww);
}

TEST(gap1_cpd_class_wndproc) {
    GAP_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    ULONG_PTR ca = U(GetClassLongPtrA)(w, GCLP_WNDPROC), cw = U(GetClassLongPtrW)(w, GCLP_WNDPROC);
    CHECK(ca != 0 && cw != 0);
    CHECK(ca != cw);
    EXPECT_EQ(U(GetWindowLongPtrA)(w, GWLP_WNDPROC), (LONG_PTR)ca);   /* unsubclassed: class and window procedure agree */
    U(DestroyWindow)(w);
}

TEST(gap1_classword_atom) {
    GAP_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    GAP_POISON();
    EXPECT_EQ(U(GetClassWord)(w, GCW_ATOM), gap_atomA);        /* user32!_GetClassData handles index -32 itself */
    U(DestroyWindow)(w);
}

TEST(gap1_classword_extra_bytes_bounds) {
    GAP_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    /* the class has cbClsExtra = 8: word offsets 0..6 are readable, 7 and beyond fail with ERROR_INVALID_INDEX */
    for (int off = 0; off <= 6; off++) {
        GAP_POISON();
        EXPECT_EQ(U(GetClassWord)(w, off), 0);
        EXPECT_EQ(GetLastError(), 0xBEEF);
    }
    int bad[] = { 7, 8, 9, 100, -2, -4, -100 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(U(GetClassWord)(w, bad[i]), 0);
        EXPECT_ERR(0x585);
    }
    U(DestroyWindow)(w);
}

TEST(gap1_classword_invalid_window) {
    GAP_BEGIN();
    GAP_POISON();
    EXPECT_EQ(U(GetClassWord)(NULL, GCW_ATOM), 0);
    EXPECT_ERR(0x578);
}

/* ---- NtUserGetProcessUIContextInformation(HANDLE process, ULONGLONG *info) ----------------------------
 * win32kfull: -1 selects the current process; otherwise the handle needs PROCESS_QUERY_LIMITED_INFORMATION (0x1000) and must
 * be a process of this session with a W32PROCESS carrying flags 0x21; failures set 0x57 (handle) or 0x5bf (no win32k state).
 * The result packs two dwords: low = (W32PROCESS flags >> 4) & 3, high = bit0 from flag 0x40 | bit1 from flag 0x100. */
TEST(gap1_uictx_current_process) {
    GAP_BEGIN(); NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    ULONGLONG info = 0x5a5a5a5a5a5a5a5aULL;
    EXPECT_EQ(f((HANDLE)-1, &info), 1);
    CHECK((info & 0xffffffff) <= 3);
    CHECK((info >> 32) <= 3);
    t_note("process UI context info %016llx", (unsigned long long)info);
}

TEST(gap1_uictx_real_handle_matches_pseudo) {
    GAP_BEGIN(); NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    HANDLE h = U(OpenProcess)(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (!h) { t_skip("OpenProcess"); return; }
    ULONGLONG a = 1, b = 2;
    EXPECT_EQ(f((HANDLE)-1, &a), 1);
    EXPECT_EQ(f(h, &b), 1);
    EXPECT_EQ(a, b);
    CloseHandle(h);
}

TEST(gap1_uictx_user_export_is_syscall) {
    GAP_BEGIN(); GetProcessUIContextInformation_pfn u = UN(GetProcessUIContextInformation); REQUIRE(u);
    NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    ULONGLONG a = 0, b = 1;
    EXPECT_EQ(u((HANDLE)-1, &a), 1);
    EXPECT_EQ(f((HANDLE)-1, &b), 1);
    EXPECT_EQ(a, b);
}

TEST(gap1_uictx_insufficient_access) {
    GAP_BEGIN(); NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    HANDLE weak = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &weak, PROCESS_TERMINATE, FALSE, 0)) { t_skip("DuplicateHandle"); return; }
    ULONGLONG info = 0x5a5a5a5a5a5a5a5aULL;
    GAP_POISON();
    EXPECT_EQ(f(weak, &info), 0);                              /* ObReferenceObjectByHandle(0x1000) fails */
    EXPECT_ERR(0x57);
    EXPECT_EQ(info, 0x5a5a5a5a5a5a5a5aULL);
    CloseHandle(weak);
}

TEST(gap1_uictx_invalid_handles) {
    GAP_BEGIN(); NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    HANDLE bad[] = { NULL, (HANDLE)(ULONG_PTR)4, (HANDLE)(ULONG_PTR)0x1234567, (HANDLE)(ULONG_PTR)-2 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ULONGLONG info = 0x5a5a5a5a5a5a5a5aULL;
        GAP_POISON();
        int r = f(bad[i], &info);
        if (bad[i] == (HANDLE)(ULONG_PTR)4 || bad[i] == (HANDLE)(ULONG_PTR)-2) { t_note("handle %p -> %d err %lu", bad[i], r, (unsigned long)GetLastError()); continue; }
        EXPECT_EQ(r, 0);
        EXPECT_ERR(0x57);
        EXPECT_EQ(info, 0x5a5a5a5a5a5a5a5aULL);
    }
}

TEST(gap1_uictx_wrong_object_type) {
    GAP_BEGIN(); NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    ULONGLONG info = 0x5a5a5a5a5a5a5a5aULL;
    if (!ev) { t_skip("CreateEvent"); return; }
    GAP_POISON();
    EXPECT_EQ(f(ev, &info), 0);                                /* STATUS_OBJECT_TYPE_MISMATCH */
    EXPECT_ERR(0x57);
    EXPECT_EQ(info, 0x5a5a5a5a5a5a5a5aULL);
    CloseHandle(ev);
}

CHILD(gap1_sleeper) { Sleep(4000); return 0; }

TEST(gap1_uictx_process_without_win32k_state) {
    GAP_BEGIN(); NtUserGetProcessUIContextInformation_pfn f = UN(NtUserGetProcessUIContextInformation); REQUIRE(f);
    if (t_is_wine()) { t_skip("needs real win32k process state"); return; }
    PROCESS_INFORMATION pi;
    HANDLE h = t_spawn_child("gap1_sleeper", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) { t_skip("cannot spawn child"); return; }
    Sleep(100);
    HANDLE q = U(OpenProcess)(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId);
    if (q) {
        ULONGLONG info = 0x5a5a5a5a5a5a5a5aULL;
        GAP_POISON();
        int r = f(q, &info);
        t_note("child without win32k: ret %d err %lu", r, (unsigned long)GetLastError());
        /* a console-subsystem child that never touched user32 has no W32PROCESS: 0x5bf; once it has one the call succeeds */
        if (r == 0) { CHECK(GetLastError() == 0x5bf || GetLastError() == 0x57); EXPECT_EQ(info, 0x5a5a5a5a5a5a5a5aULL); }
        CloseHandle(q);
    }
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 5000);
    CloseHandle(pi.hThread); CloseHandle(h);
}

/* ---- NtUserInjectGesture(HWND, int, LONGLONG, void *gestureInfo, LONGLONG): rejection paths only --------
 * win32kfull: ValidateHwnd failure -> 0 / 0x578; unless (arg3 == 0 && arg4 != NULL && arg2 == 0) the call returns 0 with
 * 0x57 before touching any caller memory. */
TEST(gap1_injectgesture_invalid_window) {
    GAP_BEGIN(); NtUserInjectGesture_pfn f = UN(NtUserInjectGesture); REQUIRE(f);
    BYTE buf[0x40] = {0};
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(f(bad[i], 0, 0, buf, 0), 0);
        EXPECT_ERR(0x578);
    }
}

TEST(gap1_injectgesture_argument_shape) {
    GAP_BEGIN(); NtUserInjectGesture_pfn f = UN(NtUserInjectGesture); REQUIRE(f);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    BYTE buf[0x40] = {0};
    GAP_POISON(); EXPECT_EQ(f(w, 0, 0, NULL, 0), 0);  EXPECT_ERR(0x57);       /* no gesture structure */
    GAP_POISON(); EXPECT_EQ(f(w, 1, 0, buf, 0), 0);   EXPECT_ERR(0x57);       /* second argument must be zero */
    GAP_POISON(); EXPECT_EQ(f(w, -1, 0, buf, 0), 0);  EXPECT_ERR(0x57);
    GAP_POISON(); EXPECT_EQ(f(w, 0, 1, buf, 0), 0);   EXPECT_ERR(0x57);       /* third argument must be zero */
    GAP_POISON(); EXPECT_EQ(f(w, 0, -1, buf, 0), 0);  EXPECT_ERR(0x57);
    GAP_POISON(); EXPECT_EQ(f(w, 3, 5, NULL, 0), 0);  EXPECT_ERR(0x57);
    U(DestroyWindow)(w);
}

TEST(gap1_injectgesture_nothing_posted_on_rejection) {
    GAP_BEGIN(); NtUserInjectGesture_pfn f = UN(NtUserInjectGesture); REQUIRE(f);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    BYTE buf[0x40] = {0};
    f(w, 1, 0, buf, 0);
    MSG m;
    EXPECT_EQ(U(PeekMessageA)(&m, w, 0x119, 0x119, PM_REMOVE), 0);   /* WM_GESTURE is only posted by the accepting path */
    U(DestroyWindow)(w);
}

/* ---- NtUserSetImeOwnerWindow(HWND ime, HWND owner): rejection paths only ---------------------------------
 * win32kfull: ValidateHwnd failure -> 0 / 0x578; with the IME subsystem disabled (SERVERINFO.dwSRVIFlags & 4 == 0) -> 0 /
 * ERROR_CALL_NOT_IMPLEMENTED (0x78); otherwise any window whose fnid is not 0x2a9 (the IME class) returns 0 silently. */
static int gap_ime_enabled(int *known) {
    struct k_user_shared_info si;
    unsigned long flags = 0;
    *known = 0;
    if (!k_shared_info(&si) || !si.psi || !k_try_read_u32(si.psi, &flags)) return 0;
    *known = 1;
    return (flags & 4) != 0;
}

TEST(gap1_imeowner_invalid_window) {
    GAP_BEGIN(); NtUserSetImeOwnerWindow_pfn f = UN(NtUserSetImeOwnerWindow); REQUIRE(f);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(f(bad[i], NULL), 0);
        EXPECT_ERR(0x578);
    }
}

TEST(gap1_imeowner_ordinary_window) {
    GAP_BEGIN(); NtUserSetImeOwnerWindow_pfn f = UN(NtUserSetImeOwnerWindow); REQUIRE(f);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    int known; int ime = gap_ime_enabled(&known);
    GAP_POISON();
    EXPECT_EQ(f(w, NULL), 0);
    DWORD e = GetLastError();
    if (known) EXPECT_EQ(e, ime ? 0xBEEF : 0x78);
    else t_note("SERVERINFO flags unreadable; last error %lu", (unsigned long)e);
    t_note("IME %s", known ? (ime ? "enabled" : "disabled") : "unknown");
    U(DestroyWindow)(w);
}

TEST(gap1_imeowner_owner_argument_ignored_for_non_ime_window) {
    GAP_BEGIN(); NtUserSetImeOwnerWindow_pfn f = UN(NtUserSetImeOwnerWindow); REQUIRE(f);
    HWND w = gap_win(0, GAP_STYLE_SYS), o = gap_win(0, GAP_STYLE_SYS);
    if (!w || !o) { t_skip("windows"); if (w) U(DestroyWindow)(w); if (o) U(DestroyWindow)(o); return; }
    EXPECT_EQ(f(w, o), 0);
    EXPECT_EQ(f(w, (HWND)(ULONG_PTR)0xdeadbeef), 0);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(w, GW_OWNER), 0);        /* ownership untouched */
    U(DestroyWindow)(o); U(DestroyWindow)(w);
}

TEST(gap1_imeowner_desktop_window) {
    GAP_BEGIN(); NtUserSetImeOwnerWindow_pfn f = UN(NtUserSetImeOwnerWindow); REQUIRE(f);
    EXPECT_EQ(f(U(GetDesktopWindow)(), NULL), 0);
}
