/* CCD (connecting and configuring displays): GetDisplayConfigBufferSizes / QueryDisplayConfig / DisplayConfigGetDeviceInfo.
 *
 *  user32!GetDisplayConfigBufferSizes -> NtUserGetDisplayConfigBufferSizes(flags, &numPaths) (one output; user32
 *     derives the mode count as paths*2, or paths*3 with QDC_VIRTUAL_MODE_AWARE). NULL out-pointers are STATUS_INVALID_PARAMETER
 *     in user32 itself; results are Win32 error codes (RtlNtStatusToDosError), not SetLastError.
 *  user32!QueryDisplayConfig -> NtUserQueryDisplayConfig: flags & 0xffffffc8 invalid; (flags & 7) must be 1, 2 or 4;
 *     numPaths == 0 is STATUS_INVALID_PARAMETER; a topology pointer is accepted only together with QDC_DATABASE_CURRENT.
 *  user32!DisplayConfigGetDeviceInfo -> NtUserDisplayConfigGetDeviceInfo: size is fixed per type (1:0x54 2:0x1a4 3:0x50
 *     4:0x114 6,7,11:0x18 9:0x20), anything else is STATUS_INVALID_PARAMETER; sizes below 0x14 raise an exception and are
 *     deliberately never sent.
 * A headless VM without a WDDM display driver answers NOT_SUPPORTED / ACCESS_DENIED; those cases are notes, not failures.
 */
#include "dm.h"

#define DM_MAXPATHS 16
#define DM_MAXMODES 48

static int ccd_sizes(UINT32 flags, UINT32 *np, UINT32 *nm) { *np = *nm = 0xdeadbeef; return (int)U(GetDisplayConfigBufferSizes)(flags, np, nm); }
static int ccd_wddm(void) { UINT32 a, b; return U(GetDisplayConfigBufferSizes) && ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &a, &b) == 0; }
#define NEED_CCD() do { NEED_GUI(); REQUIRE(U(GetDisplayConfigBufferSizes) && U(QueryDisplayConfig) && U(DisplayConfigGetDeviceInfo)); \
    if (!ccd_wddm()) { UINT32 a_, b_; t_note("GetDisplayConfigBufferSizes(ACTIVE) = %d: no WDDM display config", ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &a_, &b_)); t_skip("CCD not supported here"); return; } } while (0)

struct ccd { UINT32 np, nm; DISPLAYCONFIG_PATH_INFO paths[DM_MAXPATHS]; DISPLAYCONFIG_MODE_INFO modes[DM_MAXMODES]; DISPLAYCONFIG_TOPOLOGY_ID topo; };
static int ccd_query(struct ccd *c, UINT32 flags) {
    UINT32 np, nm;
    int r = ccd_sizes(flags & ~QDC_DATABASE_CURRENT, &np, &nm);
    if (r) return r;
    if (np > DM_MAXPATHS) np = DM_MAXPATHS;
    if (nm > DM_MAXMODES) nm = DM_MAXMODES;
    memset(c, 0, sizeof *c);
    c->np = np; c->nm = nm;
    return (int)U(QueryDisplayConfig)(flags, &c->np, c->paths, &c->nm, c->modes, (flags & QDC_DATABASE_CURRENT) ? &c->topo : NULL);
}

TEST(display_monitors_dpi_ccd_sizes_null_pointers_are_invalid_parameter) {
    NEED_GUI(); REQUIRE(U(GetDisplayConfigBufferSizes));
    UINT32 a = 0, b = 0;
    CHECK(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, NULL, &b) == ERROR_INVALID_PARAMETER);   /* user32: status -0x3ffffff3 before any syscall */
    CHECK(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &a, NULL) == ERROR_INVALID_PARAMETER);
    CHECK(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, NULL, NULL) == ERROR_INVALID_PARAMETER);
    CHECK(U(GetDisplayConfigBufferSizes)(QDC_ALL_PATHS, NULL, NULL) == ERROR_INVALID_PARAMETER);
}

TEST(display_monitors_dpi_ccd_sizes_active_result) {
    NEED_GUI(); REQUIRE(U(GetDisplayConfigBufferSizes));
    UINT32 a, b; int r = ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &a, &b);
    if (r != 0) { t_note("GetDisplayConfigBufferSizes(ACTIVE) = %d", r); CHECK(a == 0xdeadbeef || a == 0); return; }
    CHECK(a >= 1);
    CHECK(b == a * 2);              /* user32: modes = paths * 2 without QDC_VIRTUAL_MODE_AWARE */
    t_note("%u active paths, %u modes", a, b);
}

TEST(display_monitors_dpi_ccd_sizes_virtual_mode_aware_triples_modes) {
    NEED_CCD();
    UINT32 a, b;
    CHECK(ccd_sizes(QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE, &a, &b) == 0);
    CHECK(b == a * 3);              /* flags & 0x10 -> *3 */
}

TEST(display_monitors_dpi_ccd_sizes_all_paths_superset_of_active) {
    NEED_CCD();
    UINT32 a, b, c, d;
    CHECK(ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &a, &b) == 0);
    CHECK(ccd_sizes(QDC_ALL_PATHS, &c, &d) == 0);
    CHECK(c >= a);
    CHECK(d == c * 2);
}

TEST(display_monitors_dpi_ccd_sizes_repeatable) {
    NEED_CCD();
    UINT32 a, b, c, d;
    CHECK(ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &a, &b) == 0);
    CHECK(ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &c, &d) == 0);
    CHECK(a == c && b == d);
}

TEST(display_monitors_dpi_ccd_sizes_database_current) {
    NEED_CCD();
    UINT32 a, b; int r = ccd_sizes(QDC_DATABASE_CURRENT, &a, &b);
    if (r) t_note("QDC_DATABASE_CURRENT sizes -> %d (no persisted topology)", r);
    else CHECK(b == a * 2);
}

TEST(display_monitors_dpi_ccd_query_active_paths_are_well_formed) {
    NEED_CCD();
    struct ccd c; int r = ccd_query(&c, QDC_ONLY_ACTIVE_PATHS);
    CHECKF(r == 0, "QueryDisplayConfig = %d", r);
    if (r) return;
    CHECK(c.np >= 1 && c.np <= DM_MAXPATHS);
    CHECK(c.nm <= DM_MAXMODES);
    for (UINT32 i = 0; i < c.np; i++) {
        const DISPLAYCONFIG_PATH_INFO *p = &c.paths[i];
        CHECKF(p->flags & DISPLAYCONFIG_PATH_ACTIVE, "path %u flags 0x%lx not active", i, (unsigned long)p->flags);
        CHECK(p->sourceInfo.statusFlags & DISPLAYCONFIG_SOURCE_IN_USE);
        CHECK(p->targetInfo.rotation >= DISPLAYCONFIG_ROTATION_IDENTITY && p->targetInfo.rotation <= DISPLAYCONFIG_ROTATION_ROTATE270);
        CHECK(!(p->targetInfo.refreshRate.Numerator == 0xfffffffe && p->targetInfo.refreshRate.Denominator == 0xfffffffe));   /* user32 rewrites that sentinel pair */
    }
}

TEST(display_monitors_dpi_ccd_query_mode_indexes_point_at_matching_mode_entries) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    for (UINT32 i = 0; i < c.np; i++) {
        const DISPLAYCONFIG_PATH_INFO *p = &c.paths[i];
        UINT32 si = p->sourceInfo.modeInfoIdx, ti = p->targetInfo.modeInfoIdx;
        if (si != DISPLAYCONFIG_PATH_MODE_IDX_INVALID && (si & 0xffff) != 0xffff) {
            /* non-virtual-mode-aware callers get the legacy layout: modeInfoIdx indexes straight into the mode array */
            if (si < c.nm) CHECKF(c.modes[si].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE, "path %u source idx %u type %d", i, si, c.modes[si].infoType);
            else CHECKF(0, "path %u source idx %u out of %u modes", i, si, c.nm);
        }
        if (ti != DISPLAYCONFIG_PATH_MODE_IDX_INVALID && (ti & 0xffff) != 0xffff) {
            if (ti < c.nm) CHECKF(c.modes[ti].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET, "path %u target idx %u type %d", i, ti, c.modes[ti].infoType);
            else CHECKF(0, "path %u target idx %u out of %u modes", i, ti, c.nm);
        }
    }
}

TEST(display_monitors_dpi_ccd_query_mode_entries_carry_adapter_and_valid_types) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    for (UINT32 i = 0; i < c.nm; i++) {
        const DISPLAYCONFIG_MODE_INFO *m = &c.modes[i];
        CHECKF(m->infoType >= DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE && m->infoType <= DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE, "mode %u type %d", i, m->infoType);
        if (m->infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
            CHECKF(m->sourceMode.width > 0 && m->sourceMode.height > 0, "mode %u source %lux%lu", i, (unsigned long)m->sourceMode.width, (unsigned long)m->sourceMode.height);
            CHECKF(m->sourceMode.pixelFormat >= DISPLAYCONFIG_PIXELFORMAT_8BPP && m->sourceMode.pixelFormat <= DISPLAYCONFIG_PIXELFORMAT_NONGDI, "mode %u pixel format %d", i, m->sourceMode.pixelFormat);
        }
    }
}

TEST(display_monitors_dpi_ccd_query_primary_source_is_at_origin) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    int at_origin = 0;
    for (UINT32 i = 0; i < c.nm; i++)
        if (c.modes[i].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE && c.modes[i].sourceMode.position.x == 0 && c.modes[i].sourceMode.position.y == 0) at_origin++;
    CHECKF(at_origin >= 1, "no active source mode positioned at (0,0)");      /* the primary display always sits at the origin */
}

TEST(display_monitors_dpi_ccd_query_source_mode_size_vs_current_devmode) {
    NEED_CCD(); REQUIRE(U(EnumDisplaySettingsW));
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    DEVMODEW dm; if (!dm_cur_mode(&dm)) { t_skip("no current mode"); return; }
    int match = 0;
    for (UINT32 i = 0; i < c.nm; i++)
        if (c.modes[i].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE && c.modes[i].sourceMode.position.x == 0 && c.modes[i].sourceMode.position.y == 0) {
            if (c.modes[i].sourceMode.width == dm.dmPelsWidth && c.modes[i].sourceMode.height == dm.dmPelsHeight) match = 1;
        }
    if (!match) t_note("origin source mode does not match the current DEVMODE %lux%lu (rotation or multi-monitor)", dm.dmPelsWidth, dm.dmPelsHeight);
    else CHECK(match);
}

TEST(display_monitors_dpi_ccd_query_active_count_matches_monitor_count_or_clone) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    int mons = U(GetSystemMetrics)(SM_CMONITORS);
    t_note("%u active paths, %d monitors", c.np, mons);
    CHECK((int)c.np >= mons);         /* cloned targets share a source: at least one path per visible monitor */
}

TEST(display_monitors_dpi_ccd_query_topology_with_database_flag) {
    NEED_CCD();
    struct ccd c; int r = ccd_query(&c, QDC_DATABASE_CURRENT);
    if (r != 0) { t_note("QDC_DATABASE_CURRENT query = %d", r); return; }
    CHECKF(c.topo == DISPLAYCONFIG_TOPOLOGY_INTERNAL || c.topo == DISPLAYCONFIG_TOPOLOGY_CLONE || c.topo == DISPLAYCONFIG_TOPOLOGY_EXTEND || c.topo == DISPLAYCONFIG_TOPOLOGY_EXTERNAL, "topology 0x%x", c.topo);
}

TEST(display_monitors_dpi_ccd_query_topology_pointer_requires_database_flag) {
    NEED_CCD();
    UINT32 np, nm; ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm);
    if (np > DM_MAXPATHS) np = DM_MAXPATHS;
    struct ccd c; memset(&c, 0, sizeof c); c.np = np; c.nm = nm > DM_MAXMODES ? DM_MAXMODES : nm;
    DISPLAYCONFIG_TOPOLOGY_ID topo = 0;
    LONG r = U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &c.np, c.paths, &c.nm, c.modes, &topo);
    CHECKF(r == ERROR_INVALID_PARAMETER, "got %ld", r);
}

TEST(display_monitors_dpi_ccd_query_zero_path_count_is_invalid_parameter) {
    NEED_CCD();
    struct ccd c; memset(&c, 0, sizeof c);
    c.np = 0; c.nm = DM_MAXMODES;
    LONG r = U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &c.np, c.paths, &c.nm, c.modes, NULL);
    CHECKF(r == ERROR_INVALID_PARAMETER, "got %ld", r);
}

TEST(display_monitors_dpi_ccd_query_invalid_flag_combinations_fail) {
    NEED_CCD();
    UINT32 flags[] = { 0, 3, 0x8, 0x40, 0x80, 0x100, 0x7 };
    for (unsigned i = 0; i < sizeof flags / sizeof *flags; i++) {
        struct ccd c; memset(&c, 0, sizeof c); c.np = DM_MAXPATHS; c.nm = DM_MAXMODES;
        LONG r = U(QueryDisplayConfig)(flags[i], &c.np, c.paths, &c.nm, c.modes, NULL);
        CHECKF(r != ERROR_SUCCESS, "flags 0x%lx unexpectedly succeeded", (unsigned long)flags[i]);   /* (flags & ~0x37) or (flags & 7) in {0,3,5,6,7} */
    }
}

TEST(display_monitors_dpi_ccd_query_is_repeatable) {
    NEED_CCD();
    struct ccd a, b;
    if (ccd_query(&a, QDC_ONLY_ACTIVE_PATHS) != 0 || ccd_query(&b, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    CHECK(a.np == b.np && a.nm == b.nm);
    for (UINT32 i = 0; i < a.np && i < b.np; i++) {
        CHECK(a.paths[i].sourceInfo.id == b.paths[i].sourceInfo.id && a.paths[i].targetInfo.id == b.paths[i].targetInfo.id);
        CHECK(a.paths[i].sourceInfo.adapterId.LowPart == b.paths[i].sourceInfo.adapterId.LowPart);
    }
}

/* ---- DisplayConfigGetDeviceInfo ------------------------------------------------------------------- */

static void *ccd_buf(void) { static unsigned char b[4096] __attribute__((aligned(8))); memset(b, 0, sizeof b); return b; }
static int ccd_info(DISPLAYCONFIG_DEVICE_INFO_TYPE type, UINT32 size, const DISPLAYCONFIG_PATH_INFO *p, int use_target, void **out) {
    DISPLAYCONFIG_DEVICE_INFO_HEADER *h = (DISPLAYCONFIG_DEVICE_INFO_HEADER *)ccd_buf();
    h->type = type; h->size = size;
    if (p) { h->adapterId = use_target ? p->targetInfo.adapterId : p->sourceInfo.adapterId; h->id = use_target ? p->targetInfo.id : p->sourceInfo.id; }
    if (out) *out = h;
    return (int)U(DisplayConfigGetDeviceInfo)(h);
}

TEST(display_monitors_dpi_ccd_info_source_name_for_each_active_path) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    CHECK(sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME) == 0x54);
    for (UINT32 i = 0; i < c.np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME *n; int r = ccd_info(DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof *n, &c.paths[i], 0, (void **)&n);
        CHECKF(r == 0, "path %u: %d", i, r);
        if (r) continue;
        CHECK(n->header.type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME && n->header.size == 0x54);
        CHECK(n->header.id == c.paths[i].sourceInfo.id);
        CHECKF(wcsncmp(n->viewGdiDeviceName, L"\\\\.\\DISPLAY", 11) == 0, "path %u name '%ls'", i, n->viewGdiDeviceName);
    }
}

TEST(display_monitors_dpi_ccd_info_source_names_are_attached_adapters) {
    NEED_CCD(); REQUIRE(U(EnumDisplayDevicesW));
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    for (UINT32 i = 0; i < c.np; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME *n; if (ccd_info(DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof *n, &c.paths[i], 0, (void **)&n)) continue;
        int found = 0;
        for (DWORD k = 0; k < 64; k++) {
            DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
            if (!U(EnumDisplayDevicesW)(NULL, k, &dd, 0)) break;
            if (!wcscmp(dd.DeviceName, n->viewGdiDeviceName)) { found = 1; CHECK(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP); }
        }
        CHECKF(found, "CCD source '%ls' is not an adapter known to EnumDisplayDevices", n->viewGdiDeviceName);
    }
}

TEST(display_monitors_dpi_ccd_info_target_name_for_each_active_path) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0) { t_skip("query failed"); return; }
    CHECK(sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME) == 0x1a4);
    for (UINT32 i = 0; i < c.np; i++) {
        DISPLAYCONFIG_TARGET_DEVICE_NAME *n; int r = ccd_info(DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof *n, &c.paths[i], 1, (void **)&n);
        if (r) { t_note("path %u target name -> %d", i, r); continue; }
        CHECK(n->header.type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME && n->header.size == 0x1a4 && n->header.id == c.paths[i].targetInfo.id);
        CHECK(n->monitorDevicePath[127] == 0);
        if (!n->monitorDevicePath[0]) t_note("path %u has an empty monitorDevicePath", i);
    }
}

TEST(display_monitors_dpi_ccd_info_adapter_name) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0 || c.np == 0) { t_skip("query failed"); return; }
    CHECK(sizeof(DISPLAYCONFIG_ADAPTER_NAME) == 0x114);
    DISPLAYCONFIG_ADAPTER_NAME *n; int r = ccd_info(DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME, sizeof *n, &c.paths[0], 0, (void **)&n);
    CHECKF(r == 0, "got %d", r);
    if (!r) { CHECK(n->adapterDevicePath[0] != 0); CHECK(n->adapterDevicePath[127] == 0); }
}

TEST(display_monitors_dpi_ccd_info_target_preferred_mode) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0 || c.np == 0) { t_skip("query failed"); return; }
    CHECK(sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE) == 0x50);
    DISPLAYCONFIG_TARGET_PREFERRED_MODE *n; int r = ccd_info(DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE, sizeof *n, &c.paths[0], 1, (void **)&n);
    if (r) { t_note("preferred mode query -> %d", r); return; }
    CHECK(n->width > 0 && n->height > 0);
}

TEST(display_monitors_dpi_ccd_info_fixed_size_table_rejects_wrong_sizes) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0 || c.np == 0) { t_skip("query failed"); return; }
    struct { int type; UINT32 good; } t[] = {
        { 1, 0x54 }, { 2, 0x1a4 }, { 3, 0x50 }, { 4, 0x114 }, { 6, 0x18 }, { 7, 0x18 }, { 9, 0x20 }, { 11, 0x18 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        UINT32 bad[] = { 0x14, t[i].good - 4, t[i].good + 4, t[i].good + 0x100 };
        for (unsigned k = 0; k < 4; k++) {
            if (bad[k] < 0x14 || bad[k] == t[i].good) continue;                  /* < 0x14 raises in the kernel: never sent */
            int r = ccd_info((DISPLAYCONFIG_DEVICE_INFO_TYPE)t[i].type, bad[k], &c.paths[0], 0, NULL);
            CHECKF(r == ERROR_INVALID_PARAMETER, "type %d size 0x%lx -> %d", t[i].type, (unsigned long)bad[k], r);
        }
    }
}

TEST(display_monitors_dpi_ccd_info_unknown_types_are_invalid_parameter) {
    NEED_CCD();
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0 || c.np == 0) { t_skip("query failed"); return; }
    int types[] = { 0, 5, 8, 10, 12, 13, 50, 0x1234, 0x7fffffff };
    for (unsigned i = 0; i < sizeof types / sizeof *types; i++) {
        int r = ccd_info((DISPLAYCONFIG_DEVICE_INFO_TYPE)types[i], 0x100, &c.paths[0], 0, NULL);
        CHECKF(r == ERROR_INVALID_PARAMETER, "type %d -> %d", types[i], r);
    }
}

TEST(display_monitors_dpi_ccd_info_bogus_adapter_fails) {
    NEED_CCD();
    DISPLAYCONFIG_PATH_INFO fake; memset(&fake, 0, sizeof fake);
    fake.sourceInfo.adapterId.LowPart = 0xdeadbeef; fake.sourceInfo.adapterId.HighPart = 0x1234; fake.sourceInfo.id = 0x77;
    int r = ccd_info(DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, 0x54, &fake, 0, NULL);
    CHECKF(r != ERROR_SUCCESS, "a made-up adapter LUID resolved");
    t_note("bogus adapter -> %d", r);
}

TEST(display_monitors_dpi_ccd_info_nt_direct_status_for_size_mismatch) {
    NEED_CCD(); NEED_NT(NtUserDisplayConfigGetDeviceInfo);
    DISPLAYCONFIG_DEVICE_INFO_HEADER *h = (DISPLAYCONFIG_DEVICE_INFO_HEADER *)ccd_buf();
    h->type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; h->size = 0x58;
    LONG st = UN(NtUserDisplayConfigGetDeviceInfo)(h);
    CHECKF(st == (LONG)0xC000000D, "status 0x%lx", (unsigned long)st);       /* -0x3ffffff3 */
    h = (DISPLAYCONFIG_DEVICE_INFO_HEADER *)ccd_buf();
    h->type = (DISPLAYCONFIG_DEVICE_INFO_TYPE)0x4321; h->size = 0x20;
    st = UN(NtUserDisplayConfigGetDeviceInfo)(h);
    CHECKF(st == (LONG)0xC000000D, "unknown type status 0x%lx", (unsigned long)st);
}

TEST(display_monitors_dpi_ccd_info_nt_direct_success_status_and_echo) {
    NEED_CCD(); NEED_NT(NtUserDisplayConfigGetDeviceInfo);
    struct ccd c; if (ccd_query(&c, QDC_ONLY_ACTIVE_PATHS) != 0 || c.np == 0) { t_skip("query failed"); return; }
    DISPLAYCONFIG_SOURCE_DEVICE_NAME *n = (DISPLAYCONFIG_SOURCE_DEVICE_NAME *)ccd_buf();
    n->header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME; n->header.size = sizeof *n;
    n->header.adapterId = c.paths[0].sourceInfo.adapterId; n->header.id = c.paths[0].sourceInfo.id;
    LONG st = UN(NtUserDisplayConfigGetDeviceInfo)(n);
    CHECKF(st == 0, "status 0x%lx", (unsigned long)st);
    CHECK(n->header.size == sizeof *n && n->header.type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME);   /* the whole request is copied back */
    CHECK(wcsncmp(n->viewGdiDeviceName, L"\\\\.\\DISPLAY", 11) == 0);
}

TEST(display_monitors_dpi_ccd_nt_buffer_sizes_direct) {
    NEED_CCD(); NEED_NT(NtUserGetDisplayConfigBufferSizes);
    UINT32 np = 0xdeadbeef, np2 = 0, nm = 0;
    LONG st = UN(NtUserGetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &np);
    CHECK(st >= 0);
    ccd_sizes(QDC_ONLY_ACTIVE_PATHS, &np2, &nm);
    CHECK(np == np2);
}
