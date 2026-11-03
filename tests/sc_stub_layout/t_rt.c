/* sc_stub_layout: runtime behaviour at the syscall boundary - KUSER_SHARED_DATA selector, raw
 * SYSCALL vs. the stub, service-number range handling in KiSystemServiceStart, user32/gdi32
 * thunks that jump straight into win32u stubs, ntdll stubs for contrast, and pristine-process
 * GUI-thread conversion (children). */
#include "sc_common.h"

#define KUSER 0x7FFE0000ULL
UDECL(ULONG_PTR, NtUserGetThreadState, (ULONG_PTR state));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR param, ULONG_PTR fnid));
UDECL(ULONG_PTR, NtGdiFlush, (void));
UDECL(ULONG_PTR, NtUserGetDoubleClickTime, (void));

static unsigned rd32(ULONGLONG a) { unsigned long v = 0; k_try_read_u32((void *)(ULONG_PTR)a, &v); return (unsigned)v; }
static unsigned status32(K_NTSTATUS s) { return (unsigned)s; }

/* ---- KUSER_SHARED_DATA ---------------------------------------------------- */
TEST(sc_stub_layout_kuser_systemcall_selector) {
    /* every stub does `test byte [7FFE0308h],1; jne -> int 2e`. KUSER_SHARED_DATA.SystemCall must
     * be readable and (on any x64 build) a 0/1 selector: 0 = SYSCALL, 1 = INT 2E fallback. */
    unsigned long v = 0xFFFFFFFF;
    CHECK(k_try_read_u32((void *)(ULONG_PTR)(KUSER + 0x308), &v));
    CHECKF((v & ~1UL) == 0 || t_is_wine(), "SystemCall 0x%lx has bits besides the selector", v);
    t_note("KUSER_SHARED_DATA.SystemCall = 0x%lx (%s)", v, (v & 1) ? "int 2e path" : "syscall path");
}
TEST(sc_stub_layout_kuser_selector_operand_matches_stub) {
    if (sc_skip_wine("stub operand")) return;
    const unsigned char *p = sc_stub("NtUserGetThreadState"); REQUIRE(p);
    unsigned long disp = 0;
    CHECK(k_try_read_u32(p + 11, &disp));       /* F6 04 25 <disp32> imm8: absolute address operand */
    EXPECT_EQ(disp, KUSER + 0x308);
    unsigned char imm = 0; CHECK(k_try_read_u8(p + 15, &imm)); EXPECT_EQ(imm, 1);
}
TEST(sc_stub_layout_kuser_page_is_user_readonly) {
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery((void *)(ULONG_PTR)KUSER, &mbi, sizeof mbi));
    if (t_is_wine()) { t_note("Wine KUSER protect 0x%lx", mbi.Protect); return; }
    EXPECT_EQ(mbi.BaseAddress, KUSER);
    CHECK(mbi.Protect == PAGE_READONLY);
    CHECK(mbi.State == MEM_COMMIT);
}
TEST(sc_stub_layout_kuser_version_matches_rtlgetversion) {
    OSVERSIONINFOEXW vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi;
    typedef LONG (WINAPI *rgv_t)(OSVERSIONINFOEXW *);
    rgv_t rgv = (rgv_t)t_proc_in("ntdll.dll", "RtlGetVersion"); REQUIRE(rgv);
    CHECK(rgv(&vi) == 0);
    unsigned major = rd32(KUSER + 0x26C), minor = rd32(KUSER + 0x270), build = rd32(KUSER + 0x260) & 0xFFFF;
    t_note("KUSER NtMajor=%u NtMinor=%u NtBuild=%u; RtlGetVersion %lu.%lu.%lu", major, minor, build, vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
    if (t_is_wine()) return;
    EXPECT_EQ(major, vi.dwMajorVersion);
    EXPECT_EQ(minor, vi.dwMinorVersion);
    EXPECT_EQ(build, vi.dwBuildNumber);
    if (vi.dwBuildNumber != 19041) t_note("build %lu: the stub/number tables are for 19041", vi.dwBuildNumber);
}

/* ---- the three stubs checked against their known byte layout -------------- */
static void check_against_asm(const char *name, const unsigned char want[SC_STUB_LEN]) {
    if (sc_skip_wine("asm comparison")) return;
    const unsigned char *p = sc_stub(name); REQUIRE(p);
    unsigned char got[SC_STUB_LEN];
    CHECK(k_try_read(p, got, sizeof got));
    CHECKF(!memcmp(got, want, SC_STUB_LEN), "%s differs from the reference /asm", name);
}
TEST(sc_stub_layout_asm_NtUserGetThreadState) {
    static const unsigned char w[SC_STUB_LEN] = { 0x4c,0x8b,0xd1, 0xb8,0x00,0x10,0x00,0x00, 0xf6,0x04,0x25,0x08,0x03,0xfe,0x7f,0x01, 0x75,0x03, 0x0f,0x05, 0xc3, 0xcd,0x2e, 0xc3 };
    check_against_asm("NtUserGetThreadState", w);
}
TEST(sc_stub_layout_asm_NtGdiBitBlt) {
    static const unsigned char w[SC_STUB_LEN] = { 0x4c,0x8b,0xd1, 0xb8,0x08,0x10,0x00,0x00, 0xf6,0x04,0x25,0x08,0x03,0xfe,0x7f,0x01, 0x75,0x03, 0x0f,0x05, 0xc3, 0xcd,0x2e, 0xc3 };
    check_against_asm("NtGdiBitBlt", w);
}
TEST(sc_stub_layout_asm_NtUserCallOneParam) {
    static const unsigned char w[SC_STUB_LEN] = { 0x4c,0x8b,0xd1, 0xb8,0x02,0x10,0x00,0x00, 0xf6,0x04,0x25,0x08,0x03,0xfe,0x7f,0x01, 0x75,0x03, 0x0f,0x05, 0xc3, 0xcd,0x2e, 0xc3 };
    check_against_asm("NtUserCallOneParam", w);
}
TEST(sc_stub_layout_stub_ends_with_syscall_ret_at_fixed_offset) {
    /* the SYSCALL return address is stub+0x14 for every stub: the kernel resumes there */
    if (sc_skip_wine("syscall offset")) return;
    const unsigned char *p = sc_stub("NtUserGetThreadState"); REQUIRE(p);
    unsigned char b[2]; CHECK(k_try_read(p + 0x12, b, 2));
    CHECK(b[0] == 0x0F && b[1] == 0x05);
    CHECK(k_try_read(p + 0x14, b, 1) && b[0] == 0xC3);
}

/* ---- raw SYSCALL vs. the stub ------------------------------------------- */
#define NEED_NATIVE() do { if (sc_skip_wine("raw win32k syscall")) return; } while (0)

TEST(sc_stub_layout_raw_matches_stub_getthreadstate) {
    NEED_NATIVE();
    NtUserGetThreadState_pfn f = UN(NtUserGetThreadState); REQUIRE(f);
    /* state 7 = message extra info, 14 = constant, 10/15/5 booleans (win32kfull NtUserGetThreadState) */
    static const ULONG_PTR st[] = { 7, 10, 14, 15, 16, 100 };
    for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++) {
        ULONG_PTR a = f(st[i]);
        ULONG_PTR b = (ULONG_PTR)k_raw_syscall(0x1000, st[i], 0, 0, 0, 0, 0, 0, 0);
        CHECKF(a == b, "state %lu: stub %llx raw %llx", (unsigned long)st[i], (unsigned long long)a, (unsigned long long)b);
    }
}
TEST(sc_stub_layout_getthreadstate_semantics) {
    NEED_NATIVE();
    NtUserGetThreadState_pfn f = UN(NtUserGetThreadState); REQUIRE(f);
    EXPECT_EQ(f(14), 1);                         /* case 0xe: unconditional 1 */
    for (ULONG_PTR s = 16; s < 24; s++) EXPECT_EQ(f(s), 0);   /* default arm: 0 */
    EXPECT_EQ(f(0x7FFFFFFF), 0);
    EXPECT_EQ(f((ULONG_PTR)-1), 0);              /* int param: low dword -1 is below every case */
    EXPECT_EQ(f(0xDEADBEEF00000010ULL), 0);      /* parameter is an int: high dword of rcx ignored */
    EXPECT_EQ(f(0xDEADBEEF0000000EULL), 1);
    ULONG_PTR b5 = f(5), b10 = f(10), b15 = f(15);
    CHECK(b5 <= 1); CHECK(b10 <= 1); CHECK(b15 <= 1);   /* booleans: (x != 0), (a == b), bit 29 & 1 */
    ULONG_PTR s8 = f(8);                          /* 0, or {1,2,4} optionally | 8 */
    CHECKF(s8 == 0 || s8 == 1 || s8 == 2 || s8 == 4 || s8 == 9 || s8 == 10 || s8 == 12, "state 8 = %llx", (unsigned long long)s8);
}
TEST(sc_stub_layout_getthreadstate_equals_getmessageextrainfo) {
    /* user32!GetMessageExtraInfo forwards to NtUserGetThreadState with a constant code (19041) */
    NEED_NATIVE();
    NtUserGetThreadState_pfn f = UN(NtUserGetThreadState); REQUIRE(f);
    REQUIRE(U(GetMessageExtraInfo)); REQUIRE(U(SetMessageExtraInfo));
    LPARAM old = U(SetMessageExtraInfo)(0x1234);
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x1234);
    EXPECT_EQ(f(7), 0x1234);
    EXPECT_EQ(k_raw_syscall(0x1000, 7, 0, 0, 0, 0, 0, 0, 0), 0x1234);
    LPARAM prev = U(SetMessageExtraInfo)(old);    /* restore; returns the previous value */
    EXPECT_EQ(prev, 0x1234);
    EXPECT_EQ(f(7), (ULONG_PTR)old);
}
TEST(sc_stub_layout_callone_param_out_of_range_fnid_returns_zero) {
    /* win32kfull!NtUserCallOneParam - an fnid below
     * 0x2c or at/above 0x54 reaches no handler, whatever the parameter. */
    NEED_NATIVE();
    NtUserCallOneParam_pfn f = UN(NtUserCallOneParam); REQUIRE(f);
    static const ULONG_PTR fn[] = { 0, 1, 0x2b, 0x54, 0x55, 0x100, 0x1000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof fn / sizeof fn[0]; i++) {
        EXPECT_EQ(f(0, fn[i]), 0);
        EXPECT_EQ(f(0x12345678, fn[i]), 0);
        EXPECT_EQ(k_raw_syscall(0x1002, 0, fn[i], 0, 0, 0, 0, 0, 0), 0);
    }
    EXPECT_EQ(f(0, 0xABCD000000000000ULL | 0x2b), 0);        /* uint param: high dword ignored */
}
TEST(sc_stub_layout_gdiflush_returns_zero) {
    /* win32k.sys!NtGdiFlush thunk -> win32kbase!NtGdiFlush returns 0 */
    NEED_NATIVE();
    NtGdiFlush_pfn f = UN(NtGdiFlush); REQUIRE(f);
    EXPECT_EQ(f(), 0);
    EXPECT_EQ(k_raw_syscall(0x1012, 0, 0, 0, 0, 0, 0, 0, 0), 0);
}
TEST(sc_stub_layout_double_click_time_via_three_paths) {
    NEED_NATIVE();
    NtUserGetDoubleClickTime_pfn f = UN(NtUserGetDoubleClickTime); REQUIRE(f);
    REQUIRE(U(GetDoubleClickTime));
    UINT a = U(GetDoubleClickTime)();
    UINT b = (UINT)f();
    UINT c = (UINT)k_raw_syscall(0x10AE, 0, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(a, b); EXPECT_EQ(b, c);
    CHECK(a >= 1 && a <= 5000);                  /* SetDoubleClickTime documents 1..5000 ms */
}

/* ---- service number range ------------------------------------------------ */
TEST(sc_stub_layout_raw_index_beyond_table_is_invalid_service) {
    /* ntoskrnl KiSystemServiceStart: after the GUI-thread conversion an index at/above the table
     * limit returns STATUS_INVALID_SYSTEM_SERVICE (0xC000001C). 0xFFF is beyond any real limit. */
    NEED_NATIVE();
    static const unsigned n[] = { 0x1FFF, 0x1FFE, 0x1F00, 0x1800 };
    for (unsigned i = 0; i < sizeof n / sizeof n[0]; i++) {
        unsigned s = status32(k_raw_syscall(n[i], 0, 0, 0, 0, 0, 0, 0, 0));
        CHECKF(s == 0xC000001C, "service 0x%x -> 0x%08x", n[i], s);
    }
    unsigned s = status32(k_raw_syscall(0x1000 + SC_COUNT, 0, 0, 0, 0, 0, 0, 0, 0));
    t_note("service 0x%x (first index past the table) -> 0x%08x", 0x1000 + SC_COUNT, s);
}
TEST(sc_stub_layout_raw_selector_bit13_is_ignored) {
    /* KiSystemServiceStart derives the table from `eax >> 7 & 0x20` (bit 12 only) and the index from
     * eax & 0xfff, so 0x3000+i reaches the same win32k entry as 0x1000+i. */
    NEED_NATIVE();
    CHECK(status32(k_raw_syscall(0x3000, 14, 0, 0, 0, 0, 0, 0, 0)) == 1);       /* NtUserGetThreadState(14) */
    CHECK(status32(k_raw_syscall(0x1000, 14, 0, 0, 0, 0, 0, 0, 0)) == 1);
    CHECK(status32(k_raw_syscall(0x3012, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);        /* NtGdiFlush */
}

/* ---- user32 thunks jump straight into win32u ---------------------------- */
static const unsigned char *module_export(const char *dll, const char *name) { return (const unsigned char *)t_proc_in(dll, name); }

/* Decodes `[mov reg32, imm32;] [rex.W] jmp [rip+rel32]` and returns the IAT slot address, or NULL. */
static const unsigned char *thunk_slot(const unsigned char *fn, int *len_out) {
    unsigned char b[16];
    if (!fn || !k_try_read(fn, b, sizeof b)) return NULL;
    int off = 0;
    if (b[0] == 0xB9 || b[0] == 0xBA) off = 5;
    else if (b[0] == 0x41 && (b[1] == 0xB8 || b[1] == 0xB9)) off = 6;
    int end;
    if (b[off] == 0x48 && b[off + 1] == 0xFF && b[off + 2] == 0x25) end = off + 7;
    else if (b[off] == 0xFF && b[off + 1] == 0x25) end = off + 6;
    else return NULL;
    int rel; memcpy(&rel, b + end - 4, 4);
    if (len_out) *len_out = end;
    return fn + end + rel;
}
static void check_user32_thunk(const char *fn, const char *nt, const unsigned char *prefix, int prefix_len) {
    if (sc_skip_wine("user32 thunk")) return;
    const unsigned char *f = module_export("user32.dll", fn); REQUIRE(f);
    int len = 0;
    const unsigned char *slot = thunk_slot(f, &len);
    CHECKF(slot != NULL, "%s is not a direct import thunk", fn);
    if (!slot) return;
    if (prefix_len) { unsigned char b[8]; CHECK(k_try_read(f, b, prefix_len)); CHECK19(!memcmp(b, prefix, prefix_len)); }
    void *target = NULL;
    CHECK(k_try_read_ptr(slot, &target));
    CHECKF(target == (void *)sc_stub(nt), "%s jumps to %p, %s is at %p", fn, target, nt, sc_stub(nt));
    t_note("%s: %d-byte thunk, IAT slot user32+0x%llx", fn, len, (unsigned long long)(slot - (const unsigned char *)GetModuleHandleA("user32.dll")));
}
TEST(sc_stub_layout_user32_GetMessageExtraInfo_thunk) {
    static const unsigned char pre[5] = { 0xB9, 0x07, 0, 0, 0 };    /* mov ecx, 7 */
    check_user32_thunk("GetMessageExtraInfo", "NtUserGetThreadState", pre, 5);
}
TEST(sc_stub_layout_user32_GetDoubleClickTime_thunk) {
    check_user32_thunk("GetDoubleClickTime", "NtUserGetDoubleClickTime", NULL, 0);
}
static unsigned long tgt_off(const void *t, const unsigned char *base) { return (unsigned long)((const unsigned char *)t - base); }
TEST(sc_stub_layout_thunk_sweep_user32_gdi32) {
    /* every user32/gdi32/gdi32full export that is a pure import-thunk into win32u must land exactly
     * on a syscall stub entry (IAT binding goes by export address). */
    if (sc_skip_wine("thunk sweep")) return;
    static const char *const mods[] = { "user32.dll", "gdi32.dll", "gdi32full.dll" };
    static const unsigned char *stubs[SC_COUNT];
    for (unsigned i = 0; i < SC_COUNT; i++) stubs[i] = sc_stub(sc_tab[i].name);
    struct sc_pe wpe; int have_w = sc_pe_open("win32u.dll", &wpe);
    for (unsigned m = 0; m < 3; m++) {
        struct sc_pe pe;
        if (!sc_pe_open(mods[m], &pe)) { t_note("%s unavailable", mods[m]); continue; }
        int thunks = 0, to_win32u = 0;
        for (DWORD i = 0; i < pe.ed->NumberOfNames; i++) {
            DWORD rva = pe.funcs[pe.ords[i]];
            if (rva >= pe.ed_rva && rva < pe.ed_rva + pe.ed_size) continue;
            const IMAGE_SECTION_HEADER *s = sc_pe_section(&pe, rva);
            if (!s || !(s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const unsigned char *slot = thunk_slot(pe.base + rva, NULL);
            void *tgt;
            if (!slot || !k_try_read_ptr(slot, &tgt)) continue;
            thunks++;
            const unsigned char *w32u = (const unsigned char *)GetModuleHandleA("win32u.dll");
            if ((const unsigned char *)tgt < w32u || (const unsigned char *)tgt >= w32u + 0x40000) continue;
            to_win32u++;
            int hit = 0;
            for (unsigned k = 0; k < SC_COUNT && !hit; k++) hit = (stubs[k] == tgt);
            /* Which thunks are bound into win32u by the time this runs depends on the loader's binding state; whatever lands
             * there must be the address of a win32u export (the IAT is bound by export address), a stub entry for the
             * services this table knows and the dispatch table value otherwise. */
            if (!hit && have_w) {
                DWORD off = (DWORD)(tgt_off(tgt, w32u));
                for (DWORD e = 0; e < wpe.ed->NumberOfFunctions && !hit; e++) hit = (wpe.funcs[e] == off);
            }
            CHECKF(hit || (const unsigned char *)tgt == (const unsigned char *)GetProcAddress((HMODULE)w32u, "gDispatchTableValues"),
                   "%s!%s thunk lands in win32u at an address that is no export entry", mods[m], sc_pe_name(&pe, i));
        }
        t_info("%s: %d import-thunk exports, %d into win32u", mods[m], thunks, to_win32u);
    }
}

/* ---- ntdll contrast: a different service table --------------------------- */
TEST(sc_stub_layout_ntdll_stubs_use_table_zero) {
    /* ntdll's kernel stubs share the template but select table 0 (number < 0x1000): the two tables
     * are told apart purely by bit 12 of the immediate. */
    struct sc_pe pe;
    if (!sc_pe_open("ntdll.dll", &pe)) { t_skip("ntdll headers"); return; }
    int valid = 0;
    for (DWORD i = 0; i < pe.ed->NumberOfNames; i++) {
        const char *n = sc_pe_name(&pe, i);
        if (!n || n[0] != 'N' || n[1] != 't') continue;
        FARPROC p = GetProcAddress((HMODULE)pe.base, n);
        long num = k_stub_number(p);
        if (num < 0) continue;
        valid++;
        CHECKF(((unsigned long)num >> 12) == 0, "ntdll!%s number 0x%lx selects a non-zero table", n, num);
    }
    if (t_is_wine()) { t_note("Wine ntdll stubs: %d", valid); return; }
    CHECK(valid > 200);
    FARPROC close = GetProcAddress((HMODULE)pe.base, "NtClose");
    CHECK19(k_stub_number(close) == 0x0F);
}
TEST(sc_stub_layout_ntdll_does_not_export_win32k_syscalls) {
    HMODULE n = GetModuleHandleA("ntdll.dll"); REQUIRE(n);
    int hits = 0;
    for (unsigned i = 0; i < SC_COUNT; i++) if (GetProcAddress(n, sc_tab[i].name)) hits++;
    if (hits) t_note("ntdll exports %d of the win32u syscall names", hits);
    if (!t_is_wine()) CHECK(hits == 0);
}

/* ---- pristine process: first win32k syscall converts the thread ---------- */
static void *teb_u(unsigned off) { void *v = NULL; k_try_read_ptr((char *)k_teb() + off, &v); return v; }
static void *peb_u(unsigned off) { void *v = NULL; k_try_read_ptr((char *)k_peb() + off, &v); return v; }

CHILD(sc_stub_layout_pristine_conversion) {
    (void)argc; (void)argv;
    int bad = 0;
    #define CC(c) do { if (!(c)) { bad++; } CHECK(c); } while (0)
    /* only win32u.dll has been loaded (by the child entry path: not even that). */
    void *pre = teb_u(TEB_Win32ThreadInfo);
    void *gdi_pre = peb_u(PEB_GdiSharedHandleTable);
    printf("  NOTE before: Win32ThreadInfo=%p GdiSharedHandleTable=%p KernelCallbackTable=%p\n", pre, gdi_pre, peb_u(PEB_KernelCallbackTable));
    if (pre) { printf("  NOTE thread already has Win32ThreadInfo (user32 injected?); skipping\n"); return 0; }
    if (t_is_wine()) { printf("  NOTE Wine: skipping syscall conversion\n"); return 0; }
    FARPROC st = t_proc_in("win32u.dll", "NtUserGetThreadState");
    CC(st != NULL);
    if (!st) return bad;
    ULONG_PTR r = ((NtUserGetThreadState_pfn)st)(14);   /* constant 1 regardless of queue state */
    void *post = teb_u(TEB_Win32ThreadInfo);
    printf("  NOTE first win32k syscall returned %llx; Win32ThreadInfo=%p GdiSharedHandleTable=%p\n", (unsigned long long)r, post, peb_u(PEB_GdiSharedHandleTable));
    /* PsConvertToGuiThread runs before the service: the call must have been dispatched. */
    CC(r == 1);   /* state 14 is the unconditional constant 1 (NtUserGetThreadState); 0xC000001C would mean no dispatch */
    CC(post != NULL);                 /* PsConvertToGuiThread installed the THREADINFO pointer */
    if (post) CC((ULONG_PTR)post >= 0xFFFF800000000000ULL);   /* THREADINFO lives in kernel space; user32 uses ClientInfo.ulClientDelta to reach it */
    return bad;
}
TEST(sc_stub_layout_pristine_first_win32k_syscall_in_child) {
    struct k_child_result r = k_child_run("sc_stub_layout_pristine_conversion", NULL, 20000);
    /* On Windows 10 the first win32k service of a process that never loaded user32 converts the thread to a
     * GUI thread, which makes the kernel call back into user mode (client thread setup) through a callback
     * table that does not exist yet (the table is installed by user32's initialisation). The callback cannot
     * be delivered and the process dies with STATUS_FATAL_USER_CALLBACK_EXCEPTION; the child never reaches
     * the code after the syscall. user32 must therefore be loaded first (see the next test). */
    if (t_is_wine()) EXPECT_EQ(r.exit_code, 0);
    else EXPECT_EQ(r.exit_code, 0xC000041Dul);
}
CHILD(sc_stub_layout_user32_load_state) {
    (void)argc; (void)argv;
    int bad = 0;
    void *pre = teb_u(TEB_Win32ThreadInfo);
    if (pre) { printf("  NOTE Win32ThreadInfo preset; skipping\n"); return 0; }
    HMODULE u = LoadLibraryA("user32.dll");
    CC(u != NULL);
    if (!u) return bad;
    void *afterload = teb_u(TEB_Win32ThreadInfo);
    void *cb = peb_u(PEB_KernelCallbackTable);
    printf("  NOTE after LoadLibrary(user32): Win32ThreadInfo=%p KernelCallbackTable=%p\n", afterload, cb);
    typedef LPARAM (WINAPI *gmei_t)(void);
    gmei_t g = (gmei_t)GetProcAddress(u, "GetMessageExtraInfo");
    CC(g != NULL);
    if (g) g();                       /* tail-jumps into win32u NtUserGetThreadState(7) */
    void *post = teb_u(TEB_Win32ThreadInfo);
    printf("  NOTE after GetMessageExtraInfo: Win32ThreadInfo=%p\n", post);
    if (!t_is_wine()) {
        CC(post != NULL);               /* user32 code reads TEB+0x78 as the "is a GUI thread" flag */
        CC(cb != NULL);
        printf("  NOTE GdiSharedHandleTable=%p\n", peb_u(PEB_GdiSharedHandleTable));
    }
    return bad;
}
TEST(sc_stub_layout_pristine_user32_load_in_child) {
    struct k_child_result r = k_child_run("sc_stub_layout_user32_load_state", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}
