/* Gamma ramp syscalls (win32kfull NtGdiGetDeviceGammaRamp/SetDeviceGammaRamp, win32kbase NtGdiGetAppliedDeviceGammaRamp/
 * GetGammaRampCapability/SetPrivateDeviceGammaRamp).
 *
 * Analysis facts used for the hard checks:
 *  - Get/SetDeviceGammaRamp: ramp == NULL -> EngSetLastError(6) and return 0, before the DC is even looked at. Otherwise
 *    the call fails when the DC cannot be locked or is not a direct display DC
 *    -> return 0 with EngSetLastError(0x57).
 *  - GetAppliedDeviceGammaRamp: NULL ramp -> 0x57; bad DC / non-direct DC -> 0x57; the user buffer is written only on success.
 *  - GetGammaRampCapability: bad or non-direct DC -> 0 (no error code); otherwise 1 only for a gamma-capable LDDM device.
 *  - SetPrivateDeviceGammaRamp: NULL ramp -> 0; copies 0x600 bytes from the caller; bad DC -> 0 (also 0 when the DC is invalid or not direct).
 * The ramp is 3 channels x 256 WORD = 0x600 bytes and must be 2-byte aligned (odd pointer -> ExRaiseDatatypeMisalignment,
 * which this area never provokes). */
#include "r2.h"

#define RAMP_WORDS (3 * 256)
static WORD g_ramp[RAMP_WORDS + 8];

static void fill_sentinel(WORD *r) { for (int i = 0; i < RAMP_WORDS; i++) r[i] = 0xA5A5; }
static int is_sentinel(const WORD *r) { for (int i = 0; i < RAMP_WORDS; i++) if (r[i] != 0xA5A5) return 0; return 1; }
static void identity(WORD *r) { for (int c = 0; c < 3; c++) for (int i = 0; i < 256; i++) r[c * 256 + i] = (WORD)(i << 8); }

/* A set of handles that must never lock as a direct display DC. */
struct baddc { const char *what; HDC dc; int own; };
static int collect_bad_dcs(struct baddc *o, HWND *wnd_out) {
    int n = 0;
    o[n++] = (struct baddc){ "NULL", NULL, 0 };
    o[n++] = (struct baddc){ "0x12345", (HDC)(ULONG_PTR)0x12345, 0 };
    o[n++] = (struct baddc){ "-1", (HDC)(ULONG_PTR)-1, 0 };
    o[n++] = (struct baddc){ "stock pen", (HDC)U(GetStockObject)(BLACK_PEN), 0 };
    o[n++] = (struct baddc){ "stock brush", (HDC)U(GetStockObject)(WHITE_BRUSH), 0 };
    HDC mem = U(CreateCompatibleDC)(NULL);                                  /* DCTYPE_MEMORY: type word at +0x20 != 0 */
    o[n++] = (struct baddc){ "memory DC", mem, 1 };
    HDC sc = r2_screen_dc();
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);                    /* DCTYPE_INFO */
    o[n++] = (struct baddc){ "information DC", ic, 2 };
    HDC dead = U(CreateCompatibleDC)(sc);
    if (dead) U(DeleteDC)(dead);
    o[n++] = (struct baddc){ "deleted DC", dead, 0 };
    r2_release_dc(sc);
    HWND w = k_make_window("r2_gamma_hwnd");
    *wnd_out = w;
    o[n++] = (struct baddc){ "HWND value", (HDC)w, 0 };
    return n;
}
static void free_bad_dcs(struct baddc *o, int n, HWND w) {
    for (int i = 0; i < n; i++) if (o[i].dc && o[i].own) U(DeleteDC)(o[i].dc);
    if (w) U(DestroyWindow)(w);
}

T2(gamma_get_null_ramp_sets_error_6) {
    NEED_NT(NtGdiGetDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    SetLastError(0); CHECK(UN(NtGdiGetDeviceGammaRamp)(dc, NULL) == 0); EXPECT_ERR(6);
    SetLastError(0); CHECK(UN(NtGdiGetDeviceGammaRamp)(NULL, NULL) == 0); EXPECT_ERR(6);              /* ramp check precedes the DC lookup */
    SetLastError(0); CHECK(UN(NtGdiGetDeviceGammaRamp)((HDC)(ULONG_PTR)0x4444, NULL) == 0); EXPECT_ERR(6);
    r2_release_dc(dc);
}
T2(gamma_set_null_ramp_sets_error_6) {
    NEED_NT(NtGdiSetDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    SetLastError(0); CHECK(UN(NtGdiSetDeviceGammaRamp)(dc, NULL) == 0); EXPECT_ERR(6);
    SetLastError(0); CHECK(UN(NtGdiSetDeviceGammaRamp)(NULL, NULL) == 0); EXPECT_ERR(6);
    SetLastError(0); CHECK(UN(NtGdiSetDeviceGammaRamp)((HDC)(ULONG_PTR)0x4444, NULL) == 0); EXPECT_ERR(6);
    r2_release_dc(dc);
}
T2(gamma_get_invalid_and_non_display_dcs_fail_with_57) {
    NEED_NT(NtGdiGetDeviceGammaRamp); NEED_GUI();
    struct baddc b[12]; HWND w; int n = collect_bad_dcs(b, &w);
    for (int i = 0; i < n; i++) {
        if (!b[i].dc && strcmp(b[i].what, "NULL") && strcmp(b[i].what, "deleted DC")) continue;
        identity(g_ramp);
        SetLastError(0);
        int r = UN(NtGdiGetDeviceGammaRamp)(b[i].dc, g_ramp);
        CHECKF(r == 0, "%s: returned %d", b[i].what, r);
        CHECKF(GetLastError() == 0x57, "%s: error %lu want 87", b[i].what, GetLastError());
    }
    free_bad_dcs(b, n, w);
}
T2(gamma_set_invalid_and_non_display_dcs_fail_with_57) {
    NEED_NT(NtGdiSetDeviceGammaRamp); NEED_GUI();
    struct baddc b[12]; HWND w; int n = collect_bad_dcs(b, &w);
    for (int i = 0; i < n; i++) {
        if (!b[i].dc && strcmp(b[i].what, "NULL") && strcmp(b[i].what, "deleted DC")) continue;
        identity(g_ramp);
        SetLastError(0);
        int r = UN(NtGdiSetDeviceGammaRamp)(b[i].dc, g_ramp);
        CHECKF(r == 0, "%s: returned %d", b[i].what, r);
        CHECKF(GetLastError() == 0x57, "%s: error %lu want 87", b[i].what, GetLastError());
    }
    free_bad_dcs(b, n, w);
}
T2(gamma_applied_get_null_ramp_fails_57) {
    NEED_NT(NtGdiGetAppliedDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    SetLastError(0); CHECK(UN(NtGdiGetAppliedDeviceGammaRamp)(dc, NULL) == 0); EXPECT_ERR(0x57);
    SetLastError(0); CHECK(UN(NtGdiGetAppliedDeviceGammaRamp)(NULL, NULL) == 0); EXPECT_ERR(0x57);
    r2_release_dc(dc);
}
T2(gamma_applied_get_invalid_dcs_fail_and_leave_buffer_untouched) {
    NEED_NT(NtGdiGetAppliedDeviceGammaRamp); NEED_GUI();
    struct baddc b[12]; HWND w; int n = collect_bad_dcs(b, &w);
    for (int i = 0; i < n; i++) {
        if (!b[i].dc && strcmp(b[i].what, "NULL") && strcmp(b[i].what, "deleted DC")) continue;
        fill_sentinel(g_ramp);
        SetLastError(0);
        int r = UN(NtGdiGetAppliedDeviceGammaRamp)(b[i].dc, g_ramp);
        CHECKF(r == 0, "%s: returned %d", b[i].what, r);
        /* Windows 10 reports ERROR_INVALID_PARAMETER for every DC the call cannot lock as a display DC */
        CHECKF(GetLastError() == 0x57, "%s: error %lu want 87", b[i].what, GetLastError());
        CHECKF(is_sentinel(g_ramp), "%s: output ramp was modified by a failing call (copy-out happens only on success)", b[i].what);
    }
    free_bad_dcs(b, n, w);
}
T2(gamma_capability_is_zero_for_invalid_and_non_display_dcs) {
    NEED_NT(NtGdiGetGammaRampCapability); NEED_GUI();
    struct baddc b[12]; HWND w; int n = collect_bad_dcs(b, &w);
    for (int i = 0; i < n; i++) {
        if (!b[i].dc && strcmp(b[i].what, "NULL") && strcmp(b[i].what, "deleted DC")) continue;
        int r = UN(NtGdiGetGammaRampCapability)(b[i].dc);
        CHECKF(r == 0, "%s: capability %d", b[i].what, r);
    }
    free_bad_dcs(b, n, w);
}
T2(gamma_capability_on_screen_dc_is_boolean) {
    NEED_NT(NtGdiGetGammaRampCapability); NEED_GUI();
    HDC dc = r2_screen_dc();
    if (!dc) { t_skip("no screen DC"); return; }
    int r = UN(NtGdiGetGammaRampCapability)(dc);
    CHECKF(r == 0 || r == 1, "capability %d (handler returns 1 only for a gamma-capable LDDM device, else 0)", r);   /* PDEVOBJ::bGammaRampCapable && bLddmDriver */
    int r2 = UN(NtGdiGetGammaRampCapability)(dc);
    CHECKF(r2 == r, "capability not stable across calls: %d then %d", r, r2);
    t_note("screen DC gamma capability = %d", r);
    r2_release_dc(dc);
}
T2(gamma_private_set_null_ramp_returns_zero) {
    NEED_NT(NtGdiSetPrivateDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    CHECK(UN(NtGdiSetPrivateDeviceGammaRamp)(dc, NULL, 0) == 0);
    CHECK(UN(NtGdiSetPrivateDeviceGammaRamp)(dc, NULL, 1) == 0);
    CHECK(UN(NtGdiSetPrivateDeviceGammaRamp)(NULL, NULL, 0) == 0);
    r2_release_dc(dc);
}
T2(gamma_private_set_invalid_and_non_display_dcs_return_zero) {
    NEED_NT(NtGdiSetPrivateDeviceGammaRamp); NEED_GUI();
    struct baddc b[12]; HWND w; int n = collect_bad_dcs(b, &w);
    identity(g_ramp);
    for (int i = 0; i < n; i++) {
        if (!b[i].dc && strcmp(b[i].what, "NULL") && strcmp(b[i].what, "deleted DC")) continue;
        for (int flag = 0; flag < 2; flag++) {
            int r = UN(NtGdiSetPrivateDeviceGammaRamp)(b[i].dc, g_ramp, flag);
            CHECKF(r == 0, "%s flag=%d: returned %d", b[i].what, flag, r);
        }
    }
    free_bad_dcs(b, n, w);
}
T2(gamma_get_on_screen_dc_matches_user_path) {
    NEED_NT(NtGdiGetDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    if (!dc) { t_skip("no screen DC"); return; }
    static WORD a[RAMP_WORDS + 8], b[RAMP_WORDS + 8];
    memset(a, 0x11, sizeof a); memset(b, 0x22, sizeof b);
    SetLastError(0);
    int r = UN(NtGdiGetDeviceGammaRamp)(dc, a);
    if (!r) {
        CHECKF(GetLastError() == 0x57, "failed with error %lu; the handler only reports 87", GetLastError());
        t_note("display driver does not expose a gamma ramp here (GetDeviceGammaRamp -> 0/87)");
        r2_release_dc(dc); return;
    }
    CHECK(r == 1);
    BOOL u = U(GetDeviceGammaRamp) ? U(GetDeviceGammaRamp)(dc, b) : FALSE;     /* gdi32full forwards straight to the syscall */
    CHECK(u == TRUE);
    CHECK(!memcmp(a, b, RAMP_WORDS * 2));
    CHECKF(a[RAMP_WORDS] == 0x1111, "write past the 0x600-byte ramp");
    r2_release_dc(dc);
}
T2(gamma_applied_ramp_on_screen_dc_protocol) {
    NEED_NT(NtGdiGetAppliedDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    if (!dc) { t_skip("no screen DC"); return; }
    static WORD a[RAMP_WORDS + 8];
    for (int i = 0; i < RAMP_WORDS + 8; i++) a[i] = 0x7777;
    int r = UN(NtGdiGetAppliedDeviceGammaRamp)(dc, a);
    CHECKF(r == 0 || r == 1, "returned %d", r);
    if (r == 1) {
        CHECKF(a[RAMP_WORDS] == 0x7777 && a[RAMP_WORDS + 7] == 0x7777, "write past 0x600 bytes");
        int ident = 1;
        for (int c = 0; c < 3; c++) for (int i = 0; i < 256; i++) if (a[c * 256 + i] != (WORD)(i << 8)) ident = 0;
        t_note("applied ramp is %s", ident ? "the identity ramp (nothing programmed: handler synthesises i<<8 when no ramp table is installed)" : "a programmed non-identity ramp");
    } else {
        CHECK(GetLastError() == 0x57 || GetLastError() == 0xe);
    }
    r2_release_dc(dc);
}
T2(gamma_set_same_ramp_roundtrip_and_restore) {
    NEED_NT(NtGdiGetDeviceGammaRamp); NEED_NT(NtGdiSetDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    if (!dc) { t_skip("no screen DC"); return; }
    static WORD saved[RAMP_WORDS + 8], cur[RAMP_WORDS + 8];
    if (!UN(NtGdiGetDeviceGammaRamp)(dc, saved)) { t_skip("no readable gamma ramp on this display"); r2_release_dc(dc); return; }
    int ok = UN(NtGdiSetDeviceGammaRamp)(dc, saved);                          /* writes back exactly what was read */
    if (!ok) t_note("re-applying the read-back ramp was refused (error %lu)", GetLastError());
    memset(cur, 0, sizeof cur);
    int r = UN(NtGdiGetDeviceGammaRamp)(dc, cur);
    CHECK(r == 1);
    if (ok) CHECKF(!memcmp(saved, cur, RAMP_WORDS * 2), "ramp changed after setting the identical ramp");
    UN(NtGdiSetDeviceGammaRamp)(dc, saved);                                    /* restore */
    r2_release_dc(dc);
}
T2(gamma_set_ramp_on_memory_dc_does_not_alter_screen_ramp) {
    NEED_NT(NtGdiGetDeviceGammaRamp); NEED_NT(NtGdiSetDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    if (!dc) { t_skip("no screen DC"); return; }
    static WORD before[RAMP_WORDS + 8], after[RAMP_WORDS + 8], junk[RAMP_WORDS + 8];
    if (!UN(NtGdiGetDeviceGammaRamp)(dc, before)) { t_skip("no readable gamma ramp"); r2_release_dc(dc); return; }
    HDC mem = U(CreateCompatibleDC)(dc);
    for (int i = 0; i < RAMP_WORDS; i++) junk[i] = (WORD)(i * 7);
    SetLastError(0);
    CHECK(UN(NtGdiSetDeviceGammaRamp)(mem, junk) == 0);                        /* rejected at the DC type test, never reaches the driver */
    EXPECT_ERR(0x57);
    UN(NtGdiGetDeviceGammaRamp)(dc, after);
    CHECK(!memcmp(before, after, RAMP_WORDS * 2));
    U(DeleteDC)(mem);
    r2_release_dc(dc);
}
T2(gamma_ramp_bytes_beyond_0x600_untouched_on_get) {
    NEED_NT(NtGdiGetDeviceGammaRamp); NEED_GUI();
    HDC dc = r2_screen_dc();
    if (!dc) { t_skip("no screen DC"); return; }
    static WORD a[RAMP_WORDS + 64];
    for (int i = 0; i < RAMP_WORDS + 64; i++) a[i] = 0x3C3C;
    int r = UN(NtGdiGetDeviceGammaRamp)(dc, a);
    for (int i = RAMP_WORDS; i < RAMP_WORDS + 64; i++) if (a[i] != 0x3C3C) { CHECKF(0, "word %d beyond the ramp changed (r=%d)", i, r); break; }
    r2_release_dc(dc);
}
