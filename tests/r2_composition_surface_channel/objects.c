/* Composition objects reachable by an ordinary process: shared resource handles, shared sections bound to a
 * channel, DwmEnableMMCSS accounting, material properties and the synchronization-object commit gate.
 *
 * Analysis facts (win32kbase.sys 19041):
 *  - NtDCompositionCreateSharedResourceHandle(type, phOut): only type 0x8e and 0xc3 pass
 *    ; the handle is written to *phOut only on success
 *    (dcomp!DCompositionCreateSharedVisualHandle passes 0xc3).
 *  - NtDCompositionEnableMMCSS(on): per-process counter at CProcessData+0x28. Disabling at 0 and enabling at
 *    0xFFFFFFFF both give 0xC0000001; no default connection (no DWM) gives 0xC0000022 first.
 *  - NtDCompositionCommitSynchronizationObject: non-DWM callers must hold the "shellExperienceComposer"
 *    capability (RtlCapabilityCheck) or get 0xC0000022 before any handle is resolved.
 *  - NtDCompositionSetMaterialProperty has the same gate (UserIsCurrentProcessDwm || CheckShellExperienceComposerAccess).
 *    NtDCompositionGetMaterialProperty: FindMaterialProperty for the current desktop, 0xC0000225 when the property
 *    has no object; the handle out parameter is written only on success.
 *  - NtDCompositionCreateAndBindSharedSection(channel, resource, size, phOut): channel lookup first (0xC0000022 for
 *    unknown ids), *phOut only on success.
 * NtDCompositionCreateSynchronizationObject is deliberately not called: CompositionObject::OpenDwmHandle opens the
 * handle in the DWM process, so a success would plant a handle in another process. */
#include "rc.h"

UDECL(NTSTATUS, NtDCompositionEnableMMCSS, (int enable));
UDECL(NTSTATUS, NtDCompositionCommitSynchronizationObject, (HANDLE h));
UDECL(NTSTATUS, NtDCompositionSetMaterialProperty, (ULONG_PTR a, ULONG_PTR b, HANDLE h));
UDECL(NTSTATUS, NtDCompositionGetMaterialProperty, (ULONG_PTR a, HANDLE *phOut));
UDECL(NTSTATUS, NtDCompositionCreateAndBindSharedSection, (UINT id, UINT resource, ULONGLONG size, HANDLE *phOut));
UDECL(NTSTATUS, NtMapViewOfSection, (HANDLE sec, HANDLE proc, PVOID *base, ULONG_PTR zbits, SIZE_T commit, PLARGE_INTEGER off, PSIZE_T vsize, ULONG inh, ULONG alloc, ULONG prot));
UDECL(NTSTATUS, NtUnmapViewOfSection, (HANDLE proc, PVOID base));
UDECL(NTSTATUS, NtQuerySection, (HANDLE sec, ULONG cls, PVOID buf, SIZE_T len, PSIZE_T ret));

#define SHARED_VISUAL 0xC3
#define SHARED_OTHER  0x8E

RT(shared_resource_type_table) {
    NEED_WIN();
    NtDCompositionCreateSharedResourceHandle_pfn f = UN(NtDCompositionCreateSharedResourceHandle); REQUIRE(f);
    static const ULONG_PTR bad[] = { 0, 1, 2, 0x8D, 0x8F, 0xC2, 0xC4, 0x100, 0xFF, 0x7FFFFFFF, 0xFFFFFFFF };
    size_t i; unsigned long h0 = rc_handles();
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HANDLE h = (HANDLE)(ULONG_PTR)0x1234;
        CHECKF(f(bad[i], &h) == S_INVPARAM, "type 0x%llx", (unsigned long long)bad[i]);
        CHECK(h == (HANDLE)(ULONG_PTR)0x1234);        /* *phOut is written only on success */
    }
    CHECK(rc_handles() == h0);
}

RT(shared_visual_handle_lifetime) {
    NEED_WIN();
    NtDCompositionCreateSharedResourceHandle_pfn f = UN(NtDCompositionCreateSharedResourceHandle); REQUIRE(f);
    HANDLE a = NULL, b = NULL; char tn[64], tn2[64];
    unsigned long h0 = rc_handles();
    NTSTATUS st = f(SHARED_VISUAL, &a);
    if (st < 0) { t_note("CreateSharedResourceHandle(0xc3) -> 0x%08lx", (unsigned long)st); t_skip("no shared visual"); return; }
    CHECK(a != NULL);
    CHECK(rc_handles() == h0 + 1);
    rc_type_name(a, tn, sizeof tn);
    t_note("shared visual handle %p is of object type \"%s\"", a, tn);
    CHECK(tn[0] != 0);                                 /* NtQueryObject works on it: it is a real kernel object handle */
    CHECK_ST(f(SHARED_VISUAL, &b), 0);
    CHECK(b != NULL && b != a);                       /* each call creates a new object and handle */
    rc_type_name(b, tn2, sizeof tn2);
    CHECK(!strcmp(tn, tn2));
    {   /* handle is duplicable within the process and the duplicate keeps the type */
        HANDLE d = NULL; char tn3[64];
        CHECK(DuplicateHandle(GetCurrentProcess(), a, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS));
        if (d) { rc_type_name(d, tn3, sizeof tn3); CHECK(!strcmp(tn, tn3)); CloseHandle(d); }
    }
    CHECK(CloseHandle(a));
    CHECK(CloseHandle(b));
    CHECK(rc_handles() == h0);
    SetLastError(0);
    CHECK(!CloseHandle(a));                           /* closed handles are dead */
}

RT(shared_other_resource_type) {
    NEED_WIN();
    NtDCompositionCreateSharedResourceHandle_pfn f = UN(NtDCompositionCreateSharedResourceHandle); REQUIRE(f);
    HANDLE h = NULL; char tn[64];
    NTSTATUS st = f(SHARED_OTHER, &h);
    t_note("CreateSharedResourceHandle(0x8e) -> 0x%08lx", (unsigned long)st);
    if (st >= 0) { rc_type_name(h, tn, sizeof tn); t_note("type of 0x8e object: \"%s\"", tn); CHECK(h != NULL); CloseHandle(h); }
    else CHECK(h == NULL);
}

RT(mmcss_counter_accounting) {
    NEED_WIN();
    NtDCompositionEnableMMCSS_pfn f = UN(NtDCompositionEnableMMCSS); REQUIRE(f);
    NTSTATUS st = f(0);
    if (st == S_DENIED) { t_note("no default DirectComposition connection (no DWM?): EnableMMCSS -> ACCESS_DENIED"); t_skip("no DWM connection"); return; }
    CHECK_ST(st, S_UNSUCC);                           /* counter is 0: nothing to disable */
    CHECK_ST(f(0), S_UNSUCC);
    st = f(1);
    t_note("EnableMMCSS(1) -> 0x%08lx", (unsigned long)st);
    if (st == 0) {
        CHECK_ST(f(1), 0);                            /* nested enable only bumps the counter */
        CHECK_ST(f(0), 0);
        CHECK_ST(f(0), 0);
        CHECK_ST(f(0), S_UNSUCC);                     /* balanced again */
    } else {
        f(0);                                         /* the counter was bumped before CConnection::EnableMMCSS ran: undo it */
    }
}

RT(commit_sync_object_resolves_handle_first) {
    NEED_WIN();
    NtDCompositionCommitSynchronizationObject_pfn f = UN(NtDCompositionCommitSynchronizationObject); REQUIRE(f);
    /* observed on Windows 10 22H2, identical for the elevated and the unprivileged token: a handle that does not
     * exist is STATUS_INVALID_HANDLE, a handle of the wrong object type is STATUS_OBJECT_TYPE_MISMATCH (the
     * process pseudo-handle is a process object); no access-denied answer comes first. */
    CHECK_ST(f(NULL), S_INVHANDLE);
    CHECK_ST(f((HANDLE)(ULONG_PTR)0x7FFC), S_INVHANDLE);
    CHECK_ST(f(GetCurrentProcess()), (NTSTATUS)0xC0000024L);
}

RT(set_material_property_ordinary_process) {
    NEED_WIN();
    NtDCompositionSetMaterialProperty_pfn f = UN(NtDCompositionSetMaterialProperty); REQUIRE(f);
    /* observed on Windows 10 22H2, identical for the elevated and the unprivileged token: an unknown property id
     * with no value is accepted from an ordinary process; a handle that is not a composition object is refused. */
    NTSTATUS st = f(0x6666, 0, NULL);
    CHECK_ST(st, 0);
    st = f(0x6667, 1, (HANDLE)(ULONG_PTR)0x7FFC);
    t_note("SetMaterialProperty with a bogus handle -> 0x%08lx", (unsigned long)st);
    CHECK(NT_FAIL(st));
}

RT(get_material_property_unknown_id) {
    NEED_WIN();
    NtDCompositionGetMaterialProperty_pfn f = UN(NtDCompositionGetMaterialProperty); REQUIRE(f);
    unsigned long h0 = rc_handles();
    HANDLE h = (HANDLE)(ULONG_PTR)0x1234;
    NTSTATUS st = f(0x66666666, &h);
    t_note("GetMaterialProperty(unknown) -> 0x%08lx", (unsigned long)st);
    if (st >= 0) { rc_close(h); t_skip("a property with that id exists"); return; }
    CHECK(h == (HANDLE)(ULONG_PTR)0x1234);
    CHECK(rc_handles() == h0);
}

RT(shared_section_unknown_channel_and_resource) {
    NEED_WIN();
    NtDCompositionCreateAndBindSharedSection_pfn f = UN(NtDCompositionCreateAndBindSharedSection); REQUIRE(f);
    struct rc_chan c; REQUIRE_CHAN(c);
    HANDLE h = (HANDLE)(ULONG_PTR)0x1234; unsigned long h0 = rc_handles();
    CHECK_ST(f(rc_bogus_id(c.id), 1, 0x1000, &h), S_DENIED);
    CHECK(h == (HANDLE)(ULONG_PTR)0x1234);
    /* No resource with that id was created through the command stream, so the bind cannot succeed. */
    NTSTATUS st = f(c.id, 0x7777, 0x1000, &h);
    t_note("bind shared section to a nonexistent resource -> 0x%08lx", (unsigned long)st);
    if (st >= 0) {
        NtMapViewOfSection_pfn mv = UN(NtMapViewOfSection); NtUnmapViewOfSection_pfn um = UN(NtUnmapViewOfSection);
        NtQuerySection_pfn qs = UN(NtQuerySection);
        struct { PVOID base; ULONG attr; ULONG pad; LARGE_INTEGER size; } bi; SIZE_T ret = 0;
        PVOID base = NULL; SIZE_T vs = 0; char tn[64];
        CHECK(h != NULL);
        rc_type_name(h, tn, sizeof tn); t_note("section handle type \"%s\"", tn);
        CHECK(!strcmp(tn, "Section"));
        if (qs) { memset(&bi, 0, sizeof bi); CHECK(qs(h, 0, &bi, sizeof bi, &ret) >= 0); CHECK(bi.size.QuadPart >= 0x1000); t_note("section size 0x%llx attr 0x%lx", (unsigned long long)bi.size.QuadPart, (unsigned long)bi.attr); }
        if (mv && um) {
            NTSTATUS m = mv(h, GetCurrentProcess(), &base, 0, 0, NULL, &vs, 1 /*ViewShare*/, 0, PAGE_READONLY);
            t_note("map view -> 0x%08lx base=%p size=0x%llx", (unsigned long)m, base, (unsigned long long)vs);
            if (m >= 0) { MEMORY_BASIC_INFORMATION mbi; CHECK(VirtualQuery(base, &mbi, sizeof mbi)); CHECK(mbi.Type == MEM_MAPPED); um(GetCurrentProcess(), base); }
        }
        rc_close(h);
    } else {
        CHECK(h == (HANDLE)(ULONG_PTR)0x1234);
        CHECK(rc_handles() == h0);
    }
    rc_chan_destroy(&c);
}
