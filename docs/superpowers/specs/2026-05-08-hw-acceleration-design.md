# 视频硬件加速集成设计

**日期**: 2026-05-08
**目标**: 将 NVIDIA CUDA/NVENC 硬件加速集成到 videoplayer 和 VideoRecoder
**FFmpeg**: 4.2.2 (已启用 --enable-cuvid --enable-nvenc --enable-nvdec --enable-ffnvcodec)

## 1. 总体架构

```
cuda_helper.h/cpp (共享工具模块)
    ├── check_cuda_available()      GPU可用性检测
    ├── hw_decoder_init()           解码器HW初始化
    ├── hw_encoder_setup()          编码器HW帧上下文
    └── get_hw_format()             HW像素格式匹配回调

MedieaPlayer (解码)              VideoRecoder (编码)
    CUDA → D3D11VA → 软解          NVENC → AMF → libx264
    send/receive API               send/receive API
    GPU→CPU transfer               CPU→GPU upload
```

## 2. 回退策略（最高优先级）

所有改动遵循：**先尝试硬件 → 失败立即回退软件 → 不影响核心功能**。

### 播放器解码链
1. av_hwdevice_ctx_create(CUDA) → 成功? Y:继续 N:step2
2. av_hwdevice_ctx_create(D3D11VA) → 成功? Y:继续 N:step3
3. 纯软件解码（原逻辑）

### 录制器编码链
1. avcodec_find_encoder_by_name("h264_nvenc") → 成功? Y:继续 N:step2
2. avcodec_find_encoder_by_name("h264_amf") → 成功? Y:继续 N:step3
3. avcodec_find_encoder(AV_CODEC_ID_H264) → libx264 软编

## 3. 播放器改动 (videoplayer)

### videoplayer.h - VideoState 新增
```cpp
AVBufferRef *hw_device_ctx;   // HW设备上下文
AVPixelFormat hw_pix_fmt;     // HW像素格式
bool use_hw_decode;           // 硬解标志
```

### videoplayer.cpp - 解码器初始化 (run方法)
- avcodec_find_decoder 后用循环尝试 CUDA/D3D11VA
- avcodec_get_hw_config 验证解码器支持该HW类型
- 成功则设置 decoder_ctx->hw_device_ctx + get_format 回调
- 失败则走原软件解码路径

### videoplayer.cpp - 视频解码线程 (video_thread)
- 替换已废弃的 avcodec_decode_video2
- 统一使用 avcodec_send_packet / avcodec_receive_frame
- 硬解时: av_hwframe_transfer_data(sw_frame, hw_frame, 0) GPU→CPU
- sw_frame 或软解 frame 统一走 sws_scale YUV→RGB → QImage 显示
- FLUSH_DATA 跳转逻辑保持不变

### 音频解码
不修改，保持软件解码。

## 4. 录制器改动 (VideoRecoder)

### savevideofilethread.h 新增
```cpp
AVBufferRef *hw_device_ctx;
bool use_hw_encode;
```

### savevideofilethread.cpp - 编码器选择 (add_Video_stream)
- 优先 h264_nvenc → h264_amf → libx264
- 保存 use_hw_encode 标志

### savevideofilethread.cpp - HW帧上下文 (open_video)
- use_hw_encode 时: 创建 CUDA hw_device, 设置 hw_frames_ctx
- fctx->format = AV_PIX_FMT_CUDA
- fctx->sw_format = AV_PIX_FMT_YUV420P
- c->pix_fmt = AV_PIX_FMT_CUDA

### savevideofilethread.cpp - 编码循环 (write_video_frame_from_queue)
- 硬编码路径: 获取 hw_frame → av_hwframe_transfer_data(CPU→GPU) → avcodec_send_frame
- 软编码路径: 原逻辑升级到 send/receive API
- write_frame 和队列管理不变

### 音频编码
不修改，保持软件 AAC 编码。

## 5. 共享工具模块 cuda_helper

两个项目各复制一份 cuda_helper.h/cpp，包含:
- `hw_device_init()`: 按优先级尝试 HW 类型
- `hw_decoder_setup()`: 为解码器配置 HW context
- `hw_encoder_setup()`: 为编码器配置 hw_frames_ctx

## 6. API 升级

两个项目同时从 FFmpeg 已废弃 API 升级到现代 API:
- `avcodec_decode_video2` → `avcodec_send_packet` + `avcodec_receive_frame`
- `avcodec_encode_video2` → `avcodec_send_frame` + `avcodec_receive_packet`
- `av_free_packet` → `av_packet_unref`
- `avcodec_alloc_context3` retains (already modern)

## 7. 改动文件清单

| 文件 | 操作 | 风险 |
|------|------|------|
| videoplayer.h | 新增HW成员 | 低 |
| videoplayer.cpp | 解码器双路径 + API升级 | 中 |
| cuda_helper.h/cpp (videoplayer) | 新建 | 低 |
| savevideofilethread.h | 新增HW成员 | 低 |
| savevideofilethread.cpp | 编码器双路径 + API升级 | 中 |
| cuda_helper.h/cpp (VideoRecoder) | 新建 | 低 |

## 8. 开发策略

两个独立 Agent 并行开发:
- **Agent A**: 播放器硬解码 (videoplayer 项目)
- **Agent B**: 录制器硬编码 (VideoRecoder 项目)

每个 Agent 完成后，由对方评审代码的合理性与适配性。
