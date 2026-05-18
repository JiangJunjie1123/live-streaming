# 音视频同步与稳帧优化设计文档

## 1. 概述

### 1.1 目标
为 VideoRecoder 项目添加音视频队列、同步机制和稳帧处理，解决以下问题：
- 录制帧率不稳定，播放时跳帧
- 音画不同步
- 编码阻塞导致数据丢失

### 1.2 实施方案
采用渐进式重构（方案B），分4个阶段实施：
1. 添加队列数据结构和入队方法
2. 实现写线程 run()，从队列取数据编码
3. 添加音视频同步逻辑
4. 添加稳帧处理

### 1.3 约束
- 被优化的代码注释保留，标注 `//优化`
- 使用 QList + QMutex 实现队列
- 队列大小限制：视频30帧，音频50帧
- 保持现有 FFmpeg API（avcodec_encode_video2/audio2）

## 2. 数据结构设计

### 2.1 队列节点结构
```cpp
struct BufferDataNode {
    uint8_t *buffer;      // 数据指针
    int bufferSize;       // 数据大小
    int64_t time;         // 时间戳（用于稳帧）
};
```

### 2.2 新增成员变量
```cpp
// 队列
QList<BufferDataNode*> m_videoDataList;    // 视频队列
QList<BufferDataNode*> m_audioDataList;    // 音频队列
BufferDataNode* lastVideoNode = nullptr;   // 上一帧视频（用于补帧）

// 线程安全
QMutex m_videoMutex;                       // 视频队列锁
QMutex m_audioMutex;                       // 音频队列锁

// 状态标志
bool isStop = false;                       // 停止标志
bool m_videoBeginFlag = false;             // 视频开始标志

// 时间戳
int64_t video_pts = 0;                     // 视频时间戳（毫秒）
int64_t audio_pts = 0;                     // 音频时间戳（毫秒）
qint64 m_videoBeginTime = 0;               // 视频开始时间

// 队列大小限制
static const int MAX_VIDEO_QUEUE_SIZE = 30;  // 约1秒视频
static const int MAX_AUDIO_QUEUE_SIZE = 50;  // 约1秒音频
```

### 2.3 新增方法声明
```cpp
// 队列操作
void videoDataQueue_Input(uint8_t *buffer, int size, int64_t time);
BufferDataNode* videoDataQueue_get(int64_t time);
void audioDataQueue_Input(const uint8_t *buffer, const int &size);
BufferDataNode* audioDataQueue_get();

// 写线程方法
bool write_video_frame_from_queue();
bool write_audio_frame_from_queue();

// 重写 run()
void run() override;
```

## 3. 队列方法设计

### 3.1 视频队列入队
```cpp
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

### 3.2 视频队列出队（带稳帧）
```cpp
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

### 3.3 音频队列入队
```cpp
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

### 3.4 音频队列出队
```cpp
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

## 4. 写线程设计

### 4.1 run() 方法
```cpp
void SaveVideoFileThread::run()
{
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
                msleep(1);
            }
        }
        
        // 停止处理
        if (isStop) {
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

### 4.2 write_video_frame_from_queue()
```cpp
bool SaveVideoFileThread::write_video_frame_from_queue()
{
    // 计算下一帧的时间戳（毫秒）
    int64_t next_frame_time = (video_st.next_pts * 1000) / m_avFormat.frame_rate;
    
    // 从队列获取帧（带稳帧处理）
    BufferDataNode *node = videoDataQueue_get(next_frame_time);
    
    if (node == nullptr) {
        if (lastVideoNode) {
            node = lastVideoNode;
        } else {
            return false;
        }
    } else {
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
        video_pts = av_rescale_q(video_st.next_pts, video_st.st->time_base, {1, 1000});
        ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing video frame\n");
        }
    }
    
    return true;
}
```

### 4.3 write_audio_frame_from_queue()
```cpp
bool SaveVideoFileThread::write_audio_frame_from_queue()
{
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
        audio_pts = av_rescale_q(audio_st.next_pts, audio_st.st->time_base, {1, 1000});
        audio_st.next_pts += audio_st.frame->nb_samples;
        
        ret = write_frame(oc, &audio_st.enc->time_base, audio_st.st, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing audio frame\n");
        }
    }
    
    return true;
}
```

## 5. Slot 方法修改

### 5.1 picinpic_read.cpp 修改
取消注释 `SIG_sendVideoFrameData` 信号发送：
```cpp
//优化 原代码注释掉，现在需要启用
//    Q_EMIT SIG_sendVideoFrame( image );

//优化 启用原始数据传递
Q_EMIT SIG_sendVideoFrameData( picture_buf, buffer_size );
```

### 5.2 savevideofilethread.h 添加声明
```cpp
public slots:
    void slot_writeVideoFrame(QImage image);
    void slot_writeVideoFrameData(uint8_t *buf, int buffer_size);  //优化 新增
    void slot_writeAudioFrameData(uint8_t *buf, int buffer_size);
    // ... 其他 slots
```

### 5.3 slot_writeVideoFrameData() 实现
```cpp
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
    
    //优化 写入队列而非直接编码
    videoDataQueue_Input(picture_buf, buffer_size, pts);
}
```

### 5.4 slot_writeVideoFrame() 修改（注释保留）
```cpp
void SaveVideoFileThread::slot_writeVideoFrame(QImage image)
{
//优化 原代码保留，注释掉
/*
    if (!m_isRecording) {
        return;
    }
    // ... 原有编码逻辑
*/
    //优化 现在由 slot_writeVideoFrameData 处理
    Q_UNUSED(image);
}
```

### 5.5 slot_writeAudioFrameData()
```cpp
void SaveVideoFileThread::slot_writeAudioFrameData(uint8_t *picture_buf, int buffer_size)
{
    if (!m_isRecording || !have_audio) {
        free(picture_buf);
        return;
    }
    
    // 如果视频还没开始，丢弃音频帧（拉齐时间）
    if ((m_avFormat.hasCamera || m_avFormat.hasDesk) && m_videoBeginFlag) {
        free(picture_buf);
        return;
    }
    
    // 写入队列
    audioDataQueue_Input(picture_buf, buffer_size);
    free(picture_buf);
}
```

## 6. 其他修改

### 6.1 构造函数初始化
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
    , lastVideoNode(nullptr)
    , isStop(false)
    , video_pts(0)
    , audio_pts(0)
    , m_videoBeginFlag(true)
{
    // ... existing code
}
```

### 6.2 析构函数清理
```cpp
~SaveVideoFileThread() {
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

### 6.3 slot_setInfo() 修改
在 `slot_setInfo()` 末尾添加：
```cpp
// 启动写线程
this->start();
```

### 6.4 slot_closeVideo() 修改
```cpp
void SaveVideoFileThread::slot_closeVideo()
{
    if (!m_isRecording) {
        fprintf(stderr, "Not recording, nothing to close\n");
        return;
    }
    m_isRecording = false;
    
    m_picInPicRead->slot_closeVideo();
    m_audioRead->slot_closeAudio();
    
    // 设置停止标志，让写线程退出
    isStop = true;
    
    // 等待线程结束
    this->wait();
    
    // 注意：资源清理已移到 run() 中
}
```

## 7. 需要添加的头文件

```cpp
#include <QList>
#include <QMutex>
```

## 8. 验证检查清单

- [ ] 队列入队/出队线程安全
- [ ] 队列大小限制生效
- [ ] 音视频同步逻辑正确
- [ ] 稳帧处理正确（丢弃过早帧，补帧过晚帧）
- [ ] 停止时队列清空
- [ ] 内存无泄漏
- [ ] 析构函数清理所有资源
- [ ] 被优化的代码已注释保留
- [ ] 优化代码标注 `//优化`

## 9. 回退方案

如果优化后出现问题，可以：
1. 取消注释被优化的代码
2. 注释掉新增的优化代码
3. 恢复原有的 slot 方法实现
