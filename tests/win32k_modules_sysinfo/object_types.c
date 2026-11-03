/* win32k_modules_sysinfo: object manager types created for the Win32 stack and the handles the
 * process holds on them.
 *
 * Kernel facts (ntoskrnl 19041):
 *   - ExpWin32Initialization creates six types: "WindowStation", "Desktop", "Composition",
 *     "RawInputManager", "CoreMessaging", "ActivationObject". The initializer carries
 *     InvalidAttributes 0x130, GenericMapping {0x20000,0x20000,0x20000,0xF0000} (ExpXxxMapping),
 *     ValidAccessMask 0xF0000, PoolType NonPagedPoolNx (0x200; ActivationObject 0x221) and
 *     ObjectTypeFlags with SecurityRequired|MaintainHandleCount; ObCreateObjectTypeEx adds
 *     0x58 (+0x10 with MaintainHandleCount) to the non-paged charge.
 *   - NtQueryObject(ObjectTypesInformation=3) works without a handle, stores the type count at +0,
 *     then OBJECT_TYPE_INFORMATION records of 0x68 bytes + align8(NameLength+2) each, starting at +8;
 *     ReturnLength is the full size and a short buffer yields STATUS_INFO_LENGTH_MISMATCH.
 *   - ObjectBasicInformation needs exactly 0x38 bytes, ObjectHandleFlagInformation at least 2,
 *     classes above 4 other than 3 are STATUS_INVALID_INFO_CLASS once the handle resolves.
 * OBJECT_TYPE_INFORMATION layout: Name +0, TotalNumberOfObjects +0x10, TotalNumberOfHandles +0x14,
 * HighWaterObjects +0x28, HighWaterHandles +0x2C, InvalidAttributes +0x40, GenericMapping +0x44,
 * ValidAccessMask +0x54, SecurityRequired +0x58, MaintainHandleCount +0x59, TypeIndex +0x5A,
 * PoolType +0x5C, DefaultPagedPoolCharge +0x60, DefaultNonPagedPoolCharge +0x64.
 */
#include "ms.h"

typedef struct { USHORT Length, MaximumLength; ULONG pad; WCHAR *Buffer; } MS_USTR;

static unsigned char *ms_types_buf(ULONG *count, ULONG *size_out) {
    static unsigned char *cache; static ULONG cnt, sz; static int tried;
    if (!tried) {
        tried = 1;
        ULONG size = 0x10000;
        for (int i = 0; i < 6 && !cache; i++) {
            unsigned char *b = (unsigned char *)calloc(1, size);
            if (!b) break;
            ULONG ret = 0;
            LONG st = ms_qo(NULL, 3, b, size, &ret);
            if (st == ST_SUCCESS) { cache = b; cnt = *(ULONG *)b; sz = ret; break; }
            free(b);
            if (st != ST_INFO_LENGTH_MISMATCH) break;
            size = ret + 0x1000;
        }
    }
    if (!cache) return NULL;
    *count = cnt; *size_out = sz; return cache;
}

typedef struct { const unsigned char *rec; char name[64]; } MS_TYPE;
static const unsigned char *ms_type_rec(const char *name) {
    ULONG n, sz; unsigned char *b = ms_types_buf(&n, &sz); if (!b) return NULL;
    ULONG off = 8;
    for (ULONG i = 0; i < n; i++) {
        const unsigned char *r = b + off;
        const MS_USTR *u = (const MS_USTR *)r;
        char nm[64]; size_t len = u->Length / 2; WCHAR w[64] = {0};
        if (len < 64 && k_try_read(u->Buffer, w, len * 2)) {
            ms_wide_to_narrow(w, nm, sizeof nm);
            if (!strcmp(nm, name)) return r;
        }
        off += 0x68 + ((u->Length + 9) & ~7u);
    }
    return NULL;
}
#define TI_U32(r, o) (*(const ULONG *)((r) + (o)))
#define TI_U8(r, o)  (*((r) + (o)))

#define MS_NEED_TYPES(n, sz, b) \
    ULONG n = 0, sz = 0; unsigned char *b = ms_types_buf(&n, &sz); \
    if (!b) { t_skip("ObjectTypesInformation unavailable"); return; } \
    if (t_is_wine()) { t_skip("emulated object manager"); return; }

TEST(win32k_modules_sysinfo_objtypes_query_without_handle) {
    MS_NEED_TYPES(n, sz, b);
    CHECK(n >= 30);
    CHECK(sz > 8 + n * 0x68);
    t_note("%lu object types, %lu bytes", (unsigned long)n, (unsigned long)sz);
}

TEST(win32k_modules_sysinfo_objtypes_returnlength_equals_record_walk) {
    MS_NEED_TYPES(n, sz, b);
    ULONG off = 8;
    for (ULONG i = 0; i < n; i++) off += 0x68 + ((((MS_USTR *)(b + off))->Length + 9) & ~7u);
    CHECKF(off == sz, "walked %lu, ReturnLength %lu", (unsigned long)off, (unsigned long)sz);
}

TEST(win32k_modules_sysinfo_objtypes_short_buffers) {
    MS_NEED_TYPES(n, sz, b);
    unsigned char tiny[16]; memset(tiny, 0xEE, sizeof tiny);
    ULONG ret = 0;
    LONG st = ms_qo(NULL, 3, tiny, 3, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 8);                       /* nothing accumulated yet: just the header */
    CHECK(tiny[0] == 0xEE);
    st = ms_qo(NULL, 3, tiny, 4, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(*(ULONG *)tiny == n);            /* the type count is stored as soon as 4 bytes are available */
    CHECK(ret == sz);
    st = ms_qo(NULL, 3, tiny, sizeof tiny, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == sz);
}

TEST(win32k_modules_sysinfo_objtypes_null_returnlength_ok) {
    MS_NEED_TYPES(n, sz, b);
    unsigned char *buf = (unsigned char *)calloc(1, sz + 0x1000); REQUIRE(buf);
    CHECK(ms_qo(NULL, 3, buf, sz + 0x1000, NULL) == ST_SUCCESS);
    CHECK(*(ULONG *)buf >= n);
    free(buf);
}

TEST(win32k_modules_sysinfo_objtypes_names_inside_buffer_and_terminated) {
    MS_NEED_TYPES(n, sz, b);
    ULONG off = 8;
    for (ULONG i = 0; i < n; i++) {
        const MS_USTR *u = (const MS_USTR *)(b + off);
        CHECK(u->Length >= 2 && u->Length < 128 && (u->Length & 1) == 0);
        CHECK(u->MaximumLength == u->Length + 2);
        CHECKF((unsigned char *)u->Buffer == b + off + 0x68, "type %lu name buffer not adjacent", (unsigned long)i);
        CHECK(u->Buffer[u->Length / 2] == 0);
        off += 0x68 + ((u->Length + 9) & ~7u);
    }
}

TEST(win32k_modules_sysinfo_objtypes_names_unique) {
    MS_NEED_TYPES(n, sz, b);
    char (*names)[64] = (char (*)[64])calloc(n, 64); REQUIRE(names);
    ULONG off = 8;
    for (ULONG i = 0; i < n; i++) {
        const MS_USTR *u = (const MS_USTR *)(b + off);
        ms_wide_to_narrow(u->Buffer, names[i], 64);
        for (ULONG j = 0; j < i; j++) CHECKF(strcmp(names[i], names[j]), "duplicate type %s", names[i]);
        off += 0x68 + ((u->Length + 9) & ~7u);
    }
    free(names);
}

TEST(win32k_modules_sysinfo_objtypes_type_indices_unique_and_in_range) {
    MS_NEED_TYPES(n, sz, b);
    unsigned char seen[256]; memset(seen, 0, sizeof seen);
    ULONG off = 8;
    for (ULONG i = 0; i < n; i++) {
        const unsigned char *r = b + off;
        unsigned idx = TI_U8(r, 0x5A);
        CHECKF(idx >= 2, "type index %u", idx);       /* indices 0/1 are reserved (free / not-a-type) */
        CHECKF(!seen[idx], "type index %u reused", idx);
        seen[idx] = 1;
        off += 0x68 + ((((const MS_USTR *)r)->Length + 9) & ~7u);
    }
}

static const char *const MS_BASE_TYPES[] = {
    "Type", "Directory", "SymbolicLink", "Token", "Job", "Process", "Thread", "Event", "Mutant", "Semaphore",
    "Timer", "Section", "File", "Key", "IoCompletion", "Callback", "KeyedEvent", "Device", "Driver", "ALPC Port", "DebugObject", "TpWorkerFactory",
};
TEST(win32k_modules_sysinfo_objtypes_base_kernel_types_present) {
    MS_NEED_TYPES(n, sz, b);
    for (size_t i = 0; i < sizeof MS_BASE_TYPES / sizeof *MS_BASE_TYPES; i++)
        CHECKF(ms_type_rec(MS_BASE_TYPES[i]) != NULL, "type %s missing", MS_BASE_TYPES[i]);
}

static const char *const MS_WIN32_TYPES[] = { "WindowStation", "Desktop", "Composition", "RawInputManager", "CoreMessaging", "ActivationObject" };
TEST(win32k_modules_sysinfo_objtypes_win32_types_present) {
    MS_NEED_TYPES(n, sz, b);
    for (size_t i = 0; i < sizeof MS_WIN32_TYPES / sizeof *MS_WIN32_TYPES; i++)
        CHECKF(ms_type_rec(MS_WIN32_TYPES[i]) != NULL, "type %s missing", MS_WIN32_TYPES[i]);
}

TEST(win32k_modules_sysinfo_objtypes_dxgk_types_note) {
    MS_NEED_TYPES(n, sz, b);
    static const char *const dx[] = { "DxgkSharedResource", "DxgkSharedSwapChainObject", "DxgkSharedSyncObject", "DxgkCurrentDxgProcessObject",
                                      "DxgkDisplayManagerObject", "DxgkSharedBundleObject", "DxgkSharedProtectedSessionObject", "DxgkCompositionObject", "DxgkSharedKeyedMutexObject" };
    for (size_t i = 0; i < sizeof dx / sizeof *dx; i++) t_note("%-34s %s", dx[i], ms_type_rec(dx[i]) ? "present" : "absent");
}

/* ---- the six win32 types, field by field ----------------------------------------------- */
static void objtype_props(const char *name, ULONG pool, ULONG valid) {
    MS_NEED_TYPES(n, sz, b);
    const unsigned char *r = ms_type_rec(name);
    if (!r) { CHECKF(0, "type %s missing", name); return; }
    CHECK(TI_U32(r, 0x40) == 0x130);                  /* InvalidAttributes: OBJ_PERMANENT|OBJ_EXCLUSIVE|OBJ_OPENLINK */
    ULONG gm[4]; memcpy(gm, r + 0x44, 16);
    t_note("%s GenericMapping read 0x%lx write 0x%lx execute 0x%lx all 0x%lx", name, (unsigned long)gm[0], (unsigned long)gm[1], (unsigned long)gm[2], (unsigned long)gm[3]);
    /* every generic right maps into the valid access mask, and GENERIC_ALL covers all of it */
    for (int i = 0; i < 4; i++) CHECKF((gm[i] & ~valid) == 0, "generic mapping %d = 0x%lx outside the valid mask", i, (unsigned long)gm[i]);
    CHECK((gm[3] & valid) == gm[3] && (gm[3] & 0xF0000) == 0xF0000);
    CHECKF(TI_U32(r, 0x54) == valid, "ValidAccessMask 0x%lx want 0x%lx", (unsigned long)TI_U32(r, 0x54), (unsigned long)valid);
    CHECK(TI_U8(r, 0x58) == 1);                       /* SecurityRequired */
    CHECK(TI_U8(r, 0x59) == 1);                       /* MaintainHandleCount */
    CHECKF(TI_U32(r, 0x5C) == pool, "PoolType 0x%lx", (unsigned long)TI_U32(r, 0x5C));
    t_note("%s: DefaultPagedPoolCharge=0x%lx DefaultNonPagedPoolCharge=0x%lx", name, (unsigned long)TI_U32(r, 0x60), (unsigned long)TI_U32(r, 0x64));
    CHECK(TI_U32(r, 0x64) != 0 || TI_U32(r, 0x60) != 0);   /* ObCreateObjectTypeEx adds the object header charge to one of the two */
}
TEST(win32k_modules_sysinfo_objtypes_windowstation_properties)   { objtype_props("WindowStation", 0x200, 0xF037F); }
TEST(win32k_modules_sysinfo_objtypes_desktop_properties)         { objtype_props("Desktop", 0x200, 0xF01FF); }
TEST(win32k_modules_sysinfo_objtypes_composition_properties)     { objtype_props("Composition", 0x200, 0xF0003); }
TEST(win32k_modules_sysinfo_objtypes_rawinputmanager_properties) { objtype_props("RawInputManager", 0x200, 0xF0003); }
TEST(win32k_modules_sysinfo_objtypes_coremessaging_properties)   { objtype_props("CoreMessaging", 0x200, 0xF0000); }
TEST(win32k_modules_sysinfo_objtypes_activation_properties)      { objtype_props("ActivationObject", 0x221, 0xF0003); }

TEST(win32k_modules_sysinfo_objtypes_access_masks_vs_user32_constants_note) {
    MS_NEED_TYPES(n, sz, b);
    const unsigned char *ws = ms_type_rec("WindowStation"), *dk = ms_type_rec("Desktop");
    REQUIRE(ws); REQUIRE(dk);
    t_note("WindowStation: Valid=0x%lx GenericAll=0x%lx (WINSTA_ALL_ACCESS 0x%x)", (unsigned long)TI_U32(ws, 0x54), (unsigned long)TI_U32(ws, 0x44 + 12), 0x37F | 0xF0000);
    t_note("Desktop:       Valid=0x%lx GenericAll=0x%lx (DESKTOP_ALL_ACCESS 0x%x)", (unsigned long)TI_U32(dk, 0x54), (unsigned long)TI_U32(dk, 0x44 + 12), 0x1FF | 0xF0000);
    /* The kernel type only knows the standard rights; win32k validates the object-specific bits in its own access checks. */
    CHECK((TI_U32(ws, 0x54) & 0xF0000) == 0xF0000);
    CHECK((TI_U32(dk, 0x54) & 0xF0000) == 0xF0000);
}

TEST(win32k_modules_sysinfo_objtypes_type_counts_sane) {
    MS_NEED_TYPES(n, sz, b);
    ULONG off = 8;
    for (ULONG i = 0; i < n; i++) {
        const unsigned char *r = b + off;
        CHECK(TI_U32(r, 0x28) >= TI_U32(r, 0x10));      /* high-water objects >= current objects (sampled once; races only grow it) */
        off += 0x68 + ((((const MS_USTR *)r)->Length + 9) & ~7u);
    }
    const unsigned char *proc = ms_type_rec("Process"); REQUIRE(proc);
    CHECK(TI_U32(proc, 0x10) >= 10);
    CHECK(TI_U32(proc, 0x14) >= 10);
}

TEST(win32k_modules_sysinfo_objtypes_winsta_desktop_counts_nonzero_after_gui_attach) {
    MS_NEED_TYPES(n, sz, b);
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)(); REQUIRE(ws);
    /* Re-query: this process now owns a window station handle and a desktop handle. */
    unsigned char *buf = (unsigned char *)calloc(1, sz + 0x2000); REQUIRE(buf);
    ULONG ret;
    REQUIRE(ms_qo(NULL, 3, buf, sz + 0x2000, &ret) == ST_SUCCESS);
    ULONG cnt = *(ULONG *)buf, off = 8;
    int got_ws = 0, got_dk = 0;
    for (ULONG i = 0; i < cnt; i++) {
        const unsigned char *r = buf + off; const MS_USTR *u = (const MS_USTR *)r;
        char nm[64]; ms_wide_to_narrow(u->Buffer, nm, sizeof nm);
        if (!strcmp(nm, "WindowStation")) { got_ws = 1; CHECK(TI_U32(r, 0x10) >= 1); CHECK(TI_U32(r, 0x14) >= 1); }
        if (!strcmp(nm, "Desktop"))       { got_dk = 1; CHECK(TI_U32(r, 0x10) >= 1); CHECK(TI_U32(r, 0x14) >= 1); }
        off += 0x68 + ((u->Length + 9) & ~7u);
    }
    CHECK(got_ws && got_dk);
    free(buf);
}

/* ---- single-handle queries ------------------------------------------------------------- */
static int ms_type_name_of(HANDLE h, char *out, size_t cap, unsigned *idx) {
    unsigned char buf[0x200]; ULONG ret = 0;
    LONG st = ms_qo(h, 2, buf, sizeof buf, &ret);
    if (st != ST_SUCCESS) return 0;
    const MS_USTR *u = (const MS_USTR *)buf;
    WCHAR w[64] = {0}; size_t len = u->Length / 2; if (len >= 64) return 0;
    if (!k_try_read(u->Buffer, w, len * 2)) return 0;
    ms_wide_to_narrow(w, out, cap);
    if (idx) *idx = buf[0x5A];
    return 1;
}

TEST(win32k_modules_sysinfo_objtypes_handle_type_event) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    char nm[64] = {0}; unsigned idx = 0;
    CHECK(ms_type_name_of(ev, nm, sizeof nm, &idx));
    CHECK(!strcmp(nm, "Event"));
    const unsigned char *r = ms_type_rec("Event");
    if (r && !t_is_wine()) CHECK(idx == TI_U8(r, 0x5A));
    CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objtypes_handle_type_pseudo_handles) {
    char nm[64] = {0};
    CHECK(ms_type_name_of(GetCurrentProcess(), nm, sizeof nm, NULL));
    CHECK(!strcmp(nm, "Process"));
    memset(nm, 0, sizeof nm);
    CHECK(ms_type_name_of(GetCurrentThread(), nm, sizeof nm, NULL));
    CHECK(!strcmp(nm, "Thread"));
}
TEST(win32k_modules_sysinfo_objtypes_handle_type_process_window_station) {
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)(); REQUIRE(ws);
    char nm[64] = {0}; unsigned idx = 0;
    if (!ms_type_name_of((HANDLE)ws, nm, sizeof nm, &idx)) { t_skip("NtQueryObject refused"); return; }
    CHECKF(!strcmp(nm, "WindowStation"), "type \"%s\"", nm);
    const unsigned char *r = ms_type_rec("WindowStation");
    if (r && !t_is_wine()) CHECK(idx == TI_U8(r, 0x5A));
}
TEST(win32k_modules_sysinfo_objtypes_handle_type_thread_desktop) {
    REQUIRE(U(GetThreadDesktop));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId()); REQUIRE(d);
    char nm[64] = {0}; unsigned idx = 0;
    if (!ms_type_name_of((HANDLE)d, nm, sizeof nm, &idx)) { t_skip("NtQueryObject refused"); return; }
    CHECKF(!strcmp(nm, "Desktop"), "type \"%s\"", nm);
    const unsigned char *r = ms_type_rec("Desktop");
    if (r && !t_is_wine()) CHECK(idx == TI_U8(r, 0x5A));
}
TEST(win32k_modules_sysinfo_objtypes_desktop_and_winsta_type_indices_differ) {
    MS_NEED_TYPES(n, sz, b);
    const unsigned char *a = ms_type_rec("WindowStation"), *d = ms_type_rec("Desktop");
    REQUIRE(a); REQUIRE(d);
    CHECK(TI_U8(a, 0x5A) != TI_U8(d, 0x5A));
}

TEST(win32k_modules_sysinfo_objtypes_basic_information_exact_length) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    unsigned char buf[0x80];
    ULONG lens[] = { 0, 4, 0x37, 0x39, 0x40 };
    for (size_t i = 0; i < sizeof lens / sizeof *lens; i++) {
        ULONG ret = 0;
        LONG st = ms_qo(ev, 0, buf, lens[i], &ret);
        CHECKF(st == ST_INFO_LENGTH_MISMATCH, "len %lu status 0x%lx", (unsigned long)lens[i], (unsigned long)st);
    }
    ULONG ret = 0;
    CHECK(ms_qo(ev, 0, buf, 0x38, &ret) == ST_SUCCESS);
    CHECK(ret == 0x38);
    CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objtypes_basic_information_fresh_event) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    ULONG b[14]; ULONG ret = 0;
    REQUIRE(ms_qo(ev, 0, b, 0x38, &ret) == ST_SUCCESS);
    /* OBJECT_BASIC_INFORMATION: Attributes +0, GrantedAccess +4, HandleCount +8, PointerCount +0xC */
    CHECK(b[2] == 1);                       /* a fresh unnamed event has exactly one handle */
    CHECK(b[3] >= 1);
    CHECKF((b[1] & EVENT_ALL_ACCESS) == EVENT_ALL_ACCESS, "granted 0x%lx", (unsigned long)b[1]);
    CHECK(!(b[0] & 0x2));                   /* not inherit-attributed in the attribute word */
    HANDLE dup = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), ev, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    REQUIRE(ms_qo(ev, 0, b, 0x38, &ret) == ST_SUCCESS);
    CHECKF(b[2] == 2, "handle count after duplicate %lu", (unsigned long)b[2]);
    CloseHandle(dup);
    REQUIRE(ms_qo(ev, 0, b, 0x38, &ret) == ST_SUCCESS);
    CHECKF(b[2] == 1, "handle count after close %lu", (unsigned long)b[2]);
    CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objtypes_basic_information_granted_access_reflects_duplicate) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    HANDLE dup = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), ev, GetCurrentProcess(), &dup, SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, 0));
    ULONG b[14], ret;
    REQUIRE(ms_qo(dup, 0, b, 0x38, &ret) == ST_SUCCESS);
    CHECKF(b[1] == (SYNCHRONIZE | EVENT_MODIFY_STATE), "granted 0x%lx", (unsigned long)b[1]);
    CloseHandle(dup); CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objtypes_basic_information_window_station_access) {
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)(); REQUIRE(ws);
    ULONG b[14]; ULONG ret = 0;
    LONG st = ms_qo((HANDLE)ws, 0, b, 0x38, &ret);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    t_info("process window station handle: access 0x%lx handles %lu pointers %lu", (unsigned long)b[1], (unsigned long)b[2], (unsigned long)b[3]);
    CHECK(b[1] != 0);
    CHECK(b[2] >= 1);
}

TEST(win32k_modules_sysinfo_objtypes_handle_flags_information) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE ev = CreateEventW(&sa, TRUE, FALSE, NULL); REQUIRE(ev);
    unsigned char f[4] = { 9, 9, 9, 9 }; ULONG ret = 0;
    LONG st = ms_qo(ev, 4, f, 2, &ret);
    CHECK(st == ST_SUCCESS);
    CHECK(ret == 2);
    CHECK(f[0] == 1);                  /* inheritable */
    CHECK(f[1] == 0);                  /* not protected from close */
    st = ms_qo(ev, 4, f, 1, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 2);
    CHECK(SetHandleInformation(ev, HANDLE_FLAG_PROTECT_FROM_CLOSE, HANDLE_FLAG_PROTECT_FROM_CLOSE));
    st = ms_qo(ev, 4, f, 2, &ret);
    CHECK(st == ST_SUCCESS);
    CHECK(f[1] == 1);
    SetHandleInformation(ev, HANDLE_FLAG_PROTECT_FROM_CLOSE, 0);
    CloseHandle(ev);
}

TEST(win32k_modules_sysinfo_objtypes_unsupported_object_classes) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    unsigned char buf[0x100];
    ULONG classes[] = { 5, 6, 7, 0x10, 0x63, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) {
        ULONG ret = 0;
        LONG st = ms_qo(ev, classes[i], buf, sizeof buf, &ret);
        CHECKF(st == ST_INVALID_INFO_CLASS, "class %lu status 0x%lx", (unsigned long)classes[i], (unsigned long)st);
    }
    CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objtypes_invalid_handles) {
    unsigned char buf[0x100]; ULONG ret = 0;
    CHECK(ms_qo(NULL, 2, buf, sizeof buf, &ret) == ST_INVALID_HANDLE);
    CHECK(ms_qo((HANDLE)(ULONG_PTR)0x7FFFFFF8, 0, buf, 0x38, &ret) == ST_INVALID_HANDLE);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    CloseHandle(ev);
    LONG st = ms_qo(ev, 2, buf, sizeof buf, &ret);
    CHECKF(st == ST_INVALID_HANDLE, "closed handle status 0x%lx", (unsigned long)st);
}
TEST(win32k_modules_sysinfo_objtypes_invalid_handle_beats_invalid_class) {
    unsigned char buf[0x40]; ULONG ret = 0;
    /* The handle is resolved before the class is dispatched (class 3 is the only handle-less one). */
    CHECK(ms_qo(NULL, 99, buf, sizeof buf, &ret) == ST_INVALID_HANDLE);
    CHECK(ms_qo(NULL, 3, buf, 3, &ret) == ST_INFO_LENGTH_MISMATCH);
}
TEST(win32k_modules_sysinfo_objtypes_type_information_short_buffer) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    unsigned char buf[0x200]; ULONG ret = 0;
    CHECK(ms_qo(ev, 2, buf, 0x10, &ret) == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 0x68 + 16);               /* header + align8(len("Event")*2 + 2) */
    CHECK(ms_qo(ev, 2, buf, ret, &ret) == ST_SUCCESS);
    CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objtypes_name_information_for_unnamed_event) {
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    unsigned char buf[0x200]; ULONG ret = 0;
    LONG st = ms_qo(ev, 1, buf, sizeof buf, &ret);
    /* An unnamed object has no name information: an empty UNICODE_STRING is returned. */
    CHECKF(st == ST_SUCCESS, "status 0x%lx", (unsigned long)st);
    CHECK(((MS_USTR *)buf)->Length == 0);
    CloseHandle(ev);
}
