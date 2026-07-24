/*
 * eth_udp.h
 * Minimal UDP/IP stack on Xilinx Zynq PS GEM.
 *
 * Configuration:
 *   Zynq IP: 192.168.1.10
 *   PC   IP: 192.168.1.100
 *   UDP port: 9999
 *
 * The stack implements:
 *   - EMAC (GEM) initialization via xemacps
 *   - ARP resolution (polling-based)
 *   - IP header construction with checksum
 *   - UDP header construction
 *   - Ethernet frame transmission via DMA
 */

#ifndef ETH_UDP_H_
#define ETH_UDP_H_

#include <stdint.h>
#include "radar_protocol.h"

/*
 * Compile-time toggle:
 *   Define ETH_ENABLE to activate Ethernet stack.
 *   Comment out to disable (skips eth_init, no UDP send).
 *   When disabled, the system works standalone via UART only.
 */
#define ETH_ENABLE

/* Network configuration */
#define ETH_ZYNQ_IP0   192
#define ETH_ZYNQ_IP1   168
#define ETH_ZYNQ_IP2   1
#define ETH_ZYNQ_IP3   10

#define ETH_PC_IP0      192
#define ETH_PC_IP1      168
#define ETH_PC_IP2      1
#define ETH_PC_IP3      100

#define ETH_UDP_PORT    9999
#define ETH_ZYNQ_PORT   8888
#define ETH_RAW_SCAN_PORT 10000
#define ETH_CONTROL_PORT 10001
#define ETH_RAW_CHUNK_DATA_MAX 1024

/* MAC address (locally administered, Xilinx OUI 00:0A:35) */
#define ETH_MAC0  0x00
#define ETH_MAC1  0x0A
#define ETH_MAC2  0x35
#define ETH_MAC3  0x00
#define ETH_MAC4  0x01
#define ETH_MAC5  0x42

/* Ethernet frame limits */
#define ETH_MTU         1500
#define ETH_HEADER_SIZE 14
#define ETH_FCS_SIZE    4

typedef struct {
    uint32_t scan_id;
    uint32_t total_len;
    uint32_t received_len;
    uint32_t packet_count;
    uint32_t duplicate_count;
    uint32_t crc_error_count;
    uint32_t protocol_error_count;
} eth_capture_info_t;

#define ETH_CAPTURE_IDLE        0
#define ETH_CAPTURE_IN_PROGRESS 1
#define ETH_CAPTURE_COMPLETE    2
#define ETH_CAPTURE_ERROR       (-1)

/* Initialize Ethernet subsystem.
 * Returns 0 on success, negative on error. */
int eth_init(void);

/* Send a UDP packet.
 * data: payload pointer
 * len:  payload length (must be <= ETH_MTU - 20 - 8)
 * dst_ip0..3: destination IP address octets
 * dst_port: destination UDP port
 * src_port: source UDP port
 * Returns 0 on success. */
int udp_send(const uint8_t *data, uint16_t len,
             uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3,
             uint16_t dst_port, uint16_t src_port);

/* Check if link is up. Returns 1 if link detected. */
int eth_link_up(void);

/* Reset and poll the non-blocking PC-to-Zynq raw scan reassembler. */
void eth_capture_reset(void);
int eth_capture_poll(uint8_t *dst, uint32_t capacity,
                     eth_capture_info_t *info);
void eth_capture_dump_rx_state(void);
int eth_capture_send_process_done(void);

/* Send a radar detection packet.
 * Convenience wrapper: builds detection payload and sends to PC.
 * range_cm, az_deg, el_deg are spherical coordinates.
 * SNR is computed as 10*log10(peak_val/baseline) by caller. */
int radar_udp_send(uint32_t frame_id, float range_cm,
                   float az_deg, float el_deg,
                   float snr_db, int confirmed);

int radar_udp_send_summary(uint32_t scan_id, uint32_t input_bytes,
                           uint32_t packet_count, uint32_t frame_count,
                           uint32_t detection_count,
                           uint16_t saturated_frames,
                           uint16_t near_field_bins,
                           float range_cm, float az_deg, float el_deg,
                           float snr_db, int target_valid, int cfar_confirmed,
                           uint8_t summary_flags);

/* Publish the final scan summary three times, one TX completion apart. */
int radar_udp_send_summary_reliable(uint32_t scan_id, uint32_t input_bytes,
                                    uint32_t packet_count, uint32_t frame_count,
                                    uint32_t detection_count,
                                    uint16_t saturated_frames,
                                    uint16_t near_field_bins,
                                    float range_cm, float az_deg, float el_deg,
                                    float snr_db, int target_valid, int cfar_confirmed,
                                    uint8_t summary_flags);

/* Publish one normalized 8-bit angle-energy map in reliable UDP chunks. */
int radar_udp_send_angle_map(uint32_t scan_id, uint16_t range_bin,
                             uint8_t map_kind, const float *values,
                             uint16_t rows, uint16_t cols);

/* Build and publish one ordered diagnostic report to the PC result receiver. */
void radar_udp_log_reset(void);
int radar_udp_printf(const char *format, ...);
int radar_udp_log_flush(void);

#endif /* ETH_UDP_H_ */
