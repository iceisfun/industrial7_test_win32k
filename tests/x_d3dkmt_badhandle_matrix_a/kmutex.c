/* x_d3dkmt_badhandle_matrix_a / kmutex: keyed mutex lifecycle and argument matrix.
 *
 * Measures NtGdiDdDDICreateKeyedMutex(2), OpenKeyedMutex(2), OpenKeyedMutexFromNtHandle, AcquireKeyedMutex(2),
 * ReleaseKeyedMutex(2) and DestroyKeyedMutex. Keyed mutexes are the one object here that Windows creates
 * with no adapter, so the full lifecycle runs for real:
 *   - Create returns two distinct non-zero handles: a shared (global) handle and the per-process mutex handle;
 *     both are distinct across creations; the 64-bit initial value is accepted in full.
 *   - Acquire succeeds only for the current key (compared as a full 64-bit value); another key with a finite
 *     timeout (zero, relative, or an absolute time in the past) answers STATUS_TIMEOUT; acquiring while the
 *     calling thread already holds it, and releasing while not holding, are STATUS_INVALID_PARAMETER;
 *     Release sets the next key, any 64-bit value.
 *   - Open takes the shared handle (not the per-process one) and gives a new per-process handle to the same
 *     mutex; Destroy takes the per-process handle only, once.
 *   - Acquire2, Release2 and (with a buffer) Open2 require the private-data size to be exactly the size given at creation.
 *   - Every bad handle (garbage, stale, wrong kind, shared handle used as a mutex handle) is
 *     STATUS_INVALID_PARAMETER on these thunks, whereas OpenKeyedMutexFromNtHandle answers STATUS_INVALID_HANDLE for
 *     a closed or non-handle value and STATUS_OBJECT_TYPE_MISMATCH for a valid NT handle of another kind.
 * Never acquires with an infinite timeout unless the key is known to match.
 *
 * Determinism: handle values are never printed; only relations between them and status names. Every mutex
 * a test creates is destroyed by that test.
 */
#include "x.h"

#define NEED_KM() do { if (!x_nt("CreateKeyedMutex") || !x_nt("DestroyKeyedMutex")) { t_skip("keyed mutex thunks not available"); return; } } while (0)

static LONG km2(void *pd, DWORD sz, DWORD flags, ULONGLONG init, DWORD *hs, DWORD *hk) {
    x_fn f = x_nt("CreateKeyedMutex2");
    x_ckm2 *c = x_edge(sizeof *c);
    if (!f) return (LONG)0xC0000BAD;
    c->InitialValue = init; c->pPrivateRuntimeData = pd; c->PrivateRuntimeDataSize = sz; c->Flags = flags;
    LONG r = f(c);
    if (hs) *hs = c->hSharedHandle;
    if (hk) *hk = c->hKeyedMutex;
    return r;
}
static LONG open1(DWORD hs, DWORD *hk) {
    x_okm *o = x_edge(sizeof *o);
    o->hSharedHandle = hs;
    LONG r = x_nt("OpenKeyedMutex")(o);
    if (hk) *hk = o->hKeyedMutex;
    return r;
}

TEST(x_km_create_exact_size_blocks_succeed) {
    NEED_KM();
    x_ckm *c = x_edge(sizeof *c);
    X_ST(x_nt("CreateKeyedMutex")(c), XS_SUCCESS, "CreateKeyedMutex with a 16-byte block");
    if (c->hKeyedMutex) X_ST(x_km_destroy(c->hKeyedMutex), XS_SUCCESS, "destroy");
    XREQ(f2, "CreateKeyedMutex2");
    x_ckm2 *d = x_edge(sizeof *d);
    X_ST(f2(d), XS_SUCCESS, "CreateKeyedMutex2 with a 32-byte block");
    if (d->hKeyedMutex) X_ST(x_km_destroy(d->hKeyedMutex), XS_SUCCESS, "destroy");
}
TEST(x_km_create_zero_block_succeeds_and_returns_two_handles) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    X_ST(x_km_create(0, &hs, &hk), XS_SUCCESS, "create");
    CHECK(hs != 0); CHECK(hk != 0); CHECK(hs != hk);
    X_ST(x_km_destroy(hk), XS_SUCCESS, "destroy");
}
TEST(x_km_create_leaves_initial_value_and_input_fields) {
    NEED_KM();
    x_ckm c1; memset(&c1, 0, sizeof c1);
    c1.InitialValue = 0x1234567890ull;
    X_ST(x_nt("CreateKeyedMutex")(&c1), XS_SUCCESS, "create");
    CHECK(c1.InitialValue == 0x1234567890ull);
    x_km_destroy(c1.hKeyedMutex);
    x_fn f2 = x_nt("CreateKeyedMutex2");
    if (f2) {
        x_ckm2 c2; memset(&c2, 0, sizeof c2);
        c2.InitialValue = 0x1234567890ull;
        X_ST(f2(&c2), XS_SUCCESS, "create2");
        CHECK(c2.InitialValue == 0x1234567890ull);
        CHECK(c2.pPrivateRuntimeData == NULL && c2.PrivateRuntimeDataSize == 0 && c2.Flags == 0);
        x_km_destroy(c2.hKeyedMutex);
    }
}
TEST(x_km_create_sixteen_all_distinct) {
    NEED_KM();
    DWORD hs[16], hk[16]; int n = 0;
    for (; n < 16; n++) if (x_km_create(0, &hs[n], &hk[n]) != XS_SUCCESS) break;
    CHECK(n == 16);
    int dup = 0;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (hs[i] == hs[j] || hk[i] == hk[j] || hs[i] == hk[j] || hk[i] == hs[j]) dup++;
    CHECKF(dup == 0, "%d duplicate handle values among %d mutexes", dup, n);
    for (int i = 0; i < n; i++) X_ST(x_km_destroy(hk[i]), XS_SUCCESS, "destroy %d", i);
}
TEST(x_km_create_accepts_every_initial_value) {
    NEED_KM();
    static const ULONGLONG v[] = { 0, 1, 7, 0xFFFFFFFFull, 0x100000000ull, 0x100000001ull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull };
    for (size_t i = 0; i < XARR(v); i++) {
        DWORD hk = 0;
        X_ST(x_km_create(v[i], NULL, &hk), XS_SUCCESS, "initial value index %zu", i);
        if (hk) X_ST(x_km_destroy(hk), XS_SUCCESS, "destroy %zu", i);
    }
}

TEST(x_km_destroy_twice_second_invalid_parameter) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(0, NULL, &hk);
    X_ST(x_km_destroy(hk), XS_SUCCESS, "first destroy");
    X_ST(x_km_destroy(hk), XS_INVALID_PARAMETER, "second destroy");
}
/* A handle value names a table slot in its upper bits and ignores its low bits (at least the low two), and the shared handle comes from
 * another counter than the mutex handle: in a given process the two can differ by less than 4, in which case
 * the shared handle simply is another spelling of the mutex handle. Whether that happens depends on how many
 * D3DKMT objects the whole system has created, so the tests below assert the matching behaviour for either case. */
static int aliased(DWORD hs, DWORD hk) { return (hs & ~3u) == (hk & ~3u); }

TEST(x_km_destroy_shared_handle_rejected_mutex_survives) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    x_km_create(0, &hs, &hk);
    if (aliased(hs, hk)) {
        X_ST(x_km_destroy(hs), XS_SUCCESS, "destroy with the aliased shared handle destroys the mutex");
        X_ST(x_km_destroy(hk), XS_INVALID_PARAMETER, "mutex handle afterwards");
        return;
    }
    X_ST(x_km_destroy(hs), XS_INVALID_PARAMETER, "destroy with the shared handle");
    X_ST(x_km_acquire(hk, 0, 0, 1), XS_SUCCESS, "mutex still usable");
    x_km_release(hk, 0);
    X_ST(x_km_destroy(hk), XS_SUCCESS, "destroy with the mutex handle");
}
TEST(x_km_handle_low_bits_are_ignored) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    x_km_create(0, &hs, &hk);
    for (DWORD low = 1; low <= 3; low++) {
        X_ST(x_km_acquire(hk | low, 0, 0, 1), XS_SUCCESS, "acquire with the handle plus %lu", (unsigned long)low);
        X_ST(x_km_release(hk | low, 0), XS_SUCCESS, "release with the handle plus %lu", (unsigned long)low);
    }
    X_ST(x_km_destroy(hk | 3), XS_SUCCESS, "destroy with the handle plus 3");
    X_ST(x_km_destroy(hk), XS_INVALID_PARAMETER, "destroy with the exact handle afterwards");
}
TEST(x_km_destroy_garbage_handles_invalid_parameter) {
    NEED_KM();
    static const DWORD g[] = { 0, 1, 2, 4, 0x7FFFFFFF, 0x80000000, 0xC0000000, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < XARR(g); i++) X_ST(x_km_destroy(g[i]), XS_INVALID_PARAMETER, "destroy handle index %zu", i);
}
TEST(x_km_stale_handle_rejected_by_every_thunk) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    x_km_create(0, &hs, &hk);
    x_km_destroy(hk);
    X_ST(x_km_acquire(hk, 0, 0, 1), XS_INVALID_PARAMETER, "acquire stale");
    X_ST(x_km_release(hk, 0), XS_INVALID_PARAMETER, "release stale");
    X_ST(open1(hs, NULL), XS_INVALID_PARAMETER, "open by the shared handle of a destroyed mutex");
}
TEST(x_km_acquire_release_garbage_handles_invalid_parameter) {
    NEED_KM();
    static const DWORD g[] = { 0, 1, 4, 0x7FFFFFFF, 0x80000000, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < XARR(g); i++) {
        X_ST(x_km_acquire(g[i], 0, 0, 1), XS_INVALID_PARAMETER, "acquire handle index %zu", i);
        X_ST(x_km_release(g[i], 0), XS_INVALID_PARAMETER, "release handle index %zu", i);
    }
}
TEST(x_km_shared_handle_is_not_a_mutex_handle) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    x_km_create(0, &hs, &hk);
    if (aliased(hs, hk)) {
        X_ST(x_km_acquire(hs, 0, 0, 1), XS_SUCCESS, "acquire with the aliased shared handle");
        X_ST(x_km_release(hs, 0), XS_SUCCESS, "release with the aliased shared handle");
        x_km_destroy(hk);
        return;
    }
    X_ST(x_km_acquire(hs, 0, 0, 1), XS_INVALID_PARAMETER, "acquire with the shared handle");
    X_ST(x_km_release(hs, 0), XS_INVALID_PARAMETER, "release with the shared handle");
    x_km_destroy(hk);
}

TEST(x_km_acquire_current_key_infinite_timeout_succeeds) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(7, NULL, &hk);
    X_ST(x_km_acquire(hk, 7, 0, 0), XS_SUCCESS, "key 7 with no timeout pointer");
    x_km_release(hk, 7);
    x_km_destroy(hk);
}
TEST(x_km_acquire_other_key_times_out) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(7, NULL, &hk);
    X_ST(x_km_acquire(hk, 3, 0, 1), XS_TIMEOUT, "timeout 0");
    X_ST(x_km_acquire(hk, 3, -10000, 1), XS_TIMEOUT, "timeout 1 ms relative");
    X_ST(x_km_acquire(hk, 3, 1, 1), XS_TIMEOUT, "timeout absolute, in the past");
    X_ST(x_km_acquire(hk, 8, 0, 1), XS_TIMEOUT, "key above the current one");
    X_ST(x_km_acquire(hk, 0, 0, 1), XS_TIMEOUT, "key 0");
    X_ST(x_km_acquire(hk, 7, 0, 1), XS_SUCCESS, "the mutex was never taken by the failed attempts");
    x_km_release(hk, 7);
    x_km_destroy(hk);
}
TEST(x_km_acquire_key_is_compared_as_64_bit) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(0x100000001ull, NULL, &hk);
    X_ST(x_km_acquire(hk, 1, 0, 1), XS_TIMEOUT, "low half only");
    X_ST(x_km_acquire(hk, 0x100000000ull, 0, 1), XS_TIMEOUT, "high half only");
    X_ST(x_km_acquire(hk, 0xFFFFFFFF00000001ull, 0, 1), XS_TIMEOUT, "different high half");
    X_ST(x_km_acquire(hk, 0x100000001ull, 0, 1), XS_SUCCESS, "full value");
    x_km_release(hk, 0x100000001ull);
    x_km_destroy(hk);
}
TEST(x_km_acquire_while_held_invalid_parameter) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(5, NULL, &hk);
    X_ST(x_km_acquire(hk, 5, 0, 1), XS_SUCCESS, "first acquire");
    X_ST(x_km_acquire(hk, 5, 0, 1), XS_INVALID_PARAMETER, "second acquire, same key");
    X_ST(x_km_acquire(hk, 6, 0, 1), XS_INVALID_PARAMETER, "second acquire, other key");
    X_ST(x_km_release(hk, 5), XS_SUCCESS, "release");
    X_ST(x_km_destroy(hk), XS_SUCCESS, "destroy");
}
TEST(x_km_release_without_acquire_invalid_parameter) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(5, NULL, &hk);
    X_ST(x_km_release(hk, 5), XS_INVALID_PARAMETER, "release a mutex nobody holds");
    X_ST(x_km_release(hk, 6), XS_INVALID_PARAMETER, "release with another key");
    x_km_destroy(hk);
}
TEST(x_km_release_sets_the_next_key) {
    NEED_KM();
    DWORD hk = 0;
    x_km_create(7, NULL, &hk);
    X_ST(x_km_acquire(hk, 7, 0, 1), XS_SUCCESS, "acquire 7");
    X_ST(x_km_release(hk, 9), XS_SUCCESS, "release to key 9");
    X_ST(x_km_acquire(hk, 7, 0, 1), XS_TIMEOUT, "old key no longer matches");
    X_ST(x_km_acquire(hk, 9, 0, 1), XS_SUCCESS, "new key matches");
    X_ST(x_km_release(hk, 7), XS_SUCCESS, "release back to key 7");
    X_ST(x_km_acquire(hk, 7, 0, 1), XS_SUCCESS, "key 7 again");
    x_km_release(hk, 7);
    x_km_destroy(hk);
}
TEST(x_km_release_accepts_every_next_key) {
    NEED_KM();
    static const ULONGLONG k[] = { 0, 1, 0xFFFFFFFFull, 0x100000000ull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull };
    DWORD hk = 0;
    x_km_create(0, NULL, &hk);
    ULONGLONG cur = 0;
    for (size_t i = 0; i < XARR(k); i++) {
        X_ST(x_km_acquire(hk, cur, 0, 1), XS_SUCCESS, "acquire step %zu", i);
        X_ST(x_km_release(hk, k[i]), XS_SUCCESS, "release to next key step %zu", i);
        cur = k[i];
    }
    x_km_destroy(hk);
}

TEST(x_km_open_by_shared_handle_gives_second_handle) {
    NEED_KM();
    DWORD hs = 0, hk = 0, ok = 0;
    x_km_create(2, &hs, &hk);
    X_ST(open1(hs, &ok), XS_SUCCESS, "open");
    CHECK(ok != 0); CHECK(ok != hk); CHECK(ok != hs);
    X_ST(x_km_destroy(ok), XS_SUCCESS, "destroy opened");
    X_ST(x_km_destroy(ok), XS_INVALID_PARAMETER, "destroy opened twice");
    X_ST(x_km_acquire(hk, 2, 0, 1), XS_SUCCESS, "creator handle unaffected");
    x_km_release(hk, 2);
    x_km_destroy(hk);
}
TEST(x_km_open_shares_one_object) {
    NEED_KM();
    DWORD hs = 0, hk = 0, ok = 0;
    x_km_create(2, &hs, &hk);
    open1(hs, &ok);
    X_ST(x_km_acquire(ok, 2, 0, 1), XS_SUCCESS, "acquire through the opened handle");
    X_ST(x_km_acquire(hk, 2, 0, 1), XS_TIMEOUT, "creator handle: the shared mutex is held");
    X_ST(x_km_release(ok, 4), XS_SUCCESS, "release through the opened handle");
    X_ST(x_km_acquire(hk, 4, 0, 1), XS_SUCCESS, "creator handle sees the next key");
    x_km_release(hk, 4);
    x_km_destroy(ok);
    x_km_destroy(hk);
}
TEST(x_km_open_survives_creator_destroy) {
    NEED_KM();
    DWORD hs = 0, hk = 0, ok = 0;
    x_km_create(2, &hs, &hk);
    open1(hs, &ok);
    X_ST(x_km_destroy(hk), XS_SUCCESS, "destroy creator handle");
    X_ST(x_km_acquire(ok, 2, 0, 1), XS_SUCCESS, "opened handle still works");
    x_km_release(ok, 2);
    X_ST(x_km_destroy(ok), XS_SUCCESS, "destroy opened handle");
    X_ST(open1(hs, NULL), XS_INVALID_PARAMETER, "shared handle dead after the last handle went");
}
TEST(x_km_open_rejects_bad_shared_handles) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    x_km_create(0, &hs, &hk);
    static const DWORD g[] = { 0, 1, 4, 0x7FFFFFFF, 0x80000000, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < XARR(g); i++) X_ST(open1(g[i], NULL), XS_INVALID_PARAMETER, "open handle index %zu", i);
    X_ST(open1(hk, NULL), XS_INVALID_PARAMETER, "open with the per-process handle");
    x_km_destroy(hk);
}
TEST(x_km_open_does_not_write_handle_on_failure) {
    NEED_KM();
    x_okm *o = x_edge(sizeof *o);
    o->hSharedHandle = 0xDEADBEEF; o->hKeyedMutex = 0x5A5A5A5A;
    X_ST(x_nt("OpenKeyedMutex")(o), XS_INVALID_PARAMETER, "open");
    CHECK(o->hKeyedMutex == 0x5A5A5A5A);
}

TEST(x_km_create2_flags_matrix) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    DWORD hk = 0;
    X_ST(km2(NULL, 0, 0, 0, NULL, &hk), XS_SUCCESS, "flags 0");
    if (hk) x_km_destroy(hk);
    hk = 0;
    X_ST(km2(NULL, 0, 1, 0, NULL, &hk), XS_SUCCESS, "flags 1");
    if (hk) x_km_destroy(hk);
    for (unsigned b = 1; b < 32; b++) {
        hk = 0;
        LONG r = km2(NULL, 0, 1u << b, 0, NULL, &hk);
        X_ST(r, XS_INVALID_PARAMETER, "flag bit %u", b);
        if (r == XS_SUCCESS && hk) x_km_destroy(hk);
    }
    hk = 0;
    LONG r = km2(NULL, 0, 0xFFFFFFFF, 0, NULL, &hk);
    X_ST(r, XS_INVALID_PARAMETER, "all flag bits");
    if (r == XS_SUCCESS && hk) x_km_destroy(hk);
}
TEST(x_km_create2_nt_security_sharing_has_no_global_handle) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    DWORD hs = 0xFFFFFFFF, hk = 0;
    X_ST(km2(NULL, 0, 1, 0, &hs, &hk), XS_SUCCESS, "create with the NT-security-sharing flag");
    CHECK(hk != 0);
    CHECKF(hs == 0, "global handle is zero when the mutex is shared by NT handle");
    X_ST(x_km_acquire(hk, 0, 0, 1), XS_SUCCESS, "still a working mutex");
    x_km_release(hk, 0);
    x_km_destroy(hk);
}
TEST(x_km_create2_private_data_matrix) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    unsigned char pd[32]; memset(pd, 0x5A, sizeof pd);
    DWORD hk;
    struct { void *p; DWORD sz; LONG want; const char *what; } t[] = {
        { NULL, 0, XS_SUCCESS, "no data" },
        { pd, 16, XS_SUCCESS, "16 bytes" },
        { pd, 32, XS_SUCCESS, "32 bytes" },
        { pd, 1, XS_SUCCESS, "1 byte" },
        { NULL, 16, XS_INVALID_PARAMETER, "size without pointer" },
        { pd, 0, XS_INVALID_PARAMETER, "pointer without size" },
        { pd, 0x100000, XS_INVALID_PARAMETER, "size beyond the allowed maximum" },
        { (void *)1, 16, XS_INVALID_PARAMETER, "unreadable pointer" },
    };
    for (size_t i = 0; i < XARR(t); i++) {
        hk = 0;
        LONG r = km2(t[i].p, t[i].sz, 0, 0, NULL, &hk);
        X_ST(r, t[i].want, "private data: %s", t[i].what);
        if (r == XS_SUCCESS && hk) x_km_destroy(hk);
    }
}
TEST(x_km_acquire2_private_data_size_must_match_creation) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    XREQ(a2, "AcquireKeyedMutex2");
    XREQ(r2, "ReleaseKeyedMutex2");
    unsigned char pd[16], out[64];
    for (int i = 0; i < 16; i++) pd[i] = (unsigned char)(i + 1);
    DWORD hk = 0;
    X_ST(km2(pd, 16, 0, 3, NULL, &hk), XS_SUCCESS, "create with 16 bytes");
    static const DWORD bad[] = { 0, 1, 8, 15, 17, 32 };
    for (size_t i = 0; i < XARR(bad); i++) {
        LARGE_INTEGER z; z.QuadPart = 0;
        x_akm2 a; memset(&a, 0, sizeof a);
        a.hKeyedMutex = hk; a.Key = 3; a.pTimeout = &z; a.pPrivateRuntimeData = out; a.PrivateRuntimeDataSize = bad[i];
        X_ST(a2(&a), XS_INVALID_PARAMETER, "acquire2 with size index %zu", i);
    }
    LARGE_INTEGER z; z.QuadPart = 0;
    x_akm2 a; memset(&a, 0, sizeof a); memset(out, 0xEE, sizeof out);
    a.hKeyedMutex = hk; a.Key = 3; a.pTimeout = &z; a.pPrivateRuntimeData = out; a.PrivateRuntimeDataSize = 16;
    X_ST(a2(&a), XS_SUCCESS, "acquire2 with the creation size");
    CHECK(memcmp(out, pd, 16) == 0);
    CHECK(out[16] == 0xEE);
    for (size_t i = 0; i < XARR(bad); i++) {
        x_rkm2 r; memset(&r, 0, sizeof r);
        r.hKeyedMutex = hk; r.Key = 3; r.pPrivateRuntimeData = pd; r.PrivateRuntimeDataSize = bad[i];
        X_ST(r2(&r), XS_INVALID_PARAMETER, "release2 with size index %zu", i);
    }
    x_rkm2 r; memset(&r, 0, sizeof r);
    r.hKeyedMutex = hk; r.Key = 3; r.pPrivateRuntimeData = pd; r.PrivateRuntimeDataSize = 16;
    X_ST(r2(&r), XS_SUCCESS, "release2 with the creation size");
    x_km_destroy(hk);
}
TEST(x_km_acquire2_on_mutex_without_private_data) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    XREQ(a2, "AcquireKeyedMutex2");
    XREQ(r2, "ReleaseKeyedMutex2");
    DWORD hk = 0; unsigned char buf[16] = {0};
    km2(NULL, 0, 0, 0, NULL, &hk);
    LARGE_INTEGER z; z.QuadPart = 0;
    x_akm2 a; memset(&a, 0, sizeof a);
    a.hKeyedMutex = hk; a.pTimeout = &z; a.pPrivateRuntimeData = buf; a.PrivateRuntimeDataSize = 16;
    X_ST(a2(&a), XS_INVALID_PARAMETER, "acquire2 asking for data the mutex does not have");
    a.pPrivateRuntimeData = NULL; a.PrivateRuntimeDataSize = 0;
    X_ST(a2(&a), XS_SUCCESS, "acquire2 without data");
    x_rkm2 r; memset(&r, 0, sizeof r);
    r.hKeyedMutex = hk;
    X_ST(r2(&r), XS_SUCCESS, "release2 without data");
    x_km_destroy(hk);
}
TEST(x_km_open2_private_data_size_must_match_creation) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    XREQ(o2, "OpenKeyedMutex2");
    unsigned char pd[16] = {1,2,3}, out[16];
    DWORD hs = 0, hk = 0;
    km2(pd, 16, 0, 0, &hs, &hk);
    static const DWORD bad[] = { 0, 1, 8, 15, 17, 24, 32 };
    for (size_t i = 0; i < XARR(bad); i++) {
        x_okm2 *o = x_edge(sizeof *o);
        o->hSharedHandle = hs; o->pPrivateRuntimeData = out; o->PrivateRuntimeDataSize = bad[i];
        X_ST(o2(o), XS_INVALID_PARAMETER, "open2 with size index %zu", i);
    }
    x_okm2 *o = x_edge(sizeof *o);
    o->hSharedHandle = hs; o->pPrivateRuntimeData = out; o->PrivateRuntimeDataSize = 16;
    X_ST(o2(o), XS_SUCCESS, "open2 with the creation size");
    CHECK(o->hKeyedMutex != 0 && o->hKeyedMutex != hk);
    x_km_destroy(o->hKeyedMutex);
    o = x_edge(sizeof *o);
    o->hSharedHandle = hs; o->pPrivateRuntimeData = NULL; o->PrivateRuntimeDataSize = 16;
    X_ST(o2(o), XS_SUCCESS, "open2 with the creation size and no buffer: the buffer is not required to be present");
    x_km_destroy(o->hKeyedMutex);
    x_km_destroy(hk);
}
TEST(x_km_open2_on_mutex_without_private_data) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    XREQ(o2, "OpenKeyedMutex2");
    unsigned char out[16];
    DWORD hs = 0, hk = 0;
    km2(NULL, 0, 0, 0, &hs, &hk);
    x_okm2 *o = x_edge(sizeof *o);
    o->hSharedHandle = hs;
    X_ST(o2(o), XS_SUCCESS, "open2 without data");
    x_km_destroy(o->hKeyedMutex);
    o = x_edge(sizeof *o);
    o->hSharedHandle = hs; o->pPrivateRuntimeData = out; o->PrivateRuntimeDataSize = 16;
    X_ST(o2(o), XS_SUCCESS, "open2 with a buffer and size on a mutex that has no data: not compared with the creation size");
    x_km_destroy(o->hKeyedMutex);
    o = x_edge(sizeof *o);
    o->hSharedHandle = hs; o->pPrivateRuntimeData = NULL; o->PrivateRuntimeDataSize = 16;
    X_ST(o2(o), XS_SUCCESS, "open2 with a size and no buffer");
    x_km_destroy(o->hKeyedMutex);
    o = x_edge(sizeof *o);
    o->hSharedHandle = hs; o->pPrivateRuntimeData = out; o->PrivateRuntimeDataSize = 0;
    X_ST(o2(o), XS_INVALID_PARAMETER, "open2 with a buffer and no size");
    x_km_destroy(hk);
}
TEST(x_km_open_plain_on_private_data_mutex) {
    NEED_KM();
    XREQ(f2, "CreateKeyedMutex2");
    unsigned char pd[16] = {9};
    DWORD hs = 0, hk = 0, ok = 0;
    km2(pd, 16, 0, 0, &hs, &hk);
    X_ST(open1(hs, &ok), XS_SUCCESS, "OpenKeyedMutex on a mutex created with private data");
    if (ok) x_km_destroy(ok);
    x_km_destroy(hk);
}

TEST(x_km_open_from_nt_handle_by_handle_kind) {
    NEED_KM();
    XREQ(fn, "OpenKeyedMutexFromNtHandle");
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE mx = CreateMutexA(NULL, FALSE, NULL);
    HANDLE sec = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
    struct { HANDLE h; LONG want; const char *what; } t[] = {
        { ev, XS_TYPE_MISMATCH, "event" },
        { mx, XS_TYPE_MISMATCH, "mutex" },
        { sec, XS_TYPE_MISMATCH, "section" },
        { GetCurrentProcess(), XS_TYPE_MISMATCH, "current process pseudo handle" },
        { GetCurrentThread(), XS_TYPE_MISMATCH, "current thread pseudo handle" },
        { INVALID_HANDLE_VALUE, XS_TYPE_MISMATCH, "-1" },
        { (HANDLE)(ULONG_PTR)0x1234567C, XS_INVALID_HANDLE, "closed or never-opened value" },
        { NULL, XS_INVALID_HANDLE, "NULL" },
        { (HANDLE)(ULONG_PTR)0xFFFFFFFFFFFFFFF0ull, XS_INVALID_HANDLE, "high garbage" },
    };
    for (size_t i = 0; i < XARR(t); i++) {
        x_okmnt *o = x_edge(sizeof *o);
        o->hNtHandle = t[i].h; o->hKeyedMutex = 0x5A5A5A5A;
        X_ST(fn(o), t[i].want, "FromNtHandle(%s)", t[i].what);
        CHECKF(o->hKeyedMutex == 0x5A5A5A5A, "output untouched for %s", t[i].what);
    }
    CloseHandle(ev); CloseHandle(mx); CloseHandle(sec);
}
TEST(x_km_open_from_nt_handle_closed_event_is_invalid_handle) {
    NEED_KM();
    XREQ(fn, "OpenKeyedMutexFromNtHandle");
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    CloseHandle(ev);
    x_okmnt *o = x_edge(sizeof *o);
    o->hNtHandle = ev;
    X_ST(fn(o), XS_INVALID_HANDLE, "closed event handle");
}
TEST(x_km_open_from_nt_handle_private_data_arguments) {
    NEED_KM();
    XREQ(fn, "OpenKeyedMutexFromNtHandle");
    unsigned char buf[16];
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    x_okmnt *o = x_edge(sizeof *o);
    /* the handle is examined before the private data arguments */
    o->hNtHandle = ev; o->pPrivateRuntimeData = (void *)1; o->PrivateRuntimeDataSize = 0xFFFFFFFF;
    X_ST(fn(o), XS_TYPE_MISMATCH, "valid event handle, garbage data arguments");
    o = x_edge(sizeof *o);
    o->hNtHandle = (HANDLE)(ULONG_PTR)0x1234567C; o->pPrivateRuntimeData = buf; o->PrivateRuntimeDataSize = 16;
    X_ST(fn(o), XS_INVALID_HANDLE, "bogus handle, valid data arguments");
    CloseHandle(ev);
}

struct hold_ctx { DWORD hk; ULONGLONG key; LONG r_zero, r_after; HANDLE go, done; };
static DWORD WINAPI other_thread(void *p) {
    struct hold_ctx *c = p;
    c->r_zero = x_km_acquire(c->hk, c->key, 0, 1);
    SetEvent(c->done);
    WaitForSingleObject(c->go, 10000);
    c->r_after = x_km_acquire(c->hk, c->key + 1, 0, 1);
    if (c->r_after == XS_SUCCESS) x_km_release(c->hk, c->key + 1);
    SetEvent(c->done);
    return 0;
}
TEST(x_km_second_thread_sees_mutex_held_then_released) {
    NEED_KM();
    DWORD hs = 0, hk = 0;
    x_km_create(10, &hs, &hk);
    X_ST(x_km_acquire(hk, 10, 0, 1), XS_SUCCESS, "main thread acquires");
    struct hold_ctx c = { hk, 10, 0x7FFF0000, 0x7FFF0000, CreateEventA(NULL, FALSE, FALSE, NULL), CreateEventA(NULL, FALSE, FALSE, NULL) };
    HANDLE th = CreateThread(NULL, 0, other_thread, &c, 0, NULL);
    CHECK(WaitForSingleObject(c.done, 10000) == WAIT_OBJECT_0);
    X_ST(c.r_zero, XS_INVALID_PARAMETER, "second thread asks for the key while the first thread holds the mutex");
    X_ST(x_km_release(hk, 11), XS_SUCCESS, "main thread releases to key 11");
    SetEvent(c.go);
    CHECK(WaitForSingleObject(c.done, 10000) == WAIT_OBJECT_0);
    X_ST(c.r_after, XS_SUCCESS, "second thread acquires the next key once the first released");
    WaitForSingleObject(th, 10000);
    CloseHandle(th); CloseHandle(c.go); CloseHandle(c.done);
    X_ST(x_km_destroy(hk), XS_SUCCESS, "destroy");
}
