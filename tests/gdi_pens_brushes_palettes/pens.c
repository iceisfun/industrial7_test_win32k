/* Pens: gdi32full!CreatePen -> NtGdiCreatePen, and
 * gdi32full!ExtCreatePen -> NtGdiExtCreatePen (win32kfull). Object read-back goes through
 * NtGdiExtGetObjectW: flag 0x800 (bOldStylePen) selects the 16-byte LOGPEN
 * form, flag 0x400 alone the EXTLOGPEN form of size 0x1c + 4 * elpNumEntries. */
#include "pb.h"

#define EPERR ERROR_INVALID_PARAMETER

static HPEN ext(DWORD style, DWORD w, UINT bs, COLORREF c, ULONG_PTR hatch, DWORD cs, const DWORD *ps) {
    LOGBRUSH lb; lb.lbStyle = bs; lb.lbColor = c; lb.lbHatch = hatch;
    return U(ExtCreatePen)(style, w, &lb, cs, ps);
}
static int get_elp(HGDIOBJ h, EXTLOGPEN *e, int cb) { memset(e, 0xCC, sizeof *e); return pb_getobj(h, cb, e); }

/* ---------------- CreatePen / old style pens ---------------- */

TEST(pens_createpen_styles_logpen) {
    PB_GDI(); REQUIRE(U(CreatePen));
    static const struct { int style, width; } v[] = { {PS_SOLID, 3}, {PS_DASH, 1}, {PS_DOT, 1}, {PS_DASHDOT, 1}, {PS_DASHDOTDOT, 1}, {PS_INSIDEFRAME, 4} };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        COLORREF c = RGB(10 + i, 20 + i, 30 + i);
        HPEN p = U(CreatePen)(v[i].style, v[i].width, c);
        CHECKF(p != NULL, "style %d", v[i].style);
        if (!p) continue;
        LOGPEN lp; memset(&lp, 0xCC, sizeof lp);
        /* cjGetBrushOrPen LOGPEN branch (flag 0x800): style @0, width.x @4, width.y @8 = 0, color @0xc; returns 0x10 */
        CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof(LOGPEN));
        CHECKF(lp.lopnStyle == (UINT)v[i].style, "style %u want %d", lp.lopnStyle, v[i].style);
        CHECKF(lp.lopnWidth.x == v[i].width, "width %ld", lp.lopnWidth.x);
        CHECK(lp.lopnWidth.y == 0);
        CHECK(lp.lopnColor == c);
        CHECK(U(GetObjectType)(p) == OBJ_PEN);
        CHECK(pb_del(p));
    }
}
TEST(pens_createpen_handle_type_bits) {
    PB_GDI(); REQUIRE(U(CreatePen));
    HPEN p = U(CreatePen)(PS_SOLID, 5, RGB(1, 2, 3));
    REQUIRE(p);
    /* ExtCreatePen ORs 0x300000 (old-style pen) into the handle type */
    CHECKF(PB_T7(p) == T7_PEN, "type7 0x%x", PB_T7(p));
    CHECK(!PB_STOCKED(p));
    /* a GDI handle is a 32-bit value that the kernel hands out sign-extended: the upper half is all zero or all one */
    CHECK((LONG_PTR)p == (LONG_PTR)(int)(unsigned)(ULONG_PTR)p);
    pb_del(p);
}
TEST(pens_createpen_cell_state) {
    PB_GDI(); PB_TABLE();
    HPEN p = U(CreatePen)(PS_SOLID, 7, RGB(9, 8, 7));
    REQUIRE(p);
    struct pb_cell c;
    CHECK(pb_cell_of(p, &c));
    CHECKF(c.objt == 0x10, "Objt %u", c.objt); /* InternalDeleteObject: type7 & 0x1f */
    CHECK((c.owner & ~1u) == PB_PID());
    CHECKF(c.upper == (unsigned short)((ULONG_PTR)p >> 16), "upper 0x%x vs handle 0x%llx", c.upper, (unsigned long long)(ULONG_PTR)p);
    CHECK(c.puser != 0 || (t_note("pen cell pUser is NULL"), 1));
    pb_del(p);
}
TEST(pens_createpen_null_style_is_stock) {
    PB_GDI(); REQUIRE(U(CreatePen));
    /* gdi32full!CreatePen: iStyle == 5 returns GetStockObject(NULL_PEN) before any syscall */
    HPEN p = U(CreatePen)(PS_NULL, 3, RGB(1, 1, 1));
    CHECK(p == (HPEN)U(GetStockObject)(NULL_PEN));
    CHECK(p != NULL && PB_STOCKED(p));
    LOGPEN lp; memset(&lp, 0xCC, sizeof lp);
    CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof lp);
    CHECK(lp.lopnStyle == PS_NULL);
}
TEST(pens_createpen_invalid_style_normalised) {
    PB_GDI(); REQUIRE(U(CreatePen));
    /* gdi32full!CreatePen: iStyle < 0, or > 4 and not 5/6, is replaced by 0 (PS_SOLID) */
    static const int bad[] = { -1, -100, 7, 8, 9, 100, 0x10000, INT_MIN };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HPEN p = U(CreatePen)(bad[i], 2, RGB(5, 5, 5));
        CHECKF(p != NULL, "style %d", bad[i]);
        if (!p) continue;
        LOGPEN lp;
        CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof lp);
        CHECKF(lp.lopnStyle == PS_SOLID, "style %d -> %u", bad[i], lp.lopnStyle);
        pb_del(p);
    }
}
TEST(pens_createpen_negative_width_abs) {
    PB_GDI(); REQUIRE(U(CreatePen));
    static const int w[] = { -1, -5, -300 };
    for (size_t i = 0; i < sizeof w / sizeof w[0]; i++) {
        HPEN p = U(CreatePen)(PS_SOLID, w[i], RGB(1, 2, 3));
        REQUIRE(p);
        LOGPEN lp;
        CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof lp);
        /* ExtCreatePen stores abs(cWidth) */
        CHECKF(lp.lopnWidth.x == -w[i], "w %d -> %ld", w[i], lp.lopnWidth.x);
        pb_del(p);
    }
}
TEST(pens_createpen_width_zero_logpen) {
    PB_GDI(); REQUIRE(U(CreatePen));
    HPEN p = U(CreatePen)(PS_SOLID, 0, RGB(11, 12, 13));
    REQUIRE(p);
    LOGPEN lp;
    CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof lp);
    CHECK(lp.lopnWidth.x == 0); /* width 0 means "1 pixel" only at draw time; the stored value is abs(0) */
    CHECK(lp.lopnColor == RGB(11, 12, 13));
    CHECK(lp.lopnStyle == PS_SOLID);
    /* a second width-0 pen: gdi32full may hand out a cached object (hGetPEBHandle) but never the same live handle twice */
    HPEN q = U(CreatePen)(PS_SOLID, 0, RGB(21, 22, 23));
    REQUIRE(q);
    CHECK(q != p);
    CHECK(pb_getobj(q, sizeof lp, &lp) == (int)sizeof lp && lp.lopnColor == RGB(21, 22, 23));
    pb_del(q); pb_del(p);
}
TEST(pens_createpen_color_roundtrip) {
    PB_GDI(); REQUIRE(U(CreatePen));
    static const COLORREF cs[] = { 0, RGB(255, 255, 255), RGB(1, 0, 0), RGB(0, 1, 0), RGB(0, 0, 1), RGB(128, 64, 32), RGB(254, 253, 252) };
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        HPEN p = U(CreatePen)(PS_SOLID, 2, cs[i]);
        REQUIRE(p);
        LOGPEN lp;
        CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof lp);
        CHECKF(lp.lopnColor == cs[i], "color 0x%lx got 0x%lx", cs[i], lp.lopnColor);
        pb_del(p);
    }
}
TEST(pens_createpen_color_high_byte) {
    PB_GDI(); REQUIRE(U(CreatePen));
    static const COLORREF cs[] = { 0x01123456, 0x02123456, 0xFF123456 };
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        HPEN p = U(CreatePen)(PS_SOLID, 2, cs[i]);
        LOGPEN lp;
        if (!p || pb_getobj(p, sizeof lp, &lp) != (int)sizeof lp) { CHECK(p != NULL); continue; }
        CHECK((lp.lopnColor & 0xFFFFFF) == (cs[i] & 0xFFFFFF));
        t_note("pen color 0x%08lx reads back as 0x%08lx", cs[i], lp.lopnColor);
        pb_del(p);
    }
}
TEST(pens_createpenindirect_roundtrip) {
    PB_GDI(); REQUIRE(U(CreatePenIndirect));
    LOGPEN in = { PS_DOT, { 1, 99 }, RGB(40, 50, 60) }, out;
    HPEN p = U(CreatePenIndirect)(&in);
    REQUIRE(p);
    memset(&out, 0xCC, sizeof out);
    CHECK(pb_getobj(p, sizeof out, &out) == (int)sizeof out);
    CHECK(out.lopnStyle == PS_DOT);
    CHECK(out.lopnWidth.x == 1);
    CHECK(out.lopnWidth.y == 0); /* lopnWidth.y is ignored on input and reported as 0 */
    CHECK(out.lopnColor == in.lopnColor);
    pb_del(p);
}
TEST(pens_getobject_logpen_buffer_protocol) {
    PB_GDI();
    HPEN p = U(CreatePen)(PS_SOLID, 3, RGB(1, 2, 3));
    REQUIRE(p);
    LOGPEN lp; unsigned char big[64];
    /* NULL buffer: gdi32full!GetObjectW answers 0x10 itself for type 0x30 (and 0x10) handles */
    CHECK(pb_getobj(p, 0, NULL) == 0x10);
    CHECK(pb_getobj(p, 100, NULL) == 0x10);
 /* cjGetBrushOrPen LOGPEN branch: for the form with a buffer */
    for (int cb = 1; cb < 0x10; cb++) CHECKF(pb_getobj(p, cb, &lp) == 0, "cb %d", cb);
    memset(big, 0xAB, sizeof big);
    CHECK(pb_getobj(p, 0x10, big) == 0x10);
    CHECK(big[0x10] == 0xAB && big[0x1f] == 0xAB); /* nothing written past 0x10 */
    memset(big, 0xAB, sizeof big);
    CHECK(pb_getobj(p, 64, big) == 0x10);
    CHECK(big[0x10] == 0xAB);
    /* NtGdiExtGetObjectW clamps a negative count to 0x250 before calling the Gre worker */
    CHECK(pb_getobj(p, -1, &lp) == 0x10);
    pb_del(p);
}
TEST(pens_createpen_delete_lifetime) {
    PB_GDI();
    HPEN p = U(CreatePen)(PS_SOLID, 4, RGB(1, 2, 3));
    REQUIRE(p);
    LOGPEN lp;
    CHECK(pb_getobj(p, sizeof lp, &lp) == (int)sizeof lp);
    CHECK(pb_del(p));
    SetLastError(0);
    CHECK(pb_getobj(p, sizeof lp, &lp) == 0);
    CHECK(U(GetObjectType)(p) == OBJ_PEN);       /* a freed cell keeps its upper word: the handle's type is still reported */
    SetLastError(0);
    CHECK(!pb_del(p));
}
TEST(pens_createpen_gdi_count_delta) {
    PB_GDI(); REQUIRE(U(GetGuiResources));
    enum { N = 40 };
    HPEN h[N];
    DWORD g0 = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < N; i++) { h[i] = U(CreatePen)(PS_SOLID, 3 + (i & 3), RGB(i, i, i)); REQUIRE(h[i]); }
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == g0 + N);
    for (int i = 0; i < N; i++) for (int j = 0; j < i; j++) CHECK(h[i] != h[j]);
    for (int i = 0; i < N; i++) pb_del(h[i]);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == g0);
}
TEST(pens_createpen_pens_in_table_have_brush_attr) {
    PB_GDI(); PB_TABLE();
    HPEN p = U(CreatePen)(PS_SOLID, 6, RGB(100, 110, 120));
    REQUIRE(p);
    DWORD a[2] = { 0, 0 };
    void *attr = pb_attr(p);
    if (!attr || !k_try_read(attr, a, sizeof a)) { t_note("pen has no readable BRUSHATTR"); pb_del(p); return; }
    /* BRUSHATTR.crBrush mirrors the color unless the kernel copy is dirty (flag 4) */
    CHECKF(a[1] == RGB(100, 110, 120) || (a[0] & 4), "attr {0x%lx,0x%lx}", a[0], a[1]);
    pb_del(p);
}

/* ---------------- NtGdiCreatePen direct ---------------- */

TEST(pens_nt_createpen_styles) {
    REQUIRE(UN(NtGdiCreatePen)); PB_GDI();
    for (int s = 0; s <= 4; s++) {
        HANDLE p = UN(NtGdiCreatePen)(s, 1, RGB(3, 4, 5), NULL);
        CHECKF(p != NULL, "style %d", s);
        if (!p) continue;
        LOGPEN lp;
        CHECK(pb_getobj(p, sizeof lp, &lp) == 0x10);
        CHECK(lp.lopnStyle == (UINT)s);
        CHECK(PB_T7(p) == T7_PEN);
        pb_del(p);
    }
    HANDLE p = UN(NtGdiCreatePen)(PS_INSIDEFRAME, 9, 0, NULL);
    REQUIRE(p);
    LOGPEN lp;
    CHECK(pb_getobj(p, sizeof lp, &lp) == 0x10 && lp.lopnStyle == PS_INSIDEFRAME && lp.lopnWidth.x == 9);
    pb_del(p);
}
TEST(pens_nt_createpen_null_style_stock) {
    REQUIRE(UN(NtGdiCreatePen)); PB_GDI();
    /* ExtCreatePen: (style & 0xf) == 5 jumps to the stock-object return before any validation */
    HANDLE p = UN(NtGdiCreatePen)(PS_NULL, 1, 0, NULL);
    CHECK(p != NULL);
    if (!p) return;
    LOGPEN lp;
    CHECK(pb_getobj(p, sizeof lp, &lp) == 0x10 && lp.lopnStyle == PS_NULL);
    CHECK(PB_STOCKED(p));
    if (p != U(GetStockObject)(NULL_PEN)) t_note("kernel NULL pen 0x%llx differs from GetStockObject(NULL_PEN) 0x%llx", (unsigned long long)(ULONG_PTR)p, (unsigned long long)(ULONG_PTR)U(GetStockObject)(NULL_PEN));
}
TEST(pens_nt_createpen_bad_styles) {
    REQUIRE(UN(NtGdiCreatePen)); PB_GDI();
    /* CreatePen - unsigned compare, so negatives fail too */
    static const LONG bad[] = { 7, 8, 9, 15, 0x10, 0x100, 0x10000, -1, INT_MIN };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        HANDLE p = UN(NtGdiCreatePen)(bad[i], 1, 0, NULL);
        CHECKF(p == NULL, "style %ld", bad[i]);
        EXPECT_ERR(EPERR);
        if (p) pb_del(p);
    }
}
TEST(pens_nt_createpen_matches_userland) {
    REQUIRE(UN(NtGdiCreatePen)); PB_GDI();
    HANDLE a = UN(NtGdiCreatePen)(PS_DASH, 1, RGB(7, 8, 9), NULL);
    HPEN b = U(CreatePen)(PS_DASH, 1, RGB(7, 8, 9));
    REQUIRE(a); REQUIRE(b);
    LOGPEN la, lb;
    CHECK(pb_getobj(a, sizeof la, &la) == 0x10 && pb_getobj(b, sizeof lb, &lb) == 0x10);
    CHECK(memcmp(&la, &lb, sizeof la) == 0);
    CHECK(PB_T7(a) == PB_T7(b));
    pb_del(a); pb_del(b);
}

/* ---------------- ExtCreatePen ---------------- */

struct ev { const char *why; DWORD style, w; UINT bs; ULONG_PTR hatch; DWORD cs; int ok; };
static const DWORD dashes16[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };

TEST(pens_extcreatepen_valid_table) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    static const struct ev v[] = {
        { "cosmetic solid", PS_COSMETIC | PS_SOLID, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic dash", PS_COSMETIC | PS_DASH, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic dot", PS_COSMETIC | PS_DOT, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic dashdot", PS_COSMETIC | PS_DASHDOT, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic dashdotdot", PS_COSMETIC | PS_DASHDOTDOT, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic alternate", PS_COSMETIC | PS_ALTERNATE, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic width -1", PS_COSMETIC | PS_SOLID, (DWORD)-1, BS_SOLID, 0, 0, 1 },
        { "cosmetic endcap square", PS_COSMETIC | PS_SOLID | PS_ENDCAP_SQUARE, 1, BS_SOLID, 0, 0, 1 },
        { "cosmetic join bevel", PS_COSMETIC | PS_SOLID | PS_JOIN_BEVEL, 1, BS_SOLID, 0, 0, 1 },
        { "geometric solid w0", PS_GEOMETRIC | PS_SOLID, 0, BS_SOLID, 0, 0, 1 },
        { "geometric solid w1", PS_GEOMETRIC | PS_SOLID, 1, BS_SOLID, 0, 0, 1 },
        { "geometric solid w5", PS_GEOMETRIC | PS_SOLID, 5, BS_SOLID, 0, 0, 1 },
        { "geometric solid w100", PS_GEOMETRIC | PS_SOLID, 100, BS_SOLID, 0, 0, 1 },
        { "geometric dash", PS_GEOMETRIC | PS_DASH, 5, BS_SOLID, 0, 0, 1 },
        { "geometric dot", PS_GEOMETRIC | PS_DOT, 5, BS_SOLID, 0, 0, 1 },
        { "geometric dashdot", PS_GEOMETRIC | PS_DASHDOT, 5, BS_SOLID, 0, 0, 1 },
        { "geometric dashdotdot", PS_GEOMETRIC | PS_DASHDOTDOT, 5, BS_SOLID, 0, 0, 1 },
        { "geometric insideframe", PS_GEOMETRIC | PS_INSIDEFRAME, 5, BS_SOLID, 0, 0, 1 },
        { "geometric hatch cross", PS_GEOMETRIC | PS_SOLID, 4, BS_HATCHED, HS_CROSS, 0, 1 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        COLORREF c = RGB(30 + i, 60, 90);
        SetLastError(0);
        HPEN p = ext(v[i].style, v[i].w, v[i].bs, c, v[i].hatch, v[i].cs, NULL);
        CHECKF(p != NULL, "%s: err %lu", v[i].why, GetLastError());
        if (!p) continue;
        EXTLOGPEN e;
        int r = get_elp(p, &e, sizeof e);
        CHECKF(r == 0x1c, "%s: size %d", v[i].why, r);
        CHECKF(e.elpPenStyle == v[i].style, "%s: elpPenStyle 0x%lx want 0x%lx", v[i].why, e.elpPenStyle, v[i].style);
        DWORD aw = (LONG)v[i].w < 0 ? (DWORD)-(LONG)v[i].w : v[i].w;
        if (!(v[i].style & PS_GEOMETRIC)) aw = 1;
        CHECKF(e.elpWidth == aw, "%s: elpWidth %lu want %lu", v[i].why, e.elpWidth, aw);
        CHECKF(e.elpBrushStyle == v[i].bs, "%s: brush %u", v[i].why, e.elpBrushStyle);
        CHECKF(e.elpNumEntries == 0, "%s: entries %lu", v[i].why, e.elpNumEntries);
        CHECKF(e.elpColor == c, "%s: color 0x%lx", v[i].why, e.elpColor);
        CHECKF(PB_T7(p) == T7_EXTPEN, "%s: type7 0x%x", v[i].why, PB_T7(p));
        CHECKF(U(GetObjectType)(p) == OBJ_EXTPEN, "%s: objtype", v[i].why);
        pb_del(p);
    }
}
TEST(pens_extcreatepen_invalid_table) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    static const struct ev v[] = {
        /* ExtCreatePen: cosmetic (type 0) requires abs(width) == 1 and brush BS_SOLID */
        { "cosmetic w0", PS_COSMETIC | PS_SOLID, 0, BS_SOLID, 0, 0, 0 },
        { "cosmetic w2", PS_COSMETIC | PS_SOLID, 2, BS_SOLID, 0, 0, 0 },
        { "cosmetic w100", PS_COSMETIC | PS_SOLID, 100, BS_SOLID, 0, 0, 0 },
        { "cosmetic w-2", PS_COSMETIC | PS_SOLID, (DWORD)-2, BS_SOLID, 0, 0, 0 },
        { "cosmetic insideframe", PS_COSMETIC | PS_INSIDEFRAME, 1, BS_SOLID, 0, 0, 0 },
        { "cosmetic style 9", PS_COSMETIC | 9, 1, BS_SOLID, 0, 0, 0 },
        { "cosmetic style 10", PS_COSMETIC | 10, 1, BS_SOLID, 0, 0, 0 },
        { "cosmetic style 15", PS_COSMETIC | 15, 1, BS_SOLID, 0, 0, 0 },
        { "cosmetic hatched", PS_COSMETIC | PS_SOLID, 1, BS_HATCHED, HS_CROSS, 0, 0 },
        { "cosmetic pattern bitmap handle", PS_COSMETIC | PS_SOLID, 1, BS_PATTERN, 0, 0, 0 },
        /* invalid pen type nibble (mask 0xf0000) */
        { "type 2", 0x20000 | PS_SOLID, 1, BS_SOLID, 0, 0, 0 },
        { "type 3", 0x30000 | PS_SOLID, 1, BS_SOLID, 0, 0, 0 },
        { "type 0xf", 0xf0000 | PS_SOLID, 1, BS_SOLID, 0, 0, 0 },
        /* endcap nibble: only 0, 0x100, 0x200 */
        { "endcap 0x300", PS_GEOMETRIC | PS_SOLID | 0x300, 1, BS_SOLID, 0, 0, 0 },
        { "endcap 0x400", PS_GEOMETRIC | PS_SOLID | 0x400, 1, BS_SOLID, 0, 0, 0 },
        { "endcap 0xf00", PS_GEOMETRIC | PS_SOLID | 0xf00, 1, BS_SOLID, 0, 0, 0 },
        /* join nibble: only 0, 0x1000, 0x2000 */
        { "join 0x3000", PS_GEOMETRIC | PS_SOLID | 0x3000, 1, BS_SOLID, 0, 0, 0 },
        { "join 0x4000", PS_GEOMETRIC | PS_SOLID | 0x4000, 1, BS_SOLID, 0, 0, 0 },
        { "join 0xf000", PS_GEOMETRIC | PS_SOLID | 0xf000, 1, BS_SOLID, 0, 0, 0 },
        /* reserved bits 0xfff000f0 */
        { "bit 0x10", PS_GEOMETRIC | PS_SOLID | 0x10, 1, BS_SOLID, 0, 0, 0 },
        { "bit 0x20", PS_GEOMETRIC | PS_SOLID | 0x20, 1, BS_SOLID, 0, 0, 0 },
        { "bit 0x40", PS_GEOMETRIC | PS_SOLID | 0x40, 1, BS_SOLID, 0, 0, 0 },
        { "bit 0x80", PS_GEOMETRIC | PS_SOLID | 0x80, 1, BS_SOLID, 0, 0, 0 },
        { "bit 0x100000", PS_GEOMETRIC | PS_SOLID | 0x100000, 1, BS_SOLID, 0, 0, 0 },
        { "bit 0x10000000", PS_GEOMETRIC | PS_SOLID | 0x10000000, 1, BS_SOLID, 0, 0, 0 },
        { "bit 0x80000000", PS_GEOMETRIC | PS_SOLID | 0x80000000u, 1, BS_SOLID, 0, 0, 0 },
        /* brush style outside the set the pen code accepts */
        { "brush style 4", PS_GEOMETRIC | PS_SOLID, 3, 4, 0, 0, 0 },
        { "brush style 7", PS_GEOMETRIC | PS_SOLID, 3, 7, 0, 0, 0 },
        { "brush style 8", PS_GEOMETRIC | PS_SOLID, 3, 8, 0, 0, 0 },
        { "brush style 9", PS_GEOMETRIC | PS_SOLID, 3, 9, 0, 0, 0 },
        { "brush style 0x100", PS_GEOMETRIC | PS_SOLID, 3, 0x100, 0, 0, 0 },
        /* gdi32full!ExtCreatePen: a pattern brush needs a non-zero lbHatch (the HBITMAP) */
        { "pattern null bitmap", PS_GEOMETRIC | PS_SOLID, 3, BS_PATTERN, 0, 0, 0 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        HPEN p = ext(v[i].style, v[i].w, v[i].bs, RGB(1, 2, 3), v[i].hatch, v[i].cs, NULL);
        CHECKF(p == NULL, "%s unexpectedly created", v[i].why);
        EXPECT_ERR(EPERR);
        if (p) pb_del(p);
    }
}
TEST(pens_extcreatepen_size_protocol) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 3, BS_SOLID, RGB(1, 2, 3), 0, 0, NULL);
    REQUIRE(p);
    EXTLOGPEN e; unsigned char buf[0x40];
    /* extended pen: handle type 0x50 is not answered by gdi32, so a NULL buffer reaches the kernel:
     * cjGetBrushOrPen returns 0x1c + 4 * numEntries */
    CHECK(pb_getobj(p, 0, NULL) == 0x1c);
    for (int cb = 0; cb < 0x1c; cb++) CHECKF(pb_getobj(p, cb, &e) == 0, "cb %d", cb);
    memset(buf, 0xAB, sizeof buf);
    CHECK(pb_getobj(p, 0x1c, buf) == 0x1c);
    CHECK(buf[0x1c] == 0xAB && buf[0x1f] == 0xAB);
    CHECK(pb_getobj(p, 0x20, buf) == 0x1c);
    CHECK(pb_getobj(p, 0x40, buf) == 0x1c);
    CHECK(pb_getobj(p, -5, buf) == 0x1c);
    pb_del(p);
}
TEST(pens_extcreatepen_userstyle_entries) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    for (DWORD n = 1; n <= 16; n++) {
        HPEN p = ext(PS_COSMETIC | PS_USERSTYLE, 1, BS_SOLID, RGB(5, 6, 7), 0, n, dashes16);
        CHECKF(p != NULL, "n=%lu err=%lu", n, GetLastError());
        if (!p) continue;
        unsigned char buf[0x1c + 16 * 4 + 8];
        EXTLOGPEN *e = (EXTLOGPEN *)buf;
        CHECK(pb_getobj(p, 0, NULL) == (int)(0x1c + 4 * n));
        memset(buf, 0, sizeof buf);
        int r = pb_getobj(p, sizeof buf, buf);
        CHECKF(r == (int)(0x1c + 4 * n), "n=%lu r=%d", n, r);
        CHECK(e->elpNumEntries == n);
        CHECK(e->elpPenStyle == (PS_COSMETIC | PS_USERSTYLE));
        for (DWORD k = 0; k < n; k++) CHECKF(e->elpStyleEntry[k] == dashes16[k], "n=%lu entry %lu = %lu", n, k, e->elpStyleEntry[k]);
        /* a buffer one byte too small yields 0 and writes nothing */
        if (n < 16) { memset(buf, 0xAB, sizeof buf); CHECK(pb_getobj(p, 0x1c + 4 * n - 1, buf) == 0); CHECK(buf[0] == 0xAB); }
        pb_del(p);
    }
}
TEST(pens_extcreatepen_userstyle_geometric_entries) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    DWORD d[3] = { 4, 2, 8 };
    HPEN p = ext(PS_GEOMETRIC | PS_USERSTYLE, 5, BS_SOLID, RGB(5, 6, 7), 0, 3, d);
    CHECKF(p != NULL, "err %lu", GetLastError());
    if (!p) return;
    unsigned char buf[0x30]; EXTLOGPEN *e = (EXTLOGPEN *)buf;
    CHECK(pb_getobj(p, sizeof buf, buf) == 0x1c + 12);
    CHECK(e->elpNumEntries == 3);
    CHECK(e->elpWidth == 5);
    /* geometric dash arrays are stored as floats and converted back by bFToL when read */
    for (int k = 0; k < 3; k++) CHECKF(e->elpStyleEntry[k] == d[k], "entry %d = %lu", k, e->elpStyleEntry[k]);
    pb_del(p);
}
TEST(pens_extcreatepen_userstyle_limits) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    static const DWORD counts[] = { 17, 18, 32, 100 };
    DWORD big[128]; for (int i = 0; i < 128; i++) big[i] = 3;
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        SetLastError(0);
        HPEN p = ext(PS_COSMETIC | PS_USERSTYLE, 1, BS_SOLID, 0, 0, counts[i], big);
        CHECKF(p == NULL, "count %lu", counts[i]);
        EXPECT_ERR(EPERR);
        if (p) pb_del(p);
        p = ext(PS_GEOMETRIC | PS_USERSTYLE, 3, BS_SOLID, 0, 0, counts[i], big);
        CHECKF(p == NULL, "geometric count %lu", counts[i]);
        if (p) pb_del(p);
    }
}
TEST(pens_extcreatepen_userstyle_null_array) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    /* gdi32full!ExtCreatePen: (style & 0xf) == 7 with pstyle == NULL -> GdiSetLastError(0x57) */
    SetLastError(0);
    HPEN p = ext(PS_GEOMETRIC | PS_USERSTYLE, 3, BS_SOLID, 0, 0, 2, NULL);
    CHECK(p == NULL); EXPECT_ERR(EPERR);
    SetLastError(0);
    p = ext(PS_COSMETIC | PS_USERSTYLE, 1, BS_SOLID, 0, 0, 0, NULL);
    CHECK(p == NULL); EXPECT_ERR(EPERR);
}
TEST(pens_extcreatepen_array_with_non_userstyle) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    static const DWORD styles[] = { PS_SOLID, PS_DASH, PS_DOT, PS_DASHDOT, PS_DASHDOTDOT, PS_NULL, PS_INSIDEFRAME, PS_ALTERNATE };
    for (size_t i = 0; i < sizeof styles / sizeof styles[0]; i++) {
        /* gdi32full!ExtCreatePen: any non-USERSTYLE style with cStyle != 0 or pstyle != NULL fails with 0x57 */
        SetLastError(0);
        HPEN p = ext(PS_GEOMETRIC | styles[i], 3, BS_SOLID, 0, 0, 2, dashes16);
        CHECKF(p == NULL, "style %lu", styles[i]); EXPECT_ERR(EPERR);
        if (p) pb_del(p);
        SetLastError(0);
        p = ext(PS_GEOMETRIC | styles[i], 3, BS_SOLID, 0, 0, 0, dashes16);
        CHECKF(p == NULL, "style %lu NULL cStyle", styles[i]); EXPECT_ERR(EPERR);
        if (p) pb_del(p);
        SetLastError(0);
        p = ext(PS_GEOMETRIC | styles[i], 3, BS_SOLID, 0, 0, 1, NULL);
        CHECKF(p == NULL, "style %lu cStyle only", styles[i]); EXPECT_ERR(EPERR);
        if (p) pb_del(p);
    }
}
TEST(pens_extcreatepen_cosmetic_width_rule) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
 /* ExtCreatePen: -> invalid, with abs(width) */
    for (LONG w = -3; w <= 3; w++) {
        SetLastError(0);
        HPEN p = ext(PS_COSMETIC | PS_SOLID, (DWORD)w, BS_SOLID, 0, 0, 0, NULL);
        int ok = (w == 1 || w == -1);
        CHECKF((p != NULL) == ok, "width %ld", w);
        if (!ok && !p) EXPECT_ERR(EPERR);
        if (p) {
            EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c); CHECK(e.elpWidth == 1);
            pb_del(p);
        }
    }
}
TEST(pens_extcreatepen_cosmetic_dash_entries_zero) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    static const DWORD styles[] = { PS_DASH, PS_DOT, PS_DASHDOT, PS_DASHDOTDOT, PS_ALTERNATE };
    for (size_t i = 0; i < sizeof styles / sizeof styles[0]; i++) {
        HPEN p = ext(PS_COSMETIC | styles[i], 1, BS_SOLID, 0, 0, 0, NULL);
        REQUIRE(p);
        /* elpNumEntries is only filled for PS_USERSTYLE (cjGetBrushOrPen reads *(brush+0xb4) only when (style & 0xf) == 7) */
        CHECKF(pb_getobj(p, 0, NULL) == 0x1c, "style %lu size", styles[i]);
        EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c);
        CHECK(e.elpNumEntries == 0);
        pb_del(p);
    }
}
TEST(pens_extcreatepen_endcap_join_matrix) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    static const DWORD caps[] = { PS_ENDCAP_ROUND, PS_ENDCAP_SQUARE, PS_ENDCAP_FLAT };
    static const DWORD joins[] = { PS_JOIN_ROUND, PS_JOIN_BEVEL, PS_JOIN_MITER };
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) {
        DWORD st = PS_GEOMETRIC | PS_SOLID | caps[a] | joins[b];
        HPEN p = ext(st, 6, BS_SOLID, RGB(1, 2, 3), 0, 0, NULL);
        CHECKF(p != NULL, "style 0x%lx", st);
        if (!p) continue;
        EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c);
        CHECKF(e.elpPenStyle == st, "0x%lx -> 0x%lx", st, e.elpPenStyle); /* raw iPenStyle is stored at +0xb0 */
        pb_del(p);
    }
}
TEST(pens_extcreatepen_type_bits_geometric_dash_with_caps) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    DWORD st = PS_GEOMETRIC | PS_DASH | PS_ENDCAP_FLAT | PS_JOIN_MITER;
    HPEN p = ext(st, 4, BS_SOLID, RGB(9, 9, 9), 0, 0, NULL);
    CHECKF(p != NULL, "err %lu", GetLastError());
    if (!p) return;
    EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c);
    CHECK(e.elpPenStyle == st);
    CHECK(e.elpWidth == 4);
    pb_del(p);
}
TEST(pens_extcreatepen_null_style_is_stock_pen) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    /* ExtCreatePen: (style & 0xf) == 5 returns the stock object before validating width/brush */
    HPEN p = ext(PS_GEOMETRIC | PS_NULL, 9, BS_SOLID, RGB(1, 2, 3), 0, 0, NULL);
    CHECK(p != NULL);
    if (!p) return;
    CHECK(PB_STOCKED(p));
    LOGPEN lp; CHECK(pb_getobj(p, sizeof lp, &lp) == 0x10 && lp.lopnStyle == PS_NULL);
    HPEN q = ext(PS_COSMETIC | PS_NULL, 77, BS_SOLID, 0, 0, 0, NULL);
    CHECK(q == p);
}
TEST(pens_extcreatepen_hollow_brush_is_stock_null_pen) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
 /* brush style 1 (BS_NULL) with a solid/no dash array returns the stock NULL pen */
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 5, BS_NULL, 0, 0, 0, NULL);
    CHECK(p != NULL);
    if (!p) return;
    CHECK(PB_STOCKED(p));
    LOGPEN lp; CHECK(pb_getobj(p, sizeof lp, &lp) == 0x10 && lp.lopnStyle == PS_NULL);
}
TEST(pens_extcreatepen_hatch_geometric) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    for (UINT h = HS_HORIZONTAL; h <= HS_DIAGCROSS; h++) {
        COLORREF c = RGB(200, h * 10, 3);
        HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 6, BS_HATCHED, c, h, 0, NULL);
        CHECKF(p != NULL, "hatch %u", h);
        if (!p) continue;
        EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c);
        CHECK(e.elpBrushStyle == BS_HATCHED);
        CHECKF(e.elpHatch == h, "hatch %u -> %llu", h, (unsigned long long)e.elpHatch);
        CHECK(e.elpColor == c);
        pb_del(p);
    }
}
TEST(pens_extcreatepen_hatch_out_of_range) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    /* hCreateHatchBrushInternal only accepts iHatch < 12 */
    static const ULONG_PTR bad[] = { 12, 13, 100, 0xffff, 0x7fffffff };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 4, BS_HATCHED, 0, bad[i], 0, NULL);
        CHECKF(p == NULL, "hatch %llu", (unsigned long long)bad[i]);
        if (p) pb_del(p);
    }
}
TEST(pens_extcreatepen_pattern_bitmap) {
    PB_GDI(); REQUIRE(U(ExtCreatePen)); REQUIRE(U(CreateBitmap));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    REQUIRE(bm);
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 8, BS_PATTERN, 0, (ULONG_PTR)bm, 0, NULL);
    CHECKF(p != NULL, "err %lu", GetLastError());
    if (p) {
        EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c);
        CHECK(e.elpBrushStyle == BS_PATTERN);
        CHECK(e.elpHatch != 0);
        /* CreatePatternBrush clones the bitmap, so the pen survives deleting the source */
        pb_del(bm);
        CHECK(get_elp(p, &e, sizeof e) == 0x1c && e.elpBrushStyle == BS_PATTERN);
        pb_del(p);
    } else pb_del(bm);
}
TEST(pens_extcreatepen_pattern_bad_bitmap) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    SetLastError(0);
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 8, BS_PATTERN, 0, 0x12345678, 0, NULL);
    CHECK(p == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* the bitmap handle fails validation -> EngSetLastError(6) */
    if (p) pb_del(p);
}
TEST(pens_extcreatepen_dibpattern) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    struct { BITMAPINFOHEADER h; DWORD px[64]; } d;
    memset(&d, 0, sizeof d);
    d.h.biSize = sizeof d.h; d.h.biWidth = 8; d.h.biHeight = 8; d.h.biPlanes = 1; d.h.biBitCount = 32;
    for (int i = 0; i < 64; i++) d.px[i] = i * 0x010101;
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 6, BS_DIBPATTERNPT, DIB_RGB_COLORS, (ULONG_PTR)&d, 0, NULL);
    CHECKF(p != NULL, "err %lu", GetLastError());
    if (!p) return;
    EXTLOGPEN e; CHECK(get_elp(p, &e, sizeof e) == 0x1c);
    /* BS_DIBPATTERNPT (6) is the style the pen reports for a packed DIB pattern */
    CHECKF(e.elpBrushStyle == BS_DIBPATTERNPT || e.elpBrushStyle == BS_DIBPATTERN || e.elpBrushStyle == BS_PATTERN, "brush style %u", e.elpBrushStyle);
    pb_del(p);
}
TEST(pens_extcreatepen_nt_bad_brush_styles) {
    REQUIRE(UN(NtGdiExtCreatePen)); PB_GDI();
    /* direct syscall: brush style 5 (BS_DIBPATTERN) is converted to 6 by gdi32, the kernel itself rejects it with 0x57 */
    static const ULONG bs[] = { 4, 5, 7, 8, 9, 100 };
    for (size_t i = 0; i < sizeof bs / sizeof bs[0]; i++) {
        SetLastError(0);
        HANDLE p = UN(NtGdiExtCreatePen)(PS_GEOMETRIC | PS_SOLID, 3, bs[i], 0, 0, 0, 0, NULL, 0, FALSE, NULL);
        CHECKF(p == NULL, "brush style %lu", bs[i]);
        EXPECT_ERR(EPERR);
        if (p) pb_del(p);
    }
}
TEST(pens_extcreatepen_nt_old_style_flag) {
    REQUIRE(UN(NtGdiExtCreatePen)); PB_GDI();
    /* bOldStylePen != 0 relaxes the cosmetic width rule and yields a LOGPEN-form (type 0x30) object */
    HANDLE p = UN(NtGdiExtCreatePen)(PS_COSMETIC | PS_SOLID, 5, BS_SOLID, RGB(1, 2, 3), 0, 0, 0, NULL, 0, TRUE, NULL);
    CHECK(p != NULL);
    if (p) {
        CHECKF(PB_T7(p) == T7_PEN, "type7 0x%x", PB_T7(p));
        LOGPEN lp; CHECK(pb_getobj(p, sizeof lp, &lp) == 0x10 && lp.lopnWidth.x == 5 && lp.lopnStyle == PS_SOLID);
        pb_del(p);
    }
    SetLastError(0);
    p = UN(NtGdiExtCreatePen)(PS_COSMETIC | PS_SOLID, 5, BS_SOLID, RGB(1, 2, 3), 0, 0, 0, NULL, 0, FALSE, NULL);
    CHECK(p == NULL); EXPECT_ERR(EPERR);
    if (p) pb_del(p);
}
TEST(pens_extcreatepen_nt_matches_user) {
    REQUIRE(UN(NtGdiExtCreatePen)); PB_GDI();
    HANDLE a = UN(NtGdiExtCreatePen)(PS_GEOMETRIC | PS_DASH | PS_ENDCAP_SQUARE, 7, BS_SOLID, RGB(9, 8, 7), 0, 0, 0, NULL, 0, FALSE, NULL);
    HPEN b = ext(PS_GEOMETRIC | PS_DASH | PS_ENDCAP_SQUARE, 7, BS_SOLID, RGB(9, 8, 7), 0, 0, NULL);
    REQUIRE(a); REQUIRE(b);
    EXTLOGPEN ea, eb;
    CHECK(get_elp(a, &ea, sizeof ea) == 0x1c && get_elp(b, &eb, sizeof eb) == 0x1c);
    CHECK(!memcmp(&ea, &eb, 0x1c));
    pb_del(a); pb_del(b);
}
TEST(pens_extcreatepen_cell_state) {
    PB_GDI(); PB_TABLE();
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 4, BS_SOLID, RGB(4, 5, 6), 0, 0, NULL);
    REQUIRE(p);
    struct pb_cell c;
    CHECK(pb_cell_of(p, &c));
    CHECK(c.objt == 0x10);
    CHECK((c.owner & ~1u) == PB_PID());
    CHECK(c.upper == (unsigned short)((ULONG_PTR)p >> 16));
    CHECK(PB_T7(p) == T7_EXTPEN);
    pb_del(p);
    CHECK(pb_getobj(p, 0, NULL) == 0);
}
TEST(pens_extcreatepen_logpen_form_from_extpen_getobject) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 4, BS_SOLID, RGB(4, 5, 6), 0, 0, NULL);
    REQUIRE(p);
    LOGPEN lp;
    /* a LOGPEN-sized buffer is smaller than 0x1c, so cjGetBrushOrPen reports 0 for the extended object */
    CHECK(pb_getobj(p, sizeof lp, &lp) == 0);
    pb_del(p);
}
TEST(pens_extcreatepen_select_and_current_object) {
    PB_GDI(); REQUIRE(U(SelectObject)); REQUIRE(U(GetCurrentObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HPEN p = ext(PS_GEOMETRIC | PS_SOLID, 3, BS_SOLID, RGB(1, 2, 3), 0, 0, NULL);
    if (!p) { U(DeleteDC)(dc); REQUIRE(p); }
    HGDIOBJ def = U(GetCurrentObject)(dc, OBJ_PEN);
    CHECK(def == U(GetStockObject)(BLACK_PEN));
    HGDIOBJ old = U(SelectObject)(dc, p);
    CHECK(old == def);
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == p);
    CHECK(U(SelectObject)(dc, old) == p);
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == def);
    CHECK(pb_del(p));
    U(DeleteDC)(dc);
}
TEST(pens_select_pen_nt_matches_current) {
    PB_GDI(); REQUIRE(U(SelectObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HPEN p = U(CreatePen)(PS_DOT, 1, RGB(8, 8, 8));
    if (!p) { U(DeleteDC)(dc); REQUIRE(p); }
    HGDIOBJ old = U(SelectObject)(dc, p);
    CHECK(old == U(GetStockObject)(BLACK_PEN));
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == p);
    /* DC_ATTR holds the selected pen's handle at the offset gdi32full!SetDCBrushColor compares against ghbrDCBrush
     * for the brush (+0xa0); the pen slot follows at +0xa8 */
    void *attr = NULL; struct pb_cell c;
    if (pb_connect() && pb_cell_of(dc, &c)) attr = (void *)(ULONG_PTR)c.puser;
    if (attr) { void *sel = NULL; if (k_try_read_ptr((char *)attr + 0xa8, &sel)) t_note("DC_ATTR+0xa8 = %p (pen %p)", sel, p); }
    U(SelectObject)(dc, old);
    pb_del(p);
    U(DeleteDC)(dc);
}
TEST(pens_delete_selected_pen_behaviour) {
    PB_GDI(); REQUIRE(U(SelectObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HPEN p = U(CreatePen)(PS_SOLID, 3, RGB(8, 8, 8));
    if (!p) { U(DeleteDC)(dc); REQUIRE(p); }
    HGDIOBJ old = U(SelectObject)(dc, p);
    SetLastError(0);
    BOOL r = pb_del(p);
    t_note("DeleteObject(pen selected in a DC) = %d err %lu", r, GetLastError());
    U(SelectObject)(dc, old);
    if (!r) CHECK(pb_del(p));
    U(DeleteDC)(dc);
}
TEST(pens_stock_pens) {
    PB_GDI();
    struct { int id; COLORREF c; UINT st; } v[] = { { WHITE_PEN, RGB(255, 255, 255), PS_SOLID }, { BLACK_PEN, 0, PS_SOLID }, { NULL_PEN, 0, PS_NULL } };
    for (int i = 0; i < 3; i++) {
        HGDIOBJ h = U(GetStockObject)(v[i].id);
        CHECKF(h != NULL, "stock %d", v[i].id);
        if (!h) continue;
        CHECK(h == U(GetStockObject)(v[i].id));
        CHECK(PB_STOCKED(h));
        CHECK(U(GetObjectType)(h) == OBJ_PEN);
        LOGPEN lp;
        CHECK(pb_getobj(h, sizeof lp, &lp) == 0x10);
        CHECK(lp.lopnStyle == v[i].st);
        if (v[i].st != PS_NULL) CHECK(lp.lopnColor == v[i].c);
    }
}
TEST(pens_stock_pen_invalid_indices) {
    PB_GDI();
    static const int bad[] = { 0x16, 0x17, 100, 0x7fffffff, -1, INT_MIN };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECKF(U(GetStockObject)(bad[i]) == NULL, "index %d", bad[i]); /* gdi32full!GetStockObject: (uint)i > 0x15 -> NULL */
}
TEST(pens_dc_pen_defaults_and_set) {
    PB_GDI(); REQUIRE(U(SetDCPenColor)); REQUIRE(U(GetDCPenColor));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    CHECK(U(GetDCPenColor)(dc) == RGB(0, 0, 0)); /* documented default */
    CHECK(U(SetDCPenColor)(dc, RGB(10, 20, 30)) == RGB(0, 0, 0));
    CHECK(U(GetDCPenColor)(dc) == RGB(10, 20, 30));
    CHECK(U(SetDCPenColor)(dc, RGB(1, 2, 3)) == RGB(10, 20, 30));
    struct pb_cell c; DWORD v = 0;
    if (pb_connect() && pb_cell_of(dc, &c) && c.puser && k_try_read_u32((char *)(ULONG_PTR)c.puser + 0xcc, &v))
        CHECKF(v == RGB(1, 2, 3), "DC_ATTR+0xcc = 0x%lx", v); /* GetDCPenColor reads DC_ATTR+0xcc without a syscall */
    U(DeleteDC)(dc);
}
TEST(pens_dc_pen_invalid_dc) {
    PB_GDI(); REQUIRE(U(GetDCPenColor));
    SetLastError(0);
    CHECK(U(GetDCPenColor)(NULL) == CLR_INVALID);
    EXPECT_ERR(EPERR); /* GetDCPenColor: failed GdiGetEntry validation -> GdiSetLastError(0x57) */
    HPEN p = U(CreatePen)(PS_SOLID, 3, 0);
    REQUIRE(p);
    SetLastError(0);
    CHECK(U(GetDCPenColor)((HDC)p) == CLR_INVALID); /* a pen handle is not a DC cell (Objt != 1) */
    EXPECT_ERR(EPERR);
    pb_del(p);
}
TEST(pens_dc_pen_select_stock_dc_pen) {
    PB_GDI(); REQUIRE(U(SelectObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HGDIOBJ dcpen = U(GetStockObject)(DC_PEN);
    CHECK(dcpen != NULL);
    HGDIOBJ old = U(SelectObject)(dc, dcpen);
    CHECK(old == U(GetStockObject)(BLACK_PEN));
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == dcpen);
    U(SelectObject)(dc, old);
    U(DeleteDC)(dc);
}
TEST(pens_nt_getobject_matches_gdi32) {
    REQUIRE(UN(NtGdiExtGetObjectW)); PB_GDI();
    HPEN p = ext(PS_GEOMETRIC | PS_DOT, 5, BS_SOLID, RGB(1, 2, 3), 0, 0, NULL);
    REQUIRE(p);
    unsigned char a[0x40], b[0x40];
    memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    CHECK(UN(NtGdiExtGetObjectW)(p, sizeof a, a) == 0x1c);
    CHECK(pb_getobj(p, sizeof b, b) == 0x1c);
    CHECK(!memcmp(a, b, 0x1c));
    CHECK(UN(NtGdiExtGetObjectW)(p, 0, NULL) == 0x1c);
    CHECK(UN(NtGdiExtGetObjectW)(p, 0x1b, a) == 0);
    pb_del(p);
}
TEST(pens_nt_getobject_old_pen_null_buffer) {
    REQUIRE(UN(NtGdiExtGetObjectW)); PB_GDI();
    HPEN p = U(CreatePen)(PS_SOLID, 2, 0);
    REQUIRE(p);
    /* kernel answer for a LOGPEN-form pen with no buffer: cjGetBrushOrPen returns 0x10 */
    CHECK(UN(NtGdiExtGetObjectW)(p, 0, NULL) == 0x10);
    CHECK(UN(NtGdiExtGetObjectW)(p, 0x10, NULL) == 0x10);
    pb_del(p);
}
TEST(pens_getobject_wrong_type_handles) {
    PB_GDI();
    LOGPEN lp;
    SetLastError(0);
    CHECK(pb_getobj(NULL, sizeof lp, &lp) == 0);
    CHECK(pb_getobj((HGDIOBJ)(ULONG_PTR)0x12345, sizeof lp, &lp) == 0);
    CHECK(pb_getobj((HGDIOBJ)(ULONG_PTR)0xFFFFFFFF, sizeof lp, &lp) == 0);
}
TEST(pens_extcreatepen_many_handles_distinct) {
    PB_GDI(); REQUIRE(U(ExtCreatePen));
    enum { N = 64 };
    HPEN h[N];
    for (int i = 0; i < N; i++) { h[i] = ext(PS_GEOMETRIC | PS_SOLID, 2 + i, BS_SOLID, RGB(i, 0, 0), 0, 0, NULL); REQUIRE(h[i]); }
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < i; j++) CHECK(h[i] != h[j] && PB_IDX(h[i]) != PB_IDX(h[j]));
        EXTLOGPEN e; CHECK(get_elp(h[i], &e, sizeof e) == 0x1c && e.elpWidth == (DWORD)(2 + i) && e.elpColor == RGB(i, 0, 0));
    }
    for (int i = 0; i < N; i++) pb_del(h[i]);
}
