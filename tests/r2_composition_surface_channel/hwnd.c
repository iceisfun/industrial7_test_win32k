/* Window-bound composition syscalls: NtUserCreate/DestroyDCompositionHwndTarget, NtUserHwndSetRedirectionInfo,
 * NtUserHwndQueryRedirectionInfo, NtUserDwmValidateWindow, NtUserAddVisualIdentifier / RemoveVisualIdentifier,
 * NtUserMapPointsByVisualIdentifier, NtMapVisualRelativePoints.
 *
 * win32kfull.sys / win32kbase.sys facts used below (19041):
 *  - HwndSetRedirectionInfo(hwnd, cls, buf, size): the status starts at 0xC000000D. Only cls==2 with buf!=NULL and
 *    size>=4 reaches the present-flags update; any other combination keeps 0xC000000D (cls != 2 inside the valid-window
 *    branch becomes 0xC0000003, a short buffer 0xC0000206). A negative status ends in
 *    UserSetLastError(RtlNtStatusToDosError(status)) and the function returns FALSE, so an invalid HWND yields 87.
 *  - HwndQueryRedirectionInfo(hwnd, cls, buf, pSize): ValidateHwnd failure returns 0 with the validator's own
 *    last error; cls >= 5 is 0xC0000003 (set through UserSetLastStatus); other failures stay 0xC000000D / 0xC0000003.
 *  - DwmValidateWindow(hwnd, pid): ValidateHwnd failure -> FALSE (its error); valid window but caller is not the DWM
 *    process -> UserSetLastError(5), FALSE.
 *  - CreateDCompositionHwndTarget(hwnd, mode, phOut): TestWindowForCompositionTarget failure -> FALSE with the NTSTATUS
 *    stored via UserSetLastStatus; *phOut is written only when the target was created.
 *  - MapVisualRelativePoints(from, to, count, in, out): count<<3 >= 2^32 -> LastError 8; count==0 -> TRUE without
 *    touching either buffer. MapPointsByVisualIdentifier(luidFrom, luidTo, count, in, out): same two rules.
 *  - AddVisualIdentifier(hSink, pLuid): ResolveHandle failure -> FALSE, error = RtlNtStatusToDosError(status).
 * Return widths follow the analysis: bool-returning bodies are declared BYTE so garbage in the upper register bits is
 * not mistaken for TRUE. */
#include "rc.h"

UDECL(BYTE, NtUserCreateDCompositionHwndTarget, (HWND h, int mode, HANDLE *phOut));
UDECL(BYTE, NtUserDestroyDCompositionHwndTarget, (HWND h, int mode));
UDECL(BYTE, NtUserHwndSetRedirectionInfo, (HWND h, int cls, void *buf, UINT size));
UDECL(int,  NtUserHwndQueryRedirectionInfo, (HWND h, int cls, void *buf, UINT *pSize));
UDECL(BYTE, NtUserDwmValidateWindow, (HWND h, int pid));
UDECL(int,  NtUserAddVisualIdentifier, (HANDLE hSink, LUID *pLuid));
UDECL(int,  NtUserRemoveVisualIdentifier, (LUID *pLuid));
UDECL(int,  NtUserMapPointsByVisualIdentifier, (LUID *from, LUID *to, UINT count, POINT *in, POINT *out));
UDECL(int,  NtMapVisualRelativePoints, (LONGLONG from, LONGLONG to, UINT count, POINT *in, POINT *out));

#define LO32_(x) ((ULONG)(ULONG_PTR)(x))
static HWND rc_window(void) {
    HWND w = k_make_window("r2cs");
    if (!w) t_skip("cannot create a helper window");
    return w;
}
#define NEED_WND(w) HWND w = rc_window(); if (!(w)) return

/* ------------------------------------------------------------------ DwmValidateWindow */
RT(dwm_validate_window_not_dwm) {
    NEED_WIN();
    NtUserDwmValidateWindow_pfn f = UN(NtUserDwmValidateWindow); REQUIRE(f);
    NEED_WND(w);
    SetLastError(0);
    BYTE r = f(w, (int)GetCurrentProcessId());
    DWORD e = GetLastError();
    CHECK(r == 0);
    CHECK_ST(e, ERROR_ACCESS_DENIED);        /* IsProcessDwm(current) == 0 -> UserSetLastError(5) */
    DestroyWindow(w);
}
RT(dwm_validate_window_bad_handles) {
    NEED_WIN();
    NtUserDwmValidateWindow_pfn f = UN(NtUserDwmValidateWindow); REQUIRE(f);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10001, (HWND)(ULONG_PTR)0xFFFFFFFF, GetDesktopWindow() };
    int i;
    for (i = 0; i < 4; i++) {
        SetLastError(0);
        BYTE r = f(bad[i], 0);
        CHECKF(r == 0, "hwnd %p", bad[i]);
        DWORD e = GetLastError();
        if (i < 3) CHECKF(e == ERROR_INVALID_WINDOW_HANDLE, "hwnd %p error %lu", bad[i], e);   /* ValidateHwnd failed */
        else CHECK(e == ERROR_ACCESS_DENIED);                                                   /* valid, but not the DWM */
    }
}

/* ------------------------------------------------------------------ DCompositionHwndTarget */
RT(hwnd_target_invalid_window) {
    NEED_WIN();
    NtUserCreateDCompositionHwndTarget_pfn cr = UN(NtUserCreateDCompositionHwndTarget);
    NtUserDestroyDCompositionHwndTarget_pfn de = UN(NtUserDestroyDCompositionHwndTarget);
    REQUIRE(cr); REQUIRE(de);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10001, (HWND)(ULONG_PTR)0xFFFFFFFF };
    int i; unsigned long h0 = rc_handles();
    for (i = 0; i < 3; i++) {
        HANDLE out = (HANDLE)(ULONG_PTR)0x1234;
        SetLastError(0);
        CHECKF(cr(bad[i], 0, &out) == 0, "create hwnd %p", bad[i]);
        CHECK(out == (HANDLE)(ULONG_PTR)0x1234);
        /* an invalid window handle is reported as a plain ERROR_INVALID_WINDOW_HANDLE, not a status-derived code */
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "create hwnd %p error %lu", bad[i], GetLastError());
        SetLastError(0);
        CHECKF(de(bad[i], 0) == 0, "destroy hwnd %p", bad[i]);
        /* DetachWindowCompositionTarget: ValidateHwnd failure -> 0xC0000001 */
        CHECKF(GetLastError() == rc_dos_error(S_UNSUCC), "destroy hwnd %p error %lu", bad[i], GetLastError());
    }
    CHECK(rc_handles() == h0);
}
RT(hwnd_target_foreign_window_rejected) {
    NEED_WIN();
    NtUserCreateDCompositionHwndTarget_pfn cr = UN(NtUserCreateDCompositionHwndTarget); REQUIRE(cr);
    NtUserDestroyDCompositionHwndTarget_pfn de = UN(NtUserDestroyDCompositionHwndTarget); REQUIRE(de);
    HWND dw = GetDesktopWindow();           /* owned by another process / the system: no composition target for us */
    HANDLE out = (HANDLE)(ULONG_PTR)0x1234;
    SetLastError(0);
    CHECK(cr(dw, 0, &out) == 0);
    CHECK(out == (HANDLE)(ULONG_PTR)0x1234);
    t_note("target on the desktop window -> LastError %lu", GetLastError());
    /* the refusal sets no last error (the code was reset to 0 before the call and is still 0), or one of the
     * status-derived codes for an invalid parameter / access denied */
    {   DWORD e = GetLastError(); CHECKF(e == 0 || e == rc_dos_error(S_INVPARAM) || e == rc_dos_error(S_DENIED), "error %lu", e); }
    SetLastError(0);
    CHECK(de(dw, 0) == 0);
    {   DWORD e = GetLastError(); CHECKF(e == rc_dos_error(S_UNSUCC) || e == rc_dos_error(S_DENIED), "destroy error %lu", e); }
}
RT(hwnd_target_attach_detach_state_machine) {
    NEED_WIN();
    NtUserCreateDCompositionHwndTarget_pfn cr = UN(NtUserCreateDCompositionHwndTarget); REQUIRE(cr);
    NtUserDestroyDCompositionHwndTarget_pfn de = UN(NtUserDestroyDCompositionHwndTarget); REQUIRE(de);
    NEED_WND(w);
    unsigned long h0 = rc_handles();
    HANDLE t1 = NULL, t2 = (HANDLE)(ULONG_PTR)0x1234; char tn[64];
    SetLastError(0);
    BYTE r = cr(w, 0, &t1);
    if (!r) {
        t_note("CreateDCompositionHwndTarget on a plain popup failed, LastError %lu", GetLastError());
        DestroyWindow(w); t_skip("composition target not available"); return;
    }
    CHECK(t1 != NULL && t1 != INVALID_HANDLE_VALUE);
    CHECK(rc_handles() == h0 + 1);
    rc_type_name(t1, tn, sizeof tn);
    t_note("hwnd target handle type: \"%s\"", tn);
    CHECK(tn[0] != 0);
    /* a window can carry only one target of a given mode */
    SetLastError(0);
    CHECK(cr(w, 0, &t2) == 0);
    CHECK(t2 == (HANDLE)(ULONG_PTR)0x1234);
    t_note("second target on same window -> LastError %lu", GetLastError());
    /* the refusal is a warning-class status: it fails the call but leaves the last error at 0 */
    CHECKF(GetLastError() == 0, "second create error %lu", GetLastError());
    CHECK(rc_handles() == h0 + 1);               /* the failed attempt left no handle behind */
    /* detach, then detach again */
    SetLastError(0);
    CHECK(de(w, 0) != 0);
    SetLastError(0);
    CHECK(de(w, 0) == 0);
    /* _DetachWindowCompositionTarget returned 0 -> 0xC0000225 */
    CHECKF(GetLastError() == rc_dos_error(S_NOTFOUND), "second detach error %lu", GetLastError());
    CloseHandle(t1);
    CHECK(rc_handles() == h0);
    DestroyWindow(w);
}

/* ------------------------------------------------------------------ redirection info */
RT(redirection_set_invalid_window) {
    NEED_WIN();
    NtUserHwndSetRedirectionInfo_pfn f = UN(NtUserHwndSetRedirectionInfo); REQUIRE(f);
    DWORD buf = 0;
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10001, (HWND)(ULONG_PTR)0xFFFFFFFF };
    int i;
    for (i = 0; i < 3; i++) {
        SetLastError(0);
        CHECK(f(bad[i], 2, &buf, 4) == 0);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "hwnd %p error %lu", bad[i], GetLastError());  /* status 0xC000000D */
    }
}
RT(redirection_set_argument_table) {
    NEED_WIN();
    NtUserHwndSetRedirectionInfo_pfn f = UN(NtUserHwndSetRedirectionInfo); REQUIRE(f);
    NEED_WND(w);
    DWORD buf[4] = { 0 };
    static const int classes[] = { 0, 1, 3, 4, 5, -1, 0x7FFFFFFF };
    size_t i;
    ULONG e_param = rc_dos_error(S_INVPARAM), e_class = rc_dos_error(S_INFOCLASS), e_size = rc_dos_error(S_BUFSIZE);
    for (i = 0; i < sizeof classes / sizeof classes[0]; i++) {
        SetLastError(0);
        CHECKF(f(w, classes[i], buf, 4) == 0, "class %d", classes[i]);
        DWORD e = GetLastError();
        CHECKF(e == e_param || e == e_class, "class %d error %lu", classes[i], e);   /* 0xC000000D or 0xC0000003 */
    }
    SetLastError(0);
    CHECK(f(w, 2, NULL, 4) == 0);                 /* class 2 without a buffer changes nothing */
    CHECK(GetLastError() == e_param);
    SetLastError(0);
    CHECK(f(w, 2, buf, 0) == 0);                  /* too small: 0xC0000206 (or 0xC000000D when the window has no redirection style) */
    {   DWORD e = GetLastError(); CHECKF(e == e_size || e == e_param, "short buffer error %lu", e); }
    SetLastError(0);
    CHECK(f(w, 2, buf, 3) == 0);
    {   DWORD e = GetLastError(); CHECK(e == e_size || e == e_param); }
    DestroyWindow(w);
}
RT(redirection_query_argument_table) {
    NEED_WIN();
    NtUserHwndQueryRedirectionInfo_pfn f = UN(NtUserHwndQueryRedirectionInfo); REQUIRE(f);
    NEED_WND(w);
    unsigned char buf[0x40]; UINT sz = sizeof buf;
    ULONG e_param = rc_dos_error(S_INVPARAM), e_class = rc_dos_error(S_INFOCLASS), e_dwm = rc_dos_error(S_DENIED);
    static const int highcls[] = { 5, 6, 100, -1, 0x7FFFFFFF };
    size_t i;
    for (i = 0; i < sizeof highcls / sizeof highcls[0]; i++) {
        memset(buf, 0xCD, sizeof buf); sz = sizeof buf;
        SetLastError(0);
        CHECKF(f(w, highcls[i], buf, &sz) == 0, "class %d", highcls[i]);
        /* cls >= 5 is INVALID_INFO_CLASS; a negative cls is < 5 and ends in the final else (same status) unless the
         * window has no redirection style (0xC000000D) */
        if (highcls[i] < 0) CHECKF(GetLastError() == e_class || GetLastError() == e_param, "class %d error %lu", highcls[i], GetLastError());
        else CHECKF(GetLastError() == e_class, "class %d error %lu", highcls[i], GetLastError());
        CHECK(sz == sizeof buf && buf[0] == 0xCD);
    }
    /* class 3 is the DWM-only query: ACCESS_DENIED for everybody else (or INVALID_PARAMETER when the window has no
     * redirection style); class 4 falls into the final else (0xC0000003). */
    memset(buf, 0xCD, sizeof buf); sz = sizeof buf;
    SetLastError(0);
    CHECK(f(w, 3, buf, &sz) == 0);
    {   DWORD e = GetLastError(); CHECKF(e == e_dwm || e == e_param, "class 3 error %lu", e); }
    SetLastError(0);
    CHECK(f(w, 4, buf, &sz) == 0);
    {   DWORD e = GetLastError(); CHECKF(e == e_class || e == e_param, "class 4 error %lu", e); }
    CHECK(buf[0] == 0xCD);                        /* nothing is copied out on failure */
    DestroyWindow(w);
}
RT(redirection_query_invalid_window_error) {
    NEED_WIN();
    NtUserHwndQueryRedirectionInfo_pfn f = UN(NtUserHwndQueryRedirectionInfo); REQUIRE(f);
    unsigned char buf[0x20]; UINT sz = sizeof buf;
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10001 };
    int i;
    for (i = 0; i < 2; i++) {
        SetLastError(0);
        CHECK(f(bad[i], 0, buf, &sz) == 0);
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "error %lu", GetLastError());   /* only ValidateHwnd's error is set */
    }
}

/* ------------------------------------------------------------------ visual identifiers */
RT(visual_identifier_add_bad_sink) {
    NEED_WIN();
    NtUserAddVisualIdentifier_pfn f = UN(NtUserAddVisualIdentifier); REQUIRE(f);
    LUID id = { 0x5A5A, 0x1357 };
    HANDLE bad[] = { NULL, (HANDLE)(ULONG_PTR)0x7FFC, GetCurrentProcess(), (HANDLE)(ULONG_PTR)-2 };
    int i;
    for (i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(f(bad[i], &id) == 0, "handle %p", bad[i]);
        CHECKF(GetLastError() != 0, "handle %p: no error", bad[i]);
        t_note("AddVisualIdentifier(%p) -> LastError %lu", bad[i], GetLastError());
    }
}
RT(visual_identifier_remove_unknown) {
    NEED_WIN();
    NtUserRemoveVisualIdentifier_pfn f = UN(NtUserRemoveVisualIdentifier); REQUIRE(f);
    LUID id = { 0x5A5A5A5A, 0x1357 };
    SetLastError(0);
    int r = f(&id);
    t_note("RemoveVisualIdentifier(unknown) -> %d LastError %lu", r, GetLastError());
    CHECK(r == 0);                              /* InputObjectMap::RemoveMapping fails for an id nobody registered */
    CHECK(GetLastError() != 0);
}
RT(map_visual_points_zero_count) {
    NEED_WIN();
    NtMapVisualRelativePoints_pfn f = UN(NtMapVisualRelativePoints); REQUIRE(f);
    NtUserMapPointsByVisualIdentifier_pfn g = UN(NtUserMapPointsByVisualIdentifier); REQUIRE(g);
    SetLastError(0);
    CHECK(f(1, 2, 0, NULL, NULL) == 1);         /* no element loop, ProbeForWrite(…, 0) is a no-op */
    SetLastError(0);
    CHECK(g(NULL, NULL, 0, NULL, NULL) == 1);
}
RT(map_visual_points_count_overflow) {
    NEED_WIN();
    NtMapVisualRelativePoints_pfn f = UN(NtMapVisualRelativePoints); REQUIRE(f);
    NtUserMapPointsByVisualIdentifier_pfn g = UN(NtUserMapPointsByVisualIdentifier); REQUIRE(g);
    static const UINT counts[] = { 0x20000000u, 0x20000001u, 0x80000000u, 0xFFFFFFFFu };
    size_t i;
    for (i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(f(1, 2, counts[i], NULL, NULL) == 0, "count 0x%x", counts[i]);
        CHECKF(GetLastError() == ERROR_NOT_ENOUGH_MEMORY, "count 0x%x error %lu", counts[i], GetLastError());   /* (count << 3) >= 2^32 -> 8 */
        SetLastError(0);
        CHECKF(g(NULL, NULL, counts[i], NULL, NULL) == 0, "ident count 0x%x", counts[i]);
        CHECKF(GetLastError() == ERROR_NOT_ENOUGH_MEMORY, "ident count 0x%x error %lu", counts[i], GetLastError());
    }
}
RT(map_visual_points_unknown_visuals) {
    NEED_WIN();
    NtMapVisualRelativePoints_pfn f = UN(NtMapVisualRelativePoints); REQUIRE(f);
    NtUserMapPointsByVisualIdentifier_pfn g = UN(NtUserMapPointsByVisualIdentifier); REQUIRE(g);
    POINT in[2] = { { 10, 20 }, { 30, 40 } }, out[2] = { { -1, -1 }, { -1, -1 } };
    LUID a = { 0x1234, 0x77 }, b = { 0x5678, 0x77 };
    SetLastError(0);
    int r = f(0x1234, 0x5678, 2, in, out);
    t_note("MapVisualRelativePoints(unknown visuals) -> %d LastError %lu out0=(%ld,%ld)", r, GetLastError(), out[0].x, out[0].y);
    if (r == 0) CHECK(GetLastError() != 0);
    out[0].x = out[0].y = out[1].x = out[1].y = -1;
    SetLastError(0);
    r = g(&a, &b, 2, in, out);
    t_note("MapPointsByVisualIdentifier(unknown ids) -> %d LastError %lu", r, GetLastError());
    CHECK(r == 0);
    CHECK(GetLastError() != 0);
    CHECK(out[1].x == -1 && out[1].y == -1);      /* the result is copied out only after MapPointsByVisualIdentifierInternal succeeded */
}

/* ------------------------------------------------------------------ resize synchronization object */
UDECL(BYTE, NtUserGetResizeDCompositionSynchronizationObject, (HWND h, HANDLE *phOut));
UDECL(ULONG_PTR, NtUserConfirmResizeCommit, (HWND h));

RT(resize_sync_object_no_object_is_success_with_null) {
    NEED_WIN();
    NtUserGetResizeDCompositionSynchronizationObject_pfn f = UN(NtUserGetResizeDCompositionSynchronizationObject); REQUIRE(f);
    /* The status local starts at 0 whenever phOut != NULL; a window without a resize sync object leaves it 0, so the
     * body stores a NULL handle and still returns TRUE (-1 < status). */
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10001 };
    int i;
    for (i = 0; i < 2; i++) {
        HANDLE h = (HANDLE)(ULONG_PTR)0x1234;
        CHECKF(f(bad[i], &h) != 0, "hwnd %p", bad[i]);
        CHECKF(h == NULL, "hwnd %p handle %p", bad[i], h);
    }
    NEED_WND(w);
    {   HANDLE h = (HANDLE)(ULONG_PTR)0x1234; unsigned long h0 = rc_handles();
        BYTE r = f(w, &h);
        CHECK(r != 0);
        CHECK(h != (HANDLE)(ULONG_PTR)0x1234);
        t_note("resize sync object of a plain popup: %p", h);
        if (h) { char tn[64]; rc_type_name(h, tn, sizeof tn); t_note("type \"%s\"", tn); CloseHandle(h); }
        CHECK(rc_handles() == h0);
    }
    DestroyWindow(w);
}
RT(confirm_resize_commit_non_dwm) {
    NEED_WIN();
    NtUserConfirmResizeCommit_pfn f = UN(NtUserConfirmResizeCommit); REQUIRE(f);
    NEED_WND(w);
    CHECK(LO32_(f(w)) == 0);                       /* only the DWM process reaches the resize-commit confirmation */
    CHECK(LO32_(f(NULL)) == 0);
    CHECK(LO32_(f((HWND)(ULONG_PTR)0x10001)) == 0);
    DestroyWindow(w);
}
