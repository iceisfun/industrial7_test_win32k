/* sc_stub_layout: static shape of every win32u.dll Nt* syscall stub (all 1316 names).
 *
 * Every stub on Windows 10 19041 x64 is the same 24-byte template (win32u!NtUserGetThreadState,
 * NtGdiBitBlt, NtUserCallOneParam ... stubs):
 *   4C 8B D1 | B8 imm32 | F6 04 25 08 03 FE 7F 01 | 75 03 | 0F 05 | C3 | CD 2E | C3
 * laid out 0x20 apart in .text in service-number order. The service number encodes the table
 * selector in bits 12-13 (1 = win32k shadow table, bit 12 picks the shadow table) and the index in bits 0-11. */
#include "sc_common.h"

/* ---- iteration helpers --------------------------------------------------- */
static int has_prefix(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

static int is_known_family(const char *n) {
    static const char *const f[] = { "NtUser", "NtGdi", "NtDComposition", "NtFlipObject", "NtDxgk",
        "NtModerncore", "NtMIT", "NtRIM", "NtTokenManager", "NtComposition", "NtCompositor", "NtMin" };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) if (has_prefix(n, f[i])) return 1;
    return 0;
}

/* Full per-stub decode and check; used by every family test so each family is reported separately. */
static void check_one_stub(const struct sc_entry *e) {
    const unsigned char *p = sc_stub(e->name);
    if (!p) { CHECKF(0, "%s: not exported by win32u.dll", e->name); return; }
    unsigned char got[SC_STUB_LEN], want[SC_STUB_LEN];
    if (!k_try_read(p, got, sizeof got)) { CHECKF(0, "%s: stub unreadable", e->name); return; }
    unsigned imm; memcpy(&imm, got + 4, 4);
    sc_expect_stub(imm, want);                       /* shape check independent of the number */
    CHECKF(!memcmp(got, want, SC_STUB_LEN), "%s: stub bytes differ from template", e->name);
    CHECKF(((imm >> 12) & 3) == 1, "%s: selector bits of 0x%x", e->name, imm);
    CHECKF((imm & 0xFFF) < SC_COUNT, "%s: index 0x%x", e->name, imm & 0xFFF);
    CHECKF((imm >> 14) == 0, "%s: number 0x%x has bits above 13", e->name, imm);
    CHECKF(k_stub_valid((FARPROC)p), "%s: k_stub_valid", e->name);
    CHECKF(k_stub_number((FARPROC)p) == (long)imm, "%s: k_stub_number", e->name);
    CHECK19F(imm == e->num, "%s: number 0x%x want 0x%x", e->name, imm, e->num);
    CHECKF(((ULONG_PTR)p & 0xF) == 0, "%s: stub %p not 16-byte aligned", e->name, p);
}

static void family(const char *prefix, int part, int parts) {
    if (sc_skip_wine("family stub sweep")) return;
    int k = 0, seen = 0;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const struct sc_entry *e = &sc_tab[i];
        int match = prefix ? has_prefix(e->name, prefix) : !is_known_family(e->name);
        if (!match) continue;
        if ((k++ % parts) != part) continue;
        seen++;
        check_one_stub(e);
    }
    if (!seen && parts == 1) t_note("family %s matched no table entries", prefix ? prefix : "(other)");
}
#define FAMILY_TEST(tag, prefix, part, parts) TEST(sc_stub_layout_family_##tag) { family(prefix, part, parts); }
FAMILY_TEST(user_0, "NtUser", 0, 6)
FAMILY_TEST(user_1, "NtUser", 1, 6)
FAMILY_TEST(user_2, "NtUser", 2, 6)
FAMILY_TEST(user_3, "NtUser", 3, 6)
FAMILY_TEST(user_4, "NtUser", 4, 6)
FAMILY_TEST(user_5, "NtUser", 5, 6)
FAMILY_TEST(gdi_0, "NtGdi", 0, 6)
FAMILY_TEST(gdi_1, "NtGdi", 1, 6)
FAMILY_TEST(gdi_2, "NtGdi", 2, 6)
FAMILY_TEST(gdi_3, "NtGdi", 3, 6)
FAMILY_TEST(gdi_4, "NtGdi", 4, 6)
FAMILY_TEST(gdi_5, "NtGdi", 5, 6)
FAMILY_TEST(dcomposition, "NtDComposition", 0, 1)
FAMILY_TEST(flipobject, "NtFlipObject", 0, 1)
FAMILY_TEST(dxgk, "NtDxgk", 0, 1)
FAMILY_TEST(moderncore, "NtModerncore", 0, 1)
FAMILY_TEST(mit, "NtMIT", 0, 1)
FAMILY_TEST(rim, "NtRIM", 0, 1)
FAMILY_TEST(tokenmanager, "NtTokenManager", 0, 1)
FAMILY_TEST(composition, "NtComposition", 0, 1)
FAMILY_TEST(compositor, "NtCompositor", 0, 1)
FAMILY_TEST(min, "NtMin", 0, 1)
FAMILY_TEST(other, NULL, 0, 1)

TEST(sc_stub_layout_family_counts) {
    /* Counts come from the 19041 reference table; other builds only note. */
    static const struct { const char *p; int n; } v[] = {
        { "NtUser", 566 }, { "NtGdi", 535 }, { "NtDComposition", 50 }, { "NtFlipObject", 21 },
        { "NtDxgk", 20 }, { "NtModerncore", 16 }, { "NtMIT", 0 }, { "NtRIM", 0 },
    };
    int n_user = 0, n_gdi = 0, n_dc = 0, n_fo = 0, n_dx = 0, n_mc = 0;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const char *s = sc_tab[i].name;
        n_user += has_prefix(s, "NtUser"); n_gdi += has_prefix(s, "NtGdi");
        n_dc += has_prefix(s, "NtDComposition"); n_fo += has_prefix(s, "NtFlipObject");
        n_dx += has_prefix(s, "NtDxgk"); n_mc += has_prefix(s, "NtModerncore");
    }
    /* embedded reference table must itself agree with the documented totals */
    EXPECT_EQ(n_user, 566); EXPECT_EQ(n_gdi, 535); EXPECT_EQ(n_dc, 50);
    EXPECT_EQ(n_fo, 21); EXPECT_EQ(n_dx, 20); EXPECT_EQ(n_mc, 16);
    if (sc_skip_wine("live family counts")) return;
    /* live: count win32u exports with each prefix through the export directory */
    struct sc_pe pe;
    if (!sc_pe_open("win32u.dll", &pe)) { t_skip("win32u.dll PE unreadable"); return; }
    int l_user = 0, l_gdi = 0, l_dc = 0, l_fo = 0, l_dx = 0, l_mc = 0;
    for (DWORD i = 0; i < pe.ed->NumberOfNames; i++) {
        const char *s = sc_pe_name(&pe, i);
        if (!s) continue;
        l_user += has_prefix(s, "NtUser"); l_gdi += has_prefix(s, "NtGdi");
        l_dc += has_prefix(s, "NtDComposition"); l_fo += has_prefix(s, "NtFlipObject");
        l_dx += has_prefix(s, "NtDxgk"); l_mc += has_prefix(s, "NtModerncore");
    }
    CHECK19(l_user == 566); CHECK19(l_gdi == 535); CHECK19(l_dc == 50);
    CHECK19(l_fo == 21); CHECK19(l_dx == 20); CHECK19(l_mc == 16);
    t_note("live families: NtUser=%d NtGdi=%d NtDComposition=%d NtFlipObject=%d NtDxgk=%d NtModerncore=%d", l_user, l_gdi, l_dc, l_fo, l_dx, l_mc);
}

/* ---- reference-table self consistency ------------------------------------ */
TEST(sc_stub_layout_table_is_dense_and_ordered) {
    for (unsigned i = 0; i < SC_COUNT; i++) {
        CHECKF(sc_tab[i].num == SC_FIRST + i, "entry %u number %u", i, sc_tab[i].num);
        CHECKF(sc_tab[i].rva == sc_tab[0].rva + SC_STRIDE * i, "entry %u rva 0x%x", i, sc_tab[i].rva);
    }
}
TEST(sc_stub_layout_table_names_unique) {
    for (unsigned i = 0; i < SC_COUNT; i++)
        for (unsigned j = i + 1; j < SC_COUNT; j++)
            if (!strcmp(sc_tab[i].name, sc_tab[j].name)) CHECKF(0, "duplicate %s", sc_tab[i].name);
    CHECK(1);
}
TEST(sc_stub_layout_table_ordinals_are_name_rank) {
    /* export ordinals are the 1-based rank of the name in byte order (the loader binary-searches); lower-case 'gDispatchTableValues' sorts after every 'Nt' name */
    for (unsigned i = 0; i < SC_COUNT; i++) {
        unsigned rank = 1;
        for (unsigned j = 0; j < SC_COUNT; j++)
            if (strcmp(sc_tab[j].name, sc_tab[i].name) < 0) rank++;
        CHECKF(sc_tab[i].ord == rank, "%s ord %u rank %u", sc_tab[i].name, sc_tab[i].ord, rank);
    }
}
TEST(sc_stub_layout_name_syntax) {
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const char *s = sc_tab[i].name;
        CHECKF(s[0] == 'N' && s[1] == 't', "%s lacks Nt prefix", s);
        CHECKF(strlen(s) > 4 && strlen(s) < 64, "%s length", s);
        for (const char *c = s; *c; c++)
            CHECKF((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_', "%s has odd char", s);
        CHECKF(!has_prefix(s, "Zw"), "%s", s);
    }
}

/* ---- live: each byte group of the stub, one facet per test --------------- */
#define LIVE_FACET_BEGIN if (sc_skip_wine("stub facet")) return; \
    for (unsigned i = 0; i < SC_COUNT; i++) { const struct sc_entry *e = &sc_tab[i]; \
        const unsigned char *p = sc_stub(e->name); unsigned char b[SC_STUB_LEN]; \
        if (!p || !k_try_read(p, b, sizeof b)) { CHECKF(0, "%s unreadable", e->name); continue; } (void)b;
#define LIVE_FACET_END }

TEST(sc_stub_layout_all_names_resolve) {
    if (sc_skip_wine("resolve")) return;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        FARPROC a = t_proc_in("win32u.dll", sc_tab[i].name);
        CHECKF(a != NULL, "%s not exported", sc_tab[i].name);
        /* win32u.dll is first in T_MODULE_LIST, so an unpinned search must land on the same address */
        CHECKF(t_proc(sc_tab[i].name) == a, "%s search order", sc_tab[i].name);
    }
}
TEST(sc_stub_layout_bytes_exact_template) {
    LIVE_FACET_BEGIN
    unsigned char w[SC_STUB_LEN]; sc_expect_stub(e->num, w);
    CHECK19F(!memcmp(b, w, SC_STUB_LEN), "%s", e->name);
    LIVE_FACET_END
}
TEST(sc_stub_layout_facet_mov_r10_rcx) {
    LIVE_FACET_BEGIN
    CHECKF(b[0] == 0x4C && b[1] == 0x8B && b[2] == 0xD1, "%s", e->name);
    LIVE_FACET_END
}
TEST(sc_stub_layout_facet_mov_eax_imm32) {
    LIVE_FACET_BEGIN
    unsigned imm; memcpy(&imm, b + 4, 4);
    CHECKF(b[3] == 0xB8, "%s opcode %02x", e->name, b[3]);
    CHECK19F(imm == e->num, "%s imm 0x%x want 0x%x", e->name, imm, e->num);
    LIVE_FACET_END
}
TEST(sc_stub_layout_facet_test_kuser_systemcall) {
    /* test byte ptr [0x7FFE0308], 1 : absolute disp32 form (modrm 04, sib 25) */
    static const unsigned char t[8] = { 0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01 };
    LIVE_FACET_BEGIN
    CHECKF(!memcmp(b + 8, t, 8), "%s", e->name);
    LIVE_FACET_END
}
TEST(sc_stub_layout_facet_jne_over_syscall) {
    LIVE_FACET_BEGIN
    CHECKF(b[16] == 0x75 && b[17] == 0x03, "%s jne rel8 %02x %02x", e->name, b[16], b[17]);
    CHECKF(b[18] == 0x0F && b[19] == 0x05, "%s syscall", e->name);
    CHECKF(b[20] == 0xC3, "%s ret", e->name);
    /* jne +3 from offset 18 lands on 21, the int 2e */
    CHECK(18 + b[17] == 21);
    LIVE_FACET_END
}
TEST(sc_stub_layout_facet_int2e_fallback) {
    LIVE_FACET_BEGIN
    CHECKF(b[21] == 0xCD && b[22] == 0x2E && b[23] == 0xC3, "%s", e->name);
    LIVE_FACET_END
}
TEST(sc_stub_layout_facet_selector_and_index) {
    LIVE_FACET_BEGIN
    unsigned n; memcpy(&n, b + 4, 4);
    CHECKF(((n >> 12) & 3) == 1, "%s selector %u", e->name, (n >> 12) & 3);
    CHECKF((n & 0xFFF) == (e->num & 0xFFF) || !sc_on_19041(), "%s index", e->name);
    CHECK19F((n & 0xFFF) < 0x524, "%s index 0x%x beyond service limit", e->name, n & 0xFFF);
    LIVE_FACET_END
}
TEST(sc_stub_layout_decoded_numbers_unique_and_dense) {
    if (sc_skip_wine("number density")) return;
    static unsigned char seen[0x1000];
    memset(seen, 0, sizeof seen);
    unsigned cnt = 0;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        long n = k_stub_number((FARPROC)sc_stub(sc_tab[i].name));
        if (n < 0) { CHECKF(0, "%s undecodable", sc_tab[i].name); continue; }
        unsigned idx = (unsigned)n & 0xFFF;
        CHECKF(!seen[idx], "%s: number 0x%lx used twice", sc_tab[i].name, n);
        seen[idx] = 1; cnt++;
    }
    for (unsigned i = 0; i < SC_COUNT; i++) CHECKF(seen[i], "index %u has no stub", i);
    for (unsigned i = SC_COUNT; i < 0x1000; i++) CHECKF(!seen[i], "index %u beyond table", i);
    EXPECT_EQ(cnt, SC_COUNT);
}
TEST(sc_stub_layout_address_order_follows_number) {
    if (sc_skip_wine("address order")) return;
    const unsigned char *prev = NULL; unsigned pn = 0;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const unsigned char *p = sc_stub(sc_tab[i].name);
        long n = k_stub_number((FARPROC)p);
        if (!p || n < 0) { CHECKF(0, "%s", sc_tab[i].name); continue; }
        if (prev) {
            CHECKF(p > prev, "%s not above previous stub", sc_tab[i].name);
            if ((unsigned)n == pn + 1) CHECKF(p - prev == SC_STRIDE, "%s stride %lld", sc_tab[i].name, (long long)(p - prev));
        }
        prev = p; pn = (unsigned)n;
    }
}
TEST(sc_stub_layout_alignment_0x10_not_0x20) {
    /* first stub at, 0x20 apart: every stub is 16- but not 32-byte aligned. */
    LIVE_FACET_BEGIN
    CHECKF(((ULONG_PTR)p & 0xF) == 0, "%s %p", e->name, p);
    CHECK19F(((ULONG_PTR)p & 0x1F) == 0x10, "%s %p", e->name, p);
    LIVE_FACET_END
}
TEST(sc_stub_layout_padding_between_stubs) {
    /* stubs are 0x18 bytes in 0x20 slots; the observed tail is an 8-byte NOP (0F 1F 84 00 00 00 00 00).
     * Not behaviour, so only noted. */
    if (sc_skip_wine("padding")) return;
    static const unsigned char nop8[8] = { 0x0F, 0x1F, 0x84, 0, 0, 0, 0, 0 };
    int odd = 0;
    for (unsigned i = 0; i + 1 < SC_COUNT; i++) {
        const unsigned char *p = sc_stub(sc_tab[i].name); unsigned char pad[8];
        if (!p || !k_try_read(p + SC_STUB_LEN, pad, 8)) { CHECKF(0, "%s", sc_tab[i].name); continue; }
        if (memcmp(pad, nop8, 8)) odd++;
    }
    t_note("stubs with a tail other than the 8-byte NOP: %d", odd);
}
TEST(sc_stub_layout_stub_pages_are_image_execute_read) {
    if (sc_skip_wine("page protection")) return;
    for (unsigned i = 0; i < SC_COUNT; i += 1) {
        const unsigned char *p = sc_stub(sc_tab[i].name);
        MEMORY_BASIC_INFORMATION mbi;
        if (!p || !VirtualQuery(p, &mbi, sizeof mbi)) { CHECKF(0, "%s", sc_tab[i].name); continue; }
        CHECKF(mbi.Type == MEM_IMAGE, "%s type 0x%lx", sc_tab[i].name, mbi.Type);
        /* .text is IMAGE_SCN_MEM_EXECUTE|READ only (section flags 0x60000020) */
        CHECKF(mbi.Protect == PAGE_EXECUTE_READ, "%s protect 0x%lx", sc_tab[i].name, mbi.Protect);
        CHECKF(mbi.AllocationBase == (void *)GetModuleHandleA("win32u.dll"), "%s allocbase", sc_tab[i].name);
    }
}
TEST(sc_stub_layout_stubs_inside_win32u_text_section) {
    if (sc_skip_wine("section")) return;
    struct sc_pe pe;
    if (!sc_pe_open("win32u.dll", &pe)) { t_skip("win32u PE"); return; }
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const unsigned char *p = sc_stub(sc_tab[i].name);
        if (!p) { CHECKF(0, "%s", sc_tab[i].name); continue; }
        DWORD rva = (DWORD)(p - pe.base);
        const IMAGE_SECTION_HEADER *s = sc_pe_section(&pe, rva);
        CHECKF(s && !memcmp(s->Name, ".text", 6), "%s section %.8s", sc_tab[i].name, s ? (const char *)s->Name : "?");
        CHECKF(s && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE) && !(s->Characteristics & IMAGE_SCN_MEM_WRITE), "%s", sc_tab[i].name);
        CHECKF(s && rva + SC_STUB_LEN <= s->VirtualAddress + s->Misc.VirtualSize, "%s tail beyond section", sc_tab[i].name);
        if (rva != sc_tab[i].rva) sc_note_once("absolute stub RVA differs from the reference build (UBR dependent)");
    }
}
TEST(sc_stub_layout_relative_rva_matches_reference) {
    /* RVAs may shift between UBRs; the spacing may not. */
    if (sc_skip_wine("rva")) return;
    struct sc_pe pe;
    if (!sc_pe_open("win32u.dll", &pe)) { t_skip("win32u PE"); return; }
    DWORD first = (DWORD)(sc_stub(sc_tab[0].name) - pe.base);
    int abs_ok = 1;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        DWORD rva = (DWORD)(sc_stub(sc_tab[i].name) - pe.base);
        CHECK19F(rva - first == sc_tab[i].rva - sc_tab[0].rva, "%s rva delta", sc_tab[i].name);
        if (rva != sc_tab[i].rva) abs_ok = 0;
    }
    if (!abs_ok) t_note("absolute stub RVAs differ from the reference image (first stub at 0x%lx, reference 0x%x)", (unsigned long)first, sc_tab[0].rva);
}
TEST(sc_stub_layout_distinct_stub_addresses) {
    if (sc_skip_wine("addresses")) return;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const unsigned char *p = sc_stub(sc_tab[i].name);
        if (i) CHECKF(p != sc_stub(sc_tab[i - 1].name), "%s aliases its neighbour", sc_tab[i].name);
    }
    CHECK(1);
}
TEST(sc_stub_layout_known_numbers) {
    static const struct { const char *n; long v; } k[] = {
        { "NtUserGetThreadState", 0x1000 }, { "NtUserPeekMessage", 0x1001 }, { "NtUserCallOneParam", 0x1002 },
        { "NtUserGetKeyState", 0x1003 }, { "NtUserInvalidateRect", 0x1004 }, { "NtGdiBitBlt", 0x1008 },
        { "NtGdiFlush", 0x1012 }, { "NtUserGetDoubleClickTime", 0x10AE },
    };
    if (sc_skip_wine("known numbers")) return;
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++) {
        const unsigned char *p = sc_stub(k[i].n);
        REQUIRE(p);
        CHECK19F(k_stub_number((FARPROC)p) == k[i].v, "%s", k[i].n);
    }
}
TEST(sc_stub_layout_helper_agrees_with_independent_decode) {
    /* common/k.c k_stub_valid/k_stub_number vs. a full-template compare */
    if (sc_skip_wine("helper cross-check")) return;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        const unsigned char *p = sc_stub(sc_tab[i].name);
        unsigned char b[SC_STUB_LEN], w[SC_STUB_LEN], big[40];
        if (!p || !k_try_read(p, b, sizeof b)) { CHECKF(0, "%s", sc_tab[i].name); continue; }
        unsigned n; memcpy(&n, b + 4, 4); sc_expect_stub(n, w);
        CHECK(!memcmp(b, w, SC_STUB_LEN) == !!k_stub_valid((FARPROC)p));
        CHECK(k_stub_bytes((FARPROC)p, big, sizeof big) >= 20);
        CHECK(!memcmp(big, b, 20));
    }
}
TEST(sc_stub_layout_other_modules_do_not_alias_win32k_stubs) {
    /* ntdll/kernel32/kernelbase export no NtUser/NtGdi names that decode to a win32k number.
     * (observational: a stub elsewhere would not be a bug in win32u, so notes only) */
    static const char *const mods[] = { "ntdll.dll", "kernelbase.dll", "kernel32.dll", "user32.dll", "gdi32.dll" };
    for (unsigned m = 0; m < sizeof mods / sizeof mods[0]; m++) {
        HMODULE h = GetModuleHandleA(mods[m]);
        if (!h) { if (!(h = LoadLibraryA(mods[m]))) continue; }
        int hits = 0;
        for (unsigned i = 0; i < SC_COUNT; i += 1) {
            FARPROC p = GetProcAddress(h, sc_tab[i].name);
            if (p && (const unsigned char *)p != sc_stub(sc_tab[i].name)) hits++;
        }
        if (hits) t_note("%s exports %d win32u syscall names", mods[m], hits);
    }
    CHECK(1);
}
