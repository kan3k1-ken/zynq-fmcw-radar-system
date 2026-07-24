/*
 * eth_udp.c
 * Minimal UDP/IP stack on Xilinx Zynq PS GEM (bare-metal).
 *
 * Implements:
 *   - EMAC init via xemacps driver
 *   - ARP resolution (send request, wait for reply)
 *   - IP header with checksum
 *   - UDP header
 *   - Ethernet frame TX via DMA
 *
 * Error codes (eth_init):
 *   -1  XEmacPs_LookupConfig failed
 *   -2  XEmacPs_CfgInitialize failed
 *   -3  TX BD ring create failed
 *   -4  RX BD ring create failed
 *   -5  RX BD alloc failed
 *   -6  PHY not found
 *   -7  PHY link timeout
 *   -8  RTL8211E RGMII delay configuration failed
 *   -9  GEM0 clock configuration failed
 *
 * Protocol constants:
 *   ETHERTYPE_ARP = 0x0806
 *   ETHERTYPE_IP  = 0x0800
 *   IPPROTO_UDP   = 17
 *   ARP_HW_ETHER  = 1
 *   ARP_REQUEST   = 1
 *   ARP_REPLY     = 2
 */

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sleep.h>
#include "xemacps.h"
#include "xemacps_bd.h"
#include "xemacps_bdring.h"
#include "xil_cache.h"
#include "xil_assert.h"
#include "xparameters.h"
#include "eth_udp.h"
#include "radar_protocol.h"

#ifndef ETH_VERBOSE_DIAGNOSTICS
#define ETH_VERBOSE_DIAGNOSTICS 0
#endif

#if ETH_VERBOSE_DIAGNOSTICS
#define ETH_DIAG_PRINTF(...) xil_printf(__VA_ARGS__)
#else
#define ETH_DIAG_PRINTF(...) do { if (ETH_VERBOSE_DIAGNOSTICS) xil_printf(__VA_ARGS__); } while (0)
#endif

/* ================================================================
 * Protocol constants
 * ================================================================ */
#define ETHERTYPE_ARP   0x0806
#define ETHERTYPE_IP    0x0800
#define IPPROTO_UDP     17
#define ARP_HW_ETHER    1
#define ARP_REQUEST     1
#define ARP_REPLY       2

#define ETH_HEADER_LEN  14
#define ARP_PKT_LEN     28
#define IP_HEADER_LEN   20
#define UDP_HEADER_LEN  8
#define MAX_PKT_SIZE    1536

/* ================================================================
 * Static data
 * ================================================================ */
static XEmacPs emac;
static XEmacPs_Config *emac_cfg;

static u8  mac_addr[6] = { ETH_MAC0, ETH_MAC1, ETH_MAC2,
                           ETH_MAC3, ETH_MAC4, ETH_MAC5 };
static u8  zynq_ip[4] = { ETH_ZYNQ_IP0, ETH_ZYNQ_IP1,
                          ETH_ZYNQ_IP2, ETH_ZYNQ_IP3 };
static u8  pc_ip[4]   = { ETH_PC_IP0, ETH_PC_IP1,
                          ETH_PC_IP2, ETH_PC_IP3 };
static u8  pc_mac[6]  = { 0, 0, 0, 0, 0, 0 };
static int pc_mac_resolved = 0;

/* BD rings and buffers */
#define TX_BD_COUNT  16
#define RX_BD_COUNT  4
#define TX_BUF_SIZE  1536
#define RX_BUF_SIZE  1536
#define TX_WAIT_ATTEMPTS 2000U
#define TX_WAIT_US       50U
#define TX_RECOVERY_MAX  3U

static XEmacPs_Bd  tx_bd[TX_BD_COUNT] __attribute__((aligned(64)));
static XEmacPs_Bd  rx_bd[RX_BD_COUNT] __attribute__((aligned(64)));
static u8          tx_buf[TX_BD_COUNT][TX_BUF_SIZE] __attribute__((aligned(64)));
static u8          rx_buf[RX_BD_COUNT][RX_BUF_SIZE] __attribute__((aligned(64)));

#define SLCR_LOCK_ADDR          0xF8000004U
#define SLCR_UNLOCK_ADDR        0xF8000008U
#define SLCR_GEM0_CLK_CTRL_ADDR 0xF8000140U
#define SLCR_UNLOCK_KEY         0x0000DF0DU
#define SLCR_LOCK_KEY           0x0000767BU
#define SLCR_GEM_CLK_DIV0_MASK  0x00003F00U
#define SLCR_GEM_CLK_DIV0_SHIFT 8U
#define SLCR_GEM_CLK_DIV1_SHIFT 20U
#define GEM_100M_DIV0           40U

#define RTL8211E_PAGE_SELECT_REG 0x1FU
#define RTL8211E_RGMII_DELAY_PAGE 0x0A43U
#define RTL8211E_RGMII_DELAY_REG 0x11U
#define RTL8211E_TX_DELAY_MASK   0x0002U
#define RTL8211E_RX_DELAY_MASK   0x0004U

static u32 tx_seq = 0;
static u32 eth_phy_addr;
static int eth_phy_valid;
static XEmacPs_Bd *tx_pending_bd;
static int tx_pending;
static int tx_pending_id;
static int tx_pending_reported;
static u32 tx_pending_txcnt;
static u32 tx_pending_octtxl;

#define RAW_CAPTURE_MAX_CHUNKS 65536U
#define RAW_CAPTURE_BITMAP_BYTES (RAW_CAPTURE_MAX_CHUNKS / 8U)

typedef struct {
    int armed;
    u32 session_id;
    u16 response_port;
    int ready_pending;
    u16 ready_port;
    u8 ready_repeats;
    int active;
    int complete;
    u32 expected_crc32;
    u8 received_map[RAW_CAPTURE_BITMAP_BYTES];
    eth_capture_info_t info;
} raw_capture_t;

static raw_capture_t raw_capture;
static u32 rx_frame_count;
static u32 rx_arp_count;
static u32 rx_udp_count;
static u32 rx_raw_port_count;
static u32 rx_other_logged;

static int arp_reply_to_request(const u8 *request, u32 request_len);
static int eth_tx_recover(void);

static int radar_status_send(u32 status, u32 session_id, u32 detail,
                             u16 dst_port)
{
    static u16 seq;
    u8 buffer[sizeof(radar_proto_header_t) + sizeof(radar_status_payload_t)];
    radar_proto_header_t *header = (radar_proto_header_t *)buffer;
    radar_status_payload_t *payload =
        (radar_status_payload_t *)(buffer + sizeof(radar_proto_header_t));

    memset(buffer, 0, sizeof(buffer));
    header->magic = RADAR_PROTO_MAGIC;
    header->version = RADAR_PROTO_VERSION;
    header->type = RADAR_PROTO_TYPE_STATUS;
    header->seq = seq++;
    header->payload_len = sizeof(*payload);
    payload->status = status;
    payload->session_id = session_id;
    payload->detail = detail;

    return udp_send(buffer, sizeof(buffer),
                    ETH_PC_IP0, ETH_PC_IP1, ETH_PC_IP2, ETH_PC_IP3,
                    dst_port, ETH_ZYNQ_PORT);
}

static void raw_capture_service_ready(void)
{
    if (raw_capture.ready_pending &&
        radar_status_send(RADAR_STATUS_READY, raw_capture.session_id, 0,
                          raw_capture.ready_port) == 0) {
        if (raw_capture.ready_repeats > 1U) {
            raw_capture.ready_repeats--;
        } else {
            raw_capture.ready_pending = 0;
        }
    }
}

static UINTPTR eth_rx_buffer_addr(const XEmacPs_Bd *bd)
{
    return (UINTPTR)(XEmacPs_BdGetBufAddr(bd) & XEMACPS_RXBUF_ADD_MASK);
}

static u16 get_be16(const u8 *p)
{
    return ((u16)p[0] << 8) | p[1];
}

static u32 crc32_compute(const u8 *data, u32 len)
{
    u32 crc = 0xFFFFFFFFU;
    u32 index;

    for (index = 0; index < len; index++) {
        u32 bit;

        crc ^= data[index];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }

    return ~crc;
}

static int eth_enable_rtl8211e_rgmii_rx_delay(u32 phy_addr)
{
    u16 saved_page = 0;
    u16 old_value = 0;
    u16 new_value = 0;
    u16 readback = 0;
    int status;
    int restore_status;

    status = XEmacPs_PhyRead(&emac, phy_addr, RTL8211E_PAGE_SELECT_REG,
                             &saved_page);
    if (status != XST_SUCCESS) {
        ETH_DIAG_PRINTF("[eth] RTL8211E RGMII delay page read failed=%d\n", status);
        return -1;
    }

    status = XEmacPs_PhyWrite(&emac, phy_addr, RTL8211E_PAGE_SELECT_REG,
                              RTL8211E_RGMII_DELAY_PAGE);
    if (status == XST_SUCCESS) {
        status = XEmacPs_PhyRead(&emac, phy_addr, RTL8211E_RGMII_DELAY_REG,
                                 &old_value);
    }
    if (status == XST_SUCCESS) {
        new_value = old_value | RTL8211E_RX_DELAY_MASK;
        if (new_value != old_value) {
            status = XEmacPs_PhyWrite(&emac, phy_addr,
                                      RTL8211E_RGMII_DELAY_REG, new_value);
        }
    }
    if (status == XST_SUCCESS) {
        status = XEmacPs_PhyRead(&emac, phy_addr, RTL8211E_RGMII_DELAY_REG,
                                 &readback);
    }
    restore_status = XEmacPs_PhyWrite(&emac, phy_addr,
                                      RTL8211E_PAGE_SELECT_REG, saved_page);

    if (status != XST_SUCCESS || restore_status != XST_SUCCESS) {
        ETH_DIAG_PRINTF("[eth] RTL8211E RGMII RX delay setup failed: op=%d restore=%d\n",
                   status, restore_status);
        return -1;
    }

    ETH_DIAG_PRINTF("[eth] RTL8211E RGMII delay: reg11 0x%04X -> 0x%04X (TX=%s RX=%s)\n",
               old_value, readback,
               (readback & RTL8211E_TX_DELAY_MASK) ? "ON" : "OFF",
               (readback & RTL8211E_RX_DELAY_MASK) ? "ON" : "OFF");

    return ((readback & RTL8211E_RX_DELAY_MASK) != 0U) ? 0 : -1;
}

static int eth_configure_gem0_100m_clock(void)
{
    volatile u32 *slcr_unlock = (volatile u32 *)SLCR_UNLOCK_ADDR;
    volatile u32 *slcr_lock = (volatile u32 *)SLCR_LOCK_ADDR;
    volatile u32 *gem0_clk_ctrl = (volatile u32 *)SLCR_GEM0_CLK_CTRL_ADDR;
    u32 old_value;
    u32 new_value;
    u32 readback;

    old_value = *gem0_clk_ctrl;
    new_value = (old_value & ~SLCR_GEM_CLK_DIV0_MASK) |
                (GEM_100M_DIV0 << SLCR_GEM_CLK_DIV0_SHIFT);

    *slcr_unlock = SLCR_UNLOCK_KEY;
    *gem0_clk_ctrl = new_value;
    readback = *gem0_clk_ctrl;
    *slcr_lock = SLCR_LOCK_KEY;

    ETH_DIAG_PRINTF("[eth] GEM0 clock: ctrl 0x%08X -> 0x%08X (readback 0x%08X, div0=%u div1=%u)\n",
               (unsigned)old_value, (unsigned)new_value, (unsigned)readback,
               (unsigned)((readback & SLCR_GEM_CLK_DIV0_MASK) >> SLCR_GEM_CLK_DIV0_SHIFT),
               (unsigned)((readback >> SLCR_GEM_CLK_DIV1_SHIFT) & 0x3FU));

    return ((readback & SLCR_GEM_CLK_DIV0_MASK) ==
            (GEM_100M_DIV0 << SLCR_GEM_CLK_DIV0_SHIFT)) ? 0 : -1;
}
/* ================================================================
 * Helper: IP checksum (RFC 791)
 * ================================================================ */
static u16 ip_checksum(const void *data, int len)
{
    u32 sum = 0;
    const u16 *p = (const u16 *)data;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const u8 *)p;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (u16)(~sum);
}

/* ================================================================
 * Helper: write 16-bit big-endian
 * ================================================================ */
static void put_be16(u8 *p, u16 v)
{
    p[0] = (u8)(v >> 8);
    p[1] = (u8)(v);
}

/* ================================================================
 * Build and send an Ethernet frame via DMA
 * ================================================================ */
static int frame_send(const u8 *data, u16 len)
{
    u16 tx_len = (len < 60) ? 60 : len;
    XEmacPs_Bd *bd = &tx_bd[0];
    u8 *tx_buffer = tx_buf[0];
    static int dbg_cnt = 0;
    int tx_id = dbg_cnt;
    int debug_enabled = (tx_id < 5);

    if (tx_pending) {
        Xil_DCacheInvalidateRange((UINTPTR)bd, sizeof(XEmacPs_Bd));
        if (!XEmacPs_BdIsTxUsed(bd)) {
            return -6;
        }
        tx_pending = 0;
        tx_pending_reported = 0;
    }

    Xil_DCacheInvalidateRange((UINTPTR)bd, sizeof(XEmacPs_Bd));
    if (!XEmacPs_BdIsTxUsed(bd)) {
        return -6;
    }
    memset(tx_buffer, 0, tx_len);
    memcpy(tx_buffer, data, len);
    Xil_DCacheFlushRange((UINTPTR)tx_buffer, tx_len);

    XEmacPs_BdSetAddressTx(bd, (UINTPTR)tx_buffer);
    XEmacPs_BdWrite(bd, XEMACPS_BD_STAT_OFFSET,
                    XEMACPS_TXBUF_WRAP_MASK | XEMACPS_TXBUF_LAST_MASK | tx_len);
    Xil_DCacheFlushRange((UINTPTR)bd, sizeof(XEmacPs_Bd));

    if (debug_enabled) {
        ETH_DIAG_PRINTF("[eth] TX #%d prepare: BD0=0x%08X buf=0x%08X len=%u stat=0x%08X\n",
                   tx_id, (unsigned)(UINTPTR)bd,
                   (unsigned)(UINTPTR)tx_buffer, (unsigned)tx_len,
                   (unsigned)XEmacPs_BdRead(bd, XEMACPS_BD_STAT_OFFSET));
    }

    {
        volatile u32 *g = (volatile u32 *)0xE000B000;
        u32 nw_before = g[0];
        g[0] = (nw_before & ~0x00000010U) |
               XEMACPS_NWCTRL_STATINC_MASK | XEMACPS_NWCTRL_STATWEN_MASK;
        g[0x14/4] = 0xFFFFFFFFU;
        g[0x24/4] = 0xFFFFFFFFU;
        tx_pending_txcnt = g[0x108/4];
        tx_pending_octtxl = g[0x100/4];
        if (debug_enabled) {
            ETH_DIAG_PRINTF("[eth] TX #%d queued: direct BD0 NWCTRL=0x%08X -> 0x%08X\n",
                       tx_id,
                       (unsigned)nw_before, (unsigned)g[0]);
            ETH_DIAG_PRINTF("[eth] TX #%d status cleared: TXSR=0x%08X ISR=0x%08X\n",
                       tx_id, (unsigned)g[0x14/4], (unsigned)g[0x24/4]);
        }
    }

    XEmacPs_Transmit(&emac);
    tx_pending_bd = bd;
    tx_pending = 1;
    tx_pending_id = tx_id;
    tx_pending_reported = 0;
    if (debug_enabled) {
        ETH_DIAG_PRINTF("[eth] TX #%d submitted asynchronously; continuing radar acquisition\n", tx_id);
    }
    dbg_cnt++;
    return 0;
}

/* A caller-owned packet buffer may be reused only after GEM releases its BD. */
static int eth_tx_wait(void)
{
    u32 attempt;
    XEmacPs_Bd *bd = tx_pending_bd;

    if (!tx_pending || bd == NULL) {
        return 0;
    }

    for (attempt = 0; attempt < TX_WAIT_ATTEMPTS; attempt++) {
        Xil_DCacheInvalidateRange((UINTPTR)bd, sizeof(XEmacPs_Bd));
        if (XEmacPs_BdIsTxUsed(bd)) {
            tx_pending = 0;
            tx_pending_bd = NULL;
            tx_pending_reported = 0;
            return 0;
        }
        usleep(TX_WAIT_US);
    }

    return -6;
}

static int eth_tx_recover(void)
{
    volatile u32 *gem = (volatile u32 *)0xE000B000U;
    u32 index;
    u32 nwctrl;

    xil_printf("COM17 NET TX: RECOVER\n");
    nwctrl = gem[0];
    gem[0] = nwctrl & ~(XEMACPS_NWCTRL_TXEN_MASK |
                         XEMACPS_NWCTRL_STARTTX_MASK);
    usleep(200U);

    for (index = 0; index < TX_BD_COUNT; index++) {
        u32 status = XEmacPs_BdRead(&tx_bd[index], XEMACPS_BD_STAT_OFFSET);
        status &= XEMACPS_TXBUF_WRAP_MASK;
        status |= XEMACPS_TXBUF_USED_MASK;
        XEmacPs_BdWrite(&tx_bd[index], XEMACPS_BD_STAT_OFFSET, status);
    }
    Xil_DCacheFlushRange((UINTPTR)tx_bd, sizeof(tx_bd));

    emac.TxBdRing.FreeHead = tx_bd;
    emac.TxBdRing.PreHead = tx_bd;
    emac.TxBdRing.HwHead = tx_bd;
    emac.TxBdRing.HwTail = tx_bd;
    emac.TxBdRing.PostHead = tx_bd;
    emac.TxBdRing.FreeCnt = TX_BD_COUNT;
    emac.TxBdRing.PreCnt = 0;
    emac.TxBdRing.HwCnt = 0;
    emac.TxBdRing.PostCnt = 0;
    gem[XEMACPS_TXQBASE_OFFSET / 4U] = (UINTPTR)tx_bd;
    gem[XEMACPS_TXSR_OFFSET / 4U] = 0xFFFFFFFFU;
    gem[0] = (nwctrl | XEMACPS_NWCTRL_TXEN_MASK) &
             ~XEMACPS_NWCTRL_STARTTX_MASK;

    tx_pending = 0;
    tx_pending_bd = NULL;
    tx_pending_reported = 0;
    return 0;
}

/* ================================================================
 * Build and send ARP request for PC's IP
 * Returns: 0 on success, -1 on failure
 * ================================================================ */
static int arp_send_request(void)
{
    u8 pkt[ETH_HEADER_LEN + ARP_PKT_LEN];
    int i;

    for (i = 0; i < 6; i++) {
        pkt[i]      = 0xFF;          /* dst: broadcast */
        pkt[6 + i]  = mac_addr[i];   /* src: our MAC */
    }
    put_be16(pkt + 12, ETHERTYPE_ARP);

    put_be16(pkt + 14, ARP_HW_ETHER);
    put_be16(pkt + 16, ETHERTYPE_IP);
    pkt[18] = 6;   /* HW addr len */
    pkt[19] = 4;   /* proto addr len */
    put_be16(pkt + 20, ARP_REQUEST);
    for (i = 0; i < 6; i++) pkt[22 + i] = mac_addr[i];
    for (i = 0; i < 4; i++) pkt[28 + i] = zynq_ip[i];
    for (i = 0; i < 6; i++) pkt[32 + i] = 0x00;   /* target HW: unknown */
    for (i = 0; i < 4; i++) pkt[38 + i] = pc_ip[i];

    return frame_send(pkt, ETH_HEADER_LEN + ARP_PKT_LEN);
}

static int arp_reply_to_request(const u8 *request, u32 request_len)
{
    u8 reply[ETH_HEADER_LEN + ARP_PKT_LEN];
    int index;

    if (request_len < ETH_HEADER_LEN + ARP_PKT_LEN ||
        get_be16(request + 20) != ARP_REQUEST ||
        memcmp(request + 38, zynq_ip, sizeof(zynq_ip)) != 0) {
        return ETH_CAPTURE_IDLE;
    }

    for (index = 0; index < 6; index++) {
        reply[index] = request[6 + index];
        reply[6 + index] = mac_addr[index];
        pc_mac[index] = request[6 + index];
    }
    put_be16(reply + 12, ETHERTYPE_ARP);
    put_be16(reply + 14, ARP_HW_ETHER);
    put_be16(reply + 16, ETHERTYPE_IP);
    reply[18] = 6;
    reply[19] = 4;
    put_be16(reply + 20, ARP_REPLY);
    memcpy(reply + 22, mac_addr, sizeof(mac_addr));
    memcpy(reply + 28, zynq_ip, sizeof(zynq_ip));
    memcpy(reply + 32, request + 22, 6);
    memcpy(reply + 38, request + 28, sizeof(zynq_ip));
    pc_mac_resolved = 1;

    return frame_send(reply, sizeof(reply));
}

/* ================================================================
 * Poll for ARP reply from PC, extract MAC
 * Returns: 1 if resolved, 0 if not yet, -1 on error
 * ================================================================ */
static int arp_poll_reply(void)
{
    XEmacPs_Bd *bd_ptr;
    XEmacPs_BdRing *ring = &emac.RxBdRing;
    u32 cnt;

    Xil_DCacheInvalidateRange((UINTPTR)rx_bd, sizeof(rx_bd));
    cnt = XEmacPs_BdRingFromHwRx(ring, 1, &bd_ptr);
    if (cnt == 0) return 0;

    u8 *buf = (u8 *)eth_rx_buffer_addr(bd_ptr);
    Xil_DCacheInvalidateRange((UINTPTR)buf, RX_BUF_SIZE);

    if (buf[12] == 0x08 && buf[13] == 0x06) {
        u16 op = ((u16)buf[20] << 8) | buf[21];
        if (op == ARP_REPLY) {
            int i;
            int match = 1;
            for (i = 0; i < 4; i++) {
                if (buf[38 + i] != zynq_ip[i]) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                for (i = 0; i < 6; i++) {
                    pc_mac[i] = buf[22 + i];
                }
                pc_mac_resolved = 1;
            }
        }
    }

    XEmacPs_BdRingFree(ring, 1, bd_ptr);

    {
        XEmacPs_Bd *new_bd;
        int s = XEmacPs_BdRingAlloc(ring, 1, &new_bd);
        if (s == 0) {
            XEmacPs_BdSetAddressRx(new_bd, (UINTPTR)buf);
            XEmacPs_BdSetStatus(new_bd, 0);
            XEmacPs_BdClearRxNew(new_bd);
            Xil_DCacheFlushRange((UINTPTR)new_bd, 64U);
            XEmacPs_BdRingToHw(ring, 1, new_bd);
        }
    }

    return pc_mac_resolved ? 1 : 0;
}

/* ================================================================
 * ARP resolve: send request, wait for reply
 * ================================================================ */
static int arp_resolve(void)
{
    int retry;
    volatile int delay;

    for (retry = 0; retry < 2; retry++) {
        ETH_DIAG_PRINTF("[eth] ARP send request %d...\n", retry);
        if (arp_send_request() != 0) {
            ETH_DIAG_PRINTF("[eth] ARP send FAILED\n");
            return -1;
        }
        ETH_DIAG_PRINTF("[eth] ARP waiting reply...\n");
        for (delay = 0; delay < 500000; delay++) {
            int r = arp_poll_reply();
            if (r == 1) {
                ETH_DIAG_PRINTF("[eth] ARP reply received!\n");
                return 0;
            }
            if (r < 0) {
                ETH_DIAG_PRINTF("[eth] ARP poll error\n");
            }
        }
    }
    ETH_DIAG_PRINTF("[eth] ARP timeout\n");
    return -1;
}

/* ================================================================
 * Initialize Ethernet subsystem
 * Returns 0 on success, negative code on error:
 *   -1 = LookupConfig failed
 *   -2 = CfgInitialize failed
 *   -3 = TX BD ring create failed
 *   -4 = RX BD ring create failed
 *   -5 = RX BD alloc failed
 *   -6 = PHY not found
 *   -7 = PHY link timeout
 *   -8 = ARP resolution failed
 * ================================================================ */
static void eth_assert_cb(const char8 *file, s32 line)
{
    ETH_DIAG_PRINTF("[eth] ASSERT FAIL at %s:%d\n", file, line);
}

int eth_init(void)
{
    int status;
    u32 phy_addr;
    u16 phy_id1, phy_id2;
    u16 phy_ctrl, phy_stat;
    int retry;
    volatile int delay;

    extern s32 Xil_AssertWait;
    Xil_AssertWait = 0;
    Xil_AssertSetCallback(eth_assert_cb);

    ETH_DIAG_PRINTF("[eth] LookupConfig...\n");
    emac_cfg = XEmacPs_LookupConfig(XPAR_XEMACPS_0_DEVICE_ID);
    if (!emac_cfg) { ETH_DIAG_PRINTF("[eth] LookupConfig FAILED\n"); return -1; }

    ETH_DIAG_PRINTF("[eth] CfgInitialize...\n");
    status = XEmacPs_CfgInitialize(&emac, emac_cfg,
                                    emac_cfg->BaseAddress);
    if (status != XST_SUCCESS) { ETH_DIAG_PRINTF("[eth] CfgInit FAILED=%d\n", status); return -2; }

    XEmacPs_SetMacAddress(&emac, mac_addr, 1);

    XEmacPs_SetOptions(&emac, XEMACPS_DEFAULT_OPTIONS);
    XEmacPs_SetOptions(&emac, XEMACPS_JUMBO_ENABLE_OPTION);

    ETH_DIAG_PRINTF("[eth] TX BdRingCreate...\n");
    status = XEmacPs_BdRingCreate(&emac.TxBdRing,
                          (UINTPTR)tx_bd,
                          (UINTPTR)tx_bd,
                          64, TX_BD_COUNT);
    if (status != XST_SUCCESS) { ETH_DIAG_PRINTF("[eth] TX BdRing FAILED=%d\n", status); return -3; }

    {
        XEmacPs_Bd tx_template;

        memset(&tx_template, 0, sizeof(tx_template));
        XEmacPs_BdSetTxUsed(&tx_template);
        status = XEmacPs_BdRingClone(&emac.TxBdRing, &tx_template,
                                     XEMACPS_SEND);
        if (status != XST_SUCCESS) {
            ETH_DIAG_PRINTF("[eth] TX BdRingClone FAILED=%d\n", status);
            return -3;
        }
        XEmacPs_BdWrite(&tx_bd[0], XEMACPS_BD_STAT_OFFSET,
                         XEmacPs_BdRead(&tx_bd[0], XEMACPS_BD_STAT_OFFSET) |
                         XEMACPS_TXBUF_WRAP_MASK);
        XEmacPs_BdSetTxUsed(&tx_bd[0]);
        ETH_DIAG_PRINTF("[eth] TX BD ring primed: all unused BDs marked USED\n");
    }

    ETH_DIAG_PRINTF("[eth] RX BdRingCreate...\n");
    status = XEmacPs_BdRingCreate(&emac.RxBdRing,
                          (UINTPTR)rx_bd,
                          (UINTPTR)rx_bd,
                          64, RX_BD_COUNT);
    if (status != XST_SUCCESS) { ETH_DIAG_PRINTF("[eth] RX BdRing FAILED=%d\n", status); return -4; }

    {
        XEmacPs_Bd rx_template;

        XEmacPs_BdClear(&rx_template);
        status = XEmacPs_BdRingClone(&emac.RxBdRing, &rx_template,
                                     XEMACPS_RECV);
        if (status != XST_SUCCESS) {
            ETH_DIAG_PRINTF("[eth] RX BdRingClone FAILED=%d\n", status);
            return -4;
        }
    }

    {
        XEmacPs_Bd *bd;
        int i;
        XEmacPs_BdRing *rx_ring = &emac.RxBdRing;
        ETH_DIAG_PRINTF("[eth] RX BdRingAlloc...\n");
        status = XEmacPs_BdRingAlloc(rx_ring, RX_BD_COUNT, &bd);
        if (status != 0) { ETH_DIAG_PRINTF("[eth] RX alloc FAILED=%d\n", status); return -5; }
        for (i = 0; i < RX_BD_COUNT; i++) {
            XEmacPs_Bd *b = &bd[i];
            XEmacPs_BdSetAddressRx(b, (UINTPTR)rx_buf[i]);
        }
        XEmacPs_BdRingToHw(rx_ring, RX_BD_COUNT, bd);
    }

    Xil_DCacheFlushRange((UINTPTR)tx_bd, sizeof(tx_bd));
    Xil_DCacheFlushRange((UINTPTR)rx_bd, sizeof(rx_bd));
    ETH_DIAG_PRINTF("[eth] BD rings flushed\n");

    ETH_DIAG_PRINTF("[eth] Scanning PHY...\n");

    phy_addr = 0;
    for (; phy_addr < 32; phy_addr++) {
        status = XEmacPs_PhyRead(&emac, phy_addr, 2, &phy_id1);
        if (status != XST_SUCCESS) continue;
        status = XEmacPs_PhyRead(&emac, phy_addr, 3, &phy_id2);
        if (status != XST_SUCCESS) continue;
        if (phy_id1 != 0xFFFF && phy_id2 != 0xFFFF &&
            !(phy_id1 == 0 && phy_id2 == 0)) {
            break;
        }
    }
    if (phy_addr >= 32) { ETH_DIAG_PRINTF("[eth] PHY not found\n"); return -6; }
    ETH_DIAG_PRINTF("[eth] PHY found at addr=%d, ID1=0x%04X ID2=0x%04X\n",
               (int)phy_addr, phy_id1, phy_id2);
    eth_phy_addr = phy_addr;
    eth_phy_valid = 1;

    XEmacPs_PhyRead(&emac, phy_addr, 1, &phy_stat);
    XEmacPs_PhyRead(&emac, phy_addr, 0, &phy_ctrl);
    ETH_DIAG_PRINTF("[eth] PHY stat=0x%04X ctrl=0x%04X\n", phy_stat, phy_ctrl);

    ETH_DIAG_PRINTF("[eth] PHY soft reset...\n");
    XEmacPs_PhyWrite(&emac, phy_addr, 0, 0x8000);
    for (retry = 0; retry < 50; retry++) {
        for (delay = 0; delay < 100000; delay++) { __asm__("nop"); }
        XEmacPs_PhyRead(&emac, phy_addr, 0, &phy_ctrl);
        if (!(phy_ctrl & 0x8000)) break;
    }
    ETH_DIAG_PRINTF("[eth] PHY reset done, ctrl=0x%04X\n", phy_ctrl);
    if (eth_enable_rtl8211e_rgmii_rx_delay(phy_addr) != 0) {
        return -8;
    }

    {
        volatile u32 *g = (volatile u32 *)0xE000B000;
        u32 nwcfg;
        u16 actual_speed, actual_duplex;
        int link_ok = 0;

        ETH_DIAG_PRINTF("[eth] Force 100M FD (skip Auto-Neg)...\n");
        XEmacPs_PhyWrite(&emac, phy_addr, 0, 0x0800);
        for (delay = 0; delay < 10000000; delay++) { __asm__("nop"); }
        XEmacPs_PhyWrite(&emac, phy_addr, 0, 0x2100);
        XEmacPs_PhyRead(&emac, phy_addr, 0, &phy_ctrl);
        ETH_DIAG_PRINTF("[eth] BMCR after write=0x%04X\n", phy_ctrl);
        for (retry = 0; retry < 200; retry++) {
            for (delay = 0; delay < 5000000; delay++) { __asm__("nop"); }
            XEmacPs_PhyRead(&emac, phy_addr, 0x10, &phy_stat);
            if ((retry % 20) == 0)
                ETH_DIAG_PRINTF("[eth]   try %d: PHYSTS=0x%04X\n", retry, phy_stat);
            if (phy_stat & 0x0100) { link_ok = 1; break; }
        }
        if (!link_ok) {
            ETH_DIAG_PRINTF("[eth] Force 100M FD timeout, no link\n");
            return -7;
        }
        {
            u16 bmcr;
            XEmacPs_PhyRead(&emac, phy_addr, 0, &bmcr);
            XEmacPs_PhyRead(&emac, phy_addr, 0x10, &phy_stat);
            actual_speed = 0;
            actual_duplex = 1;
            ETH_DIAG_PRINTF("[eth] Force 100M FD done: BMCR=0x%04X PHYSTS=0x%04X LINK=UP\n",
                       bmcr, phy_stat);
        }

        nwcfg = g[1];
        ETH_DIAG_PRINTF("[eth] GEM NWCFG before sync=0x%08X\n", (unsigned)nwcfg);
        XEmacPs_SetOperatingSpeed(&emac, 100);
        nwcfg = g[1];
        if (actual_duplex == 1) {
            nwcfg |= 0x00000002U;
        } else {
            nwcfg &= ~0x00000002U;
        }
        g[1] = nwcfg;
        ETH_DIAG_PRINTF("[eth] GEM NWCFG synced: %s %s = 0x%08X\n",
            (actual_speed == 1) ? "10M" : "100M",
            (actual_duplex == 1) ? "FD" : "HD",
            (unsigned)nwcfg);

        if (eth_configure_gem0_100m_clock() != 0) {
            ETH_DIAG_PRINTF("[eth] GEM0 clock readback mismatch\n");
            return -9;
        }
    }

    {
        volatile u32 *g = (volatile u32 *)0xE000B000;
        ETH_DIAG_PRINTF("[eth] GEM NWCTRL before start=0x%08X\n", (unsigned)g[0]);
    }

    ETH_DIAG_PRINTF("[eth] Link up! Starting GEM TX/RX...\n");

    {
        volatile u32 *g = (volatile u32 *)0xE000B000;
        u32 nwctrl_before;

        nwctrl_before = g[0];
        ETH_DIAG_PRINTF("[eth] === RXQBASE write experiment ===\n");
        ETH_DIAG_PRINTF("[eth] NWCTRL before = 0x%08X (RXEN=%d TXEN=%d)\n",
                   (unsigned)nwctrl_before,
                   (nwctrl_before & 0x04) ? 1 : 0,
                   (nwctrl_before & 0x08) ? 1 : 0);
        ETH_DIAG_PRINTF("[eth] RXQBASE before = 0x%08X\n", (unsigned)g[0x18/4]);

        ETH_DIAG_PRINTF("[eth] Step 1: clear RXEN, write RXQBASE...\n");
        g[0] = nwctrl_before & ~0x04U;
        ETH_DIAG_PRINTF("[eth]   NWCTRL after clear RXEN = 0x%08X\n", (unsigned)g[0]);
        g[0x18/4] = (UINTPTR)rx_bd;
        ETH_DIAG_PRINTF("[eth]   RXQBASE after write (RXEN=0) = 0x%08X\n", (unsigned)g[0x18/4]);

        ETH_DIAG_PRINTF("[eth] Step 2: set RXEN, read RXQBASE...\n");
        g[0] = nwctrl_before;
        ETH_DIAG_PRINTF("[eth]   NWCTRL after restore RXEN = 0x%08X\n", (unsigned)g[0]);
        ETH_DIAG_PRINTF("[eth]   RXQBASE after restore RXEN = 0x%08X\n", (unsigned)g[0x18/4]);

        if (g[0x18/4] == (UINTPTR)rx_bd) {
            ETH_DIAG_PRINTF("[eth] *** RXQBASE write SUCCESS! ***\n");
        } else {
            ETH_DIAG_PRINTF("[eth] *** RXQBASE write FAILED (readback=0x%08X, expected=0x%08X) ***\n",
                       (unsigned)g[0x18/4], (unsigned)(UINTPTR)rx_bd);
        }
        ETH_DIAG_PRINTF("[eth] === experiment done ===\n");
    }

    XEmacPs_Start(&emac);

    {
        volatile u32 *g = (volatile u32 *)0xE000B000;
        ETH_DIAG_PRINTF("[eth] After XEmacPs_Start: NWCTRL=0x%08X RXQBASE=0x%08X TXQBASE=0x%08X\n",
                   (unsigned)g[0], (unsigned)g[0x18/4], (unsigned)g[0x1C/4]);
    }

    memset(pc_mac, 0, sizeof(pc_mac));
    pc_mac_resolved = 0;

    {
        volatile u32 *g = (volatile u32 *)0xE000B000;
        u32 txqbase_hw = g[0x1C/4];
        u32 rxqbase_hw = g[0x18/4];
        ETH_DIAG_PRINTF("[eth] ADDR DIAGNOSIS:\n");
        ETH_DIAG_PRINTF("[eth]   tx_bd[] addr    = 0x%08X\n", (unsigned)(UINTPTR)tx_bd);
        ETH_DIAG_PRINTF("[eth]   rx_bd[] addr    = 0x%08X\n", (unsigned)(UINTPTR)rx_bd);
        ETH_DIAG_PRINTF("[eth]   TXQBASE (hw)    = 0x%08X\n", (unsigned)txqbase_hw);
        ETH_DIAG_PRINTF("[eth]   RXQBASE (hw)    = 0x%08X\n", (unsigned)rxqbase_hw);
        ETH_DIAG_PRINTF("[eth]   tx_buf[0] addr  = 0x%08X\n", (unsigned)(UINTPTR)tx_buf[0]);
        ETH_DIAG_PRINTF("[eth]   MATCH: TXQBASE==tx_bd? %s\n",
                   (txqbase_hw == (UINTPTR)tx_bd) ? "YES" : "NO *** MISMATCH ***");
    }

    ETH_DIAG_PRINTF("[eth] Init complete\n");
    return 0;
}

/* ================================================================
 * Check link status
 * ================================================================ */
int eth_link_up(void)
{
    return pc_mac_resolved;
}

void eth_capture_reset(void)
{
    memset(&raw_capture, 0, sizeof(raw_capture));
}

void eth_capture_dump_rx_state(void)
{
    volatile u32 *gem = (volatile u32 *)0xE000B000U;

    Xil_DCacheInvalidateRange((UINTPTR)rx_bd, sizeof(rx_bd));
    ETH_DIAG_PRINTF("[eth] RX diag: frames=%lu arp=%lu udp=%lu raw=%lu "
               "RXCNT=%lu OCTRXL=%lu RXSR=0x%08X ISR=0x%08X "
               "ring(H=%lu F=%lu P=%lu) BD0(addr=0x%08X stat=0x%08X)\n",
               (unsigned long)rx_frame_count,
               (unsigned long)rx_arp_count,
               (unsigned long)rx_udp_count,
               (unsigned long)rx_raw_port_count,
               (unsigned long)gem[0x158U / 4U],
               (unsigned long)gem[0x150U / 4U],
               (unsigned)gem[0x20U / 4U],
               (unsigned)gem[0x24U / 4U],
               (unsigned long)emac.RxBdRing.HwCnt,
               (unsigned long)emac.RxBdRing.FreeCnt,
               (unsigned long)emac.RxBdRing.PostCnt,
               (unsigned)XEmacPs_BdRead(&rx_bd[0], XEMACPS_BD_ADDR_OFFSET),
               (unsigned)XEmacPs_BdRead(&rx_bd[0], XEMACPS_BD_STAT_OFFSET));
}

static int eth_rx_recycle_bd(XEmacPs_Bd *bd_ptr)
{
    XEmacPs_BdRing *ring = &emac.RxBdRing;
    XEmacPs_Bd *new_bd;
    UINTPTR buffer_addr = eth_rx_buffer_addr(bd_ptr);
    int status;

    status = XEmacPs_BdRingFree(ring, 1, bd_ptr);
    if (status != XST_SUCCESS) {
        return -1;
    }

    status = XEmacPs_BdRingAlloc(ring, 1, &new_bd);
    if (status != XST_SUCCESS) {
        return -1;
    }

    XEmacPs_BdSetAddressRx(new_bd, buffer_addr);
    XEmacPs_BdSetStatus(new_bd, 0);
    XEmacPs_BdClearRxNew(new_bd);
    Xil_DCacheFlushRange((UINTPTR)new_bd, 64U);

    status = XEmacPs_BdRingToHw(ring, 1, new_bd);
    return (status == XST_SUCCESS) ? 0 : -1;
}

static int raw_capture_process(const u8 *payload, u16 payload_len,
                               u8 *dst, u32 capacity)
{
    static u32 protocol_debug_count;
    radar_proto_header_t proto_header;
    radar_raw_chunk_header_t chunk_header;
    const u8 *chunk_data;
    u32 expected_chunks;
    u32 byte_index;

    if (!raw_capture.armed) {
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    if (payload_len < sizeof(proto_header) + sizeof(chunk_header)) {
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    memcpy(&proto_header, payload, sizeof(proto_header));
    if (proto_header.magic != RADAR_PROTO_MAGIC ||
        proto_header.version != RADAR_PROTO_VERSION ||
        proto_header.type != RADAR_PROTO_TYPE_RAW_CHUNK ||
        proto_header.payload_len != payload_len - sizeof(proto_header)) {
        if (protocol_debug_count < 3U) {
            protocol_debug_count++;
            ETH_DIAG_PRINTF("[eth] raw header: len=%u magic=0x%08X ver=%u type=0x%02X "
                       "seq=%u declared=%u expected=%u\n",
                       payload_len, (unsigned)proto_header.magic,
                       proto_header.version, proto_header.type, proto_header.seq,
                       proto_header.payload_len,
                       (u16)(payload_len - sizeof(proto_header)));
        }
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    memcpy(&chunk_header, payload + sizeof(proto_header), sizeof(chunk_header));
    chunk_data = payload + sizeof(proto_header) + sizeof(chunk_header);

    expected_chunks = (chunk_header.total_len + ETH_RAW_CHUNK_DATA_MAX - 1U) /
                      ETH_RAW_CHUNK_DATA_MAX;
    if (chunk_header.total_len == 0 || chunk_header.total_len > capacity ||
        chunk_header.chunk_count != expected_chunks ||
        chunk_header.chunk_count > RAW_CAPTURE_MAX_CHUNKS ||
        chunk_header.chunk_index >= chunk_header.chunk_count ||
        chunk_header.chunk_len == 0 ||
        chunk_header.chunk_len > ETH_RAW_CHUNK_DATA_MAX ||
        chunk_header.chunk_len != payload_len - sizeof(proto_header) - sizeof(chunk_header) ||
        chunk_header.offset != (u32)chunk_header.chunk_index * ETH_RAW_CHUNK_DATA_MAX ||
        chunk_header.offset + chunk_header.chunk_len > chunk_header.total_len) {
        if (protocol_debug_count < 3U) {
            protocol_debug_count++;
            ETH_DIAG_PRINTF("[eth] raw chunk: total=%lu off=%lu idx=%u/%u len=%u expected=%lu flags=0x%04X\n",
                       (unsigned long)chunk_header.total_len,
                       (unsigned long)chunk_header.offset,
                       chunk_header.chunk_index, chunk_header.chunk_count,
                       chunk_header.chunk_len,
                       (unsigned long)(payload_len - sizeof(proto_header) - sizeof(chunk_header)),
                       chunk_header.flags);
        }
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    if (!raw_capture.active || raw_capture.info.scan_id != chunk_header.scan_id) {
        u32 session_id;
        u16 response_port;

        if (chunk_header.offset != 0 ||
            !(chunk_header.flags & RADAR_RAW_CHUNK_FLAG_FIRST)) {
            raw_capture.info.protocol_error_count++;
            return ETH_CAPTURE_ERROR;
        }
        session_id = raw_capture.session_id;
        response_port = raw_capture.response_port;
        eth_capture_reset();
        raw_capture.armed = 1;
        raw_capture.session_id = session_id;
        raw_capture.response_port = response_port;
        raw_capture.active = 1;
        raw_capture.info.scan_id = chunk_header.scan_id;
        raw_capture.info.total_len = chunk_header.total_len;
        raw_capture.expected_crc32 = chunk_header.scan_crc32;
    }

    if (raw_capture.info.total_len != chunk_header.total_len ||
        raw_capture.expected_crc32 != chunk_header.scan_crc32) {
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    if (crc32_compute(chunk_data, chunk_header.chunk_len) != chunk_header.chunk_crc32) {
        raw_capture.info.crc_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    byte_index = chunk_header.chunk_index >> 3;
    if (raw_capture.received_map[byte_index] & (1U << (chunk_header.chunk_index & 7U))) {
        raw_capture.info.duplicate_count++;
        return ETH_CAPTURE_IN_PROGRESS;
    }

    memcpy(dst + chunk_header.offset, chunk_data, chunk_header.chunk_len);
    raw_capture.received_map[byte_index] |= (u8)(1U << (chunk_header.chunk_index & 7U));
    raw_capture.info.received_len += chunk_header.chunk_len;
    raw_capture.info.packet_count++;

    if (raw_capture.info.received_len == raw_capture.info.total_len) {
        if (crc32_compute(dst, raw_capture.info.total_len) != raw_capture.expected_crc32) {
            raw_capture.info.crc_error_count++;
            raw_capture.active = 0;
            return ETH_CAPTURE_ERROR;
        }
        raw_capture.complete = 1;
        return ETH_CAPTURE_COMPLETE;
    }

    return ETH_CAPTURE_IN_PROGRESS;
}

static int raw_capture_control_process(const u8 *payload, u16 payload_len,
                                       u16 source_port)
{
    radar_proto_header_t proto_header;
    radar_control_payload_t control_payload;

    if (payload_len != sizeof(proto_header) + sizeof(control_payload)) {
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    memcpy(&proto_header, payload, sizeof(proto_header));
    memcpy(&control_payload, payload + sizeof(proto_header),
           sizeof(control_payload));
    if (proto_header.magic != RADAR_PROTO_MAGIC ||
        proto_header.version != RADAR_PROTO_VERSION ||
        proto_header.type != RADAR_PROTO_TYPE_CONTROL ||
        proto_header.payload_len != sizeof(control_payload) ||
        control_payload.command != RADAR_CONTROL_START) {
        raw_capture.info.protocol_error_count++;
        return ETH_CAPTURE_ERROR;
    }

    if (!raw_capture.armed ||
        raw_capture.session_id != control_payload.session_id) {
        eth_capture_reset();
        raw_capture.armed = 1;
        raw_capture.session_id = control_payload.session_id;
    }
    raw_capture.response_port = source_port;
    raw_capture.ready_port = source_port;
    raw_capture.ready_repeats = 3U;
    raw_capture.ready_pending = 1;
    raw_capture_service_ready();
    return ETH_CAPTURE_IDLE;
}

int eth_capture_send_process_done(void)
{
    u32 sent;
    u32 attempt;
    int status = -1;

    if (!raw_capture.armed || raw_capture.response_port == 0U) {
        return -1;
    }

    for (sent = 0; sent < 3U; sent++) {
        for (attempt = 0; attempt < TX_WAIT_ATTEMPTS; attempt++) {
            status = radar_status_send(RADAR_STATUS_PROCESS_DONE,
                                       raw_capture.session_id,
                                       raw_capture.info.scan_id,
                                       raw_capture.response_port);
            if (status == 0) {
                break;
            }
            usleep(TX_WAIT_US);
        }
        if (status != 0) {
            if (eth_tx_recover() != 0) {
                return status;
            }
            sent--;
        }
    }
    return 0;
}

int eth_capture_poll(uint8_t *dst, uint32_t capacity,
                     eth_capture_info_t *info)
{
    XEmacPs_Bd *bd_ptr;
    u32 count;
    u8 *frame;
    u32 frame_len;
    u32 ip_header_len;
    u32 udp_offset;
    u16 udp_len;
    int result = ETH_CAPTURE_IDLE;

    raw_capture_service_ready();

    if (raw_capture.complete) {
        if (info != NULL) {
            *info = raw_capture.info;
        }
        return ETH_CAPTURE_COMPLETE;
    }

    Xil_DCacheInvalidateRange((UINTPTR)rx_bd, sizeof(rx_bd));
    count = XEmacPs_BdRingFromHwRx(&emac.RxBdRing, 1, &bd_ptr);
    if (count == 0) {
        if (info != NULL) {
            *info = raw_capture.info;
        }
        return raw_capture.active ? ETH_CAPTURE_IN_PROGRESS : ETH_CAPTURE_IDLE;
    }

    frame = (u8 *)eth_rx_buffer_addr(bd_ptr);
    frame_len = XEmacPs_BdGetLength(bd_ptr);
    Xil_DCacheInvalidateRange((UINTPTR)frame, RX_BUF_SIZE);
    rx_frame_count++;

    if (frame_len >= ETH_HEADER_LEN + ARP_PKT_LEN &&
        frame[12] == 0x08 && frame[13] == 0x06) {
        rx_arp_count++;
        result = arp_reply_to_request(frame, frame_len);
    } else if (frame_len >= ETH_HEADER_LEN + IP_HEADER_LEN &&
               frame[12] == 0x08 && frame[13] == 0x00 &&
               (frame[14] >> 4) == 4 && frame[23] == IPPROTO_UDP) {
        rx_udp_count++;
        if (memcmp(frame + ETH_HEADER_LEN + 12, pc_ip, sizeof(pc_ip)) == 0) {
            memcpy(pc_mac, frame + 6, sizeof(pc_mac));
            pc_mac_resolved = 1;
        }
        ip_header_len = (u32)(frame[14] & 0x0FU) * 4U;
        udp_offset = ETH_HEADER_LEN + ip_header_len;
        if (ip_header_len >= IP_HEADER_LEN && frame_len >= udp_offset + UDP_HEADER_LEN) {
            udp_len = get_be16(frame + udp_offset + 4);
            if (udp_len >= UDP_HEADER_LEN && frame_len >= udp_offset + udp_len) {
                u16 dst_port = get_be16(frame + udp_offset + 2);
                const u8 *udp_payload = frame + udp_offset + UDP_HEADER_LEN;
                u16 udp_payload_len = udp_len - UDP_HEADER_LEN;

                if (dst_port == ETH_CONTROL_PORT) {
                    result = raw_capture_control_process(
                        udp_payload, udp_payload_len,
                        get_be16(frame + udp_offset));
                } else if (dst_port == ETH_RAW_SCAN_PORT) {
                    rx_raw_port_count++;
                    result = raw_capture_process(udp_payload, udp_payload_len,
                                                 dst, capacity);
                }
            }
        }
    } else if (rx_other_logged < 8U) {
        rx_other_logged++;
        ETH_DIAG_PRINTF("[eth] RX other: len=%lu type=%02X%02X dst=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   (unsigned long)frame_len, frame[12], frame[13],
                   frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
    }

    if (eth_rx_recycle_bd(bd_ptr) != 0) {
        result = ETH_CAPTURE_ERROR;
    }

    if (info != NULL) {
        *info = raw_capture.info;
    }
    return result;
}

/* ================================================================
 * Send UDP packet
 * ================================================================ */
int udp_send(const uint8_t *data, uint16_t len,
             uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
             uint16_t dst_port, uint16_t src_port)
{
    u8 pkt[MAX_PKT_SIZE];
    u8 *eth  = pkt;
    u8 *ip   = pkt + ETH_HEADER_LEN;
    u8 *udp  = ip + IP_HEADER_LEN;
    u8 *pay  = udp + UDP_HEADER_LEN;
    u16 total_len = IP_HEADER_LEN + UDP_HEADER_LEN + len;
    int i;
    int status;

    if (!pc_mac_resolved) return -1;
    if (total_len > MAX_PKT_SIZE - ETH_HEADER_LEN) return -2;

    memcpy(pay, data, len);

    for (i = 0; i < 6;  i++) eth[i]  = pc_mac[i];
    for (i = 0; i < 6;  i++) eth[6+i] = mac_addr[i];
    put_be16(eth + 12, ETHERTYPE_IP);

    ip[0] = 0x45;          /* Version=4, IHL=5 */
    ip[1] = 0;             /* DSCP/ECN */
    put_be16(ip + 2, total_len);
    put_be16(ip + 4, (u16)(tx_seq & 0xFFFF));
    put_be16(ip + 6, 0x4000);  /* Flags=Don't Fragment */
    ip[8] = 64;            /* TTL */
    ip[9] = IPPROTO_UDP;
    put_be16(ip + 10, 0);  /* checksum (computed below) */
    for (i = 0; i < 4; i++) ip[12 + i] = zynq_ip[i];
    ip[16] = ip0; ip[17] = ip1; ip[18] = ip2; ip[19] = ip3;

    put_be16(ip + 10, ip_checksum(ip, IP_HEADER_LEN));

    put_be16(udp + 0, src_port);
    put_be16(udp + 2, dst_port);
    put_be16(udp + 4, UDP_HEADER_LEN + len);
    put_be16(udp + 6, 0);  /* UDP checksum: 0 = no checksum */

    tx_seq++;
    status = frame_send(pkt, ETH_HEADER_LEN + total_len);
    if (status != 0) {
        return status;
    }
    return eth_tx_wait();
}

static int udp_send_retry(const uint8_t *data, u16 len)
{
    u32 attempt;
    u32 recovery;
    int status = -1;

    if (!pc_mac_resolved) {
        return -1;
    }

    for (recovery = 0; recovery <= TX_RECOVERY_MAX; recovery++) {
        for (attempt = 0; attempt < TX_WAIT_ATTEMPTS; attempt++) {
            status = udp_send(data, len,
                              ETH_PC_IP0, ETH_PC_IP1, ETH_PC_IP2, ETH_PC_IP3,
                              ETH_UDP_PORT, ETH_ZYNQ_PORT);
            if (status == 0) {
                return 0;
            }
            usleep(TX_WAIT_US);
        }
        if (recovery == TX_RECOVERY_MAX || eth_tx_recover() != 0) {
            break;
        }
    }
    return status;
}

#define RADAR_LOG_CAPACITY   65536U
#define RADAR_LOG_CHUNK_DATA 1000U
#define RADAR_ANGLE_MAP_MAX_BYTES 4096U
#define RADAR_ANGLE_MAP_CHUNK_DATA 1000U

static char radar_log[RADAR_LOG_CAPACITY];
static u32 radar_log_length;
static u32 radar_log_report_id;

void radar_udp_log_reset(void)
{
    radar_log_length = 0;
}

int radar_udp_printf(const char *format, ...)
{
    char text[1024];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    if (length <= 0 || radar_log_length >= RADAR_LOG_CAPACITY) {
        return length;
    }
    if (length >= (int)sizeof(text)) {
        length = sizeof(text) - 1;
    }
    if ((u32)length > RADAR_LOG_CAPACITY - radar_log_length) {
        length = (int)(RADAR_LOG_CAPACITY - radar_log_length);
    }

    memcpy(radar_log + radar_log_length, text, (u32)length);
    radar_log_length += (u32)length;
    return length;
}

int radar_udp_log_flush(void)
{
    static u16 seq;
    u32 offset;
    u32 report_id;
    u16 chunk_count;
    u16 chunk_index;
    u32 next_progress = 25U;

    if (radar_log_length == 0) {
        return 0;
    }

    chunk_count = (u16)((radar_log_length + RADAR_LOG_CHUNK_DATA - 1U) /
                        RADAR_LOG_CHUNK_DATA);
    report_id = ++radar_log_report_id;

    for (chunk_index = 0, offset = 0; chunk_index < chunk_count; chunk_index++) {
        u8 buffer[sizeof(radar_proto_header_t) +
                  sizeof(radar_text_chunk_header_t) + RADAR_LOG_CHUNK_DATA];
        radar_proto_header_t *header = (radar_proto_header_t *)buffer;
        radar_text_chunk_header_t *chunk =
            (radar_text_chunk_header_t *)(buffer + sizeof(*header));
        u16 chunk_len = (u16)(radar_log_length - offset);

        if (chunk_len > RADAR_LOG_CHUNK_DATA) {
            chunk_len = RADAR_LOG_CHUNK_DATA;
        }

        memset(buffer, 0, sizeof(*header) + sizeof(*chunk));
        header->magic = RADAR_PROTO_MAGIC;
        header->version = RADAR_PROTO_VERSION;
        header->type = RADAR_PROTO_TYPE_TEXT;
        header->seq = seq++;
        header->payload_len = (u16)(sizeof(*chunk) + chunk_len);
        chunk->report_id = report_id;
        chunk->chunk_index = chunk_index;
        chunk->chunk_count = chunk_count;
        memcpy(buffer + sizeof(*header) + sizeof(*chunk),
               radar_log + offset, chunk_len);

        {
            u32 copy;
            for (copy = 0; copy < 2U; copy++) {
                if (udp_send_retry(buffer,
                                   (u16)(sizeof(*header) + sizeof(*chunk) + chunk_len)) != 0) {
                    return -1;
                }
            }
        }
        offset += chunk_len;
        while (next_progress <= 100U &&
               (u32)(chunk_index + 1U) * 100U >= (u32)chunk_count * next_progress) {
            printf("COM17 NET TX: REPORT %lu%%\n", (unsigned long)next_progress);
            next_progress += 25U;
        }
    }

    radar_log_length = 0;
    return 0;
}

int radar_udp_send_angle_map(uint32_t scan_id, uint16_t range_bin,
                             uint8_t map_kind, const float *values,
                             uint16_t rows, uint16_t cols)
{
    static u16 seq;
    static u8 quantized[RADAR_ANGLE_MAP_MAX_BYTES];
    u32 total_len = (u32)rows * (u32)cols;
    u32 index;
    u32 offset;
    u16 chunk_count;
    u16 chunk_index;
    float value_min;
    float value_max;
    float scale;
    u32 map_crc32;

    if (values == NULL || rows == 0U || cols == 0U ||
        total_len == 0U || total_len > RADAR_ANGLE_MAP_MAX_BYTES ||
        (map_kind != RADAR_ANGLE_MAP_HORIZONTAL &&
         map_kind != RADAR_ANGLE_MAP_VERTICAL)) {
        return -1;
    }

    value_min = values[0];
    value_max = values[0];
    for (index = 1U; index < total_len; index++) {
        if (values[index] < value_min) value_min = values[index];
        if (values[index] > value_max) value_max = values[index];
    }

    scale = value_max - value_min;
    for (index = 0U; index < total_len; index++) {
        float normalized = (scale > 0.0f) ?
            ((values[index] - value_min) * 255.0f / scale) : 0.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 255.0f) normalized = 255.0f;
        quantized[index] = (u8)(normalized + 0.5f);
    }

    map_crc32 = crc32_compute(quantized, total_len);
    chunk_count = (u16)((total_len + RADAR_ANGLE_MAP_CHUNK_DATA - 1U) /
                        RADAR_ANGLE_MAP_CHUNK_DATA);

    for (chunk_index = 0U, offset = 0U;
         chunk_index < chunk_count;
         chunk_index++) {
        u8 buffer[sizeof(radar_proto_header_t) +
                  sizeof(radar_angle_map_chunk_header_t) +
                  RADAR_ANGLE_MAP_CHUNK_DATA];
        radar_proto_header_t *header = (radar_proto_header_t *)buffer;
        radar_angle_map_chunk_header_t *chunk =
            (radar_angle_map_chunk_header_t *)(buffer + sizeof(*header));
        u16 chunk_len = (u16)(total_len - offset);
        u32 copy;

        if (chunk_len > RADAR_ANGLE_MAP_CHUNK_DATA) {
            chunk_len = RADAR_ANGLE_MAP_CHUNK_DATA;
        }
        memset(buffer, 0, sizeof(*header) + sizeof(*chunk));
        header->magic = RADAR_PROTO_MAGIC;
        header->version = RADAR_PROTO_VERSION;
        header->type = RADAR_PROTO_TYPE_SPECTRUM;
        header->seq = seq++;
        header->payload_len = (u16)(sizeof(*chunk) + chunk_len);
        chunk->scan_id = scan_id;
        chunk->range_bin = range_bin;
        chunk->map_kind = map_kind;
        chunk->encoding = RADAR_ANGLE_MAP_ENCODING_U8_NORMALIZED;
        chunk->rows = rows;
        chunk->cols = cols;
        chunk->chunk_index = chunk_index;
        chunk->chunk_count = chunk_count;
        chunk->data_len = chunk_len;
        chunk->value_min = value_min;
        chunk->value_max = value_max;
        chunk->map_crc32 = map_crc32;
        memcpy(buffer + sizeof(*header) + sizeof(*chunk),
               quantized + offset, chunk_len);

        for (copy = 0U; copy < 2U; copy++) {
            if (udp_send_retry(buffer,
                               (u16)(sizeof(*header) + sizeof(*chunk) + chunk_len)) != 0) {
                return -1;
            }
        }
        offset += chunk_len;
    }

    return 0;
}

/* ================================================================
 * Send radar detection packet
 * ================================================================ */

static float eth_sinf_deg(float deg)
{
    float rad = deg * 0.017453293f;
    float x2 = rad * rad;
    return rad * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}

static float eth_cosf_deg(float deg)
{
    float rad = deg * 0.017453293f;
    float x2 = rad * rad;
    return 1.0f - x2 * (0.5f - x2 * (0.041666667f - x2 * 0.001388889f));
}

int radar_udp_send(uint32_t frame_id, float range_cm,
                   float az_deg, float el_deg,
                   float snr_db, int confirmed)
{
    static u16 seq = 0;
    u8 buf[sizeof(radar_proto_header_t) + sizeof(radar_detection_payload_t)];
    radar_proto_header_t *hdr = (radar_proto_header_t *)buf;
    radar_detection_payload_t *pay = (radar_detection_payload_t *)(buf + sizeof(radar_proto_header_t));

    float cos_el = eth_cosf_deg(el_deg);
    float sin_el = eth_sinf_deg(el_deg);
    float sin_az = eth_sinf_deg(az_deg);
    float cos_az = eth_cosf_deg(az_deg);

    memset(buf, 0, sizeof(buf));

    hdr->magic       = RADAR_PROTO_MAGIC;
    hdr->version     = RADAR_PROTO_VERSION;
    hdr->type        = RADAR_PROTO_TYPE_DETECTION;
    hdr->seq         = seq++;
    hdr->timestamp   = 0;
    hdr->payload_len = sizeof(radar_detection_payload_t);

    pay->frame_id  = frame_id;
    pay->range_cm  = range_cm;
    pay->az_deg    = az_deg;
    pay->el_deg    = el_deg;
    pay->x_cm      = range_cm * cos_el * sin_az;
    pay->y_cm      = range_cm * cos_el * cos_az;
    pay->z_cm      = range_cm * sin_el;
    pay->snr_db    = snr_db;
    pay->confirmed = (u8)(confirmed ? 1 : 0);

    return udp_send(buf, sizeof(buf),
                    ETH_PC_IP0, ETH_PC_IP1, ETH_PC_IP2, ETH_PC_IP3,
                    ETH_UDP_PORT, ETH_ZYNQ_PORT);
}

int radar_udp_send_summary(uint32_t scan_id, uint32_t input_bytes,
                           uint32_t packet_count, uint32_t frame_count,
                           uint32_t detection_count,
                           uint16_t saturated_frames,
                           uint16_t near_field_bins,
                           float range_cm, float az_deg, float el_deg,
                           float snr_db, int target_valid, int cfar_confirmed,
                           uint8_t summary_flags)
{
    static u16 seq;
    u8 buffer[sizeof(radar_proto_header_t) +
              sizeof(radar_scan_summary_payload_t)];
    radar_proto_header_t *header = (radar_proto_header_t *)buffer;
    radar_scan_summary_payload_t *payload =
        (radar_scan_summary_payload_t *)(buffer + sizeof(radar_proto_header_t));
    float cos_el = eth_cosf_deg(el_deg);
    float sin_el = eth_sinf_deg(el_deg);
    float sin_az = eth_sinf_deg(az_deg);
    float cos_az = eth_cosf_deg(az_deg);

    memset(buffer, 0, sizeof(buffer));
    header->magic = RADAR_PROTO_MAGIC;
    header->version = RADAR_PROTO_VERSION;
    header->type = RADAR_PROTO_TYPE_SCAN_SUMMARY;
    header->seq = seq++;
    header->payload_len = sizeof(*payload);

    payload->scan_id = scan_id;
    payload->input_bytes = input_bytes;
    payload->packet_count = packet_count;
    payload->frame_count = frame_count;
    payload->detection_count = detection_count;
    payload->saturated_frames = saturated_frames;
    payload->near_field_bins = near_field_bins;
    payload->range_cm = range_cm;
    payload->az_deg = az_deg;
    payload->el_deg = el_deg;
    payload->x_cm = range_cm * cos_el * sin_az;
    payload->y_cm = range_cm * cos_el * cos_az;
    payload->z_cm = range_cm * sin_el;
    payload->snr_db = snr_db;
    payload->target_valid = target_valid ? 1U : 0U;
    payload->cfar_confirmed = cfar_confirmed ? 1U : 0U;
    payload->reserved[0] = summary_flags;

    return udp_send_retry(buffer, sizeof(buffer));
}

int radar_udp_send_summary_reliable(uint32_t scan_id, uint32_t input_bytes,
                                    uint32_t packet_count, uint32_t frame_count,
                                    uint32_t detection_count,
                                    uint16_t saturated_frames,
                                    uint16_t near_field_bins,
                                    float range_cm, float az_deg, float el_deg,
                                    float snr_db, int target_valid, int cfar_confirmed,
                                    uint8_t summary_flags)
{
    u32 sent;
    int status = -1;

    for (sent = 0; sent < 3U; sent++) {
        status = radar_udp_send_summary(scan_id, input_bytes, packet_count,
                                        frame_count, detection_count,
                                        saturated_frames, near_field_bins,
                                        range_cm, az_deg, el_deg, snr_db,
                                        target_valid, cfar_confirmed, summary_flags);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}
