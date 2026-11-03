/* hooks_winevents: parameter validation of NtUserSetWindowsHookEx / NtUserUnhookWindowsHookEx /
 * NtUserCallNextHookEx, reached both through user32 (SetWindowsHookEx*, UnhookWindowsHookEx, CallNextHookEx) and
 * through the win32u stubs directly.
 *
 * Order of the kernel checks (win32kfull!NtUserSetWindowsHookEx then SetWindowsHookEx):
 *   1. tid != 0 and PtiFromThreadId(tid) == NULL            -> ERROR_INVALID_PARAMETER (0x57)
 *   2. hmod == PEB->ImageBaseAddress                        -> hmod and pstrLib are zeroed
 *   3. (idHook + 1) > 15 (unsigned)                         -> 0x592 ERROR_INVALID_HOOK_FILTER (1426)
 *   4. proc == NULL                                         -> 0x593 ERROR_INVALID_FILTER_PROC (1427)
 *   5. tid != 0: attribute-table bit 0x02 clear              -> 0x595 ERROR_GLOBAL_ONLY_HOOK (1429)
 *      tid == 0, hmod == 0, attribute bits 0x24 clear        -> 0x594 ERROR_HOOK_NEEDS_HMOD (1428)
 *
 * The values 1426/1427 are the winerror.h numbers for ERROR_INVALID_HOOK_FILTER/ERROR_INVALID_FILTER_PROC.
 */
#include "hk.h"
#include <limits.h>

static void gui(void) { U(GetDesktopWindow)(); }
#define NEED() do { REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(UnhookWindowsHookEx)); REQUIRE(U(CallNextHookEx)); REQUIRE(U(GetDesktopWindow)); gui(); } while (0)
#define NEED_NT() do { REQUIRE(UN(NtUserSetWindowsHookEx)); REQUIRE(UN(NtUserUnhookWindowsHookEx)); NEED(); } while (0)

static HHOOK set_u(int id, HOOKPROC p, DWORD tid) { SetLastError(0xDEAD0001); return U(SetWindowsHookExW)(id, p, NULL, tid); }
static HHOOK set_nt(int id, void *p, DWORD tid, HMODULE hmod) {
    SetLastError(0xDEAD0001);
    return (HHOOK)UN(NtUserSetWindowsHookEx)(hmod, NULL, (int)tid, id, p, 0);
}

/* ---- invalid hook ids: (id + 1) > 15 as unsigned ---- */
static const int bad_ids[] = { -2, -3, -100, 15, 16, 17, 255, 0x7fffffff, INT_MIN, -0x7fffffff };

TEST(hooks_winevents_set_invalid_id_user32) {
    NEED();
    for (unsigned i = 0; i < sizeof bad_ids / sizeof *bad_ids; i++) {
        HHOOK h = set_u(bad_ids[i], hk_nop_proc, GetCurrentThreadId());
        CHECKF(h == NULL, "id %d returned %p", bad_ids[i], h);
        if (h) U(UnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
    }
}
TEST(hooks_winevents_set_invalid_id_nt) {
    NEED_NT();
    for (unsigned i = 0; i < sizeof bad_ids / sizeof *bad_ids; i++) {
        HHOOK h = set_nt(bad_ids[i], hk_nop_proc, GetCurrentThreadId(), NULL);
        CHECKF(h == NULL, "id %d returned %p", bad_ids[i], h);
        if (h) UN(NtUserUnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
    }
}
TEST(hooks_winevents_set_invalid_id_global_scope) {
    /* the id check (SetWindowsHookEx first branch) does not depend on the scope */
    NEED();
    for (unsigned i = 0; i < sizeof bad_ids / sizeof *bad_ids; i++) {
        HHOOK h = set_u(bad_ids[i], hk_nop_proc, 0);
        CHECKF(h == NULL, "id %d", bad_ids[i]);
        EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
    }
}
TEST(hooks_winevents_set_boundary_ids) {
    NEED();
    /* -1 (WH_MSGFILTER) is the lowest valid id: accepted as a thread hook; 15 is one past WH_MAXHOOK(14). */
    HHOOK h = set_u(-1, hk_nop_proc, GetCurrentThreadId());
    CHECK(h != NULL);
    if (h) CHECK(U(UnhookWindowsHookEx)(h));
    HHOOK h15 = set_u(15, hk_nop_proc, GetCurrentThreadId());
    CHECK(h15 == NULL); EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
    HHOOK h14 = set_u(14, hk_nop_proc, GetCurrentThreadId()); /* WH_MOUSE_LL is global-only: valid id, wrong scope */
    CHECK(h14 == NULL); EXPECT_ERR(HKE_GLOBAL_ONLY_HOOK);
}

/* ---- NULL procedure ---- */
TEST(hooks_winevents_set_null_proc_thread_scope_every_valid_id) {
    NEED();
    for (int id = -1; id <= 14; id++) {
        HHOOK h = set_u(id, NULL, GetCurrentThreadId());
        CHECKF(h == NULL, "id %d", id);
        if (h) U(UnhookWindowsHookEx)(h);
        /* the proc check (0x593) precedes the scope check, so even the global-only ids report the proc error */
        EXPECT_ERR(HKE_INVALID_FILTER_PROC);
    }
}
TEST(hooks_winevents_set_null_proc_global_scope_every_valid_id) {
    NEED();
    for (int id = -1; id <= 14; id++) {
        HHOOK h = set_u(id, NULL, 0);
        CHECKF(h == NULL, "id %d", id);
        if (h) U(UnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_INVALID_FILTER_PROC);
    }
}
TEST(hooks_winevents_set_null_proc_nt) {
    NEED_NT();
    for (int id = -1; id <= 14; id++) {
        HHOOK h = set_nt(id, NULL, GetCurrentThreadId(), NULL);
        CHECKF(h == NULL, "id %d", id);
        EXPECT_ERR(HKE_INVALID_FILTER_PROC);
    }
}
TEST(hooks_winevents_set_id_checked_before_proc) {
    NEED();
    HHOOK h = set_u(99, NULL, GetCurrentThreadId());
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
    h = set_u(-2, NULL, 0);
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
}

/* ---- thread id validation ---- */
static const DWORD bad_tids[] = { 0xFFFFFFF0u, 0x7FFFFFF0u, 0x80000000u, 0xFFFFFFFFu, 0x00FFFFFCu };

TEST(hooks_winevents_set_nonexistent_tid_user32) {
    NEED();
    for (unsigned i = 0; i < sizeof bad_tids / sizeof *bad_tids; i++) {
        HHOOK h = set_u(WH_GETMESSAGE, hk_nop_proc, bad_tids[i]);
        CHECKF(h == NULL, "tid 0x%lx", (unsigned long)bad_tids[i]);
        if (h) U(UnhookWindowsHookEx)(h);
        /* NtUserSetWindowsHookEx: PtiFromThreadId(tid) == 0 -> UserSetLastError(0x57) */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(hooks_winevents_set_nonexistent_tid_nt) {
    NEED_NT();
    for (unsigned i = 0; i < sizeof bad_tids / sizeof *bad_tids; i++) {
        HHOOK h = set_nt(WH_GETMESSAGE, hk_nop_proc, bad_tids[i], NULL);
        CHECKF(h == NULL, "tid 0x%lx", (unsigned long)bad_tids[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(hooks_winevents_set_tid_checked_before_id_and_proc) {
    NEED_NT();
    /* the thread lookup is the first thing NtUserSetWindowsHookEx does, so it wins over both zzz checks */
    HHOOK h = set_nt(99, hk_nop_proc, 0xFFFFFFF0u, NULL);
    CHECK(h == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    h = set_nt(WH_GETMESSAGE, NULL, 0xFFFFFFF0u, NULL);
    CHECK(h == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
static DWORD WINAPI exit_now(LPVOID p) { return 0; }
TEST(hooks_winevents_set_terminated_thread_tid) {
    NEED();
    DWORD tid = 0;
    HANDLE th = CreateThread(NULL, 0, exit_now, NULL, 0, &tid);
    REQUIRE(th);
    WaitForSingleObject(th, 5000);
    HHOOK h = set_u(WH_GETMESSAGE, hk_nop_proc, tid); /* handle kept open so the id is not recycled */
    CHECK(h == NULL);
    if (h) U(UnhookWindowsHookEx)(h);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CloseHandle(th);
}
static DWORD WINAPI block_thread(LPVOID p) { WaitForSingleObject((HANDLE)p, 10000); return 0; }
TEST(hooks_winevents_set_non_gui_thread_tid) {
    NEED();
    /* PtiFromThreadId requires a W32THREAD with the "gui initialised" flag: a thread that never made a win32k call
     * (blocked in WaitForSingleObject) has none, so the lookup fails exactly like a missing thread. */
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD tid = 0;
    HANDLE th = CreateThread(NULL, 0, block_thread, ev, 0, &tid);
    REQUIRE(th);
    Sleep(50);
    HHOOK h = set_u(WH_GETMESSAGE, hk_nop_proc, tid);
    if (t_is_wine()) { if (h) U(UnhookWindowsHookEx)(h); SetEvent(ev); WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(ev); t_skip("Wine creates a queue for every thread"); return; }
    CHECK(h == NULL);
    if (h) U(UnhookWindowsHookEx)(h);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetEvent(ev); WaitForSingleObject(th, 5000); CloseHandle(th); CloseHandle(ev);
}

/* ---- global scope needs a module ---- */
static const int needs_hmod_ids[] = { -1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12 };
TEST(hooks_winevents_set_global_without_module_needs_hmod_user32) {
    NEED();
    for (unsigned i = 0; i < sizeof needs_hmod_ids / sizeof *needs_hmod_ids; i++) {
        HHOOK h = set_u(needs_hmod_ids[i], hk_nop_proc, 0);
        CHECKF(h == NULL, "id %d", needs_hmod_ids[i]);
        if (h) U(UnhookWindowsHookEx)(h); /* never expected: would be a live global hook in the whole session */
        EXPECT_ERR(HKE_HOOK_NEEDS_HMOD);
    }
}
TEST(hooks_winevents_set_global_without_module_needs_hmod_nt) {
    NEED_NT();
    for (unsigned i = 0; i < sizeof needs_hmod_ids / sizeof *needs_hmod_ids; i++) {
        HHOOK h = set_nt(needs_hmod_ids[i], hk_nop_proc, 0, NULL);
        CHECKF(h == NULL, "id %d", needs_hmod_ids[i]);
        if (h) UN(NtUserUnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_HOOK_NEEDS_HMOD);
    }
}
TEST(hooks_winevents_set_global_with_exe_module_is_zeroed_user32) {
    NEED();
    /* NtUserSetWindowsHookEx: hmod == PEB->ImageBaseAddress is replaced by 0, so an in-exe procedure cannot be a
     * global hook (it could not be injected into other processes) - same result as passing no module at all. */
    HMODULE exe = GetModuleHandleW(NULL);
    for (unsigned i = 0; i < sizeof needs_hmod_ids / sizeof *needs_hmod_ids; i++) {
        SetLastError(0xDEAD0001);
        HHOOK h = U(SetWindowsHookExW)(needs_hmod_ids[i], hk_nop_proc, exe, 0);
        CHECKF(h == NULL, "id %d", needs_hmod_ids[i]);
        if (h) U(UnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_HOOK_NEEDS_HMOD);
    }
}
TEST(hooks_winevents_set_global_with_exe_module_is_zeroed_nt) {
    NEED_NT();
    HMODULE exe = GetModuleHandleW(NULL);
    /* pstrLib stays NULL: with hmod == the image base the kernel never reads it */
    for (unsigned i = 0; i < sizeof needs_hmod_ids / sizeof *needs_hmod_ids; i++) {
        HHOOK h = set_nt(needs_hmod_ids[i], hk_nop_proc, 0, exe);
        CHECKF(h == NULL, "id %d", needs_hmod_ids[i]);
        if (h) UN(NtUserUnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_HOOK_NEEDS_HMOD);
    }
}
TEST(hooks_winevents_set_thread_with_exe_module_accepted) {
    NEED();
    HMODULE exe = GetModuleHandleW(NULL);
    for (unsigned i = 0; i < sizeof hk_thread_ids / sizeof *hk_thread_ids; i++) {
        int id = hk_thread_ids[i];
        SetLastError(0xDEAD0001);
        HHOOK h = U(SetWindowsHookExW)(id, hk_nop_proc, exe, GetCurrentThreadId());
        CHECKF(h != NULL, "id %d err %lu", id, GetLastError());
        if (h) CHECK(U(UnhookWindowsHookEx)(h));
    }
}
TEST(hooks_winevents_set_bogus_module_handle_user32_fails) {
    NEED();
    /* user32!SetWindowsHookExAW resolves the module path first; a bad handle ends the call before any syscall */
    SetLastError(0xDEAD0001);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, hk_nop_proc, (HMODULE)(ULONG_PTR)0x10000, 0);
    CHECK(h == NULL);
    if (h) U(UnhookWindowsHookEx)(h);
    t_note("GetLastError after bogus hmod = %lu", GetLastError());
    CHECK(GetLastError() != 0xDEAD0001);
}

/* ---- global-only ids with a thread id ---- */
TEST(hooks_winevents_set_global_only_ids_reject_thread_scope_user32) {
    NEED();
    for (unsigned i = 0; i < sizeof hk_global_only_ids / sizeof *hk_global_only_ids; i++) {
        int id = hk_global_only_ids[i];
        HHOOK h = set_u(id, hk_nop_proc, GetCurrentThreadId());
        CHECKF(h == NULL, "id %d", id);
        if (h) U(UnhookWindowsHookEx)(h);
        /* attribute-table bit 0x02 clear: WH_JOURNALRECORD/PLAYBACK, WH_SYSMSGFILTER, WH_KEYBOARD_LL, WH_MOUSE_LL */
        EXPECT_ERR(HKE_GLOBAL_ONLY_HOOK);
    }
}
TEST(hooks_winevents_set_global_only_ids_reject_thread_scope_nt) {
    NEED_NT();
    for (unsigned i = 0; i < sizeof hk_global_only_ids / sizeof *hk_global_only_ids; i++) {
        int id = hk_global_only_ids[i];
        HHOOK h = set_nt(id, hk_nop_proc, GetCurrentThreadId(), NULL);
        CHECKF(h == NULL, "id %d", id);
        if (h) UN(NtUserUnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_GLOBAL_ONLY_HOOK);
    }
}
TEST(hooks_winevents_set_global_only_ids_ignore_exe_module_with_thread_scope) {
    NEED();
    HMODULE exe = GetModuleHandleW(NULL);
    for (unsigned i = 0; i < sizeof hk_global_only_ids / sizeof *hk_global_only_ids; i++) {
        SetLastError(0xDEAD0001);
        HHOOK h = U(SetWindowsHookExW)(hk_global_only_ids[i], hk_nop_proc, exe, GetCurrentThreadId());
        CHECKF(h == NULL, "id %d", hk_global_only_ids[i]);
        if (h) U(UnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_GLOBAL_ONLY_HOOK);
    }
}

/* ---- successful thread hooks ---- */
TEST(hooks_winevents_set_thread_ids_accepted_and_unhooked) {
    NEED();
    for (unsigned i = 0; i < sizeof hk_thread_ids / sizeof *hk_thread_ids; i++) {
        int id = hk_thread_ids[i];
        HHOOK h = set_u(id, hk_nop_proc, GetCurrentThreadId());
        CHECKF(h != NULL, "id %d err %lu", id, GetLastError());
        if (!h) continue;
        CHECKF(((ULONG_PTR)h >> 32) == 0, "hook handle 0x%p is not a 32-bit USER handle", h);
        CHECK(U(UnhookWindowsHookEx)(h));
    }
}
TEST(hooks_winevents_set_thread_ids_accepted_nt) {
    NEED_NT();
    for (unsigned i = 0; i < sizeof hk_thread_ids / sizeof *hk_thread_ids; i++) {
        int id = hk_thread_ids[i];
        HHOOK h = set_nt(id, hk_nop_proc, GetCurrentThreadId(), NULL);
        CHECKF(h != NULL, "id %d err %lu", id, GetLastError());
        if (!h) continue;
        EXPECT_EQ(UN(NtUserUnhookWindowsHookEx)(h), 1); /* UnhookWindowsHookEx returns 1 */
    }
}
TEST(hooks_winevents_set_flags_are_masked_nt) {
    NEED_NT();
    /* SetWindowsHookEx stores flags & 0x42; no flag value is rejected */
    static const DWORD fl[] = { 0, 2, 0x40, 0x42, 0xFFFFFFFFu, 0x80000000u, 0x1, 0x4 };
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        SetLastError(0xDEAD0001);
        HHOOK h = (HHOOK)UN(NtUserSetWindowsHookEx)(NULL, NULL, (int)GetCurrentThreadId(), WH_GETMESSAGE, hk_nop_proc, fl[i]);
        CHECKF(h != NULL, "flags 0x%lx err %lu", (unsigned long)fl[i], GetLastError());
        if (h) CHECK(UN(NtUserUnhookWindowsHookEx)(h) == 1);
    }
}
TEST(hooks_winevents_set_ansi_variant_accepted) {
    NEED();
    for (unsigned i = 0; i < sizeof hk_thread_ids / sizeof *hk_thread_ids; i++) {
        int id = hk_thread_ids[i];
        SetLastError(0xDEAD0001);
        HHOOK h = U(SetWindowsHookExA)(id, hk_nop_proc, NULL, GetCurrentThreadId());
        CHECKF(h != NULL, "id %d err %lu", id, GetLastError());
        if (h) CHECK(U(UnhookWindowsHookEx)(h));
    }
}
TEST(hooks_winevents_set_returns_distinct_handles) {
    NEED();
    HHOOK h[8]; int n = 0;
    for (int i = 0; i < 8; i++) { h[i] = set_u(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId()); if (h[i]) n++; }
    CHECK(n == 8);
    for (int i = 0; i < 8; i++) for (int j = i + 1; j < 8; j++) CHECKF(!h[i] || h[i] != h[j], "handles %d and %d equal", i, j);
    for (int i = 0; i < 8; i++) if (h[i]) CHECK(U(UnhookWindowsHookEx)(h[i]));
}
TEST(hooks_winevents_set_handle_is_type_hook_in_user_table) {
    NEED();
    REQUIRE(U(IsWindow));
    HHOOK h = set_u(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId());
    REQUIRE(h);
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) { U(UnhookWindowsHookEx)(h); t_skip("USER handle table not readable"); return; }
    EXPECT_EQ(e.type, 5); /* TYPE_HOOK: the handle allocator in SetWindowsHookEx */
    EXPECT_EQ(e.uniq, (unsigned)(((ULONG_PTR)h >> 16) & 0xffff));
    CHECK((e.flags & 1) == 0);
    CHECK(U(UnhookWindowsHookEx)(h));
    CHECK(hk_user_obj_dead(h));
}
TEST(hooks_winevents_set_counts_one_user_object_per_hook) {
    NEED();
    REQUIRE(U(GetGuiResources));
    unsigned long before = hk_gui_objects();
    HHOOK h[6]; int n = 0;
    for (int i = 0; i < 6; i++) { h[i] = set_u(i & 1 ? WH_CBT : WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId()); if (h[i]) n++; }
    unsigned long during = hk_gui_objects();
    EXPECT_EQ(during - before, (unsigned long)n); /* TYPE_HOOK is a charged object type (the handle allocator per-process count) */
    for (int i = 0; i < 6; i++) if (h[i]) U(UnhookWindowsHookEx)(h[i]);
    EXPECT_EQ(hk_gui_objects(), before);
}

/* ---- unhook ---- */
TEST(hooks_winevents_unhook_twice_fails_invalid_hook_handle) {
    NEED();
    HHOOK h = set_u(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId());
    REQUIRE(h);
    CHECK(U(UnhookWindowsHookEx)(h));
    SetLastError(0xDEAD0001);
    CHECK(!U(UnhookWindowsHookEx)(h));
    EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);   /* HMValidateHandle(h, 5) */
}
TEST(hooks_winevents_unhook_nt_return_values) {
    NEED_NT();
    HHOOK h = set_nt(WH_CBT, hk_nop_proc, GetCurrentThreadId(), NULL);
    REQUIRE(h);
    EXPECT_EQ(UN(NtUserUnhookWindowsHookEx)(h), 1);
    SetLastError(0xDEAD0001);
    EXPECT_EQ(UN(NtUserUnhookWindowsHookEx)(h), 0);
    EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);
}
TEST(hooks_winevents_unhook_invalid_handles) {
    NEED_NT();
    static const ULONG_PTR bad[] = { 0, 1, 2, 0xFFFF, 0x10000, 0xFFFF0000u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x12345678u };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0xDEAD0001);
        int r = UN(NtUserUnhookWindowsHookEx)((HHOOK)bad[i]);
        CHECKF(r == 0, "handle 0x%llx returned %d", (unsigned long long)bad[i], r);
        EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);
    }
}
TEST(hooks_winevents_unhook_high_bits_do_not_alias_handle) {
    NEED_NT();
    /* HMValidateHandle takes a 32-bit user handle; a live handle with garbage in the upper half must not unhook it
     * through user32 either - the stub passes the 64-bit register unchanged, so validate that the real handle stays. */
    HHOOK h = set_nt(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId(), NULL);
    REQUIRE(h);
    HHOOK forged = (HHOOK)((ULONG_PTR)h | 0xDEAD00000000ull);
    int r = UN(NtUserUnhookWindowsHookEx)(forged);
    t_note("unhook(handle|0xDEAD<<32) = %d, live hook %s", r, hk_user_obj_valid(h, 5) ? "still valid" : "gone");
    if (r == 0) CHECK(hk_user_obj_valid(h, 5));
    if (hk_user_obj_valid(h, 5)) UN(NtUserUnhookWindowsHookEx)(h);
}
TEST(hooks_winevents_unhook_wrong_object_type_window) {
    NEED_NT();
    HWND w = hk_window("unhook_type");
    REQUIRE(w);
    SetLastError(0xDEAD0001);
    CHECK(UN(NtUserUnhookWindowsHookEx)((HHOOK)w) == 0); /* handle valid but type 1 != 5 */
    EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);
    CHECK(U(IsWindow)(w));
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_unhook_wrong_object_type_winevent) {
    NEED_NT();
    REQUIRE(UN(NtUserSetWinEventHook)); REQUIRE(UN(NtUserUnhookWinEvent));
    HWINEVENTHOOK we = (HWINEVENTHOOK)UN(NtUserSetWinEventHook)(0xA000, 0xA0FF, NULL, NULL, we_proc, 0, 0, 0);
    REQUIRE(we);
    SetLastError(0xDEAD0001);
    CHECK(UN(NtUserUnhookWindowsHookEx)((HHOOK)we) == 0);
    EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);
    CHECK(hk_user_obj_valid(we, 15));
    CHECK(UN(NtUserUnhookWinEvent)(we) == 1);
}
TEST(hooks_winevents_unhook_stale_handle_after_reuse_of_slot) {
    NEED();
    HHOOK a = set_u(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId());
    REQUIRE(a);
    CHECK(U(UnhookWindowsHookEx)(a));
    HHOOK b = set_u(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId());
    REQUIRE(b);
    if (((ULONG_PTR)a & 0xffff) == ((ULONG_PTR)b & 0xffff)) {
        /* the slot was reused: the uniqueness word must have advanced, so the stale value stays invalid */
        CHECK((ULONG_PTR)a != (ULONG_PTR)b);
    }
    SetLastError(0xDEAD0001);
    CHECK(!U(UnhookWindowsHookEx)(a));
    EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);
    CHECK(hk_user_obj_valid(b, 5));
    CHECK(U(UnhookWindowsHookEx)(b));
}

/* ---- CallNextHookEx outside a hook procedure ---- */
TEST(hooks_winevents_callnext_outside_hook_returns_zero_user32) {
    NEED();
    static const int codes[] = { -1, 0, 1, 3, 100, INT_MIN };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        SetLastError(0xDEAD0001);
        LRESULT r = U(CallNextHookEx)(NULL, codes[i], 0x1111, 0x2222);
        CHECKF(r == 0, "code %d -> %lld", codes[i], (long long)r);
    }
}
TEST(hooks_winevents_callnext_outside_hook_returns_zero_nt) {
    NEED();
    REQUIRE(UN(NtUserCallNextHookEx));
    static const int codes[] = { -1, 0, 1, 2, 3, 5, 9, 12, 100 };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        /* NtUserCallNextHookEx: pti->sphkCurrent == 0 -> return 0 before touching any argument */
        LRESULT r = UN(NtUserCallNextHookEx)(codes[i], 0x1111, 0x2222, 0);
        CHECKF(r == 0, "code %d -> %lld", codes[i], (long long)r);
    }
}
TEST(hooks_winevents_callnext_outside_hook_does_not_dereference_lparam) {
    NEED();
    REQUIRE(UN(NtUserCallNextHookEx));
    /* lParam is a pointer for every hook type; outside a hook chain the kernel returns before reading it */
    CHECK(UN(NtUserCallNextHookEx)(0, 0, (LPARAM)0xFFFF800000000000ull, 0) == 0);
    CHECK(UN(NtUserCallNextHookEx)(0, 0, (LPARAM)0xdeadbeef, 1) == 0);
    CHECK(UN(NtUserCallNextHookEx)(HC_ACTION, 0, 0, 0) == 0);
}
TEST(hooks_winevents_callnext_installed_hooks_but_not_inside_one_is_zero) {
    NEED();
    REQUIRE(UN(NtUserCallNextHookEx));
    HHOOK h = set_u(WH_GETMESSAGE, hk_nop_proc, GetCurrentThreadId());
    REQUIRE(h);
    /* an installed hook is not the current hook: pti->sphkCurrent is only set while a hook is being called */
    CHECK(UN(NtUserCallNextHookEx)(0, 0, 0, 0) == 0);
    CHECK(U(UnhookWindowsHookEx)(h));
}
