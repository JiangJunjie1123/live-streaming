#include "savevideofilethread.h"
#include "audio_mixer.h"
#include "cuda_helper.h"
#include <cstring>  // for memset
#include <cmath>  // for tanhf
#include <QElapsedTimer>
#include <QDebug>

//优化 前向声明
static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt);

SaveVideoFileThread::SaveVideoFileThread()
    : oc(nullptr)
    , fmt(nullptr)
    , video_codec(nullptr)
    , audio_codec(nullptr)
    , have_video(0)
    , have_audio(0)
    , encode_video(0)
    , encode_audio(0)
    , m_isRecording(false)
    , m_startTime(-1)
    , mAudioOneFrameSize(0)
    //优化 新增成员初始化
    , lastVideoNode(nullptr)
    , isStop(false)
    , video_pts(0)
    , audio_pts(0)
    , m_videoBeginFlag(true)
    , m_videoBeginTime(0)
    , hw_device_ctx(nullptr)
    , use_hw_encode(false)
{
    m_picInPicRead = new PicInPic_Read;
    m_audioRead = new Audio_Mixer("立体声混音 (Realtek(R) Audio)",
                                   "麦克风阵列 (适用于数字麦克风的英特尔® 智音技术)");

    connect(m_picInPicRead,SIGNAL(SIG_sendPicInPic(QImage)),this,SIGNAL(SIG_sendPicInPic(QImage)));
    connect(m_picInPicRead,SIGNAL(SIG_sendVideoFrame(QImage)),this,SIGNAL(SIG_sendVideoFrame(QImage)));
    connect(m_picInPicRead,SIGNAL(SIG_sendVideoFrameData(uint8_t*,int)),this,SLOT(slot_writeVideoFrameData(uint8_t*,int)));

    connect(m_audioRead, SIGNAL(SIG_sendAudioFrameData(uint8_t*,int)),
            this, SLOT(slot_writeAudioFrameData(uint8_t*,int)));

    // 初始化 video_st 和 audio_st 结构体
    memset(&video_st, 0, sizeof(video_st));
    memset(&audio_st, 0, sizeof(audio_st));
}


//优化 析构函数，清理队列资源
SaveVideoFileThread::~SaveVideoFileThread()
{
    // 清理视频队列
    m_videoMutex.lock();
    while (!m_videoDataList.isEmpty()) {
        BufferDataNode* node = m_videoDataList.takeFirst();
        if (node) {
            av_free(node->buffer);
            free(node);
        }
    }
    m_videoMutex.unlock();

    // 清理音频队列
    m_audioMutex.lock();
    while (!m_audioDataList.isEmpty()) {
        BufferDataNode* node = m_audioDataList.takeFirst();
        if (node) {
            free(node->buffer);
            free(node);
        }
    }
    m_audioMutex.unlock();

    // 清理 m_audioRead
    delete m_audioRead;
    m_audioRead = nullptr;
    delete m_picInPicRead;
    m_picInPicRead = nullptr;

    // 清理 lastVideoNode
    if (lastVideoNode) {
        av_free(lastVideoNode->buffer);
        free(lastVideoNode);
        lastVideoNode = nullptr;
    }

    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
}


//优化 视频队列入队
void SaveVideoFileThread::videoDataQueue_Input(uint8_t *buffer, int size, int64_t time,
                                                int width, int height, int stride)
{
    // 队列大小限制，防止内存溢出
    m_videoMutex.lock();
    if (m_videoDataList.size() >= MAX_VIDEO_QUEUE_SIZE) {
        BufferDataNode* oldNode = m_videoDataList.takeFirst();
        if (oldNode) {
            av_free(oldNode->buffer);
            free(oldNode);
        }
    }
    m_videoMutex.unlock();

    // 创建新节点
    BufferDataNode *node = (BufferDataNode*)malloc(sizeof(BufferDataNode));
    node->bufferSize = size;
    node->time = time;
    node->buffer = buffer;
    node->width = width;
    node->height = height;
    node->stride = stride;

    m_videoMutex.lock();
    m_videoDataList.append(node);
    m_videoMutex.unlock();
}

//优化 视频队列出队（带稳帧）
BufferDataNode* SaveVideoFileThread::videoDataQueue_get(int64_t time)
{
    BufferDataNode *node = nullptr;

    m_videoMutex.lock();
    while (m_videoDataList.size() != 0) {
        node = m_videoDataList.takeFirst();
        if (m_videoDataList.size() == 0) break;

        if (time > node->time) {
            // 丢弃过早的帧
            av_free(node->buffer);
            free(node);
            node = nullptr;
        } else {
            break;
        }
    }
    m_videoMutex.unlock();

    return node;
}


//优化 音频队列入队
void SaveVideoFileThread::audioDataQueue_Input(const uint8_t *buffer, int size)
{
    // 队列大小限制
    m_audioMutex.lock();
    if (m_audioDataList.size() >= MAX_AUDIO_QUEUE_SIZE) {
        BufferDataNode* oldNode = m_audioDataList.takeFirst();
        if (oldNode) {
            free(oldNode->buffer);
            free(oldNode);
        }
    }
    m_audioMutex.unlock();

    // 创建新节点（复制数据）
    BufferDataNode *node = (BufferDataNode*)malloc(sizeof(BufferDataNode));
    node->bufferSize = size;
    node->buffer = (uint8_t*)malloc(size);
    memcpy(node->buffer, buffer, size);

    m_audioMutex.lock();
    m_audioDataList.append(node);
    m_audioMutex.unlock();
}

//优化 音频队列出队
BufferDataNode* SaveVideoFileThread::audioDataQueue_get()
{
    BufferDataNode *node = nullptr;

    m_audioMutex.lock();
    if (m_audioDataList.size() != 0) {
        node = m_audioDataList.takeFirst();
    }
    m_audioMutex.unlock();

    return node;
}

//优化 从队列写视频帧 (RGB→YUV 改在编码线程完成，与旧工作代码管线一致)
bool SaveVideoFileThread::write_video_frame_from_queue()
{
    // 计算下一帧的时间戳（毫秒）
    int64_t next_frame_time = (video_st.next_pts * 1000) / m_avFormat.frame_rate;

    // 从队列获取帧（带稳帧处理）
    BufferDataNode *node = videoDataQueue_get(next_frame_time);

    if (node == nullptr) {
        // 队列为空，尝试使用上一帧补帧
        if (lastVideoNode) {
            node = lastVideoNode;
        } else {
            return false;
        }
    } else {
        // 更新 lastVideoNode
        if (node != lastVideoNode) {
            if (lastVideoNode) {
                av_free(lastVideoNode->buffer);
                free(lastVideoNode);
            }
            lastVideoNode = node;
        }
    }

    if (!node) return false;

    int width = video_st.enc->width;
    int height = video_st.enc->height;
    int stride = (node->stride > 0) ? node->stride : width * 3;  // 兼容旧数据

    // 队列存储的是 QImage 原始缓冲区（含行末填充）
    int expectedSize = stride * height;
    if (node->bufferSize < expectedSize) {
        fprintf(stderr, "RGB buffer size mismatch: got %d, expected %d (w=%d h=%d stride=%d)\n",
                node->bufferSize, expectedSize, width, height, stride);
        return false;
    }

    // 确保帧可写 — 编码器可能持有上一帧的引用
    int ret = av_frame_make_writable(video_st.frame);
    if (ret < 0) {
        fprintf(stderr, "Could not make frame writable\n");
        return false;
    }

    // 创建或复用 sws_ctx（与旧工作代码一致：一次创建，反复使用）
    if (!video_st.sws_ctx) {
        video_st.sws_ctx = sws_getContext(width, height, AV_PIX_FMT_RGB24,
                                          width, height, AV_PIX_FMT_YUV420P,
                                          SWS_BICUBIC, NULL, NULL, NULL);
        if (!video_st.sws_ctx) {
            fprintf(stderr, "Failed to create sws context\n");
            return false;
        }
    }

    // 准备输入：使用 QImage 原生 stride，与旧工作代码数据布局完全一致
    uint8_t *src_data[4] = { node->buffer, NULL, NULL, NULL };
    int src_linesize[4] = { stride, 0, 0, 0 };

    // 一步完成 RGB→YUV 转换，直接写入编码器帧
    sws_scale(video_st.sws_ctx, (const uint8_t *const *)src_data, src_linesize,
              0, height, video_st.frame->data, video_st.frame->linesize);

    // 设置 pts
    video_st.frame->pts = video_st.next_pts++;

    if (use_hw_encode) {
        // HW encode path: CPU YUV → GPU upload → NVENC encode

        AVFrame *hw_frame = hw_encoder_get_frame(video_st.enc);
        if (!hw_frame) {
            fprintf(stderr, "Failed to get HW frame, dropping\n");
            return false;
        }

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
            AVRational tb = (AVRational){1, 1000};
            video_pts = av_rescale_q(video_st.next_pts, video_st.st->time_base, tb);
            ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
            if (ret < 0) {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                Q_EMIT SIG_EncodeError(QString("Push stream write failed: %1").arg(errbuf));
                isStop = true;
                av_packet_unref(&pkt);
                return false;
            }
            av_packet_unref(&pkt);
        }
    } else {
        // SW encode path (send/receive API replacing deprecated encode_video2)

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
            AVRational tb = (AVRational){1, 1000};
            video_pts = av_rescale_q(video_st.next_pts, video_st.st->time_base, tb);
            ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
            if (ret < 0) {
                char errbuf[128];
                av_strerror(ret, errbuf, sizeof(errbuf));
                Q_EMIT SIG_EncodeError(QString("Push stream write failed: %1").arg(errbuf));
                isStop = true;
                av_packet_unref(&pkt);
                return false;
            }
            av_packet_unref(&pkt);
        }
    }

    return true;
}

//优化 从队列写音频帧
bool SaveVideoFileThread::write_audio_frame_from_queue()
{
    // 从队列获取音频数据
    BufferDataNode *node = audioDataQueue_get();

    if (!node) {
        return false;
    }

    // 复制数据到帧缓冲区
    memcpy(audio_st.frameBuffer, node->buffer, node->bufferSize);

    // 跳过全零帧（WASAPI 初始化阶段产生的静音），避免编码器被零帧搞垮
    bool allZero = true;
    float *checkBuf = (float *)audio_st.frameBuffer;
    for (int i = 0; i < audio_st.frame->nb_samples * 2; i++) {
        if (checkBuf[i] != 0.0f) { allZero = false; break; }
    }
    if (allZero) {
        free(node->buffer);
        free(node);
        return false;
    }

    // —— 诊断层4：检查送入编码器的数据 ——
    static int diagFrameCount = 0;
    if (diagFrameCount < 5 || diagFrameCount % 100 == 0) {
        float *fL = (float *)audio_st.frameBuffer;
        float *fR = (float *)(audio_st.frameBuffer + node->bufferSize/2);
        float maxL = 0.0f, maxR = 0.0f;
        int zeroRunL = 0, maxZeroRunL = 0;
        for (int i = 0; i < 1024; i++) {
            if (fabsf(fL[i]) > maxL) maxL = fabsf(fL[i]);
            if (fabsf(fR[i]) > maxR) maxR = fabsf(fR[i]);
            if (fL[i] == 0.0f) { zeroRunL++; if (zeroRunL > maxZeroRunL) maxZeroRunL = zeroRunL; }
            else zeroRunL = 0;
        }
        fprintf(stderr, "[Encode] frame #%d L[0..3]=%.6f,%.6f,%.6f,%.6f maxL=%.4f maxR=%.4f zeroRun=%d\n",
                diagFrameCount, fL[0], fL[1], fL[2], fL[3], maxL, maxR, maxZeroRunL);
    }
    diagFrameCount++;
    if (diagFrameCount == 501) diagFrameCount = 5; // reset counter to keep logging

    // 释放节点
    free(node->buffer);
    free(node);

    // 计算 pts
    AVRational rational;
    rational.num = 1;
    rational.den = audio_st.enc->sample_rate;
    audio_st.frame->pts = av_rescale_q(audio_st.samples_count, rational, audio_st.enc->time_base);
    audio_st.samples_count += audio_st.frame->nb_samples;

    // 编码并写入
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    int got_packet = 0;
    int ret = avcodec_encode_audio2(audio_st.enc, &pkt, audio_st.frame, &got_packet);

    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame\n");
        return false;
    }

    if (got_packet) {
        // 更新 audio_pts（毫秒）
        AVRational tb = {1, 1000};
        audio_pts = av_rescale_q(audio_st.next_pts, audio_st.st->time_base, tb);
        audio_st.next_pts += audio_st.frame->nb_samples;

        ret = write_frame(oc, &audio_st.enc->time_base, audio_st.st, &pkt);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            Q_EMIT SIG_EncodeError(QString("Push stream write failed: %1").arg(errbuf));
            isStop = true;
            av_packet_unref(&pkt);
            return false;
        }
    }

    return true;
}


static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

int interrupt_cb(void *ctx) {
    SaveVideoFileThread *self = (SaveVideoFileThread*)ctx;
    return self->isStop ? 1 : 0;
}


void SaveVideoFileThread::slot_writeVideoFrame(QImage image)
{
//优化 原代码保留，注释掉
/*
    if (!m_isRecording) {
        return;
    }
    
    if (!video_st.frame || !video_st.enc) {
        fprintf(stderr, "Video frame or encoder not initialized\n");
        return;
    }
    
    // 确保帧可写
    int ret = av_frame_make_writable(video_st.frame);
    if (ret < 0) {
        fprintf(stderr, "Could not make frame writable\n");
        return;
    }
    
    // 将 QImage 转换为 YUV420P 格式
    int w = image.width();
    int h = image.height();
    
    // 确保图像格式是 RGB24
    QImage rgbImage = image.convertToFormat(QImage::Format_RGB888);
    
    // 使用 sws_scale 进行颜色空间转换 (RGB -> YUV420P)
    if (!video_st.sws_ctx) {
        video_st.sws_ctx = sws_getContext(w, h, AV_PIX_FMT_RGB24,
                                          video_st.enc->width, video_st.enc->height, AV_PIX_FMT_YUV420P,
                                          SWS_BICUBIC, NULL, NULL, NULL);
        if (!video_st.sws_ctx) {
            fprintf(stderr, "Failed to create sws context\n");
            return;
        }
    }
    
    // 准备输入数据
    uint8_t *src_data[4] = { nullptr };
    int src_linesize[4] = { 0 };
    src_data[0] = rgbImage.bits();
    src_linesize[0] = rgbImage.bytesPerLine();
    
    // 转换到 YUV420P
    sws_scale(video_st.sws_ctx, (const uint8_t *const *)src_data, src_linesize,
              0, h, video_st.frame->data, video_st.frame->linesize);
    
    // 设置帧的 pts (使用基于时间的计算，确保播放速度正确)
    if (m_startTime < 0) {
        m_startTime = QDateTime::currentMSecsSinceEpoch();
    }
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 elapsedMs = currentTime - m_startTime;
    // 将毫秒转换为帧数 (基于帧率)
    video_st.frame->pts = (elapsedMs * m_avFormat.frame_rate) / 1000;
    
    // 编码并写入帧
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    
    int got_packet = 0;
    ret = avcodec_encode_video2(video_st.enc, &pkt, video_st.frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding video frame\n");
        return;
    }
    
    if (got_packet) {
        ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing video frame\n");
        }
    }
*/
    //优化 现在由 slot_writeVideoFrameData 处理
    Q_UNUSED(image);
}

//优化 视频帧数据写入队列
void SaveVideoFileThread::slot_writeVideoFrameData(uint8_t *picture_buf, int buffer_size)
{
    if (!m_isRecording) {
        av_free(picture_buf);
        return;
    }

    // 记录第一帧时间
    if (m_videoBeginFlag) {
        m_videoBeginTime = QDateTime::currentMSecsSinceEpoch();
        m_videoBeginFlag = false;
    }

    // 计算当前帧的时间戳
    qint64 curTime = QDateTime::currentMSecsSinceEpoch();
    qint64 pts = curTime - m_videoBeginTime;

    // 写入队列
    videoDataQueue_Input(picture_buf, buffer_size, pts);
}

// 接收 QImage 并在编码线程中直接做 RGB→YUV 转换（恢复旧工作代码的管线）
void SaveVideoFileThread::slot_queueVideoFrame(QImage image)
{
    if (!m_isRecording) return;

    // 记录第一帧时间
    if (m_videoBeginFlag) {
        m_videoBeginTime = QDateTime::currentMSecsSinceEpoch();
        m_videoBeginFlag = false;
    }

    // 防御性确保格式为 RGB888（scaled() 可能改变格式）
    if (image.format() != QImage::Format_RGB888) {
        image = image.convertToFormat(QImage::Format_RGB888);
    }

    int w = image.width();
    int h = image.height();
    int stride = image.bytesPerLine();  // QImage 原生行跨度（含 4 字节对齐填充）

    // 完整拷贝 QImage 原始缓冲区（含行末填充字节），保持数据布局与旧工作代码完全一致
    int dataSize = stride * h;
    uint8_t *rgbBuf = (uint8_t *)av_malloc(dataSize);
    if (!rgbBuf) return;
    memcpy(rgbBuf, image.constBits(), dataSize);

    qint64 curTime = QDateTime::currentMSecsSinceEpoch();
    qint64 pts = curTime - m_videoBeginTime;

    videoDataQueue_Input(rgbBuf, dataSize, pts, w, h, stride);
}

/* Add an output stream. */
void SaveVideoFileThread::add_Video_stream(OutputStream *ost, AVFormatContext *oc,
                                           AVCodec **codec,
                                           enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int ret;

    // Atomic: try HW encoder select + setup, fall back to SW immediately
    use_hw_encode = false;
    hw_device_ctx = nullptr;

    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

    AVCodec *hw_codec = hw_encoder_select();
    if (hw_codec) {
        AVCodecContext *test_ctx = avcodec_alloc_context3(hw_codec);
        if (test_ctx) {
            test_ctx->width  = m_avFormat.width;
            test_ctx->height = m_avFormat.height;
            test_ctx->bit_rate = m_avFormat.videoBitRate;
            test_ctx->framerate = (AVRational){ m_avFormat.frame_rate, 1 };
            test_ctx->time_base = (AVRational){ 1, m_avFormat.frame_rate };

            if (hw_encoder_setup(test_ctx, &hw_device_ctx,
                                  m_avFormat.width, m_avFormat.height,
                                  &hw_pix_fmt) >= 0) {
                // Full test: try to actually open the HW encoder
                if (avcodec_open2(test_ctx, hw_codec, NULL) >= 0) {
                    avcodec_close(test_ctx);
                    *codec = hw_codec;
                    use_hw_encode = true;
                    fprintf(stderr, "HW encoder ready: %s\n", hw_codec->name);
                } else {
                    fprintf(stderr, "HW encoder %s setup OK but open failed, falling back to SW\n",
                            hw_codec->name);
                    hw_encoder_cleanup(&hw_device_ctx);
                }
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

    c->framerate = (AVRational){ m_avFormat.frame_rate, 1 };
    ost->st->avg_frame_rate = (AVRational){ m_avFormat.frame_rate, 1 };
    ost->st->r_frame_rate = (AVRational){ m_avFormat.frame_rate, 1 };
    ost->st->time_base = (AVRational){ 1, m_avFormat.frame_rate };
    c->time_base = (AVRational){ 1, m_avFormat.frame_rate };
    c->ticks_per_frame = 1;

    c->gop_size = 15;
    c->max_b_frames = 0;  // 禁用B帧，降低推流延迟

    if (use_hw_encode) {
        // Re-create hw_frames_ctx for the actual encoder context
        AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!hw_frames_ref) {
            fprintf(stderr, "Failed to alloc hw frames context\n");
            return;
        }
        AVHWFramesContext *fctx = (AVHWFramesContext *)hw_frames_ref->data;
        fctx->format    = hw_pix_fmt;
        fctx->sw_format = AV_PIX_FMT_YUV420P;
        fctx->width     = c->width;
        fctx->height    = c->height;
        fctx->initial_pool_size = 20;
        ret = av_hwframe_ctx_init(hw_frames_ref);
        if (ret < 0) {
            fprintf(stderr, "Failed to init hw frames context\n");
            av_buffer_unref(&hw_frames_ref);
            return;
        }
        c->hw_frames_ctx = hw_frames_ref;
        c->pix_fmt = hw_pix_fmt;
    } else {
        c->pix_fmt = STREAM_PIX_FMT;
    }

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}


void SaveVideoFileThread::open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    if (!use_hw_encode) {
        // x264 software encoder options
        av_dict_set(&opt, "preset", "superfast", 0);
        av_dict_set(&opt, "tune", "zerolatency", 0);
    }
    // HW encoder (NVENC) uses default options — no preset/tune needed

    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Could not open video codec: %s\n", errbuf);
        // Fall back to SW encoder if HW open fails
        if (use_hw_encode) {
            fprintf(stderr, "HW encoder open failed, falling back to SW\n");
            use_hw_encode = false;
            hw_encoder_cleanup(&hw_device_ctx);
            // Re-create SW encoder context
            avcodec_free_context(&ost->enc);
            AVCodec *sw_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            if (!sw_codec) { fprintf(stderr, "No SW encoder found\n"); return; }
            c = avcodec_alloc_context3(sw_codec);
            if (!c) return;
            ost->enc = c;
            c->codec_id = AV_CODEC_ID_H264;
            c->bit_rate = m_avFormat.videoBitRate;
            c->width    = m_avFormat.width;
            c->height   = m_avFormat.height;
            c->framerate = (AVRational){ m_avFormat.frame_rate, 1 };
            c->time_base = (AVRational){ 1, m_avFormat.frame_rate };
            c->ticks_per_frame = 1;
            c->gop_size = 15;
            c->pix_fmt = STREAM_PIX_FMT;
            c->max_b_frames = 0;
            AVDictionary *sw_opt = NULL;
            av_dict_set(&sw_opt, "preset", "superfast", 0);
            av_dict_set(&sw_opt, "tune", "zerolatency", 0);
            ret = avcodec_open2(c, sw_codec, &sw_opt);
            av_dict_free(&sw_opt);
            if (ret < 0) {
                fprintf(stderr, "SW encoder also failed\n");
                return;
            }
            codec = sw_codec;
        } else {
            return;
        }
    }

    // Allocate CPU YUV frame (needed for both HW and SW as sws_scale output target)
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

    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Add an audio output stream. */
void SaveVideoFileThread::add_audio_stream(OutputStream *ost, AVFormatContext *oc,
                                           AVCodec **codec, enum AVCodecID codec_id)
{
    AVCodecContext *c;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
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

    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    c->bit_rate = 64000;
    c->sample_rate = 44100;
    c->channel_layout = AV_CH_LAYOUT_STEREO;
    c->channels = av_get_channel_layout_nb_channels(c->channel_layout);
    ost->st->time_base = { 1, c->sample_rate };

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

void SaveVideoFileThread::open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    c = ost->enc;

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec\n");
        exit(1);
    }

    nb_samples = c->frame_size;
    ost->frame = av_frame_alloc();
    if (!ost->frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }
    ost->frame->format = c->sample_fmt;
    ost->frame->channel_layout = c->channel_layout;
    ost->frame->sample_rate = c->sample_rate;
    ost->frame->nb_samples = nb_samples;

    //申请空间
    mAudioOneFrameSize = av_samples_get_buffer_size(NULL, c->channels,
                                                     c->frame_size, c->sample_fmt, 1);
    fprintf(stderr, "[Encode] AAC: frame_size=%d ch=%d fmt=%d(FLTP=%d) bufSize=%d\n",
            c->frame_size, c->channels, c->sample_fmt, AV_SAMPLE_FMT_FLTP, mAudioOneFrameSize);

    ost->frameBuffer = (uint8_t *)av_malloc(mAudioOneFrameSize);
    ost->frameBufferSize = mAudioOneFrameSize;

    ///这句话必须要(设置这个frame里面的采样点个数)
    int oneChannelBufferSize = mAudioOneFrameSize / c->channels;  //计算出一个声道的数据
    int nb_samplesize = oneChannelBufferSize / av_get_bytes_per_sample(c->sample_fmt);  //计算出采样点个数
    ost->frame->nb_samples = nb_samplesize;

    av_samples_fill_arrays(ost->frame->data, ost->frame->linesize, ost->frameBuffer,
                           c->channels, ost->frame->nb_samples, c->sample_fmt, 0);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

void SaveVideoFileThread::slot_writeAudioFrameData(uint8_t *picture_buf, int buffer_size)
{
    if (!m_isRecording) {
        free(picture_buf);
        return;
    }

    // 如果视频还没开始，丢弃音频帧（拉齐时间）
    if ((m_avFormat.hasCamera || m_avFormat.hasDesk) && m_videoBeginFlag) {
        free(picture_buf);
        return;
    }

    // 麦克风和系统音频共用同一个队列和编码路径
    audioDataQueue_Input(picture_buf, buffer_size);
    free(picture_buf);
}

void SaveVideoFileThread::slot_openAudio()
{
    if (m_avFormat.hasAudio) {
        m_audioRead->setEnableSystemAudio(m_avFormat.hasSysAudio);
        m_audioRead->slot_openAudio();
    }
}

void SaveVideoFileThread::slot_closeAudio()
{
    m_audioRead->slot_closeAudio();
}

void SaveVideoFileThread::slot_setInfo(STRU_AV_FORMAT& avFormat)
{
    m_avFormat = avFormat;

    // 重置 video_st 和 audio_st 结构体（如果之前录制过）
    memset(&video_st, 0, sizeof(video_st));
    memset(&audio_st, 0, sizeof(audio_st));
    have_video = 0;
    have_audio = 0;
    encode_video = 0;
    encode_audio = 0;

    // 清理残留队列数据
    m_videoMutex.lock();
    while (!m_videoDataList.isEmpty()) {
        BufferDataNode* node = m_videoDataList.takeFirst();
        if (node) {
            av_free(node->buffer);
            free(node);
        }
    }
    m_videoMutex.unlock();

    m_audioMutex.lock();
    while (!m_audioDataList.isEmpty()) {
        BufferDataNode* node = m_audioDataList.takeFirst();
        if (node) {
            free(node->buffer);
            free(node);
        }
    }
    m_audioMutex.unlock();

    // 清理 lastVideoNode
    if (lastVideoNode) {
        av_free(lastVideoNode->buffer);
        free(lastVideoNode);
        lastVideoNode = nullptr;
    }

    m_isRecording = false;
    m_startTime = -1;
    isStop = false;
    m_videoBeginFlag = true;
    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
    hw_device_ctx = nullptr;
    use_hw_encode = false;
    oc = nullptr;
    fmt = nullptr;

    // 将阻塞的 FFmpeg 初始化移到 run() 线程中，避免 UI 冻结
    this->start();
}

void SaveVideoFileThread::slot_openVideo()
{
    m_picInPicRead->slot_openVideo();
}

void SaveVideoFileThread::close_stream(AVFormatContext *oc, OutputStream *ost)
{
    (void)oc; // 消除未使用参数警告

    // Audio frameBuffer is separately malloc'd, must be freed manually
    // Must be done BEFORE avcodec_free_context which nullifies ost->enc
    if (ost->frameBuffer && ost->enc && ost->enc->codec_type == AVMEDIA_TYPE_AUDIO) {
        av_free(ost->frameBuffer);
        ost->frameBuffer = nullptr;
    }
    if (ost->enc) {
        avcodec_free_context(&ost->enc);
        ost->enc = nullptr;
    }
    // 注意：对于视频，frameBuffer 是 frame 内部缓冲区的一部分，不需要单独释放
    if (ost->frame) {
        av_frame_free(&ost->frame);
        ost->frame = nullptr;
        ost->frameBuffer = nullptr;
    }
    if (ost->tmp_frame) {
        av_frame_free(&ost->tmp_frame);
        ost->tmp_frame = nullptr;
    }
    if (ost->sws_ctx) {
        sws_freeContext(ost->sws_ctx);
        ost->sws_ctx = nullptr;
    }
    if (ost->swr_ctx) {
        swr_free(&ost->swr_ctx);
        ost->swr_ctx = nullptr;
    }
    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
}

// 线程运行函数 — 异步执行 FFmpeg 初始化 + 编码循环
void SaveVideoFileThread::run()
{
    int ret;
    char filename[260] = "";
    QByteArray pathBytes = m_avFormat.fileName.toUtf8();
    sprintf(filename, "%.259s", pathBytes.constData());

    fprintf(stderr, "[encode thread] Opening output file: %s\n", filename);

    // === FFmpeg 初始化（在线程中执行，不阻塞 UI）===

    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        fprintf(stderr, "Could not deduce output format from file extension: using FLV.\n");
        avformat_alloc_output_context2(&oc, NULL, "flv", filename);
    }
    if (!oc) {
        fprintf(stderr, "Could not allocate output context\n");
        Q_EMIT SIG_EncodeError(QString("无法创建输出上下文"));
        return;
    }

    fmt = oc->oformat;

    // 设置中断回调，允许快速停止
    oc->interrupt_callback.callback = interrupt_cb;
    oc->interrupt_callback.opaque = this;

    if (m_avFormat.hasCamera || m_avFormat.hasDesk) {
        add_Video_stream(&video_st, oc, &video_codec, AV_CODEC_ID_H264);
        have_video = 1;
        encode_video = 1;
    }
    if (m_avFormat.hasAudio) {
        add_audio_stream(&audio_st, oc, &audio_codec, AV_CODEC_ID_AAC);
        have_audio = 1;
        encode_audio = 1;
    }

    if (have_video)
        open_video(oc, video_codec, &video_st);
    if (have_audio)
        open_audio(oc, audio_codec, &audio_st);

    av_dump_format(oc, 0, filename, 1);

    // 打开输出（RTMP 连接在此处，不阻塞 UI）
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE, NULL, NULL);
        if (ret < 0) {
            char errbuf[256] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "avio_open failed: %s (err=%d)\n", errbuf, ret);
            Q_EMIT SIG_EncodeError(QString("Cannot open RTMP URL: %1\nFFmpeg error: %2 (code=%3)")
                .arg(filename).arg(errbuf).arg(ret));
            goto run_fail;
        }
    }

    ret = avformat_write_header(oc, NULL);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "avformat_write_header failed: %s\n", errbuf);
        Q_EMIT SIG_EncodeError(QString("写入文件头失败: %1").arg(errbuf));
        goto run_fail;
    }

    // 初始化完成，通知采集线程开始写入队列
    m_isRecording = true;
    m_startTime = -1;

    // === 编码主循环 ===

    // 等待第一帧到达
    while (m_videoBeginFlag && m_isRecording) {
        msleep(1);
    }

    while (true) {
        // 音视频同步：比较 pts，决定写视频还是音频
        if (have_video &&
            (!have_audio ||
             (av_compare_ts(video_st.next_pts, video_st.enc->time_base,
                           audio_st.next_pts, audio_st.enc->time_base) <= 0))) {
            if (!write_video_frame_from_queue()) {
                msleep(1);
            }
        } else {
            if (!write_audio_frame_from_queue()) {
                // 音频数据未就绪时，输出视频防止画面卡死
                if (have_video && !m_videoDataList.isEmpty()) {
                    write_video_frame_from_queue();
                } else {
                    msleep(1);
                }
            }
        }

        if (isStop) {
            if (m_audioDataList.size() == 0 && m_videoDataList.size() == 0) {
                break;
            }
        }
    }

    // 写入文件尾
    ret = av_write_trailer(oc);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qDebug() << "av_write_trailer failed:" << errbuf;
    }

    // 关闭流
    m_isRecording = false;
    if (m_avFormat.hasCamera || m_avFormat.hasDesk)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);
    if (!(fmt->flags & AVFMT_NOFILE))
        avio_closep(&oc->pb);
    avformat_free_context(oc);
    oc = nullptr;
    return;

run_fail:
    m_isRecording = false;
    if (have_video) close_stream(oc, &video_st);
    if (have_audio) close_stream(oc, &audio_st);
    if (!(fmt->flags & AVFMT_NOFILE) && oc && oc->pb)
        avio_closep(&oc->pb);
    if (oc)
        avformat_free_context(oc);
    oc = nullptr;
}

void SaveVideoFileThread::slot_closeVideo()
{
    // 防止重复关闭
    if (!m_isRecording) {
        fprintf(stderr, "Not recording, nothing to close\n");
        return;
    }
    m_isRecording = false;

    fprintf(stderr, "Closing video, frames written: %lld\n", video_st.next_pts);

    m_picInPicRead->slot_closeVideo();
    m_audioRead->slot_closeAudio();

//优化 原代码保留，注释掉（资源清理已移到 run() 中）
/*
    if (oc) {
        fprintf(stderr, "Writing trailer...\n");
        av_write_trailer(oc);

        if (m_avFormat.hasCamera || m_avFormat.hasDesk)
            close_stream(oc, &video_st);

        if (m_avFormat.hasAudio)
            close_stream(oc, &audio_st);

        if (!(fmt->flags & AVFMT_NOFILE))
            avio_closep(&oc->pb);

        avformat_free_context(oc);
        oc = nullptr;
    }
*/

    //优化 设置停止标志，让写线程退出
    isStop = true;

    // 等待编码线程退出（带超时，防止 RTMP 清理阻塞 UI）
    // av_write_trailer + avio_closep 对 RTMP 可能耗时，但不应超过 3 秒
    if (!this->wait(10000)) {  // 3秒→10秒，给av_write_trailer足够时间
        fprintf(stderr, "Warning: encoder thread did not finish within 10 seconds\n");
        this->terminate();  // 强制终止（最后手段）
    }
}

const STRU_AV_FORMAT &SaveVideoFileThread::avFormat() const
{
    return m_avFormat;
}
