#ifndef AUDIO_PWM_DMA_H
#define AUDIO_PWM_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pico/types.h"
#include "wav.h"

typedef struct {
    wav_info_t wav;
    const uint8_t *cursor;
    size_t remaining;
    uint16_t frame_stride;
    uint slice_num;
    uint dma_chan_a;
    uint dma_chan_b;
    uint gpio;
    uint pwm_channel;
    uint pace_slice;
    bool done;
} audio_player_t;

// Initializes the PWM, pacing PWM, and DMA chains for playback.
bool audio_pwm_dma_init(audio_player_t *player, const wav_info_t *wav, uint gpio);

// Starts DMA playback after initialization.
void audio_pwm_dma_start(audio_player_t *player);

#endif
