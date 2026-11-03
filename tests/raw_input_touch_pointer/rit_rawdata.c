/* GetRawInputData / GetRawInputBuffer and the thread-local state behind them.
 *
 * win32kfull!NtUserGetRawInputData(hRawInput, cmd, pData, *pcb, cbSizeHeader): cbSizeHeader must be
 * 0x18 (sizeof RAWINPUTHEADER on x64) -> else ERROR_INVALID_PARAMETER; then the handle must be a
 * type-0x12 USER object (HMValidateHandle) -> else ERROR_INVALID_HANDLE; only then is cmd looked at.
 * user32!GetRawInputBuffer rejects NULL pcbSize / cbSizeHeader != 0x18 with 87 before any syscall and
 * returns 0 (storing *pcbSize = 0) without a syscall unless CLIENTTHREADINFO.fsWakeBits has QS_RAWINPUT
 * (0x400) set, which reads _CLIENTINFO+0x60 -> CLIENTTHREADINFO+6 straight out of the TEB.
 */
#include "rit.h"

static const ULONG_PTR bad_handles[] = { 0, 1, 2, 0x10001, 0x12345678, 0xffff, 0xffffffff, (ULONG_PTR)-1 };
#define NBAD (sizeof bad_handles / sizeof bad_handles[0])

/* ---- GetRawInputData --------------------------------------------------------------------- */
static void getdata_bad(UINT cmd, int with_buf) {
    REQUIRE(U(GetRawInputData));
    for (unsigned i = 0; i < NBAD; i++) {
        unsigned char buf[64]; memset(buf, 0x33, sizeof buf);
        UINT sz = with_buf ? sizeof buf : 0x1111;
        SET_ERR(0);
        UINT r = U(GetRawInputData)((HRAWINPUT)bad_handles[i], cmd, with_buf ? buf : NULL, &sz, RIT_RAWINPUT_HDR);
        CHECKF(r == (UINT)-1, "handle 0x%llx", (unsigned long long)bad_handles[i]);
        EXPECT_ERR(E_INVALID_HANDLE);
        CHECK(sz == (with_buf ? sizeof buf : 0x1111));    /* size not touched on failure */
        CHECK(buf[0] == 0x33);
    }
}
RT(getdata_badhandle_input_query)   { getdata_bad(RID_INPUT, 0); }
RT(getdata_badhandle_header_query)  { getdata_bad(RID_HEADER, 0); }
RT(getdata_badhandle_input_buf)     { getdata_bad(RID_INPUT, 1); }
RT(getdata_badhandle_header_buf)    { getdata_bad(RID_HEADER, 1); }
RT(getdata_badhandle_bogus_cmd)     { getdata_bad(0x10000004, 0); }          /* handle check precedes the command check */
RT(getdata_badhandle_zero_cmd)      { getdata_bad(0, 1); }

static void getdata_cb(UINT cb) {
    REQUIRE(U(GetRawInputData));
    UINT sz = 0x2222;
    SET_ERR(0);
    CHECK(U(GetRawInputData)((HRAWINPUT)0, RID_INPUT, NULL, &sz, cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);                 /* the header-size test is first: even a bogus handle reports 87 */
    CHECK(sz == 0x2222);
}
RT(getdata_cbheader_0)    { getdata_cb(0); }
RT(getdata_cbheader_16)   { getdata_cb(16); }
RT(getdata_cbheader_20)   { getdata_cb(20); }
RT(getdata_cbheader_23)   { getdata_cb(23); }
RT(getdata_cbheader_25)   { getdata_cb(25); }
RT(getdata_cbheader_32)   { getdata_cb(32); }
RT(getdata_cbheader_big)  { getdata_cb(0x10000000); }

RT(getdata_sizeof_header) {
    CHECK(sizeof(RAWINPUTHEADER) == RIT_RAWINPUT_HDR);
}

RT(getdata_direct_stub_order) {
    REQUIRE(UN(NtUserGetRawInputData));
    rit_gui();
    UINT sz = 8;
    SET_ERR(0);
    CHECK(UN(NtUserGetRawInputData)((HANDLE)0x4321, RID_INPUT, NULL, &sz, 24) == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtUserGetRawInputData)((HANDLE)0x4321, RID_INPUT, NULL, &sz, 16) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
}

RT(getdata_window_handle_is_wrong_type) {
    REQUIRE(U(GetRawInputData));
    HWND h = k_make_window("rd");
    if (!h) { t_skip("no window"); return; }
    UINT sz = 0;
    SET_ERR(0);
    /* a live HWND is a type-1 object, HMValidateHandle(h, 0x12) must reject it */
    CHECK(U(GetRawInputData)((HRAWINPUT)h, RID_INPUT, NULL, &sz, RIT_RAWINPUT_HDR) == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);
    U(DestroyWindow)(h);
}

RT(getdata_device_handle_is_wrong_type) {
    REQUIRE(U(GetRawInputData));
    rit_dev d[4];
    if (!rit_devices(d, 4)) { t_skip("no devices"); return; }
    UINT sz = 0;
    SET_ERR(0);
    /* a device handle is type 0x13, raw input data type 0x12 */
    CHECK(U(GetRawInputData)((HRAWINPUT)d[0].h, RID_HEADER, NULL, &sz, RIT_RAWINPUT_HDR) == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);
}

RT(devinfo_raw_data_handle_is_wrong_type_for_device_info) {
    REQUIRE(U(GetRawInputDeviceInfoW));
    HWND h = k_make_window("rd");
    if (!h) { t_skip("no window"); return; }
    UINT sz = 0;
    SET_ERR(0);
    CHECK(U(GetRawInputDeviceInfoW)((HANDLE)h, RIDI_DEVICEINFO, NULL, &sz) == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);
    U(DestroyWindow)(h);
}

/* ---- GetRawInputBuffer ------------------------------------------------------------------- */
static void getbuf_bad(PRAWINPUT p, PUINT pcb, UINT cb) {
    REQUIRE(U(GetRawInputBuffer));
    SET_ERR(0);
    CHECK(U(GetRawInputBuffer)(p, pcb, cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(getbuf_null_pcb)       { getbuf_bad(NULL, NULL, 24); }
RT(getbuf_null_pcb_buf)   { char b[64]; getbuf_bad((PRAWINPUT)b, NULL, 24); }
RT(getbuf_cb_0)  { UINT s = 5; getbuf_bad(NULL, &s, 0);  CHECK(s == 5); }
RT(getbuf_cb_16) { UINT s = 5; getbuf_bad(NULL, &s, 16); CHECK(s == 5); }
RT(getbuf_cb_20) { UINT s = 5; getbuf_bad(NULL, &s, 20); CHECK(s == 5); }
RT(getbuf_cb_32) { UINT s = 5; getbuf_bad(NULL, &s, 32); CHECK(s == 5); }
RT(getbuf_cb_ffffffff) { UINT s = 5; getbuf_bad(NULL, &s, 0xffffffff); CHECK(s == 5); }

static void *client_thread_info(void) {
    void *teb = k_teb(), *p = NULL;
    if (!k_try_read_ptr((char *)teb + TEB_Win32ClientInfo + RIT_CI_pClientThreadInfo, &p)) return NULL;
    return p;
}
RT(getbuf_clientthreadinfo_present) {
    rit_gui();
    void *cti = client_thread_info();
    if (!cti) { t_skip("no CLIENTTHREADINFO pointer (emulator)"); return; }
    unsigned short wake = 0;
    CHECK(k_try_read_u16((char *)cti + 6, &wake));
    t_note("CLIENTTHREADINFO %p fsWakeBits 0x%04x", cti, wake);
}

RT(getbuf_empty_queue_wrapper_returns_zero) {
    REQUIRE(U(GetRawInputBuffer));
    rit_unreg_all();
    rit_gui();
    void *cti = client_thread_info();
    unsigned short wake = 0;
    if (!cti || !k_try_read_u16((char *)cti + 6, &wake)) { t_skip("no CLIENTTHREADINFO"); return; }
    if (wake & 0x400) { t_skip("QS_RAWINPUT pending on this thread"); return; }
    UINT sz = 0x99;
    SET_ERR(0);
    /* no QS_RAWINPUT wake bit: user32 stores *pcbSize = 0 and returns 0 without entering the kernel */
    CHECK(U(GetRawInputBuffer)(NULL, &sz, RIT_RAWINPUT_HDR) == 0);
    CHECK(sz == 0);
    sz = 0x99;
    char buf[256]; memset(buf, 0x44, sizeof buf);
    CHECK(U(GetRawInputBuffer)((PRAWINPUT)buf, &sz, RIT_RAWINPUT_HDR) == 0);
    CHECK(sz == 0);
    CHECK(buf[0] == 0x44);
}

RT(getbuf_queue_status_matches_wake_bit) {
    REQUIRE(U(GetQueueStatus));
    rit_unreg_all();
    rit_gui();
    void *cti = client_thread_info();
    unsigned short wake = 0;
    if (!cti || !k_try_read_u16((char *)cti + 6, &wake)) { t_skip("no CLIENTTHREADINFO"); return; }
    DWORD qs = U(GetQueueStatus)(QS_RAWINPUT);
    unsigned short wake2 = 0;
    k_try_read_u16((char *)cti + 6, &wake2);
    /* QS_RAWINPUT is bit 0x400 of the low word (current wake bits) */
    if (wake == wake2) CHECK(((qs & 0xffff) & 0x400) == (wake2 & 0x400));
    else t_note("wake bits changed between reads");
}

RT(getbuf_direct_stub_bad_cb_before_deref) {
    REQUIRE(UN(NtUserGetRawInputBuffer));
    rit_gui();
    SET_ERR(0);
    /* win32kfull!NtUserGetRawInputBuffer checks cbSizeHeader == 0x18 before touching pcbSize */
    CHECK(UN(NtUserGetRawInputBuffer)(NULL, NULL, 16) == -1);
    EXPECT_ERR(E_INVALID_PARAM);
    UINT s = 7;
    SET_ERR(0);
    CHECK(UN(NtUserGetRawInputBuffer)(NULL, &s, 0) == -1);
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK(s == 7);
}

RT(getbuf_direct_stub_empty_queue_query) {
    REQUIRE(UN(NtUserGetRawInputBuffer));
    rit_unreg_all();
    rit_gui();
    void *cti = client_thread_info();
    unsigned short wake = 0;
    if (cti && k_try_read_u16((char *)cti + 6, &wake) && (wake & 0x400)) { t_skip("raw input pending"); return; }
    UINT s = 4096;
    SET_ERR(0);
    /* nothing queued: *pcb = 0, returns 0 */
    CHECK(UN(NtUserGetRawInputBuffer)(NULL, &s, 24) == 0);
    CHECK(s == 0);
}

RT(getbuf_direct_stub_empty_queue_with_buffer) {
    REQUIRE(UN(NtUserGetRawInputBuffer));
    rit_unreg_all();
    rit_gui();
    void *cti = client_thread_info();
    unsigned short wake = 0;
    if (cti && k_try_read_u16((char *)cti + 6, &wake) && (wake & 0x400)) { t_skip("raw input pending"); return; }
    char buf[512]; memset(buf, 0x55, sizeof buf);
    UINT s = sizeof buf;
    CHECK(UN(NtUserGetRawInputBuffer)((PRAWINPUT)buf, &s, 24) == 0);      /* zero records copied */
    CHECK(buf[0] == 0x55);
    s = 0;
    SET_ERR(0);
 /* *pcb == 0 can never hold a record: 0 < 0 is false and a buffer was given -> 122 */
    CHECK(UN(NtUserGetRawInputBuffer)((PRAWINPUT)buf, &s, 24) == -1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(s == 0);
}

/* A hidden target window registered with a sink never makes GetRawInputBuffer misbehave. */
RT(getbuf_registered_sink_then_drain) {
    REQUIRE(UN(NtUserGetRawInputBuffer));
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rb");
    if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE d = { 0xFF00, 1, RIDEV_INPUTSINK, h };
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    char buf[1024];
    UINT s = sizeof buf;
    INT r = UN(NtUserGetRawInputBuffer)((PRAWINPUT)buf, &s, 24);
    /* vendor usage 0xFF00/1 has no producing device here: no records */
    CHECK(r >= 0);
    rit_unreg_all();
    U(DestroyWindow)(h);
}
