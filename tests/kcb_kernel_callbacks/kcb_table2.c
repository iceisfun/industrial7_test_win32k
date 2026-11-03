/* kcb_kernel_callbacks: structure of PEB->KernelCallbackTable beyond identity - which section each
 * entry lives in, the sharing pattern between slots, how the table appears and what the other side
 * of the channel (ntdll's dispatcher, NtCallbackReturn) looks like from user mode, and the callbacks
 * the kernel makes on its own initiative (ClientThreadSetup, NCDESTROY).
 *
 * Ground truth: user32.dll!apfnDispatch on 19041.2965) holds 129 function pointers
 * followed by a NULL qword. The kernel indexes it directly (win32kfull calls
 * KeUserModeCallback(0x54, ...) and slot 84 resolves to user32!__ClientThreadSetup, whose body ends
 * with NtCallbackReturn(0, 0, status)). Several message classes share one thunk, so the table has
 * 129 slots but only 112 distinct targets; that sharing pattern (not the RVAs) is stable across the
 * servicing builds of 19041..19045.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"
#include "kcb_echo.h"

/* kcb_group[i] = lowest slot index that has the same target as slot i on the reference build. */
static const unsigned char kcb_group[129] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,13,15,16,17,18,19,20,21,18,23,24,25,26,26,28,29,30,31,31,33,34,35,13,8,38,39,
    40,41,42,43,44,45,46,47,48,49,50,51,52,53,9,51,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,
    77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,6,97,98,99,100,101,102,103,53,105,30,107,30,109,30,111,
    112,113,13,13,116,117,118,119,120,121,122,123,124,125,39,44,128
};

static BOOL kcb_slot(unsigned i, void **out) {
    void *tbl = k_cb_table();
    *out = NULL;
    return tbl && k_try_read_ptr((char *)tbl + (size_t)i * sizeof(void *), out);
}
static BOOL kcb_user32_nt(HMODULE *m, IMAGE_NT_HEADERS64 *nt) {
    *m = GetModuleHandleA("user32.dll");
    if (!*m) return FALSE;
    IMAGE_DOS_HEADER dos;
    if (!k_try_read(*m, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    if (!k_try_read((char *)*m + dos.e_lfanew, nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return FALSE;
    return TRUE;
}
/* Characteristics of the section of the image that contains rva, or 0. */
static DWORD kcb_section_flags(HMODULE m, const IMAGE_NT_HEADERS64 *nt, DWORD rva) {
    IMAGE_DOS_HEADER dos;
    k_try_read(m, &dos, sizeof dos);
    char *sec0 = (char *)m + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && i < 96; i++) {
        IMAGE_SECTION_HEADER sh;
        if (!k_try_read(sec0 + i * sizeof sh, &sh, sizeof sh)) return 0;
        DWORD sz = sh.Misc.VirtualSize > sh.SizeOfRawData ? sh.Misc.VirtualSize : sh.SizeOfRawData;
        if (rva >= sh.VirtualAddress && rva < sh.VirtualAddress + sz) return sh.Characteristics;
    }
    return 0;
}

TEST(kcb_table2_every_entry_lies_in_an_executable_section) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    unsigned n = k_cb_count();
    REQUIRE(n > 0);
    HMODULE u32; IMAGE_NT_HEADERS64 nt;
    REQUIRE(kcb_user32_nt(&u32, &nt));
    if (t_is_wine()) { t_skip("Wine's table points at Wine's own user32 internals"); return; }
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++) {
        void *p;
        if (!kcb_slot(i, &p)) { bad++; continue; }
        DWORD fl = kcb_section_flags(u32, &nt, (DWORD)((ULONG_PTR)p - (ULONG_PTR)u32));
        /* An entry is a code address: its section must be executable (and not writable). */
        if (!(fl & IMAGE_SCN_MEM_EXECUTE) || (fl & IMAGE_SCN_MEM_WRITE)) { bad++; t_note("slot %u -> %p flags 0x%lx", i, p, (unsigned long)fl); }
    }
    CHECKF(bad == 0, "%u of %u slots are not in a read-only executable section of user32", bad, n);
}

TEST(kcb_table2_entries_begin_with_real_code) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    unsigned n = k_cb_count();
    REQUIRE(n > 0);
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++) {
        void *p; unsigned char b[8];
        if (!kcb_slot(i, &p) || !k_try_read(p, b, sizeof b)) { bad++; continue; }
        /* Never padding (0xCC) or zero fill, i.e. the pointer is the start of a function body. */
        if (b[0] == 0xCC || b[0] == 0x00) bad++;
    }
    CHECKF(bad == 0, "%u slots do not point at function code", bad);
}

TEST(kcb_table2_is_pointer_aligned_and_terminated) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    CHECKF(((ULONG_PTR)tbl & 7) == 0, "table %p not 8-byte aligned", tbl);
    unsigned n = k_cb_count();
    void *p = (void *)1;
    if (kcb_slot(n, &p)) CHECKF(p == NULL, "slot %u (first past the live entries) = %p, want NULL", n, p);
    else CHECK(t_is_wine());   /* on Windows the slot after the last entry is readable table memory */
    if (kcb_win10_2004_family()) {
        CHECKF(n == 129, "n=%u", n);
        void *q = (void *)1;
        CHECK(kcb_slot(129, &q));
        CHECK(q == NULL);
    }
}

TEST(kcb_table2_sharing_pattern_matches_reference) {
    if (!kcb_win10_2004_family()) { t_skip("needs a real 19041-19045 user32"); return; }
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    unsigned n = k_cb_count();
    REQUIRE(n == 129);
    void *t[129];
    for (unsigned i = 0; i < n; i++) REQUIRE(kcb_slot(i, &t[i]));
    unsigned mism = 0, distinct = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned first = i;
        for (unsigned j = 0; j < i; j++) if (t[j] == t[i]) { first = j; break; }
        if (first == i) distinct++;
        if (first != kcb_group[i]) { mism++; t_note("slot %u first shares target with slot %u, reference says %u", i, first, kcb_group[i]); }
    }
    CHECKF(mism == 0, "%u slots deviate from the reference sharing pattern", mism);
    EXPECT_EQ(distinct, 112);
}

TEST(kcb_table2_named_groups) {
    /* Spelled out from the same data for readability: slots dispatching the same user32 thunk. */
    if (!kcb_win10_2004_family()) { t_skip("needs a real 19041-19045 user32"); return; }
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    static const struct { unsigned a, b; const char *what; } same[] = {
        { 13, 14, "__fnINLPHELPINFOSTRUCT" }, { 13, 36, "__fnINLPHELPINFOSTRUCT" }, { 13, 114, "__fnINLPHELPINFOSTRUCT" },
        { 13, 115, "__fnINLPHELPINFOSTRUCT" }, { 18, 22, "__fnINOUTLPWINDOWPOS" }, { 26, 27, "__fnINSTRING" },
        { 31, 32, "__fnOUTDWORDDWORD" }, { 8, 37, "__fnINCNTOUTSTRINGNULL" }, { 9, 54, "__fnSHELLWINDOWMANAGEMENTNOTIFY" },
        { 51, 55, "__ClientCallDummyCallback" }, { 53, 104, "__fnSHELLWINDOWMANAGEMENTCALLOUT" },
        { 6, 96, "__fnGETTEXTLENGTHS" }, { 30, 106, "__fnINLPUAHNCPAINTMENUPOPUP" }, { 30, 108, "__fnINLPUAHNCPAINTMENUPOPUP" },
        { 30, 110, "__fnINLPUAHNCPAINTMENUPOPUP" }, { 39, 126, "__fnINOUTLPSIZE" }, { 44, 127, "__fnHkINLPNOTIFYSTRUCT" },
    };
    static const struct { unsigned a, b; } differ[] = { { 0, 1 }, { 2, 117 }, { 10, 15 }, { 12, 11 }, { 17, 18 }, { 20, 34 }, { 29, 28 }, { 35, 7 }, { 75, 74 } };
    for (unsigned i = 0; i < sizeof same / sizeof same[0]; i++) {
        void *a, *b;
        if (!kcb_slot(same[i].a, &a) || !kcb_slot(same[i].b, &b)) { CHECK(0); continue; }
        CHECKF(a == b, "slots %u and %u (%s) should share a thunk: %p vs %p", same[i].a, same[i].b, same[i].what, a, b);
    }
    for (unsigned i = 0; i < sizeof differ / sizeof differ[0]; i++) {
        void *a, *b;
        if (!kcb_slot(differ[i].a, &a) || !kcb_slot(differ[i].b, &b)) { CHECK(0); continue; }
        CHECKF(a != b, "slots %u and %u must be different thunks", differ[i].a, differ[i].b);
    }
}

static void *volatile g_thread_table;
static DWORD WINAPI kcb_read_table_thread(LPVOID arg) { (void)arg; g_thread_table = k_cb_table(); return 0; }
TEST(kcb_table2_same_table_seen_from_other_thread) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    void *mine = k_cb_table();
    REQUIRE(mine);
    /* The table is a PEB field (process-wide), not per-thread: a thread that never touched user32 sees it too. */
    g_thread_table = NULL;
    HANDLE th = CreateThread(NULL, 0, kcb_read_table_thread, NULL, 0, NULL);
    REQUIRE(th);
    WaitForSingleObject(th, 4000);
    CloseHandle(th);
    EXPECT_EQ(g_thread_table, mine);
}

CHILD(kcb_table2_rva_child) {
    (void)argc; (void)argv;
    t_proc("IsWindow");   /* loads user32 */
    void *tbl = k_cb_table();
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (!tbl || !u32) return 0x7FFFFFFE;
    return (int)(((ULONG_PTR)tbl - (ULONG_PTR)u32) & 0x7FFFFFFF);
}
TEST(kcb_table2_table_rva_is_the_same_in_other_processes) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    HMODULE u32 = GetModuleHandleA("user32.dll");
    REQUIRE(tbl); REQUIRE(u32);
    struct k_child_result r = k_child_run("kcb_table2_rva_child", NULL, 15000);
    DWORD mine = (DWORD)(((ULONG_PTR)tbl - (ULONG_PTR)u32) & 0x7FFFFFFF);
    /* Same user32 image in every process, so the static apfnDispatch array sits at the same RVA. */
    CHECKF(r.exit_code == mine, "child saw RVA 0x%lx, this process 0x%lx", (unsigned long)r.exit_code, (unsigned long)mine);
}

CHILD(kcb_table2_loadlib_child) {
    (void)argc; (void)argv;
    int fail = 0;
    if (k_cb_table() != NULL && !t_is_wine()) { printf("FAIL: table already set (%p) before user32 loaded\n", k_cb_table()); fail++; }
    /* user32!_UserClientDllInitialize runs at DLL_PROCESS_ATTACH and stores &apfnDispatch into
     * PEB+0x58 (analysis line: *(PEB + 0x58) = &apfnDispatch) - no win32k syscall of ours is needed. */
    HMODULE u32 = LoadLibraryA("user32.dll");
    if (!u32) { printf("FAIL: LoadLibrary(user32) failed\n"); return fail + 1; }
    void *tbl = k_cb_table();
    if (!tbl) { printf("FAIL: table still NULL after LoadLibrary(user32.dll)\n"); fail++; }
    else if (((ULONG_PTR)tbl & 7) != 0) { printf("FAIL: table %p misaligned (tag bits left set?)\n", tbl); fail++; }
    else if (!t_is_wine() && ((ULONG_PTR)tbl < (ULONG_PTR)u32 || (ULONG_PTR)tbl >= (ULONG_PTR)u32 + 0x200000)) { printf("FAIL: table %p outside user32 %p\n", tbl, (void *)u32); fail++; }
    return fail;
}
TEST(kcb_table2_appears_at_user32_load_in_pristine_child) {
    struct k_child_result r = k_child_run("kcb_table2_loadlib_child", NULL, 15000);
    CHECKF(r.exit_code == 0, "child exit=%lu", (unsigned long)r.exit_code);
}

/* ---- the far side of the channel in ntdll ---------------------------------------------- */

TEST(kcb_ntdll_kiusercallbackdispatcher_is_an_exported_code_address) {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    REQUIRE(nt);
    FARPROC p = GetProcAddress(nt, "KiUserCallbackDispatcher");
    if (!p) { if (t_is_wine()) { t_skip("Wine does not export it"); return; } CHECK(p != NULL); return; }
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(VirtualQuery((void *)p, &mbi, sizeof mbi));
    CHECK(mbi.AllocationBase == (void *)nt);
    CHECK((mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0);
    /* On x64 the dispatcher is reached only by the kernel, never called from user code: its first
     * byte must be readable code. */
    unsigned char b = 0;
    CHECK(k_try_read((void *)p, &b, 1));
}

TEST(kcb_ntdll_ntcallbackreturn_without_active_callback_fails) {
    /* NtCallbackReturn unwinds to the most recent KeUserModeCallback frame; with none active the
     * kernel returns STATUS_NO_CALLBACK_ACTIVE (0xC0000258) and the call simply comes back. */
    typedef LONG (NTAPI *NtCallbackReturn_t)(PVOID, ULONG, LONG);
    NtCallbackReturn_t fn = (NtCallbackReturn_t)t_proc_in("ntdll.dll", "NtCallbackReturn");
    REQUIRE(fn);
    LONG st = fn(NULL, 0, 0);
    CHECKF((ULONG)st == 0xC0000258u, "NtCallbackReturn(NULL,0,0) = 0x%lx want STATUS_NO_CALLBACK_ACTIVE", (unsigned long)st);
    char junk[16] = {0};
    st = fn(junk, sizeof junk, 0);
    CHECKF((ULONG)st == 0xC0000258u, "NtCallbackReturn(buf,16,0) = 0x%lx", (unsigned long)st);
}

/* ---- callbacks the kernel makes without any message being sent ---------------------------- */

static DWORD WINAPI kcb_first_syscall_thread(LPVOID arg) {
    (void)arg;
    /* First win32k syscall of a fresh thread (NtUserGetForegroundWindow): the kernel converts the
     * thread to a GUI thread and calls user32!__ClientThreadSetup (slot 84) back on it. */
    U(GetForegroundWindow)();
    return 0;
}
TEST(kcb_clientthreadsetup_runs_for_every_new_gui_thread) {
    REQUIRE(U(GetForegroundWindow));
    U(GetForegroundWindow)();
    if (k_cb_count() == 0) { t_skip("no callback table"); return; }
    CHECK(k_cb_hook(84) || t_is_wine());
    for (int i = 1; i <= 3; i++) {
        HANDLE th = CreateThread(NULL, 0, kcb_first_syscall_thread, NULL, 0, NULL);
        REQUIRE(th);
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
        if (!t_is_wine()) CHECKF(k_cb_hit_count(84) >= (unsigned long)i, "after %d new GUI threads slot 84 hits=%lu", i, k_cb_hit_count(84));
    }
    k_cb_unhook_all();
}

TEST(kcb_clientthreadsetup_not_repeated_on_same_thread) {
    REQUIRE(U(GetForegroundWindow));
    U(GetForegroundWindow)();   /* this thread is a GUI thread from here on */
    if (k_cb_count() == 0) { t_skip("no callback table"); return; }
    k_cb_hook(84);
    for (int i = 0; i < 20; i++) U(GetForegroundWindow)();
    if (!t_is_wine()) EXPECT_EQ(k_cb_hit_count(84), 0);
    k_cb_unhook_all();
}

TEST(kcb_destroywindow_calls_back_through_ncdestroy_slot_on_same_thread) {
    /* WM_NCDESTROY is sent by the kernel (window destruction) to the owning thread's procedure, so it
     * goes through the table even though sender and receiver are the same thread (contrast with
     * user-originated same-thread SendMessage, which never leaves user32). */
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("no callback table"); return; }
    struct kcb_echo e;
    if (!kcb_echo_start(&e, 0, "ncd", TRUE, TRUE)) { t_skip("could not create window"); return; }
    kcb_hook_all();
    DestroyWindow(e.hwnd);
    e.hwnd = NULL;
    if (!t_is_wine()) {
        CHECKF(k_cb_hit_count(3) >= 1, "slot 3 (__fnNCDESTROY) hits=%lu after DestroyWindow", k_cb_hit_count(3));
        EXPECT_EQ(k_cb_hit_count(10), 0);
    } else t_note("slot 3 hits=%lu under Wine", k_cb_hit_count(3));
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_create_window_lifecycle_with_all_slots_hooked) {
    /* Pass-through trampolines on every slot must be transparent: a whole window lifecycle with all
     * of them installed behaves exactly as without, and the creation/destruction slots count. */
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("no callback table"); return; }
    unsigned n = kcb_hook_all();
    CHECK(n == k_cb_count());
    struct kcb_echo e;
    BOOL ok = kcb_echo_start(&e, 0, "lc", FALSE, TRUE);
    if (!ok) { k_cb_unhook_all(); t_skip("could not create window"); return; }
    CHECK(U(IsWindow)(e.hwnd));
    if (!t_is_wine()) EXPECT_EQ(k_cb_hit_count(10), 2);
    SetWindowTextA(e.hwnd, "lifecycle");
    char b[32] = {0};
    GetWindowTextA(e.hwnd, b, sizeof b);
    /* the echo procedure owns WM_SETTEXT/WM_GETTEXT: it recorded the text and answers with its own */
    EXPECT_STR(e.text, "lifecycle");
    EXPECT_STR(b, "kcb-echo-text");
    HWND dead = e.hwnd;
    kcb_echo_stop(&e, 0);
    CHECK(!U(IsWindow)(dead));
    if (!t_is_wine()) CHECK(k_cb_hit_count(3) >= 1);
    k_cb_unhook_all();
    /* After the restore every slot holds its original again. */
    void *tbl = k_cb_table();
    for (unsigned i = 0; i < n; i++) {
        void *cur = NULL;
        k_try_read_ptr((char *)tbl + (size_t)i * sizeof(void *), &cur);
        if (cur != k_cb_original(i)) { CHECKF(0, "slot %u not restored", i); break; }
    }
}
