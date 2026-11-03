/* Syscall-stub level facts: service numbers, gdi32 forwarders into win32u, gdi32-local wrappers. */
#include "d.h"

static int in_module(FARPROC p, const char *mod) {
    HMODULE m = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m)) return 0;
    return m == GetModuleHandleA(mod);
}

/* tools/win32u_syscalls.tsv: every in-scope stub sits in the win32k service table (>= 0x1000). */
TEST(gdi_d3dkmt_dd_stub_exist_and_shape) {
    for (int i = 0; i < dk_num_cnt; i++) {
        FARPROC p = t_proc_in("win32u.dll", dk_num_tab[i].name);
        CHECKF(p != NULL, "win32u export %s", dk_num_tab[i].name);
        if (p) CHECKF(k_stub_valid(p), "%s prologue shape", dk_num_tab[i].name);
    }
}
TEST(gdi_d3dkmt_dd_stub_service_numbers) {
    if (t_is_wine()) { t_skip("Wine win32u uses its own syscall numbering"); return; }
    for (int i = 0; i < dk_num_cnt; i++) {
        FARPROC p = t_proc_in("win32u.dll", dk_num_tab[i].name);
        if (!p || !k_stub_valid(p)) continue;
        CHECKF(k_stub_number(p) == dk_num_tab[i].num, "%s number %ld want %d", dk_num_tab[i].name, k_stub_number(p), dk_num_tab[i].num);
    }
}
TEST(gdi_d3dkmt_dd_stub_numbers_in_win32k_table) {
    if (t_is_wine()) { t_skip("Wine numbering"); return; }
    /* the win32k service table starts at 0x1000 (KiServiceTable shadow); table index lives in bits 12..13 */
    for (int i = 0; i < dk_num_cnt; i++) {
        CHECKF(dk_num_tab[i].num >= 0x1000 && dk_num_tab[i].num < 0x2000, "%s %d", dk_num_tab[i].name, dk_num_tab[i].num);
    }
}
TEST(gdi_d3dkmt_dd_stub_ddi_block_contiguous) {
    if (t_is_wine()) { t_skip("Wine numbering"); return; }
    /* The NtGdiDdDDI* names sort alphabetically in win32k.sys's table, so the numbers are consecutive in the
     * order of the .tsv; assert strict monotonic growth across the NtGdiDdDDI block (no gaps from other families). */
    int prev = -1, n = 0;
    for (int i = 0; i < dk_num_cnt; i++) {
        if (strncmp(dk_num_tab[i].name, "NtGdiDdDDI", 10)) continue;
        if (prev >= 0) CHECKF(dk_num_tab[i].num == prev + 1, "%s %d after %d", dk_num_tab[i].name, dk_num_tab[i].num, prev);
        prev = dk_num_tab[i].num; n++;
    }
    CHECK(n > 150);
}

TEST(gdi_d3dkmt_dd_forwarders_resolve_to_win32u) {
    if (t_is_wine()) { t_skip("Wine gdi32 does not forward D3DKMT through win32u by name"); return; }
    int checked = 0;
    for (int i = 0; i < dk_fwd_cnt; i++) {
        FARPROC a = t_proc_in("gdi32.dll", dk_fwd_tab[i].name);
        FARPROC b = t_proc_in("win32u.dll", dk_fwd_tab[i].target);
        CHECKF(a != NULL && b != NULL, "%s -> %s", dk_fwd_tab[i].name, dk_fwd_tab[i].target);
        if (a && b) { CHECKF(a == b, "%s resolves to %p, win32u!%s is %p", dk_fwd_tab[i].name, a, dk_fwd_tab[i].target, b); checked++; }
    }
    CHECK(checked > 150);
}
TEST(gdi_d3dkmt_dd_forwarders_land_in_win32u_module) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (int i = 0; i < dk_fwd_cnt; i++) {
        FARPROC a = t_proc_in("gdi32.dll", dk_fwd_tab[i].name);
        if (a) CHECKF(in_module(a, "win32u.dll"), "%s", dk_fwd_tab[i].name);
    }
}
TEST(gdi_d3dkmt_dd_forwarders_adapter_family) {
    static const char *const n[] = { "EnumAdapters", "EnumAdapters2", "CloseAdapter", "OpenAdapterFromDeviceName", "OpenAdapterFromLuid", "QueryAdapterInfo", "CheckVidPnExclusiveOwnership", "GetDisplayModeList", "GetScanLine", "WaitForVerticalBlankEvent", "GetSharedPrimaryHandle", "QueryStatistics", "Escape" };
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (size_t i = 0; i < sizeof n / sizeof n[0]; i++) {
        char d[96], w[96];
        snprintf(d, sizeof d, "D3DKMT%s", n[i]); snprintf(w, sizeof w, "NtGdiDdDDI%s", n[i]);
        FARPROC a = t_proc_in("gdi32.dll", d), b = t_proc_in("win32u.dll", w);
        CHECKF(a && a == b, "%s vs %s", d, w);
    }
}
TEST(gdi_d3dkmt_dd_forwarders_device_family) {
    static const char *const n[] = { "CreateDevice", "DestroyDevice", "CreateContext", "DestroyContext", "DestroyAllocation", "DestroyAllocation2", "Lock", "Unlock", "Render", "Present", "GetDeviceState", "MakeResident", "Evict" };
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (size_t i = 0; i < sizeof n / sizeof n[0]; i++) {
        char d[96], w[96];
        snprintf(d, sizeof d, "D3DKMT%s", n[i]); snprintf(w, sizeof w, "NtGdiDdDDI%s", n[i]);
        FARPROC a = t_proc_in("gdi32.dll", d), b = t_proc_in("win32u.dll", w);
        CHECKF(a && a == b, "%s vs %s", d, w);
    }
}
TEST(gdi_d3dkmt_dd_forwarders_renamed_targets) {
    /* D3DKMT exports whose win32u target does not carry the same suffix (decl. from the gdi32 export forwarder strings). */
    static const struct { const char *e, *t; } v[] = {
        { "D3DKMTCreateAllocation2", "NtGdiDdDDICreateAllocation" },
        { "D3DKMTCreateSynchronizationObject2", "NtGdiDdDDICreateSynchronizationObject" },
        { "D3DKMTOpenResource2", "NtGdiDdDDIOpenResource" },
        { "D3DKMTSetVidPnSourceOwner2", "NtGdiDdDDISetVidPnSourceOwner" },
        { "D3DKMTSignalSynchronizationObject2", "NtGdiDdDDISignalSynchronizationObject" },
        { "D3DKMTWaitForSynchronizationObject2", "NtGdiDdDDIWaitForSynchronizationObject" },
        { "D3DKMTNetDispStartMiracastDisplayDeviceEx", "NtGdiDdDDINetDispStartMiracastDisplayDevice" },
        { "D3DKMTEnumAdapters3", "NtDxgkEnumAdapters3" },
        { "D3DKMTDuplicateHandle", "NtDxgkDuplicateHandle" },
        { "D3DKMTPinResources", "NtDxgkPinResources" },
        { "D3DKMTUnpinResources", "NtDxgkUnpinResources" },
        { "D3DKMTGetProcessList", "NtDxgkGetProcessList" },
    };
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        FARPROC a = t_proc_in("gdi32.dll", v[i].e), b = t_proc_in("win32u.dll", v[i].t);
        CHECKF(a && a == b, "%s -> %s", v[i].e, v[i].t);
    }
}
TEST(gdi_d3dkmt_dd_gdi32_local_wrappers_not_forwarded) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    /* 16 D3DKMT exports are real gdi32 code (OpenAdapterFromHdc, CreateAllocation, SetVidPnSourceOwner, ...) */
    int n = 0;
    for (int i = 0; i < dk_local_cnt; i++) {
        FARPROC a = t_proc_in("gdi32.dll", dk_local_tab[i]);
        CHECKF(a != NULL, "%s", dk_local_tab[i]);
        if (!a) continue;
        CHECKF(in_module(a, "gdi32.dll"), "%s should live in gdi32", dk_local_tab[i]);
        n++;
    }
    CHECK(n == dk_local_cnt);
}
TEST(gdi_d3dkmt_dd_local_wrappers_are_not_syscall_stubs) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (int i = 0; i < dk_local_cnt; i++) {
        FARPROC a = t_proc_in("gdi32.dll", dk_local_tab[i]);
        if (a) CHECKF(!k_stub_valid(a), "%s must not be a bare SYSCALL stub", dk_local_tab[i]);
    }
}
TEST(gdi_d3dkmt_dd_win32u_has_no_unprefixed_d3dkmt) {
    /* win32u exports Nt* names only; the D3DKMT* spellings are gdi32's */
    if (t_is_wine()) { t_skip("Wine"); return; }
    for (int i = 0; i < dk_fwd_cnt; i++) CHECKF(t_proc_in("win32u.dll", dk_fwd_tab[i].name) == NULL, "%s", dk_fwd_tab[i].name);
}

/* Raw SYSCALL with the number decoded from the stub must behave exactly like the stub. */
TEST(gdi_d3dkmt_dd_raw_syscall_close_adapter_matches_stub) {
    FARPROC p = t_proc_in("win32u.dll", "NtGdiDdDDICloseAdapter");
    REQUIRE(p);
    if (t_is_wine() || !k_stub_valid(p)) { t_skip("raw syscall needs Windows numbering"); return; }
    dk_closeargs c = { 0 };
    LONG a = dk_close(0);
    LONG b = (LONG)k_raw_syscall((unsigned long)k_stub_number(p), (ULONG_PTR)&c, 0, 0, 0, 0, 0, 0, 0);
    CHECKF(a == b, "stub 0x%lx raw 0x%lx", (unsigned long)a, (unsigned long)b);
    CHECK(!NTOK(b));
}
TEST(gdi_d3dkmt_dd_raw_syscall_enum_adapters2_matches_stub) {
    FARPROC p = t_proc_in("win32u.dll", "NtGdiDdDDIEnumAdapters2");
    REQUIRE(p);
    if (t_is_wine() || !k_stub_valid(p)) { t_skip("raw syscall needs Windows numbering"); return; }
    dk_enum2 a = { 0, NULL }, b = { 0, NULL };
    LONG sa = ((dk_fn1)p)(&a);
    LONG sb = (LONG)k_raw_syscall((unsigned long)k_stub_number(p), (ULONG_PTR)&b, 0, 0, 0, 0, 0, 0, 0);
    CHECKF(sa == sb, "0x%lx vs 0x%lx", (unsigned long)sa, (unsigned long)sb);
    CHECKF(a.NumAdapters == b.NumAdapters, "%lu vs %lu", (unsigned long)a.NumAdapters, (unsigned long)b.NumAdapters);
}
