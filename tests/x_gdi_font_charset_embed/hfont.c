/* NtGdiHfontCreate called directly: any LFTYPE value and flag value yields a font object, the LOGFONT (charset
 * included) comes back through GetObjectW unchanged, the object is a non-stock OBJ_FONT with a fresh handle each time,
 * and a font made this way selects and reports charsets the same as one made by CreateFontIndirectW. Bad sizes and a NULL
 * pointer fail. Pointers the kernel cannot read are not probed (they raise in the caller). */
#include "xf.h"

struct elf { LOGFONTW lf; WCHAR full[64], style[32], script[32]; DWORD res, axes; LONG dv[16]; };
static void elf_fill(struct elf *e, const WCHAR *face, int cs) {
    memset(e, 0, sizeof *e);
    e->lf.lfHeight = -12; e->lf.lfWeight = 400; e->lf.lfCharSet = (BYTE)cs;
    for (int i = 0; face[i] && i < 31; i++) e->lf.lfFaceName[i] = face[i];
}

static void hf_cs(int cs) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"Arial", cs);
    HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    CHECK(f != NULL); if (!f) return;
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    CHECK(U(GetObjectW)(f, sizeof lf, &lf) == (int)sizeof lf);
    CHECK(lf.lfCharSet == (BYTE)cs);
    CHECK(lf.lfHeight == -12);
    CHECK(xf_wcseq(lf.lfFaceName, L"Arial"));
    HFONT g = U(CreateFontW)(-12, 0, 0, 0, 400, 0, 0, 0, cs, 0, 0, 0, 0, L"Arial");
    HDC dc = U(CreateCompatibleDC)(NULL);
    HGDIOBJ o = U(SelectObject)(dc, f);
    int c1 = U(GetTextCharset)(dc); FONTSIGNATURE s1; memset(&s1, 0, sizeof s1); U(GetTextCharsetInfo)(dc, &s1, 0);
    TEXTMETRICW t1; U(GetTextMetricsW)(dc, &t1);
    U(SelectObject)(dc, g);
    int c2 = U(GetTextCharset)(dc); FONTSIGNATURE s2; memset(&s2, 0, sizeof s2); U(GetTextCharsetInfo)(dc, &s2, 0);
    TEXTMETRICW t2; U(GetTextMetricsW)(dc, &t2);
    CHECK(c1 == c2);
    CHECK(c1 == t1.tmCharSet);
    CHECK(!memcmp(&s1, &s2, sizeof s1));
    CHECK(t1.tmHeight == t2.tmHeight && t1.tmAveCharWidth == t2.tmAveCharWidth);
    U(SelectObject)(dc, o); U(DeleteDC)(dc); U(DeleteObject)(f); U(DeleteObject)(g);
}
#define X(nm, cs) XT(hfont_charset_##nm) { hf_cs(cs); }
XF_CHARSETS(X)
#undef X

XT(hfont_every_type_value_creates_a_font) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    static const ULONG t[] = { 0, 1, 2, 3, 4, 5, 6, 0x80, 0xffff, 0xffffffffu };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        struct elf e; elf_fill(&e, L"Arial", 0);
        HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, t[i], 0, NULL);
        CHECKF(f != NULL, "type 0x%lx", (unsigned long)t[i]);
        if (f) { CHECKF(U(GetObjectType)(f) == OBJ_FONT, "type 0x%lx", (unsigned long)t[i]); CHECKF(U(DeleteObject)(f), "type 0x%lx", (unsigned long)t[i]); }
    }
}
XT(hfont_every_flag_value_creates_a_font) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    static const ULONG fl[] = { 0, 1, 2, 0x10, 0x100, 0xffffffffu };
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        struct elf e; elf_fill(&e, L"Arial", 0);
        HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, fl[i], NULL);
        CHECKF(f != NULL, "flags 0x%lx", (unsigned long)fl[i]);
        if (f) U(DeleteObject)(f);
    }
}
XT(hfont_handles_are_distinct_and_plain) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"Arial", 0);
    HFONT a = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL), b = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    CHECK(a && b && a != b);
    CHECK(((((ULONG_PTR)a) >> 16) & 0x7f) == 0x0a);        /* font object type in the handle */
    CHECK((((ULONG_PTR)a) & 0x800000) == 0);                /* not a stock object */
    CHECK(U(DeleteObject)(a)); CHECK(U(DeleteObject)(b));
    CHECK(!U(DeleteObject)(a));                              /* deleted handle is gone */
}
XT(hfont_deleted_font_is_not_retrievable) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"Arial", 0);
    HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    CHECK(f != NULL);
    CHECK(U(DeleteObject)(f));
    LOGFONTW lf; CHECK(U(GetObjectW)(f, sizeof lf, &lf) == 0);
    CHECK(!U(DeleteObject)(f));
}
XT(hfont_delete_while_selected_is_deferred) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"Arial", 161);
    HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    HDC dc = U(CreateCompatibleDC)(NULL); HGDIOBJ o = U(SelectObject)(dc, f);
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
    CHECK(U(DeleteObject)(f));                               /* accepted, the DC keeps the realized font alive */
    CHECK(U(GetTextCharset)(dc) == tm.tmCharSet);
    U(SelectObject)(dc, o);
    LOGFONTW lf; CHECK(U(GetObjectW)(f, sizeof lf, &lf) == 0);   /* gone once nothing selects it */
    U(DeleteDC)(dc);
}
XT(hfont_size_boundaries) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    static const ULONG ok[] = { 1, 4, 0x5c, 0x100, 0x164, 0x1a4 }, bad[] = { 0, 0x1a5, 0x1000, 0xffffffffu };
    for (unsigned i = 0; i < 6; i++) { struct elf e; elf_fill(&e, L"Arial", 0); HFONT f = XF_NT(NtGdiHfontCreate)(&e, ok[i], 0, 0, NULL); CHECKF(f != NULL, "cb 0x%lx", (unsigned long)ok[i]); if (f) U(DeleteObject)(f); }
    for (unsigned i = 0; i < 4; i++) { struct elf e; elf_fill(&e, L"Arial", 0); CHECKF(XF_NT(NtGdiHfontCreate)(&e, bad[i], 0, 0, NULL) == NULL, "cb 0x%lx", (unsigned long)bad[i]); }
}
XT(hfont_null_pointer_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    CHECK(XF_NT(NtGdiHfontCreate)(NULL, 0x164, 0, 0, NULL) == NULL);
    CHECK(XF_NT(NtGdiHfontCreate)(NULL, 0x5c, 0, 0, NULL) == NULL);
}
XT(hfont_unknown_face_keeps_name_but_maps) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"NoSuchFaceXyzzy", 0);
    HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    CHECK(f != NULL); if (!f) return;
    LOGFONTW lf; U(GetObjectW)(f, sizeof lf, &lf);
    CHECK(xf_wcseq(lf.lfFaceName, L"NoSuchFaceXyzzy"));      /* the logical font keeps the requested name */
    HDC dc = U(CreateCompatibleDC)(NULL); HGDIOBJ o = U(SelectObject)(dc, f);
    WCHAR face[64]; face[0] = 0; U(GetTextFaceW)(dc, 64, face);
    CHECK(face[0] != 0); CHECK(!xf_wcseq(face, L"NoSuchFaceXyzzy"));    /* the realized face is a real one */
    int cs = U(GetTextCharset)(dc);
    CHECK(cs == U(GetTextCharsetInfo)(dc, NULL, 0));
    U(SelectObject)(dc, o); U(DeleteDC)(dc); U(DeleteObject)(f);
}
XT(hfont_logfont_fields_round_trip) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"Arial", 177);
    e.lf.lfHeight = -21; e.lf.lfWidth = 7; e.lf.lfEscapement = 300; e.lf.lfOrientation = 300; e.lf.lfWeight = 700; e.lf.lfItalic = 1; e.lf.lfUnderline = 1; e.lf.lfStrikeOut = 1;
    e.lf.lfOutPrecision = OUT_TT_PRECIS; e.lf.lfClipPrecision = CLIP_DEFAULT_PRECIS; e.lf.lfQuality = ANTIALIASED_QUALITY; e.lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    CHECK(f != NULL); if (!f) return;
    LOGFONTW lf; memset(&lf, 0, sizeof lf); CHECK(U(GetObjectW)(f, sizeof lf, &lf) == (int)sizeof lf);
    CHECK(!memcmp(&lf, &e.lf, offsetof(LOGFONTW, lfFaceName)));
    CHECK(xf_wcseq(lf.lfFaceName, L"Arial"));
    U(DeleteObject)(f);
}
XT(hfont_same_font_same_charset_info_in_two_dcs) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiHfontCreate));
    struct elf e; elf_fill(&e, L"Arial", 204);
    HFONT f = XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL);
    HDC a = U(CreateCompatibleDC)(NULL), b = U(GetDC)(NULL);
    HGDIOBJ oa = U(SelectObject)(a, f), ob = U(SelectObject)(b, f);
    CHECK(U(GetTextCharset)(a) == U(GetTextCharset)(b));
    CHECK(XF_NT(NtGdiGetCharSet)(a) == XF_NT(NtGdiGetCharSet)(b));
    U(SelectObject)(a, oa); U(SelectObject)(b, ob); U(DeleteDC)(a); U(ReleaseDC)(NULL, b); U(DeleteObject)(f);
}
