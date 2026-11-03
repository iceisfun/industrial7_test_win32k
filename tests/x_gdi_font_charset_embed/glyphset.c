/* NtGdiEngComputeGlyphSet / gdi32!EngComputeGlyphSet: a driver-side helper that user mode cannot use. From a normal GUI
 * process the syscall returns NULL for every (code page, first char, count) triple, including nonsense ones, without
 * raising and without disturbing the calling thread's GDI state. */
#include "xf.h"

static const int cps[] = { 0, 437, 850, 874, 932, 936, 949, 950, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258, 42, 65001, 12345, -1, 0x7fffffff };

XT(glyphset_nt_returns_null_for_code_pages) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEngComputeGlyphSet));
    for (unsigned i = 0; i < sizeof cps / sizeof *cps; i++) CHECKF(XF_NT(NtGdiEngComputeGlyphSet)(cps[i], 32, 224) == NULL, "code page %d", cps[i]);
}
XT(glyphset_gdi32_returns_null_for_code_pages) {
    XF_GUI(); XF_NEED(UN(EngComputeGlyphSet));
    for (unsigned i = 0; i < sizeof cps / sizeof *cps; i++) CHECKF(UN(EngComputeGlyphSet)(cps[i], 32, 224) == NULL, "code page %d", cps[i]);
}
XT(glyphset_nt_returns_null_for_ranges) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEngComputeGlyphSet));
    static const int r[][2] = { { 0, 0 }, { 0, 1 }, { 0, 256 }, { 32, 224 }, { 0, 0x10000 }, { 0xffff, 1 }, { 0xffff, 0x10000 }, { -1, 1 }, { 1, -1 }, { -1, -1 }, { 0x7fffffff, 0x7fffffff } };
    for (unsigned i = 0; i < sizeof r / sizeof *r; i++) CHECKF(XF_NT(NtGdiEngComputeGlyphSet)(1252, r[i][0], r[i][1]) == NULL, "range %u", i);
}
XT(glyphset_gdi32_returns_null_for_ranges) {
    XF_GUI(); XF_NEED(UN(EngComputeGlyphSet));
    static const int r[][2] = { { 0, 0 }, { 0, 256 }, { 32, 224 }, { 0, 0x10000 }, { -1, -1 } };
    for (unsigned i = 0; i < sizeof r / sizeof *r; i++) CHECKF(UN(EngComputeGlyphSet)(932, r[i][0], r[i][1]) == NULL, "range %u", i);
}
XT(glyphset_ignores_selected_font) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEngComputeGlyphSet));
    XD(d, L"Arial", 161, 0);
    CHECK(XF_NT(NtGdiEngComputeGlyphSet)(1253, 0, 256) == NULL);
    CHECK(U(GetTextCharset)(d.dc) == d.tm.tmCharSet);
    xd_close(&d);
}
XT(glyphset_does_not_change_gdi_object_count) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEngComputeGlyphSet)); XF_NEED(U(GetGuiResources));
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; i++) XF_NT(NtGdiEngComputeGlyphSet)(1252, 0, 256);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a);
}
