# 系统音频捕获 — 设计文档

**日期:** 2026-05-03
**状态:** 已批准

## 目标

在 VideoRecoder 推流中增加"系统音频"（电脑扬声器播放的声音）的录制支持。目前项目已支持麦克风采集和视频采集，缺少系统音频采集能力。

用户可独立勾选"麦克风"和"系统音频"两个复选框，两路音频在编码前做 PCM 层面的 float 混音，合并为一路 AAC 音频流推送到 RTMP。

## 当前架构回顾

```
Audio_Read (QAudioInput) → PCM S16 → swr_convert → FLTP → 音频队列 → AAC 编码 → RTMP
```

`Audio_Read` 使用的是 `QAudioInput` + `QAudioDeviceInfo::defaultInputDevice()`，在 Windows 上底层是 WASAPI Capture，但只捕获输入设备（麦克风），无法捕获扬声器输出。

## 新增架构

```
Audio_Read        (麦克风)   ─→ PCM S16 → swr → FLTP → 音频队列_A ─┐
SystemAudioRead   (系统音频) ─→ Float   → swr → FLTP → 音频队列_B ─┤
                                                                     ↓
                                                               混音 (float加法)
                                                                     ↓
                                                                AAC 编码 → RTMP
```

## 核心新增类：SystemAudioRead

### API

```cpp
class SystemAudioRead : public QObject
{
    Q_OBJECT
signals:
    void SIG_sendAudioFrameData(uint8_t* buf, int buffer_size);  // 与 Audio_Read 一致

public slots:
    void slot_openAudio();   // 初始化 WASAPI Loopback，启动定时器
    void slot_closeAudio();  // 停止捕获
    void UnInit();           // 回收 COM + timer + swr 资源
};
```

### WASAPI 初始化流程

1. `CoInitializeEx(COINIT_MULTITHREADED)` — COM 初始化
2. `CoCreateInstance(IMMDeviceEnumerator)` — 获取设备枚举器
3. `GetDefaultAudioEndpoint(eRender, eConsole)` — 获取默认扬声器设备
4. `Activate(IID_IAudioClient)` → `Initialize(SHARED, LOOPBACK, 10ms buffer)` — 以 Loopback 模式打开
5. `GetService(IID_IAudioCaptureClient)` — 获取捕获接口
6. `GetMixFormat` — 获取混音格式，配置 swr_ctx

### 音频格式转换

| 阶段 | 采样率 | 格式 | 声道 |
|------|--------|------|------|
| WASAPI 输出 (mix format) | 48000 Hz | Float (AV_SAMPLE_FMT_FLT) | 立体声 |
| swr_convert 后 | 44100 Hz | FLTP (AV_SAMPLE_FMT_FLTP) | 立体声 |

重采样后的数据格式与麦克风采集管线完全一致，确保混音时两路数据兼容。

### 采集参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 定时器间隔 | 10ms | 比麦克风的 20ms 更短，匹配 WASAPI 更小的 buffer |
| 每次 throw 帧数 | 1 帧 | 1024 采样点 × 2 声道 × 4 字节 = 8192 字节 |
| 信号格式 | uint8_t FLTP planar | 与 Audio_Read 的 SIG_sendAudioFrameData 完全一致 |

### 与 Audio_Read 的差异

| | Audio_Read | SystemAudioRead |
|------|-----------|-----------------|
| 音频源 | 麦克风（默认输入设备） | 扬声器（默认输出设备 Loopback） |
| API | QAudioInput（Qt 封装） | WASAPI COM（直接调用） |
| 原始格式 | S16 PCM | Float PCM |
| 采集间隔 | 20ms | 10ms |
| 转换目标 | S16 → FLTP | Float → FLTP |

## SaveVideoFileThread 改动

### 新增成员

```cpp
SystemAudioRead *m_sysAudioRead;                    // 系统音频采集对象
QList<BufferDataNode*> m_sysAudioDataList;          // 系统音频队列
QMutex m_sysAudioMutex;                             // 队列锁
static const int MAX_SYS_AUDIO_QUEUE_SIZE = 30;
bool hasSysAudio;                                   // 是否启用系统音频
```

### 队列方法（与现有麦克风队列对称）

```cpp
void sysAudioDataQueue_Input(const uint8_t *buffer, int size);  // 入队
BufferDataNode* sysAudioDataQueue_get();                        // 出队
```

### 混音函数

```cpp
// 从两个队列各取一帧 FLTP 数据（1024采样点 × 立体声 planar = 8192字节）
// float 逐采样点相加，使用 tanh 软限幅防止削波:
//   output[i] = tanh(inputA[i] + inputB[i])
// 若只有一路音频有数据，直接复制该路，不做混音
```

### run() 编码循环改动

当前 AV 同步逻辑不变。音频编码帧的条件修改为：

```
if (麦克风启用且队列有数据) 或 (系统音频启用且队列有数据):
    从有数据的队列各取一帧 → 混音 → AAC 编码
```

### STRU_AV_FORMAT 新增字段

```cpp
bool hasSystemAudio;   // 是否录制系统声音
```

### 其他改动点

### 生命周期

| 阶段 | 操作 |
|------|------|
| 构造函数 | `new SystemAudioRead`，`connect` 信号到 `slot_writeSysAudioFrameData`（只创建对象，不采集） |
| `slot_setInfo()` | 设置 `hasSysAudio = avFormat.hasSystemAudio`，不启动采集 |
| `slot_openAudio()` | 若 `hasAudio` 则启动麦克风，若 `hasSysAudio` 则启动系统音频 |
| `slot_closeAudio()` | 两路同时停止 |
| 析构函数 | 清理 `m_sysAudioRead` 和系统音频队列残帧 |

### 音频启用组合

| 麦克风 | 系统音频 | 行为 |
|--------|---------|------|
| ✓ | ✓ | 两路混音 → AAC |
| ✓ | ✗ | 仅麦克风（现有行为） |
| ✗ | ✓ | 仅系统音频 |
| ✗ | ✗ | 无音频流 |

## UI 改动

修改 `recorderdialog.ui`：

- 移除现有的单一 `cb_audio`（音频）复选框
- 新增两个复选框：
  - `cb_mic` — "麦克风"（默认勾选）
  - `cb_system_audio` — "系统音频"（默认不勾选）

修改 `recorderdialog.cpp` 的 `on_pb_start_clicked()`：

```cpp
format.hasAudio      = ui->cb_mic->isChecked();          // 麦克风
format.hasSystemAudio = ui->cb_system_audio->isChecked();  // 系统音频
```

## VideoPlayer 端

**不做改动。** RTMP 推流始终只包含一个 AAC 音频流（已在编码端混音），播放器无需任何修改即可正常解码播放。

## 编译依赖

需要链接以下 Windows SDK 库：

```
ole32.lib        — COM 基础
```

需要的头文件（Windows SDK 已包含）：

```cpp
#include <mmdeviceapi.h>     // IMMDeviceEnumerator, IMMDevice
#include <audioclient.h>     // IAudioClient, IAudioCaptureClient
#include <functiondiscoverykeys_devpkey.h>  // PKEY_Device_FriendlyName
```

项目 `.pro` 文件需添加：

```qmake
LIBS += -lole32
```

## 改动文件清单

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| `systemaudioread.h` | **新增** | SystemAudioRead 类声明 |
| `systemaudioread.cpp` | **新增** | WASAPI Loopback 采集实现 |
| `savevideofilethread.h` | 修改 | 新增 SystemAudioRead 成员、队列、混音方法声明 |
| `savevideofilethread.cpp` | 修改 | 混音逻辑、双队列消费、hasSystemAudio 支持 |
| `recorderdialog.ui` | 修改 | 拆分音频复选框 |
| `recorderdialog.cpp` | 修改 | on_pb_start_clicked 传递 hasSystemAudio |
| `VideoRecoder.pro` | 修改 | 新增源文件 + ole32.lib |

## 不做什么

- 不修改 Audio_Read 麦克风采集逻辑
- 不修改 AAC 编码器配置（仍为 44100Hz 立体声 FLTP，64kbps）
- 不修改 VideoPlayer 端
- 不支持系统音频的音量独立调节（首版不做）
- 不支持 macOS / Linux（仅 Windows WASAPI）

## 错误处理

- WASAPI 初始化失败（无扬声器设备）→ 弹窗提示，系统音频录制无效，麦克风/视频不受影响
- 混音时一方无数据 → 直接使用有数据的一方，不做混音
- COM 未初始化 → SystemAudioRead 构造函数中调用 CoInitializeEx
