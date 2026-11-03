/* Charset per font: for a font selected into a DC, every way of asking for its charset has to agree.
 * Measures GetTextCharset, GetTextCharsetInfo (with and without a FONTSIGNATURE), the NtGdiGetTextCharsetInfo and
 * NtGdiGetCharSet syscalls, the TEXTMETRIC charset and the LOGFONT charset, for fonts made by CreateFontW and by
 * NtGdiHfontCreate. The charset the mapper resolves a request to is machine dependent and is never compared with
 * a literal; only the relations between the answers are asserted. Determinism: no value of a signature is printed. */
#include "xf.h"

static int csi_of(int cs, CHARSETINFO *ci) {
    DWORD v = (DWORD)cs;
    if (!U(TranslateCharsetInfo)) return 0;
    return U(TranslateCharsetInfo)((DWORD *)(ULONG_PTR)v, ci, TCI_SRCCHARSET) != 0;
}

static void cs_agree(int cs) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo)); XF_NEED(U(GetTextCharsetInfo)); XF_NEED(U(GetTextCharset));
    for (int route = 0; route < 2; route++) {
        XD(d, L"Arial", cs, route);
        FONTSIGNATURE a, b, c; memset(&a, 0xAA, sizeof a); memset(&b, 0xBB, sizeof b); memset(&c, 0xCC, sizeof c);
        int tmcs = d.tm.tmCharSet;
        CHECKF(U(GetTextCharset)(d.dc) == tmcs, "route %d", route);
        CHECKF(U(GetTextCharsetInfo)(d.dc, NULL, 0) == tmcs, "route %d", route);
        CHECKF(U(GetTextCharsetInfo)(d.dc, &a, 0) == tmcs, "route %d", route);
        CHECKF(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, &b, 0) == tmcs, "route %d", route);
        CHECKF(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, NULL, 0) == tmcs, "route %d", route);
        CHECKF(!memcmp(&a, &b, sizeof a), "route %d: both routes fill the same signature", route);
        CHECKF(U(GetTextCharset)(d.dc) == U(GetTextCharset)(d.dc), "route %d: stable", route);
        (void)c;
        xd_close(&d);
    }
}
static void cs_getcharset(int cs) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetCharSet));
    XD(d, L"Arial", cs, 0);
    ULONG v = XF_NT(NtGdiGetCharSet)(d.dc);
    CHECK((v >> 16) == (ULONG)d.tm.tmCharSet);              /* charset in bits 16..23, nothing above */
    CHARSETINFO ci;
    if (csi_of(d.tm.tmCharSet, &ci)) CHECK((v & 0xffff) == ci.ciACP);   /* low word is the code page of that charset */
    else if (d.tm.tmCharSet == OEM_CHARSET) CHECK((v & 0xffff) == 1);   /* OEM charset carries the pseudo code page 1 */
    else t_skip("TranslateCharsetInfo does not know this charset");
    CHECK(v == XF_NT(NtGdiGetCharSet)(d.dc));
    xd_close(&d);
}
static void cs_logfont(int cs) {
    XF_GUI();
    for (int route = 0; route < 2; route++) {
        XD(d, L"Arial", cs, route);
        LOGFONTW lf; memset(&lf, 0, sizeof lf);
        CHECKF(U(GetObjectW)(d.f, sizeof lf, &lf) == (int)sizeof lf, "route %d", route);
        CHECKF(lf.lfCharSet == (BYTE)cs, "route %d: LOGFONT keeps the requested charset", route);
        CHECKF(U(GetObjectType)(d.f) == OBJ_FONT, "route %d", route);
        if (xf_wcseq(d.face, L"Arial") && cs != DEFAULT_CHARSET && d.tm.tmCharSet != 0 && cs != 0) CHECKF(d.tm.tmCharSet == cs, "route %d: Arial supplies the requested charset", route);
        xd_close(&d);
    }
}
static void cs_fontsig(int cs) {
    XF_GUI();
    XD(d, L"Arial", cs, 0);
    if (!xf_wcseq(d.face, L"Arial")) { xd_close(&d); t_skip("the mapper picked another face for this charset"); return; }
    XD(r, L"Arial", ANSI_CHARSET, 0);
    if (!xf_wcseq(r.face, L"Arial")) { xd_close(&r); xd_close(&d); t_skip("no Arial"); return; }
    FONTSIGNATURE a, b; memset(&a, 0xAA, sizeof a); memset(&b, 0xBB, sizeof b);
    U(GetTextCharsetInfo)(d.dc, &a, 0); U(GetTextCharsetInfo)(r.dc, &b, 0);
    CHECK(!memcmp(&a, &b, sizeof a));      /* the signature belongs to the face, not to the requested charset */
    CHECK((a.fsCsb[0] | a.fsCsb[1]) != 0);
    CHARSETINFO ci;
    if (csi_of(d.tm.tmCharSet, &ci) && ci.fs.fsCsb[0] && d.tm.tmCharSet != SYMBOL_CHARSET && (ci.fs.fsCsb[0] & 0x1ff) == ci.fs.fsCsb[0] && cs != 0 && cs != DEFAULT_CHARSET)
        CHECK((a.fsCsb[0] & ci.fs.fsCsb[0]) != 0);   /* a single-byte code page Arial covers sets its bit */
    xd_close(&r); xd_close(&d);
}
#define X(nm, cs) XT(cs_agree_##nm) { cs_agree(cs); } XT(cs_getcharset_##nm) { cs_getcharset(cs); } XT(cs_logfont_##nm) { cs_logfont(cs); }
XF_CHARSETS(X)
#undef X
/* The signature belongs to the face: only charsets Arial itself covers are compared. */
#define SIGT(nm, cs) XT(cs_fontsig_##nm) { cs_fontsig(cs); }
SIGT(ansi, 0) SIGT(greek, 161) SIGT(turkish, 162) SIGT(vietnamese, 163) SIGT(hebrew, 177) SIGT(arabic, 178) SIGT(baltic, 186) SIGT(russian, 204) SIGT(easteurope, 238)

XT(cs_default_charset_resolves) {
    XF_GUI();
    XD(d, L"Arial", DEFAULT_CHARSET, 0);
    CHECK(d.tm.tmCharSet != DEFAULT_CHARSET);              /* DEFAULT_CHARSET is resolved to a concrete charset */
    CHECK(U(GetTextCharset)(d.dc) != DEFAULT_CHARSET);
    xd_close(&d);
}
XT(cs_unknown_charset_falls_back) {
    XF_GUI();
    static const int bad[] = { 3, 77, 99, 200, 254 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        XD(d, L"Arial", bad[i], 0);
        LOGFONTW lf; U(GetObjectW)(d.f, sizeof lf, &lf);
        CHECKF(lf.lfCharSet == bad[i], "cs %d kept in the LOGFONT", bad[i]);
        CHECKF(U(GetTextCharset)(d.dc) == d.tm.tmCharSet, "cs %d", bad[i]);
        CHECKF(d.tm.tmCharSet != DEFAULT_CHARSET, "cs %d", bad[i]);
        xd_close(&d);
    }
}
XT(cs_deselect_restores_default) {
    XF_GUI();
    HDC dc = U(CreateCompatibleDC)(NULL);
    int def = U(GetTextCharset)(dc);
    FONTSIGNATURE fs0; memset(&fs0, 0, sizeof fs0); U(GetTextCharsetInfo)(dc, &fs0, 0);
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 161, 0, 0, 0, 0, L"Arial");
    HGDIOBJ o = U(SelectObject)(dc, f);
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
    CHECK(U(GetTextCharset)(dc) == tm.tmCharSet);
    U(SelectObject)(dc, o);
    CHECK(U(GetTextCharset)(dc) == def);
    FONTSIGNATURE fs1; memset(&fs1, 0xCC, sizeof fs1); U(GetTextCharsetInfo)(dc, &fs1, 0);
    CHECK(!memcmp(&fs0, &fs1, sizeof fs0));
    U(DeleteObject)(f); U(DeleteDC)(dc);
}
XT(cs_savedc_restoredc_charset) {
    XF_GUI();
    XD(d, L"Arial", 161, 0);
    int greek = U(GetTextCharset)(d.dc);
    int id = U(SaveDC)(d.dc);
    CHECK(id > 0);
    HFONT f2 = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 204, 0, 0, 0, 0, L"Arial");
    U(SelectObject)(d.dc, f2);
    TEXTMETRICW tm; U(GetTextMetricsW)(d.dc, &tm);
    CHECK(U(GetTextCharset)(d.dc) == tm.tmCharSet);
    CHECK(U(RestoreDC)(d.dc, id));
    CHECK(U(GetTextCharset)(d.dc) == greek);
    CHECK(XF_NT(NtGdiGetCharSet)(d.dc) >> 16 == (ULONG)greek);
    U(DeleteObject)(f2);
    xd_close(&d);
}
XT(cs_each_dc_has_its_own_font) {
    XF_GUI();
    XD(a, L"Arial", 161, 0); XD(b, L"Arial", 204, 0);
    CHECK(U(GetTextCharset)(a.dc) == a.tm.tmCharSet);
    CHECK(U(GetTextCharset)(b.dc) == b.tm.tmCharSet);
    if (xf_wcseq(a.face, L"Arial") && xf_wcseq(b.face, L"Arial")) CHECK(U(GetTextCharset)(a.dc) != U(GetTextCharset)(b.dc));
    xd_close(&a); xd_close(&b);
}
XT(cs_same_font_two_dcs) {
    XF_GUI();
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 177, 0, 0, 0, 0, L"Arial");
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(NULL);
    HGDIOBJ oa = U(SelectObject)(a, f), ob = U(SelectObject)(b, f);
    CHECK(U(GetTextCharset)(a) == U(GetTextCharset)(b));
    CHECK(XF_NT(NtGdiGetCharSet)(a) == XF_NT(NtGdiGetCharSet)(b));
    U(SelectObject)(a, oa); U(SelectObject)(b, ob);
    U(DeleteObject)(f); U(DeleteDC)(a); U(DeleteDC)(b);
}
XT(cs_font_size_does_not_change_charset) {
    XF_GUI();
    static const int h[] = { -8, -16, -32, 24, 0 };
    int base = -1;
    for (unsigned i = 0; i < sizeof h / sizeof *h; i++) {
        HDC dc = U(CreateCompatibleDC)(NULL);
        HFONT f = U(CreateFontW)(h[i], 0, 0, 0, 400, 0, 0, 0, 162, 0, 0, 0, 0, L"Arial");
        HGDIOBJ o = U(SelectObject)(dc, f);
        int c = U(GetTextCharset)(dc);
        if (base < 0) base = c;
        CHECKF(c == base, "height %d", h[i]);
        U(SelectObject)(dc, o); U(DeleteObject)(f); U(DeleteDC)(dc);
    }
}
XT(cs_text_state_does_not_change_charset) {
    XF_GUI();
    XD(d, L"Arial", 238, 0);
    int c = U(GetTextCharset)(d.dc);
    U(SetMapMode)(d.dc, MM_ANISOTROPIC); U(SetTextCharacterExtra)(d.dc, 3); U(SetTextAlign)(d.dc, TA_RIGHT | TA_BASELINE); U(SetBkMode)(d.dc, TRANSPARENT);
    U(SetTextJustification)(d.dc, 10, 2);
    CHECK(U(GetTextCharset)(d.dc) == c);
    CHECK(U(GetTextCharsetInfo)(d.dc, NULL, 0) == c);
    U(SetTextCharacterExtra)(d.dc, 0); U(SetTextJustification)(d.dc, 0, 0); U(SetMapMode)(d.dc, MM_TEXT);
    xd_close(&d);
}
