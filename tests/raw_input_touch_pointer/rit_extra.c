/* Pointer promotion / downlevel touchpad / precision touchpad entry points whose validation is
 * visible in win32kfull/win32kbase and needs no real device. */
#include "rit.h"

/* win32kfull!NtUserPromotePointer(UINT pointerId, UINT type) */
UDECL(LONGLONG, NtUserPromotePointer, (UINT, UINT));
/* win32kbase!NtUserDownlevelTouchpad(int pointerId, ULONGLONG *state) */
UDECL(UINT, NtUserDownlevelTouchpad, (int, ULONGLONG *));
/* win32kbase!NtUserGetPrecisionTouchPadConfiguration(UINT *cfg: first dword must be 0, 0x18 bytes) */
UDECL(LONGLONG, NtUserGetPrecisionTouchPadConfiguration, (void *));
UDECL(ULONGLONG, NtUserAutoPromoteMouseInPointer, (void));

static NTSTATUS last_status(void) {
    /* TEB.LastStatusValue (x64 +0x1250) is what UserSetLastStatus writes. */
    return *(NTSTATUS *)((char *)NtCurrentTeb() + 0x1250);
}

/* NtUserPromotePointer: id 0, id with a high word, and id 1 (the mouse) fail the first test; every
 * one of them ends in UserSetLastError(0x57) without touching the touch processor. */
RT(extra_promote_bad_pointer_id) {
    WINE_UNIMPL("NtUserPromotePointer");
    REQUIRE(UN(NtUserPromotePointer));
    rit_gui();
    UINT ids[] = { 0, 1, 0x10000, 0x10001, 0x80000000, 0xffffffff, 0xffff0000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SET_ERR(0);
        CHECKF((UN(NtUserPromotePointer)(ids[i], 0) & 0xffffffff) == 0, "id 0x%x", ids[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
/* A well-formed id with an unrecognised target type value never reaches the pointer-to-mouse promotion:
 * (type & 0xffffff) must be 0 (then type must be 0) or one of the enumerated 0x1000x/0x2000x/
 * 0x4000x/0x8000x ranges. */
RT(extra_promote_bad_type) {
    WINE_UNIMPL("NtUserPromotePointer");
    REQUIRE(UN(NtUserPromotePointer));
    rit_gui();
    UINT types[] = { 1, 2, 0x5, 0xff, 0x10000, 0x10003, 0x20000, 0x20003, 0x40000, 0x40003, 0x80000,
                     0x80003, 0x100000, 0x1000000, 0x7fffffff, 0xffffffff, 0x01000000 };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        SET_ERR(0);
        CHECKF((UN(NtUserPromotePointer)(0x1234, types[i]) & 0xffffffff) == 0, "type 0x%x", types[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
/* win32kbase!NtUserDownlevelTouchpad: id 0 / high word / 1 are 87 before the state is read. */
RT(extra_downlevel_bad_id) {
    WINE_UNIMPL("NtUserDownlevelTouchpad");
    REQUIRE(UN(NtUserDownlevelTouchpad));
    rit_gui();
    int ids[] = { 0, 1, 0x10000, 0x10001, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        ULONGLONG st = 0;
        SET_ERR(0);
        CHECKF(UN(NtUserDownlevelTouchpad)(ids[i], &st) == 0, "id 0x%x", ids[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
/* The state's low dword is a command 0..2; larger values are 87 before the downlevel input path. */
RT(extra_downlevel_bad_state) {
    WINE_UNIMPL("NtUserDownlevelTouchpad");
    REQUIRE(UN(NtUserDownlevelTouchpad));
    rit_gui();
    ULONGLONG vals[] = { 3, 0x100, 0xffffffffull, 0x123400000003ull };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        ULONGLONG st = vals[i];
        SET_ERR(0);
        CHECKF(UN(NtUserDownlevelTouchpad)(0x1234, &st) == 0, "state 0x%llx", vals[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
/* NtUserGetPrecisionTouchPadConfiguration: a non-zero version dword fails with
 * UserSetLastStatus(STATUS_NOT_IMPLEMENTED) before the config is read, and writes nothing back. */
RT(extra_ptp_config_bad_version) {
    WINE_UNIMPL("NtUserGetPrecisionTouchPadConfiguration");
    REQUIRE(UN(NtUserGetPrecisionTouchPadConfiguration));
    rit_gui();
    DWORD vals[] = { 1, 2, 0x100, 0xffffffff };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        ULONGLONG cfg[3];                       /* 0x18 bytes, 8-aligned */
        memset(cfg, 0xaa, sizeof cfg);
        *(DWORD *)cfg = vals[i];
        CHECKF(UN(NtUserGetPrecisionTouchPadConfiguration)(cfg) == 0, "ver 0x%lx", vals[i]);
        if (last_status() != (NTSTATUS)0xC0000002)
            t_note("last NTSTATUS 0x%lx (UserSetLastStatus(0xc0000002) expected)", (unsigned long)last_status());
        CHECK(cfg[1] == 0xaaaaaaaaaaaaaaaaull && cfg[2] == 0xaaaaaaaaaaaaaaaaull);
    }
}
/* NtUserAutoPromoteMouseInPointer: refused (error 5) unless the calling thread has mouse-in-pointer
 * enabled; a thread that never enabled it is the common case. */
RT(extra_autopromote_not_enabled) {
    WINE_UNIMPL("NtUserAutoPromoteMouseInPointer");
    REQUIRE(UN(NtUserAutoPromoteMouseInPointer));
    REQUIRE(UN(NtUserIsMouseInPointerEnabled));
    rit_gui();
    if (UN(NtUserIsMouseInPointerEnabled)()) { t_skip("mouse-in-pointer already enabled in this process"); return; }
    SET_ERR(0);
    CHECK(UN(NtUserAutoPromoteMouseInPointer)() == 0);
    EXPECT_ERR(E_ACCESS_DENIED);
}
