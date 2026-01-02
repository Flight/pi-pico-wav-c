#include "wav.h"

#include <string.h>

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool parse_wav(const uint8_t *buffer, size_t length, wav_info_t *out) {
    if (!buffer || !out) {
        return false;
    }
    if (length < 44 || memcmp(buffer, "RIFF", 4) || memcmp(buffer + 8, "WAVE", 4)) {
        return false;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t *data_ptr = NULL;
    uint32_t data_size = 0;

    size_t offset = 12;
    while (offset + 8 <= length) {
        const uint8_t *chunk = buffer + offset;
        uint32_t chunk_size = read_u32_le(chunk + 4);
        const uint8_t *chunk_data = chunk + 8;
        if (!memcmp(chunk, "fmt ", 4) && chunk_size >= 16 && offset + 8 + chunk_size <= length) {
            audio_format = read_u16_le(chunk_data + 0);
            channels = read_u16_le(chunk_data + 2);
            sample_rate = read_u32_le(chunk_data + 4);
            bits_per_sample = read_u16_le(chunk_data + 14);
        } else if (!memcmp(chunk, "data", 4) && offset + 8 + chunk_size <= length) {
            data_ptr = chunk_data;
            data_size = chunk_size;
        }

        offset += 8 + chunk_size + (chunk_size & 1u);
    }

    if (audio_format != 1 || !data_ptr || !data_size || sample_rate == 0) {
        return false;
    }
    if (channels != 1 && channels != 2) {
        return false;
    }
    if (bits_per_sample != 8 && bits_per_sample != 16) {
        return false;
    }

    uint32_t stride = (bits_per_sample / 8) * channels;
    out->data = data_ptr;
    out->data_size = data_size - (data_size % stride);
    out->sample_rate = sample_rate;
    out->bits_per_sample = bits_per_sample;
    out->channels = channels;
    if (out->data_size == 0) {
        return false;
    }
    return true;
}
