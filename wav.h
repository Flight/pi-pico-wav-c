#ifndef WAV_H
#define WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t data_size;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
} wav_info_t;

// Minimal WAV parser for PCM mono/stereo, 8/16-bit.
bool parse_wav(const uint8_t *buffer, size_t length, wav_info_t *out);

#endif
