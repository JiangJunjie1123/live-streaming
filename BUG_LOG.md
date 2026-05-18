# 音频录制问题排查日志

## 问题描述

VideoRecoder 在录制视频时，音频出现三种异常：
- **抖动**：声音不稳定，时快时慢
- **回声/反复播放**：同一段音频像被多次播放
- **滋滋爆破音**：高频电流声

录制方式：双设备混音 —— 系统音频（立体声混音）+ 麦克风阵列 → Audio_Mixer 混音 → AAC 编码 → FLV 文件。

---

## 初始诊断数据

```
[Audio_Mixer diag] systemPeak: 4 micPeak: 22 framesSys: 7 framesMic: 12
[Audio_Mixer diag] systemPeak: 4 micPeak: 2294 framesSys: 13 framesMic: 5
[Audio_Mixer diag] systemPeak: 911 micPeak: 4 framesSys: 9 framesMic: 2
...
[Audio_Mixer diag] systemPeak: 692 micPeak: 7 framesSys: 11 framesMic: 6
[aac @ 2783ce40] 2 frames left in the queue on closing
```

关键发现：`framesSys` 和 `framesMic` 严重不平衡 — 系统音频可达 29 帧而麦克风只有 2-5 帧。

---

## 失败尝试 #1：混音器 max() + 静音填充

### 假设
两个设备的帧数差导致 `min()` 让慢设备拖住快设备，旧数据残留后在下一轮被重混。

### 修改
- `audio_mixer.cpp`：`min(framesSys, framesMic)` → `max(framesSys, framesMic)`
- 慢设备零填充，各消费各自实际帧数
- `savevideofilethread.cpp`：音频编码 API 从 `avcodec_encode_audio2` 改为 `avcodec_send_frame`/`avcodec_receive_packet`
- 添加编码器 flush
- 删除全零帧跳过逻辑

### 结果：**效果更差**
静音填充导致麦克风音频在系统音频多出的帧中出现明显断档，人声时有时无。

### 教训
同时修改了太多变量（混音逻辑 + 编码 API + PTS），无法确定哪个导致恶化。静音填充理论上正确但实践中产生新的不连续感。

---

## 失败尝试 #2：Buffer 9 帧上限

### 假设
快设备 buffer 中旧数据与新数据时间差过大，混合后产生回声。

### 修改
仅 `audio_mixer.cpp`：`framesSys/framesMic` 计算后，若一方超出另一方 9 帧（≈200ms），截断旧帧。

### 结果：**基本没区别**
截断引入了新的采样点不连续，抵消了去除旧数据的好处。

---

## 失败尝试 #3：删除全零帧跳过 + 编码层 PTS 修复

### 假设
跳过全零帧时 `samples_count` 未递增，PTS 序列断裂导致 AAC 解码爆破音。

### 修改
- 删除 `allZero` 检查（10 行）
- `next_pts` 移到 `got_packet` 块外无条件递增
- 编码器关闭前用旧 API 刷残留帧

### 结果：**只改善了一点点**
核心问题不在此。

---

## 失败尝试 #4：统一 buffer 大小

### 假设
两设备使用不同默认 buffer 导致数据量不均。

### 修改
仅一行：`dev.audio_in->setBufferSize(8192)`

### 结果：**效果更差**
Qt 在不同音频端点类型上 buffer 机制不同，硬设 buffer 反而打乱了底层驱动的时序。

---

## 转折点：文献分析与架构审视

三篇 Qt 音频编程文献都展示了相同的标准模式：

```cpp
m_audioDevice = m_audioInput->start();  // 返回 QIODevice
connect(m_audioDevice, &QIODevice::readyRead,  // 事件驱动
        this, &AudioCapture::handleAudioDataReady);

void handleAudioDataReady() {
    QByteArray audioData = m_audioDevice->readAll();  // 立即全量读取
    // 立即处理，不积累
}
```

### 我们代码的实际做法

```cpp
timer->start(20);  // 每 20ms 定时轮询

void slot_readMore() {
    qint64 len = audio_in->bytesReady();  // 查询可用量
    // 复杂的 buffer 管理，30 行代码
    // 数据不删除，用 m_buffPos 标记消费位置
    // min() 取齐，旧数据残留
}
```

### 根本差异

| 维度 | 标准做法 | 我们的代码 |
|------|---------|-----------|
| 驱动方式 | `readyRead` 信号事件驱动 | QTimer 定时轮询 |
| 数据读取 | `readAll()` 一次全量 | `bytesReady()` + 循环 read |
| Buffer 管理 | 消费后真正移除 | `m_buffPos` 标记，数据不删 |
| 数据积累 | 数据到了立即处理，不堆积 | 20ms 内两设备各积累不同量 |

核心矛盾：定时轮询下两个独立时钟的音频设备在固定时间窗口内必然积累不同量的数据，`min()` 让快的等慢的，快的旧数据残留被下轮重混。

---

## 最终解决方案

### 修改内容（仅 `audio_mixer.cpp`）

**1. `readFromDevice`：从 30 行简化为 4 行**
```cpp
// 旧：bytesReady() + 手动 read 循环 + m_buffPos 管理
// 新：readAll() 一次性读取，追加到 buffer 末尾
QByteArray data = dev.myBuffer_in->readAll();
if (data.isEmpty()) return;
dev.m_audiobuff.resize(oldSize + data.size());
memcpy(dev.m_audiobuff.data() + oldSize, data.constData(), data.size());
```

**2. `updateBufferPos`：消费后真正移除数据**
```cpp
// 旧：m_buffPos = consumedBytes（数据残留）
// 新：memmove 移除已消费部分，buffer 缩小
memmove(dev.m_audiobuff.data(),
        dev.m_audiobuff.data() + consumedBytes, remaining);
dev.m_audiobuff.resize(remaining);
```

**3. `slot_openAudio`：readyRead 事件驱动代替定时器**
```cpp
// 旧：timer->start(AUDIO_INTERVAL);
// 新：
connect(m_systemDev.myBuffer_in, &QIODevice::readyRead,
        this, &Audio_Mixer::slot_readMore);
connect(m_micDev.myBuffer_in, &QIODevice::readyRead,
        this, &Audio_Mixer::slot_readMore);
```

### 为什么有效

1. **事件驱动**：数据到达立即处理，不在固定时间窗口内积累，两设备数据量自然接近
2. **消费即移除**：`memmove` 真正删除已混音数据，旧数据不可能被重混
3. **不改混音逻辑**：`slot_readMore` 的核心逻辑（读取→计算帧数→min()→混音→发出）完全保留

---

## 关键教训

1. **先确定问题层级再动手**：在混音算法、编码 API、PTS、buffer 大小四个不同层面反复试探，浪费大量时间。应该先用排除法隔离问题源头。

2. **一次只改一个变量**：尝试 #1 同时改了 4 处，根本无法判断哪个起作用、哪个导致恶化。

3. **先对照标准做法**：三篇文献都指向 `readyRead` + `readAll()`，这是 Qt 官方推荐的模式。先和标准对齐，再看其他。

4. **回退方案是安全的保障**：每次改动前保留回退注释或记录原始代码，否则多次失败后容易丢失原始状态。

5. **"一帧反复播放" 的含义**：用户描述的"回声"实际上是同一段系统音频在连续多个 AAC 帧中出现——不是回声，是 buffer 残留导致的重混。
