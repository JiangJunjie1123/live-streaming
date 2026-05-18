# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 协作规则

1. **语言要求**：与我沟通必须使用中文，思考过程也要用中文表达。
2. **工程规范**：每次交互必须调用 superpowers 技能体系，严格遵守工程化编程流程（brainstorming → writing-plans → executing-plans / subagent-driven-development → verification → code-review）。
3. **多模块 / Bug 修复**：遇到多模块复杂任务或出现 bug 时，优先使用 subAgent 模式进行并行开发和错误排查。

## Build

```bash
# From Qt Creator with MinGW 32-bit kit (Qt 5.12.11)
qmake VideoRecoder/VideoRecoder.pro -spec win32-g++
make -j4 -f Makefile.Debug    # Debug build
make -j4 -f Makefile.Release  # Release build
```

Output binaries go to `build-VideoRecoder-Desktop_Qt_5_12_11_MinGW_32_bit-Debug/`.
The release/debug subdirectories need a copied `platforms/` folder from the Qt install
(see `fix_qt_platforms.bat`).

## Architecture

This is a **Qt 5.12 desktop screen recorder** using FFmpeg 4.2.2 for H.264/AAC encoding
and OpenCV 4.2.0 for camera capture. All third-party libs are bundled locally under
`VideoRecoder/ffmpeg-4.2.2/` and `VideoRecoder/opencv/`.

### Component diagram

```
RecorderDialog (main UI, QDialog)
  ├── PictureWidget (frameless PiP overlay for camera preview)
  ├── PicInPic_Read (QObject)     — camera + desktop capture, 25fps timer
  └── SaveVideoFileThread (QThread) — FFmpeg encoder + file muxer
       ├── PicInPic_Read (owns a second instance internally)
       └── Audio_Read (owns an instance internally)
```

### Signal flow

1. User clicks **开始** → `RecorderDialog::on_pb_start_clicked()` sets `STRU_AV_FORMAT`
   (filename, resolution, bitrate, which sources to record), starts capture and encoding.

2. **Video path**: `PicInPic_Read` fires a QTimer at 25fps. Each tick:
   - Captures camera frame via OpenCV `VideoCapture` → scaled to 320×240 → emitted as
     `SIG_sendPicInPic(QImage)` for the PiP overlay.
   - Grabs full desktop via `QScreen::grabWindow()` → converts RGB24 → YUV420P via
     sws_scale → emitted as `SIG_sendVideoFrameData(uint8_t*, int)`.

3. **Audio path**: `Audio_Read` fires a QTimer at 20ms intervals, reads PCM from
   `QAudioInput`, converts S16 → FLTP planar via `swr_convert`, and emits
   `SIG_sendAudioFrameData` one frame (8192 bytes for stereo 1024-sample AAC) at a time.

4. **Encoding**: `SaveVideoFileThread` receives frames into bounded producer-consumer
   queues (`QList<BufferDataNode*>` protected by `QMutex`). The thread's `run()` loop
   performs AV-sync by comparing `video_st.next_pts` and `audio_st.next_pts`, then
   encodes via `avcodec_encode_video2` / `avcodec_encode_audio2` and muxes with
   `av_interleaved_write_frame` to an MP4 (or FLV) file.

5. User clicks **停止** → sets `isStop=true`, thread drains remaining queue frames,
   writes trailer, closes codecs and file.

### Key files

| File | Role |
|---|---|
| `recorderdialog.cpp/h/ui` | Main UI, orchestrates start/stop, owns PiP widget and thread |
| `picinpic_read.cpp/h` | Camera + desktop frame capture, RGB→YUV conversion |
| `audio_read.cpp/h` | Qt audio input, PCM resampling S16→FLTP |
| `savevideofilethread.cpp/h` | FFmpeg setup (H.264 + AAC), queue management, encode+mux loop |
| `picturewidget.cpp/h` | Frameless camera preview overlay window |
| `common.h` | Shared FFmpeg + OpenCV includes and constants (FRAME_RATE=15, pixel format) |
| `VideoRecoder.pro` | qmake project file — all source config, lib/include paths |

### Important constants

- Screen capture operates at the primary screen's native resolution.
- Encoded video framerate: 15 fps (`FRAME_RATE` in `common.h`), capture timer at 25 fps
  (`FRAME_RATE` in `picinpic_read.h` — these are intentionally different).
- Video bitrate: 5 Mbps, H.264 codec with `superfast` preset and `zerolatency` tune.
- Audio: 44100 Hz stereo, AAC codec, 64 kbps, 1024-sample frame size.
- Queue caps: 30 video frames, 50 audio frames (≈1 second each).

## 音频测试说明

**Audio_Mixer** 通过 QTimer PreciseTimer 同步轮询两个 QAudioInput 设备（立体声混音 + 麦克风），在 S16 域混合（sys×0.8 + mic×1.0）后经由 swr_convert 转为 FLTP 格式送入 AAC 编码器。

录制器界面有「系统音频」复选框：

- **推流给观众（播放器在别的机器）**：勾选「系统音频」，立体声混音正常捕获系统音频，观众能听到系统声音 + 麦克风。
- **本机测试（推流端 + 观众端同机）**：**必须关闭「系统音频」**，否则立体声混音会捕获播放器输出，形成音频闭环反馈（声音无限循环、越来越尖锐）。同机测试只用麦克风。
- **录制本地文件**：勾选「系统音频」可录制系统音频 + 麦克风，不涉及网络播放，无闭环问题。
