/* NtGdiGetGammaRampCapability(hdc). Returns 1 only for a direct display DC on a gamma-capable adapter. Everything else
 * (NULL, junk and deleted handles, objects that are not DCs, memory, information and metafile DCs) returns 0 without
 * setting a last error. A real display DC answers 1 on hardware that supports gamma ramps and 0 on one that does not
 * (a virtual adapter), so only the relation {0, 1} is asserted for it. */
#include "xo.h"

#define NEEDS() NEED(NtGdiGetGammaRampCapability); if (!U(GetDC) || !U(CreateCompatibleDC)) { t_skip("gdi32/user32 unavailable"); return; }
static int cap(HDC dc) { SetLastError(0x77); int r = UN(NtGdiGetGammaRampCapability)(dc); CHECKF(GetLastError() == 0x77, "dc %p: last error changed to %lu", dc, GetLastError()); return r; }

XT(gamma_cap_null_dc)        { NEEDS(); CHECK(cap(NULL) == 0); }
XT(gamma_cap_junk_handles)   { NEEDS(); static const ULONG_PTR v[] = { 1, 0x1234, 0xDEADBEEF, 0x100000000ULL, (ULONG_PTR)-1, 0x7FFFFFFFFFFFFFFFULL }; for (int i = 0; i < 6; i++) CHECKF(cap((HDC)v[i]) == 0, "handle %p", (void *)v[i]); }
XT(gamma_cap_stock_objects)  { NEEDS(); CHECK(cap((HDC)U(GetStockObject)(BLACK_PEN)) == 0); CHECK(cap((HDC)U(GetStockObject)(WHITE_BRUSH)) == 0); CHECK(cap((HDC)U(GetStockObject)(SYSTEM_FONT)) == 0); CHECK(cap((HDC)U(GetStockObject)(DEFAULT_PALETTE)) == 0); }
XT(gamma_cap_window_handle)  { NEEDS(); CHECK(cap((HDC)U(GetDesktopWindow)()) == 0); }
XT(gamma_cap_memory_dc)      { NEEDS(); HDC m = U(CreateCompatibleDC)(NULL); CHECK(m != NULL); CHECK(cap(m) == 0); U(DeleteDC)(m); }
XT(gamma_cap_memory_dc_with_bitmap) {
    NEEDS(); HDC sc = U(GetDC)(NULL), m = U(CreateCompatibleDC)(sc); HBITMAP b = U(CreateCompatibleBitmap)(sc, 8, 8); HGDIOBJ old = U(SelectObject)(m, b);
    CHECK(cap(m) == 0); U(SelectObject)(m, old); U(DeleteObject)(b); U(DeleteDC)(m); U(ReleaseDC)(NULL, sc);
}
XT(gamma_cap_information_dc) { NEEDS(); HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL); if (!ic) { t_skip("no information DC"); return; } CHECK(cap(ic) == 0); U(DeleteDC)(ic); }
XT(gamma_cap_deleted_dc)     { NEEDS(); HDC m = U(CreateCompatibleDC)(NULL); U(DeleteDC)(m); CHECK(cap(m) == 0); }
XT(gamma_cap_metafile_dc) {
    NEEDS(); HDC e = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); if (!e) { t_skip("no metafile DC"); return; }
    CHECK(cap(e) == 0); HENHMETAFILE h = U(CloseEnhMetaFile)(e); if (h) U(DeleteEnhMetaFile)(h);
}
XT(gamma_cap_screen_dc_is_boolean) {
    NEEDS(); HDC sc = U(GetDC)(NULL); int r = cap(sc);
    CHECKF(r == 0 || r == 1, "screen DC: %d", r); U(ReleaseDC)(NULL, sc);
}
XT(gamma_cap_window_dc_is_boolean) { NEEDS(); HDC w = U(GetWindowDC)(NULL); int r = cap(w); CHECKF(r == 0 || r == 1, "window DC: %d", r); U(ReleaseDC)(NULL, w); }
XT(gamma_cap_created_display_dc_is_boolean) {
    NEEDS(); HDC d = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL); if (!d) { t_skip("no display DC"); return; }
    int r = cap(d); CHECKF(r == 0 || r == 1, "display DC: %d", r); U(DeleteDC)(d);
}
XT(gamma_cap_answer_is_stable) {
    NEEDS(); HDC sc = U(GetDC)(NULL); int a = cap(sc), b = cap(sc), c = cap(sc);
    CHECK(a == b && b == c); U(ReleaseDC)(NULL, sc);
}
XT(gamma_cap_screen_dc_and_created_display_dc_agree) {
    NEEDS(); HDC sc = U(GetDC)(NULL), d = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL); if (!d) { t_skip("no display DC"); U(ReleaseDC)(NULL, sc); return; }
    CHECK(cap(sc) == cap(d)); U(DeleteDC)(d); U(ReleaseDC)(NULL, sc);
}
XT(gamma_cap_does_not_change_dc_state) {
    NEEDS(); HDC sc = U(GetDC)(NULL); int w0 = U(GetDeviceCaps)(sc, HORZRES), h0 = U(GetDeviceCaps)(sc, VERTRES), b0 = U(GetDeviceCaps)(sc, BITSPIXEL);
    cap(sc); cap(NULL);
    CHECK(U(GetDeviceCaps)(sc, HORZRES) == w0 && U(GetDeviceCaps)(sc, VERTRES) == h0 && U(GetDeviceCaps)(sc, BITSPIXEL) == b0); U(ReleaseDC)(NULL, sc);
}
