/* x_ntqip_qip_sched_security / t_values: what NtQueryInformationProcess reports for the scheduling,
 * priority, security and per-process flag classes, and how it follows the public setters.
 *
 * Observed on Windows 10 22H2 x64:
 *   12  default hard error mode: the SetErrorMode value with bit 0 inverted (SEM_FAILCRITICALERRORS is stored
 *       as "not set"); a mode with any bit above the low three set also gets bit 2 (alignment faults) added.
 *   18  {BOOLEAN foreground, UCHAR class}: class numbers idle 1, normal 2, high 3, realtime 4, below normal 5,
 *       above normal 6, following SetPriorityClass.
 *   22  priority boost disabled: mirrors SetProcessPriorityBoost / GetProcessPriorityBoost.
 *   33  I/O priority (normal 2, very low 0 while a process is in background mode); 39 page priority (default 5,
 *       1 in background mode, set with SetProcessInformation(ProcessMemoryPriority)).
 *   29, 45, 46, 59, 74 read 0 for ordinary processes; 34 reports permanent DEP; 61 is the protection byte;
 *   36 a per-process non-zero cookie, stable and different per process, readable with PROCESS_VM_WRITE;
 *   92 a per-process start sequence number (unique, grows with creation order); 94 the security domain (0 for the elevated token, non-zero for a restricted one);
 *   87 and 96 one flag byte of the memory-logging settings.
 * Machine dependent numbers are checked only by relation. */
#include "qs.h"

static int fails;
#define CC(c) do { if (!(c)) { printf("  FAIL child: %s\n", #c); fails++; } } while (0)

static ULONG q32(HANDLE h, ULONG cls, LONG *st) {
    ULONG v = 0xCCCCCCCC, rl = 0; LONG s = qs_q(h, cls, &v, 4, &rl);
    if (st) *st = s;
    return v;
}
static unsigned q8(HANDLE h, ULONG cls, LONG *st) {   /* classes writing one byte, length 1 */
    unsigned char b = 0xCC; ULONG rl = 0; LONG s = qs_q(h, cls, &b, 1, &rl);
    if (st) *st = s;
    return b;
}
static unsigned long long q64(HANDLE h, ULONG cls, LONG *st) {
    _Alignas(8) unsigned long long v = 0xCCCCCCCCCCCCCCCCull; ULONG rl = 0; LONG s = qs_q(h, cls, &v, 8, &rl);
    if (st) *st = s;
    return v;
}
#define REQ_Q do { if (!qs_have()) { t_skip("NtQueryInformationProcess missing"); return; } } while (0)

/* ---- class 10 --------------------------------------------------------------------------------- */
/* Class 10 needs PROCESS_QUERY_INFORMATION|PROCESS_VM_READ and then reports STATUS_NOT_IMPLEMENTED for every length;
 * the handle is examined before the length. */
TEST(qip_ss_c10_pseudo) {
    REQ_Q;
    ULONG lens[] = { 0, 1, 2, 4, 8, 0x20, 0x100 };
    for (unsigned i = 0; i < 7; i++) {
        ULONG rl; LONG s = qs_call((HANDLE)-1, 10, 0, lens[i], 0, 0, &rl);
        CHECKF(s == QS_NOTIMPL, "len %lu: 0x%lx", lens[i], (unsigned long)s);
        CHECK(rl == QS_SENT);
    }
    ULONG rl; LONG s = qs_call((HANDLE)-1, 10, 0, 0, 1, 0, &rl);
    EXPECT_EQ((ULONG)s, (ULONG)QS_NOTIMPL);
    s = qs_call((HANDLE)-1, 10, 0, 4, 0, 1, &rl);
    EXPECT_EQ((ULONG)s, (ULONG)QS_NOTIMPL);
}
TEST(qip_ss_c10_rights) {
    REQ_Q;
    DWORD masks[] = { R_SYNC, R_LIM, R_QRY, R_VMR, R_VMW, R_VMO, 0x200, R_LIM | R_VMR, R_QRY | R_VMR, R_QRY | R_VMR | R_LIM,
                      R_QRY | R_VMW, 0x1fffff };
    HANDLE ch = qs_child();
    if (!ch) { t_skip("no child"); return; }
    for (int who = 0; who < 3; who++) for (unsigned i = 0; i < sizeof masks / sizeof *masks; i++) {
        HANDLE h = who == 0 ? qs_dup(ch, masks[i]) : who == 1 ? qs_self_open(masks[i]) : qs_dup(qs_dead(), masks[i]);
        if (!h) continue;
        ULONG rl; LONG s = qs_call(h, 10, 0, 4, 0, 0, &rl);
        LONG want = (masks[i] & (R_QRY | R_VMR)) == (R_QRY | R_VMR) ? QS_NOTIMPL : QS_DENIED;
        CHECKF(s == want, "who %d mask 0x%lx: 0x%lx want 0x%lx", who, masks[i], (unsigned long)s, (unsigned long)want);
        CHECK(rl == QS_SENT);
        CloseHandle(h);
    }
}
TEST(qip_ss_c10_handles) {
    REQ_Q;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE dup = qs_dup(qs_child(), R_QRY | R_VMR); if (dup) CloseHandle(dup);
    struct { HANDLE h; LONG want; } t[] = { { NULL, QS_BADHANDLE }, { dup, QS_BADHANDLE }, { (HANDLE)0x12344, QS_BADHANDLE },
        { (HANDLE)(ULONG_PTR)-2, QS_TYPEMISM }, { ev, QS_TYPEMISM } };
    /* the handle is looked at first: even lengths that other classes reject give the handle error */
    for (unsigned i = 0; i < 5; i++) for (ULONG len = 0; len < 10; len++) {
        if (!t[i].h && i) continue;
        ULONG rl; LONG s = qs_call(t[i].h, 10, 0, len, 0, 0, &rl);
        CHECKF(s == t[i].want, "handle #%u len %lu: 0x%lx want 0x%lx", i, len, (unsigned long)s, (unsigned long)t[i].want);
    }
    CloseHandle(ev);
}
TEST(qip_ss_c10_align) {
    REQ_Q;
    for (ULONG off = 1; off < 8; off++) {
        ULONG rl; LONG s = qs_call((HANDLE)-1, 10, off, 8, 0, 0, &rl);
        CHECKF(s == (off % 4 ? QS_MISALIGN : QS_NOTIMPL), "off %lu: 0x%lx", off, (unsigned long)s);
        s = qs_call((HANDLE)-1, 10, off, 0, 0, 0, &rl);
        CHECKF(s == QS_NOTIMPL, "off %lu len 0: 0x%lx", off, (unsigned long)s);
        s = qs_call(NULL, 10, off, 8, 0, 0, &rl);
        CHECKF(s == (off % 4 ? QS_MISALIGN : QS_BADHANDLE), "null handle off %lu: 0x%lx", off, (unsigned long)s);
    }
}

/* ---- invalid classes ---------------------------------------------------------------------------- */
static const ULONG g_invalid[] = { 5, 6, 8, 9, 11, 13, 16, 17, 25, 35, 40, 41, 48, 53, 56, 57, 62, 63, 68, 78, 80, 83, 86, 90, 91,
    93, 95, 98, 99, 100, 127, 0x100, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff };
/* Unassigned numbers (and the set-only ones) are rejected before the handle and the length are examined. */
TEST(qip_ss_invalid_class_pseudo) {
    REQ_Q;
    for (unsigned i = 0; i < sizeof g_invalid / sizeof *g_invalid; i++) {
        ULONG rl; LONG s = qs_call((HANDLE)-1, g_invalid[i], 0, 4, 0, 0, &rl);
        CHECKF(s == QS_BADCLASS, "class %lu: 0x%lx", g_invalid[i], (unsigned long)s);
        CHECK(rl == QS_SENT);
    }
}
TEST(qip_ss_invalid_class_handle_first_no) {
    REQ_Q;
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE hs[] = { NULL, (HANDLE)0x12344, (HANDLE)(ULONG_PTR)-2, ev };
    for (unsigned i = 0; i < sizeof g_invalid / sizeof *g_invalid; i++) for (unsigned j = 0; j < 4; j++) {
        ULONG rl; LONG s = qs_call(hs[j], g_invalid[i], 0, 4, 0, 0, &rl);
        CHECKF(s == QS_BADCLASS, "class %lu handle #%u: 0x%lx", g_invalid[i], j, (unsigned long)s);
    }
    CloseHandle(ev);
}
TEST(qip_ss_invalid_class_lengths) {
    REQ_Q;
    ULONG lens[] = { 0, 1, 8, 0x100 };
    for (unsigned i = 0; i < sizeof g_invalid / sizeof *g_invalid; i++) for (unsigned j = 0; j < 4; j++) {
        ULONG rl; LONG s = qs_call((HANDLE)-1, g_invalid[i], 0, lens[j], j == 0 && i % 2, 0, &rl);
        CHECKF(s == QS_BADCLASS, "class %lu len %lu: 0x%lx", g_invalid[i], lens[j], (unsigned long)s);
    }
}
TEST(qip_ss_invalid_class_denied_handle) {
    REQ_Q;
    HANDLE sy = qs_dup(qs_child(), R_SYNC);
    if (!sy) { t_skip("no handle"); return; }
    for (unsigned i = 0; i < sizeof g_invalid / sizeof *g_invalid; i++) {
        ULONG rl; LONG s = qs_call(sy, g_invalid[i], 0, 4, 0, 0, &rl);
        CHECKF(s == QS_BADCLASS, "class %lu: 0x%lx", g_invalid[i], (unsigned long)s);
    }
    CloseHandle(sy);
}
TEST(qip_ss_invalid_class_misaligned) {
    REQ_Q;
    /* the alignment test precedes the class switch */
    for (unsigned i = 0; i < sizeof g_invalid / sizeof *g_invalid; i++) {
        ULONG rl; LONG s = qs_call((HANDLE)-1, g_invalid[i], 1, 4, 0, 0, &rl);
        CHECKF(s == QS_MISALIGN, "class %lu: 0x%lx", g_invalid[i], (unsigned long)s);
        s = qs_call((HANDLE)-1, g_invalid[i], 2, 0, 0, 0, &rl);
        CHECKF(s == QS_BADCLASS, "class %lu len 0: 0x%lx", g_invalid[i], (unsigned long)s);
    }
}
TEST(qip_ss_class65_alignment_front_door) {
    REQ_Q;
    /* class 65 wants an 8-aligned buffer; only the alignment result is asserted here */
    for (ULONG off = 1; off < 8; off++) {
        ULONG rl; LONG s = qs_call((HANDLE)-1, 65, off, 0x20, 0, 0, &rl);
        CHECKF(s == QS_MISALIGN, "off %lu: 0x%lx", off, (unsigned long)s);
        s = qs_call(NULL, 65, off, 0x20, 0, 0, &rl);
        CHECKF(s == QS_MISALIGN, "null handle off %lu: 0x%lx", off, (unsigned long)s);
    }
    ULONG rl; LONG s = qs_call((HANDLE)-1, 65, 0, 0x20, 0, 0, &rl);
    CHECKF(s != QS_MISALIGN, "aligned: 0x%lx", (unsigned long)s);
    s = qs_call((HANDLE)-1, 65, 0, 0x20, 0, 0, &rl);
    EXPECT_NE((ULONG)s, (ULONG)QS_MISALIGN);
}

/* ---- class 12 ----------------------------------------------------------------------------------- */
typedef UINT (WINAPI *getem_t)(void);
/* The process-wide mode is changed here without ever setting SEM_NOALIGNMENTFAULTEXCEPT (value 4): once set
 * that bit cannot be cleared again, so its behaviour is measured in a child process (qsem below). */
TEST(qip_ss_c12_follows_seterrormode) {
    REQ_Q;
    UINT old = SetErrorMode(0);
    UINT modes[] = { 0, 1, 2, 3, 0x8000, 0x8001, 0x8002, 0x8003, 0x10, 0x20, 0x100, 0x4000, 0x7ffb, 0xfffb, 0xfffffffb };
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++) {
        SetErrorMode(modes[i]);
        LONG s; ULONG v = q32((HANDLE)-1, 12, &s);
        CHECKF(s == QS_OK && v == (modes[i] ^ 1), "mode 0x%x: got 0x%lx", modes[i], v);
        HANDLE sh = qs_self_open(R_LIM);
        if (sh) { CHECK(q32(sh, 12, &s) == v && s == QS_OK); CloseHandle(sh); }
    }
    SetErrorMode(old);
}
TEST(qip_ss_c12_getseterrormode_roundtrip) {
    REQ_Q;
    UINT old = SetErrorMode(0);
    getem_t ge = (getem_t)t_proc_in("kernel32.dll", "GetErrorMode");
    for (UINT m = 0; m < 4; m++) {
        UINT prev = SetErrorMode(m);
        if (m) CHECKF(prev == m - 1, "previous %u after %u", prev, m - 1);
        if (ge) CHECK(ge() == m);
        LONG s; CHECK(q32((HANDLE)-1, 12, &s) == (m ^ 1));
    }
    SetErrorMode(old);
}
CHILD(qsem) {
    LONG s; HANDLE me = GetCurrentProcess();
    ULONG v = q32(me, 12, &s);
    CC(s == 0 && v == 2);                    /* inherited from the parent: failcritical + nogpfault */
    SetErrorMode(4);
    v = q32(me, 12, &s); CC(s == 0 && v == 5);
    CC(SetErrorMode(0) == 4);                /* the alignment bit stays */
    v = q32(me, 12, &s); CC(s == 0 && v == 5);
    SetErrorMode(1);
    v = q32(me, 12, &s); CC(s == 0 && v == 4);
    SetErrorMode(0x8003);
    v = q32(me, 12, &s); CC(s == 0 && v == 0x8006);
    CC(SetErrorMode(0) == 0x8007);
    v = q32(me, 12, &s); CC(s == 0 && v == 5);
    return fails;
}
TEST(qip_ss_c12_alignment_bit_is_permanent) {
    REQ_Q;
    struct k_child_result r = k_child_run("qsem", "", 30000);
    EXPECT_EQ(r.exit_code, 0);
}
TEST(qip_ss_c12_child_independent) {
    REQ_Q;
    HANDLE h = qs_dup(qs_child(), R_LIM);
    if (!h) { t_skip("no child"); return; }
    LONG s1, s2; ULONG before = q32(h, 12, &s1);
    UINT old = SetErrorMode(0); SetErrorMode(0x8003);
    ULONG after = q32(h, 12, &s2);
    CHECK(s1 == QS_OK && s2 == QS_OK && before == after);   /* the setter acts on the caller only */
    SetErrorMode(old);
    CHECK((before & ~0xffffu) == 0);
    CloseHandle(h);
}

/* ---- class 18 ----------------------------------------------------------------------------------- */
static const struct { DWORD win; unsigned char cls; } g_pc[] = {
    { IDLE_PRIORITY_CLASS, 1 }, { BELOW_NORMAL_PRIORITY_CLASS, 5 }, { NORMAL_PRIORITY_CLASS, 2 },
    { ABOVE_NORMAL_PRIORITY_CLASS, 6 }, { HIGH_PRIORITY_CLASS, 3 } };
static unsigned c18_class(HANDLE h, LONG *s, unsigned *fg) {
    _Alignas(4) unsigned char b[2] = { 0xCC, 0xCC }; ULONG rl = 0;
    *s = qs_q(h, 18, b, 2, &rl);
    if (fg) *fg = b[0];
    return b[1];
}
TEST(qip_ss_c18_follows_setpriorityclass_child) {
    REQ_Q;
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    for (unsigned i = 0; i < 5; i++) {
        CHECK(SetPriorityClass(h, g_pc[i].win));
        LONG s; unsigned fg; unsigned c = c18_class(h, &s, &fg);
        CHECKF(s == QS_OK && c == g_pc[i].cls, "win 0x%lx -> %u", g_pc[i].win, c);
        CHECK(GetPriorityClass(h) == g_pc[i].win);
        CHECK(fg <= 1);
    }
    SetPriorityClass(h, NORMAL_PRIORITY_CLASS);
    CloseHandle(h);
}
TEST(qip_ss_c18_realtime_conditional) {
    REQ_Q;
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    if (!SetPriorityClass(h, REALTIME_PRIORITY_CLASS)) { t_skip("realtime refused (error %lu): token lacks the privilege", GetLastError()); CloseHandle(h); return; }
    /* a token without the privilege is silently given the high class instead */
    LONG s; unsigned c = c18_class(h, &s, NULL);
    DWORD wp = GetPriorityClass(h);
    CHECKF(s == QS_OK && c == (wp == REALTIME_PRIORITY_CLASS ? 4 : 3) && (wp == REALTIME_PRIORITY_CLASS || wp == HIGH_PRIORITY_CLASS), "got %u win 0x%lx", c, wp);
    SetPriorityClass(h, NORMAL_PRIORITY_CLASS);
    c = c18_class(h, &s, NULL);
    CHECK(c == 2);
    CloseHandle(h);
}
TEST(qip_ss_c18_self_matches_getpriorityclass) {
    REQ_Q;
    DWORD pc = GetPriorityClass(GetCurrentProcess());
    LONG s; unsigned fg; unsigned c = c18_class((HANDLE)-1, &s, &fg);
    unsigned want = 0;
    for (unsigned i = 0; i < 5; i++) if (g_pc[i].win == pc) want = g_pc[i].cls;
    if (!want) { t_skip("process runs at an unusual class 0x%lx", pc); return; }
    CHECKF(s == QS_OK && c == want, "class %u want %u", c, want);
    CHECK(fg <= 1);
}
TEST(qip_ss_c18_new_child_is_normal) {
    REQ_Q;
    HANDLE h = qs_dup(qs_child(), R_LIM);
    LONG s; unsigned c = c18_class(h, &s, NULL);
    CHECKF(s == QS_OK && c >= 1 && c <= 6, "class %u", c);
    CloseHandle(h);
}

/* ---- class 22 ----------------------------------------------------------------------------------- */
TEST(qip_ss_c22_follows_priority_boost) {
    REQ_Q;
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    LONG s; ULONG v = q32(h, 22, &s);
    CHECK(s == QS_OK && v == 0);   /* boosting enabled by default */
    for (int round = 0; round < 3; round++) for (int dis = 1; dis >= 0; dis--) {
        CHECK(SetProcessPriorityBoost(h, dis));
        BOOL g = -1; CHECK(GetProcessPriorityBoost(h, &g) && g == dis);
        v = q32(h, 22, &s);
        CHECKF(s == QS_OK && v == (ULONG)dis, "disable %d -> %lu", dis, v);
    }
    CloseHandle(h);
}
TEST(qip_ss_c22_self_mirror) {
    REQ_Q;
    BOOL g = -1; GetProcessPriorityBoost(GetCurrentProcess(), &g);
    LONG s; ULONG v = q32((HANDLE)-1, 22, &s);
    CHECK(s == QS_OK && v == (ULONG)g);
}

/* ---- class 33 / 39 ------------------------------------------------------------------------------ */
typedef BOOL (WINAPI *spi_t)(HANDLE, int, LPVOID, DWORD);
TEST(qip_ss_c39_follows_setprocessinformation) {
    REQ_Q;
    spi_t sp = (spi_t)t_proc_in("kernel32.dll", "SetProcessInformation");
    if (!sp) { t_skip("SetProcessInformation missing"); return; }
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    LONG s; ULONG v = q32(h, 39, &s);
    CHECK(s == QS_OK && v == 5);
    for (ULONG p = 1; p <= 5; p++) {
        ULONG in = p;
        CHECKF(sp(h, 0 /* ProcessMemoryPriority */, &in, 4), "set %lu err %lu", p, GetLastError());
        v = q32(h, 39, &s);
        CHECKF(s == QS_OK && v == p, "set %lu -> %lu", p, v);
    }
    ULONG in = 5; sp(h, 0, &in, 4);
    CloseHandle(h);
}
TEST(qip_ss_c33_default_normal) {
    REQ_Q;
    HANDLE h = qs_dup(qs_child(), R_LIM);
    LONG s; ULONG v = q32(h, 33, &s);
    CHECK(s == QS_OK && v == 2);
    v = q32((HANDLE)-1, 33, &s);
    CHECKF(s == QS_OK && v <= 4, "self io priority %lu", v);
    CloseHandle(h);
}
CHILD(qbg) {   /* background mode lowers the I/O and page priority of the caller, ending it restores them */
    LONG s; HANDLE me = GetCurrentProcess();
    CC(q32(me, 33, &s) == 2 && s == 0);
    CC(q32(me, 39, &s) == 5 && s == 0);
    CC(SetPriorityClass(me, PROCESS_MODE_BACKGROUND_BEGIN));
    ULONG io = q32(me, 33, &s); CC(s == 0 && io == 0);
    ULONG pg = q32(me, 39, &s); CC(s == 0 && pg == 1);
    CC(SetPriorityClass(me, PROCESS_MODE_BACKGROUND_END));
    CC(q32(me, 33, &s) == 2 && s == 0);
    CC(q32(me, 39, &s) == 5 && s == 0);
    return fails;
}
TEST(qip_ss_c33_c39_background_mode) {
    REQ_Q;
    struct k_child_result r = k_child_run("qbg", "", 30000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- flag classes ------------------------------------------------------------------------------- */
TEST(qip_ss_c29_c45_c46_c59_c74_defaults) {
    REQ_Q;
    HANDLE ch = qs_child();
    HANDLE hl = qs_dup(ch, R_QRY | R_LIM);
    HANDLE hs[] = { (HANDLE)-1, hl, qs_dup(qs_dead(), R_QRY) };
    for (unsigned i = 0; i < 3; i++) {
        LONG s;
        if (!hs[i]) continue;
        ULONG v = q32(hs[i], 29, &s);  CHECKF(s == QS_OK && v == 0, "c29 #%u: %lu", i, v);
        if (i != 2) { v = q32(hs[i], 45, &s);  CHECKF(s == QS_OK && v == 0, "c45 #%u: %lu", i, v);
                      v = q32(hs[i], 46, &s);  CHECKF(s == QS_OK && v == 0, "c46 #%u: %lu", i, v);
                      unsigned b = q8(hs[i], 74, &s); CHECKF(s == QS_OK && b == 0, "c74 #%u: %u", i, b); }
        v = q32(hs[i], 59, &s);  CHECKF(s == QS_OK && v == 0, "c59 #%u: %lu", i, v);
    }
    if (hs[1]) CloseHandle(hs[1]);
    if (hs[2]) CloseHandle(hs[2]);
}
TEST(qip_ss_c34_permanent_dep) {
    REQ_Q;
    LONG s; ULONG a = q32((HANDLE)-1, 34, &s);
    CHECK(s == QS_OK);
    CHECKF((a & 9) == 9, "flags 0x%lx", a);   /* execute-disable and permanent */
    HANDLE h = qs_dup(qs_child(), R_QRY);
    ULONG b = q32(h, 34, &s);
    CHECK(s == QS_OK && b == a);
    HANDLE sh = qs_self_open(R_QRY);
    ULONG c = q32(sh, 34, &s);
    CHECK(s == QS_OK && c == a);
    HANDLE d = qs_dup(qs_dead(), R_QRY);
    ULONG e = q32(d, 34, &s);
    CHECK(s == QS_OK && e == a);
    t_note("execute flags value: 0x%lx", a);
    CloseHandle(h); CloseHandle(sh); CloseHandle(d);
}
TEST(qip_ss_c34_pseudo_needs_no_right_but_real_does) {
    REQ_Q;
    HANDLE sh = qs_self_open(R_LIM | R_VMR | R_VMW | R_VMO);
    LONG s; q32(sh, 34, &s);
    CHECK(s == QS_DENIED);
    q32((HANDLE)-1, 34, &s);
    CHECK(s == QS_OK);
    CloseHandle(sh);
}
TEST(qip_ss_c59_requires_query_information) {
    REQ_Q;
    HANDLE sh = qs_self_open(R_LIM);
    LONG s; q32(sh, 59, &s);
    CHECK(s == QS_DENIED);
    CloseHandle(sh);
    sh = qs_self_open(R_QRY);
    ULONG v = q32(sh, 59, &s);
    CHECK(s == QS_OK && v == 0);
    CloseHandle(sh);
}
TEST(qip_ss_c61_protection_byte) {
    REQ_Q;
    LONG s; unsigned b = q8((HANDLE)-1, 61, &s);
    CHECK(s == QS_OK && b == 0);
    HANDLE h = qs_dup(qs_child(), R_LIM);
    b = q8(h, 61, &s); CHECK(s == QS_OK && b == 0);
    CloseHandle(h);
    h = qs_dup(qs_dead(), R_LIM);
    b = q8(h, 61, &s); CHECK(s == QS_OK && b == 0);
    CloseHandle(h);
    /* bigger buffers: only the first byte is written, ReturnLength says 1 */
    memset(qs_arena(), QS_FILL, 32); ULONG rl = 0;
    s = qs_q((HANDLE)-1, 61, qs_arena(), 32, &rl);
    CHECK(s == QS_OK && rl == 1 && qs_arena()[0] == 0 && qs_arena()[1] == QS_FILL);
}
TEST(qip_ss_c61_length_zero_orders) {
    REQ_Q;
    ULONG rl; HANDLE sy = qs_dup(qs_child(), R_SYNC), lim = qs_dup(qs_child(), R_LIM);
    /* length 0: a bad handle or a missing right is reported first, with a good handle the length error */
    EXPECT_EQ((ULONG)qs_call(NULL, 61, 0, 0, 0, 0, &rl), (ULONG)QS_BADHANDLE);
    EXPECT_EQ((ULONG)qs_call(sy, 61, 0, 0, 0, 0, &rl), (ULONG)QS_DENIED);
    EXPECT_EQ((ULONG)qs_call((HANDLE)(ULONG_PTR)-2, 61, 0, 0, 0, 0, &rl), (ULONG)QS_TYPEMISM);
    EXPECT_EQ((ULONG)qs_call(lim, 61, 0, 0, 0, 0, &rl), (ULONG)QS_LENMISM);
    EXPECT_EQ((ULONG)qs_call(lim, 61, 0, 0, 1, 0, &rl), (ULONG)QS_LENMISM);
    CloseHandle(sy); CloseHandle(lim);
}
TEST(qip_ss_c74_boolean) {
    REQ_Q;
    LONG s; unsigned b = q8((HANDLE)-1, 74, &s);
    CHECK(s == QS_OK && b == 0);
    ULONG rl;
    for (ULONG len = 2; len < 9; len++) EXPECT_EQ((ULONG)qs_call((HANDLE)-1, 74, 0, len, 0, 0, &rl), (ULONG)QS_LENMISM);
}
TEST(qip_ss_c87_c96_flag_bytes) {
    REQ_Q;
    LONG s; unsigned b87 = q8((HANDLE)-1, 87, &s);
    CHECK(s == QS_OK && (b87 & ~3u) == 0);
    ULONG v = 0xCCCCCC00; ULONG rl = 77;
    s = qs_q((HANDLE)-1, 96, &v, 4, &rl);
    CHECK(s == QS_OK && (v & 0xff) <= 0xf && (v >> 8) == 0xCCCCCC && rl == 77);
    HANDLE h = qs_dup(qs_child(), R_LIM);
    LONG s2; unsigned c87 = q8(h, 87, &s2);
    CHECK(s2 == QS_OK && c87 == b87);   /* both are ordinary processes of this session */
    CloseHandle(h);
    t_note("c87 byte 0x%x, c96 byte 0x%lx", b87, v & 0xff);
}
TEST(qip_ss_c87_c96_length_rules) {
    REQ_Q;
    ULONG rl;
    EXPECT_EQ((ULONG)qs_call((HANDLE)-1, 87, 0, 0, 0, 0, &rl), (ULONG)QS_LENMISM);
    EXPECT_EQ((ULONG)qs_call(NULL, 87, 0, 0, 0, 0, &rl), (ULONG)QS_LENMISM);   /* length zero precedes the handle */
    for (ULONG len = 1; len < 40; len += 7) EXPECT_EQ((ULONG)qs_call((HANDLE)-1, 87, 0, len, 0, 0, &rl), (ULONG)QS_OK);
    for (ULONG len = 0; len < 4; len++) EXPECT_EQ((ULONG)qs_call((HANDLE)-1, 96, 0, len, 0, 0, &rl), (ULONG)QS_LENMISM);
    for (ULONG len = 4; len < 40; len += 5) EXPECT_EQ((ULONG)qs_call((HANDLE)-1, 96, 0, len, 0, 0, &rl), (ULONG)QS_OK);
    EXPECT_EQ((ULONG)qs_call(NULL, 96, 0, 4, 0, 0, &rl), (ULONG)QS_BADHANDLE);
    EXPECT_EQ((ULONG)qs_call(NULL, 96, 0, 3, 0, 0, &rl), (ULONG)QS_LENMISM);
}

/* ---- class 36 (cookie) -------------------------------------------------------------------------- */
static HANDLE spawn_extra(DWORD *pid) {
    char a[32]; PROCESS_INFORMATION pi;
    snprintf(a, sizeof a, "%lu", GetCurrentProcessId());
    HANDLE h = t_spawn_child("qsleeper", a, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return NULL;
    CloseHandle(pi.hThread);
    if (pid) *pid = pi.dwProcessId;
    return h;
}
TEST(qip_ss_c36_cookie_self) {
    REQ_Q;
    LONG s; ULONG a = q32((HANDLE)-1, 36, &s);
    CHECK(s == QS_OK && a != 0);
    CHECK(q32((HANDLE)-1, 36, &s) == a && s == QS_OK);
    HANDLE sh = qs_self_open(R_VMW);
    CHECK(q32(sh, 36, &s) == a && s == QS_OK);
    CloseHandle(sh);
    sh = qs_self_open(R_QRY | R_VMR | R_LIM | R_VMO | 0x200);
    q32(sh, 36, &s);
    CHECK(s == QS_DENIED);
    CloseHandle(sh);
}
TEST(qip_ss_c36_cookie_foreign) {
    REQ_Q;
    LONG s; ULONG mine = q32((HANDLE)-1, 36, &s);
    HANDLE h = qs_dup(qs_child(), R_VMW);
    ULONG a = q32(h, 36, &s);
    CHECK(s == QS_OK && a != 0 && a != mine);
    CHECK(q32(h, 36, &s) == a);
    HANDLE all = qs_dup(qs_child(), 0x1fffff);
    CHECK(q32(all, 36, &s) == a && s == QS_OK);
    CloseHandle(h); CloseHandle(all);
    DWORD pid2; HANDLE c2 = spawn_extra(&pid2);
    if (!c2) { t_skip("second child failed"); return; }
    ULONG b = q32(c2, 36, &s);
    CHECK(s == QS_OK && b != 0 && b != a && b != mine);
    TerminateProcess(c2, 0); WaitForSingleObject(c2, 10000);
    CHECK(q32(c2, 36, &s) == b && s == QS_OK);   /* still answered, same value, after the process ended */
    CloseHandle(c2);
}
TEST(qip_ss_c36_cookie_right_matrix) {
    REQ_Q;
    DWORD ok[] = { R_VMW, R_VMW | R_LIM, R_VMW | R_QRY | R_VMR, 0x1fffff };
    DWORD bad[] = { R_SYNC, R_LIM, R_QRY, R_VMR, R_VMO, 0x200, R_LIM | R_QRY | R_VMR | R_VMO | 0x200 | 0x40 };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        HANDLE h = qs_dup(qs_child(), ok[i]); LONG s; q32(h, 36, &s);
        CHECKF(s == QS_OK, "mask 0x%lx: 0x%lx", ok[i], (unsigned long)s);
        CloseHandle(h);
    }
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        HANDLE h = qs_dup(qs_child(), bad[i]); LONG s; q32(h, 36, &s);
        CHECKF(s == QS_DENIED, "mask 0x%lx: 0x%lx", bad[i], (unsigned long)s);
        CloseHandle(h);
    }
}

/* ---- classes 92 and 94 --------------------------------------------------------------------------- */
TEST(qip_ss_c92_sequence_numbers) {
    REQ_Q;
    LONG s;
    unsigned long long me = q64((HANDLE)-1, 92, &s); CHECK(s == QS_OK && me != 0);
    HANDLE h = qs_dup(qs_child(), R_LIM);
    unsigned long long a = q64(h, 92, &s); CHECK(s == QS_OK && a != 0 && a != me);
    CHECK(q64(h, 92, &s) == a);
    CHECK(a < 0x100000000ull && me < 0x100000000ull);
    HANDLE sh = qs_self_open(R_LIM);
    CHECK(q64(sh, 92, &s) == me && s == QS_OK);
    CloseHandle(sh);
    DWORD pid2; HANDLE c2 = spawn_extra(&pid2);
    if (c2) {
        unsigned long long b = q64(c2, 92, &s);
        CHECK(s == QS_OK && b != a && b != me);
        CHECKF(b > a, "later child has the larger number: %llu vs %llu", b, a);
        TerminateProcess(c2, 0); WaitForSingleObject(c2, 10000);
        CHECK(q64(c2, 92, &s) == b && s == QS_OK);
        CloseHandle(c2);
    }
    CloseHandle(h);
}
TEST(qip_ss_c92_dead_process_keeps_number) {
    REQ_Q;
    HANDLE d = qs_dup(qs_dead(), R_LIM);
    LONG s; unsigned long long v = q64(d, 92, &s);
    CHECK(s == QS_OK && v != 0);
    CHECK(q64(d, 92, &s) == v);
    CloseHandle(d);
}
TEST(qip_ss_c94_security_domain) {
    REQ_Q;
    HANDLE hs[3] = { (HANDLE)-1, qs_dup(qs_child(), R_LIM), qs_dup(qs_dead(), R_LIM) };
    for (int i = 0; i < 3; i++) {
        LONG s; unsigned long long v = q64(hs[i], 94, &s);
        /* the value is an identifier of the security domain of the process token: 0 for the plain elevated token,
         * non-zero for restricted tokens, so only the relation between the three processes is checked */
        static unsigned long long first;
        if (!i) first = v;
        CHECKF(s == QS_OK && v == first, "handle #%d: status 0x%lx value %llu vs %llu", i, (unsigned long)s, v, first);
        if (i) CloseHandle(hs[i]);
    }
}

/* ---- another, protected process ----------------------------------------------------------------- */
TEST(qip_ss_other_process_csrss) {
    REQ_Q;
    DWORD pid = qs_find_pid("csrss.exe");
    if (!pid) { t_skip("no csrss.exe"); return; }
    HANDLE h = OpenProcess(R_LIM, FALSE, pid);
    if (!h) { t_skip("csrss.exe not openable here (error %lu)", GetLastError()); return; }
    LONG s;
    q32(h, 12, &s); CHECK(s == QS_OK);
    unsigned c = c18_class(h, &s, NULL); CHECKF(s == QS_OK && c >= 1 && c <= 6, "class %u", c);
    q32(h, 22, &s); CHECK(s == QS_OK);
    ULONG v = q32(h, 29, &s); CHECKF(s == QS_OK && v == 1, "csrss is a critical process: %lu", v);
    v = q32(h, 33, &s); CHECK(s == QS_OK && v <= 4);
    q32(h, 34, &s); CHECK(s == QS_DENIED);       /* needs PROCESS_QUERY_INFORMATION */
    q32(h, 36, &s); CHECK(s == QS_DENIED);       /* needs PROCESS_VM_WRITE */
    q32(h, 59, &s); CHECK(s == QS_DENIED);
    unsigned b = q8(h, 61, &s); CHECKF(s == QS_OK && b != 0, "protection byte 0x%x", b);
    unsigned long long q = q64(h, 92, &s); CHECK(s == QS_OK && q != 0);
    CHECK(q64(h, 94, &s) == 0 && s == QS_OK);
    ULONG rl; CHECK(qs_call(h, 10, 0, 4, 0, 0, &rl) == QS_DENIED);
    CloseHandle(h);
}
