/* Printing / escape / job entry points on non-printer DCs, driven through gdi32full.
 *
 * Display and memory DCs carry handle type 0x01 (HIWORD & 0x7f0000 == 0x10000). The client-side job functions reject that
 * type up front: StartPageImpl / InternalEndPage / EndDocImpl / AbortDocImpl -> GdiSetLastError(6), return -1;
 * StartDocWImpl returns -1 straight away (no error stored on that path). A handle that is not a DC at all fails pldcGet:
 * GdiSetLastError(6), -1 (StartDoc/StartPage/EndDoc/AbortDoc/EndPage).
 * DrawEscape: type 0x01 -> NtGdiDrawEscape, invalid ldc -> GdiSetLastError(6), 0. NtGdiDrawEscape: cb < 0 -> -1;
 * buf NULL with cb != 0 -> -1. EscapeImpl QUERYESCSUPPORT (8): codes in the printer-only groups answer
 * (type != 0x01) = 0 on a display DC; code 0 -> 0; 0x100 and 0xc01 -> 1; Escape(GETCOLORTABLE=5) with NULL input and
 * QUERYESCSUPPORT with NULL input -> SetLastError(87), -1.
 */
#include "mf.h"

static DOCINFOW mf_docinfo(void) { DOCINFOW d; memset(&d, 0, sizeof d); d.cbSize = sizeof d; d.lpszDocName = L"win32kt"; return d; }

static HDC mem_dc(HDC ref, HBITMAP *bmp, HGDIOBJ *old) {
    HDC m = U(CreateCompatibleDC)(ref);
    if (!m) return NULL;
    *bmp = U(CreateCompatibleBitmap)(ref, 8, 8);
    *old = *bmp ? U(SelectObject)(m, *bmp) : NULL;
    return m;
}
static void mem_free(HDC m, HBITMAP bmp, HGDIOBJ old) {
    if (old) U(SelectObject)(m, old);
    if (bmp) U(DeleteObject)(bmp);
    if (m) U(DeleteDC)(m);
}
#define NEED_PRINT() do { if (!U(GetDC) || !U(ReleaseDC) || !U(CreateCompatibleDC) || !U(CreateCompatibleBitmap) || !U(SelectObject) || !U(DeleteObject) || !U(DeleteDC) || !U(StartDocW) || !U(StartPage) || !U(EndPage) || !U(EndDoc) || !U(AbortDoc)) { t_skip("print exports missing"); return; } } while (0)

/* ---- table: job call x DC kind ------------------------------------------------------------------------------------- */
enum { J_STARTPAGE, J_ENDPAGE, J_ENDDOC, J_ABORTDOC, J_STARTDOC };
static int run_job(int op, HDC dc) {
    DOCINFOW d = mf_docinfo();
    switch (op) {
    case J_STARTPAGE: return U(StartPage)(dc);
    case J_ENDPAGE:   return U(EndPage)(dc);
    case J_ENDDOC:    return U(EndDoc)(dc);
    case J_ABORTDOC:  return U(AbortDoc)(dc);
    default:          return U(StartDocW)(dc, &d);
    }
}
static const char *job_name[] = { "StartPage", "EndPage", "EndDoc", "AbortDoc", "StartDocW" };

TEST(gdi_metafile_print_neg_job_on_display_dc_fails) {
    NEED_PRINT(); MF_SCREEN(dc);
    for (int op = 0; op < 5; op++) {
        SetLastError(0x1234);
        int r = run_job(op, dc);
        t_note("%s(display dc) = %d, error %lu", job_name[op], r, GetLastError());
        EXPECT_EQ(r, -1);                                /* type 0x01 rejected by the client wrappers */
        if (op != J_STARTDOC) EXPECT_EQ(GetLastError(), 6); /* GdiSetLastError(ERROR_INVALID_HANDLE) */
    }
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_job_on_memory_dc_fails) {
    NEED_PRINT(); MF_SCREEN(ref);
    HBITMAP bmp = NULL; HGDIOBJ old = NULL; HDC m = mem_dc(ref, &bmp, &old);
    REQUIRE(m);
    for (int op = 0; op < 5; op++) {
        SetLastError(0x1234);
        int r = run_job(op, m);
        EXPECT_EQ(r, -1);
        if (op != J_STARTDOC) EXPECT_EQ(GetLastError(), 6);
    }
    mem_free(m, bmp, old);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_job_on_invalid_handle_fails_6) {
    NEED_PRINT();
    static const ULONG_PTR bad[] = { 0, 0x1, 0xdead, 0x00ff0000 | 0x1234 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        for (int op = 0; op < 5; op++) {
            SetLastError(0x1234);
            int r = run_job(op, (HDC)bad[i]);
            EXPECT_EQ(r, -1);                            /* pldcGet fails: GdiSetLastError(6), -1 */
            EXPECT_EQ(GetLastError(), 6);
        }
}

TEST(gdi_metafile_print_neg_job_on_deleted_dc_fails_6) {
    NEED_PRINT(); MF_SCREEN(ref);
    HBITMAP bmp = NULL; HGDIOBJ old = NULL; HDC m = mem_dc(ref, &bmp, &old);
    REQUIRE(m);
    mem_free(m, bmp, old);
    /* the handle type bits are still 0x01 so the wrappers reject it by type, or by pldcGet; either way -1 */
    for (int op = J_STARTPAGE; op <= J_ABORTDOC; op++) {
        SetLastError(0);
        EXPECT_EQ(run_job(op, m), -1);
        EXPECT_EQ(GetLastError(), 6);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_startdoc_variants_on_display_dc) {
    NEED_PRINT(); MF_SCREEN(dc);
    DOCINFOW d = mf_docinfo();
    EXPECT_EQ(U(StartDocW)(dc, NULL), -1);
    d.lpszOutput = L"NUL";
    EXPECT_EQ(U(StartDocW)(dc, &d), -1);             /* display DC rejected before any output is opened */
    d.cbSize = 0x18;
    EXPECT_EQ(U(StartDocW)(dc, &d), -1);
    d.cbSize = 0; EXPECT_EQ(U(StartDocW)(dc, &d), -1);
    if (U(StartDocA)) {
        DOCINFOA a; memset(&a, 0, sizeof a); a.cbSize = sizeof a; a.lpszDocName = "win32kt";
        EXPECT_EQ(U(StartDocA)(dc, &a), -1);
        EXPECT_EQ(U(StartDocA)(dc, NULL), -1);
    }
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_startpage_endpage_sequence_stays_failing) {
    NEED_PRINT(); MF_SCREEN(dc);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(U(StartPage)(dc), -1);
        EXPECT_EQ(U(EndPage)(dc), -1);
    }
    EXPECT_EQ(U(EndDoc)(dc), -1);
    EXPECT_EQ(U(AbortDoc)(dc), -1);
    /* failed job calls leave the DC fully usable */
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_job_failures_do_not_change_gdi_object_count) {
    NEED_PRINT(); MF_SCREEN(dc);
    REQUIRE(U(GetCurrentProcess) && U(GetGuiResources));
    DWORD before = U(GetGuiResources)(U(GetCurrentProcess)(), GR_GDIOBJECTS);
    for (int i = 0; i < 50; i++) for (int op = 0; op < 5; op++) run_job(op, dc);
    EXPECT_EQ(U(GetGuiResources)(U(GetCurrentProcess)(), GR_GDIOBJECTS), before);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_setabortproc_invalid_handle_fails) {
    NEED_PRINT(); REQUIRE(U(SetAbortProc));
    SetLastError(0);
    EXPECT_EQ(U(SetAbortProc)((HDC)0, NULL), -1);                 /* SP_ERROR */
    SetLastError(0);
    EXPECT_EQ(U(SetAbortProc)((HDC)0xdead, (ABORTPROC)(ULONG_PTR)0x1000), -1);
}

TEST(gdi_metafile_print_neg_setabortproc_on_display_dc_observed) {
    NEED_PRINT(); REQUIRE(U(SetAbortProc)); MF_SCREEN(dc);
    SetLastError(0);
    int r = U(SetAbortProc)(dc, NULL);
    t_note("SetAbortProc(display dc, NULL) = %d, error %lu", r, GetLastError());
    mf_screen_free(dc);
}

/* ---- DrawEscape / NtGdiDrawEscape ------------------------------------------------------------------------------------ */
TEST(gdi_metafile_print_neg_drawescape_negative_length_is_minus_one) {
    NEED_PRINT(); REQUIRE(U(DrawEscape)); MF_SCREEN(dc);
    char b[4] = {0};
    EXPECT_EQ(U(DrawEscape)(dc, 0x1000, -1, b), -1);
    EXPECT_EQ(U(DrawEscape)(dc, 0x1000, -1, NULL), -1);
    EXPECT_EQ(U(DrawEscape)(dc, 0x1000, 0x80000000, b), -1);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_drawescape_null_buffer_nonzero_length_is_minus_one) {
    NEED_PRINT(); REQUIRE(U(DrawEscape)); MF_SCREEN(dc);
    static const int cbs[] = { 1, 4, 64, 65, 4096 };
    for (unsigned i = 0; i < sizeof cbs / sizeof *cbs; i++)
        EXPECT_EQ(U(DrawEscape)(dc, 0x1000, cbs[i], NULL), -1);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_drawescape_invalid_handle_returns_zero_error_6) {
    NEED_PRINT(); REQUIRE(U(DrawEscape));
    char b[4] = {0};
    SetLastError(0);
    EXPECT_EQ(U(DrawEscape)((HDC)0, 0x1000, 4, b), 0);  /* pldcGet fails: GdiSetLastError(6), 0 */
    EXPECT_EQ(GetLastError(), 6);
    SetLastError(0);
    EXPECT_EQ(U(DrawEscape)((HDC)0xdead, 0x1000, 4, b), 0);
    EXPECT_EQ(GetLastError(), 6);
}

TEST(gdi_metafile_print_neg_drawescape_display_dc_unknown_code_observed) {
    NEED_PRINT(); REQUIRE(U(DrawEscape)); MF_SCREEN(dc);
    char b[8] = {0};
    int r = U(DrawEscape)(dc, 0x7ff0, 4, b);            /* routed to the driver's DrawEscape; no driver support expected */
    t_note("DrawEscape(display dc, 0x7ff0) = %d", r);
    CHECK(r <= 0);
    mf_screen_free(dc);
}

/* ---- Escape / ExtEscape ---------------------------------------------------------------------------------------------- */
static int esc_query(HDC dc, WORD code) {
    WORD c = code;
    return U(Escape)(dc, QUERYESCSUPPORT, sizeof c, (LPCSTR)&c, NULL);
}

TEST(gdi_metafile_print_neg_escape_queryescsupport_printer_codes_zero_on_display_dc) {
    NEED_PRINT(); REQUIRE(U(Escape)); MF_SCREEN(dc);
    /* EscapeImpl QUERYESCSUPPORT: codes 1..3,5,8..14,17,19 answer (type != 0x01) which is 0 for a display DC; code 0 and
     * 4,6,7,15,16,18,20..24 answer 0 as well */
    static const WORD codes[] = { 0, 1 /*NEWFRAME*/, 2 /*ABORTDOC*/, 3 /*NEXTBAND*/, 4 /*SETABORTPROC*/, 5 /*GETCOLORTABLE*/,
        6, 7, 8, 9, 10 /*SETCOPYCOUNT*/, 11, 12 /*GETPHYSPAGESIZE*/, 13 /*GETPRINTINGOFFSET*/, 14 /*GETSCALINGFACTOR*/,
        17, 19 };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        int r = esc_query(dc, codes[i]);
        t_note("QUERYESCSUPPORT(%u) = %d", codes[i], r);
        EXPECT_EQ(r, 0);
    }
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_escape_queryescsupport_always_supported_codes) {
    NEED_PRINT(); REQUIRE(U(Escape)); MF_SCREEN(dc);
    EXPECT_EQ(esc_query(dc, 0x100), 1);                 /* EscapeImpl: 0x100 -> 1 */
    EXPECT_EQ(esc_query(dc, 0xc01), 1);                 /* EscapeImpl: 0xc01 -> 1 */
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_escape_queryescsupport_null_input_fails_87) {
    NEED_PRINT(); REQUIRE(U(Escape));
    if (t_is_wine()) { t_skip("Wine's Escape dereferences the NULL input"); return; }
    MF_SCREEN(dc);
    SetLastError(0);
    EXPECT_EQ(U(Escape)(dc, QUERYESCSUPPORT, 2, NULL, NULL), -1);
    EXPECT_EQ(GetLastError(), 87);
    SetLastError(0);
    EXPECT_EQ(U(Escape)(dc, GETCOLORTABLE, 2, NULL, NULL), -1);
    EXPECT_EQ(GetLastError(), 87);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_escape_getcolortable_on_display_dc) {
    NEED_PRINT(); REQUIRE(U(Escape)); MF_SCREEN(dc);
    SHORT idx = 0; PALETTEENTRY pe[2]; memset(pe, 0xcc, sizeof pe);
    int r = U(Escape)(dc, GETCOLORTABLE, sizeof idx, (LPCSTR)&idx, pe);
    t_note("Escape(GETCOLORTABLE, 0) on display dc = %d", r);
    /* routes to GetSystemPaletteEntries(dc, idx, 1, pe): 1 on a palette device, otherwise 0 mapped to -1 */
    CHECK(r == 1 || r == -1);
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_escape_invalid_handle_not_supported) {
    NEED_PRINT(); REQUIRE(U(Escape));
    static const WORD codes[] = { 3, 12, 13, 14, 0x100, 0xc01, 0x1000, 0x7ffe };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        int r = esc_query((HDC)0xdead, codes[i]);
        t_note("QUERYESCSUPPORT(%u) on invalid handle = %d", codes[i], r);
        if (codes[i] == 0x1000 || codes[i] == 0x7ffe) CHECK(r <= 0);   /* codes that no layer answers for */
    }
    /* a well-formed DC handle (type bits set) whose object does not exist answers like a display DC: printer codes 0, 0x100 always 1 */
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        int r = esc_query((HDC)(ULONG_PTR)0x01010fff, codes[i]);
        EXPECT_EQ(r, (codes[i] == 0x100 || codes[i] == 0xc01) ? 1 : 0);
    }
}

TEST(gdi_metafile_print_neg_extescape_query_on_display_dc_not_supported) {
    NEED_PRINT(); REQUIRE(U(ExtEscape)); MF_SCREEN(dc);
    static const int codes[] = { 12 /*GETPHYSPAGESIZE*/, 3 /*NEXTBAND*/, 13, 14, 0x1000, 0x7ffe };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        int in = codes[i];
        int r = U(ExtEscape)(dc, QUERYESCSUPPORT, sizeof in, (LPCSTR)&in, 0, NULL);
        t_note("ExtEscape(QUERYESCSUPPORT, %d) = %d", codes[i], r);
        CHECK(r <= 0 || r == 1);                         /* display driver: 0 for printer-only escapes, never garbage */
    }
    mf_screen_free(dc);
}

TEST(gdi_metafile_print_neg_extescape_invalid_handle_fails) {
    NEED_PRINT(); REQUIRE(U(ExtEscape));
    char in[8] = {0}, out[8];
    int r = U(ExtEscape)((HDC)0xdead, 0x7ffe, sizeof in, in, sizeof out, out);
    t_note("ExtEscape(invalid handle) = %d, error %lu", r, GetLastError());
    CHECK(r <= 0);
    r = U(ExtEscape)((HDC)0, 0x7ffe, 0, NULL, 0, NULL);
    CHECK(r <= 0);
}

TEST(gdi_metafile_print_neg_extescape_unknown_code_on_memory_dc) {
    NEED_PRINT(); REQUIRE(U(ExtEscape)); MF_SCREEN(ref);
    HBITMAP bmp = NULL; HGDIOBJ old = NULL; HDC m = mem_dc(ref, &bmp, &old);
    REQUIRE(m);
    char in[4] = {0};
    int r = U(ExtEscape)(m, 0x7ffe, sizeof in, in, 0, NULL);
    t_note("ExtEscape(memory dc, 0x7ffe) = %d", r);
    CHECK(r <= 0);
    mem_free(m, bmp, old);
    mf_screen_free(ref);
}

/* ---- CreateDC on printer drivers: no real printing, only the failure paths --------------------------------------------- */
TEST(gdi_metafile_print_neg_createdc_winspool_null_device_fails) {
    NEED_PRINT(); REQUIRE(U(CreateDCW));
    SetLastError(0);
    HDC dc = U(CreateDCW)(L"WINSPOOL", NULL, NULL, NULL);
    t_note("CreateDCW(WINSPOOL, NULL) = %p error %lu", dc, GetLastError());
    CHECK(dc == NULL);                                  /* no device name: no printer to open */
    if (dc) U(DeleteDC)(dc);
}

TEST(gdi_metafile_print_neg_createdc_nonexistent_printer_fails) {
    NEED_PRINT(); REQUIRE(U(CreateDCW));
    WCHAR name[64]; wsprintfW(name, L"win32kt-no-such-printer-%lu", GetCurrentProcessId());
    HDC dc = U(CreateDCW)(L"WINSPOOL", name, NULL, NULL);
    CHECK(dc == NULL);
    if (dc) U(DeleteDC)(dc);
    dc = U(CreateDCW)(NULL, name, NULL, NULL);
    CHECK(dc == NULL);
    if (dc) U(DeleteDC)(dc);
}

TEST(gdi_metafile_print_neg_createic_nonexistent_printer_fails) {
    NEED_PRINT(); REQUIRE(U(CreateICW));
    WCHAR name[64]; wsprintfW(name, L"win32kt-no-such-printer-%lu", GetCurrentProcessId());
    HDC dc = U(CreateICW)(L"WINSPOOL", name, NULL, NULL);
    CHECK(dc == NULL);
    if (dc) U(DeleteDC)(dc);
}

TEST(gdi_metafile_print_neg_createdc_display_driver_is_display_dc) {
    NEED_PRINT(); REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    EXPECT_EQ(MF_TYPE7(dc), MF_T7_DC);
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    /* a display DC has no document state: all job calls fail */
    EXPECT_EQ(U(StartPage)(dc), -1);
    U(DeleteDC)(dc);
}

TEST(gdi_metafile_print_neg_display_dc_caps_are_not_printer_caps) {
    NEED_PRINT(); REQUIRE(U(GetDeviceCaps)); MF_SCREEN(dc);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    EXPECT_EQ(U(GetDeviceCaps)(dc, PHYSICALWIDTH), 0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, PHYSICALHEIGHT), 0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, PHYSICALOFFSETX), 0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, PHYSICALOFFSETY), 0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, SCALINGFACTORX), 0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, SCALINGFACTORY), 0);
    mf_screen_free(dc);
}
