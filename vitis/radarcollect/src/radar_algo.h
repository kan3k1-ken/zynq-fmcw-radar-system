#ifndef RADAR_ALGO_H_
#define RADAR_ALGO_H_

#ifdef Linux
#include "Types.h"
#else
#include "xil_types.h"
#endif

typedef struct {
    s16 i;
    s16 q;
} iq_sample_t;

int radar_parse_iq(const u8 *raw, u32 byte_count, iq_sample_t *out, u32 max_samples);

#endif