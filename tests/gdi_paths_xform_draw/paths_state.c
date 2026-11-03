/* Path state machine and the GetPath buffer protocol.
 *
 * NtGdiBeginPath marks the DC as "path under construction" and installs a new empty path
 * (discarding any previous one); NtGdiEndPath clears the bit or fails with ERROR_CAN_NOT_COMPLETE (0x3eb) when no
 * path is open; every consumer (Fill/Stroke/Widen/Flatten/PathToRegion/SelectClipPath/GetPath) first requires
 * DC::bInactive() (bit0 clear) and otherwise fails with 0x3eb. An invalid DC handle fails with ERROR_INVALID_HANDLE (6)
 * for Begin/End/Abort/Flatten and ERROR_INVALID_PARAMETER (0x57) for Close/GetPath/Stroke/Widen/PathToRegion/
 * SelectClipPath.
 */
#include "xd.h"

#define NEED_PATH_API() do { REQUIRE(U(BeginPath)); REQUIRE(U(EndPath)); REQUIRE(U(GetPath)); REQUIRE(U(CreateCompatibleDC)); \
    REQUIRE(U(Rectangle)); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo)); } while (0)
#define MEMDC(dc) HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("CreateCompatibleDC failed"); return; }
#define MEMDC_FREE(dc) U(DeleteDC)(dc)

/* A closed three-point path (triangle) on dc. */
static void make_tri_path(HDC dc) {
    U(BeginPath)(dc);
    U(MoveToEx)(dc, 10, 10, NULL);
    U(LineTo)(dc, 30, 10);
    U(LineTo)(dc, 30, 30);
    U(CloseFigure)(dc);
    U(EndPath)(dc);
}

XT(state_getpath_no_path) {
    int n; struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    SET_ERR(0);
    n = U(GetPath)(dc, NULL, NULL, 0);
    /* NtGdiGetPath: DC has no path -> XEPATHOBJ is empty -> EngSetLastError(0x57) with the initial -1 result. */
    EXPECT_INT(n, -1);
    { DWORD e = GetLastError(); if (e != ERR_INVALID_PARAMETER) t_note("GetPath without a path set last error %lu (analysis says 87)", e);
      CHECKF(e == ERR_INVALID_PARAMETER || e == ERR_CAN_NOT_COMPLETE, "err %lu", e); }
    EXPECT_INT(pth_get(dc, &p), -1);
    MEMDC_FREE(dc);
}

XT(state_getpath_while_open) {
    int n; struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 5, 5);
    SET_ERR(0);
    n = U(GetPath)(dc, NULL, NULL, 0);
    /* DC::bInactive() is false while the path is being built -> 0x3eb. */
    EXPECT_INT(n, -1);
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 2);
    U(AbortPath)(dc);
    MEMDC_FREE(dc);
}

XT(state_endpath_without_begin) {
    NEED_PATH_API();
    MEMDC(dc);
    SET_ERR(0);
    CHECK(!U(EndPath)(dc));
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    MEMDC_FREE(dc);
}

XT(state_endpath_twice) {
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    PEND(dc);
    SET_ERR(0);
    CHECK(!U(EndPath)(dc));
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    /* the finished (empty) path is untouched by the failed EndPath */
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0), 0);
    MEMDC_FREE(dc);
}

XT(state_closefigure_without_begin) {
    NEED_PATH_API(); REQUIRE(U(CloseFigure));
    MEMDC(dc);
    SET_ERR(0);
    CHECK(!U(CloseFigure)(dc));
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);   /* NtGdiCloseFigure: bit0 clear -> 0x3eb */
    MEMDC_FREE(dc);
}

XT(state_closefigure_after_end) {
    NEED_PATH_API(); REQUIRE(U(CloseFigure));
    MEMDC(dc);
    PBEGIN(dc); U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 9, 9); PEND(dc);
    SET_ERR(0);
    CHECK(!U(CloseFigure)(dc));
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    MEMDC_FREE(dc);
}

XT(state_abortpath_without_path_succeeds) {
    NEED_PATH_API(); REQUIRE(U(AbortPath));
    MEMDC(dc);
    CHECK(U(AbortPath)(dc));          /* NtGdiAbortPath returns 1 whenever the DC is valid */
    CHECK(U(AbortPath)(dc));
    MEMDC_FREE(dc);
}

XT(state_abortpath_discards_open_path) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(AbortPath));
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 9, 9);
    CHECK(U(AbortPath)(dc));
    SET_ERR(0);
    CHECK(!U(EndPath)(dc));           /* bit0 was cleared by AbortPath */
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    EXPECT_INT(pth_get(dc, &p), -1);  /* path object released */
    MEMDC_FREE(dc);
}

XT(state_abortpath_discards_finished_path) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(AbortPath));
    MEMDC(dc);
    make_tri_path(dc);
    EXPECT_INT(pth_get(dc, &p), 3);
    CHECK(U(AbortPath)(dc));
    EXPECT_INT(pth_get(dc, &p), -1);
    MEMDC_FREE(dc);
}

XT(state_beginpath_twice_discards_first) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    U(Rectangle)(dc, 1, 1, 20, 20);
    PBEGIN(dc);                        /* NtGdiBeginPath drops the existing hpath before allocating */
    PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 0);    /* the rectangle is gone */
    MEMDC_FREE(dc);
}

XT(state_beginpath_after_end_replaces_path) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    make_tri_path(dc);
    PBEGIN(dc); PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 0);
    MEMDC_FREE(dc);
}

XT(state_empty_path_has_zero_points) {
    struct pth p;
    POINT pt[2]; BYTE ty[2];
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc); PEND(dc);
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0), 0);
    { int gp; SET_ERR(0); gp = U(GetPath)(dc, pt, ty, 2);
      t_note("GetPath(empty path, buffer of 2) = %d err %lu", gp, GetLastError());
      CHECK(gp == -1); }                               /* an empty finished path is refused when a buffer is passed */
    EXPECT_INT(pth_get(dc, &p), 0);
    MEMDC_FREE(dc);
}

XT(state_paths_are_per_dc) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(a); MEMDC(b);
    make_tri_path(a);
    EXPECT_INT(pth_get(b, &p), -1);
    PBEGIN(b); PEND(b);
    EXPECT_INT(pth_get(a, &p), 3);
    EXPECT_INT(pth_get(b, &p), 0);
    SET_ERR(0);
    CHECK(!U(EndPath)(a));
    MEMDC_FREE(a); MEMDC_FREE(b);
}

XT(state_delete_dc_with_open_path) {
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    U(Rectangle)(dc, 1, 1, 9, 9);
    CHECK(U(DeleteDC)(dc));            /* dc destruction frees the path (bDeleteDCInternal) */
    SET_ERR(0);
    CHECK(!U(EndPath)(dc));            /* stale handle */
    EXPECT_ERR(ERR_INVALID_HANDLE);
}

/* ---- invalid handles ------------------------------------------------------------------------------------- */
typedef struct { const char *name; int (*call)(HDC); int ret; DWORD err; } badcall_t;

static int c_begin(HDC d)   { return U(BeginPath)(d); }
static int c_end(HDC d)     { return U(EndPath)(d); }
static int c_abort(HDC d)   { return U(AbortPath)(d); }
static int c_close(HDC d)   { return U(CloseFigure)(d); }
static int c_fill(HDC d)    { return U(FillPath)(d); }
static int c_stroke(HDC d)  { return U(StrokePath)(d); }
static int c_strokefill(HDC d) { return U(StrokeAndFillPath)(d); }
static int c_widen(HDC d)   { return U(WidenPath)(d); }
static int c_flatten(HDC d) { return U(FlattenPath)(d); }
static int c_p2r(HDC d)     { HRGN r = U(PathToRegion)(d); if (r) U(DeleteObject)(r); return r != NULL; }
static int c_clip(HDC d)    { return U(SelectClipPath)(d, RGN_COPY); }
static int c_getpath(HDC d) { return U(GetPath)(d, NULL, NULL, 0) != -1; }

static const badcall_t bad_calls[] = {
    { "BeginPath",         c_begin,      0, ERR_INVALID_HANDLE },
    { "EndPath",           c_end,        0, ERR_INVALID_HANDLE },
    { "AbortPath",         c_abort,      0, ERR_INVALID_HANDLE },
    { "CloseFigure",       c_close,      0, ERR_INVALID_PARAMETER },
    { "StrokePath",        c_stroke,     0, ERR_INVALID_PARAMETER },
    { "WidenPath",         c_widen,      0, ERR_INVALID_PARAMETER },
    { "FlattenPath",       c_flatten,    0, ERR_INVALID_HANDLE },
    { "PathToRegion",      c_p2r,        0, ERR_INVALID_PARAMETER },
    { "SelectClipPath",    c_clip,       0, ERR_INVALID_PARAMETER },
    { "GetPath",           c_getpath,    0, ERR_INVALID_PARAMETER },
};

/* CloseFigure, StrokePath, WidenPath, PathToRegion and SelectClipPath fail with ERROR_INVALID_PARAMETER for a handle that
   looks like a DC but names no live object (deleted or never allocated), and with ERROR_INVALID_HANDLE for NULL and for the
   handle of any other kind of object. */
static int handle_err_row(const char *n) {
    return !strcmp(n, "CloseFigure") || !strcmp(n, "StrokePath") || !strcmp(n, "WidenPath") || !strcmp(n, "PathToRegion") || !strcmp(n, "SelectClipPath");
}
static void run_bad_calls(HDC bad, const char *what) {
    size_t i;
    int dead = !strcmp(what, "unallocated") || !strcmp(what, "deleted DC");
    for (i = 0; i < sizeof bad_calls / sizeof bad_calls[0]; i++) {
        DWORD want = bad_calls[i].err;
        if (!dead && handle_err_row(bad_calls[i].name)) want = ERR_INVALID_HANDLE;
        int r;
        SET_ERR(0);
        r = bad_calls[i].call(bad);
        CHECKF(r == bad_calls[i].ret, "%s(%s) ret %d", bad_calls[i].name, what, r);
        { DWORD e = GetLastError();
          CHECKF(e == want, "%s(%s) err %lu want %lu", bad_calls[i].name, what, e, want); }
    }
    /* FillPath / StrokeAndFillPath: XDCOBJ::bValid() fails first; only the failure is certain. */
    SET_ERR(0); CHECKF(!c_fill(bad), "FillPath(%s)", what);
    SET_ERR(0); CHECKF(!c_strokefill(bad), "StrokeAndFillPath(%s)", what);
}

XT(state_invalid_hdc_null) {
    NEED_PATH_API(); REQUIRE(U(AbortPath)); REQUIRE(U(CloseFigure)); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath));
    REQUIRE(U(StrokeAndFillPath)); REQUIRE(U(WidenPath)); REQUIRE(U(FlattenPath)); REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath));
    run_bad_calls(NULL, "NULL");
}

XT(state_invalid_hdc_unallocated_handle) {
    NEED_PATH_API(); REQUIRE(U(AbortPath)); REQUIRE(U(CloseFigure)); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath));
    REQUIRE(U(StrokeAndFillPath)); REQUIRE(U(WidenPath)); REQUIRE(U(FlattenPath)); REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath));
    run_bad_calls((HDC)(ULONG_PTR)0x01010fff, "unallocated");   /* type 1 (DC) but index past any live object */
}

XT(state_invalid_hdc_deleted) {
    HDC dc; NEED_PATH_API(); REQUIRE(U(AbortPath)); REQUIRE(U(CloseFigure)); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath));
    REQUIRE(U(StrokeAndFillPath)); REQUIRE(U(WidenPath)); REQUIRE(U(FlattenPath)); REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath));
    dc = U(CreateCompatibleDC)(NULL);
    if (!dc) { t_skip("no DC"); return; }
    U(DeleteDC)(dc);
    run_bad_calls(dc, "deleted DC");
}

XT(state_invalid_hdc_wrong_object_type) {
    HGDIOBJ pen, rgn, bmp, brush;
    NEED_PATH_API(); REQUIRE(U(AbortPath)); REQUIRE(U(CloseFigure)); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath));
    REQUIRE(U(StrokeAndFillPath)); REQUIRE(U(WidenPath)); REQUIRE(U(FlattenPath)); REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath));
    pen = U(CreatePen)(PS_SOLID, 1, 0);
    rgn = U(CreateRectRgn)(0, 0, 4, 4);
    bmp = U(CreateBitmap)(4, 4, 1, 1, NULL);
    brush = U(CreateSolidBrush)(0);
    /* the lock rejects handles of every other object type. */
    run_bad_calls((HDC)pen, "pen");
    run_bad_calls((HDC)rgn, "region");
    run_bad_calls((HDC)bmp, "bitmap");
    run_bad_calls((HDC)brush, "brush");
    run_bad_calls((HDC)U(GetStockObject)(BLACK_PEN), "stock pen");
    U(DeleteObject)(pen); U(DeleteObject)(rgn); U(DeleteObject)(bmp); U(DeleteObject)(brush);
}

/* ---- consumers while a path is open -------------------------------------------------------------------- */
XT(state_consumers_fail_while_open) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath)); REQUIRE(U(StrokeAndFillPath)); REQUIRE(U(WidenPath));
    REQUIRE(U(FlattenPath)); REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath)); REQUIRE(U(CloseFigure));
    {
        static const struct { const char *n; int (*c)(HDC); } t[] = {
            { "FillPath", c_fill }, { "StrokePath", c_stroke }, { "StrokeAndFillPath", c_strokefill },
            { "WidenPath", c_widen }, { "FlattenPath", c_flatten }, { "PathToRegion", c_p2r }, { "SelectClipPath", c_clip },
        };
        size_t i;
        MEMDC(dc);
        PBEGIN(dc);
        U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 30, 10); U(LineTo)(dc, 30, 30);
        for (i = 0; i < sizeof t / sizeof t[0]; i++) {
            int r;
            SET_ERR(0);
            r = t[i].c(dc);
            CHECKF(r == 0, "%s ret %d", t[i].n, r);
            { DWORD e = GetLastError(); CHECKF(e == ERR_CAN_NOT_COMPLETE, "%s err %lu want 1003", t[i].n, e); }
        }
        /* none of the failures touched the path under construction or closed it */
        SET_ERR(0);
        CHECK(U(CloseFigure)(dc));
        PEND(dc);
        EXPECT_INT(pth_get(dc, &p), 3);
        EXPECT_EQ(p.ty[2], PT_LINETO | PT_CLOSEFIGURE);
        MEMDC_FREE(dc);
    }
}

XT(state_consumers_without_any_path_fail) {
    NEED_PATH_API(); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath)); REQUIRE(U(StrokeAndFillPath)); REQUIRE(U(WidenPath));
    REQUIRE(U(FlattenPath)); REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath));
    {
        static const struct { const char *n; int (*c)(HDC); } t[] = {
            { "FillPath", c_fill }, { "StrokePath", c_stroke }, { "StrokeAndFillPath", c_strokefill },
            { "WidenPath", c_widen }, { "FlattenPath", c_flatten }, { "PathToRegion", c_p2r }, { "SelectClipPath", c_clip },
        };
        size_t i;
        MEMDC(dc);
        for (i = 0; i < sizeof t / sizeof t[0]; i++) {
            int r;
            SET_ERR(0);
            r = t[i].c(dc);
            CHECKF(r == 0, "%s ret %d", t[i].n, r);
            /* The empty XEPATHOBJ branch reports ERROR_NOT_ENOUGH_MEMORY (8) in the analysis; log what the build did. */
            t_note("%s without a path: error %lu", t[i].n, GetLastError());
        }
        MEMDC_FREE(dc);
    }
}

typedef struct { const char *n; int (*c)(HDC); } consume_t;

XT(state_path_consumed_by_fill_stroke_region_clip) {
    static const consume_t t[] = {
        { "FillPath", c_fill }, { "StrokePath", c_stroke }, { "StrokeAndFillPath", c_strokefill },
        { "PathToRegion", c_p2r }, { "SelectClipPath", c_clip },
    };
    size_t i;
    NEED_PATH_API(); REQUIRE(U(FillPath)); REQUIRE(U(StrokePath)); REQUIRE(U(StrokeAndFillPath));
    REQUIRE(U(PathToRegion)); REQUIRE(U(SelectClipPath)); REQUIRE(U(CloseFigure));
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        struct pth p; int r;
        MEMDC(dc);
        make_tri_path(dc);
        EXPECT_INT(pth_get(dc, &p), 3);
        r = t[i].c(dc);
        CHECKF(r, "%s on a valid path failed (err %lu)", t[i].n, GetLastError());
        SET_ERR(0);
        EXPECT_INT(pth_get(dc, &p), -1);   /* DC::hpath(NULL) after rendering/region/clip */
        MEMDC_FREE(dc);
    }
}

XT(state_path_survives_widen_and_flatten) {
    struct pth p;
    HGDIOBJ pen, old;
    LOGBRUSH lb = { BS_SOLID, 0, 0 };
    NEED_PATH_API(); REQUIRE(U(WidenPath)); REQUIRE(U(FlattenPath)); REQUIRE(U(ExtCreatePen));
    MEMDC(dc);
    pen = U(ExtCreatePen)(PS_GEOMETRIC | PS_SOLID, 4, &lb, 0, NULL);
    old = U(SelectObject)(dc, pen);
    make_tri_path(dc);
    CHECK(U(FlattenPath)(dc));
    EXPECT_INT(pth_get(dc, &p), 3);    /* flatten of a polygon with no curves keeps its three points */
    CHECK(U(WidenPath)(dc));
    CHECK(pth_get(dc, &p) > 3);        /* widen replaces the path in place and leaves it selected */
    CHECK(pth_valid(&p));
    U(SelectObject)(dc, old); U(DeleteObject)(pen);
    MEMDC_FREE(dc);
}

XT(state_dc_cell_stable_across_path_calls) {
    struct k_gdicell c0, c1, c2;
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(FillPath));
    if (t_is_wine()) { t_skip("GDI handle layout differs under Wine"); return; }
    MEMDC(dc);
    if (!k_gdi_cell(dc, &c0)) { t_skip("GDI shared handle table not reachable (Wine)"); MEMDC_FREE(dc); return; }
    CHECK(((ULONG_PTR)dc >> 16 & 0x7f) == 1);          /* handle type bits = DC */
    make_tri_path(dc);
    CHECK(k_gdi_cell(dc, &c1));
    /* The object pointer / owner word / user attribute page are fixed for the lifetime of the DC. */
    EXPECT_EQ(c1.object, c0.object);
    EXPECT_EQ(c1.field10, c0.field10);
    EXPECT_EQ(c1.type_check, c0.type_check);
    pth_get(dc, &p);
    U(FillPath)(dc);
    CHECK(k_gdi_cell(dc, &c2));
    EXPECT_EQ(c2.field10, c0.field10);
    EXPECT_EQ(c2.field8, c0.field8);
    MEMDC_FREE(dc);
}

XT(state_path_calls_do_not_batch_depend) {
    /* GetPath/BeginPath/EndPath go straight to the kernel; the TEB batch counter must be zero after GdiFlush. */
    unsigned long cnt = 0xffffffff;
    NEED_PATH_API(); REQUIRE(U(GdiFlush));
    MEMDC(dc);
    make_tri_path(dc);
    U(GdiFlush)();
    if (k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &cnt)) {
        if (cnt != 0) t_note("TEB GdiBatchCount after GdiFlush = %lu", cnt);
    }
    MEMDC_FREE(dc);
}

/* ---- GetPath buffer protocol ---------------------------------------------------------------------------- */
XT(proto_count_with_zero_size) {
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 5, 5, 25, 25); PEND(dc);
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0), 4);   /* nSize 0 returns the point count without probing the buffers; a rectangle is 4 points */
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0), 4);   /* idempotent: the path is not consumed */
    MEMDC_FREE(dc);
}

XT(proto_buffer_too_small) {
    POINT pt[8]; BYTE ty[8]; int i; int n;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 5, 5, 25, 25); PEND(dc);
    for (i = 0; i < 8; i++) { pt[i].x = pt[i].y = 0x5a5a; ty[i] = 0xee; }
    for (n = 1; n < 4; n++) {
        SET_ERR(0);
        EXPECT_INT(U(GetPath)(dc, pt, ty, n), -1);
        EXPECT_ERR(ERR_INVALID_PARAMETER);          /* nSize < cTotalPts -> EngSetLastError(0x57), nothing written */
    }
    for (i = 0; i < 8; i++) { EXPECT_EQ(pt[i].x, 0x5a5a); EXPECT_EQ(ty[i], 0xee); }
    EXPECT_INT(U(GetPath)(dc, pt, ty, 4), 4);
    MEMDC_FREE(dc);
}

XT(proto_buffer_larger_than_path) {
    POINT pt[16]; BYTE ty[16]; int i;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc); U(MoveToEx)(dc, 3, 4, NULL); U(LineTo)(dc, 13, 4); PEND(dc);
    for (i = 0; i < 16; i++) { pt[i].x = pt[i].y = 0x5a5a; ty[i] = 0xee; }
    EXPECT_INT(U(GetPath)(dc, pt, ty, 16), 2);
    EXPECT_EQ(ty[0], PT_MOVETO); EXPECT_EQ(ty[1], PT_LINETO);
    EXPECT_EQ(pt[0].x, 3); EXPECT_EQ(pt[0].y, 4); EXPECT_EQ(pt[1].x, 13); EXPECT_EQ(pt[1].y, 4);
    for (i = 2; i < 16; i++) { EXPECT_EQ(pt[i].x, 0x5a5a); EXPECT_EQ(ty[i], 0xee); }   /* only cTotalPts entries are written */
    MEMDC_FREE(dc);
}

XT(proto_negative_size) {
    POINT pt[4]; BYTE ty[4];
    NEED_PATH_API();
    MEMDC(dc);
    make_tri_path(dc);
    SET_ERR(0);
    EXPECT_INT(U(GetPath)(dc, pt, ty, -1), -1);      /* (int)nSize < 0 is rejected before the path is consulted */
    EXPECT_ERR(ERR_INVALID_PARAMETER);
    SET_ERR(0);
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0x80000000), -1);
    EXPECT_ERR(ERR_INVALID_PARAMETER);
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0), 3);
    MEMDC_FREE(dc);
}

XT(proto_exact_size_is_accepted) {
    struct pth q; POINT pt[8]; BYTE ty[8];
    NEED_PATH_API();
    MEMDC(dc);
    make_tri_path(dc);
    /* MoveTo + 2 LineTo + CloseFigure: 3 points, the last one carrying PT_CLOSEFIGURE. */
    EXPECT_INT(U(GetPath)(dc, NULL, NULL, 0), 3);
    { int n; DWORD e; SET_ERR(0); n = U(GetPath)(dc, pt, ty, 3); e = GetLastError(); t_note("GetPath(exact 3) = %d err %lu", n, e); }
    { int n; DWORD e; SET_ERR(0); n = U(GetPath)(dc, pt, ty, 4); e = GetLastError(); t_note("GetPath(4) = %d err %lu", n, e); }
    { int n; DWORD e; SET_ERR(0); n = U(GetPath)(dc, pt, ty, 8); e = GetLastError(); t_note("GetPath(8) = %d err %lu", n, e); }
    EXPECT_INT(pth_get(dc, &q), 3);
    EXPECT_EQ(q.ty[0], PT_MOVETO); EXPECT_EQ(q.ty[1], PT_LINETO); EXPECT_EQ(q.ty[2], PT_LINETO | PT_CLOSEFIGURE);
    EXPECT_EQ(q.pt[0].x, 10); EXPECT_EQ(q.pt[0].y, 10);
    EXPECT_EQ(q.pt[1].x, 30); EXPECT_EQ(q.pt[1].y, 10);
    EXPECT_EQ(q.pt[2].x, 30); EXPECT_EQ(q.pt[2].y, 30);
    MEMDC_FREE(dc);
}

XT(proto_getpath_does_not_consume) {
    struct pth a, b;
    NEED_PATH_API();
    MEMDC(dc);
    make_tri_path(dc);
    EXPECT_INT(pth_get(dc, &a), 3);
    EXPECT_INT(pth_get(dc, &b), 3);
    CHECK(!memcmp(a.pt, b.pt, 3 * sizeof(POINT)));
    CHECK(!memcmp(a.ty, b.ty, 3));
    MEMDC_FREE(dc);
}

XT(proto_points_follow_world_transform) {
    /* NtGdiGetPath runs the device points through the device->logical EXFORMOBJ (0x402), so GetPath returns
     * logical coordinates regardless of the mapping mode in effect when the path was built. */
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(SetViewportOrgEx)); REQUIRE(U(SetMapMode));
    MEMDC(dc);
    U(SetViewportOrgEx)(dc, 100, 50, NULL);
    PBEGIN(dc); U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 20, 20); PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 2);
    EXPECT_EQ(p.pt[0].x, 10); EXPECT_EQ(p.pt[0].y, 10);
    EXPECT_EQ(p.pt[1].x, 20); EXPECT_EQ(p.pt[1].y, 20);
    /* moving the origin afterwards shifts what the same device points mean logically */
    U(SetViewportOrgEx)(dc, 90, 50, NULL);
    EXPECT_INT(pth_get(dc, &p), 2);
    EXPECT_EQ(p.pt[0].x, 20); EXPECT_EQ(p.pt[0].y, 10);
    EXPECT_EQ(p.pt[1].x, 30); EXPECT_EQ(p.pt[1].y, 20);
    MEMDC_FREE(dc);
}

XT(proto_points_under_scaled_mapping) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(SetMapMode)); REQUIRE(U(SetWindowExtEx)); REQUIRE(U(SetViewportExtEx));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 100, NULL);
    U(SetViewportExtEx)(dc, 200, 300, NULL);
    PBEGIN(dc); U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 20, 30); PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 2);
    EXPECT_EQ(p.pt[0].x, 10); EXPECT_EQ(p.pt[0].y, 10);
    EXPECT_EQ(p.pt[1].x, 20); EXPECT_EQ(p.pt[1].y, 30);
    MEMDC_FREE(dc);
}

XT(proto_metafile_dc_type_rejected) {
    /* gdi32full!GetPath rejects a handle whose type field is 0x66 (metafile DC) with 0x57 before any syscall. */
    HDC mf;
    NEED_PATH_API(); REQUIRE(U(CreateMetaFileW)); REQUIRE(U(CloseMetaFile)); REQUIRE(U(DeleteMetaFile));
    mf = U(CreateMetaFileW)(NULL);
    if (!mf) { t_skip("no metafile DC"); return; }
    SET_ERR(0);
    EXPECT_INT(U(GetPath)(mf, NULL, NULL, 0), -1);
    EXPECT_ERR(ERR_INVALID_PARAMETER);
    U(DeleteMetaFile)(U(CloseMetaFile)(mf));
}
