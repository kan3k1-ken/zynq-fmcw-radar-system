/*
 * radar_protocol.h
 * Binary protocol for radar data over UDP.
 *
 * Packet layout:
 *   [16-byte header] + [36-byte detection payload]
 *
 * Magic: 0x52414441 ("RADA")
 * Version: 1
 * Type 0x01 = detection result
 * Type 0x02 = range profile (future)
 * Type 0x03 = status/handshake response
 * Type 0x04 = scan summary and final target result
 * Type 0x11 = control command
 */

#ifndef RADAR_PROTOCOL_H_
#define RADAR_PROTOCOL_H_

#include <stdint.h>

#define RADAR_PROTO_MAGIC   0x52414441u  /* "RADA" */
#define RADAR_PROTO_VERSION 1
#define RADAR_PROTO_TYPE_DETECTION 0x01
#define RADAR_PROTO_TYPE_SPECTRUM  0x02
#define RADAR_PROTO_TYPE_STATUS    0x03
#define RADAR_PROTO_TYPE_SCAN_SUMMARY 0x04
#define RADAR_PROTO_TYPE_TEXT      0x05
#define RADAR_PROTO_TYPE_RAW_CHUNK 0x10
#define RADAR_PROTO_TYPE_CONTROL   0x11

#define RADAR_CONTROL_START        0x00000001u

#define RADAR_STATUS_READY         0x00000001u
#define RADAR_STATUS_CAPTURE_DONE  0x00000002u
#define RADAR_STATUS_ERROR         0x00000003u
#define RADAR_STATUS_PROCESS_DONE  0x00000004u

#define RADAR_RAW_CHUNK_FLAG_FIRST 0x0001u
#define RADAR_RAW_CHUNK_FLAG_LAST  0x0002u

#define RADAR_ANGLE_MAP_HORIZONTAL 1u
#define RADAR_ANGLE_MAP_VERTICAL   2u
#define RADAR_ANGLE_MAP_ENCODING_U8_NORMALIZED 1u

/* radar_scan_summary_payload_t.reserved[0] quality/capability bits */
#define RADAR_SUMMARY_FLAG_ANGLE_VALID 0x01u
#define RADAR_SUMMARY_FLAG_DEGRADED    0x02u
#define RADAR_SUMMARY_FLAG_ANGLE_ESTIMATE 0x04u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;       /* 0x52414441 "RADA" */
    uint8_t  version;     /* protocol version */
    uint8_t  type;        /* packet type */
    uint16_t seq;         /* sequence number, wraps */
    uint32_t timestamp;   /* milliseconds since boot */
    uint16_t payload_len; /* payload bytes */
    uint16_t reserved;
} radar_proto_header_t;

typedef struct {
    uint32_t frame_id;    /* frame index */
    float    range_cm;    /* interpolated distance */
    float    az_deg;      /* azimuth angle */
    float    el_deg;      /* elevation angle */
    float    x_cm;        /* cross-range */
    float    y_cm;        /* down-range */
    float    z_cm;        /* height */
    float    snr_db;      /* peak/baseline ratio in dB */
    uint8_t  confirmed;   /* 1 = CFAR confirmed */
    uint8_t  reserved[3];
} radar_detection_payload_t;

typedef struct {
    uint32_t command;
    uint32_t session_id;
} radar_control_payload_t;

typedef struct {
    uint32_t status;
    uint32_t session_id;
    uint32_t detail;
} radar_status_payload_t;

typedef struct {
    uint32_t scan_id;
    uint32_t input_bytes;
    uint32_t packet_count;
    uint32_t frame_count;
    uint32_t detection_count;
    uint16_t saturated_frames;
    uint16_t near_field_bins;
    float    range_cm;
    float    az_deg;
    float    el_deg;
    float    x_cm;
    float    y_cm;
    float    z_cm;
    float    snr_db;
    uint8_t  target_valid;
    uint8_t  cfar_confirmed;
    uint8_t  reserved[2];
} radar_scan_summary_payload_t;

typedef struct {
    uint32_t report_id;
    uint16_t chunk_index;
    uint16_t chunk_count;
} radar_text_chunk_header_t;

typedef struct {
    uint32_t scan_id;
    uint16_t range_bin;
    uint8_t  map_kind;
    uint8_t  encoding;
    uint16_t rows;
    uint16_t cols;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t data_len;
    uint16_t reserved;
    float    value_min;
    float    value_max;
    uint32_t map_crc32;
} radar_angle_map_chunk_header_t;

/*
 * PC-to-Zynq replay payload. Each UDP datagram contains one protocol header,
 * this chunk header, and chunk_len bytes of the original UART byte stream.
 * The original stream includes the "AllDataBack" header so the processing
 * pipeline receives exactly the same bytes in UART and UDP modes.
 */
typedef struct {
    uint32_t scan_id;
    uint32_t total_len;
    uint32_t offset;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t chunk_len;
    uint16_t flags;
    uint32_t chunk_crc32;
    uint32_t scan_crc32;
} radar_raw_chunk_header_t;
#pragma pack(pop)

#endif /* RADAR_PROTOCOL_H_ */
