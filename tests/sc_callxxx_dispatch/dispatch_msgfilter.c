/* sc_callxxx_dispatch - NtUserCallMsgFilter(LPMSG lpMsg, int nCode).
 *
 * win32kfull.sys!NtUserCallMsgFilter analysis (19041.2965):
 *   - probes/copies 6 ULONG64s (0x30 bytes - exactly sizeof(MSG) on x64: HWND, UINT message,
 *     WPARAM, LPARAM, DWORD time, POINT pt) from the caller's buffer into a kernel-stack local;
 *     the probe clamps the pointer to the user-mode limit and touches the (possibly clamped)
 *     address first - this is the usermode-buffer-probe pattern, NOT a __try/__except guard, so a bad lpMsg is NOT this area's business (that
 *     belongs to kprobe_user_pointers, the dedicated area for probe-guarded syscalls) - every test
 *     here uses a real, on-stack MSG.
 *   - validates the copied message id (the second qword, low 32 bits): the message id must be < 0x20000 (WM_APP range tops out at 0xBFFF,
 *     registered-message range at 0xC000..0xFFFF, so this rejects anything with bit 17+ set, a
 *     much larger ceiling than any real message ever uses). On failure: UserSetLastError(0x57 ==
 *     ERROR_INVALID_PARAMETER == 87), and returns (the return value on this path is not
 *     documented; the public wrapper, user32!CallMsgFilterW/A, treats it as returning BOOL).
 *   - on success, conditionally calls the WH_SYSMSGFILTER hooks then the WH_MSGFILTER
 *     hooks depending on a per-thread/per-desktop "hooks installed" bitmask, copies the
 *     (possibly hook-modified) message back out to the caller's buffer, and returns.
 *
 * user32.dll!CallMsgFilterW and CallMsgFilterA both forward to this call
 * after taking an MSG from their own (LPMSG,int) parameters - so direct dispatch and the public API take the identical path; this
 * file's "matches public api" tests build the same MSG by hand and call both.
 */
#include "t.h"
#include "k.h"

#pragma pack(push, 1)
struct msgfilter_msg { ULONG_PTR hwnd; UINT message; UINT pad; ULONG_PTR wParam; LONG_PTR lParam; DWORD time; LONG ptx, pty; };
#pragma pack(pop)
/* MSG on x64 is HWND(8) UINT(4)+pad(4) WPARAM(8) LPARAM(8) DWORD time(4)+pad(4) POINT(8) = 0x30
 * bytes, matching the 6 qwords win32kfull copies - built explicitly rather than including
 * <windows.h>'s MSG to keep the exact byte layout self-evident at the call site. */

UDECL(BOOL, NtUserCallMsgFilter, (MSG *lpMsg, int nCode));

#define ERR_INVALID_PARAMETER 87u /* 0x57 - win32kfull!NtUserCallMsgFilter's only UserSetLastError call */

static void fill_valid_msg(MSG *m, UINT message) {
    ZeroMemory(m, sizeof(*m));
    m->message = message;
    m->time = GetTickCount();
}

/* =============================================================================================
 * Valid message ids (message < 0x20000) with no hooks installed: dispatch succeeds, no error.
 * ============================================================================================= */
TEST(sc_callxxx_dispatch_msgfilter_valid_message_no_hooks_succeeds) {
    REQUIRE(UN(NtUserCallMsgFilter));
    MSG m; fill_valid_msg(&m, WM_NULL);
    SetLastError(0);
    BOOL r = UN(NtUserCallMsgFilter)(&m, -1 /* MSGF_... / no filter hook code in use */);
    t_note("NtUserCallMsgFilter(WM_NULL, -1) = %d, err=%lu (no hook chain installed -> no "
           "WH_MSGFILTER delivered, so %d is the documented 'not filtered' result)", r, GetLastError(), r);
    CHECK(GetLastError() != ERR_INVALID_PARAMETER); /* a valid message must never take the 0x57 path */
}
/* user32!CallMsgFilterW/A only reach the syscall when the thread is a GUI thread AND hook bits
 * are set (Self+0x820 -> +0x10 or Self+0x838 nonzero), and never for messages 0x240 / 0x119 /
 * 0x11b. With no WH_MSGFILTER/WH_SYSMSGFILTER hook installed they return FALSE without any
 * kernel call, so the public result is not comparable to the direct stub's (the stub's return
 * value is a lost-signature tail call). What is comparable, and provable from the analysis, is the
 * message round trip: the kernel copies the (unhooked) message back unchanged. */
TEST(sc_callxxx_dispatch_msgfilter_direct_roundtrips_message_unchanged) {
    REQUIRE(UN(NtUserCallMsgFilter));
    MSG a; fill_valid_msg(&a, WM_USER);
    a.hwnd = (HWND)(ULONG_PTR)0x1234; a.wParam = 0x1122; a.lParam = 0x3344; a.pt.x = 17; a.pt.y = -9;
    MSG b = a;
    UN(NtUserCallMsgFilter)(&a, 0);
    CHECK(a.hwnd == b.hwnd && a.message == b.message && a.wParam == b.wParam && a.lParam == b.lParam);
    CHECK(a.time == b.time && a.pt.x == b.pt.x && a.pt.y == b.pt.y);
    CHECKF(GetLastError() != ERR_INVALID_PARAMETER || t_is_wine(), "valid message took the 0x57 path");
}
TEST(sc_callxxx_dispatch_msgfilter_callmsgfilter_public_does_not_fault) {
    REQUIRE(U(CallMsgFilterW)); REQUIRE(U(CallMsgFilterA));
    MSG a; fill_valid_msg(&a, WM_USER + 1);
    MSG b = a;
    BOOL w = U(CallMsgFilterW)(&a, 0);
    BOOL x = U(CallMsgFilterA)(&b, 0);
    t_note("CallMsgFilterW=%d CallMsgFilterA=%d (FALSE expected when no message-filter hook is installed)", w, x);
    CHECK(a.message == WM_USER + 1 && b.message == WM_USER + 1);
}

/* =============================================================================================
 * Out-of-range message id (bit 17+ set, i.e. message >= 0x20000) -> ERROR_INVALID_PARAMETER,
 * verified both via the direct stub and via CallMsgFilterW (which passes the id straight through
 * with no validation of its own - user32 analysis shows no range check before the syscall).
 * ============================================================================================= */
TEST(sc_callxxx_dispatch_msgfilter_message_id_too_large_fails) {
    REQUIRE(UN(NtUserCallMsgFilter));
    UINT bad_messages[] = { 0x20000, 0x20001, 0x40000, 0x80000000u, 0xffffffffu };
    for (size_t i = 0; i < sizeof(bad_messages)/sizeof(bad_messages[0]); i++) {
        MSG m; fill_valid_msg(&m, bad_messages[i]);
        SetLastError(0);
        UN(NtUserCallMsgFilter)(&m, 0);
        if (t_is_wine()) {
            /* Observed: Wine's win32u!NtUserCallMsgFilter does not implement the message-id
             * range check at all (last error is left at Wine's own prior unrelated value, never
             * set to 87) - a Wine implementation difference, not evidence this expectation is
             * wrong on real Windows (which is the authority this area targets). */
            t_note("message 0x%lx under Wine: err=%lu (Wine does not enforce the id<0x20000 check)",
                   (unsigned long)bad_messages[i], GetLastError());
            continue;
        }
        CHECKF(GetLastError() == ERR_INVALID_PARAMETER,
               "message 0x%lx: got err %lu want %u", (unsigned long)bad_messages[i], GetLastError(), ERR_INVALID_PARAMETER);
    }
}
/* CallMsgFilterW has no range check of its own, but without a message-filter hook it returns FALSE
 * before the syscall (see above), so last error is either untouched (no hook) or 87 (a hook made it
 * reach the kernel); anything else is wrong. */
TEST(sc_callxxx_dispatch_msgfilter_message_id_too_large_via_callmsgfilterw) {
    REQUIRE(U(CallMsgFilterW));
    MSG m; fill_valid_msg(&m, 0x20000);
    SetLastError(0x1111);
    U(CallMsgFilterW)(&m, 0);
    DWORD e = GetLastError();
    if (t_is_wine()) { t_note("CallMsgFilterW(0x20000) under Wine: err=%lu", e); return; }
    CHECKF(e == 0x1111 || e == ERR_INVALID_PARAMETER, "err %lu", e);
}
/* The exact boundary: 0x1ffff must succeed (bit 17 clear), 0x20000 must fail (bit 17 set) -
 * "uStack_48 & 0xfffe0000" is precisely "bits 17 and up". */
TEST(sc_callxxx_dispatch_msgfilter_message_id_boundary_0x1ffff_vs_0x20000) {
    REQUIRE(UN(NtUserCallMsgFilter));
    MSG ok; fill_valid_msg(&ok, 0x1ffff);
    SetLastError(0);
    UN(NtUserCallMsgFilter)(&ok, 0);
    CHECKF(GetLastError() != ERR_INVALID_PARAMETER, "0x1ffff wrongly rejected, err %lu", GetLastError());

    MSG bad; fill_valid_msg(&bad, 0x20000);
    SetLastError(0);
    UN(NtUserCallMsgFilter)(&bad, 0);
    if (t_is_wine()) { t_note("0x20000 under Wine: err=%lu (see msgfilter_message_id_too_large_fails)", GetLastError()); return; }
    EXPECT_ERR(ERR_INVALID_PARAMETER);
}

/* Field-preservation on failure: when the message is rejected, win32kfull returns via the
 * UserSetLastError(0x57) path BEFORE the  block, so the
 * caller's MSG buffer must be left exactly as it was (the kernel never wrote anything useful
 * back for a rejected message - only the earlier self-read-write probe touched it, which cannot
 * change its contents). */
TEST(sc_callxxx_dispatch_msgfilter_rejected_message_buffer_unmodified) {
    REQUIRE(UN(NtUserCallMsgFilter));
    MSG m; fill_valid_msg(&m, 0x30000);
    m.wParam = 0x1122334455667788ULL;
    m.lParam = (LPARAM)0x99AABBCCDDEEFF00LL;
    MSG before = m;
    UN(NtUserCallMsgFilter)(&m, 0);
    CHECK(m.message == before.message);
    CHECK(m.wParam == before.wParam);
    CHECK(m.lParam == before.lParam);
}
