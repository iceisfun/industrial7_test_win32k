/* Charset and font signature per well-known face: a face present on the machine reports one charset through all
 * routes and a signature that is coherent with it (symbol faces: symbol bit and an empty Unicode range mask;
 * all others: at least one code page bit and one Unicode range bit). A missing face or one the mapper replaced is a
 * conditional SKIP. Determinism: no signature value is printed or compared with a literal. */
#include "xf.h"

static void face_case(const WCHAR *face, int cs) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo));
    XD(d, face, cs, 0);
    if (!xf_wcseq(d.face, face)) { xd_close(&d); t_skip("face not installed (mapper chose another)"); return; }
    FONTSIGNATURE fs, nt; memset(&fs, 0xCC, sizeof fs); memset(&nt, 0xDD, sizeof nt);
    int c1 = U(GetTextCharsetInfo)(d.dc, &fs, 0), c2 = XF_NT(NtGdiGetTextCharsetInfo)(d.dc, &nt, 0);
    CHECK(c1 == d.tm.tmCharSet); CHECK(c2 == d.tm.tmCharSet); CHECK(U(GetTextCharset)(d.dc) == d.tm.tmCharSet);
    CHECK(!memcmp(&fs, &nt, sizeof fs));
    ULONG v = XF_NT(NtGdiGetCharSet)(d.dc);
    CHECK((v >> 16) == (ULONG)d.tm.tmCharSet);
    if (d.tm.tmCharSet == SYMBOL_CHARSET) {
        CHECK(fs.fsCsb[0] & 0x80000000u);
        CHECK(!fs.fsUsb[0] && !fs.fsUsb[1] && !fs.fsUsb[2] && !fs.fsUsb[3]);
        CHECK((v & 0xffff) == 42);                          /* the symbol "code page" */
    } else {
        CHECK(fs.fsCsb[0] != 0);
        CHECK((fs.fsUsb[0] | fs.fsUsb[1] | fs.fsUsb[2] | fs.fsUsb[3]) != 0);
    }
    xd_close(&d);
}
#define FACE(nm, w) XT(face_##nm) { face_case(w, ANSI_CHARSET); }
#define SFACE(nm, w) XT(face_##nm) { face_case(w, SYMBOL_CHARSET); }
FACE(arial, L"Arial") FACE(courier_new, L"Courier New") FACE(times_new_roman, L"Times New Roman") FACE(segoe_ui, L"Segoe UI")
FACE(tahoma, L"Tahoma") FACE(verdana, L"Verdana") SFACE(symbol, L"Symbol") SFACE(wingdings, L"Wingdings") SFACE(webdings, L"Webdings")
SFACE(marlett, L"Marlett") FACE(ms_gothic, L"MS Gothic") FACE(simsun, L"SimSun") 

XT(face_symbol_fonts_force_symbol_charset_for_ansi_request) {
    XF_GUI();
    static const WCHAR *const f[] = { L"Symbol", L"Wingdings", L"Webdings", L"Marlett" };
    int seen = 0;
    for (unsigned i = 0; i < 4; i++) {
        XD(d, f[i], ANSI_CHARSET, 0);
        if (xf_wcseq(d.face, f[i])) { seen++; CHECKF(d.tm.tmCharSet == SYMBOL_CHARSET, "face %u", i); CHECKF(U(GetTextCharset)(d.dc) == SYMBOL_CHARSET, "face %u", i); }
        xd_close(&d);
    }
    if (!seen) t_skip("no symbol face installed");
}
XT(face_arial_symbol_request_maps_to_symbol_face) {
    XF_GUI();
    XD(d, L"Arial", SYMBOL_CHARSET, 0);
    CHECK(U(GetTextCharset)(d.dc) == d.tm.tmCharSet);
    if (d.tm.tmCharSet == SYMBOL_CHARSET) {
        FONTSIGNATURE fs; memset(&fs, 0xCC, sizeof fs); U(GetTextCharsetInfo)(d.dc, &fs, 0);
        CHECK(fs.fsCsb[0] & 0x80000000u);
    } else t_skip("the mapper did not return a symbol face");
    xd_close(&d);
}
