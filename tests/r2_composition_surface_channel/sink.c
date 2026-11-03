/* Composition input sinks: NtCreateCompositionInputSink, NtCreateImplicitCompositionInputSink,
 * NtCloseCompositionInputSink, NtQueryCompositionInputIsImplicit / SinkLuid / SinkViewId,
 * NtUserCompositionInputSinkLuidFromPoint, NtUserCompositionInputSinkViewInstanceIdFromPoint.
 *
 * Analysis (win32kbase / win32kfull, 19041):
 *  - _CreateCompositionInputSink(info, implicit, phOut) copies a COMPOSITION_INPUT_SINK of at most 0x128 bytes
 *    (cbSize in the first dword); the implicit variant sets bit 0 of the flags that end up at CompositionInputObject
 *    +0xc0. The layout used here is the one dcomp!DesktopWindowTarget::CreateInputSink builds: cbSize 0x128, four
 *    16-byte {2, ?, ULONG64 target} blocks at +0x08/+0x40/+0x78/+0xB0 and the dword 2 at +0x120.
 *  - NtQueryCompositionInputIsImplicit returns CompositionInputObject+0xc0 & 1, SinkLuid the qword at +0x10, SinkViewId
 *    the dword at +0xb8; all three call CompositionInputObject::ResolveHandle first and store the output only on success.
 *  - NtUserCompositionInputSinkLuidFromPoint(type, ...): type must be 0 or 6, otherwise UserSetLastError(0x57) and FALSE
 *    before any pointer is read; the DWM process gets ERROR_ACCESS_DENIED instead of an answer. */
#include "rc.h"

UDECL(NTSTATUS, NtCreateCompositionInputSink, (void *info, HANDLE *phOut));
UDECL(NTSTATUS, NtCreateImplicitCompositionInputSink, (void *info, HANDLE *phOut));
UDECL(NTSTATUS, NtCloseCompositionInputSink, (HANDLE h));
UDECL(NTSTATUS, NtQueryCompositionInputIsImplicit, (HANDLE h, UINT *pOut));
UDECL(NTSTATUS, NtQueryCompositionInputSinkLuid, (HANDLE h, ULONGLONG *pOut));
UDECL(NTSTATUS, NtQueryCompositionInputSinkViewId, (HANDLE h, UINT *pOut));
UDECL(LONGLONG, NtUserCompositionInputSinkLuidFromPoint, (int type, POINT *pt, ULONGLONG *pLuid, ULONGLONG *pExtra, void *pInfo));

static void sink_info(unsigned char *s, HWND w) {
    static const int off[] = { 0x08, 0x40, 0x78, 0xB0 };
    int i;
    memset(s, 0, 0x128);
    *(DWORD *)s = 0x128;
    for (i = 0; i < 4; i++) { *(DWORD *)(s + off[i]) = 2; *(ULONGLONG *)(s + off[i] + 8) = (ULONGLONG)(ULONG_PTR)w; }
    *(DWORD *)(s + 0x120) = 2;
}
static HANDLE make_sink(HWND w, int implicit, NTSTATUS *pst) {
    unsigned char info[0x128] __attribute__((aligned(8)));
    HANDLE h = NULL;
    NtCreateCompositionInputSink_pfn a = UN(NtCreateCompositionInputSink);
    NtCreateImplicitCompositionInputSink_pfn b = UN(NtCreateImplicitCompositionInputSink);
    NTSTATUS st;
    sink_info(info, w);
    st = implicit ? (b ? b(info, &h) : S_NOTIMPL) : (a ? a(info, &h) : S_NOTIMPL);
    if (pst) *pst = st;
    if (st < 0) return NULL;
    return h;
}
static void close_sink(HANDLE h) {
    NtCloseCompositionInputSink_pfn c = UN(NtCloseCompositionInputSink);
    if (c && h) c(h);
}

RT(sink_query_bad_handle) {
    NEED_WIN();
    NtQueryCompositionInputIsImplicit_pfn q1 = UN(NtQueryCompositionInputIsImplicit); REQUIRE(q1);
    NtQueryCompositionInputSinkLuid_pfn q2 = UN(NtQueryCompositionInputSinkLuid); REQUIRE(q2);
    NtQueryCompositionInputSinkViewId_pfn q3 = UN(NtQueryCompositionInputSinkViewId); REQUIRE(q3);
    HANDLE bad[] = { NULL, (HANDLE)(ULONG_PTR)0x7FFC, GetCurrentProcess(), (HANDLE)(ULONG_PTR)-2, GetCurrentThread() };
    int i;
    for (i = 0; i < 5; i++) {
        UINT a = 0xDEADBEEFu, c = 0xDEADBEEFu; ULONGLONG b = 0x1122334455667788ull;
        NTSTATUS s1 = q1(bad[i], &a), s2 = q2(bad[i], &b), s3 = q3(bad[i], &c);
        CHECKF(NT_FAIL(s1), "IsImplicit(%p) = 0x%08lx", bad[i], (unsigned long)s1);
        CHECKF(NT_FAIL(s2), "SinkLuid(%p) = 0x%08lx", bad[i], (unsigned long)s2);
        CHECKF(NT_FAIL(s3), "SinkViewId(%p) = 0x%08lx", bad[i], (unsigned long)s3);
        CHECK(a == 0xDEADBEEFu && b == 0x1122334455667788ull && c == 0xDEADBEEFu);    /* outputs untouched on failure */
        if (i == 1) t_note("invalid handle -> 0x%08lx / 0x%08lx / 0x%08lx", (unsigned long)s1, (unsigned long)s2, (unsigned long)s3);
    }
}
RT(sink_close_bad_handle) {
    NEED_WIN();
    NtCloseCompositionInputSink_pfn c = UN(NtCloseCompositionInputSink); REQUIRE(c);
    /* the body is ETW tracing followed by NtClose(handle): an invalid handle gives STATUS_INVALID_HANDLE */
    unsigned long h0 = rc_handles();
    CHECK_ST(c(NULL), S_INVHANDLE);
    CHECK_ST(c((HANDLE)(ULONG_PTR)0x7FFC), S_INVHANDLE);
    CHECK(rc_handles() == h0);
}
RT(sink_explicit_create_query_close) {
    NEED_WIN();
    NtQueryCompositionInputIsImplicit_pfn q1 = UN(NtQueryCompositionInputIsImplicit); REQUIRE(q1);
    NtQueryCompositionInputSinkLuid_pfn q2 = UN(NtQueryCompositionInputSinkLuid); REQUIRE(q2);
    NtQueryCompositionInputSinkViewId_pfn q3 = UN(NtQueryCompositionInputSinkViewId); REQUIRE(q3);
    HWND w = k_make_window("r2cs-sink"); REQUIRE(w);
    NTSTATUS st = 0; unsigned long h0 = rc_handles();
    HANDLE s = make_sink(w, 0, &st);
    if (!s) { t_note("CreateCompositionInputSink -> 0x%08lx", (unsigned long)st); DestroyWindow(w); t_skip("could not create an input sink"); return; }
    char tn[64]; rc_type_name(s, tn, sizeof tn);
    t_note("input sink handle %p type \"%s\"", s, tn);
    UINT imp = 0xDEADBEEFu, vid = 0xDEADBEEFu; ULONGLONG luid = 0;
    CHECK_ST(q1(s, &imp), 0);
    CHECK(imp == 0);                               /* created through the non-implicit entry point */
    CHECK_ST(q2(s, &luid), 0);
    CHECK(luid != 0);
    CHECK_ST(q3(s, &vid), 0);
    t_info("sink luid=0x%llx", (unsigned long long)luid); t_note("sink view id=%u", vid);
    {   ULONGLONG l2 = 0; CHECK_ST(q2(s, &l2), 0); CHECK(l2 == luid); }       /* stable */
    close_sink(s);
    imp = 0xDEADBEEFu;
    CHECK(NT_FAIL(q1(s, &imp)));                   /* the handle is gone */
    CHECK(imp == 0xDEADBEEFu);
    CHECK(rc_handles() == h0);
    DestroyWindow(w);
}
RT(sink_implicit_flag_and_distinct_luids) {
    NEED_WIN();
    NtQueryCompositionInputIsImplicit_pfn q1 = UN(NtQueryCompositionInputIsImplicit); REQUIRE(q1);
    NtQueryCompositionInputSinkLuid_pfn q2 = UN(NtQueryCompositionInputSinkLuid); REQUIRE(q2);
    HWND w = k_make_window("r2cs-sink2"); REQUIRE(w);
    NTSTATUS s1 = 0, s2 = 0;
    HANDLE a = make_sink(w, 1, &s1), b = make_sink(w, 0, &s2);
    if (!a || !b) { t_note("implicit -> 0x%08lx explicit -> 0x%08lx", (unsigned long)s1, (unsigned long)s2); close_sink(a); close_sink(b); DestroyWindow(w); t_skip("sink creation unavailable"); return; }
    UINT ia = 7, ib = 7; ULONGLONG la = 0, lb = 0;
    CHECK_ST(q1(a, &ia), 0); CHECK_ST(q1(b, &ib), 0);
    CHECK(ia == 1);                                /* CreateImplicit sets bit 0 of the flags at +0xc0 */
    CHECK(ib == 0);
    CHECK_ST(q2(a, &la), 0); CHECK_ST(q2(b, &lb), 0);
    CHECK(la != lb);                               /* every sink gets its own identifier */
    close_sink(a); close_sink(b);
    DestroyWindow(w);
}
RT(sink_luid_from_point_type_table) {
    NEED_WIN();
    NtUserCompositionInputSinkLuidFromPoint_pfn f = UN(NtUserCompositionInputSinkLuidFromPoint); REQUIRE(f);
    static const int bad[] = { 1, 2, 3, 4, 5, 7, 8, 0x10, 0x100, -1, 0x7FFFFFFF };
    size_t i;
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF((int)f(bad[i], NULL, NULL, NULL, NULL) == 0, "type %d", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "type %d error %lu", bad[i], GetLastError());   /* 0x57 */
    }
}
