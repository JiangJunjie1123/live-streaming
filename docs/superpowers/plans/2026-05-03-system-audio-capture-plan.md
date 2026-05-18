# 系统音频捕获 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**目标:** 在 VideoRecoder 推流中增加 Windows 系统音频（扬声器 Loopback）采集，与现有麦克风采集在 PCM 层面混音后统一编码为 AAC 推流。

**架构:** 新增 `SystemAudioRead` 类通过 WASAPI Loopback 捕获系统音频，输出 FLTP 格式与麦克风完全一致。`SaveVideoFileThread` 新增系统音频队列和混音逻辑，两路 float 数据逐采样点加法 + tanh 限幅后送入同一个 AAC 编码器。

**技术栈:** C++11, Qt 5.12, FFmpeg 4.2.2, Windows WASAPI (mmdeviceapi.h / audioclient.h)

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `systemaudioread.h` **(新增)** | SystemAudioRead 类声明 — WASAPI COM 接口持有、swr context、定时器 |
| `systemaudioread.cpp` **(新增)** | WASAPI Loopback 初始化、定时捕获 Float → swr → FLTP → emit |
| `savevideofilethread.h` **(修改)** | STRU_AV_FORMAT 加 hasSystemAudio；类加 m_sysAudioRead、队列、hasSysAudio |
| `savevideofilethread.cpp` **(修改)** | 构造函数、slot_setInfo、slot_openAudio/closeAudio、系统音频队列、混音编码逻辑 |
| `recorderdialog.ui` **(修改)** | cb_audio 拆分为 cb_mic + cb_system_audio |
| `recorderdialog.cpp` **(修改)** | on_pb_start_clicked 传递 hasSystemAudio |
| `VideoRecoder.pro` **(修改)** | 新增源文件 + ole32.lib |

---

### Task 1: 创建 systemaudioread.h

**文件:** 创建 `D:/Video/VideoRecoder/VideoRecoder/systemaudioread.h`

- [ ] **Step 1: 编写 systemaudioread.h 完整代码**

```cpp
#ifndef SYSTEMAUDIOREAD_H
#define SYSTEMAUDIOREAD_H

#include <QObject>
#include <QTimer>
#include <QMessageBox>
#include <vector>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

extern "C" {
#include "libswresample/swresample.h"
}

// 与 Audio_Read 保持一致：1024 采样点 × 4 字节/float = 4096 字节/声道
#define SYS_OneAudioSize   (4096)
#define SYS_SampleRate      (44100)
#define SYS_AUDIO_INTERVAL  (10)    // WASAPI buffer 约 10ms，比麦克风 20ms 更短

class SystemAudioRead : public QObject
{
    Q_OBJECT

signals:
    // 与 Audio_Read::SIG_sendAudioFrameData 签名完全一致
    void SIG_sendAudioFrameData(uint8_t* buf, int buffer_size);

public:
    explicit SystemAudioRead(QObject *parent = nullptr);
    ~SystemAudioRead();

public slots:
    void slot_openAudio();   // 初始化 WASAPI Loopback，启动定时器
    void slot_closeAudio();  // 停止捕获
    void UnInit();           // 回收 COM + timer + swr 资源

private slots:
    void slot_readMore();    // 定时读取 WASAPI buffer → swr → FLTP → emit

private:
    bool initWASAPI();       // WASAPI Loopback 初始化，失败返回 false
    void releaseWASAPI();    // 释放所有 COM 接口

    QTimer *m_timer;

    // WASAPI COM 接口
    IMMDeviceEnumerator *m_pEnumerator;
    IMMDevice *m_pDevice;
    IAudioClient *m_pAudioClient;
    IAudioCaptureClient *m_pCaptureClient;
    UINT32 m_bufferFrameCount;   // WASAPI 每次回调帧数

    // 音频重采样
    SwrContext *m_swr;
    int m_nbSamples;             // 每帧采样点数 (1024)

    enum State { state_stop, state_play, state_pause };
    State m_state;
};

#endif // SYSTEMAUDIOREAD_H
```

- [ ] **Step 2: 验证文件已创建**

```
确认 D:/Video/VideoRecoder/VideoRecoder/systemaudioread.h 文件存在且内容正确
```

---

### Task 2: 创建 systemaudioread.cpp

**文件:** 创建 `D:/Video/VideoRecoder/VideoRecoder/systemaudioread.cpp`

- [ ] **Step 1: 编写 systemaudioread.cpp 构造函数和析构函数**

```cpp
#include "systemaudioread.h"
#include <QDebug>
#include <cstring>
#include <cmath>
#include <objbase.h>

// 辅助宏：安全释放 COM 接口
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while(0)

SystemAudioRead::SystemAudioRead(QObject *parent)
    : QObject(parent)
    , m_timer(nullptr)
    , m_pEnumerator(nullptr)
    , m_pDevice(nullptr)
    , m_pAudioClient(nullptr)
    , m_pCaptureClient(nullptr)
    , m_bufferFrameCount(0)
    , m_swr(nullptr)
    , m_nbSamples(1024)
    , m_state(state_stop)
{
    // COM 初始化（每线程需调用一次）
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("COM 初始化失败，系统音频不可用"));
    }

    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(slot_readMore()));
}

SystemAudioRead::~SystemAudioRead()
{
    UnInit();
}

void SystemAudioRead::UnInit()
{
    if (m_timer) {
        m_timer->stop();
    }
    releaseWASAPI();
    if (m_swr) {
        swr_free(&m_swr);
        m_swr = nullptr;
    }
}
```

- [ ] **Step 2: 编写 initWASAPI() — WASAPI Loopback 初始化**

```cpp
bool SystemAudioRead::initWASAPI()
{
    HRESULT hr;

    // 1. 创建设备枚举器
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("无法创建音频设备枚举器，系统音频不可用"));
        return false;
    }

    // 2. 获取默认扬声器（渲染端点）
    hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("未找到扬声器设备，系统音频不可用"));
        return false;
    }

    // 3. 激活 IAudioClient
    hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                             NULL, (void**)&m_pAudioClient);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("激活音频客户端失败"));
        return false;
    }

    // 4. 获取混音格式
    WAVEFORMATEX *pwfx = NULL;
    hr = m_pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("获取混音格式失败"));
        return false;
    }

    // 5. 初始化音频客户端（Loopback 模式）
    // 使用 10ms 的 buffer duration
    REFERENCE_TIME hnsRequestedDuration = 100000; // 10ms = 100000 百纳秒
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        hnsRequestedDuration,
        0,
        pwfx,
        NULL);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("初始化音频客户端失败，请检查音频设备"));
        return false;
    }

    // 6. 获取 buffer 大小
    hr = m_pAudioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        return false;
    }

    // 7. 获取捕获客户端
    hr = m_pAudioClient->GetService(__uuidof(IAudioCaptureClient),
                                    (void**)&m_pCaptureClient);
    if (FAILED(hr)) {
        CoTaskMemFree(pwfx);
        return false;
    }

    // 8. 配置 swr_ctx: Float interleaved (48000) → FLTP planar (44100)
    m_swr = swr_alloc_set_opts(
        nullptr,
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, SYS_SampleRate,    // 输出
        AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT,  pwfx->nSamplesPerSec, // 输入
        0, nullptr);
    if (!m_swr || swr_init(m_swr) < 0) {
        CoTaskMemFree(pwfx);
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("初始化音频转换器失败"));
        return false;
    }

    CoTaskMemFree(pwfx);
    return true;
}

void SystemAudioRead::releaseWASAPI()
{
    SAFE_RELEASE(m_pCaptureClient);
    SAFE_RELEASE(m_pAudioClient);
    SAFE_RELEASE(m_pDevice);
    SAFE_RELEASE(m_pEnumerator);
}
```

- [ ] **Step 3: 编写 slot_openAudio() 和 slot_closeAudio()**

```cpp
void SystemAudioRead::slot_openAudio()
{
    if (m_state == state_play) {
        // 已经在播放，先停止再重启
        m_timer->stop();
        if (m_pAudioClient) {
            m_pAudioClient->Stop();
        }
        releaseWASAPI();
    }

    m_state = state_stop;

    if (!initWASAPI()) {
        // 初始化失败，不启动采集（initWASAPI 已弹窗提示）
        return;
    }

    // 启动 WASAPI 捕获
    HRESULT hr = m_pAudioClient->Start();
    if (FAILED(hr)) {
        QMessageBox::information(NULL, QString::fromUtf8("提示"),
                                 QString::fromUtf8("启动音频捕获失败"));
        return;
    }

    m_state = state_play;
    m_timer->start(SYS_AUDIO_INTERVAL);
}

void SystemAudioRead::slot_closeAudio()
{
    if (m_timer) {
        m_timer->stop();
    }
    if (m_pAudioClient && m_state == state_play) {
        m_pAudioClient->Stop();
    }
    m_state = state_pause;
}
```

- [ ] **Step 4: 编写 slot_readMore() — 核心采集和转换逻辑**

```cpp
void SystemAudioRead::slot_readMore()
{
    if (m_state != state_play || !m_pCaptureClient)
        return;

    UINT32 packetLength = 0;
    HRESULT hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
    if (FAILED(hr))
        return;

    while (packetLength > 0) {
        BYTE *pData = NULL;
        UINT32 numFrames = 0;
        DWORD flags = 0;

        hr = m_pCaptureClient->GetBuffer(&pData, &numFrames, &flags, NULL, NULL);
        if (FAILED(hr))
            break;

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            // 静音帧：填充零数据
            // 每帧 FLTP 数据量 = 1024 × 2 声道 × 4 字节 = 8192
            uint8_t *buffer = (uint8_t *)malloc(SYS_OneAudioSize * 2);
            memset(buffer, 0, SYS_OneAudioSize * 2);
            Q_EMIT SIG_sendAudioFrameData(buffer, SYS_OneAudioSize * 2);
        } else {
            // 有音频数据：swr_convert Float interleaved → FLTP planar
            // 输入 buffer 布局: [L0,R0,L1,R1,...] × numFrames float samples
            const uint8_t *in_data = (const uint8_t *)pData;

            // 输出 buffer: [L0..L1023 | R0..R1023] planar float
            uint8_t *out_buffer[2];
            out_buffer[0] = (uint8_t *)malloc(SYS_OneAudioSize);
            out_buffer[1] = (uint8_t *)malloc(SYS_OneAudioSize);
            memset(out_buffer[0], 0, SYS_OneAudioSize);
            memset(out_buffer[1], 0, SYS_OneAudioSize);

            int converted = swr_convert(m_swr,
                                        out_buffer, SYS_OneAudioSize / 4,  // 输出采样数
                                        &in_data,  numFrames);              // 输入采样数

            if (converted > 0) {
                // 拼接为 planar FLTP: L 声道在前，R 声道在后
                uint8_t *buffer = (uint8_t *)malloc(SYS_OneAudioSize * 2);
                memcpy(buffer, out_buffer[0], SYS_OneAudioSize);               // 左声道
                memcpy(buffer + SYS_OneAudioSize, out_buffer[1], SYS_OneAudioSize); // 右声道

                Q_EMIT SIG_sendAudioFrameData(buffer, SYS_OneAudioSize * 2);
            } else {
                free(out_buffer[0]);
                free(out_buffer[1]);
            }
        }

        m_pCaptureClient->ReleaseBuffer(numFrames);

        hr = m_pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr))
            break;
    }
}
```

- [ ] **Step 5: 验证文件内容**

```
确认 D:/Video/VideoRecoder/VideoRecoder/systemaudioread.cpp 文件存在且各方法完整
```

---

### Task 3: 修改 savevideofilethread.h — STRU_AV_FORMAT 和类成员

**文件:** 修改 `D:/Video/VideoRecoder/VideoRecoder/savevideofilethread.h`

- [ ] **Step 1: STRU_AV_FORMAT 新增 hasSystemAudio 字段**

定位到第 46-54 行的 `STRU_AV_FORMAT` 结构体，在 `hasAudio` 后新增一行：

```cpp
typedef struct STRU_AV_FORMAT {
    QString fileName;
    int width;
    int height;
    int frame_rate;
    int videoBitRate;
    bool hasCamera;
    bool hasDesk;
    bool hasAudio;
    bool hasSystemAudio;   // 是否录制系统声音（新增）
} STRU_AV_FORMAT;
```

- [ ] **Step 2: 前向声明 SystemAudioRead**

定位到第 56 行 `class Audio_Read;`，其后新增：

```cpp
class SystemAudioRead;  // 前向声明（新增）
```

- [ ] **Step 3: 类成员新增 — m_sysAudioRead 指针和 hasSysAudio 标志**

定位到第 104-105 行（`m_audioRead` 和注释），其后新增：

```cpp
    SystemAudioRead *m_sysAudioRead;  // 系统音频采集对象（新增）
```

定位到第 114 行 `int have_audio;`，其后新增：

```cpp
    int have_sys_audio;  // 是否有系统音频（新增）
```

定位到第 116 行 `int encode_audio;`，其后新增：

```cpp
    int encode_sys_audio;  // 是否编码系统音频（新增）
```

- [ ] **Step 4: 类成员新增 — 系统音频队列和互斥锁**

定位到第 123-124 行（`m_audioDataList` 和 `m_audioMutex`），其后新增：

```cpp
    QList<BufferDataNode*> m_sysAudioDataList;    // 系统音频队列（新增）
    QMutex m_sysAudioMutex;                       // 系统音频队列锁（新增）
```

定位到第 136 行 `MAX_AUDIO_QUEUE_SIZE = 30`，其后新增：

```cpp
    static const int MAX_SYS_AUDIO_QUEUE_SIZE = 30;  // 系统音频队列上限（新增）
```

- [ ] **Step 5: 新增系统音频队列方法和混音方法声明**

定位到第 95-96 行（`audioDataQueue_Input` 和 `audioDataQueue_get`），其后新增：

```cpp
    void sysAudioDataQueue_Input(const uint8_t *buffer, int size);  // 系统音频入队（新增）
    BufferDataNode* sysAudioDataQueue_get();                        // 系统音频出队（新增）
```

定位到第 100 行（`write_audio_frame_from_queue`），其后新增：

```cpp
    bool write_mixed_audio_frame();  // 混音并编码音频帧（新增，替代原 write_audio_frame_from_queue）
```

- [ ] **Step 6: 新增 slot_writeSysAudioFrameData 槽函数声明**

定位到第 75 行 `slot_writeAudioFrameData`，其后新增：

```cpp
    void slot_writeSysAudioFrameData(uint8_t *buf, int buffer_size);  // 系统音频帧写入（新增）
```

- [ ] **Step 7: 验证头文件**

```
确认 savevideofilethread.h 修改无语法错误，所有新增成员有合理默认值路径
```

---

### Task 4: 修改 savevideofilethread.cpp — 完整实现

**文件:** 修改 `D:/Video/VideoRecoder/VideoRecoder/savevideofilethread.cpp`

- [ ] **Step 1: 包含 systemaudioread.h 头文件**

在第 2 行 `#include "audio_read.h"` 后新增：

```cpp
#include "systemaudioread.h"
```

在第 3 行 `#include <cstring>` 后新增：

```cpp
#include <cmath>  // for tanhf（新增）
```

- [ ] **Step 2: 构造函数 — 初始化系统音频成员和信号连接**

构造函数初始化列表（第 10-28 行），在 `m_audioRead` 相关初始化后新增成员：

```cpp
SaveVideoFileThread::SaveVideoFileThread()
    : oc(nullptr)
    , fmt(nullptr)
    , video_codec(nullptr)
    , audio_codec(nullptr)
    , have_video(0)
    , have_audio(0)
    , have_sys_audio(0)          // 新增
    , encode_video(0)
    , encode_audio(0)
    , encode_sys_audio(0)        // 新增
    , m_isRecording(false)
    , m_startTime(-1)
    , mAudioOneFrameSize(0)
    , lastVideoNode(nullptr)
    , isStop(false)
    , video_pts(0)
    , audio_pts(0)
    , m_videoBeginFlag(true)
    , m_videoBeginTime(0)
```

构造函数体（第 29-43 行），在 `m_audioRead` 和其 connect 之后新增：

```cpp
    m_picInPicRead = new PicInPic_Read;
    m_audioRead = new Audio_Read;
    m_sysAudioRead = new SystemAudioRead;  // 新增

    connect(m_picInPicRead,SIGNAL(SIG_sendPicInPic(QImage)),this,SIGNAL(SIG_sendPicInPic(QImage)));
    connect(m_picInPicRead,SIGNAL(SIG_sendVideoFrame(QImage)),this,SIGNAL(SIG_sendVideoFrame(QImage)));
    connect(m_picInPicRead,SIGNAL(SIG_sendVideoFrameData(uint8_t*,int)),this,SLOT(slot_writeVideoFrameData(uint8_t*,int)));

    connect(m_audioRead, SIGNAL(SIG_sendAudioFrameData(uint8_t*,int)),
            this, SLOT(slot_writeAudioFrameData(uint8_t*,int)));

    // 新增：连接系统音频信号
    connect(m_sysAudioRead, SIGNAL(SIG_sendAudioFrameData(uint8_t*,int)),
            this, SLOT(slot_writeSysAudioFrameData(uint8_t*,int)));

    memset(&video_st, 0, sizeof(video_st));
    memset(&audio_st, 0, sizeof(audio_st));
```

- [ ] **Step 3: 析构函数 — 清理系统音频队列**

在析构函数（第 48-78 行）中，麦克风音频队列清理后追加系统音频队列清理：

```cpp
    // 清理系统音频队列（新增）
    m_sysAudioMutex.lock();
    while (!m_sysAudioDataList.isEmpty()) {
        BufferDataNode* node = m_sysAudioDataList.takeFirst();
        if (node) {
            free(node->buffer);
            free(node);
        }
    }
    m_sysAudioMutex.unlock();

    // 清理 m_sysAudioRead（新增）
    delete m_sysAudioRead;
```

- [ ] **Step 4: 实现系统音频队列入队方法**

在 `audioDataQueue_Input` 方法（第 136-158 行）之后新增：

```cpp
// 系统音频队列入队（新增）
void SaveVideoFileThread::sysAudioDataQueue_Input(const uint8_t *buffer, int size)
{
    m_sysAudioMutex.lock();
    if (m_sysAudioDataList.size() >= MAX_SYS_AUDIO_QUEUE_SIZE) {
        BufferDataNode* oldNode = m_sysAudioDataList.takeFirst();
        if (oldNode) {
            free(oldNode->buffer);
            free(oldNode);
        }
    }
    m_sysAudioMutex.unlock();

    BufferDataNode *node = (BufferDataNode*)malloc(sizeof(BufferDataNode));
    node->bufferSize = size;
    node->buffer = (uint8_t*)malloc(size);
    memcpy(node->buffer, buffer, size);

    m_sysAudioMutex.lock();
    m_sysAudioDataList.append(node);
    m_sysAudioMutex.unlock();
}

// 系统音频队列出队（新增）
BufferDataNode* SaveVideoFileThread::sysAudioDataQueue_get()
{
    BufferDataNode *node = nullptr;
    m_sysAudioMutex.lock();
    if (m_sysAudioDataList.size() != 0) {
        node = m_sysAudioDataList.takeFirst();
    }
    m_sysAudioMutex.unlock();
    return node;
}
```

- [ ] **Step 5: 实现 slot_writeSysAudioFrameData**

在 `slot_writeAudioFrameData` 方法（第 675-724 行）之后新增：

```cpp
// 系统音频帧写入队列（新增）
void SaveVideoFileThread::slot_writeSysAudioFrameData(uint8_t *picture_buf, int buffer_size)
{
    if (!m_isRecording || !have_sys_audio) {
        free(picture_buf);
        return;
    }

    // 如果视频还没开始，丢弃音频帧（拉齐时间）
    if ((m_avFormat.hasCamera || m_avFormat.hasDesk) && m_videoBeginFlag) {
        free(picture_buf);
        return;
    }

    sysAudioDataQueue_Input(picture_buf, buffer_size);
    free(picture_buf);
}
```

- [ ] **Step 6: 实现 write_mixed_audio_frame() — 混音编码核心逻辑**

替换原有的 `write_audio_frame_from_queue()`（第 273-323 行），新增 `write_mixed_audio_frame()`：

```cpp
// 混音并编码音频帧：从麦克风和系统音频队列取数据，混音后编码（新增）
bool SaveVideoFileThread::write_mixed_audio_frame()
{
    BufferDataNode *micNode = nullptr;
    BufferDataNode *sysNode = nullptr;

    // 从各队列取数据
    if (have_audio) {
        micNode = audioDataQueue_get();
    }
    if (have_sys_audio) {
        sysNode = sysAudioDataQueue_get();
    }

    // 两路都没有数据
    if (!micNode && !sysNode) {
        return false;
    }

    // 一路有数据、另一路无：直接复制到 frameBuffer
    if (micNode && !sysNode) {
        memcpy(audio_st.frameBuffer, micNode->buffer, micNode->bufferSize);
        free(micNode->buffer);
        free(micNode);
    } else if (!micNode && sysNode) {
        memcpy(audio_st.frameBuffer, sysNode->buffer, sysNode->bufferSize);
        free(sysNode->buffer);
        free(sysNode);
    } else {
        // 两路都有数据：FLTP planar float 逐采样点混音 + tanh 软限幅
        int nbSamples = audio_st.frame->nb_samples;  // 1024
        float *micL = (float *)(micNode->buffer);
        float *micR = (float *)(micNode->buffer + SYS_OneAudioSize);
        float *sysL = (float *)(sysNode->buffer);
        float *sysR = (float *)(sysNode->buffer + SYS_OneAudioSize);
        float *outL = (float *)(audio_st.frameBuffer);
        float *outR = (float *)(audio_st.frameBuffer + SYS_OneAudioSize);

        for (int i = 0; i < nbSamples; i++) {
            outL[i] = tanhf(micL[i] + sysL[i]);
            outR[i] = tanhf(micR[i] + sysR[i]);
        }

        free(micNode->buffer);
        free(micNode);
        free(sysNode->buffer);
        free(sysNode);
    }

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
        AVRational tb = {1, 1000};
        audio_pts = av_rescale_q(audio_st.next_pts, audio_st.st->time_base, tb);
        audio_st.next_pts += audio_st.frame->nb_samples;

        ret = write_frame(oc, &audio_st.enc->time_base, audio_st.st, &pkt);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error while writing audio frame: %s (errno %d)\n", errbuf, ret);
        }
    }

    return true;
}
```

- [ ] **Step 7: 修改 run() 中的音频编码条件**

在 `run()` 方法（第 828 行起）中，修改音频流创建条件。定位到第 857-861 行：

```cpp
    // 原代码：
    // if (m_avFormat.hasAudio) {
    //     add_audio_stream(&audio_st, oc, &audio_codec, AV_CODEC_ID_AAC);
    //     have_audio = 1;
    //     encode_audio = 1;
    // }

    // 修改为：
    if (m_avFormat.hasAudio || m_avFormat.hasSystemAudio) {
        add_audio_stream(&audio_st, oc, &audio_codec, AV_CODEC_ID_AAC);
        have_audio = m_avFormat.hasAudio ? 1 : 0;
        have_sys_audio = m_avFormat.hasSystemAudio ? 1 : 0;
        encode_audio = 1;
    }
```

在同一个 `run()` 中，修改音频编码调用。定位到约第 902-915 行的编码循环：

```cpp
    // 原代码中调用 write_audio_frame_from_queue() 的位置
    // 改为调用 write_mixed_audio_frame()
    while (true) {
        if (have_video &&
            (!(have_audio || have_sys_audio) ||
             (av_compare_ts(video_st.next_pts, video_st.enc->time_base,
                           audio_st.next_pts, audio_st.enc->time_base) <= 0))) {
            if (!write_video_frame_from_queue()) {
                msleep(1);
            }
        } else {
            if (!write_mixed_audio_frame()) {
                msleep(1);
            }
        }

        if (isStop) {
            if (m_audioDataList.size() == 0 && m_sysAudioDataList.size() == 0 &&
                m_videoDataList.size() == 0) {
                break;
            }
        }
    }
```

- [ ] **Step 8: 修改 slot_setInfo() — 清理系统音频队列和重置标志**

在 `slot_setInfo()` 方法（第 737 行起）中，麦克风队列清理后追加系统音频队列清理：

```cpp
    m_audioMutex.lock();
    while (!m_audioDataList.isEmpty()) {
        BufferDataNode* node = m_audioDataList.takeFirst();
        if (node) {
            free(node->buffer);
            free(node);
        }
    }
    m_audioMutex.unlock();

    // 清理系统音频队列（新增）
    m_sysAudioMutex.lock();
    while (!m_sysAudioDataList.isEmpty()) {
        BufferDataNode* node = m_sysAudioDataList.takeFirst();
        if (node) {
            free(node->buffer);
            free(node);
        }
    }
    m_sysAudioMutex.unlock();
```

在 `slot_setInfo()` 方法内，重置状态标志部分，确保 `have_sys_audio` 也被重置（定位到约第 743 行 `have_audio = 0;` 处）：

```cpp
    have_video = 0;
    have_audio = 0;
    have_sys_audio = 0;  // 新增
    encode_video = 0;
    encode_audio = 0;
```

- [ ] **Step 9: 修改 slot_openAudio() — 同时启动系统音频**

在 `slot_openAudio()`（第 726-729 行）：

```cpp
void SaveVideoFileThread::slot_openAudio()
{
    if (m_avFormat.hasAudio) {
        m_audioRead->slot_openAudio();
    }
    if (m_avFormat.hasSystemAudio) {
        m_sysAudioRead->slot_openAudio();
    }
}
```

- [ ] **Step 10: 修改 slot_closeAudio() — 同时停止系统音频**

在 `slot_closeAudio()`（第 731-734 行）：

```cpp
void SaveVideoFileThread::slot_closeAudio()
{
    m_audioRead->slot_closeAudio();
    m_sysAudioRead->slot_closeAudio();  // 新增
}
```

- [ ] **Step 11: 修改 slot_closeVideo() — isStop 条件**

在 `slot_closeVideo()`（第 955 行起）中，`isStop` 检查后确保系统音频也被停止：

```cpp
    m_picInPicRead->slot_closeVideo();
    m_audioRead->slot_closeAudio();
    m_sysAudioRead->slot_closeAudio();  // 新增
```

- [ ] **Step 12: 修改 run() 末尾和 run_fail 的 close_stream 条件**

在 `run()` 末尾（约第 935-936 行）和 `run_fail` 标签（约第 946 行），将音频流关闭条件改为涵盖系统音频：

```cpp
    // run() 正常结束路径（约第 935-936 行）：
    // 原代码：
    // if (m_avFormat.hasAudio)
    //     close_stream(oc, &audio_st);

    // 修改为：
    if (m_avFormat.hasAudio || m_avFormat.hasSystemAudio)
        close_stream(oc, &audio_st);

    // run_fail 错误路径（约第 946 行）：
    // 原代码：
    // if (have_audio) close_stream(oc, &audio_st);

    // 修改为：
    if (have_audio || have_sys_audio) close_stream(oc, &audio_st);
```

- [ ] **Step 13: 验证 savevideofilethread.cpp 修改完整**

```
确认所有修改点一致，SYS_OneAudioSize 宏在 systemaudioread.h 中定义且 savevideofilethread.cpp 通过 include 可访问
```

---

### Task 5: 修改 recorder 对话框 UI

**文件:** 修改 `D:/Video/VideoRecoder/VideoRecoder/recorderdialog.ui`

- [ ] **Step 1: 移除 cb_audio，新增 cb_mic 和 cb_system_audio**

定位到第 190-198 行，替换 `cb_audio` 为两个独立复选框：

原代码（约第 190-198 行）：
```xml
        <item>
         <widget class="QCheckBox" name="cb_audio">
          <property name="text">
           <string>音频</string>
          </property>
          <property name="checked">
           <bool>true</bool>
          </property>
         </widget>
        </item>
```

替换为：
```xml
        <item>
         <widget class="QCheckBox" name="cb_mic">
          <property name="text">
           <string>麦克风</string>
          </property>
          <property name="checked">
           <bool>true</bool>
          </property>
         </widget>
        </item>
        <item>
         <widget class="QCheckBox" name="cb_system_audio">
          <property name="text">
           <string>系统音频</string>
          </property>
          <property name="checked">
           <bool>false</bool>
          </property>
         </widget>
        </item>
```

- [ ] **Step 2: 验证 UI 文件 XML 结构**

```
确认 recorderdialog.ui 的 XML 结构完整、无语法错误
```

---

### Task 6: 修改 recorderdialog.cpp — 传递 hasSystemAudio

**文件:** 修改 `D:/Video/VideoRecoder/VideoRecoder/recorderdialog.cpp`

- [ ] **Step 1: 修改 on_pb_start_clicked() 中的音频源设置**

定位到第 55 行 `format.hasAudio = ui->cb_audio->isChecked();`：

```cpp
// 原代码：
// format.hasAudio = ui->cb_audio->isChecked();

// 修改为：
format.hasAudio = ui->cb_mic->isChecked();                  // 麦克风
format.hasSystemAudio = ui->cb_system_audio->isChecked();    // 系统音频
```

同时更新启动音频采集的条件（第 84-86 行）：

```cpp
    // 原代码：
    // if (format.hasAudio) {
    //     m_saveFileThread->slot_openAudio();
    // }

    // 修改为：
    if (format.hasAudio || format.hasSystemAudio) {
        m_saveFileThread->slot_openAudio();
    }
```

注：由于 `ui_recorderdialog.h` 是 Qt UIC 自动生成的，`cb_mic` 和 `cb_system_audio` 的访问器会在编译时自动生成，无需手动修改 `ui_recorderdialog.h`。

- [ ] **Step 2: 验证 recorderdialog.cpp 修改**

```
确认变量名 cb_mic / cb_system_audio 与 UI 文件中的 widget name 一致
```

---

### Task 7: 修改 VideoRecoder.pro — 新增源文件和库

**文件:** 修改 `D:/Video/VideoRecoder/VideoRecoder/VideoRecoder.pro`

- [ ] **Step 1: 新增 SOURCES 和 HEADERS**

在第 12-13 行之间插入 `systemaudioread.cpp`（SOURCES），在第 20 行之后插入 `systemaudioread.h`（HEADERS）：

```qmake
SOURCES += \
    main.cpp \
    audio_read.cpp \
    systemaudioread.cpp \     # 新增
    picinpic_read.cpp \
    picturewidget.cpp \
    recorderdialog.cpp \
    savevideofilethread.cpp

HEADERS += \
    audio_read.h \
    systemaudioread.h \       # 新增
    common.h \
    picinpic_read.h \
    picturewidget.h \
    recorderdialog.h \
    savevideofilethread.h
```

- [ ] **Step 2: 新增 ole32.lib 链接**

在第 58 行 `LIBS += -lstdc++ -lgcc` 之后新增：

```qmake
# Windows WASAPI Loopback 依赖（新增）
LIBS += -lole32
```

- [ ] **Step 3: 验证 .pro 文件**

```
确认 .pro 文件语法正确，文件路径与项目目录一致
```

---

### 编译验证

- [ ] **构建项目**

```bash
cd D:/Video/VideoRecoder
qmake VideoRecoder/VideoRecoder.pro -spec win32-g++
make -j4 -f Makefile.Debug
```

预期：编译通过，无错误。

### 功能验证

- [ ] **场景 1: 仅麦克风** — 勾选麦克风，不勾选系统音频 → 推流仅含麦克风音频（现有行为不变）
- [ ] **场景 2: 仅系统音频** — 不勾选麦克风，勾选系统音频 → 推流含电脑播放声音
- [ ] **场景 3: 麦克风 + 系统音频** — 两路都勾选 → 推流含混音
- [ ] **场景 4: 都不勾选** — 无音频流，仅视频推流
- [ ] **场景 5: 无扬声器设备** — 系统音频初始失败 → 弹窗提示，其他功能正常
