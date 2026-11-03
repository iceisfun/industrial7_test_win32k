/* x_pointer_touch_inertia_neg / infolist: NtUserGetPointerInfoList(id, kind, history, frame, cbEach, &entries, &pointers, out).
 *
 * Measures the argument screening of the pointer-info list query, in the order the kernel applies it, without ever
 * needing a live pointer. The trick: an output buffer that cannot be written (address 1) is probed only after every
 * argument check has passed, so "last error ERROR_NOACCESS" means the arguments were accepted and ERROR_INVALID_PARAMETER
 * means they were refused before the buffer was looked at. A pointer id that does not exist (0x1234) is refused with
 * ERROR_INVALID_PARAMETER only after the buffer was probed, so it is used where the buffer is valid.
 *
 * Observed on Windows 10 22H2:
 *   - the pair (kind, cbEach) must be one of (1,0x60) (2,0x90) (3,0x78) (5,0x90); every other kind (0, 4, 6..9, large) or
 *     size (0, 0x70, 0x98, 0xA0, all ones) is refused;
 *   - id must be non-zero with a zero high word; id 1 (the mouse) and 0xFFFF are accepted at this stage;
 *   - both count pointers must be non-NULL and both counts non-zero; entries * pointers * cbEach must fit 32 bits (the
 *     product of the two counts must itself fit 32 bits first);
 *   - a NULL output is refused unless the history or frame flag is set; the history and frame flags do not change the
 *     screening of the other arguments;
 *   - an unwritable count pointer is ERROR_NOACCESS (counts are read first), a NULL count pointer ERROR_INVALID_PARAMETER;
 *   - a pointer id that is not one of the thread's pointers is ERROR_INVALID_PARAMETER with both counts and the buffer
 *     untouched; the mouse id 1 is refused the same way while mouse-in-pointer is off for the thread.
 *
 * Determinism: only symbolic results are printed.
 */
#include "x.h"

static const struct { ULONG kind; UINT cb; } good_pairs[] = { {1, 0x60}, {2, 0x90}, {3, 0x78}, {5, 0x90} };
static int pair_ok(ULONG kind, UINT cb) {
    for (unsigned i = 0; i < sizeof good_pairs / sizeof good_pairs[0]; i++) if (good_pairs[i].kind == kind && good_pairs[i].cb == cb) return 1;
    return 0;
}

/* Calls with an unwritable output; returns the last error. Counts must come back as given. */
static DWORD probe(UINT id, ULONG kind, int hist, int frame, UINT cb, UINT a, UINT b) {
    UINT ea = a, eb = b;
    SET_ERR(0x4D2);
    LONGLONG r = UN(NtUserGetPointerInfoList)(id, kind, hist, frame, cb, &ea, &eb, XP_BADPTR);
    DWORD gle = GetLastError();
    CHECKF((r & 0xFFFFFFFF) == 0, "id %x kind %lu cb %x: returned 0x%llx", id, kind, cb, (unsigned long long)r);
    CHECKF(ea == a && eb == b, "id %x kind %lu cb %x: counts changed to %u/%u", id, kind, cb, ea, eb);
    return gle;
}
static void expect_accept(UINT id, ULONG kind, int hist, int frame, UINT cb, UINT a, UINT b, const char *what) {
    DWORD gle = probe(id, kind, hist, frame, cb, a, b);
    CHECKF(gle == XE_NOACCESS, "%s: last error %lu, want NOACCESS (arguments accepted)", what, gle);
}
static void expect_refuse(UINT id, ULONG kind, int hist, int frame, UINT cb, UINT a, UINT b, const char *what) {
    DWORD gle = probe(id, kind, hist, frame, cb, a, b);
    CHECKF(gle == XE_INVALID_PARAMETER, "%s: last error %lu, want INVALID_PARAMETER (arguments refused)", what, gle);
}

/* ---- kind x size table ---- */
static void kind_row(ULONG kind) {
    NEED(NtUserGetPointerInfoList);
    static const UINT sizes[] = { 0, 0x60, 0x70, 0x78, 0x90, 0x98, 0xA0, 0x100, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        char w[64]; snprintf(w, sizeof w, "kind %lu size %x", kind, sizes[i]);
        if (pair_ok(kind, sizes[i])) expect_accept(0x1234, kind, 0, 0, sizes[i], 1, 1, w);
        else expect_refuse(0x1234, kind, 0, 0, sizes[i], 1, 1, w);
    }
}
#define KIND(k) XT(kind_##k) { kind_row(k); }
KIND(0) KIND(1) KIND(2) KIND(3) KIND(4) KIND(5) KIND(6) KIND(7) KIND(8) KIND(9) KIND(0x10) KIND(0x100) KIND(0x80000000u) KIND(0xFFFFFFFFu)

XT(good_pairs_accepted_for_every_history_frame_combination) {
    NEED(NtUserGetPointerInfoList);
    for (unsigned i = 0; i < 4; i++) for (int h = 0; h < 2; h++) for (int f = 0; f < 2; f++)
        expect_accept(0x1234, good_pairs[i].kind, h, f, good_pairs[i].cb, 1, 1, "history/frame combination");
}
XT(wrong_size_refused_for_every_history_frame_combination) {
    NEED(NtUserGetPointerInfoList);
    for (unsigned i = 0; i < 4; i++) for (int h = 0; h < 2; h++) for (int f = 0; f < 2; f++)
        expect_refuse(0x1234, good_pairs[i].kind, h, f, good_pairs[i].cb + 8, 1, 1, "history/frame combination");
}
XT(history_and_frame_are_plain_booleans) {
    NEED(NtUserGetPointerInfoList);
    static const int v[] = { 1, 2, -1, 0x100, 0x7FFFFFFF };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        expect_accept(0x1234, 1, v[i], 0, 0x60, 1, 1, "history non-zero");
        expect_accept(0x1234, 1, 0, v[i], 0x60, 1, 1, "frame non-zero");
        expect_accept(0x1234, 1, v[i], v[i], 0x60, 1, 1, "both non-zero");
    }
}

/* ---- pointer id ---- */
#define IDCASE(nm, id, acc) XT(id_##nm) { NEED(NtUserGetPointerInfoList); \
    if (acc) { expect_accept(id, 1, 0, 0, 0x60, 1, 1, #nm); expect_accept(id, 2, 0, 0, 0x90, 1, 1, #nm " touch"); expect_accept(id, 3, 0, 0, 0x78, 1, 1, #nm " pen"); } \
    else { expect_refuse(id, 1, 0, 0, 0x60, 1, 1, #nm); expect_refuse(id, 2, 0, 0, 0x90, 1, 1, #nm " touch"); expect_refuse(id, 3, 0, 0, 0x78, 1, 1, #nm " pen"); } }
IDCASE(zero, 0, 0)
IDCASE(one_mouse, 1, 1)
IDCASE(two, 2, 1)
IDCASE(0x1234, 0x1234, 1)
IDCASE(0x7FFF, 0x7FFF, 1)
IDCASE(0x8000, 0x8000, 1)
IDCASE(0xFFFF, 0xFFFF, 1)
IDCASE(0x10000, 0x10000, 0)
IDCASE(0x10001, 0x10001, 0)
IDCASE(0x20002, 0x20002, 0)
IDCASE(0x80000001u, 0x80000001u, 0)
IDCASE(all_ones, 0xFFFFFFFFu, 0)
XT(id_high_word_refused_with_history_and_frame) {
    NEED(NtUserGetPointerInfoList);
    for (int h = 0; h < 2; h++) for (int f = 0; f < 2; f++) { expect_refuse(0x10000, 1, h, f, 0x60, 1, 1, "id 0x10000"); expect_refuse(0, 1, h, f, 0x60, 1, 1, "id 0"); }
}

/* ---- counts ---- */
XT(zero_entries_refused) { NEED(NtUserGetPointerInfoList); expect_refuse(0x1234, 1, 0, 0, 0x60, 0, 1, "entries 0"); expect_refuse(0x1234, 1, 1, 0, 0x60, 0, 1, "entries 0 history"); }
XT(zero_pointers_refused) { NEED(NtUserGetPointerInfoList); expect_refuse(0x1234, 1, 0, 0, 0x60, 1, 0, "pointers 0"); expect_refuse(0x1234, 1, 0, 1, 0x60, 1, 0, "pointers 0 frame"); }
XT(both_zero_refused) { NEED(NtUserGetPointerInfoList); expect_refuse(0x1234, 1, 0, 0, 0x60, 0, 0, "0/0"); }
XT(small_counts_accepted) {
    NEED(NtUserGetPointerInfoList);
    static const UINT c[] = { 1, 2, 3, 4, 8, 10, 256, 1000, 0x10000 };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) { expect_accept(0x1234, 1, 1, 0, 0x60, c[i], 1, "entries"); expect_accept(0x1234, 1, 0, 1, 0x60, 1, c[i], "pointers"); expect_accept(0x1234, 1, 1, 1, 0x60, c[i], 2, "both"); }
}
XT(size_limit_kind1_boundary) {
    NEED(NtUserGetPointerInfoList);
    /* 0x2AAAAAA * 0x60 = 0xFFFFFFC0 fits in 32 bits, 0x2AAAAAB * 0x60 does not */
    expect_accept(0x1234, 1, 0, 0, 0x60, 0x2AAAAAA, 1, "0x2AAAAAA entries");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 0x2AAAAAB, 1, "0x2AAAAAB entries");
    expect_accept(0x1234, 1, 0, 0, 0x60, 1, 0x2AAAAAA, "0x2AAAAAA pointers");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 1, 0x2AAAAAB, "0x2AAAAAB pointers");
    expect_accept(0x1234, 1, 0, 0, 0x60, 2, 0x1555555, "2 * 0x1555555");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 2, 0x1555556, "2 * 0x1555556");
}
XT(size_limit_kind2_boundary) {
    NEED(NtUserGetPointerInfoList);
    /* 0x1C71C71 * 0x90 = 0xFFFFFF90, one more does not fit */
    expect_accept(0x1234, 2, 0, 0, 0x90, 0x1C71C71, 1, "0x1C71C71 entries");
    expect_refuse(0x1234, 2, 0, 0, 0x90, 0x1C71C72, 1, "0x1C71C72 entries");
}
XT(size_limit_kind3_boundary) {
    NEED(NtUserGetPointerInfoList);
    /* 0x2222222 * 0x78 = 0xFFFFFFF0, one more does not fit */
    expect_accept(0x1234, 3, 0, 0, 0x78, 0x2222222, 1, "0x2222222 entries");
    expect_refuse(0x1234, 3, 0, 0, 0x78, 0x2222223, 1, "0x2222223 entries");
}
XT(count_product_beyond_32_bits_refused) {
    NEED(NtUserGetPointerInfoList);
    expect_refuse(0x1234, 1, 0, 0, 0x60, 0x10000, 0x10000, "65536 * 65536");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 0xFFFFFFFFu, 2, "all ones * 2");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 0xFFFFFFFFu, 0xFFFFFFFFu, "all ones * all ones");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 0xFFFFFFFFu, 1, "all ones * 1");
    expect_refuse(0x1234, 1, 0, 0, 0x60, 0x80000000u, 2, "2^31 * 2");
    expect_refuse(0x1234, 1, 1, 1, 0x60, 0x10000, 0x10000, "65536 * 65536 history frame");
}

/* ---- pointers and output ---- */
XT(null_count_pointers_refused) {
    NEED(NtUserGetPointerInfoList);
    UINT a = 1, b = 1;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, NULL, &b, XP_BADPTR), XE_INVALID_PARAMETER);
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, &a, NULL, XP_BADPTR), XE_INVALID_PARAMETER);
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, NULL, NULL, XP_BADPTR), XE_INVALID_PARAMETER);
    char out[0x60]; memset(out, 0xCD, sizeof out);
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 1, 0, 0x60, NULL, &b, out), XE_INVALID_PARAMETER);
    CHECK(a == 1 && b == 1);
    for (unsigned i = 0; i < sizeof out; i++) if ((unsigned char)out[i] != 0xCD) { CHECKF(0, "output written at %u", i); break; }
}
XT(unwritable_count_pointers_are_noaccess) {
    NEED(NtUserGetPointerInfoList);
    UINT a = 1, b = 1; char out[0x60];
    const void *bad[] = { XP_BADPTR, XP_KERNELPTR, (void *)(ULONG_PTR)0xFFFFFFFFFFFFFFF0ULL };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, (UINT *)bad[i], &b, out), XE_NOACCESS);
        SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, &a, (UINT *)bad[i], out), XE_NOACCESS);
    }
}
XT(unwritable_output_is_noaccess) {
    NEED(NtUserGetPointerInfoList);
    UINT a = 1, b = 1;
    const void *bad[] = { XP_BADPTR, XP_KERNELPTR, (void *)(ULONG_PTR)0xFFFFFFFFFFFFFFF0ULL, (void *)(ULONG_PTR)0x7FFFFFFF0000ULL };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, &a, &b, (void *)bad[i]), XE_NOACCESS);
        SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 3, 1, 0, 0x78, &a, &b, (void *)bad[i]), XE_NOACCESS);
    }
    CHECK(a == 1 && b == 1);
}
XT(buffer_that_ends_one_page_early_is_noaccess) {
    NEED(NtUserGetPointerInfoList);
    /* the probed length is entries * pointers * cbEach: a one-record buffer at the end of a page is fine for one record,
     * two records run into the next, inaccessible page */
    SYSTEM_INFO si; GetSystemInfo(&si);
    char *base = VirtualAlloc(NULL, si.dwPageSize * 2, MEM_RESERVE, PAGE_NOACCESS);
    if (!base) { t_skip("no address space"); return; }
    if (!VirtualAlloc(base, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE)) { VirtualFree(base, 0, MEM_RELEASE); t_skip("no memory"); return; }
    char *end = base + si.dwPageSize - 0x60;
    UINT a = 1, b = 1;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, &a, &b, end), XE_INVALID_PARAMETER);   /* readable record: probe passes, the pointer is unknown */
    a = 2;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 1, 0, 0x60, &a, &b, end), XE_NOACCESS);             /* two records overrun the page */
    a = 1; b = 2;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 1, 0x60, &a, &b, end), XE_NOACCESS);
    VirtualFree(base, 0, MEM_RELEASE);
}
XT(null_output_needs_history_or_frame) {
    NEED(NtUserGetPointerInfoList);
    UINT a = 1, b = 1;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, &a, &b, NULL), XE_INVALID_PARAMETER);
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(1, 1, 0, 0, 0x60, &a, &b, NULL), XE_INVALID_PARAMETER);
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 2, 0, 0, 0x90, &a, &b, NULL), XE_INVALID_PARAMETER);
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 1, 0, 0x60, &a, &b, NULL), XE_INVALID_PARAMETER);   /* accepted, then the id is unknown */
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 1, 0x60, &a, &b, NULL), XE_INVALID_PARAMETER);
    CHECK(a == 1 && b == 1);
}

/* ---- ids that exist nowhere: refused with the buffer and counts untouched ---- */
static void unknown_untouched(ULONG kind, UINT cb, int hist, int frame) {
    NEED(NtUserGetPointerInfoList);
    unsigned char out[0x200]; memset(out, 0xCD, sizeof out);
    UINT a = 3, b = 2;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(0x1234, kind, hist, frame, cb, &a, &b, out), XE_INVALID_PARAMETER);
    CHECKF(a == 3 && b == 2, "counts changed to %u/%u", a, b);
    int clean = 1; for (unsigned i = 0; i < sizeof out; i++) if (out[i] != 0xCD) clean = 0;
    CHECK(clean);
}
XT(unknown_id_pointer_info_untouched)        { unknown_untouched(1, 0x60, 0, 0); }
XT(unknown_id_touch_info_untouched)          { unknown_untouched(2, 0x90, 0, 0); }
XT(unknown_id_pen_info_untouched)            { unknown_untouched(3, 0x78, 0, 0); }
XT(unknown_id_kind5_untouched)               { unknown_untouched(5, 0x90, 0, 0); }
XT(unknown_id_history_untouched)             { unknown_untouched(1, 0x60, 1, 0); }
XT(unknown_id_touch_history_untouched)       { unknown_untouched(2, 0x90, 1, 0); }
XT(unknown_id_pen_history_untouched)         { unknown_untouched(3, 0x78, 1, 0); }
XT(unknown_id_frame_untouched)               { unknown_untouched(1, 0x60, 0, 1); }
XT(unknown_id_touch_frame_untouched)         { unknown_untouched(2, 0x90, 0, 1); }
XT(unknown_id_pen_frame_untouched)           { unknown_untouched(3, 0x78, 0, 1); }
XT(unknown_id_frame_history_untouched)       { unknown_untouched(1, 0x60, 1, 1); }
XT(mouse_id_refused_while_mouse_in_pointer_is_off) {
    NEED(NtUserGetPointerInfoList);
    unsigned char out[0x60]; memset(out, 0xCD, sizeof out);
    UINT a = 1, b = 1;
    SET_ERR(0x4D2); XFAIL(UN(NtUserGetPointerInfoList)(1, 1, 0, 0, 0x60, &a, &b, out), XE_INVALID_PARAMETER);
    int clean = 1; for (unsigned i = 0; i < sizeof out; i++) if (out[i] != 0xCD) clean = 0;
    CHECK(clean); CHECK(a == 1 && b == 1);
}
XT(repeated_refusal_is_stable) {
    NEED(NtUserGetPointerInfoList);
    for (int i = 0; i < 50; i++) { expect_refuse(0x1234, 4, 0, 0, 0x60, 1, 1, "kind 4"); expect_accept(0x1234, 1, 0, 0, 0x60, 1, 1, "kind 1"); }
}

/* ---- the user32 front doors on ids that are not live pointers ---- */
typedef BOOL (WINAPI *front_fn)(UINT, void *, void *, void *);
static void front(const char *name, int arity, UINT cbElem) {
    front_fn f = (front_fn)t_proc_in("user32.dll", name);
    if (!f) { t_skip("user32!%s not exported", name); return; }
    if (t_is_wine()) { t_skip("emulator"); return; }
    static const UINT ids[] = { 0, 0x1234, 0xFFFF, 0x10000, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        unsigned char out[0x200]; UINT n1 = 1, n2 = 1; memset(out, 0xCD, sizeof out);
        SET_ERR(0x4D2);
        BOOL r = arity == 2 ? f(ids[i], out, NULL, NULL) : arity == 3 ? f(ids[i], &n1, out, NULL) : f(ids[i], &n1, &n2, out);
        DWORD gle = GetLastError();
        CHECKF(!r, "%s(%x) succeeded", name, ids[i]);
        CHECKF(gle == XE_INVALID_PARAMETER, "%s(%x): last error %lu", name, ids[i], gle);
        CHECKF(n1 == 1 && n2 == 1, "%s(%x): counts changed", name, ids[i]);
        int clean = 1; for (unsigned k = 0; k < cbElem && k < sizeof out; k++) if (out[k] != 0xCD) clean = 0;
        CHECKF(clean, "%s(%x): output written", name, ids[i]);
    }
}
#define FRONT(nm, ar, cb) XT(front_##nm) { front(#nm, ar, cb); }
FRONT(GetPointerInfo, 2, 0x60)
FRONT(GetPointerTouchInfo, 2, 0x90)
FRONT(GetPointerPenInfo, 2, 0x78)
FRONT(GetPointerFrameInfo, 3, 0x60)
FRONT(GetPointerFrameTouchInfo, 3, 0x90)
FRONT(GetPointerFramePenInfo, 3, 0x78)
FRONT(GetPointerInfoHistory, 3, 0x60)
FRONT(GetPointerTouchInfoHistory, 3, 0x90)
FRONT(GetPointerPenInfoHistory, 3, 0x78)
FRONT(GetPointerFrameInfoHistory, 4, 0x60)
FRONT(GetPointerFrameTouchInfoHistory, 4, 0x90)
FRONT(GetPointerFramePenInfoHistory, 4, 0x78)
