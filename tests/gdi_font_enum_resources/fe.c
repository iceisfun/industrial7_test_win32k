#include "fe.h"
#include <stdlib.h>

void fe_set_init(struct fe_set *s) { memset(s, 0, sizeof *s); s->ret = 1; }
void fe_set_free(struct fe_set *s) { free(s->it); memset(s, 0, sizeof *s); }

static int CALLBACK fe_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD ft, LPARAM lp) {
    struct fe_set *s = (struct fe_set *)lp;
    const ENUMLOGFONTEXW *e = (const ENUMLOGFONTEXW *)lf;
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 256;
        struct fe_item *p = (struct fe_item *)realloc(s->it, nc * sizeof *p);
        if (!p) return 0;
        s->it = p; s->cap = nc;
    }
    struct fe_item *it = &s->it[s->n];
    memset(it, 0, sizeof *it);
    lstrcpynW(it->face, lf->lfFaceName, LF_FACESIZE);
    lstrcpynW(it->full, e->elfFullName, LF_FULLFACESIZE);
    lstrcpynW(it->style, (const WCHAR *)e->elfStyle, LF_FACESIZE);
    lstrcpynW(it->script, (const WCHAR *)e->elfScript, LF_FACESIZE);
    it->weight = lf->lfWeight; it->italic = lf->lfItalic; it->charset = lf->lfCharSet; it->pitch = lf->lfPitchAndFamily;
    it->ftype = ft;
    it->tmPitch = tm->tmPitchAndFamily;
    if (ft & FE_TRUETYPE) {
        const NEWTEXTMETRICEXW *n = (const NEWTEXTMETRICEXW *)tm;
        it->hasntm = 1; it->ntmFlags = n->ntmTm.ntmFlags; it->sig = n->ntmFontSig; it->sizeEM = n->ntmTm.ntmSizeEM;
    }
    s->n++;
    if (s->hook) s->hook(s, it);
    if (s->stop_after > 0 && s->n >= s->stop_after) return 0;
    return s->ret;
}
int fe_find(const struct fe_set *s, const WCHAR *face) {
    int c = 0; for (int i = 0; i < s->n; i++) if (!lstrcmpiW(s->it[i].face, face)) c++; return c;
}
int fe_find_full(const struct fe_set *s, const WCHAR *full) {
    int c = 0; for (int i = 0; i < s->n; i++) if (!lstrcmpiW(s->it[i].full, full)) c++; return c;
}
int fe_enum(HDC dc, const WCHAR *face, int charset, struct fe_set *s) {
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfCharSet = (BYTE)charset;
    if (face) lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    return U(EnumFontFamiliesExW)(dc, &lf, fe_cb, (LPARAM)s, 0);
}
int fe_count_family(HDC dc, const WCHAR *face, int charset) {
    struct fe_set s; fe_set_init(&s); fe_enum(dc, face, charset, &s); int n = s.n; fe_set_free(&s); return n;
}

ULONG fe_raw_enum(HDC dc, ULONG type, const WCHAR *face, ULONG cs, BYTE **out, ULONG *size) {
    ULONG cwch = face ? (ULONG)lstrlenW(face) + 1 : 0;
    ULONG sz = 0, r = 0;
    *out = NULL; *size = 0;
    for (int tries = 0; tries < 4; tries++) {
        sz = 0;
        r = UN(NtGdiEnumFonts)(dc, type, 0, cwch, face, cs, &sz, NULL);
        if (!r) return 0;
        BYTE *b = (BYTE *)calloc(1, sz ? sz : 4);
        if (!b) return 0;
        ULONG sz2 = sz;
        r = UN(NtGdiEnumFonts)(dc, type, 0, cwch, face, cs, &sz2, b);
        if (r) { *out = b; *size = sz2; return r; }
        free(b);
        if (GetLastError() != 0x6f) return 0;
    }
    return 0;
}
int fe_raw_walk(const BYTE *buf, ULONG size, struct fe_rec **recs) {
    int n = 0, cap = 0; struct fe_rec *r = NULL; ULONG off = 0;
    while (off < size) {
        ULONG sz, cj;
        if (size - off < 12) { free(r); return -1; }
        memcpy(&sz, buf + off, 4); memcpy(&cj, buf + off + 4, 4);
        if (sz < 12 + sizeof(ENUMLOGFONTEXW) || sz > size - off || (sz & 3)) { free(r); return -1; }
        if (cj + 8 < 12 + sizeof(ENUMLOGFONTEXW) || (ULONGLONG)cj + 8 + sizeof(NEWTEXTMETRICEXW) > sz) { free(r); return -1; }
        if (n == cap) { cap = cap ? cap * 2 : 256; r = (struct fe_rec *)realloc(r, cap * sizeof *r); if (!r) return -1; }
        r[n].size = sz; r[n].cjEfdw = cj; memcpy(&r[n].ftype, buf + off + 8, 4);
        memcpy(&r[n].elf, buf + off + 12, sizeof(ENUMLOGFONTEXW));
        memcpy(&r[n].ntm, buf + off + 8 + cj, sizeof(NEWTEXTMETRICEXW));
        n++; off += sz;
    }
    if (recs) *recs = r; else free(r);
    return n;
}

int fe_is_vector_face(const WCHAR *face) {
    return !lstrcmpiW(face, L"Modern") || !lstrcmpiW(face, L"Roman") || !lstrcmpiW(face, L"Script");
}
void fe_wide(const char *a, WCHAR *w, size_t cap) { MultiByteToWideChar(CP_ACP, 0, a, -1, w, (int)cap); }
void fe_nt_path(const char *dos, WCHAR *out, size_t cap, ULONG *cwc) {
    WCHAR tmp[MAX_PATH]; fe_wide(dos, tmp, MAX_PATH);
    lstrcpyW(out, L"\\??\\"); lstrcatW(out, tmp);
    if (cwc) *cwc = (ULONG)lstrlenW(out) + 1;
}
const char *fe_windir_font(const char *leaf, char *out, size_t cap) {
    char w[MAX_PATH]; if (!GetWindowsDirectoryA(w, sizeof w)) strcpy(w, "C:\\Windows");
    snprintf(out, cap, "%s\\Fonts\\%s", w, leaf); return out;
}
int fe_file_exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
BYTE *fe_read_file(const char *path, DWORD *len) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), got = 0;
    BYTE *b = NULL;
    if (sz != INVALID_FILE_SIZE && sz < 64u * 1024 * 1024) { b = (BYTE *)malloc(sz ? sz : 1); if (b && !ReadFile(h, b, sz, &got, NULL)) { free(b); b = NULL; } }
    CloseHandle(h);
    if (b) *len = got;
    return b;
}

/* ---- minimal TrueType writer ---- */
struct wbuf { BYTE *p; size_t n; };
static void w8(struct wbuf *w, unsigned v) { w->p[w->n++] = (BYTE)v; }
static void w16(struct wbuf *w, unsigned v) { w8(w, v >> 8); w8(w, v); }
static void w32(struct wbuf *w, unsigned v) { w16(w, v >> 16); w16(w, v); }
static void wpad(struct wbuf *w) { while (w->n & 3) w8(w, 0); }
static unsigned csum(const BYTE *p, size_t n) {
    unsigned s = 0; size_t i;
    for (i = 0; i + 4 <= n; i += 4) s += ((unsigned)p[i] << 24) | (p[i+1] << 16) | (p[i+2] << 8) | p[i+3];
    if (i < n) { unsigned t = 0; int sh = 24; for (; i < n; i++, sh -= 8) t |= (unsigned)p[i] << sh; s += t; }
    return s;
}
static void wname(struct wbuf *w, const char *s) { for (; *s; s++) { w8(w, 0); w8(w, (BYTE)*s); } }

size_t fe_make_ttf(BYTE *out, size_t cap, const char *family) {
    BYTE tab[10][1024]; size_t tlen[10]; const char *tags[10] = {"OS/2","cmap","glyf","head","hhea","hmtx","loca","maxp","name","post"};
    struct wbuf w; int i; (void)cap;
    size_t fl = strlen(family);
    /* OS/2 v1 */
    w.p = tab[0]; w.n = 0;
    w16(&w,1); w16(&w,600); w16(&w,400); w16(&w,5); w16(&w,0);
    for (i = 0; i < 10; i++) w16(&w, i == 8 || i == 9 ? 50 : 100);
    w16(&w,0); for (i = 0; i < 10; i++) w8(&w, 0);
    w32(&w,1); w32(&w,0); w32(&w,0); w32(&w,0);
    w8(&w,'T'); w8(&w,'E'); w8(&w,'S'); w8(&w,'T');
    w16(&w,0x40); w16(&w,0x20); w16(&w,0x41); w16(&w,800); w16(&w,(unsigned)-200); w16(&w,0); w16(&w,800); w16(&w,200);
    w32(&w,1); w32(&w,0);
    tlen[0] = w.n;
    /* cmap: format 4, (3,1) */
    w.p = tab[1]; w.n = 0;
    w16(&w,0); w16(&w,1); w16(&w,3); w16(&w,1); w32(&w,12);
    w16(&w,4); w16(&w,40); w16(&w,0); w16(&w,6); w16(&w,4); w16(&w,1); w16(&w,2);
    w16(&w,0x20); w16(&w,0x41); w16(&w,0xFFFF); w16(&w,0);
    w16(&w,0x20); w16(&w,0x41); w16(&w,0xFFFF);
    w16(&w,(unsigned)(1 - 0x20)); w16(&w,(unsigned)(2 - 0x41)); w16(&w,1);
    w16(&w,0); w16(&w,0); w16(&w,0);
    tlen[1] = w.n;
    /* glyf: one box, used by .notdef and 'A' */
    w.p = tab[2]; w.n = 0;
    for (i = 0; i < 2; i++) {
        w16(&w,1); w16(&w,100); w16(&w,0); w16(&w,500); w16(&w,700);
        w16(&w,3); w16(&w,0);
        for (int k = 0; k < 4; k++) w8(&w, 1);
        w16(&w,100); w16(&w,400); w16(&w,0); w16(&w,(unsigned)-400);
        w16(&w,0); w16(&w,0); w16(&w,700); w16(&w,0);
        wpad(&w);
        if (i == 0) { /* glyph 1 (space) is empty, glyph 2 follows */ }
    }
    tlen[2] = w.n; /* 72 */
    /* head */
    w.p = tab[3]; w.n = 0;
    w32(&w,0x00010000); w32(&w,0x00010000); w32(&w,0); w32(&w,0x5F0F3CF5); w16(&w,3); w16(&w,1024);
    w32(&w,0); w32(&w,0); w32(&w,0); w32(&w,0);
    w16(&w,100); w16(&w,0); w16(&w,500); w16(&w,700); w16(&w,0); w16(&w,8); w16(&w,2); w16(&w,1); w16(&w,0);
    tlen[3] = w.n;
    /* hhea */
    w.p = tab[4]; w.n = 0;
    w32(&w,0x00010000); w16(&w,800); w16(&w,(unsigned)-200); w16(&w,0); w16(&w,600); w16(&w,0); w16(&w,100); w16(&w,500);
    w16(&w,1); w16(&w,0); w16(&w,0); for (i = 0; i < 4; i++) w16(&w,0); w16(&w,0); w16(&w,3);
    tlen[4] = w.n;
    /* hmtx */
    w.p = tab[5]; w.n = 0;
    for (i = 0; i < 3; i++) { w16(&w,600); w16(&w,i == 1 ? 0 : 100); }
    tlen[5] = w.n;
    /* loca (long) */
    w.p = tab[6]; w.n = 0;
    w32(&w,0); w32(&w,36); w32(&w,36); w32(&w,72);
    tlen[6] = w.n;
    /* maxp */
    w.p = tab[7]; w.n = 0;
    w32(&w,0x00010000); w16(&w,3); w16(&w,4); w16(&w,1); w16(&w,0); w16(&w,0); w16(&w,1);
    for (i = 0; i < 8; i++) w16(&w,0);
    tlen[7] = w.n;
    /* name: ids 0..6 for the Macintosh (1,0,0; one byte per character) and the Windows (3,1,0x409; UTF-16BE) platforms, in
     * platform order. Windows 10 refuses a font whose name table holds only the four Windows records for ids 1,2,4,6. */
    w.p = tab[8]; w.n = 0;
    {
        const char *str[7] = { "Test", family, "Regular", family, family, "Version 1.0", family };
        size_t off = 0, hdr = 6 + 12 * 14;
        w16(&w,0); w16(&w,14); w16(&w,(unsigned)hdr);
        for (int plat = 0; plat < 2; plat++)
            for (i = 0; i < 7; i++) {
                size_t l = strlen(str[i]) * (plat ? 2 : 1);
                w16(&w, plat ? 3 : 1); w16(&w, plat ? 1 : 0); w16(&w, plat ? 0x409 : 0); w16(&w, i); w16(&w, (unsigned)l); w16(&w, (unsigned)off);
                off += l;
            }
        for (int plat = 0; plat < 2; plat++)
            for (i = 0; i < 7; i++) { if (plat) wname(&w, str[i]); else for (const char *c = str[i]; *c; c++) w8(&w, (BYTE)*c); }
    }
    tlen[8] = w.n;
    /* post v3 */
    w.p = tab[9]; w.n = 0;
    w32(&w,0x00030000); w32(&w,0); w16(&w,(unsigned)-100); w16(&w,50); w32(&w,0); w32(&w,0); w32(&w,0); w32(&w,0); w32(&w,0);
    tlen[9] = w.n;

    w.p = out; w.n = 0;
    w32(&w,0x00010000); w16(&w,10); w16(&w,128); w16(&w,3); w16(&w,32);
    size_t off = 12 + 16 * 10, headoff = 0, headdir = 0;
    for (i = 0; i < 10; i++) {
        size_t l = tlen[i];
        w32(&w, ((unsigned)tags[i][0] << 24) | (tags[i][1] << 16) | (tags[i][2] << 8) | tags[i][3]);
        w32(&w, csum(tab[i], l)); w32(&w, (unsigned)off); w32(&w, (unsigned)l);
        if (i == 3) { headoff = off; headdir = w.n - 12; }
        off += (l + 3) & ~(size_t)3;
    }
    for (i = 0; i < 10; i++) { memcpy(out + w.n, tab[i], tlen[i]); w.n += tlen[i]; wpad(&w); }
    (void)headdir;
    unsigned total = csum(out, w.n), adj = 0xB1B0AFBAu - total;
    out[headoff + 8] = (BYTE)(adj >> 24); out[headoff + 9] = (BYTE)(adj >> 16); out[headoff + 10] = (BYTE)(adj >> 8); out[headoff + 11] = (BYTE)adj;
    return w.n;
}
void fe_family(char *out, size_t cap, int n) { snprintf(out, cap, "Win32ktT%lux%d", GetCurrentProcessId(), n); }
int fe_write_ttf(const char *family, char *path, size_t cap) {
    BYTE buf[2048]; size_t n = fe_make_ttf(buf, sizeof buf, family);
    char leaf[64]; snprintf(leaf, sizeof leaf, "%s.ttf", family);
    t_tmpfile(path, cap, leaf);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0; BOOL ok = WriteFile(h, buf, (DWORD)n, &w, NULL); CloseHandle(h);
    return ok && w == n;
}
HDC fe_screen_dc(void) { return U(GetDC)(NULL); }
int CALLBACK fe_cb_pub(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD ft, LPARAM lp) { return fe_cb(lf, tm, ft, lp); }
