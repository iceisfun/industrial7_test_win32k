/* gap_05: GDI syscalls that are invalid-handle / no-op by construction - font-to-DC attach calls, bitmap and
 * brush attribute calls, EUDC link/timestamp validation. */
#include "g5.h"

/* ---------------------------------------------------------------- NtGdiAddEmbFontToDC / AddRemoteFontToDC / RemoveMergeFont
 * win32kfull: each starts by locking the DC (DCOBJ / XDCOBJ::vLock); a handle that is not a live DC of the right
 * type leaves the lock empty and the function returns 0 without looking at any other argument (AddEmbFontToDC
 * does read *pFontId first, so that pointer must be valid). */
TEST(gap5_addembfont_bad_dc_returns_zero) {
    G5_GUI(); G5_NT(NtGdiAddEmbFontToDC); G5_NEED(NtGdiAddEmbFontToDC_);
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) {
        ULONG64 id = 0x1234;
        ULONG r = NtGdiAddEmbFontToDC_((HDC)b.h[i], &id);
        CHECKF(r == 0, "%s: returned %lu", b.what[i], (unsigned long)r);
        CHECKF(id == 0x1234, "%s: id modified", b.what[i]);
    }
    /* the attempts must not have disturbed the objects used as wrong-type handles */
    if (b.pen) CHECK(U(GetObjectType)(b.pen) == OBJ_PEN);
    if (b.brush) CHECK(U(GetObjectType)(b.brush) == OBJ_BRUSH);
    if (b.bmp) CHECK(U(GetObjectType)(b.bmp) == OBJ_BITMAP);
    g5_bad_close(&b);
}
TEST(gap5_addembfont_ids_do_not_matter_for_bad_dc) {
    G5_GUI(); G5_NT(NtGdiAddEmbFontToDC); G5_NEED(NtGdiAddEmbFontToDC_);
    ULONG64 ids[] = { 0, 1, 0xFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        ULONG64 id = ids[i];
        CHECK(NtGdiAddEmbFontToDC_(NULL, &id) == 0);
        CHECK(id == ids[i]);
    }
}
TEST(gap5_addremotefont_bad_dc_returns_zero) {
    G5_GUI(); G5_NT(NtGdiAddRemoteFontToDC); G5_NEED(NtGdiAddRemoteFontToDC_);
    g5_bad b; g5_bad_open(&b);
    unsigned char fake[64] = {0};
    for (int i = 0; i < b.n; i++) {
        ULONG64 ufi = 0x55;
        ULONG r1 = NtGdiAddRemoteFontToDC_((HDC)b.h[i], NULL, 0, NULL);
        CHECKF(r1 == 0, "%s (NULL buf): returned %lu", b.what[i], (unsigned long)r1);
        ULONG r2 = NtGdiAddRemoteFontToDC_((HDC)b.h[i], fake, sizeof fake, &ufi);
        CHECKF(r2 == 0, "%s (buf): returned %lu", b.what[i], (unsigned long)r2);
        CHECKF(ufi == 0x55, "%s: ufi modified", b.what[i]);
    }
    g5_bad_close(&b);
}
TEST(gap5_removemergefont_bad_dc_returns_zero) {
    G5_GUI(); G5_NT(NtGdiRemoveMergeFont); G5_NEED(NtGdiRemoveMergeFont_);
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) {
        ULONG64 ufi = 0;
        ULONG r = NtGdiRemoveMergeFont_((HDC)b.h[i], &ufi);
        CHECKF(r == 0, "%s: returned %lu", b.what[i], (unsigned long)r);
    }
    g5_bad_close(&b);
}

/* ---------------------------------------------------------------- NtGdiSet/ClearBitmapAttributes
 * win32kfull: both start with; only bit 0 (stock) is ever acted on.
 * gdi32full!Set/ClearBitmapAttributes additionally return 0 in user mode when any bit above bit 0 is set. */
TEST(gap5_bmattr_without_bit0_is_noop) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes); G5_NT(NtGdiClearBitmapAttributes);
    G5_NEED(NtGdiSetBitmapAttributes_); G5_NEED(NtGdiClearBitmapAttributes_);
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 1, NULL);
    REQUIRE(bm);
    struct k_gdicell before, after;
    int have = k_gdi_cell(bm, &before);
    DWORD flags[] = { 0, 2, 4, 6, 0x10, 0x80, 0x100, 0x80000000u, 0xFFFFFFFEu };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        CHECKF(NtGdiSetBitmapAttributes_(bm, flags[i]) == 0, "set 0x%lx", flags[i]);
        CHECKF(NtGdiClearBitmapAttributes_(bm, flags[i]) == 0, "clear 0x%lx", flags[i]);
    }
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP);
    if (have && k_gdi_cell(bm, &after)) {
        CHECK(before.object == after.object);
        CHECK(before.type_check == after.type_check);
    }
    CHECK(U(DeleteObject)(bm));
}
TEST(gap5_bmattr_without_bit0_ignores_the_handle) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes); G5_NT(NtGdiClearBitmapAttributes);
    G5_NEED(NtGdiSetBitmapAttributes_); G5_NEED(NtGdiClearBitmapAttributes_);
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) {
        CHECKF(NtGdiSetBitmapAttributes_((HBITMAP)b.h[i], 0) == 0, "set %s", b.what[i]);
        CHECKF(NtGdiClearBitmapAttributes_((HBITMAP)b.h[i], 2) == 0, "clear %s", b.what[i]);
    }
    g5_bad_close(&b);
}
TEST(gap5_bmattr_set_stock_bad_handle_zero) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes); G5_NEED(NtGdiSetBitmapAttributes_);
    /* Making a bitmap stock requires a bitmap object (type 5); no such object -> result stays 0. */
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) {
        if (b.h[i] == (HGDIOBJ)b.bmp) continue; /* a real bitmap: state-changing, see dx_gap_05 */
        /* Only handles that are not a live bitmap may be passed (a surface made stock is irreversible for
         * this handle): the guessed index handle is re-checked, GetObjectType() of a live bitmap is OBJ_BITMAP. */
        if (U(GetObjectType) && U(GetObjectType)(b.h[i]) == OBJ_BITMAP) continue;
        ULONG_PTR r = NtGdiSetBitmapAttributes_((HBITMAP)b.h[i], 1);
        CHECKF(r == 0, "%s: returned %p", b.what[i], (void *)r);
    }
    if (b.pen) CHECK(U(GetObjectType)(b.pen) == OBJ_PEN);
    g5_bad_close(&b);
}
TEST(gap5_bmattr_clear_stock_nonexistent_zero) {
    G5_GUI(); G5_NT(NtGdiClearBitmapAttributes); G5_NEED(NtGdiClearBitmapAttributes_);
    /* A handle that names no bitmap cannot be turned into a non-stock bitmap. */
    HBITMAP none[] = { NULL, (HBITMAP)1, (HBITMAP)0xFFFFFFFF, (HBITMAP)0x7FFFFFFF, (HBITMAP)0x00050123 };
    for (size_t i = 0; i < sizeof none / sizeof none[0]; i++) {
        ULONG_PTR r = NtGdiClearBitmapAttributes_(none[i], 1);
        CHECKF(r == 0, "%p: returned %p", none[i], (void *)r);
    }
}
TEST(gap5_bmattr_user_mode_masks_high_bits) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes);
    SetBitmapAttributes_pfn s = (SetBitmapAttributes_pfn)t_proc_in("gdi32.dll", "SetBitmapAttributes");
    ClearBitmapAttributes_pfn c = (ClearBitmapAttributes_pfn)t_proc_in("gdi32.dll", "ClearBitmapAttributes");
    G5_NEED(s); G5_NEED(c);
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 1, NULL);
    REQUIRE(bm);
    /* gdi32full!SetBitmapAttributes before the syscall */
    DWORD flags[] = { 2, 3, 0x10, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        CHECKF(s(bm, flags[i]) == 0, "SetBitmapAttributes 0x%lx", flags[i]);
        CHECKF(c(bm, flags[i]) == 0, "ClearBitmapAttributes 0x%lx", flags[i]);
    }
    CHECK(s(bm, 0) == 0);
    CHECK(c(bm, 0) == 0);
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP);
    CHECK(U(DeleteObject)(bm));
}

/* ---------------------------------------------------------------- NtGdiSet/ClearBrushAttributes
 * win32kfull 0x52ed0 is `xor eax,eax; ret` for both exports: any arguments, result 0, nothing touched. */
TEST(gap5_brushattr_always_zero) {
    G5_GUI(); G5_NT(NtGdiSetBrushAttributes); G5_NT(NtGdiClearBrushAttributes);
    G5_NEED(NtGdiSetBrushAttributes_); G5_NEED(NtGdiClearBrushAttributes_);
    HBRUSH br = U(CreateSolidBrush)(RGB(10, 20, 30));
    REQUIRE(br);
    DWORD flags[] = { 0, 1, 2, 3, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        CHECKF(NtGdiSetBrushAttributes_(br, flags[i]) == 0, "set 0x%lx", flags[i]);
        CHECKF(NtGdiClearBrushAttributes_(br, flags[i]) == 0, "clear 0x%lx", flags[i]);
    }
    U(DeleteObject)(br);
}
TEST(gap5_brushattr_leaves_brush_untouched) {
    G5_GUI(); G5_NT(NtGdiSetBrushAttributes); G5_NT(NtGdiClearBrushAttributes);
    G5_NEED(NtGdiSetBrushAttributes_); G5_NEED(NtGdiClearBrushAttributes_);
    COLORREF col = RGB(11, 22, 33);
    HBRUSH br = U(CreateSolidBrush)(col);
    REQUIRE(br);
    struct k_gdicell before, after;
    int have = k_gdi_cell(br, &before);
    NtGdiSetBrushAttributes_(br, 1);
    NtGdiClearBrushAttributes_(br, 1);
    LOGBRUSH lb; memset(&lb, 0xCC, sizeof lb);
    CHECK(U(GetObjectW)(br, sizeof lb, &lb) == (int)sizeof lb);
    CHECK(lb.lbStyle == BS_SOLID);
    CHECK(lb.lbColor == col);
    CHECK(U(GetObjectType)(br) == OBJ_BRUSH);
    if (have && k_gdi_cell(br, &after)) {
        CHECK(before.object == after.object);
        CHECK(before.type_check == after.type_check);
        CHECK(before.field8 == after.field8);
    }
    CHECK(U(DeleteObject)(br));
    /* gdi32!GetObjectType keeps answering from the type encoded in a freed handle; the kernel lookup is what fails */
    LOGBRUSH dead; memset(&dead, 0, sizeof dead);
    CHECK(U(GetObjectW)(br, sizeof dead, &dead) == 0);
}
TEST(gap5_brushattr_stock_brush_unchanged) {
    G5_GUI(); G5_NT(NtGdiSetBrushAttributes); G5_NT(NtGdiClearBrushAttributes);
    G5_NEED(NtGdiSetBrushAttributes_); G5_NEED(NtGdiClearBrushAttributes_);
    HGDIOBJ st = U(GetStockObject)(WHITE_BRUSH);
    REQUIRE(st);
    CHECK(NtGdiSetBrushAttributes_((HBRUSH)st, 1) == 0);
    CHECK(NtGdiClearBrushAttributes_((HBRUSH)st, 1) == 0);
    LOGBRUSH lb;
    CHECK(U(GetObjectW)(st, sizeof lb, &lb) == (int)sizeof lb);
    CHECK(lb.lbColor == RGB(255, 255, 255));
}
TEST(gap5_brushattr_does_not_validate_the_handle) {
    G5_GUI(); G5_NT(NtGdiSetBrushAttributes); G5_NT(NtGdiClearBrushAttributes);
    G5_NEED(NtGdiSetBrushAttributes_); G5_NEED(NtGdiClearBrushAttributes_);
    g5_bad b; g5_bad_open(&b);
    for (int i = 0; i < b.n; i++) {
        SET_ERR(0x3333);
        CHECKF(NtGdiSetBrushAttributes_((HBRUSH)b.h[i], 1) == 0, "set %s", b.what[i]);
        CHECKF(NtGdiClearBrushAttributes_((HBRUSH)b.h[i], 1) == 0, "clear %s", b.what[i]);
        CHECKF(GetLastError() == 0x3333, "%s: last error %lu", b.what[i], GetLastError());
    }
    g5_bad_close(&b);
}
TEST(gap5_brushattr_user_mode_path) {
    G5_GUI(); G5_NT(NtGdiSetBrushAttributes); G5_NEED(NtGdiSetBrushAttributes_);
    SetBrushAttributes_pfn s = (SetBrushAttributes_pfn)t_proc_in("gdi32.dll", "SetBrushAttributes");
    ClearBrushAttributes_pfn c = (ClearBrushAttributes_pfn)t_proc_in("gdi32.dll", "ClearBrushAttributes");
    G5_NEED(s); G5_NEED(c);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 1, 1));
    REQUIRE(br);
    /* gdi32full masks flags above bit 0 in user mode; both then end at the kernel no-op (0). */
    for (DWORD f = 0; f < 4; f++) {
        CHECKF(s(br, f) == 0, "SetBrushAttributes %lu", f);
        CHECKF(c(br, f) == 0, "ClearBrushAttributes %lu", f);
    }
    CHECK(U(DeleteObject)(br));
}

/* ---------------------------------------------------------------- NtGdiEudcLoadUnloadLink
 * win32kfull 0x298240 (base, cwcBase, path, cwcPath, prio, type, load): the first thing is
 *, else EngSetLastError(0x57) and return 0, for both the
 * load and the unload direction, before any pointer is dereferenced. */
static const WCHAR g_face[] = L"Arial";
static const WCHAR g_path[] = L"C:\\gap5-nonexistent-eudc.tte";
TEST(gap5_eudc_link_rejects_bad_lengths) {
    G5_GUI(); G5_NT(NtGdiEudcLoadUnloadLink); G5_NEED(NtGdiEudcLoadUnloadLink_);
    struct { ULONG cb, cp; const WCHAR *path; } rows[] = {
        { 0x21, 12, g_path }, { 0x22, 12, g_path }, { 0xFFFFFFFF, 12, g_path }, { 0x80000000u, 12, g_path },
        { 5, 0, g_path }, { 5, 0x124, g_path }, { 5, 0x1000, g_path }, { 5, 0xFFFFFFFF, g_path }, { 5, 0x7FFFFFFF, g_path },
        { 5, 12, NULL }, { 0, 0, g_path }, { 0x21, 0, NULL },
    };
    for (BOOL load = 0; load <= 1; load++)
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            SET_ERR(0);
            ULONG r = NtGdiEudcLoadUnloadLink_(g_face, rows[i].cb, rows[i].path, rows[i].cp, 0, 0, load);
            CHECKF(r == 0, "load=%d row %zu: returned %lu", load, i, (unsigned long)r);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "load=%d row %zu: last error %lu", load, i, GetLastError());
        }
}
TEST(gap5_eudc_link_validation_ignores_priority_and_type) {
    G5_GUI(); G5_NT(NtGdiEudcLoadUnloadLink); G5_NEED(NtGdiEudcLoadUnloadLink_);
    ULONG prios[] = { 0, 1, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof prios / sizeof prios[0]; i++) {
        SET_ERR(0);
        CHECK(NtGdiEudcLoadUnloadLink_(g_face, 0x21, g_path, 12, prios[i], prios[i], TRUE) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(gap5_eudc_link_null_face_still_needs_path) {
    G5_GUI(); G5_NT(NtGdiEudcLoadUnloadLink); G5_NEED(NtGdiEudcLoadUnloadLink_);
    /* A NULL base-face name selects the system-wide link, but the path is mandatory either way. */
    SET_ERR(0);
    CHECK(NtGdiEudcLoadUnloadLink_(NULL, 0, NULL, 0, 0, 0, FALSE) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(NtGdiEudcLoadUnloadLink_(NULL, 0, g_path, 0, 0, 0, TRUE) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---------------------------------------------------------------- NtGdiGetEudcTimeStampEx
 * win32kfull 0x298410 (name, cwc, bSystem): bSystem != 0 returns the system EUDC time stamp counter and never
 * looks at name; bSystem == 0 with NULL name or cwc == 0 returns the other (per-face) global; a name with
 * cwc > 0x20 is EngSetLastError(0x57) and 0; a name with 1..0x20 chars is copied (read only) and yields 0. */
TEST(gap5_eudc_timestamp_system_is_stable_and_ignores_name) {
    G5_GUI(); G5_NT(NtGdiGetEudcTimeStampEx); G5_NEED(NtGdiGetEudcTimeStampEx_);
    ULONG t = NtGdiGetEudcTimeStampEx_(NULL, 0, TRUE);
    for (int i = 0; i < 5; i++) CHECK(NtGdiGetEudcTimeStampEx_(NULL, 0, TRUE) == t);
    WCHAR name[0x40] = L"Arial";
    CHECK(NtGdiGetEudcTimeStampEx_(name, 5, TRUE) == t);
    CHECK(NtGdiGetEudcTimeStampEx_(name, 0x1000, TRUE) == t);
    CHECK(NtGdiGetEudcTimeStampEx_(name, 0xFFFFFFFF, 2) == t);
    t_note("system EUDC time stamp counter: %lu", (unsigned long)t);
}
TEST(gap5_eudc_timestamp_global_when_no_face) {
    G5_GUI(); G5_NT(NtGdiGetEudcTimeStampEx); G5_NEED(NtGdiGetEudcTimeStampEx_);
    WCHAR name[0x40] = L"Arial";
    ULONG g = NtGdiGetEudcTimeStampEx_(NULL, 0, FALSE);
    CHECK(NtGdiGetEudcTimeStampEx_(NULL, 7, FALSE) == g);   /* NULL name */
    CHECK(NtGdiGetEudcTimeStampEx_(name, 0, FALSE) == g);   /* cwc == 0 */
    CHECK(NtGdiGetEudcTimeStampEx_(NULL, 0, FALSE) == g);
}
TEST(gap5_eudc_timestamp_long_face_name_invalid) {
    G5_GUI(); G5_NT(NtGdiGetEudcTimeStampEx); G5_NEED(NtGdiGetEudcTimeStampEx_);
    WCHAR name[0x60]; for (int i = 0; i < 0x60; i++) name[i] = L'A';
    ULONG lens[] = { 0x21, 0x22, 0x40, 0x5F };
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        SET_ERR(0);
        CHECKF(NtGdiGetEudcTimeStampEx_(name, lens[i], FALSE) == 0, "len %lu", lens[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SET_ERR(0);
    CHECK(NtGdiGetEudcTimeStampEx_(name, 0xFFFFFFFF, FALSE) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap5_eudc_timestamp_named_face_returns_zero) {
    G5_GUI(); G5_NT(NtGdiGetEudcTimeStampEx); G5_NEED(NtGdiGetEudcTimeStampEx_);
    WCHAR name[0x40] = L"Arial";
    for (ULONG n = 1; n <= 0x20; n += 7) {
        SET_ERR(0x4242);
        CHECKF(NtGdiGetEudcTimeStampEx_(name, n, FALSE) == 0, "len %lu", n);
        CHECKF(GetLastError() == 0x4242, "len %lu: last error %lu", n, GetLastError());
    }
}
TEST(gap5_eudc_timestamp_user_mode_wrappers) {
    G5_GUI(); G5_NT(NtGdiGetEudcTimeStampEx); G5_NEED(NtGdiGetEudcTimeStampEx_);
    GetEUDCTimeStamp_pfn a = (GetEUDCTimeStamp_pfn)t_proc_in("gdi32.dll", "GetEUDCTimeStamp");
    GetEUDCTimeStampExW_pfn b = (GetEUDCTimeStampExW_pfn)t_proc_in("gdi32.dll", "GetEUDCTimeStampExW");
    G5_NEED(a); G5_NEED(b);
    /* GetEUDCTimeStamp() is NtGdiGetEudcTimeStampEx(NULL,0,1); the W form passes (name, wcslen, 0). */
    CHECK(a() == NtGdiGetEudcTimeStampEx_(NULL, 0, TRUE));
    CHECK(b(L"Arial") == 0);
    CHECK(b(NULL) == NtGdiGetEudcTimeStampEx_(NULL, 0, FALSE));
}
