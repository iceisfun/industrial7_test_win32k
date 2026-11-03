/* Window long / word syscalls: user32!Get/SetWindowLong[Ptr]/Word -> NtUserSetWindowLong,
 * NtUserSetWindowLongPtr, NtUserSetWindowWord (win32kfull) and the client-side readers (_GetWindowLong, GetWindowData).
 * The reads never enter the kernel for a same-thread window: they dereference the read-only desktop-heap
 * tagWND, so state checks can compare the API result with the raw tagWND field. */
#include "wpsq.h"

static LONG_PTR gwlp(HWND h, int i) { return U(GetWindowLongPtrW)(h, i); }
static LONG_PTR swlp(HWND h, int i, LONG_PTR v) { return U(SetWindowLongPtrW)(h, i, v); }
static LONG     gwl(HWND h, int i)  { return U(GetWindowLongW)(h, i); }
static LONG     swl(HWND h, int i, LONG v) { return U(SetWindowLongW)(h, i, v); }

/* ---------------- invalid window handles ------------------------------------------------- */

static HWND destroyed_hwnd(void) {
    HWND h = wpsq_mk(0, "gone");
    if (h) U(DestroyWindow)(h);
    return h;
}

WT(long_get_invalid_hwnd_32) {
    REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND gone = destroyed_hwnd();
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x10, (HWND)(ULONG_PTR)0x7FFFFFFF, gone };
    int idx[] = { GWL_STYLE, GWL_EXSTYLE, GWL_ID, GWL_USERDATA, 0, 4 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (i == 5 && !gone) continue;
        for (unsigned j = 0; j < sizeof idx / sizeof *idx; j++) {
            SetLastError(0);
            LONG r = gwl(bad[i], idx[j]);
            /* user32!GetWindowLongW: ValidateHwnd fails -> RtlSetLastWin32Error(0x578), return 0 */
            CHECKF(r == 0, "hwnd %p idx %d -> %ld", bad[i], idx[j], r);
            EXPECT_ERR(E_INVALID_HWND);
        }
    }
}

WT(long_get_invalid_hwnd_ptr) {
    REQUIRE(U(GetWindowLongPtrW)); REQUIRE(U(CreateWindowExW));
    HWND gone = destroyed_hwnd();
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x10, (HWND)(ULONG_PTR)0x7FFFFFFF, gone };
    int idx[] = { GWLP_WNDPROC, GWLP_HINSTANCE, GWLP_HWNDPARENT, GWLP_USERDATA, GWLP_ID, GWL_STYLE, 0, 8 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (i == 5 && !gone) continue;
        for (unsigned j = 0; j < sizeof idx / sizeof *idx; j++) {
            SetLastError(0);
            LONG_PTR r = gwlp(bad[i], idx[j]);
            CHECKF(r == 0, "hwnd %p idx %d -> %llx", bad[i], idx[j], (unsigned long long)r);
            EXPECT_ERR(E_INVALID_HWND);
        }
    }
}

WT(long_set_invalid_hwnd_32) {
    REQUIRE(U(SetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND gone = destroyed_hwnd();
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (i == 3 && !gone) continue;
        SetLastError(0);
        /* _SetWindowLong: HMValidateHandle fails -> returns 0 with last error set by the validator */
        LONG r = swl(bad[i], GWL_USERDATA, 5);
        CHECKF(r == 0, "hwnd %p -> %ld", bad[i], r);
        EXPECT_ERR(E_INVALID_HWND);
    }
}

WT(long_set_invalid_hwnd_ptr) {
    REQUIRE(U(SetWindowLongPtrW)); REQUIRE(U(CreateWindowExW));
    HWND gone = destroyed_hwnd();
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (i == 3 && !gone) continue;
        SetLastError(0);
        LONG_PTR r = swlp(bad[i], GWLP_USERDATA, 5);
        CHECKF(r == 0, "hwnd %p -> %llx", bad[i], (unsigned long long)r);
        EXPECT_ERR(E_INVALID_HWND);
    }
}

WT(long_set_invalid_hwnd_direct_stub) {
    /* NtUserSetWindowLong(Ptr): ValidateHwndEx(hwnd,1,1) fails before anything is touched -> 0 */
    FARPROC a = t_proc_in("win32u.dll", "NtUserSetWindowLong"), b = t_proc_in("win32u.dll", "NtUserSetWindowLongPtr");
    REQUIRE(a); REQUIRE(b);
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1 };
    U(GetWindowLongW);   /* make sure user32 is connected before the raw syscalls */
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        CHECK(UN(NtUserSetWindowLong)(bad[i], GWL_USERDATA, 1, 0) == 0);
        EXPECT_ERR(E_INVALID_HWND);
        SetLastError(0);
        CHECK(UN(NtUserSetWindowLongPtr)(bad[i], GWLP_USERDATA, 1, 0) == 0);
        EXPECT_ERR(E_INVALID_HWND);
        SetLastError(0);
        CHECK(UN(NtUserSetWindowWord)(bad[i], GWL_USERDATA, 1) == 0);
    }
}

WT(long_stale_handle_after_destroy) {
    REQUIRE(U(DestroyWindow)); REQUIRE(U(IsWindow));
    NEED_WND(h, 0);
    swlp(h, GWLP_USERDATA, 0x55);
    CHECK(U(DestroyWindow)(h));
    CHECK(!U(IsWindow)(h));
    SetLastError(0);
    WEQ(gwlp(h, GWLP_USERDATA), 0);
    EXPECT_ERR(E_INVALID_HWND);
    SetLastError(0);
    WEQ(swlp(h, GWLP_USERDATA, 9), 0);
    EXPECT_ERR(E_INVALID_HWND);
    /* the USER handle-table entry must be free or flagged destroyed (HMValidateHandle rejects bit0 of +0x19, type 0) */
    struct k_user_entry e;
    if (k_user_entry(h, &e)) CHECKF(e.type != K_TYPE_WINDOW || (e.flags & 1), "type=%u flags=%02x", e.type, e.flags);
}

/* ---------------- state of a live window -------------------------------------------------- */

WT(long_handle_table_entry_of_window) {
    NEED_WND(h, 0);
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) { t_skip("USER handle table not readable"); U(DestroyWindow)(h); return; }
    /* user32!GetWindowLongW fast path: entry.type == 1, bit0 of +0x19 clear, uniq == HIWORD(hwnd) or -1 */
    WEQ(e.type, K_TYPE_WINDOW);
    CHECK((e.flags & 1) == 0);
    CHECKF(e.uniq == (unsigned short)((ULONG_PTR)h >> 16) || e.uniq == 0xFFFF, "uniq=%04x hwnd=%p", e.uniq, h);
    U(DestroyWindow)(h);
}

WT(long_tagwnd_head_hwnd_matches_handle) {
    NEED_WND(h, 0);
    unsigned char *p = wpsq_wnd(h);
    if (!p) { t_skip("client tagWND pointer unavailable"); U(DestroyWindow)(h); return; }
    ULONG_PTR v = 0;
    /* GetWindowThreadProcessId / GetWindowTextW compare *(u64*)pwnd against the handle: tagWND.head.h is the hwnd */
    if (k_try_read(p + WND_hwnd, &v, sizeof v)) WEQ(v, (ULONG_PTR)h);
    else CHECK(0);
    U(DestroyWindow)(h);
}

WT(long_tagwnd_userdata_field) {
    NEED_WND(h, 0);
    if (!wpsq_wnd(h)) { t_skip("client tagWND pointer unavailable"); U(DestroyWindow)(h); return; }
    static const ULONG_PTR vals[] = { 0, 1, 0x12345678, 0xFFFFFFFFull, 0x1122334455667788ull, ~(ULONG_PTR)0 };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        swlp(h, GWLP_USERDATA, (LONG_PTR)vals[i]);
        ULONG_PTR raw = ~vals[i];
        /* GetWindowData(-0x15) returns *(pwnd+0xd8) */
        CHECK(wpsq_rd(h, WND_userdata, &raw, sizeof raw));
        WEQ(raw, vals[i]);
        WEQ((ULONG_PTR)gwlp(h, GWLP_USERDATA), vals[i]);
    }
    U(DestroyWindow)(h);
}

WT(long_tagwnd_hinstance_field) {
    NEED_WND(h, 0);
    if (!wpsq_wnd(h)) { t_skip("client tagWND pointer unavailable"); U(DestroyWindow)(h); return; }
    ULONG_PTR raw = 0;
    CHECK(wpsq_rd(h, WND_hmodule, &raw, sizeof raw));
    WEQ(raw, (ULONG_PTR)GetModuleHandleA(NULL));          /* GetWindowData(-6) returns *(pwnd+0x20) */
    WEQ((ULONG_PTR)gwlp(h, GWLP_HINSTANCE), (ULONG_PTR)GetModuleHandleA(NULL));
    U(DestroyWindow)(h);
}

WT(long_tagwnd_style_and_exstyle_fields) {
    HWND h = wpsq_mkex(0, WS_POPUP | WS_DISABLED | WS_CLIPSIBLINGS, WS_EX_ACCEPTFILES, NULL, "s", 0);
    if (!h) { t_skip("cannot create window"); return; }
    if (!wpsq_wnd(h)) { t_skip("client tagWND pointer unavailable"); U(DestroyWindow)(h); return; }
    DWORD st = 0, ex = 0;
    CHECK(wpsq_rd(h, WND_style, &st, 4));
    CHECK(wpsq_rd(h, WND_exstyle, &ex, 4));
    /* GetWindowData(-0x10): *(u32*)(pwnd+0x1c) | 0x10000000 if flag 0x800 set; (-0x14): mask & *(u32*)(pwnd+0x18) */
    DWORD gs = (DWORD)gwl(h, GWL_STYLE);
    CHECKF((gs & ~0x10000000u) == (st & ~0x10000000u), "api=%08lx raw=%08lx", gs, st);
    CHECK((gs & WS_POPUP) && (gs & WS_DISABLED) && (gs & WS_CLIPSIBLINGS));
    DWORD ge = (DWORD)gwl(h, GWL_EXSTYLE);
    CHECKF((ge & ~ex) == 0, "api=%08lx raw=%08lx (api must be a masked subset)", ge, ex);
    CHECK(ge & WS_EX_ACCEPTFILES);
    CHECK(ge & WS_EX_TOOLWINDOW);
    U(DestroyWindow)(h);
}

WT(long_tagwnd_extra_bytes_layout) {
    NEED_WND(h, 0);
    unsigned char *p = wpsq_wnd(h);
    if (!p) { t_skip("client tagWND pointer unavailable"); U(DestroyWindow)(h); return; }
    DWORD cb = 0, ib = 99; void *xp = NULL;
    CHECK(wpsq_rd(h, WND_cbextra, &cb, 4));
    CHECK(wpsq_rd(h, WND_ibextra, &ib, 4));
    CHECK(wpsq_rd(h, WND_extraptr, &xp, 8));
    WEQ(cb, WPSQ_EXTRA);      /* class cbWndExtra is copied into the window; _GetWindowLong bound-checks idx+4 <= cb */
    WEQ(ib, 0);               /* plain (fnid 0) window: no server-owned prefix */
    CHECK(xp != NULL);
    for (int i = 0; i < 16; i++) swl(h, i * 4, 0x01010101 * (i + 1));
    /* GetWindowLong index i reads *(u32*)(*(pwnd+0x128) + i) */
    for (int i = 0; i < 16; i++) {
        DWORD v = 0;
        CHECK(k_try_read((char *)xp + i * 4, &v, 4));
        WEQ(v, (DWORD)(0x01010101 * (i + 1)));
    }
    U(DestroyWindow)(h);
}

WT(long_tagwnd_child_id_field) {
    NEED_WND(par, 0);
    HWND c = wpsq_mkchild(par, 0x4321);
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    ULONG_PTR raw = 0;
    if (wpsq_wnd(c)) {
        CHECK(wpsq_rd(c, WND_id, &raw, 8));
        WEQ(raw, 0x4321);       /* GetWindowData(-0xc) for WS_CHILD returns *(pwnd+0x98) */
    }
    WEQ(gwlp(c, GWLP_ID), 0x4321);
    WEQ(gwl(c, GWL_ID), 0x4321);
    U(DestroyWindow)(par);
}

/* ---------------- negative index validation ---------------------------------------------- */

WT(long_get32_valid_negative_indexes) {
    REQUIRE(U(GetWindowLongW));
    NEED_WND(h, 0);
    /* _GetWindowLong lets only idx+0x15 in {0,1,5,9} (mask 0x223) through: USERDATA, EXSTYLE, STYLE, ID */
    static const int ok[] = { GWL_USERDATA, GWL_EXSTYLE, GWL_STYLE, GWL_ID };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        gwl(h, ok[i]);
    }
    U(DestroyWindow)(h);
}

WT(long_get32_rejects_pointer_sized_indexes) {
    REQUIRE(U(GetWindowLongW));
    NEED_WND(h, 0);
    /* GWL_WNDPROC/GWL_HINSTANCE/GWL_HWNDPARENT are not in the 32-bit mask, so GetWindowLongW falls to 0x585 on x64 */
    static const int bad[] = { GWL_WNDPROC, GWL_HINSTANCE, GWL_HWNDPARENT };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        LONG r = gwl(h, bad[i]);
        CHECKF(r == 0, "idx %d -> %ld", bad[i], r);
        EXPECT_ERR(E_INVALID_INDEX);
    }
    U(DestroyWindow)(h);
}

WT(long_get_ptr_valid_negative_indexes) {
    REQUIRE(U(GetWindowLongPtrW));
    NEED_WND(h, 0);
    /* GetWindowData switch arms: -40,-21,-20,-16,-12,-8,-6,-4,-2,-1; everything else is the 0x585 default */
    static const int ok[] = { -40, GWLP_USERDATA, GWL_EXSTYLE, GWL_STYLE, GWLP_ID, GWLP_HWNDPARENT, GWLP_HINSTANCE, GWLP_WNDPROC, -2, -1 };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        SetLastError(0);
        gwlp(h, ok[i]);
        CHECKF(GetLastError() != E_INVALID_INDEX, "idx %d rejected", ok[i]);
    }
    U(DestroyWindow)(h);
}

WT(long_get_ptr_invalid_negative_indexes) {
    REQUIRE(U(GetWindowLongPtrW));
    NEED_WND(h, 0);
    static const int bad[] = { -3, -5, -7, -9, -10, -11, -13, -14, -15, -17, -18, -19, -22, -23, -24, -30, -39, -41, -64, -100, -255, -0x1000, -0x10000, INT_MIN, INT_MIN + 1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        LONG_PTR r = gwlp(h, bad[i]);
        CHECKF(r == 0, "idx %d -> %llx", bad[i], (unsigned long long)r);
        CHECKF(GetLastError() == E_INVALID_INDEX, "idx %d err %lu", bad[i], GetLastError());
        SetLastError(0);
        r = gwl(h, bad[i]);
        CHECKF(r == 0, "32-bit idx %d -> %lx", bad[i], r);
        EXPECT_ERR(E_INVALID_INDEX);
    }
    U(DestroyWindow)(h);
}

WT(long_set_ptr_invalid_negative_indexes) {
    REQUIRE(U(SetWindowLongPtrW));
    NEED_WND(h, 0);
    swlp(h, GWLP_USERDATA, 0x77);
    static const int bad[] = { -1, -3, -5, -7, -9, -10, -11, -13, -14, -15, -17, -18, -19, -22, -23, -30, -39, -41, -100, INT_MIN };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        /* SetWindowLongPtr default arm: UserSetLastError(0x585), return 0 */
        LONG_PTR r = swlp(h, bad[i], 0x1234);
        CHECKF(r == 0, "idx %d -> %llx", bad[i], (unsigned long long)r);
        CHECKF(GetLastError() == E_INVALID_INDEX, "idx %d err %lu", bad[i], GetLastError());
    }
    WEQ(gwlp(h, GWLP_USERDATA), 0x77);   /* nothing was disturbed */
    U(DestroyWindow)(h);
}

WT(long_set32_invalid_negative_indexes) {
    REQUIRE(U(SetWindowLongW));
    NEED_WND(h, 0);
    LONG_PTR proc = gwlp(h, GWLP_WNDPROC), inst = gwlp(h, GWLP_HINSTANCE);
    /* SetWindowLong only forwards idx+0x15 in {0,1,5,9}; on x64 the 32-bit setter cannot replace pointers */
    static const int bad[] = { GWL_WNDPROC, GWL_HINSTANCE, GWL_HWNDPARENT, -1, -2, -3, -5, -7, -9, -40, -100, INT_MIN };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        LONG r = swl(h, bad[i], 0x1000);
        CHECKF(r == 0, "idx %d -> %lx", bad[i], r);
        CHECKF(GetLastError() == E_INVALID_INDEX, "idx %d err %lu", bad[i], GetLastError());
    }
    WEQ(gwlp(h, GWLP_WNDPROC), proc);
    WEQ(gwlp(h, GWLP_HINSTANCE), inst);
    U(DestroyWindow)(h);
}

WT(long_set32_valid_negative_indexes_succeed) {
    REQUIRE(U(SetWindowLongW));
    NEED_WND(h, 0);
    SetLastError(0);
    CHECK(swl(h, GWL_USERDATA, 0x31) == 0);
    CHECK(swl(h, GWL_USERDATA, 0x32) == 0x31);   /* returns previous value */
    CHECK(gwl(h, GWL_USERDATA) == 0x32);
    U(DestroyWindow)(h);
}

/* ---------------- extra-byte indexes ------------------------------------------------------ */

WT(long_extra_roundtrip_32_all_slots) {
    NEED_WND(h, 0);
    for (int i = 0; i + 4 <= WPSQ_EXTRA; i += 4) {
        SetLastError(0);
        CHECKF(swl(h, i, 0xA0000000u | i) == 0, "slot %d initial", i);       /* extra bytes start zeroed */
    }
    for (int i = 0; i + 4 <= WPSQ_EXTRA; i += 4)
        CHECKF((DWORD)gwl(h, i) == (0xA0000000u | i), "slot %d", i);
    for (int i = 0; i + 4 <= WPSQ_EXTRA; i += 4)
        CHECKF((DWORD)swl(h, i, i) == (0xA0000000u | i), "slot %d old value", i);
    U(DestroyWindow)(h);
}

WT(long_extra_roundtrip_ptr_all_slots) {
    NEED_WND(h, 0);
    for (int i = 0; i + 8 <= WPSQ_EXTRA; i += 8) {
        CHECKF(swlp(h, i, 0x1122334400000000ll | i) == 0, "slot %d initial", i);
    }
    for (int i = 0; i + 8 <= WPSQ_EXTRA; i += 8)
        CHECKF(gwlp(h, i) == (LONG_PTR)(0x1122334400000000ll | i), "slot %d", i);
    U(DestroyWindow)(h);
}

WT(long_extra_boundary_last_valid_slot) {
    NEED_WND(h, 0);
    SetLastError(0);
    /* last LONG slot: idx + 4 == cbWndExtra; last LONG_PTR slot: idx + 8 == cbWndExtra */
    swl(h, WPSQ_EXTRA - 4, 0x5A5A5A5A);
    WEQ((DWORD)gwl(h, WPSQ_EXTRA - 4), 0x5A5A5A5A);
    swlp(h, WPSQ_EXTRA - 8, 0x0102030405060708ll);
    WEQ(gwlp(h, WPSQ_EXTRA - 8), 0x0102030405060708ll);
    U(DestroyWindow)(h);
}

WT(long_extra_out_of_range_32) {
    NEED_WND(h, 0);
    static const int bad[] = { WPSQ_EXTRA - 3, WPSQ_EXTRA - 2, WPSQ_EXTRA - 1, WPSQ_EXTRA, WPSQ_EXTRA + 1, WPSQ_EXTRA + 4, 1000, 0x10000, 0x7FFFFFF0, INT_MAX };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        LONG r = gwl(h, bad[i]);
        CHECKF(r == 0 && GetLastError() == E_INVALID_INDEX, "get idx %d -> %lx err %lu", bad[i], r, GetLastError());
        SetLastError(0);
        r = swl(h, bad[i], 1);
        CHECKF(r == 0 && GetLastError() == E_INVALID_INDEX, "set idx %d -> %lx err %lu", bad[i], r, GetLastError());
    }
    U(DestroyWindow)(h);
}

WT(long_extra_out_of_range_ptr) {
    NEED_WND(h, 0);
    static const int bad[] = { WPSQ_EXTRA - 7, WPSQ_EXTRA - 4, WPSQ_EXTRA - 1, WPSQ_EXTRA, WPSQ_EXTRA + 8, 1000, 0x10000, 0x7FFFFFF0, INT_MAX };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        LONG_PTR r = gwlp(h, bad[i]);
        CHECKF(r == 0 && GetLastError() == E_INVALID_INDEX, "get idx %d -> %llx err %lu", bad[i], (unsigned long long)r, GetLastError());
        SetLastError(0);
        r = swlp(h, bad[i], 1);
        CHECKF(r == 0 && GetLastError() == E_INVALID_INDEX, "set idx %d -> %llx err %lu", bad[i], (unsigned long long)r, GetLastError());
    }
    U(DestroyWindow)(h);
}

WT(long_extra_zero_cb_window_rejects_index0) {
    /* a class registered with cbWndExtra == 0: idx 0 fails the idx + 4 <= cb + ib test */
    REQUIRE(U(RegisterClassA)); REQUIRE(U(CreateWindowExA));
    char name[64]; snprintf(name, sizeof name, "win32ktWpsqZ%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = U(DefWindowProcA); wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = name;
    if (!U(RegisterClassA)(&wc)) { t_skip("RegisterClass failed"); return; }
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, name, "z", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (h) {
        SetLastError(0); WEQ(gwl(h, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
        SetLastError(0); WEQ(swl(h, 0, 5), 0); EXPECT_ERR(E_INVALID_INDEX);
        SetLastError(0); WEQ(gwlp(h, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
        SetLastError(0); WEQ(swlp(h, 0, 5), 0); EXPECT_ERR(E_INVALID_INDEX);
        U(DestroyWindow)(h);
    } else CHECK(0);
    U(UnregisterClassA)(name, wc.hInstance);
}

WT(long_extra_unaligned_offsets) {
    NEED_WND(h, 0);
    /* SetWindowLong only checks idx + 4 <= cb: there is no alignment requirement */
    for (int i = 1; i <= 3; i++) {
        SetLastError(0);
        swl(h, 8, 0);
        swl(h, 12, 0);
        swl(h, 8 + i, 0xCAFEBABE);
        WEQ((DWORD)gwl(h, 8 + i), 0xCAFEBABE);
    }
    U(DestroyWindow)(h);
}

WT(long_extra_ptr_overlaps_two_32bit_slots) {
    NEED_WND(h, 0);
    swlp(h, 16, 0x1122334455667788ll);
    WEQ((DWORD)gwl(h, 16), 0x55667788);      /* little endian halves of the same bytes */
    WEQ((DWORD)gwl(h, 20), 0x11223344);
    swl(h, 20, 0xDEADBEEF);
    WEQ((ULONG_PTR)gwlp(h, 16), 0xDEADBEEF55667788ull);
    U(DestroyWindow)(h);
}

WT(long_extra_windows_are_independent) {
    NEED_WND(a, 0);
    HWND b = wpsq_mk(0, "b");
    if (!b) { t_skip("no second window"); U(DestroyWindow)(a); return; }
    swl(a, 0, 111); swl(b, 0, 222);
    WEQ(gwl(a, 0), 111);
    WEQ(gwl(b, 0), 222);
    U(DestroyWindow)(a);
    WEQ(gwl(b, 0), 222);
    U(DestroyWindow)(b);
}

WT(long_extra_ansi_and_unicode_calls_share_storage) {
    REQUIRE(U(GetWindowLongA)); REQUIRE(U(SetWindowLongA)); REQUIRE(U(SetWindowLongPtrA)); REQUIRE(U(GetWindowLongPtrA));
    NEED_WND(h, 0);
    /* extra bytes are untranslated data: SetWindowLongA stores the same bits SetWindowLongW does */
    U(SetWindowLongA)(h, 8, 0x600DF00D);
    WEQ((DWORD)U(GetWindowLongW)(h, 8), 0x600DF00D);
    U(SetWindowLongPtrA)(h, 16, 0x0BADC0DE12345678ll);
    WEQ((ULONG_PTR)U(GetWindowLongPtrW)(h, 16), 0x0BADC0DE12345678ull);
    WEQ((ULONG_PTR)U(GetWindowLongPtrA)(h, 16), 0x0BADC0DE12345678ull);
    U(DestroyWindow)(h);
}

/* ---------------- direct syscall stubs ---------------------------------------------------- */

WT(long_direct_set_window_long_extra) {
    REQUIRE(UN(NtUserSetWindowLong)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    SetLastError(0);
    /* (hwnd, index, value, bAnsi): bAnsi only matters for the WNDPROC arm */
    WEQ(UN(NtUserSetWindowLong)(h, 4, 0x1234, 0), 0);
    WEQ(UN(NtUserSetWindowLong)(h, 4, 0x4321, 1), 0x1234);
    WEQ((DWORD)gwl(h, 4), 0x4321);
    U(DestroyWindow)(h);
}

WT(long_direct_set_window_long_ptr_extra) {
    REQUIRE(UN(NtUserSetWindowLongPtr)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    WEQ(UN(NtUserSetWindowLongPtr)(h, 8, 0x1122334455667788ll, 0), 0);
    WEQ(UN(NtUserSetWindowLongPtr)(h, 8, 0, 0), 0x1122334455667788ll);
    WEQ(gwlp(h, 8), 0);
    U(DestroyWindow)(h);
}

WT(long_direct_stub_bounds_match_userland) {
    REQUIRE(UN(NtUserSetWindowLong)); REQUIRE(UN(NtUserSetWindowLongPtr)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    /* kernel re-checks the bounds that user32 checks (SetWindowLong: idx + 4 <= ibExtra + cbExtra) */
    SetLastError(0);
    WEQ(UN(NtUserSetWindowLong)(h, WPSQ_EXTRA - 3, 1, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
    SetLastError(0);
    WEQ(UN(NtUserSetWindowLong)(h, WPSQ_EXTRA, 1, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
    SetLastError(0);
    WEQ(UN(NtUserSetWindowLongPtr)(h, WPSQ_EXTRA - 7, 1, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
    SetLastError(0);
    WEQ(UN(NtUserSetWindowLong)(h, INT_MAX, 1, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
    if (!t_is_wine()) {   /* Wine's own win32u would treat the value as a window procedure */
        SetLastError(0);
        WEQ(UN(NtUserSetWindowLong)(h, GWL_WNDPROC, 1, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
    }
    SetLastError(0);
    WEQ(UN(NtUserSetWindowLongPtr)(h, -3, 1, 0), 0); EXPECT_ERR(E_INVALID_INDEX);
    U(DestroyWindow)(h);
}

WT(long_direct_set_userdata_and_get_state) {
    REQUIRE(UN(NtUserSetWindowLongPtr)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    UN(NtUserSetWindowLongPtr)(h, GWLP_USERDATA, 0xABCDEF, 0);
    WEQ(gwlp(h, GWLP_USERDATA), 0xABCDEF);
    ULONG_PTR raw = 0;
    if (wpsq_wnd(h)) { CHECK(wpsq_rd(h, WND_userdata, &raw, 8)); WEQ(raw, 0xABCDEF); }
    U(DestroyWindow)(h);
}

WT(long_set_window_word_extra) {
    REQUIRE(U(SetWindowWord)); REQUIRE(U(GetWindowWord));
    NEED_WND(h, 0);
    SetLastError(0);
    WEQ(U(SetWindowWord)(h, 6, 0xBEEF), 0);
    WEQ(U(GetWindowWord)(h, 6), 0xBEEF);
    WEQ(U(SetWindowWord)(h, 6, 0x1234), 0xBEEF);       /* returns previous 16-bit value */
    WEQ(U(GetWindowWord)(h, 6), 0x1234);
    WEQ(U(GetWindowWord)(h, 8), 0);                     /* neighbours untouched */
    WEQ((DWORD)gwl(h, 4), 0x12340000);                  /* bytes 6,7 of the dword at 4 */
    U(DestroyWindow)(h);
}

WT(long_window_word_bounds) {
    REQUIRE(U(SetWindowWord)); REQUIRE(U(GetWindowWord));
    NEED_WND(h, 0);
    /* last word slot is idx 62 (idx + 2 <= 64); 63 straddles the end */
    SetLastError(0);
    U(SetWindowWord)(h, WPSQ_EXTRA - 2, 0x7777);
    WEQ(U(GetWindowWord)(h, WPSQ_EXTRA - 2), 0x7777);
    SetLastError(0);
    WEQ(U(SetWindowWord)(h, WPSQ_EXTRA - 1, 1), 0); EXPECT_ERR(E_INVALID_INDEX);
    SetLastError(0);
    WEQ(U(SetWindowWord)(h, WPSQ_EXTRA, 1), 0); EXPECT_ERR(E_INVALID_INDEX);
    SetLastError(0);
    WEQ(U(GetWindowWord)(h, WPSQ_EXTRA), 0); EXPECT_ERR(E_INVALID_INDEX);
    SetLastError(0);
    WEQ(U(GetWindowWord)(h, 100000), 0); EXPECT_ERR(E_INVALID_INDEX);
    U(DestroyWindow)(h);
}

WT(long_window_word_userdata_rebuilds_low_dword) {
    REQUIRE(U(SetWindowWord)); REQUIRE(U(GetWindowWord));
    NEED_WND(h, 0);
    swlp(h, GWLP_USERDATA, 0x1122334455667788ll);
    /* SetWindowWord(-21) keeps the high 16 bits of the old low dword, puts the new word below them and clears the high dword */
    WEQ(U(SetWindowWord)(h, GWL_USERDATA, 0xAAAA), 0x7788);
    WEQ((ULONG_PTR)gwlp(h, GWLP_USERDATA), 0x5566AAAAull);  /* high 16 bits of old low dword kept, new word below, high dword cleared */
    WEQ(U(GetWindowWord)(h, GWL_USERDATA), 0xAAAA);
    U(DestroyWindow)(h);
}

WT(long_window_word_invalid_hwnd) {
    REQUIRE(U(SetWindowWord)); REQUIRE(U(GetWindowWord));
    WEQ(U(GetWindowWord)(GARBAGE_HWND, 0), 0);
    WEQ(U(GetWindowWord)(NULL, GWL_USERDATA), 0);
    SetLastError(0);
    WEQ(U(SetWindowWord)(GARBAGE_HWND, 0, 5), 0);
    EXPECT_ERR(E_INVALID_HWND);
}

/* ---------------- value semantics of the standard indexes -------------------------------- */

WT(long_userdata_roundtrip_and_return_old) {
    NEED_WND(h, 0);
    WEQ(gwlp(h, GWLP_USERDATA), 0);              /* fresh window: zero */
    WEQ(swlp(h, GWLP_USERDATA, 0xAA), 0);
    WEQ(swlp(h, GWLP_USERDATA, 0xBB), 0xAA);
    WEQ(swlp(h, GWLP_USERDATA, 0), 0xBB);
    WEQ(gwlp(h, GWLP_USERDATA), 0);
    U(DestroyWindow)(h);
}

WT(long_userdata_truncation_32bit) {
    NEED_WND(h, 0);
    swlp(h, GWLP_USERDATA, 0x1122334455667788ll);
    WEQ((DWORD)gwl(h, GWL_USERDATA), 0x55667788);       /* GetWindowLong returns the low dword */
    swl(h, GWL_USERDATA, (LONG)0xA1B2C3D4);
    WEQ((DWORD)gwlp(h, GWLP_USERDATA), 0xA1B2C3D4);    /* low dword of the stored qword */
    WEQ((DWORD)gwl(h, GWL_USERDATA), 0xA1B2C3D4);
    U(DestroyWindow)(h);
}

WT(long_userdata_pointer_values_survive) {
    NEED_WND(h, 0);
    static const LONG_PTR v[] = { 0x7FFFFFFFFFFFFFFFll, (LONG_PTR)0x8000000000000000ull, -1, 0x00007FFF00000000ll, 0xFFFF800000000000ull };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        swlp(h, GWLP_USERDATA, v[i]);
        WEQ(gwlp(h, GWLP_USERDATA), v[i]);
    }
    U(DestroyWindow)(h);
}

WT(long_userdata_independent_from_extra_bytes) {
    NEED_WND(h, 0);
    swlp(h, GWLP_USERDATA, 0xCC);
    swlp(h, 0, 0xDD);
    WEQ(gwlp(h, GWLP_USERDATA), 0xCC);
    WEQ(gwlp(h, 0), 0xDD);
    U(DestroyWindow)(h);
}

WT(long_hinstance_roundtrip) {
    NEED_WND(h, 0);
    HINSTANCE self = GetModuleHandleA(NULL);
    WEQ(gwlp(h, GWLP_HINSTANCE), (LONG_PTR)self);
    LONG_PTR old = swlp(h, GWLP_HINSTANCE, 0x10000);          /* SetWindowLongPtr(-6) stores the raw value */
    WEQ(old, (LONG_PTR)self);
    WEQ(gwlp(h, GWLP_HINSTANCE), 0x10000);
    swlp(h, GWLP_HINSTANCE, old);
    WEQ(gwlp(h, GWLP_HINSTANCE), (LONG_PTR)self);
    U(DestroyWindow)(h);
}

WT(long_hinstance_user32_getmodulename_follows_field) {
    REQUIRE(U(GetWindowModuleFileNameW)); REQUIRE(U(GetModuleFileNameW));
    NEED_WND(h, 0);
    WCHAR a[MAX_PATH] = {0}, b[MAX_PATH] = {0};
    /* GetWindowModuleFileNameW = GetModuleFileNameW(pwnd->hModule) */
    UINT n = U(GetWindowModuleFileNameW)(h, a, MAX_PATH);
    DWORD m = U(GetModuleFileNameW)(NULL, b, MAX_PATH);
    CHECK(n > 0);
    WEQ(n, m);
    CHECK(!lstrcmpiW(a, b));
    U(DestroyWindow)(h);
}

WT(long_hwndparent_toplevel_is_zero) {
    NEED_WND(h, 0);
    WEQ(gwlp(h, GWLP_HWNDPARENT), 0);      /* unowned popup: GetWindowData(-8) resolves to no owner */
    U(DestroyWindow)(h);
}

WT(long_hwndparent_child_is_parent) {
    NEED_WND(par, 0);
    HWND c = wpsq_mkchild(par, 1);
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    WEQ(gwlp(c, GWLP_HWNDPARENT), (LONG_PTR)par);
    WEQ((HWND)U(GetParent)(c), par);
    U(DestroyWindow)(par);
}

WT(long_hwndparent_set_owner_on_toplevel) {
    NEED_WND(a, 0);
    HWND b = wpsq_mk(0, "owner");
    if (!b) { t_skip("no 2nd window"); U(DestroyWindow)(a); return; }
    /* SetWindowLongPtr(-8) on a top-level: assigns the owner; GetWindow(GW_OWNER) and GWLP_HWNDPARENT agree */
    LONG_PTR old = swlp(a, GWLP_HWNDPARENT, (LONG_PTR)b);
    WEQ(old, 0);
    WEQ(gwlp(a, GWLP_HWNDPARENT), (LONG_PTR)b);
    WEQ((HWND)U(GetWindow)(a, GW_OWNER), b);
    swlp(a, GWLP_HWNDPARENT, 0);
    WEQ(gwlp(a, GWLP_HWNDPARENT), 0);
    U(DestroyWindow)(a);
    U(DestroyWindow)(b);
}

WT(long_hwndparent_invalid_owner_fails) {
    NEED_WND(a, 0);
    SetLastError(0);
 /* SetWindowLongPtr(-8): ValidateHwnd fails and argument 3 != 0 -> UserSetLastError(0x57) */
    LONG_PTR r = swlp(a, GWLP_HWNDPARENT, (LONG_PTR)GARBAGE_HWND);
    WEQ(r, 0);
    EXPECT_ERR(E_INVALID_PARAM);
    WEQ(gwlp(a, GWLP_HWNDPARENT), 0);
    U(DestroyWindow)(a);
}

WT(long_id_child_roundtrip) {
    NEED_WND(par, 0);
    HWND c = wpsq_mkchild(par, 7);
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    static const LONG_PTR ids[] = { 0, 1, 0x1234, 0xFFFF, 0x7FFFFFFF, 0x123456789ll };
    LONG_PTR prev = 7;
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        WEQ(swlp(c, GWLP_ID, ids[i]), prev);
        WEQ(gwlp(c, GWLP_ID), ids[i]);
        prev = ids[i];
    }
    swl(c, GWL_ID, 0x4242);
    WEQ(gwl(c, GWL_ID), 0x4242);
    WEQ(U(GetDlgCtrlID)(c), 0x4242);    /* GetDlgCtrlID reads the same field */
    U(DestroyWindow)(par);
}

WT(long_id_toplevel_without_menu_is_zero) {
    NEED_WND(h, 0);
    WEQ(gwlp(h, GWLP_ID), 0);      /* non-child: the value is the menu handle (none) */
    U(DestroyWindow)(h);
}

WT(long_id_toplevel_menu_handle_roundtrip) {
    REQUIRE(U(CreateMenu)); REQUIRE(U(DestroyMenu)); REQUIRE(U(GetMenu));
    NEED_WND(h, 0);
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("CreateMenu failed"); U(DestroyWindow)(h); return; }
    /* SetWindowLongPtr(-12) on a non-child: menu handle validation and locking, so GWLP_ID is the HMENU */
    WEQ(swlp(h, GWLP_ID, (LONG_PTR)m), 0);
    WEQ(gwlp(h, GWLP_ID), (LONG_PTR)m);
    WEQ((HMENU)U(GetMenu)(h), m);
    WEQ(swlp(h, GWLP_ID, 0), (LONG_PTR)m);
    WEQ(gwlp(h, GWLP_ID), 0);
    WEQ((HMENU)U(GetMenu)(h), NULL);
    U(DestroyMenu)(m);
    U(DestroyWindow)(h);
}

/* ---------------- GWL_STYLE / GWL_EXSTYLE ------------------------------------------------- */

WT(long_style_get_matches_createwindow) {
    HWND h = wpsq_mkex(0, WS_POPUP | WS_DISABLED | WS_CLIPCHILDREN | WS_BORDER, WS_EX_ACCEPTFILES, NULL, "st", 0);
    if (!h) { t_skip("no window"); return; }
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
    CHECKF((st & (WS_POPUP | WS_DISABLED | WS_CLIPCHILDREN | WS_BORDER)) == (WS_POPUP | WS_DISABLED | WS_CLIPCHILDREN | WS_BORDER), "style %08lx", st);
    CHECK(!(st & WS_VISIBLE));
    CHECK(!(st & WS_CHILD));
    CHECK(!U(IsWindowVisible)(h));
    CHECK(!U(IsWindowEnabled)(h));
    U(DestroyWindow)(h);
}

WT(long_style_ptr_and_32_agree) {
    NEED_WND(h, 0);
    WEQ((DWORD)gwlp(h, GWL_STYLE), (DWORD)gwl(h, GWL_STYLE));
    WEQ((DWORD)gwlp(h, GWL_EXSTYLE), (DWORD)gwl(h, GWL_EXSTYLE));
    U(DestroyWindow)(h);
}

WT(long_style_set_tabstop_group_roundtrip) {
    NEED_WND(h, 0);
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
    DWORD ret = (DWORD)swl(h, GWL_STYLE, st | WS_TABSTOP | WS_GROUP);
    WEQ(ret, st);                                  /* returns the previous style */
    DWORD now = (DWORD)gwl(h, GWL_STYLE);
    CHECK((now & (WS_TABSTOP | WS_GROUP)) == (WS_TABSTOP | WS_GROUP));
    swl(h, GWL_STYLE, st);
    WEQ((DWORD)gwl(h, GWL_STYLE), st);
    U(DestroyWindow)(h);
}

WT(long_style_set_disabled_changes_enabled_state) {
    REQUIRE(U(IsWindowEnabled));
    NEED_WND(h, 0);
    CHECK(U(IsWindowEnabled)(h));
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
    swl(h, GWL_STYLE, st | WS_DISABLED);
    CHECK(!U(IsWindowEnabled)(h));            /* IsWindowEnabled reads WS_DISABLED from the same style word */
    CHECK((DWORD)gwl(h, GWL_STYLE) & WS_DISABLED);
    swl(h, GWL_STYLE, st);
    CHECK(U(IsWindowEnabled)(h));
    U(DestroyWindow)(h);
}

WT(long_style_ptr_setter_ignores_high_dword) {
    NEED_WND(h, 0);
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
 /* SetWindowLongPtr passes argument 3 & 0xffffffff to the style change */
    swlp(h, GWL_STYLE, (LONG_PTR)(0xFFFFFFFF00000000ull | (st | WS_TABSTOP)));
    DWORD now = (DWORD)gwl(h, GWL_STYLE);
    CHECK(now & WS_TABSTOP);
    WEQ(now & WS_POPUP, WS_POPUP);
    swl(h, GWL_STYLE, st);
    U(DestroyWindow)(h);
}

WT(long_style_stylechanging_message) {
    NEED_WND(h, 0);
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
    wpsq_log_reset(); g_wlog_on = 1;
    swl(h, GWL_STYLE, st | WS_TABSTOP);
    g_wlog_on = 0;
    /* the style change: SendMessage(hwnd, WM_STYLECHANGING=0x7c, index, &STYLESTRUCT{old,new}) when the window asks for it */
    CHECK(wpsq_log_count(WM_STYLECHANGING) == 1);
    CHECK(wpsq_log_count(WM_STYLECHANGED) == 1);
    struct wpsq_log *e = wpsq_log_find(WM_STYLECHANGING, 0);
    if (e) WEQ((LONG)e->w, GWL_STYLE);
    e = wpsq_log_find(WM_STYLECHANGED, 0);
    if (e) WEQ((LONG)e->w, GWL_STYLE);
    swl(h, GWL_STYLE, st);
    U(DestroyWindow)(h);
}

WT(long_style_stylechanging_can_rewrite_new_style) {
    NEED_WND(h, 0);
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
    /* the new style is re-read from the STYLESTRUCT after the WM_STYLECHANGING send (uStack_8c) */
    g_style_force_idx = GWL_STYLE;
    g_style_force_val = (LONG)(st | WS_GROUP);
    g_style_force_on = 1;
    swl(h, GWL_STYLE, st | WS_TABSTOP);
    g_style_force_on = 0;
    DWORD now = (DWORD)gwl(h, GWL_STYLE);
    CHECK(now & WS_GROUP);
    CHECK(!(now & WS_TABSTOP));
    swl(h, GWL_STYLE, st);
    U(DestroyWindow)(h);
}

WT(long_style_stylechanged_struct_has_old_and_new) {
    NEED_WND(h, 0);
    DWORD st = (DWORD)gwl(h, GWL_STYLE);
    wpsq_log_reset(); g_wlog_on = 1;
    swl(h, GWL_STYLE, st | WS_TABSTOP);
    g_wlog_on = 0;
    struct wpsq_log *e = wpsq_log_find(WM_STYLECHANGING, 0);
    CHECK(e != NULL);
    swl(h, GWL_STYLE, st);
    U(DestroyWindow)(h);
}

WT(long_exstyle_set_accept_files) {
    NEED_WND(h, 0);
    DWORD ex = (DWORD)gwl(h, GWL_EXSTYLE);
    CHECK(!(ex & WS_EX_ACCEPTFILES));
    WEQ((DWORD)swl(h, GWL_EXSTYLE, ex | WS_EX_ACCEPTFILES), ex);
    CHECK((DWORD)gwl(h, GWL_EXSTYLE) & WS_EX_ACCEPTFILES);
    swl(h, GWL_EXSTYLE, ex);
    CHECK(!((DWORD)gwl(h, GWL_EXSTYLE) & WS_EX_ACCEPTFILES));
    U(DestroyWindow)(h);
}

WT(long_exstyle_changing_message_index) {
    NEED_WND(h, 0);
    DWORD ex = (DWORD)gwl(h, GWL_EXSTYLE);
    wpsq_log_reset(); g_wlog_on = 1;
    swl(h, GWL_EXSTYLE, ex | WS_EX_CONTEXTHELP);
    g_wlog_on = 0;
    struct wpsq_log *e = wpsq_log_find(WM_STYLECHANGING, 0);
    CHECK(e != NULL);
    if (e) WEQ((LONG)e->w, GWL_EXSTYLE);
    swl(h, GWL_EXSTYLE, ex);
    U(DestroyWindow)(h);
}

WT(long_exstyle_toolwindow_noactivate_preserved) {
    NEED_WND(h, 0);
    DWORD ex = (DWORD)gwl(h, GWL_EXSTYLE);
    CHECK(ex & WS_EX_TOOLWINDOW);
    CHECK(ex & WS_EX_NOACTIVATE);
    U(DestroyWindow)(h);
}

WT(long_exstyle_unsupported_bits_not_reported) {
    NEED_WND(h, 0);
    /* GetWindowData(-0x14) masks with 0xa7777ff (plus WS_EX_LAYERED-bit rule) so unrelated bits never leak */
    DWORD ex = (DWORD)gwl(h, GWL_EXSTYLE);
    CHECKF((ex & ~(0x0A7777FFu | 0x00080000u)) == 0, "exstyle %08lx has bits outside the public mask", ex);
    U(DestroyWindow)(h);
}

WT(long_style_child_flag_visible) {
    NEED_WND(par, 0);
    HWND c = wpsq_mkchild(par, 1);
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    DWORD st = (DWORD)gwl(c, GWL_STYLE);
    CHECK(st & WS_CHILD);
    CHECK(!(st & WS_POPUP));
    U(DestroyWindow)(par);
}

WT(long_style_cross_thread_same_process) {
    struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "xt")) { t_skip("owner thread failed"); return; }
    DWORD st = (DWORD)gwl(o.hwnd, GWL_STYLE);
    swl(o.hwnd, GWL_STYLE, st | WS_TABSTOP);
    CHECK((DWORD)gwl(o.hwnd, GWL_STYLE) & WS_TABSTOP);
    swl(o.hwnd, GWL_STYLE, st);
    wpsq_owner_stop(&o);
}

/* ---------------- window procedures ------------------------------------------------------- */

WT(long_wndproc_unicode_window_returns_class_proc) {
    NEED_WND(h, 0);
    LONG_PTR p = gwlp(h, GWLP_WNDPROC);
    WEQ(p, (LONG_PTR)wpsq_procW);     /* same-thread, unicode: GetWindowData(-4) unmaps to the registered pointer */
    CHECK(U(IsWindowUnicode)(h));
    U(DestroyWindow)(h);
}

WT(long_wndproc_ansi_window_returns_class_proc) {
    REQUIRE(U(GetWindowLongPtrA));
    NEED_WND(h, 1);
    LONG_PTR p = U(GetWindowLongPtrA)(h, GWLP_WNDPROC);   /* the W getter on an ANSI window yields a call-proc handle */
    WEQ(p, (LONG_PTR)wpsq_procA);
    CHECK(!U(IsWindowUnicode)(h));
    U(DestroyWindow)(h);
}

WT(long_wndproc_other_encoding_returns_callproc_handle) {
    REQUIRE(U(GetWindowLongPtrA));
    NEED_WND(h, 0);
    LONG_PTR w = gwlp(h, GWLP_WNDPROC);
    LONG_PTR a = U(GetWindowLongPtrA)(h, GWLP_WNDPROC);
    /* GetWindowData(-4) with a mismatching charset asks NtUserGetCPD for a call-proc-data object */
    CHECK(a != 0);
    CHECK(a != w);
    /* SetWindowLongPtr(-4) recognises call-proc handles by (low dword & 0xffff0000) == 0xffff0000 */
    CHECKF(((DWORD)a & 0xFFFF0000u) == 0xFFFF0000u, "cpd %p", (void *)a);
    LONG_PTR a2 = U(GetWindowLongPtrA)(h, GWLP_WNDPROC);
    WEQ(a, a2);                         /* the CPD is cached per (proc, charset) */
    struct k_user_entry e;
    if (k_user_entry((HANDLE)(ULONG_PTR)a, &e)) {
        t_note("callproc handle %p: entry type %u", (void *)a, e.type);
    }
    LONG_PTR w2 = gwlp(h, GWLP_WNDPROC);
    WEQ(w2, w);
    U(DestroyWindow)(h);
}

WT(long_wndproc_callproc_handle_translates_settext) {
    REQUIRE(U(GetWindowLongPtrA)); REQUIRE(U(CallWindowProcA)); REQUIRE(U(GetWindowTextW));
    NEED_WND(h, 0);
    WNDPROC pa = (WNDPROC)U(GetWindowLongPtrA)(h, GWLP_WNDPROC);
    CHECK(pa != NULL);
    if (!pa) { U(DestroyWindow)(h); return; }
    wpsq_log_reset(); g_wlog_on = 1;
    /* ANSI caller -> unicode wndproc: the call-proc object converts the lParam string to UTF-16 */
    LRESULT r = U(CallWindowProcA)(pa, h, WM_SETTEXT, 0, (LPARAM)"callproc");
    g_wlog_on = 0;
    CHECK(r != 0);
    struct wpsq_log *e = wpsq_log_find(WM_SETTEXT, 0);
    if (e) { CHECK(e->head[0] == 'c' && e->head[1] == 0 && e->head[2] == 'a' && e->head[3] == 0); }
    else CHECK(0);
    WCHAR buf[32] = {0};
    U(GetWindowTextW)(h, buf, 32);
    CHECK(!lstrcmpW(buf, L"callproc"));
    U(DestroyWindow)(h);
}

WT(long_wndproc_callproc_handle_translates_gettext_for_ansi_caller) {
    REQUIRE(U(GetWindowLongPtrA)); REQUIRE(U(CallWindowProcA));
    NEED_WND(h, 0);
    WNDPROC pa = (WNDPROC)U(GetWindowLongPtrA)(h, GWLP_WNDPROC);
    if (!pa) { CHECK(0); U(DestroyWindow)(h); return; }
    U(SetWindowTextW)(h, L"convé");
    char buf[32]; memset(buf, 0x7f, sizeof buf);
    LRESULT n = U(CallWindowProcA)(pa, h, WM_GETTEXT, sizeof buf, (LPARAM)buf);
    CHECK(n >= 4);
    CHECK(!strncmp(buf, "conv", 4));
    CHECK(buf[n] == 0);
    U(DestroyWindow)(h);
}

WT(long_wndproc_set_ansi_proc_flips_unicode_flag) {
    REQUIRE(U(SetWindowLongPtrA)); REQUIRE(U(IsWindowUnicode));
    NEED_WND(h, 0);
    CHECK(U(IsWindowUnicode)(h));
    unsigned char b = 0;
    int have = wpsq_rd(h, WND_state2b, &b, 1);
    if (have) CHECK((b & 8) == 0);
    LONG_PTR old = U(SetWindowLongPtrA)(h, GWLP_WNDPROC, (LONG_PTR)wpsq_procA);
    /* the previous unicode proc is handed back in the caller's (ANSI) encoding, i.e. possibly as a call-proc handle */
    CHECK(old != 0);
    /* SetWindowLongPtr(-4): SetOrClrWF(bAnsi != 0, pwnd, 0x208) marks the window ANSI */
    CHECK(!U(IsWindowUnicode)(h));
    if (have && wpsq_rd(h, WND_state2b, &b, 1)) CHECK(b & 8);
    WEQ(U(GetWindowLongPtrA)(h, GWLP_WNDPROC), (LONG_PTR)wpsq_procA);   /* the W getter would get a call-proc handle */
    U(DestroyWindow)(h);
}

WT(long_wndproc_set_unicode_proc_restores_unicode_flag) {
    REQUIRE(U(SetWindowLongPtrA));
    NEED_WND(h, 0);
    U(SetWindowLongPtrA)(h, GWLP_WNDPROC, (LONG_PTR)wpsq_procA);
    CHECK(!U(IsWindowUnicode)(h));
    LONG_PTR old = swlp(h, GWLP_WNDPROC, (LONG_PTR)wpsq_procW);
    (void)old;
    CHECK(U(IsWindowUnicode)(h));
    U(DestroyWindow)(h);
}

WT(long_wndproc_ansi_proc_receives_ansi_settext) {
    REQUIRE(U(SetWindowLongPtrA)); REQUIRE(U(SetWindowTextW));
    NEED_WND(h, 0);
    U(SetWindowLongPtrA)(h, GWLP_WNDPROC, (LONG_PTR)wpsq_procA);
    wpsq_log_reset(); g_wlog_on = 1;
    U(SetWindowTextW)(h, L"ab");
    g_wlog_on = 0;
    struct wpsq_log *e = wpsq_log_find(WM_SETTEXT, 0);
    /* a proc installed with SetWindowLongPtrA is called with ANSI text: 'a','b',0 */
    CHECK(e != NULL);
    if (e) CHECK(e->head[0] == 'a' && e->head[1] == 'b' && e->head[2] == 0);
    U(DestroyWindow)(h);
}

WT(long_wndproc_unicode_proc_receives_wide_settext) {
    REQUIRE(U(SetWindowTextA));
    NEED_WND(h, 0);
    wpsq_log_reset(); g_wlog_on = 1;
    U(SetWindowTextA)(h, "ab");
    g_wlog_on = 0;
    struct wpsq_log *e = wpsq_log_find(WM_SETTEXT, 0);
    CHECK(e != NULL);
    if (e) CHECK(e->head[0] == 'a' && e->head[1] == 0 && e->head[2] == 'b' && e->head[3] == 0);
    U(DestroyWindow)(h);
}

WT(long_wndproc_set_returns_previous_and_cpd_roundtrip) {
    REQUIRE(U(GetWindowLongPtrA)); REQUIRE(U(SetWindowLongPtrA));
    NEED_WND(h, 0);
    LONG_PTR cpd = U(GetWindowLongPtrA)(h, GWLP_WNDPROC);    /* ANSI view of the unicode proc */
    LONG_PTR old = U(SetWindowLongPtrA)(h, GWLP_WNDPROC, cpd);
    /* handing back the call-proc handle must restore the underlying unicode proc rather than nest */
    CHECK(old != 0);
    CHECK(U(IsWindowUnicode)(h));
    WEQ(gwlp(h, GWLP_WNDPROC), (LONG_PTR)wpsq_procW);
    U(DestroyWindow)(h);
}

static volatile int g_sub_hits;
static WNDPROC g_sub_prev;
static LRESULT CALLBACK sub_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_USER + 5) { g_sub_hits++; return 0x5151; }
    return U(CallWindowProcW)(g_sub_prev, h, m, w, l);
}

WT(long_wndproc_subclass_and_unsubclass) {
    REQUIRE(U(CallWindowProcW)); REQUIRE(U(SendMessageW));
    NEED_WND(h, 0);
    g_sub_hits = 0;
    g_sub_prev = (WNDPROC)swlp(h, GWLP_WNDPROC, (LONG_PTR)sub_proc);
    WEQ((LONG_PTR)g_sub_prev, (LONG_PTR)wpsq_procW);
    WEQ(gwlp(h, GWLP_WNDPROC), (LONG_PTR)sub_proc);
    WEQ(U(SendMessageW)(h, WM_USER + 5, 0, 0), 0x5151);
    WEQ(g_sub_hits, 1);
    WEQ(swlp(h, GWLP_WNDPROC, (LONG_PTR)g_sub_prev), (LONG_PTR)sub_proc);
    WEQ(gwlp(h, GWLP_WNDPROC), (LONG_PTR)wpsq_procW);
    WEQ(U(SendMessageW)(h, WM_USER + 5, 0, 0), 0);   /* DefWindowProc ignores WM_USER+5 */
    U(DestroyWindow)(h);
}

WT(long_wndproc_set_cross_thread_same_process_allowed) {
    struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "wp")) { t_skip("owner thread failed"); return; }
    /* SetWindowLongPtr(-4) only requires the caller's process to own the window, not its thread */
    LONG_PTR old = swlp(o.hwnd, GWLP_WNDPROC, (LONG_PTR)wpsq_procW);
    WEQ(old, (LONG_PTR)wpsq_procW);
    LONG_PTR cur = gwlp(o.hwnd, GWLP_WNDPROC);
    WEQ(cur, (LONG_PTR)wpsq_procW);
    wpsq_owner_stop(&o);
}

WT(long_wndproc_32bit_setter_never_replaces_proc) {
    REQUIRE(U(SetWindowLongA));
    NEED_WND(h, 0);
    SetLastError(0);
    LONG r = U(SetWindowLongA)(h, GWL_WNDPROC, (LONG)(ULONG_PTR)wpsq_procA);
    WEQ(r, 0);
    EXPECT_ERR(E_INVALID_INDEX);
    WEQ(gwlp(h, GWLP_WNDPROC), (LONG_PTR)wpsq_procW);
    U(DestroyWindow)(h);
}

/* ---------------- dialog-class windows (DLGWINDOWEXTRA) ---------------------------------- */

static HWND mk_dialog(void) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"#32770", L"dlg", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
}

WT(long_dialog_msgresult_and_user_roundtrip) {
    REQUIRE(U(CreateWindowExW));
    HWND h = mk_dialog();
    if (!h) { t_skip("no dialog-class window (err %lu)", GetLastError()); return; }
    /* DWLP_MSGRESULT (0) and DWLP_USER (0x10) are the extra slots SetWindowLong lets user code touch on a window
     * with a PDIALOG; whether a bare #32770 window already owns one is an implementation detail, so observe only. */
    SetLastError(0);
    LONG_PTR o1 = swlp(h, DWLP_MSGRESULT, 0x77);
    t_note("SetWindowLongPtr(dlg, DWLP_MSGRESULT) -> %p err %lu, readback %p", (void *)o1, GetLastError(), (void *)gwlp(h, DWLP_MSGRESULT));
    SetLastError(0);
    LONG_PTR o2 = swlp(h, DWLP_USER, 0x99);
    t_note("SetWindowLongPtr(dlg, DWLP_USER) -> %p err %lu, readback %p", (void *)o2, GetLastError(), (void *)gwlp(h, DWLP_USER));
    U(DestroyWindow)(h);
}

WT(long_dialog_reserved_index_errors) {
    REQUIRE(U(CreateWindowExW));
    HWND h = mk_dialog();
    if (!h) { t_skip("no dialog-class window"); return; }
    /* the getters never write; _GetWindowLong dialog branch: idx 8 on the 32-bit getter -> 0x585, other 0<idx<0x1e (not 0x10) -> 0x587 */
    SetLastError(0);
    LONG r = gwl(h, DWLP_DLGPROC);
    WEQ(r, 0);
    t_note("GetWindowLongW(dlg, DWLP_DLGPROC) last error %lu", GetLastError());
    static const int bad[] = { 4, 12, 20, 24, 28 };
    for (unsigned i = 0; i < 5; i++) {
        SetLastError(0);
        LONG_PTR p = gwlp(h, bad[i]);
        CHECKF(p == 0, "idx %d -> %llx", bad[i], (unsigned long long)p);
        t_note("GetWindowLongPtrW(dlg, %d) err %lu", bad[i], GetLastError());
    }
    U(DestroyWindow)(h);
}
