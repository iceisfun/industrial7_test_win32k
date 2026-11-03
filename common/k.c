/* common/k.c - implementation of the kernel-state helper layer. See k.h for the documented API
 * and where each offset/byte pattern was verified. */
#include "k.h"
#include <stdlib.h>
#include <intrin.h>

/* ---- safe read ---------------------------------------------------------- */

bool k_try_read(const void *addr, void *out, size_t size) {
    if (!addr || !out || !size) return false;
    SIZE_T got = 0;
    BOOL ok = ReadProcessMemory(GetCurrentProcess(), addr, out, size, &got);
    return ok && got == size;
}
bool k_try_read_ptr(const void *addr, void **out) { return k_try_read(addr, out, sizeof(void *)); }
bool k_try_read_u32(const void *addr, unsigned long *out) { return k_try_read(addr, out, sizeof(unsigned long)); }
bool k_try_read_u16(const void *addr, unsigned short *out) { return k_try_read(addr, out, sizeof(unsigned short)); }
bool k_try_read_u8(const void *addr, unsigned char *out) { return k_try_read(addr, out, sizeof(unsigned char)); }

void *k_teb(void) { return NtCurrentTeb(); }
void *k_peb(void) {
    void *teb = k_teb();
    void *peb = NULL;
    k_try_read_ptr((char *)teb + 0x60 /* _TEB.ProcessEnvironmentBlock */, &peb);
    return peb;
}

/* ---- win32u stub decoding ------------------------------------------------ */

/* 4C 8B D1 B8 xx xx xx xx F6 04 25 08 03 FE 7F 01 75 03 0F 05 C3 CD 2E C3 */
static const unsigned char k_stub_prologue[4] = { 0x4C, 0x8B, 0xD1, 0xB8 };
#define K_STUB_MIN_LEN 20

bool k_stub_valid(FARPROC stub) {
    unsigned char buf[K_STUB_MIN_LEN];
    if (!k_try_read((void *)stub, buf, sizeof buf)) return false;
    if (memcmp(buf, k_stub_prologue, sizeof k_stub_prologue) != 0) return false;
    /* bytes 8..14: F6 04 25 08 03 FE 7F 01 (test byte [0x7FFE0308], 1) */
    static const unsigned char test_insn[8] = { 0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01 };
    if (memcmp(buf + 8, test_insn, sizeof test_insn) != 0) return false;
    return true;
}
long k_stub_number(FARPROC stub) {
    unsigned char buf[K_STUB_MIN_LEN];
    if (!k_stub_valid(stub)) return -1;
    if (!k_try_read((void *)stub, buf, sizeof buf)) return -1;
    unsigned long n;
    memcpy(&n, buf + 4, 4);
    return (long)n;
}
size_t k_stub_bytes(FARPROC stub, unsigned char *out, size_t cap) {
    unsigned char buf[K_STUB_MIN_LEN];
    if (!k_try_read((void *)stub, buf, sizeof buf)) return 0;
    size_t n = cap < sizeof buf ? cap : sizeof buf;
    memcpy(out, buf, n);
    return n;
}

/* Raw syscall trampoline: mov r10,rcx; mov eax,<imm32 patched>; syscall; ret.
 * Shared, self-modified buffer: k_raw_syscall is documented NOT thread-safe. */
typedef K_NTSTATUS (WINAPI *k_raw8_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                        ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);
static unsigned char *g_raw_stub;

static void k_raw_stub_init(void) {
    if (g_raw_stub) return;
    unsigned char code[] = {
        0x4C, 0x8B, 0xD1,             /* mov r10, rcx */
        0xB8, 0x00, 0x00, 0x00, 0x00, /* mov eax, imm32  (patched at offset 4) */
        0x0F, 0x05,                   /* syscall */
        0xC3                          /* ret */
    };
    g_raw_stub = (unsigned char *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (g_raw_stub) memcpy(g_raw_stub, code, sizeof code);
}
K_NTSTATUS k_raw_syscall(unsigned long number,
                          ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4,
                          ULONG_PTR a5, ULONG_PTR a6, ULONG_PTR a7, ULONG_PTR a8) {
    k_raw_stub_init();
    if (!g_raw_stub) return (K_NTSTATUS)0xC0000017; /* STATUS_NO_MEMORY */
    memcpy(g_raw_stub + 4, &number, 4);
    k_raw8_fn fn = (k_raw8_fn)(void *)g_raw_stub;
    return fn(a1, a2, a3, a4, a5, a6, a7, a8);
}

/* ---- GDI shared handle table --------------------------------------------- */

static void *gdi_table_base(void) {
    void *peb = k_peb();
    if (!peb) return NULL;
    void *table = NULL;
    k_try_read_ptr((char *)peb + PEB_GdiSharedHandleTable, &table);
    return table;
}
bool k_gdi_cell(void *obj, struct k_gdicell *out) {
    void *base = gdi_table_base();
    if (!base) return false;
    unsigned idx = (unsigned)((ULONG_PTR)obj & 0xffff);
    char *cell = (char *)base + (ULONG_PTR)idx * GDI_CELL_SIZE;
    void *p0 = NULL, *p10 = NULL;
    unsigned long long f8 = 0;
    unsigned char tc = 0;
    if (!k_try_read_ptr(cell + 0x00, &p0)) return false;
    if (!k_try_read(cell + 0x08, &f8, sizeof f8)) return false;
    if (!k_try_read_u8(cell + 0x0d, &tc)) return false;
    if (!k_try_read_ptr(cell + 0x10, &p10)) return false;
    if (out) { out->object = p0; out->field8 = f8; out->type_check = tc; out->field10 = p10; }
    return true;
}
unsigned k_gdi_index(void *obj) { return (unsigned)((ULONG_PTR)obj & 0xffff); }
unsigned char k_gdi_type_byte(void *obj) {
    struct k_gdicell c;
    if (!k_gdi_cell(obj, &c)) return 0;
    return c.type_check;
}

/* ---- USER shared info / handle table -------------------------------------- */

void *k_shared_info_ptr(void) {
    HMODULE u = GetModuleHandleA("user32.dll");
    if (!u) return NULL;
    return (void *)GetProcAddress(u, "gSharedInfo");
}
bool k_shared_info(struct k_user_shared_info *out) {
    void *si = k_shared_info_ptr();
    if (!si) return false;
    void *psi = NULL, *ahe = NULL;
    unsigned long sz = 0;
    if (!k_try_read_ptr((char *)si + 0x00, &psi)) return false;
    if (!k_try_read_ptr((char *)si + 0x08, &ahe)) return false;
    if (!k_try_read_u32((char *)si + 0x10, &sz)) return false;
    if (out) { out->psi = psi; out->aheList = ahe; out->heEntrySize = sz; }
    return true;
}

bool k_user_entry(HANDLE h, struct k_user_entry *out) {
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.aheList) return false;
    unsigned idx = (unsigned)((ULONG_PTR)h & 0xffff);
    unsigned long entsz = si.heEntrySize ? si.heEntrySize : USER_HANDLE_SIZE;
    char *ent = (char *)si.aheList + (ULONG_PTR)idx * entsz;
    void *delta = NULL, *f10 = NULL;
    unsigned char type = 0, flags = 0;
    unsigned short uniq = 0;
    if (!k_try_read_ptr(ent + 0x00, &delta)) return false;
    if (!k_try_read_ptr(ent + 0x10, &f10)) return false;
    if (!k_try_read_u8(ent + 0x18, &type)) return false;
    if (!k_try_read_u8(ent + 0x19, &flags)) return false;
    if (!k_try_read_u16(ent + 0x1a, &uniq)) return false;
    if (out) { out->delta = delta; out->field10 = f10; out->type = type; out->flags = flags; out->uniq = uniq; }
    return true;
}

void *k_wnd_ptr(HWND hwnd) {
    struct k_user_entry e;
    if (!k_user_entry(hwnd, &e)) return NULL;
    if (e.flags & 1) return NULL; /* HMValidateHandle: bit0 set => invalid/deleted */
    unsigned short hiword = (unsigned short)((ULONG_PTR)hwnd >> 16);
    if (e.uniq != hiword && e.uniq != 0xffff) return NULL;
    void *guard = NULL;
    if (!k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + CI_pDeskHeapBaseGuard, &guard)) return NULL;
    if (!guard) return NULL;
    void *guard_val = NULL;
    if (!k_try_read_ptr(guard, &guard_val)) return NULL;
    if (guard_val != e.field10) return NULL; /* client-side desktop heap mapping stale for this process; the real caller would ask the kernel (NtUserCallOneParam) instead */
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + CI_pDeskHeapClientBase, &base)) return NULL;
    return (char *)base + (ULONG_PTR)e.delta;
}

/* ---- kernel callback table ------------------------------------------------- */

void *k_cb_table(void) {
    void *peb = k_peb();
    if (!peb) return NULL;
    void *tbl = NULL;
    k_try_read_ptr((char *)peb + PEB_KernelCallbackTable, &tbl);
    return tbl;
}
unsigned k_cb_count(void) {
    void *tbl = k_cb_table();
    if (!tbl) return 0;
    unsigned n = 0;
    for (; n < K_CB_MAX; n++) {
        void *p = NULL;
        if (!k_try_read_ptr((char *)tbl + (size_t)n * sizeof(void *), &p)) break;
        if (!p) break;
    }
    return n;
}

struct k_cb_hook_state {
    bool active;
    void *original;
    void *trampoline; /* executable: counts, captures args, jmp original */
    volatile unsigned long hits;
    ULONG_PTR last_args[4];
};
static struct k_cb_hook_state g_cb[K_CB_MAX];
static bool g_cb_atexit_registered;

/* Trampoline shape (per-slot, built at hook time), x64:
 *   mov  rax, &g_cb[index]        ; r11 scratch not needed; use absolute addressing via mov
 *   lock inc dword [rax+hits_off]
 *   mov  [rax+args_off+0*8], rcx
 *   mov  [rax+args_off+1*8], rdx
 *   mov  [rax+args_off+2*8], r8
 *   mov  [rax+args_off+3*8], r9
 *   mov  rax, [rax+orig_off]
 *   jmp  rax
 * Built generically with the state struct's real field offsets so it is C-struct-layout-safe. */
static void k_cb_build_trampoline(unsigned index) {
    struct k_cb_hook_state *st = &g_cb[index];
    size_t hits_off = offsetof(struct k_cb_hook_state, hits);
    size_t args_off = offsetof(struct k_cb_hook_state, last_args);
    size_t orig_off = offsetof(struct k_cb_hook_state, original);
    unsigned char code[128];
    size_t n = 0;
    #define EMIT(...) do { unsigned char _b[] = {__VA_ARGS__}; memcpy(code+n,_b,sizeof _b); n+=sizeof _b; } while(0)
    /* mov rax, imm64 (st) */
    EMIT(0x48, 0xB8); unsigned long long stv = (unsigned long long)(ULONG_PTR)st;
    memcpy(code + n, &stv, 8); n += 8;
    /* lock inc dword [rax+hits_off] (hits_off fits a byte for this struct) */
    EMIT(0xF0, 0xFF, 0x40, (unsigned char)hits_off);
    /* mov [rax+args_off+0], rcx ; +8 rdx ; +16 r8 ; +24 r9 */
    EMIT(0x48, 0x89, 0x48, (unsigned char)(args_off + 0));
    EMIT(0x48, 0x89, 0x50, (unsigned char)(args_off + 8));
    EMIT(0x4C, 0x89, 0x40, (unsigned char)(args_off + 16));
    EMIT(0x4C, 0x89, 0x48, (unsigned char)(args_off + 24));
    /* mov rax, [rax+orig_off] */
    EMIT(0x48, 0x8B, 0x40, (unsigned char)orig_off);
    /* jmp rax */
    EMIT(0xFF, 0xE0);
    #undef EMIT
    st->trampoline = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (st->trampoline) memcpy(st->trampoline, code, n);
}
bool k_cb_hook(unsigned index) {
    if (index >= K_CB_MAX) return false;
    if (g_cb[index].active) return false;
    void *tbl = k_cb_table();
    if (!tbl) return false;
    void *orig = NULL;
    if (!k_try_read_ptr((char *)tbl + (size_t)index * sizeof(void *), &orig) || !orig) return false;
    g_cb[index].original = orig;
    g_cb[index].hits = 0;
    memset((void *)g_cb[index].last_args, 0, sizeof g_cb[index].last_args);
    k_cb_build_trampoline(index);
    if (!g_cb[index].trampoline) return false;
    DWORD old;
    void *slot = (char *)tbl + (size_t)index * sizeof(void *);
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
    memcpy(slot, &g_cb[index].trampoline, sizeof(void *));
    VirtualProtect(slot, sizeof(void *), old, &old);
    g_cb[index].active = true;
    if (!g_cb_atexit_registered) { atexit(k_cb_unhook_all); g_cb_atexit_registered = true; }
    return true;
}
void k_cb_unhook_all(void) {
    void *tbl = k_cb_table();
    for (unsigned i = 0; i < K_CB_MAX; i++) {
        if (!g_cb[i].active) continue;
        if (tbl) {
            void *slot = (char *)tbl + (size_t)i * sizeof(void *);
            DWORD old;
            if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
                memcpy(slot, &g_cb[i].original, sizeof(void *));
                VirtualProtect(slot, sizeof(void *), old, &old);
            }
        }
        g_cb[i].active = false;
    }
}
unsigned long k_cb_hit_count(unsigned index) { return index < K_CB_MAX ? g_cb[index].hits : 0; }
void *k_cb_original(unsigned index) { return index < K_CB_MAX ? g_cb[index].original : NULL; }
bool k_cb_last_args(unsigned index, ULONG_PTR argv[4]) {
    if (index >= K_CB_MAX || !g_cb[index].active) return false;
    memcpy(argv, (void *)g_cb[index].last_args, sizeof g_cb[index].last_args);
    return true;
}

/* ---- window / message helpers --------------------------------------------- */

/* user32 is resolved at call time, never imported: a static USER32 import would load user32.dll (and
 * connect the process to win32k) before main() in EVERY test exe and child, destroying the
 * pristine-state property docs/KERNEL_TESTING.md promises. */
static HMODULE k_user32(void) { return LoadLibraryA("user32.dll"); }
#define K_USER32(ret, name, args) typedef ret (WINAPI *k_##name##_pfn) args; \
    static k_##name##_pfn k_get_##name(void) { return (k_##name##_pfn)GetProcAddress(k_user32(), #name); }
K_USER32(LRESULT, DefWindowProcA, (HWND, UINT, WPARAM, LPARAM))
K_USER32(ATOM, RegisterClassA, (const WNDCLASSA *))
K_USER32(HWND, CreateWindowExA, (DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID))
K_USER32(BOOL, PeekMessageA, (LPMSG, HWND, UINT, UINT, UINT))
K_USER32(BOOL, TranslateMessage, (const MSG *))
K_USER32(LRESULT, DispatchMessageA, (const MSG *))
K_USER32(HDESK, OpenInputDesktop, (DWORD, BOOL, ACCESS_MASK))
K_USER32(BOOL, GetUserObjectInformationA, (HANDLE, int, PVOID, DWORD, LPDWORD))
K_USER32(BOOL, CloseDesktop, (HDESK))

static LRESULT CALLBACK k_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return k_get_DefWindowProcA()(h, m, w, l); }
static ATOM k_class_atom(void) {
    static ATOM a;
    if (a) return a;
    k_RegisterClassA_pfn reg = k_get_RegisterClassA();
    if (!reg) return 0;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = k_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "win32ktHelperWnd";
    a = reg(&wc);
    return a;
}
HWND k_make_window(const char *title) {
    if (!k_class_atom()) return NULL;
    k_CreateWindowExA_pfn cw = k_get_CreateWindowExA();
    if (!cw) return NULL;
    return cw(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "win32ktHelperWnd",
              title ? title : "win32kt", WS_POPUP, -32000, -32000, 1, 1,
              NULL, NULL, GetModuleHandleA(NULL), NULL);
}
int k_pump(unsigned ms) {
    DWORD start = GetTickCount();
    int n = 0;
    MSG msg;
    k_PeekMessageA_pfn pk = k_get_PeekMessageA();
    k_TranslateMessage_pfn tm = k_get_TranslateMessage();
    k_DispatchMessageA_pfn dm = k_get_DispatchMessageA();
    if (!pk || !tm || !dm) { Sleep(ms); return 0; }
    while (GetTickCount() - start < ms) {
        while (pk(&msg, NULL, 0, 0, PM_REMOVE)) {
            tm(&msg); dm(&msg); n++;
        }
        Sleep(5);
    }
    return n;
}
struct k_run_gui_ctx { k_thread_fn fn; void *arg; DWORD ret; };
static DWORD WINAPI k_run_gui_thunk(LPVOID p) {
    struct k_run_gui_ctx *c = (struct k_run_gui_ctx *)p;
    c->ret = c->fn(c->arg);
    return 0;
}
DWORD k_run_gui_thread(k_thread_fn fn, void *arg) {
    struct k_run_gui_ctx ctx = { fn, arg, 0 };
    HANDLE h = CreateThread(NULL, 0, k_run_gui_thunk, &ctx, 0, NULL);
    if (!h) return 0xFFFFFFFF;
    WaitForSingleObject(h, 60000);
    CloseHandle(h);
    return ctx.ret;
}

/* ---- child process helpers -------------------------------------------------- */

static struct k_child_result k_child_run_common(const char *name, const char *extra_args, DWORD timeout_ms, LPSTARTUPINFOEXA siex_or_null) {
    struct k_child_result r = { 0xDEAD0000, 0 };
    HANDLE rd = NULL, wr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    if (!CreatePipe(&rd, &wr, &sa, 0)) return r;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    char cmd[4096];
    snprintf(cmd, sizeof cmd, "\"%s\" --child %s %s", t_self_path(), name, extra_args ? extra_args : "");

    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    BOOL ok;
    if (siex_or_null) {
        siex_or_null->StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        siex_or_null->StartupInfo.hStdOutput = wr;
        siex_or_null->StartupInfo.hStdError = wr;
        siex_or_null->StartupInfo.hStdInput = NULL;
        ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                             &siex_or_null->StartupInfo, &pi);
    } else {
        STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
        si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
        ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    }
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return r; }

    char buf[4096]; size_t buflen = 0;
    bool timed_out = false;
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) {
            DWORD got = 0;
            if (ReadFile(rd, buf + buflen, (DWORD)(sizeof(buf) - 1 - buflen), &got, NULL) && got) {
                buflen += got;
                for (;;) {
                    char *nl = memchr(buf, '\n', buflen);
                    if (!nl) break;
                    *nl = 0;
                    t_scrub(buf); printf("  CHILD %s: %s\n", name, buf);
                    r.relayed_lines++;
                    size_t used = (size_t)(nl - buf) + 1;
                    memmove(buf, nl + 1, buflen - used);
                    buflen -= used;
                }
            }
        }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;
        if ((timeout_ms -= (timeout_ms > 20 ? 20 : timeout_ms)) == 0) { timed_out = true; break; }
    }
    if (timed_out) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000); }
    /* The child may have written its last line(s) and exited before this loop's last PeekNamedPipe
     * ran; drain whatever is still sitting in the pipe now that the process is no longer running
     * (or has been killed), so a fast-exiting child's output is never dropped. */
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) || !avail) break;
        DWORD got = 0;
        if (!ReadFile(rd, buf + buflen, (DWORD)(sizeof(buf) - 1 - buflen), &got, NULL) || !got) break;
        buflen += got;
    }
    for (;;) {
        char *nl = memchr(buf, '\n', buflen);
        if (!nl) break;
        *nl = 0;
        t_scrub(buf); printf("  CHILD %s: %s\n", name, buf);
        r.relayed_lines++;
        size_t used = (size_t)(nl - buf) + 1;
        memmove(buf, nl + 1, buflen - used);
        buflen -= used;
    }
    if (buflen) { buf[buflen] = 0; t_scrub(buf); printf("  CHILD %s: %s\n", name, buf); r.relayed_lines++; }
    r.exit_code = timed_out ? 0xDEAD0001 : 0;
    if (!timed_out) GetExitCodeProcess(pi.hProcess, &r.exit_code);

    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
    return r;
}
struct k_child_result k_child_run(const char *name, const char *extra_args, DWORD timeout_ms) {
    return k_child_run_common(name, extra_args, timeout_ms, NULL);
}
struct k_child_result k_child_run_ex(const char *name, const char *extra_args, DWORD timeout_ms, LPSTARTUPINFOEXA siex) {
    return k_child_run_common(name, extra_args, timeout_ms, siex);
}

/* ---- status names / version / token ---------------------------------------- */

const char *k_status_name(K_NTSTATUS st) {
    static char buf[32];
    switch ((unsigned long)(ULONG_PTR)st) {
        case 0x00000000: return "STATUS_SUCCESS";
        case 0xC0000008: return "STATUS_INVALID_HANDLE";
        case 0xC000000D: return "STATUS_INVALID_PARAMETER";
        case 0xC000001C: return "STATUS_INVALID_SYSTEM_SERVICE"; /* KiSystemCall64: service index out of range for the selected table, see sc_raw_syscall */
        case 0xC0000022: return "STATUS_ACCESS_DENIED";
        case 0xC0000005: return "STATUS_ACCESS_VIOLATION";
        case 0xC000000B: return "STATUS_INVALID_CID";
        case 0xC0000034: return "STATUS_OBJECT_NAME_NOT_FOUND";
        case 0xC0000061: return "STATUS_PRIVILEGE_NOT_HELD";
        case 0xC00000BB: return "STATUS_NOT_SUPPORTED";
        case 0xC0000225: return "STATUS_NOT_FOUND";
        case 0xC000009A: return "STATUS_INSUFFICIENT_RESOURCES";
        case 0xC0000904: return "STATUS_INVALID_DEVICE_STATE"; /* placeholder slot, keep table sorted-ish */
        default: snprintf(buf, sizeof buf, "0x%08lX", (unsigned long)(ULONG_PTR)st); return buf;
    }
}
void k_win_build(unsigned long *build, unsigned long *ubr) {
    typedef LONG (WINAPI *RtlGetVersion_t)(PRTL_OSVERSIONINFOW);
    RtlGetVersion_t fn = (RtlGetVersion_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    RTL_OSVERSIONINFOW vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi;
    if (fn) fn(&vi);
    if (build) *build = vi.dwBuildNumber;
    if (ubr) {
        *ubr = 0;
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &k) == 0) {
            DWORD val = 0, sz = sizeof val, type = 0;
            if (RegQueryValueExA(k, "UBR", NULL, &type, (LPBYTE)&val, &sz) == 0) *ubr = val;
            RegCloseKey(k);
        }
    }
}
bool k_is_19041(void) { unsigned long b = 0; k_win_build(&b, NULL); return b == 19041; }

unsigned long k_integrity_level(void) {
    HANDLE tok;
    unsigned long level = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 0;
    DWORD need = 0;
    GetTokenInformation(tok, TokenIntegrityLevel, NULL, 0, &need);
    if (need) {
        TOKEN_MANDATORY_LABEL *lbl = (TOKEN_MANDATORY_LABEL *)malloc(need);
        if (lbl && GetTokenInformation(tok, TokenIntegrityLevel, lbl, need, &need)) {
            DWORD sub = *GetSidSubAuthorityCount(lbl->Label.Sid) - 1;
            level = *GetSidSubAuthority(lbl->Label.Sid, sub);
        }
        free(lbl);
    }
    CloseHandle(tok);
    return level;
}
bool k_is_elevated(void) {
    HANDLE tok; bool elevated = false;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION te; DWORD sz;
    if (GetTokenInformation(tok, TokenElevation, &te, sizeof te, &sz)) elevated = te.TokenIsElevated != 0;
    CloseHandle(tok);
    return elevated;
}
unsigned long k_session_id(void) {
    DWORD sid = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    return sid;
}
bool k_has_interactive_desktop(void) {
    k_OpenInputDesktop_pfn od = k_get_OpenInputDesktop();
    k_GetUserObjectInformationA_pfn gi = k_get_GetUserObjectInformationA();
    k_CloseDesktop_pfn cd = k_get_CloseDesktop();
    if (!od || !gi || !cd) return false;
    HDESK d = od(0, FALSE, DESKTOP_READOBJECTS);
    if (!d) return false;
    char name[256] = {0};
    DWORD need = 0;
    bool interactive = true;
    if (gi(d, UOI_NAME, name, sizeof name, &need)) {
        if (_stricmp(name, "Winlogon") == 0 || _stricmp(name, "Disconnect") == 0) interactive = false;
    }
    cd(d);
    return interactive;
}
