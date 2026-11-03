/* Report the identity the process runs with: used to verify the std (unprivileged) and adm launch modes.
 * Prints PROBE lines: user, session, integrity, elevation, window station, desktop, privileges, group
 * attributes of the Administrators SID, and then the output of whoami /groups /priv.
 * Exit 0 always; the verdict is read from the PROBE lines. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

static void *tinfo(HANDLE t, TOKEN_INFORMATION_CLASS c) {
    DWORD n = 0; GetTokenInformation(t, c, NULL, 0, &n);
    void *p = n ? malloc(n) : NULL;
    if (p && !GetTokenInformation(t, c, p, n, &n)) { free(p); p = NULL; }
    return p;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    HANDLE t; OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t);
    DWORD sid = (DWORD)-1; ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    printf("PROBE session=%lu\n", sid);
    TOKEN_USER *u = tinfo(t, TokenUser); char *s = NULL;
    if (u && ConvertSidToStringSidA(u->User.Sid, &s)) {
        printf("PROBE user_sid=%s\n", s);
        /* the account's own RID is guest-specific: classify with well-known RIDs only */
        const char *k = strstr(s, "-500") ? "builtin-administrator" : strstr(s, "-501") ? "guest" : "other-account";
        printf("PROBE account=%s\n", k);
    }
    TOKEN_MANDATORY_LABEL *il = tinfo(t, TokenIntegrityLevel);
    if (il) { PUCHAR n = GetSidSubAuthorityCount(il->Label.Sid); printf("PROBE integrity_rid=0x%lx\n", *GetSidSubAuthority(il->Label.Sid, *n - 1)); }
    TOKEN_ELEVATION_TYPE et = 0; DWORD n; GetTokenInformation(t, TokenElevationType, &et, sizeof et, &n);
    TOKEN_ELEVATION el = { 0 }; GetTokenInformation(t, TokenElevation, &el, sizeof el, &n);
    DWORD restricted = 0; GetTokenInformation(t, TokenHasRestrictions, &restricted, sizeof restricted, &n);
    printf("PROBE elevation_type=%d is_elevated=%lu has_restrictions=%lu\n", (int)et, el.TokenIsElevated, restricted);
    char nm[128] = "?";
    GetUserObjectInformationA(GetProcessWindowStation(), UOI_NAME, nm, sizeof nm, &n);
    printf("PROBE window_station=%s\n", nm);
    strcpy(nm, "?");
    HDESK d = GetThreadDesktop(GetCurrentThreadId());
    GetUserObjectInformationA(d, UOI_NAME, nm, sizeof nm, &n);
    printf("PROBE desktop=%s\n", nm);
    TOKEN_PRIVILEGES *tp = tinfo(t, TokenPrivileges); int dbg = 0;
    if (tp) for (DWORD i = 0; i < tp->PrivilegeCount; i++) {
        char pn[64]; DWORD l = sizeof pn; LookupPrivilegeNameA(NULL, &tp->Privileges[i].Luid, pn, &l);
        printf("PROBE priv=%s attr=0x%lx\n", pn, tp->Privileges[i].Attributes);
        if (!strcmp(pn, "SeDebugPrivilege")) dbg = 1;
    }
    printf("PROBE SeDebugPrivilege=%s\n", dbg ? "present" : "absent");
    PSID adm = NULL; ConvertStringSidToSidA("S-1-5-32-544", &adm);
    TOKEN_GROUPS *g = tinfo(t, TokenGroups); const char *state = "not-in-token";
    if (g) for (DWORD i = 0; i < g->GroupCount; i++) if (EqualSid(g->Groups[i].Sid, adm)) {
        DWORD a = g->Groups[i].Attributes;
        state = (a & SE_GROUP_USE_FOR_DENY_ONLY) ? "deny-only" : (a & SE_GROUP_ENABLED) ? "enabled" : "disabled";
    }
    printf("PROBE administrators_sid=%s\n", state);
    BOOL isadm = FALSE; CheckTokenMembership(NULL, adm, &isadm);
    printf("PROBE CheckTokenMembership_admin=%d\n", (int)isadm);
    PSID usr = NULL; ConvertStringSidToSidA("S-1-5-32-545", &usr);
    BOOL isusr = FALSE; CheckTokenMembership(NULL, usr, &isusr);
    printf("PROBE CheckTokenMembership_users=%d\n", (int)isusr);
    if (g) {
        DWORD ng = 0; char list[1024] = "";
        for (DWORD i = 0; i < g->GroupCount; i++) {
            DWORD a = g->Groups[i].Attributes; char *gs = NULL;
            if (!(a & SE_GROUP_LOGON_ID) && ConvertSidToStringSidA(g->Groups[i].Sid, &gs)) {
                if (strncmp(gs, "S-1-5-21-", 9) && strlen(list) + strlen(gs) + 8 < sizeof list)
                    sprintf(list + strlen(list), " %s%s", gs, (a & SE_GROUP_USE_FOR_DENY_ONLY) ? "(deny)" : (a & SE_GROUP_ENABLED) ? "" : "(off)");
                ng++;
            }
        }
        printf("PROBE groups=%s\n", list);
    }
    fflush(stdout);
    if (!(argc > 1 && !strcmp(argv[1], "--no-whoami"))) system("whoami /groups /priv");
    return 0;
}
