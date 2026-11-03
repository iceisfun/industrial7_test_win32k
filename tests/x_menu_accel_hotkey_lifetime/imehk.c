/* x_menu_accel_hotkey_lifetime / imehk: NtUserSetImeHotKey and NtUserGetImeHotKey seen across threads and processes.
 *
 * The IME hotkey list is one table for the session, not owned by the thread or process that edits it: an entry set anywhere is
 * visible everywhere, survives the end of whoever set it, and can be changed or deleted by anyone. Actions other than 1 (delete)
 * and 2 (set) do nothing and answer FALSE without setting a last error; deleting an absent id is ERROR_INVALID_PARAMETER (87);
 * reading an absent id is ERROR_HOTKEY_NOT_REGISTERED (1419); a second id on a key some other id holds is
 * ERROR_HOTKEY_ALREADY_REGISTERED (1409). The reset action (3) is deliberately never used here: it restores the whole table.
 *
 * Ids are private ones far above the system's; every entry is deleted before a test ends.
 * Determinism: only symbolic results.
 */
#include "x.h"

#define PRIV 0x7ff20000u
#define SETK(id, m, v) UN(NtUserSetImeHotKey)((id), (m), (v), NULL, 2)
#define DELK(id)       UN(NtUserSetImeHotKey)((id), 0, 0, NULL, 1)
static BOOL getk(DWORD id, UINT *m, UINT *v) { return UN(NtUserGetImeHotKey)(id, m, v, NULL); }

/* ---- this thread ---- */
XT(set_get_delete_round_trip) {
    NEED_IHK();
    XOK(SETK(PRIV + 1, 0x3, 0x7D), 1);
    UINT m = 0, v = 0; XOK(getk(PRIV + 1, &m, &v), 1);
    EXPECT_EQ(m, 3); EXPECT_EQ(v, 0x7D);
    XOK(DELK(PRIV + 1), 1);
    XFAIL(getk(PRIV + 1, &m, &v), XE_HOTKEY_NOT_REGISTERED);
}
XT(delete_of_an_absent_id_is_invalid_parameter) {
    NEED_IHK();
    XFAIL(DELK(PRIV + 2), XE_INVALID_PARAMETER);
    XFAIL(DELK(0x7fffffff), XE_INVALID_PARAMETER);
}
XT(delete_twice_second_is_invalid_parameter) {
    NEED_IHK();
    SETK(PRIV + 3, 0x1, 0x7C);
    XOK(DELK(PRIV + 3), 1);
    XFAIL(DELK(PRIV + 3), XE_INVALID_PARAMETER);
}
static void bad_action(DWORD act) {
    NEED_IHK();
    SET_ERR(XE_SENT);
    BOOL r = UN(NtUserSetImeHotKey)(PRIV + 4, 0x3, 0x7E, NULL, act);
    CHECKF(r == 0, "action 0x%lx returned %d", act, r);
    EXPECT_ERR(XE_SENT);
    UINT m, v; XFAIL(getk(PRIV + 4, &m, &v), XE_HOTKEY_NOT_REGISTERED);   /* and created nothing */
}
#define BA(n, a) XT(unknown_action_##n##_does_nothing) { bad_action(a); }
BA(0, 0) BA(4, 4) BA(5, 5) BA(0x10, 0x10) BA(0x7fffffff, 0x7fffffff) BA(0xffffffff, 0xffffffff)
XT(update_in_place_keeps_one_entry) {
    NEED_IHK();
    SETK(PRIV + 5, 0x1, 0x7C); SETK(PRIV + 5, 0x2, 0x7D);
    UINT m = 0, v = 0; XOK(getk(PRIV + 5, &m, &v), 1);
    EXPECT_EQ(m, 2); EXPECT_EQ(v, 0x7D);
    XOK(DELK(PRIV + 5), 1);
    XFAIL(DELK(PRIV + 5), XE_INVALID_PARAMETER);
}
XT(same_key_on_a_second_id_clashes_then_succeeds_after_delete) {
    NEED_IHK();
    SETK(PRIV + 6, 0x3, 0x7F);
    XFAIL(SETK(PRIV + 7, 0x3, 0x7F), XE_HOTKEY_ALREADY_REGISTERED);
    DELK(PRIV + 6);
    XOK(SETK(PRIV + 7, 0x3, 0x7F), 1);
    DELK(PRIV + 7);
}
XT(private_ids_with_low_bits_clear_are_ordinary_ids) {
    /* ids are private ones: small ids belong to the input languages installed and are never touched here */
    NEED_IHK();
    DWORD ids[] = { PRIV + 0x400, PRIV + 0x410, PRIV + 0x430, PRIV + 0x48f };
    for (unsigned i = 0; i < 4; i++) {
        UINT m = 0, v = 0;
        BOOL s = SETK(ids[i], 0x3, 0x7D + i);
        BOOL g = getk(ids[i], &m, &v);
        CHECKF(s && g && m == 3 && v == 0x7D + i, "id %u: set %d get %d mods %u vk 0x%x", i, s, g, m, v);
        DELK(ids[i]);
    }
}
XT(eighty_ids_set_read_and_delete) {
    NEED_IHK();
    enum { N = 80 };
    int setbad = 0, getbad = 0, delbad = 0, leftbad = 0;
    /* (mods 1..7, vk F13..F24) repeats every 84 ids: all 80 key pairs are distinct, so every set must succeed */
    for (int i = 0; i < N; i++) if (SETK(PRIV + 0x100 + i, 1 + i % 7, 0x7C + i % 12) != 1) setbad++;
    for (int i = 0; i < N; i++) { UINT m = 0, v = 0; if (!getk(PRIV + 0x100 + i, &m, &v) || m != (UINT)(1 + i % 7) || v != (UINT)(0x7C + i % 12)) getbad++; }
    for (int i = 0; i < N; i++) if (DELK(PRIV + 0x100 + i) != 1) delbad++;
    for (int i = 0; i < N; i++) { UINT m, v; if (getk(PRIV + 0x100 + i, &m, &v)) leftbad++; }
    EXPECT_EQ(setbad, 0); EXPECT_EQ(getbad, 0); EXPECT_EQ(delbad, 0); EXPECT_EQ(leftbad, 0);
}

/* ---- other threads ---- */
struct ih { DWORD id; UINT m, v; BOOL get, set, del; DWORD gle; int op; };
static DWORD WINAPI ih_thread(LPVOID p) {
    struct ih *h = p;
    t_module("user32.dll");
    SET_ERR(XE_SENT);
    switch (h->op) {
    case 1: h->get = getk(h->id, &h->m, &h->v); break;
    case 2: h->set = SETK(h->id, h->m, h->v); break;
    case 3: h->del = DELK(h->id); break;
    }
    h->gle = GetLastError();
    return 0;
}
static void on_thread(struct ih *h) { HANDLE t = CreateThread(NULL, 0, ih_thread, h, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t); }
XT(entry_set_here_is_read_on_another_thread) {
    NEED_IHK();
    SETK(PRIV + 0x200, 0x3, 0x7D);
    struct ih h = { PRIV + 0x200, 0, 0, 0, 0, 0, 0, 1 }; on_thread(&h);
    EXPECT_EQ(h.get, 1); EXPECT_EQ(h.m, 3); EXPECT_EQ(h.v, 0x7D);
    DELK(PRIV + 0x200);
}
XT(entry_set_on_a_thread_outlives_that_thread) {
    NEED_IHK();
    struct ih h = { PRIV + 0x201, 0x3, 0x7E, 0, 0, 0, 0, 2 }; on_thread(&h);
    EXPECT_EQ(h.set, 1);
    UINT m = 0, v = 0; XOK(getk(PRIV + 0x201, &m, &v), 1);
    EXPECT_EQ(m, 3); EXPECT_EQ(v, 0x7E);
    DELK(PRIV + 0x201);
}
XT(entry_set_here_is_deleted_by_another_thread) {
    NEED_IHK();
    SETK(PRIV + 0x202, 0x3, 0x7C);
    struct ih h = { PRIV + 0x202, 0, 0, 0, 0, 0, 0, 3 }; on_thread(&h);
    EXPECT_EQ(h.del, 1);
    UINT m, v; XFAIL(getk(PRIV + 0x202, &m, &v), XE_HOTKEY_NOT_REGISTERED);
}
XT(another_thread_sees_a_clash_with_our_key) {
    NEED_IHK();
    SETK(PRIV + 0x203, 0x3, 0x7F);
    struct ih h = { PRIV + 0x204, 0x3, 0x7F, 0, 0, 0, 0, 2 }; on_thread(&h);
    EXPECT_EQ(h.set, 0); EXPECT_EQ(h.gle, XE_HOTKEY_ALREADY_REGISTERED);
    DELK(PRIV + 0x203);
}
XT(last_writer_wins_between_threads) {
    NEED_IHK();
    SETK(PRIV + 0x205, 0x1, 0x7C);
    struct ih h = { PRIV + 0x205, 0x2, 0x7D, 0, 0, 0, 0, 2 }; on_thread(&h);
    UINT m = 0, v = 0; XOK(getk(PRIV + 0x205, &m, &v), 1);
    EXPECT_EQ(m, 2); EXPECT_EQ(v, 0x7D);
    DELK(PRIV + 0x205);
}
XT(unknown_action_on_another_thread_does_nothing_either) {
    NEED_IHK();
    struct ih h = { 0, 0, 0, 0, 0, 0, 0, 0 };
    SETK(PRIV + 0x206, 0x3, 0x7E);
    HANDLE t = CreateThread(NULL, 0, ih_thread, &h, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    UINT m, v; XOK(getk(PRIV + 0x206, &m, &v), 1);   /* entry untouched */
    DELK(PRIV + 0x206);
}

/* ---- other processes ---- */
/* Child: op 1 get, 2 set, 3 delete on the id; publishes the result and the values. */
CHILD(xm_ime_child) {
    HANDLE m; struct xm_shm *s = xm_shm_open(argv[1], &m); if (!s) return 1;
    t_module("user32.dll");
    int op = atoi(argv[2]); DWORD id = strtoul(argv[3], 0, 16); UINT mods = atoi(argv[4]), vk = atoi(argv[5]);
    SET_ERR(XE_SENT);
    BOOL r = 0; UINT gm = 0, gv = 0;
    if (op == 1) r = UN(NtUserGetImeHotKey)(id, &gm, &gv, NULL);
    else if (op == 2) r = UN(NtUserSetImeHotKey)(id, mods, vk, NULL, 2);
    else if (op == 3) r = UN(NtUserSetImeHotKey)(id, 0, 0, NULL, 1);
    s->v[0] = r; s->v[1] = gm; s->v[2] = gv; s->e[0] = GetLastError();
    return 0;
}
static int child_op(int op, DWORD id, UINT m, UINT v, struct xm_shm **out, HANDLE *sm) {
    const char *tag = xm_tag(); char ex[96];
    *out = xm_shm_create(tag, sm);
    snprintf(ex, sizeof ex, "%d %lx %u %u", op, id, m, v);
    HANDLE p = xm_spawn("xm_ime_child", tag, ex, NULL);
    if (!p) return 0;
    DWORD ec = xm_reap(p, NULL, 20000);
    return ec == 0;
}
XT(entry_set_here_is_read_by_another_process) {
    NEED_IHK();
    SETK(PRIV + 0x300, 0x3, 0x7D);
    struct xm_shm *s; HANDLE sm;
    CHECK(child_op(1, PRIV + 0x300, 0, 0, &s, &sm));
    EXPECT_EQ(s->v[0], 1); EXPECT_EQ(s->v[1], 3); EXPECT_EQ(s->v[2], 0x7D);
    DELK(PRIV + 0x300);
}
XT(entry_set_by_a_process_outlives_it) {
    NEED_IHK();
    struct xm_shm *s; HANDLE sm;
    CHECK(child_op(2, PRIV + 0x301, 3, 0x7E, &s, &sm));
    EXPECT_EQ(s->v[0], 1);
    UINT m = 0, v = 0; XOK(getk(PRIV + 0x301, &m, &v), 1);
    EXPECT_EQ(m, 3); EXPECT_EQ(v, 0x7E);
    DELK(PRIV + 0x301);
}
XT(entry_set_here_is_deleted_by_another_process) {
    NEED_IHK();
    SETK(PRIV + 0x302, 0x3, 0x7C);
    struct xm_shm *s; HANDLE sm;
    CHECK(child_op(3, PRIV + 0x302, 0, 0, &s, &sm));
    EXPECT_EQ(s->v[0], 1);
    UINT m, v; XFAIL(getk(PRIV + 0x302, &m, &v), XE_HOTKEY_NOT_REGISTERED);
}
XT(another_process_sees_a_clash_with_our_key) {
    NEED_IHK();
    SETK(PRIV + 0x303, 0x3, 0x7F);
    struct xm_shm *s; HANDLE sm;
    CHECK(child_op(2, PRIV + 0x304, 3, 0x7F, &s, &sm));
    EXPECT_EQ(s->v[0], 0); EXPECT_EQ(s->e[0], XE_HOTKEY_ALREADY_REGISTERED);
    DELK(PRIV + 0x303);
}
XT(another_process_cannot_read_what_was_deleted) {
    NEED_IHK();
    SETK(PRIV + 0x305, 0x3, 0x7D); DELK(PRIV + 0x305);
    struct xm_shm *s; HANDLE sm;
    CHECK(child_op(1, PRIV + 0x305, 0, 0, &s, &sm));
    EXPECT_EQ(s->v[0], 0); EXPECT_EQ(s->e[0], XE_HOTKEY_NOT_REGISTERED);
}
XT(another_process_deleting_an_absent_id_is_invalid_parameter) {
    NEED_IHK();
    struct xm_shm *s; HANDLE sm;
    CHECK(child_op(3, PRIV + 0x306, 0, 0, &s, &sm));
    EXPECT_EQ(s->v[0], 0); EXPECT_EQ(s->e[0], XE_INVALID_PARAMETER);
}
