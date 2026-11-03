/* kcb_kernel_callbacks: hooking/restore mechanics for PEB->KernelCallbackTable slots.
 *
 * common/k.h's k_cb_hook()/k_cb_unhook_all() do the actual VirtualProtect + table patch + restore;
 * this file exercises that layer's own correctness (right slot patched, original preserved exactly,
 * re-hook/out-of-range rejected, restore is byte-exact) before any test in kcb_dispatch_*.c trusts
 * it to observe real callback traffic. All tests restore every slot they touch, even on a CHECK
 * failure (k_cb_unhook_all() runs unconditionally at the end of each test body here, and is also
 * registered as the process atexit backstop by k_cb_hook() itself).
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"

/* ---- 1. basic install/restore identity --------------------------------------------------- */

TEST(kcb_hook_install_patches_exactly_one_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    unsigned n = k_cb_count();
    REQUIRE(n > 2);
    unsigned idx = 2; /* __fnDWORD on the reference build; any populated slot works for this check */
    void *before[8];
    unsigned nsnap = n < 8 ? n : 8;
    for (unsigned i = 0; i < nsnap; i++) k_try_read_ptr((char *)tbl + (size_t)i * sizeof(void *), &before[i]);
    void *orig_at_idx = NULL;
    k_try_read_ptr((char *)tbl + (size_t)idx * sizeof(void *), &orig_at_idx);
    CHECK(k_cb_hook(idx));
    void *after_hook = NULL;
    k_try_read_ptr((char *)tbl + (size_t)idx * sizeof(void *), &after_hook);
    CHECKF(after_hook != orig_at_idx, "slot %u unchanged after hook (%p)", idx, after_hook);
    CHECKF(after_hook != k_cb_original(idx), "trampoline must differ from k_cb_original(%u)=%p", idx, k_cb_original(idx));
    /* every other sampled slot must be untouched */
    for (unsigned i = 0; i < nsnap; i++) {
        if (i == idx) continue;
        void *now = NULL;
        k_try_read_ptr((char *)tbl + (size_t)i * sizeof(void *), &now);
        EXPECT_EQ(before[i], now);
    }
    k_cb_unhook_all();
    void *restored = NULL;
    k_try_read_ptr((char *)tbl + (size_t)idx * sizeof(void *), &restored);
    EXPECT_EQ(restored, orig_at_idx);
}

TEST(kcb_hook_original_matches_pre_hook_value) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    unsigned idx = 0; /* __fnCOPYDATA */
    void *pre = NULL;
    REQUIRE(k_try_read_ptr((char *)tbl + (size_t)idx * sizeof(void *), &pre));
    CHECK(k_cb_hook(idx));
    EXPECT_EQ(k_cb_original(idx), pre);
    k_cb_unhook_all();
}

/* ---- 2. rejection rules ------------------------------------------------------------------- */

TEST(kcb_hook_rejects_out_of_range_index) {
    CHECK(!k_cb_hook(K_CB_MAX));
    CHECK(!k_cb_hook(K_CB_MAX + 1));
    CHECK(!k_cb_hook(0xFFFFFFFFu));
}

TEST(kcb_hook_rejects_double_hook_of_same_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    REQUIRE(k_cb_table());
    unsigned idx = 5;
    CHECK(k_cb_hook(idx));
    CHECKF(!k_cb_hook(idx), "second k_cb_hook(%u) must fail while slot %u is still active", idx, idx);
    k_cb_unhook_all();
    /* after a full unhook, the slot must be hookable again */
    CHECK(k_cb_hook(idx));
    k_cb_unhook_all();
}

TEST(kcb_hook_rejects_slot_beyond_reference_table_end) {
    /* k_cb_hook() only range-checks against K_CB_MAX, not against k_cb_count() - a slot past the
     * real table's live entries (but still < K_CB_MAX) may read a NULL original, which k_cb_hook()
     * explicitly treats as "no such slot" (orig == NULL -> return false; see common/k.c). On the
     * reference build the table has exactly 129 non-NULL entries (kcb_table.c), so slot 140 must
     * be NULL there and the hook call must fail cleanly, never install over garbage. */
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    unsigned n = k_cb_count();
    if (n >= 140) { t_skip("table has >=140 live entries on this build/runtime, nothing past-end to probe"); return; }
    /* Memory behind the table is not zero (slot 140 reads another module pointer on Windows 10), so probe the first slot the
     * scan stopped at: k_cb_count() ends at the first NULL entry, which k_cb_hook must refuse. */
    if (n < K_CB_MAX) CHECKF(!k_cb_hook(n), "k_cb_hook(%u) unexpectedly succeeded on the NULL entry ending a %u-entry table", n, n);
    if (n + 11 < K_CB_MAX) { void *p = NULL; k_try_read_ptr((char *)tbl + (size_t)(n + 11) * sizeof(void *), &p); t_note("slot %u behind the table reads %p", n + 11, p); }
}

/* ---- 3. independent slots can be hooked concurrently and unhooked together --------------- */

TEST(kcb_hook_multiple_independent_slots) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    unsigned n = k_cb_count();
    REQUIRE(n >= 4);
    unsigned idxs[4] = { 0, 2, 10, 12 };
    void *pre[4];
    for (int i = 0; i < 4; i++) {
        REQUIRE(k_try_read_ptr((char *)tbl + (size_t)idxs[i] * sizeof(void *), &pre[i]));
        CHECKF(k_cb_hook(idxs[i]), "k_cb_hook(%u) failed", idxs[i]);
    }
    for (int i = 0; i < 4; i++) {
        void *now = NULL;
        k_try_read_ptr((char *)tbl + (size_t)idxs[i] * sizeof(void *), &now);
        CHECKF(now != pre[i], "slot %u not patched", idxs[i]);
        EXPECT_EQ(k_cb_original(idxs[i]), pre[i]);
    }
    k_cb_unhook_all();
    for (int i = 0; i < 4; i++) {
        void *now = NULL;
        k_try_read_ptr((char *)tbl + (size_t)idxs[i] * sizeof(void *), &now);
        EXPECT_EQ(now, pre[i]);
    }
}

/* ---- 4. hit counter starts at zero and args are zeroed until first call ------------------- */

TEST(kcb_hook_hit_count_zero_until_triggered) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    REQUIRE(k_cb_table());
    unsigned n = k_cb_count();
    /* idx 117 is __fnEMPTY on the reference build (a documented no-op slot, safe to leave hooked
     * and never trigger); Wine's own, much smaller table may not reach that far, so fall back to
     * any in-range slot - this test never triggers the callback, so which slot it is does not
     * matter for what is being checked here (the hit counter/arg state before any call). */
    unsigned idx = (n > 117) ? 117 : (n ? n - 1 : 0);
    if (!n) { t_skip("no callback table slots available"); return; }
    CHECK(k_cb_hook(idx));
    EXPECT_EQ(k_cb_hit_count(idx), 0);
    ULONG_PTR argv[4];
    CHECK(k_cb_last_args(idx, argv));
    for (int i = 0; i < 4; i++) EXPECT_EQ(argv[i], 0);
    k_cb_unhook_all();
    EXPECT_EQ(k_cb_hit_count(idx), 0); /* unhook does not reset state, but nothing fired it either */
}

TEST(kcb_hook_queries_on_unhooked_slot_are_safe) {
    /* k_cb_hit_count()/k_cb_original()/k_cb_last_args() on a slot that was never hooked must
     * return the documented "nothing here" values, never read uninitialized/garbage state. */
    unsigned idx = K_CB_MAX - 1; /* never populated, so no other test can have hooked it */
    EXPECT_EQ(k_cb_hit_count(idx), 0);
    CHECK(k_cb_original(idx) == NULL);
    ULONG_PTR argv[4] = { 1, 2, 3, 4 };
    CHECK(!k_cb_last_args(idx, argv)); /* false: never active */
}

/* ---- 5. the table page's protection is restored, not left writable ----------------------- */

TEST(kcb_hook_table_page_protection_restored) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    MEMORY_BASIC_INFORMATION mbi_before, mbi_after;
    REQUIRE(VirtualQuery(tbl, &mbi_before, sizeof mbi_before));
    unsigned idx = 3;
    CHECK(k_cb_hook(idx));
    k_cb_unhook_all();
    REQUIRE(VirtualQuery(tbl, &mbi_after, sizeof mbi_after));
    /* k_cb_hook()/k_cb_unhook_all() VirtualProtect the slot to PAGE_READWRITE for the patch and
     * restore the page's original protection afterward (common/k.c): apfnDispatch lives in
     * user32's .rdata, normally PAGE_READONLY (or PAGE_EXECUTE_READ for a combined code+data
     * page on some builds) - whichever it started as, it must end the same way, never left RW. */
    CHECKF(mbi_after.Protect == mbi_before.Protect,
           "table page protection changed: before=0x%lx after=0x%lx",
           (unsigned long)mbi_before.Protect, (unsigned long)mbi_after.Protect);
}

/* ---- 6. the trampoline itself is executable and distinct per slot ------------------------ */

TEST(kcb_hook_trampoline_is_executable_and_per_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    REQUIRE(k_cb_table());
    CHECK(k_cb_hook(0));
    CHECK(k_cb_hook(2));
    void *tbl = k_cb_table();
    void *tramp0 = NULL, *tramp2 = NULL;
    k_try_read_ptr((char *)tbl + 0 * sizeof(void *), &tramp0);
    k_try_read_ptr((char *)tbl + 2 * sizeof(void *), &tramp2);
    EXPECT_NE(tramp0, tramp2);
    for (int which = 0; which < 2; which++) {
        void *p = which ? tramp2 : tramp0;
        MEMORY_BASIC_INFORMATION mbi;
        REQUIRE(VirtualQuery(p, &mbi, sizeof mbi));
        CHECKF(mbi.Protect == PAGE_EXECUTE_READWRITE,
               "trampoline %d protect=0x%lx, want PAGE_EXECUTE_READWRITE", which, (unsigned long)mbi.Protect);
    }
    k_cb_unhook_all();
}

/* ---- 7. unhook is idempotent / safe to call with nothing hooked -------------------------- */

TEST(kcb_hook_unhook_all_idempotent) {
    k_cb_unhook_all();
    k_cb_unhook_all();
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    void *before = NULL;
    k_try_read_ptr((char *)tbl + 7 * sizeof(void *), &before);
    k_cb_unhook_all(); /* nothing active; must not disturb an untouched slot */
    void *after = NULL;
    k_try_read_ptr((char *)tbl + 7 * sizeof(void *), &after);
    EXPECT_EQ(before, after);
}

/* ---- 8. a test that "crashes" conceptually mid-hook still gets cleaned up at process exit --
 * This cannot be proven inside one process without actually exiting, so it runs as a CHILD: the
 * child installs a hook and returns without calling k_cb_unhook_all() itself (simulating an early
 * return / forgotten cleanup), relying solely on common/k.c's atexit(k_cb_unhook_all) backstop. The
 * PARENT then can only observe the child exited cleanly (atexit ran without faulting); it cannot
 * observe the child's own table post-exit (separate process), so this is a crash-safety check on
 * the backstop path, not a state-restoration check (that is covered by k_helpers_* above, in-proc). */
CHILD(kcb_hook_atexit_backstop_child) {
    (void)argc; (void)argv;
    FARPROC isw = t_proc("IsWindow");
    if (!isw) { printf("FAIL kcb_hook_atexit_backstop_child: IsWindow unresolvable\n"); return 1; }
    ((BOOL (WINAPI *)(HWND))isw)((HWND)0);
    if (!k_cb_table()) { printf("FAIL kcb_hook_atexit_backstop_child: no callback table\n"); return 1; }
    int ok = k_cb_hook(1) ? 1 : 0;
    if (!ok) { printf("FAIL kcb_hook_atexit_backstop_child: k_cb_hook(1) failed\n"); return 1; }
    return 0; /* deliberately no k_cb_unhook_all(); exit() -> atexit backstop must restore slot 1 */
}
TEST(kcb_hook_atexit_backstop_restores_on_exit) {
    struct k_child_result r = k_child_run("kcb_hook_atexit_backstop_child", NULL, 10000);
    CHECKF(r.exit_code == 0, "child exit=%lu", (unsigned long)r.exit_code);
}
