/* win32k_mitigation / attrsize: PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY payload size, as two separate questions.
 *
 *   parse        which payload sizes UpdateProcThreadAttribute accepts (4, 8 and 16 bytes), which second-word
 *                values CreateProcess rejects (ERROR_INVALID_PARAMETER) and that a word outside the size is
 *                never looked at;
 *   enforcement  the child really runs with the policy: it reads it back with GetProcessMitigationPolicy.
 *
 * The 16-byte form carries a second DWORD64 of policy flags (PROCESS_CREATION_MITIGATION_POLICY2_*). The
 * enforcement tests use second-word flags that are visible through GetProcessMitigationPolicy and harmless on
 * the guest (a value that is accepted AND readable is what catches a payload silently cut back to 8 bytes):
 *   second word, nibble 5 (bits 20..23) == 1  ALLOW_DOWNGRADE_DYNAMIC_CODE_POLICY -> dynamic code policy bit 2
 *   second word, nibble 4 (bits 16..19) == 1  RESTRICT_INDIRECT_BRANCH_PREDICTION -> side channel policy bit 0
 *   second word, nibble 6 (bits 24..27) == 1  SPECULATIVE_STORE_BYPASS_DISABLE    -> side channel policy bit 3
 * First-word flags used: strict handle checks (nibble 6), prohibit dynamic code (nibble 9), extension points
 * (nibble 8), control flow guard (nibble 10), force relocate images (nibble 2). */
#include "mit.h"

#define W1_STRICT_HANDLE_ON  (1ULL << 24)
#define W1_DYNCODE_ON        (1ULL << 36)
#define W1_EXTPOINT_ON       (1ULL << 32)
#define W1_CFG_ON            (1ULL << 40)
#define W1_RELOC_ON          (1ULL << 8)
#define W2_DOWNGRADE_ON      (1ULL << 20)
#define W2_DOWNGRADE_OFF     (2ULL << 20)
#define W2_IBP_ON            (1ULL << 16)
#define W2_SSBD_ON           (1ULL << 24)
#define W2_IBP_RESERVED      (3ULL << 16)
#define W2_UNDEFINED_LOW     (1ULL << 0)

/* ---- child: read policies back --------------------------------------------------------------- */
/* arg: tokens "+C.M" (every bit of mask M set in policy class C) or "-C.M" (every bit clear), joined by '_'. */
SCN(expect_policy) {
    typedef BOOL (WINAPI *get_fn)(HANDLE, int, PVOID, SIZE_T);
    get_fn f = (get_fn)mit_k32("GetProcessMitigationPolicy");
    char tok[128]; snprintf(tok, sizeof tok, "%s", mit_arg());
    MC(f != NULL);
    if (!f) return;
    for (char *s = strtok(tok, "_"); s; s = strtok(NULL, "_")) {
        int want_set = s[0] == '+'; int cls = 0; unsigned mask = 0;
        MCF(sscanf(s + 1, "%d.%u", &cls, &mask) == 2, "bad token %s", s);
        DWORD v[4] = { 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA, 0xAAAAAAAA };
        BOOL ok = f(GetCurrentProcess(), cls, v, 4);
        MCF(ok, "GetProcessMitigationPolicy(class %d) failed, error %lu", cls, GetLastError());
        if (!ok) continue;
        if (want_set) MCF((v[0] & mask) == mask, "class %d: flags 0x%lx lack mask 0x%x", cls, v[0], mask);
        else          MCF((v[0] & mask) == 0, "class %d: flags 0x%lx contain mask 0x%x", cls, v[0], mask);
    }
}

/* ---- parent helpers ------------------------------------------------------------------------- */
static struct mit_attr mk(ULONGLONG w1, ULONGLONG w2, SIZE_T size) {
    struct mit_attr a = { 1, { w1, w2 }, size, 0, { 0, 0 }, 0 };
    return a;
}
static void run_expect(const struct mit_attr *a, const char *tokens) {
    if (t_is_wine()) { t_skip("Wine does not implement ProcessMitigationPolicy"); return; }
    mit_run_attr("expect_policy", tokens, "plain", a);
}
/* UpdateProcThreadAttribute alone: returns TRUE/FALSE and leaves the error in *err. */
static BOOL upta(SIZE_T size, DWORD *err) {
    SIZE_T sz = 0; BOOL ok;
    InitializeProcThreadAttributeList(NULL, 1, 0, &sz);
    LPPROC_THREAD_ATTRIBUTE_LIST l = (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(1, sz);
    InitializeProcThreadAttributeList(l, 1, 0, &sz);
    ULONGLONG v[8] = { MIT_CREATE_OFF, 0, 0, 0, 0, 0, 0, 0 };
    SetLastError(0);
    ok = UpdateProcThreadAttribute(l, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, v, size, NULL, NULL);
    *err = GetLastError();
    DeleteProcThreadAttributeList(l); free(l);
    return ok;
}

/* ---- parse ---------------------------------------------------------------------------------- */
TEST(win32k_mitigation_attrsize_parse_update_sizes) {
    int accepted = 0;
    for (SIZE_T s = 0; s <= 48; s++) {
        DWORD e; BOOL ok = upta(s, &e);
        int want = s == 4 || s == 8 || s == 16;
        CHECKF(ok == want, "payload size %u: accepted %d, want %d (error %lu)", (unsigned)s, ok, want, e);
        if (!ok) CHECKF(e == ERROR_BAD_LENGTH, "payload size %u: error %lu want ERROR_BAD_LENGTH", (unsigned)s, e);
        accepted += ok;
    }
    EXPECT_EQ(accepted, 3);
}
TEST(win32k_mitigation_attrsize_parse_larger_sizes) {
    static const SIZE_T big[] = { 24, 32, 64, 256, 4096 };
    for (unsigned i = 0; i < sizeof big / sizeof *big; i++) {
        DWORD e; BOOL ok = upta(big[i], &e);
        CHECKF(!ok && e == ERROR_BAD_LENGTH, "payload size %u: ok %d error %lu", (unsigned)big[i], ok, e);
    }
}
static void create_ok(const struct mit_attr *a) {
    if (t_is_wine()) { t_skip("Wine does not validate mitigation options"); return; }
    run_expect(a, "-3.3");   /* any readable, unset policy: only the creation matters here */
}
TEST(win32k_mitigation_attrsize_parse_create_size4) { struct mit_attr a = mk(MIT_CREATE_OFF, 0, 4); create_ok(&a); }
TEST(win32k_mitigation_attrsize_parse_create_size8) { struct mit_attr a = mk(MIT_CREATE_OFF, 0, 8); create_ok(&a); }
TEST(win32k_mitigation_attrsize_parse_create_size16) { struct mit_attr a = mk(MIT_CREATE_OFF, 0, 16); create_ok(&a); }
TEST(win32k_mitigation_attrsize_parse_create_size16_second_word_supported) {
    struct mit_attr a = mk(0, W2_DOWNGRADE_ON | W2_IBP_ON | W2_SSBD_ON, 16); create_ok(&a);
}
TEST(win32k_mitigation_attrsize_parse_create_size16_second_word_off) {
    struct mit_attr a = mk(0, W2_DOWNGRADE_OFF, 16); create_ok(&a);
}
static void create_reject(const struct mit_attr *a, const char *what) {
    struct mit_proc p;
    if (t_is_wine()) { t_skip("Wine does not validate mitigation options"); return; }
    int ok = mit_start(&p, "expect_policy:-3.3:plain", a, 1);
    if (ok) { mit_release(&p); mit_finish(&p, 20000); }
    CHECKF(!ok, "CreateProcess accepted %s", what);
    if (!ok) EXPECT_EQ(p.err, ERROR_INVALID_PARAMETER);
}
TEST(win32k_mitigation_attrsize_parse_create_size16_second_word_reserved_nibble_rejected) {
    struct mit_attr a = mk(0, W2_IBP_RESERVED, 16); create_reject(&a, "nibble value 3 in the second word");
}
TEST(win32k_mitigation_attrsize_parse_create_size16_second_word_undefined_bits_rejected) {
    struct mit_attr a = mk(0, W2_UNDEFINED_LOW, 16); create_reject(&a, "an undefined low flag in the second word");
}
TEST(win32k_mitigation_attrsize_parse_create_size16_valid_first_bad_second_rejected) {
    /* a valid first word does not rescue an invalid second word */
    struct mit_attr a = mk(W1_STRICT_HANDLE_ON, W2_IBP_RESERVED, 16); create_reject(&a, "valid first word with reserved second word");
}
TEST(win32k_mitigation_attrsize_parse_create_size8_second_word_not_read) {
    /* the second qword sits in the caller's buffer but is outside the declared size: it is not validated */
    struct mit_attr a = mk(0, W2_IBP_RESERVED | W2_UNDEFINED_LOW, 8); create_ok(&a);
}
TEST(win32k_mitigation_attrsize_parse_create_size4_upper_dword_not_read) {
    /* reserved nibble 3 in bits 52..55 lies in the upper dword of the first qword: size 4 does not carry it */
    struct mit_attr a = mk(3ULL << 52, 0, 4); create_ok(&a);
}
TEST(win32k_mitigation_attrsize_parse_create_size8_first_word_reserved_rejected) {
    struct mit_attr a = mk(3ULL << 52, 0, 8); create_reject(&a, "nibble value 3 in the upper dword of the first word");
}

/* ---- enforcement ---------------------------------------------------------------------------- */
TEST(win32k_mitigation_attrsize_enforce_control_defaults_clear) {
    run_expect(NULL, "-2.1_-2.4_-3.3_-6.1_-7.1_-14.1_-14.8_-1.2");
}
TEST(win32k_mitigation_attrsize_enforce_size8_strict_handle) {
    struct mit_attr a = mk(W1_STRICT_HANDLE_ON, 0, 8); run_expect(&a, "+3.3_-2.1_-2.4");
}
TEST(win32k_mitigation_attrsize_enforce_size8_dynamic_code) {
    struct mit_attr a = mk(W1_DYNCODE_ON, 0, 8); run_expect(&a, "+2.1_-3.3_-2.4");
}
TEST(win32k_mitigation_attrsize_enforce_size8_upper_dword_flags) {
    struct mit_attr a = mk(W1_EXTPOINT_ON | W1_CFG_ON, 0, 8); run_expect(&a, "+6.1_+7.1_-2.1");
}
TEST(win32k_mitigation_attrsize_enforce_size4_lower_dword_flag) {
    struct mit_attr a = mk(W1_RELOC_ON, 0, 4); run_expect(&a, "+1.2");
}
TEST(win32k_mitigation_attrsize_enforce_size4_drops_upper_dword) {
    struct mit_attr a = mk(W1_RELOC_ON | W1_EXTPOINT_ON | W1_CFG_ON | W1_DYNCODE_ON, 0, 4);
    run_expect(&a, "+1.2_-6.1_-7.1_-2.1");
}
TEST(win32k_mitigation_attrsize_enforce_size16_zero_second_word_matches_size8) {
    struct mit_attr a = mk(W1_STRICT_HANDLE_ON | W1_CFG_ON, 0, 16); run_expect(&a, "+3.3_+7.1_-2.4_-14.9");
}
TEST(win32k_mitigation_attrsize_enforce_size16_second_word_downgrade) {
    /* catches a 16-byte payload silently truncated to 8: the flag lives only in the second word */
    struct mit_attr a = mk(0, W2_DOWNGRADE_ON, 16); run_expect(&a, "+2.4_-2.1_-14.9");
}
TEST(win32k_mitigation_attrsize_enforce_size16_second_word_branch_prediction) {
    struct mit_attr a = mk(0, W2_IBP_ON, 16); run_expect(&a, "+14.1_-14.8_-2.4");
}
TEST(win32k_mitigation_attrsize_enforce_size16_second_word_store_bypass) {
    struct mit_attr a = mk(0, W2_SSBD_ON, 16); run_expect(&a, "+14.8_-14.1_-2.4");
}
TEST(win32k_mitigation_attrsize_enforce_size16_second_word_all) {
    struct mit_attr a = mk(0, W2_DOWNGRADE_ON | W2_IBP_ON | W2_SSBD_ON, 16); run_expect(&a, "+2.4_+14.9");
}
TEST(win32k_mitigation_attrsize_enforce_size16_both_words) {
    struct mit_attr a = mk(W1_STRICT_HANDLE_ON | W1_DYNCODE_ON | W1_RELOC_ON, W2_DOWNGRADE_ON | W2_SSBD_ON, 16);
    run_expect(&a, "+3.3_+2.1_+1.2_+2.4_+14.8_-14.1");
}
TEST(win32k_mitigation_attrsize_enforce_size16_second_word_off_not_set) {
    struct mit_attr a = mk(0, W2_DOWNGRADE_OFF, 16); run_expect(&a, "-2.4");
}
TEST(win32k_mitigation_attrsize_enforce_size16_win32k_and_second_word) {
    /* the win32k lockdown (first word) and a second-word flag are both applied: the prelude of the "cre"
     * flavor verifies the lockdown by readback, the scenario the second word */
    if (t_is_wine()) { t_skip("Wine does not implement ProcessMitigationPolicy"); return; }
    struct mit_attr a = mk(MIT_CREATE_ON, W2_DOWNGRADE_ON, 16);
    mit_run_attr("expect_policy", "+2.4", "cre", &a);
}
TEST(win32k_mitigation_attrsize_enforce_size8_ignores_second_word_in_buffer) {
    /* nonzero second qword in the buffer, declared size 8: nothing from it may be applied */
    struct mit_attr a = mk(W1_STRICT_HANDLE_ON, W2_DOWNGRADE_ON | W2_IBP_ON | W2_SSBD_ON, 8);
    run_expect(&a, "+3.3_-2.4_-14.9");
}
TEST(win32k_mitigation_attrsize_enforce_size4_ignores_second_word_in_buffer) {
    struct mit_attr a = mk(W1_STRICT_HANDLE_ON, W2_DOWNGRADE_ON | W2_IBP_ON | W2_SSBD_ON, 4);
    run_expect(&a, "+3.3_-2.4_-14.9");
}
