/* x_dxgk_tracked_workload_vail_neg: the thunk table, the status model and the argument builders. */
#include "x.h"
#include <sddl.h>

const struct xsc xsc_tab[X_COUNT] = {
    [X_CREATE]  = { "NtDxgkCreateTrackedWorkload",            "D3DKMTCreateTrackedWorkload",            4433 },
    [X_UPDATE]  = { "NtDxgkUpdateTrackedWorkload",            "D3DKMTUpdateTrackedWorkload",            4449 },
    [X_DESTROY] = { "NtDxgkDestroyTrackedWorkload",           "D3DKMTDestroyTrackedWorkload",           4434 },
    [X_RESET]   = { "NtDxgkResetTrackedWorkloadStatistics",   "D3DKMTResetTrackedWorkloadStatistics",   4445 },
    [X_GETIDX]  = { "NtDxgkGetAvailableTrackedWorkloadIndex", "D3DKMTGetAvailableTrackedWorkloadIndex", 4439 },
    [X_GETSTAT] = { "NtDxgkGetTrackedWorkloadStatistics",     "D3DKMTGetTrackedWorkloadStatistics",     4441 },
    [X_DISPMGR] = { "NtDxgkDispMgrOperation",                 "D3DKMTDispMgrOperation",                 4435 },
    [X_DPORT]   = { "NtDxgkDisplayPortOperation",             "D3DKMTDisplayPortOperation",             4436 },
    [X_DUP]     = { "NtDxgkDuplicateHandle",                  "D3DKMTDuplicateHandle",                  4437 },
    [X_ODUPL]   = { "NtDxgkOutputDuplPresentToHwQueue",       "D3DKMTOutputDuplPresentToHwQueue",       4442 },
    [X_BLT]     = { "NtDxgkSubmitPresentBltToHwQueue",        "D3DKMTSubmitPresentBltToHwQueue",        4446 },
    [X_PRESENT] = { "NtDxgkSubmitPresentToHwQueue",           "D3DKMTSubmitPresentToHwQueue",           4447 },
    [X_PIN]     = { "NtDxgkPinResources",                     "D3DKMTPinResources",                     4443 },
    [X_UNPIN]   = { "NtDxgkUnpinResources",                   "D3DKMTUnpinResources",                   4448 },
    [X_REGVAIL] = { "NtDxgkRegisterVailProcess",              "D3DKMTRegisterVailProcess",              4444 },
    [X_VCONN]   = { "NtDxgkVailConnect",                      "D3DKMTVailConnect",                      4450 },
    [X_VDISC]   = { "NtDxgkVailDisconnect",                   "D3DKMTVailDisconnect",                   4451 },
    [X_PROMOTE] = { "NtDxgkVailPromoteCompositionSurface",    "D3DKMTVailPromoteCompositionSurface",    4452 },
};

int x_privileged(void) {
    unsigned long il = k_integrity_level();
    BOOL member = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins = NULL;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins)) {
        if (!CheckTokenMembership(NULL, admins, &member)) member = FALSE;
        FreeSid(admins);
    }
    if (il >= SECURITY_MANDATORY_HIGH_RID && member) return 1;
    if (il < SECURITY_MANDATORY_HIGH_RID) return 0;
    return -1;
}

int x_model(int sc, enum xshape sh, LONG *want, const char **why) {
    switch (sc) {
    case X_CREATE: *want = XS_NOT_SUPPORTED; return 1;
    case X_BLT:    *want = XS_NOT_IMPLEMENTED; return 1;
    case X_VCONN: case X_VDISC: *want = XS_ACCESS_DENIED; return 1;
    case X_DPORT: {
        /* the privilege gate runs before anything of the arguments is looked at */
        int p = x_privileged();
        if (p < 0) { *why = "caller is neither a high-integrity administrator nor a medium-or-lower integrity token"; return 0; }
        *want = p ? XS_INVALID_PARAMETER : XS_ACCESS_DENIED;
        return 1; }
    case X_DISPMGR:
        switch (sh) {
        case XS_BLOCK0: case XS_BLOCKS_ALL: case XS_FILL_41: case XS_FILL_01: *want = XS_INVALID_HANDLE; return 1;
        case XS_FILL_FF: *want = XS_TYPE_MISMATCH; return 1;   /* the handle field holds all ones: the current-process pseudo handle */
        default: *want = XS_INVALID_PARAMETER; return 1;
        }
    case X_REGVAIL:
        switch (sh) {
        case XS_BLOCK0: case XS_BLOCKS_ALL: case XS_FILL_FF: case XS_FILL_41: case XS_FILL_01: *want = XS_GFX_C01E0011; return 1;
        default: *want = XS_INVALID_PARAMETER; return 1;
        }
    case X_PROMOTE:
        switch (sh) {
        case XS_BLOCKS_ALL: case XS_FILL_FF: case XS_FILL_41: case XS_FILL_01: *want = XS_ACCESS_DENIED; return 1;
        case XS_WILD_ALL: *want = XS_ACCESS_VIOLATION; return 1;
        default: *want = XS_INVALID_PARAMETER; return 1;
        }
    default: *want = XS_INVALID_PARAMETER; return 1;
    }
}

static ULONG_PTR wild(int v) {
    switch (v % X_NWILD) {
    case 0: return 1;
    case 1: return 0xFFFF800000000000ull;
    case 2: return ~(ULONG_PTR)0;
    case 3: return (ULONG_PTR)x_freed();
    case 4: return 0x7FFFFFFF0000ull;
    case 5: return 0xDEADBEEF;
    case 6: return 0x10000;
    default: return 0xFFFFFFFF;
    }
}

/* n bytes filled with `fill`, ending exactly at the end of a committed page whose successor is inaccessible.
 * Every slot has its own pages, so blocks of different slots never overlap. */
void *x_blk(int slot, size_t n, int fill) {
    static unsigned char *pages[16];
    if (n > 0x1000) return NULL;
    if (!pages[slot]) {
        pages[slot] = VirtualAlloc(NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!pages[slot]) return NULL;
        DWORD old;
        VirtualProtect(pages[slot] + 0x1000, 0x1000, PAGE_NOACCESS, &old);
    }
    unsigned char *p = pages[slot] + 0x1000 - n;
    memset(p, fill, n);
    return p;
}
static void *own_block(int k, int fill) { return x_blk(k, 0x100, fill); }

void x_args(enum xshape sh, int pos, int variant, ULONG_PTR a[8]) {
    int k;
    memset(a, 0, 8 * sizeof a[0]);
    switch (sh) {
    case XS_ZERO: break;
    case XS_BLOCK0: a[0] = (ULONG_PTR)own_block(0, 0); break;
    case XS_BLOCKN: a[pos] = (ULONG_PTR)own_block(pos, 0); break;
    case XS_BLOCKS_ALL: for (k = 0; k < 8; k++) a[k] = (ULONG_PTR)own_block(k, 0); break;
    case XS_WILD0: a[0] = wild(variant); break;
    case XS_WILDN: a[pos] = wild(variant); break;
    case XS_WILD_ALL: for (k = 0; k < 8; k++) a[k] = wild(variant); break;
    case XS_FILL_FF: for (k = 0; k < 8; k++) a[k] = (ULONG_PTR)own_block(k, 0xFF); break;
    case XS_FILL_41: for (k = 0; k < 8; k++) a[k] = (ULONG_PTR)own_block(k, 0x41); break;
    case XS_FILL_01: for (k = 0; k < 8; k++) a[k] = (ULONG_PTR)own_block(k, 0x01); break;
    }
}

x_fn8 x_resolve(int sc, int via) {
    return via ? x_gdi8(xsc_tab[sc].gdi) : x_nt8(xsc_tab[sc].nt);
}
LONG x_call(int sc, int via, const ULONG_PTR a[8]) {
    x_fn8 f = x_resolve(sc, via);
    if (!f) return (LONG)0xC0000BAD;
    return f(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
}
