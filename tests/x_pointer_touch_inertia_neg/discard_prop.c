/* x_pointer_touch_inertia_neg / discard_prop: NtUserDiscardPointerFrameMessages(id) and NtUserGetPointerProprietaryId(id, GUID *).
 *
 * DiscardPointerFrameMessages (user32!SkipPointerFrameMessages): id 0 and any id with a non-zero high word is
 * ERROR_INVALID_PARAMETER; id 1 (the mouse) is ERROR_ACCESS_DENIED while mouse-in-pointer is not enabled for the thread;
 * every other id with a zero high word is ERROR_INVALID_PARAMETER when the thread has no such pointer. The answer does not
 * depend on the calling thread.
 *
 * GetPointerProprietaryId: a pointer that is not one of the thread's pointers has no proprietary id, whatever the id: the
 * answer is FALSE with ERROR_NO_DATA (232), the GUID is not written, and an unwritable GUID pointer is never touched
 * (the failure comes first).
 *
 * Determinism: only symbolic results are printed.
 */
#include "x.h"

/* ---- DiscardPointerFrameMessages ---- */
#define DISC(nm, id, err) XT(discard_##nm) { NEED(NtUserDiscardPointerFrameMessages); XFAIL(UN(NtUserDiscardPointerFrameMessages)(id), err); }
DISC(id0,          0,          XE_INVALID_PARAMETER)
DISC(id1_mouse,    1,          XE_ACCESS_DENIED)
DISC(id2,          2,          XE_INVALID_PARAMETER)
DISC(id0x1234,     0x1234,     XE_INVALID_PARAMETER)
DISC(id0x7FFF,     0x7FFF,     XE_INVALID_PARAMETER)
DISC(id0x8000,     0x8000,     XE_INVALID_PARAMETER)
DISC(id0xFFFF,     0xFFFF,     XE_INVALID_PARAMETER)
DISC(id0x10000,    0x10000,    XE_INVALID_PARAMETER)
DISC(id0x10001,    0x10001,    XE_INVALID_PARAMETER)
DISC(id0x80000001, 0x80000001u, XE_INVALID_PARAMETER)
DISC(id0xFFFF0001, 0xFFFF0001u, XE_INVALID_PARAMETER)
DISC(all_ones,     0xFFFFFFFFu, XE_INVALID_PARAMETER)

static DWORD WINAPI discard_worker(LPVOID p) {
    t_module("user32.dll");
    U(GetDesktopWindow)();
    int *f = p;
    SET_ERR(0x4D2); if (UN(NtUserDiscardPointerFrameMessages)(1) || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    SET_ERR(0x4D2); if (UN(NtUserDiscardPointerFrameMessages)(0x1234) || GetLastError() != XE_INVALID_PARAMETER) (*f)++;
    return 0;
}
XT(discard_same_answers_on_another_thread) {
    NEED(NtUserDiscardPointerFrameMessages);
    int fails = 0;
    HANDLE t = CreateThread(NULL, 0, discard_worker, &fails, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(fails, 0);
}
XT(discard_repeated_calls_do_not_change_the_answer) {
    NEED(NtUserDiscardPointerFrameMessages);
    for (int i = 0; i < 100; i++) { XFAIL(UN(NtUserDiscardPointerFrameMessages)(1), XE_ACCESS_DENIED); XFAIL(UN(NtUserDiscardPointerFrameMessages)(2), XE_INVALID_PARAMETER); }
}
XT(discard_user32_front_door_matches) {
    NEED(NtUserDiscardPointerFrameMessages);
    BOOL (WINAPI *f)(UINT) = (void *)t_proc_in("user32.dll", "SkipPointerFrameMessages");
    if (!f) { t_skip("user32!SkipPointerFrameMessages not exported"); return; }
    XFAIL(f(0), XE_INVALID_PARAMETER);
    XFAIL(f(1), XE_ACCESS_DENIED);
    XFAIL(f(0x1234), XE_INVALID_PARAMETER);
    XFAIL(f(0x10001), XE_INVALID_PARAMETER);
}

/* ---- GetPointerProprietaryId ---- */
static void propid(UINT id) {
    NEED(NtUserGetPointerProprietaryId);
    unsigned char g[16]; memset(g, 0xCD, sizeof g);
    XFAIL(UN(NtUserGetPointerProprietaryId)(id, g), XE_NO_DATA);
    int clean = 1; for (unsigned i = 0; i < sizeof g; i++) if (g[i] != 0xCD) clean = 0;
    CHECKF(clean, "GUID written on failure");
    XFAIL(UN(NtUserGetPointerProprietaryId)(id, NULL), XE_NO_DATA);
    XFAIL(UN(NtUserGetPointerProprietaryId)(id, XP_BADPTR), XE_NO_DATA);
    XFAIL(UN(NtUserGetPointerProprietaryId)(id, XP_KERNELPTR), XE_NO_DATA);
}
#define PROP(nm, id) XT(propid_##nm) { propid(id); }
PROP(id0, 0)
PROP(id1_mouse, 1)
PROP(id2, 2)
PROP(id0x1234, 0x1234)
PROP(id0x7FFF, 0x7FFF)
PROP(id0xFFFF, 0xFFFF)
PROP(id0x10000, 0x10000)
PROP(id0x10001, 0x10001)
PROP(id0x80000000, 0x80000000u)
PROP(all_ones, 0xFFFFFFFFu)

static DWORD WINAPI prop_worker(LPVOID p) {
    t_module("user32.dll"); U(GetDesktopWindow)();
    int *f = p; unsigned char g[16];
    SET_ERR(0x4D2); if (UN(NtUserGetPointerProprietaryId)(1, g) || GetLastError() != XE_NO_DATA) (*f)++;
    return 0;
}
XT(propid_same_answer_on_another_thread) {
    NEED(NtUserGetPointerProprietaryId);
    int fails = 0;
    HANDLE t = CreateThread(NULL, 0, prop_worker, &fails, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(fails, 0);
}
XT(propid_repeated_calls_do_not_change_the_answer) {
    NEED(NtUserGetPointerProprietaryId);
    unsigned char g[16];
    for (int i = 0; i < 100; i++) XFAIL(UN(NtUserGetPointerProprietaryId)(1 + (i & 3), g), XE_NO_DATA);
}
