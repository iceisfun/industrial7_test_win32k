/* win32k_mitigation / load: what still works, and what user32/gdi32 do, in a Win32k-locked process.
 *
 * Everything below goes through kernel32/ntdll (plain NT services, below 0x1000) or LoadLibrary of the GUI
 * DLLs, whose DllMain runs user32!_UserClientDllInitialize / gdi32's GdiProcessSetup: those talk to win32k
 * and csrss, which a locked process cannot reach. The observable contract is that the thread is never converted
 * and no kernel-owned win32 state appears in the process; the exact LoadLibrary outcome is recorded with t_note. */
#include "mit.h"

static const char *k_status(unsigned long st) { return k_status_name((K_NTSTATUS)st); }

SCN(load_user32) {
    MIT_NEED_WIN32K();
    SetLastError(0);
    HMODULE m = LoadLibraryA("user32.dll");
    DWORD e = GetLastError();
    t_note("LoadLibrary(user32.dll) in a Win32k-locked process: %p, error %lu", (void *)m, e);
    MC(mit_unconverted());                                   /* no syscall of user32's init converts the thread */
    MC(mit_peb64(PEB_GdiSharedHandleTable) == 0);            /* GdiProcessCallout never ran */
    if (m) {
        BOOL (WINAPI *isgui)(BOOL) = (BOOL (WINAPI *)(BOOL))GetProcAddress(m, "IsGUIThread");
        if (isgui) { BOOL g = FALSE; MIT_SOFT(g = isgui(FALSE)); MC(!g); }   /* reads TEB.Win32ThreadInfo */
        HDC (WINAPI *gdc)(HWND) = (HDC (WINAPI *)(HWND))GetProcAddress(m, "GetDC");
        if (gdc) { HDC dc = (HDC)1; MIT_SOFT(dc = gdc(NULL)); t_note("user32!GetDC(NULL) -> %p", (void *)dc); }
        MC(mit_unconverted());
    }
}
SCN(load_gdi32) {
    MIT_NEED_WIN32K();
    SetLastError(0);
    HMODULE m = LoadLibraryA("gdi32.dll");
    DWORD e = GetLastError();
    t_note("LoadLibrary(gdi32.dll) in a Win32k-locked process: %p, error %lu", (void *)m, e);
    MC(mit_unconverted());
    MC(mit_peb64(PEB_GdiSharedHandleTable) == 0);
    if (m) {
        HDC (WINAPI *ccdc)(HDC) = (HDC (WINAPI *)(HDC))GetProcAddress(m, "CreateCompatibleDC");
        if (ccdc) { HDC dc = (HDC)1; MIT_SOFT(dc = ccdc(NULL)); t_note("gdi32!CreateCompatibleDC(NULL) -> %p", (void *)dc); }
        HGDIOBJ (WINAPI *gso)(int) = (HGDIOBJ (WINAPI *)(int))GetProcAddress(m, "GetStockObject");
        if (gso) { HGDIOBJ o = (HGDIOBJ)1; MIT_SOFT(o = gso(BLACK_BRUSH)); t_note("gdi32!GetStockObject(BLACK_BRUSH) -> %p", (void *)o); }
        MC(mit_unconverted());
    }
}
SCN(load_user32_then_stub) {
    MIT_NEED_WIN32K();
    LoadLibraryA("user32.dll");
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    unsigned long st = mit_call_stub(f);
    MCF(mit_status_ok(st), "-> %s", k_status(st));
    MC(mit_unconverted());
}
SCN(load_win32u) {
    SetLastError(0);
    HMODULE m = LoadLibraryA("win32u.dll");
    MC(m != NULL);                                           /* win32u has no connection DllMain */
    MC(GetModuleHandleA("win32u.dll") == m);
    MC(mit_unconverted());
}
SCN(load_non_gui_dlls) {
    /* DLLs that never reach win32k import nothing GUI. */
    static const char *dlls[] = { "version.dll", "psapi.dll", "bcrypt.dll", "wtsapi32.dll" };
    for (unsigned i = 0; i < sizeof dlls / sizeof *dlls; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (!m) t_note("LoadLibrary(%s) failed: %lu", dlls[i], GetLastError());
    }
    MC(LoadLibraryA("version.dll") != NULL);
    MC(mit_unconverted());
}
SCN(load_gui_importing_dlls) {
    MIT_NEED_WIN32K();
    static const char *dlls[] = { "comctl32.dll", "shell32.dll", "shlwapi.dll", "winmm.dll", "ole32.dll", "imm32.dll", "uxtheme.dll", "comdlg32.dll" };
    for (unsigned i = 0; i < sizeof dlls / sizeof *dlls; i++) {
        SetLastError(0);
        HMODULE m = LoadLibraryA(dlls[i]);
        t_note("LoadLibrary(%s): %p error %lu", dlls[i], (void *)m, GetLastError());
    }
    MC(mit_unconverted());
    MC(mit_peb64(PEB_GdiSharedHandleTable) == 0);
}
MIT_LOCKED2(load_user32, "load_user32", "")
MIT_LOCKED2(load_gdi32, "load_gdi32", "")
MIT_LOCKED2(load_user32_then_stub, "load_user32_then_stub", "")
MIT_LOCKED2(load_win32u, "load_win32u", "")
MIT_LOCKED2(load_non_gui_dlls, "load_non_gui_dlls", "")
MIT_LOCKED2(load_gui_importing_dlls, "load_gui_importing_dlls", "")

/* ---- kernel32 / ntdll keep working ------------------------------------------------------------ */
static DWORD WINAPI add_one(LPVOID p) { (*(volatile LONG *)p)++; return 7; }
SCN(kernel32_battery) {
    char path[MAX_PATH]; DWORD n;
    /* memory */
    void *v = VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    MC(v != NULL);
    if (v) { memset(v, 0x5A, 0x10000); MC(((unsigned char *)v)[0xFFFF] == 0x5A); MC(VirtualFree(v, 0, MEM_RELEASE)); }
    HANDLE hp = GetProcessHeap(); void *h = HeapAlloc(hp, HEAP_ZERO_MEMORY, 1000);
    MC(h != NULL); MC(HeapFree(hp, 0, h));
    /* files */
    t_tmpfile(path, sizeof path, "lock.bin");
    HANDLE f = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    MC(f != INVALID_HANDLE_VALUE);
    if (f != INVALID_HANDLE_VALUE) {
        char buf[16] = "win32k-locked"; char rd[16] = { 0 };
        MC(WriteFile(f, buf, 14, &n, NULL) && n == 14);
        MC(SetFilePointer(f, 0, NULL, FILE_BEGIN) == 0);
        MC(ReadFile(f, rd, 14, &n, NULL) && n == 14);
        MC(!strcmp(rd, buf));
        CloseHandle(f);
        MC(DeleteFileA(path));
    }
    /* sync + threads */
    LONG counter = 0;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL), mx = CreateMutexA(NULL, FALSE, NULL);
    MC(ev && mx);
    MC(WaitForSingleObject(mx, 0) == WAIT_OBJECT_0); ReleaseMutex(mx);
    MC(WaitForSingleObject(ev, 0) == WAIT_TIMEOUT); SetEvent(ev); MC(WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
    HANDLE th = CreateThread(NULL, 0, add_one, &counter, 0, NULL);
    MC(th != NULL);
    if (th) { MC(WaitForSingleObject(th, 5000) == WAIT_OBJECT_0); GetExitCodeThread(th, &n); MEQ(n, 7); CloseHandle(th); }
    MEQ(counter, 1);
    CloseHandle(ev); CloseHandle(mx);
    /* time, ids, environment */
    LARGE_INTEGER q; MC(QueryPerformanceCounter(&q) && q.QuadPart > 0);
    MC(GetTickCount64() > 0);
    MC(GetCurrentProcessId() != 0); MC(GetCurrentThreadId() != 0);
    MC(GetModuleFileNameA(NULL, path, sizeof path) > 0);
    SetEnvironmentVariableA("WIN32KT_MIT", "x"); MC(GetEnvironmentVariableA("WIN32KT_MIT", path, sizeof path) == 1);
    SYSTEM_INFO si; GetSystemInfo(&si); MC(si.dwNumberOfProcessors >= 1 && si.dwPageSize == 4096);
    MC(GetCurrentDirectoryA(sizeof path, path) > 0);
    Sleep(5);
    /* the interactive-session facts that need no win32k */
    MC(k_session_id() == k_session_id());
    MC(mit_unconverted());
}
SCN(ntdll_battery) {
    typedef LONG (WINAPI *qsi_fn)(ULONG, PVOID, ULONG, PULONG);
    typedef LONG (WINAPI *qip_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    typedef LONG (WINAPI *qo_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    qsi_fn qsi = (qsi_fn)mit_nt("NtQuerySystemInformation");
    qip_fn qip = (qip_fn)mit_nt("NtQueryInformationProcess");
    qo_fn qo = (qo_fn)mit_nt("NtQueryObject");
    MC(qsi && qip && qo); if (!(qsi && qip && qo)) return;
    BYTE sbi[64]; ULONG ret = 0;
    MST(qsi(0 /* SystemBasicInformation */, sbi, sizeof sbi, &ret), ST_SUCCESS);
    PROCESS_BASIC_INFORMATION pbi; memset(&pbi, 0, sizeof pbi);
    MST(qip(GetCurrentProcess(), 0 /* ProcessBasicInformation */, &pbi, sizeof pbi, &ret), ST_SUCCESS);
    MC(pbi.PebBaseAddress == k_peb());
    MC((ULONG_PTR)pbi.UniqueProcessId == GetCurrentProcessId());
    BYTE ob[512]; memset(ob, 0, sizeof ob);
    HANDLE self = mit_open_self(PROCESS_QUERY_LIMITED_INFORMATION);
    MST(qo(self, 2 /* ObjectTypeInformation */, ob, sizeof ob, &ret), ST_SUCCESS);
    if (self) CloseHandle(self);
    UNICODE_STRING *tn = (UNICODE_STRING *)ob;                 /* OBJECT_TYPE_INFORMATION.TypeName */
    MC(tn->Length == 14 && tn->Buffer && !memcmp(tn->Buffer, L"Process", 14));
    DWORD sess = ~0UL; MST(qip(GetCurrentProcess(), 24 /* ProcessSessionInformation */, &sess, 4, &ret), ST_SUCCESS);
    MC(sess == k_session_id());
    RTL_OSVERSIONINFOW vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi;
    LONG (WINAPI *gv)(PRTL_OSVERSIONINFOW) = (LONG (WINAPI *)(PRTL_OSVERSIONINFOW))mit_nt("RtlGetVersion");
    if (gv) { MST(gv(&vi), ST_SUCCESS); MC(vi.dwMajorVersion >= 10); t_note("build %lu", vi.dwBuildNumber); }
    MC(mit_unconverted());
}
SCN(token_and_security) {
    HANDLE tok = NULL;
    MC(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok));
    if (tok) CloseHandle(tok);
    unsigned long il = k_integrity_level();
    MC(il >= 0x1000);                                        /* SECURITY_MANDATORY_LOW_RID.. */
    MC(k_session_id() < 0xFFFF);
}
SCN(child_process_plain) {
    /* CreateProcess/WaitForSingleObject are kernel32/NtCreateUserProcess: no win32k involved. */
    struct mit_proc p;
    int ok = mit_start(&p, "report_flags::plain", NULL, 1);
    MCF(ok, "error %lu", p.err);
    if (!ok) return;
    mit_release(&p);
    DWORD code = mit_finish(&p, 20000);
    MEQ(code, 0);
}
SCN(exit_code_propagates) {
    /* ExitProcess from a locked process: teardown has no W32PROCESS to destroy. Verified by the parent seeing 0. */
    MC(mit_unconverted());
}
MIT_LOCKED2(kernel32_battery, "kernel32_battery", "")
MIT_LOCKED2(ntdll_battery, "ntdll_battery", "")
MIT_LOCKED2(token_and_security, "token_and_security", "")
MIT_LOCKED2(child_process_plain, "child_process_plain", "")
MIT_LOCKED2(exit_code_propagates, "exit_code_propagates", "")
