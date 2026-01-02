#include "audio_pwm_dma.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

// DMA buffers hold pre-scaled 8-bit samples (0-255) as 16-bit PWM levels.
typedef struct {
    uint16_t *buffer;
    size_t sample_count;
} dma_buffer_t;

// Two buffers allow refill while the other DMA channel streams.
#define DMA_SAMPLES 512
static uint16_t dma_samples_a[DMA_SAMPLES];
static uint16_t dma_samples_b[DMA_SAMPLES];
static dma_buffer_t g_dma_buf_a;
static dma_buffer_t g_dma_buf_b;
// IRQ handler needs a stable pointer to the active player.
static audio_player_t *g_player;

// Pick a different PWM slice to use as the DMA pacing clock.
static uint pick_pace_slice(uint audio_slice) {
    uint pace_slice = (audio_slice + 1u) & 0x7u;
    if (pace_slice == audio_slice) {
        pace_slice = (audio_slice + 2u) & 0x7u;
    }
    return pace_slice;
}

// Configure PWM on the audio GPIO at a high carrier frequency.
static void init_audio_pwm(uint gpio, uint *slice_out, uint *channel_out) {
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio);
    uint channel = pwm_gpio_to_channel(gpio);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 255); // 8-bit duty cycle
    pwm_config_set_clkdiv(&cfg, 1.0f); // high carrier for PWM audio
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(gpio, 128); // idle midpoint

    *slice_out = slice;
    *channel_out = channel;
}

// Configure a PWM slice to generate the sample-rate DMA pacing DREQ.
static void init_pace_pwm(uint32_t sample_rate, uint slice) {
    pwm_config cfg = pwm_get_default_config();
    uint32_t clk_hz = clock_get_hz(clk_sys);
    uint32_t wrap = clk_hz / sample_rate;
    if (wrap == 0) {
        wrap = 1;
    }
    if (wrap > 0x10000u) {
        wrap = 0x10000u;
    }
    pwm_config_set_wrap(&cfg, (uint16_t)(wrap - 1u));
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_init(slice, &cfg, true);
}

// Convert WAV samples into 8-bit PWM levels for DMA streaming.
static void fill_dma_buffer(audio_player_t *player, uint16_t *buffer, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (player->remaining < player->frame_stride) {
            player->done = true;
            buffer[i] = 128;
            continue;
        }

        uint16_t level = 128; // midpoint for silence
        if (player->wav.bits_per_sample == 8) {
            level = player->cursor[0];
        } else {
            const int16_t *s = (const int16_t *)player->cursor;
            level = (uint16_t)(((int32_t)*s + 32768) >> 8);
        }

        buffer[i] = level;
        player->cursor += player->frame_stride;
        player->remaining -= player->frame_stride;
    }
}

// Refill the buffer that just finished and re-arm its DMA channel.
static void __isr dma_irq_handler(void) {
    if (!g_player) {
        return;
    }

    uint32_t status = dma_hw->ints0;
    if (status & (1u << g_player->dma_chan_a)) {
        dma_hw->ints0 = 1u << g_player->dma_chan_a;
        fill_dma_buffer(g_player, g_dma_buf_a.buffer, g_dma_buf_a.sample_count);
        dma_channel_set_read_addr(g_player->dma_chan_a, g_dma_buf_a.buffer, false);
        dma_channel_set_trans_count(g_player->dma_chan_a, g_dma_buf_a.sample_count, false);
    }
    if (status & (1u << g_player->dma_chan_b)) {
        dma_hw->ints0 = 1u << g_player->dma_chan_b;
        fill_dma_buffer(g_player, g_dma_buf_b.buffer, g_dma_buf_b.sample_count);
        dma_channel_set_read_addr(g_player->dma_chan_b, g_dma_buf_b.buffer, false);
        dma_channel_set_trans_count(g_player->dma_chan_b, g_dma_buf_b.sample_count, false);
    }
}

// Initialize PWM output, pacing PWM, and chained DMA channels.
bool audio_pwm_dma_init(audio_player_t *player, const wav_info_t *wav, uint gpio) {
    if (!player || !wav) {
        return false;
    }

    *player = (audio_player_t){
        .wav = *wav,
        .cursor = wav->data,
        .remaining = wav->data_size,
        .frame_stride = (uint16_t)((wav->bits_per_sample / 8) * wav->channels),
        .gpio = gpio,
        .done = false,
    };

    if (player->frame_stride == 0) {
        return false;
    }

    init_audio_pwm(gpio, &player->slice_num, &player->pwm_channel);
    player->pace_slice = pick_pace_slice(player->slice_num);
    init_pace_pwm(wav->sample_rate, player->pace_slice);

    player->dma_chan_a = dma_claim_unused_channel(true);
    player->dma_chan_b = dma_claim_unused_channel(true);

    g_dma_buf_a.buffer = dma_samples_a;
    g_dma_buf_a.sample_count = DMA_SAMPLES;
    g_dma_buf_b.buffer = dma_samples_b;
    g_dma_buf_b.sample_count = DMA_SAMPLES;

    fill_dma_buffer(player, g_dma_buf_a.buffer, g_dma_buf_a.sample_count);
    fill_dma_buffer(player, g_dma_buf_b.buffer, g_dma_buf_b.sample_count);

    // Point DMA at the correct half-word (A/B) of the PWM CC register.
    uint16_t *cc_half = ((uint16_t *)&pwm_hw->slice[player->slice_num].cc) + player->pwm_channel;
    dma_channel_config cfg_a = dma_channel_get_default_config(player->dma_chan_a);
    dma_channel_config cfg_b = dma_channel_get_default_config(player->dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg_a, true);
    channel_config_set_write_increment(&cfg_a, false);
    channel_config_set_dreq(&cfg_a, DREQ_PWM_WRAP0 + player->pace_slice);
    channel_config_set_chain_to(&cfg_a, player->dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg_b, true);
    channel_config_set_write_increment(&cfg_b, false);
    channel_config_set_dreq(&cfg_b, DREQ_PWM_WRAP0 + player->pace_slice);
    channel_config_set_chain_to(&cfg_b, player->dma_chan_a);

    // Chain A->B and B->A for continuous playback.
    dma_channel_configure(
        player->dma_chan_a,
        &cfg_a,
        cc_half,
        g_dma_buf_a.buffer,
        g_dma_buf_a.sample_count,
        false);

    dma_channel_configure(
        player->dma_chan_b,
        &cfg_b,
        cc_half,
        g_dma_buf_b.buffer,
        g_dma_buf_b.sample_count,
        false);

    g_player = player;
    dma_channel_set_irq0_enabled(player->dma_chan_a, true);
    dma_channel_set_irq0_enabled(player->dma_chan_b, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler);
    irq_set_priority(DMA_IRQ_0, PICO_HIGHEST_IRQ_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    return true;
}

// Start the DMA chain; it will run continuously until stopped.
void audio_pwm_dma_start(audio_player_t *player) {
    if (!player) {
        return;
    }
    dma_channel_start(player->dma_chan_a);
}
