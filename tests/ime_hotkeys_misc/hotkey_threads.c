/* Hotkey ownership across threads and processes: the table is session-wide (gphkHashTable) but each
 * entry is owned by the registering pti, so key clashes cross threads and processes while unregister
 * only ever matches the caller's own entries (HKRemoveMatchingHotkeys query type 3). */
#include "imhm.h"

#define SENT 0x7e57u

struct ht {
    UINT mods, vk;
    int id;
    HWND hwnd;
    HANDLE ready, go;   /* worker -> main, main -> worker */
    BOOL reg_ok;
    DWORD reg_err;
    BOOL second_ok;
    DWORD second_err;
    BOOL unreg_ok;
    DWORD unreg_err;
    int mode;
};

/* mode 1: register key then hold until `go`; mode 2: register, exit immediately (thread teardown frees it)
 * mode 3: try to register the key (expect clash) then try to unregister the id (expect not-registered) */
static DWORD WINAPI worker(void *p) {
    struct ht *h = (struct ht *)p;
    if (h->mode == 1 || h->mode == 2) {
        SetLastError(SENT);
        h->reg_ok = RHK(NULL, h->id, h->mods, h->vk);
        h->reg_err = GetLastError();
        SetEvent(h->ready);
        if (h->mode == 1) {
            DWORD t0 = GetTickCount();
            while (WaitForSingleObject(h->go, 20) == WAIT_TIMEOUT && GetTickCount() - t0 < 10000) {
                MSG m;
                while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) {}
            }
        }
        return 0;
    }
    SetLastError(SENT);
    h->reg_ok = RHK(NULL, h->id, h->mods, h->vk);
    h->reg_err = GetLastError();
    SetLastError(SENT);
    h->unreg_ok = UHK(NULL, h->id);
    h->unreg_err = GetLastError();
    if (h->hwnd) {
        SetLastError(SENT);
        h->second_ok = RHK(h->hwnd, h->id, h->mods, h->vk);
        h->second_err = GetLastError();
    }
    SetEvent(h->ready);
    return 0;
}
static HANDLE spawn(struct ht *h) {
    h->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    h->go = CreateEventW(NULL, TRUE, FALSE, NULL);
    return CreateThread(NULL, 0, worker, h, 0, NULL);
}
static void reap(struct ht *h, HANDLE t) {
    SetEvent(h->go);
    if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
    CloseHandle(h->ready); CloseHandle(h->go);
}

TEST(ime_hotkeys_misc_hkt_other_thread_clashes_on_same_key) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 1, k.mods, k.vk));
    struct ht h = { k.mods, k.vk, 2, 0 };
    h.mode = 3;
    HANDLE t = spawn(&h);
    WaitForSingleObject(h.ready, 5000);
    CHECK(!h.reg_ok);
    EXPECT_EQ(h.reg_err, ERROR_HOTKEY_ALREADY_REGISTERED);
    reap(&h, t);
    UHK(NULL, 1);
}
TEST(ime_hotkeys_misc_hkt_other_thread_cannot_unregister_ours) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 3, k.mods, k.vk));
    struct ht h = { k.mods, k.vk, 3, 0 };
    h.mode = 3;
    HANDLE t = spawn(&h);
    WaitForSingleObject(h.ready, 5000);
    CHECK(!h.unreg_ok);
    EXPECT_EQ(h.unreg_err, ERROR_HOTKEY_NOT_REGISTERED); /* same id, but owned by another pti */
    reap(&h, t);
    CHECK(UHK(NULL, 3));  /* still ours */
}
TEST(ime_hotkeys_misc_hkt_other_thread_same_id_distinct_key_is_independent) {
    NEED_HK(); HK_DECL(a); HK_DECL(b);
    CHECK(RHK(NULL, 4, a.mods, a.vk));
    struct ht h = { b.mods, b.vk, 4, 0 };
    h.mode = 1;
    HANDLE t = spawn(&h);
    WaitForSingleObject(h.ready, 5000);
    CHECK(h.reg_ok);  /* ids are per (thread, hwnd) */
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 5, b.mods, b.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED); /* the other thread holds b */
    CHECK(UHK(NULL, 4));
    reap(&h, t);
}
TEST(ime_hotkeys_misc_hkt_thread_exit_releases_keys) {
    NEED_HK(); HK_DECL(k);
    struct ht h = { k.mods, k.vk, 6, 0 };
    h.mode = 2;
    HANDLE t = spawn(&h);
    WaitForSingleObject(h.ready, 5000);
    CHECK(h.reg_ok);
    WaitForSingleObject(t, 5000);
    /* thread destruction runs DestroyThreadsHotKeys -> HKRemoveMatchingHotkeys(pti) */
    BOOL r = FALSE;
    for (int i = 0; i < 40 && !r; i++) {   /* bounded poll: thread teardown in the kernel may lag the exit */
        SET_ERR(SENT);
        r = RHK(NULL, 7, k.mods, k.vk);
        if (!r) Sleep(50);
    }
    CHECKF(r, "key still held after owner thread exit, err %lu", GetLastError());
    if (r) UHK(NULL, 7);
    reap(&h, t);
}
TEST(ime_hotkeys_misc_hkt_held_key_blocks_until_release) {
    NEED_HK(); HK_DECL(k);
    struct ht h = { k.mods, k.vk, 8, 0 };
    h.mode = 1;
    HANDLE t = spawn(&h);
    WaitForSingleObject(h.ready, 5000);
    CHECK(h.reg_ok);
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 9, k.mods, k.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    SetEvent(h.go);
    WaitForSingleObject(t, 5000);
    BOOL r9 = FALSE;
    for (int i = 0; i < 40 && !r9; i++) { r9 = RHK(NULL, 9, k.mods, k.vk); if (!r9) Sleep(50); }
    CHECK(r9);
    UHK(NULL, 9);
    reap(&h, t);
}
TEST(ime_hotkeys_misc_hkt_register_on_foreign_thread_window) {
    NEED_HK(); HK_DECL(k);
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); t_skip("helper window"); return; }
    SET_ERR(SENT);
    BOOL r = RHK(x.hwnd, 10, k.mods, k.vk);
    EXPECT_FAIL(r == 0, ERROR_WINDOW_OF_OTHER_THREAD);  /* */
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_hkt_unregister_foreign_window_is_not_registered) {
    NEED_HK();
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); t_skip("helper window"); return; }
    SET_ERR(SENT);
    BOOL r = UHK(x.hwnd, 11);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_NOT_REGISTERED);  /* valid window, no entry of our pti */
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_hkt_helper_thread_own_window_hotkey) {
    NEED_HK(); HK_DECL(k);
    struct ht h = { k.mods, k.vk, 12, 0 };
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); t_skip("helper window"); return; }
    h.hwnd = x.hwnd;
    h.mode = 3;
    HANDLE t = spawn(&h);
    WaitForSingleObject(h.ready, 5000);
    /* the worker thread is not the owner of x.hwnd either, so it gets 1408 for the second step; its first
     * (thread) registration works and is dropped by its own unregister */
    CHECK(h.reg_ok);
    CHECK(h.unreg_ok);
    CHECK(!h.second_ok);
    EXPECT_EQ(h.second_err, ERROR_WINDOW_OF_OTHER_THREAD);
    reap(&h, t);
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_hkt_many_threads_unique_keys) {
    NEED_HK();
    enum { N = 8 };
    struct ht h[N];
    HANDLE t[N];
    for (int i = 0; i < N; i++) {
        struct hk k = hk_next();
        memset(&h[i], 0, sizeof h[i]);
        h[i].mods = k.mods; h[i].vk = k.vk; h[i].id = 1; h[i].mode = 1;
        t[i] = spawn(&h[i]);
    }
    int ok = 0;
    for (int i = 0; i < N; i++) { WaitForSingleObject(h[i].ready, 5000); ok += h[i].reg_ok; }
    EXPECT_EQ(ok, N);  /* identical id in each thread is fine: ids are per owner */
    for (int i = 0; i < N; i++) reap(&h[i], t[i]);
}
TEST(ime_hotkeys_misc_hkt_many_threads_same_key_one_wins) {
    NEED_HK(); HK_DECL(k);
    enum { N = 6 };
    struct ht h[N];
    HANDLE t[N];
    for (int i = 0; i < N; i++) {
        memset(&h[i], 0, sizeof h[i]);
        h[i].mods = k.mods; h[i].vk = k.vk; h[i].id = 1; h[i].mode = 1;
        t[i] = spawn(&h[i]);
        WaitForSingleObject(h[i].ready, 5000);  /* serialize so the winner is deterministic: thread 0 */
    }
    int ok = 0, dup = 0;
    for (int i = 0; i < N; i++) { ok += h[i].reg_ok; dup += (!h[i].reg_ok && h[i].reg_err == ERROR_HOTKEY_ALREADY_REGISTERED); }
    EXPECT_EQ(ok, 1);
    EXPECT_EQ(dup, N - 1);
    CHECK(h[0].reg_ok);
    for (int i = 0; i < N; i++) reap(&h[i], t[i]);
}

/* ---- cross-process --------------------------------------------------------------------------- */
/* Child: first win32k call of the process is RegisterHotKey. Holds the key until released. */
CHILD(imhm_hk_holder) {
    if (argc < 5) return 1;
    UINT mods = (UINT)strtoul(argv[1], NULL, 16);
    UINT vk = (UINT)strtoul(argv[2], NULL, 16);
    char ready[64], go[64];
    snprintf(ready, sizeof ready, "win32kt_imhm_ready_%s", argv[3]);
    snprintf(go, sizeof go, "win32kt_imhm_go_%s", argv[3]);
    HANDLE hr = OpenEventA(EVENT_MODIFY_STATE, FALSE, ready);
    HANDLE hg = OpenEventA(SYNCHRONIZE, FALSE, go);
    if (!hr || !hg) { printf("child: events missing\n"); return 1; }
    int fails = 0;
    im_install_stub_callbacks();      /* no user32 in this process: see imhm.h */
    SetLastError(SENT);
    BOOL r = RHK(NULL, (int)strtoul(argv[4], NULL, 10), mods, vk);
    if (!r) { fails++; printf("  FAIL child RegisterHotKey err %lu\n", GetLastError()); }
    SetEvent(hr);
    WaitForSingleObject(hg, 10000);
    return fails;
}
static HANDLE start_holder(UINT mods, UINT vk, int id, HANDLE *hr, HANDLE *hg, PROCESS_INFORMATION *pi) {
    char name[64], args[128];
    DWORD pid = GetCurrentProcessId();
    static int seq;
    snprintf(name, sizeof name, "win32kt_imhm_ready_%lu_%d", pid, ++seq);
    *hr = CreateEventA(NULL, TRUE, FALSE, name);
    snprintf(name, sizeof name, "win32kt_imhm_go_%lu_%d", pid, seq);
    *hg = CreateEventA(NULL, TRUE, FALSE, name);
    snprintf(args, sizeof args, "%x %x %lu_%d %d", mods, vk, pid, seq, id);
    return t_spawn_child("imhm_hk_holder", args, 0, NULL, NULL, NULL, FALSE, NULL, pi);
}
TEST(ime_hotkeys_misc_hkp_other_process_holds_key) {
    NEED_HK(); HK_DECL(k);
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    HANDLE hr, hg; PROCESS_INFORMATION pi;
    HANDLE p = start_holder(k.mods, k.vk, 1, &hr, &hg, &pi);
    if (!p) { t_skip("spawn failed"); return; }
    if (WaitForSingleObject(hr, 15000) != WAIT_OBJECT_0) { t_skip("child did not register in time"); TerminateProcess(p, 1); goto out; }
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 1, k.mods, k.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);  /* gphkHashTable is session wide */
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(NULL, 1) == 0, ERROR_HOTKEY_NOT_REGISTERED);  /* cannot remove another process' entry */
    SetEvent(hg);
    if (WaitForSingleObject(p, 10000) == WAIT_OBJECT_0) {
        DWORD ec = 99; GetExitCodeProcess(p, &ec);
        EXPECT_EQ(ec, 0);
        Sleep(100);
        BOOL r = RHK(NULL, 2, k.mods, k.vk);
        CHECKF(r, "key still held after child exit, err %lu", GetLastError());  /* process teardown destroys its hotkeys */
        if (r) UHK(NULL, 2);
    } else { t_note("child did not exit"); TerminateProcess(p, 1); }
out:
    CloseHandle(p); CloseHandle(pi.hThread); CloseHandle(hr); CloseHandle(hg);
}
TEST(ime_hotkeys_misc_hkp_other_process_different_key_coexists) {
    NEED_HK(); HK_DECL(a); HK_DECL(b);
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    HANDLE hr, hg; PROCESS_INFORMATION pi;
    HANDLE p = start_holder(a.mods, a.vk, 1, &hr, &hg, &pi);
    if (!p) { t_skip("spawn failed"); return; }
    if (WaitForSingleObject(hr, 15000) != WAIT_OBJECT_0) { t_skip("child did not register in time"); TerminateProcess(p, 1); goto out; }
    CHECK(RHK(NULL, 1, b.mods, b.vk));
    CHECK(UHK(NULL, 1));
    SetEvent(hg);
    WaitForSingleObject(p, 10000);
out:
    CloseHandle(p); CloseHandle(pi.hThread); CloseHandle(hr); CloseHandle(hg);
}
TEST(ime_hotkeys_misc_hkp_child_first_gui_call_is_register) {
    NEED_HK(); HK_DECL(k);
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    HANDLE hr, hg; PROCESS_INFORMATION pi;
    HANDLE p = start_holder(k.mods, k.vk, 77, &hr, &hg, &pi);
    if (!p) { t_skip("spawn failed"); return; }
    DWORD w = WaitForSingleObject(hr, 15000);
    CHECK(w == WAIT_OBJECT_0);  /* the child registered as its very first win32k syscall (thread conversion path) */
    SetEvent(hg);
    DWORD ec = 99;
    if (WaitForSingleObject(p, 10000) == WAIT_OBJECT_0) GetExitCodeProcess(p, &ec); else TerminateProcess(p, 1);
    EXPECT_EQ(ec, 0);
    CloseHandle(p); CloseHandle(pi.hThread); CloseHandle(hr); CloseHandle(hg);
}
