/* x_ntqip_qip_image_session_peb: helpers, park child, and the protocol tests shared by the image / session /
 * environment information classes of NtQueryInformationProcess (Windows 10 22H2 x64).
 *
 * What is measured here (the per-class files measure layouts and relations):
 *   - alignment: a non-zero length with a buffer that is not 4-byte aligned is answered with
 *     STATUS_DATATYPE_MISALIGNMENT before anything else is looked at;
 *   - length protocol: fixed-size classes take exactly one length and leave the return length alone otherwise;
 *   - handle protocol: invalid handle, wrong object type, missing access right (QUERY_LIMITED vs QUERY);
 *   - the pseudo handle and a real handle to the caller answer identically;
 *   - failing calls never write into the caller's buffer;
 *   - NULL return length is accepted where documented, a NULL buffer with length 0 is not an access violation. */
#include "ip.h"

typedef LONG (NTAPI *qip_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
static qip_t g_qip;
static unsigned char *g_buf;

int ip_init(void) {
    if (!g_qip) g_qip = (qip_t)t_proc_in("ntdll.dll", "NtQueryInformationProcess");
    if (!g_buf) g_buf = (unsigned char *)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return g_qip != NULL && g_buf != NULL;
}
LONG ip_q(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *rl) {
    if (rl) *rl = RL_SENT;
    return g_qip(h, cls, buf, len, rl);
}
LONG ip_q_nullrl(HANDLE h, ULONG cls, void *buf, ULONG len) { return g_qip(h, cls, buf, len, NULL); }
unsigned char *ip_buf(void) { return g_buf; }
void ip_fill(void *p, size_t n) { memset(p, FILLB, n); }
int ip_all_filler(const void *p, size_t n) { const unsigned char *c = p; for (size_t i = 0; i < n; i++) if (c[i] != FILLB) return 0; return 1; }
HANDLE ip_dup_self(void) { return OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId()); }

/* ---- park child ------------------------------------------------------------------------------------------ */
CHILD(park) {
    DWORD ppid = argc > 1 ? (DWORD)strtoul(argv[1], NULL, 10) : 0;
    HANDLE p = ppid ? OpenProcess(SYNCHRONIZE, FALSE, ppid) : NULL;
    char name[64]; snprintf(name, sizeof name, "Local\\ipimg_ready_%lu", ppid);
    HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (ev) { SetEvent(ev); CloseHandle(ev); }
    if (p) WaitForSingleObject(p, 120000); else Sleep(120000);
    return 0;
}
CHILD(quick) { return 7; }

static HANDLE g_ready;
int kid_start(struct kid *k, const char *extra, DWORD cflags, const char *title, DWORD sflags, const char *exe) {
    char cmd[4096]; STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = sflags;
    if (sflags & STARTF_USESHOWWINDOW) si.wShowWindow = SW_HIDE;
    if (sflags & STARTF_USEPOSITION) { si.dwX = 10; si.dwY = 20; }
    if (sflags & STARTF_USESIZE) { si.dwXSize = 300; si.dwYSize = 200; }
    if (title) si.lpTitle = (char *)title;
    if (!g_ready) { char n[64]; snprintf(n, sizeof n, "Local\\ipimg_ready_%lu", GetCurrentProcessId()); g_ready = CreateEventA(NULL, FALSE, FALSE, n); }
    snprintf(cmd, sizeof cmd, "\"%s\" --child park %lu%s%s", exe ? exe : t_self_path(), GetCurrentProcessId(), extra && *extra ? " " : "", extra ? extra : "");
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, cflags, NULL, NULL, &si, &pi)) { k->h = NULL; return 0; }
    k->h = pi.hProcess; k->th = pi.hThread; k->pid = pi.dwProcessId;
    return 1;
}
int kid_start_w(struct kid *k, const WCHAR *title_w, DWORD sflags) {
    WCHAR cmd[2048], self[MAX_PATH]; STARTUPINFOW si; PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, self, MAX_PATH);
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = sflags; si.lpTitle = (WCHAR *)title_w;
    if (sflags & STARTF_USESHOWWINDOW) si.wShowWindow = SW_HIDE;
    if (!g_ready) { char n[64]; snprintf(n, sizeof n, "Local\\ipimg_ready_%lu", GetCurrentProcessId()); g_ready = CreateEventA(NULL, FALSE, FALSE, n); }
    swprintf(cmd, 2048, L"\"%ls\" --child park %lu", self, GetCurrentProcessId());
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { k->h = NULL; return 0; }
    k->h = pi.hProcess; k->th = pi.hThread; k->pid = pi.dwProcessId;
    return 1;
}
void kid_wait_ready(const struct kid *k) { if (g_ready) WaitForSingleObject(g_ready, 20000); (void)k; }
void kid_stop(struct kid *k) {
    if (k->h) { TerminateProcess(k->h, 0); WaitForSingleObject(k->h, 10000); CloseHandle(k->h); }
    if (k->th) CloseHandle(k->th);
    k->h = k->th = NULL;
}
HANDLE kid_open(const struct kid *k, DWORD rights) { return OpenProcess(rights, FALSE, k->pid); }

/* ---- class table ---------------------------------------------------------------------------------------- */
#define END 0xFFFF
const struct cls IPC[] = {
    { C_DEVMAP,    "devmap",   0x24, R_QRY, { 0, 0x20, 0x23, 0x28, 0x2c, 0x34, END }, 0 },
    { C_SESSION,   "session",  4,    R_LIM, { 0, 2, 3, 5, 8, END }, 0 },
    { C_WOW64,     "wow64",    8,    R_LIM, { 0, 4, 7, 9, 16, END }, 0 },
    { C_IMAGEINFO, "imginfo",  0x40, R_LIM, { 0, 0x3c, 0x3f, 0x41, 0x48, END }, 0 },
    { C_WOW86FLAG, "wow86flag",4,    R_QRY, { 0, 2, 3, 5, 8, END }, 0 },
    { C_AFFINITY,  "affinity", 8,    R_LIM, { 0, 4, 0xc, 0x14, 0x18, END }, 0 },
    { C_CONHOST,   "conhost",  8,    R_LIM, { 0, 4, 7, 9, 16, END }, 0 },
    { C_LUIDMAPS,  "luidmaps", 4,    R_NONE,{ 0, 2, 3, 5, 8, END }, 0 },
    { C_FILEMAP,   "filemap",  8,    R_QRY, { 0, 4, 7, 9, 16, END }, 1 },
    { C_SECTION,   "section",  8,    R_NONE,{ 0, 4, 7, 9, 16, END }, 1 },
    { C_DEFCPUSETS,"defcpu",   8,    R_LIM, { 1, 4, 7, 0xa1, 0xa8, 0xb0, END }, 0 },
    { C_ALLOWCPUSETS,"allowcpu",8,   R_LIM, { 1, 4, 7, 0xa1, 0xa8, 0xb0, END }, 0 },
};
const int IPC_N = sizeof IPC / sizeof IPC[0];
const struct cls *ip_cls(ULONG id) { for (int i = 0; i < IPC_N; i++) if (IPC[i].id == id) return &IPC[i]; return NULL; }

/* ---- generic protocol tests ------------------------------------------------------------------------------ */
/* Classes with a fixed accepted length: every other listed length is a length mismatch that leaves the return length
 * and the buffer alone. The handle is the pseudo handle, so only the length decides. */
static void gen_len_mismatch(ULONG id) {
    if (!ip_init()) return;
    const struct cls *c = ip_cls(id); unsigned char *b = ip_buf();
    for (int i = 0; c->bad[i] != END; i++) {
        ULONG rl; ip_fill(b, 0x100);
        LONG st = ip_q((HANDLE)-1, c->id, b, c->bad[i], &rl);
        CHECKF(st == ST_INFO_LEN, "class %lu len 0x%lx: status 0x%lx", c->id, c->bad[i], (unsigned long)st);
        CHECKF(rl == RL_SENT, "class %lu len 0x%lx: return length written (%lu)", c->id, c->bad[i], rl);
        CHECKF(ip_all_filler(b, 0x100), "class %lu len 0x%lx: buffer touched", c->id, c->bad[i]);
    }
}
/* A NULL buffer with length 0 is a plain length mismatch (no access violation). */
static void gen_null_buffer_len0(ULONG id) {
    if (!ip_init()) return;
    ULONG rl; LONG st = ip_q((HANDLE)-1, id, NULL, 0, &rl);
    EXPECT_EQ(st, ST_INFO_LEN);
    CHECK(rl == RL_SENT);
}
/* Invalid / wrong-type handles with an accepted length. */
static void gen_bad_handles(ULONG id) {
    if (!ip_init()) return;
    const struct cls *c = ip_cls(id); unsigned char *b = ip_buf();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    struct { const char *n; HANDLE h; LONG want; } H[] = {
        { "null", NULL, ST_INVALID_HANDLE }, { "unused slot", (HANDLE)(ULONG_PTR)0x7ffc0000, ST_INVALID_HANDLE },
        { "-3", (HANDLE)(LONG_PTR)-3, ST_INVALID_HANDLE },
        { "thread pseudo", (HANDLE)(LONG_PTR)-2, ST_TYPE_MISMATCH }, { "event", ev, ST_TYPE_MISMATCH },
    };
    for (int i = 0; i < 5; i++) {
        ULONG rl; ip_fill(b, 0x100);
        LONG st = ip_q(H[i].h, c->id, b, c->ok, &rl);
        CHECKF(st == H[i].want, "class %lu handle %s: status 0x%lx want 0x%lx", c->id, H[i].n, (unsigned long)st, (unsigned long)H[i].want);
        CHECKF(rl == RL_SENT && ip_all_filler(b, 0x100), "class %lu handle %s: output written", c->id, H[i].n);
    }
    CloseHandle(ev);
}
/* Access rights of a handle to another process: PROCESS_SYNCHRONIZE alone never suffices; QUERY_LIMITED suffices for
 * the R_LIM classes, the R_QRY classes need PROCESS_QUERY_INFORMATION. */
static void gen_rights(ULONG id) {
    if (!ip_init()) return;
    const struct cls *c = ip_cls(id); unsigned char *b = ip_buf();
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start child"); return; }
    kid_wait_ready(&k);
    DWORD rights[] = { SYNCHRONIZE, PROC_LIM, PROC_QRY, PROCESS_ALL_ACCESS };
    for (int i = 0; i < 4; i++) {
        HANDLE h = kid_open(&k, rights[i]);
        if (!h) { t_skip("OpenProcess 0x%lx refused", rights[i]); continue; }
        ULONG rl; ip_fill(b, 0x100);
        LONG st = ip_q(h, c->id, b, c->ok, &rl);
        LONG want;
        if (rights[i] == SYNCHRONIZE) want = ST_ACCESS_DENIED;
        else if (rights[i] == PROC_LIM && c->right == R_QRY) want = ST_ACCESS_DENIED;
        else want = ST_OK;
        if (c->id == C_DEVMAP && want == ST_OK) want = ST_INVALID_PARAM;   /* another process has no device map to report */
        CHECKF(st == want, "class %lu rights 0x%lx: status 0x%lx want 0x%lx", c->id, rights[i], (unsigned long)st, (unsigned long)want);
        if (want != ST_OK) CHECKF(rl == RL_SENT && ip_all_filler(b, 0x100), "class %lu rights 0x%lx: output written", c->id, rights[i]);
        CloseHandle(h);
    }
    kid_stop(&k);
}
/* The pseudo handle and a real PROCESS_ALL_ACCESS handle to the caller give identical status, length and bytes. */
static void gen_pseudo_vs_real(ULONG id) {
    if (!ip_init()) return;
    const struct cls *c = ip_cls(id); unsigned char *b = ip_buf();
    HANDLE self = ip_dup_self(); if (!self) { t_skip("cannot open self"); return; }
    unsigned char a1[0x100], a2[0x100]; ULONG r1, r2;
    ip_fill(b, 0x100); LONG s1 = ip_q((HANDLE)-1, c->id, b, c->ok, &r1); memcpy(a1, b, 0x100);
    ip_fill(b, 0x100); LONG s2 = ip_q(self, c->id, b, c->ok, &r2); memcpy(a2, b, 0x100);
    CHECKF(s1 == ST_OK && s2 == ST_OK, "class %lu: 0x%lx / 0x%lx", c->id, (unsigned long)s1, (unsigned long)s2);
    CHECKF(r1 == r2, "class %lu: return lengths differ %lu %lu", c->id, r1, r2);
    CHECKF(!memcmp(a1, a2, 0x100), "class %lu: bytes differ", c->id);
    CloseHandle(self);
}
/* A NULL return length is accepted. */
static void gen_rl_null(ULONG id) {
    if (!ip_init()) return;
    const struct cls *c = ip_cls(id); unsigned char *b = ip_buf();
    ip_fill(b, 0x100);
    LONG st = g_qip((HANDLE)-1, c->id, b, c->ok, NULL);
    EXPECT_EQ(st, ST_OK);
}
/* On success the return length is the number of bytes written and nothing past it is touched. */
static void gen_ok_rl(ULONG id) {
    if (!ip_init()) return;
    const struct cls *c = ip_cls(id); unsigned char *b = ip_buf();
    ULONG rl; ip_fill(b, 0x100);
    LONG st = ip_q((HANDLE)-1, c->id, b, c->ok, &rl);
    EXPECT_EQ(st, ST_OK);
    CHECKF(rl == c->ok, "class %lu: return length %lu want %lu", c->id, rl, c->ok);
    CHECKF(ip_all_filler(b + c->ok, 0x100 - c->ok), "class %lu: wrote past the accepted length", c->id);
}
/* Misalignment: any non-zero length at an address that is not a multiple of 4 gives STATUS_DATATYPE_MISALIGNMENT
 * before length, class-specific rules or the handle are looked at. Length 0 is exempt. */
static void gen_misaligned(ULONG id, ULONG len) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    for (int off = 1; off <= 3; off++) {
        ip_fill(b, 0x200);
        LONG st = ip_q((HANDLE)-1, id, b + off, len, &rl);
        CHECKF(st == ST_MISALIGN, "class %lu offset %d: status 0x%lx", id, off, (unsigned long)st);
        CHECK(rl == RL_SENT);
        st = ip_q(NULL, id, b + off, len, &rl);     /* the null handle is not reached */
        CHECKF(st == ST_MISALIGN, "class %lu offset %d null handle: status 0x%lx", id, off, (unsigned long)st);
        st = ip_q((HANDLE)-1, id, b + off, 0x3, &rl);   /* a wrong length does not matter either */
        CHECKF(st == ST_MISALIGN, "class %lu offset %d wrong length: status 0x%lx", id, off, (unsigned long)st);
    }
}
static void gen_len0_unaligned_ok(ULONG id) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    LONG st = ip_q((HANDLE)-1, id, b + 1, 0, &rl);
    CHECKF(st != ST_MISALIGN, "class %lu: length 0 at an odd address is checked for alignment (0x%lx)", id, (unsigned long)st);
}
/* 4-byte alignment is enough even for classes whose fields are 8 bytes wide. */
static void gen_align4_enough(ULONG id, ULONG len) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    ip_fill(b, 0x200);
    LONG st = ip_q((HANDLE)-1, id, b + 4, len, &rl);
    CHECKF(st == ST_OK, "class %lu at offset 4: status 0x%lx", id, (unsigned long)st);
}

#define GEN_LEN(tag, id)   TEST(ip_len_mismatch_##tag) { gen_len_mismatch(id); }
#define GEN_HAND(tag, id)  TEST(ip_bad_handles_##tag) { gen_bad_handles(id); }
#define GEN_RIGHTS(tag, id) TEST(ip_rights_##tag) { gen_rights(id); }
#define GEN_PV(tag, id)    TEST(ip_pseudo_vs_real_##tag) { gen_pseudo_vs_real(id); }
#define GEN_RLN(tag, id)   TEST(ip_rl_null_##tag) { gen_rl_null(id); }
#define GEN_OKRL(tag, id)  TEST(ip_ok_return_length_##tag) { gen_ok_rl(id); }
#define GEN_MIS(tag, id, len) TEST(ip_misaligned_##tag) { gen_misaligned(id, len); }
#define GEN_NB(tag, id)    TEST(ip_null_buffer_len0_##tag) { gen_null_buffer_len0(id); }

GEN_LEN(devmap, C_DEVMAP)   GEN_LEN(session, C_SESSION)   GEN_LEN(wow64, C_WOW64)     GEN_LEN(imginfo, C_IMAGEINFO)
GEN_LEN(wow86flag, C_WOW86FLAG) GEN_LEN(affinity, C_AFFINITY) GEN_LEN(conhost, C_CONHOST) GEN_LEN(luidmaps, C_LUIDMAPS)
GEN_LEN(defcpu, C_DEFCPUSETS) GEN_LEN(allowcpu, C_ALLOWCPUSETS)

GEN_HAND(devmap, C_DEVMAP)  GEN_HAND(session, C_SESSION)  GEN_HAND(wow64, C_WOW64)    GEN_HAND(imginfo, C_IMAGEINFO)
GEN_HAND(wow86flag, C_WOW86FLAG) GEN_HAND(affinity, C_AFFINITY) GEN_HAND(conhost, C_CONHOST)
GEN_HAND(defcpu, C_DEFCPUSETS) GEN_HAND(allowcpu, C_ALLOWCPUSETS)

GEN_RIGHTS(devmap, C_DEVMAP) GEN_RIGHTS(session, C_SESSION) GEN_RIGHTS(wow64, C_WOW64) GEN_RIGHTS(imginfo, C_IMAGEINFO)
GEN_RIGHTS(wow86flag, C_WOW86FLAG) GEN_RIGHTS(affinity, C_AFFINITY) GEN_RIGHTS(conhost, C_CONHOST)
GEN_RIGHTS(defcpu, C_DEFCPUSETS) GEN_RIGHTS(allowcpu, C_ALLOWCPUSETS)

GEN_PV(session, C_SESSION) GEN_PV(wow64, C_WOW64) GEN_PV(imginfo, C_IMAGEINFO) GEN_PV(wow86flag, C_WOW86FLAG)
GEN_PV(affinity, C_AFFINITY) GEN_PV(luidmaps, C_LUIDMAPS) GEN_PV(defcpu, C_DEFCPUSETS) GEN_PV(allowcpu, C_ALLOWCPUSETS)

GEN_RLN(devmap, C_DEVMAP) GEN_RLN(session, C_SESSION) GEN_RLN(wow64, C_WOW64) GEN_RLN(imginfo, C_IMAGEINFO)
GEN_RLN(wow86flag, C_WOW86FLAG) GEN_RLN(affinity, C_AFFINITY) GEN_RLN(conhost, C_CONHOST) GEN_RLN(luidmaps, C_LUIDMAPS)
GEN_RLN(defcpu, C_DEFCPUSETS) GEN_RLN(allowcpu, C_ALLOWCPUSETS)

GEN_OKRL(devmap, C_DEVMAP) GEN_OKRL(session, C_SESSION) GEN_OKRL(wow64, C_WOW64) GEN_OKRL(imginfo, C_IMAGEINFO)
GEN_OKRL(wow86flag, C_WOW86FLAG) GEN_OKRL(affinity, C_AFFINITY) GEN_OKRL(conhost, C_CONHOST) GEN_OKRL(luidmaps, C_LUIDMAPS)

GEN_MIS(devmap, C_DEVMAP, 0x24) GEN_MIS(session, C_SESSION, 4) GEN_MIS(wow64, C_WOW64, 8) GEN_MIS(ntpath, C_NTPATH, 0x100)
GEN_MIS(dospath, C_DOSPATH, 0x100) GEN_MIS(imginfo, C_IMAGEINFO, 0x40) GEN_MIS(cmdline, C_CMDLINE, 0x100)
GEN_MIS(section, C_SECTION, 8) GEN_MIS(conhost, C_CONHOST, 8) GEN_MIS(wininfo, C_WININFO, 0x100) GEN_MIS(luidmaps, C_LUIDMAPS, 4)
GEN_MIS(wow86flag, C_WOW86FLAG, 4) GEN_MIS(affinity, C_AFFINITY, 8) GEN_MIS(groups, C_GROUPS, 2) GEN_MIS(defcpu, C_DEFCPUSETS, 8)
GEN_MIS(allowcpu, C_ALLOWCPUSETS, 8) GEN_MIS(leap, C_LEAP, 8) GEN_MIS(filemap, C_FILEMAP, 8)

TEST(ip_length_zero_is_not_alignment_checked) {
    ULONG ids[] = { C_DEVMAP, C_SESSION, C_WOW64, C_NTPATH, C_DOSPATH, C_IMAGEINFO, C_CMDLINE, C_SECTION, C_CONHOST, C_WININFO,
                    C_LUIDMAPS, C_WOW86FLAG, C_AFFINITY, C_GROUPS, C_DEFCPUSETS, C_ALLOWCPUSETS, C_LEAP, C_FILEMAP };
    for (int i = 0; i < 18; i++) gen_len0_unaligned_ok(ids[i]);
}
TEST(ip_four_byte_alignment_is_enough) {
    gen_align4_enough(C_WOW64, 8); gen_align4_enough(C_CONHOST, 8); gen_align4_enough(C_AFFINITY, 0x10);
    gen_align4_enough(C_IMAGEINFO, 0x40); gen_align4_enough(C_SESSION, 4); gen_align4_enough(C_LUIDMAPS, 4);
    gen_align4_enough(C_DEFCPUSETS, 0x10);
}
TEST(ip_null_buffer_with_length_zero_fixed_classes) {
    ULONG ids[] = { C_DEVMAP, C_SESSION, C_WOW64, C_IMAGEINFO, C_WOW86FLAG, C_AFFINITY, C_CONHOST, C_LUIDMAPS, C_FILEMAP, C_SECTION };
    for (int i = 0; i < 10; i++) gen_null_buffer_len0(ids[i]);
}
TEST(ip_invalid_class_number_is_reported) {
    if (!ip_init()) return;
    ULONG rl; unsigned char *b = ip_buf();
    ip_fill(b, 0x40);
    LONG st = ip_q((HANDLE)-1, 0xFFFFFFFF, b, 0x20, &rl);
    EXPECT_EQ(st, (LONG)0xC0000003);
    st = ip_q(NULL, 0x7FFFFFFF, b, 0x20, &rl);       /* checked before the handle */
    EXPECT_EQ(st, (LONG)0xC0000003);
}
