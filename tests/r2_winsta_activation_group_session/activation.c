/* Activation objects / activation groups / window groups. These entry points belong to the immersive
 * activation manager (IAM): NtUserCreateWindowGroup / DeleteWindowGroup / EnableWindowGroupPolicy /
 * SetWindowGroup / SetActivationFilter all begin with an access check on the calling thread, which is true only for
 * the IAM thread , so an ordinary process gets ERROR_ACCESS_DENIED. The activation object
 * calls take a LUID, look it up in the manager's hash (GetActivationObjectFromLuid) and report
 * STATUS_NOT_FOUND (-> ERROR_NOT_FOUND 1168) for an unknown one. */
#include "r2.h"

/* ---- activation objects ------------------------------------------------------------------------ */
static const ULONGLONG unknown_luids[] = { 0, 1, 0x0000000100000000ull, 0xDEADBEEF12345678ull, 0xFFFFFFFFFFFFFFFFull };
#define N_LUIDS 5

RT(activation_object_stubs_are_exported) {
    if (t_is_wine()) { t_skip("Wine win32u lacks these syscalls"); return; }
    CHECK(UN(NtUserCreateActivationObject) != NULL);
    CHECK(UN(NtUserConfigureActivationObject) != NULL);
    CHECK(UN(NtUserQueryActivationObject) != NULL);
    CHECK(UN(NtUserDestroyActivationObject) != NULL);
    CHECK(UN(NtUserCreateActivationGroup) != NULL);
    CHECK(UN(NtUserDestroyActivationGroup) != NULL);
    CHECK(UN(NtUserSetActivationFilter) != NULL);
    CHECK(UN(NtUserCreateWindowGroup) != NULL);
    CHECK(UN(NtUserDeleteWindowGroup) != NULL);
    CHECK(UN(NtUserSetWindowGroup) != NULL);
    CHECK(UN(NtUserGetWindowGroupId) != NULL);
    CHECK(UN(NtUserEnableWindowGroupPolicy) != NULL);
}
RT(activation_object_stubs_have_win32k_service_numbers) {
    static const char *names[] = { "NtUserCreateActivationObject", "NtUserConfigureActivationObject", "NtUserQueryActivationObject",
        "NtUserDestroyActivationObject", "NtUserCreateActivationGroup", "NtUserDestroyActivationGroup", "NtUserSetActivationFilter",
        "NtUserCreateWindowGroup", "NtUserDeleteWindowGroup", "NtUserSetWindowGroup", "NtUserGetWindowGroupId", "NtUserEnableWindowGroupPolicy" };
    for (unsigned i = 0; i < 12; i++) {
        FARPROC p = r2_proc_in("win32u.dll", names[i]);
        if (!p) { t_note("%s not exported", names[i]); continue; }
        if (!k_stub_valid(p)) { t_note("%s: unrecognised stub prologue", names[i]); continue; }
        long n = k_stub_number(p);
        CKF(n >= 0x1000, "%s service number 0x%lx", names[i], n);   /* win32k service table starts at 0x1000 */
    }
}
RT(query_activation_object_unknown_luid_not_found) {
    REQUIRE(UN(NtUserQueryActivationObject));
    for (unsigned i = 0; i < N_LUIDS; i++) {
        ULONGLONG luid = unknown_luids[i];
        DWORD out[4] = { 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC };
        SET_ERR(SENT);
        UN(NtUserQueryActivationObject)(&luid, out);
        CKF(GetLastError() == ERR_NOT_FOUND, "luid=%llx gle=%lu", (unsigned long long)luid, GetLastError());
        CKF(out[0] == 0xCCCCCCCC && out[3] == 0xCCCCCCCC, "info written for unknown luid");
        CKF(luid == unknown_luids[i], "luid input modified");
    }
}
RT(configure_activation_object_unknown_luid_not_found) {
    REQUIRE(UN(NtUserConfigureActivationObject));
    for (unsigned i = 0; i < N_LUIDS; i++) {
        ULONGLONG luid = unknown_luids[i];
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserConfigureActivationObject)(&luid, 0, 0, 0);
        CKF(R_B(r) == 0, "luid=%llx r=%llx", (unsigned long long)luid, (unsigned long long)r);
        CKF(GetLastError() == ERR_NOT_FOUND, "luid=%llx gle=%lu", (unsigned long long)luid, GetLastError());
    }
}
RT(configure_activation_object_state_arguments_do_not_matter_for_unknown_luid) {
    REQUIRE(UN(NtUserConfigureActivationObject));
    static const DWORD v[][3] = { {0,1,1}, {1,0,2}, {2,2,0}, {3,1,0}, {1,3,3} };
    for (unsigned i = 0; i < 5; i++) {
        ULONGLONG luid = 0xDEADBEEF12345678ull;
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserConfigureActivationObject)(&luid, v[i][0], v[i][1], v[i][2])) == 0, "case %u", i);
        CKF(GetLastError() == ERR_NOT_FOUND, "case %u gle=%lu", i, GetLastError());
    }
}
RT(destroy_activation_object_unknown_luid_not_found) {
    REQUIRE(UN(NtUserDestroyActivationObject));
    for (unsigned i = 0; i < N_LUIDS; i++) {
        ULONGLONG luid = unknown_luids[i];
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserDestroyActivationObject)(&luid);
        CKF(R_B(r) == 0, "luid=%llx r=%llx", (unsigned long long)luid, (unsigned long long)r);
        CKF(GetLastError() == ERR_NOT_FOUND, "luid=%llx gle=%lu", (unsigned long long)luid, GetLastError());
    }
}
RT(destroy_activation_object_twice_still_not_found) {
    REQUIRE(UN(NtUserDestroyActivationObject));
    ULONGLONG luid = 0x0000000200000001ull;
    for (int i = 0; i < 2; i++) {
        SET_ERR(SENT);
        CK(R_B(UN(NtUserDestroyActivationObject)(&luid)) == 0);
        EXPECT_ERR(ERR_NOT_FOUND);
    }
}
/* NtUserCreateActivationObject: a NULL first argument (the window/handle the object is created for) is
 * ERROR_INVALID_PARAMETER before the other arguments are looked at. */
RT(create_activation_object_null_first_argument) {
    REQUIRE(UN(NtUserCreateActivationObject));
    ULONGLONG in = 0, outluid = 0xCCCCCCCCCCCCCCCCull;
    SET_ERR(SENT);
    UN(NtUserCreateActivationObject)(NULL, &in, &outluid, 0);
    EXPECT_ERR(ERR_INVALID_PARAM);
    CK(outluid == 0xCCCCCCCCCCCCCCCCull);   /* LUID is written only on success */
}
RT(create_activation_object_null_first_argument_flags_ignored) {
    REQUIRE(UN(NtUserCreateActivationObject));
    static const DWORD flags[] = { 0, 1, 2, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < 5; i++) {
        ULONGLONG in = 0x1234, outluid = 0xCCCCCCCCCCCCCCCCull;
        SET_ERR(SENT);
        UN(NtUserCreateActivationObject)(NULL, &in, &outluid, flags[i]);
        CKF(GetLastError() == ERR_INVALID_PARAM, "flags=%lx gle=%lu", flags[i], GetLastError());
        CKF(outluid == 0xCCCCCCCCCCCCCCCCull, "flags=%lx luid written", flags[i]);
    }
}
RT(query_after_failed_create_still_not_found) {
    REQUIRE(UN(NtUserCreateActivationObject)); REQUIRE(UN(NtUserQueryActivationObject));
    ULONGLONG in = 0, outluid = 0;
    UN(NtUserCreateActivationObject)(NULL, &in, &outluid, 0);
    DWORD info[4];
    SET_ERR(SENT);
    UN(NtUserQueryActivationObject)(&outluid, info);
    EXPECT_ERR(ERR_NOT_FOUND);
}

/* The Create/DestroyActivationGroup service entries share one body (same RVA in win32kbase): it takes the user
 * critical section shared, and returns 0 without raising an error. */
RT(create_activation_group_is_inert_returns_zero) {
    REQUIRE(UN(NtUserCreateActivationGroup));
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserCreateActivationGroup)(0, 0, 0, 0);
    CKF(r == 0, "r=%llx", (unsigned long long)r);
    EXPECT_ERR(SENT);
}
RT(destroy_activation_group_is_inert_returns_zero) {
    REQUIRE(UN(NtUserDestroyActivationGroup));
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserDestroyActivationGroup)(0, 0, 0, 0);
    CKF(r == 0, "r=%llx", (unsigned long long)r);
    EXPECT_ERR(SENT);
}
RT(activation_group_calls_ignore_their_arguments) {
    REQUIRE(UN(NtUserCreateActivationGroup)); REQUIRE(UN(NtUserDestroyActivationGroup));
    static const ULONG_PTR v[] = { 1, 0x7777770, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFull };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        CKF(UN(NtUserCreateActivationGroup)(v[i], v[i], v[i], v[i]) == 0, "arg=%llx", (unsigned long long)v[i]);
        CKF(UN(NtUserDestroyActivationGroup)(v[i], v[i], v[i], v[i]) == 0, "arg=%llx", (unsigned long long)v[i]);
        CKF(GetLastError() == SENT, "arg=%llx gle=%lu", (unsigned long long)v[i], GetLastError());
    }
}

/* ---- SetActivationFilter (IAM thread only) ------------------------------------------------------------- */
RT(set_activation_filter_denied_before_hwnd_validation) {
    REQUIRE(UN(NtUserSetActivationFilter));
    /* the thread access check runs before window validation, so even a bad handle reports access denied */
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserSetActivationFilter)((HWND)r2_bad_hwnds[i], 1)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(set_activation_filter_denied_for_valid_window) {
    REQUIRE(UN(NtUserSetActivationFilter));
    NEED_WIN(w);
    static const DWORD v[] = { 0, 1, 2, 0xFFFFFFFF };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserSetActivationFilter)(w, v[i])) == 0, "filter=%lx", v[i]);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "filter=%lx gle=%lu", v[i], GetLastError());
    }
    END_WIN(w);
}

/* ---- window groups ----------------------------------------------------------------------------------- */
RT(create_window_group_denied_outside_iam) {
    REQUIRE(UN(NtUserCreateWindowGroup));
    DWORD id = 0xCCCCCCCC;
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserCreateWindowGroup)(&id);
    CKF(R_B(r) == 0, "r=%llx", (unsigned long long)r);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    CKF(id == 0xCCCCCCCC, "id written: 0x%lx", id);   /* the out pointer is only touched past the IAM check */
}
RT(create_window_group_repeatable_never_allocates) {
    REQUIRE(UN(NtUserCreateWindowGroup));
    for (int i = 0; i < 8; i++) {
        DWORD id = 0xCCCCCCCC;
        SET_ERR(SENT);
        CK(R_B(UN(NtUserCreateWindowGroup)(&id)) == 0);
        CK(GetLastError() == ERR_ACCESS_DENIED);
        CK(id == 0xCCCCCCCC);
    }
}
RT(delete_window_group_denied_outside_iam) {
    REQUIRE(UN(NtUserDeleteWindowGroup));
    static const DWORD ids[] = { 0, 1, 2, 0x1000, 0xFFFFFFFF };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserDeleteWindowGroup)(ids[i])) == 0, "id=%lx", ids[i]);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "id=%lx gle=%lu", ids[i], GetLastError());
    }
}
RT(enable_window_group_policy_denied_outside_iam) {
    REQUIRE(UN(NtUserEnableWindowGroupPolicy));
    static const DWORD ids[] = { 0, 1, 0xFFFFFFFF };
    static const ULONG_PTR flags[] = { 0, 1, 2, 0xFFFFFFFF };
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 4; j++) {
            SET_ERR(SENT);
            CKF(R_B(UN(NtUserEnableWindowGroupPolicy)(ids[i], 1, flags[j])) == 0, "id=%lx flags=%llx", ids[i], (unsigned long long)flags[j]);
            CKF(GetLastError() == ERR_ACCESS_DENIED, "id=%lx flags=%llx gle=%lu", ids[i], (unsigned long long)flags[j], GetLastError());
        }
}
/* NtUserSetWindowGroup validates the window first: bad handle -> 1400, desktop/switch windows (FNID 0x29d/0x29f)
 * -> plain 0 with no error, otherwise the IAM check -> 5. */
RT(set_window_group_invalid_hwnds) {
    REQUIRE(UN(NtUserSetWindowGroup));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserSetWindowGroup)(1, (HWND)r2_bad_hwnds[i], 0, 0)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(set_window_group_desktop_window_silently_rejected) {
    REQUIRE(UN(NtUserSetWindowGroup)); REQUIRE(U(GetDesktopWindow));
    SET_ERR(SENT);
    CK(R_B(UN(NtUserSetWindowGroup)(1, U(GetDesktopWindow)(), 0, 0)) == 0);
    EXPECT_ERR(SENT);
}
RT(set_window_group_valid_window_denied_outside_iam) {
    REQUIRE(UN(NtUserSetWindowGroup));
    NEED_WIN(w);
    static const DWORD opts[] = { 0, 1, 3, 4 };
    static const DWORD roles[] = { 0, 1, 2, 3 };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserSetWindowGroup)(1, w, opts[i], roles[i])) == 0, "opts=%lx role=%lx", opts[i], roles[i]);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "opts=%lx role=%lx gle=%lu", opts[i], roles[i], GetLastError());
    }
    END_WIN(w);
}
RT(set_window_group_child_window_denied_outside_iam) {
    REQUIRE(UN(NtUserSetWindowGroup));
    NEED_WIN(w);
    HWND c = U(CreateWindowExW)(0, L"STATIC", L"c", WS_CHILD, 0, 0, 4, 4, w, NULL, NULL, NULL);
    if (!c) { t_skip("child window creation failed"); END_WIN(w); return; }
    SET_ERR(SENT);
    CK(R_B(UN(NtUserSetWindowGroup)(1, c, 0, 0)) == 0);
    CK(GetLastError() == ERR_ACCESS_DENIED);   /* the IAM check precedes the top-level check */
    U(DestroyWindow)(c);
    END_WIN(w);
}
RT(get_window_group_id_invalid_hwnds) {
    REQUIRE(UN(NtUserGetWindowGroupId));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        DWORD id = 0xCCCCCCCC;
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserGetWindowGroupId)((HWND)r2_bad_hwnds[i], &id)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
        CKF(id == 0xCCCCCCCC, "id written for invalid hwnd");
    }
}
RT(get_window_group_id_valid_window_succeeds_and_writes) {
    REQUIRE(UN(NtUserGetWindowGroupId));
    NEED_WIN(w);
    DWORD id = 0xCCCCCCCC;
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserGetWindowGroupId)(w, &id);
    CKF(R_D(r) == 1, "r=%llx gle=%lu", (unsigned long long)r, GetLastError());
    CKF(id != 0xCCCCCCCC, "id not written");
    t_note("window group id of an ungrouped window: 0x%lx", id);
    END_WIN(w);
}
RT(get_window_group_id_is_stable_across_calls) {
    REQUIRE(UN(NtUserGetWindowGroupId));
    NEED_WIN(w);
    DWORD a = 1, b = 2;
    CK(R_D(UN(NtUserGetWindowGroupId)(w, &a)) == 1);
    CK(R_D(UN(NtUserGetWindowGroupId)(w, &b)) == 1);
    CKF(a == b, "%lx vs %lx", a, b);
    END_WIN(w);
}
RT(get_window_group_id_peer_windows_share_default_group) {
    REQUIRE(UN(NtUserGetWindowGroupId));
    NEED_WIN(a); HWND b = r2_win();
    if (!b) { t_skip("second window failed"); END_WIN(a); return; }
    DWORD ia = 1, ib = 2;
    CK(R_D(UN(NtUserGetWindowGroupId)(a, &ia)) == 1);
    CK(R_D(UN(NtUserGetWindowGroupId)(b, &ib)) == 1);
    CKF(ia == ib, "%lx vs %lx", ia, ib);
    END_WIN(a); END_WIN(b);
}
RT(get_window_group_id_desktop_window_readable) {
    REQUIRE(UN(NtUserGetWindowGroupId)); REQUIRE(U(GetDesktopWindow));
    DWORD id = 0xCCCCCCCC;
    CKF(R_D(UN(NtUserGetWindowGroupId)(U(GetDesktopWindow)(), &id)) == 1, "gle=%lu", GetLastError());
    CK(id != 0xCCCCCCCC);
}

/* ---- shared window data: no kernel implementation behind these two service entries in win32kbase/win32kfull;
 * win32k.sys dispatches through an import slot, so only absence of a fault can be asserted. ---------------- */
RT(shared_window_data_calls_do_not_fault) {
    REQUIRE(UN(NtUserGetSharedWindowData)); REQUIRE(UN(NtUserSetSharedWindowData));
    NEED_WIN(w);
    ULONG_PTR r1 = UN(NtUserGetSharedWindowData)((ULONG_PTR)w, 0, 0, 0);
    ULONG_PTR r2 = UN(NtUserSetSharedWindowData)((ULONG_PTR)w, 0, 0, 0);
    t_note("GetSharedWindowData -> 0x%llx  SetSharedWindowData -> 0x%llx gle=%lu", (unsigned long long)r1, (unsigned long long)r2, GetLastError());
    CHECK(U(IsWindow)(w));
    END_WIN(w);
}
