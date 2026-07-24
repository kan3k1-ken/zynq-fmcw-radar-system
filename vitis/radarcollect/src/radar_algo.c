#include "radar_algo.h"

int radar_parse_iq(const u8 *raw, u32 byte_count, iq_sample_t *out, u32 max_samples)
{
    u32 pair_count = byte_count / 2;
    u32 sample_count = pair_count / 2;

    if (sample_count > max_samples)
        sample_count = max_samples;

    for (u32 k = 0; k < sample_count; k++) {
        out[k].i = (s16)(raw[k * 4]     | ((u16)raw[k * 4 + 1] << 8));
        out[k].q = (s16)(raw[k * 4 + 2] | ((u16)raw[k * 4 + 3] << 8));
    }

    return sample_count;
}