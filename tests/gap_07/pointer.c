/* gap_07 pointer-input syscalls (direct stubs): argument validation of the pointer device queries as decoded from
 * win32kfull/win32kbase, plus count/size negotiation protocols. A VM usually has no touch or pen digitizer, so
 * checks that depend on the number of devices are conditional on the reported count. */
#include "g7.h"

#define DEVINFO_SIZE 0x438   /* stride used by NtUserGetPointerDevices */
static HANDLE bogus_dev(void) { return (HANDLE)(ULONG_PTR)0x7ffe1234; }

/* ---- NtUserGetPointerDevices ------------------------------------------------------------------------------- */
TEST(gap7_ptrdevs_null_count_fails_57) {
    NtUserGetPointerDevices_pfn f = G7N(NtUserGetPointerDevices); REQUIRE(f);
    SetLastError(0);
    EXPECT_EQ(f(NULL, NULL), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    static char buf[DEVINFO_SIZE];
    SetLastError(0);
    EXPECT_EQ(f(NULL, buf), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_ptrdevs_count_query_succeeds_and_is_stable) {
    NtUserGetPointerDevices_pfn f = G7N(NtUserGetPointerDevices); REQUIRE(f);
    UINT a = 0xdeadbeef, b = 0xdeadbeef;
    EXPECT_EQ(f(&a, NULL), TRUE);          /* devices == NULL: only the count is written */
    EXPECT_EQ(f(&b, NULL), TRUE);
    EXPECT_EQ(a, b);
    CHECK(a < 1000);
    t_note("pointer device count %u", a);
}
TEST(gap7_ptrdevs_zero_capacity_with_buffer) {
    NtUserGetPointerDevices_pfn f = G7N(NtUserGetPointerDevices); REQUIRE(f);
    UINT n = 0; f(&n, NULL);
    static char buf[DEVINFO_SIZE]; memset(buf, 0x5a, sizeof buf);
    UINT cap = 0;
    SetLastError(0);
    EXPECT_EQ(f(&cap, buf), FALSE);
    if (n == 0) {
        EXPECT_ERR(0x3bc3);                /* capacity 0 or no devices: the dedicated "no data" error */
        EXPECT_EQ(cap, 0);
    } else {
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);   /* *count < devices: required count is written back */
        EXPECT_EQ(cap, n);
    }
    EXPECT_EQ((unsigned char)buf[0], 0x5a);
}
TEST(gap7_ptrdevs_capacity_too_small_reports_required) {
    NtUserGetPointerDevices_pfn f = G7N(NtUserGetPointerDevices); REQUIRE(f);
    UINT n = 0; f(&n, NULL);
    if (n == 0) { t_skip("no pointer devices"); return; }
    UINT cap = n - 1;
    char *buf = (char *)calloc(n + 1, DEVINFO_SIZE);
    SetLastError(0);
    EXPECT_EQ(f(&cap, buf), FALSE);
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    EXPECT_EQ(cap, n);
    free(buf);
}
TEST(gap7_ptrdevs_full_read_matches_per_device_query) {
    NtUserGetPointerDevices_pfn f = G7N(NtUserGetPointerDevices);
    NtUserGetPointerDevice_pfn g = G7N(NtUserGetPointerDevice); REQUIRE(f && g);
    UINT n = 0; f(&n, NULL);
    if (n == 0) { t_skip("no pointer devices"); return; }
    char *buf = (char *)calloc(n, DEVINFO_SIZE);
    UINT cap = n;
    EXPECT_EQ(f(&cap, buf), TRUE);
    EXPECT_EQ(cap, n);
    for (UINT i = 0; i < n; i++) {
        char *e = buf + (size_t)i * DEVINFO_SIZE;
        HANDLE dev = *(HANDLE *)(e + 8);
        CHECK(dev != NULL);
        char one[DEVINFO_SIZE]; memset(one, 0, sizeof one);
        EXPECT_EQ(g(dev, one), TRUE);
        CHECK(memcmp(one, e, DEVINFO_SIZE) == 0);
        /* productString (WCHAR[520]) is NUL terminated */
        WCHAR *ps = (WCHAR *)(e + 0x28); BOOL term = FALSE;
        for (int k = 0; k < 520; k++) if (!ps[k]) { term = TRUE; break; }
        CHECK(term);
    }
    free(buf);
}
TEST(gap7_ptrdevs_user32_route_count_matches) {
    NtUserGetPointerDevices_pfn f = G7N(NtUserGetPointerDevices); REQUIRE(f);
    UINT a = 0, b = 0;
    EXPECT_EQ(f(&a, NULL), TRUE);
    EXPECT_EQ(U(GetPointerDevices)(&b, NULL), TRUE);
    EXPECT_EQ(a, b);
}

/* ---- NtUserGetPointerDevice / Cursors / Properties / Rects ------------------------------------------------- */
TEST(gap7_ptrdev_null_arguments_fail_57) {
    NtUserGetPointerDevice_pfn f = G7N(NtUserGetPointerDevice); REQUIRE(f);
    static char info[DEVINFO_SIZE];
    SetLastError(0); EXPECT_EQ(f(NULL, info), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), NULL), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(NULL, NULL), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_ptrdev_unknown_handle_fails_invalid_handle) {
    NtUserGetPointerDevice_pfn f = G7N(NtUserGetPointerDevice); REQUIRE(f);
    static char info[DEVINFO_SIZE]; memset(info, 0x3c, sizeof info);
    static const ULONG_PTR h[] = { 0x1234, 0x7ffe1234, 0xfffff000 };
    for (unsigned i = 0; i < sizeof h / sizeof *h; i++) {
        SetLastError(0);
        EXPECT_EQ(f((HANDLE)h[i], info), FALSE);   /* ResolveMouseOrPointerDevice fails -> ERROR_INVALID_HANDLE */
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
    EXPECT_EQ((unsigned char)info[0], 0x3c);
    EXPECT_EQ((unsigned char)info[DEVINFO_SIZE - 1], 0x3c);
}
TEST(gap7_ptrdev_user32_route_agrees) {
    REQUIRE(U(GetPointerDevice));
    NtUserGetPointerDevice_pfn f = G7N(NtUserGetPointerDevice); REQUIRE(f);
    static char info[DEVINFO_SIZE];
    SetLastError(0);
    EXPECT_EQ(U(GetPointerDevice)(bogus_dev(), (POINTER_DEVICE_INFO *)info), FALSE);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
TEST(gap7_ptrdevcursors_null_arguments_fail_57) {
    NtUserGetPointerDeviceCursors_pfn f = G7N(NtUserGetPointerDeviceCursors); REQUIRE(f);
    UINT n = 0; static char buf[64];
    SetLastError(0); EXPECT_EQ(f(NULL, &n, buf), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), NULL, buf), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(NULL, NULL, NULL), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_ptrdevcursors_unknown_handle_fails_invalid_handle) {
    NtUserGetPointerDeviceCursors_pfn f = G7N(NtUserGetPointerDeviceCursors); REQUIRE(f);
    UINT n = 7; static char buf[64];
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), &n, NULL), FALSE); EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), &n, buf), FALSE); EXPECT_ERR(ERROR_INVALID_HANDLE);
    EXPECT_EQ(n, 7);     /* count untouched on failure */
}
TEST(gap7_ptrdevprops_null_arguments_fail_57) {
    NtUserGetPointerDeviceProperties_pfn f = G7N(NtUserGetPointerDeviceProperties); REQUIRE(f);
    UINT n = 0; static char buf[64];
    SetLastError(0); EXPECT_EQ(f(NULL, &n, buf), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), NULL, buf), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_ptrdevprops_unknown_handle_fails_invalid_handle) {
    NtUserGetPointerDeviceProperties_pfn f = G7N(NtUserGetPointerDeviceProperties); REQUIRE(f);
    UINT n = 3; static char buf[64];
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), &n, NULL), FALSE); EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), &n, buf), FALSE); EXPECT_ERR(ERROR_INVALID_HANDLE);
    EXPECT_EQ(n, 3);
}
TEST(gap7_ptrdevrects_missing_arguments_fail_57) {
    NtUserGetPointerDeviceRects_pfn f = G7N(NtUserGetPointerDeviceRects); REQUIRE(f);
    RECT a, b;
    SetLastError(0); EXPECT_EQ(f(NULL, &a, &b), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), NULL, &b), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(f(bogus_dev(), &a, NULL), FALSE); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_ptrdevrects_unknown_handle_fails_57_and_keeps_output) {
    NtUserGetPointerDeviceRects_pfn f = G7N(NtUserGetPointerDeviceRects); REQUIRE(f);
    RECT a = { 1, 2, 3, 4 }, b = { 5, 6, 7, 8 };
    SetLastError(0);
    EXPECT_EQ(f(bogus_dev(), &a, &b), FALSE);      /* _GetPointerDeviceRects fails -> UserSetLastError(0x57) */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(a.left, 1); EXPECT_EQ(a.bottom, 4); EXPECT_EQ(b.left, 5); EXPECT_EQ(b.bottom, 8);
}
TEST(gap7_ptrdev_real_devices_cursors_and_rects) {
    NtUserGetPointerDevices_pfn fd = G7N(NtUserGetPointerDevices);
    NtUserGetPointerDeviceCursors_pfn fc = G7N(NtUserGetPointerDeviceCursors);
    NtUserGetPointerDeviceRects_pfn fr = G7N(NtUserGetPointerDeviceRects); REQUIRE(fd && fc && fr);
    UINT n = 0; fd(&n, NULL);
    if (n == 0) { t_skip("no pointer devices"); return; }
    char *buf = (char *)calloc(n, DEVINFO_SIZE);
    UINT cap = n;
    if (!fd(&cap, buf)) { free(buf); return; }
    for (UINT i = 0; i < n; i++) {
        HANDLE dev = *(HANDLE *)(buf + (size_t)i * DEVINFO_SIZE + 8);
        UINT cc = 0;
        EXPECT_EQ(fc(dev, &cc, NULL), TRUE);          /* NULL cursors: count only */
        if (cc) {
            UINT small = cc - 1; char *cur = (char *)calloc(cc + 1, 24);
            SetLastError(0);
            EXPECT_EQ(fc(dev, &small, cur), FALSE);
            EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
            EXPECT_EQ(small, cc);
            free(cur);
        }
        RECT dr, sr; memset(&dr, 0, sizeof dr); memset(&sr, 0, sizeof sr);
        if (fr(dev, &dr, &sr)) CHECK(sr.right > sr.left && sr.bottom > sr.top);
    }
    free(buf);
}

/* ---- NtUserGetPointerCursorId / GetRawPointerDeviceData / DiscardPointerFrameMessages --------------------- */
TEST(gap7_ptrcursorid_invalid_pointer_id_fails_57) {
    NtUserGetPointerCursorId_pfn f = G7N(NtUserGetPointerCursorId); REQUIRE(f);
    static const UINT bad[] = { 0, 0x10000, 0x10001, 0x80000000, 0xffff0001, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        UINT out = 0x77;
        SetLastError(0);
        EXPECT_EQ(f(bad[i], &out), FALSE);       /* id == 0 or id >> 16 != 0 */
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "id 0x%x err %lu", bad[i], GetLastError());
        EXPECT_EQ(out, 0x77);
    }
}
TEST(gap7_ptrcursorid_null_out_fails_57) {
    NtUserGetPointerCursorId_pfn f = G7N(NtUserGetPointerCursorId); REQUIRE(f);
    SetLastError(0);
    EXPECT_EQ(f(1, NULL), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(f(0xffff, NULL), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_ptrcursorid_unknown_pointer_fails_without_writing) {
    NtUserGetPointerCursorId_pfn f = G7N(NtUserGetPointerCursorId); REQUIRE(f);
    UINT out = 0x77;
    BOOL r = f(0x7e57, &out);                    /* no such pointer is active in this test process */
    EXPECT_EQ(r, FALSE);
    EXPECT_EQ(out, 0x77);
    t_note("unknown pointer id error %lu", GetLastError());
}
TEST(gap7_ptrcursorid_user32_route_agrees) {
    REQUIRE(U(GetPointerCursorId));
    SetLastError(0);
    UINT out = 0;
    EXPECT_EQ(U(GetPointerCursorId)(0, &out), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_rawptrdata_argument_validation_fails_57) {
    NtUserGetRawPointerDeviceData_pfn f = G7N(NtUserGetRawPointerDeviceData); REQUIRE(f);
    static char props[0x1c * 2]; static LONG vals[4];
    struct { UINT id, hist, pc; void *props; LONG *vals; const char *why; } c[] = {
        { 0,       1, 1, props, vals, "id 0" },
        { 0x10000, 1, 1, props, vals, "id > 0xffff" },
        { 1,       0, 1, props, vals, "history 0" },
        { 1,       1, 0, props, vals, "propCount 0" },
        { 1,       1, 1, NULL,  vals, "props NULL" },
        { 1,       1, 1, props, NULL, "values NULL" },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        SetLastError(0);
        EXPECT_EQ(f(c[i].id, c[i].hist, c[i].pc, c[i].props, c[i].vals), FALSE);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "%s: err %lu", c[i].why, GetLastError());
    }
}
TEST(gap7_rawptrdata_overflowing_product_fails_57) {
    NtUserGetRawPointerDeviceData_pfn f = G7N(NtUserGetRawPointerDeviceData); REQUIRE(f);
    static char props[0x1c]; static LONG vals[1];
    SetLastError(0);
    EXPECT_EQ(f(1, 0x10000, 0x10000, props, vals), FALSE);   /* history * propCount > 0xffffffff */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(f(1, 0xffffffff, 2, props, vals), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_rawptrdata_user32_route_agrees) {
    REQUIRE(U(GetRawPointerDeviceData));
    static LONG vals[1];
    SetLastError(0);
    EXPECT_EQ(U(GetRawPointerDeviceData)(0, 1, 1, NULL, vals), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap7_skipptrframes_invalid_pointer_id_fails_57) {
    NtUserDiscardPointerFrameMessages_pfn f = G7N(NtUserDiscardPointerFrameMessages); REQUIRE(f);
    static const UINT bad[] = { 0, 0x10000, 0xffff0001, 0x80000002, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        EXPECT_EQ(f(bad[i]), FALSE);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "id 0x%x err %lu", bad[i], GetLastError());
    }
}
TEST(gap7_skipptrframes_user32_route_agrees) {
    REQUIRE(U(SkipPointerFrameMessages));
    SetLastError(0);
    EXPECT_EQ(U(SkipPointerFrameMessages)(0), FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
