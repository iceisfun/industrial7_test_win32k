/* gap_05: font-related GDI syscalls - FontIsLinked, GetCharWidthInfo, GetCurrentDpiInfo, GetStringBitmapW,
 * GetFontFileInfo, GetFontFileData. */
#include "g5.h"

#define SENT 0xA5

/* ---------------------------------------------------------------- NtGdiFontIsLinked
 * A DC lock failure returns 0; otherwise a 0/1 answer about EUDC/font-association
 * linking for the DC's selected font. */
TEST(gap5_fontislinked_bad_dc_false) {
    G5_GUI(); G5_NT(NtGdiFontIsLinked); G5_NEED(NtGdiFontIsLinked_);
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) CHECKF(NtGdiFontIsLinked_((HDC)b.h[i]) == 0, "%s", b.what[i]);
    g5_bad_close(&b);
}
TEST(gap5_fontislinked_is_boolean_and_stable) {
    G5_GUI(); G5_NT(NtGdiFontIsLinked); G5_NEED(NtGdiFontIsLinked_);
    static const WCHAR *faces[] = { L"Arial", L"Segoe UI", L"MS Gothic", L"Courier New" };
    for (size_t i = 0; i < sizeof faces / sizeof faces[0]; i++) {
        g5_fdc d;
        if (!g5_fdc_open(&d, faces[i])) { g5_fdc_close(&d); continue; }
        ULONG a = NtGdiFontIsLinked_(d.dc), b = NtGdiFontIsLinked_(d.dc);
        CHECKF(a <= 1, "returned %lu", (unsigned long)a);
        CHECK(a == b);
        t_note("%ls: FontIsLinked=%lu", faces[i], (unsigned long)a);
        g5_fdc_close(&d);
    }
}
TEST(gap5_fontislinked_default_font_dc) {
    G5_GUI(); G5_NT(NtGdiFontIsLinked); G5_NEED(NtGdiFontIsLinked_);
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    ULONG a = NtGdiFontIsLinked_(dc);
    CHECK(a <= 1);
    CHECK(NtGdiFontIsLinked_(dc) == a);
    U(DeleteDC)(dc);
}
TEST(gap5_fontislinked_wrapper_is_a_tail_jump) {
    G5_GUI(); G5_NT(NtGdiFontIsLinked); G5_NEED(NtGdiFontIsLinked_);
    FontIsLinked_pfn w = (FontIsLinked_pfn)t_proc_in("gdi32.dll", "FontIsLinked");
    G5_NEED(w);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    CHECK(w(d.dc) == NtGdiFontIsLinked_(d.dc));
    CHECK(w(NULL) == 0);
    g5_fdc_close(&d);
}

/* ---------------------------------------------------------------- NtGdiGetCharWidthInfo
 * The call fetches the width info for the DC; on success the 12-byte CHWIDTHINFO is copied out
 * (8-byte store + 4-byte store); on failure nothing is written. */
TEST(gap5_charwidthinfo_bad_dc_false_and_untouched) {
    G5_GUI(); G5_NT(NtGdiGetCharWidthInfo); G5_NEED(NtGdiGetCharWidthInfo_);
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) {
        unsigned char out[16]; memset(out, SENT, sizeof out);
        unsigned char ref[16]; memcpy(ref, out, sizeof ref);
        CHECKF(NtGdiGetCharWidthInfo_((HDC)b.h[i], out) == 0, "%s", b.what[i]);
        CHECKF(!memcmp(out, ref, sizeof out), "%s: output written", b.what[i]);
    }
    g5_bad_close(&b);
}
TEST(gap5_charwidthinfo_truetype_writes_exactly_twelve_bytes) {
    G5_GUI(); G5_NT(NtGdiGetCharWidthInfo); G5_NEED(NtGdiGetCharWidthInfo_);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    unsigned char out[32]; memset(out, SENT, sizeof out);
    ULONG r = NtGdiGetCharWidthInfo_(d.dc, out);
    if (!r) { t_note("Arial: GetCharWidthInfo failed (no realization data)"); g5_fdc_close(&d); return; }
    CHECK(r == 1);
    for (int i = 12; i < 32; i++) CHECKF(out[i] == SENT, "byte %d past the structure was written", i);
    LONG *v = (LONG *)out;
    t_note("Arial CHWIDTHINFO: lMaxNegA=%ld lMaxNegC=%ld lMinWidthD=%ld", v[0], v[1], v[2]);
    unsigned char out2[16]; memset(out2, 0x11, sizeof out2);
    CHECK(NtGdiGetCharWidthInfo_(d.dc, out2) == 1);
    CHECK(!memcmp(out, out2, 12));   /* deterministic for an unchanged DC/font */
    g5_fdc_close(&d);
}
TEST(gap5_charwidthinfo_wrapper_matches_syscall) {
    G5_GUI(); G5_NT(NtGdiGetCharWidthInfo); G5_NEED(NtGdiGetCharWidthInfo_);
    GetCharWidthInfo_pfn w = (GetCharWidthInfo_pfn)t_proc_in("gdi32.dll", "GetCharWidthInfo");
    G5_NEED(w);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    unsigned char a[16] = {0}, c[16] = {0};
    ULONG r1 = NtGdiGetCharWidthInfo_(d.dc, a), r2 = w(d.dc, c);
    CHECK(r1 == r2);
    if (r1) CHECK(!memcmp(a, c, 12));
    g5_fdc_close(&d);
}
TEST(gap5_charwidthinfo_scales_with_font_size) {
    G5_GUI(); G5_NT(NtGdiGetCharWidthInfo); G5_NEED(NtGdiGetCharWidthInfo_);
    /* The three values are font-unit extents scaled by the realised size (bFToL of value*16*scale), so the
     * advance/minimum-width term must not shrink when the same face is realised at twice the height. */
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    LONG big[3] = {0}, small[3] = {0};
    int ok = 0;
    for (int pass = 0; pass < 2; pass++) {
        HFONT f = U(CreateFontW)(pass ? -48 : -24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                                 CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
        if (!f) break;
        HGDIOBJ old = U(SelectObject)(dc, f);
        LONG v[3] = {0};
        ULONG r = NtGdiGetCharWidthInfo_(dc, v);
        if (r) { memcpy(pass ? big : small, v, sizeof v); ok++; }
        U(SelectObject)(dc, old);
        U(DeleteObject)(f);
    }
    U(DeleteDC)(dc);
    if (ok < 2) { t_skip("no char width info for Arial"); return; }
    t_note("24px: %ld %ld %ld ; 48px: %ld %ld %ld", small[0], small[1], small[2], big[0], big[1], big[2]);
    CHECKF(labs(big[0]) >= labs(small[0]), "lMaxNegA shrank");
    CHECKF(labs(big[1]) >= labs(small[1]), "lMaxNegC shrank");
    CHECKF(labs(big[2]) >= labs(small[2]), "lMinWidthD shrank");
}

/* ---------------------------------------------------------------- NtGdiGetCurrentDpiInfo
 * win32kbase 0x31640 (HMONITOR, out[0x60]): UserGetHDevFromMonitor(hmon) == 0 -> STATUS_INVALID_PARAMETER
 * (0xC000000D) with nothing written; otherwise the 96-byte DPI_INFO block is copied out. The gdi32 wrapper maps
 * 0xC000000D to ERROR_INVALID_HANDLE (6) and 0xC0000001 to ERROR_INVALID_PARAMETER (0x57), returning 0. */
TEST(gap5_dpiinfo_invalid_monitor) {
    G5_GUI(); G5_NT(NtGdiGetCurrentDpiInfo); G5_NEED(NtGdiGetCurrentDpiInfo_);
    HMONITOR bad[] = { NULL, (HMONITOR)1, (HMONITOR)0xDEAD0, (HMONITOR)0x7FFFFFFF, (HMONITOR)0xFFFFFFFFull, (HMONITOR)(LONG_PTR)-1 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        unsigned char buf[0x100]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x100]; memcpy(ref, buf, sizeof ref);
        LONG st = NtGdiGetCurrentDpiInfo_(bad[i], buf);
        CHECKF((ULONG)st == 0xC000000Du, "%p: status 0x%lx", bad[i], (unsigned long)st);
        CHECKF(!memcmp(buf, ref, sizeof buf), "%p: buffer modified", bad[i]);
    }
}
TEST(gap5_dpiinfo_wrong_handle_types_rejected) {
    G5_GUI(); G5_NT(NtGdiGetCurrentDpiInfo); G5_NEED(NtGdiGetCurrentDpiInfo_);
    /* A window and a DC handle are valid handles but not monitors. */
    HWND w = k_make_window("gap5-dpi-wrongtype");
    HDC dc = U(CreateCompatibleDC)(NULL);
    unsigned char buf[0x100], ref[0x100];
    memset(buf, SENT, sizeof buf); memcpy(ref, buf, sizeof ref);
    if (w) { CHECK((ULONG)NtGdiGetCurrentDpiInfo_((HMONITOR)w, buf) == 0xC000000Du); U(DestroyWindow)(w); }
    if (dc) { CHECK((ULONG)NtGdiGetCurrentDpiInfo_((HMONITOR)dc, buf) == 0xC000000Du); U(DeleteDC)(dc); }
    CHECK(!memcmp(buf, ref, sizeof buf));
}
TEST(gap5_dpiinfo_primary_monitor) {
    G5_GUI(); G5_NT(NtGdiGetCurrentDpiInfo); G5_NEED(NtGdiGetCurrentDpiInfo_);
    G5_NEED(U(MonitorFromPoint));
    POINT o = { 0, 0 };
    HMONITOR m = U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY);
    if (!m) { t_skip("no primary monitor"); return; }
    unsigned char buf[0x100]; memset(buf, SENT, sizeof buf);
    LONG st = NtGdiGetCurrentDpiInfo_(m, buf);
    CHECKF((ULONG)st != 0xC000000Du, "valid monitor rejected: 0x%lx", (unsigned long)st);
    if (st >= 0) {
        for (int i = 0x60; i < 0x100; i++) CHECKF(buf[i] == SENT, "byte 0x%x past the 0x60-byte block was written", i);
        int touched = 0; for (int i = 0; i < 0x60; i++) touched += (buf[i] != SENT);
        t_note("DPI_INFO: %d of 96 bytes differ from the fill pattern; first dwords %08lx %08lx %08lx %08lx", touched,
               ((ULONG *)buf)[0], ((ULONG *)buf)[1], ((ULONG *)buf)[2], ((ULONG *)buf)[3]);
        unsigned char again[0x60]; memset(again, 0x33, sizeof again);
        CHECK(NtGdiGetCurrentDpiInfo_(m, again) >= 0);
        CHECK(!memcmp(again, buf, 0x60));    /* nothing in the block changes between two reads */
    } else t_note("primary monitor status 0x%lx", (unsigned long)st);
}
TEST(gap5_dpiinfo_user_mode_error_mapping) {
    G5_GUI(); G5_NT(NtGdiGetCurrentDpiInfo); G5_NEED(NtGdiGetCurrentDpiInfo_);
    GetCurrentDpiInfo_pfn w = (GetCurrentDpiInfo_pfn)t_proc_in("gdi32.dll", "GetCurrentDpiInfo");
    G5_NEED(w);
    unsigned char buf[0x60] = {0};
    SET_ERR(0);
    CHECK(w((HMONITOR)0xDEAD0, buf) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(w(NULL, buf) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

/* ---------------------------------------------------------------- NtGdiGetStringBitmapW
 * win32kfull 0x298570 (hdc, &wchar, cwc, cb, buf): cwc must be exactly 1, else 0; cb > 0x2710000 -> 0; the
 * character is read through the pointer; the call returns the bitmap size {8-byte header
 * (cx, cy) + ((cx+7)>>3)*cy} and only copies when cb >= size. A bad DC sets ERROR_INVALID_HANDLE. */
TEST(gap5_stringbitmap_cwc_must_be_one) {
    G5_GUI(); G5_NT(NtGdiGetStringBitmapW); G5_NEED(NtGdiGetStringBitmapW_);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    WCHAR c[4] = { L'A', L'B', 0, 0 };
    unsigned char buf[0x100]; memset(buf, SENT, sizeof buf);
    unsigned char ref[0x100]; memcpy(ref, buf, sizeof ref);
    ULONG cwcs[] = { 0, 2, 3, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof cwcs / sizeof cwcs[0]; i++) {
        CHECKF(NtGdiGetStringBitmapW_(d.dc, c, cwcs[i], sizeof buf, buf) == 0, "cwc %lu", (unsigned long)cwcs[i]);
        CHECKF(NtGdiGetStringBitmapW_(NULL, c, cwcs[i], 0, NULL) == 0, "NULL dc, cwc %lu", (unsigned long)cwcs[i]);
    }
    CHECK(!memcmp(buf, ref, sizeof buf));
    g5_fdc_close(&d);
}
TEST(gap5_stringbitmap_bad_dc_invalid_handle) {
    G5_GUI(); G5_NT(NtGdiGetStringBitmapW); G5_NEED(NtGdiGetStringBitmapW_);
    g5_bad b; g5_bad_open(&b);
    WCHAR c = L'A';
    for (int i = 0; i < b.n; i++) {
        SET_ERR(0);
        ULONG r = NtGdiGetStringBitmapW_((HDC)b.h[i], &c, 1, 0, NULL);
        CHECKF(r == 0, "%s: returned %lu", b.what[i], (unsigned long)r);
        CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "%s: last error %lu", b.what[i], GetLastError());
    }
    g5_bad_close(&b);
}
TEST(gap5_stringbitmap_oversized_request_fails) {
    G5_GUI(); G5_NT(NtGdiGetStringBitmapW); G5_NEED(NtGdiGetStringBitmapW_);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    WCHAR c = L'A';
    unsigned char buf[16]; memset(buf, SENT, sizeof buf);
    /* 0x2710000 is the largest accepted request; anything larger is refused before the DC is looked at */
    ULONG cbs[] = { 0x2710001, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        CHECKF(NtGdiGetStringBitmapW_(d.dc, &c, 1, cbs[i], buf) == 0, "cb 0x%lx", (unsigned long)cbs[i]);
        CHECKF(NtGdiGetStringBitmapW_(NULL, &c, 1, cbs[i], buf) == 0, "NULL dc, cb 0x%lx", (unsigned long)cbs[i]);
    }
    for (int i = 0; i < 16; i++) CHECK(buf[i] == SENT);
    g5_fdc_close(&d);
}
static void sb_layout(const WCHAR ch) {
    G5_NT(NtGdiGetStringBitmapW);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    ULONG need = NtGdiGetStringBitmapW_(d.dc, &ch, 1, 0, NULL);
    if (!need) { t_note("GetStringBitmapW(U+%04X) size query returned 0 (error %lu)", ch, GetLastError()); g5_fdc_close(&d); return; }
    CHECKF(need >= 8, "size %lu smaller than the header", (unsigned long)need);
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, need + 64);
    REQUIRE(buf);
    memset(buf, SENT, need + 64);
    /* too small by one byte: the required size comes back and nothing is copied */
    CHECK(NtGdiGetStringBitmapW_(d.dc, &ch, 1, need - 1, buf) == need);
    int clean = 1; for (ULONG i = 0; i < need + 64; i++) clean &= (buf[i] == SENT);
    CHECK(clean);
    /* exact fit */
    ULONG got = NtGdiGetStringBitmapW_(d.dc, &ch, 1, need, buf);
    CHECK(got == need);
    LONG cx = ((LONG *)buf)[0], cy = ((LONG *)buf)[1];
    CHECKF(cx > 0 && cy > 0, "cx=%ld cy=%ld", cx, cy);
    if (cx > 0 && cy > 0) {
        CHECKF(need == 8 + (ULONG)(((cx + 7) >> 3) * cy), "size %lu vs %ldx%ld", (unsigned long)need, cx, cy);
        int ink = 0; for (ULONG i = 8; i < need; i++) ink |= buf[i];
        t_note("U+%04X: %ldx%ld, %lu bytes, ink=%d", ch, cx, cy, (unsigned long)need, ink != 0);
    }
    for (ULONG i = need; i < need + 64; i++) CHECKF(buf[i] == SENT, "byte %lu past the result was written", (unsigned long)i);
    /* a roomy buffer returns the same size */
    memset(buf, SENT, need + 64);
    CHECK(NtGdiGetStringBitmapW_(d.dc, &ch, 1, need + 32, buf) == need);
    HeapFree(GetProcessHeap(), 0, buf);
    g5_fdc_close(&d);
}
TEST(gap5_stringbitmap_size_protocol_A) { G5_GUI(); G5_NEED(g5_nt("NtGdiGetStringBitmapW")); sb_layout(L'A'); }
TEST(gap5_stringbitmap_size_protocol_W) { G5_GUI(); G5_NEED(g5_nt("NtGdiGetStringBitmapW")); sb_layout(L'W'); }
TEST(gap5_stringbitmap_size_protocol_dot) { G5_GUI(); G5_NEED(g5_nt("NtGdiGetStringBitmapW")); sb_layout(L'.'); }
TEST(gap5_stringbitmap_wider_glyph_is_not_narrower) {
    G5_GUI(); G5_NT(NtGdiGetStringBitmapW); G5_NEED(NtGdiGetStringBitmapW_);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    WCHAR w = L'W', dot = L'.';
    ULONG sw = NtGdiGetStringBitmapW_(d.dc, &w, 1, 0, NULL), sd = NtGdiGetStringBitmapW_(d.dc, &dot, 1, 0, NULL);
    if (!sw || !sd) { t_note("size queries returned %lu %lu", (unsigned long)sw, (unsigned long)sd); g5_fdc_close(&d); return; }
    CHECKF(sw >= sd, "W bitmap %lu bytes, '.' bitmap %lu bytes", (unsigned long)sw, (unsigned long)sd);
    g5_fdc_close(&d);
}
TEST(gap5_stringbitmap_ansi_wrapper) {
    G5_GUI(); G5_NT(NtGdiGetStringBitmapW); G5_NEED(NtGdiGetStringBitmapW_);
    GetStringBitmapA_pfn a = (GetStringBitmapA_pfn)t_proc_in("gdi32.dll", "GetStringBitmapA");
    G5_NEED(a);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    /* gdi32full!GetStringBitmapA: byte counts >= 3 fail in user mode; 1..2 bytes convert to exactly one WCHAR
     * and forward to the syscall with cwc = 1. */
    CHECK(a(d.dc, "ABC", 3, 0, NULL) == 0);
    CHECK(a(d.dc, "ABC", 100, 0, NULL) == 0);
    WCHAR wc = L'A';
    ULONG sw = NtGdiGetStringBitmapW_(d.dc, &wc, 1, 0, NULL);
    CHECK(a(d.dc, "A", 1, 0, NULL) == sw);
    CHECK(a(NULL, "A", 1, 0, NULL) == 0);
    g5_fdc_close(&d);
}

/* ---------------------------------------------------------------- NtGdiGetFontFileInfo / NtGdiGetFontFileData
 * Both identify a font by the instance id GetFontRealizationInfo reports (PFF id) and a file index below the
 * PFF's file count (+0x24). Unknown id or index -> ERROR_INVALID_PARAMETER (0x57) returned as the value, with
 * nothing written. FileInfo: required size = 0x10 + 2*(len+1) written to *pNeeded first, ERROR_INSUFFICIENT_BUFFER
 * (0x7a) when the buffer is NULL/too small. FileData: offset+len must lie within the file, else 0x57. */
TEST(gap5_fontfile_info_unknown_id) {
    G5_GUI(); G5_NT(NtGdiGetFontFileInfo); G5_NEED(NtGdiGetFontFileInfo_);
    ULONG ids[] = { 0xFFFFFFFF, 0x7FFFFFFF, 0xDEAD0000, 0x80000001 };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        unsigned char buf[0x40]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x40]; memcpy(ref, buf, sizeof ref);
        SIZE_T need = 0x1234;
        ULONG r = NtGdiGetFontFileInfo_(ids[i], 0, buf, sizeof buf, &need);
        CHECKF(r == ERROR_INVALID_PARAMETER, "id 0x%lx: returned %lu", (unsigned long)ids[i], (unsigned long)r);
        CHECKF(need == 0x1234, "id 0x%lx: *pNeeded written", (unsigned long)ids[i]);
        CHECK(!memcmp(buf, ref, sizeof buf));
        CHECK(NtGdiGetFontFileInfo_(ids[i], 0xFFFFFFFF, NULL, 0, NULL) == ERROR_INVALID_PARAMETER);
    }
}
TEST(gap5_fontfile_data_unknown_id) {
    G5_GUI(); G5_NT(NtGdiGetFontFileData); G5_NEED(NtGdiGetFontFileData_);
    ULONG ids[] = { 0xFFFFFFFF, 0x7FFFFFFF, 0xDEAD0000, 0x80000001 };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        unsigned char buf[0x40]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x40]; memcpy(ref, buf, sizeof ref);
        ULONG64 off = 0;
        CHECKF(NtGdiGetFontFileData_(ids[i], 0, &off, buf, 16) == ERROR_INVALID_PARAMETER, "id 0x%lx", (unsigned long)ids[i]);
        CHECK(!memcmp(buf, ref, sizeof buf));
        CHECK(off == 0);
    }
}
TEST(gap5_fontfile_data_user_wrapper_sets_last_error) {
    G5_GUI();
    GetFontFileData_pfn w = (GetFontFileData_pfn)t_proc_in("gdi32.dll", "GetFontFileData");
    G5_NEED(w);
    unsigned char buf[16];
    SET_ERR(0);
    CHECK(w(0xFFFFFFFF, 0, 0, buf, sizeof buf) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* Realisation info: {size, flags/kind bytes, +8 cache id, +0xc instance id, +0x10 file count, +0x14 face index,
 * +0x16 simulations} for size 0x18 (RFONTOBJ::GetRealizationInfo). */
typedef struct { ULONG size; BYTE kind, kind2; USHORT zero; ULONG cache; ULONG instance; ULONG files; USHORT face, sim; } g5_real;
static int get_real(HDC dc, g5_real *ri) {
    GetFontRealizationInfo_pfn f = (GetFontRealizationInfo_pfn)t_proc_in("gdi32.dll", "GetFontRealizationInfo");
    if (!f) return 0;
    memset(ri, 0, sizeof *ri);
    ri->size = 0x18;
    return f(dc, ri) != 0;
}
static int open_ttf(g5_fdc *d, g5_real *ri) {
    if (!g5_fdc_open(d, L"Arial")) return 0;
    if (!get_real(d->dc, ri)) return 0;
    return ri->files >= 1;
}
TEST(gap5_fontfile_realization_fields) {
    G5_GUI();
    g5_fdc d; g5_real ri;
    if (!open_ttf(&d, &ri)) { g5_fdc_close(&d); t_skip("no realization info for Arial"); return; }
    CHECK(ri.size == 0x18);
    CHECK(ri.files >= 1 && ri.files < 64);
    CHECK(ri.instance != 0);
    t_note("Arial realization: kind=%u/%u cache=%lu instance=%lu files=%lu face=%u sim=%u", ri.kind, ri.kind2, (unsigned long)ri.cache, (unsigned long)ri.instance, (unsigned long)ri.files, ri.face, ri.sim);
    g5_fdc_close(&d);
}
TEST(gap5_fontfile_info_size_protocol) {
    G5_GUI(); G5_NT(NtGdiGetFontFileInfo); G5_NEED(NtGdiGetFontFileInfo_);
    g5_fdc d; g5_real ri;
    if (!open_ttf(&d, &ri)) { g5_fdc_close(&d); t_skip("no realization info for Arial"); return; }
    SIZE_T need = 0;
    ULONG r = NtGdiGetFontFileInfo_(ri.instance, 0, NULL, 0, &need);
    CHECKF(r == ERROR_INSUFFICIENT_BUFFER, "size query returned %lu", (unsigned long)r);
    CHECKF(need >= 0x12 && (need & 1) == 0, "needed %lu", (unsigned long)need);
    SIZE_T need_null_out = 0;
    CHECK(NtGdiGetFontFileInfo_(ri.instance, 0, NULL, 0, NULL) == ERROR_INSUFFICIENT_BUFFER);
    (void)need_null_out;
    if (need < 0x12 || need > 0x2000) { g5_fdc_close(&d); return; }
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, need + 32);
    REQUIRE(buf);
    memset(buf, SENT, need + 32);
    SIZE_T n2 = 0;
    r = NtGdiGetFontFileInfo_(ri.instance, 0, buf, need - 1, &n2);
    CHECK(r == ERROR_INSUFFICIENT_BUFFER);
    CHECK(n2 == need);
    int clean = 1; for (SIZE_T i = 0; i < need + 32; i++) clean &= (buf[i] == SENT);
    CHECK(clean);
    SIZE_T n3 = 0;
    r = NtGdiGetFontFileInfo_(ri.instance, 0, buf, need, &n3);
    CHECK(r == 0);
    CHECK(n3 == need);
    for (SIZE_T i = need; i < need + 32; i++) CHECKF(buf[i] == SENT, "byte %lu past the buffer was written", (unsigned long)i);
    const WCHAR *path = (const WCHAR *)(buf + 0x10);
    SIZE_T maxc = (need - 0x10) / 2, len = 0;
    while (len < maxc && path[len]) len++;
    CHECKF(len < maxc, "path not terminated within the reported size");
    CHECKF(need == 0x10 + (len + 1) * 2, "need %lu vs path length %lu", (unsigned long)need, (unsigned long)len);
    ULONG64 fsize = *(ULONG64 *)(buf + 8);
    CHECKF(fsize > 0 && (fsize >> 32) == 0, "file size field %llu", (unsigned long long)fsize);
    t_note("font file: %ls size=%llu writetime=%08lx%08lx", path, (unsigned long long)fsize, (unsigned long)((ULONG *)buf)[1], (unsigned long)((ULONG *)buf)[0]);
    /* the size field is the length of the file the path names, when the path can be opened */
    WCHAR ow[MAX_PATH + 8];
    if (len > 4 && len < MAX_PATH && !memcmp(path, L"\\??\\", 8)) { wcscpy(ow, L"\\\\?\\"); wcscat(ow, path + 4); }
    else wcscpy(ow, len < MAX_PATH ? path : L"");
    HANDLE f = ow[0] ? U(CreateFileW)(ow, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz;
        if (GetFileSizeEx(f, &sz)) CHECKF((ULONG64)sz.QuadPart == fsize, "disk size %lld vs reported %llu", (long long)sz.QuadPart, (unsigned long long)fsize);
        CloseHandle(f);
    } else t_note("font path not openable by name (%lu)", GetLastError());
    HeapFree(GetProcessHeap(), 0, buf);
    g5_fdc_close(&d);
}
TEST(gap5_fontfile_info_index_out_of_range) {
    G5_GUI(); G5_NT(NtGdiGetFontFileInfo); G5_NEED(NtGdiGetFontFileInfo_);
    g5_fdc d; g5_real ri;
    if (!open_ttf(&d, &ri)) { g5_fdc_close(&d); t_skip("no realization info for Arial"); return; }
    ULONG idx[] = { ri.files, ri.files + 1, 0x1000, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        unsigned char buf[0x40]; memset(buf, SENT, sizeof buf);
        unsigned char ref[0x40]; memcpy(ref, buf, sizeof ref);
        SIZE_T need = 0x777;
        CHECKF(NtGdiGetFontFileInfo_(ri.instance, idx[i], buf, sizeof buf, &need) == ERROR_INVALID_PARAMETER, "index %lu", (unsigned long)idx[i]);
        CHECK(need == 0x777);
        CHECK(!memcmp(buf, ref, sizeof buf));
    }
    g5_fdc_close(&d);
}
static int file_len_via_info(ULONG id, ULONG64 *len) {
    G5_NT(NtGdiGetFontFileInfo);
    unsigned char buf[0x800]; SIZE_T need = 0;
    if (!NtGdiGetFontFileInfo_) return 0;
    if (NtGdiGetFontFileInfo_(id, 0, NULL, 0, &need) != ERROR_INSUFFICIENT_BUFFER || need > sizeof buf) return 0;
    if (NtGdiGetFontFileInfo_(id, 0, buf, need, &need) != 0) return 0;
    *len = *(ULONG64 *)(buf + 8);
    return 1;
}
TEST(gap5_fontfile_data_header_and_bounds) {
    G5_GUI(); G5_NT(NtGdiGetFontFileData); G5_NEED(NtGdiGetFontFileData_);
    g5_fdc d; g5_real ri;
    if (!open_ttf(&d, &ri)) { g5_fdc_close(&d); t_skip("no realization info for Arial"); return; }
    ULONG64 flen = 0;
    if (!file_len_via_info(ri.instance, &flen) || !flen) { g5_fdc_close(&d); t_skip("no file length"); return; }
    unsigned char hdr[16]; memset(hdr, SENT, sizeof hdr);
    ULONG64 off = 0;
    ULONG r = NtGdiGetFontFileData_(ri.instance, 0, &off, hdr, 4);
    CHECKF(r == 0, "header read returned %lu", (unsigned long)r);
    if (r == 0) {
        int ok = !memcmp(hdr, "\0\1\0\0", 4) || !memcmp(hdr, "true", 4) || !memcmp(hdr, "OTTO", 4) || !memcmp(hdr, "ttcf", 4);
        CHECKF(ok, "unexpected sfnt tag %02x %02x %02x %02x", hdr[0], hdr[1], hdr[2], hdr[3]);
        for (int i = 4; i < 16; i++) CHECK(hdr[i] == SENT);
    }
    /* last byte of the file is readable, one past is not */
    unsigned char last = 0; off = flen - 1;
    CHECK(NtGdiGetFontFileData_(ri.instance, 0, &off, &last, 1) == 0);
    unsigned char b2 = SENT; off = flen;
    CHECK(NtGdiGetFontFileData_(ri.instance, 0, &off, &b2, 1) == ERROR_INVALID_PARAMETER);
    CHECK(b2 == SENT);
    off = flen - 1;
    CHECK(NtGdiGetFontFileData_(ri.instance, 0, &off, hdr, 2) == ERROR_INVALID_PARAMETER);
    /* offset+length overflows 64 bits, or a length no file can have */
    off = 0xFFFFFFFFFFFFFFF0ull;
    CHECK(NtGdiGetFontFileData_(ri.instance, 0, &off, hdr, 16) == ERROR_INVALID_PARAMETER);
    off = 0;
    CHECK(NtGdiGetFontFileData_(ri.instance, 0, &off, hdr, (SIZE_T)-1) == ERROR_INVALID_PARAMETER);
    off = 1;
    CHECK(NtGdiGetFontFileData_(ri.instance, 0, &off, hdr, (SIZE_T)-1) == ERROR_INVALID_PARAMETER);
    for (int i = 4; i < 16; i++) CHECK(hdr[i] == SENT);
    g5_fdc_close(&d);
}
TEST(gap5_fontfile_data_index_out_of_range) {
    G5_GUI(); G5_NT(NtGdiGetFontFileData); G5_NEED(NtGdiGetFontFileData_);
    g5_fdc d; g5_real ri;
    if (!open_ttf(&d, &ri)) { g5_fdc_close(&d); t_skip("no realization info for Arial"); return; }
    unsigned char buf[8]; memset(buf, SENT, sizeof buf);
    ULONG idx[] = { ri.files, ri.files + 1, 0x1000, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        ULONG64 off = 0;
        CHECKF(NtGdiGetFontFileData_(ri.instance, idx[i], &off, buf, 4) == ERROR_INVALID_PARAMETER, "index %lu", (unsigned long)idx[i]);
    }
    for (int i = 0; i < 8; i++) CHECK(buf[i] == SENT);
    g5_fdc_close(&d);
}
TEST(gap5_fontfile_data_matches_disk_file) {
    G5_GUI(); G5_NT(NtGdiGetFontFileData); G5_NT(NtGdiGetFontFileInfo);
    G5_NEED(NtGdiGetFontFileData_); G5_NEED(NtGdiGetFontFileInfo_);
    g5_fdc d; g5_real ri;
    if (!open_ttf(&d, &ri)) { g5_fdc_close(&d); t_skip("no realization info for Arial"); return; }
    unsigned char ib[0x800]; SIZE_T need = 0;
    if (NtGdiGetFontFileInfo_(ri.instance, 0, NULL, 0, &need) != ERROR_INSUFFICIENT_BUFFER || need > sizeof ib) { g5_fdc_close(&d); t_skip("no file info"); return; }
    CHECK(NtGdiGetFontFileInfo_(ri.instance, 0, ib, need, &need) == 0);
    const WCHAR *path = (const WCHAR *)(ib + 0x10);
    WCHAR ow[MAX_PATH + 8];
    if (!wcsncmp(path, L"\\??\\", 4)) { wcscpy(ow, L"\\\\?\\"); wcsncat(ow, path + 4, MAX_PATH); } else wcsncpy(ow, path, MAX_PATH);
    ow[MAX_PATH + 7] = 0;
    HANDLE f = U(CreateFileW)(ow, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { t_note("font file not openable (%lu)", GetLastError()); g5_fdc_close(&d); return; }
    unsigned char want[256], got[256];
    DWORD n = 0;
    ULONG64 flen = *(ULONG64 *)(ib + 8);
    ULONG64 offs[] = { 0, 4, 64, flen > 300 ? flen / 2 : 0 };
    for (size_t i = 0; i < sizeof offs / sizeof offs[0]; i++) {
        LARGE_INTEGER p; p.QuadPart = (LONGLONG)offs[i];
        SetFilePointerEx(f, p, NULL, FILE_BEGIN);
        if (!ReadFile(f, want, sizeof want, &n, NULL) || n < 16) continue;
        ULONG64 off = offs[i];
        memset(got, SENT, sizeof got);
        CHECKF(NtGdiGetFontFileData_(ri.instance, 0, &off, got, n) == 0, "offset %llu", (unsigned long long)offs[i]);
        CHECKF(!memcmp(got, want, n), "offset %llu: bytes differ from the file on disk", (unsigned long long)offs[i]);
    }
    CloseHandle(f);
    g5_fdc_close(&d);
}
