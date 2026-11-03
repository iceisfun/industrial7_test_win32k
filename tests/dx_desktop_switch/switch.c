/* dx_desktop_switch: real input-desktop switches, each bounded (about 150-250 ms) and restored.
 *
 * the kernel desktop-switch path (the part after the "already the input desktop" short-cut):
 *   - a caller that is not the logon process, targeting a desktop of a visible window station, is
 *     refused with STATUS_ACCESS_DENIED unless it is the Winlogon/disconnect desktop or flag 2 is set
 *     (the flag argument user32 passes is 0) - this policy is the build-dependent part, so the tests
 *     here never hard-assume success; they assert the state that must be consistent either way:
 *       rc == TRUE   <=> the input desktop is now the target (OpenInputDesktop name)
 *       rc == FALSE  ==> the input desktop is unchanged
 *   - on success: ClearKeyboardStates (async key state cleared), foreground reset
 *     (the foreground change), RIT thread and queue re-pointed to the new desktop, the composition and the graphics side notified; the calling thread's own desktop assignment is NOT touched.
 */
#include "dxd.h"

/* create a desktop, switch to it, sample. 1 = desktop created (switch result is in *s). */
static int sw_open(const char *tag, HDESK *d, WCHAR *n, struct dxd_sw *s, DWORD dwell) {
    *d = dxd_mk_desk(tag, DESKTOP_ALL_RIGHTS, n);
    if (!*d) { t_skip("CreateDesktop(%s) failed, err %lu", tag, GetLastError()); return 0; }
    dxd_do_switch(*d, s, dwell);
    return 1;
}
static void sw_close(HDESK d) {
    CHECK(dxd_restore());
    U(CloseDesktop)(d);
}
/* the invariant that holds whether or not the platform permits the switch */
static void sw_consistent(const struct dxd_sw *s, const WCHAR *target) {
    if (!s->in_ok) { t_note("OpenInputDesktop failed after the switch attempt (err %lu)", GetLastError()); return; }
    if (s->rc) CHECKF(!lstrcmpiW(s->in_name, target), "switched but input is \"%ls\"", s->in_name);
    else CHECKF(!lstrcmpiW(s->in_name, dxd_orig_name()), "refused but input is \"%ls\"", s->in_name);
}

DXT(switch_new_desktop_roundtrip_consistent) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    if (!sw_open("rt", &d, n, &s, 200)) return;
    sw_consistent(&s, n);
    if (!s.rc) t_note("SwitchDesktop to a new desktop of the interactive station refused, LastError %lu", s.err);
    sw_close(d);
    WCHAR cur[128]; CHECK(dxd_input_name(cur, 128)); EXPECT_WSTR(cur, dxd_orig_name());
}

DXT(switch_result_is_deterministic_over_three_roundtrips) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d = dxd_mk_desk("det", DESKTOP_ALL_RIGHTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    BOOL first = 0;
    for (int i = 0; i < 3; i++) {
        struct dxd_sw s; dxd_do_switch(d, &s, 80);
        if (i == 0) first = s.rc; else CHECKF(s.rc == first, "attempt %d rc %d != first %d", i, s.rc, first);
        sw_consistent(&s, n);
        CHECK(dxd_restore());
    }
    U(CloseDesktop)(d);
}

DXT(switch_calling_thread_desktop_is_not_changed) {
    DXD_BEGIN();
    HDESK before = U(GetThreadDesktop)(GetCurrentThreadId());
    WCHAR nb[128], na[128], n[64]; HDESK d; struct dxd_sw s;
    CHECK(before && dxd_uoi_str(before, DXD_UOI_NAME, nb, 128));
    if (!sw_open("thr", &d, n, &s, 120)) return;
    HDESK after = U(GetThreadDesktop)(GetCurrentThreadId());
    /* the desktop-switch path only re-points the RIT thread and its queue (the new desktop assignment) */
    CHECK(after == before);
    CHECK(after && dxd_uoi_str(after, DXD_UOI_NAME, na, 128));
    EXPECT_WSTR(na, nb);
    sw_close(d);
}

DXT(switch_foreground_window_is_null_for_caller_on_other_desktop) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    if (!sw_open("fg", &d, n, &s, 150)) return;
    if (s.rc) {
        /* NtUserGetForegroundWindow returns a window only when it lives on the caller's own desktop;
         * after the switch gpqForeground was reset and the new desktop has no windows. */
        SetLastError(0);
        HWND fg = U(GetForegroundWindow)();
        CHECKF(fg == NULL, "foreground %p", fg);
    } else t_note("switch refused (err %lu); foreground check skipped", s.err);
    sw_close(d);
}

DXT(switch_clears_async_key_state) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    if (!sw_open("key", &d, n, &s, 150)) return;
    if (s.rc) {
        /* ClearKeyboardStates zeroes gafAsyncKeyState; GetAsyncKeyState additionally answers 0 (with
         * LastError 5) to a caller that cannot access the foreground queue. Either way no key reads down. */
        static const int vk[] = { VK_LBUTTON, VK_RBUTTON, VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_SPACE, 'A', VK_RETURN, VK_ESCAPE };
        for (size_t i = 0; i < sizeof vk / sizeof vk[0]; i++) {
            SHORT st = U(GetAsyncKeyState)(vk[i]);
            CHECKF(!(st & 0x8000), "vk 0x%x reads down (0x%x)", vk[i], (unsigned)(USHORT)st);
        }
    } else t_note("switch refused (err %lu); key-state check skipped", s.err);
    sw_close(d);
}

DXT(switch_input_desktop_handle_names_the_same_object) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    if (!sw_open("obj", &d, n, &s, 100)) return;
    if (s.rc) {
        HDESK in = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
        CHECK(in != NULL);
        if (in) {
            CHECK(in != d);
            CHECK(dxd_same_object(in, d));          /* both handles reference the desktop object just switched to */
            U(CloseDesktop)(in);
        }
    }
    sw_close(d);
}

DXT(switch_again_to_new_current_desktop_succeeds) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    if (!sw_open("again", &d, n, &s, 80)) return;
    if (s.rc) {
        /* now d == the input desktop: the first branch of the desktop-switch path returns success */
        for (int i = 0; i < 5; i++) { SetLastError(0); CHECK(U(SwitchDesktop)(d)); }
        WCHAR cur[128]; CHECK(dxd_input_name(cur, 128)); EXPECT_WSTR(cur, n);
    }
    sw_close(d);
}

DXT(switch_back_with_original_handle_succeeds_after_forward_switch) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    if (!sw_open("back", &d, n, &s, 80)) return;
    if (s.rc) {
        SetLastError(0);
        BOOL back = U(SwitchDesktop)(dxd_orig_handle());
        DWORD e = GetLastError();
        CHECKF(back, "switch back refused, err %lu", e);
        WCHAR cur[128]; CHECK(dxd_input_name(cur, 128));
        if (back) EXPECT_WSTR(cur, dxd_orig_name());
    }
    sw_close(d);
}

DXT(switch_chain_two_new_desktops_and_back) {
    DXD_BEGIN();
    WCHAR na[64], nb[64]; HDESK a, b; struct dxd_sw s1, s2;
    if (!sw_open("ca", &a, na, &s1, 100)) return;
    b = dxd_mk_desk("cb", DESKTOP_ALL_RIGHTS, nb);
    if (!b) { t_skip("second CreateDesktop failed"); sw_close(a); return; }
    sw_consistent(&s1, na);
    if (s1.rc) {
        dxd_do_switch(b, &s2, 100);
        CHECKF(s2.rc, "second hop A->B refused (err %lu) after first hop worked", s2.err);
        if (s2.in_ok && s2.rc) EXPECT_WSTR(s2.in_name, nb);
        dxd_do_switch(a, &s2, 100);
        if (s2.in_ok && s2.rc) EXPECT_WSTR(s2.in_name, na);
    }
    sw_close(a);
    U(CloseDesktop)(b);
}

DXT(switch_input_name_tracks_each_hop) {
    DXD_BEGIN();
    WCHAR na[64], nb[64]; HDESK a = dxd_mk_desk("ta", DESKTOP_ALL_RIGHTS, na), b = dxd_mk_desk("tb", DESKTOP_ALL_RIGHTS, nb);
    if (!a || !b) { t_skip("CreateDesktop failed"); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    HDESK seq[6] = { a, b, a, b, a, b };
    const WCHAR *nm[6] = { na, nb, na, nb, na, nb };
    for (int i = 0; i < 6; i++) {
        struct dxd_sw s; dxd_do_switch(seq[i], &s, 40);
        sw_consistent(&s, nm[i]);
        if (!s.rc) break;                 /* policy refuses the very first hop: nothing more to learn */
    }
    CHECK(dxd_restore());
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}

DXT(switch_target_with_window_isolated_from_caller) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d = dxd_mk_desk("win", DESKTOP_ALL_RIGHTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t);
    t.desk = d; t.want_window = 1; t.visible = 1;
    if (!dxd_thr_start(&t, "swwin") || !t.set_ok || !t.hw) {
        t_skip("worker on new desktop could not create a window (set_ok=%d err=%lu)", t.set_ok, t.set_err);
        dxd_thr_stop(&t); U(CloseDesktop)(d); return;
    }
    struct dxd_sw s; dxd_do_switch(d, &s, 150);
    sw_consistent(&s, n);
    if (s.rc) {
        HWND fg = U(GetForegroundWindow)();
        CHECKF(fg == NULL, "caller (other desktop) sees foreground %p", fg);
        HWND found = U(FindWindowW)(t.cls, NULL);
        CHECK(found == NULL);                    /* FindWindow searches the caller's desktop only */
    }
    CHECK(dxd_restore());
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(switch_target_window_visible_state_same_before_and_after) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d = dxd_mk_desk("vis", DESKTOP_ALL_RIGHTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t);
    t.desk = d; t.want_window = 1; t.visible = 1;
    t.action = NULL;
    if (!dxd_thr_start(&t, "swvis") || !t.set_ok || !t.hw) {
        t_skip("worker could not create a window on the new desktop");
        dxd_thr_stop(&t); U(CloseDesktop)(d); return;
    }
    /* IsWindowVisible is answered client-side from the window structure in the desktop heap; whether a foreign desktop's
     * heap is mapped here is not guaranteed, so only before/after-return equality is asserted. */
    BOOL v0 = U(IsWindowVisible)(t.hw);
    struct dxd_sw s; dxd_do_switch(d, &s, 120);
    BOOL v1 = U(IsWindowVisible)(t.hw);
    CHECK(dxd_restore());
    BOOL v2 = U(IsWindowVisible)(t.hw);
    CHECKF(v2 == v0, "visible %d before the switch, %d after returning", v0, v2);
    t_note("IsWindowVisible across desktops: %d/%d/%d", v0, v1, v2);
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(switch_elapsed_time_is_bounded) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d; struct dxd_sw s;
    DWORD t0 = GetTickCount();
    if (!sw_open("tm", &d, n, &s, 50)) return;
    sw_close(d);
    DWORD dt = GetTickCount() - t0;
    t_note("switch + dwell + restore took %lu ms", dt);
    CHECKF(dt < 30000, "round trip took %lu ms", dt);   /* generous: only catches a hang-like stall */
}

DXT(switch_does_not_invalidate_existing_desktop_handles) {
    DXD_BEGIN();
    WCHAR n[64], other[64]; HDESK d, o = dxd_mk_desk("keep", DESKTOP_ALL_RIGHTS, other); struct dxd_sw s;
    if (!o) { t_skip("CreateDesktop failed"); return; }
    if (!sw_open("sw", &d, n, &s, 80)) { U(CloseDesktop)(o); return; }
    WCHAR t1[32], nm[64];
    CHECK(dxd_uoi_str(o, DXD_UOI_TYPE, t1, 32)); EXPECT_WSTR(t1, L"Desktop");
    CHECK(dxd_uoi_str(o, DXD_UOI_NAME, nm, 64)); EXPECT_WSTR(nm, other);
    CHECK(dxd_uoi_str(d, DXD_UOI_NAME, nm, 64)); EXPECT_WSTR(nm, n);
    sw_close(d);
    CHECK(dxd_uoi_str(o, DXD_UOI_NAME, nm, 64)); EXPECT_WSTR(nm, other);
    U(CloseDesktop)(o);
}

DXT(switch_after_close_of_creating_handle_other_handle_still_switches) {
    DXD_BEGIN();
    WCHAR n[64]; HDESK d = dxd_mk_desk("dup", DESKTOP_ALL_RIGHTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    HDESK d2 = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS);
    CHECK(d2 != NULL);
    CHECK(d2 != d);
    CHECK(U(CloseDesktop)(d));              /* the object stays alive through d2 (named object, one open handle) */
    if (d2) {
        struct dxd_sw s; dxd_do_switch(d2, &s, 80);
        sw_consistent(&s, n);
        CHECK(dxd_restore());
        U(CloseDesktop)(d2);
    }
}
