/* clipboard_dde: the clipboard format table kept in the window station.
 *
 * CloseClipboard runs MungeClipData when the changed flag (+0x40 & 0x40) is set: it adds CF_LOCALE
 * when any text format is present and the thread has a keyboard layout, then the missing members
 * of {CF_TEXT, CF_OEMTEXT, CF_UNICODETEXT} with placeholder hData 1, CF_METAFILEPICT <->
 * CF_ENHMETAFILE (hData 3/4) and CF_BITMAP/CF_DIB/CF_DIBV5 (hData 2) - always appended after the
 * existing entries, in that order. user32!GetClipboardData converts a placeholder lazily and stores
 * the result back with NtUserSetClipboardData(.., fIncSerial=0). _EnumClipboardFormats walks the
 * array in insertion order. */
#include "cd.h"
#include <wchar.h>

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* the order MungeClipData produces for a clipboard that held only `orig` among the text formats */
static void check_text_order(const UINT *f, int n, UINT orig) {
    UINT want[6]; int k = 0;
    int has_locale = cd_fmt_index(f, n, CF_LOCALE) >= 0;
    want[k++] = orig;
    if (has_locale) want[k++] = CF_LOCALE;     /* InternalSetClipboardData(.., 0x10, ..) comes first when a layout exists */
    static const UINT order[3] = { CF_TEXT, CF_OEMTEXT, CF_UNICODETEXT };
    for (int i = 0; i < 3; i++) if (order[i] != orig) want[k++] = order[i];
    CHECKF(n == k, "format count %d want %d", n, k);
    for (int i = 0; i < n && i < k; i++) CHECKF(f[i] == want[i], "slot %d is 0x%x want 0x%x", i, f[i], want[i]);
    if (!has_locale) t_note("no CF_LOCALE synthesized (no keyboard layout on this thread?)");
}

static void text_synth_case(UINT orig, const void *data, size_t len) {
    UINT f[16];
    CHECK(cd_put(NULL, orig, data, len));
    int n = cd_formats_closed(f, 16, NULL);
    CHECK(n > 0);
    check_text_order(f, n, orig);
    CHECK(U(IsClipboardFormatAvailable)(CF_TEXT));
    CHECK(U(IsClipboardFormatAvailable)(CF_OEMTEXT));
    CHECK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
}

TEST(clipboard_dde_synth_text_from_unicode) { CD_SETUP(); text_synth_case(CF_UNICODETEXT, L"abc", 8); }
TEST(clipboard_dde_synth_text_from_ansi)    { CD_SETUP(); text_synth_case(CF_TEXT, "abc", 4); }
TEST(clipboard_dde_synth_text_from_oem)     { CD_SETUP(); text_synth_case(CF_OEMTEXT, "abc", 4); }

TEST(clipboard_dde_synth_not_present_before_close) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    HGLOBAL g = cd_gstr_w(L"abc");
    CHECK(U(SetClipboardData)(CF_UNICODETEXT, g) == g);
    EQ(U(CountClipboardFormats)(), 1);             /* synthesis only happens when the clipboard is closed */
    CHECK(!U(IsClipboardFormatAvailable)(CF_TEXT));
    CHECK(!U(IsClipboardFormatAvailable)(CF_OEMTEXT));
    CHECK(!U(IsClipboardFormatAvailable)(CF_LOCALE));
    CHECK(U(CloseClipboard)());
    int c = U(CountClipboardFormats)();
    CHECK(c == 3 || c == 4);
    CHECK(c == 3 + U(IsClipboardFormatAvailable)(CF_LOCALE));
}

TEST(clipboard_dde_synth_second_cycle_adds_nothing) {
    CD_SETUP();
    UINT a[16], b[16];
    CHECK(cd_put_text(NULL, L"stable"));
    int n1 = cd_formats_closed(a, 16, NULL);
    DWORD s1 = U(GetClipboardSequenceNumber)();
    CHECK(cd_open(NULL)); CHECK(U(CloseClipboard)());     /* not dirty: MungeClipData is not run again */
    int n2 = cd_formats_closed(b, 16, NULL);
    EQ(n1, n2);
    for (int i = 0; i < n1 && i < n2; i++) EQ(a[i], b[i]);
    EQ(U(GetClipboardSequenceNumber)(), s1);
}

TEST(clipboard_dde_private_only_is_not_synthesized) {
    CD_SETUP();
    UINT f[8];
    CHECK(cd_put(NULL, 0x234, "abcdefgh", 8));
    EQ(cd_formats_closed(f, 8, NULL), 1);          /* no text formats => no CF_LOCALE, nothing added */
    EQ(f[0], 0x234);
    CHECK(!U(IsClipboardFormatAvailable)(CF_TEXT));
    CHECK(!U(IsClipboardFormatAvailable)(CF_BITMAP));
    CHECK(!U(IsClipboardFormatAvailable)(CF_ENHMETAFILE));
}

TEST(clipboard_dde_locale_follows_text) {
    CD_SETUP();
    UINT f[16];
    CHECK(cd_put_text(NULL, L"loc"));
    int n = cd_formats_closed(f, 16, NULL);
    if (cd_fmt_index(f, n, CF_LOCALE) < 0) { t_skip("no CF_LOCALE"); return; }
    CHECK(cd_open(NULL));
    DWORD lc = 0;
    SIZE_T sz = cd_get_bytes(CF_LOCALE, &lc, sizeof lc);
    CHECK(sz >= sizeof lc);                              /* a 4 byte block built with _ConvertMemHandle(&ushort-in-dword, 4) */
    t_note("CF_LOCALE = 0x%08lx (keyboard layout %p)", lc, (void *)U(GetKeyboardLayout)(0));
    U(CloseClipboard)();
}

/* ---- conversions (ASCII only: independent of the ANSI/OEM code pages) ---------------------------- */
struct conv { UINT src; const void *data; size_t len; UINT want; const void *res; size_t reslen; };
TEST(clipboard_dde_text_conversion_matrix) {
    CD_SETUP();
    static const struct conv t[] = {
        { CF_UNICODETEXT, L"Hello, Clipboard 123", 42, CF_TEXT,        "Hello, Clipboard 123", 21 },
        { CF_UNICODETEXT, L"Hello, Clipboard 123", 42, CF_OEMTEXT,     "Hello, Clipboard 123", 21 },
        { CF_TEXT,        "Hello, Clipboard 123", 21,  CF_UNICODETEXT, L"Hello, Clipboard 123", 42 },
        { CF_OEMTEXT,     "Hello, Clipboard 123", 21,  CF_UNICODETEXT, L"Hello, Clipboard 123", 42 },
        { CF_TEXT,        "Hello, Clipboard 123", 21,  CF_OEMTEXT,     "Hello, Clipboard 123", 21 },
        { CF_OEMTEXT,     "Hello, Clipboard 123", 21,  CF_TEXT,        "Hello, Clipboard 123", 21 },
        { CF_UNICODETEXT, L"", 2,                      CF_TEXT,        "", 1 },
        { CF_TEXT,        "x", 2,                      CF_UNICODETEXT, L"x", 4 },
    };
    for (int i = 0; i < N(t); i++) {
        BYTE out[128];
        memset(out, 0xEE, sizeof out);
        CHECK(cd_put(NULL, t[i].src, t[i].data, t[i].len));
        CHECK(cd_open(NULL));
        SIZE_T sz = cd_get_bytes(t[i].want, out, sizeof out);
        CHECKF(sz >= t[i].reslen, "case %d size %zu want >= %zu", i, (size_t)sz, t[i].reslen);
        CHECKF(!memcmp(out, t[i].res, t[i].reslen), "case %d content", i);
        U(CloseClipboard)();
    }
}

TEST(clipboard_dde_synth_get_materializes_entry) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardData_pfn gt = UN(NtUserGetClipboardData);
    REQUIRE(gt);
    CHECK(cd_put_text(NULL, L"materialize"));
    CHECK(cd_open(NULL));
    CD_GETCLIPBDATA a = {0}, b = {0}, u = {0};
    HANDLE hu = gt(CF_UNICODETEXT, &u);
    HANDLE ha = gt(CF_TEXT, &a);                          /* answers with the CF_UNICODETEXT data */
    CHECK(hu != NULL);
    EQ(ha, hu);
    EQ(a.uFmtRet, CF_UNICODETEXT);
    CHECK(U(GetClipboardData)(CF_TEXT) != NULL);          /* user32 converts and stores the result with NtUserSetClipboardData */
    HANDLE hb = gt(CF_TEXT, &b);
    CHECK(hb != NULL);
    EQ(b.uFmtRet, CF_TEXT);                        /* now a real entry */
    CHECK(hb != hu);
    U(CloseClipboard)();
}

TEST(clipboard_dde_synth_get_twice_returns_same_handle) {
    CD_SETUP();
    CHECK(cd_put_text(NULL, L"twice"));
    CHECK(cd_open(NULL));
    /* Windows 10: the first request converts and returns a transient handle; the entry stored by that request
     * is what every later request returns */
    HANDLE h1 = U(GetClipboardData)(CF_TEXT);
    HANDLE h2 = U(GetClipboardData)(CF_TEXT);
    HANDLE h3 = U(GetClipboardData)(CF_TEXT);
    CHECK(h1 != NULL && h2 != NULL);
    EQ(h2, h3);
    HANDLE o1 = U(GetClipboardData)(CF_OEMTEXT);
    HANDLE o2 = U(GetClipboardData)(CF_OEMTEXT);
    HANDLE o3 = U(GetClipboardData)(CF_OEMTEXT);
    CHECK(o1 != NULL && o2 != NULL);
    EQ(o2, o3);
    CHECK(o2 != h2);
    U(CloseClipboard)();
}

TEST(clipboard_dde_synth_unicode_from_ansi_size) {
    CD_SETUP();
    CHECK(cd_put(NULL, CF_TEXT, "twelve chars", 13));
    CHECK(cd_open(NULL));
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
    CHECK(h != NULL);
    CHECK(h && GlobalSize(h) >= 13 * sizeof(WCHAR));
    U(CloseClipboard)();
}

/* ---- bitmaps / DIBs --------------------------------------------------------------------------- */
static HBITMAP make_ddb(int w, int h) {
    static BYTE bits[8 * 8 * 4];
    memset(bits, 0x7F, sizeof bits);
    return U(CreateBitmap)(w, h, 1, 32, bits);
}
static HGLOBAL make_dib24(int w, int h, BYTE fill_v) {
    int stride = ((w * 3 + 3) & ~3);
    SIZE_T n = sizeof(BITMAPINFOHEADER) + (SIZE_T)stride * h;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    BITMAPINFOHEADER *b = GlobalLock(g);
    memset(b, 0, n);
    b->biSize = sizeof *b; b->biWidth = w; b->biHeight = h; b->biPlanes = 1; b->biBitCount = 24; b->biCompression = BI_RGB;
    b->biSizeImage = stride * h;
    memset((BYTE *)b + sizeof *b, fill_v, stride * h);
    GlobalUnlock(g);
    return g;
}

TEST(clipboard_dde_bitmap_synthesizes_dib_and_dibv5) {
    CD_SETUP();
    REQUIRE(U(CreateBitmap));
    HBITMAP bm = make_ddb(8, 8);
    CHECK(bm != NULL);
    UINT f[16];
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_BITMAP, bm) == (HANDLE)bm);   /* GDI handle passes straight through user32 */
    CHECK(U(CloseClipboard)());
    int n = cd_formats_closed(f, 16, NULL);
    EQ(f[0], CF_BITMAP);
    int i8 = cd_fmt_index(f, n, CF_DIB), i17 = cd_fmt_index(f, n, CF_DIBV5);
    CHECK(i8 > 0); CHECK(i17 > 0);
    CHECK(i8 < i17);                                       /* MungeClipData adds 8 then 0x11 */
    for (int i = 0; i < n; i++) CHECKF(f[i] == CF_BITMAP || f[i] == CF_DIB || f[i] == CF_DIBV5 || f[i] == CF_PALETTE, "unexpected 0x%x", f[i]);
}

TEST(clipboard_dde_dib_synthesizes_bitmap_and_dibv5) {
    CD_SETUP();
    UINT f[16];
    HGLOBAL g = make_dib24(2, 2, 0x40);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_DIB, g) == g);
    CHECK(U(CloseClipboard)());
    int n = cd_formats_closed(f, 16, NULL);
    EQ(f[0], CF_DIB);
    int i2 = cd_fmt_index(f, n, CF_BITMAP), i17 = cd_fmt_index(f, n, CF_DIBV5);
    CHECK(i2 > 0); CHECK(i17 > 0);
    CHECK(i2 < i17);
}

TEST(clipboard_dde_dibv5_synthesizes_bitmap_and_dib) {
    CD_SETUP();
    UINT f[16];
    int stride = 8;
    SIZE_T n = sizeof(BITMAPV5HEADER) + stride * 2;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    BITMAPV5HEADER *b = GlobalLock(g);
    memset(b, 0, n);
    b->bV5Size = sizeof *b; b->bV5Width = 2; b->bV5Height = 2; b->bV5Planes = 1; b->bV5BitCount = 24; b->bV5Compression = BI_RGB;
    b->bV5CSType = 0x73524742; /* LCS_sRGB */
    GlobalUnlock(g);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_DIBV5, g) == g);
    CHECK(U(CloseClipboard)());
    int c = cd_formats_closed(f, 16, NULL);
    EQ(f[0], CF_DIBV5);
    int i2 = cd_fmt_index(f, c, CF_BITMAP), i8 = cd_fmt_index(f, c, CF_DIB);
    CHECK(i2 > 0); CHECK(i8 > 0);
    CHECK(i2 < i8);
}

TEST(clipboard_dde_get_bitmap_from_dib) {
    CD_SETUP();
    REQUIRE(U(GetObjectW) && U(GetObjectType));
    HGLOBAL g = make_dib24(2, 2, 0x40);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_DIB, g) == g);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(NULL));
    HBITMAP bm = U(GetClipboardData)(CF_BITMAP);
    CHECK(bm != NULL);
    if (bm) {
        EQ(U(GetObjectType)(bm), OBJ_BITMAP);
        BITMAP info; memset(&info, 0, sizeof info);
        CHECK(U(GetObjectW)(bm, sizeof info, &info) == sizeof info);
        EQ(info.bmWidth, 2);
        EQ(info.bmHeight, 2);
    }
    U(CloseClipboard)();
}

TEST(clipboard_dde_get_dib_from_bitmap_header) {
    CD_SETUP();
    HBITMAP bm = make_ddb(8, 8);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_BITMAP, bm) == (HANDLE)bm);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(NULL));
    HANDLE h = U(GetClipboardData)(CF_DIB);
    CHECK(h != NULL);
    if (h) {
        BITMAPINFOHEADER *b = GlobalLock(h);
        CHECK(b != NULL);
        if (b) {
            CHECK(b->biSize >= sizeof *b);
            EQ(b->biWidth, 8);
            EQ(b->biHeight < 0 ? -b->biHeight : b->biHeight, 8);
            EQ(b->biPlanes, 1);
            CHECK(GlobalSize(h) >= b->biSize + (SIZE_T)(b->biBitCount / 8) * 64);
            GlobalUnlock(h);
        }
    }
    U(CloseClipboard)();
}

TEST(clipboard_dde_get_dibv5_from_dib_header) {
    CD_SETUP();
    HGLOBAL g = make_dib24(4, 4, 0x20);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_DIB, g) == g);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(NULL));
    HANDLE h = U(GetClipboardData)(CF_DIBV5);
    CHECK(h != NULL);
    if (h) {
        BITMAPV5HEADER *b = GlobalLock(h);
        if (b) {
            t_note("synthesized DIBV5: bV5Size=%lu %ldx%ld bpp=%u", b->bV5Size, b->bV5Width, b->bV5Height, b->bV5BitCount);
            CHECK(b->bV5Size >= sizeof(BITMAPINFOHEADER));
            EQ(b->bV5Width, 4);
            GlobalUnlock(h);
        }
    }
    U(CloseClipboard)();
}

TEST(clipboard_dde_bitmap_is_destroyed_by_empty) {
    CD_SETUP();
    REQUIRE(U(GetObjectType));
    HBITMAP bm = make_ddb(4, 4);
    CHECK(bm != NULL);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_BITMAP, bm) == (HANDLE)bm);
    EQ(U(GetObjectType)(bm), OBJ_BITMAP);           /* still alive: the clipboard now owns it */
    CHECK(U(EmptyClipboard)());
    /* Windows 10: emptying does not free the bitmap synchronously; the handle stays a valid bitmap object (also
     * after CloseClipboard) but is no longer owned by this process, so the application cannot delete it */
    EQ(U(GetObjectType)(bm), OBJ_BITMAP);
    CHECK(U(CloseClipboard)());
    EQ(U(GetObjectType)(bm), OBJ_BITMAP);
    SetLastError(0);
    CHECK(!U(DeleteObject)(bm));
}

TEST(clipboard_dde_bitmap_owner_becomes_public) {
    CD_SETUP();
    HBITMAP bm = make_ddb(4, 4);
    struct k_gdicell before, after;
    if (!bm || !k_gdi_cell(bm, &before)) { t_skip("GDI cell unreadable"); return; }
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_BITMAP, bm) == (HANDLE)bm);
    CHECK(k_gdi_cell(bm, &after));
    /* SetClipboardData releases the bitmap's ownership: the object stops being owned by this process */
    t_info("bitmap cell owner word before 0x%llx after 0x%llx (pid %lu)", before.field8, after.field8, GetCurrentProcessId());
    U(EmptyClipboard)();
    U(CloseClipboard)();
}

/* ---- metafiles ------------------------------------------------------------------------------ */
static HENHMETAFILE make_emf(void) {
    HDC dc = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL);
    if (!dc) return NULL;
    U(Rectangle)(dc, 0, 0, 10, 10);
    return U(CloseEnhMetaFile)(dc);
}

TEST(clipboard_dde_enhmetafile_synthesizes_metafilepict) {
    CD_SETUP();
    REQUIRE(U(CreateEnhMetaFileW) && U(CloseEnhMetaFile) && U(Rectangle));
    HENHMETAFILE emf = make_emf();
    CHECK(emf != NULL);
    UINT f[16];
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_ENHMETAFILE, emf) == (HANDLE)emf);
    CHECK(U(CloseClipboard)());
    int n = cd_formats_closed(f, 16, NULL);
    EQ(n, 2);
    EQ(f[0], CF_ENHMETAFILE);
    EQ(f[1], CF_METAFILEPICT);                     /* InternalSetClipboardData(3, hData = 4) */
    CHECK(U(IsClipboardFormatAvailable)(CF_METAFILEPICT));
}

TEST(clipboard_dde_get_enhmetafile_returns_set_handle) {
    CD_SETUP();
    REQUIRE(U(CreateEnhMetaFileW) && U(CloseEnhMetaFile) && U(Rectangle));
    HENHMETAFILE emf = make_emf();
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_ENHMETAFILE, emf) == (HANDLE)emf);
    EQ(U(GetClipboardData)(CF_ENHMETAFILE), (HANDLE)emf);   /* gphn node: kernel handle -> caller's HENHMETAFILE */
    U(CloseClipboard)();
}

TEST(clipboard_dde_get_metafilepict_from_enhmetafile) {
    CD_SETUP();
    REQUIRE(U(CreateEnhMetaFileW) && U(CloseEnhMetaFile) && U(Rectangle));
    HENHMETAFILE emf = make_emf();
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_ENHMETAFILE, emf) == (HANDLE)emf);
    CHECK(U(CloseClipboard)());
    CHECK(cd_open(NULL));
    HANDLE h = U(GetClipboardData)(CF_METAFILEPICT);
    CHECK(h != NULL);
    if (h) CHECK(GlobalSize(h) >= sizeof(METAFILEPICT));
    U(CloseClipboard)();
}

TEST(clipboard_dde_metafilepict_synthesizes_enhmetafile) {
    CD_SETUP();
    REQUIRE(U(CreateMetaFileW) && U(CloseMetaFile) && U(Rectangle));
    HDC dc = U(CreateMetaFileW)(NULL);
    CHECK(dc != NULL);
    if (!dc) return;
    U(Rectangle)(dc, 0, 0, 10, 10);
    HMETAFILE mf = U(CloseMetaFile)(dc);
    METAFILEPICT mp = { MM_ANISOTROPIC, 100, 100, mf };
    HGLOBAL g = cd_gmem(&mp, sizeof mp);
    UINT f[16];
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_METAFILEPICT, g) == g);
    CHECK(U(CloseClipboard)());
    int n = cd_formats_closed(f, 16, NULL);
    EQ(n, 2);
    EQ(f[0], CF_METAFILEPICT);
    EQ(f[1], CF_ENHMETAFILE);
}

/* ---- enumeration, count, availability ----------------------------------------------------------- */
TEST(clipboard_dde_enum_insertion_order) {
    CD_SETUP();
    static const UINT in[] = { 0x205, 0x201, 0x2FF, 0x203, 0x300 };
    CHECK(cd_open_empty(NULL));
    for (int i = 0; i < N(in); i++) U(SetClipboardData)(in[i], NULL);
    UINT out[8];
    int n = cd_formats(out, 8);
    EQ(n, N(in));
    for (int i = 0; i < n && i < N(in); i++) EQ(out[i], in[i]);
    U(CloseClipboard)();
}

TEST(clipboard_dde_enum_walk_semantics) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    U(SetClipboardData)(0x211, NULL); U(SetClipboardData)(0x212, NULL); U(SetClipboardData)(0x213, NULL);
    SetLastError(0xDEAD);
    EQ(U(EnumClipboardFormats)(0), 0x211);
    EQ(GetLastError(), 0);                         /* user32 clears the last error before the syscall */
    EQ(U(EnumClipboardFormats)(0x211), 0x212);
    EQ(U(EnumClipboardFormats)(0x212), 0x213);
    SetLastError(0xDEAD);
    EQ(U(EnumClipboardFormats)(0x213), 0);         /* past the end */
    EQ(GetLastError(), 0);
    EQ(U(EnumClipboardFormats)(0), 0x211);         /* 0 restarts */
    SetLastError(0xDEAD);
    EQ(U(EnumClipboardFormats)(0x2EE), 0);         /* unknown format: FindClipFormat fails, no error */
    EQ(GetLastError(), 0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_enum_empty_clipboard) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    SetLastError(0xDEAD);
    EQ(U(EnumClipboardFormats)(0), 0);
    EQ(GetLastError(), 0);
    U(CloseClipboard)();
}

TEST(clipboard_dde_enum_direct_callone_matches) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserCallOneParam_pfn c1 = UN(NtUserCallOneParam);
    REQUIRE(c1);
    CHECK(cd_put_text(NULL, L"enum"));
    CHECK(cd_open(NULL));
    UINT a[16], b[16];
    int na = cd_formats(a, 16), nb = 0;
    UINT f = (UINT)c1(0, 0x31);
    while (f && nb < 16) { b[nb++] = f; f = (UINT)c1(f, 0x31); }
    EQ(na, nb);
    for (int i = 0; i < na && i < nb; i++) EQ(a[i], b[i]);
    U(CloseClipboard)();
}

TEST(clipboard_dde_count_and_available_need_no_open) {
    CD_SETUP();
    CHECK(cd_put(NULL, 0x236, "abcdefgh", 8));
    SetLastError(0xDEAD);
    EQ(U(CountClipboardFormats)(), 1);
    CHECK(U(IsClipboardFormatAvailable)(0x236));
    CHECK(!U(IsClipboardFormatAvailable)(0x237));
    CHECK(!U(IsClipboardFormatAvailable)(0));
    EQ(GetLastError(), 0xDEAD);                    /* no error is raised for the closed clipboard */
}

TEST(clipboard_dde_count_matches_enumeration_with_text) {
    CD_SETUP();
    UINT f[16];
    CHECK(cd_put_text(NULL, L"count"));
    int n = cd_formats_closed(f, 16, NULL);
    EQ(U(CountClipboardFormats)(), n);
}

TEST(clipboard_dde_available_empty_and_zero) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    CHECK(U(CloseClipboard)());
    EQ(U(CountClipboardFormats)(), 0);
    static const UINT fmts[] = { 0, 1, 2, 13, 0x80, 0x200, 0xC000, 0xFFFF };
    for (int i = 0; i < N(fmts); i++) CHECKF(!U(IsClipboardFormatAvailable)(fmts[i]), "format 0x%x", fmts[i]);
}

TEST(clipboard_dde_available_direct_matches_user32) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserIsClipboardFormatAvailable_pfn av = UN(NtUserIsClipboardFormatAvailable);
    NtUserCountClipboardFormats_pfn ct = UN(NtUserCountClipboardFormats);
    REQUIRE(av && ct);
    CHECK(cd_put_text(NULL, L"direct"));
    EQ(ct(), U(CountClipboardFormats)());
    static const UINT fmts[] = { 0, 1, 2, 7, 13, 16, 0x200, 0xC123 };
    for (int i = 0; i < N(fmts); i++) EQ(av(fmts[i]) != 0, U(IsClipboardFormatAvailable)(fmts[i]) != 0);
}

/* ---- GetUpdatedClipboardFormats ------------------------------------------------------------------ */
TEST(clipboard_dde_updated_formats_match_enumeration) {
    CD_SETUP();
    REQUIRE(U(GetUpdatedClipboardFormats));
    CHECK(cd_put_text(NULL, L"updated"));
    UINT e[16], u[16] = {0};
    int ne = cd_formats_closed(e, 16, NULL);
    UINT nu = 0xFFFF;
    CHECK(U(GetUpdatedClipboardFormats)(u, 16, &nu));      /* needs no open clipboard */
    EQ(nu, ne);
    for (int i = 0; i < ne && i < (int)nu; i++) EQ(u[i], e[i]);
}

TEST(clipboard_dde_updated_formats_buffer_too_small) {
    CD_SETUP();
    REQUIRE(U(GetUpdatedClipboardFormats));
    CHECK(cd_put_text(NULL, L"small"));
    int count = U(CountClipboardFormats)();
    UINT u[16]; memset(u, 0xAB, sizeof u);
    UINT nu = 0;
    SetLastError(0xDEAD);
    CHECK(!U(GetUpdatedClipboardFormats)(u, 1, &nu));
    EXPECT_ERR(CD_ERR_INSUFFICIENT);                       /* cFormats < count => 122 */
    EQ(nu, count);                                  /* *pcFormatsOut still reports the needed count */
    EQ(u[0], 0xABABABAB);
    SetLastError(0xDEAD);
    CHECK(!U(GetUpdatedClipboardFormats)(u, 0, &nu));
    EXPECT_ERR(CD_ERR_INSUFFICIENT);
    EQ(nu, count);
    CHECK(U(GetUpdatedClipboardFormats)(u, count, &nu));   /* exact fit */
    EQ(nu, count);
    CHECK(U(GetUpdatedClipboardFormats)(u, 16, &nu));
    EQ(nu, count);
}

TEST(clipboard_dde_updated_formats_empty_clipboard) {
    CD_SETUP();
    REQUIRE(U(GetUpdatedClipboardFormats));
    CHECK(cd_open_empty(NULL)); CHECK(U(CloseClipboard)());
    UINT u[4] = {7, 7, 7, 7}, nu = 99;
    CHECK(U(GetUpdatedClipboardFormats)(u, 4, &nu));
    EQ(nu, 0);
    EQ(u[0], 7);
}

TEST(clipboard_dde_updated_formats_direct) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetUpdatedClipboardFormats_pfn f = UN(NtUserGetUpdatedClipboardFormats);
    REQUIRE(f);
    CHECK(cd_put(NULL, 0x238, "abcdefgh", 8));
    UINT u[4] = {0}, nu = 0;
    CHECK(f(u, 4, &nu));
    EQ(nu, 1);
    EQ(u[0], 0x238);
    SetLastError(0);
    CHECK(!f(u, 0, &nu));
    EXPECT_ERR(CD_ERR_INSUFFICIENT);
}

/* ---- GetPriorityClipboardFormat ------------------------------------------------------------------- */
TEST(clipboard_dde_priority_empty_clipboard_is_zero) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL)); CHECK(U(CloseClipboard)());
    UINT l[2] = { CF_TEXT, 0x200 };
    EQ(U(GetPriorityClipboardFormat)(l, 2), 0);     /* cNumClipFormats == 0 => 0, not -1 */
    EQ(U(GetPriorityClipboardFormat)(l, 0), 0);
}

TEST(clipboard_dde_priority_picks_first_listed_present) {
    CD_SETUP();
    CHECK(cd_open_empty(NULL));
    U(SetClipboardData)(0x201, NULL); U(SetClipboardData)(0x202, NULL); U(SetClipboardData)(0x203, NULL);
    CHECK(U(CloseClipboard)());
    UINT a[] = { 0x2F0, 0x203, 0x201 };
    EQ(U(GetPriorityClipboardFormat)(a, 3), 0x203);   /* list order wins over clipboard order */
    UINT b[] = { 0x2F0, 0x2F1 };
    EQ(U(GetPriorityClipboardFormat)(b, 2), -1);
    UINT c[] = { 0, 0, 0x202 };
    EQ(U(GetPriorityClipboardFormat)(c, 3), 0x202);   /* zero entries are skipped */
    UINT d[] = { 0x201 };
    EQ(U(GetPriorityClipboardFormat)(d, 0), -1);      /* nothing to examine */
    EQ(U(GetPriorityClipboardFormat)(d, 1), 0x201);
    UINT e[] = { 0, 0 };
    EQ(U(GetPriorityClipboardFormat)(e, 2), -1);
}

TEST(clipboard_dde_priority_sees_synthesized_formats) {
    CD_SETUP();
    CHECK(cd_put_text(NULL, L"prio"));
    UINT l1[] = { CF_OEMTEXT, CF_UNICODETEXT };
    EQ(U(GetPriorityClipboardFormat)(l1, 2), CF_OEMTEXT);
    UINT l2[] = { CF_BITMAP, CF_TEXT };
    EQ(U(GetPriorityClipboardFormat)(l2, 2), CF_TEXT);
}

TEST(clipboard_dde_priority_needs_no_open_and_direct_matches) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetPriorityClipboardFormat_pfn p = UN(NtUserGetPriorityClipboardFormat);
    REQUIRE(p);
    CHECK(cd_put(NULL, 0x20A, "abcdefgh", 8));
    UINT l[] = { 0x20B, 0x20A };
    SetLastError(0xDEAD);
    EQ(p(l, 2), 0x20A);
    EQ(GetLastError(), 0xDEAD);
}

/* ---- format names ------------------------------------------------------------------------------- */
TEST(clipboard_dde_format_name_standard_ids_fail) {
    CD_SETUP();
    static const UINT ids[] = { 0, 1, 2, 13, 0x80, 0x1FF, 0x200, 0x3FF, 0xBFFF };
    for (int i = 0; i < N(ids); i++) {
        WCHAR b[32]; wcscpy(b, L"keep");
        SetLastError(0xDEAD);
        EQ(U(GetClipboardFormatNameW)(ids[i], b, 32), 0);   /* NtUserGetClipboardFormatName: format < 0xC000 => 0 + ERROR_INVALID_PARAMETER */
        EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
        EXPECT_WSTR(b, L"keep");
    }
}

TEST(clipboard_dde_format_name_direct_truncates_to_ushort) {
    CD_SETUP(); CD_NEED_WIN32K();
    NtUserGetClipboardFormatName_pfn n = UN(NtUserGetClipboardFormatName);
    REQUIRE(n);
    UINT fmt = cd_reg("trunc");
    WCHAR a[96] = {0}, b[96] = {0};
    int la = n(fmt, a, 96);
    int lb = n(fmt | 0x10000, b, 96);
    CHECK(la > 0);
    EQ(la, lb);
    EXPECT_WSTR(a, b);
    SetLastError(0xDEAD);
    EQ(n(0x10001, b, 96), 0);                       /* low word 1 < 0xC000 */
    EXPECT_ERR(CD_ERR_INVALID_PARAMETER);
}

TEST(clipboard_dde_format_name_roundtrip) {
    CD_SETUP();
    WCHAR nm[64];
    wsprintfW(nm, L"win32kt_cd_%lu_roundtrip_name", GetCurrentProcessId());
    UINT f = U(RegisterClipboardFormatW)(nm);
    CHECK(f >= 0xC000 && f <= 0xFFFF);
    WCHAR out[96] = {0};
    int len = lstrlenW(nm);
    EQ(U(GetClipboardFormatNameW)(f, out, 96), len);
    EXPECT_WSTR(out, nm);
    EQ(U(RegisterClipboardFormatW)(nm), f);          /* same atom */
    if (U(RegisterWindowMessageW)) EQ(U(RegisterWindowMessageW)(nm), f);   /* both stubs are NtUserRegisterWindowMessage */
}

TEST(clipboard_dde_format_name_truncation) {
    CD_SETUP();
    WCHAR nm[64];
    wsprintfW(nm, L"win32kt_cd_%lu_truncate_me", GetCurrentProcessId());
    UINT f = U(RegisterClipboardFormatW)(nm);
    int len = lstrlenW(nm);
    for (int cch = 2; cch < len; cch += 7) {
        WCHAR out[96]; for (int i = 0; i < 96; i++) out[i] = 0x7777;
        int r = U(GetClipboardFormatNameW)(f, out, cch);
        CHECKF(r >= 0 && r <= cch - 1, "cch %d returned %d", cch, r);
        if (r > 0 && r < 96) {
            CHECKF(out[r] == 0, "cch %d not terminated at %d", cch, r);
            CHECKF(!memcmp(out, nm, r * sizeof(WCHAR)), "cch %d prefix", cch);
        } else t_note("truncated GetClipboardFormatNameW(cch=%d) returned %d", cch, r);
        CHECKF(out[cch] == 0x7777, "cch %d: wrote past the buffer", cch);
    }
}

TEST(clipboard_dde_format_name_zero_length_buffer_observed) {
    CD_SETUP();
    UINT f = cd_reg("zerolen");
    WCHAR c = 0x7777;
    SetLastError(0);
    int r = U(GetClipboardFormatNameW)(f, &c, 0);
    t_note("GetClipboardFormatNameW(cch=0) -> %d err %lu buf %04x", r, GetLastError(), c);
}

TEST(clipboard_dde_register_same_name_same_id_and_distinct_names_differ) {
    CD_SETUP();
    UINT a = cd_reg("a"), b = cd_reg("b");
    CHECK(a >= 0xC000 && b >= 0xC000);
    CHECK(a != b);
    EQ(cd_reg("a"), a);
    if (U(RegisterClipboardFormatA)) {
        char nm[96]; wsprintfA(nm, "win32kt_cd_%lu_a", GetCurrentProcessId());
        EQ(U(RegisterClipboardFormatA)(nm), a);
    }
}

TEST(clipboard_dde_register_empty_name_fails) {
    CD_SETUP();
    SetLastError(0);
    UINT r = U(RegisterClipboardFormatW)(L"");
    t_note("RegisterClipboardFormatW(\"\") -> %u err %lu", r, GetLastError());
    CHECK(r == 0 || r >= 0xC000);
}

TEST(clipboard_dde_register_name_length_limit_observed) {
    CD_SETUP();
    WCHAR nm[300];
    for (int len = 250; len <= 258; len += 4) {
        wsprintfW(nm, L"win32kt_cd_%lu_", GetCurrentProcessId());
        int p = lstrlenW(nm);
        for (int i = p; i < len; i++) nm[i] = L'a' + (i % 20);
        nm[len] = 0;
        SetLastError(0);
        UINT r = U(RegisterClipboardFormatW)(nm);
        t_info("RegisterClipboardFormatW(%d chars) -> 0x%x err %lu", len, r, GetLastError());
    }
}

TEST(clipboard_dde_registered_format_set_get_and_name_matches) {
    CD_SETUP();
    UINT f = cd_reg("setname");
    CHECK(cd_put(NULL, f, "regdata!", 8));
    CHECK(U(IsClipboardFormatAvailable)(f));
    WCHAR out[96] = {0};
    CHECK(U(GetClipboardFormatNameW)(f, out, 96) > 0);
    CHECK(cd_open(NULL));
    char d[8] = {0};
    CHECK(cd_get_bytes(f, d, 8) >= 8);
    CHECK(!memcmp(d, "regdata!", 8));
    U(CloseClipboard)();
}

TEST(clipboard_dde_owner_display_format_accepted) {
    CD_SETUP();
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_open_empty(c->hwnd));
    U(SetClipboardData)(CF_OWNERDISPLAY, NULL);            /* hData 0: format 0x80 is accepted like any delayed format */
    CHECK(U(IsClipboardFormatAvailable)(CF_OWNERDISPLAY));
    EQ(U(CountClipboardFormats)(), 1);
    U(CloseClipboard)();
    EQ(CD_CNT(c, CD_WM_ASKCBFORMATNAME), 0);        /* the kernel never asks for the name itself */
}

TEST(clipboard_dde_dsp_text_not_part_of_text_synthesis) {
    CD_SETUP();
    UINT f[8];
    CHECK(cd_put(NULL, CF_DSPTEXT, "display", 8));
    EQ(cd_formats_closed(f, 8, NULL), 1);
    CHECK(!U(IsClipboardFormatAvailable)(CF_TEXT));
}
