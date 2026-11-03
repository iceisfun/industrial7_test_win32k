/* Wrong handle types for the charset queries: a NULL handle, any GDI object that is not a DC, junk, a window handle and a
 * deleted DC all give DEFAULT_CHARSET (1) from GetTextCharset / GetTextCharsetInfo / NtGdiGetTextCharsetInfo with
 * ERROR_INVALID_HANDLE (6), zero the FONTSIGNATURE (24 bytes, nothing beyond), and NtGdiGetCharSet returns
 * DEFAULT_CHARSET in bits 16..23 with code page 0. */
#include "xf.h"

static void bad_gtci(int k) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo)); XH(x);
    HDC d = (HDC)x.h[k];
    for (int route = 0; route < 2; route++) {
        unsigned char buf[40]; memset(buf, 0xCC, sizeof buf);
        SetLastError(0);
        int r = route ? XF_NT(NtGdiGetTextCharsetInfo)(d, (FONTSIGNATURE *)buf, 0) : U(GetTextCharsetInfo)(d, (FONTSIGNATURE *)buf, 0);
        CHECKF(r == DEFAULT_CHARSET, "%s route %d", xh_name[k], route);
        EXPECT_ERR(6);
        int z = 1; for (int i = 0; i < 24; i++) if (buf[i]) z = 0;
        CHECKF(z, "%s route %d: signature zeroed", xh_name[k], route);
        for (int i = 24; i < 40; i++) CHECKF(buf[i] == 0xCC, "%s route %d: byte %d beyond the signature untouched", xh_name[k], route, i);
        SetLastError(0);
        r = route ? XF_NT(NtGdiGetTextCharsetInfo)(d, NULL, 0) : U(GetTextCharsetInfo)(d, NULL, 0);
        CHECKF(r == DEFAULT_CHARSET, "%s route %d, no signature", xh_name[k], route);
        EXPECT_ERR(6);
    }
    xh_close(&x);
}
static void bad_charset(int k) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetCharSet)); XH(x);
    HDC d = (HDC)x.h[k];
    SetLastError(0);
    CHECKF(U(GetTextCharset)(d) == DEFAULT_CHARSET, "%s", xh_name[k]); EXPECT_ERR(6);
    CHECKF(XF_NT(NtGdiGetCharSet)(d) == (ULONG)(DEFAULT_CHARSET << 16), "%s", xh_name[k]);
    xh_close(&x);
}
#define BT(nm, k) XT(bad_gtci_##nm) { bad_gtci(k); } XT(bad_charset_##nm) { bad_charset(k); }
BT(null, H_NULL) BT(bitmap, H_BITMAP) BT(pen, H_PEN) BT(brush, H_BRUSH) BT(region, H_REGION) BT(palette, H_PALETTE)
BT(stock_pen, H_STOCKPEN) BT(junk, H_JUNK) BT(minus1, H_MINUS1) BT(hwnd, H_HWND) BT(font, H_FONT) BT(stock_font, H_STOCKFONT)
BT(hatch, H_HATCH) BT(stock_brush, H_STOCKBRUSH) BT(stock_palette, H_STOCKPAL) BT(deleted_dc, H_DEADDC)

XT(bad_handles_do_not_disturb_valid_dc) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetCharSet)); XH(x);
    XD(d, L"Arial", 177, 0);
    int before = U(GetTextCharset)(d.dc);
    for (int k = 0; k < H_COUNT; k++) { U(GetTextCharset)((HDC)x.h[k]); XF_NT(NtGdiGetCharSet)((HDC)x.h[k]); }
    CHECK(U(GetTextCharset)(d.dc) == before);
    CHECK(U(GetObjectType)(x.pn) == OBJ_PEN);
    CHECK(U(GetObjectType)(x.bm) == OBJ_BITMAP);
    xd_close(&d); xh_close(&x);
}
