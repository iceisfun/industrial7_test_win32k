/* helpers for x_ntqip_qip_mitigation_policy_job_energy (see qh.h) */
#include "qh.h"

static LONG (NTAPI *pq)(HANDLE, ULONG, PVOID, ULONG, PULONG);

ULONG qh(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl) {
    if (!pq) pq = (void *)t_proc_in("ntdll.dll", "NtQueryInformationProcess");
    if (rl) *rl = RL_UNSET;
    return (ULONG)pq(h, cls, buf, len, rl);
}

struct qh_res qh_probe(HANDLE h, ULONG cls, ULONG len, const void *in, ULONG inlen, ULONG skip) {
    static unsigned char A[QH_MAXLEN + 16] __attribute__((aligned(16)));
    static unsigned char B[QH_MAXLEN + 16] __attribute__((aligned(16)));
    struct qh_res r; memset(&r, 0, sizeof r);
    memset(A, FILL_A, sizeof A); memset(B, FILL_B, sizeof B);
    if (in && inlen) { memcpy(A, in, inlen); memcpy(B, in, inlen); }
    ULONG rlA, rlB;
    ULONG sa = qh(h, cls, len ? A : NULL, len, &rlA);
    ULONG sb = qh(h, cls, len ? B : NULL, len, &rlB);
    r.st = sa; r.rl = rlA;
    if (sa != sb || rlA != rlB) t_note("probe: two runs differ st %lx/%lx rl %lx/%lx", sa, sb, rlA, rlB);
    for (ULONG i = skip; i < len; i++)
        if (!(A[i] == FILL_A && B[i] == FILL_B)) { r.touched_cnt++; r.touched_end = i + 1; }
    memcpy(r.out, A, sizeof r.out);
    return r;
}

int qh_err(const char *tag, HANDLE h, ULONG cls, ULONG len, ULONG want, const void *in, ULONG inlen, ULONG skip) {
    struct qh_res r = qh_probe(h, cls, len, in, inlen, skip);
    int ok = 1;
    if (r.st != want) { CHECKF(0, "%s: cls %lu len %lu status 0x%lx want 0x%lx", tag, cls, len, r.st, want); ok = 0; }
    if (r.rl != RL_UNSET) { CHECKF(0, "%s: cls %lu len %lu ReturnLength written (0x%lx) on error", tag, cls, len, r.rl); ok = 0; }
    if (r.touched_cnt) { CHECKF(0, "%s: cls %lu len %lu output written on error (%lu bytes)", tag, cls, len, r.touched_cnt); ok = 0; }
    return ok;
}

HANDLE qh_open(DWORD rights, DWORD pid) {
    return OpenProcess(rights, FALSE, pid ? pid : GetCurrentProcessId());
}
HANDLE qh_closed_handle(void) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (h) CloseHandle(h);
    return h;
}
int qh_in_job(HANDLE h) {
    BOOL r = FALSE;
    if (!IsProcessInJob(h, NULL, &r)) return -1;
    return r ? 1 : 0;
}

const struct qh_cls qh_classes[] = {
    {52, 8, "52"}, {69, 0x28, "69"}, {70, 1, "70"}, {73, 3, "73"}, {75, 4, "75"}, {79, 8, "79"}, {72, 16, "72"},
    {76, 0x1b0, "76"}, {77, 12, "77"}, {81, 0x30, "81"}, {82, 0x90, "82"}, {84, 4, "84"}, {88, 0x38, "88"}, {65, 0x20, "65"},
};
const int qh_nclasses = sizeof qh_classes / sizeof qh_classes[0];

/* ---- children ------------------------------------------------------------------------------------------ */
CHILD(qsleep) {
    if (argc > 1) { HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[1]); if (e) { SetEvent(e); CloseHandle(e); } }
    for (int i = 0; i < 600; i++) Sleep(100);
    return 0;
}

int qh_child_start(struct qh_child *c, int variant, HANDLE job) {
    static int seq;
    char evn[96], cmd[2048];
    memset(c, 0, sizeof *c);
    snprintf(evn, sizeof evn, "Local\\w32kt_qip_%lu_%d", GetCurrentProcessId(), ++seq);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, evn);
    if (!ev) return 0;
    snprintf(cmd, sizeof cmd, "\"%s\" --child qsleep %s", t_self_path(), evn);
    STARTUPINFOEXA si; memset(&si, 0, sizeof si); si.StartupInfo.cb = sizeof si;
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    DWORD64 mit = 1ULL << 28; DWORD cp = 1; DWORD flags = 0; SIZE_T sz = 0;
    if (variant) {
        InitializeProcThreadAttributeList(NULL, 1, 0, &sz);
        si.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, sz);
        InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &sz);
        if (variant == QC_WIN32K_DISABLE) UpdateProcThreadAttribute(si.lpAttributeList, 0, 0x00020007, &mit, 8, NULL, NULL);
        else UpdateProcThreadAttribute(si.lpAttributeList, 0, 0x0002000E, &cp, 4, NULL, NULL);
        flags = EXTENDED_STARTUPINFO_PRESENT;
    }
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si.StartupInfo, &pi);
    if (si.lpAttributeList) { DeleteProcThreadAttributeList(si.lpAttributeList); HeapFree(GetProcessHeap(), 0, si.lpAttributeList); }
    if (!ok) { CloseHandle(ev); return 0; }
    CloseHandle(pi.hThread);
    int ready = WaitForSingleObject(ev, 20000) == WAIT_OBJECT_0;
    CloseHandle(ev);
    c->h = pi.hProcess; c->pid = pi.dwProcessId;
    if (!ready) { qh_child_stop(c); return 0; }
    if (job && !AssignProcessToJobObject(job, c->h)) { qh_child_stop(c); return 0; }
    return 1;
}
void qh_child_stop(struct qh_child *c) {
    if (c->h) { TerminateProcess(c->h, 0); WaitForSingleObject(c->h, 5000); CloseHandle(c->h); }
    c->h = NULL;
}
