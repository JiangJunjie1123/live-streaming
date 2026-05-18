# 视频硬件加速集成 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 NVIDIA CUDA/NVENC 硬加速集成到 videoplayer (硬解) 和 VideoRecoder (硬编)，失败自动回退软解/软编。

**Architecture:** 两个独立并行工作流 — Agent A 改播放器解码端，Agent B 改录制器编码端。各项目内创建一个轻量 cuda_helper 工具文件。所有改动有软回退路径。

**Tech Stack:** C++, Qt 5.12.11, FFmpeg 4.2.2, MinGW 32-bit, Windows 10+

---

## 并行工作流

本计划分为两个独立工作流，可同时执行：
- **Agent A**: 播放器硬件解码 (`D:\Video\videoplayer\MedieaPlayer`)
- **Agent B**: 录制器硬件编码 (`D:\Video\VideoRecoder\VideoRecoder`)

完成后的交叉评审：每个 Agent 检查对方修改的文件，确认回退策略正确、API 升级一致、无内存泄漏。

---

# Agent A: 播放器硬件解码

---

### Task A1: 创建 cuda_helper.h

**Files:**
- Create: `D:\Video\videoplayer\MedieaPlayer\cuda_helper.h`

- [ ] **Step 1: 编写 cuda_helper.h**

```cpp
#ifndef CUDA_HELPER_H
#define CUDA_HELPER_H

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixfmt.h"
}

// 按优先级尝试创建 HW 设备上下文 (CUDA → D3D11VA → NONE)
// 成功返回 >= 0，失败返回 < 0
int hw_device_init(AVBufferRef **hw_device_ctx, AVHWDeviceType *out_type);

// 检查解码器是否支持指定 HW 类型，返回匹配的 HW 像素格式
// 不支持则 hw_pix_fmt 保持 AV_PIX_FMT_NONE
void hw_decoder_check_support(const AVCodec *decoder, AVHWDeviceType hw_type,
                               AVPixelFormat *hw_pix_fmt);

// get_format 回调：让解码器输出 HW 像素格式
AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts);

#endif // CUDA_HELPER_H
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add cuda_helper.h
git commit -m "feat(player): add cuda_helper.h header for HW decode init"
```

---

### Task A2: 创建 cuda_helper.cpp

**Files:**
- Create: `D:\Video\videoplayer\MedieaPlayer\cuda_helper.cpp`

- [ ] **Step 1: 编写 cuda_helper.cpp**

```cpp
#include "cuda_helper.h"

int hw_device_init(AVBufferRef **hw_device_ctx, AVHWDeviceType *out_type)
{
    *hw_device_ctx = nullptr;
    *out_type = AV_HWDEVICE_TYPE_NONE;

    // 尝试优先级: CUDA → D3D11VA
    AVHWDeviceType types[] = {AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_D3D11VA};
    for (auto t : types) {
        int ret = av_hwdevice_ctx_create(hw_device_ctx, t, nullptr, nullptr, 0);
        if (ret >= 0) {
            *out_type = t;
            return ret;
        }
    }
    return -1;
}

void hw_decoder_check_support(const AVCodec *decoder, AVHWDeviceType hw_type,
                               AVPixelFormat *hw_pix_fmt)
{
    *hw_pix_fmt = AV_PIX_FMT_NONE;
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
        if (!config) break;
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            && config->device_type == hw_type) {
            *hw_pix_fmt = config->pix_fmt;
            break;
        }
    }
}

AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
{
    AVPixelFormat hw_fmt = AV_PIX_FMT_NONE;
    // 通过 opaque 传入 hw_pix_fmt
    if (ctx->opaque) {
        hw_fmt = *(AVPixelFormat *)ctx->opaque;
    }
    for (const AVPixelFormat *p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == hw_fmt) return *p;
    }
    // 不支持的 HW 格式，返回第一个软件格式
    return pix_fmts[0];
}
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add cuda_helper.cpp
git commit -m "feat(player): implement cuda_helper for HW device init and fallback"
```

---

### Task A3: 修改 videoplayer.h — 新增 HW 成员

**Files:**
- Modify: `D:\Video\videoplayer\MedieaPlayer\videoplayer.h`

- [ ] **Step 1: 在 VideoState 结构体中新增 HW 成员**

在 `VideoState` 结构体末尾（`VideoPlayer* m_player;` 之前）添加：

```cpp
    AVBufferRef *hw_device_ctx;// 硬件设备上下文 (CUDA/D3D11VA)
    AVPixelFormat hw_pix_fmt;  // 硬件像素格式 (AV_PIX_FMT_CUDA 等)
    bool use_hw_decode;        // 是否使用硬件解码
```

在 `VideoState()` 构造函数初始化列表中添加：

```cpp
        hw_device_ctx = nullptr;
        hw_pix_fmt = AV_PIX_FMT_NONE;
        use_hw_decode = false;
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add videoplayer.h
git commit -m "feat(player): add HW decode members to VideoState struct"
```

---

### Task A4: 修改 videoplayer.cpp — 解码器初始化（run 方法）

**Files:**
- Modify: `D:\Video\videoplayer\MedieaPlayer\videoplayer.cpp`
- Add include: `#include "cuda_helper.h"` at top

- [ ] **Step 1: 添加 include**

在 videoplayer.cpp 顶部 `#include "videoplayer.h"` 之后添加：

```cpp
#include "cuda_helper.h"
```

- [ ] **Step 2: 替换视频解码器初始化逻辑**

将以下原有代码块（`run()` 方法中，约第503-521行）：

```cpp
    //如果videoStream 为-1 说明没有找到视频流
    //视频
    if(videoStream != -1){
        //5.查找解码器
        pCodecCtx = pFormatCtx->streams[videoStream]->codec;
        qDebug() << "Video codec id:" << pCodecCtx->codec_id;
        pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
        if (pCodec == NULL) {
            qDebug()<< "Video Codec not found." ;
            goto error_cleanup;
        }
        qDebug() << "Video codec found:" << pCodec->name;
        //打开解码器
        if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            qDebug()<< "Could not open video codec." ;
            goto error_cleanup;
        }
        qDebug() << "Video codec opened";
        //视频流
        m_videoState.video_st = pFormatCtx->streams[ videoStream ];
        m_videoState.pCodecCtx = pCodecCtx;
```

替换为：

```cpp
    //如果videoStream 为-1 说明没有找到视频流
    //视频
    if(videoStream != -1){
        //5.查找解码器（新版: 复制 codecpar 避免直接使用 stream->codec）
        const AVCodecParameters *codecpar = pFormatCtx->streams[videoStream]->codecpar;
        pCodec = avcodec_find_decoder(codecpar->codec_id);
        if (pCodec == NULL) {
            qDebug()<< "Video Codec not found." ;
            goto error_cleanup;
        }
        qDebug() << "Video codec found:" << pCodec->name;

        // 分配解码器上下文
        pCodecCtx = avcodec_alloc_context3(pCodec);
        if (!pCodecCtx) {
            qDebug() << "Failed to allocate decoder context";
            goto error_cleanup;
        }
        avcodec_parameters_to_context(pCodecCtx, codecpar);

        // ---- 尝试硬件解码 ----
        m_videoState.use_hw_decode = false;
        m_videoState.hw_pix_fmt = AV_PIX_FMT_NONE;
        m_videoState.hw_device_ctx = nullptr;

        AVHWDeviceType hw_type;
        if (hw_device_init(&m_videoState.hw_device_ctx, &hw_type) >= 0) {
            hw_decoder_check_support(pCodec, hw_type, &m_videoState.hw_pix_fmt);
            if (m_videoState.hw_pix_fmt != AV_PIX_FMT_NONE) {
                pCodecCtx->hw_device_ctx = av_buffer_ref(m_videoState.hw_device_ctx);
                pCodecCtx->opaque = &m_videoState.hw_pix_fmt;
                pCodecCtx->get_format = get_hw_format;
                m_videoState.use_hw_decode = true;
                qDebug() << "HW decode enabled, pix_fmt:" << m_videoState.hw_pix_fmt;
            } else {
                av_buffer_unref(&m_videoState.hw_device_ctx);
                qDebug() << "Decoder does not support HW type, falling back to SW";
            }
        } else {
            qDebug() << "No HW device available, using software decoding";
        }
        // ---- 硬件解码初始化结束 ----

        //打开解码器
        if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
            qDebug()<< "Could not open video codec." ;
            avcodec_free_context(&pCodecCtx);
            goto error_cleanup;
        }
        qDebug() << "Video codec opened, HW decode:" << m_videoState.use_hw_decode;
        //视频流
        m_videoState.video_st = pFormatCtx->streams[ videoStream ];
        m_videoState.pCodecCtx = pCodecCtx;
```

- [ ] **Step 3: 修改错误清理路径**

在 `error_cleanup` 标签中，`avcodec_close` 调用替换为 `avcodec_free_context`：

```cpp
    // 回收空间
    if( videoStream != -1 && pCodecCtx )
    {
        avcodec_free_context(&pCodecCtx);
    }
```

并在 error_cleanup 中新增 HW 设备释放：

```cpp
    if (m_videoState.hw_device_ctx)
        av_buffer_unref(&m_videoState.hw_device_ctx);
```

- [ ] **Step 4: 修改正常退出清理路径**

在正常退出路径（约第768-777行），将：

```cpp
    if( videoStream != -1 )
    {
        avcodec_close(pCodecCtx);
    }
    avformat_close_input(&pFormatCtx);
```

替换为：

```cpp
    if( videoStream != -1 && pCodecCtx )
    {
        avcodec_free_context(&pCodecCtx);
    }
    if (m_videoState.hw_device_ctx)
        av_buffer_unref(&m_videoState.hw_device_ctx);
    avformat_close_input(&pFormatCtx);
```

- [ ] **Step 5: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add videoplayer.cpp
git commit -m "feat(player): HW decode init in run() with CUDA/D3D11VA fallback chain"
```

---

### Task A5: 修改 videoplayer.cpp — video_thread 解码循环

**Files:**
- Modify: `D:\Video\videoplayer\MedieaPlayer\videoplayer.cpp` — `video_thread()` 函数

- [ ] **Step 1: video_thread 中新增 sw_frame 变量**

在 `video_thread()` 函数开头，`pFrameRGB` 声明之后新增：

```cpp
    AVFrame *sw_frame = nullptr; // 用于 HW→CPU 传输
```

在 pFrameRGB 的 av_frame_alloc 之后新增：

```cpp
    if (is->use_hw_decode) {
        sw_frame = av_frame_alloc();
    }
```

- [ ] **Step 2: 替换解码调用**

将以下代码（约第139行）：

```cpp
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error decoding video frame\n");
            break;
        }
        //获取显示时间pts
        video_pts = pFrame->pts = pFrame->best_effort_timestamp;
        video_pts *= 1000000 *av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);//视频时钟补偿
```

替换为：

```cpp
        // 新版 send/receive 解码
        ret = avcodec_send_packet(pCodecCtx, packet);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error sending packet for decoding\n");
            break;
        }
        ret = avcodec_receive_frame(pCodecCtx, pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_unref(packet);
            continue;
        } else if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error receiving decoded frame\n");
            break;
        }

        // HW→CPU 传输
        AVFrame *display_frame = pFrame;
        if (is->use_hw_decode && pFrame->format == is->hw_pix_fmt) {
            av_hwframe_transfer_data(sw_frame, pFrame, 0);
            display_frame = sw_frame;
        }

        got_picture = 1;
        //获取显示时间pts
        video_pts = pFrame->pts = pFrame->best_effort_timestamp;
        video_pts *= 1000000 *av_q2d(is->video_st->time_base);
        video_pts = synchronize_video(is, pFrame, video_pts);//视频时钟补偿
```

- [ ] **Step 3: 修改 sws_scale 使用 display_frame**

将 `got_picture` 条件块中的 sws_scale 调用（约第163行）的数据源从 `pFrame` 改为 `display_frame`：

```cpp
        if (got_picture) {
            sws_scale(img_convert_ctx,
                      (uint8_t const * const *) display_frame->data,
                      display_frame->linesize, 0, pCodecCtx->height, pFrameRGB->data,
                      pFrameRGB->linesize);
```

- [ ] **Step 4: 替换 av_free_packet 为 av_packet_unref**

在 video_thread 中的所有 `av_free_packet(packet)` 替换为 `av_packet_unref(packet)`。

FLUSH_DATA 处理中的 `av_free_packet(packet)` 也替换为 `av_packet_unref(packet)`。

- [ ] **Step 5: 清理退出**

在 video_thread 末尾资源释放处，`av_free(pFrame)` 之前添加：

```cpp
    if (sw_frame)
        av_frame_free(&sw_frame);
```

保留原有的 `av_frame_free(&pFrame);`（注意 pFrame 用 av_frame_free 而非 av_free，因为用了 av_frame_alloc）。

- [ ] **Step 6: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add videoplayer.cpp
git commit -m "feat(player): upgrade video_thread to send/receive API with HW decode path"
```

---

### Task A6: 修改 videoplayer.cpp — timer_callback 函数

**Files:**
- Modify: `D:\Video\videoplayer\MedieaPlayer\videoplayer.cpp` — `timer_callback()` 函数

- [ ] **Step 1: 替换 timer_callback 中的解码调用**

将 `timer_callback()` 中的（约第220行）：

```cpp
        ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture,packet);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error decoding video frame\n");
            break;
        }
```

替换为：

```cpp
        ret = avcodec_send_packet(pCodecCtx, packet);
        if (ret < 0) break;
        ret = avcodec_receive_frame(pCodecCtx, pFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_unref(packet);
            continue;
        } else if (ret < 0) {
            break;
        }
        got_picture = 1;
```

- [ ] **Step 2: 替换 av_free_packet**

timer_callback 中的 `av_free_packet(packet)` → `av_packet_unref(packet)`。

- [ ] **Step 3: 替换 av_free 为现代 API**

timer_callback 末尾的 `av_free` 替换：

```cpp
    av_frame_free(&pFrame);
    av_frame_free(&pFrameRGB);
    av_free(out_buffer_rgb);
    sws_freeContext(img_convert_ctx);
```

- [ ] **Step 4: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add videoplayer.cpp
git commit -m "fix(player): upgrade timer_callback to send/receive decode API"
```

---

### Task A7: 修改 run() 方法中的 av_free_packet 调用

**Files:**
- Modify: `D:\Video\videoplayer\MedieaPlayer\videoplayer.cpp` — `run()` 方法中的读取循环

- [ ] **Step 1: 替换 run() 中读取循环的 av_free_packet**

在 run() 方法的主循环（约第730-741行）中，将 `av_free_packet(packet)` 替换为 `av_packet_unref(packet)`：

三处替换：
1. 非视频非音频流的释放：`av_free_packet(packet)` → `av_packet_unref(packet)`
2. error_cleanup 中的资源释放：将 `av_free_packet(packet)` → `av_packet_unref(packet)`
3. 将 `free(packet)` 保留不变（packet 本身是 malloc 的）

- [ ] **Step 2: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add videoplayer.cpp
git commit -m "fix(player): replace deprecated av_free_packet with av_packet_unref"
```

---

### Task A8: 修改 audio_decode_frame 中的 av_free_packet

**Files:**
- Modify: `D:\Video\videoplayer\MedieaPlayer\videoplayer.cpp` — `audio_decode_frame()` 函数

- [ ] **Step 1: 替换音频解码中的 av_free_packet**

将所有 `av_free_packet(&pkt)` 和 `av_free_packet(pkt)` 替换为 `av_packet_unref(&pkt)`。

共 3 处（约第983行，第1063行，第1066行）。

- [ ] **Step 2: 提交**

```bash
cd D:/Video/videoplayer/MedieaPlayer
git add videoplayer.cpp
git commit -m "fix(player): replace deprecated av_free_packet in audio decode"
```

---

# Agent B: 录制器硬件编码

---

### Task B1: 创建 cuda_helper.h

**Files:**
- Create: `D:\Video\VideoRecoder\VideoRecoder\cuda_helper.h`

- [ ] **Step 1: 编写 cuda_helper.h**

```cpp
#ifndef CUDA_HELPER_H
#define CUDA_HELPER_H

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixfmt.h"
}

// 选择合适的硬件编码器 (h264_nvenc → h264_amf → nullptr)
// 返回硬件编码器指针，失败返回 nullptr
AVCodec *hw_encoder_select();

// 为硬件编码器创建 hw_device_ctx 和 hw_frames_ctx
// 成功返回 >= 0，失败返回 < 0（调用者应回退到软编）
int hw_encoder_setup(AVCodecContext *enc_ctx, AVBufferRef **hw_device_ctx,
                      int width, int height);

// 获取一个 GPU 帧用于编码输入
AVFrame *hw_encoder_get_frame(AVCodecContext *enc_ctx);

// 清理 HW 编码资源
void hw_encoder_cleanup(AVBufferRef **hw_device_ctx);

#endif // CUDA_HELPER_H
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add cuda_helper.h
git commit -m "feat(recorder): add cuda_helper.h header for HW encode"
```

---

### Task B2: 创建 cuda_helper.cpp

**Files:**
- Create: `D:\Video\VideoRecoder\VideoRecoder\cuda_helper.cpp`

- [ ] **Step 1: 编写 cuda_helper.cpp**

```cpp
#include "cuda_helper.h"

AVCodec *hw_encoder_select()
{
    AVCodec *codec = nullptr;
    codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (codec) return codec;
    codec = avcodec_find_encoder_by_name("h264_amf");
    if (codec) return codec;
    return nullptr;
}

int hw_encoder_setup(AVCodecContext *enc_ctx, AVBufferRef **hw_device_ctx,
                      int width, int height)
{
    // 创建 CUDA 设备
    int ret = av_hwdevice_ctx_create(hw_device_ctx, AV_HWDEVICE_TYPE_CUDA,
                                      nullptr, nullptr, 0);
    if (ret < 0) {
        // NVIDIA 不可用，尝试 D3D11VA（某些卡用这个也能加速编码）
        ret = av_hwdevice_ctx_create(hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA,
                                      nullptr, nullptr, 0);
        if (ret < 0) return ret;
    }

    // 创建 HW 帧上下文
    AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(*hw_device_ctx);
    if (!hw_frames_ref) {
        av_buffer_unref(hw_device_ctx);
        return -1;
    }

    AVHWFramesContext *fctx = (AVHWFramesContext *)hw_frames_ref->data;
    fctx->format    = AV_PIX_FMT_CUDA;
    fctx->sw_format = AV_PIX_FMT_YUV420P;
    fctx->width     = width;
    fctx->height    = height;
    fctx->initial_pool_size = 20;

    ret = av_hwframe_ctx_init(hw_frames_ref);
    if (ret < 0) {
        av_buffer_unref(&hw_frames_ref);
        av_buffer_unref(hw_device_ctx);
        return ret;
    }

    enc_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    enc_ctx->pix_fmt = AV_PIX_FMT_CUDA;

    av_buffer_unref(&hw_frames_ref);
    return 0;
}

AVFrame *hw_encoder_get_frame(AVCodecContext *enc_ctx)
{
    if (!enc_ctx || !enc_ctx->hw_frames_ctx) return nullptr;
    AVFrame *hw_frame = av_frame_alloc();
    if (!hw_frame) return nullptr;
    int ret = av_hwframe_get_buffer(enc_ctx->hw_frames_ctx, hw_frame, 0);
    if (ret < 0) {
        av_frame_free(&hw_frame);
        return nullptr;
    }
    return hw_frame;
}

void hw_encoder_cleanup(AVBufferRef **hw_device_ctx)
{
    if (*hw_device_ctx) {
        av_buffer_unref(hw_device_ctx);
        *hw_device_ctx = nullptr;
    }
}
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add cuda_helper.cpp
git commit -m "feat(recorder): implement cuda_helper for HW encoder selection and setup"
```

---

### Task B3: 修改 savevideofilethread.h

**Files:**
- Modify: `D:\Video\VideoRecoder\VideoRecoder\savevideofilethread.h`

- [ ] **Step 1: 添加 HW 编码成员**

在 `SaveVideoFileThread` 类的 private 区域，现有 FFmpeg 成员之后添加：

```cpp
    AVBufferRef *hw_device_ctx;   // HW 编码器设备上下文
    bool use_hw_encode;           // 是否使用硬件编码
```

在构造函数初始化列表中需要初始化为 `nullptr` 和 `false`。

- [ ] **Step 2: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add savevideofilethread.h
git commit -m "feat(recorder): add HW encode members to SaveVideoFileThread"
```

---

### Task B4: 修改 savevideofilethread.cpp — 编码器选择与 HW 初始化

**Files:**
- Modify: `D:\Video\VideoRecoder\VideoRecoder\savevideofilethread.cpp`
- Add include: `#include "cuda_helper.h"` at top

- [ ] **Step 1: 添加 include**

在 savevideofilethread.cpp 顶部 `#include "audio_read.h"` 之后：

```cpp
#include "cuda_helper.h"
```

- [ ] **Step 2: 重写 add_Video_stream — 编码器选择 + HW 初始化（原子操作，失败即回退）**

```cpp
void SaveVideoFileThread::add_Video_stream(OutputStream *ost, AVFormatContext *oc,
                                           AVCodec **codec,
                                           enum AVCodecID codec_id)
{
    AVCodecContext *c;

    // 原子决策: 尝试 HW 编码器选择 + 初始化，失败立即回退软编
    use_hw_encode = false;
    hw_device_ctx = nullptr;

    AVCodec *hw_codec = hw_encoder_select();
    if (hw_codec) {
        // 分配临时上下文，尝试 HW 初始化
        AVCodecContext *test_ctx = avcodec_alloc_context3(hw_codec);
        if (test_ctx) {
            test_ctx->width  = m_avFormat.width;
            test_ctx->height = m_avFormat.height;
            test_ctx->bit_rate = m_avFormat.videoBitRate;
            test_ctx->framerate = { m_avFormat.frame_rate, 1 };
            test_ctx->time_base = { 1, m_avFormat.frame_rate };

            if (hw_encoder_setup(test_ctx, &hw_device_ctx,
                                  m_avFormat.width, m_avFormat.height) >= 0) {
                *codec = hw_codec;
                use_hw_encode = true;
                fprintf(stderr, "HW encoder ready: %s\n", hw_codec->name);
            } else {
                fprintf(stderr, "HW encoder %s found but setup failed, falling back to SW\n",
                        hw_codec->name);
                hw_encoder_cleanup(&hw_device_ctx);
            }
            avcodec_free_context(&test_ctx);
        }
    }

    if (!use_hw_encode) {
        *codec = avcodec_find_encoder(codec_id);
        fprintf(stderr, "Using software encoder: %s\n",
                (*codec) ? (*codec)->name : "NONE");
    }

    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    c->codec_id = codec_id;
    c->bit_rate = m_avFormat.videoBitRate;
    c->width    = m_avFormat.width;
    c->height   = m_avFormat.height;

    c->framerate = { m_avFormat.frame_rate, 1 };
    ost->st->avg_frame_rate = { m_avFormat.frame_rate, 1 };
    ost->st->r_frame_rate = { m_avFormat.frame_rate, 1 };
    ost->st->time_base = { 1, m_avFormat.frame_rate };
    c->time_base = { 1, m_avFormat.frame_rate };
    c->ticks_per_frame = 1;

    c->gop_size = 15;

    if (use_hw_encode) {
        // 将已初始化好的 hw_frames_ctx 转移到实际编码器上下文
        // hw_encoder_setup 已在 test_ctx 上创建了 hw_device_ctx 和 hw_frames_ctx
        // 需要重新创建 hw_frames_ctx 绑定到 ost->enc
        AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
        AVHWFramesContext *fctx = (AVHWFramesContext *)hw_frames_ref->data;
        fctx->format    = AV_PIX_FMT_CUDA;
        fctx->sw_format = AV_PIX_FMT_YUV420P;
        fctx->width     = c->width;
        fctx->height    = c->height;
        fctx->initial_pool_size = 20;
        av_hwframe_ctx_init(hw_frames_ref);
        c->hw_frames_ctx = hw_frames_ref;
        c->pix_fmt = AV_PIX_FMT_CUDA;
    } else {
        c->pix_fmt = STREAM_PIX_FMT;
    }

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}
```

- [ ] **Step 3: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add savevideofilethread.cpp
git commit -m "feat(recorder): atomic HW encoder select+init with fallback to libx264"
```

---

### Task B5: 修改 savevideofilethread.cpp — open_video 适配 HW/SW 双路径

**Files:**
- Modify: `D:\Video\VideoRecoder\VideoRecoder\savevideofilethread.cpp` — `open_video()` 函数

- [ ] **Step 1: 重写 open_video — HW/SW 双路径帧分配**

```cpp
void SaveVideoFileThread::open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_set(&opt, "preset", "superfast", 0);
    av_dict_set(&opt, "tune", "zerolatency", 0);

    // 打开编码器（HW/SW 共用）
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    // 无论 HW/SW，都需要 CPU YUV 帧作为 sws_scale 输出目标
    ost->frame = av_frame_alloc();
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    ost->frame->format = (use_hw_encode) ? AV_PIX_FMT_YUV420P : c->pix_fmt;
    ost->frame->width  = c->width;
    ost->frame->height = c->height;
    ret = av_frame_get_buffer(ost->frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame buffer\n");
        exit(1);
    }
    ost->frameBuffer = ost->frame->data[0];
    ost->frameBufferSize = ost->frame->buf[0]->size;

    // 复制流参数到 muxer
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add savevideofilethread.cpp
git commit -m "feat(recorder): open_video dual-path frame alloc for HW/SW encode"
```

---

### Task B6: 修改 savevideofilethread.cpp — 编码循环 HW 路径

**Files:**
- Modify: `D:\Video\VideoRecoder\VideoRecoder\savevideofilethread.cpp` — `write_video_frame_from_queue()` 函数

- [ ] **Step 1: 重写 write_video_frame_from_queue 编码段为 HW/SW 双路径**

RGB→YUV 转换（sws_scale）之前的逻辑保持不变。`video_st.frame` 在 HW 和 SW 模式下都存在，作为 sws_scale 的输出目标（CPU YUV 帧）。

将接下来的编码+mux 段（从 `video_st.frame->pts = video_st.next_pts++;` 开始到函数结束）替换为：

```cpp
    // 设置 pts（HW/SW 共用）
    video_st.frame->pts = video_st.next_pts++;

    if (use_hw_encode) {
        // === HW 编码路径: CPU YUV → GPU 上传 → NVENC 编码 ===

        AVFrame *hw_frame = hw_encoder_get_frame(video_st.enc);
        if (!hw_frame) {
            fprintf(stderr, "Failed to get HW frame, dropping\n");
            return false;
        }

        // video_st.frame (CPU YUV420P) → hw_frame (GPU CUDA)
        ret = av_hwframe_transfer_data(hw_frame, video_st.frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Failed to transfer frame to GPU\n");
            av_frame_free(&hw_frame);
            return false;
        }
        hw_frame->pts = video_st.frame->pts;

        ret = avcodec_send_frame(video_st.enc, hw_frame);
        av_frame_free(&hw_frame);
        if (ret < 0) {
            fprintf(stderr, "Error sending frame to HW encoder\n");
            return false;
        }

        AVPacket pkt = { 0 };
        av_init_packet(&pkt);
        while (ret >= 0) {
            ret = avcodec_receive_packet(video_st.enc, &pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;
            AVRational tb = {1, 1000};
            video_pts = av_rescale_q(video_st.next_pts, video_st.st->time_base, tb);
            ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
            av_packet_unref(&pkt);
        }
    } else {
        // === SW 编码路径（send/receive API 替代已废弃的 encode_video2）===

        ret = avcodec_send_frame(video_st.enc, video_st.frame);
        if (ret < 0) {
            fprintf(stderr, "Error sending frame to encoder\n");
            return false;
        }

        AVPacket pkt = { 0 };
        av_init_packet(&pkt);
        while (ret >= 0) {
            ret = avcodec_receive_packet(video_st.enc, &pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;
            AVRational tb = {1, 1000};
            video_pts = av_rescale_q(video_st.next_pts, video_st.st->time_base, tb);
            ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
            av_packet_unref(&pkt);
        }
    }

    return true;
```

- [ ] **Step 2: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add savevideofilethread.cpp
git commit -m "feat(recorder): dual-path encode loop with HW NVENC and SW libx264 fallback"
```

---

### Task B7: 修改 savevideofilethread.cpp — 清理 HW 资源

**Files:**
- Modify: `D:\Video\VideoRecoder\VideoRecoder\savevideofilethread.cpp` — `close_stream()` 和 `~SaveVideoFileThread()`

- [ ] **Step 1: 在 close_stream 中新增 HW 设备释放**

在 `close_stream()` 函数末尾添加：

```cpp
    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
```

- [ ] **Step 2: 修改析构函数**

在 `~SaveVideoFileThread()` 中添加：

```cpp
    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
```

- [ ] **Step 3: 构造函数初始化新成员**

在 `SaveVideoFileThread()` 构造函数初始化列表中添加：

```cpp
    , hw_device_ctx(nullptr)
    , use_hw_encode(false)
```

- [ ] **Step 4: 提交**

```bash
cd D:/Video/VideoRecoder/VideoRecoder
git add savevideofilethread.cpp
git commit -m "fix(recorder): HW resource cleanup in destructor and close_stream"
```

---

# 交叉评审

两个 Agent 都完成后，进行交叉评审。

### Task CR1: Agent A 评审 Agent B 的代码

检查项:
- `use_hw_encode` 在各路径中是否被正确设置为 false 时不会崩溃
- `hw_encoder_setup` 失败后是否释放了所有资源
- `write_video_frame_from_queue` 中 HW pkt 的 `av_packet_unref` 是否正确
- 构造函数是否正确初始化了新增成员

### Task CR2: Agent B 评审 Agent A 的代码

检查项:
- `use_hw_decode` 为 false 时 `sw_frame` 是否为 null 且不会被解引用
- `avcodec_free_context` 替代 `avcodec_close` 后的双重释放风险
- `display_frame` 指针赋值是否正确处理了软硬解
- `hw_device_ctx` 在正常退出和 error_cleanup 中都释放了吗

---

# 构建验证

### Task V1: 构建播放器

```bash
cd D:/Video/videoplayer
qmake MedieaPlayer/MedieaPlayer.pro -spec win32-g++
make -j4 -f Makefile.Debug
```

### Task V2: 构建录制器

```bash
cd D:/Video/VideoRecoder
qmake VideoRecoder/VideoRecoder.pro -spec win32-g++
make -j4 -f Makefile.Debug
```
