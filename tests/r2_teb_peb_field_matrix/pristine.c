/* Pristine process vs. after the first GUI call.
 *
 * A child that never touched user32/gdi32 must show the all-zero win32 TEB/PEB members that the loader and
 * kernel leave behind for a non-GUI thread (the kernel only fills them in when PsConvertToGuiThread runs for the first win32k service; user32!ClientThreadSetup and
 * UserClientDllInitialize fill the user-mode halves). The children use their own failure counter and return it
 * as the exit code. */
#include "rtp.h"

static int cbad;
#define CC(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); cbad++; } } while (0)
#define CCF(c, ...) do { if (!(c)) { printf("  FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); cbad++; } } while (0)

static void parent_child(const char *name, int allow_crash_note) {
    struct k_child_result r = k_child_run(name, "", 30000);
    if (r.exit_code == 0xDEAD0000) { t_skip("child did not start"); return; }
    if (allow_crash_note && (r.exit_code & 0xC0000000) == 0xC0000000) {
        t_note("child %s died with 0x%08lx", name, r.exit_code);
        return;
    }
    EXPECT_EQ(r.exit_code, 0);
}

static int no_gui_modules(void) {
    return !GetModuleHandleA("user32.dll") && !GetModuleHandleA("gdi32.dll") && !GetModuleHandleA("gdi32full.dll");
}

/* checks that a snapshot is the all-zero state of a thread that never entered win32k */
static void zero_state(const struct rsnap *s, const char *what) {
    CCF(s->ok, "%s: TEB unreadable", what);
    CCF(s->ti == 0, "%s: Win32ThreadInfo=0x%llx", what, s->ti);
    CCF(rs_ci_all_zero(s, 0, CIO_flags2), "%s: _CLIENTINFO[0..0xe0) not all zero", what);
    CCF(s->gdi_count == 0, "%s: GdiBatchCount=%lu", what, s->gdi_count);
    CCF(s->gdi_off == 0, "%s: GdiTebBatch.Offset=0x%lx", what, s->gdi_off);
    CCF(s->gdi_hdc == 0, "%s: GdiTebBatch.HDC=0x%llx", what, s->gdi_hdc);
    CCF(s->gdi_tli == 0, "%s: GdiThreadLocalInfo=0x%llx", what, s->gdi_tli);
    CCF(s->gl_dispatch0 == 0 && s->gl_sectioninfo == 0 && s->gl_section == 0 && s->gl_table == 0 &&
        s->gl_currentrc == 0 && s->gl_context == 0, "%s: OpenGL TEB members not zero", what);
}

static DWORD WINAPI first_thread(void *p) { rs_take((struct rsnap *)p); return 0; }

CHILD(r2_teb_peb_field_matrix_c_pristine) {
    cbad = 0;
    if (!no_gui_modules()) { t_skip("user32/gdi32 already loaded in this host"); return 0; }
    struct rsnap s; rs_take(&s);
    zero_state(&s, "main thread");
    CC(s.pid == GetCurrentProcessId());
    CC(s.tid == GetCurrentThreadId());
    CC(rt_rd64((ULONG_PTR)k_peb(), PEB_KernelCallbackTable) == 0);          /* user32 installs it */
    t_note("pristine PEB: GdiSharedHandleTable=0x%llx GdiDCAttributeList=%lu",
           rt_rd64((ULONG_PTR)k_peb(), PEB_GdiSharedHandleTable), rt_rd32((ULONG_PTR)k_peb(), PEB_GdiDCAttributeList));
    /* a second thread of the same pristine process looks the same */
    struct rsnap t2; memset(&t2, 0, sizeof t2);
    HANDLE h = CreateThread(NULL, 0, first_thread, &t2, 0, NULL);
    if (h) { WaitForSingleObject(h, 5000); CloseHandle(h); zero_state(&t2, "second thread"); CC(t2.tid != s.tid); }
    return cbad;
}

CHILD(r2_teb_peb_field_matrix_c_user32_first) {
    cbad = 0;
    if (!no_gui_modules()) { t_skip("user32/gdi32 already loaded in this host"); return 0; }
    struct rsnap a, b, c;
    rs_take(&a);
    zero_state(&a, "before");
    HWND dw = rt_gui();                       /* LoadLibrary(user32) -> UserClientDllInitialize -> ProcessConnect */
    rs_take(&b);
    CC(dw != NULL);
    CCF(b.ti != 0, "Win32ThreadInfo still NULL after the first user32 call");
    CCF((rs_ci64(&b, CIO_flags) & CIF_INITIALIZED) != 0, "CI_flags=0x%llx", rs_ci64(&b, CIO_flags));
    unsigned long long kcb = rt_rd64((ULONG_PTR)k_peb(), PEB_KernelCallbackTable);
    CCF(rt_in_user32(kcb, 8), "KernelCallbackTable=0x%llx not in user32", kcb);
    CCF(rt_in_user32(rs_ci64(&b, CIO_regClasses), 4), "lpdwRegisteredClasses=0x%llx not in user32", rs_ci64(&b, CIO_regClasses));
    CC(rs_ci64(&b, CIO_pDeskInfo) != 0);
    CC(rs_ci64(&b, CIO_ulClientDelta) != 0);
    CC(rs_ci64(&b, CIO_hKL) != 0);
    CC(b.pid == a.pid && b.tid == a.tid && b.teb == a.teb);
    /* a second user32 call keeps the very same thread info */
    t_proc("GetFocus"); rt_ts(0xE);
    rs_take(&c);
    CC(c.ti == b.ti);
    CC(rs_ci64(&c, CIO_pDeskInfo) == rs_ci64(&b, CIO_pDeskInfo));
    CC(rs_ci64(&c, CIO_ulClientDelta) == rs_ci64(&b, CIO_ulClientDelta));
    CC(rs_ci32(&c, CIO_dwExpWinVer) == rs_ci32(&b, CIO_dwExpWinVer));
    t_info("after first user32 call: ti=0x%llx flags=0x%llx expver=0x%lx tiflags=0x%lx hkl=0x%llx cp=%u",
           b.ti, rs_ci64(&b, CIO_flags), rs_ci32(&b, CIO_dwExpWinVer), rs_ci32(&b, CIO_dwTIFlags),
           rs_ci64(&b, CIO_hKL), rs_ci16(&b, CIO_CodePage));
    return cbad;
}

CHILD(r2_teb_peb_field_matrix_c_win32u_first) {
    cbad = 0;
    if (!no_gui_modules()) { t_skip("user32/gdi32 already loaded in this host"); return 0; }
    struct rsnap a, b, c;
    rs_take(&a);
    zero_state(&a, "before");
    HMODULE w = t_module("win32u.dll");
    CC(w != NULL);
    struct rsnap loaded; rs_take(&loaded);
    if (loaded.ti) t_note("loading win32u.dll alone already converted the thread (ti=0x%llx)", loaded.ti);
    ULONG_PTR r = rt_ts(0xE);                 /* win32k service without user32: PsConvertToGuiThread */
    rs_take(&b);
    t_note("win32u-only first call: ret=%llu ti=0x%llx flags=0x%llx pDeskInfo=0x%llx delta=0x%llx kcb=0x%llx",
           (unsigned long long)r, b.ti, rs_ci64(&b, CIO_flags), rs_ci64(&b, CIO_pDeskInfo),
           rs_ci64(&b, CIO_ulClientDelta), rt_rd64((ULONG_PTR)k_peb(), PEB_KernelCallbackTable));
    if (!b.ti) { t_note("no GUI conversion happened without user32"); return cbad; }
    CC(b.ti != 0);
    CC(b.pid == a.pid && b.tid == a.tid);
    /* user32's half of the setup (CI_flags bit 3 and the PEB table) is not there yet */
    CCF((rs_ci64(&b, CIO_flags) & CIF_INITIALIZED) == 0, "ClientThreadSetup ran without user32");
    CC(rt_rd64((ULONG_PTR)k_peb(), PEB_KernelCallbackTable) == 0);
    /* now bring user32 up on the already converted thread */
    rt_gui();
    rs_take(&c);
    CCF(c.ti == b.ti, "Win32ThreadInfo changed from 0x%llx to 0x%llx", b.ti, c.ti);
    CC((rs_ci64(&c, CIO_flags) & CIF_INITIALIZED) != 0);
    CC(rt_in_user32(rt_rd64((ULONG_PTR)k_peb(), PEB_KernelCallbackTable), 8));
    CC(rs_ci32(&c, CIO_dwExpWinVer) == rs_ci32(&b, CIO_dwExpWinVer));
    return cbad;
}

CHILD(r2_teb_peb_field_matrix_c_gdi_first) {
    cbad = 0;
    if (!no_gui_modules()) { t_skip("user32/gdi32 already loaded in this host"); return 0; }
    HMODULE g = t_module("gdi32.dll");
    CC(g != NULL);
    HDC (WINAPI *cdc)(HDC) = (HDC (WINAPI *)(HDC))(g ? GetProcAddress(g, "CreateCompatibleDC") : NULL);
    BOOL (WINAPI *ddc)(HDC) = (BOOL (WINAPI *)(HDC))(g ? GetProcAddress(g, "DeleteDC") : NULL);
    if (!cdc || !ddc) { t_skip("no gdi32 exports"); return cbad; }
    HDC dc = cdc(NULL);
    unsigned long long tbl = rt_rd64((ULONG_PTR)k_peb(), PEB_GdiSharedHandleTable);
    struct rsnap s; rs_take(&s);
    t_info("gdi-first: dc=%p table=0x%llx dcattr=%lu ti=0x%llx user32=%p", dc, tbl,
           rt_rd32((ULONG_PTR)k_peb(), PEB_GdiDCAttributeList), s.ti, (void *)GetModuleHandleA("user32.dll"));
    if (!dc) { t_note("CreateCompatibleDC(NULL) failed with %lu", GetLastError()); return cbad; }
    CCF(tbl != 0 && rt_user_va(tbl), "GdiSharedHandleTable=0x%llx", tbl);
    CC((tbl & 0xFFF) == 0);
    CC(rt_rd32((ULONG_PTR)k_peb(), PEB_GdiDCAttributeList) == K_GDI_DC_ATTRIBUTE_LIST_INIT);
    CC(s.gdi_count == 0);                      /* creating a DC queues nothing */
    ddc(dc);
    return cbad;
}

TEST(r2_teb_peb_field_matrix_pristine_child_has_zero_win32_members) { parent_child("r2_teb_peb_field_matrix_c_pristine", 0); }
TEST(r2_teb_peb_field_matrix_user32_first_call_fills_thread_state) { parent_child("r2_teb_peb_field_matrix_c_user32_first", 0); }
TEST(r2_teb_peb_field_matrix_win32u_only_first_call_converts_without_user32) { parent_child("r2_teb_peb_field_matrix_c_win32u_first", 1); }
TEST(r2_teb_peb_field_matrix_gdi_first_call_maps_shared_table) { parent_child("r2_teb_peb_field_matrix_c_gdi_first", 1); }
