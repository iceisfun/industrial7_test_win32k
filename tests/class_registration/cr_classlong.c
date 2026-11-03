/* Get/SetClassLong, GetClassLongPtr/SetClassLongPtr, GetClassWord/SetClassWord and the matching syscalls
 * NtUserSetClassLong / NtUserSetClassLongPtr / NtUserSetClassWord.
 * Reads are client side (user32 GetClassLongPtr / GetClassWord over the desktop-heap CLS
 * image); writes are kernel side (the kernel class-data setters).
 * Error numbers: 0x585 ERROR_INVALID_INDEX, 0x57, 0xd, 5 (foreign process), 1400 invalid window. */
#include "cr.h"

#define GCL_ATOM_IDX CR_GCW_ATOM

/* a class with 40 extra class bytes and a window of it */
struct fx { const WCHAR *name; HWND h; };
static int fx_make(struct fx *f, UINT style, int cbCls, int cbWnd) {
    f->name = cr_name();
    f->h = NULL;
    if (!cr_reg(f->name, style, cbCls, cbWnd)) return 0;
    f->h = cr_win(f->name);
    if (!f->h) { cr_unreg(f->name); return 0; }
    return 1;
}
static void fx_free(struct fx *f) { if (f->h) U(DestroyWindow)(f->h); cr_unreg(f->name); }
#define FX(f, st, c, w) struct fx f; if (!fx_make(&f, st, c, w)) { t_skip("cannot make class/window"); return; }

/* ----- read side ---------------------------------------------------------------------------- */

TEST(class_registration_classlong_32bit_indices) {
    CR_NEED();
    FX(f, CS_DBLCLKS | CS_HREDRAW, 24, 12);
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_STYLE), CS_DBLCLKS | CS_HREDRAW);
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_CBCLSEXTRA), 24);
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_CBWNDEXTRA), 12);
    ATOM a = (ATOM)U(GetClassLongW)(f.h, CR_GCW_ATOM);
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    CHECK(a >= 0xC000);
    CHECK(U(GetClassInfoExW)(cr_hinst(), f.name, &w));
    if (cr_nt()) { WNDCLASSEXW o; EXPECT_EQ(cr_nt_info(cr_hinst(), f.name, &o, FALSE), a); }
    EXPECT_EQ(U(GetClassWord)(f.h, CR_GCW_ATOM), a);
    fx_free(&f);
}

TEST(class_registration_classlong_pointer_sized_indices_fail_in_32bit_api) {
    CR_NEED();
    /* GetClassLong only serves indices whose table entry is <= 4 bytes; WNDPROC, HMODULE,
     * HBRBACKGROUND, MENUNAME and HICONSM are 8-byte values on x64: ERROR_INVALID_INDEX, result 0 */
    FX(f, 0, 0, 0);
    static const int idx[] = { CR_GCL_WNDPROC, CR_GCL_HMODULE, CR_GCL_HBRBACKGROUND, CR_GCL_MENUNAME, CR_GCL_HICONSM };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        SET_ERR(0);
        DWORD r = U(GetClassLongW)(f.h, idx[i]);
        CHECKF(r == 0, "index %d returned 0x%lx", idx[i], r);
        EXPECT_ERR(CR_ERR_INVALID_INDEX);
    }
    fx_free(&f);
}

TEST(class_registration_classlong_ptr_indices_roundtrip) {
    CR_NEED();
    FX(f, CS_OWNDC, 16, 8);
    SET_ERR(0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_WNDPROC), (ULONG_PTR)cr_proc);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HMODULE), (ULONG_PTR)cr_hinst());
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCL_STYLE), CS_OWNDC);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCL_CBCLSEXTRA), 16);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCL_CBWNDEXTRA), 8);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HBRBACKGROUND), 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_MENUNAME), 0);
    EXPECT_EQ(GetLastError(), 0);
    fx_free(&f);
}

TEST(class_registration_classlong_ptr_matches_class_info) {
    CR_NEED();
    HICON ic = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32515));
    HCURSOR cu = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32513));
    if (!ic || !cu) { t_skip("stock icon/cursor unavailable"); return; }
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, CS_VREDRAW, 4, 4);
    wc.hIcon = ic; wc.hCursor = cu; wc.hbrBackground = (HBRUSH)(ULONG_PTR)(COLOR_WINDOW + 1);
    REQUIRE(U(RegisterClassExW)(&wc));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    WNDCLASSEXW o; memset(&o, 0, sizeof o); o.cbSize = sizeof o;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &o));
    EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HICON), (ULONG_PTR)o.hIcon);
    EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HCURSOR), (ULONG_PTR)o.hCursor);
    EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HBRBACKGROUND), (ULONG_PTR)o.hbrBackground);
    EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_WNDPROC), (ULONG_PTR)o.lpfnWndProc);
    EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HMODULE), (ULONG_PTR)cr_hinst());
    /* GetClassLongW can read the 64-bit icon/cursor handle slots truncated: their table size is 0 */
    EXPECT_EQ(U(GetClassLongW)(h, CR_GCL_HICON), (DWORD)(ULONG_PTR)ic);
    EXPECT_EQ(U(GetClassLongW)(h, CR_GCL_HCURSOR), (DWORD)(ULONG_PTR)cu);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_classlong_invalid_window) {
    CR_NEED();
    static const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xdead0000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECK(U(GetClassLongW)((HWND)bad[i], GCL_STYLE) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SET_ERR(0);
        CHECK(U(GetClassLongPtrW)((HWND)bad[i], GCLP_WNDPROC) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SET_ERR(0);
        CHECK(U(GetClassWord)((HWND)bad[i], 0) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
}

TEST(class_registration_classlong_destroyed_window_invalid) {
    CR_NEED();
    FX(f, 0, 8, 0);
    HWND h = f.h;
    U(DestroyWindow)(h);
    f.h = NULL;
    SET_ERR(0);
    CHECK(U(GetClassLongW)(h, GCL_STYLE) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    if (UN(NtUserSetClassLongPtr)) {
        SET_ERR(0);
        CHECK(UN(NtUserSetClassLongPtr)(h, 0, 1, FALSE) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
    fx_free(&f);
}

/* ----- class extra bytes: bounds --------------------------------------------------------------- */

TEST(class_registration_extra_ptr_bounds_matrix) {
    CR_NEED();
    FX(f, 0, 40, 0);
    /* GetClassLongPtr / SetClassLongPtr accept offset when offset + 8 <= cbClsExtra */
    static const struct { int off; int ok; } v[] = { { 0, 1 }, { 1, 1 }, { 4, 1 }, { 8, 1 }, { 24, 1 }, { 31, 1 }, { 32, 1 },
        { 33, 0 }, { 36, 0 }, { 39, 0 }, { 40, 0 }, { 41, 0 }, { 1000, 0 }, { 0x7ffffff8, 0 } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        SET_ERR(0);
        ULONG_PTR r = U(GetClassLongPtrW)(f.h, v[i].off);
        if (v[i].ok) { CHECKF(r == 0 && GetLastError() == 0, "get off %d -> 0x%llx err %lu", v[i].off, (unsigned long long)r, GetLastError()); }
        else { CHECKF(r == 0, "get off %d -> 0x%llx", v[i].off, (unsigned long long)r); EXPECT_ERR(CR_ERR_INVALID_INDEX); }
        SET_ERR(0);
        r = U(SetClassLongPtrW)(f.h, v[i].off, 0);      /* writing zero over zero keeps the class intact */
        if (v[i].ok) CHECKF(GetLastError() == 0, "set off %d err %lu", v[i].off, GetLastError());
        else { CHECK(r == 0); EXPECT_ERR(CR_ERR_INVALID_INDEX); }
    }
    fx_free(&f);
}

TEST(class_registration_extra_long_bounds_matrix) {
    CR_NEED();
    FX(f, 0, 40, 0);
    /* 32-bit accessors: offset + 4 <= cbClsExtra (user32!_GetClassLong) */
    static const struct { int off; int ok; } v[] = { { 0, 1 }, { 2, 1 }, { 36, 1 }, { 37, 0 }, { 40, 0 }, { 100, 0 } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        SET_ERR(0);
        DWORD r = U(GetClassLongW)(f.h, v[i].off);
        if (v[i].ok) CHECKF(r == 0 && GetLastError() == 0, "get off %d err %lu", v[i].off, GetLastError());
        else { CHECK(r == 0); EXPECT_ERR(CR_ERR_INVALID_INDEX); }
        if (!cr_nt()) continue;
        SET_ERR(0);
        r = UN(NtUserSetClassLong)(f.h, v[i].off, 0, FALSE);
        if (v[i].ok) CHECKF(GetLastError() == 0, "nt set off %d err %lu", v[i].off, GetLastError());
        else { CHECK(r == 0); EXPECT_ERR(CR_ERR_INVALID_INDEX); }
    }
    fx_free(&f);
}

TEST(class_registration_extra_word_bounds_matrix) {
    CR_NEED();
    FX(f, 0, 10, 0);
    static const struct { int off; int ok; } v[] = { { 0, 1 }, { 1, 1 }, { 8, 1 }, { 9, 0 }, { 10, 0 }, { -1, 0 }, { -2, 0 }, { -26, 0 } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        SET_ERR(0);
        WORD r = U(GetClassWord)(f.h, v[i].off);
        if (v[i].ok) CHECKF(r == 0 && GetLastError() == 0, "get off %d err %lu", v[i].off, GetLastError());
        else { CHECK(r == 0); EXPECT_ERR(CR_ERR_INVALID_INDEX); }
        SET_ERR(0);
        r = U(SetClassWord)(f.h, v[i].off, 0);
        if (v[i].ok) CHECKF(GetLastError() == 0, "set off %d err %lu", v[i].off, GetLastError());
        else { CHECK(r == 0); EXPECT_ERR(CR_ERR_INVALID_INDEX); }
    }
    fx_free(&f);
}

TEST(class_registration_extra_zero_size_class_has_no_extra_bytes) {
    CR_NEED();
    FX(f, 0, 0, 0);
    SET_ERR(0);
    CHECK(U(GetClassLongPtrW)(f.h, 0) == 0);
    EXPECT_ERR(CR_ERR_INVALID_INDEX);
    SET_ERR(0);
    CHECK(U(GetClassLongW)(f.h, 0) == 0);
    EXPECT_ERR(CR_ERR_INVALID_INDEX);
    SET_ERR(0);
    CHECK(U(GetClassWord)(f.h, 0) == 0);
    EXPECT_ERR(CR_ERR_INVALID_INDEX);
    fx_free(&f);
}

/* ----- class extra bytes: values ------------------------------------------------------------- */

TEST(class_registration_extra_ptr_write_returns_old_value) {
    CR_NEED();
    FX(f, 0, 40, 0);
    static const ULONG_PTR vals[] = { 0x1111111122222222ull, 0xFFFFFFFFFFFFFFFFull, 0, 0x8000000000000001ull };
    ULONG_PTR prev = 0;
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        SET_ERR(0);
        ULONG_PTR old = U(SetClassLongPtrW)(f.h, 8, vals[i]);
        EXPECT_EQ(old, prev);                    /* SetClassLongPtr returns the previous qword */
        EXPECT_EQ(U(GetClassLongPtrW)(f.h, 8), vals[i]);
        prev = vals[i];
    }
    /* neighbours untouched */
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 0), 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 16), 0);
    fx_free(&f);
}

TEST(class_registration_extra_unaligned_offsets) {
    CR_NEED();
    FX(f, 0, 40, 0);
    /* the bounds test is the only constraint; an odd offset is legal */
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, 3, 0x0102030405060708ull), 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 3), 0x0102030405060708ull);
    /* the unaligned qword is visible through narrower overlapping reads (little endian) */
    EXPECT_EQ(U(GetClassWord)(f.h, 3), 0x0708);
    EXPECT_EQ(U(GetClassWord)(f.h, 5), 0x0506);
    EXPECT_EQ(U(GetClassLongW)(f.h, 7), 0x01020304);
    fx_free(&f);
}

TEST(class_registration_extra_word_long_ptr_overlay) {
    CR_NEED();
    FX(f, 0, 16, 0);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, 0, 0xAABBCCDDEEFF0011ull), 0);
    EXPECT_EQ(U(GetClassLongW)(f.h, 0), 0xEEFF0011u);
    EXPECT_EQ(U(GetClassLongW)(f.h, 4), 0xAABBCCDDu);
    EXPECT_EQ(U(GetClassWord)(f.h, 0), 0x0011);
    EXPECT_EQ(U(GetClassWord)(f.h, 6), 0xAABB);
    WORD ow = U(SetClassWord)(f.h, 2, 0x1234);
    EXPECT_EQ(ow, 0xEEFF);                       /* SetClassWord returns the old word */
    EXPECT_EQ(U(GetClassLongW)(f.h, 0), 0x12340011u);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 0), 0xAABBCCDD12340011ull);
    fx_free(&f);
}

TEST(class_registration_extra_shared_between_windows_of_class) {
    CR_NEED();
    FX(f, 0, 16, 0);
    HWND h2 = cr_win(f.name);
    if (!h2) { t_skip("second window failed"); fx_free(&f); return; }
    U(SetClassLongPtrW)(f.h, 8, 0x5555);
    /* class memory, not window memory: the second window of the class sees the write */
    EXPECT_EQ(U(GetClassLongPtrW)(h2, 8), 0x5555);
    U(SetClassLongPtrW)(h2, 0, 0x6666);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 0), 0x6666);
    U(DestroyWindow)(h2);
    fx_free(&f);
}

TEST(class_registration_extra_survives_window_destruction) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 16, 0));
    HWND a = cr_win(n);
    if (!a) { t_skip("no window"); cr_unreg(n); return; }
    U(SetClassLongPtrW)(a, 0, 0xC0FFEE);
    U(DestroyWindow)(a);
    HWND b = cr_win(n);
    if (b) { EXPECT_EQ(U(GetClassLongPtrW)(b, 0), 0xC0FFEE); U(DestroyWindow)(b); }
    cr_unreg(n);
}

TEST(class_registration_extra_nt_set_matches_user32) {
    CR_NEED_NT();
    FX(f, 0, 24, 0);
    EXPECT_EQ(UN(NtUserSetClassLongPtr)(f.h, 8, 0x1122334455667788ull, FALSE), 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 8), 0x1122334455667788ull);
    EXPECT_EQ(UN(NtUserSetClassLong)(f.h, 8, 0xdeadbeef, FALSE), 0x55667788u);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 8), 0x11223344deadbeefull);
    EXPECT_EQ(UN(NtUserSetClassWord)(f.h, 8, 0xabcd), 0xbeef);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, 8), 0x11223344deadabcdull);
    fx_free(&f);
}

TEST(class_registration_extra_client_image_mirrors_kernel_write) {
    CR_NEED();
    FX(f, 0, 32, 0);
    BYTE *cls = (BYTE *)cr_cls_client(f.h);
    if (!cls) { t_skip("client CLS image not reachable"); fx_free(&f); return; }
    /* the class extra bytes begin after the fixed class header (as read by GetClassLongPtr) */
    U(SetClassLongPtrW)(f.h, 8, 0x0123456789abcdefull);
    ULONG_PTR v = 0;
    CHECK(cr_rd(cls + 0x58 + 8, &v, 8));
    EXPECT_EQ(v, 0x0123456789abcdefull);
    U(SetClassWord)(f.h, 1, 0x7766);
    unsigned short w = 0;
    CHECK(cr_rd(cls + 0x58 + 1, &w, 2));
    EXPECT_EQ(w, 0x7766);
    fx_free(&f);
}

/* ----- CLS image fields ---------------------------------------------------------------------- */

TEST(class_registration_cls_image_fields) {
    CR_NEED();
    FX(f, CS_DBLCLKS | CS_PARENTDC, 20, 28);
    BYTE *cls = (BYTE *)cr_cls_client(f.h);
    if (!cls) { t_skip("client CLS image not reachable"); fx_free(&f); return; }
    unsigned short atomNV = 0, atom = 0, fnid = 0xffff, flags = 0;
    unsigned style = 0, cbCls = 0, cbWnd = 0;
    ULONG_PTR proc = 0, hmod = 0;
    CHECK(cr_rd(cls + 0, &atomNV, 2));
    CHECK(cr_rd(cls + 2, &atom, 2));
    CHECK(cr_rd(cls + 4, &fnid, 2));
    CHECK(cr_rd(cls + 6, &flags, 2));
    CHECK(cr_rd(cls + 8, &style, 4));
    CHECK(cr_rd(cls + 0xc, &cbCls, 4));
    CHECK(cr_rd(cls + 0x20, &proc, 8));
    CHECK(cr_rd(cls + 0x40, &hmod, 8));
    CHECK(cr_rd(cls + 0x50, &cbWnd, 4));
    EXPECT_EQ(atom, U(GetClassWord)(f.h, CR_GCW_ATOM));
    EXPECT_EQ(atomNV, atom);                     /* no activation context: version-less == plain */
    EXPECT_EQ(fnid, 0);                          /* an application class has no fnid */
    EXPECT_EQ(style, CS_DBLCLKS | CS_PARENTDC);
    EXPECT_EQ(cbCls, 20);
    EXPECT_EQ(cbWnd, 28);
    EXPECT_EQ(proc, (ULONG_PTR)cr_proc);
    EXPECT_EQ(hmod, (ULONG_PTR)cr_hinst());
    CHECK((flags & 2) == 0);                     /* Unicode window procedure: ANSI bit clear */
    fx_free(&f);
}

TEST(class_registration_cls_image_ansi_bit) {
    CR_NEED();
    const WCHAR *n = cr_name();
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    WNDCLASSEXA wa; memset(&wa, 0, sizeof wa);
    wa.cbSize = sizeof wa; wa.lpfnWndProc = cr_procA; wa.hInstance = cr_hinst(); wa.lpszClassName = an;
    REQUIRE(U(RegisterClassExA)(&wa));
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, an, "", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, cr_hinst(), NULL);
    if (!h) { t_skip("no window"); U(UnregisterClassA)(an, cr_hinst()); return; }
    BYTE *cls = (BYTE *)cr_cls_client(h);
    if (cls) {
        unsigned short flags = 0; unsigned long long proc = 0;
        CHECK(cr_rd(cls + 6, &flags, 2));
        CHECK(cr_rd(cls + 0x20, &proc, 8));
        CHECK((flags & 2) != 0);                /* RegisterClassExA passes flag 0x02: CSF_ANSIPROC */
        EXPECT_EQ(proc, (ULONG_PTR)cr_procA);
    } else t_note("client CLS image not reachable");
    /* the A accessor returns the procedure as registered, the W accessor a translation handle */
    EXPECT_EQ(U(GetClassLongPtrA)(h, GCLP_WNDPROC), (ULONG_PTR)cr_procA);
    ULONG_PTR pw = U(GetClassLongPtrW)(h, GCLP_WNDPROC);
    CHECK(pw != (ULONG_PTR)cr_procA && pw != 0);
    U(DestroyWindow)(h);
    U(UnregisterClassA)(an, cr_hinst());
}

TEST(class_registration_cls_image_system_class_fnid) {
    CR_NEED();
    static const WCHAR *const names[] = { L"Button", L"Edit", L"Static", L"ListBox", L"ComboBox", L"ScrollBar" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, names[i], L"", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, cr_hinst(), NULL);
        if (!h) { t_note("cannot create %ls", names[i]); continue; }
        BYTE *cls = (BYTE *)cr_cls_client(h);
        if (cls) {
            unsigned short fnid = 0;
            CHECK(cr_rd(cls + 4, &fnid, 2));
            /* system classes carry the FNID_ range 0x29a..0x2b8 */
            CHECKF(fnid >= 0x29a && fnid <= 0x2b8, "%ls cls fnid 0x%x", names[i], fnid);
        }
        U(DestroyWindow)(h);
    }
}

/* ----- writes: validation -------------------------------------------------------------------- */

TEST(class_registration_set_style_roundtrip) {
    CR_NEED();
    FX(f, CS_DBLCLKS, 0, 0);
    ULONG_PTR old = U(SetClassLongPtrW)(f.h, GCL_STYLE, CS_HREDRAW | CS_VREDRAW);
    EXPECT_EQ(old, CS_DBLCLKS);
    EXPECT_EQ(U(GetClassLongW)(f.h, GCL_STYLE), CS_HREDRAW | CS_VREDRAW);
    DWORD o2 = U(SetClassLongW)(f.h, GCL_STYLE, CS_OWNDC);
    EXPECT_EQ(o2, CS_HREDRAW | CS_VREDRAW);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCL_STYLE), CS_OWNDC);
    fx_free(&f);
}

TEST(class_registration_set_style_ime_bit_refused) {
    CR_NEED_NT();
    /* NtUserSetClassLong / NtUserSetClassLongPtr: GCL_STYLE with bit 16 (CS_IME) set is
     * ERROR_INVALID_DATA before the class is touched */
    FX(f, CS_DBLCLKS, 0, 0);
    SET_ERR(0);
    CHECK(UN(NtUserSetClassLong)(f.h, CR_GCL_STYLE, 0x10000 | CS_DBLCLKS, FALSE) == 0);
    EXPECT_ERR(CR_ERR_INVALID_DATA);
    SET_ERR(0);
    CHECK(UN(NtUserSetClassLongPtr)(f.h, CR_GCL_STYLE, 0x10000, FALSE) == 0);
    EXPECT_ERR(CR_ERR_INVALID_DATA);
    SET_ERR(0);
    CHECK(U(SetClassLongPtrW)(f.h, GCL_STYLE, 0x10000) == 0);
    EXPECT_ERR(CR_ERR_INVALID_DATA);
    EXPECT_EQ(U(GetClassLongW)(f.h, GCL_STYLE), CS_DBLCLKS);
    fx_free(&f);
}

TEST(class_registration_set_atom_refused) {
    CR_NEED();
    FX(f, 0, 0, 0);
    ATOM a = (ATOM)U(GetClassLongW)(f.h, CR_GCW_ATOM);
    SET_ERR(0);
    CHECK(U(SetClassLongPtrW)(f.h, CR_GCW_ATOM, 0x1234) == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);        /* the class-data setter: index -32 */
    EXPECT_EQ((ATOM)U(GetClassLongW)(f.h, CR_GCW_ATOM), a);
    SET_ERR(0);
    CHECK(U(SetClassWord)(f.h, CR_GCW_ATOM, 0x1234) == 0);
    EXPECT_ERR(CR_ERR_INVALID_INDEX);            /* SetClassWord only accepts non-negative offsets */
    EXPECT_EQ((ATOM)U(GetClassLongW)(f.h, CR_GCW_ATOM), a);
    fx_free(&f);
}

TEST(class_registration_set_cbclsextra_refused) {
    CR_NEED();
    FX(f, 0, 8, 0);
    SET_ERR(0);
    U(SetClassLongPtrW)(f.h, CR_GCL_CBCLSEXTRA, 64);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);        /* only WOW (flag 8) classes may change it */
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_CBCLSEXTRA), 8);
    fx_free(&f);
}

TEST(class_registration_set_cbwndextra_affects_new_windows) {
    CR_NEED();
    FX(f, 0, 0, 8);
    SET_ERR(0);
    U(SetClassLongPtrW)(f.h, CR_GCL_CBWNDEXTRA, 24);
    EXPECT_EQ(GetLastError(), 0);
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_CBWNDEXTRA), 24);
    HWND h2 = cr_win(f.name);
    if (h2) {
        SET_ERR(0);
        U(SetWindowLongPtrW)(h2, 16, 0x77);           /* offset 16 + 8 <= 24: legal in the new window */
        EXPECT_EQ(GetLastError(), 0);
        EXPECT_EQ(U(GetWindowLongPtrW)(h2, 16), 0x77);
        U(DestroyWindow)(h2);
    }
    /* the first window was created with 8 bytes and keeps them */
    SET_ERR(0);
    U(GetWindowLongPtrW)(f.h, 16);
    EXPECT_ERR(CR_ERR_INVALID_INDEX);
    fx_free(&f);
}

TEST(class_registration_set_unknown_negative_index_refused) {
    CR_NEED_NT();
    FX(f, 0, 8, 0);
    /* the class-data setter: negative indices outside the table (and beyond -34) are ERROR_INVALID_INDEX */
    static const int idx[] = { -1, -2, -3, -4, -5, -6, -7, -9, -11, -13, -15, -17, -19, -21, -22, -23, -25, -27, -28, -30, -31, -33, -35, -64, -1000, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        SET_ERR(0);
        ULONG_PTR r = UN(NtUserSetClassLongPtr)(f.h, idx[i], 0x1234, FALSE);
        CHECKF(r == 0, "index %d returned 0x%llx", idx[i], (unsigned long long)r);
        EXPECT_ERR(CR_ERR_INVALID_INDEX);
        SET_ERR(0);
        r = UN(NtUserSetClassLong)(f.h, idx[i], 0x1234, FALSE);
        CHECKF(r == 0, "32-bit index %d returned 0x%lx", idx[i], (unsigned long)r);
        EXPECT_ERR(CR_ERR_INVALID_INDEX);
    }
    fx_free(&f);
}

TEST(class_registration_set_long_pointer_sized_index_refused) {
    CR_NEED_NT();
    /* SetClassLong only dispatches to the class-data setter for table entries below 5 bytes: the 8-byte slots
     * (WNDPROC, HMODULE, HICON, HCURSOR, HBRBACKGROUND, MENUNAME, HICONSM) are ERROR_INVALID_INDEX through
     * the 32-bit entry point, with no change to the class */
    FX(f, 0, 0, 0);
    static const int idx[] = { CR_GCL_WNDPROC, CR_GCL_HMODULE, CR_GCL_HICON, CR_GCL_HCURSOR_, CR_GCL_HBRBACKGROUND, CR_GCL_MENUNAME, CR_GCL_HICONSM };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        SET_ERR(0);
        CHECKF(UN(NtUserSetClassLong)(f.h, idx[i], 0x1234, FALSE) == 0, "index %d", idx[i]);
        EXPECT_ERR(CR_ERR_INVALID_INDEX);
        SET_ERR(0);
        U(SetClassLongW)(f.h, idx[i], 0x1234);
        EXPECT_ERR(CR_ERR_INVALID_INDEX);
    }
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_WNDPROC), (ULONG_PTR)cr_proc);
    fx_free(&f);
}

TEST(class_registration_set_long_four_byte_indices) {
    CR_NEED_NT();
    FX(f, CS_DBLCLKS, 0, 4);
    EXPECT_EQ(UN(NtUserSetClassLong)(f.h, CR_GCL_STYLE, CS_HREDRAW, FALSE), CS_DBLCLKS);
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_STYLE), CS_HREDRAW);
    EXPECT_EQ(UN(NtUserSetClassLong)(f.h, CR_GCL_CBWNDEXTRA, 12, FALSE), 4);
    EXPECT_EQ(U(GetClassLongW)(f.h, CR_GCL_CBWNDEXTRA), 12);
    SET_ERR(0);
    CHECK(UN(NtUserSetClassLong)(f.h, CR_GCL_CBCLSEXTRA, 12, FALSE) == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(UN(NtUserSetClassLong)(f.h, CR_GCW_ATOM, 12, FALSE) == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    fx_free(&f);
}

TEST(class_registration_set_invalid_window_refused) {
    CR_NEED_NT();
    static const ULONG_PTR bad[] = { 0, 2, 0x4242, 0xbad00000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECK(UN(NtUserSetClassLongPtr)((HWND)bad[i], 0, 1, FALSE) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SET_ERR(0);
        CHECK(UN(NtUserSetClassLong)((HWND)bad[i], 0, 1, FALSE) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SET_ERR(0);
        CHECK(UN(NtUserSetClassWord)((HWND)bad[i], 0, 1) == 0);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
}

TEST(class_registration_set_foreign_window_access_denied) {
    CR_NEED_NT();
    /* SetClassLongPtr / SetClassWord compare the window's process with the caller's first */
    HWND d = GetDesktopWindow();
    static const int idx[] = { 0, CR_GCL_WNDPROC, CR_GCL_HICON, CR_GCL_HBRBACKGROUND, CR_GCL_CBWNDEXTRA };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        SET_ERR(0);
        CHECK(UN(NtUserSetClassLongPtr)(d, idx[i], 0, FALSE) == 0);
        EXPECT_ERR(CR_ERR_ACCESS_DENIED);
        SET_ERR(0);
        CHECK(UN(NtUserSetClassLong)(d, idx[i], 0, FALSE) == 0);
        EXPECT_ERR(CR_ERR_ACCESS_DENIED);
    }
    SET_ERR(0);
    CHECK(UN(NtUserSetClassWord)(d, 0, 0) == 0);
    EXPECT_ERR(CR_ERR_ACCESS_DENIED);
    SET_ERR(0);
    CHECK(U(SetClassLongPtrW)(d, GCL_STYLE, 0) == 0);
    EXPECT_ERR(CR_ERR_ACCESS_DENIED);
}

TEST(class_registration_get_foreign_window_class_readable) {
    CR_NEED();
    HWND d = GetDesktopWindow();
    /* reading a foreign window's class goes through the shared desktop heap: no access check */
    SET_ERR(0);
    DWORD st = U(GetClassLongW)(d, GCL_STYLE);
    EXPECT_EQ(GetLastError(), 0);
    t_note("desktop class style 0x%lx atom 0x%x", st, U(GetClassWord)(d, CR_GCW_ATOM));
    EXPECT_EQ(U(GetClassLongW)(d, CR_GCL_CBCLSEXTRA), 0);
}

TEST(class_registration_set_brush_validation) {
    CR_NEED();
    FX(f, 0, 0, 0);
    /* user32!SetClassLongPtrW: index -10 with a value above 0x1e that GdiValidateHandle rejects is ERROR_INVALID_HANDLE */
    SET_ERR(0);
    CHECK(U(SetClassLongPtrW)(f.h, CR_GCL_HBRBACKGROUND, 0x77770) == 0);
    EXPECT_ERR(6);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, CR_GCL_HBRBACKGROUND), 0);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, CR_GCL_HBRBACKGROUND, COLOR_BTNFACE + 1), 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, CR_GCL_HBRBACKGROUND), COLOR_BTNFACE + 1);
    HGDIOBJ st = U(GetStockObject)(DKGRAY_BRUSH);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, CR_GCL_HBRBACKGROUND, (ULONG_PTR)st), COLOR_BTNFACE + 1);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, CR_GCL_HBRBACKGROUND), (ULONG_PTR)st);
    fx_free(&f);
}

TEST(class_registration_set_hmodule_roundtrip) {
    CR_NEED();
    FX(f, 0, 0, 0);
    HINSTANCE keep = cr_hinst();
    ULONG_PTR old = U(SetClassLongPtrW)(f.h, GCLP_HMODULE, 0x7fff0000);
    EXPECT_EQ(old, (ULONG_PTR)keep);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HMODULE), 0x7fff0000);
    U(SetClassLongPtrW)(f.h, GCLP_HMODULE, (ULONG_PTR)keep);       /* restore so the class can be unregistered */
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HMODULE), (ULONG_PTR)keep);
    fx_free(&f);
}

TEST(class_registration_set_wndproc_affects_new_windows) {
    CR_NEED();
    FX(f, 0, 0, 0);
    ULONG_PTR old = U(SetClassLongPtrW)(f.h, GCLP_WNDPROC, (ULONG_PTR)cr_proc2);
    EXPECT_EQ(old, (ULONG_PTR)cr_proc);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_WNDPROC), (ULONG_PTR)cr_proc2);
    WNDCLASSEXW o; memset(&o, 0, sizeof o); o.cbSize = sizeof o;
    CHECK(U(GetClassInfoExW)(cr_hinst(), f.name, &o));
    EXPECT_EQ(o.lpfnWndProc, cr_proc2);
    HWND h2 = cr_win(f.name);
    if (h2) {
        EXPECT_EQ(U(GetWindowLongPtrW)(h2, GWLP_WNDPROC), (ULONG_PTR)cr_proc2);
        U(DestroyWindow)(h2);
    }
    t_note("pre-existing window proc after class change: %p", (void *)U(GetWindowLongPtrW)(f.h, GWLP_WNDPROC));
    fx_free(&f);
}

TEST(class_registration_set_wndproc_ansi_unicode_flip) {
    CR_NEED();
    FX(f, 0, 0, 0);
    /* SetClassLongPtrA records that the stored procedure expects ANSI (CSF_ANSIPROC); the opposite accessor
     * then returns a call-proc handle for it */
    U(SetClassLongPtrA)(f.h, GCLP_WNDPROC, (ULONG_PTR)cr_procA);
    EXPECT_EQ(U(GetClassLongPtrA)(f.h, GCLP_WNDPROC), (ULONG_PTR)cr_procA);
    ULONG_PTR w = U(GetClassLongPtrW)(f.h, GCLP_WNDPROC);
    CHECK(w != (ULONG_PTR)cr_procA && w != 0);
    BYTE *cls = (BYTE *)cr_cls_client(f.h);
    if (cls) { unsigned short fl = 0; CHECK(cr_rd(cls + 6, &fl, 2)); CHECK((fl & 2) != 0); }
    U(SetClassLongPtrW)(f.h, GCLP_WNDPROC, (ULONG_PTR)cr_proc);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_WNDPROC), (ULONG_PTR)cr_proc);
    ULONG_PTR a = U(GetClassLongPtrA)(f.h, GCLP_WNDPROC);
    CHECK(a != (ULONG_PTR)cr_proc && a != 0);
    if (cls) { unsigned short fl = 0xffff; CHECK(cr_rd(cls + 6, &fl, 2)); CHECK((fl & 2) == 0); }
    fx_free(&f);
}

TEST(class_registration_set_menu_name_forms) {
    CR_NEED();
    FX(f, 0, 0, 0);
    ULONG_PTR old = U(SetClassLongPtrW)(f.h, GCLP_MENUNAME, (ULONG_PTR)MAKEINTRESOURCEW(321));
    EXPECT_EQ(old, 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_MENUNAME), 321);
    WCHAR m[] = L"win32kt_set_menu";
    U(SetClassLongPtrW)(f.h, GCLP_MENUNAME, (ULONG_PTR)m);
    ULONG_PTR p = U(GetClassLongPtrW)(f.h, GCLP_MENUNAME);
    CHECK(p > 0xffff && p != (ULONG_PTR)m);
    if (p > 0xffff && !IsBadReadPtr((void *)p, 2)) EXPECT_WSTR((WCHAR *)p, L"win32kt_set_menu");
    ULONG_PTR pa = U(GetClassLongPtrA)(f.h, GCLP_MENUNAME);
    CHECK(pa > 0xffff);
    if (pa > 0xffff && !IsBadReadPtr((void *)pa, 2)) EXPECT_STR((char *)pa, "win32kt_set_menu");
    U(SetClassLongPtrW)(f.h, GCLP_MENUNAME, 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_MENUNAME), 0);
    fx_free(&f);
}

TEST(class_registration_set_icon_cursor_roundtrip) {
    CR_NEED();
    HICON i1 = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512));
    HICON i2 = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32513));
    HICON i3 = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32514));
    HCURSOR c1 = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512));
    HCURSOR c2 = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32513));
    if (!i1 || !i2 || !i3 || !c1 || !c2) { t_skip("stock icons unavailable"); return; }
    FX(f, 0, 0, 0);
    ULONG_PTR o = U(SetClassLongPtrW)(f.h, GCLP_HICON, (ULONG_PTR)i1);
    t_info("old hIcon of an icon-less class: %p", (void *)o);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HICON), (ULONG_PTR)i1);
    o = U(SetClassLongPtrW)(f.h, GCLP_HICON, (ULONG_PTR)i2);
    EXPECT_EQ(o, (ULONG_PTR)i1);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HICON), (ULONG_PTR)i2);
    U(SetClassLongPtrW)(f.h, GCLP_HCURSOR, (ULONG_PTR)c1);
    o = U(SetClassLongPtrW)(f.h, GCLP_HCURSOR, (ULONG_PTR)c2);
    EXPECT_EQ(o, (ULONG_PTR)c1);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HCURSOR), (ULONG_PTR)c2);
    U(SetClassLongPtrW)(f.h, GCLP_HICONSM, (ULONG_PTR)i3);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HICONSM), (ULONG_PTR)i3);
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    CHECK(U(GetClassInfoExW)(cr_hinst(), f.name, &w));
    EXPECT_EQ(w.hIcon, i2);
    EXPECT_EQ(w.hCursor, c2);
    EXPECT_EQ(w.hIconSm, i3);
    U(SetClassLongPtrW)(f.h, GCLP_HICON, 0);
    EXPECT_EQ(U(GetClassLongPtrW)(f.h, GCLP_HICON), 0);
    fx_free(&f);
}

TEST(class_registration_set_returns_old_for_all_scalar_indices) {
    CR_NEED();
    FX(f, CS_DBLCLKS, 8, 8);
    /* every settable scalar returns the previous value: STYLE, WNDPROC, HMODULE, HBRBACKGROUND */
    ULONG_PTR keep_mod = U(GetClassLongPtrW)(f.h, GCLP_HMODULE);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCL_STYLE, CS_HREDRAW), CS_DBLCLKS);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCL_STYLE, CS_DBLCLKS), CS_HREDRAW);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCLP_WNDPROC, (ULONG_PTR)cr_proc2), (ULONG_PTR)cr_proc);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCLP_WNDPROC, (ULONG_PTR)cr_proc), (ULONG_PTR)cr_proc2);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCLP_HMODULE, keep_mod), keep_mod);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCLP_HBRBACKGROUND, 3), 0);
    EXPECT_EQ(U(SetClassLongPtrW)(f.h, GCLP_HBRBACKGROUND, 0), 3);
    fx_free(&f);
}
