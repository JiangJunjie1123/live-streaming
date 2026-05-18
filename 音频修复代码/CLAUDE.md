# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Audio capture module that records system audio output (speaker loopback) on Windows using Qt Multimedia and converts it from S16 interleaved PCM to FLTP (float planar) format via FFmpeg's `swr_convert`, then emits frames as Qt signals for downstream encoding (e.g., AAC).

## Dependencies

- **Qt 5 or 6** (Core, Multimedia) — `QAudioInput`, `QAudioDeviceInfo`, `QTimer`, `QIODevice`
- **FFmpeg** (libswresample, libavutil) — `swr_convert`, `AVSampleFormat`, `AV_CH_LAYOUT_STEREO`
- **Windows** — uses `QAudioDeviceInfo::defaultOutputDevice()` for WASAPI loopback capture

## Architecture

### `Audio_Read` class (audio_read.h, audio_read.cpp)

The sole class. Lifecycle and data flow:

1. **`slot_openAudio()`** — Opens a `QAudioInput` on the default output device (loopback), starts a 20ms `QTimer`, and initializes the `SwrContext` (S16 → FLTP, 44.1kHz stereo).
2. **`slot_readMore()`** (timer callback every 20ms) — Reads available PCM bytes from the audio device into an internal accumulation buffer (`m_audiobuff`). Once enough bytes for complete frame(s) accumulate, runs `swr_convert` to produce planar float left/right buffers, then interleaves them into `LLLL…RRRR…` layout and emits `SIG_sendAudioFrameData` for each frame. Each emitted frame is `OneAudioSize * 2` = 8192 bytes (4096 per channel).
3. **`slot_closeAudio()`** — Stops the timer and QAudioInput.
4. **`UnInit()`** — Stops both timer and audio input.

### Key constants (all in the header)

| Constant | Value | Meaning |
|---|---|---|
| `OneAudioSize` | 4096 | Bytes per channel per frame (1024 samples × 4 bytes float) |
| `AudioCollectFrequency` | 44100 | Sample rate in Hz |
| `AUDIO_INTERVAL` | 20 | Timer interval in ms |
| `AudioChannelCount` | 2 | Stereo |
| `AVCODEC_MAX_AUDIO_FRAME_SIZE` | 192000 | Legacy define, not used in this code |

### Memory management caveats

- `slot_readMore()` heap-allocates each emitted buffer via `malloc` — the receiver **must** free these buffers.
- `slot_openAudio()` recreates `audio_in` on every call (deletes old one if restarting).
- The accumulation buffer (`m_audiobuff`) shifts unconsumed bytes to the front on each read to avoid data loss.

## Notes

- `#include "common.h"` is referenced in the header but **not present** in this repository — it may exist in the parent project this code was extracted from. Compilation will fail without it (or remove the include if unused).
- The `QMessageBox` include in the header suggests this was used in a GUI application context; the actual `.cpp` only uses it for a fatal error dialog.

## Building

No build system is present. To compile, link against Qt Multimedia and FFmpeg:

```sh
# Example (adjust paths to your Qt and FFmpeg installations)
g++ -std=c++11 audio_read.cpp -I/path/to/qt/include -I/path/to/ffmpeg \
    -lQt5Multimedia -lQt5Core -lavutil -lswresample
```
