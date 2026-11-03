/* Wrong-type and forged handles against every destroy path, plus direct win32u stub calls. */
#include "uh.h"

UDECL(int, NtUserDestroyWindow, (HWND));
UDECL(int, NtUserDestroyMenu, (HMENU));
UDECL(int, NtUserDestroyCursor, (HANDLE, ULONG));
UDECL(int, NtUserDestroyAcceleratorTable, (HACCEL));
UDECL(int, NtUserUnhookWindowsHookEx, (HHOOK));
UDECL(int, NtUserUnhookWinEvent, (HWINEVENTHOOK));
UDECL(int, NtUserEndDeferWindowPosEx, (HDWP, BOOL));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));

static const struct uh_desc *const K[] = { &uh_d_wtop, &uh_d_menu, &uh_d_icon, &uh_d_hook, &uh_d_accel, &uh_d_hdwp, &uh_d_winevent };
#define NK (sizeof K / sizeof K[0])

/* ---- user32 destroy API applied to a handle of every other type ---- */
static void matrix(const struct uh_desc *A) {
    UH_NEED();
    for (unsigned i = 0; i < NK; i++) {
        const struct uh_desc *B = K[i];
        if (B->type == A->type) continue;
        HANDLE h = B->create();
        if (!h) { t_note("cannot create %s", B->name); continue; }
        uh_entry_t e0, e1;
        CHECK(uh_read_h(h, &e0));
        BOOL r; UH_CALL_ERR(r, A->destroy(h));
        CHECKF(!r, "%s destroy API accepted a %s handle", A->name, B->name);
        EXPECT_ERR(A->err);
        CHECK(uh_read_h(h, &e1));
        EXPECT_EQ(e1.type, e0.type); EXPECT_EQ(e1.uniq, e0.uniq); CHECK((e1.flags & 1) == 0);
        B->destroy(h);
    }
}
#define MX(tag, d) TEST(user_handle_table_wrong_type_##tag##_rejects_other_types) { matrix(&d); }
MX(window, uh_d_wtop) MX(menu, uh_d_menu) MX(icon, uh_d_icon) MX(hook, uh_d_hook)
MX(accel, uh_d_accel) MX(hdwp, uh_d_hdwp) MX(winevent, uh_d_winevent)

TEST(user_handle_table_wrong_type_iswindow_false_for_other_types) {
    UH_NEED();
    for (unsigned i = 0; i < NK; i++) {
        if (K[i]->type == UT_WINDOW) continue;
        HANDLE h = K[i]->create(); if (!h) continue;
        CHECKF(!U(IsWindow)((HWND)h), "IsWindow accepted a %s handle", K[i]->name); /* IsWindow requires bType == 1 */
        K[i]->destroy(h);
    }
}
TEST(user_handle_table_wrong_type_ismenu_false_and_sets_579) {
    UH_NEED();
    for (unsigned i = 0; i < NK; i++) {
        if (K[i]->type == UT_MENU) continue;
        HANDLE h = K[i]->create(); if (!h) continue;
        BOOL r; UH_CALL_ERR(r, U(IsMenu)((HMENU)h));
        CHECKF(!r, "IsMenu accepted a %s handle", K[i]->name);
        EXPECT_ERR(0x579); /* user32!HMValidateHandle(type 2) -> ERROR_INVALID_MENU_HANDLE */
        K[i]->destroy(h);
    }
}
TEST(user_handle_table_ismenu_forged_menu_uniq_sets_579) {
    UH_NEED();
    HMENU m = U(CreateMenu)(); REQUIRE(m);
    HMENU f = (HMENU)uh_forge(m, UH_UNIQ(m) == 5 ? 6 : 5);
    BOOL r; UH_CALL_ERR(r, U(IsMenu)(f));
    CHECK(!r); EXPECT_ERR(0x579);
    CHECK(U(IsMenu)(m));
    U(DestroyMenu)(m);
}

/* ---- direct win32u stubs ---- */
static int nt_destroy(const struct uh_desc *d, HANDLE h, int *have) {
    *have = 1;
    switch (d->type) {
    case UT_WINDOW:       if (!UN(NtUserDestroyWindow)) break;            return UN(NtUserDestroyWindow)((HWND)h);
    case UT_MENU:         if (!UN(NtUserDestroyMenu)) break;              return UN(NtUserDestroyMenu)((HMENU)h);
    case UT_CURSOR:       if (!UN(NtUserDestroyCursor)) break;            return UN(NtUserDestroyCursor)(h, 1);
    case UT_SETWINDOWPOS: if (!UN(NtUserEndDeferWindowPosEx)) break;      return UN(NtUserEndDeferWindowPosEx)((HDWP)h, 0);
    case UT_HOOK:         if (!UN(NtUserUnhookWindowsHookEx)) break;      return UN(NtUserUnhookWindowsHookEx)((HHOOK)h);
    case UT_ACCEL:        if (!UN(NtUserDestroyAcceleratorTable)) break;  return UN(NtUserDestroyAcceleratorTable)((HACCEL)h);
    case UT_WINEVENT:     if (!UN(NtUserUnhookWinEvent)) break;           return UN(NtUserUnhookWinEvent)((HWINEVENTHOOK)h);
    }
    *have = 0; return 0;
}
static const struct { const char *n; long num; } stub_nums[] = {
    { "NtUserDestroyWindow", 4250 }, { "NtUserDestroyMenu", 4304 }, { "NtUserDestroyCursor", 4249 },
    { "NtUserDestroyAcceleratorTable", 4337 }, { "NtUserUnhookWindowsHookEx", 4206 },
    { "NtUserUnhookWinEvent", 4344 }, { "NtUserEndDeferWindowPosEx", 4134 }, { "NtUserGetGuiResources", 5131 },
    { "NtUserCallOneParam", 4098 }, { "NtUserProcessConnect", 4329 }, { "NtUserCreateWindowEx", 4212 },
    { "NtUserCreateAcceleratorTable", 4324 }, { "NtUserSetWindowsHookEx", 4233 }, { "NtUserSetWinEventHook", 4343 },
    { "NtUserCreateInputContext", 5053 }, { "NtUserDestroyInputContext", 5066 }, { "NtUserSetTimer", 4120 },
    { "NtUserKillTimer", 4123 }, { "NtUserEnumDisplayMonitors", 4170 },
};
TEST(user_handle_table_nt_stub_service_numbers) {
    for (unsigned i = 0; i < sizeof stub_nums / sizeof *stub_nums; i++) {
        FARPROC p = t_proc_in("win32u.dll", stub_nums[i].n);
        if (!p) { t_skip("win32u.dll missing %s", stub_nums[i].n); continue; }
        CHECK(k_stub_valid(p));
        if (t_is_wine()) continue;
        CHECKF(k_stub_number(p) == stub_nums[i].num, "%s got %ld want %ld", stub_nums[i].n, k_stub_number(p), stub_nums[i].num);
    }
}
static void nt_valid(const struct uh_desc *d) {
    UH_NEED();
    if (d->setup) d->setup();
    HANDLE h = d->create(); if (!h) { t_skip("cannot create"); goto out; }
    int have; uh_entry_t e;
    int r = nt_destroy(d, h, &have);
    if (!have) { t_skip("stub missing"); d->destroy(h); goto out; }
    /* NtUserEndDeferWindowPosEx on an empty DWP returns whatever the kernel decides,
     * so only types whose destroy path is a plain TRUE are asserted. */
    if (d->destroy_ok) CHECK(r != 0);
    CHECK(uh_read_h(h, &e));
    CHECK(e.type == 0 || (e.flags & 1));
out:
    if (d->teardown) d->teardown();
}
static void nt_forged(const struct uh_desc *d) {
    UH_NEED();
    if (d->setup) d->setup();
    HANDLE h = d->create(); if (!h) { t_skip("cannot create"); goto out; }
    HANDLE f = uh_forge(h, UH_UNIQ(h) == 7 ? 8 : 7);
    int have; SetLastError(UH_SENTINEL);
    int r = nt_destroy(d, f, &have);
    if (!have) { t_skip("stub missing"); d->destroy(h); goto out; }
    DWORD err = GetLastError();
    CHECK(r == 0);
    EXPECT_EQ(err, d->err);
    uh_entry_t e; CHECK(uh_read_h(h, &e)); EXPECT_EQ(e.type, d->type); EXPECT_EQ(e.uniq, UH_UNIQ(h));
    d->destroy(h);
out:
    if (d->teardown) d->teardown();
}
static void nt_null(const struct uh_desc *d) {
    UH_NEED();
    int have; SetLastError(UH_SENTINEL);
    int r = nt_destroy(d, NULL, &have);
    if (!have) { t_skip("stub missing"); return; }
    CHECK(r == 0);
    EXPECT_EQ(GetLastError(), d->err); /* HMValidateHandle: index 0 is the reserved entry (type 0) */
}
#define NT(tag, d) \
    TEST(user_handle_table_nt_##tag##_destroy_valid)  { nt_valid(&d); } \
    TEST(user_handle_table_nt_##tag##_destroy_forged) { nt_forged(&d); } \
    TEST(user_handle_table_nt_##tag##_destroy_null)   { nt_null(&d); }
NT(window, uh_d_wtop) NT(menu, uh_d_menu) NT(icon, uh_d_icon) NT(hook, uh_d_hook)
NT(accel, uh_d_accel) NT(hdwp, uh_d_hdwp) NT(winevent, uh_d_winevent)

/* NtUserDestroyCursor(h, mode): mode 0 requires ownership, 1 skips it, >1 is ERROR_INVALID_PARAMETER. */
TEST(user_handle_table_nt_destroycursor_mode_validation) {
    UH_NEED(); REQUIRE(UN(NtUserDestroyCursor));
    HANDLE h = uh_d_icon.create(); REQUIRE(h);
    for (ULONG m = 2; m < 6; m++) {
        SetLastError(UH_SENTINEL);
        CHECK(UN(NtUserDestroyCursor)(h, m) == 0);
        EXPECT_ERR(0x57);
    }
    uh_entry_t e; CHECK(uh_read_h(h, &e)); EXPECT_EQ(e.type, UT_CURSOR);
    uh_d_icon.destroy(h);
}
TEST(user_handle_table_nt_destroycursor_mode0_owner_ok) {
    UH_NEED(); REQUIRE(UN(NtUserDestroyCursor));
    HANDLE h = uh_d_icon.create(); REQUIRE(h);
 /* the owning PROCESSINFO == current Win32Process => allowed */
    CHECK(UN(NtUserDestroyCursor)(h, 0) != 0);
    uh_entry_t e; CHECK(uh_read_h(h, &e)); CHECK(e.type == 0 || (e.flags & 1));
}
TEST(user_handle_table_nt_destroycursor_invalid_before_mode) {
    UH_NEED(); REQUIRE(UN(NtUserDestroyCursor));
    SetLastError(UH_SENTINEL);
    CHECK(UN(NtUserDestroyCursor)(NULL, 2) == 0);
    EXPECT_ERR(0x57a); /* handle validated (type 3) before the mode argument */
}

/* ---- wildcard uniq against the kernel's ValidateHwnd ---- */
TEST(user_handle_table_wildcard_window_accepted_by_user32_and_kernel) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    HWND wc = (HWND)uh_forge(w, 0xffff);
    CHECK(U(IsWindow)(wc));
    DWORD pid = 0;
    /* IsWindow and DestroyWindow take the wildcard handle, but GetWindowThreadProcessId rejects it: 0 for both ids and
     * ERROR_INVALID_WINDOW_HANDLE (Windows 10) */
    SetLastError(UH_SENTINEL);
    DWORD tid = U(GetWindowThreadProcessId)(wc, &pid);
    EXPECT_EQ(tid, 0); EXPECT_EQ(pid, 0);
    EXPECT_ERR(0x578);
    CHECK(U(DestroyWindow)(wc));
    CHECK(!U(IsWindow)(w));
}
TEST(user_handle_table_zero_hiword_window_rejected) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    HWND z = (HWND)uh_forge(w, 0);
    CHECK(!U(IsWindow)(z));
    DWORD pid = 77; SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetWindowThreadProcessId)(z, &pid), 0);
    EXPECT_ERR(0x578);
    U(DestroyWindow)(w);
}
TEST(user_handle_table_stale_window_getwindowthreadprocessid_fails) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    U(DestroyWindow)(w);
    DWORD pid = 77; SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetWindowThreadProcessId)(w, &pid), 0); /* ValidateHwnd -> 0x578 */
    EXPECT_ERR(0x578);
}
TEST(user_handle_table_stale_window_ops_set_invalid_window_handle) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    U(DestroyWindow)(w);
    BOOL r;
    UH_CALL_ERR(r, U(EnableWindow)(w, TRUE)); CHECK(!r); EXPECT_ERR(0x578);
    UH_CALL_ERR(r, U(SetWindowPos)(w, NULL, 0, 0, 1, 1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)); CHECK(!r); EXPECT_ERR(0x578);
    UH_CALL_ERR(r, U(ShowWindow)(w, SW_HIDE)); CHECK(!r); EXPECT_ERR(0x578);
    UH_CALL_ERR(r, U(DestroyWindow)(w)); CHECK(!r); EXPECT_ERR(0x578);
}

/* ---- NtUserCallOneParam(hwnd, 0x38): the slow path of user32!IsWindow/HMValidateHandle ---- */
TEST(user_handle_table_callone_getwndptr_matches_fast_path) {
    UH_NEED(); REQUIRE(UN(NtUserCallOneParam));
    HWND w = uh_make_top(); REQUIRE(w);
    ULONG_PTR p = UN(NtUserCallOneParam)((ULONG_PTR)w, 0x38);
    CHECK(p != 0);
    void *fast = uh_client_ptr(w);
    if (fast) EXPECT_EQ(p, (ULONG_PTR)fast); /* both branches of HMValidateHandle return the same pwnd */
    else t_note("fast path unavailable (desktop heap guard mismatch)");
    unsigned long long q[2] = {0, 0};
    CHECK(k_try_read((void *)p, q, sizeof q));
    EXPECT_EQ((unsigned)q[0], (unsigned)(ULONG_PTR)w);
    U(DestroyWindow)(w);
}
TEST(user_handle_table_iswindow_cache_slot_does_not_bypass_destroy) {
    UH_NEED();
    /* IsWindow short-circuits on the TEB cache slot (CI+0x40/0x48): destroying the window must
     * invalidate the answer even though it was the last validated handle. */
    HWND w = uh_make_top(); REQUIRE(w);
    CHECK(U(IsWindow)(w)); CHECK(U(IsWindow)(w));
    U(DestroyWindow)(w);
    CHECK(!U(IsWindow)(w));
}
