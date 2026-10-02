Put WAV files here and copy the folder to the SD card root as `/sounds/`.

Format: PCM, 8- or 16-bit, mono or stereo (downmixed), 8–48 kHz. 22050 Hz / 16-bit / mono is the sweet spot:
small, no SD bandwidth concerns, and the internal 8-bit DAC cannot resolve more anyway.

Convert anything with:  `ffmpeg -i in.mp3 -ac 1 -ar 22050 -sample_fmt s16 scare1.wav`
Normalise to about -3 dBFS; the firmware applies `AUDIO_VOLUME` on top.
The scare file name is `SCARE_SOUND` in config.h (default `/sounds/scare1.wav`).
