/* win32k_mitigation / syscalls: what a Win32k-locked process sees when it executes win32u stubs.
 *
 * Kernel path (ntoskrnl, Windows 10 19041): a win32k service number (>= 0x1000) executed by a thread that is not
 * yet a GUI thread reaches KiSystemServiceStart's "edi == 0x20" tail, which calls KiConvertToGuiThread ->
 * PsConvertToGuiThread. That function returns STATUS_ACCESS_DENIED when EPROCESS+0x9d0 & 0x1000 (disallow) is set,
 * *before* PsInvokeWin32Callout, so no W32PROCESS/W32THREAD is created and win32k never sees the call. The tail
 * only tests the result for zero; on failure it indexes a signed-byte table after the win32k offset array
 * (KeServiceDescriptorTableShadow + 0x20): a value <= 0 is returned as the status, otherwise 0xC000001C
 * (STATUS_INVALID_SYSTEM_SERVICE), also used when the index is beyond the table. So the user-visible status is
 * STATUS_INVALID_SYSTEM_SERVICE (or 0 for a service the table whitelists), never STATUS_ACCESS_DENIED.
 * The stubs are called with all-zero arguments: the service body never runs in a locked process. */
#include "mit.h"

static const char *k_status(unsigned long st) { return k_status_name((K_NTSTATUS)st); }

SCN(stub_call) {
    const char *name = mit_arg();
    FARPROC f = mit_w32u(name);
    MCF(f != NULL, "win32u export %s missing", name);
    if (!f) return;
    long num = k_stub_number(f);
    MCF(k_stub_valid(f), "%s prologue", name);
    MCF(num >= 0x1000, "%s service number 0x%lx", name, num);
    SetLastError(1234);
    unsigned long st = mit_call_stub(f);
    MCF(mit_status_ok(st), "%s -> %s", name, k_status(st));
    if (st == 0) t_note("%s returned 0 in a locked process (whitelisted by the service-table tail)", name);
    else t_note("%s (0x%lx) -> %s", name, num, k_status(st));
    MEQ(GetLastError(), 1234);               /* the stub is just SYSCALL; nothing sets the Win32 last error */
    unsigned long st2 = mit_call_stub(f);
    MST(st2, st);                            /* deterministic */
    MC(mit_unconverted());
    mit_check_pristine_state(name);
}

#define STUB_LIST(X) \
    X(NtUserGetThreadState) X(NtUserGetDC) X(NtGdiCreateCompatibleDC) X(NtUserGetDCEx) X(NtUserProcessConnect) \
    X(NtUserInitializeClientPfnArrays) X(NtUserGetForegroundWindow) X(NtUserPeekMessage) X(NtUserGetMessage) \
    X(NtUserFindWindowEx) X(NtUserCreateWindowEx) X(NtGdiCreateSolidBrush) X(NtGdiOpenDCW) X(NtGdiInit) \
    X(NtUserOpenWindowStation) X(NtUserGetProcessWindowStation) X(NtGdiCreateBitmap) X(NtGdiCreateDIBSection) \
    X(NtGdiFlush) X(NtUserCallNoParam) X(NtUserCallOneParam) X(NtUserGetKeyState) X(NtUserGetAsyncKeyState) \
    X(NtUserSystemParametersInfo) X(NtGdiGetDeviceCaps) X(NtGdiDeleteObjectApp) X(NtUserGetGuiResources) \
    X(NtGdiCreatePen) X(NtGdiCreateCompatibleBitmap) X(NtUserOpenDesktop) X(NtUserSetProcessWindowStation) \
    X(NtUserRegisterClassExWOW) X(NtUserGetKeyboardLayout) X(NtUserGetClipboardOwner) X(NtUserGetAtomName) \
    X(NtUserSetTimer) X(NtUserGetSystemMenu) X(NtGdiGetDCDword) X(NtGdiSelectBitmap) X(NtUserBuildHwndList) \
    X(NtUserMessageCall) X(NtUserQueryWindow) X(NtUserGetObjectInformation) X(NtUserShowWindow) X(NtUserDestroyWindow) \
    X(NtGdiBitBlt) X(NtUserEnumDisplayMonitors) X(NtGdiHfontCreate) X(NtUserReleaseDC)
#define STUB_TESTS(nm) \
    MIT_TEST(stub_##nm##_set, "stub_call", #nm, "set") \
    MIT_TEST(stub_##nm##_cre, "stub_call", #nm, "cre")
STUB_LIST(STUB_TESTS)

/* ---- export enumeration --------------------------------------------------------------------- */
typedef void (*export_cb)(const char *name, FARPROC addr, void *ctx);
static int enum_exports(HMODULE m, export_cb cb, void *ctx) {
    unsigned char *base = (unsigned char *)m;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd.VirtualAddress) return 0;
    IMAGE_EXPORT_DIRECTORY *ed = (IMAGE_EXPORT_DIRECTORY *)(base + dd.VirtualAddress);
    DWORD *names = (DWORD *)(base + ed->AddressOfNames), *funcs = (DWORD *)(base + ed->AddressOfFunctions);
    WORD *ords = (WORD *)(base + ed->AddressOfNameOrdinals);
    int n = 0;
    for (DWORD i = 0; i < ed->NumberOfNames; i++) {
        DWORD rva = funcs[ords[i]];
        if (rva >= dd.VirtualAddress && rva < dd.VirtualAddress + dd.Size) continue;   /* forwarder */
        cb((const char *)(base + names[i]), (FARPROC)(base + rva), ctx);
        n++;
    }
    return n;
}
struct sweep { int total, e1c, zero, other, invalid, minus1; char m1_nm[16][48]; unsigned long other_st[8]; char other_nm[8][64]; };
static void sweep_cb(const char *name, FARPROC addr, void *ctx) {
    struct sweep *s = (struct sweep *)ctx;
    if (name[0] != 'N' || name[1] != 't') return;
    if (!k_stub_valid(addr)) { s->invalid++; return; }
    if (k_stub_number(addr) < 0x1000) return;
    unsigned long st = mit_call_stub(addr);
    s->total++;
    if (st == ST_INVALID_SYSTEM_SERVICE) s->e1c++;
    else if (st == 0) s->zero++;
    else if (st == 0xFFFFFFFFul) { if (s->minus1 < 16) snprintf(s->m1_nm[s->minus1], 48, "%s", name); s->minus1++; }   /* a few services (GDI, MIT, menu, activation, display) answer -1 as their own failure value */
    else { if (s->other < 8) { s->other_st[s->other] = st; snprintf(s->other_nm[s->other], 64, "%s", name); } s->other++; }
}
SCN(sweep_all_stubs) {
    HMODULE m = LoadLibraryA("win32u.dll");
    struct sweep s; memset(&s, 0, sizeof s);
    MC(m != NULL);
    if (!m) return;
    enum_exports(m, sweep_cb, &s);
    t_note("win32u sweep: %d win32k stubs, %d x STATUS_INVALID_SYSTEM_SERVICE, %d x 0, %d other, %d non-standard prologues",
           s.total, s.e1c, s.zero, s.other, s.invalid);
    t_note("%d services answer 0xFFFFFFFF (-1) instead of the status", s.minus1);
    MEQ(s.e1c + s.zero + s.minus1 + s.other, s.total);
    for (int i = 0; i < s.minus1 && i < 16; i++) t_note("  -1 from %s", s.m1_nm[i]);
    MCF(s.total > 500, "only %d win32k stubs found", s.total);
    for (int i = 0; i < s.other && i < 8; i++) MCF(0, "%s -> %s (not 0xC000001C / 0)", s.other_nm[i], k_status(s.other_st[i]));
    MCF(s.e1c > s.zero, "a locked process must fail most services (%d fail, %d success)", s.e1c, s.zero);
    MC(mit_unconverted());
    mit_check_pristine_state("after sweep");
}
MIT_LOCKED2(sweep_all_stubs, "sweep_all_stubs", "")

SCN(sweep_twice_same) {
    HMODULE m = LoadLibraryA("win32u.dll");
    struct sweep a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    MC(m != NULL);
    if (!m) return;
    enum_exports(m, sweep_cb, &a);
    enum_exports(m, sweep_cb, &b);
    MEQ(a.total, b.total); MEQ(a.e1c, b.e1c); MEQ(a.zero, b.zero);   /* the service-table tail is stateless */
    MC(mit_unconverted());
}
MIT_LOCKED2(sweep_twice_same, "sweep_twice_same", "")

/* ---- argument variants ---------------------------------------------------------------------- */
typedef ULONG_PTR (WINAPI *stub1_fn)(ULONG_PTR);
typedef ULONG_PTR (WINAPI *stub3_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR);
SCN(thread_state_infotypes) {
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    for (ULONG_PTR i = 0; i < 24; i++) {
        unsigned long st = (unsigned long)((stub1_fn)f)(i);
        MCF(mit_status_ok(st), "NtUserGetThreadState(%lu) -> %s", (unsigned long)i, k_status(st));
    }
    MC(mit_unconverted());
}
SCN(getdc_variants) {
    FARPROC f = mit_w32u("NtUserGetDC"), g = mit_w32u("NtUserGetDCEx");
    MC(f && g); if (!f || !g) return;
    static const ULONG_PTR hw[] = { 0, 1, 0x10010, 0xFFFFFFFF, 0xFFFF000000000000ULL };
    for (unsigned i = 0; i < sizeof hw / sizeof *hw; i++) {
        unsigned long st = (unsigned long)((stub1_fn)f)(hw[i]);
        MCF(mit_status_ok(st), "NtUserGetDC(%llx) -> %s", (unsigned long long)hw[i], k_status(st));
        st = (unsigned long)((stub3_fn)g)(hw[i], 0, 0x1B);
        MCF(mit_status_ok(st), "NtUserGetDCEx(%llx) -> %s", (unsigned long long)hw[i], k_status(st));
    }
    MC(mit_unconverted());
}
SCN(kernel_pointers_not_probed) {
    /* The locked path returns before any service body, so even a kernel-range argument is never dereferenced. */
    FARPROC f = mit_w32u("NtUserRegisterClassExWOW");
    MC(f != NULL); if (!f) return;
    unsigned long st = (unsigned long)((stub3_fn)f)(0xFFFF800000000000ULL, 0xFFFF800000000000ULL, 0);
    MCF(mit_status_ok(st), "-> %s", k_status(st));
    MC(mit_unconverted());
}
MIT_LOCKED2(thread_state_infotypes, "thread_state_infotypes", "")
MIT_LOCKED2(getdc_variants, "getdc_variants", "")
MIT_LOCKED2(kernel_pointers_not_probed, "kernel_pointers_not_probed", "")

SCN(raw_syscall_matches_stub) {
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    long num = k_stub_number(f);
    MC(num >= 0x1000); if (num < 0x1000) return;
    unsigned long a = mit_call_stub(f);
    unsigned long b = (unsigned long)k_raw_syscall((unsigned long)num, 0, 0, 0, 0, 0, 0, 0, 0);
    MST(b, a);
    MC(mit_status_ok(b));
    MC(mit_unconverted());
}
SCN(raw_syscall_out_of_range) {
    /* Index 0xFFF is beyond the win32k service limit: the tail's jae sends it to 0xC000001C. */
    unsigned long st = (unsigned long)k_raw_syscall(0x1FFF, 0, 0, 0, 0, 0, 0, 0, 0);
    MST(st, ST_INVALID_SYSTEM_SERVICE);
    st = (unsigned long)k_raw_syscall(0x1800, 0, 0, 0, 0, 0, 0, 0, 0);
    MST(st, ST_INVALID_SYSTEM_SERVICE);
    MC(mit_unconverted());
}
MIT_LOCKED2(raw_syscall_matches_stub, "raw_syscall_matches_stub", "")
MIT_LOCKED2(raw_syscall_out_of_range, "raw_syscall_out_of_range", "")

/* ---- state a locked process must keep ------------------------------------------------------- */
SCN(state_pristine_at_start) { mit_check_pristine_state("at start"); }
SCN(state_pristine_after_win32u_load) {
    /* win32u.dll has no DllMain that talks to win32k: loading it creates no W32PROCESS. */
    HMODULE m = LoadLibraryA("win32u.dll");
    MC(m != NULL);
    mit_check_pristine_state("after LoadLibrary(win32u)");
}
struct tctx { FARPROC f; unsigned long st; unsigned long long ti; };
static DWORD WINAPI tstate_fn(LPVOID p) { struct tctx *t = (struct tctx *)p; t->st = mit_call_stub(t->f); t->ti = mit_teb64(TEB_Win32ThreadInfo); return 0; }
SCN(state_stable_across_threads) {
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    for (int i = 0; i < 4; i++) {
        struct tctx c = { f, 0xFFFFFFFF, 0xFFFFFFFF };
        MC(mit_thread(tstate_fn, &c, 10000));
        MCF(mit_status_ok(c.st), "thread %d -> %s", i, k_status(c.st));
        MEQ(c.ti, 0);                      /* the worker's TEB stays unconverted too */
    }
    MC(mit_unconverted());
}
MIT_LOCKED2(state_pristine_at_start, "state_pristine_at_start", "")
MIT_LOCKED2(state_pristine_after_win32u_load, "state_pristine_after_win32u_load", "")
MIT_LOCKED2(state_stable_across_threads, "state_stable_across_threads", "")

/* Control: the same stubs in an unlocked pristine process are not blocked (only observed through a
 * harmless call: NtUserGetThreadState(0) = GetQueueStatus-less state query; the child converts to a GUI thread). */
SCN(control_unlocked_converts) {
    MIT_NEED_WIN32K();
    /* On Windows 10 a bare win32u call from a process that has not loaded user32 dies (the client callback table does not
     * exist yet), so the conversion is made by the user32 load; the stub call afterwards is then served normally. */
    MC(LoadLibraryA("user32.dll") != NULL);
    MC(!mit_unconverted());
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    unsigned long st = (unsigned long)((stub1_fn)f)(0);
    MC(!mit_unconverted());                /* PsConvertToGuiThread succeeded: TEB.Win32ThreadInfo is the thread id marker */
    MCF(st != ST_INVALID_SYSTEM_SERVICE, "unlocked process got %s", k_status(st));
    MC(mit_peb64(PEB_GdiSharedHandleTable) != 0);   /* GdiProcessCallout mapped the shared handle table */
}
MIT_TEST(control_unlocked_converts, "control_unlocked_converts", "", "plain")
