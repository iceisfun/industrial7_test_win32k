/* r2_wnd_cls_heap_fields: tagTHREADINFO / tagPROCESSINFO state as far as it is visible from user mode:
 * TEB.Win32ThreadInfo, TEB.Win32ClientInfo (the CLIENTINFO the kernel keeps in sync with the THREADINFO)
 * and the CLIENTTHREADINFO it points to. Every offset is one 19041 user32 reads (see r2h.h). */
#define R2H_TAG L"ci"
#include "r2h.h"

#define QS_POSTMSG 0x0008
#define QS_SENDMSG 0x0040

/* ---- Win32ThreadInfo -------------------------------------------------------------------------- */
TEST(r2_wnd_cls_heap_fields_ci_pti_nonzero_and_unreadable) {
    NEED_GUI();
    uint64_t pti = pti_now();
    CHECK(pti != 0);
    /* Win10 22H2 keeps a small token here, not an address: all that is checked is that user mode cannot read through it */
    unsigned char b[8];
    CHECK(!k_try_read((void *)(ULONG_PTR)pti, b, sizeof b));   /* user mode cannot read it */
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_pti_stable_across_calls) {
    NEED_GUI();
    uint64_t a = pti_now();
    HWND h = mk_plain();
    U(GetForegroundWindow)();
    if (h) U(ShowWindow)(h, SW_SHOWNA);
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    EXPECT_EQ(pti_now(), a);
    END();
}
struct pti_thr { uint64_t pti, deskinfo, delta; HANDLE ev; HANDLE go; };
static DWORD WINAPI pti_thread(void *p) {
    struct pti_thr *t = p;
    U(GetDesktopWindow)();
    t->pti = pti_now(); t->deskinfo = deskinfo_now(); t->delta = delta_now();
    SetEvent(t->ev);
    WaitForSingleObject(t->go, 5000);
    return 0;
}
TEST(r2_wnd_cls_heap_fields_ci_pti_distinct_per_thread_desktopinfo_shared) {
    NEED_GUI();
    struct pti_thr t = { 0 };
    t.ev = CreateEventW(NULL, TRUE, FALSE, NULL); t.go = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, pti_thread, &t, 0, NULL);
    CHECK(WaitForSingleObject(t.ev, 5000) == WAIT_OBJECT_0);
    CHECK(t.pti != 0);
    CHECK(t.pti != pti_now());                        /* one THREADINFO per thread */
    EXPECT_EQ(t.deskinfo, deskinfo_now());            /* threads of one desktop share the DESKTOPINFO */
    EXPECT_EQ(t.delta, delta_now());                  /* and the same heap delta */
    SetEvent(t.go);
    WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(t.ev); CloseHandle(t.go);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_window_owner_is_pti_of_thread) {
    NEED_GUI();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ(w.e.owner, pti_now());
    EXPECT_EQ(teb_q(TEBO_PTI), pti_now());
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_deskinfo_in_user_range_and_readable) {
    NEED_GUI();
    uint64_t di = deskinfo_now();
    CHECK(di != 0 && di < 0x800000000000ull);
    unsigned char b[0x30];
    CHECK(k_try_read((void *)(ULONG_PTR)di, b, sizeof b));
    uint64_t d = delta_now();
    CHECK(d != 0);
    END();
}

/* ---- ExpWinVer and compatibility flags ----------------------------------------------------------- */
/* Expected Windows version: MajorSubsystemVersion << 8 | (MinorSubsystemVersion & 0xff); a major of 1 means 3.10 */
static uint32_t expwinver_of_image(void) {
    unsigned char *base = *(unsigned char **)((char *)k_peb() + 0x10);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    unsigned maj = nt->OptionalHeader.MajorSubsystemVersion, min = nt->OptionalHeader.MinorSubsystemVersion;
    if (maj == 1) { maj = 3; min = 10; }
    return (maj << 8) | (min & 0xff);
}
TEST(r2_wnd_cls_heap_fields_ci_expwinver_matches_image_subsystem_version) {
    NEED_GUI();
    uint32_t want = expwinver_of_image(), got = teb_d(CI_EXPWINVER);
    CHECK(got != 0);
    CHECKF(got == want, "ClientInfo.dwExpWinVer 0x%lx, image subsystem version gives 0x%lx", (unsigned long)got, (unsigned long)want);
    END();
}
UDECL(DWORD, GetAppCompatFlags, (HANDLE));
UDECL(DWORD, GetAppCompatFlags2, (WORD));
TEST(r2_wnd_cls_heap_fields_ci_appcompat_flags_match_raw) {
    NEED_GUI();
    REQUIRE(UN(GetAppCompatFlags));
    EXPECT_EQ(UN(GetAppCompatFlags)(NULL), teb_d(CI_COMPAT1));
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_appcompat_flags2_gated_by_expwinver) {
    NEED_GUI();
    REQUIRE(UN(GetAppCompatFlags2));
    uint32_t ev = teb_d(CI_EXPWINVER);
    /* GetAppCompatFlags2(v) is 0 when v < dwExpWinVer, else ClientInfo.dwCompatFlags2 */
    EXPECT_EQ(UN(GetAppCompatFlags2)(0xffff), teb_d(CI_COMPAT2));
    EXPECT_EQ(UN(GetAppCompatFlags2)((WORD)ev), teb_d(CI_COMPAT2));
    if (ev > 0) EXPECT_EQ(UN(GetAppCompatFlags2)((WORD)(ev - 1)), 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_tiflags_note) {
    NEED_GUI();
    uint32_t f = teb_d(CI_TIFLAGS);
    t_note("ClientInfo.dwTIFlags = 0x%08lx, CI_flags = 0x%llx, hook state phkCurrent=%llx", (unsigned long)f, (unsigned long long)teb_q(CI_FLAGS), (unsigned long long)teb_q(CI_PHKCUR));
    if (f & 0x2000000) t_note("dwTIFlags bit 0x2000000 set outside any message (meaning not proven from analysis)");
    EXPECT_EQ(teb_q(CI_PHKCUR), 0);                   /* no hook procedure running */
    END();
}

/* ---- hooks: ClientInfo.fsHooks, one bit per hook id, bit = 1 << (id + 1) ------------------------- */
static LRESULT CALLBACK nop_hook(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }

static void fshook_case(int id) {
    NEED_GUI();
    uint32_t bit = 1u << (id + 1);     /* SendMessageWorker's 0x2020 is hooks 4 and 12 */
    uint32_t before = teb_d(CI_FSHOOKS), di_before = read_d(deskinfo_now() + DI_FSHOOKS);
    EXPECT_EQ(before & ~0xffffu, 0);
    HHOOK hk = U(SetWindowsHookExW)(id, nop_hook, NULL, GetCurrentThreadId());
    if (!hk) { DWORD e = GetLastError(); t_skip("SetWindowsHookEx(%d) failed: %lu", id, e); return; }
    uint32_t mid = teb_d(CI_FSHOOKS);
    CHECKF((mid & bit) == bit, "fsHooks 0x%lx lacks 0x%lx", (unsigned long)mid, (unsigned long)bit);
    EXPECT_EQ(mid & ~bit, before & ~bit);              /* nothing else changed */
    uint32_t di_mid = read_d(deskinfo_now() + DI_FSHOOKS);
    if (di_mid != di_before) t_note("thread hook %d also changed DESKTOPINFO.fsHooks 0x%lx -> 0x%lx", id, (unsigned long)di_before, (unsigned long)di_mid);
    CHECK(U(UnhookWindowsHookEx)(hk));
    uint32_t after = teb_d(CI_FSHOOKS);
    EXPECT_EQ(after & bit, before & bit);
    EXPECT_EQ(after, before);
    END();
}
#define FH(name, id) TEST(r2_wnd_cls_heap_fields_ci_fshooks_##name) { fshook_case(id); }
FH(msgfilter, -1)
FH(keyboard, 2)
FH(getmessage, 3)
FH(callwndproc, 4)
FH(cbt, 5)
FH(mouse, 7)
FH(foregroundidle, 11)
FH(callwndprocret, 12)

TEST(r2_wnd_cls_heap_fields_ci_fshooks_two_hooks_same_type_keep_bit) {
    NEED_GUI();
    uint32_t before = teb_d(CI_FSHOOKS);
    HHOOK a = U(SetWindowsHookExW)(WH_GETMESSAGE, nop_hook, NULL, GetCurrentThreadId());
    HHOOK b = U(SetWindowsHookExW)(WH_GETMESSAGE, nop_hook, NULL, GetCurrentThreadId());
    if (!a || !b) { if (a) U(UnhookWindowsHookEx)(a); if (b) U(UnhookWindowsHookEx)(b); t_skip("hook install failed"); return; }
    uint32_t bit = 1u << (WH_GETMESSAGE + 1);
    CHECK(teb_d(CI_FSHOOKS) & bit);
    CHECK(U(UnhookWindowsHookEx)(a));
    CHECK(teb_d(CI_FSHOOKS) & bit);                    /* the second hook is still installed */
    CHECK(U(UnhookWindowsHookEx)(b));
    EXPECT_EQ(teb_d(CI_FSHOOKS), before);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_fshooks_independent_bits) {
    NEED_GUI();
    uint32_t before = teb_d(CI_FSHOOKS);
    HHOOK a = U(SetWindowsHookExW)(WH_KEYBOARD, nop_hook, NULL, GetCurrentThreadId());
    HHOOK b = U(SetWindowsHookExW)(WH_MOUSE, nop_hook, NULL, GetCurrentThreadId());
    if (!a || !b) { if (a) U(UnhookWindowsHookEx)(a); if (b) U(UnhookWindowsHookEx)(b); t_skip("hook install failed"); return; }
    uint32_t ka = 1u << (WH_KEYBOARD + 1), kb = 1u << (WH_MOUSE + 1);
    EXPECT_EQ(teb_d(CI_FSHOOKS) & (ka | kb), ka | kb);
    U(UnhookWindowsHookEx)(a);
    EXPECT_EQ(teb_d(CI_FSHOOKS) & (ka | kb), kb);
    U(UnhookWindowsHookEx)(b);
    EXPECT_EQ(teb_d(CI_FSHOOKS), before);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_fshooks_unhook_invalid_handle) {
    NEED_GUI();
    uint32_t before = teb_d(CI_FSHOOKS);
    SetLastError(0);
    CHECK(!U(UnhookWindowsHookEx)((HHOOK)(ULONG_PTR)0x1234abcd));
    EXPECT_ERR(ERROR_INVALID_HOOK_HANDLE);
    EXPECT_EQ(teb_d(CI_FSHOOKS), before);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_deskinfo_fshooks_valid_mask) {
    NEED_GUI();
    uint32_t f = read_d(deskinfo_now() + DI_FSHOOKS);
    EXPECT_EQ(f & ~0xffffu, 0);                        /* bits 0..15 = hook ids -1..14 */
    END();
}

/* ---- hook procedure running: ClientInfo.phkCurrent / dwHookCurrent ----------------------------- */
struct hrec { int on; int n; int id; uint64_t phk; uint32_t cur; uint32_t fs; };
static struct hrec g_h;
static LRESULT CALLBACK rec_hook(int c, WPARAM w, LPARAM l) {
    if (g_h.on && c >= 0 && g_h.n == 0) {
        g_h.n = 1;
        g_h.phk = teb_q(CI_PHKCUR);
        g_h.cur = teb_d(CI_HOOKCUR);
        g_h.fs = teb_d(CI_FSHOOKS);
    }
    return U(CallNextHookEx)(NULL, c, w, l);
}
static void hookcur_case(int id, int how) {
    NEED_GUI();
    HWND h = mk_plain();
    if (!h) { wk_reap(); t_skip("window creation failed"); return; }
    memset(&g_h, 0, sizeof g_h);
    HHOOK hk = U(SetWindowsHookExW)(id, rec_hook, NULL, GetCurrentThreadId());
    if (!hk) { wk_reap(); t_skip("SetWindowsHookEx(%d) failed", id); return; }
    g_h.on = 1;
    MSG m;
    switch (how) {
    case 0: U(SendMessageW)(h, WM_APP + 5, 1, 2); break;
    case 1: U(PostMessageW)(h, WM_APP + 6, 1, 2); U(PeekMessageW)(&m, h, WM_APP + 6, WM_APP + 6, PM_REMOVE); break;
    case 2: wk_add(U(CreateWindowExW)(EX_GUEST, plain_class(), L"cbt", WS_POPUP, -30000, -30000, 5, 5, NULL, NULL, g_hinst(), NULL)); break;
    }
    g_h.on = 0;
    U(UnhookWindowsHookEx)(hk);
    EXPECT_EQ(g_h.n, 1);
    /* Win10 22H2 forwards CallNextHookEx to the kernel: the user-mode phkCurrent slot stays NULL during the hook call,
     * while HIWORD(dwHookCurrent) carries the hook id (observed for all four hook kinds). */
    EXPECT_EQ(g_h.phk, 0);
    EXPECT_EQ(g_h.cur >> 16, id);
    CHECK(g_h.fs & (1u << (id + 1)));                  /* the hook is registered while its procedure runs */
    EXPECT_EQ(teb_q(CI_PHKCUR), 0);                    /* and cleared once the hook returned */
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_hookcurrent_callwndproc) { hookcur_case(WH_CALLWNDPROC, 0); }
TEST(r2_wnd_cls_heap_fields_ci_hookcurrent_callwndprocret) { hookcur_case(WH_CALLWNDPROCRET, 0); }
TEST(r2_wnd_cls_heap_fields_ci_hookcurrent_getmessage) { hookcur_case(WH_GETMESSAGE, 1); }
TEST(r2_wnd_cls_heap_fields_ci_hookcurrent_cbt) { hookcur_case(WH_CBT, 2); }

/* ---- CLIENTTHREADINFO ---------------------------------------------------------------------------- */
static uint64_t cti(void) { return teb_q(CI_PCTI); }
static void drain(void) {
    MSG m;
    for (int i = 0; i < 200 && U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE); i++) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
}
TEST(r2_wnd_cls_heap_fields_ci_cti_present_and_readable) {
    NEED_GUI();
    uint64_t c = cti();
    CHECK(c != 0 && c < 0x800000000000ull);
    unsigned char b[0x18];
    CHECK(k_try_read((void *)(ULONG_PTR)c, b, sizeof b));
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_cti_postmessage_sets_wake_bit) {
    NEED_GUI();
    uint64_t c = cti();
    if (!c) { wk_reap(); t_skip("no CLIENTTHREADINFO"); return; }
    drain();
    CHECK((read_w(c + CTI_WAKE) & QS_POSTMSG) == 0);
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 3, 0, 0));
    CHECK((read_w(c + CTI_WAKE) & QS_POSTMSG) != 0);     /* fsWakeBits: QS_POSTMESSAGE while one is queued */
    MSG m;
    CHECK(U(PeekMessageW)(&m, NULL, WM_APP + 3, WM_APP + 3, PM_REMOVE));
    EXPECT_EQ(m.message, WM_APP + 3);
    CHECK((read_w(c + CTI_WAKE) & QS_POSTMSG) == 0);     /* removed: bit gone again */
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_cti_getqueuestatus_matches_raw_bits) {
    NEED_GUI();
    uint64_t c = cti();
    if (!c) { wk_reap(); t_skip("no CLIENTTHREADINFO"); return; }
    drain();
    U(GetQueueStatus)(QS_POSTMSG);                        /* clear the change bit */
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 4, 0, 0));
    CHECK((read_w(c + CTI_CHANGE) & QS_POSTMSG) != 0);    /* fsChangeBits: arrived since the last query */
    DWORD q = U(GetQueueStatus)(QS_POSTMSG);
    EXPECT_EQ(HIWORD(q) & QS_POSTMSG, QS_POSTMSG);        /* in the queue now == fsWakeBits */
    EXPECT_EQ(LOWORD(q) & QS_POSTMSG, QS_POSTMSG);        /* new since last call == fsChangeBits */
    CHECK((read_w(c + CTI_CHANGE) & QS_POSTMSG) == 0);    /* GetQueueStatus consumed the change bit */
    CHECK((read_w(c + CTI_WAKE) & QS_POSTMSG) != 0);      /* but the message is still queued */
    q = U(GetQueueStatus)(QS_POSTMSG);
    EXPECT_EQ(LOWORD(q) & QS_POSTMSG, 0);
    EXPECT_EQ(HIWORD(q) & QS_POSTMSG, QS_POSTMSG);
    MSG m;
    U(PeekMessageW)(&m, NULL, WM_APP + 4, WM_APP + 4, PM_REMOVE);
    EXPECT_EQ(HIWORD(U(GetQueueStatus)(QS_POSTMSG)) & QS_POSTMSG, 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_cti_wake_bits_mask_matches_flags) {
    NEED_GUI();
    uint64_t c = cti();
    if (!c) { wk_reap(); t_skip("no CLIENTTHREADINFO"); return; }
    drain();
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 4, 0, 0));
    /* the HIWORD is fsWakeBits & flags: unrelated flags give 0 */
    EXPECT_EQ(HIWORD(U(GetQueueStatus)(QS_MOUSEBUTTON)) & QS_POSTMSG, 0);
    EXPECT_EQ(HIWORD(U(GetQueueStatus)(QS_POSTMSG | QS_KEY)) & QS_POSTMSG, QS_POSTMSG);
    MSG m;
    U(PeekMessageW)(&m, NULL, WM_APP + 4, WM_APP + 4, PM_REMOVE);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_cti_getqueuestatus_invalid_flags) {
    NEED_GUI();
    SetLastError(0);
    EXPECT_EQ(U(GetQueueStatus)(0x80000000u), 0);        /* user32 rejects the reserved bits before any syscall */
    EXPECT_ERR(ERROR_INVALID_FLAGS);
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_cti_getinputstate_follows_change_bits) {
    NEED_GUI();
    uint64_t c = cti();
    if (!c) { wk_reap(); t_skip("no CLIENTTHREADINFO"); return; }
    drain();
    U(GetQueueStatus)(QS_ALLINPUT);
    uint8_t ch = 0;
    k_try_read((void *)(ULONG_PTR)(c + CTI_CHANGE), &ch, 1);
    if (ch & 5) { t_note("input change bits 0x%x set by background input", ch); }
    else CHECK(!U(GetInputState)());                      /* GetInputState returns FALSE when fsChangeBits & (QS_KEY|QS_MOUSEBUTTON) is clear */
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_cti_timelastread_follows_retrieval) {
    NEED_GUI();
    uint64_t c = cti();
    if (!c) { wk_reap(); t_skip("no CLIENTTHREADINFO"); return; }
    drain();
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 8, 0, 0));
    MSG m;
    CHECK(U(PeekMessageW)(&m, NULL, WM_APP + 8, WM_APP + 8, PM_REMOVE));
    DWORD now = GetTickCount();
    int32_t last = (int32_t)read_d(c + CTI_TIMEREAD);
    int32_t d = (int32_t)(now - (DWORD)last);
    CHECKF(d >= -1 && d < 5000, "now %lu last read %ld", (unsigned long)now, (long)last);
    END();
}

/* InSendMessage: set (CTI flag bit 1) only while handling a message sent from another thread. */
static struct { int n; uint32_t flags; BOOL ins; DWORD insex; HWND sender_ok; } g_ins;
static LRESULT CALLBACK ins_wp(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_APP + 9) {
        uint64_t c = cti();
        g_ins.n++;
        g_ins.flags = c ? read_d(c + CTI_FLAGS) : 0xffffffffu;
        g_ins.ins = U(InSendMessage)();
        g_ins.insex = U(InSendMessageEx)(NULL);
        return 77;
    }
    return DefWindowProcW(h, m, wp, lp);
}
struct sender { HWND h; LRESULT r; HANDLE done; };
static DWORD WINAPI sender_main(void *p) {
    struct sender *s = p;
    s->r = SendMessageW(s->h, WM_APP + 9, 0, 0);
    SetEvent(s->done);
    return 0;
}
TEST(r2_wnd_cls_heap_fields_ci_cti_insendmessage_flag_cross_thread) {
    NEED_GUI();
    uint64_t c = cti();
    if (!c) { wk_reap(); t_skip("no CLIENTTHREADINFO"); return; }
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, ins_wp, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    if (!h) { wk_reap(); t_skip("window creation failed"); return; }
    /* same thread first: not "in send message" */
    memset(&g_ins, 0, sizeof g_ins);
    EXPECT_EQ(U(SendMessageW)(h, WM_APP + 9, 0, 0), 77);
    EXPECT_EQ(g_ins.n, 1);
    EXPECT_EQ(g_ins.flags & 2, 0);
    EXPECT_EQ(g_ins.ins, FALSE);
    EXPECT_EQ(g_ins.insex, 0);
    /* now from a second thread */
    memset(&g_ins, 0, sizeof g_ins);
    struct sender s = { h, 0, CreateEventW(NULL, TRUE, FALSE, NULL) };
    HANDLE th = CreateThread(NULL, 0, sender_main, &s, 0, NULL);
    DWORD t0 = GetTickCount();
    while (WaitForSingleObject(s.done, 0) != WAIT_OBJECT_0 && GetTickCount() - t0 < 5000) {
        MsgWaitForMultipleObjects(1, &s.done, FALSE, 50, QS_ALLINPUT);
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
    }
    CHECK(WaitForSingleObject(s.done, 1000) == WAIT_OBJECT_0);
    EXPECT_EQ(s.r, 77);
    EXPECT_EQ(g_ins.n, 1);
    EXPECT_EQ(g_ins.flags & 2, 2);                       /* CLIENTTHREADINFO flag read by InSendMessage */
    EXPECT_EQ(g_ins.ins, TRUE);
    CHECK(g_ins.insex != 0);
    EXPECT_EQ(read_d(c + CTI_FLAGS) & 2, 0);             /* cleared once the message is done */
    CHECK(!U(InSendMessage)());
    WaitForSingleObject(th, 2000); CloseHandle(th); CloseHandle(s.done);
    END();
}

/* ---- key state cache ------------------------------------------------------------------------------ */
UDECL(SHORT, NtUserGetKeyState, (int));
TEST(r2_wnd_cls_heap_fields_ci_keystate_cache_matches_syscall) {
    NEED_GUI();
    REQUIRE(UN(NtUserGetKeyState));
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { wk_reap(); t_skip("no shared info"); return; }
    int checked = 0;
    for (int vk = 0; vk < 0x20; vk++) {
        SHORT api = U(GetKeyState)(vk);
        SHORT sys = UN(NtUserGetKeyState)(vk);
        uint32_t cache = teb_d(CI_KEYCACHE), gcache = read_d((uint64_t)(ULONG_PTR)si.psi + GPSI_KEYCACHE);
        EXPECT_EQ((USHORT)api & 0x8000, (USHORT)sys & 0x8000);        /* down state agrees on either path */
        EXPECT_EQ(api & 1, sys & 1);                                  /* toggle state agrees */
        if (cache == gcache) {
            /* user32 reads two bits per key from ClientInfo.afKeyState: bit 2*(vk&3) down, bit 2*(vk&3)+1 toggle */
            uint8_t b = 0;
            k_try_read((char *)k_teb() + CI_KEYSTATE + (vk >> 2), &b, 1);
            int down = (b >> ((vk & 3) * 2)) & 1, tog = (b >> ((vk & 3) * 2 + 1)) & 1;
            EXPECT_EQ(((USHORT)sys & 0x8000) != 0, down);
            EXPECT_EQ(sys & 1, tog);
            checked++;
        }
    }
    if (!checked) t_note("ClientInfo key-state cache was never current (shared cache 0x%lx)", (unsigned long)read_d((uint64_t)(ULONG_PTR)si.psi + GPSI_KEYCACHE));
    END();
}
TEST(r2_wnd_cls_heap_fields_ci_keystate_unpressed_mouse_and_modifiers) {
    NEED_GUI();
    REQUIRE(UN(NtUserGetKeyState));
    static const int keys[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_SHIFT, VK_CONTROL, VK_MENU, VK_RETURN, VK_ESCAPE };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        SHORT s = U(GetKeyState)(keys[i]);
        if (s & 0x8000) t_note("key 0x%x is down in this session", keys[i]);
        /* user32's cached path returns 0xff80 | toggle for a down key and just the toggle bit otherwise */
        if (s & 0x8000) EXPECT_EQ((USHORT)s & 0xff80, 0xff80); else EXPECT_EQ((USHORT)s & ~1, 0);
    }
    END();
}
