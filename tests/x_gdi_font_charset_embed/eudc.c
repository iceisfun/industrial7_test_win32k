/* EUDC and font linking switches: NtGdiEnableEudc / gdi32!EnableEUDC accept any argument and report TRUE, the setting is
 * restored to enabled by every test; NtGdiAnyLinkedFonts mirrors gdi32!AnyLinkedFonts, returns a BOOL and is not
 * changed by toggling EUDC; NtGdiFontIsLinked answers 0 for anything that is not a DC. Whether linked fonts exist at all
 * depends on the machine's font link configuration, so dependent checks are conditional. */
#include "xf.h"

XT(eudc_enable_returns_true_for_any_argument) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEnableEudc));
    static const BOOL v[] = { 0, 1, 2, 5, 0x100, (BOOL)0x80000000, -1 };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) CHECKF(XF_NT(NtGdiEnableEudc)(v[i]) == TRUE, "arg %d", v[i]);
    XF_NT(NtGdiEnableEudc)(TRUE);
}
XT(eudc_enable_gdi32_route_returns_true) {
    XF_GUI(); XF_NEED(UN(EnableEUDC));
    CHECK(UN(EnableEUDC)(FALSE) == TRUE);
    CHECK(UN(EnableEUDC)(TRUE) == TRUE);
    CHECK(UN(EnableEUDC)(TRUE) == TRUE);
}
XT(eudc_toggle_sequence_is_idempotent) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEnableEudc));
    for (int i = 0; i < 5; i++) { CHECK(XF_NT(NtGdiEnableEudc)(FALSE) == TRUE); CHECK(XF_NT(NtGdiEnableEudc)(TRUE) == TRUE); }
    CHECK(XF_NT(NtGdiEnableEudc)(TRUE) == TRUE);
}
XT(eudc_toggle_does_not_change_charset) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEnableEudc));
    XD(d, L"Arial", 161, 0);
    int c = U(GetTextCharset)(d.dc); ULONG v = XF_NT(NtGdiGetCharSet)(d.dc);
    XF_NT(NtGdiEnableEudc)(FALSE);
    CHECK(U(GetTextCharset)(d.dc) == c);
    CHECK(XF_NT(NtGdiGetCharSet)(d.dc) == v);
    XF_NT(NtGdiEnableEudc)(TRUE);
    CHECK(U(GetTextCharset)(d.dc) == c);
    CHECK(XF_NT(NtGdiGetCharSet)(d.dc) == v);
    xd_close(&d);
}
XT(eudc_toggle_does_not_change_text_metrics) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiEnableEudc));
    XD(d, L"Arial", 0, 0);
    SIZE a, b; WCHAR s[] = L"Hello EUDC";
    U(GetTextExtentPoint32W)(d.dc, s, 10, &a);
    XF_NT(NtGdiEnableEudc)(FALSE);
    U(GetTextExtentPoint32W)(d.dc, s, 10, &b);
    XF_NT(NtGdiEnableEudc)(TRUE);
    CHECK(a.cx == b.cx && a.cy == b.cy);
    xd_close(&d);
}
XT(linked_any_matches_gdi32) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAnyLinkedFonts)); XF_NEED(UN(AnyLinkedFonts));
    BOOL a = XF_NT(NtGdiAnyLinkedFonts)(), b = UN(AnyLinkedFonts)();
    CHECK(a == b);
    CHECK(a == 0 || a == 1);
}
XT(linked_any_is_stable) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAnyLinkedFonts));
    BOOL a = XF_NT(NtGdiAnyLinkedFonts)();
    for (int i = 0; i < 4; i++) CHECK(XF_NT(NtGdiAnyLinkedFonts)() == a);
}
XT(linked_any_unchanged_by_eudc_toggle) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAnyLinkedFonts)); XF_NEED(XF_NT(NtGdiEnableEudc));
    BOOL a = XF_NT(NtGdiAnyLinkedFonts)();
    XF_NT(NtGdiEnableEudc)(FALSE);
    CHECK(XF_NT(NtGdiAnyLinkedFonts)() == a);
    XF_NT(NtGdiEnableEudc)(TRUE);
    CHECK(XF_NT(NtGdiAnyLinkedFonts)() == a);
}
XT(linked_font_is_linked_bad_handles) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiFontIsLinked)); XH(x);
    for (int k = 0; k < H_COUNT; k++) CHECKF(XF_NT(NtGdiFontIsLinked)((HDC)x.h[k]) == 0, "%s", xh_name[k]);
    xh_close(&x);
}
XT(linked_font_is_linked_is_boolean_for_real_fonts) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiFontIsLinked));
    static const WCHAR *const f[] = { L"Arial", L"Segoe UI", L"Courier New" };
    for (unsigned i = 0; i < 3; i++) {
        XD(d, f[i], 0, 0);
        ULONG r = XF_NT(NtGdiFontIsLinked)(d.dc);
        CHECKF(r == 0 || r == 1, "face %u: %lu", i, (unsigned long)r);
        CHECKF(XF_NT(NtGdiFontIsLinked)(d.dc) == r, "face %u stable", i);
        xd_close(&d);
    }
}
XT(linked_font_is_linked_independent_of_eudc_switch) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiFontIsLinked)); XF_NEED(XF_NT(NtGdiEnableEudc));
    XD(d, L"Arial", 0, 0);
    ULONG a = XF_NT(NtGdiFontIsLinked)(d.dc);
    XF_NT(NtGdiEnableEudc)(FALSE);
    CHECK(XF_NT(NtGdiFontIsLinked)(d.dc) == a);
    XF_NT(NtGdiEnableEudc)(TRUE);
    CHECK(XF_NT(NtGdiFontIsLinked)(d.dc) == a);
    xd_close(&d);
}
