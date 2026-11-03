/* State-changing gap syscalls: NtGdiMakeObjectXferable (marks a DC's GDI cell as transferable to another process) and
 * NtGdiSetUMPDSandboxState (process-wide printer-driver sandbox flag, set in a child). */
#include "gap0.h"

TEST(dx_gap0_xferable_foreign_pid_marks_cell_and_unxferable_clears) {
    REQUIRE(UN(NtGdiMakeObjectXferable)); REQUIRE(UN(NtGdiMakeObjectUnXferable));
    G0FX(f, 2, 2);
    int fl0 = g0_cell_u8(f.dc, CELL_FLAGS);
    /* a pid other than our own is accepted and the DC is marked transferable */
    EXPECT_EQ(UN(NtGdiMakeObjectXferable)(f.dc, 0x1234), 1);
    int fl1 = g0_cell_u8(f.dc, CELL_FLAGS);
    if (fl0 >= 0) {
        CHECKF((fl1 & 0x20) != 0, "cell flags 0x%x after Xferable (0x%x before)", fl1, fl0);
        EXPECT_EQ(fl1 & ~0x20, fl0 & ~0x20);
    }
    EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(f.dc), 1);
    int fl2 = g0_cell_u8(f.dc, CELL_FLAGS);
    if (fl0 >= 0) EXPECT_EQ(fl2 & 0x20, 0);
    g0fx_close(&f);
}
TEST(dx_gap0_xferable_repeat_and_retarget) {
    REQUIRE(UN(NtGdiMakeObjectXferable)); REQUIRE(UN(NtGdiMakeObjectUnXferable));
    G0FX(f, 2, 2);
    static const ULONG pids[] = { 4, 0x1234, 8, 0x1234 };
    for (unsigned i = 0; i < 4; i++) {
        CHECKF(UN(NtGdiMakeObjectXferable)(f.dc, pids[i]) == 1, "pid %lu", (unsigned long)pids[i]);
        int fl = g0_cell_u8(f.dc, CELL_FLAGS);
        if (fl >= 0) CHECKF((fl & 0x20) != 0, "flags 0x%x", fl);
    }
    EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(f.dc), 1);
    EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(f.dc), 1);     /* clearing an already clear flag still succeeds */
    int fl = g0_cell_u8(f.dc, CELL_FLAGS);
    if (fl >= 0) EXPECT_EQ(fl & 0x20, 0);
    g0fx_close(&f);
}
TEST(dx_gap0_xferable_dc_is_still_usable_and_deletable) {
    REQUIRE(UN(NtGdiMakeObjectXferable));
    G0FX(f, 4, 4);
    EXPECT_EQ(UN(NtGdiMakeObjectXferable)(f.dc, 0x1234), 1);
    CHECK(U(SetPixel)(f.dc, 1, 1, RGB(5, 6, 7)) != (COLORREF)-1);
    EXPECT_EQ(U(GetPixel)(f.dc, 1, 1) & 0xffffff, RGB(5, 6, 7));
    HBITMAP old = f.old; f.old = NULL;
    U(SelectObject)(f.dc, old);
    EXPECT_EQ(U(DeleteDC)(f.dc), TRUE);                    /* deleting a transferable DC of this process is allowed */
    U(DeleteObject)(f.bmp);
}

CHILD(gap0dx_umpd_sandbox) {
    (void)argc; (void)argv;
    int fails = 0;
    if (!UN(NtGdiSetUMPDSandboxState) || !U(CreateCompatibleDC)) { printf("  NOTE no NtGdiSetUMPDSandboxState\n"); return 0; }
    /* the body stores its argument in the process state (if any) and always returns 0 */
    static const ULONG vals[] = { 1, 0, 0x12345678, 0xffffffff, 1, 0 };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        ULONG_PTR r = UN(NtGdiSetUMPDSandboxState)(vals[i]);
        if (r != 0) { printf("  FAIL state 0x%lx -> 0x%llx\n", (unsigned long)vals[i], (unsigned long long)r); fails++; }
    }
    HDC d = U(CreateCompatibleDC)(NULL);
    if (!d) { printf("  FAIL CreateCompatibleDC after the state change\n"); fails++; }
    else {
        HBITMAP b = g0_dib(d, 4, 4, 32);
        if (!b) { printf("  FAIL CreateDIBSection after the state change\n"); fails++; } else U(DeleteObject)(b);
        U(DeleteDC)(d);
    }
    return fails;
}
TEST(dx_gap0_umpd_sandbox_state_in_child) {
    REQUIRE(UN(NtGdiSetUMPDSandboxState));
    struct k_child_result r = k_child_run("gap0dx_umpd_sandbox", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}
