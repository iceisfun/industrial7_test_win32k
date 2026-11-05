/* x_ntqip_qip_sched_security: shared helpers (see qs.h). Not a test file. */
#include "qs.h"
#include <tlhelp32.h>

typedef LONG (NTAPI *qip_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static qip_fn g_qip;
static unsigned char g_arena[0x800] __attribute__((aligned(64)));

static const struct qs_cls g_tab[] = {
    /* id  mode     len right  align rlw rlv nwr */
    { 12, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 18, M_EXACT, 2, K_LIM, 4, 1, 2, 2 },
    { 22, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 29, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 33, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 34, M_EXACT, 4, K_QRY, 4, 1, 4, 4 },
    { 36, M_EXACT, 4, K_VMW, 4, 1, 4, 4 },
    { 39, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 45, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 46, M_EXACT, 4, K_LIM, 4, 1, 4, 4 },
    { 59, M_EXACT, 4, K_QRY, 4, 0, 0, 4 },
    { 61, M_MIN1,  1, K_LIM, 1, 1, 1, 1 },
    { 74, M_EXACT, 1, K_LIM, 1, 0, 0, 1 },
    { 87, M_MIN1,  1, K_LIM, 1, 0, 0, 1 },
    { 92, M_EXACT, 8, K_LIM, 8, 1, 8, 8 },
    { 94, M_EXACT, 8, K_LIM, 8, 1, 8, 8 },
    { 96, M_MIN4,  4, K_LIM, 4, 0, 0, 1 },
};
const struct qs_cls *qs_find(ULONG id) {
    for (unsigned i = 0; i < sizeof g_tab / sizeof *g_tab; i++) if (g_tab[i].id == id) return &g_tab[i];
    return NULL;
}
int qs_have(void) {
    if (!g_qip) g_qip = (qip_fn)t_proc_in("ntdll.dll", "NtQueryInformationProcess");
    return g_qip != NULL;
}
unsigned char *qs_arena(void) { return g_arena; }
LONG qs_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl) {
    if (!qs_have()) return (LONG)0xDEAD0001;
    return g_qip(h, cls, buf, len, rl);
}
LONG qs_call(HANDLE h, ULONG cls, ULONG off, ULONG len, int nobuf, int norl, ULONG *rl_out) {
    ULONG rl = QS_SENT;
    memset(g_arena, QS_FILL, sizeof g_arena);
    LONG s = qs_q(h, cls, nobuf ? NULL : g_arena + off, len, norl ? NULL : &rl);
    if (rl_out) *rl_out = rl;
    return s;
}
int qs_mask_ok(int right, DWORD mask) {
    switch (right) {
    case K_LIM: return (mask & (R_LIM | R_QRY)) != 0;
    case K_QRY: return (mask & R_QRY) != 0;
    default:    return (mask & R_VMW) != 0;
    }
}
int qs_len_ok(const struct qs_cls *c, ULONG len) {
    switch (c->mode) {
    case M_EXACT: return len == c->len;
    case M_MIN1:  return len >= 1;
    default:      return len >= 4;
    }
}
ULONG qs_good_len(const struct qs_cls *c) { return c->len; }

static HANDLE g_child, g_dead;
static DWORD g_cpid;
CHILD(qsleeper) {   /* argv[1] = parent pid; lives until the parent is gone (bounded) */
    HANDLE p = argc > 1 ? OpenProcess(SYNCHRONIZE, FALSE, (DWORD)atoi(argv[1])) : NULL;
    if (p) WaitForSingleObject(p, 180000); else Sleep(60000);
    return 0;
}
CHILD(qexit) { return 7; }
static void kill_children(void) {
    if (g_child) { TerminateProcess(g_child, 0); CloseHandle(g_child); g_child = NULL; }
    if (g_dead) { CloseHandle(g_dead); g_dead = NULL; }
}
HANDLE qs_child(void) {
    if (g_child) return g_child;
    char a[32]; PROCESS_INFORMATION pi;
    snprintf(a, sizeof a, "%lu", GetCurrentProcessId());
    HANDLE h = t_spawn_child("qsleeper", a, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return NULL;
    g_child = h; g_cpid = pi.dwProcessId; CloseHandle(pi.hThread);
    atexit(kill_children);
    return g_child;
}
DWORD qs_child_pid(void) { qs_child(); return g_cpid; }
HANDLE qs_dead(void) {
    if (g_dead) return g_dead;
    PROCESS_INFORMATION pi;
    HANDLE h = t_spawn_child("qexit", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return NULL;
    CloseHandle(pi.hThread);
    if (WaitForSingleObject(h, 30000) != WAIT_OBJECT_0) { TerminateProcess(h, 1); }
    g_dead = h; qs_child();   /* make sure the exit hook is installed */
    return g_dead;
}
HANDLE qs_dup(HANDLE full, DWORD mask) {
    HANDLE out = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), full, GetCurrentProcess(), &out, mask, FALSE, 0)) return NULL;
    return out;
}
HANDLE qs_self_open(DWORD mask) { return OpenProcess(mask, FALSE, GetCurrentProcessId()); }
DWORD qs_find_pid(const char *exe) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 e; DWORD r = 0;
    if (s == INVALID_HANDLE_VALUE) return 0;
    e.dwSize = sizeof e;
    if (Process32First(s, &e)) do { if (!_stricmp(e.szExeFile, exe)) { r = e.th32ProcessID; break; } } while (Process32Next(s, &e));
    CloseHandle(s);
    return r;
}
int qs_is_admin_mode(void) { return k_integrity_level() >= SECURITY_MANDATORY_HIGH_RID; }
