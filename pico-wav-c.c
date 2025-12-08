#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "wav_data.h"

// GPIO that feeds the RC filter / amplifier for PWM audio.
#define AUDIO_PIN 0

typedef struct {
    const uint8_t *data;
    size_t data_size;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
} wav_info_t;

typedef struct {
    wav_info_t info;
    const uint8_t *cursor;
    size_t remaining;
    uint16_t frame_stride;
    uint slice_num;
    uint gpio;
} playback_state_t;

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

// Minimal WAV parser for PCM mono/stereo, 8/16-bit.
static bool parse_wav(const uint8_t *buffer, size_t length, wav_info_t *out) {
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

        // Chunks are word padded.
        offset += 8 + chunk_size + (chunk_size & 1u);
    }

    if (audio_format != 1 || !data_ptr || !data_size || sample_rate == 0) {
        return false;
    }
    if (channels == 0 || (channels != 1 && channels != 2)) {
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

static bool init_pwm_audio(uint gpio, uint *slice_out) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 255); // 8-bit duty cycle
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(gpio, 128); // idle midpoint

    *slice_out = slice;
    return true;
}

static bool audio_timer_callback(struct repeating_timer *t) {
    playback_state_t *state = (playback_state_t *)t->user_data;
    if (!state || !state->remaining) {
        return true;
    }

    if (state->remaining < state->frame_stride) {
        state->cursor = state->info.data;
        state->remaining = state->info.data_size;
    }

    uint16_t level = 128; // midpoint for silence
    if (state->info.bits_per_sample == 8) {
        level = state->cursor[0];
    } else {
        const int16_t *s = (const int16_t *)state->cursor;
        level = (uint16_t)(((int32_t)*s + 32768) >> 8); // scale to 0-255
    }

    pwm_set_gpio_level(state->gpio, level);
    state->cursor += state->frame_stride;
    state->remaining -= state->frame_stride;
    return true;
}

int main() {
    stdio_init_all();

    wav_info_t wav = {0};
    if (!parse_wav(wav_data, wav_data_len, &wav)) {
        // Parsing failed; stop early so we do not drive the pin with nonsense.
        while (true) {
            tight_loop_contents();
        }
    }

    playback_state_t state = {
        .info = wav,
        .cursor = wav.data,
        .remaining = wav.data_size,
        .frame_stride = (uint16_t)((wav.bits_per_sample / 8) * wav.channels),
        .gpio = AUDIO_PIN,
    };

    if (state.frame_stride == 0) {
        while (true) {
            tight_loop_contents();
        }
    }

    init_pwm_audio(AUDIO_PIN, &state.slice_num);

    // Use a repeating timer to push samples at the WAV sample rate.
    struct repeating_timer timer;
    int64_t interval_us = -((int64_t)1000000LL / (int64_t)wav.sample_rate);
    add_repeating_timer_us(interval_us, audio_timer_callback, &state, &timer);

    while (true) {
        tight_loop_contents();
    }
}
