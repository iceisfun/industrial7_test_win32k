/* r2_gdi_internal_eng_glue: font-service and DX-rendering glue syscalls that reject on up-front checks.
 *
 *  NtGdiAddInitialFonts()                                  win32kfull 0x12b070
 *  NtGdiSetFontEnumeration(ULONG)                          win32kfull 0x29ffc0
 *  NtGdiUnmapMemFont(PVOID)                                win32kfull 0x12ddf0 (folded with a "return 1" body)
 *  NtGdiGetProcessSessionFonts(h, ph, pn, pwsz, pcch)      win32kfull 0x21250
 *  NtGdiGetStats(pid, type, filter, buf, size)             win32kbase 0x140cf0
 *  NtGdiBeginGdiRendering(HBITMAP, BOOL, ULONG dev)        win32kfull 0x10cab8
 *  NtGdiEndGdiRendering(HBITMAP, BOOL, ULONG *, ULONG dev) win32kfull 0x10b880
 */
#include "r2e.h"

#define PEB_NtGlobalFlag 0xBC
#define FLG_BIT_0x400 0x400u

/* ---- NtGdiAddInitialFonts ------------------------------------------------------------------- */

/* Only the CSRSS process may run it: any other caller gets STATUS_ACCESS_DENIED before the
 * font host is touched. */
TEST(r2_gdi_internal_eng_glue_addinitialfonts_denied_outside_csrss) {
    r2e_fn f = r2e_stub("NtGdiAddInitialFonts"); REQUIRE(f);
    r2e_check_number("NtGdiAddInitialFonts", 4479);
    for (int i = 0; i < 3; i++) CHECK(R2E_LO(f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_ACCESS_DENIED);
}
TEST(r2_gdi_internal_eng_glue_addinitialfonts_ignores_arguments) {
    r2e_fn f = r2e_stub("NtGdiAddInitialFonts"); REQUIRE(f);
    unsigned char canary[32]; memset(canary, 0x5C, sizeof canary);
    ULONG_PTR p = (ULONG_PTR)canary;
    CHECK(R2E_LO(f(p, p, p, p, p, p, p, p, p, p, p, p, p, p)) == R2E_STATUS_ACCESS_DENIED);
    for (unsigned i = 0; i < sizeof canary; i++) CHECK(canary[i] == 0x5C);
}
TEST(r2_gdi_internal_eng_glue_addinitialfonts_from_second_thread) {
    r2e_fn f = r2e_stub("NtGdiAddInitialFonts"); REQUIRE(f);
    /* the gate is the calling PROCESS (== gpepCSRSS), not the thread */
    static r2e_fn g; g = f;
    DWORD WINAPI th(LPVOID a) { (void)a; return R2E_LO(g(0,0,0,0,0,0,0,0,0,0,0,0,0,0)) == R2E_STATUS_ACCESS_DENIED ? 0 : 1; }
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL); REQUIRE(h);
    DWORD w = WaitForSingleObject(h, 10000), code = 9;
    CHECK(w == WAIT_OBJECT_0);
    if (w == WAIT_OBJECT_0) { GetExitCodeThread(h, &code); CHECK(code == 0); }
    CloseHandle(h);
}
TEST(r2_gdi_internal_eng_glue_addinitialfonts_pristine_child) {
    REQUIRE(r2e_stub("NtGdiAddInitialFonts"));
    struct k_child_result r = k_child_run("r2e_misc_child", "addinitialfonts", 15000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

/* ---- NtGdiSetFontEnumeration ---------------------------------------------------------------- */

/* The whole body is EngSetLastError(ERROR_NOT_SUPPORTED = 0x32) plus returning the global
 * gulFontInformation, whatever the argument. */
TEST(r2_gdi_internal_eng_glue_setfontenum_sets_not_supported) {
    r2e_fn f = r2e_stub("NtGdiSetFontEnumeration"); REQUIRE(f);
    r2e_check_number("NtGdiSetFontEnumeration", 4363);
    static const ULONG_PTR args[] = { 0, 1, 2, 0xFF, 0x80000000ul, 0xFFFFFFFFul };
    for (unsigned i = 0; i < sizeof args / sizeof args[0]; i++) {
        SET_ERR(0xBEEF);
        f(args[i], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        EXPECT_ERR(ERROR_NOT_SUPPORTED);
    }
}
TEST(r2_gdi_internal_eng_glue_setfontenum_result_is_stable) {
    r2e_fn f = r2e_stub("NtGdiSetFontEnumeration"); REQUIRE(f);
    unsigned long a = R2E_LO(f(0,0,0,0,0,0,0,0,0,0,0,0,0,0));
    unsigned long b = R2E_LO(f(7,0,0,0,0,0,0,0,0,0,0,0,0,0));
    unsigned long c = R2E_LO(f(0xFFFFFFFFul,0,0,0,0,0,0,0,0,0,0,0,0,0));
    CHECK(a == b && b == c);          /* the argument is ignored; the global only changes with the font set */
    t_note("gulFontInformation = 0x%lx", a);
}
TEST(r2_gdi_internal_eng_glue_setfontenum_no_object_growth) {
    r2e_fn f = r2e_stub("NtGdiSetFontEnumeration"); REQUIRE(f);
    unsigned long g0 = r2e_gdi_count(), u0 = r2e_user_count();
    for (int i = 0; i < 20; i++) f(i,0,0,0,0,0,0,0,0,0,0,0,0,0);
    CHECK(r2e_gdi_count() == g0);
    CHECK(r2e_user_count() == u0);
}
TEST(r2_gdi_internal_eng_glue_setfontenum_via_gdi32full_export) {
    /* gdi32full!SetFontEnumeration is a bare jump into the syscall: same error, same value */
    r2e_fn f = r2e_stub("NtGdiSetFontEnumeration"); REQUIRE(f);
    FARPROC g = t_proc_in("gdi32full.dll", "SetFontEnumeration");
    if (!g) { t_skip("gdi32full!SetFontEnumeration not exported"); return; }
    unsigned long a = R2E_LO(f(0,0,0,0,0,0,0,0,0,0,0,0,0,0));
    SET_ERR(0xBEEF);
    unsigned long b = R2E_LO(((r2e_fn)g)(0,0,0,0,0,0,0,0,0,0,0,0,0,0));
    EXPECT_ERR(ERROR_NOT_SUPPORTED);
    CHECK(a == b);
}

/* ---- NtGdiUnmapMemFont ---------------------------------------------------------------------- */

/* win32kfull maps the entry to a function that is just (it shares the body with
 * EditionGetExecutionEvironment): success for every argument, nothing is unmapped. */
TEST(r2_gdi_internal_eng_glue_unmapmemfont_returns_one) {
    r2e_fn f = r2e_stub("NtGdiUnmapMemFont"); REQUIRE(f);
    r2e_check_number("NtGdiUnmapMemFont", 4900);
    static const ULONG_PTR args[] = { 0, 1, 0x10000, 0xDEADBEEFull, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof args / sizeof args[0]; i++)
        CHECKF(R2E_LO(f(args[i], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 1, "arg 0x%llx", (unsigned long long)args[i]);
}
TEST(r2_gdi_internal_eng_glue_unmapmemfont_does_not_touch_memory) {
    r2e_fn f = r2e_stub("NtGdiUnmapMemFont"); REQUIRE(f);
    void *p = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); REQUIRE(p);
    memset(p, 0x77, 4096);
    CHECK(R2E_LO(f((ULONG_PTR)p, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 1);
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery(p, &mbi, sizeof mbi) == sizeof mbi);
    CHECK(mbi.State == MEM_COMMIT && mbi.RegionSize == 4096);
    CHECK(((unsigned char *)p)[0] == 0x77 && ((unsigned char *)p)[4095] == 0x77);
    VirtualFree(p, 0, MEM_RELEASE);
}
TEST(r2_gdi_internal_eng_glue_unmapmemfont_preserves_last_error) {
    r2e_fn f = r2e_stub("NtGdiUnmapMemFont"); REQUIRE(f);
    SET_ERR(0x1234);
    f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_ERR(0x1234);
}

/* ---- NtGdiGetProcessSessionFonts ------------------------------------------------------------ */

/* is the first statement, before any pointer (even the
 * misalignment-raising ones) is looked at. An interactive test session is never session 0. */
TEST(r2_gdi_internal_eng_glue_getprocsessionfonts_nonzero_session_returns_zero) {
    r2e_fn f = r2e_stub("NtGdiGetProcessSessionFonts"); REQUIRE(f);
    r2e_check_number("NtGdiGetProcessSessionFonts", 4817);
    if (k_session_id() == 0) { t_skip("running in session 0"); return; }
    SET_ERR(0x4242);
    CHECK(R2E_LO(f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    EXPECT_ERR(0x4242);            /* the error path that calls EngSetLastError is never reached */
}
TEST(r2_gdi_internal_eng_glue_getprocsessionfonts_arguments_untouched) {
    r2e_fn f = r2e_stub("NtGdiGetProcessSessionFonts"); REQUIRE(f);
    if (k_session_id() == 0) { t_skip("running in session 0"); return; }
    unsigned long cnt = 0x11, cch = 0x22;
    unsigned long long handles[4] = { 1, 2, 3, 4 };
    WCHAR names[8]; for (int i = 0; i < 8; i++) names[i] = L'a' + i;
    CHECK(R2E_LO(f((ULONG_PTR)GetCurrentProcess(), (ULONG_PTR)handles, (ULONG_PTR)&cnt, (ULONG_PTR)names, (ULONG_PTR)&cch, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    CHECK(cnt == 0x11 && cch == 0x22);
    CHECK(handles[0] == 1 && handles[3] == 4);
    CHECK(names[0] == L'a' && names[7] == L'h');
}
/* A misaligned count pointer would raise datatype-misalignment if the body got that far. */
TEST(r2_gdi_internal_eng_glue_getprocsessionfonts_misaligned_not_inspected) {
    r2e_fn f = r2e_stub("NtGdiGetProcessSessionFonts"); REQUIRE(f);
    if (k_session_id() == 0) { t_skip("running in session 0"); return; }
    unsigned char b[16] = {0};
    CHECK(R2E_LO(f(0, 0, (ULONG_PTR)(b + 1), 0, (ULONG_PTR)(b + 3), 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
}
TEST(r2_gdi_internal_eng_glue_getprocsessionfonts_no_handles_created) {
    r2e_fn f = r2e_stub("NtGdiGetProcessSessionFonts"); REQUIRE(f);
    if (k_session_id() == 0) { t_skip("running in session 0"); return; }
    DWORD h0 = 0, h1 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h0);
    for (int i = 0; i < 10; i++) f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    GetProcessHandleCount(GetCurrentProcess(), &h1);
    CHECK(h0 == h1);
}

/* ---- NtGdiGetStats --------------------------------------------------------------------------- */

/* GetStats gates on RtlGetNtGlobalFlags(), the kernel's system-wide NtGlobalFlag (analysis:
 * `~-(flags & 0x400 != 0) & 0xc0000022`), not on this process's PEB copy, which image-file
 * options can change. SystemFlagsInformation (class 9) returns that system value; the PEB value is
 * only the fallback when the query is unavailable. */
static int ntglobalflag_0x400(void) {
    typedef LONG (WINAPI *qsi_t)(ULONG, PVOID, ULONG, PULONG);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    qsi_t q = nt ? (qsi_t)GetProcAddress(nt, "NtQuerySystemInformation") : NULL;
    unsigned long v = 0, len = 0;
    if (q && q(9, &v, sizeof v, &len) >= 0 && len == sizeof v) return (v & FLG_BIT_0x400) != 0;
    k_try_read((char *)k_peb() + PEB_NtGlobalFlag, &v, 4);
    return (v & FLG_BIT_0x400) != 0;
}
/* The status starts as ACCESS_DENIED (unless NtGlobalFlag & 0x400 is set), then iType 0 with a
 * buffer under 0x7c bytes becomes STATUS_BUFFER_TOO_SMALL and an iType outside 0..4 becomes
 * STATUS_NOT_IMPLEMENTED; both overrides apply before the probe, so no buffer is needed. */
TEST(r2_gdi_internal_eng_glue_getstats_type0_small_buffer) {
    r2e_fn f = r2e_stub("NtGdiGetStats"); REQUIRE(f);
    r2e_check_number("NtGdiGetStats", 4822);
    static const ULONG_PTR sizes[] = { 0, 1, 4, 0x78, 0x7b };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        CHECKF(R2E_LO(f(0, 0, 0, 0, sizes[i], 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_BUFFER_TOO_SMALL, "size 0x%llx", (unsigned long long)sizes[i]);
}
TEST(r2_gdi_internal_eng_glue_getstats_unknown_type) {
    r2e_fn f = r2e_stub("NtGdiGetStats"); REQUIRE(f);
    static const ULONG_PTR types[] = { 5, 6, 0x10, 0x7fffffff, 0xFFFFFFFFull /* -1 as int */, 0x80000000ull };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++)
        CHECKF(R2E_LO(f(0, types[i], 0, 0, 0x1000, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_NOT_IMPLEMENTED, "type 0x%llx", (unsigned long long)types[i]);
}
/* Unknown types are rejected whatever the other arguments are, and nothing is written. */
TEST(r2_gdi_internal_eng_glue_getstats_rejects_before_buffer) {
    r2e_fn f = r2e_stub("NtGdiGetStats"); REQUIRE(f);
    unsigned char buf[0x80]; memset(buf, 0xE7, sizeof buf);
    CHECK(R2E_LO(f(GetCurrentProcessId(), 9, 1, (ULONG_PTR)buf, sizeof buf, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_NOT_IMPLEMENTED);
    CHECK(R2E_LO(f(GetCurrentProcessId(), 0, 1, (ULONG_PTR)buf, 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_BUFFER_TOO_SMALL);
    for (unsigned i = 0; i < sizeof buf; i++) CHECK(buf[i] == 0xE7);
}
/* Without NtGlobalFlag 0x400 the remaining requests (types 1..4, or type 0 with a large enough
 * buffer) are all answered STATUS_ACCESS_DENIED before the buffer is probed or written. */
TEST(r2_gdi_internal_eng_glue_getstats_access_denied_without_flag) {
    r2e_fn f = r2e_stub("NtGdiGetStats"); REQUIRE(f);
    if (ntglobalflag_0x400()) { t_skip("NtGlobalFlag 0x400 is set; the stats path is open"); return; }
    unsigned char buf[0x100]; memset(buf, 0xE7, sizeof buf);
    for (ULONG_PTR t = 1; t <= 4; t++)
        CHECKF(R2E_LO(f(GetCurrentProcessId(), t, 1, (ULONG_PTR)buf, sizeof buf, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_ACCESS_DENIED, "type %llu", (unsigned long long)t);
    CHECK(R2E_LO(f(GetCurrentProcessId(), 0, 1, (ULONG_PTR)buf, 0x7c, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_ACCESS_DENIED);
    CHECK(R2E_LO(f(GetCurrentProcessId(), 0, 1, (ULONG_PTR)buf, 0x100, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_ACCESS_DENIED);
    for (unsigned i = 0; i < sizeof buf; i++) CHECK(buf[i] == 0xE7);
}
TEST(r2_gdi_internal_eng_glue_getstats_denied_with_null_buffer) {
    r2e_fn f = r2e_stub("NtGdiGetStats"); REQUIRE(f);
    if (ntglobalflag_0x400()) { t_skip("NtGlobalFlag 0x400 is set"); return; }
    CHECK(R2E_LO(f(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_ACCESS_DENIED);   /* type 1..4: no size check at all */
    CHECK(R2E_LO(f(0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == R2E_STATUS_ACCESS_DENIED);
}

/* ---- NtGdiBeginGdiRendering / NtGdiEndGdiRendering ------------------------------------------- */

/* Begin: with the device argument 0 no DX call is made; the kernel wants a surface
 * (bitmap) handle, anything else gives 0 and the function leaves the handle alone. */
TEST(r2_gdi_internal_eng_glue_begingdirendering_null_and_wrong_type) {
    r2e_fn f = r2e_stub("NtGdiBeginGdiRendering"); REQUIRE(f);
    r2e_check_number("NtGdiBeginGdiRendering", 4490);
    CHECK(R2E_LO(r2e_call(f, 0, 0, 0, 0, 0)) == 0);
    CHECK(R2E_LO(r2e_call(f, 0, 1, 0, 0, 0)) == 0);
    HDC d = U(CreateCompatibleDC)(NULL); REQUIRE(d);
    HPEN p = U(CreatePen)(PS_SOLID, 1, 0); REQUIRE(p);
    unsigned long g0 = r2e_gdi_count();
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, 0, 0, 0)) == 0);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)p, 1, 0, 0, 0)) == 0);
    CHECK(U(GetObjectType)(d) == OBJ_MEMDC);
    CHECK(U(GetObjectType)(p) == OBJ_PEN);
    CHECK(r2e_gdi_count() == g0);
    U(DeleteObject)(p); U(DeleteDC)(d);
}
TEST(r2_gdi_internal_eng_glue_begingdirendering_deleted_bitmap) {
    r2e_fn f = r2e_stub("NtGdiBeginGdiRendering"); REQUIRE(f);
    HBITMAP b = U(CreateBitmap)(4, 4, 1, 1, NULL); REQUIRE(b);
    U(DeleteObject)(b);
    CHECK(U(GetObjectType)(b) == OBJ_BITMAP);   /* freed cell keeps its upper word until reallocated; the old handle is still typed */
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)b, 0, 0, 0, 0)) == 0);
}
/* A plain GDI bitmap is not a DX-shared surface: Begin has nothing to start and returns 0, the bitmap stays usable. */
TEST(r2_gdi_internal_eng_glue_begingdirendering_plain_bitmap) {
    r2e_fn f = r2e_stub("NtGdiBeginGdiRendering"); REQUIRE(f);
    HBITMAP b = U(CreateBitmap)(8, 8, 1, 1, NULL); REQUIRE(b);
    unsigned long g0 = r2e_gdi_count();
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)b, 0, 0, 0, 0)) == 0);
    BITMAP bm; memset(&bm, 0, sizeof bm);
    CHECK(U(GetObjectW)(b, sizeof bm, &bm) == sizeof bm);
    CHECK(bm.bmWidth == 8 && bm.bmHeight == 8);
    CHECK(r2e_gdi_count() == g0);
    U(DeleteObject)(b);
}

/* End: the result flag word is written even when the call fails (local_res18[0] = 0 is stored
 * through the clamped pointer), and the result is 0 for a handle that is not a surface. */
TEST(r2_gdi_internal_eng_glue_endgdirendering_null_handle_clears_flag) {
    r2e_fn f = r2e_stub("NtGdiEndGdiRendering"); REQUIRE(f);
    r2e_check_number("NtGdiEndGdiRendering", 4723);
    unsigned long out = 0xDEADBEEF;
    CHECK(R2E_LO(r2e_call(f, 0, 0, (ULONG_PTR)&out, 0, 0)) == 0);
    CHECK(out == 0);
    out = 0xDEADBEEF;
    CHECK(R2E_LO(r2e_call(f, 0, 1, (ULONG_PTR)&out, 0, 0)) == 0);
    CHECK(out == 0);
}
TEST(r2_gdi_internal_eng_glue_endgdirendering_null_out_pointer) {
    r2e_fn f = r2e_stub("NtGdiEndGdiRendering"); REQUIRE(f);
    CHECK(R2E_LO(r2e_call(f, 0, 0, 0, 0, 0)) == 0);        /* the store through the third argument is guarded by a NULL test */
}
TEST(r2_gdi_internal_eng_glue_endgdirendering_wrong_type) {
    r2e_fn f = r2e_stub("NtGdiEndGdiRendering"); REQUIRE(f);
    HDC d = U(CreateCompatibleDC)(NULL); REQUIRE(d);
    unsigned long out = 0xDEADBEEF;
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, (ULONG_PTR)&out, 0, 0)) == 0);
    CHECK(out == 0);
    CHECK(U(GetObjectType)(d) == OBJ_MEMDC);
    U(DeleteDC)(d);
}
TEST(r2_gdi_internal_eng_glue_endgdirendering_deleted_bitmap) {
    r2e_fn f = r2e_stub("NtGdiEndGdiRendering"); REQUIRE(f);
    HBITMAP b = U(CreateBitmap)(4, 4, 1, 1, NULL); REQUIRE(b);
    U(DeleteObject)(b);
    unsigned long out = 0xDEADBEEF;
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)b, 0, (ULONG_PTR)&out, 0, 0)) == 0);
    CHECK(out == 0);
}
/* A plain bitmap that never went through Begin: the result varies with the surface layout
 * (only DX-shared surfaces take the driver callback), so it is only recorded. */
TEST(r2_gdi_internal_eng_glue_endgdirendering_plain_bitmap_recorded) {
    r2e_fn f = r2e_stub("NtGdiEndGdiRendering"); REQUIRE(f);
    HBITMAP b = U(CreateBitmap)(8, 8, 1, 1, NULL); REQUIRE(b);
    unsigned long out = 0xDEADBEEF, g0 = r2e_gdi_count();
    unsigned long r = R2E_LO(r2e_call(f, (ULONG_PTR)b, 0, (ULONG_PTR)&out, 0, 0));
    t_note("EndGdiRendering(plain bitmap) = %lu out=0x%lx", r, out);
    CHECK(r <= 1);
    CHECK(U(GetObjectType)(b) == OBJ_BITMAP);
    CHECK(r2e_gdi_count() == g0);
    U(DeleteObject)(b);
}

/* ---- pristine child: these must work as the very first win32k call of a process ------------- */
CHILD(r2e_misc_child) {
    int bad = 0;
    const char *which = argc > 1 ? argv[1] : "";
    unsigned long long before = 0, after = 0;
    r2e_stub_callback_table();
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &before, 8);
    if (before) { bad++; printf("  FAIL thread already converted\n"); }
    if (!strcmp(which, "addinitialfonts")) {
        r2e_fn f = (r2e_fn)t_proc_in("win32u.dll", "NtGdiAddInitialFonts");
        if (!f) { printf("  FAIL no stub\n"); return 1; }
        if (R2E_LO(f(0,0,0,0,0,0,0,0,0,0,0,0,0,0)) != R2E_STATUS_ACCESS_DENIED) { bad++; printf("  FAIL AddInitialFonts not denied\n"); }
    } else { printf("  FAIL unknown child scenario\n"); return 1; }
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &after, 8);
    if (!after) { bad++; printf("  FAIL thread not converted by the first win32k syscall\n"); }
    return bad;
}
