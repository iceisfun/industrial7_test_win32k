/* Keyboard layout list / thread layout / name / load / unload / activate, observed through the
 * win32u stubs and the user32 wrappers, plus the _CLIENTINFO fields the kernel keeps in the TEB.
 *
 * Kernel references (Win10 19041):
 *   NtUserGetKeyboardLayoutList -> the kernel handler: counts / copies
 *     the winsta layout ring (next link), skipping entries flagged 0x20000000 (unloaded);
 *     a NULL buffer forces nBuff = 0 (count only).
 *   NtUserGetKeyboardLayout -> _GetKeyboardLayout: idThread 0 = the calling thread's active HKL,
 *     otherwise a walk of the thread list, 0 when the thread is not found.
 *   the kernel handler: 0 = previous (prev link), 1 = next, high word 0 = match by LANGID,
 *     otherwise exact HKL; entries flagged 0x20000000 are skipped for 0/1.
 *   the kernel handler: returns the previous HKL, stores the new layout for the thread and mirrors hKL/CodePage into the TEB client info.
 *   NtUserSetThreadLayoutHandles: swaps the thread layout only (no _CLIENTINFO update).
 */
#include "kls.h"
#define P "kbd_layouts_state_"

/* ======================================================================================= list */

TEST(kbd_layouts_state_list_count_positive) {
    KLS_NEED();
    int n = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    CHECKF(n >= 1, "layout count %d", n);   /* the window-station ring always holds the base layout */
    CHECK(n < KLS_MAXL);
}

TEST(kbd_layouts_state_list_null_buffer_ignores_n) {
    KLS_NEED();
    /* NtUserGetKeyboardLayoutList then the counting branch of
     * _GetKeyboardLayoutList runs, so any n returns the full count. */
    int base = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    static const UINT ns[] = { 1, 2, 5, 64, 1000 };
    for (unsigned i = 0; i < sizeof ns / sizeof *ns; i++) {
        int n = UN(NtUserGetKeyboardLayoutList)(ns[i], NULL);
        CHECKF(n == base, "n=%u -> %d want %d", ns[i], n, base);
    }
}

TEST(kbd_layouts_state_list_zero_n_with_buffer_untouched) {
    KLS_NEED();
    HKL buf[4]; for (int i = 0; i < 4; i++) buf[i] = (HKL)(ULONG_PTR)0xDEADBEEFDEADBEEFull;
    int base = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    int n = UN(NtUserGetKeyboardLayoutList)(0, buf);   /* nBuff == 0 takes the counting branch */
    CHECK(n == base);
    for (int i = 0; i < 4; i++) CHECK(buf[i] == (HKL)(ULONG_PTR)0xDEADBEEFDEADBEEFull);
}

TEST(kbd_layouts_state_list_fill_matches_count) {
    KLS_NEED();
    HKL l[KLS_MAXL]; int base = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    int n = UN(NtUserGetKeyboardLayoutList)(KLS_MAXL, l);
    CHECKF(n == base, "filled %d count %d", n, base);
    for (int i = 0; i < n && i < KLS_MAXL; i++) CHECK(l[i] != NULL);
}

TEST(kbd_layouts_state_list_small_buffer_truncates) {
    KLS_NEED();
    int base = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    if (base < 2) {
        /* one installed layout: load a second one for the duration of the test (kls_cleanup unloads it again) */
        HKL o = kls_other(kls_cur());
        base = UN(NtUserGetKeyboardLayoutList)(0, NULL);
        if (!o || base < 2) { kls_cleanup(); t_skip("only %d layout and no second one is loadable", base); return; }
    }
    HKL all[KLS_MAXL]; UN(NtUserGetKeyboardLayoutList)(KLS_MAXL, all);
    HKL b[3]; b[0] = b[1] = b[2] = (HKL)(ULONG_PTR)0x1111222233334444ull;
    int n = UN(NtUserGetKeyboardLayoutList)(1, b);
    /* the copy loop decrements nBuff per written entry and stops at 0 */
    CHECK(n == 1);
    CHECK(b[0] == all[0]);
    CHECK(b[1] == (HKL)(ULONG_PTR)0x1111222233334444ull);
    CHECK(b[2] == (HKL)(ULONG_PTR)0x1111222233334444ull);
    kls_cleanup();
}

TEST(kbd_layouts_state_load_then_unload_restores_the_list) {
    KLS_NEED(); REQUIRE(U(UnloadKeyboardLayout));
    if (t_is_wine()) { t_skip("layout unloading is win32k specific"); return; }
    HKL b0[KLS_MAXL]; int n0 = kls_list(b0, KLS_MAXL);
    HKL o = kls_other(kls_cur());
    if (!o) { kls_cleanup(); t_skip("no second layout is loadable"); return; }
    int n1 = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    kls_cleanup();
    int n2 = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    HKL b2[KLS_MAXL]; int m2 = kls_list(b2, KLS_MAXL);
    CHECKF(n1 >= n0, "after load %d before %d", n1, n0);
    CHECKF(n2 == n0, "after unload %d before %d", n2, n0);
    for (int i = 0; i < n0 && i < m2; i++) CHECK(b0[i] == b2[i]);       /* the original layouts keep their order */
}

TEST(kbd_layouts_state_list_large_buffer_leaves_tail) {
    KLS_NEED();
    int base = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    HKL b[KLS_MAXL];
    for (int i = 0; i < KLS_MAXL; i++) b[i] = (HKL)(ULONG_PTR)0x5a5a5a5a5a5a5a5aull;
    int n = UN(NtUserGetKeyboardLayoutList)(KLS_MAXL, b);
    CHECK(n == base);
    for (int i = n; i < KLS_MAXL; i++) CHECK(b[i] == (HKL)(ULONG_PTR)0x5a5a5a5a5a5a5a5aull);
}

TEST(kbd_layouts_state_list_entries_have_langid) {
    KLS_NEED();
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    for (int i = 0; i < n; i++) {
        /* documented HKL shape: low word = language identifier (never 0) */
        CHECKF(((ULONG_PTR)l[i] & 0xffff) != 0, "entry %d = %p", i, l[i]);
        /* the HKL is stored as (longlong)(int)value: the high dword is the sign extension of bit 31 */
        unsigned long hi = (unsigned long)((ULONG_PTR)l[i] >> 32), lo = (unsigned long)((ULONG_PTR)l[i] & 0xffffffff);
        CHECKF(hi == ((lo & 0x80000000ul) ? 0xfffffffful : 0), "entry %d = %p not sign extended", i, l[i]);
    }
}

TEST(kbd_layouts_state_list_entries_unique) {
    KLS_NEED();
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECKF(l[i] != l[j], "dup %p at %d,%d", l[i], i, j);
}

TEST(kbd_layouts_state_list_no_special_values) {
    KLS_NEED();
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    /* HKL_PREV (0) and HKL_NEXT (1) are HKLtoPKL selectors and can never be real layout handles */
    for (int i = 0; i < n; i++) { CHECK(l[i] != (HKL)0); CHECK(l[i] != (HKL)1); }
}

TEST(kbd_layouts_state_list_stable_between_calls) {
    KLS_NEED();
    HKL a[KLS_MAXL], b[KLS_MAXL];
    int na = kls_list(a, KLS_MAXL), nb = kls_list(b, KLS_MAXL);
    CHECK(na == nb);
    CHECK(memcmp(a, b, sizeof(HKL) * (na < nb ? na : nb)) == 0);
}

TEST(kbd_layouts_state_list_user_wrapper_matches_nt) {
    KLS_NEED();
    REQUIRE(U(GetKeyboardLayoutList));
    /* user32!GetKeyboardLayoutList is a bare jump to NtUserGetKeyboardLayoutList */
    int n1 = U(GetKeyboardLayoutList)(0, NULL), n2 = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    CHECK(n1 == n2);
    HKL a[KLS_MAXL], b[KLS_MAXL];
    int na = U(GetKeyboardLayoutList)(KLS_MAXL, a), nb = kls_list(b, KLS_MAXL);
    CHECK(na == nb);
    CHECK(memcmp(a, b, sizeof(HKL) * (na < nb ? na : nb)) == 0);
}

TEST(kbd_layouts_state_list_contains_thread_layout) {
    KLS_NEED();
    HKL cur = kls_cur();
    CHECK(cur != NULL);
    CHECKF(kls_in_list(cur), "thread layout %p missing from the window-station list", cur);
}

static DWORD w_list_grow(void *arg) {
    (void)arg;
    int before = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    HKL cur = kls_cur();
    HKL o = kls_other(cur);
    if (!o) { t_note("no second layout available"); return 0; }
    if (!kls_was_added(o)) { t_note("a second layout already existed; grow/shrink not observable"); return 0; }
    int mid = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    CHECKF(mid == before + 1, "after load %d (was %d)", mid, before);
    CHECK(kls_in_list(o));
    CHECK(U(UnloadKeyboardLayout)(o));
    kls_nadded--;
    int after = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    CHECKF(after == before, "after unload %d want %d", after, before);
    CHECK(!kls_in_list(o));   /* unloaded entries carry the 0x20000000 flag and are skipped */
    return 0;
}
TEST(kbd_layouts_state_list_grows_and_shrinks_on_load_unload) {
    KLS_NEED(); REQUIRE(U(UnloadKeyboardLayout));
    if (t_is_wine()) { t_skip("win32u.NtUserUnloadKeyboardLayout is an aborting stub under Wine"); return; }
    kls_thread(w_list_grow, NULL);
    kls_cleanup();
}

/* ============================================================================ GetKeyboardLayout */

TEST(kbd_layouts_state_gkl_current_nonzero_langid) {
    KLS_NEED();
    HKL h = kls_cur();
    CHECK(h != NULL);
    CHECK(((ULONG_PTR)h & 0xffff) != 0);
}

TEST(kbd_layouts_state_gkl_user_matches_nt) {
    KLS_NEED();
    unsigned long f = 0;
    HKL a = U(GetKeyboardLayout)(0), b = kls_cur();
    /* user32!GetKeyboardLayout only post-processes through the IMM callback when CLIENTINFO flag bit 14 is set */
    if (kls_ci_u32(0x0, &f) && !(f >> 14 & 1)) CHECK(a == b);
    else t_note("CLIENTINFO flag bit14 set (%lx): wrapper may substitute the IMM layout", f);
}

TEST(kbd_layouts_state_gkl_own_tid_equals_zero) {
    KLS_NEED();
    CHECK(UN(NtUserGetKeyboardLayout)(GetCurrentThreadId()) == kls_cur());
}

TEST(kbd_layouts_state_gkl_bogus_tids_null) {
    KLS_NEED();
    /* thread ids are multiples of 4 and these are far above any live id; the list walk ends -> NULL */
    static const DWORD t[] = { 3, 0xFFFFFFF0, 0xFFFFFFFC, 0x7FFFFFFC, 0x00FFFFF0 };
    kls_cur();
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) CHECKF(UN(NtUserGetKeyboardLayout)(t[i]) == NULL, "tid %lx", t[i]);
}

static DWORD WINAPI th_idle(LPVOID p) { WaitForSingleObject((HANDLE)p, 5000); return 0; }
TEST(kbd_layouts_state_gkl_fresh_non_gui_thread_is_null) {
    KLS_NEED();
    kls_cur();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD tid = 0;
    HANDLE th = CreateThread(NULL, 0, th_idle, ev, 0, &tid);
    REQUIRE(th);
    /* the thread has made no win32k call, so it has no THREADINFO in the desktop thread list */
    CHECK(UN(NtUserGetKeyboardLayout)(tid) == NULL);
    SetEvent(ev); WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(ev);
}

struct peer { HANDLE up, go; DWORD tid; HKL hkl; HKL ci_hkl; };
static DWORD WINAPI th_peer(LPVOID p) {
    struct peer *s = p;
    s->tid = GetCurrentThreadId();
    s->hkl = kls_cur();                      /* first win32k call converts this thread */
    unsigned long long v = 0; kls_ci_u64(KLS_CI_HKL, &v); s->ci_hkl = (HKL)(ULONG_PTR)v;
    SetEvent(s->up);
    WaitForSingleObject(s->go, 8000);
    return 0;
}
TEST(kbd_layouts_state_gkl_other_gui_thread_in_process) {
    KLS_NEED();
    kls_cur();
    struct peer s = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), 0, 0, 0 };
    HANDLE th = CreateThread(NULL, 0, th_peer, &s, 0, NULL);
    REQUIRE(th);
    if (WaitForSingleObject(s.up, 5000) == WAIT_OBJECT_0) {
        HKL got = UN(NtUserGetKeyboardLayout)(s.tid);
        CHECKF(got == s.hkl, "peer %p queried %p", s.hkl, got);
        CHECK(s.hkl != NULL);
        /* new GUI threads start on the process-wide layout */
        t_note("peer layout %p, main %p, peer CI hKL %p", s.hkl, kls_cur(), s.ci_hkl);
    } else CHECK(!"peer thread did not start");
    SetEvent(s.go); WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(s.up); CloseHandle(s.go);
}

CHILD(kbd_layouts_state_peer) {
    (void)argc; (void)argv;
    HKL h = U(GetKeyboardLayout) ? U(GetKeyboardLayout)(0) : NULL;   /* loads user32, connects, makes this a GUI thread */
    printf("TID=%lu HKL=%llx\n", GetCurrentThreadId(), (unsigned long long)(ULONG_PTR)h);
    char b[8]; DWORD r;
    ReadFile(GetStdHandle(STD_INPUT_HANDLE), b, sizeof b, &r, NULL);   /* released when the parent closes its end */
    return 0;
}
TEST(kbd_layouts_state_gkl_cross_process_thread) {
    KLS_NEED();
    kls_cur();
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE inr, inw, outr, outw;
    REQUIRE(CreatePipe(&inr, &inw, &sa, 0));
    REQUIRE(CreatePipe(&outr, &outw, &sa, 0));
    SetHandleInformation(inw, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outr, HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("kbd_layouts_state_peer", "", 0, inr, outw, outw, TRUE, NULL, &pi);
    CloseHandle(inr); CloseHandle(outw);
    if (!hp) { t_skip("cannot spawn peer"); CloseHandle(inw); CloseHandle(outr); return; }
    char buf[256]; DWORD got = 0; DWORD t0 = GetTickCount(); buf[0] = 0;
    while (GetTickCount() - t0 < 8000) {
        DWORD avail = 0;
        if (!PeekNamedPipe(outr, NULL, 0, NULL, &avail, NULL)) break;
        if (avail) { ReadFile(outr, buf, sizeof buf - 1, &got, NULL); buf[got] = 0; if (strchr(buf, '\n')) break; }
        else Sleep(20);
    }
    unsigned long tid = 0; unsigned long long hk = 0;
    if (sscanf(buf, "TID=%lu HKL=%llx", &tid, &hk) == 2 && tid) {
        HKL got_h = UN(NtUserGetKeyboardLayout)(tid);
        /* the lookup walks the desktop's thread list, which includes threads of other processes */
        CHECKF(got_h == (HKL)(ULONG_PTR)hk, "peer reported %llx, queried %p", hk, got_h);
        CHECK(hk != 0);
    } else t_skip("peer did not report (%s)", buf);
    CloseHandle(inw);   /* EOF on its stdin -> it exits */
    if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(outr);
}

/* ======================================================================================== name */

TEST(kbd_layouts_state_name_user_format) {
    KLS_NEED(); REQUIRE(U(GetKeyboardLayoutNameW));
    HKL cur = kls_cur();
    if (kls_isdead_ime(cur)) { t_skip("IME layout active"); return; }
    WCHAR b[16]; for (int i = 0; i < 16; i++) b[i] = 0x7777;
    CHECK(U(GetKeyboardLayoutNameW)(b));
    /* RtlIntegerToUnicode(kl->klid, 16, 8): exactly 8 uppercase hex digits and a NUL at [8] */
    CHECK(b[8] == 0);
    for (int i = 0; i < 8; i++) CHECKF((b[i] >= '0' && b[i] <= '9') || (b[i] >= 'A' && b[i] <= 'F'), "char %d = %04x", i, b[i]);
    CHECK(b[9] == 0x7777);
}

TEST(kbd_layouts_state_name_nt_small_max_length_rejected) {
    KLS_NEED(); REQUIRE(UN(NtUserGetKeyboardLayoutName));
    if (kls_isdead_ime(kls_cur())) { t_skip("IME layout active"); return; }
    /* needs MaximumLength >= 0x12 (8 chars + NUL); otherwise SetLastError(ERROR_INVALID_PARAMETER) */
    static const USHORT ml[] = { 0, 2, 0x10, 0x11 };
    for (unsigned i = 0; i < sizeof ml / sizeof *ml; i++) {
        WCHAR b[16]; for (int j = 0; j < 16; j++) b[j] = 0x7777;
        UNICODE_STRING us = { 0, ml[i], b };
        SET_ERR(0x1234);
        int r = UN(NtUserGetKeyboardLayoutName)(&us);
        CHECKF(r == 0, "max %x returned %d", ml[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECK(b[0] == 0x7777);
    }
}

TEST(kbd_layouts_state_name_nt_accepts_exact_and_larger) {
    KLS_NEED(); REQUIRE(UN(NtUserGetKeyboardLayoutName));
    if (kls_isdead_ime(kls_cur())) { t_skip("IME layout active"); return; }
    static const USHORT ml[] = { 0x12, 0x14, 0x40 };
    WCHAR ref[16] = { 0 };
    for (unsigned i = 0; i < sizeof ml / sizeof *ml; i++) {
        WCHAR b[32]; for (int j = 0; j < 32; j++) b[j] = 0x7777;
        UNICODE_STRING us = { 0x55, ml[i], b };
        CHECK(UN(NtUserGetKeyboardLayoutName)(&us) == 1);
        CHECK(b[8] == 0);
        if (!ref[0]) memcpy(ref, b, 9 * sizeof(WCHAR)); else CHECK(!memcmp(ref, b, 9 * sizeof(WCHAR)));
        for (int j = 9; j < 32; j++) CHECK(b[j] == 0x7777);   /* nothing past the NUL is written */
        /* the kernel works on a probed copy of the descriptor: caller's Length is never updated */
        CHECK(us.Length == 0x55);
        CHECK(us.MaximumLength == ml[i]);
    }
}

TEST(kbd_layouts_state_name_matches_user_wrapper) {
    KLS_NEED(); REQUIRE(UN(NtUserGetKeyboardLayoutName)); REQUIRE(U(GetKeyboardLayoutNameW));
    if (kls_isdead_ime(kls_cur())) { t_skip("IME layout active"); return; }
    WCHAR a[16] = { 0 }, b[16] = { 0 };
    UNICODE_STRING us = { 0, 0x12, a };
    CHECK(UN(NtUserGetKeyboardLayoutName)(&us) == 1);
    CHECK(U(GetKeyboardLayoutNameW)(b));
    CHECK(!wcscmp(a, b));
}

static DWORD w_name_us(void *arg) {
    HKL us = (HKL)arg;
    HKL prev = UN(NtUserActivateKeyboardLayout)(us, 0);
    (void)prev;
    WCHAR n[16] = { 0 };
    UNICODE_STRING s = { 0, 0x12, n };
    CHECK(UN(NtUserGetKeyboardLayoutName)(&s) == 1);
    EXPECT_WSTR(n, L"00000409");   /* KLID parsed from the name it was loaded under */
    CHECK(((ULONG_PTR)kls_cur() & 0xffff) == 0x409);
    return 0;
}
TEST(kbd_layouts_state_name_us_is_00000409) {
    KLS_NEED(); REQUIRE(UN(NtUserGetKeyboardLayoutName)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("US layout not loadable"); return; }
    CHECK(((ULONG_PTR)us & 0xffff) == 0x409);
    kls_thread(w_name_us, us);
    kls_cleanup();
}

static DWORD w_name_follow(void *arg) {
    HKL o = (HKL)arg;
    WCHAR before[16] = { 0 }, after[16] = { 0 };
    UNICODE_STRING s1 = { 0, 0x12, before }, s2 = { 0, 0x12, after };
    UN(NtUserGetKeyboardLayoutName)(&s1);
    HKL prev = UN(NtUserActivateKeyboardLayout)(o, 0);
    UN(NtUserGetKeyboardLayoutName)(&s2);
    if (o != prev) CHECK(wcscmp(before, after) != 0);   /* two different layouts have different KLIDs */
    UN(NtUserActivateKeyboardLayout)(prev, 0);
    UN(NtUserGetKeyboardLayoutName)(&s2);
    CHECK(!wcscmp(before, s2.Buffer));
    return 0;
}
TEST(kbd_layouts_state_name_follows_activation) {
    KLS_NEED(); REQUIRE(UN(NtUserGetKeyboardLayoutName)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL cur = kls_cur();
    if (kls_isdead_ime(cur)) { t_skip("IME layout active"); return; }
    HKL o = kls_other(cur);
    if (!o || kls_isdead_ime(o)) { t_skip("no second non-IME layout"); return; }
    kls_thread(w_name_follow, o);
    kls_cleanup();
}

/* ======================================================================================== load */

TEST(kbd_layouts_state_load_us_langid) {
    KLS_NEED();
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    CHECK(((ULONG_PTR)us & 0xffff) == 0x409);
    CHECK(kls_in_list(us));
    kls_cleanup();
}

TEST(kbd_layouts_state_load_idempotent_same_hkl) {
    KLS_NEED();
    HKL a = kls_us();
    if (!a) { t_skip("00000409 not loadable"); return; }
    HKL b = U(LoadKeyboardLayoutW)(L"00000409", 0);
    /* LoadKeyboardLayout finds the ring entry with the same HKL and returns its handle */
    CHECK(a == b);
    int n1 = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    HKL c = U(LoadKeyboardLayoutW)(L"00000409", 0);
    CHECK(c == a);
    CHECK(UN(NtUserGetKeyboardLayoutList)(0, NULL) == n1);
    kls_cleanup();
}

TEST(kbd_layouts_state_load_flag_variants_return_same_hkl) {
    KLS_NEED();
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    /* KLF_SUBSTITUTE_OK and KLF_NOTELLSHELL only change bookkeeping (shell notification, substitution), never the handle */
    static const UINT f[] = { KLS_KLF_SUBSTITUTE_OK, KLS_KLF_NOTELLSHELL, KLS_KLF_SUBSTITUTE_OK | KLS_KLF_NOTELLSHELL };
    int n = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) {
        HKL h = U(LoadKeyboardLayoutW)(L"00000409", f[i]);
        CHECKF(h == us, "flags %x -> %p want %p", f[i], h, us);
    }
    CHECK(UN(NtUserGetKeyboardLayoutList)(0, NULL) == n);
    /* an over-long KLID is truncated to 8 characters by the worker's StringCchCopy */
    HKL t = U(LoadKeyboardLayoutW)(L"000004090", 0);
    t_note("9-char KLID loads as %p (US is %p)", t, us);
    kls_cleanup();
}

static DWORD w_load_unknown(void *arg) {
    static const WCHAR *const names[] = { L"ZZZZZZZZ", L"12345678", L"" };
    HKL before[KLS_MAXL], after[KLS_MAXL];
    int nb = kls_list(before, KLS_MAXL);
    for (unsigned i = 0; i < 3; i++) {
        HKL h = U(LoadKeyboardLayoutW)(names[i], 0);
        int na = kls_list(after, KLS_MAXL), in = 0;
        for (int j = 0; j < na; j++) if (after[j] == h) in = 1;
        /* An unregistered KLID does not fail on Windows 10: the call falls back to a layout that is already loaded, so the
         * handle is on the list and the list neither grows nor changes. */
        CHECKF(h != NULL, "KLID '%ls' returned NULL", names[i]);
        CHECKF(in, "KLID '%ls' returned %p, not on the layout list", names[i], (void *)h);
        CHECKF(na == nb && !memcmp(before, after, nb * sizeof(HKL)), "KLID '%ls' changed the layout list (%d -> %d)", names[i], nb, na);
    }
    return 0;
}
TEST(kbd_layouts_state_load_unknown_klid_falls_back_to_loaded_layout) {
    KLS_NEED();
    kls_thread(w_load_unknown, NULL);
    kls_cleanup();
}

static DWORD w_load_noact(void *arg) {
    HKL o = (HKL)arg;
    HKL before = kls_cur();
    (void)o;
    CHECK(kls_cur() == before);   /* loading never activates unless KLF_ACTIVATE */
    return 0;
}
TEST(kbd_layouts_state_load_without_activate_keeps_thread_layout) {
    KLS_NEED();
    HKL before = kls_cur();
    HKL o = kls_other(before);
    if (!o) { t_skip("no second layout"); return; }
    HKL again = U(LoadKeyboardLayoutW)(L"00000409", 0);
    (void)again;
    CHECK(kls_cur() == before);
    unsigned long long ci = 0;
    if (kls_ci_u64(KLS_CI_HKL, &ci)) t_note("CI hKL %llx", ci);
    kls_thread(w_load_noact, o);
    kls_cleanup();
}

static DWORD w_load_activate(void *arg) {
    (void)arg;
    HKL before = kls_cur();
    HKL us = U(LoadKeyboardLayoutW)(L"00000409", KLS_KLF_ACTIVATE);
    if (!us) return 0;
    if (!kls_in_list(us)) return 0;
    /* LoadKeyboardLayout: (flags & 1) -> layout activation for the calling thread */
    CHECK(kls_cur() == us);
    unsigned long long ci = 0;
    if (kls_ci_u64(KLS_CI_HKL, &ci)) CHECK((HKL)(ULONG_PTR)ci == us);
    if (before != us) UN(NtUserActivateKeyboardLayout)(before, 0);
    return 0;
}
TEST(kbd_layouts_state_load_klf_activate_activates_calling_thread) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL main_before = kls_cur();
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_load_activate, NULL);
    CHECK(kls_cur() == main_before);   /* other threads are not affected */
    kls_cleanup();
}

TEST(kbd_layouts_state_loadex_invalid_flag_bits) {
    KLS_NEED(); REQUIRE(UN(NtUserLoadKeyboardLayoutEx));
    if (t_is_wine()) { t_skip("different NtUserLoadKeyboardLayoutEx signature under Wine"); return; }
    /* NtUserLoadKeyboardLayoutEx rejects (flags & 0x1f00fc60) with ERROR_INVALID_FLAGS before it
     * reads anything but the descriptor; all other arguments here are inert. */
    static const UINT bad[] = { 0x20, 0x40, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x1000000,
                                0x2000000, 0x4000000, 0x8000000, 0x10000000, 0x1f00fc60, 0x21, 0x409 };
    WCHAR nm[10] = L"00000409";
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        UNICODE_STRING us = { 16, 18, nm };
        SET_ERR(0x1234);
        HKL r = UN(NtUserLoadKeyboardLayoutEx)(NULL, 0, 0, NULL, NULL, &us, 0x04090409, bad[i]);
        CHECKF(r == NULL, "flags %x -> %p", bad[i], r);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
}

TEST(kbd_layouts_state_loadex_null_file_returns_null) {
    KLS_NEED(); REQUIRE(UN(NtUserLoadKeyboardLayoutEx));
    if (t_is_wine()) { t_skip("different NtUserLoadKeyboardLayoutEx signature under Wine"); return; }
    /* a NULL locale handle fails validation -> NULL; the last error is
     * reset to 0 unless flags & 0x40000200 (KLF_RESET | KLF_INITTIME) which return silently. */
    WCHAR nm[10] = L"00000409";
    static const UINT ok[] = { 0, 1, 2, 3, 8, 0x80, 0x100, 0x10000, 0x40000 };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        UNICODE_STRING us = { 16, 18, nm };
        SET_ERR(0x1234);
        HKL r = UN(NtUserLoadKeyboardLayoutEx)(NULL, 0, 0, NULL, NULL, &us, 0x04090409, ok[i]);
        CHECKF(r == NULL, "flags %x -> %p", ok[i], r);
        EXPECT_ERR(0);
    }
    UNICODE_STRING us = { 16, 18, nm };
    SET_ERR(0x1234);
    CHECK(UN(NtUserLoadKeyboardLayoutEx)(NULL, 0, 0, NULL, NULL, &us, 0x04090409, KLS_KLF_INITTIME) == NULL);
    EXPECT_ERR(0x1234);   /* flag 0x200 returns before SetLastError */
}

TEST(kbd_layouts_state_loadex_bogus_file_handle_returns_null) {
    KLS_NEED(); REQUIRE(UN(NtUserLoadKeyboardLayoutEx));
    if (t_is_wine()) { t_skip("different NtUserLoadKeyboardLayoutEx signature under Wine"); return; }
    WCHAR nm[10] = L"00000409";
    UNICODE_STRING us = { 16, 18, nm };
    SET_ERR(0x1234);
    CHECK(UN(NtUserLoadKeyboardLayoutEx)((HANDLE)(ULONG_PTR)0x7ffffff0, 0, 0, NULL, NULL, &us, 0x04090409, 0) == NULL);
    EXPECT_ERR(0);
}

/* ====================================================================================== unload */

TEST(kbd_layouts_state_unload_bogus_hkl_fails) {
    KLS_NEED(); REQUIRE(U(UnloadKeyboardLayout));
    if (t_is_wine()) { t_skip("win32u.NtUserUnloadKeyboardLayout is an aborting stub under Wine"); return; }
    /* High word non-zero and 0/1 excluded: HKLtoPKL does an exact compare and finds nothing. Never
     * pass 0, 1 or a bare LANGID here: those are PREV/NEXT/by-language selectors on real entries. */
    static const ULONG_PTR h[] = { 0x12345678, 0xDEAD0409, 0x7f7f7f7f, 0x0ABC0ABC };
    int n = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    for (unsigned i = 0; i < sizeof h / sizeof *h; i++) {
        if (kls_in_list((HKL)h[i])) continue;
        CHECK(!U(UnloadKeyboardLayout)((HKL)h[i]));
        CHECK(!UN(NtUserUnloadKeyboardLayout)((HKL)h[i]));
    }
    CHECK(UN(NtUserGetKeyboardLayoutList)(0, NULL) == n);
}

static DWORD w_unload_removes(void *arg) {
    HKL o = (HKL)arg;
    int n0 = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    CHECK(kls_in_list(o));
    CHECK(UN(NtUserUnloadKeyboardLayout)(o));
    kls_nadded--;
    CHECK(!kls_in_list(o));
    CHECK(UN(NtUserGetKeyboardLayoutList)(0, NULL) == n0 - 1);
    return 0;
}
TEST(kbd_layouts_state_unload_added_layout_removes_it) {
    KLS_NEED(); REQUIRE(UN(NtUserUnloadKeyboardLayout));
    if (t_is_wine()) { t_skip("win32u.NtUserUnloadKeyboardLayout is an aborting stub under Wine"); return; }
    HKL o = kls_other(kls_cur());
    if (!o || !kls_was_added(o)) { t_skip("could not add a private layout (won't unload user layouts)"); kls_cleanup(); return; }
    kls_thread(w_unload_removes, o);
    kls_cleanup();
}

static DWORD w_unload_active(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    UN(NtUserActivateKeyboardLayout)(o, 0);
    CHECK(kls_cur() == o);
    /* layout unload: when the thread's active layout is the one being removed it
     * is re-activated onto HKLtoPKL(pti, HKL_NEXT) (the unloaded entry is already flagged hidden) */
    CHECK(UN(NtUserUnloadKeyboardLayout)(o));
    kls_nadded--;
    HKL now = kls_cur();
    CHECKF(now != o, "thread still on unloaded layout %p", o);
    CHECK(now != NULL);
    unsigned long long ci = 0;
    if (kls_ci_u64(KLS_CI_HKL, &ci)) CHECK((HKL)(ULONG_PTR)ci == now);
    if (now != orig) UN(NtUserActivateKeyboardLayout)(orig, 0);
    return 0;
}
TEST(kbd_layouts_state_unload_active_layout_moves_thread_on) {
    KLS_NEED(); REQUIRE(UN(NtUserUnloadKeyboardLayout)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    if (t_is_wine()) { t_skip("win32u.NtUserUnloadKeyboardLayout is an aborting stub under Wine"); return; }
    HKL o = kls_other(kls_cur());
    if (!o || !kls_was_added(o)) { t_skip("could not add a private layout"); kls_cleanup(); return; }
    kls_thread(w_unload_active, o);
    kls_cleanup();
}

/* ==================================================================================== activate */

struct act { HKL o; HKL us; };

static DWORD w_act_same(void *arg) {
    (void)arg;
    HKL cur = kls_cur();
    /* same layout and no KLF_SETFORPROCESS -> returns the HKL untouched */
    CHECK(UN(NtUserActivateKeyboardLayout)(cur, 0) == cur);
    CHECK(kls_cur() == cur);
    CHECK(U(ActivateKeyboardLayout)(cur, 0) == cur);
    return 0;
}
TEST(kbd_layouts_state_activate_same_returns_same) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    kls_thread(w_act_same, NULL);
}

static DWORD w_act_other(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    int n = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    HKL prev = UN(NtUserActivateKeyboardLayout)(o, 0);
    CHECK(prev == orig);
    CHECK(kls_cur() == o);
    CHECK(UN(NtUserGetKeyboardLayoutList)(0, NULL) == n);   /* activation does not touch the ring */
    HKL back = UN(NtUserActivateKeyboardLayout)(orig, 0);
    CHECK(back == o);
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_activate_other_switches_and_returns_previous) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL o = kls_other(kls_cur());
    if (!o) { t_skip("no second layout"); return; }
    kls_thread(w_act_other, o);
    kls_cleanup();
}

static DWORD w_act_ci(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    unsigned long long a = 0, b = 0, cp1 = 0, cp2 = 0;
    CHECK(kls_ci_u64(KLS_CI_HKL, &a));
    UN(NtUserActivateKeyboardLayout)(o, 0);
    kls_ci_u64(KLS_CI_CODEPAGE, &cp1);
    CHECK(kls_ci_u64(KLS_CI_HKL, &b));
    /* both stores happen together in the normal (non-cleanup) branch of layout activation */
    CHECKF((HKL)(ULONG_PTR)b == o, "CI hKL %llx want %p", b, o);
    UN(NtUserActivateKeyboardLayout)(orig, 0);
    kls_ci_u64(KLS_CI_HKL, &a);
    kls_ci_u64(KLS_CI_CODEPAGE, &cp2);
    CHECK((HKL)(ULONG_PTR)a == orig);
    t_note("codepage word: other %llx, original %llx", cp1 & 0xffff, cp2 & 0xffff);
    return 0;
}
TEST(kbd_layouts_state_activate_updates_client_info_hkl) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL o = kls_other(kls_cur());
    if (!o) { t_skip("no second layout"); return; }
    if (t_is_wine()) { t_skip("CLIENTINFO layout is Windows specific"); return; }
    kls_thread(w_act_ci, o);
    kls_cleanup();
}

static DWORD w_act_langid(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    /* HKLtoPKL with a zero high word matches the first ring entry whose LANGID equals the low word */
    WORD lang = (WORD)((ULONG_PTR)o & 0xffff);
    UN(NtUserActivateKeyboardLayout)((HKL)(ULONG_PTR)lang, 0);
    CHECK(((ULONG_PTR)kls_cur() & 0xffff) == lang);
    UN(NtUserActivateKeyboardLayout)(orig, 0);
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_activate_by_langid_only) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL o = kls_other(kls_cur());
    if (!o) { t_skip("no second layout"); return; }
    kls_thread(w_act_langid, o);
    kls_cleanup();
}

static DWORD w_act_cycle(void *arg) {
    (void)arg;
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    HKL orig = kls_cur();
    int idx = -1; for (int i = 0; i < n; i++) if (l[i] == orig) idx = i;
    if (idx < 0 || n < 2) { t_note("cannot cycle: n=%d idx=%d", n, idx); return 0; }
    /* HKL_NEXT follows the list direction returned by GetKeyboardLayoutList; HKL_PREV the reverse */
    HKL prev = UN(NtUserActivateKeyboardLayout)(KLS_HKL_NEXT, 0);
    CHECK(prev == orig);
    CHECKF(kls_cur() == l[(idx + 1) % n], "next of %d is %p want %p", idx, kls_cur(), l[(idx + 1) % n]);
    prev = UN(NtUserActivateKeyboardLayout)(KLS_HKL_PREV, 0);
    CHECK(kls_cur() == orig);
    CHECK(prev == l[(idx + 1) % n]);
    prev = UN(NtUserActivateKeyboardLayout)(KLS_HKL_PREV, 0);
    CHECKF(kls_cur() == l[(idx + n - 1) % n], "prev of %d", idx);
    UN(NtUserActivateKeyboardLayout)(orig, 0);
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_activate_next_prev_walk_the_ring) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    kls_other(kls_cur());
    kls_thread(w_act_cycle, NULL);
    kls_cleanup();
}

static DWORD w_act_bogus(void *arg) {
    (void)arg;
    HKL orig = kls_cur();
    static const ULONG_PTR h[] = { 0x12345678, 0xDEAD0409, 0x7f7f7f7f };
    for (unsigned i = 0; i < 3; i++) {
        if (kls_in_list((HKL)h[i])) continue;
        SET_ERR(0);
        HKL r = UN(NtUserActivateKeyboardLayout)((HKL)h[i], 0);
        CHECKF(r == NULL, "bogus %llx -> %p", (unsigned long long)h[i], r);   /* HKL lookup fails -> 0 */
        CHECK(kls_cur() == orig);
    }
    return 0;
}
TEST(kbd_layouts_state_activate_bogus_hkl_returns_null) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    kls_thread(w_act_bogus, NULL);
}

struct iso { HKL o; HANDLE a_done, b_ready, b_check; HKL b_seen, b_before; };
static DWORD WINAPI th_iso_b(LPVOID p) {
    struct iso *s = p;
    s->b_before = kls_cur();
    SetEvent(s->b_ready);
    WaitForSingleObject(s->a_done, 8000);
    s->b_seen = kls_cur();
    SetEvent(s->b_check);
    return 0;
}
static DWORD w_act_iso(void *arg) {
    struct iso *s = arg;
    HANDLE hb = CreateThread(NULL, 0, th_iso_b, s, 0, NULL);
    if (!hb) return 0;
    WaitForSingleObject(s->b_ready, 5000);
    HKL orig = kls_cur();
    UN(NtUserActivateKeyboardLayout)(s->o, 0);
    CHECK(kls_cur() == s->o);
    SetEvent(s->a_done);
    WaitForSingleObject(s->b_check, 5000);
    UN(NtUserActivateKeyboardLayout)(orig, 0);
    WaitForSingleObject(hb, 5000); CloseHandle(hb);
    return 0;
}
TEST(kbd_layouts_state_activate_affects_only_calling_thread) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL main0 = kls_cur();
    HKL o = kls_other(main0);
    if (!o) { t_skip("no second layout"); return; }
    struct iso s = { o, CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), 0, 0 };
    kls_thread(w_act_iso, &s);
    /* without KLF_SETFORPROCESS only the calling thread's active layout is assigned */
    CHECKF(s.b_seen == s.b_before, "sibling thread moved from %p to %p", s.b_before, s.b_seen);
    CHECK(kls_cur() == main0);
    CloseHandle(s.a_done); CloseHandle(s.b_ready); CloseHandle(s.b_check);
    kls_cleanup();
}

static DWORD w_act_reorder(void *arg) {
    (void)arg;
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    if (n < 2) return 0;
    HKL orig = kls_cur();
    HKL target = l[n - 1];
    UN(NtUserActivateKeyboardLayout)(target, KLS_KLF_REORDER);
    HKL m[KLS_MAXL]; int n2 = kls_list(m, KLS_MAXL);
    CHECK(n2 == n);
    /* ReorderKeyboardLayouts moves the entry to the head of the ring and keeps everyone else's order */
    CHECKF(m[0] == target, "head is %p want %p", m[0], target);
    for (int i = 1; i < n; i++) CHECKF(m[i] == l[i - 1], "slot %d", i);
    /* restore the user's ordering: re-promote from the back to the front */
    for (int i = n - 1; i >= 0; i--) UN(NtUserActivateKeyboardLayout)(l[i], KLS_KLF_REORDER);
    kls_list(m, KLS_MAXL);
    for (int i = 0; i < n; i++) CHECKF(m[i] == l[i], "restored slot %d", i);
    UN(NtUserActivateKeyboardLayout)(orig, 0);
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_activate_reorder_moves_to_head) {
    KLS_NEED(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    if (t_is_wine()) { t_skip("ring ordering is win32k specific"); return; }
    kls_other(kls_cur());
    kls_thread(w_act_reorder, NULL);
    kls_cleanup();
}

static DWORD w_act_user(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    HKL r = U(ActivateKeyboardLayout)(o, 0);
    CHECK(r == orig);
    CHECK(kls_cur() == o);
    r = U(ActivateKeyboardLayout)(orig, 0);
    CHECK(r == o);
    return 0;
}
TEST(kbd_layouts_state_activate_user_wrapper_matches_nt) {
    KLS_NEED();
    HKL o = kls_other(kls_cur());
    if (!o) { t_skip("no second layout"); return; }
    kls_thread(w_act_user, o);
    kls_cleanup();
}

/* KLF_SETFORPROCESS is process-wide and therefore lives in a child process. */
static int cfail;
#define CC(c) do { if (!(c)) { cfail++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
struct sfp { HANDLE up, go, seen; HKL before, after, ci_after; };
static DWORD WINAPI th_sfp(LPVOID p) {
    struct sfp *s = p;
    s->before = kls_cur();
    SetEvent(s->up);
    WaitForSingleObject(s->go, 8000);
    s->after = kls_cur();
    unsigned long long v = 0; kls_ci_u64(KLS_CI_HKL, &v); s->ci_after = (HKL)(ULONG_PTR)v;
    SetEvent(s->seen);
    return 0;
}
CHILD(kbd_layouts_state_sfp) {
    (void)argc; (void)argv;
    if (!kls_have_core()) { printf("  NOTE exports missing\n"); return 0; }
    HKL orig = U(GetKeyboardLayout)(0);
    HKL o = kls_other(orig);
    if (!o) { printf("  NOTE no second layout\n"); return 0; }
    struct sfp s = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), 0, 0, 0 };
    HANDLE th = CreateThread(NULL, 0, th_sfp, &s, 0, NULL);
    WaitForSingleObject(s.up, 5000);
    HKL prev = UN(NtUserActivateKeyboardLayout)(o, KLS_KLF_SETFORPROCESS);
    CC(prev == orig);
    /* layout activation walks the process's threads and assigns the layout (and CI hKL/CodePage) to every GUI thread */
    CC(UN(NtUserGetKeyboardLayout)(0) == o);
    SetEvent(s.go);
    WaitForSingleObject(s.seen, 5000);
    CC(s.after == o);
    CC(s.ci_after == o);
    prev = UN(NtUserActivateKeyboardLayout)(orig, KLS_KLF_SETFORPROCESS);
    CC(UN(NtUserGetKeyboardLayout)(0) == orig);
    WaitForSingleObject(th, 5000);
    kls_cleanup();
    return cfail;
}
TEST(kbd_layouts_state_activate_setforprocess_reaches_other_threads) {
    KLS_NEED();
    if (t_is_wine()) { t_skip("process-wide layout semantics are win32k specific"); return; }
    struct k_child_result r = k_child_run("kbd_layouts_state_sfp", "", 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

/* ================================================================== SetThreadLayoutHandles */

static DWORD w_sth_switch(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    unsigned long long ci0 = 0, ci1 = 0;
    kls_ci_u64(KLS_CI_HKL, &ci0);
    CHECK(UN(NtUserSetThreadLayoutHandles)(o, orig));
    CHECK(kls_cur() == o);
    kls_ci_u64(KLS_CI_HKL, &ci1);
    /* NtUserSetThreadLayoutHandles changes the thread layout only; _CLIENTINFO keeps the old hKL */
    CHECKF(ci0 == ci1, "CI hKL changed %llx -> %llx", ci0, ci1);
    CHECK(UN(NtUserSetThreadLayoutHandles)(orig, o));
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_sth_switches_without_client_info_update) {
    KLS_NEED(); REQUIRE(UN(NtUserSetThreadLayoutHandles));
    if (t_is_wine()) { t_skip("win32u.NtUserSetThreadLayoutHandles is an aborting stub under Wine"); return; }
    HKL o = kls_other(kls_cur());
    if (!o) { t_skip("no second layout"); return; }
    if (t_is_wine()) { t_skip("CLIENTINFO layout is Windows specific"); return; }
    kls_thread(w_sth_switch, o);
    kls_cleanup();
}

static DWORD w_sth_wrongold(void *arg) {
    HKL o = (HKL)arg;
    HKL orig = kls_cur();
    /* the second argument must equal the thread's current HKL when the thread already has a layout */
    CHECK(!UN(NtUserSetThreadLayoutHandles)(o, o));
    CHECK(kls_cur() == orig);
    CHECK(!UN(NtUserSetThreadLayoutHandles)(orig, (HKL)(ULONG_PTR)0x12345678));
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_sth_wrong_old_handle_rejected) {
    KLS_NEED(); REQUIRE(UN(NtUserSetThreadLayoutHandles));
    if (t_is_wine()) { t_skip("win32u.NtUserSetThreadLayoutHandles is an aborting stub under Wine"); return; }
    HKL o = kls_other(kls_cur());
    if (!o) { t_skip("no second layout"); return; }
    kls_thread(w_sth_wrongold, o);
    kls_cleanup();
}

static DWORD w_sth_unknown(void *arg) {
    (void)arg;
    HKL orig = kls_cur();
    CHECK(!UN(NtUserSetThreadLayoutHandles)((HKL)(ULONG_PTR)0x12345678, orig));   /* HKL lookup fails */
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_sth_unknown_new_handle_rejected) {
    KLS_NEED(); REQUIRE(UN(NtUserSetThreadLayoutHandles));
    if (t_is_wine()) { t_skip("win32u.NtUserSetThreadLayoutHandles is an aborting stub under Wine"); return; }
    kls_thread(w_sth_unknown, NULL);
}

static DWORD w_sth_same(void *arg) {
    (void)arg;
    HKL orig = kls_cur();
    CHECK(UN(NtUserSetThreadLayoutHandles)(orig, orig));
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_sth_same_layout_ok) {
    KLS_NEED(); REQUIRE(UN(NtUserSetThreadLayoutHandles));
    if (t_is_wine()) { t_skip("win32u.NtUserSetThreadLayoutHandles is an aborting stub under Wine"); return; }
    kls_thread(w_sth_same, NULL);
}

static DWORD w_sth_next(void *arg) {
    (void)arg;
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    HKL orig = kls_cur(); int idx = -1;
    for (int i = 0; i < n; i++) if (l[i] == orig) idx = i;
    if (idx < 0 || n < 2) return 0;
    /* the new handle goes through HKLtoPKL, so the NEXT/PREV selectors work here too */
    CHECK(UN(NtUserSetThreadLayoutHandles)(KLS_HKL_NEXT, orig));
    CHECK(kls_cur() == l[(idx + 1) % n]);
    HKL nxt = kls_cur();
    CHECK(UN(NtUserSetThreadLayoutHandles)(KLS_HKL_PREV, nxt));
    CHECK(kls_cur() == orig);
    return 0;
}
TEST(kbd_layouts_state_sth_next_prev_selectors) {
    KLS_NEED(); REQUIRE(UN(NtUserSetThreadLayoutHandles));
    if (t_is_wine()) { t_skip("win32u.NtUserSetThreadLayoutHandles is an aborting stub under Wine"); return; }
    kls_other(kls_cur());
    kls_thread(w_sth_next, NULL);
    kls_cleanup();
}

/* ============================================================================= pristine child */

CHILD(kbd_layouts_state_pristine) {
    (void)argc; (void)argv;
    /* no user32 yet: the first win32k syscall converts the thread */
    unsigned long long t0 = 0, t1 = 0;
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &t0, 8);
    CC(t0 == 0);
    if (!UN(NtUserGetKeyboardLayoutList)) return 0;
    kls_install_stub_callbacks();
    int n = UN(NtUserGetKeyboardLayoutList)(0, NULL);
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &t1, 8);
    CC(t1 != 0);
    printf("  NOTE pristine process: layout count %d, thread layout %p\n", n, UN(NtUserGetKeyboardLayout)(0));
    unsigned long long ci = 0; kls_ci_u64(KLS_CI_HKL, &ci);
    printf("  NOTE pristine CI hKL %llx\n", ci);
    return cfail;
}
TEST(kbd_layouts_state_pristine_first_call_converts_thread) {
    KLS_NEED();
    if (t_is_wine()) { t_skip("Win32ThreadInfo conversion is win32k specific"); return; }
    struct k_child_result r = k_child_run("kbd_layouts_state_pristine", "", 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}
