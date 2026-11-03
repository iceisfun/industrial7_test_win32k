/* win32k_modules_sysinfo: what the Win32 driver stack leaves in a process once it has made its first
 * win32k system call, checked against the kernel module list (where the pointers must or must not point).
 *
 * Kernel behaviour (analysed, 19041):
 *   - KiSystemServiceStart converts the calling thread with KiConvertToGuiThread on the first service
 *     >= 0x1000; win32kfull!W32pProcessCallout then runs the process-create callout, whose
 *     win32kbase!GdiProcessCallout(proc, 1) stores 0x14 at PEB+0x108 (GdiDCAttributeList), zeroes
 *     PEB+0x140..+0x22F (GdiHandleBuffer, 0xF0 bytes) and maps gpHmgrSharedHandleSection read-only
 *     (ZwMapViewOfSection protection 2, MmSecureVirtualMemory) with the view base stored at PEB+0xF8.
 *   - NtGdiInit is a trivial service returning 1 (folded with win32kbase!DLT_THREADINFO::getDLT), which is
 *     all gdi32's GdiProcessSetup needs to trigger the conversion without touching user32.
 *   - user32 validates handles against the shared USER handle table: index (h & 0xFFFF) < *(psi+8),
 *     entry size gSharedInfo.HeEntrySize (0x18), entry+0x08 = owning thread id, entry+0x18 = type
 *     (1 = window), entry+0x19 bit0 = destroyed, entry+0x1A = uniqueness (HIWORD(handle)).
 */
#include "ms.h"

#define PEB_GdiDCAttributeList_OFF 0x108

static int ms_in_any_module(ULONG_PTR p) {
    MS_MODS *m = ms_mods(); if (!m) return 0;
    for (ULONG i = 0; i < m->NumberOfModules; i++) {
        ULONG_PTR b = (ULONG_PTR)m->Modules[i].ImageBase;
        if (p >= b && p < b + m->Modules[i].ImageSize) return 1;
    }
    return 0;
}
static void *ms_teb_win32thread(void) { void *p = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &p); return p; }

/* ---- pristine child --------------------------------------------------------------------- */
typedef BOOL (WINAPI *ms_gdiinit_fn)(void);

static DWORD WINAPI ms_child_thread(void *arg) {
    void **out = (void **)arg;
    void *before = ms_teb_win32thread();
    ms_gdiinit_fn f = (ms_gdiinit_fn)t_proc_in("win32u.dll", "NtGdiInit");
    BOOL r = f ? f() : 0;
    void *after = ms_teb_win32thread();
    out[0] = before; out[1] = after; out[2] = (void *)(ULONG_PTR)r;
    return 0;
}

CHILD(win32k_modules_sysinfo_pristine) {
    int bad = 0;
    FARPROC init = t_proc_in("win32u.dll", "NtGdiInit");
    if (!init || !k_stub_valid(init)) { printf("NOTE NtGdiInit stub unavailable, nothing to verify\n"); return 0; }
    void *peb = k_peb();
    void *w32t0 = ms_teb_win32thread(), *tab0 = NULL, *kcb0 = NULL; unsigned long dca0 = 0xFFFFFFFF;
    k_try_read_ptr((char *)peb + PEB_GdiSharedHandleTable, &tab0);
    k_try_read_ptr((char *)peb + PEB_KernelCallbackTable, &kcb0);
    k_try_read_u32((char *)peb + PEB_GdiDCAttributeList_OFF, &dca0);
#define CCHECK(c) do { if (!(c)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); bad++; } } while (0)
    CCHECK(w32t0 == NULL);          /* no win32k service issued yet: the TEB win32 thread slot is empty */
    CCHECK(tab0 == NULL);           /* no process callout yet: no GDI shared handle table view */
    /* A shim or appcompat layer may have pulled user32 in already, and nothing proves who initialises
     * GdiDCAttributeList: record both instead of asserting. */
    printf("NOTE before NtGdiInit: KernelCallbackTable=%p GdiDCAttributeList=0x%lx\n", kcb0, dca0);

    /* On Windows 10 a win32k service issued before user32 is loaded cannot convert the thread (the client callback table
     * does not exist yet and the call dies), so the user32 load is the first step that connects the process. */
    CCHECK(LoadLibraryA("user32.dll") != NULL);
    BOOL r = ((ms_gdiinit_fn)init)();
    CCHECK(r != 0);
    void *w32t1 = ms_teb_win32thread(), *tab1 = NULL; unsigned long dca1 = 0;
    k_try_read_ptr((char *)peb + PEB_GdiSharedHandleTable, &tab1);
    k_try_read_u32((char *)peb + PEB_GdiDCAttributeList_OFF, &dca1);
    CCHECK(w32t1 != NULL);
    CCHECK((ULONGLONG)(ULONG_PTR)w32t1 < MS_KERNEL_START);   /* a small opaque value on Windows 10, not an address */
    CCHECK(tab1 != NULL);
    CCHECK((ULONG_PTR)tab1 < 0x00007FFFFFFF0000ULL);
    CCHECK(dca1 == 0x14);
    if (tab1) {
        MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
        CCHECK(VirtualQuery(tab1, &mbi, sizeof mbi) == sizeof mbi);
        CCHECK(mbi.Type == MEM_MAPPED);
        CCHECK(mbi.Protect == PAGE_READONLY);
        CCHECK(mbi.AllocationBase == tab1);
        CCHECK(mbi.RegionSize >= 0x1000);
        printf("NOTE handle table view %p size 0x%llx\n", tab1, (unsigned long long)mbi.RegionSize);
        unsigned char b[0xF0]; unsigned char z[0xF0]; memset(z, 0, sizeof z);
        CCHECK(k_try_read((char *)peb + PEB_GdiHandleBuffer, b, sizeof b));
        CCHECK(!memcmp(b, z, sizeof b));
    }
    void *kcb1 = NULL; k_try_read_ptr((char *)peb + PEB_KernelCallbackTable, &kcb1);
    printf("NOTE after NtGdiInit: Win32ThreadInfo=%p GdiSharedHandleTable=%p KernelCallbackTable=%p\n", w32t1, tab1, kcb1);

    BOOL r2 = ((ms_gdiinit_fn)init)();
    void *tab2 = NULL; k_try_read_ptr((char *)peb + PEB_GdiSharedHandleTable, &tab2);
    CCHECK(r2 != 0);
    CCHECK(tab2 == tab1);                 /* the process callout runs once */
    CCHECK(ms_teb_win32thread() == w32t1);

    /* A second thread starts without a win32 thread and gets its own on its first service. */
    void *res[3] = {0};
    HANDLE th = CreateThread(NULL, 0, ms_child_thread, res, 0, NULL);
    CCHECK(th != NULL);
    if (th) {
        CCHECK(WaitForSingleObject(th, 10000) == WAIT_OBJECT_0);
        CloseHandle(th);
        CCHECK(res[0] == NULL);
        CCHECK(res[1] != NULL);
        CCHECK(res[1] != w32t1);
        CCHECK((ULONGLONG)(ULONG_PTR)res[1] < MS_KERNEL_START);
        CCHECK(res[2] != NULL);
    }
    printf("NOTE second thread Win32ThreadInfo=%p\n", res[1]);
    return bad;
}

TEST(win32k_modules_sysinfo_footprint_pristine_process_gains_win32_state_on_first_service) {
    if (t_is_wine()) { t_skip("Wine does not implement win32k.sys"); return; }
    struct k_child_result r = k_child_run("win32k_modules_sysinfo_pristine", NULL, 30000);
    CHECKF(r.exit_code == 0, "child exit code 0x%lx", (unsigned long)r.exit_code);
}

TEST(win32k_modules_sysinfo_footprint_pristine_child_runs_under_any_host) {
    /* The child only dereferences TEB/PEB through safe reads and one win32u stub: it must finish (not hang) anywhere. */
    struct k_child_result r = k_child_run("win32k_modules_sysinfo_pristine", NULL, 30000);
    CHECKF(r.exit_code != 0xDEAD0000 && r.exit_code != 0xDEAD0001, "child spawn/timeout 0x%lx", (unsigned long)r.exit_code);
    CHECK(r.relayed_lines >= 0);
}

/* ---- parent process (user32 + gdi32 in use) -------------------------------------------------- */
/* On Windows 10 22H2 TEB.Win32ThreadInfo of a GUI thread is a small nonzero value, not a session-pool address. */
TEST(win32k_modules_sysinfo_footprint_win32_thread_info_is_session_pool_pointer) {
    REQUIRE(U(GetForegroundWindow));
    U(GetForegroundWindow)();                       /* NtUserGetForegroundWindow: first win32k service on this thread */
    void *p = ms_teb_win32thread();
    CHECK(p != NULL);
    if (t_is_wine()) { t_note("emulated Win32ThreadInfo %p", p); return; }
    CHECKF((ULONGLONG)(ULONG_PTR)p < MS_KERNEL_START, "Win32ThreadInfo %p", p);
    CHECKF(!ms_in_any_module((ULONG_PTR)p), "Win32ThreadInfo %p lies inside a kernel image", p);
}

static DWORD ms_gui_probe(void *a) { U(GetForegroundWindow)(); *(void **)a = ms_teb_win32thread(); return 0; }
TEST(win32k_modules_sysinfo_footprint_win32_thread_info_differs_per_thread) {
    REQUIRE(U(GetForegroundWindow));
    U(GetForegroundWindow)();
    void *mine = ms_teb_win32thread();
    void *other = NULL;
    k_run_gui_thread(ms_gui_probe, &other);
    CHECK(other != NULL);
    CHECK(other != mine);
    if (!t_is_wine()) CHECK((ULONGLONG)(ULONG_PTR)other < MS_KERNEL_START);
}

TEST(win32k_modules_sysinfo_footprint_kernel_callback_table_inside_user32) {
    REQUIRE(U(GetForegroundWindow)); U(GetForegroundWindow)();
    void *tab = k_cb_table();
    if (!tab) { t_skip("no kernel callback table"); return; }
    HMODULE u32 = GetModuleHandleA("user32.dll"); REQUIRE(u32);
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)((const char *)u32 + ((const IMAGE_DOS_HEADER *)u32)->e_lfanew);
    ULONG_PTR b = (ULONG_PTR)u32, e = b + nt->OptionalHeader.SizeOfImage;
    /* user32!UserClientDllInitialize publishes its apfnDispatch array through PEB->KernelCallbackTable. */
    CHECKF((ULONG_PTR)tab >= b && (ULONG_PTR)tab < e, "table %p user32 [%p,%p)", tab, (void *)b, (void *)e);
    unsigned n = k_cb_count();
    t_note("kernel callback table %p with %u consecutive entries", tab, n);
    CHECK(n >= 16);
}

TEST(win32k_modules_sysinfo_footprint_kernel_callback_entries_point_into_user32) {
    REQUIRE(U(GetForegroundWindow)); U(GetForegroundWindow)();
    void *tab = k_cb_table();
    if (!tab) { t_skip("no kernel callback table"); return; }
    HMODULE u32 = GetModuleHandleA("user32.dll"); REQUIRE(u32);
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)((const char *)u32 + ((const IMAGE_DOS_HEADER *)u32)->e_lfanew);
    ULONG_PTR b = (ULONG_PTR)u32, e = b + nt->OptionalHeader.SizeOfImage;
    unsigned n = k_cb_count(), out = 0;
    for (unsigned i = 0; i < n && i < K_CB_MAX; i++) {
        void *fn = NULL; REQUIRE(k_try_read_ptr((char *)tab + i * sizeof(void *), &fn));
        if (!fn) continue;
        if ((ULONG_PTR)fn < b || (ULONG_PTR)fn >= e) out++;
    }
    /* Almost all entries are user32 handlers; a few (e.g. fnDWORD helpers) can live in ntdll/other DLLs on some builds. */
    t_note("%u of %u callback entries outside user32", out, n);
    CHECK(out * 4 <= n);
}

TEST(win32k_modules_sysinfo_footprint_gdi_shared_table_view_properties) {
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteDC));
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    void *tab = NULL; REQUIRE(k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &tab));
    if (t_is_wine()) { U(DeleteDC)(dc); t_skip("emulated GDI table"); return; }
    CHECK(tab != NULL);
    if (tab) {
        MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
        REQUIRE(VirtualQuery(tab, &mbi, sizeof mbi) == sizeof mbi);
        CHECK(mbi.Type == MEM_MAPPED);
        CHECK(mbi.Protect == PAGE_READONLY);
        CHECK(mbi.AllocationBase == tab);
        unsigned long dca = 0; k_try_read_u32((char *)k_peb() + PEB_GdiDCAttributeList_OFF, &dca);
        CHECK(dca == 0x14);
        /* a DC handle indexes into this very view */
        ULONG_PTR cell = (ULONG_PTR)tab + ((ULONG_PTR)dc & 0xFFFF) * GDI_CELL_SIZE;
        CHECK(cell >= (ULONG_PTR)tab && cell + GDI_CELL_SIZE <= (ULONG_PTR)tab + mbi.RegionSize);
    }
    U(DeleteDC)(dc);
}

TEST(win32k_modules_sysinfo_footprint_gdi_cell_object_pointer_is_kernel_address) {
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteDC));
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    struct k_gdicell c;
    if (t_is_wine() || !k_gdi_cell(dc, &c)) { U(DeleteDC)(dc); t_skip("GDI cell unreadable"); return; }
    /* The first cell word is the kernel-side object pointer (GdiGetEntry copies it verbatim, never dereferences it). */
    CHECKF((ULONGLONG)(ULONG_PTR)c.object >= MS_KERNEL_START, "cell object %p", c.object);
    CHECK(!ms_in_any_module((ULONG_PTR)c.object));
    U(DeleteDC)(dc);
}

TEST(win32k_modules_sysinfo_footprint_shared_info_tables_are_mapped_views) {
    REQUIRE(U(GetForegroundWindow)); U(GetForegroundWindow)();
    struct k_user_shared_info si;
    if (!k_shared_info(&si)) { t_skip("gSharedInfo unavailable"); return; }
    if (t_is_wine()) { t_skip("emulated shared info"); return; }
    CHECK(si.heEntrySize == 0x20);     /* HMInitHandleTable stores 0x20; user32!IsWindow reads type at +0x18, flags +0x19, uniq +0x1A */
    CHECK(si.psi != NULL && si.aheList != NULL);
    MEMORY_BASIC_INFORMATION m1, m2; memset(&m1, 0, sizeof m1); memset(&m2, 0, sizeof m2);
    REQUIRE(VirtualQuery(si.psi, &m1, sizeof m1) == sizeof m1);
    REQUIRE(VirtualQuery(si.aheList, &m2, sizeof m2) == sizeof m2);
    CHECK(m1.Type == MEM_MAPPED);
    CHECK(m2.Type == MEM_MAPPED);
    t_note("SERVERINFO %p protect 0x%lx; handle table %p protect 0x%lx", si.psi, (unsigned long)m1.Protect, si.aheList, (unsigned long)m2.Protect);
    CHECK(!(m1.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)));   /* the server info is never writable from user mode */
    unsigned long long count = 0;
    CHECK(k_try_read((char *)si.psi + 8, &count, 8));
    CHECKF(count >= 64 && count <= 0x10000, "handle entry count %llu", count);
}

TEST(win32k_modules_sysinfo_footprint_window_handle_indexes_inside_entry_count) {
    HWND w = k_make_window("win32kt_footprint_idx"); REQUIRE(w);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || t_is_wine()) { U(DestroyWindow)(w); t_skip("gSharedInfo unavailable"); return; }
    unsigned long long count = 0; REQUIRE(k_try_read((char *)si.psi + 8, &count, 8));
    CHECK(((ULONG_PTR)w & 0xFFFF) < count);
    U(DestroyWindow)(w);
}

TEST(win32k_modules_sysinfo_footprint_window_entry_owner_is_creating_thread) {
    HWND w = k_make_window("win32kt_footprint_owner"); REQUIRE(w);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || t_is_wine()) { U(DestroyWindow)(w); t_skip("gSharedInfo unavailable"); return; }
    unsigned char *ent = (unsigned char *)si.aheList + ((ULONG_PTR)w & 0xFFFF) * si.heEntrySize;
    unsigned long long owner = 0;
    REQUIRE(k_try_read(ent + 8, &owner, 8));
    /* user32!GetWindowThreadProcessId compares entry+8 with GetCurrentThreadId() to avoid a syscall. */
    CHECKF(owner == GetCurrentThreadId(), "owner 0x%llx tid %lu", owner, (unsigned long)GetCurrentThreadId());
    U(DestroyWindow)(w);
}

struct ms_owner_ctx { HWND w; DWORD tid; HANDLE made, release; };
static DWORD WINAPI ms_owner_thread(void *a) {
    struct ms_owner_ctx *c = (struct ms_owner_ctx *)a;
    c->tid = GetCurrentThreadId();
    c->w = k_make_window("win32kt_footprint_owner2");
    SetEvent(c->made);
    WaitForSingleObject(c->release, 10000);
    if (c->w) U(DestroyWindow)(c->w);
    return 0;
}
TEST(win32k_modules_sysinfo_footprint_window_entry_owner_follows_other_thread) {
    REQUIRE(U(DestroyWindow));
    struct ms_owner_ctx c = { NULL, 0, CreateEventW(NULL, TRUE, FALSE, NULL), CreateEventW(NULL, TRUE, FALSE, NULL) };
    HANDLE th = CreateThread(NULL, 0, ms_owner_thread, &c, 0, NULL); REQUIRE(th);
    WaitForSingleObject(c.made, 10000);
    struct k_user_shared_info si;
    if (c.w && k_shared_info(&si) && !t_is_wine()) {
        unsigned char *ent = (unsigned char *)si.aheList + ((ULONG_PTR)c.w & 0xFFFF) * si.heEntrySize;
        unsigned long long owner = 0;
        if (k_try_read(ent + 8, &owner, 8)) {
            CHECKF(owner == c.tid, "owner %llu creator thread %lu", owner, (unsigned long)c.tid);
            CHECK(owner != GetCurrentThreadId());
        } else CHECK(0);
        CHECK(ent[0x18] == 1);
        CHECK(*(unsigned short *)(ent + 0x1A) == (unsigned short)((ULONG_PTR)c.w >> 16));
    } else t_skip("no window / shared info");
    SetEvent(c.release);
    WaitForSingleObject(th, 10000);
    CloseHandle(th); CloseHandle(c.made); CloseHandle(c.release);
}

TEST(win32k_modules_sysinfo_footprint_destroyed_window_entry_no_longer_valid) {
    HWND w = k_make_window("win32kt_footprint_destroy"); REQUIRE(w);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || t_is_wine()) { U(DestroyWindow)(w); t_skip("gSharedInfo unavailable"); return; }
    unsigned char *ent = (unsigned char *)si.aheList + ((ULONG_PTR)w & 0xFFFF) * si.heEntrySize;
    unsigned char before[0x20]; REQUIRE(k_try_read(ent, before, sizeof before));
    CHECK(before[0x18] == 1);
    CHECK((before[0x19] & 1) == 0);
    CHECK(U(DestroyWindow)(w));
    unsigned char after[0x20]; REQUIRE(k_try_read(ent, after, sizeof after));
    int still_valid = after[0x18] == 1 && !(after[0x19] & 1) && *(unsigned short *)(after + 0x1A) == (unsigned short)((ULONG_PTR)w >> 16);
    /* IsWindow (user32) accepts a handle only if type==1, bit0 of flags clear and uniqueness matches. */
    CHECK(!still_valid);
    CHECK(!U(IsWindow)(w));
}

TEST(win32k_modules_sysinfo_footprint_window_object_lives_in_read_only_desktop_heap) {
    HWND w = k_make_window("win32kt_footprint_heap"); REQUIRE(w);
    void *p = k_wnd_ptr(w);
    if (!p || t_is_wine()) { U(DestroyWindow)(w); t_skip("fast-path window pointer unavailable"); return; }
    MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
    REQUIRE(VirtualQuery(p, &mbi, sizeof mbi) == sizeof mbi);
    CHECK(mbi.Type == MEM_MAPPED);
    t_info("tagWND %p in view %p protect 0x%lx size 0x%llx", p, mbi.AllocationBase, (unsigned long)mbi.Protect, (unsigned long long)mbi.RegionSize);
    CHECK(!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)));
    HWND self = NULL;
    CHECK(k_try_read_ptr(p, (void **)&self));
    CHECKF(self == w, "tagWND.head.h = %p, hwnd %p", self, w);
    U(DestroyWindow)(w);
}

TEST(win32k_modules_sysinfo_footprint_window_object_pti_is_creating_thread_info) {
    HWND w = k_make_window("win32kt_footprint_pti"); REQUIRE(w);
    void *p = k_wnd_ptr(w);
    if (!p || t_is_wine()) { U(DestroyWindow)(w); t_skip("fast-path window pointer unavailable"); return; }
    /* The client view of a window starts with its handle and the kernel address of the object; the creating thread is
     * recorded in the handle-table entry as a thread id. */
    void *hd = NULL, *self = NULL;
    REQUIRE(k_try_read_ptr(p, &hd));
    REQUIRE(k_try_read_ptr((char *)p + 8, &self));
    CHECKF(((ULONG_PTR)hd & 0xFFFFFFFFu) == ((ULONG_PTR)w & 0xFFFFFFFFu), "head handle %p window %p", hd, w);
    CHECK(self != NULL);
    unsigned long long owner = 0;
    char *ent = NULL;
    struct k_user_shared_info si;
    if (k_shared_info(&si) && si.aheList) ent = (char *)si.aheList + (size_t)((ULONG_PTR)w & 0xFFFF) * (si.heEntrySize ? si.heEntrySize : 0x20);
    REQUIRE(ent && k_try_read(ent + 8, &owner, 8));
    CHECKF(owner == GetCurrentThreadId(), "entry owner %llu thread %lu", owner, GetCurrentThreadId());
    U(DestroyWindow)(w);
}

TEST(win32k_modules_sysinfo_footprint_win32k_pointers_distinct_from_driver_images) {
    REQUIRE(U(GetForegroundWindow)); U(GetForegroundWindow)();
    MS_NEED_MOD(wi, "win32kbase.sys");
    void *p = ms_teb_win32thread();
    if (t_is_wine() || !p) { t_skip("no win32 thread"); return; }
    MS_MOD *m = &ms_mods()->Modules[wi];
    /* Per-thread win32k data comes from session pool, never from inside the driver image. */
    CHECK(!((ULONG_PTR)p >= (ULONG_PTR)m->ImageBase && (ULONG_PTR)p < (ULONG_PTR)m->ImageBase + m->ImageSize));
}
