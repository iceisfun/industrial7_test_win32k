/* r2_composition_surface_channel: shared declarations.
 *
 * Every syscall here is reached through its win32u.dll stub (UDECL + UN). Signatures are derived from the
 * win32kbase.sys / win32kfull.sys analysis of the service body and from the real callers
 * (dcomp.dll DirectComposition::KernelChannel / CDevice::Initialize, user32 exports, dwm-side callers); each
 * test file cites the branch that makes an expectation hold. Statuses used below:
 *   0xC0000001 STATUS_UNSUCCESSFUL, 0xC0000002 STATUS_NOT_IMPLEMENTED, 0xC0000003 STATUS_INVALID_INFO_CLASS,
 *   0xC0000008 STATUS_INVALID_HANDLE, 0xC000000D STATUS_INVALID_PARAMETER, 0xC0000022 STATUS_ACCESS_DENIED,
 *   0xC0000206 STATUS_INVALID_BUFFER_SIZE, 0xC0000225 STATUS_NOT_FOUND.
 * Wine has its own win32u without these services, so every test that enters a win32k-private syscall is
 * skipped there (NEED_WIN); the tests still compile and register everywhere. */
#ifndef RC_H
#define RC_H
#include "t.h"
#include "k.h"

#define RT(n) TEST(r2_composition_surface_channel_##n)

#define S_OK_       ((NTSTATUS)0)
#define S_UNSUCC    ((NTSTATUS)0xC0000001L)
#define S_NOTIMPL   ((NTSTATUS)0xC0000002L)
#define S_INFOCLASS ((NTSTATUS)0xC0000003L)
#define S_INVHANDLE ((NTSTATUS)0xC0000008L)
#define S_INVPARAM  ((NTSTATUS)0xC000000DL)
#define S_DENIED    ((NTSTATUS)0xC0000022L)
#define S_BUFSIZE   ((NTSTATUS)0xC0000206L)
#define S_NOTFOUND  ((NTSTATUS)0xC0000225L)

#ifdef RC_NO_WINE_SKIP
#define NEED_WIN() do { } while (0)
#else
#define NEED_WIN() do { if (t_is_wine()) { t_skip("win32k-private syscall; Wine's win32u does not implement it"); return; } } while (0)
#endif

#define CHECK_ST(got, want) do { NTSTATUS g_ = (NTSTATUS)(got); NTSTATUS w_ = (NTSTATUS)(want); \
    CHECKF(g_ == w_, "%s: got %s (0x%08lx) want %s", #got, k_status_name((K_NTSTATUS)g_), (unsigned long)g_, k_status_name((K_NTSTATUS)w_)); } while (0)
#define NT_FAIL(st) ((NTSTATUS)(st) < 0)

UDECL(NTSTATUS, NtQueryObject, (HANDLE h, ULONG cls, PVOID buf, ULONG len, PULONG ret));
UDECL(NTSTATUS, NtClose, (HANDLE h));
UDECL(ULONG, RtlNtStatusToDosError, (NTSTATUS st));

/* Number of handles the process holds (used to prove a failing syscall leaked nothing). */
static unsigned long rc_handles(void) {
    typedef BOOL (WINAPI *pfn)(HANDLE, PDWORD);
    pfn f = (pfn)t_proc_in("kernel32.dll", "GetProcessHandleCount");
    DWORD n = 0;
    if (!f || !f(GetCurrentProcess(), &n)) return 0;
    return n;
}
/* NtQueryObject(ObjectTypeInformation): type name of a kernel object handle, "" when it cannot be read. */
static void rc_type_name(HANDLE h, char *out, size_t cap) {
    unsigned char buf[1024];
    ULONG ret = 0;
    NtQueryObject_pfn q = UN(NtQueryObject);
    out[0] = 0;
    if (!q) return;
    memset(buf, 0, sizeof buf);
    if (q(h, 2, buf, sizeof buf, &ret) < 0) return;
    {
        USHORT len = *(USHORT *)buf;
        WCHAR *w = *(WCHAR **)(buf + 8);
        size_t i, n = len / 2;
        if (!w) return;
        for (i = 0; i < n && i + 1 < cap; i++) out[i] = (char)w[i];
        out[i] = 0;
    }
}
static void rc_close(HANDLE h) {
    NtClose_pfn c = UN(NtClose);
    if (c && h && h != INVALID_HANDLE_VALUE) c(h);
}
static ULONG rc_dos_error(NTSTATUS st) {
    RtlNtStatusToDosError_pfn f = UN(RtlNtStatusToDosError);
    return f ? f(st) : 0xFFFFFFFFu;
}

/* ---- DirectComposition application channel (win32kbase DirectComposition::CApplicationChannel) ---- */
UDECL(NTSTATUS, NtDCompositionCreateChannel, (UINT *pId, UINT *pSize, void **ppShared));
UDECL(NTSTATUS, NtDCompositionDestroyChannel, (UINT id));
UDECL(NTSTATUS, NtDCompositionCommitChannel, (UINT id, UINT *pBatch, BYTE *pFlag, int discard, HANDLE hSync));

UDECL(NTSTATUS, NtDCompositionCreateSharedResourceHandle, (ULONG_PTR type, HANDLE *phOut));

struct rc_chan { UINT id; UINT size; void *mem; int ok; };
/* Same call dcomp!CDevice::Initialize makes: size in = 0x1000. */
static struct rc_chan rc_chan_create(void) {
    struct rc_chan c; NtDCompositionCreateChannel_pfn f = UN(NtDCompositionCreateChannel);
    NTSTATUS st;
    memset(&c, 0, sizeof c);
    if (!f) return c;
    c.size = 0x1000;
    st = f(&c.id, &c.size, &c.mem);
    c.ok = (st == 0);
    if (!c.ok) t_note("NtDCompositionCreateChannel failed 0x%08lx", (unsigned long)st);
    return c;
}
static void rc_chan_destroy(struct rc_chan *c) {
    NtDCompositionDestroyChannel_pfn f = UN(NtDCompositionDestroyChannel);
    if (c->ok && f) f(c->id);
    c->ok = 0;
}
/* An id that is certainly not a live channel of this process (ids are small per-process counters). */
static UINT rc_bogus_id(UINT live) { return (live ^ 0x40000000u) | 0x00700000u; }

/* Make sure the process has DirectComposition per-process data (the channel table exists once a channel was
 * created), so "unknown channel" lookups reach RtlLookupElementGenericTable (0xC0000022) rather than the
 * no-process-data early exit (0xC0000001). Returns the keep-alive channel; destroy with rc_chan_destroy. */
#define REQUIRE_CHAN(c) do { (c) = rc_chan_create(); if (!(c).ok) { t_skip("cannot create a DirectComposition channel here"); return; } } while (0)

#endif
