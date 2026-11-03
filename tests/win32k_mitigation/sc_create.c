/* win32k_mitigation / create: PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY at CreateProcess time, and what a
 * locked process passes on to its own children.
 *
 * ntoskrnl!PspApplyMitigationOptions (called from PspAllocateProcess with the creating/parent process):
 *   - options nibble 7 (bits 28..29) == 1 (ALWAYS_ON) sets EPROCESS+0x9d0 |= 0x3000;
 *   - if the parent already has 0x1000, the nibble is rewritten to ALWAYS_ON before that test, so a child of a
 *     locked process is locked whatever the creator asked for (DEFER, ALWAYS_OFF or nothing);
 *   - the audit options qword with the same nibble == 1 sets 0x2000, but only if 0x1000 is not set;
 *   - PspValidateMitigationOptions rejects any nibble equal to 3 (RESERVED) with STATUS_INVALID_PARAMETER. */
#include "mit.h"

/* ---- scenarios ------------------------------------------------------------------------------- */
SCN(readback_zero) {
    DWORD fl = ~0UL;
    if (t_is_wine() && (!mit_get_scd(GetCurrentProcess(), &fl) || fl != 0)) { printf("  SKIP Wine: no policy\n"); return; }
    MC(mit_get_scd(GetCurrentProcess(), &fl));
    MEQ(fl, 0);
}
SCN(spawn_plain_child) {
    /* arg: attribute given to the grandchild's CreateProcess; the grandchild expects to be locked ("cre"). */
    const struct mit_attr *a = NULL; const char *n = mit_arg();
    if (!strcmp(n, "off")) a = &MIT_ATTR_OFF;
    else if (!strcmp(n, "defer")) a = &MIT_ATTR_DEFER;
    else if (!strcmp(n, "on")) a = &MIT_ATTR_ON8;
    struct mit_proc p;
    int ok = mit_start(&p, "readback_flags::cre", a, 1);
    MCF(ok, "CreateProcess from a locked process failed, error %lu", p.err);
    if (!ok) return;
    mit_release(&p);
    DWORD code = mit_finish(&p, 20000);
    MCF(code == 0, "grandchild exit 0x%lx", code);
}
SCN(spawn_plain_child_noinherit) {
    /* Same, but bInheritHandles FALSE and no std handles: the verdict is the exit code alone. */
    const struct mit_attr *a = NULL; const char *n = mit_arg();
    if (!strcmp(n, "off")) a = &MIT_ATTR_OFF;
    else if (!strcmp(n, "defer")) a = &MIT_ATTR_DEFER;
    struct mit_proc p;
    int ok = mit_start(&p, "readback_flags::cre", a, 0);
    MCF(ok, "CreateProcess (no inherit) failed, error %lu", p.err);
    if (!ok) return;
    mit_release(&p);
    DWORD code = mit_finish(&p, 20000);
    MCF(code == 0, "grandchild exit 0x%lx", code);
}

#define INH(base, arg) \
    MIT_TEST(inherit_##base##_##arg##_set, "spawn_plain_child", #arg, "set") \
    MIT_TEST(inherit_##base##_##arg##_cre, "spawn_plain_child", #arg, "cre")
INH(plain, none)
INH(plain, off)
INH(plain, defer)
INH(plain, on)
#define INHN(arg) \
    MIT_TEST(inherit_noinherit_##arg##_set, "spawn_plain_child_noinherit", #arg, "set") \
    MIT_TEST(inherit_noinherit_##arg##_cre, "spawn_plain_child_noinherit", #arg, "cre")
INHN(none)
INHN(off)
INHN(defer)

/* ---- parent-driven creation tests ---------------------------------------------------------------- */
TEST(win32k_mitigation_create_on_qword) { mit_run_attr("readback_flags", "", "cre", &MIT_ATTR_ON8); }
TEST(win32k_mitigation_create_on_two_qwords) { mit_run_attr("readback_flags", "", "cre", &MIT_ATTR_ON16); }
TEST(win32k_mitigation_create_on_dword) { mit_run_attr("readback_flags", "", "cre", &MIT_ATTR_ON4); }
TEST(win32k_mitigation_create_on_pristine_state) { mit_run_attr("state_pristine_at_start", "", "cre", &MIT_ATTR_ON8); }
TEST(win32k_mitigation_create_on_blocks_stubs) { mit_run_attr("stub_call", "NtUserGetThreadState", "cre", &MIT_ATTR_ON16); }
TEST(win32k_mitigation_create_on_cannot_clear) { mit_run_attr("cannot_clear_disallow", "", "cre", &MIT_ATTR_ON4); }
TEST(win32k_mitigation_create_off_not_locked) {
    mit_run_attr("readback_zero", "", "plain", &MIT_ATTR_OFF);    /* ALWAYS_OFF: nibble 2 is accepted and applies nothing */
}
TEST(win32k_mitigation_create_off_converts) { mit_run_attr("control_unlocked_converts", "", "plain", &MIT_ATTR_OFF); }
TEST(win32k_mitigation_create_defer_not_locked) { mit_run_attr("readback_zero", "", "plain", &MIT_ATTR_DEFER); }
TEST(win32k_mitigation_create_defer_converts) { mit_run_attr("control_unlocked_converts", "", "plain", &MIT_ATTR_DEFER); }
TEST(win32k_mitigation_create_none_not_locked) { mit_run_attr("readback_zero", "", "plain", NULL); }
TEST(win32k_mitigation_create_none_converts) { mit_run_attr("control_unlocked_converts", "", "plain", NULL); }
TEST(win32k_mitigation_create_other_policy_bits_do_not_lock) {
    /* Bits of other mitigations in the same mask (here: DEP enable, nibble 4 == 1: bottom-up ASLR) leave the win32k nibble at DEFER. */
    struct mit_attr a = { 1, { 0x10000ULL, 0 }, 8, 0, { 0, 0 }, 0 };
    mit_run_attr("readback_zero", "", "plain", &a);
}
TEST(win32k_mitigation_create_on_with_other_bits) {
    struct mit_attr a = { 1, { MIT_CREATE_ON | 0x10000ULL, 0 }, 8, 0, { 0, 0 }, 0 };
    mit_run_attr("readback_flags", "", "cre", &a);
}
TEST(win32k_mitigation_create_on_with_audit_attribute) {
    /* Enforce wins: the audit nibble only sets 0x2000 when 0x1000 is clear, and readback prefers bit 0 anyway. */
    struct mit_attr a = { 1, { MIT_CREATE_ON, 0 }, 8, 1, { MIT_CREATE_ON, 0 }, 8 };
    mit_run_attr("readback_flags", "", "cre", &a);
}
TEST(win32k_mitigation_create_reserved_nibble_rejected) {
    struct mit_proc p;
    struct mit_attr a = { 1, { MIT_CREATE_RESERVED, 0 }, 8, 0, { 0, 0 }, 0 };
    if (t_is_wine()) { t_skip("Wine does not validate mitigation options"); return; }
    int ok = mit_start(&p, "readback_zero::plain", &a, 1);
    if (ok) { mit_release(&p); mit_finish(&p, 20000); }
    CHECKF(!ok, "CreateProcess accepted nibble value 3 for the win32k mitigation");
    if (!ok) EXPECT_EQ(p.err, ERROR_INVALID_PARAMETER);   /* PspValidateMitigationOptions: STATUS_INVALID_PARAMETER */
}
TEST(win32k_mitigation_create_audit_attribute_alone) {
    struct mit_proc p;
    if (!mit_start(&p, "audit_readback_stable::audc", &MIT_ATTR_AUDIT, 1)) { t_note("audit attribute rejected: error %lu", p.err); t_skip("no audit attribute"); return; }
    mit_release(&p);
    DWORD code = mit_finish(&p, 20000);
    CHECKF(code == 0, "exit 0x%lx", code);
}
TEST(win32k_mitigation_create_audit_attribute_converts) { mit_run("audit_user32_path_works", "", "audc"); }
TEST(win32k_mitigation_create_on_user32_load) { mit_run_attr("load_user32", "", "cre", &MIT_ATTR_ON16); }
