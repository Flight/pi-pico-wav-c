# Pico WAV Player (PWM)

Plays an embedded WAV file out of the RP2040/RP2350 PWM pin (`GPIO0`) using a simple RC filter + speaker.

## Build
- Configure once: `cmake -S . -B build`
- Build: `ninja -C build`
- Output UF2: `build/pico-wav-c.uf2`

## Flash to Pico
- Hold BOOTSEL on the Pico and plug in USB; a drive named RPI-RP2 appears.
- Copy `build/pico-wav-c.uf2` to that drive (drag/drop or `cp`).
- The Pico reboots and begins playing immediately.

## Wiring
- Connect `GPIO0` through an RC filter (e.g., 10 kΩ + 0.1 µF) into your amplifier/speaker input.
- Share ground between Pico and amplifier.

## Converting your own WAV
- The player supports PCM WAV only (no compression), 8- or 16-bit, mono or stereo. Stereo is downmixed by channel stride; sample rate is played as-is.
- Recommended: convert to mono 8-bit unsigned PCM to match the PWM wrap (0–255).
  - Example with ffmpeg: `ffmpeg -i sample.wav -ac 1 -ar 16000 -sample_fmt u8 out.wav`
- Convert the WAV into a C header:
  - `xxd -i sample.wav > wav_data.h`
  - Ensure the header keeps the symbols `wav_data` and `wav_data_len` (rename if needed).
- Rebuild: `ninja -C build` and reflash the new UF2.

## Notes
- The sample loops when it reaches the end.
- If playback is silent, double-check: WAV is PCM (not ADPCM/MP3), sample rate is non-zero, and the header file is included as `wav_data.h`.

## MicroPython C module (rp2)
- Build MicroPython with this module:
  - `git clone https://github.com/micropython/micropython.git`
  - `git submodule update --init` (inside the repo)
  - `cmake -S ports/rp2 -B build-pico -DUSER_C_MODULES=/absolute/path/to/pico-wav-c/micropython.cmake`
  - `cmake --build build-pico`
  - Flash `build-pico/firmware.uf2` to the Pico.
- Use from MicroPython (blocking playback):
  ```python
  import pico_wav
  with open("sample.wav", "rb") as f:
      data = f.read()
  pico_wav.play(data, 0)  # GPIO0 PWM output
  ```
- Requirements: WAV must be PCM (8- or 16-bit), mono or stereo. Stereo is played by stepping through the buffer using the channel stride (effectively taking the first channel). Playback blocks the VM for the duration of the clip.
