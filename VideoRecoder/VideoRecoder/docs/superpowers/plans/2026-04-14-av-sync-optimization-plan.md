# 音视频同步与稳帧优化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 VideoRecoder 添加音视频队列、同步机制和稳帧处理，解决录制帧率不稳定和音画不同步问题。

**Architecture:** 使用 QList + QMutex 实现线程安全队列，重写 QThread::run() 实现独立写线程，通过 av_compare_ts 比较时间戳实现音视频同步，通过丢弃过早帧和补帧实现稳帧。

**Tech Stack:** Qt 5.12.11, FFmpeg 4.2.2, C++11, MinGW 32-bit

---

## 文件结构

| 文件 | 操作 | 职责 |
|------|------|------|
| `savevideofilethread.h` | 修改 | 添加队列成员变量和方法声明 |
| `savevideofilethread.cpp` | 修改 | 实现队列方法、run()、修改 slot 方法 |
| `picinpic_read.cpp` | 修改 | 启用 SIG_sendVideoFrameData 信号 |

---

## Task 1: 添加头文件和数据结构

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.h`

- [ ] **Step 1: 添加头文件包含**

在 `#include <QDateTime>` 后添加：
```cpp
#include <QList>
#include <QMutex>
```

- [ ] **Step 2: 添加队列节点结构体**

在 `#define STREAM_PIX_FMT AV_PIX_FMT_YUV420P` 后添加：
```cpp
//优化 队列节点结构
struct BufferDataNode {
    uint8_t *buffer;      // 数据指针
    int bufferSize;       // 数据大小
    int64_t time;         // 时间戳（用于稳帧）
};
```

- [ ] **Step 3: 添加成员变量**

在 `private:` 区域，`int mAudioOneFrameSize;` 后添加：
```cpp
    //优化 队列相关成员
    QList<BufferDataNode*> m_videoDataList;    // 视频队列
    QList<BufferDataNode*> m_audioDataList;    // 音频队列
    BufferDataNode* lastVideoNode;             // 上一帧视频（用于补帧）
    QMutex m_videoMutex;                       // 视频队列锁
    QMutex m_audioMutex;                       // 音频队列锁
    bool isStop;                               // 停止标志
    int64_t video_pts;                         // 视频时间戳（毫秒）
    int64_t audio_pts;                         // 音频时间戳（毫秒）
    bool m_videoBeginFlag;                     // 视频开始标志
    qint64 m_videoBeginTime;                   // 视频开始时间
    
    //优化 队列大小限制
    static const int MAX_VIDEO_QUEUE_SIZE = 30;  // 约1秒视频
    static const int MAX_AUDIO_QUEUE_SIZE = 50;  // 约1秒音频
```

- [ ] **Step 4: 添加方法声明**

在 `private:` 区域，`void close_stream(...)` 后添加：
```cpp
    //优化 队列操作方法
    void videoDataQueue_Input(uint8_t *buffer, int size, int64_t time);
    BufferDataNode* videoDataQueue_get(int64_t time);
    void audioDataQueue_Input(const uint8_t *buffer, const int &size);
    BufferDataNode* audioDataQueue_get();
    
    //优化 写线程方法
    bool write_video_frame_from_queue();
    bool write_audio_frame_from_queue();
```

- [ ] **Step 5: 添加 slot 声明**

在 `public slots:` 区域，`void slot_writeVideoFrame(QImage image);` 后添加：
```cpp
    void slot_writeVideoFrameData(uint8_t *buf, int buffer_size);  //优化 视频帧数据写入
```

- [ ] **Step 6: 添加 run() 重写声明**

在 `protected:` 区域添加（如果没有 protected 区域则添加）：
```cpp
protected:
    void run() override;  //优化 重写线程运行函数
```

---

## Task 2: 修改构造函数初始化

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 修改构造函数初始化列表**

找到构造函数 `SaveVideoFileThread::SaveVideoFileThread()`，修改初始化列表：
```cpp
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
    , m_videoBeginTime(0)
    , mAudioOneFrameSize(0)
    //优化 新增成员初始化
    , lastVideoNode(nullptr)
    , isStop(false)
    , video_pts(0)
    , audio_pts(0)
    , m_videoBeginFlag(true)
{
```

---

## Task 3: 实现视频队列方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 实现 videoDataQueue_Input 方法**

在构造函数后添加：
```cpp
//优化 视频队列入队
void SaveVideoFileThread::videoDataQueue_Input(uint8_t *buffer, int size, int64_t time)
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
    
    m_videoMutex.lock();
    m_videoDataList.append(node);
    m_videoMutex.unlock();
}
```

- [ ] **Step 2: 实现 videoDataQueue_get 方法**

添加：
```cpp
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
```

---

## Task 4: 实现音频队列方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 实现 audioDataQueue_Input 方法**

在视频队列方法后添加：
```cpp
//优化 音频队列入队
void SaveVideoFileThread::audioDataQueue_Input(const uint8_t *buffer, const int &size)
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
```

- [ ] **Step 2: 实现 audioDataQueue_get 方法**

添加：
```cpp
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
```

---

## Task 5: 实现写线程方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 实现 write_video_frame_from_queue 方法**

在队列方法后添加：
```cpp
//优化 从队列写视频帧
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
    
    // 复制数据到帧缓冲区
    memcpy(video_st.frameBuffer, node->buffer, node->bufferSize);
    
    // 设置 pts
    video_st.frame->pts = video_st.next_pts++;
    
    // 编码并写入
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    
    int got_packet = 0;
    int ret = avcodec_encode_video2(video_st.enc, &pkt, video_st.frame, &got_packet);
    
    if (ret < 0) {
        fprintf(stderr, "Error encoding video frame\n");
        return false;
    }
    
    if (got_packet) {
        // 更新 video_pts（毫秒）
        AVRational tb = {1, 1000};
        video_pts = av_rescale_q(video_st.next_pts, video_st.st->time_base, tb);
        
        ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing video frame\n");
        }
    }
    
    return true;
}
```

- [ ] **Step 2: 实现 write_audio_frame_from_queue 方法**

添加：
```cpp
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
            fprintf(stderr, "Error while writing audio frame\n");
        }
    }
    
    return true;
}
```

---

## Task 6: 实现 run() 方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 实现 run() 方法**

在写线程方法后添加：
```cpp
//优化 线程运行函数
void SaveVideoFileThread::run()
{
    while (true) {
        // 音视频同步：比较 pts，决定写视频还是音频
        if (have_video && 
            (!have_audio || 
             (av_compare_ts(video_st.next_pts, video_st.enc->time_base,
                           audio_st.next_pts, audio_st.enc->time_base) <= 0))) {
            // 写视频帧
            if (!write_video_frame_from_queue()) {
                msleep(1);  // 队列为空，短暂休眠
            }
        } else {
            // 写音频帧
            if (!write_audio_frame_from_queue()) {
                msleep(1);  // 队列为空，短暂休眠
            }
        }
        
        // 停止处理
        if (isStop) {
            // 等待队列清空
            if (m_audioDataList.size() == 0 && m_videoDataList.size() == 0) {
                break;
            }
        }
    }
    
    // 写入文件尾
    av_write_trailer(oc);
    
    // 关闭流
    if (m_avFormat.hasCamera || m_avFormat.hasDesk)
        close_stream(oc, &video_st);
    if (m_avFormat.hasAudio)
        close_stream(oc, &audio_st);
    
    // 关闭文件
    if (!(fmt->flags & AVFMT_NOFILE))
        avio_closep(&oc->pb);
    
    // 释放上下文
    avformat_free_context(oc);
    oc = nullptr;
}
```

---

## Task 7: 修改 slot_writeVideoFrame 方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 注释原有 slot_writeVideoFrame 实现**

找到 `void SaveVideoFileThread::slot_writeVideoFrame(QImage image)` 方法，将原有代码注释掉：
```cpp
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
```

---

## Task 8: 实现 slot_writeVideoFrameData 方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 实现 slot_writeVideoFrameData 方法**

在 `slot_writeVideoFrame` 方法后添加：
```cpp
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
```

---

## Task 9: 修改 slot_writeAudioFrameData 方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 注释原有实现并添加队列写入**

找到 `void SaveVideoFileThread::slot_writeAudioFrameData(uint8_t *picture_buf, int buffer_size)` 方法，修改为：
```cpp
void SaveVideoFileThread::slot_writeAudioFrameData(uint8_t *picture_buf, int buffer_size)
{
    if (!m_isRecording || !have_audio) {
        free(picture_buf);
        return;
    }

//优化 原代码保留，注释掉
/*
    AVCodecContext *c = audio_st.enc;

    memcpy(audio_st.frameBuffer, picture_buf, buffer_size);
    free(picture_buf);

    // 计算音频PTS
    AVRational rational;
    rational.num = 1;
    rational.den = c->sample_rate;
    audio_st.frame->pts = av_rescale_q(audio_st.samples_count, rational, c->time_base);
    audio_st.samples_count += audio_st.frame->nb_samples;

    // 编码音频帧
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    int got_packet = 0;
    int ret = avcodec_encode_audio2(c, &pkt, audio_st.frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding audio frame\n");
        return;
    }

    if (got_packet) {
        ret = write_frame(oc, &c->time_base, audio_st.st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing audio frame\n");
        }
    }
*/

    //优化 如果视频还没开始，丢弃音频帧（拉齐时间）
    if ((m_avFormat.hasCamera || m_avFormat.hasDesk) && m_videoBeginFlag) {
        free(picture_buf);
        return;
    }
    
    //优化 写入队列
    audioDataQueue_Input(picture_buf, buffer_size);
    free(picture_buf);
}
```

---

## Task 10: 修改 slot_setInfo 方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 在 slot_setInfo 末尾添加线程启动**

找到 `void SaveVideoFileThread::slot_setInfo(STRU_AV_FORMAT& avFormat)` 方法，在 `m_isRecording = true;` 后添加：
```cpp
    m_isRecording = true;
    m_startTime = -1;  // 重置开始时间戳，将在第一帧时设置
    
    //优化 启动写线程
    this->start();
```

---

## Task 11: 修改 slot_closeVideo 方法

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

- [ ] **Step 1: 修改 slot_closeVideo 方法**

找到 `void SaveVideoFileThread::slot_closeVideo()` 方法，修改为：
```cpp
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
    
    //优化 等待线程结束
    this->wait();
}
```

---

## Task 12: 启用 picinpic_read.cpp 中的信号

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\picinpic_read.cpp`

- [ ] **Step 1: 修改信号发送**

找到 `void PicInPic_Read::slot_getOneFrame()` 方法中的信号发送部分，修改为：
```cpp
    //计算视频帧
    long long time = 0;
    uint8_t * picture_buf = NULL;
    int buffer_size = ImageToYuvBuffer( image , &picture_buf );

//优化 原代码注释掉
//    Q_EMIT SIG_sendVideoFrame( image );

//优化 启用原始数据传递
    Q_EMIT SIG_sendVideoFrameData( picture_buf, buffer_size );
```

---

## Task 13: 添加析构函数清理

**Files:**
- Modify: `d:\VideoRecoder\VideoRecoder\savevideofilethread.h`

- [ ] **Step 1: 添加析构函数声明**

在 `public:` 区域，构造函数后添加：
```cpp
    explicit SaveVideoFileThread();
    ~SaveVideoFileThread();  //优化 添加析构函数
```

- [ ] **Step 2: 实现析构函数**

在 `savevideofilethread.cpp` 构造函数后添加：
```cpp
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
    
    // 清理 lastVideoNode
    if (lastVideoNode) {
        av_free(lastVideoNode->buffer);
        free(lastVideoNode);
        lastVideoNode = nullptr;
    }
}
```

---

## Task 14: 编译测试

**Files:**
- None (编译验证)

- [ ] **Step 1: 执行 qmake**

在 Qt Creator 中：
1. 右键点击项目
2. 选择 "执行 qmake"

- [ ] **Step 2: 构建项目**

在 Qt Creator 中：
1. 点击 "构建" → "构建项目 VideoRecoder"
2. 检查是否有编译错误

- [ ] **Step 3: 运行程序**

在 Qt Creator 中：
1. 点击 "运行"
2. 测试录制功能是否正常

---

## 验证检查清单

- [ ] 编译无错误
- [ ] 程序能正常启动
- [ ] 录制功能正常
- [ ] 录制的视频播放流畅
- [ ] 音画同步
- [ ] 停止录制后文件正常关闭
- [ ] 无内存泄漏

---

## 回退方案

如果出现问题，按以下步骤回退：
1. 取消所有 `//优化` 标记的代码注释
2. 注释掉所有新增的 `//优化` 代码
3. 恢复 `picinpic_read.cpp` 中的 `SIG_sendVideoFrame` 信号
