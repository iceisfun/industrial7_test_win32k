/* x_d3dkmt_badhandle_matrix_b / nthandle: the three thunks that take an NT handle to a shared graphics object.
 *
 * OpenResourceFromNtHandle (handle in the second qword), OpenProtectedSessionFromNtHandle and
 * QueryProtectedSessionInfoFromNtHandle (handle in the first qword) look the NT handle up as an object of their own
 * type. Measures, with real kernel handles of the wrong kind and with handles that name nothing:
 *   - an event, file, process, section, mutex, or the current-process / current-thread pseudo handle is
 *     STATUS_OBJECT_TYPE_MISMATCH (the handle exists, it is the wrong kind of object);
 *   - zero, 1, 0x1234, 0xFFFFFFFF and a value whose low dword is zero are STATUS_INVALID_HANDLE;
 *   - a handle placed in the other qword is not seen (the answer is the zero-block answer);
 *   - the handle is neither closed nor consumed: it still works afterwards, and the process handle count is unchanged;
 *   - QueryProtectedSessionInfoFromNtHandle validates its size and pointer dwords before it looks the handle up, so
 *     a real event handle plus any one of those dwords set is STATUS_INVALID_PARAMETER instead of the type mismatch.
 *
 * Determinism: handle values are never printed; only symbolic statuses are.
 */
#include "x.h"

struct nth { const char *name; unsigned off; unsigned other; };
static const struct nth NTH[] = {
    { "OpenResourceFromNtHandle", 8, 0 },
    { "OpenProtectedSessionFromNtHandle", 0, 8 },
    { "QueryProtectedSessionInfoFromNtHandle", 0, 8 },
};

static LONG call_with(x_fn f, unsigned off, HANDLE h) {
    char *p = x_edge(0x400);
    *(HANDLE *)(p + off) = h;
    return f(p);
}

static void real_handles(const struct nth *a) {
    x_fn f = x_nt(a->name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE fl = CreateFileW(L"NUL", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    HANDLE sec = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x1000, NULL);
    HANDLE mt = CreateMutexW(NULL, FALSE, NULL);
    CHECK(ev && fl != INVALID_HANDLE_VALUE && pr && sec && mt);
    DWORD h0 = 0, h1 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h0);
    X_ST(call_with(f, a->off, ev), XS_TYPE_MISMATCH, "%s(event)", a->name);
    X_ST(call_with(f, a->off, fl), XS_TYPE_MISMATCH, "%s(file)", a->name);
    X_ST(call_with(f, a->off, pr), XS_TYPE_MISMATCH, "%s(process)", a->name);
    X_ST(call_with(f, a->off, sec), XS_TYPE_MISMATCH, "%s(section)", a->name);
    X_ST(call_with(f, a->off, mt), XS_TYPE_MISMATCH, "%s(mutex)", a->name);
    X_ST(call_with(f, a->off, (HANDLE)(LONG_PTR)-1), XS_TYPE_MISMATCH, "%s(current process pseudo handle)", a->name);
    X_ST(call_with(f, a->off, (HANDLE)(LONG_PTR)-2), XS_TYPE_MISMATCH, "%s(current thread pseudo handle)", a->name);
    GetProcessHandleCount(GetCurrentProcess(), &h1);
    CHECKF(h0 == h1, "%s: process handle count changed", a->name);
    /* the handles are still ours and still work */
    CHECK(SetEvent(ev));
    CHECK(WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
    
    CHECK(CloseHandle(ev)); CHECK(CloseHandle(fl)); CHECK(CloseHandle(pr)); CHECK(CloseHandle(sec)); CHECK(CloseHandle(mt));
}

static void no_such_handles(const struct nth *a) {
    x_fn f = x_nt(a->name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    static const ULONGLONG v[] = { 0, 1, 0x1234, 0xFFFFFFFFull, 0x100000000ull, 0x7FFFFFFF00000000ull };
    static const char *const n[] = { "zero", "1", "0x1234", "0xFFFFFFFF", "low dword zero", "high bits only" };
    for (unsigned i = 0; i < XARR(v); i++)
        X_ST(call_with(f, a->off, (HANDLE)(ULONG_PTR)v[i]), XS_INVALID_HANDLE, "%s(%s)", a->name, n[i]);
}

static void wrong_slot(const struct nth *a) {
    x_fn f = x_nt(a->name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(ev != NULL);
    /* the handle where the thunk does not look: same answer as a zero block */
    char *p = x_edge(0x400);
    *(HANDLE *)(p + a->other) = ev;
    LONG r = f(p);
    if (!strcmp(a->name, "QueryProtectedSessionInfoFromNtHandle")) {
        /* offset 8 is the size dword pair: a nonzero value there fails the argument check first */
        X_ST(r, XS_INVALID_PARAMETER, "%s(event in the size slot)", a->name);
    } else {
        X_ST(r, XS_INVALID_HANDLE, "%s(event in the other slot)", a->name);
    }
    CloseHandle(ev);
}

#define NTH_TESTS(idx, tag) \
TEST(x_nth_##tag##_real_handles_wrong_type) { real_handles(&NTH[idx]); } \
TEST(x_nth_##tag##_handles_naming_nothing) { no_such_handles(&NTH[idx]); } \
TEST(x_nth_##tag##_handle_in_other_slot) { wrong_slot(&NTH[idx]); }
NTH_TESTS(0, openresource)
NTH_TESTS(1, openprotected)
NTH_TESTS(2, queryprotected)

TEST(x_nth_queryprotected_argument_check_precedes_lookup) {
    x_fn f = x_nt("QueryProtectedSessionInfoFromNtHandle");
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(ev != NULL);
    X_ST(call_with(f, 0, ev), XS_TYPE_MISMATCH, "event handle, otherwise zero block");
    static const unsigned dw[] = { 2, 3, 4, 6, 7, 8 };
    for (unsigned i = 0; i < XARR(dw); i++) {
        DWORD *p = x_edge(0x400);
        *(HANDLE *)p = ev;
        p[dw[i]] = 1;
        X_ST(f(p), XS_INVALID_PARAMETER, "event handle with dword %u = 1", dw[i]);
    }
    /* dwords the thunk ignores do not change the lookup */
    static const unsigned ign[] = { 5, 9, 10, 11, 12 };
    for (unsigned i = 0; i < XARR(ign); i++) {
        DWORD *p = x_edge(0x400);
        *(HANDLE *)p = ev;
        p[ign[i]] = 0xDEADBEEF;
        X_ST(f(p), XS_TYPE_MISMATCH, "event handle with dword %u = garbage", ign[i]);
    }
    CloseHandle(ev);
}

TEST(x_nth_all_three_close_nothing) {
    if (!x_nt("OpenResourceFromNtHandle")) { t_skip("win32u export not available (emulator or missing export)"); return; }
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(ev != NULL);
    for (int round = 0; round < 32; round++)
        for (unsigned i = 0; i < XARR(NTH); i++)
            call_with(x_nt(NTH[i].name), NTH[i].off, ev);
    DWORD flags = 0;
    CHECK(GetHandleInformation(ev, &flags));
    CHECK(SetEvent(ev));
    CHECK(CloseHandle(ev));
}
