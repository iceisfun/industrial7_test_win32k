/* x_ntqip_qip_basic_quota_handles: shared helpers (see qb.h). Not a test file. */
#include "qb.h"

typedef LONG (NTAPI *qip_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *sip_fn)(HANDLE, ULONG, PVOID, ULONG);
static qip_fn g_qip;
static sip_fn g_sip;
static unsigned char g_arena[0x3000] __attribute__((aligned(64)));
static unsigned char g_big[QB_BIG_SIZE] __attribute__((aligned(64)));

unsigned char *qb_arena(void) { return g_arena; }
unsigned char *qb_big(void) { return g_big; }
int qb_have(void) {
    if (!g_qip) g_qip = (qip_fn)t_proc_in("ntdll.dll", "NtQueryInformationProcess");
    if (!g_sip) g_sip = (sip_fn)t_proc_in("ntdll.dll", "NtSetInformationProcess");
    return g_qip != NULL;
}
LONG qb_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl) {
    if (!qb_have()) return (LONG)0xDEAD0001;
    return g_qip(h, cls, buf, len, rl);
}
LONG qb_set(HANDLE h, ULONG cls, void *buf, ULONG len) {
    if (!qb_have() || !g_sip) return (LONG)0xDEAD0001;
    return g_sip(h, cls, buf, len);
}
LONG qb_call(HANDLE h, ULONG cls, ULONG off, ULONG len, int nobuf, int norl, ULONG *rl_out) {
    ULONG rl = QB_SENT;
    memset(g_arena, QB_FILL, sizeof g_arena);
    LONG s = qb_q(h, cls, nobuf ? NULL : g_arena + off, len, norl ? NULL : &rl);
    if (rl_out) *rl_out = rl;
    return s;
}
int qb_untouched(ULONG from, ULONG to) {
    for (ULONG i = from; i < to && i < sizeof g_arena; i++) if (g_arena[i] != QB_FILL) return 0;
    return 1;
}
ULONGLONG qb_u64(ULONG off) { ULONGLONG v; memcpy(&v, g_arena + off, 8); return v; }
ULONG qb_u32(ULONG off) { ULONG v; memcpy(&v, g_arena + off, 4); return v; }

static HANDLE g_child, g_dead;
static DWORD g_cpid;
CHILD(qbsleeper) {   /* argv[1] = parent pid; lives until the parent is gone (bounded) */
    HANDLE p = argc > 1 ? OpenProcess(SYNCHRONIZE, FALSE, (DWORD)atoi(argv[1])) : NULL;
    if (p) WaitForSingleObject(p, 180000); else Sleep(60000);
    return 0;
}
CHILD(qbexit) { return 7; }
static void kill_children(void) {
    if (g_child) { TerminateProcess(g_child, 0); CloseHandle(g_child); g_child = NULL; }
    if (g_dead) { CloseHandle(g_dead); g_dead = NULL; }
}
static int g_hook;
/* wait until the idle child has finished its start-up: the handle count is the same for four reads 40 ms apart */
static void settle(HANDLE h) {
    ULONG last = 0xffffffff;
    int same = 0;
    for (int i = 0; i < 125 && same < 4; i++) {
        ULONG n = 0;
        if (qb_q(h, 20, &n, 4, NULL) != 0) return;
        if (n == last) same++; else { same = 0; last = n; }
        Sleep(40);
    }
}
HANDLE qb_fresh_child(DWORD *pid) {
    char a[32]; PROCESS_INFORMATION pi;
    snprintf(a, sizeof a, "%lu", GetCurrentProcessId());
    HANDLE h = t_spawn_child("qbsleeper", a, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return NULL;
    if (pid) *pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    settle(h);
    if (!g_hook) { g_hook = 1; atexit(kill_children); }
    return h;
}
HANDLE qb_child(void) {
    if (!g_child) g_child = qb_fresh_child(&g_cpid);
    return g_child;
}
DWORD qb_child_pid(void) { qb_child(); return g_cpid; }
HANDLE qb_dead(void) {
    if (g_dead) return g_dead;
    PROCESS_INFORMATION pi;
    HANDLE h = t_spawn_child("qbexit", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return NULL;
    CloseHandle(pi.hThread);
    if (WaitForSingleObject(h, 30000) != WAIT_OBJECT_0) TerminateProcess(h, 1);
    g_dead = h;
    if (!g_hook) { g_hook = 1; atexit(kill_children); }
    return g_dead;
}
HANDLE qb_child_open(DWORD mask) {
    DWORD pid = qb_child_pid();
    return pid ? OpenProcess(mask, FALSE, pid) : NULL;
}
HANDLE qb_closed_handle(void) {
    HANDLE h = qb_child_open(R_LIM);
    if (h) CloseHandle(h);
    return h;
}
int qb_is_admin_mode(void) { return k_integrity_level() >= SECURITY_MANDATORY_HIGH_RID; }
