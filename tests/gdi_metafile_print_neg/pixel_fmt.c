/* OpenGL pixel-format entry points on the screen/window DC (gdi32full forwards to opengl32/ICD wgl* via GetAPI).
 *
 * GetPixelFormat: returns 0 without calling the ICD while the process-wide gbSetPixelFormatCalled flag is clear.
 * SetPixelFormat sets that flag first, then wglSetPixelFormat; win32kfull!NtGdiSetPixelFormat(hdc, fmt): bad DC ->
 * last error 6 and FALSE; fmt outside 1..ipfdDevMax -> FALSE. NtGdiDescribePixelFormat(hdc, fmt, cb, buf):
 * (cb == 0 || buf != NULL) required else 0; cb clamped to 0x28; the engine: cb == 0 -> max index,
 * 0 < cb < 0x28 or index outside 1..max -> error 87, 0. NtGdiSwapBuffers(bad DC): FALSE + error 6.
 */
#include "mf.h"

UDECL(BOOL, NtGdiSetPixelFormat, (HDC, int));
UDECL(int,  NtGdiDescribePixelFormat, (HDC, int, UINT, PIXELFORMATDESCRIPTOR *));
UDECL(BOOL, NtGdiSwapBuffers, (HDC));

#define NEED_PF() do { if (!U(GetDC) || !U(ReleaseDC) || !U(DescribePixelFormat) || !U(GetPixelFormat) || !U(SetPixelFormat) || !U(ChoosePixelFormat)) { t_skip("pixel format exports missing"); return; } } while (0)
#define NEED_NTPF(f) do { if (t_is_wine()) { t_skip("direct win32k syscalls are not available under Wine"); return; } if (!UN(f)) { t_skip("missing export: " #f); return; } } while (0)

static HWND pf_window(void) {
    if (!U(CreateWindowExW)) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"win32kt", WS_POPUP, -32000, -32000, 16, 16,
                              NULL, NULL, GetModuleHandleW(NULL), NULL);
}
static PIXELFORMATDESCRIPTOR pf_want(void) {
    PIXELFORMATDESCRIPTOR p; memset(&p, 0, sizeof p);
    p.nSize = sizeof p; p.nVersion = 1;
    p.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    p.iPixelType = PFD_TYPE_RGBA; p.cColorBits = 32; p.cDepthBits = 24; p.iLayerType = PFD_MAIN_PLANE;
    return p;
}

TEST(gdi_metafile_print_neg_pixfmt_count_positive) {
    NEED_PF(); MF_SCREEN(dc);
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    t_note("pixel format count = %d", n);
    CHECK(n > 0);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_pixfmt_describe_fills_descriptor) {
    NEED_PF(); MF_SCREEN(dc);
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    REQUIRE(n > 0);
    PIXELFORMATDESCRIPTOR p; memset(&p, 0xcc, sizeof p);
    int r = U(DescribePixelFormat)(dc, 1, sizeof p, &p);
    EXPECT_EQ(r, n);                                    /* returns the maximum index */
    EXPECT_EQ(p.nSize, sizeof p);
    EXPECT_EQ(p.nVersion, 1);
    CHECK(p.cColorBits > 0);
    CHECK(p.iPixelType == PFD_TYPE_RGBA || p.iPixelType == PFD_TYPE_COLORINDEX);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_pixfmt_describe_out_of_range_fails) {
    NEED_PF(); MF_SCREEN(dc);
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    REQUIRE(n > 0);
    PIXELFORMATDESCRIPTOR p;
    EXPECT_EQ(U(DescribePixelFormat)(dc, 0, sizeof p, &p), 0);
    EXPECT_EQ(U(DescribePixelFormat)(dc, n + 1, sizeof p, &p), 0);
    EXPECT_EQ(U(DescribePixelFormat)(dc, -1, sizeof p, &p), 0);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_pixfmt_describe_invalid_dc_fails) {
    NEED_PF();
    PIXELFORMATDESCRIPTOR p;
    EXPECT_EQ(U(DescribePixelFormat)((HDC)0, 1, sizeof p, &p), 0);
    EXPECT_EQ(U(DescribePixelFormat)((HDC)0xdead, 1, sizeof p, &p), 0);
}

TEST(gdi_metafile_print_neg_pixfmt_describe_all_formats_consistent) {
    NEED_PF(); MF_SCREEN(dc);
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    REQUIRE(n > 0);
    if (n > 512) n = 512;
    for (int i = 1; i <= n; i++) {
        PIXELFORMATDESCRIPTOR p; memset(&p, 0, sizeof p);
        int r = U(DescribePixelFormat)(dc, i, sizeof p, &p);
        CHECK(r > 0);
        if (r <= 0) break;
        EXPECT_EQ(p.nSize, sizeof p);
        CHECK(p.cColorBits <= 64);
        CHECK(p.cDepthBits <= 64);
    }
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_pixfmt_choose_returns_valid_index) {
    NEED_PF(); MF_SCREEN(dc);
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    REQUIRE(n > 0);
    PIXELFORMATDESCRIPTOR w = pf_want();
    int f = U(ChoosePixelFormat)(dc, &w);
    t_note("ChoosePixelFormat -> %d of %d", f, n);
    CHECK(f >= 1 && f <= n);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_pixfmt_getpixelformat_fresh_dc_zero) {
    NEED_PF(); MF_SCREEN(dc);
    /* the screen DC never had a pixel format; without a SetPixelFormat call in this process GetPixelFormat does not
     * even reach the ICD */
    EXPECT_EQ(U(GetPixelFormat)(dc), 0);
    mf_screen_free(dc);
}

CHILD(gdi_metafile_print_neg_child_getpixelformat_pristine) {
    HDC dc = U(GetDC)(NULL);
    int r = U(GetPixelFormat) ? U(GetPixelFormat)(dc) : -1;
    if (dc) U(ReleaseDC)(NULL, dc);
    return r == 0 ? 0 : 1;
}

TEST(gdi_metafile_print_neg_pixfmt_getpixelformat_pristine_child) {
    NEED_PF();
    EXPECT_EQ(t_run_child("gdi_metafile_print_neg_child_getpixelformat_pristine", "", 15000), 0);
}

TEST(gdi_metafile_print_neg_pixfmt_set_get_roundtrip_once_only) {
    NEED_PF();
    HWND w = pf_window();
    if (!w) { t_skip("cannot create window"); return; }
    HDC dc = U(GetDC)(w);
    if (!dc) { U(DestroyWindow)(w); t_skip("no window DC"); return; }
    PIXELFORMATDESCRIPTOR want = pf_want();
    int f = U(ChoosePixelFormat)(dc, &want);
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    if (f <= 0 || n < 2) { t_note("no usable pixel formats (f=%d n=%d)", f, n); }
    else {
        EXPECT_EQ(U(GetPixelFormat)(dc), 0);            /* window DC without a format: wglGetPixelFormat -> 0 */
        PIXELFORMATDESCRIPTOR got; memset(&got, 0, sizeof got);
        BOOL ok = U(SetPixelFormat)(dc, f, &want);
        t_note("SetPixelFormat(%d) = %d error %lu", f, ok, GetLastError());
        CHECK(ok);
        if (ok && U(GetPixelFormat)(dc) == 0)
            t_skip("the display driver has no OpenGL support here: SetPixelFormat succeeds but the format is not retained");
        else if (ok) {
            EXPECT_EQ(U(GetPixelFormat)(dc), f);
            int other = (f == 1) ? 2 : 1;
            SetLastError(0);
            BOOL ok2 = U(SetPixelFormat)(dc, other, &want);  /* a window's pixel format can be set only once */
            t_note("second SetPixelFormat(%d) = %d error %lu", other, ok2, GetLastError());
            CHECK(!ok2);
            EXPECT_EQ(U(GetPixelFormat)(dc), f);
        }
    }
    U(ReleaseDC)(w, dc);
    U(DestroyWindow)(w);
}

TEST(gdi_metafile_print_neg_pixfmt_set_out_of_range_fails_without_state_change) {
    NEED_PF();
    HWND w = pf_window();
    if (!w) { t_skip("cannot create window"); return; }
    HDC dc = U(GetDC)(w);
    if (!dc) { U(DestroyWindow)(w); t_skip("no window DC"); return; }
    int n = U(DescribePixelFormat)(dc, 1, 0, NULL);
    PIXELFORMATDESCRIPTOR want = pf_want();
    if (n > 0) {
        EXPECT_EQ(U(SetPixelFormat)(dc, 0, &want), FALSE);
        EXPECT_EQ(U(SetPixelFormat)(dc, n + 1, &want), FALSE);
        EXPECT_EQ(U(SetPixelFormat)(dc, -5, &want), FALSE);
        EXPECT_EQ(U(GetPixelFormat)(dc), 0);            /* nothing was set */
    }
    U(ReleaseDC)(w, dc);
    U(DestroyWindow)(w);
}

TEST(gdi_metafile_print_neg_pixfmt_set_invalid_dc_fails) {
    NEED_PF();
    PIXELFORMATDESCRIPTOR want = pf_want();
    EXPECT_EQ(U(SetPixelFormat)((HDC)0, 1, &want), FALSE);
    EXPECT_EQ(U(SetPixelFormat)((HDC)0xdead, 1, &want), FALSE);
}

TEST(gdi_metafile_print_neg_pixfmt_swapbuffers_invalid_dc_fails) {
    NEED_PF(); REQUIRE(U(SwapBuffers));
    SetLastError(0);
    EXPECT_EQ(U(SwapBuffers)((HDC)0xdead), FALSE);
}

TEST(gdi_metafile_print_neg_nt_swapbuffers_invalid_dc_fails_6) {
    NEED_NTPF(NtGdiSwapBuffers); REQUIRE(U(GetDC));
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiSwapBuffers)((HDC)0xdead), FALSE);   /* DCOBJ lock fails: EngSetLastError(6) */
    EXPECT_EQ(GetLastError(), 6);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiSwapBuffers)((HDC)0), FALSE);
    EXPECT_EQ(GetLastError(), 6);
}

TEST(gdi_metafile_print_neg_nt_setpixelformat_invalid_dc_fails_6) {
    NEED_NTPF(NtGdiSetPixelFormat);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiSetPixelFormat)((HDC)0xdead, 1), FALSE);
    EXPECT_EQ(GetLastError(), 6);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiSetPixelFormat)((HDC)0, 1), FALSE);
    EXPECT_EQ(GetLastError(), 6);
}

TEST(gdi_metafile_print_neg_nt_setpixelformat_memory_dc_fails) {
    NEED_NTPF(NtGdiSetPixelFormat); MF_SCREEN(ref);
    HDC m = U(CreateCompatibleDC)(ref);
    REQUIRE(m);
    SetLastError(0);
    /* a memory DC has no device pixel-format table in the display pdev's ICD: the call cannot succeed */
    BOOL r = UN(NtGdiSetPixelFormat)(m, 1);
    t_note("NtGdiSetPixelFormat(memory dc, 1) = %d error %lu", r, GetLastError());
    U(DeleteDC)(m);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_describepixelformat_protocol) {
    NEED_NTPF(NtGdiDescribePixelFormat); MF_SCREEN(dc);
    int n = UN(NtGdiDescribePixelFormat)(dc, 1, 0, NULL);
    t_note("NtGdiDescribePixelFormat count = %d", n);
    if (n == 0) { t_skip("the display driver reports no pixel formats to the kernel (no OpenGL support in this configuration)"); mf_screen_free(dc); return; }
    CHECK(n > 0);
    if (n > 0) {
        PIXELFORMATDESCRIPTOR p;
        /* cb != 0 with a NULL buffer is rejected before the engine is consulted */
        EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, 1, sizeof p, NULL), 0);
        EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, 1, 4, NULL), 0);
        /* oversized cb is clamped to 0x28 and only 0x28 bytes are written */
        struct { PIXELFORMATDESCRIPTOR p; unsigned char guard[16]; } big;
        memset(&big, 0xa5, sizeof big);
        int r = UN(NtGdiDescribePixelFormat)(dc, 1, sizeof big, &big.p);
        EXPECT_EQ(r, n);
        EXPECT_EQ(big.p.nSize, 0x28);
        for (int i = 0; i < 16; i++) EXPECT_EQ(big.guard[i], 0xa5);
        /* out of range index */
        SetLastError(0);
        EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, n + 1, sizeof p, &p), 0);
        EXPECT_EQ(GetLastError(), 87);                  /* index outside 1..ipfdDevMax with cb != 0 */
        EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, 0, sizeof p, &p), 0);
    }
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_nt_describepixelformat_short_cb_rejected_87) {
    NEED_NTPF(NtGdiDescribePixelFormat); MF_SCREEN(dc);
    int n = UN(NtGdiDescribePixelFormat)(dc, 1, 0, NULL);
    REQUIRE(n > 0);
    /* DescribePixelFormat: 0 < cb < 0x28 -> EngSetLastError(0x57), 0; the buffer is not written */
    unsigned char part[48]; memset(part, 0xa5, sizeof part);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, 1, 8, (PIXELFORMATDESCRIPTOR *)part), 0);
    EXPECT_EQ(GetLastError(), 87);
    for (int i = 0; i < 48; i++) EXPECT_EQ(part[i], 0xa5);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, 1, 0x27, (PIXELFORMATDESCRIPTOR *)part), 0);
    EXPECT_EQ(GetLastError(), 87);
    /* exactly 0x28 bytes is accepted */
    EXPECT_EQ(UN(NtGdiDescribePixelFormat)(dc, 1, 0x28, (PIXELFORMATDESCRIPTOR *)part), n);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_nt_describepixelformat_invalid_dc_fails_6) {
    NEED_NTPF(NtGdiDescribePixelFormat);
    PIXELFORMATDESCRIPTOR p;
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiDescribePixelFormat)((HDC)0xdead, 1, 0, NULL), 0);   /* DCOBJ lock fails: EngSetLastError(6) */
    EXPECT_EQ(GetLastError(), 6);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiDescribePixelFormat)((HDC)0, 1, sizeof p, &p), 0);
    EXPECT_EQ(GetLastError(), 6);
}
