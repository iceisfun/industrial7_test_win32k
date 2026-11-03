/* Launch a command line with a genuinely unprivileged token that still lives in the interactive session.
 *
 * usage: w32k_runas.exe <command line of the program to run>
 *
 * The caller (an elevated local Administrator in the interactive session) is only the source of the
 * token. The child gets a CreateRestrictedToken copy of it with
 *   - every privilege except SeChangeNotifyPrivilege removed (DISABLE_MAX_PRIVILEGE),
 *   - Administrators and the other powerful built-in aliases present in the token set deny-only,
 *   - integrity lowered to Medium,
 *   - owner and default DACL set to the user SID (the deny-only Administrators group grants nothing).
 * It is started with CreateProcessAsUser on winsta0\default in the same session, so window-station and
 * desktop tests behave as for a normal logged-on standard user. It does not use CreateProcessWithLogon
 * (that would land in a non-interactive window station). The child runs inside a kill-on-close job, so
 * killing this helper kills the whole tree. The child's standard handles are inherited from this process.
 * Exit code: the child's exit code, or 0xDEAD00xx when launching failed (message on stderr).
 */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

static void enable_priv(HANDLE tok, const wchar_t *name) {
    TOKEN_PRIVILEGES tp; tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, name, &tp.Privileges[0].Luid)) AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
}
static void *tok_info(HANDLE t, TOKEN_INFORMATION_CLASS c) {
    DWORD n = 0; GetTokenInformation(t, c, NULL, 0, &n);
    void *p = n ? malloc(n) : NULL;
    if (p && !GetTokenInformation(t, c, p, n, &n)) { free(p); p = NULL; }
    return p;
}
static const wchar_t *rest_of_cmdline(void) {
    const wchar_t *p = GetCommandLineW();
    if (*p == L'"') { p++; while (*p && *p != L'"') p++; if (*p) p++; }
    else while (*p && *p != L' ' && *p != L'\t') p++;
    while (*p == L' ' || *p == L'\t') p++;
    return p;
}
#define FAIL(code, what) do { fprintf(stderr, "w32k_runas: %s failed, error %lu\n", what, GetLastError()); return 0xDEAD0000 | (code); } while (0)

int main(void) {
    const wchar_t *cmd = rest_of_cmdline();
    if (!*cmd) { fprintf(stderr, "usage: w32k_runas <command line>\n"); return 0xDEAD0001; }
    HANDLE self = NULL, rtok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &self)) FAIL(2, "OpenProcessToken");
    enable_priv(self, L"SeIncrementQuotaPrivilege");
    enable_priv(self, L"SeAssignPrimaryTokenPrivilege");

    /* deny-only list: every group of the token that is one of the powerful built-in aliases */
    static const char *powerful[] = { "S-1-5-32-544", "S-1-5-32-547", "S-1-5-32-548", "S-1-5-32-549", "S-1-5-32-550",
                                      "S-1-5-32-551", "S-1-5-32-552", "S-1-5-32-556", "S-1-5-32-578", "S-1-5-32-580" };
    PSID psid[16]; int np = 0;
    for (int i = 0; i < (int)(sizeof powerful / sizeof *powerful); i++) {
        PSID s = NULL;
        if (ConvertStringSidToSidA(powerful[i], &s)) psid[np++] = s;
    }
    TOKEN_GROUPS *groups = tok_info(self, TokenGroups);
    if (!groups) FAIL(3, "TokenGroups");
    SID_AND_ATTRIBUTES dis[16]; int nd = 0;
    for (DWORD g = 0; g < groups->GroupCount; g++)
        for (int i = 0; i < np; i++)
            if (EqualSid(groups->Groups[g].Sid, psid[i]) && nd < 16) { dis[nd].Sid = groups->Groups[g].Sid; dis[nd].Attributes = 0; nd++; }

    if (!CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, nd, nd ? dis : NULL, 0, NULL, 0, NULL, &rtok)) FAIL(4, "CreateRestrictedToken");

    /* integrity: Medium (what a standard user's interactive token has) */
    PSID med = NULL; ConvertStringSidToSidA("S-1-16-8192", &med);
    TOKEN_MANDATORY_LABEL tml = { { med, SE_GROUP_INTEGRITY } };
    if (!SetTokenInformation(rtok, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(med))) FAIL(5, "set integrity");

    /* owner and default DACL: the user SID and SYSTEM */
    TOKEN_USER *user = tok_info(self, TokenUser);
    if (!user) FAIL(6, "TokenUser");
    TOKEN_OWNER own = { user->User.Sid };
    SetTokenInformation(rtok, TokenOwner, &own, sizeof own);
    {
        PSID sys = NULL; ConvertStringSidToSidA("S-1-5-18", &sys);
        DWORD sz = sizeof(ACL) + 2 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) + GetLengthSid(user->User.Sid) + GetLengthSid(sys) + 16;
        ACL *acl = calloc(1, sz);
        InitializeAcl(acl, sz, ACL_REVISION);
        AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, user->User.Sid);
        AddAccessAllowedAce(acl, ACL_REVISION, GENERIC_ALL, sys);
        TOKEN_DEFAULT_DACL dd = { acl };
        SetTokenInformation(rtok, TokenDefaultDacl, &dd, sizeof dd);
    }

    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = { 0 };
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }

    wchar_t *cl = _wcsdup(cmd);
    STARTUPINFOW si = { 0 }; si.cb = sizeof si;
    si.lpDesktop = L"winsta0\\default";
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE); si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    if (!CreateProcessAsUserW(rtok, NULL, cl, NULL, NULL, TRUE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi)) {
        /* breakaway not permitted by the parent job: retry without it */
        if (!CreateProcessAsUserW(rtok, NULL, cl, NULL, NULL, TRUE, CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) FAIL(7, "CreateProcessAsUser");
    }
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) fprintf(stderr, "w32k_runas: warning, job assign failed %lu\n", GetLastError());
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0; GetExitCodeProcess(pi.hProcess, &rc);
    return (int)rc;
}
