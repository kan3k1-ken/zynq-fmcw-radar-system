#include <stdio.h>
#include <string.h>
#include "bb_driver.h"
#include "bkbase.h"
#ifdef Linux
#else
#include "xil_printf.h"
#include "xil_cache.h"
#include "xil_exception.h"
#endif
#include "xaxidma.h"
#include "radar_config.h"
#include "frame_buffer.h"
#include "fft_proc.h"
#include "cfar_detect.h"
#include "bg_cancel.h"
#include "output_fmt.h"
#include "pre_filter.h"
#include "clutter_rem.h"
#include "eth_udp.h"

#ifndef RADAR_VERBOSE_DIAGNOSTICS
#define RADAR_VERBOSE_DIAGNOSTICS 0
#endif

#ifndef RADAR_NET_DIAGNOSTICS
#define RADAR_NET_DIAGNOSTICS 1
#endif

#if RADAR_VERBOSE_DIAGNOSTICS
#define RADAR_DIAG_PRINTF(...) printf(__VA_ARGS__)
#elif RADAR_NET_DIAGNOSTICS
#define RADAR_DIAG_PRINTF(...) radar_udp_printf(__VA_ARGS__)
#else
#define RADAR_DIAG_PRINTF(...) ((void)0)
#endif

#define RADAR_PROCESS_DIAGNOSTICS \
    (RADAR_VERBOSE_DIAGNOSTICS || RADAR_NET_DIAGNOSTICS)

/* Data abort handler: captures fault address and status for debugging */
static volatile u32 g_dabt_far = 0;
static volatile u32 g_dabt_fsr = 0;
static volatile u32 g_dabt_hit = 0;

static void data_abort_handler(void *data)
{
    /* Read DFAR and DFSR from CP15 */
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(g_dabt_far));
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(g_dabt_fsr));
    g_dabt_hit = 1;

    /* Try to print via FIFO (direct register access, no printf) */
    {
        volatile u32 *uart_sr   = (volatile u32*)0xE000002Cu;
        volatile u32 *uart_fifo = (volatile u32*)0xE0000030u;
        const char *msg = "\n!!! DATA ABORT !!!\nFAR=";
        const char *hex = "0123456789ABCDEF";
        int k;
        for (k = 0; msg[k]; k++) {
            while (*uart_sr & 0x10) {}  /* wait TXFULL=0 */
            *uart_fifo = msg[k];
        }
        for (k = 28; k >= 0; k -= 4) {
            while (*uart_sr & 0x10) {}
            *uart_fifo = hex[(g_dabt_far >> k) & 0xF];
        }
        msg = " FSR=";
        for (k = 0; msg[k]; k++) {
            while (*uart_sr & 0x10) {}
            *uart_fifo = msg[k];
        }
        for (k = 28; k >= 0; k -= 4) {
            while (*uart_sr & 0x10) {}
            *uart_fifo = hex[(g_dabt_fsr >> k) & 0xF];
        }
        msg = "\n";
        for (k = 0; msg[k]; k++) {
            while (*uart_sr & 0x10) {}
            *uart_fifo = msg[k];
        }
    }
    while (1) {}
}

#define bk_uart_index   BK_UART_BASE       /* 1000 */
#define SUBSYS_bk_uart  BK_UART_SUBSYS      /* 1 */

static XAxiDma axi_dma;
static u32 dma_rx_buf[DMA_DATA_WORDS] __attribute__((aligned(64)));
static u32 dma_tx_buf[DMA_TX_WORDS] __attribute__((aligned(64)));

static u32 packed_complex_power(u32 packed)
{
    s32 real = (s16)(packed & 0xFFFFu);
    s32 imag = (s16)(packed >> 16);
    u64 power = ((u64)(real * real) + (u64)(imag * imag)) <<
                PL_POWER_RESCALE_SHIFT;
    return power > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (u32)power;
}

static int packed_complex_clipped(const u32 *spectrum, u32 bins)
{
    u32 bin;
    for (bin = 0; bin < bins; bin++) {
        s16 real = (s16)(spectrum[bin] & 0xFFFFu);
        s16 imag = (s16)(spectrum[bin] >> 16);
        if (real == 32767 || real == -32768 ||
            imag == 32767 || imag == -32768)
            return 1;
    }
    return 0;
}

static void com18_send_status(const char *message)
{
    while (*message != '\0') {
        bk_uart_send_byte((u8)*message++);
    }
}

int dma_init(void)
{
    int Status;
    XAxiDma_Config *cfg;

    cfg = XAxiDma_LookupConfig(XPAR_AXIDMA_0_DEVICE_ID);
    if (!cfg) {
        printf("DMA: LookupConfig failed\n");
        return -1;
    }

    Status = XAxiDma_CfgInitialize(&axi_dma, cfg);
    if (Status != XST_SUCCESS) {
        printf("DMA: CfgInitialize failed %d\n", Status);
        return -1;
    }

    printf("DMA: init OK, base=0x%08X\n", (unsigned int)cfg->BaseAddr);
    return 0;
}

static int dma_recv_start(u8 *rx_buf, u32 len)
{
    Xil_DCacheFlushRange((UINTPTR)rx_buf, len);
    if (XAxiDma_SimpleTransfer(&axi_dma, (UINTPTR)rx_buf,
                               len, XAXIDMA_DEVICE_TO_DMA) != XST_SUCCESS)
        return -1;
    return 0;
}

static int dma_recv_wait(void)
{
    int timeout = 1000000;
    while (timeout) {
        if (XAxiDma_Busy(&axi_dma, XAXIDMA_DEVICE_TO_DMA) == 0) break;
        timeout--;
    }
    if (timeout == 0) return -1;
    return 0;
}

static int find_frame_header(const volatile u8 *data, u32 len)
{
    u32 i;
    if (len < FRAME_HEADER_LEN)
        return -1;
    for (i = 0; i <= len - FRAME_HEADER_LEN; i++) {
        if (data[i] == (u8)'A') {
            int j;
            for (j = 1; j < FRAME_HEADER_LEN; j++) {
                if (data[i + j] != (u8)FRAME_HEADER[j])
                    break;
            }
            if (j == FRAME_HEADER_LEN)
                return (int)i;
        }
    }
    return -1;
}

int main()
{
    int i;
#if RADAR_INPUT_MODE == RADAR_INPUT_UART
    u8 rx_buf[8];
    int rx_nums;
#endif

    volatile u8 *store = (volatile u8 *)RX_STORE_BASE;
    u32 total = 0;
    volatile u8 *radar_data = NULL;
    u32 num_frames = 0;
    u32 udp_scan_id = 0;
    u32 udp_packet_count = 0;
    int eth_ready = 0;

    /* ============================================================
                     * System initialization
     * ============================================================ */

    /* ---- Set PS UART0 (COM17) baud to 921600 FIRST, before any printf ---- */
    /* BaudRate = 100MHz / (BAUDGEN * (BAUDDIV+1)) = 100M / (18*6) = 925925 */
    {
        #define PS_UART_CR      0xE0000000u
        #define PS_UART_BAUDGEN (0xE0000000u + 0x18)
        #define PS_UART_BAUDDIV (0xE0000000u + 0x34)
        volatile u32 *cr  = (volatile u32*)PS_UART_CR;
        volatile u32 *bgen = (volatile u32*)PS_UART_BAUDGEN;
        volatile u32 *bdiv = (volatile u32*)PS_UART_BAUDDIV;
        u32 cr_save = *cr;
        *cr = cr_save & ~0x03u;          /* disable TX/RX */
        *bgen = 18;                       /* BRGR = 18 */
        *bdiv = 5;                        /* BAUDDIV+1 = 6, 100M/(18*6)=925925 */
        *cr = cr_save;                    /* restore TX/RX */
    }

    printf("Radar Collection System\n");

    /* Install data abort catcher BEFORE any risky operations */
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_DATA_ABORT_INT,
                                  (Xil_ExceptionHandler)data_abort_handler,
                                  NULL);
    printf("Data abort handler installed\n");

    board_init();

    BB_init();

    /* ---- Init legacy UART input only when selected ---- */
#if RADAR_INPUT_MODE == RADAR_INPUT_UART
    bk_uart_init_ex(bk_uart_index, 115200);
    SubSystemChoose(SUBSYS_bk_uart);
    set_bk_mode(0);

    printf("UART init OK, DESR=0x01, loopback=OFF\n");
    printf("Waiting for external RX on COM18 at 115200 8N1...\n");
#else
    bk_uart_init_ex(bk_uart_index, 115200);
    printf("COM17 SYS: UDP input mode\n");
    com18_send_status("COM18 TX: STATUS READY\r\n");
#endif

    /* ---- Direct PL register test: confirm DMA vs bkt_forwarder ---- */
    {
        volatile u32 *bkt = (volatile u32*)0x43C00000u;
        volatile u32 *dma_tx = (volatile u32*)0x40400000u;
        volatile u32 *dma_rx = (volatile u32*)0x40400030u;
        u32 val;

        RADAR_DIAG_PRINTF("PL test: read bkt_forwarder 0x43C00000...\n");
        val = *bkt;
        RADAR_DIAG_PRINTF("PL test: bkt_forwarder = 0x%08X\n", (unsigned int)val);

        RADAR_DIAG_PRINTF("PL test: read DMA TX(MM2S) 0x40400000...\n");
        val = *dma_tx;
        RADAR_DIAG_PRINTF("PL test: DMA TX = 0x%08X\n", (unsigned int)val);

        RADAR_DIAG_PRINTF("PL test: read DMA RX(S2MM) 0x40400030...\n");
        val = *dma_rx;
        RADAR_DIAG_PRINTF("PL test: DMA RX = 0x%08X\n", (unsigned int)val);
        (void)val;
    }

    /* ---- DMA Init ---- */
    if (dma_init() != 0) {
        printf("DMA init failed, halt\n");
        while (1);
    }
    RADAR_DIAG_PRINTF("DMA init OK\n");

    /* ---- Frame Buffer Init (2D angle processing) ---- */
    RADAR_DIAG_PRINTF("Init frame buffer...\n");
    frame_buffer_init();
    RADAR_DIAG_PRINTF("Frame buffer init OK (%dx%dx%d matrix)\n", SCAN_ROWS, SCAN_COLS, FFT_HALF_SIZE);

    /*
     * bg_cancel_init() is reserved for per-position background subtraction
     * (calibration mode).  The current pipeline uses clutter_rem (global
     * per-bin mean subtraction) which is simpler and more robust for
     * single-scan processing.  Uncomment below when calibration data is
     * available.
     */
    /* ---- Background Cancellation Init (reserved) ----
    printf("Init bg cancel...\n");
    bg_cancel_init();
    printf("Background cancel init OK (base=0x%08X, %lu bytes)\n",
           BG_CANCEL_BASE, (unsigned long)BG_CANCEL_SIZE);
    */

    /* Clear legacy UART data accumulated during initialization. */
#if RADAR_INPUT_MODE == RADAR_INPUT_UART
    while (bk_uart_read(bk_uart_index, rx_buf) > 0) {
        /* discard */
    }
#endif

    /* ---- Legacy COM17 raw-byte display, UART input mode only ---- */
#if RADAR_INPUT_MODE == RADAR_INPUT_UART
    /* PS UART0 (COM17) registers: TXFIFO=0x30, SR=0x2C, TXFULL=bit4 */
    #define PS_UART_BASE      0xE0000000u
    #define PS_UART_SR        (*(volatile u32*)(PS_UART_BASE + 0x2C))
    #define PS_UART_FIFO      (*(volatile u32*)(PS_UART_BASE + 0x30))
    #define PS_UART_SR_TXFULL 0x00000010u

    #define FIFO_PUTC(c) do { \
        while (PS_UART_SR & PS_UART_SR_TXFULL) {} \
        PS_UART_FIFO = (u8)(c); \
    } while(0)

    #define FIFO_PUTDEC(v) do { \
        char _dbuf[12]; int _dp = 0; u32 _v = (u32)(v); \
        if (_v == 0) _dbuf[_dp++] = '0'; \
        else { while (_v) { _dbuf[_dp++] = '0' + (_v % 10); _v /= 10; } } \
        while (_dp) { while (PS_UART_SR & PS_UART_SR_TXFULL) {} PS_UART_FIFO = _dbuf[--_dp]; } \
    } while(0)

    static const u8 hex_tab[] = "0123456789ABCDEF";
    u32 ovfl_seen = 0;
    u32 batch     = 0;
    u32 idle_cnt     = 0;
    u32 timed_out    = 0;
    u32 data_started = 0;
    u32 flushed       = 0;
    u32 dbg_poll      = 0;   /* debug: heartbeat counter */
#endif

#if RADAR_INPUT_MODE == RADAR_INPUT_UART
    printf("RX ready, real-time display (COM17)...\n");
#else
    printf("COM17 SYS: BOOT OK\n");
#endif

    /* ---- GEM0 register access test (emulates XEmacPs_Reset steps) ---- */
#if RADAR_VERBOSE_DIAGNOSTICS
    {
        volatile u32 *gem_base = (volatile u32 *)0xE000B000;
        u32 reg;

        printf("GEM test (1/6) read NWCTRL ...\n");
        reg = gem_base[0x00/4];
        printf("  NWCTRL = 0x%08X OK\n", (unsigned)reg);

        printf("GEM test (2/6) write IDR ...\n");
        gem_base[0x2C/4] = 0x00007FFF;
        printf("  IDR write OK\n");

        printf("GEM test (3/6) write NWCTRL ...\n");
        gem_base[0x00/4] = (reg & ~0x0C);  /* clear TXEN/RXEN like Stop() */
        printf("  NWCTRL write OK\n");

        printf("GEM test (4/6) read VERSION ...\n");
        reg = gem_base[0xFC/4];
        printf("  VERSION = 0x%08X OK\n", (unsigned)reg);

        printf("GEM test (5/6) write NWCTRL+NWCFG ...\n");
        gem_base[0x00/4] = 0x00000030;  /* STATCLR | MDEN */
        reg = gem_base[0x04/4];         /* read NWCFG */
        reg &= 0x00FFC000;              /* clear MDCCLKDIV mask */
        reg |= 0x000C0002;              /* 100M | FDEN | UCASTHASHEN */
        gem_base[0x04/4] = reg;         /* write NWCFG */
        printf("  NWCFG = 0x%08X, write OK\n", (unsigned)reg);

        printf("GEM test (6/6) write DMACR ...\n");
        gem_base[0x10/4] = 0x00010001;  /* default DMA config */
        printf("  DMACR write OK\n");

        printf("GEM test (7/11) write TXSR+TXQBASE+RXSR ...\n");
        gem_base[0x14/4] = 0x000001FF;  /* TXSR */
        gem_base[0x1C/4] = 0;           /* TXQBASE */
        gem_base[0x20/4] = 0x0000000F;  /* RXSR */
        printf("  TXSR/TXQBASE/RXSR write OK\n");

        printf("GEM test (8/11) write HASHL+HASHH ...\n");
        gem_base[0x08/4] = 0;           /* HASHL */
        gem_base[0x0C/4] = 0;           /* HASHH */
        printf("  HASHL/HASHH write OK\n");

        printf("GEM test (9/11) read counters (0x100-0x1B4) ...\n");
        {
            u32 offset;
            for (offset = 0x100; offset <= 0x1B4; offset += 4) {
                reg = gem_base[offset/4];
            }
        }
        printf("  counters OK\n");

        printf("GEM test (10/11) write LADDR (0x88-0xA4) ...\n");
        {
            u32 off;
            for (off = 0x88; off <= 0xA4; off += 4) {
                gem_base[off/4] = 0;
            }
        }
        printf("  LADDR write OK\n");

        printf("GEM test (11/11) disable RX ...\n");
        reg = gem_base[0x00/4];
        gem_base[0x00/4] = reg & ~0x04;
        printf("  disable RX OK\n");
    }
#endif

    /* ---- Ethernet Init ---- */
#ifdef ETH_ENABLE
    {
        int eth_ret = eth_init();
        if (eth_ret == 0) {
            eth_ready = 1;
            printf("COM17 NET: LINK UP, UDP READY\n");
            com18_send_status("COM18 TX: NET READY\r\n");
        } else {
            printf("Ethernet init FAILED (err=%d), UDP disabled\n", eth_ret);
        }
    }
#else
    printf("Ethernet disabled (ETH_ENABLE not defined)\n");
#endif

    /* ============================================================
                     * Data reception
     *   - UDP mode receives the original UART byte stream in verified chunks.
     *   - UART mode remains a compatibility fallback.
     * ============================================================ */
#if RADAR_INPUT_MODE == RADAR_INPUT_UDP
    {
        eth_capture_info_t capture_info;
        int capture_status;
        u32 last_protocol_errors = 0;
        u32 last_crc_errors = 0;
        u32 idle_polls = 0;
        u32 next_rx_progress = 25;

        if (!eth_ready) {
            printf("UDP input requested but Ethernet is unavailable.\n");
            return -1;
        }

udp_wait_scan:
        memset(&capture_info, 0, sizeof(capture_info));
        last_protocol_errors = 0;
        last_crc_errors = 0;
        idle_polls = 0;
        next_rx_progress = 25;
        eth_capture_reset();
        printf("Waiting for START on port %u, then UDP scan on port %u...\n",
               ETH_CONTROL_PORT, ETH_RAW_SCAN_PORT);

        while (1) {
            capture_status = eth_capture_poll((u8 *)store, RX_STORE_SIZE,
                                              &capture_info);
            if (capture_info.total_len != 0) {
                while (next_rx_progress <= 100 &&
                       capture_info.received_len >=
                       (capture_info.total_len * next_rx_progress + 99) / 100) {
                    printf("COM17 RX: TRANSFER %lu%% (%lu/%lu bytes)\n",
                           (unsigned long)next_rx_progress,
                           (unsigned long)capture_info.received_len,
                           (unsigned long)capture_info.total_len);
                    next_rx_progress += 25;
                }
            }
            if (capture_status == ETH_CAPTURE_COMPLETE) {
                total = capture_info.total_len;
                udp_scan_id = capture_info.scan_id;
                udp_packet_count = capture_info.packet_count;
                Xil_DCacheFlushRange((UINTPTR)store, total);
                printf("UDP scan %lu complete: %lu bytes, %lu packets\n",
                       (unsigned long)capture_info.scan_id,
                       (unsigned long)capture_info.total_len,
                       (unsigned long)capture_info.packet_count);
                printf("COM17 RX: COMPLETE\n");
                com18_send_status("COM18 TX: SCAN RX OK\r\n");
                printf("COM17 PROC: ANALYZING SCAN\n");
                com18_send_status("COM18 TX: PROCESSING\r\n");
                break;
            }
            if (capture_status == ETH_CAPTURE_IDLE) {
                idle_polls++;
                if (idle_polls >= 500000000U) {
                    idle_polls = 0;
                    eth_capture_dump_rx_state();
                }
            } else {
                idle_polls = 0;
            }
            if (capture_info.protocol_error_count != last_protocol_errors ||
                capture_info.crc_error_count != last_crc_errors) {
                last_protocol_errors = capture_info.protocol_error_count;
                last_crc_errors = capture_info.crc_error_count;
                if (last_protocol_errors <= 3U ||
                    (last_protocol_errors != 0U &&
                     (last_protocol_errors & 0x3FU) == 0U) ||
                    (last_crc_errors != 0U &&
                     (last_crc_errors <= 3U ||
                      (last_crc_errors & 0x3FU) == 0U))) {
                    printf("UDP input errors: protocol=%lu crc=%lu\n",
                           (unsigned long)last_protocol_errors,
                           (unsigned long)last_crc_errors);
                }
            }
        }
    }
#else
    while (1) {
        rx_nums = bk_uart_read(bk_uart_index, rx_buf);
        if (rx_nums == 0) {
            if (!data_started) {
                dbg_poll++;
                if (dbg_poll >= 10000000) {
                    dbg_poll = 0;
                    printf(".\n");
                }
                continue;
            }
            if (timed_out) {
                /* Flush: keep reading until FIFO is truly empty */
                rx_nums = bk_uart_read(bk_uart_index, rx_buf);
                if (rx_nums == 0) {
                    FIFO_PUTC('\n');
                    break;
                }
                goto process_bytes;
            }
            idle_cnt++;
            if (idle_cnt > IDLE_TIMEOUT) {
                timed_out = 1;
                continue;
            }
            if (total >= RX_STORE_SIZE) {
                FIFO_PUTC('\n');
                break;
            }
            continue;
        }
        idle_cnt = 0;
        data_started = 1;
        process_bytes:

        for (i = 0; i < rx_nums; i++) {
            u8 b  = rx_buf[i];

            u8 hi = hex_tab[b >> 4];
            u8 lo = hex_tab[b & 0xF];

            /* Batch prefix "G64:" every 64 bytes */
            if (batch == 0) {
                while (PS_UART_SR & PS_UART_SR_TXFULL) {}
                PS_UART_FIFO = 'G';
                while (PS_UART_SR & PS_UART_SR_TXFULL) {}
                PS_UART_FIFO = '6';
                while (PS_UART_SR & PS_UART_SR_TXFULL) {}
                PS_UART_FIFO = '4';
                while (PS_UART_SR & PS_UART_SR_TXFULL) {}
                PS_UART_FIFO = ':';
            }

            /* Write hex_high to PS UART TX FIFO (COM17) */
            while (PS_UART_SR & PS_UART_SR_TXFULL) {}
            PS_UART_FIFO = hi;

            /* Write hex_low */
            while (PS_UART_SR & PS_UART_SR_TXFULL) {}
            PS_UART_FIFO = lo;

            /* Store to DDR as-is (original byte order) */
            if (total < RX_STORE_SIZE)
                store[total++] = b;

            batch++;
            if (batch >= 64) {
                while (PS_UART_SR & PS_UART_SR_TXFULL) {}
                PS_UART_FIFO = '\n';
                batch = 0;
            }
        }

        /* Check overflow */
        if (!ovfl_seen) {
            set_bk_mode(0);
            u32 st = get_bk_status();
            if (st & BK_UART_RX_OVERFLOW) {
                ovfl_seen = 1;
                printf("OVFL!\n");
            }
        }

        /*
         * Flush D-Cache to DDR every 4KB during reception.
         * Ensures DMA / PL / JTAG always see consistent data,
         * even if reception is interrupted mid-stream.
         * 4KB is aligned to cache-line (64B) and page-table boundaries,
         * avoiding excessive overhead from per-byte flushing.
         */
        if (total - flushed >= 4096) {
            Xil_DCacheFlushRange((UINTPTR)(store + flushed), total - flushed);
            flushed = total;
        }

        if (total >= RX_STORE_SIZE) {
            FIFO_PUTC('\n');
            break;
        }
    }

    /* Flush remaining bytes (less than 4KB) that didn't trigger the batch flush */
    if (total > flushed) {
        Xil_DCacheFlushRange((UINTPTR)(store + flushed), total - flushed);
    }
#endif

    /* ============================================================
     * Acquisition verification
     * ============================================================ */
    radar_udp_log_reset();
#if RADAR_NET_DIAGNOSTICS && !RADAR_VERBOSE_DIAGNOSTICS
#define printf(...) radar_udp_printf(__VA_ARGS__)
#endif
    RADAR_DIAG_PRINTF("\n===== ACQUISITION SUMMARY =====\n");
    RADAR_DIAG_PRINTF("Received bytes : %lu\n", (unsigned long)total);
#if RADAR_INPUT_MODE == RADAR_INPUT_UART
    RADAR_DIAG_PRINTF("FPGA RX count  : %lu\n", (unsigned long)bk_uart_get_rx_total());
#else
    RADAR_DIAG_PRINTF("Input source   : Ethernet UDP replay\n");
#endif

    if (total == 0) {
        printf("No data received, exit.\n");
        return 0;
    }

    /* ============================================================
         * Frame header detection
     *   - Locate "AllDataBack" (11 bytes) in DDR
     *   - Skip header, calculate effective data and frame count
     * ============================================================ */
    RADAR_DIAG_PRINTF("\n===== FRAME DECODE =====\n");
    {
        int header_offset = find_frame_header(store, total);
        if (header_offset < 0) {
            printf("Frame header '%s' not found in received data!\n", FRAME_HEADER);
            printf("First 64 bytes (hex):\n");
            for (i = 0; i < 64 && i < (int)total; i++) {
                printf("%02X ", store[i]);
                if ((i & 15) == 15) printf("\n");
            }
            printf("\n");
            return 0;
        }
        RADAR_DIAG_PRINTF("Header status  : VALID\n");

        radar_data = (volatile u8 *)(store + header_offset + FRAME_HEADER_LEN);
        u32 data_bytes = total - (u32)(header_offset + FRAME_HEADER_LEN);
        num_frames = data_bytes / BYTES_PER_ELEMENT;

        RADAR_DIAG_PRINTF("Header offset  : %d bytes (0x%X)\n", header_offset, header_offset);
        RADAR_DIAG_PRINTF("Payload bytes  : %lu\n", (unsigned long)data_bytes);
        RADAR_DIAG_PRINTF("Frame layout   : %lu frames x %d bytes\n",
                          (unsigned long)num_frames, BYTES_PER_ELEMENT);

        if (num_frames == 0) {
            printf("No valid frames after header, exit.\n");
            return 0;
        }

        /*
         * Memory safety: reordered data sits at RX_STORE_BASE (0x10000000).
         * SPECTRUM starts at 0x12000000 (32MB offset).  If data_bytes exceeds
         * 32MB, the reordered data tail overlaps with FFT spectrum writes.
         * Typical single-scan data is ~590KB, far below this limit.
         */
        if (data_bytes > 0x02000000u) {
            printf("WARNING: data (%lu bytes) exceeds 32MB safe limit.\n",
                   (unsigned long)data_bytes);
            printf("  RX_STORE/SPECTRUM overlap risk.  Truncating to 32MB.\n");
            data_bytes = 0x02000000u;
            num_frames = data_bytes / BYTES_PER_ELEMENT;
        }

        {
            u32 max_spectrum_frames = SPECTRUM_SIZE / FFT_OUTPUT_BYTES;
            if (num_frames > max_spectrum_frames) {
                printf("WARNING: %lu frames exceed spectrum capacity %lu; truncating.\n",
                       (unsigned long)num_frames,
                       (unsigned long)max_spectrum_frames);
                num_frames = max_spectrum_frames;
            }
        }

        /* ---- Remove frame header + Reorder data in DDR ----
         *     1. Move effective data to DDR start (physically delete header)
         *     2. Apply 4-byte reverse per word during copy (teacher's method)
         *
         *     Before: [HDR HDR HDR ... | B0 B1 B2 B3 | B4 B5 B6 B7 | ...]
         *             ^store             ^radar_data
         *     After:  [B3 B2 B1 B0 | B7 B6 B5 B4 | ... | 00 00 00 00]
         *             ^store (header removed, data reordered)          ^padding
         *
         *     DMA LE read: TDATA = {B0,B1,B2,B3} (original order restored)
         *     FPGA sees: TDATA[31:16]=Q, TDATA[15:0]=I → correct for FFT ---- */
        {
            volatile u8 *dst = store;
            volatile u8 *src = radar_data;
            u32 k;
            for (k = 0; k + 3 < data_bytes; k += 4) {
                dst[k]     = src[k + 3];  /* B3 → pos 0 */
                dst[k + 1] = src[k + 2];  /* B2 → pos 1 */
                dst[k + 2] = src[k + 1];  /* B1 → pos 2 */
                dst[k + 3] = src[k];      /* B0 → pos 3 */
            }

            /* Zero-fill remaining bytes if data_bytes not multiple of 4 */
            for (; k < data_bytes; k++) {
                dst[k] = 0x00;
            }

            /* Update radar_data to point to new location (DDR start) */
            radar_data = dst;
        }

        /* Reception already flushed store[] to DDR via streaming 4KB batches;
         * no additional flush needed here. Data is cache-coherent for DMA. */

        RADAR_DIAG_PRINTF("Decode status  : READY at 0x%08lX (32-bit byte reorder applied)\n",
                          (unsigned long)(UINTPTR)radar_data);

        /* ============================================================
         * Signal processing pipeline
         *
         *   FFT/clutter: DMA → PL FFT → store spectrum → accumulate clutter sum
         *            + near-field blanking + diagnostic output
         *   Between: compute per-bin mean (clutter profile)
         *   Detection: read spectrum from DDR → clutter removal →
         *            noise floor → OS-CFAR → peak validate →
         *            CFAR histogram → scan accumulation
         * ============================================================ */
        RADAR_DIAG_PRINTF("\n===== SIGNAL PROCESSING =====\n");
        {
            u32 frame_idx;
            u32 clean_frames = 0;
            u32 saturated_frames = 0;
            u32 pass2_frames = 0;
            u32 analyzed_frames = 0;
            u32 near_field_bins = 0;
            u32 packed_clip_frames = 0;
            u32 pass1_next_progress = 25;
            float g_az_deg = 0.0f, g_el_deg = 0.0f;
            int g_angle_estimate_available = 0;
            int g_angle_calibrated = 0;
            int pl_format_error = 0;
            volatile u32 *peak_store = (volatile u32 *)PEAK_STORE_BASE;
            volatile u32 *spectrum   = (volatile u32 *)SPECTRUM_BASE;
            volatile u32 *cfar_hist  = (volatile u32 *)CFAR_HIST_BASE;
            volatile u32 *cfar_wgt   = (volatile u32 *)CFAR_WGT_BASE;
            static u8 frame_saturated[SPECTRUM_SIZE / FFT_OUTPUT_BYTES];

            /* ---- Diagnostic: print raw I/Q stats for key frames ---- */
#if RADAR_PROCESS_DIAGNOSTICS
            printf("IQ sample checks:\n");
            {
                u32 diag_frames[] = {0, 128, 256, 512, 640};
                int diag_i;
                for (diag_i = 0; diag_i < 5; diag_i++) {
                    u32 fi = diag_frames[diag_i];
                    if (fi >= num_frames) break;
                    volatile u32 *words = (volatile u32 *)(radar_data + fi * BYTES_PER_ELEMENT);
                    u64 sum_sq = 0;
                    int w;
                    s16 i0 = 0, q0 = 0;
                    for (w = 0; w < BYTES_PER_ELEMENT / 4; w++) {
                        u32 word = words[w];
                        s16 iv = (s16)(word & 0xFFFF);
                        s16 qv = (s16)((word >> 16) & 0xFFFF);
                        if (w == 0) { i0 = iv; q0 = qv; }
                        sum_sq += (u64)((s32)iv * (s32)iv) + (u64)((s32)qv * (s32)qv);
                    }
                    printf("  F%lu: I0=%d Q0=%d | sum_sq=%llu\n",
                           (unsigned long)fi, (int)i0, (int)q0,
                           (unsigned long long)sum_sq);
                }
            }
            printf("\n");
#endif

            /* ---- Init clutter removal (per-bin sum accumulator) ---- */
            clutter_rem_init();

            /* ---- Init CFAR peak histogram ---- */
            memset((void *)cfar_hist, 0, CFAR_HIST_SIZE);
            memset((void *)cfar_wgt,  0, CFAR_WGT_SIZE);
            memset(frame_saturated, 0, num_frames);
            frame_buffer_init();

            /* ---- Init range profile accumulators (dual-view) ---- */
            {
                volatile u64 *rpa_raw = (volatile u64 *)RANGE_PROFILE_BASE;
                volatile u64 *rpa_nf  = (volatile u64 *)RANGE_PROFILE_NF_BASE;
                memset((void *)rpa_raw, 0, RANGE_PROFILE_SIZE);
                memset((void *)rpa_nf,  0, RANGE_PROFILE_NF_SIZE);
            }

            /* ================================================
             * FFT and clutter modeling
             * ================================================ */
            RADAR_DIAG_PRINTF("FFT/clutter model : RUNNING\n");
            xil_printf("COM17 PROC: FFT/CLUTTER RUNNING\n");
            {
                for (frame_idx = 0; frame_idx < num_frames; frame_idx++) {
                volatile u8 *src = radar_data + frame_idx * BYTES_PER_ELEMENT;
                int k;
                int is_saturated;

                /* Copy 256 bytes to tx_buf */
                for (k = 0; k < BYTES_PER_ELEMENT; k++) {
                    ((u8 *)dma_tx_buf)[k] = src[k];
                }

                /*
                 * Saturation detection: check raw IQ for ADC saturation.
                 * Saturated frames have clipped I/Q samples and produce
                 * distorted spectra.  Flag them for reduced confidence.
                 */
                is_saturated = saturation_detect(
                    dma_tx_buf, BYTES_PER_ELEMENT / sizeof(u32));
                /* Pre-configure S2MM receive buffer */
                if (dma_recv_start((u8 *)dma_rx_buf, FFT_OUTPUT_BYTES) != 0) {
                    printf("Frame %lu: dma_recv_start failed!\n", (unsigned long)frame_idx);
                    break;
                }

                /* Send via MM2S (blocking) */
                {
                    Xil_DCacheFlushRange((UINTPTR)dma_tx_buf, BYTES_PER_ELEMENT);
                    if (XAxiDma_SimpleTransfer(&axi_dma, (UINTPTR)dma_tx_buf,
                                               BYTES_PER_ELEMENT,
                                               XAXIDMA_DMA_TO_DEVICE) != XST_SUCCESS) {
                        printf("Frame %lu: MM2S start failed!\n", (unsigned long)frame_idx);
                        break;
                    }
                    {
                        int timeout = 1000000;
                        while (timeout) {
                            if (XAxiDma_Busy(&axi_dma, XAXIDMA_DMA_TO_DEVICE) == 0) break;
                            timeout--;
                        }
                        if (timeout == 0) {
                            printf("Frame %lu: MM2S timeout!\n", (unsigned long)frame_idx);
                            break;
                        }
                    }
                }

                /* Wait for S2MM completion */
                if (dma_recv_wait() != 0) {
                    printf("Frame %lu: S2MM timeout!\n", (unsigned long)frame_idx);
                    break;
                }

                /* Invalidate receive buffer cache */
                Xil_DCacheInvalidateRange((UINTPTR)dma_rx_buf, FFT_OUTPUT_BYTES);

                if (dma_rx_buf[FFT_OUTPUT_WORDS - 1] !=
                    PL_COMPLEX_FORMAT_MAGIC) {
                    pl_format_error = 1;
                    printf("PL format mismatch: expected CXI6 marker 0x%08lX, got 0x%08lX\n",
                           (unsigned long)PL_COMPLEX_FORMAT_MAGIC,
                           (unsigned long)dma_rx_buf[FFT_OUTPUT_WORDS - 1]);
                    break;
                }

                if (packed_complex_clipped(dma_rx_buf, FFT_HALF_SIZE)) {
                    packed_clip_frames++;
                    is_saturated = 1;
                }
                frame_saturated[frame_idx] = (u8)(is_saturated != 0);
                if (is_saturated)
                    saturated_frames++;

                {
                    u32 row = frame_idx / SCAN_COLS;
                    u32 col = frame_idx % SCAN_COLS;
                    if (row < SCAN_ROWS && col < SCAN_COLS)
                        frame_buffer_store_complex(row, col, dma_rx_buf,
                                                   !is_saturated);
                }

                for (k = 0; k < FFT_OUTPUT_WORDS; k++)
                    dma_rx_buf[k] = packed_complex_power(dma_rx_buf[k]);

                /* Store FFT spectrum to DDR */
                if (frame_idx < (SPECTRUM_SIZE / FFT_OUTPUT_BYTES)) {
                    volatile u32 *dst = spectrum + frame_idx * FFT_OUTPUT_WORDS;
                    for (k = 0; k < FFT_OUTPUT_WORDS; k++)
                        dst[k] = dma_rx_buf[k];
                    Xil_DCacheFlushRange((UINTPTR)dst, FFT_OUTPUT_BYTES);
                }

                /* Accumulate into clutter sum (for per-bin mean)
                 * NOTE: Skip saturated frames — their distorted spectra
                 * inflate near-field bins and push the adaptive boundary outward.
                 * near_field_mask is NOT applied here either — the clutter
                 * profile must capture the FULL self-coupling signature. */
                if (!is_saturated) {
                    clutter_rem_accumulate(dma_rx_buf, FFT_HALF_SIZE);
                    clean_frames++;
                }

                /* Short inter-frame delay */
                for (i = 0; i < 10000; i++) asm("nop");

                while (pass1_next_progress <= 100 &&
                       frame_idx + 1 >= (num_frames * pass1_next_progress + 99) / 100) {
                    xil_printf("COM17 PROC: FFT/CLUTTER %lu%%\n",
                               (unsigned long)pass1_next_progress);
                    pass1_next_progress += 25;
                }
            }

            RADAR_DIAG_PRINTF("Frame quality     : %lu saturated / %lu clean\n",
                              (unsigned long)saturated_frames, (unsigned long)clean_frames);
            RADAR_DIAG_PRINTF("Complex clipping  : %lu frames\n",
                              (unsigned long)packed_clip_frames);
            if (pl_format_error)
                RADAR_DIAG_PRINTF("Processing status : ABORTED / PL-FW FORMAT MISMATCH\n");
            }

            /* Compute per-bin mean from accumulated sum (clean frames only) */
            clutter_rem_compute_mean(clean_frames);

            /* ---- Adaptive near-field boundary detection ----
             * Analyzes the clutter profile to find where self-coupling
             * power drops to the noise floor.  No hardcoded bin numbers.
             * Industrial-grade: adapts to any dataset automatically. */
            {
                const u32 *clutter_profile = clutter_rem_get_mean();
                u32 adaptive_boundary = clutter_rem_find_boundary(
                    clutter_profile, FFT_HALF_SIZE);
                near_field_bins = adaptive_boundary;
                pre_filter_set_near_field_bins(adaptive_boundary);
                RADAR_DIAG_PRINTF("Near-field guard  : %lu bins (%.1f cm)\n",
                                  (unsigned long)adaptive_boundary,
                                  (double)fmt_dist_cm(adaptive_boundary));
            }

            RADAR_DIAG_PRINTF("FFT/clutter model : COMPLETE (%lu frames)\n",
                              (unsigned long)frame_idx);
            xil_printf("COM17 PROC: FFT/CLUTTER DONE\n");

            /* ================================================
             * Range detection and validation
             * ================================================ */
            RADAR_DIAG_PRINTF("Range detection   : RUNNING (clutter + noise floor + OS-CFAR)\n");
            xil_printf("COM17 PROC: RANGE DETECTION RUNNING\n");
            {
                u32 cfar_start = pre_filter_get_near_field_bins() + CFAR_HALF_WIN;
                u32 pass2_next_progress = 25;
                pass2_frames = frame_idx;
                analyzed_frames = 0;
                if (cfar_start < DC_SKIP_BINS)
                    cfar_start = DC_SKIP_BINS;

                for (frame_idx = 0; frame_idx < pass2_frames; frame_idx++) {
                    static u32 cleaned[FFT_HALF_SIZE];
                    static u32 pre_noise[FFT_HALF_SIZE];
                    if (frame_saturated[frame_idx]) {
                        memset(cleaned, 0, sizeof(cleaned));
                        peak_store[frame_idx] = 0xFFFFFFFFu;
                        while (pass2_next_progress <= 100 &&
                               frame_idx + 1 >=
                               (pass2_frames * pass2_next_progress + 99) / 100) {
                            xil_printf("COM17 PROC: RANGE DETECTION %lu%%\n",
                                       (unsigned long)pass2_next_progress);
                            pass2_next_progress += 25;
                        }
                        continue;
                    }

                    analyzed_frames++;

                    /* Read spectrum from DDR */
                    {
                        volatile u32 *src = spectrum + frame_idx * FFT_OUTPUT_WORDS;
                        u32 k;
                        for (k = 0; k < FFT_HALF_SIZE; k++)
                            cleaned[k] = src[k];
                    }

                    /* Layer 2: Static clutter removal (per-bin mean subtraction).
                     * Removes self-coupling + static reflections from all bins. */
                    clutter_rem_apply(cleaned, cleaned, FFT_HALF_SIZE);

                    /*
                     * Save pre-noise-floor spectrum for SNR validation.
                     * At this point, cleaned has been through clutter removal
                     * but NOT near-field blanking or noise-floor subtraction.
                     * This preserves the true local noise floor for meaningful
                     * SNR calculation in peak_validate.
                     */
                    {
                        u32 k;
                        for (k = 0; k < FFT_HALF_SIZE; k++)
                            pre_noise[k] = cleaned[k];
                    }

                    /* Accumulate RAW range profile: clutter-removed spectrum
                     * (pre-noise-floor).  Used for interference rejection —
                     * self-coupling residual, saturation effects, edge artifacts. */
                    {
                        volatile u64 *rpa = (volatile u64 *)RANGE_PROFILE_BASE;
                        u32 k;
                        for (k = 0; k < FFT_HALF_SIZE; k++)
                            rpa[k] += (u64)pre_noise[k];
                    }

                    /* Layer 3: Noise floor estimation.
                     * MUST run before Layer 1.  If near-field bins are zeroed
                     * first, the sliding median window includes zero values and
                     * underestimates the noise floor at the boundary bin, causing
                     * a false CFAR peak at the blanking edge. */
                    noise_floor_estimate(cleaned, cleaned, FFT_HALF_SIZE);

                    /* Accumulate NF range profile: noise-floor-subtracted
                     * spectrum.  This is what CFAR sees — local baseline
                     * removed, weak targets become visible. */
                    {
                        volatile u64 *rpa = (volatile u64 *)RANGE_PROFILE_NF_BASE;
                        u32 k;
                        for (k = 0; k < FFT_HALF_SIZE; k++)
                            rpa[k] += (u64)cleaned[k];
                    }

                    /* Layer 1: Near-field blanking.
                     * Applied AFTER noise floor estimation.  At this point,
                     * near-field bins are near-zero (clutter removed + noise
                     * floor subtracted), so zeroing creates no edge.  The CFAR
                     * sees a clean spectrum with correct noise floor everywhere. */
                    near_field_mask(cleaned, FFT_HALF_SIZE);

                    /* Layer 4: OS-CFAR detection + peak validation */
                    {
                        peak_info_t peaks[MAX_PEAKS_PER_FRAME];
                        int num_peaks;

                        num_peaks = cfar_detect_1d(cleaned, peaks, cfar_start);

                        /* Peak validation:
                         * - cleaned:  for -3dB width check (near-zero baseline)
                         * - pre_noise: for SNR noise estimation (true noise floor) */
                        num_peaks = peak_validate(peaks, num_peaks,
                                                  cleaned, pre_noise);

                        if (num_peaks > 0) {
                            u32 peak_idx = peaks[0].freq_index;

                            if (frame_idx < (PEAK_STORE_SIZE / sizeof(u32)))
                                peak_store[frame_idx] = peak_idx;

                            #if RADAR_VERBOSE_DIAGNOSTICS
                            if ((frame_idx & 127) == 0) {
                                output_1d_frame(frame_idx, peaks, num_peaks, cleaned);
                            }
                            #endif

                            /* Accumulate CFAR peak histogram + weighted histogram */
                            {
                                int p;
                                for (p = 0; p < num_peaks && p < MAX_PEAKS_PER_FRAME; p++) {
                                    u32 bin = peaks[p].freq_index;
                                    if (bin < FFT_HALF_SIZE) {
                                        cfar_hist[bin]++;
                                        cfar_wgt[bin] += peaks[p].magnitude;
                                    }
                                }
                            }

                            /* Diagnostic: dump raw spectrum when bin 1023 detected */
                            #if RADAR_PROCESS_DIAGNOSTICS
                            {
                                static int dump_count = 0;
                                int p;
                                for (p = 0; p < num_peaks && dump_count < 3; p++) {
                                    if (peaks[p].freq_index == 1023) {
                                        int db;
                                        printf("\n--- Spectrum Dump #%d: Frame %lu, "
                                               "bin 1023 detected ---\n",
                                               dump_count + 1,
                                               (unsigned long)frame_idx);
                                        printf("Bin    Raw Value\n");
                                        for (db = 1000; db <= 1023; db++)
                                            printf(" %4d  %10lu\n",
                                                   db, (unsigned long)cleaned[db]);
                                        printf("--- End Dump ---\n\n");
                                        dump_count++;
                                        break;
                                    }
                                }
                            }
                            #endif
                        } else {
                            /* No peaks after validation: fallback to global max */
                            u32 max_val = 0;
                            u32 max_idx = 0;
                            int j;
                            for (j = (int)cfar_start; j < (int)(FFT_HALF_SIZE - CFAR_HALF_WIN); j++) {
                                if (cleaned[j] > max_val) {
                                    max_val = cleaned[j];
                                    max_idx = (u32)j;
                                }
                            }
                            peak_store[frame_idx] = max_idx;
                        }
                    }

                    while (pass2_next_progress <= 100 &&
                           frame_idx + 1 >= (pass2_frames * pass2_next_progress + 99) / 100) {
                        xil_printf("COM17 PROC: RANGE DETECTION %lu%%\n",
                                   (unsigned long)pass2_next_progress);
                        pass2_next_progress += 25;
                    }
                }

                /* Flush peak_store to DDR */
                if (frame_idx > 0) {
                    Xil_DCacheFlushRange((UINTPTR)peak_store,
                        frame_idx * sizeof(u32));
                    Xil_DCacheFlushRange((UINTPTR)cfar_hist,
                        CFAR_HIST_SIZE);
                    Xil_DCacheFlushRange((UINTPTR)cfar_wgt,
                        CFAR_WGT_SIZE);
                }

            #if RADAR_VERBOSE_DIAGNOSTICS
            output_cfar_histogram((const u32 *)cfar_hist, analyzed_frames);
            #endif

            /* ---- Range Profile: dual-view (RAW + NF) ---- */
            #if RADAR_PROCESS_DIAGNOSTICS
            {
                const volatile u64 *rp_raw = (const volatile u64 *)RANGE_PROFILE_BASE;
                const volatile u64 *rp_nf  = (const volatile u64 *)RANGE_PROFILE_NF_BASE;
                u32 i;
                u32 rp_start = pre_filter_get_near_field_bins();
                if (rp_start < DC_SKIP_BINS) rp_start = DC_SKIP_BINS;

                /* ===== RAW Profile: interference rejection ===== */
                printf("\n===== RANGE PROFILE / CLUTTER-REMOVED =====\n");
                printf("Use: interference and edge-artifact review\n");
                {
                    u32 rp_top[10], rp_bins[10], rp_cnt = 0;
                    for (i = rp_start; i < FFT_HALF_SIZE; i++) {
                        u32 mean = analyzed_frames > 0
                                 ? (u32)(rp_raw[i] / analyzed_frames) : 0;
                        if (rp_cnt < 10) {
                            rp_top[rp_cnt] = mean; rp_bins[rp_cnt] = i; rp_cnt++;
                        } else {
                            u32 min_idx = 0, min_val = rp_top[0];
                            u32 j;
                            for (j = 1; j < 10; j++)
                                if (rp_top[j] < min_val) { min_val = rp_top[j]; min_idx = j; }
                            if (mean > min_val) { rp_top[min_idx] = mean; rp_bins[min_idx] = i; }
                        }
                    }
                    {
                        u32 j, k;
                        for (j = 0; j < rp_cnt; j++)
                            for (k = j + 1; k < rp_cnt; k++)
                                if (rp_top[k] > rp_top[j]) {
                                    u32 t;
                                    t = rp_top[j]; rp_top[j] = rp_top[k]; rp_top[k] = t;
                                    t = rp_bins[j]; rp_bins[j] = rp_bins[k]; rp_bins[k] = t;
                                }
                        for (j = 0; j < rp_cnt; j++)
                            printf("  #%2lu: Bin %lu (%.1f cm) -- %lu\n",
                                   (unsigned long)(j+1), (unsigned long)rp_bins[j],
                                   (double)fmt_dist_cm(rp_bins[j]), (unsigned long)rp_top[j]);
                    }
                }

                /* ===== NF Profile: weak target detection ===== */
                printf("\n===== RANGE PROFILE / DETECTION INPUT =====\n");
                printf("Use: noise-floor-subtracted target candidates\n");
                {
                    u32 rp_top[10], rp_bins[10], rp_cnt = 0;
                    for (i = rp_start; i < FFT_HALF_SIZE; i++) {
                        u32 mean = analyzed_frames > 0
                                 ? (u32)(rp_nf[i] / analyzed_frames) : 0;
                        if (rp_cnt < 10) {
                            rp_top[rp_cnt] = mean; rp_bins[rp_cnt] = i; rp_cnt++;
                        } else {
                            u32 min_idx = 0, min_val = rp_top[0];
                            u32 j;
                            for (j = 1; j < 10; j++)
                                if (rp_top[j] < min_val) { min_val = rp_top[j]; min_idx = j; }
                            if (mean > min_val) { rp_top[min_idx] = mean; rp_bins[min_idx] = i; }
                        }
                    }
                    {
                        u32 j, k;
                        for (j = 0; j < rp_cnt; j++)
                            for (k = j + 1; k < rp_cnt; k++)
                                if (rp_top[k] > rp_top[j]) {
                                    u32 t;
                                    t = rp_top[j]; rp_top[j] = rp_top[k]; rp_top[k] = t;
                                    t = rp_bins[j]; rp_bins[j] = rp_bins[k]; rp_bins[k] = t;
                                }
                        for (j = 0; j < rp_cnt; j++)
                            printf("  #%2lu: Bin %lu (%.1f cm) -- %lu\n",
                                   (unsigned long)(j+1), (unsigned long)rp_bins[j],
                                   (double)fmt_dist_cm(rp_bins[j]), (unsigned long)rp_top[j]);
                    }
                }

                /* Full scan: bins 110-400, NF view, 10 bins per line.
                 * Peaks >=50K (in NF) marked with brackets. */
#if RADAR_VERBOSE_DIAGNOSTICS
                printf("\nVerbose bins 110-400 (noise-floor-subtracted, K units):\n");
                {
                    u32 line_start = 110;
                    while (line_start < 400) {
                        u32 line_end = line_start + 10;
                        if (line_end > 400) line_end = 400;
                        printf("  %3lu: ", (unsigned long)line_start);
                        for (i = line_start; i < line_end; i++) {
                            u32 mean = analyzed_frames > 0
                                     ? (u32)(rp_nf[i] / analyzed_frames) : 0;
                            u32 mean_k = mean / 1000;
                            printf("%s%3lu%s ",
                                   (mean_k >= 50) ? "[" : " ",
                                   (unsigned long)mean_k,
                                   (mean_k >= 50) ? "]" : " ");
                        }
                        printf("\n");
                        line_start = line_end;
                    }
                }
#endif
                xil_printf("COM17 PROC: RANGE DETECTION DONE\n");
            }
            #endif
            }

            /* ============================================================
             * 2D angle processing from one complete 48x48 complex scan.
             *   frame_buffer -> row/column spatial FFT -> planar AoA.
             * ============================================================ */
            RADAR_DIAG_PRINTF("\n===== ANGLE CAPABILITY =====\n");
            RADAR_DIAG_PRINTF("AoA input       : COMPLEX I16/Q16 / 48x48 planar array\n");
            {
                u32 valid_elements = frame_buffer_valid_count();
                u32 valid_percent = valid_elements * 100u / SCAN_TOTAL_FRAMES;
                RADAR_DIAG_PRINTF("Spatial quality : %lu/%lu valid elements (%lu%%)\n",
                                  (unsigned long)valid_elements,
                                  (unsigned long)SCAN_TOTAL_FRAMES,
                                  (unsigned long)valid_percent);

                if (pl_format_error) {
                    RADAR_DIAG_PRINTF("AoA status      : UNAVAILABLE / PL FORMAT MISMATCH\n");
                } else if (pass2_frames != SCAN_TOTAL_FRAMES) {
                    RADAR_DIAG_PRINTF("AoA status      : UNAVAILABLE / INCOMPLETE ARRAY\n");
                } else if (valid_percent < AOA_MIN_VALID_PERCENT) {
                    RADAR_DIAG_PRINTF("AoA status      : UNAVAILABLE / VALID ELEMENTS BELOW %lu%%\n",
                                      (unsigned long)AOA_MIN_VALID_PERCENT);
                } else if (analyzed_frames > 0) {
                    const u32 *matrix = frame_buffer_get_matrix();
                    const volatile u64 *rp_nf =
                        (const volatile u64 *)RANGE_PROFILE_NF_BASE;
                    u32 range_bin = 0, range_value = 0;
                    u32 range_start = pre_filter_get_near_field_bins() + CFAR_HALF_WIN;
                    u32 bin;
                    static float h_angle[SCAN_ROWS * ANGLE_FFT_SIZE];
                    static float v_angle[SCAN_COLS * ANGLE_FFT_SIZE];
                    static float h_spectrum[ANGLE_FFT_SIZE];
                    static float v_spectrum[ANGLE_FFT_SIZE];
                    u32 h_cuts = 0, v_cuts = 0;
                    float h_bin = 0.0f, v_bin = 0.0f;
                    float h_peak = 0.0f, v_peak = 0.0f;
                    float h_base = 0.0f, v_base = 0.0f;
                    float h_ratio = 0.0f, v_ratio = 0.0f;
                    int h_ok, v_ok, geometry_ok;

                    if (range_start < DC_SKIP_BINS)
                        range_start = DC_SKIP_BINS;
                    for (bin = range_start; bin < FFT_HALF_SIZE; bin++) {
                        u32 value = (u32)(rp_nf[bin] / analyzed_frames);
                        if (value > range_value) {
                            range_value = value;
                            range_bin = bin;
                        }
                    }

                    angle_fft_horizontal_complex(matrix, range_bin,
                                                 h_angle, h_spectrum, &h_cuts);
                    angle_fft_vertical_complex(matrix, range_bin,
                                               v_angle, v_spectrum, &v_cuts);
                    h_ok = angle_spectrum_peak(h_spectrum, &h_bin, &h_peak,
                                               &h_base, &h_ratio) == 0;
                    v_ok = angle_spectrum_peak(v_spectrum, &v_bin, &v_peak,
                                               &v_base, &v_ratio) == 0;
                    geometry_ok = fmt_planar_angles(h_bin, v_bin,
                                                    &g_az_deg, &g_el_deg) == 0;

                    RADAR_DIAG_PRINTF("Range gate      : bin %lu / %.2f cm\n",
                                      (unsigned long)range_bin,
                                      (double)fmt_dist_cm(range_bin));
                    RADAR_DIAG_PRINTF("Azimuth cut     : bin %.3f / peak %.2fx / %lu valid rows\n",
                                      (double)h_bin, (double)h_ratio,
                                      (unsigned long)h_cuts);
                    RADAR_DIAG_PRINTF("Elevation cut   : bin %.3f / peak %.2fx / %lu valid columns\n",
                                      (double)v_bin, (double)v_ratio,
                                      (unsigned long)v_cuts);

                    if (h_ok && v_ok && geometry_ok &&
                        h_cuts >= AOA_MIN_CUT_ELEMENTS &&
                        v_cuts >= AOA_MIN_CUT_ELEMENTS &&
                        h_ratio * 256.0f >= (float)AOA_MIN_PEAK_RATIO_Q8 &&
                        v_ratio * 256.0f >= (float)AOA_MIN_PEAK_RATIO_Q8) {
                        g_angle_estimate_available = 1;
                        g_angle_calibrated = RADAR_AOA_PHASE_CALIBRATED;
                        RADAR_DIAG_PRINTF("Angle estimate  : azimuth %.2f deg / elevation %.2f deg\n",
                                          (double)g_az_deg, (double)g_el_deg);
                        RADAR_DIAG_PRINTF("AoA status      : %s\n",
                                          g_angle_calibrated
                                          ? "VALID / PHASE CALIBRATED"
                                          : "ESTIMATE / PHASE UNCALIBRATED");
#ifdef ETH_ENABLE
                        if (radar_udp_send_angle_map(
                                udp_scan_id, (u16)range_bin,
                                RADAR_ANGLE_MAP_HORIZONTAL, h_angle,
                                SCAN_ROWS, ANGLE_FFT_SIZE) != 0 ||
                            radar_udp_send_angle_map(
                                udp_scan_id, (u16)range_bin,
                                RADAR_ANGLE_MAP_VERTICAL, v_angle,
                                SCAN_COLS, ANGLE_FFT_SIZE) != 0)
                            xil_printf("COM17 NET TX: ANGLE MAP FAILED\n");
                        else
                            xil_printf("COM17 NET TX: ANGLE MAP READY\n");
#endif
                    } else {
                        RADAR_DIAG_PRINTF("AoA status      : UNAVAILABLE / ANGLE QUALITY GATE FAILED\n");
                    }
                } else {
                    RADAR_DIAG_PRINTF("AoA status      : UNAVAILABLE / NO VALID RANGE DATA\n");
                }
            }

            /* ============================================================
             * Target distance measurement
             *
             * Industrial-grade target identification:
             *
             *   PRIMARY:  NF profile peak.
             *     The NF profile (noise-floor-subtracted, frame-averaged)
             *     uses a 64-bin median estimator — wide enough to see
             *     the true noise floor even under wide peaks.  CFAR's
             *     narrow training window (10 bins) can land on the peak
             *     itself, inflating the local threshold and missing the
             *     peak top.  The NF profile suffers no such problem.
             *
             *   SECONDARY:  CFAR histogram cross-validation.
             *     If CFAR has detections within +/-5 bins of the NF peak,
             *     the target is CFAR-CONFIRMED.  Otherwise NF-ONLY.
             *     NF-ONLY means the peak is wide enough that CFAR cannot
             *     resolve it, but the NF profile still shows it clearly.
             *
             *   PROMINENCE CHECK:
             *     A genuine target must stand out from the local baseline.
             *     Baseline is the median of bins 30-60 away from the peak.
             *     Peak/baseline >= 3.0x is required.  This rejects
             *     flat-spectrum data where the "peak" is just noise.
             *
             *   No prior knowledge of target distance is needed.
             * ============================================================ */
            RADAR_DIAG_PRINTF("\n===== DETECTION SUMMARY =====\n");
            {
                const volatile u64 *rp_nf = (const volatile u64 *)RANGE_PROFILE_NF_BASE;
                const u32 *hist = (const u32 *)cfar_hist;
                #if RADAR_PROCESS_DIAGNOSTICS
                const u32 *wgt  = (const u32 *)cfar_wgt;
                #endif
                u32 i, peak_bin = 0, peak_val = 0, total_peaks = 0;
                float summary_range_cm = 0.0f;
                float summary_snr_db = 0.0f;
                int summary_target_valid = 0;
                int summary_cfar_confirmed = 0;
                int scan_quality_ok;
                u8 summary_flags = 0;
                u32 cfar_min = pre_filter_get_near_field_bins() + CFAR_HALF_WIN;
                if (cfar_min < DC_SKIP_BINS)
                    cfar_min = DC_SKIP_BINS;

                scan_quality_ok = (pass2_frames == SCAN_TOTAL_FRAMES &&
                                   analyzed_frames == pass2_frames &&
                                   saturated_frames == 0);
                if (!scan_quality_ok)
                    summary_flags |= RADAR_SUMMARY_FLAG_DEGRADED;
                if (g_angle_calibrated)
                    summary_flags |= RADAR_SUMMARY_FLAG_ANGLE_VALID;
                else if (g_angle_estimate_available)
                    summary_flags |= RADAR_SUMMARY_FLAG_ANGLE_ESTIMATE;
                RADAR_DIAG_PRINTF("Quality         : %s\n",
                                   scan_quality_ok ? "VALID" : "DEGRADED");
                RADAR_DIAG_PRINTF("Frame usage     : %lu valid / %lu total / %lu saturated\n",
                                  (unsigned long)analyzed_frames,
                                  (unsigned long)pass2_frames,
                                  (unsigned long)saturated_frames);

                for (i = cfar_min; i < FFT_HALF_SIZE; i++)
                    total_peaks += hist[i];

                /* ---- Primary: NF profile peak ---- */
                for (i = cfar_min; i < FFT_HALF_SIZE; i++) {
                    u32 mean = analyzed_frames > 0
                             ? (u32)(rp_nf[i] / analyzed_frames) : 0;
                    if (mean > peak_val) { peak_val = mean; peak_bin = i; }
                }

                /* ---- Prominence: baseline median 30-60 bins away ---- */
                u32 bl_cnt = 0;
                u32 bl_buf[61];
                u32 bl_start, bl_end;
                bl_start = (peak_bin > 60) ? (peak_bin - 60) : cfar_min;
                bl_end   = (peak_bin > 30) ? (peak_bin - 30) : cfar_min;
                for (i = bl_start; i < bl_end && i < FFT_HALF_SIZE; i++)
                    bl_buf[bl_cnt++] = analyzed_frames > 0
                                     ? (u32)(rp_nf[i] / analyzed_frames) : 0;
                bl_start = (peak_bin + 30 < FFT_HALF_SIZE) ? (peak_bin + 30) : FFT_HALF_SIZE;
                bl_end   = (peak_bin + 60 < FFT_HALF_SIZE) ? (peak_bin + 60) : FFT_HALF_SIZE;
                for (i = bl_start; i < bl_end && i < FFT_HALF_SIZE; i++)
                    bl_buf[bl_cnt++] = analyzed_frames > 0
                                     ? (u32)(rp_nf[i] / analyzed_frames) : 0;
                u32 baseline = 0;
                if (bl_cnt > 0) {
                    u32 s, t;
                    for (s = 0; s < bl_cnt - 1; s++)
                        for (t = s + 1; t < bl_cnt; t++)
                            if (bl_buf[t] < bl_buf[s]) {
                                u32 tmp = bl_buf[s];
                                bl_buf[s] = bl_buf[t];
                                bl_buf[t] = tmp;
                            }
                    baseline = bl_buf[bl_cnt / 2];
                }

                /* ---- CFAR confirmation near NF peak ---- */
                u32 cfar_near = 0;
                {
                    u32 p;
                    u32 p_start = (peak_bin > 5) ? (peak_bin - 5) : 0;
                    u32 p_end   = (peak_bin + 5 < FFT_HALF_SIZE)
                                ? (peak_bin + 5) : (FFT_HALF_SIZE - 1);
                    for (p = p_start; p <= p_end; p++)
                        cfar_near += hist[p];
                }

                {
                    float prominence_ratio = baseline > 0
                                           ? (float)peak_val / (float)baseline
                                           : 0.0f;

                RADAR_DIAG_PRINTF("Detection count : %lu (%.2f per valid frame)\n",
                                  (unsigned long)total_peaks,
                                  analyzed_frames > 0
                                  ? (double)total_peaks / analyzed_frames : 0.0);
                RADAR_DIAG_PRINTF("Primary peak    : bin %lu / %.2f cm / amplitude %lu\n",
                                  (unsigned long)peak_bin,
                                  (double)fmt_dist_cm(peak_bin),
                                  (unsigned long)peak_val);
                RADAR_DIAG_PRINTF("Local baseline  : %lu / prominence %.2fx\n",
                                  (unsigned long)baseline,
                                  (double)prominence_ratio);
                RADAR_DIAG_PRINTF("CFAR support    : %lu detections within +/-5 bins\n",
                                  (unsigned long)cfar_near);
                if (peak_val == 0) {
                    RADAR_DIAG_PRINTF("Result          : NO TARGET / flat detection profile\n");
                } else if (baseline == 0 ||
                           (u64)peak_val * 256u <
                           (u64)PROMINENCE_MIN_RATIO_Q8 * baseline) {
                    RADAR_DIAG_PRINTF("Result          : NO TARGET / prominence %.2fx below 3.0x\n",
                                      (double)prominence_ratio);
                } else {
                    RADAR_DIAG_PRINTF("Result          : RANGE CANDIDATE %.2f cm / bin %lu / %s\n",
                                      (double)fmt_dist_cm(peak_bin),
                                      (unsigned long)peak_bin,
                                      (cfar_near >= 3 && scan_quality_ok)
                                      ? "[CFAR-CONFIRMED]" : "[NF-ONLY/DEGRADED]");

                    {
                        u32 p = peak_bin;
                        float delta = 0.0f;
                        float interp_cm = ((float)p + delta) * PER_BIN_CM_F;

                        if (p > 0 && p < FFT_HALF_SIZE - 1) {
                            float A_left  = (float)(rp_nf[p - 1] / analyzed_frames);
                            float A_peak  = (float)(rp_nf[p]     / analyzed_frames);
                            float A_right = (float)(rp_nf[p + 1] / analyzed_frames);
                            float denom = 2.0f * (A_left - 2.0f * A_peak + A_right);
                            delta = (denom > 1.0f || denom < -1.0f)
                                  ? (A_left - A_right) / denom : 0.0f;
                            interp_cm = ((float)p + delta) * PER_BIN_CM_F;
                        }

                        RADAR_DIAG_PRINTF("Interpolation   : bin %.4f / %.4f cm / delta %.4f\n",
                                          (double)((float)p + delta), (double)interp_cm, (double)delta);
                        #if RADAR_VERBOSE_DIAGNOSTICS
                        if (g_angle_estimate_available)
                            output_cartesian(interp_cm, g_az_deg, g_el_deg);
                        #endif
                        {
                            float snr_db = fmt_power_ratio_db(prominence_ratio);
                            summary_range_cm = interp_cm;
                            summary_snr_db = snr_db;
                            summary_target_valid = 1;
                            summary_cfar_confirmed =
                                (cfar_near >= 3 && scan_quality_ok) ? 1 : 0;
                        }
                    }
                }
                }

                /* ---- CFAR histogram for reference ---- */
                #if RADAR_PROCESS_DIAGNOSTICS
                printf("\n===== CFAR REFERENCE PEAKS =====\n");
                {
                    u32 min_hits = 4;
                    u32 top_hits[12] = {0};
                    u32 top_bins[12] = {0};
                    u32 top_count = 0;
                    u32 any = 0;
                    u32 j, k;
                    for (i = cfar_min; i < FFT_HALF_SIZE; i++) {
                        if (hist[i] >= min_hits) {
                            any = 1;
                            if (top_count < 12) {
                                top_hits[top_count] = hist[i];
                                top_bins[top_count++] = i;
                            } else {
                                u32 min_idx = 0;
                                for (j = 1; j < 12; j++)
                                    if (top_hits[j] < top_hits[min_idx]) min_idx = j;
                                if (hist[i] > top_hits[min_idx]) {
                                    top_hits[min_idx] = hist[i];
                                    top_bins[min_idx] = i;
                                }
                            }
                        }
                    }
                    for (j = 0; j < top_count; j++) {
                        for (k = j + 1; k < top_count; k++) {
                            if (top_hits[k] > top_hits[j]) {
                                u32 swap = top_hits[j];
                                top_hits[j] = top_hits[k];
                                top_hits[k] = swap;
                                swap = top_bins[j];
                                top_bins[j] = top_bins[k];
                                top_bins[k] = swap;
                            }
                        }
                        printf("  #%2lu bin=%lu range=%.1f cm hits=%lu mean=%lu%s\n",
                               (unsigned long)(j + 1),
                               (unsigned long)top_bins[j],
                               (double)fmt_dist_cm(top_bins[j]),
                               (unsigned long)top_hits[j],
                               (unsigned long)(wgt[top_bins[j]] / top_hits[j]),
                               (top_bins[j] == peak_bin) ? " [PRIMARY]" : "");
                    }
                    if (!any)
                        printf("  (none)\n");
                }
                #endif

#ifdef ETH_ENABLE
                xil_printf("COM17 PROC: REPORT UPLOAD\n");
                radar_udp_log_flush();
                radar_udp_send_summary_reliable(
                    udp_scan_id, total, udp_packet_count, frame_idx, total_peaks,
                    (u16)saturated_frames, (u16)near_field_bins,
                    summary_range_cm, g_az_deg, g_el_deg, summary_snr_db,
                    summary_target_valid, summary_cfar_confirmed, summary_flags);
                eth_capture_send_process_done();
#endif
            }
        }
    }

#if RADAR_NET_DIAGNOSTICS && !RADAR_VERBOSE_DIAGNOSTICS
#undef printf
#endif

#if RADAR_INPUT_MODE == RADAR_INPUT_UDP
    printf("COM17 SYS: PROCESS OK\n");
    com18_send_status("COM18 TX: PROCESS OK\r\n");
    RADAR_DIAG_PRINTF("Scan processing complete; returning to UDP wait state.\n");
    goto udp_wait_scan;
#else
    /* ============================================================
     * ILA Debug Loop: continuously send frames through DMA
     * so ILA can capture complete FFT pipeline waveforms.
     * Press board reset or Ctrl+C to stop.
     * ============================================================ */
    printf("\n===== ILA Debug Loop: streaming frames for capture =====\n");
    printf("Open ILA, set trigger, then watch waveform.\n");
    {
        volatile u8 *src = radar_data;
        u32 loop_cnt = 0;
        int k;
        while (1) {
            for (k = 0; k < BYTES_PER_ELEMENT; k++)
                ((u8 *)dma_tx_buf)[k] = src[k];

            if (dma_recv_start((u8 *)dma_rx_buf, FFT_OUTPUT_BYTES) == 0) {
                Xil_DCacheFlushRange((UINTPTR)dma_tx_buf, BYTES_PER_ELEMENT);
                XAxiDma_SimpleTransfer(&axi_dma, (UINTPTR)dma_tx_buf,
                                       BYTES_PER_ELEMENT,
                                       XAXIDMA_DMA_TO_DEVICE);
                {
                    int timeout = 1000000;
                    while (timeout) {
                        if (XAxiDma_Busy(&axi_dma, XAXIDMA_DMA_TO_DEVICE) == 0)
                            break;
                        timeout--;
                    }
                }
                dma_recv_wait();
            }

            loop_cnt++;
            if ((loop_cnt & 1023) == 0)
                printf("ILA loop: %lu frames streamed\n", (unsigned long)loop_cnt);
        }
    }
#endif

    return 0;
}
