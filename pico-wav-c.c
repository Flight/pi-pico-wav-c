#include <stdio.h>
#include "pico/stdlib.h"
#include "audio_pwm_dma.h"
#include "wav_data.h"
#include "wav.h"

// GPIO that feeds the RC filter / amplifier for PWM audio.
#define AUDIO_PIN 0

int main() {
    stdio_init_all();
    sleep_ms(2000);

    wav_info_t wav = {0};
    if (!parse_wav(wav_data, wav_data_len, &wav)) {
        // Parsing failed; stop early so we do not drive the pin with nonsense.
        printf("ERROR: WAV parsing failed!\n");
        while (true) {
            tight_loop_contents();
        }
    }

    audio_player_t player = {0};
    if (!audio_pwm_dma_init(&player, &wav, AUDIO_PIN)) {
        printf("ERROR: Failed to initialize PWM + DMA audio\n");
        while (true) {
            tight_loop_contents();
        }
    }

    audio_pwm_dma_start(&player);

    printf("Pico WAV Player - USB Debug Enabled\n");
    printf("WAV Info:\n");
    printf("  Sample Rate: %lu Hz\n", wav.sample_rate);
    printf("  Bits per Sample: %u\n", wav.bits_per_sample);
    printf("  Channels: %u\n", wav.channels);
    printf("  Data Size: %zu bytes\n", wav.data_size);
    printf("Playback started! PWM + DMA running (silence after EOF).\n");

    while (true) {
        tight_loop_contents();
    }
}
