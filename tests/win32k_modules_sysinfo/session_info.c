/* win32k_modules_sysinfo: Terminal-services session identity as seen by the Win32 stack, and
 * NtQuerySystemInformation(SystemSessionProcessInformation, class 0x35).
 *
 * Kernel behaviour (ntoskrnl 19041, ExpQuerySystemInformation case 0x35 + ExpGetProcessInformation):
 *   - input is {ULONG SessionId; ULONG BufferLength; PVOID Buffer} and needs >= 0x10 bytes, else
 *     STATUS_INFO_LENGTH_MISMATCH with ReturnLength = 0x10;
 *   - entries are SYSTEM_PROCESS_INFORMATION records (>= 0x270 bytes each); only processes whose
 *     MmGetSessionId equals SessionId are copied (the idle process is skipped when a session is
 *     given), SessionId is stored at +0x64 of each record;
 *   - a Buffer shorter than one record fails with STATUS_INFO_LENGTH_MISMATCH.
 * Session identity itself comes from the process: PEB+0x2C0, NtQueryInformationProcess(24) and the
 * token's session all name the same session as ProcessIdToSessionId.
 */
#include "ms.h"
#include <winternl.h>

typedef struct { ULONG SessionId; ULONG BufferLength; PVOID Buffer; } MS_SESSION_PROC_REQ;

static unsigned char *ms_session_procs(ULONG session, ULONG *used, LONG *status_out) {
    ULONG size = 0x100000;
    for (int i = 0; i < 8; i++) {
        unsigned char *buf = (unsigned char *)calloc(1, size);
        if (!buf) return NULL;
        MS_SESSION_PROC_REQ req = { session, size, buf };
        ULONG ret = 0;
        LONG st = ms_qsi(0x35, &req, sizeof req, &ret);
        if (status_out) *status_out = st;
        if (st == ST_SUCCESS) { *used = ret; return buf; }
        free(buf);
        if (st != ST_INFO_LENGTH_MISMATCH) return NULL;
        size *= 2;
    }
    return NULL;
}

static const SYSTEM_PROCESS_INFORMATION *ms_find_pid(const unsigned char *buf, ULONG used, ULONG_PTR pid) {
    ULONG off = 0;
    for (;;) {
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)(buf + off);
        if ((ULONG_PTR)p->UniqueProcessId == pid) return p;
        if (!p->NextEntryOffset || off + p->NextEntryOffset >= used + 0x1000) return NULL;
        off += p->NextEntryOffset;
    }
}
static int ms_name_is(const SYSTEM_PROCESS_INFORMATION *p, const char *name) {
    if (!p->ImageName.Buffer || !p->ImageName.Length) return 0;
    char tmp[64]; size_t n = p->ImageName.Length / 2; if (n >= sizeof tmp) return 0;
    WCHAR w[64]; if (!k_try_read(p->ImageName.Buffer, w, n * 2)) return 0;
    w[n] = 0; ms_wide_to_narrow(w, tmp, sizeof tmp);
    return !_stricmp(tmp, name);
}
static int ms_has_proc(const unsigned char *buf, ULONG used, const char *name) {
    ULONG off = 0;
    for (;;) {
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)(buf + off);
        if (ms_name_is(p, name)) return 1;
        if (!p->NextEntryOffset || off + p->NextEntryOffset >= used + 0x1000) return 0;
        off += p->NextEntryOffset;
    }
}

/* ---- identity ----------------------------------------------------------------------- */
TEST(win32k_modules_sysinfo_session_peb_matches_processidtosessionid) {
    unsigned long peb_sid = 0;
    REQUIRE(k_try_read_u32((char *)k_peb() + PEB_SessionId, &peb_sid));
    CHECKF(peb_sid == k_session_id(), "PEB %lu API %lu", peb_sid, k_session_id());
}
TEST(win32k_modules_sysinfo_session_ntqueryinformationprocess_session) {
    ULONG sid[2] = { 0xFFFFFFFF, 0 }, ret = 0;
    LONG st = ms_qip(GetCurrentProcess(), 24, sid, sizeof sid[0], &ret);   /* ProcessSessionInformation */
    CHECKF(st == ST_SUCCESS, "status 0x%lx", (unsigned long)st);
    CHECK(ret == 4);
    CHECKF(sid[0] == k_session_id(), "class24 %lu API %lu", (unsigned long)sid[0], k_session_id());
}
TEST(win32k_modules_sysinfo_session_process_session_info_length_checked) {
    ULONG v[2] = {0}, ret = 0;
    LONG st = ms_qip(GetCurrentProcess(), 24, v, 2, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECKF(ret == 0, "return length %lu", (unsigned long)ret);   /* a rejected length leaves the return length alone on Windows 10 */
}
TEST(win32k_modules_sysinfo_session_token_session_id_matches) {
    HANDLE tok = NULL;
    REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok));
    DWORD sid = 0xFFFFFFFF, len = 0;
    BOOL ok = GetTokenInformation(tok, TokenSessionId, &sid, sizeof sid, &len);
    CloseHandle(tok);
    CHECK(ok);
    CHECKF(sid == k_session_id(), "token %lu process %lu", (unsigned long)sid, k_session_id());
}
TEST(win32k_modules_sysinfo_session_current_thread_token_free_process_id_same_session) {
    DWORD sid = 0xFFFFFFFF;
    CHECK(ProcessIdToSessionId(GetCurrentProcessId(), &sid));
    CHECK(sid == k_session_id());
    DWORD parent = 0; /* a nonexistent pid fails with ERROR_INVALID_PARAMETER */
    SET_ERR(0);
    BOOL ok = ProcessIdToSessionId(0xFFFFFFF0, &parent);
    CHECK(!ok);
}
TEST(win32k_modules_sysinfo_session_id_small_number) {
    CHECKF(k_session_id() < 1000, "session %lu", k_session_id());
    t_note("process session %lu; session 0 isolation %s", k_session_id(), k_session_id() == 0 ? "NOT in effect (service session)" : "in effect");
}
TEST(win32k_modules_sysinfo_session_console_id_relation_note) {
    typedef DWORD (WINAPI *fn)(void);
    fn f = (fn)t_proc_in("kernel32.dll", "WTSGetActiveConsoleSessionId"); REQUIRE(f);
    DWORD con = f();
    t_note("active console session %lu, own session %lu%s", (unsigned long)con, k_session_id(), con == k_session_id() ? " (we are on the console)" : "");
    CHECK(con < 4096 || con == 0xFFFFFFFF);
}
TEST(win32k_modules_sysinfo_session_gui_session_matches_interactive_desktop_note) {
    t_note("OpenInputDesktop-based interactive check: %d", k_has_interactive_desktop());
}

/* ---- window station / desktop attached to the process ---------------------------------- */
TEST(win32k_modules_sysinfo_session_process_window_station_has_name) {
    REQUIRE(U(GetProcessWindowStation));
    REQUIRE(U(GetUserObjectInformationW));
    HWINSTA ws = U(GetProcessWindowStation)();
    REQUIRE(ws);
    WCHAR name[128] = {0}; DWORD need = 0;
    BOOL ok = U(GetUserObjectInformationW)(ws, UOI_NAME, name, sizeof name, &need);
    CHECK(ok);
    CHECK(need >= 2 && need <= sizeof name);
    CHECK(name[0] != 0);
    t_note("process window station: %ls", name);
}
TEST(win32k_modules_sysinfo_session_interactive_station_is_winsta0_when_visible) {
    REQUIRE(U(GetProcessWindowStation)); REQUIRE(U(GetUserObjectInformationW));
    HWINSTA ws = U(GetProcessWindowStation)(); REQUIRE(ws);
    WCHAR name[128] = {0}; DWORD need;
    REQUIRE(U(GetUserObjectInformationW)(ws, UOI_NAME, name, sizeof name, &need));
    USEROBJECTFLAGS fl; memset(&fl, 0, sizeof fl);
    REQUIRE(U(GetUserObjectInformationW)(ws, UOI_FLAGS, &fl, sizeof fl, &need));
    CHECK(need == sizeof fl);
    /* WinSta0 is the only station with WSF_VISIBLE (the interactive one); service stations are invisible. */
    if (!lstrcmpiW(name, L"WinSta0")) CHECK(fl.dwFlags & WSF_VISIBLE);
    else CHECK(!(fl.dwFlags & WSF_VISIBLE));
}
TEST(win32k_modules_sysinfo_session_non_session0_process_uses_winsta0_in_console) {
    REQUIRE(U(GetProcessWindowStation)); REQUIRE(U(GetUserObjectInformationW));
    if (k_session_id() == 0) { t_skip("session 0"); return; }
    if (!k_has_interactive_desktop()) { t_skip("no interactive desktop"); return; }
    WCHAR name[128] = {0}; DWORD need;
    REQUIRE(U(GetUserObjectInformationW)(U(GetProcessWindowStation)(), UOI_NAME, name, sizeof name, &need));
    CHECKF(!lstrcmpiW(name, L"WinSta0"), "station \"%ls\"", name);
}
TEST(win32k_modules_sysinfo_session_thread_desktop_has_name) {
    REQUIRE(U(GetThreadDesktop)); REQUIRE(U(GetUserObjectInformationW));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    REQUIRE(d);
    WCHAR name[128] = {0}; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_NAME, name, sizeof name, &need));
    CHECK(name[0] != 0);
    t_note("thread desktop: %ls", name);
}
TEST(win32k_modules_sysinfo_session_window_station_user_object_type) {
    REQUIRE(U(GetProcessWindowStation)); REQUIRE(U(GetUserObjectInformationW));
    WCHAR type[64] = {0}; DWORD need = 0;
    HWINSTA ws = U(GetProcessWindowStation)(); REQUIRE(ws);
    CHECK(U(GetUserObjectInformationW)(ws, UOI_TYPE, type, sizeof type, &need));
    CHECK(!lstrcmpW(type, L"WindowStation"));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId()); REQUIRE(d);
    memset(type, 0, sizeof type);
    CHECK(U(GetUserObjectInformationW)(d, UOI_TYPE, type, sizeof type, &need));
    CHECK(!lstrcmpW(type, L"Desktop"));
}

/* ---- SystemSessionProcessInformation (0x35) -------------------------------------------- */
TEST(win32k_modules_sysinfo_sessproc_request_header_too_small) {
    unsigned char req[0x20] = {0};
    ULONG ret = 0;
    LONG st = ms_qsi(0x35, req, 0xF, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 0x10);
    st = ms_qsi(0x35, req, 0, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 0x10);
}
TEST(win32k_modules_sysinfo_sessproc_buffer_smaller_than_one_record) {
    unsigned char *buf = (unsigned char *)calloc(1, 0x200); REQUIRE(buf);
    MS_SESSION_PROC_REQ req = { k_session_id(), 0x100, buf };
    ULONG ret = 0;
    LONG st = ms_qsi(0x35, &req, sizeof req, &ret);
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_own_session_lists_current_process) {
    ULONG used = 0; LONG st = 0;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { if (t_is_wine()) { t_skip("unsupported under the emulator (0x%lx)", (unsigned long)st); return; } CHECKF(0, "query failed 0x%lx", (unsigned long)st); return; }
    CHECK(used >= 0x270);
    const SYSTEM_PROCESS_INFORMATION *me = ms_find_pid(buf, used, GetCurrentProcessId());
    CHECK(me != NULL);
    if (me) {
        CHECK(me->SessionId == k_session_id());
        CHECK(me->NumberOfThreads >= 1);
        CHECK(me->HandleCount >= 4);
    }
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_all_entries_belong_to_requested_session) {
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { t_skip("class 0x35 unavailable (0x%lx)", (unsigned long)st); return; }
    ULONG off = 0; int n = 0;
    for (;;) {
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)(buf + off);
        CHECKF(p->SessionId == k_session_id(), "pid %lu session %lu", (unsigned long)(ULONG_PTR)p->UniqueProcessId, (unsigned long)p->SessionId);
        n++;
        if (!p->NextEntryOffset) break;
        CHECK(p->NextEntryOffset >= 0x270);
        off += p->NextEntryOffset;
        if (off >= used) { CHECK(0); break; }
    }
    CHECK(n >= 2);
    t_note("%d processes in session %lu", n, k_session_id());
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_chain_stays_in_returned_length) {
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { t_skip("class 0x35 unavailable"); return; }
    ULONG off = 0;
    for (;;) {
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)(buf + off);
        CHECK(off + 0x270 <= used + 0x400);
        if (!p->NextEntryOffset) break;
        CHECK((p->NextEntryOffset & 7) == 0);
        off += p->NextEntryOffset;
        if (off >= used) { CHECK(0); break; }
    }
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_pids_unique_and_multiples_of_four) {
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { t_skip("class 0x35 unavailable"); return; }
    ULONG_PTR seen[2048]; int n = 0; ULONG off = 0;
    for (;;) {
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)(buf + off);
        ULONG_PTR pid = (ULONG_PTR)p->UniqueProcessId;
        CHECK((pid & 3) == 0);
        for (int i = 0; i < n; i++) CHECK(seen[i] != pid);
        if (n < 2048) seen[n++] = pid;
        if (!p->NextEntryOffset || off + p->NextEntryOffset >= used) break;
        off += p->NextEntryOffset;
    }
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_idle_and_system_not_in_gui_session) {
    if (k_session_id() == 0) { t_skip("session 0 contains System"); return; }
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { t_skip("class 0x35 unavailable"); return; }
    CHECK(ms_find_pid(buf, used, 0) == NULL);   /* idle process is skipped when a session is requested */
    CHECK(ms_find_pid(buf, used, 4) == NULL);   /* System runs in session 0 */
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_gui_session_has_csrss_and_winlogon) {
    if (k_session_id() == 0) { t_skip("session 0"); return; }
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { t_skip("class 0x35 unavailable"); return; }
    /* Every interactive session has its own csrss.exe (win32k's subsystem server) and winlogon.exe. */
    CHECK(ms_has_proc(buf, used, "csrss.exe"));
    CHECK(ms_has_proc(buf, used, "winlogon.exe"));
    t_note("dwm.exe in session: %d, explorer.exe: %d", ms_has_proc(buf, used, "dwm.exe"), ms_has_proc(buf, used, "explorer.exe"));
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_session_zero_has_system_services) {
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(0, &used, &st);
    if (!buf) { t_skip("session 0 listing unavailable (0x%lx)", (unsigned long)st); return; }
    const SYSTEM_PROCESS_INFORMATION *sys = ms_find_pid(buf, used, 4);
    CHECK(sys != NULL);
    if (sys) CHECK(ms_name_is(sys, "System"));
    CHECK(ms_has_proc(buf, used, "services.exe"));
    CHECK(ms_has_proc(buf, used, "wininit.exe"));
    ULONG off = 0;
    for (;;) {
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)(buf + off);
        CHECK(p->SessionId == 0);
        if (!p->NextEntryOffset || off + p->NextEntryOffset >= used) break;
        off += p->NextEntryOffset;
    }
    free(buf);
}
TEST(win32k_modules_sysinfo_sessproc_unused_session_has_no_processes_note) {
    ULONG used = 0; LONG st = 0;
    unsigned char *buf = ms_session_procs(0x7FF0, &used, &st);
    t_note("session 0x7FF0: status 0x%lx used %lu", (unsigned long)st, (unsigned long)used);
    if (buf) {
        /* an empty list: whatever is returned never contains a process of another session */
        const SYSTEM_PROCESS_INFORMATION *p = (const SYSTEM_PROCESS_INFORMATION *)buf;
        if (used >= 0x270 && p->UniqueProcessId) CHECK(p->SessionId == 0x7FF0);
        free(buf);
    }
}
TEST(win32k_modules_sysinfo_sessproc_matches_system_process_information) {
    ULONG used = 0; LONG st;
    unsigned char *s = ms_session_procs(k_session_id(), &used, &st);
    if (!s) { t_skip("class 0x35 unavailable"); return; }
    ULONG size = 0x200000, ret = 0; unsigned char *all = NULL;
    for (int i = 0; i < 8; i++) {
        all = (unsigned char *)calloc(1, size); REQUIRE(all);
        st = ms_qsi(5, all, size, &ret);
        if (st == ST_SUCCESS) break;
        free(all); all = NULL;
        if (st != ST_INFO_LENGTH_MISMATCH) break;
        size = ret + 0x10000;
    }
    if (!all) { t_skip("SystemProcessInformation unavailable"); free(s); return; }
    /* The same process appears in both with identical session, thread count (+-race) and image name. */
    const SYSTEM_PROCESS_INFORMATION *a = ms_find_pid(all, ret, GetCurrentProcessId());
    const SYSTEM_PROCESS_INFORMATION *b = ms_find_pid(s, used, GetCurrentProcessId());
    CHECK(a && b);
    if (a && b) {
        CHECK(a->SessionId == b->SessionId);
        CHECK(a->SessionId == k_session_id());
        CHECK(a->UniqueProcessId == b->UniqueProcessId);
        long d = (long)a->NumberOfThreads - (long)b->NumberOfThreads; if (d < 0) d = -d;
        CHECK(d <= 2);
        CHECK(a->ImageName.Length == b->ImageName.Length);
    }
    free(all); free(s);
}
TEST(win32k_modules_sysinfo_sessproc_own_image_name_matches_exe) {
    ULONG used = 0; LONG st;
    unsigned char *buf = ms_session_procs(k_session_id(), &used, &st);
    if (!buf) { t_skip("class 0x35 unavailable"); return; }
    const SYSTEM_PROCESS_INFORMATION *me = ms_find_pid(buf, used, GetCurrentProcessId());
    REQUIRE(me);
    const char *self = t_self_path(); const char *leaf = strrchr(self, '\\'); leaf = leaf ? leaf + 1 : self;
    char l15[16]; snprintf(l15, sizeof l15, "%s", leaf);
    CHECKF(ms_name_is(me, leaf), "image name differs from %s", leaf);
    free(buf);
}
