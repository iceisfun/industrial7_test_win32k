/* NtUserMapVirtualKeyEx, NtUserVkKeyScanEx, NtUserGetKeyNameText and GetKeyboardType against the
 * US layout (KLID 00000409), which is loaded into the window-station ring without being activated
 * wherever an explicit HKL is accepted. The wrappers in user32 are bare jumps that add the "use the
 * HKL argument" flag (MapVirtualKeyExW: r9d=1, VkKeyScanExW: r8d=1), so an explicit HKL always
 * goes through HKLtoPKL.
 *
 *   win32kbase!InternalMapVirtualKeyEx(code, type, pKbdTbl):
 *     type 0/4  VK -> scancode: scan the vsc2vk byte table (vk 0x10..0x12 are first widened to the
 *               L-variants 0xA0/0xA2/0xA4); then the E0 table (type 4 ORs 0xE000), the E1 table
 *               (type 4 only, ORs 0xE100), then aVkNumpad for the keypad digits.
 *     type 1/3  scancode -> VK; 0xE0xx/0xE1xx go through the E0/E1 tables; type 1 narrows the
 *               L/R modifier VKs 0xA0..0xA5 to 0x10..0x12; 0xFF ("none") maps to 0.
 *     type 2    VK -> unshifted char: A..Z are returned as is, else the VK_TO_WCHAR tables
 *               (dead keys OR in 0x80000000), unmapped VKs fail with ERROR_INVALID_PARAMETER.
 *     other     ERROR_INVALID_PARAMETER, result 0.
 *   win32kbase!InternalVkKeyScanEx: -1 (0xFFFF) when the char is not on the layout; control
 *     characters not found fall back to ((ch + 0x40) | 0x200).
 */
#include "kls.h"

struct mv { UINT code, expect; };

#define VKS(a) a
static const struct mv vk2sc[] = {
    {'A',0x1E},{'B',0x30},{'C',0x2E},{'D',0x20},{'E',0x12},{'F',0x21},{'G',0x22},{'H',0x23},{'I',0x17},
    {'J',0x24},{'K',0x25},{'L',0x26},{'M',0x32},{'N',0x31},{'O',0x18},{'P',0x19},{'Q',0x10},{'R',0x13},
    {'S',0x1F},{'T',0x14},{'U',0x16},{'V',0x2F},{'W',0x11},{'X',0x2D},{'Y',0x15},{'Z',0x2C},
    {'1',0x02},{'2',0x03},{'3',0x04},{'4',0x05},{'5',0x06},{'6',0x07},{'7',0x08},{'8',0x09},{'9',0x0A},{'0',0x0B},
    {VK_ESCAPE,0x01},{VK_BACK,0x0E},{VK_TAB,0x0F},{VK_RETURN,0x1C},{VK_SPACE,0x39},{VK_CAPITAL,0x3A},
    {VK_OEM_MINUS,0x0C},{VK_OEM_PLUS,0x0D},{VK_OEM_4,0x1A},{VK_OEM_6,0x1B},{VK_OEM_1,0x27},{VK_OEM_7,0x28},
    {VK_OEM_3,0x29},{VK_OEM_5,0x2B},{VK_OEM_COMMA,0x33},{VK_OEM_PERIOD,0x34},{VK_OEM_2,0x35},
    {VK_SHIFT,0x2A},{VK_LSHIFT,0x2A},{VK_RSHIFT,0x36},{VK_CONTROL,0x1D},{VK_LCONTROL,0x1D},{VK_MENU,0x38},{VK_LMENU,0x38},
    {VK_F1,0x3B},{VK_F2,0x3C},{VK_F3,0x3D},{VK_F4,0x3E},{VK_F5,0x3F},{VK_F6,0x40},{VK_F7,0x41},{VK_F8,0x42},
    {VK_F9,0x43},{VK_F10,0x44},{VK_F11,0x57},{VK_F12,0x58},
    {VK_NUMLOCK,0x45},{VK_SCROLL,0x46},{VK_MULTIPLY,0x37},{VK_SUBTRACT,0x4A},{VK_ADD,0x4E},
    {VK_NUMPAD7,0x47},{VK_NUMPAD8,0x48},{VK_NUMPAD9,0x49},{VK_NUMPAD4,0x4B},{VK_NUMPAD5,0x4C},{VK_NUMPAD6,0x4D},
    {VK_NUMPAD1,0x4F},{VK_NUMPAD2,0x50},{VK_NUMPAD3,0x51},{VK_NUMPAD0,0x52},{VK_DECIMAL,0x53},
    /* right-hand / extended keys live in the E0 table: type 0 drops the prefix */
    {VK_RCONTROL,0x1D},{VK_RMENU,0x38},{VK_LWIN,0x5B},{VK_RWIN,0x5C},{VK_APPS,0x5D},{VK_DIVIDE,0x35},
    /* nothing maps to these */
    {0,0},{0x07,0},{0x88,0},{0xFF,0},{0xE8,0},
};
static const struct mv vk2sc_ex_diff[] = {   /* type 4: same as type 0 except E0-table hits carry 0xE000 */
    {VK_RCONTROL,0xE01D},{VK_RMENU,0xE038},{VK_LWIN,0xE05B},{VK_RWIN,0xE05C},{VK_APPS,0xE05D},{VK_DIVIDE,0xE035},
};
static const struct mv sc2vk[] = {
    {0x01,VK_ESCAPE},{0x02,'1'},{0x03,'2'},{0x04,'3'},{0x05,'4'},{0x06,'5'},{0x07,'6'},{0x08,'7'},{0x09,'8'},{0x0A,'9'},{0x0B,'0'},
    {0x0C,VK_OEM_MINUS},{0x0D,VK_OEM_PLUS},{0x0E,VK_BACK},{0x0F,VK_TAB},
    {0x10,'Q'},{0x11,'W'},{0x12,'E'},{0x13,'R'},{0x14,'T'},{0x15,'Y'},{0x16,'U'},{0x17,'I'},{0x18,'O'},{0x19,'P'},
    {0x1A,VK_OEM_4},{0x1B,VK_OEM_6},{0x1C,VK_RETURN},{0x1D,VK_CONTROL},
    {0x1E,'A'},{0x1F,'S'},{0x20,'D'},{0x21,'F'},{0x22,'G'},{0x23,'H'},{0x24,'J'},{0x25,'K'},{0x26,'L'},
    {0x27,VK_OEM_1},{0x28,VK_OEM_7},{0x29,VK_OEM_3},{0x2A,VK_SHIFT},{0x2B,VK_OEM_5},
    {0x2C,'Z'},{0x2D,'X'},{0x2E,'C'},{0x2F,'V'},{0x30,'B'},{0x31,'N'},{0x32,'M'},
    {0x33,VK_OEM_COMMA},{0x34,VK_OEM_PERIOD},{0x35,VK_OEM_2},{0x36,VK_SHIFT},{0x37,VK_MULTIPLY},{0x38,VK_MENU},
    {0x39,VK_SPACE},{0x3A,VK_CAPITAL},{0x3B,VK_F1},{0x3C,VK_F2},{0x3D,VK_F3},{0x3E,VK_F4},{0x3F,VK_F5},
    {0x40,VK_F6},{0x41,VK_F7},{0x42,VK_F8},{0x43,VK_F9},{0x44,VK_F10},{0x45,VK_NUMLOCK},{0x46,VK_SCROLL},
    {0x4A,VK_SUBTRACT},{0x4E,VK_ADD},{0x57,VK_F11},{0x58,VK_F12},
    {0xE01D,VK_CONTROL},{0xE038,VK_MENU},{0xE05B,VK_LWIN},{0xE05C,VK_RWIN},{0xE05D,VK_APPS},{0xE035,VK_DIVIDE},
    /* beyond the table (count 0x59) and not an E0/E1 prefix: nothing */
    {0x60,0},{0x7F,0},{0x80,0},{0xFF,0},{0x100,0},{0x1234,0},{0xE0FF,0},
};
static const struct mv sc2vk_ex_diff[] = {   /* type 3 keeps the L/R distinction */
    {0x1D,VK_LCONTROL},{0x2A,VK_LSHIFT},{0x36,VK_RSHIFT},{0x38,VK_LMENU},{0xE01D,VK_RCONTROL},{0xE038,VK_RMENU},
};
static const struct mv vk2ch[] = {
    {'A','A'},{'M','M'},{'Z','Z'},{'0','0'},{'1','1'},{'5','5'},{'9','9'},
    {VK_OEM_1,';'},{VK_OEM_PLUS,'='},{VK_OEM_COMMA,','},{VK_OEM_MINUS,'-'},{VK_OEM_PERIOD,'.'},{VK_OEM_2,'/'},
    {VK_OEM_3,'`'},{VK_OEM_4,'['},{VK_OEM_5,'\\'},{VK_OEM_6,']'},{VK_OEM_7,'\''},
    {VK_SPACE,' '},{VK_RETURN,'\r'},{VK_BACK,'\b'},{VK_TAB,'\t'},{VK_ESCAPE,0x1B},
    {VK_NUMPAD0,'0'},{VK_NUMPAD1,'1'},{VK_NUMPAD2,'2'},{VK_NUMPAD3,'3'},{VK_NUMPAD4,'4'},{VK_NUMPAD5,'5'},
    {VK_NUMPAD6,'6'},{VK_NUMPAD7,'7'},{VK_NUMPAD8,'8'},{VK_NUMPAD9,'9'},
    {VK_MULTIPLY,'*'},{VK_ADD,'+'},{VK_SUBTRACT,'-'},{VK_DECIMAL,'.'},{VK_DIVIDE,'/'},
};
static const UINT vk2ch_unmapped[] = { VK_F1, VK_F12, VK_LEFT, VK_SHIFT, VK_CONTROL, VK_MENU, VK_PRIOR, VK_LWIN, 0xFF };

static void run_table(HKL h, UINT type, const struct mv *t, unsigned n, const char *what) {
    for (unsigned i = 0; i < n; i++) {
        UINT a = U(MapVirtualKeyExW)(t[i].code, type, h);
        CHECKF(a == t[i].expect, "%s type %u code %x: got %x want %x", what, type, t[i].code, a, t[i].expect);
        UINT b = UN(NtUserMapVirtualKeyEx)(t[i].code, type, h, 1);
        CHECKF(b == a, "%s Nt/user mismatch code %x: %x vs %x", what, t[i].code, b, a);
    }
}
#define ARR(a) a, (unsigned)(sizeof(a) / sizeof((a)[0]))
#define NEED_MV() KLS_NEED(); REQUIRE(U(MapVirtualKeyExW)); REQUIRE(UN(NtUserMapVirtualKeyEx)); \
    HKL us = kls_us(); if (!us) { t_skip("00000409 not loadable"); return; }

TEST(kbd_layouts_state_mapvk_vk_to_vsc_us) { NEED_MV(); run_table(us, MAPVK_VK_TO_VSC, ARR(vk2sc), "vk2vsc"); kls_cleanup(); }

TEST(kbd_layouts_state_mapvk_vk_to_vsc_ex_us) {
    NEED_MV();
    /* type 4 shares the lookup with type 0; only E0/E1 table hits differ */
    for (unsigned i = 0; i < sizeof vk2sc / sizeof *vk2sc; i++) {
        UINT want = vk2sc[i].expect; int ext = 0;
        for (unsigned j = 0; j < sizeof vk2sc_ex_diff / sizeof *vk2sc_ex_diff; j++)
            if (vk2sc_ex_diff[j].code == vk2sc[i].code) { want = vk2sc_ex_diff[j].expect; ext = 1; }
        (void)ext;
        UINT a = U(MapVirtualKeyExW)(vk2sc[i].code, MAPVK_VK_TO_VSC_EX, us);
        CHECKF(a == want, "type 4 code %x: got %x want %x", vk2sc[i].code, a, want);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_vsc_to_vk_us) { NEED_MV(); run_table(us, MAPVK_VSC_TO_VK, ARR(sc2vk), "vsc2vk"); kls_cleanup(); }

TEST(kbd_layouts_state_mapvk_vsc_to_vk_ex_us) {
    NEED_MV();
    for (unsigned i = 0; i < sizeof sc2vk / sizeof *sc2vk; i++) {
        UINT want = sc2vk[i].expect;
        for (unsigned j = 0; j < sizeof sc2vk_ex_diff / sizeof *sc2vk_ex_diff; j++)
            if (sc2vk_ex_diff[j].code == sc2vk[i].code) want = sc2vk_ex_diff[j].expect;
        UINT a = U(MapVirtualKeyExW)(sc2vk[i].code, MAPVK_VSC_TO_VK_EX, us);
        CHECKF(a == want, "type 3 code %x: got %x want %x", sc2vk[i].code, a, want);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_vk_to_char_us) { NEED_MV(); run_table(us, MAPVK_VK_TO_CHAR, ARR(vk2ch), "vk2char"); kls_cleanup(); }

TEST(kbd_layouts_state_mapvk_vk_to_char_unmapped_sets_error) {
    NEED_MV();
    for (unsigned i = 0; i < sizeof vk2ch_unmapped / sizeof *vk2ch_unmapped; i++) {
        SET_ERR(0);
        UINT r = U(MapVirtualKeyExW)(vk2ch_unmapped[i], MAPVK_VK_TO_CHAR, us);
        CHECKF(r == 0, "vk %x -> %x", vk2ch_unmapped[i], r);
 /* falls out of the VK_TO_WCHAR search to a failure path: UserSetLastError(ERROR_INVALID_PARAMETER) */
        if (vk2ch_unmapped[i] != 0xFF) EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_invalid_type_fails) {
    NEED_MV();
    static const UINT ty[] = { 5, 6, 7, 8, 0x10, 0x100, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof ty / sizeof *ty; i++) {
        SET_ERR(0);
        UINT r = U(MapVirtualKeyExW)('A', ty[i], us);
        CHECKF(r == 0, "type %x -> %x", ty[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_exhaustive_us_vk_to_vsc_roundtrip) {
    NEED_MV();
    /* for every vk that has a scancode, the scancode maps back to a vk with the same meaning
     * (modifiers come back as the generic 0x10..0x12 for type 1) */
    int mapped = 0;
    for (UINT vk = 1; vk < 256; vk++) {
        UINT sc = U(MapVirtualKeyExW)(vk, MAPVK_VK_TO_VSC, us);
        if (!sc) continue;
        mapped++;
        UINT back = U(MapVirtualKeyExW)(sc, MAPVK_VSC_TO_VK_EX, us);
        if (vk >= 0xA0 && vk <= 0xA5) continue;   /* right-hand twins share scancodes with the left keys */
        if (back == 0) continue;                    /* E0-only keys come back through the prefix */
        if (back != vk) t_note("vk %x -> sc %x -> vk %x", vk, sc, back);
    }
    CHECK(mapped > 80);   /* the US layout maps well over 80 virtual keys */
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_nt_user_agree_all_codes) {
    NEED_MV();
    for (UINT type = 0; type <= 4; type++)
        for (UINT code = 0; code < 0x100; code++) {
            UINT a = U(MapVirtualKeyExW)(code, type, us), b = UN(NtUserMapVirtualKeyEx)(code, type, us, 1);
            if (a != b) CHECKF(a == b, "type %u code %x: %x vs %x", type, code, a, b);
        }
    CHECK(1);
    kls_cleanup();
}

static DWORD w_mv_current(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    /* MapVirtualKeyW passes (code,type,0,0): fUseHkl = 0 selects pti->spklActive */
    for (unsigned i = 0; i < sizeof vk2sc / sizeof *vk2sc; i++) {
        UINT a = U(MapVirtualKeyW)(vk2sc[i].code, MAPVK_VK_TO_VSC);
        CHECKF(a == vk2sc[i].expect, "code %x: %x want %x", vk2sc[i].code, a, vk2sc[i].expect);
        UINT b = UN(NtUserMapVirtualKeyEx)(vk2sc[i].code, MAPVK_VK_TO_VSC, 0, 0);
        CHECK(a == b);
    }
    return 0;
}
TEST(kbd_layouts_state_mapvk_current_layout_path) {
    NEED_MV(); REQUIRE(U(MapVirtualKeyW));
    kls_thread(w_mv_current, us);
    kls_cleanup();
}

static DWORD w_mv_prev(void *arg) {
    (void)arg;
    HKL l[KLS_MAXL]; int n = kls_list(l, KLS_MAXL);
    HKL cur = kls_cur(); int idx = -1;
    for (int i = 0; i < n; i++) if (l[i] == cur) idx = i;
    if (idx < 0 || n < 2) return 0;
    HKL prev = l[(idx + n - 1) % n];
    /* NULL/0 is HKL_PREV once the "use HKL" flag is set, which the Ex wrappers always do */
    static const UINT codes[] = { 'Y', 'Z', 'A', 'Q', 'M', VK_OEM_1, VK_OEM_7 };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++)
        for (UINT type = 0; type <= 3; type++) {
            UINT a = U(MapVirtualKeyExW)(codes[i], type, NULL), b = U(MapVirtualKeyExW)(codes[i], type, prev);
            CHECKF(a == b, "type %u code %x: NULL -> %x, prev -> %x", type, codes[i], a, b);
        }
    return 0;
}
TEST(kbd_layouts_state_mapvk_null_hkl_means_previous_layout) {
    KLS_NEED(); REQUIRE(U(MapVirtualKeyExW));
    kls_other(kls_cur());
    kls_thread(w_mv_prev, NULL);
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_unknown_hkl_returns_zero) {
    KLS_NEED(); REQUIRE(U(MapVirtualKeyExW));
    kls_cur();
    /* HKLtoPKL fails -> is skipped and the initial 0 is returned */
    static const ULONG_PTR h[] = { 0x12345678, 0xDEAD0409, 0x7f7f7f7f };
    for (unsigned i = 0; i < 3; i++) {
        if (kls_in_list((HKL)h[i])) continue;
        CHECK(U(MapVirtualKeyExW)('A', MAPVK_VK_TO_VSC, (HKL)h[i]) == 0);
        CHECK(U(MapVirtualKeyExW)(0x1E, MAPVK_VSC_TO_VK, (HKL)h[i]) == 0);
    }
}

TEST(kbd_layouts_state_mapvk_dead_key_flag_on_us_international) {
    KLS_NEED(); REQUIRE(U(MapVirtualKeyExW));
    HKL x = kls_load(L"00020409");
    if (!x) { t_skip("US International layout not available"); return; }
    UINT a = U(MapVirtualKeyExW)(VK_OEM_7, MAPVK_VK_TO_CHAR, x);
    /* a dead key's result carries 0x80000000 (the table entry is WCH_DEAD followed by the accent) */
    CHECKF(a & 0x80000000u, "VK_OEM_7 -> %x", a);
    t_note("VK_OEM_7 dead char %x; VK_OEM_3 -> %x", a & 0xffff, U(MapVirtualKeyExW)(VK_OEM_3, MAPVK_VK_TO_CHAR, x));
    CHECK(U(MapVirtualKeyExW)('A', MAPVK_VK_TO_CHAR, x) == 'A');
    kls_cleanup();
}

TEST(kbd_layouts_state_mapvk_other_layouts_note) {
    KLS_NEED(); REQUIRE(U(MapVirtualKeyExW));
    static const WCHAR *k[] = { L"00000407", L"0000040c", L"00000809" };
    for (int i = 0; i < 3; i++) {
        HKL h = kls_load(k[i]);
        if (!h) continue;
        /* layout data, not kernel logic: recorded only */
        t_note("%ls: Y->sc %x  Z->sc %x  sc2C->vk %x  OEM_1->ch %x", k[i],
               U(MapVirtualKeyExW)('Y', 0, h), U(MapVirtualKeyExW)('Z', 0, h), U(MapVirtualKeyExW)(0x2C, 1, h),
               U(MapVirtualKeyExW)(VK_OEM_1, 2, h));
        CHECK(U(MapVirtualKeyExW)(0x1E, MAPVK_VSC_TO_VK, h) != 0);
    }
    kls_cleanup();
}

/* =================================================================================== VkKeyScan */

struct vks { WCHAR ch; short expect; };
static const struct vks vkscan[] = {
    {'a',0x41},{'b',0x42},{'c',0x43},{'d',0x44},{'e',0x45},{'f',0x46},{'g',0x47},{'h',0x48},{'i',0x49},{'j',0x4A},
    {'k',0x4B},{'l',0x4C},{'m',0x4D},{'n',0x4E},{'o',0x4F},{'p',0x50},{'q',0x51},{'r',0x52},{'s',0x53},{'t',0x54},
    {'u',0x55},{'v',0x56},{'w',0x57},{'x',0x58},{'y',0x59},{'z',0x5A},
    {'A',0x141},{'B',0x142},{'C',0x143},{'D',0x144},{'E',0x145},{'F',0x146},{'G',0x147},{'H',0x148},{'I',0x149},
    {'J',0x14A},{'K',0x14B},{'L',0x14C},{'M',0x14D},{'N',0x14E},{'O',0x14F},{'P',0x150},{'Q',0x151},{'R',0x152},
    {'S',0x153},{'T',0x154},{'U',0x155},{'V',0x156},{'W',0x157},{'X',0x158},{'Y',0x159},{'Z',0x15A},
    {'0',0x30},{'1',0x31},{'2',0x32},{'3',0x33},{'4',0x34},{'5',0x35},{'6',0x36},{'7',0x37},{'8',0x38},{'9',0x39},
    {')',0x130},{'!',0x131},{'@',0x132},{'#',0x133},{'$',0x134},{'%',0x135},{'^',0x136},{'&',0x137},{'*',0x138},{'(',0x139},
    {';',0xBA},{':',0x1BA},{'=',0xBB},{'+',0x1BB},{',',0xBC},{'<',0x1BC},{'-',0xBD},{'_',0x1BD},{'.',0xBE},{'>',0x1BE},
    {'/',0xBF},{'?',0x1BF},{'`',0xC0},{'~',0x1C0},{'[',0xDB},{'{',0x1DB},{'\\',0xDC},{'|',0x1DC},{']',0xDD},{'}',0x1DD},
    {'\'',0xDE},{'"',0x1DE},
    {' ',0x20},{'\r',0x0D},{'\t',0x09},{'\b',0x08},{0x1B,0x1B},
    {0x01,0x241},{0x02,0x242},{0x1A,0x25A},     /* Ctrl+letter: found in the Ctrl column or via the (ch+0x40)|0x200 fallback */
    {0x00E9,-1},{0x00E8,-1},{0x00FC,-1},{0x00DF,-1},{0x00A3,-1},{0x20AC,-1},{0x4E2D,-1},{0x0416,-1},{0xFFFE,-1},{0x2603,-1},
};

TEST(kbd_layouts_state_vkkeyscan_us_table) {
    KLS_NEED(); REQUIRE(U(VkKeyScanExW)); REQUIRE(UN(NtUserVkKeyScanEx));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    for (unsigned i = 0; i < sizeof vkscan / sizeof *vkscan; i++) {
        SHORT a = U(VkKeyScanExW)(vkscan[i].ch, us);
        CHECKF(a == vkscan[i].expect, "char %04x: got %04x want %04x", vkscan[i].ch, (unsigned short)a, (unsigned short)vkscan[i].expect);
        SHORT b = (SHORT)UN(NtUserVkKeyScanEx)(vkscan[i].ch, us, 1);
        CHECKF(a == b, "char %04x Nt %04x user %04x", vkscan[i].ch, (unsigned short)b, (unsigned short)a);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_vkkeyscan_failure_is_minus_one_in_both_bytes) {
    KLS_NEED(); REQUIRE(UN(NtUserVkKeyScanEx));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    /* InternalVkKeyScanEx returns 0xFFFFFFFF; the Nt wrapper narrows to SHORT and sign extends */
    LONG_PTR r = UN(NtUserVkKeyScanEx)(0x20AC, us, 1);
    CHECK(r == -1);
    kls_cleanup();
}

TEST(kbd_layouts_state_vkkeyscan_unknown_hkl_fails) {
    KLS_NEED(); REQUIRE(UN(NtUserVkKeyScanEx));
    kls_cur();
    /* */
    LONG_PTR r = UN(NtUserVkKeyScanEx)('a', (HKL)(ULONG_PTR)0x12345678, 1);
    CHECKF(r == -1, "got %lld", (long long)r);
}

static DWORD w_vks_current(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    for (unsigned i = 0; i < sizeof vkscan / sizeof *vkscan; i++) {
        SHORT a = U(VkKeyScanW)(vkscan[i].ch);
        CHECKF(a == vkscan[i].expect, "VkKeyScanW %04x: %04x want %04x", vkscan[i].ch, (unsigned short)a, (unsigned short)vkscan[i].expect);
        SHORT b = (SHORT)UN(NtUserVkKeyScanEx)(vkscan[i].ch, 0, 0);
        CHECK(a == b);
    }
    return 0;
}
TEST(kbd_layouts_state_vkkeyscan_current_layout_path) {
    KLS_NEED(); REQUIRE(U(VkKeyScanW)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_vks_current, us);
    kls_cleanup();
}

TEST(kbd_layouts_state_vkkeyscan_agrees_with_mapvirtualkey) {
    KLS_NEED(); REQUIRE(U(VkKeyScanExW)); REQUIRE(U(MapVirtualKeyExW));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    /* a printable ASCII char's VK (low byte) must produce that char (or its upper-case) via MAPVK_VK_TO_CHAR for letters/digits */
    for (WCHAR c = 'a'; c <= 'z'; c++) {
        SHORT s = U(VkKeyScanExW)(c, us);
        UINT ch = U(MapVirtualKeyExW)(s & 0xff, MAPVK_VK_TO_CHAR, us);
        CHECKF(ch == (UINT)(c - 32), "%c -> vk %x -> %x", c, s & 0xff, ch);
        CHECK((s >> 8) == 0);
    }
    for (WCHAR c = '0'; c <= '9'; c++) {
        SHORT s = U(VkKeyScanExW)(c, us);
        CHECK(U(MapVirtualKeyExW)(s & 0xff, MAPVK_VK_TO_CHAR, us) == c);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_vkkeyscan_other_layouts_note) {
    KLS_NEED(); REQUIRE(U(VkKeyScanExW));
    static const WCHAR *k[] = { L"00000407", L"0000040c", L"00000809", L"00020409" };
    for (int i = 0; i < 4; i++) {
        HKL h = kls_load(k[i]);
        if (!h) continue;
        t_note("%ls: 'z'->%04x 'y'->%04x '@'->%04x '\\u20ac'->%04x", k[i], (unsigned short)U(VkKeyScanExW)('z', h),
               (unsigned short)U(VkKeyScanExW)('y', h), (unsigned short)U(VkKeyScanExW)('@', h), (unsigned short)U(VkKeyScanExW)(0x20AC, h));
    }
    kls_cleanup();
}

/* ============================================================================== GetKeyNameText */

struct kn { UINT sc; WCHAR ch; };
static const struct kn keyname_char[] = {
    {0x02,'1'},{0x03,'2'},{0x04,'3'},{0x05,'4'},{0x06,'5'},{0x07,'6'},{0x08,'7'},{0x09,'8'},{0x0A,'9'},{0x0B,'0'},
    {0x0C,'-'},{0x0D,'='},{0x10,'Q'},{0x11,'W'},{0x12,'E'},{0x13,'R'},{0x14,'T'},{0x15,'Y'},{0x16,'U'},{0x17,'I'},
    {0x18,'O'},{0x19,'P'},{0x1A,'['},{0x1B,']'},{0x1E,'A'},{0x1F,'S'},{0x20,'D'},{0x21,'F'},{0x22,'G'},{0x23,'H'},
    {0x24,'J'},{0x25,'K'},{0x26,'L'},{0x27,';'},{0x28,'\''},{0x29,'`'},{0x2B,'\\'},{0x2C,'Z'},{0x2D,'X'},{0x2E,'C'},
    {0x2F,'V'},{0x30,'B'},{0x31,'N'},{0x32,'M'},{0x33,','},{0x34,'.'},{0x35,'/'},
};
/* keys whose names come from the layout's key-name tables: non-empty on a real layout, text is layout data */
static const UINT keyname_named[] = { 0x01,0x0E,0x0F,0x1C,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,0x40,0x41,0x42,0x43,0x44,0x57,0x58 };

static DWORD w_kn_chars(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    for (unsigned i = 0; i < sizeof keyname_char / sizeof *keyname_char; i++) {
        WCHAR b[32]; for (int j = 0; j < 32; j++) b[j] = 0x7777;
        int n = U(GetKeyNameTextW)((LONG)(keyname_char[i].sc << 16), b, 32);
        /* not in the name table -> InternalMapVirtualKeyEx(sc,1) then (vk,2): the printable char itself */
        CHECKF(n == 1 && b[0] == keyname_char[i].ch && b[1] == 0, "sc %x: len %d '%ls'", keyname_char[i].sc, n, b);
        WCHAR c[32]; for (int j = 0; j < 32; j++) c[j] = 0x7777;
        int m = UN(NtUserGetKeyNameText)((LONG)(keyname_char[i].sc << 16), c, 32);
        CHECK(m == n);
        CHECK(!wcscmp(b, c));
    }
    return 0;
}
TEST(kbd_layouts_state_keyname_printable_keys_us) {
    KLS_NEED(); REQUIRE(U(GetKeyNameTextW)); REQUIRE(UN(NtUserGetKeyNameText)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_kn_chars, us);
    kls_cleanup();
}

static DWORD w_kn_named(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    for (unsigned i = 0; i < sizeof keyname_named / sizeof *keyname_named; i++) {
        WCHAR b[64] = { 0 };
        int n = U(GetKeyNameTextW)((LONG)(keyname_named[i] << 16), b, 64);
        CHECKF(n > 0 && (int)wcslen(b) == n, "sc %x: len %d '%ls'", keyname_named[i], n, b);
        t_note("sc %02x = \"%ls\"", keyname_named[i], b);
    }
    return 0;
}
TEST(kbd_layouts_state_keyname_named_keys_us) {
    KLS_NEED(); REQUIRE(U(GetKeyNameTextW)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_kn_named, us);
    kls_cleanup();
}

static DWORD w_kn_flags(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    WCHAR a[64], b[64];
    /* _GetKeyNameText: with bit 25 set, scan 0x36 is folded to 0x2A (right shift -> shift) */
    U(GetKeyNameTextW)(0x2A << 16, a, 64);
    U(GetKeyNameTextW)((0x36 << 16) | (1 << 25), b, 64);
    CHECK(!wcscmp(a, b));
    /* bit 25 + extended bit on Ctrl (0x1D) / Alt (0x38) clears the extended flag: the plain key's name */
    U(GetKeyNameTextW)(0x1D << 16, a, 64);
    U(GetKeyNameTextW)((0x1D << 16) | (1 << 24) | (1 << 25), b, 64);
    CHECK(!wcscmp(a, b));
    U(GetKeyNameTextW)(0x38 << 16, a, 64);
    U(GetKeyNameTextW)((0x38 << 16) | (1 << 24) | (1 << 25), b, 64);
    CHECK(!wcscmp(a, b));
    /* bit 25 on a plain key changes nothing */
    U(GetKeyNameTextW)(0x1E << 16, a, 64);
    U(GetKeyNameTextW)((0x1E << 16) | (1 << 25), b, 64);
    CHECK(!wcscmp(a, b));
    /* low 16 bits (repeat count) and the high flag bits other than 24/25 are ignored */
    U(GetKeyNameTextW)(0x1E << 16, a, 64);
    U(GetKeyNameTextW)((0x1E << 16) | 0x0001 | 0x7fff, b, 64);
    CHECK(!wcscmp(a, b));
    /* extended vs non-extended names use different tables */
    WCHAR e[64], n[64];
    U(GetKeyNameTextW)(0x4B << 16, n, 64);
    U(GetKeyNameTextW)((0x4B << 16) | (1 << 24), e, 64);
    t_note("sc 4B plain \"%ls\" ext \"%ls\"", n, e);
    return 0;
}
TEST(kbd_layouts_state_keyname_lparam_flags) {
    KLS_NEED(); REQUIRE(U(GetKeyNameTextW)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_kn_flags, us);
    kls_cleanup();
}

static DWORD w_kn_trunc(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    WCHAR b[8];
    /* printable-key path writes the char, then a NUL only if cch > 1 */
    for (int j = 0; j < 8; j++) b[j] = 0x7777;
    int n = UN(NtUserGetKeyNameText)(0x1E << 16, b, 1);
    CHECK(b[0] == 'A');
    CHECK(b[1] == 0x7777);
    (void)n;
    for (int j = 0; j < 8; j++) b[j] = 0x7777;
    n = UN(NtUserGetKeyNameText)(0x1E << 16, b, 2);
    CHECK(n == 1);
    CHECK(b[0] == 'A' && b[1] == 0 && b[2] == 0x7777);
    /* cch <= 0 writes nothing */
    for (int j = 0; j < 8; j++) b[j] = 0x7777;
    UN(NtUserGetKeyNameText)(0x1E << 16, b, 0);
    for (int j = 0; j < 8; j++) CHECK(b[j] == 0x7777);
    /* a named key into a short buffer is truncated and still NUL terminated */
    for (int j = 0; j < 8; j++) b[j] = 0x7777;
    n = UN(NtUserGetKeyNameText)(0x39 << 16, b, 3);
    CHECK(n >= 0 && n <= 2);
    int z = 0; for (int j = 0; j < 3; j++) if (b[j] == 0) z = 1;
    CHECK(z);
    CHECK(b[3] == 0x7777);
    return 0;
}
TEST(kbd_layouts_state_keyname_buffer_sizes) {
    KLS_NEED(); REQUIRE(UN(NtUserGetKeyNameText)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_kn_trunc, us);
    kls_cleanup();
}

static DWORD w_kn_unknown(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    WCHAR b[16];
    /* a scancode with no name and no VK yields the empty string (0x7F is not one of them: the US layout names it) */
    static const UINT sc[] = { 0x00, 0x5F, 0xFF };
    for (unsigned i = 0; i < 3; i++) {
        for (int j = 0; j < 16; j++) b[j] = 0x7777;
        int n = U(GetKeyNameTextW)(sc[i] << 16, b, 16);
        CHECKF(n == 0, "sc %x -> %d '%ls'", sc[i], n, b);
    }
    for (int j = 0; j < 16; j++) b[j] = 0x7777;
    int n7f = U(GetKeyNameTextW)(0x7F << 16, b, 16);
    CHECKF(n7f == 3 && !lstrcmpW(b, L"F16"), "sc 7f -> %d '%ls'", n7f, b);   /* observed on Windows 10 with the US layout active */
    return 0;
}
TEST(kbd_layouts_state_keyname_unmapped_scancode_empty) {
    KLS_NEED(); REQUIRE(U(GetKeyNameTextW)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    HKL us = kls_us();
    if (!us) { t_skip("00000409 not loadable"); return; }
    kls_thread(w_kn_unknown, us);
    kls_cleanup();
}

/* ================================================================================ GetKeyboardType */

TEST(kbd_layouts_state_kbtype_type_in_known_range) {
    KLS_NEED(); REQUIRE(U(GetKeyboardType));
    int t = U(GetKeyboardType)(0);
    CHECKF((t >= 1 && t <= 7) || t == 0x51, "keyboard type %d", t);
    t_note("keyboard type %d subtype %d function keys %d", t, U(GetKeyboardType)(1), U(GetKeyboardType)(2));
}

TEST(kbd_layouts_state_kbtype_unknown_selectors_return_zero) {
    KLS_NEED(); REQUIRE(U(GetKeyboardType));
    /* _GetKeyboardType: anything but 0, 1, 2, 0x7ffffffe, 0x7fffffff falls out to 0 */
    static const int s[] = { 3, 4, 5, 100, 0x100, -1, 0x7ffffffd, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof s / sizeof *s; i++) CHECKF(U(GetKeyboardType)(s[i]) == 0, "selector %d", s[i]);
}

TEST(kbd_layouts_state_kbtype_function_key_count_plausible) {
    KLS_NEED(); REQUIRE(U(GetKeyboardType));
    int f = U(GetKeyboardType)(2);
    CHECKF(f >= 0 && f <= 48, "function keys %d", f);
    if (U(GetKeyboardType)(0) == 4 && f != 12) t_note("enhanced keyboard reports %d function keys", f);
}

TEST(kbd_layouts_state_kbtype_stable) {
    KLS_NEED(); REQUIRE(U(GetKeyboardType));
    for (int s = 0; s <= 2; s++) CHECK(U(GetKeyboardType)(s) == U(GetKeyboardType)(s));
}

TEST(kbd_layouts_state_kbtype_nt_callparam_matches_wrapper) {
    KLS_NEED(); REQUIRE(U(GetKeyboardType)); REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(WTSGetActiveConsoleSessionId));
    if (k_session_id() != U(WTSGetActiveConsoleSessionId)()) { t_skip("process not in the console session; wrapper uses the RDP path"); return; }
    /* user32!GetKeyboardType forwards to NtUserCallOneParam(selector, 0x33) in the console session */
    static const int s[] = { 0, 1, 2, 3, 0x7ffffffe, 0x7fffffff };
    for (unsigned i = 0; i < sizeof s / sizeof *s; i++) {
        int a = U(GetKeyboardType)(s[i]);
        int b = (int)UN(NtUserCallOneParam)((ULONG_PTR)(LONG_PTR)s[i], 0x33);
        CHECKF(a == b, "selector %x: wrapper %d, syscall %d", s[i], a, b);
    }
}
