/* Charset queries across DC kinds and across the optional arguments: memory DC, screen DC, information DC, enhanced
 * metafile DC and a fresh process default; the flags argument of the syscall is ignored; the signature is exactly 24
 * bytes; a signature pointer the kernel cannot read gives DEFAULT_CHARSET and leaves the DC usable. */
#include "xf.h"

XT(kind_memdc_default_font) {
    XF_GUI();
    HDC dc = U(CreateCompatibleDC)(NULL);
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
    FONTSIGNATURE fs; memset(&fs, 0xCC, sizeof fs);
    CHECK(U(GetTextCharsetInfo)(dc, &fs, 0) == tm.tmCharSet);
    CHECK(U(GetTextCharset)(dc) == tm.tmCharSet);
    CHECK(XF_NT(NtGdiGetCharSet)(dc) >> 16 == (ULONG)tm.tmCharSet);
    U(DeleteDC)(dc);
}
XT(kind_screen_dc) {
    XF_GUI();
    HDC dc = U(GetDC)(NULL);
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 161, 0, 0, 0, 0, L"Arial");
    HGDIOBJ o = U(SelectObject)(dc, f);
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
    CHECK(U(GetTextCharset)(dc) == tm.tmCharSet);
    CHECK(XF_NT(NtGdiGetCharSet)(dc) >> 16 == (ULONG)tm.tmCharSet);
    U(SelectObject)(dc, o); U(DeleteObject)(f); U(ReleaseDC)(NULL, dc);
}
XT(kind_released_screen_dc_is_invalid) {
    XF_GUI();
    HDC dc = U(GetDC)(NULL);
    U(ReleaseDC)(NULL, dc);
    SetLastError(0);
    CHECK(U(GetTextCharset)(dc) == DEFAULT_CHARSET);
    EXPECT_ERR(6);
}
XT(kind_information_dc) {
    XF_GUI(); XF_NEED(U(CreateICW));
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
    if (!ic) { t_skip("no display information DC"); return; }
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 204, 0, 0, 0, 0, L"Arial");
    HGDIOBJ o = U(SelectObject)(ic, f);
    TEXTMETRICW tm; U(GetTextMetricsW)(ic, &tm);
    CHECK(U(GetTextCharset)(ic) == tm.tmCharSet);
    CHECK(XF_NT(NtGdiGetCharSet)(ic) >> 16 == (ULONG)tm.tmCharSet);
    U(SelectObject)(ic, o); U(DeleteObject)(f); U(DeleteDC)(ic);
}
XT(kind_enhanced_metafile_dc) {
    XF_GUI(); XF_NEED(U(CreateEnhMetaFileW));
    HDC em = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL);
    if (!em) { t_skip("cannot create a metafile DC"); return; }
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 161, 0, 0, 0, 0, L"Arial");
    HGDIOBJ o = U(SelectObject)(em, f);
    FONTSIGNATURE fs; memset(&fs, 0xCC, sizeof fs);
    CHECK(U(GetTextCharsetInfo)(em, &fs, 0) == 161);         /* answered from the LOGFONT selected into the metafile DC */
    CHECK(U(GetTextCharset)(em) == 161);
    CHECK(XF_NT(NtGdiGetCharSet)(em) >> 16 == 161u);
    U(SelectObject)(em, o);
    HENHMETAFILE hm = U(CloseEnhMetaFile)(em);
    if (hm) U(DeleteEnhMetaFile)(hm);
    U(DeleteObject)(f);
}
XT(kind_closed_metafile_dc_is_invalid) {
    XF_GUI(); XF_NEED(U(CreateEnhMetaFileW));
    HDC em = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL);
    if (!em) { t_skip("cannot create a metafile DC"); return; }
    HENHMETAFILE hm = U(CloseEnhMetaFile)(em);
    CHECK(U(GetTextCharset)(em) == DEFAULT_CHARSET);
    if (hm) U(DeleteEnhMetaFile)(hm);
}
XT(args_flags_are_ignored) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo));
    XD(d, L"Arial", 238, 0);
    FONTSIGNATURE ref; memset(&ref, 0xCC, sizeof ref);
    int r0 = XF_NT(NtGdiGetTextCharsetInfo)(d.dc, &ref, 0);
    static const DWORD fl[] = { 1, 2, 0x77, 0x100, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        FONTSIGNATURE fs; memset(&fs, 0xAA, sizeof fs);
        CHECKF(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, &fs, fl[i]) == r0, "flags 0x%lx", (unsigned long)fl[i]);
        CHECKF(!memcmp(&fs, &ref, sizeof fs), "flags 0x%lx", (unsigned long)fl[i]);
        CHECKF(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, NULL, fl[i]) == r0, "flags 0x%lx, no signature", (unsigned long)fl[i]);
    }
    xd_close(&d);
}
XT(args_gdi32_flags_ignored) {
    XF_GUI();
    XD(d, L"Arial", 238, 0);
    FONTSIGNATURE ref, fs; memset(&ref, 0, sizeof ref); memset(&fs, 0xAA, sizeof fs);
    int r0 = U(GetTextCharsetInfo)(d.dc, &ref, 0);
    CHECK(U(GetTextCharsetInfo)(d.dc, &fs, 0x1234) == r0);
    CHECK(!memcmp(&fs, &ref, sizeof fs));
    xd_close(&d);
}
XT(args_signature_is_24_bytes) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo));
    XD(d, L"Arial", 0, 0);
    unsigned char buf[64]; memset(buf, 0xCC, sizeof buf);
    XF_NT(NtGdiGetTextCharsetInfo)(d.dc, (FONTSIGNATURE *)(buf + 8), 0);
    for (int i = 0; i < 8; i++) CHECKF(buf[i] == 0xCC, "byte %d before", i);
    for (int i = 32; i < 64; i++) CHECKF(buf[i] == 0xCC, "byte %d after", i);
    xd_close(&d);
}
XT(args_unaligned_signature) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo));
    XD(d, L"Arial", 0, 0);
    FONTSIGNATURE ref; memset(&ref, 0, sizeof ref); U(GetTextCharsetInfo)(d.dc, &ref, 0);
    unsigned char buf[64]; memset(buf, 0, sizeof buf);
    for (int off = 1; off < 8; off++) {
        memset(buf, 0, sizeof buf);
        CHECKF(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, (FONTSIGNATURE *)(buf + off), 0) == d.tm.tmCharSet, "offset %d", off);
        CHECKF(!memcmp(buf + off, &ref, sizeof ref), "offset %d", off);
    }
    xd_close(&d);
}
XT(args_unreadable_signature_pointer) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetTextCharsetInfo));
    XD(d, L"Arial", 161, 0);
    CHECK(d.tm.tmCharSet != DEFAULT_CHARSET);
    CHECK(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, (FONTSIGNATURE *)(ULONG_PTR)0x10, 0) == DEFAULT_CHARSET);
    CHECK(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, (FONTSIGNATURE *)0xFFFF800000000000ull, 0) == DEFAULT_CHARSET);
    CHECK(XF_NT(NtGdiGetTextCharsetInfo)(d.dc, (FONTSIGNATURE *)0xFFFFFFFFFFFFFFF0ull, 0) == DEFAULT_CHARSET);
    CHECK(U(GetTextCharset)(d.dc) == d.tm.tmCharSet);         /* the DC is undisturbed */
    xd_close(&d);
}
XT(args_charset_unchanged_by_failed_query) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetCharSet));
    XD(d, L"Arial", 177, 0);
    ULONG a = XF_NT(NtGdiGetCharSet)(d.dc);
    XF_NT(NtGdiGetTextCharsetInfo)(d.dc, (FONTSIGNATURE *)(ULONG_PTR)0x10, 0);
    CHECK(XF_NT(NtGdiGetCharSet)(d.dc) == a);
    xd_close(&d);
}
XT(args_gdi32_batch_limit_does_not_matter) {
    XF_GUI(); XF_NEED(U(GdiSetBatchLimit)); XF_NEED(U(GdiFlush));
    XD(d, L"Arial", 204, 0);
    DWORD old = U(GdiSetBatchLimit)(1);
    int a = U(GetTextCharset)(d.dc);
    U(GdiFlush)();
    if (old) U(GdiSetBatchLimit)(old);
    CHECK(a == d.tm.tmCharSet);
    xd_close(&d);
}
