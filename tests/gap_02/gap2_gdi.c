/* gap_02 GDI group: NtGdiUpdateTransform, NtGdiQueryFonts, NtGdiGetCharWidthW, NtGdiGetCharABCWidthsW,
 * NtGdiStartDoc, NtGdiResetDC, NtGdiDrawStream, NtUserBitBltSysBmp, NtUserGetOemBitmapSize.
 *
 * Analysis facts (kernel behaviour):
 *  - NtGdiUpdateTransform(hdc): XDCOBJ::vLock; NULL lock -> return 0, otherwise vUpdateWtoDXform and return 1.
 *  - NtGdiGetCharWidthW(hdc, first, cwc, pwc, fl, out): cwc==0 -> 0; copies cwc*4 bytes out only if the width query
 *    succeeded; temp buffer caps: cwc >= 0x9c4001 -> no buffer -> 0; with pwc != NULL cwc >= 0x1388001 -> 0.
 *  - NtGdiGetCharABCWidthsW: out==NULL -> 0; copies cwc*12 bytes on success; cwc >= 0x341556 -> 0.
 *  - NtGdiQueryFonts(buf, n, &ts): n > 0x4e2000 with a buffer -> -1 and ERROR_NOT_ENOUGH_MEMORY (8).
 *  - NtGdiResetDC: an unlockable hdc -> EngSetLastError(6) and 0.
 *  - NtGdiDrawStream: stream < 4 bytes or wrong magic ('SwrD' = 0x44727753) -> 0.
 *  - NtUserGetOemBitmapSize(id, out): id >= 0x5d -> ERROR_INVALID_PARAMETER and 0 (out untouched); else out = {w,h} is
 *    always stored and the result is (w != 0 && h != 0).
 */
#include "gap2.h"
#include <string.h>

static HFONT mkfont(const wchar_t *face) {
    return U(CreateFontW)(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
}
/* memory DC with a selected font; caller deletes with gdi_dc_free */
struct fx { HDC dc; HFONT f; HGDIOBJ old; };
static int fx_open(struct fx *x, const wchar_t *face) {
    memset(x, 0, sizeof *x);
    if (!U(CreateCompatibleDC) || !U(CreateFontW) || !U(SelectObject)) return 0;
    x->dc = U(CreateCompatibleDC)(NULL);
    if (!x->dc) return 0;
    x->f = mkfont(face);
    if (x->f) x->old = U(SelectObject)(x->dc, x->f);
    return 1;
}
static void fx_close(struct fx *x) {
    if (x->old) U(SelectObject)(x->dc, x->old);
    if (x->f) U(DeleteObject)(x->f);
    if (x->dc) U(DeleteDC)(x->dc);
}

/* ---------- UpdateTransform ---------- */
TEST(gap2_updxf_null_and_junk_handles) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f);
    EXPECT_EQ(f(NULL), 0);
    EXPECT_EQ(f((HDC)(ULONG_PTR)0x7ffe1234), 0);
    EXPECT_EQ(f((HDC)(ULONG_PTR)0xffffffff), 0);
    EXPECT_EQ(f((HDC)(ULONG_PTR)1), 0);
}
TEST(gap2_updxf_dc_kinds_succeed) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(GetDC));
    HDC mem = U(CreateCompatibleDC)(NULL);
    CHECK(mem != NULL);
    if (mem) { EXPECT_EQ(f(mem), 1); U(DeleteDC)(mem); }
    HDC scr = U(GetDC)(NULL);
    if (scr) { EXPECT_EQ(f(scr), 1); U(ReleaseDC)(NULL, scr); }
    HWND h = gap2_top();
    if (h) {
        HDC wdc = U(GetDC)(h);
        if (wdc) { EXPECT_EQ(f(wdc), 1); U(ReleaseDC)(h, wdc); }
        gap2_destroy(h);
    }
    if (U(CreateICW)) {
        HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
        if (ic) { EXPECT_EQ(f(ic), 1); U(DeleteDC)(ic); }
    }
}
TEST(gap2_updxf_deleted_dc_fails) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC mem = U(CreateCompatibleDC)(NULL); if (!mem) { t_skip("no DC"); return; }
    EXPECT_EQ(f(mem), 1);
    U(DeleteDC)(mem);
    EXPECT_EQ(f(mem), 0);
}
TEST(gap2_updxf_stale_uniqueness_bits) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC mem = U(CreateCompatibleDC)(NULL); if (!mem) { t_skip("no DC"); return; }
    /* same table index, different uniqueness word: vLock rejects it */
    HDC forged = (HDC)((ULONG_PTR)mem ^ 0x00400000);
    EXPECT_EQ(f(forged), 0);
    EXPECT_EQ(f(mem), 1);
    U(DeleteDC)(mem);
}
TEST(gap2_updxf_non_dc_gdi_objects) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f); REQUIRE(U(GetStockObject));
    int stock[] = { WHITE_BRUSH, BLACK_PEN, SYSTEM_FONT, DEFAULT_PALETTE, NULL_BRUSH };
    for (size_t i = 0; i < sizeof stock / sizeof *stock; i++)
        EXPECT_EQ(f((HDC)U(GetStockObject)(stock[i])), 0);   /* object type mismatch in vLock */
    if (U(CreateRectRgn)) { HRGN r = U(CreateRectRgn)(0, 0, 4, 4); if (r) { EXPECT_EQ(f((HDC)r), 0); U(DeleteObject)(r); } }
    if (U(CreateBitmap)) { HBITMAP b = U(CreateBitmap)(4, 4, 1, 1, NULL); if (b) { EXPECT_EQ(f((HDC)b), 0); U(DeleteObject)(b); } }
}
TEST(gap2_updxf_preserves_world_transform) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    REQUIRE(U(SetGraphicsMode)); REQUIRE(U(SetWorldTransform)); REQUIRE(U(GetWorldTransform));
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }
    if (!U(SetGraphicsMode)(dc, GM_ADVANCED)) { U(DeleteDC)(dc); t_skip("GM_ADVANCED unavailable"); return; }
    XFORM x = { 2.0f, 0.5f, -0.5f, 3.0f, 17.0f, -9.0f }, g;
    CHECK(U(SetWorldTransform)(dc, &x));
    memset(&g, 0, sizeof g);
    CHECK(U(GetWorldTransform)(dc, &g));
    EXPECT_EQ(f(dc), 1);
    XFORM g2; memset(&g2, 0, sizeof g2);
    CHECK(U(GetWorldTransform)(dc, &g2));
    /* the call only recomputes the cached world->device matrix from the same inputs */
    CHECK(!memcmp(&g, &g2, sizeof g));
    CHECK(g2.eM11 == 2.0f && g2.eM22 == 3.0f && g2.eDx == 17.0f);
    U(DeleteDC)(dc);
}
TEST(gap2_updxf_preserves_mapping) {
    NtGdiUpdateTransform_pfn f = GN(NtGdiUpdateTransform); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    REQUIRE(U(SetMapMode)); REQUIRE(U(SetWindowExtEx)); REQUIRE(U(SetViewportExtEx)); REQUIRE(U(DPtoLP)); REQUIRE(U(LPtoDP));
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 50, NULL);
    U(SetViewportExtEx)(dc, 300, 200, NULL);
    POINT a[3] = { {0, 0}, {10, 10}, {-7, 33} }, b[3];
    memcpy(b, a, sizeof a);
    U(LPtoDP)(dc, a, 3);
    EXPECT_EQ(f(dc), 1);
    EXPECT_EQ(f(dc), 1);
    U(LPtoDP)(dc, b, 3);
    for (int i = 0; i < 3; i++) { EXPECT_EQ(a[i].x, b[i].x); EXPECT_EQ(a[i].y, b[i].y); }
    EXPECT_EQ(a[1].x, 30);
    EXPECT_EQ(a[1].y, 40);
    U(DeleteDC)(dc);
}

/* ---------- QueryFonts ---------- */
TEST(gap2_queryfonts_count_and_timestamp) {
    NtGdiQueryFonts_pfn f = GN(NtGdiQueryFonts); REQUIRE(f);
    LONGLONG ts = 0x1122334455667788LL;
    ULONG n = f(NULL, 0, &ts);
    CHECK(n != 0xffffffff);
    CHECK(ts != 0x1122334455667788LL);   /* the kernel stores the font-table timestamp for every non-error result */
    t_note("public font count %lu, timestamp %llx", (unsigned long)n, (unsigned long long)ts);
    LONGLONG ts2 = 0; ULONG n2 = f(NULL, 0, &ts2);
    EXPECT_EQ(n2, n);
    EXPECT_EQ(ts2, ts);
}
TEST(gap2_queryfonts_zero_count_with_buffer_fails) {
    NtGdiQueryFonts_pfn f = GN(NtGdiQueryFonts); REQUIRE(f);
    LONGLONG ts = 0; ULONG n0 = f(NULL, 0, &ts);
    unsigned char buf[64]; memset(buf, 0xA5, sizeof buf);
    LONGLONG ts2 = 0;
    SET_ERR(0);
    ULONG n = f(buf, 0, &ts2);   /* a buffer with a zero count is refused: -1 with ERROR_NOT_ENOUGH_MEMORY, nothing copied */
    EXPECT_EQ(n, 0xffffffff);
    EXPECT_ERR(ERROR_NOT_ENOUGH_MEMORY);
    CHECK(n0 != 0xffffffff);
    for (size_t i = 0; i < sizeof buf; i++) if (buf[i] != 0xA5) { CHECKF(0, "buffer byte %zu modified", i); break; }
}
TEST(gap2_queryfonts_fills_at_most_requested) {
    NtGdiQueryFonts_pfn f = GN(NtGdiQueryFonts); REQUIRE(f);
    LONGLONG ts = 0; ULONG n = f(NULL, 0, &ts);
    if (n == 0 || n == 0xffffffff) { t_skip("no public fonts"); return; }
    enum { CAP = 8 };
    ULONG want = n < CAP ? n : CAP;
    struct { ULONG a[2 * (CAP + 4)]; } b;
    memset(&b, 0xCD, sizeof b);
    LONGLONG ts2 = 0;
    ULONG r = f(b.a, want, &ts2);
    EXPECT_EQ(r, want);   /* with a buffer the return value is the number of entries copied; only the NULL-buffer query returns the total */
    int canary_ok = 1;
    for (ULONG i = want * 2; i < 2 * (CAP + 4); i++) if (b.a[i] != 0xCDCDCDCD) canary_ok = 0;
    CHECK(canary_ok);   /* memcpy copies min(count, n) * 8 bytes only */
    int touched = 0;
    for (ULONG i = 0; i < want * 2; i++) if (b.a[i] != 0xCDCDCDCD) touched = 1;
    CHECK(touched);
}
TEST(gap2_queryfonts_entries_are_stable) {
    NtGdiQueryFonts_pfn f = GN(NtGdiQueryFonts); REQUIRE(f);
    LONGLONG ts = 0; ULONG n = f(NULL, 0, &ts);
    if (n == 0 || n == 0xffffffff || n > 4096) { t_skip("font count unsuitable"); return; }
    ULONG *a = HeapAlloc(GetProcessHeap(), 0, n * 8), *b = HeapAlloc(GetProcessHeap(), 0, n * 8);
    if (!a || !b) { t_skip("alloc"); return; }
    LONGLONG t1 = 0, t2 = 0;
    EXPECT_EQ(f(a, n, &t1), n);
    EXPECT_EQ(f(b, n, &t2), n);
    CHECK(!memcmp(a, b, n * 8));
    EXPECT_EQ(t1, t2);
    HeapFree(GetProcessHeap(), 0, a); HeapFree(GetProcessHeap(), 0, b);
}
TEST(gap2_queryfonts_oversize_request_fails) {
    NtGdiQueryFonts_pfn f = GN(NtGdiQueryFonts); REQUIRE(f);
    ULONG tmp[4] = { 0 }; LONGLONG ts = 0x55;
    SET_ERR(0);
    EXPECT_EQ(f(tmp, 0x4e2001, &ts), 0xffffffff);   /* no temp buffer allocated for n >= 0x4e2001 -> -1 */
    EXPECT_ERR(ERROR_NOT_ENOUGH_MEMORY);
    EXPECT_EQ(ts, 0x55);   /* the timestamp store sits behind the success check */
    EXPECT_EQ(tmp[0], 0);
}

/* ---------- GetCharWidthW ---------- */
TEST(gap2_charwidth_range_basic) {
    NtGdiGetCharWidthW_pfn f = GN(NtGdiGetCharWidthW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    int w[3] = { -1, -1, -1 };
    BOOL r = f(x.dc, 'A', 3, NULL, 3, w);
    CHECK(r);
    for (int i = 0; i < 3; i++) CHECKF(w[i] > 0, "width[%d]=%d", i, w[i]);
    int w2[3] = { 0 };
    CHECK(f(x.dc, 'A', 3, NULL, 3, w2));
    CHECK(!memcmp(w, w2, sizeof w));   /* deterministic for an unchanged DC/font */
    if (U(GetCharWidthW)) { int g[3] = { 0 }; if (U(GetCharWidthW)(x.dc, 'A', 'C', g)) { for (int i = 0; i < 3; i++) if (g[i] != w[i]) t_note("user path width[%d]=%d syscall=%d", i, g[i], w[i]); } }
    fx_close(&x);
}
TEST(gap2_charwidth_explicit_array_matches_range) {
    NtGdiGetCharWidthW_pfn f = GN(NtGdiGetCharWidthW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    int r3[3] = { 0 };
    if (!f(x.dc, 'A', 3, NULL, 3, r3)) { fx_close(&x); t_skip("range query failed"); return; }
    WCHAR pwc[3] = { 'C', 'A', 'B' };
    int a3[3] = { 0 };
    CHECK(f(x.dc, 0, 3, pwc, 3, a3));
    EXPECT_EQ(a3[0], r3[2]);
    EXPECT_EQ(a3[1], r3[0]);
    EXPECT_EQ(a3[2], r3[1]);
    fx_close(&x);
}
TEST(gap2_charwidth_zero_count) {
    NtGdiGetCharWidthW_pfn f = GN(NtGdiGetCharWidthW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    int w[2] = { 0x1234, 0x5678 };
    EXPECT_EQ(f(x.dc, 'A', 0, NULL, 3, w), 0);
    EXPECT_EQ(w[0], 0x1234);
    EXPECT_EQ(w[1], 0x5678);
    fx_close(&x);
}
TEST(gap2_charwidth_bad_dc_leaves_output) {
    NtGdiGetCharWidthW_pfn f = GN(NtGdiGetCharWidthW); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC dead = U(CreateCompatibleDC)(NULL); if (dead) U(DeleteDC)(dead);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x7ffe1234, dead, (HDC)U(GetStockObject)(SYSTEM_FONT) };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        int w[2] = { 0x1111, 0x2222 };
        EXPECT_EQ(f(bad[i], 'A', 2, NULL, 3, w), 0);   /* the width query fails -> nothing copied out */
        EXPECT_EQ(w[0], 0x1111);
        EXPECT_EQ(w[1], 0x2222);
    }
}
TEST(gap2_charwidth_oversize_counts_rejected) {
    NtGdiGetCharWidthW_pfn f = GN(NtGdiGetCharWidthW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    int w[2] = { 7, 8 };
    EXPECT_EQ(f(x.dc, 0, 0x9c4001, NULL, 3, w), 0);   /* temp buffer only below 0x9c4001 chars */
    WCHAR pwc[2] = { 'A', 'B' };
    EXPECT_EQ(f(x.dc, 0, 0x1388001, pwc, 3, w), 0);   /* char array copy-in capped at 0x1388000 */
    EXPECT_EQ(w[0], 7);
    EXPECT_EQ(w[1], 8);
    fx_close(&x);
}
TEST(gap2_charwidth_wide_range_edges) {
    NtGdiGetCharWidthW_pfn f = GN(NtGdiGetCharWidthW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    int w[256]; memset(w, 0xFF, sizeof w);
    BOOL r = f(x.dc, 0x20, 96, NULL, 3, w);   /* printable ASCII */
    CHECK(r);
    if (r) { int pos = 0; for (int i = 0; i < 96; i++) if (w[i] > 0) pos++; CHECKF(pos >= 90, "positive widths %d", pos); EXPECT_EQ(w[0] > 0, 1); }
    fx_close(&x);
}

/* ---------- GetCharABCWidthsW ---------- */
TEST(gap2_charabc_null_buffer) {
    NtGdiGetCharABCWidthsW_pfn f = GN(NtGdiGetCharABCWidthsW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    EXPECT_EQ(f(x.dc, 'A', 3, NULL, 1, NULL), 0);
    EXPECT_EQ(f(NULL, 'A', 3, NULL, 1, NULL), 0);
    fx_close(&x);
}
TEST(gap2_charabc_truetype_values) {
    NtGdiGetCharABCWidthsW_pfn f = GN(NtGdiGetCharABCWidthsW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    ABC abc[3]; memset(abc, 0, sizeof abc);
    BOOL r = f(x.dc, 'A', 3, NULL, 1, abc);
    if (!r) { t_note("GetCharABCWidths failed (font not TrueType here)"); fx_close(&x); return; }
    for (int i = 0; i < 3; i++) CHECKF(abc[i].abcB > 0, "abcB[%d]=%u", i, abc[i].abcB);
    ABC abc2[3]; memset(abc2, 0, sizeof abc2);
    CHECK(f(x.dc, 'A', 3, NULL, 1, abc2));
    CHECK(!memcmp(abc, abc2, sizeof abc));
    if (U(GetCharABCWidthsW)) { ABC g[3]; memset(g, 0, sizeof g); if (U(GetCharABCWidthsW)(x.dc, 'A', 'C', g)) CHECK(!memcmp(g, abc, sizeof g)); }
    fx_close(&x);
}
TEST(gap2_charabc_array_matches_range) {
    NtGdiGetCharABCWidthsW_pfn f = GN(NtGdiGetCharABCWidthsW); REQUIRE(f);
    struct fx x; if (!fx_open(&x, L"Arial")) { t_skip("no DC"); return; }
    ABC r3[3]; memset(r3, 0, sizeof r3);
    if (!f(x.dc, 'A', 3, NULL, 1, r3)) { fx_close(&x); t_skip("ABC query failed"); return; }
    WCHAR pwc[3] = { 'B', 'C', 'A' };
    ABC a3[3]; memset(a3, 0, sizeof a3);
    CHECK(f(x.dc, 0, 3, pwc, 1, a3));
    CHECK(!memcmp(&a3[0], &r3[1], sizeof(ABC)));
    CHECK(!memcmp(&a3[1], &r3[2], sizeof(ABC)));
    CHECK(!memcmp(&a3[2], &r3[0], sizeof(ABC)));
    fx_close(&x);
}
TEST(gap2_charabc_bad_dc_and_oversize) {
    NtGdiGetCharABCWidthsW_pfn f = GN(NtGdiGetCharABCWidthsW); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC dead = U(CreateCompatibleDC)(NULL); if (dead) U(DeleteDC)(dead);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x7ffe1234, dead };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        ABC abc[2]; memset(abc, 0x5A, sizeof abc);
        EXPECT_EQ(f(bad[i], 'A', 2, NULL, 1, abc), 0);
        EXPECT_EQ(((unsigned char *)abc)[0], 0x5A);
        EXPECT_EQ(((unsigned char *)abc)[sizeof abc - 1], 0x5A);
    }
    struct fx x; if (!fx_open(&x, L"Arial")) return;
    ABC one; memset(&one, 0x5A, sizeof one);
    EXPECT_EQ(f(x.dc, 0, 0x341556, NULL, 1, &one), 0);   /* cwc*12 temp buffer capped below 0x341556 */
    EXPECT_EQ(((unsigned char *)&one)[0], 0x5A);
    fx_close(&x);
}

/* ---------- StartDoc / ResetDC / DrawStream: invalid-handle and capture paths ---------- */
TEST(gap2_startdoc_invalid_dc_returns_zero) {
    NtGdiStartDoc_pfn f = GN(NtGdiStartDoc); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    WCHAR name[] = L"win32kt gap2 doc";
    DOCINFOW di; memset(&di, 0, sizeof di);
    di.cbSize = sizeof di; di.lpszDocName = name;
    HDC dead = U(CreateCompatibleDC)(NULL); if (dead) U(DeleteDC)(dead);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x7ffe1234, dead };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        int banding = 0x4242;
        EXPECT_EQ(f(bad[i], &di, &banding, 0), 0);
        EXPECT_EQ(banding, 0x4242);   /* the store follows a non-zero job id only */
    }
}
TEST(gap2_startdoc_null_docinfo_invalid_dc) {
    NtGdiStartDoc_pfn f = GN(NtGdiStartDoc); REQUIRE(f);
    int banding = 0x4242;
    EXPECT_EQ(f(NULL, NULL, &banding, 0), 0);
    EXPECT_EQ(banding, 0x4242);
}
TEST(gap2_startdoc_all_strings_captured_invalid_dc) {
    NtGdiStartDoc_pfn f = GN(NtGdiStartDoc); REQUIRE(f);
    WCHAR n[] = L"doc", o[] = L"nul:", d[] = L"RAW";
    DOCINFOW di; memset(&di, 0, sizeof di);
    di.cbSize = sizeof di; di.lpszDocName = n; di.lpszOutput = o; di.lpszDatatype = d;
    int banding = 9;
    EXPECT_EQ(f(NULL, &di, &banding, 0), 0);   /* name/output/datatype all copied (0x27 < cb), then the DC lookup fails */
    EXPECT_EQ(banding, 9);
    di.cbSize = 0x18;   /* too small to carry the datatype pointer: it is not read */
    EXPECT_EQ(f(NULL, &di, &banding, 0), 0);
    EXPECT_EQ(banding, 9);
}
TEST(gap2_resetdc_invalid_dc_sets_invalid_handle) {
    NtGdiResetDC_pfn f = GN(NtGdiResetDC); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC dead = U(CreateCompatibleDC)(NULL); if (dead) U(DeleteDC)(dead);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x7ffe1234, dead };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        int banding = 0x77;
        SET_ERR(0);
        EXPECT_EQ(f(bad[i], NULL, &banding, NULL, NULL), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);   /* DC lookup failed -> EngSetLastError(6) */
        EXPECT_EQ(banding, 0x77);
    }
}
TEST(gap2_resetdc_memory_dc_is_not_destroyed) {
    NtGdiResetDC_pfn f = GN(NtGdiResetDC); REQUIRE(f); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(GetObjectType));
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }
    int banding = 0;
    BOOL r = f(dc, NULL, &banding, NULL, NULL);
    t_note("ResetDC(memory dc, NULL) = %d, last error %lu", r, (unsigned long)GetLastError());
    /* a refused reset must leave the DC usable; a successful one hands back a DC of the same handle */
    CHECK(U(GetObjectType)(dc) == OBJ_MEMDC);
    U(DeleteDC)(dc);
}
TEST(gap2_drawstream_too_short_or_bad_magic) {
    NtGdiDrawStream_pfn f = GN(NtGdiDrawStream); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }
    unsigned char buf[64]; memset(buf, 0, sizeof buf);
    EXPECT_EQ(f(dc, 0, buf), 0);   /* size < 4 -> return 0 */
    EXPECT_EQ(f(dc, 3, buf), 0);
    EXPECT_EQ(f(NULL, 0, buf), 0);
    EXPECT_EQ(f(dc, 8, buf), 0);   /* zeroed header != 0x44727753 */
    memset(buf, 0xFF, sizeof buf);
    EXPECT_EQ(f(dc, 16, buf), 0);
    EXPECT_EQ(f(NULL, 16, buf), 0);
    unsigned magic = 0x44727752;   /* one off */
    memcpy(buf, &magic, 4);
    EXPECT_EQ(f(dc, 8, buf), 0);
    U(DeleteDC)(dc);
}
TEST(gap2_drawstream_larger_than_stack_copy_bad_magic) {
    NtGdiDrawStream_pfn f = GN(NtGdiDrawStream); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }
    /* > 0x100 bytes take the AllocFreeTmpBuffer path; the magic check still rejects the stream */
    unsigned char *big = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4096);
    if (big) {
        EXPECT_EQ(f(dc, 0x101, big), 0);
        EXPECT_EQ(f(dc, 4096, big), 0);
        HeapFree(GetProcessHeap(), 0, big);
    }
    U(DeleteDC)(dc);
}

/* ---------- GetOemBitmapSize ---------- */
TEST(gap2_oembmp_ids_in_range_are_consistent) {
    NtUserGetOemBitmapSize_pfn f = GN(NtUserGetOemBitmapSize); REQUIRE(f);
    int nonzero = 0;
    for (UINT id = 0; id < 0x5d; id++) {
        int sz[2] = { 0x7a7a7a7a, 0x7b7b7b7b };
        DWORD r = f(id, sz);
        /* output is stored before the result is derived: result == (width != 0 && height != 0) */
        CHECKF((r != 0) == (sz[0] != 0 && sz[1] != 0), "id %u r=%lu size %dx%d", id, (unsigned long)r, sz[0], sz[1]);
        if (r) nonzero++;
    }
    t_note("%d of 93 OEM bitmap ids have a non-empty size", nonzero);
}
TEST(gap2_oembmp_out_of_range_ids) {
    NtUserGetOemBitmapSize_pfn f = GN(NtUserGetOemBitmapSize); REQUIRE(f);
    UINT ids[] = { 0x5d, 0x5e, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0xffffffff };
    for (size_t i = 0; i < sizeof ids / sizeof *ids; i++) {
        int sz[2] = { 0x1357, 0x2468 };
        SET_ERR(0);
        EXPECT_EQ(f(ids[i], sz), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(sz[0], 0x1357);
        EXPECT_EQ(sz[1], 0x2468);
    }
}
TEST(gap2_oembmp_repeatable_per_dpi) {
    NtUserGetOemBitmapSize_pfn f = GN(NtUserGetOemBitmapSize); REQUIRE(f);
    for (UINT id = 0; id < 0x5d; id++) {
        int a[2] = { 0, 0 }, b[2] = { 1, 1 };
        DWORD ra = f(id, a), rb = f(id, b);
        EXPECT_EQ(ra, rb);
        EXPECT_EQ(a[0], b[0]);
        EXPECT_EQ(a[1], b[1]);
    }
}
TEST(gap2_oembmp_sizes_are_small_and_sane) {
    NtUserGetOemBitmapSize_pfn f = GN(NtUserGetOemBitmapSize); REQUIRE(f);
    for (UINT id = 0; id < 0x5d; id++) {
        int s[2] = { 0, 0 };
        if (!f(id, s)) continue;
        CHECKF(s[0] > 0 && s[1] > 0 && s[0] < 512 && s[1] < 512, "id %u size %dx%d", id, s[0], s[1]);
    }
}

/* ---------- BitBltSysBmp ---------- */
TEST(gap2_bitblt_sysbmp_bad_id) {
    NtUserBitBltSysBmp_pfn f = GN(NtUserBitBltSysBmp); REQUIRE(f); REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }
    UINT ids[] = { 0x5d, 0x100, 0xffff, 0xffffffff };
    for (size_t i = 0; i < sizeof ids / sizeof *ids; i++) {
        SET_ERR(0);
        EXPECT_EQ(f(dc, 0, 0, ids[i], SRCCOPY), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SET_ERR(0);
    EXPECT_EQ(f(NULL, 0, 0, 0x5d, SRCCOPY), 0);   /* id range is validated before the DC */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(DeleteDC)(dc);
}
TEST(gap2_bitblt_sysbmp_invalid_dc) {
    NtUserBitBltSysBmp_pfn f = GN(NtUserBitBltSysBmp); REQUIRE(f);
    EXPECT_EQ(f(NULL, 0, 0, 0, SRCCOPY), 0);
    EXPECT_EQ(f((HDC)(ULONG_PTR)0x7ffe1234, 0, 0, 0, SRCCOPY), 0);
}
TEST(gap2_bitblt_sysbmp_draws_inside_destination_only) {
    NtUserBitBltSysBmp_pfn f = GN(NtUserBitBltSysBmp); NtUserGetOemBitmapSize_pfn g = GN(NtUserGetOemBitmapSize);
    REQUIRE(f); REQUIRE(g); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(CreateDIBSection)); REQUIRE(U(GdiFlush));
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 256; bi.bmiHeader.biHeight = -256;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = U(CreateCompatibleDC)(NULL); void *bits = NULL;
    HBITMAP bmp = dc ? U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (!dc || !bmp || !bits) { if (bmp) U(DeleteObject)(bmp); if (dc) U(DeleteDC)(dc); t_skip("DIB fixture failed"); return; }
    HGDIOBJ old = U(SelectObject)(dc, bmp);
    UINT id = 0xffffffff; int sz[2] = { 0, 0 };
    for (UINT i = 0; i < 0x5d; i++) if (g(i, sz)) { id = i; break; }
    if (id == 0xffffffff) { t_skip("no OEM bitmap with a size"); goto out; }
    unsigned *px = bits;
    for (int i = 0; i < 256 * 256; i++) px[i] = 0x00FF00FF;
    U(GdiFlush)();
    INT_PTR r = f(dc, 0, 0, id, SRCCOPY);
    U(GdiFlush)();
    t_note("BitBltSysBmp id %u (%dx%d) = %lld", id, sz[0], sz[1], (long long)r);
    if (r) {
        /* an OEM bitmap is at most a few dozen pixels (more when DPI-scaled), so the far corner is outside the blit */
        CHECK(px[255 * 256 + 255] == 0x00FF00FF);
        CHECK(px[255 * 256 + 128] == 0x00FF00FF);
        int changed = 0;
        for (int y = 0; y < 128 && !changed; y++) for (int x = 0; x < 128; x++) if (px[y * 256 + x] != 0x00FF00FF) { changed = 1; break; }
        CHECK(changed);
    }
out:
    U(SelectObject)(dc, old); U(DeleteObject)(bmp); U(DeleteDC)(dc);
}

/* ---------- AreDpiAwarenessContextsEqual (named in the route table for GetUniformSpaceMapping; pure user32 logic) ---------- */
TEST(gap2_dpictx_equality_rules) {
    REQUIRE(U(AreDpiAwarenessContextsEqual));
    if (t_is_wine()) { t_skip("wine does not validate the per-monitor v2 context"); return; }
    DPI_AWARENESS_CONTEXT c[] = { DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE,
                                  DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 };
    for (size_t i = 0; i < 4; i++)
        for (size_t j = 0; j < 4; j++)
            CHECKF(!!U(AreDpiAwarenessContextsEqual)(c[i], c[j]) == (i == j), "ctx %zu vs %zu", i, j);
    CHECK(!U(AreDpiAwarenessContextsEqual)(NULL, c[0]));
    CHECK(!U(AreDpiAwarenessContextsEqual)(c[0], NULL));
}
