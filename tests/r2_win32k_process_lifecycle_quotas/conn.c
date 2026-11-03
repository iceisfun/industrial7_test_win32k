/* conn: state of a process that is connected to win32k, as left by user32!_UserClientDllInitialize /
 * ClientThreadSetup. gSharedInfo is the 0x238-byte SHAREDINFO copied out of the USERCONNECT reply
 * (user32 copies reply+8 .. reply+0x240, see ClientThreadSetup). Layout (x64):
 *   +0x000 psi  +0x008 aheList  +0x010 HeEntrySize(ULONG)  +0x018 pDispInfo  +0x020 ulSharedDelta
 *   +0x028 awmControl[31] {ULONG maxMsgs; BYTE *abMsgs}  +0x218 DefWindowMsgs  +0x228 DefWindowSpecMsgs
 */
#include "r2.h"

#define GSI_SIZE 0x238
#define GSI_AWM  0x28
#define GSI_DEFW 0x218
#define GSI_DEFS 0x228

UDECL(LONG, NtUserProcessConnect, (ULONG_PTR, ULONG_PTR, ULONG_PTR));

static char *gsi(void) { return (char *)k_shared_info_ptr(); }
static ULONG_PTR user_max(void) { SYSTEM_INFO si; GetSystemInfo(&si); return (ULONG_PTR)si.lpMaximumApplicationAddress; }
static int gsi_snapshot(unsigned char *buf) { char *g = gsi(); return g && k_try_read(g, buf, GSI_SIZE); }

TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_whole_struct_readable) {
    R2_NEED_WIN();
    unsigned char b[GSI_SIZE];
    /* ClientThreadSetup copies exactly 0x238 bytes into gSharedInfo */
    CHECK(gsi_snapshot(b));
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_pointers_are_user_mode) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    void *psi = NULL, *ahe = NULL, *disp = NULL;
    CHECK(k_try_read_ptr(g + 0x00, &psi)); CHECK(k_try_read_ptr(g + 0x08, &ahe)); CHECK(k_try_read_ptr(g + 0x18, &disp));
    /* the client views of SERVERINFO, the handle table and DISPLAYINFO live in the user half */
    CHECK(psi && (ULONG_PTR)psi < user_max());
    CHECK(ahe && (ULONG_PTR)ahe < user_max());
    CHECK(disp && (ULONG_PTR)disp < user_max());
    CHECK(psi != ahe);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_entry_size_covers_entry_fields) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    unsigned long sz = 0; CHECK(k_try_read_u32(g + 0x10, &sz));
    /* user32!IsWindow reads the entry type at +0x18, flags at +0x19 and uniqueness at +0x1a of an entry addressed
     * with stride HeEntrySize, so a stride below 0x1c would make those bytes belong to the next entry */
    CHECKF(sz >= 0x1c && (sz % 8) == 0, "HeEntrySize 0x%lx", sz);
    t_note("HeEntrySize = 0x%lx", sz);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_aheList_mapped_readonly) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    void *ahe = NULL; CHECK(k_try_read_ptr(g + 8, &ahe)); CHECK(ahe);
    MEMORY_BASIC_INFORMATION m;
    CHECK(r2_vq(ahe, &m));
    CHECK(m.State == MEM_COMMIT);
    CHECK(m.Type == MEM_MAPPED);                       /* section view of the session handle table */
    CHECK(m.Protect == PAGE_READONLY);                 /* clients never write the USER handle table */
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_psi_mapped_readonly_same_section_type) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    void *psi = NULL; CHECK(k_try_read_ptr(g, &psi)); CHECK(psi);
    MEMORY_BASIC_INFORMATION m;
    CHECK(r2_vq(psi, &m));
    CHECK(m.State == MEM_COMMIT); CHECK(m.Type == MEM_MAPPED); CHECK(m.Protect == PAGE_READONLY);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_dispinfo_view_note) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    void *d = NULL; CHECK(k_try_read_ptr(g + 0x18, &d)); CHECK(d);
    MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(d, &m));
    CHECK(m.State == MEM_COMMIT);
    CHECK(m.Protect == PAGE_READONLY || m.Protect == PAGE_READWRITE);
    t_note("pDispInfo view: type 0x%lx protect 0x%lx regionsize 0x%llx", (unsigned long)m.Type, (unsigned long)m.Protect, (unsigned long long)m.RegionSize);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_shared_delta_note) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    ULONG_PTR delta = 0; CHECK(k_try_read_ptr(g + 0x20, (void **)&delta));
    /* added to kernel addresses stored in the shared monitor/DISPLAYINFO structures to reach the client view: the
     * stored addresses are kernel-range, the view user-range, so the delta cannot be zero */
    CHECK(delta != 0);
    t_note("ulSharedDelta = 0x%llx", (unsigned long long)delta);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_awmcontrol_tables_readable) {
    R2_NEED_WIN();
    char *g = gsi(); REQUIRE(g);
    int nz = 0;
    for (int i = 0; i < 31 + 2; i++) {
        char *e = g + (i < 31 ? GSI_AWM + i * 16 : (i == 31 ? GSI_DEFW : GSI_DEFS));
        unsigned long max = 0; void *bits = NULL;
        CHECK(k_try_read_u32(e, &max)); CHECK(k_try_read_ptr(e + 8, &bits));
        if (max) {
            nz++;
            CHECKF(bits != NULL, "WNDMSG %d has maxMsgs %lu but no bitmap", i, max);
            /* abMsgs is a bit array indexed by message number: it must cover maxMsgs bits */
            unsigned char tmp[1024]; size_t n = (max + 7) / 8; if (n > sizeof tmp) n = sizeof tmp;
            CHECKF(bits && k_try_read(bits, tmp, n), "WNDMSG %d bitmap unreadable (%lu msgs)", i, max);
            CHECK(max <= 0x10000);
        }
    }
    t_note("%d of 33 WNDMSG tables non-empty", nz);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_stable_across_threads) {
    R2_NEED_WIN();
    static unsigned char a[GSI_SIZE], b[GSI_SIZE];
    CHECK(gsi_snapshot(a));
    DWORD WINAPI th(LPVOID p) { (void)U(GetSystemMetrics)(SM_CXSCREEN); return gsi_snapshot(b) ? 0 : 1; }
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL);
    REQUIRE(h);
    EXPECT_EQ(r2_wait_thread(h, 5000), 0); CloseHandle(h);
    /* process-wide: every thread sees the same SHAREDINFO bytes */
    CHECK(!memcmp(a, b, GSI_SIZE));
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gsi_reload_user32_keeps_state) {
    R2_NEED_WIN();
    unsigned char a[GSI_SIZE], b[GSI_SIZE];
    CHECK(gsi_snapshot(a));
    HMODULE u1 = GetModuleHandleA("user32.dll");
    HMODULE u2 = LoadLibraryA("user32.dll");
    /* DLL_PROCESS_ATTACH ran once; a second load only bumps the refcount and does not reconnect */
    CHECK(u1 != NULL && u1 == u2);
    CHECK(gsi_snapshot(b)); CHECK(!memcmp(a, b, GSI_SIZE));
    if (u2) FreeLibrary(u2);
    CHECK(GetModuleHandleA("user32.dll") == u1);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_second_connect_refused_state_untouched) {
    R2_NEED_WIN();
    REQUIRE(UN(NtUserProcessConnect));
    unsigned char a[GSI_SIZE], b[GSI_SIZE], out[0x240];
    CHECK(gsi_snapshot(a));
    memset(out, 0xCC, sizeof out);
    *(DWORD *)out = 0x00050000;
    /* win32kfull!NtUserProcessConnect returns STATUS_UNSUCCESSFUL before touching its arguments unless the caller is csrss */
    LONG r = UN(NtUserProcessConnect)((ULONG_PTR)-1, (ULONG_PTR)out, 0x240);
    RST(r, 0xC0000001);
    CHECK(out[4] == 0xCC && out[0x100] == 0xCC && out[0x23f] == 0xCC);
    CHECK(gsi_snapshot(b)); CHECK(!memcmp(a, b, GSI_SIZE));
}
TEST(r2_win32k_process_lifecycle_quotas_conn_second_connect_refused_for_bad_arguments) {
    R2_NEED_WIN();
    REQUIRE(UN(NtUserProcessConnect));
    /* the caller check is first, so no argument is probed: NULL / kernel pointers do not raise */
    ULONG_PTR args[][2] = { { 0, 0 }, { (ULONG_PTR)-1, 0 }, { (ULONG_PTR)GetCurrentProcess(), 0xFFFF800000000000ULL }, { 0x1234, 0x10 } };
    for (unsigned i = 0; i < sizeof args / sizeof args[0]; i++) {
        LONG r = UN(NtUserProcessConnect)(args[i][0], args[i][1], 0);
        CHECKF((ULONG)r == 0xC0000001, "case %u: %s", i, k_status_name(r));
    }
}
TEST(r2_win32k_process_lifecycle_quotas_conn_teb_win32threadinfo_set_after_gui_call) {
    R2_NEED_WIN();
    (void)U(GetSystemMetrics)(SM_CXSCREEN);
    void *pti = NULL; CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti));
    /* the slot is filled once the thread is a GUI thread; the value is not a user-mode pointer to anything readable */
    CHECK(pti != NULL);
    unsigned char b8[8];
    CHECK(!k_try_read(pti, b8, sizeof b8));
}
TEST(r2_win32k_process_lifecycle_quotas_conn_teb_win32threadinfo_distinct_per_thread) {
    R2_NEED_WIN();
    static void *main_pti, *other_pti;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &main_pti);
    DWORD WINAPI th(LPVOID p) { (void)U(GetSystemMetrics)(SM_CXSCREEN); (void)U(GetFocus)(); k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &other_pti); return 0; }
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL); REQUIRE(h);
    r2_wait_thread(h, 5000); CloseHandle(h);
    CHECK(main_pti != NULL); CHECK(other_pti != NULL); CHECK(main_pti != other_pti);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_teb_win32threadinfo_stable) {
    R2_NEED_WIN();
    void *a = NULL, *b = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &a);
    HWND w = r2_window(); (void)U(IsWindow)(w); if (w) U(DestroyWindow)(w);
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &b);
    CHECK(a != NULL && a == b);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_clientinfo_setup_flag_main_thread) {
    R2_NEED_WIN();
    /* user32!ClientThreadSetup ends by or-ing 8 into the first qword of Win32ClientInfo (TEB+0x800) */
    unsigned long long fl = 0; CHECK(k_try_read((char *)k_teb() + TEB_Win32ClientInfo, &fl, 8));
    CHECKF(fl & 8, "CI flags 0x%llx", fl);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_clientinfo_setup_flag_new_thread) {
    R2_NEED_WIN();
    static unsigned long long fl;
    DWORD WINAPI th(LPVOID p) { (void)U(GetSystemMetrics)(SM_CXSCREEN); (void)U(GetFocus)(); k_try_read((char *)k_teb() + TEB_Win32ClientInfo, &fl, 8); return 0; }
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL); REQUIRE(h);
    r2_wait_thread(h, 5000); CloseHandle(h);
    CHECKF(fl & 8, "CI flags 0x%llx", fl);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_kernelcallbacktable_inside_user32) {
    R2_NEED_WIN();
    void *kct = k_cb_table(); REQUIRE(kct);
    MEMORY_BASIC_INFORMATION m; CHECK(r2_vq(kct, &m));
    CHECK(m.Type == MEM_IMAGE);
    CHECK(m.AllocationBase == (void *)GetModuleHandleA("user32.dll"));    /* apfnDispatch lives in user32's data */
    CHECK(k_cb_count() > 100);
}
TEST(r2_win32k_process_lifecycle_quotas_conn_kernelcallbacktable_same_in_all_threads) {
    R2_NEED_WIN();
    static void *kct_t;
    DWORD WINAPI th(LPVOID p) { (void)U(GetSystemMetrics)(SM_CXSCREEN); kct_t = k_cb_table(); return 0; }
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL); REQUIRE(h);
    r2_wait_thread(h, 5000); CloseHandle(h);
    CHECK(kct_t != NULL && kct_t == k_cb_table());
}
TEST(r2_win32k_process_lifecycle_quotas_conn_gdi_table_pointer_same_in_all_threads) {
    R2_NEED_WIN();
    static void *t_t;
    DWORD WINAPI th(LPVOID p) { (void)U(CreateSolidBrush); k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &t_t); return 0; }
    void *m = NULL; k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &m);
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL); REQUIRE(h);
    r2_wait_thread(h, 5000); CloseHandle(h);
    CHECK(m != NULL && m == t_t);
}
