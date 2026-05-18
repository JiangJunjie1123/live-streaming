---
name: audio-looping-fix-plan
description: 推流音频循环问题的完整修复方案，可直接交给 agent 执行
type: plan
---

# 推流音频循环问题 — 修复方案

## 背景

项目是 RTMP 推流直播系统（Windows Qt + FFmpeg）。推流时音频持续循环重复，说一句话后这句话无限循环，再说新话时旧话+新话一起循环。

经过系统调试（systematic-debugging）确认：

**根因：软件音频闭环。** Audio_Mixer 使用 `"立体声混音 (Realtek(R) Audio)"` 作为系统音频输入源，该设备捕获 Windows 音频输出总线。推流端和播放端在同一台机器时，播放器输出的声音被立体声混音重新采集，形成正反馈——每循环一圈增益叠加（诊断日志显示 sysPeak 从 12 指数增长到 27283）。

戴耳机也无法消除，因为立体声混音采集的是软件层面的音频总线，不是空气传播的物理声音。

**之前尝试过但无效的修复（不要重复）：**
- 修复播放器 `audio_decode_frame` 的 FFmpeg API 误用 — 现象不变
- 升级编码器 `avcodec_encode_audio2` → send/receive API — 现象不变

这两个虽然是有价值的改进，但不是根因。

**之前已回退到原始代码，所有文件当前都是修改前状态，可以直接开始改。**

---

## 修复策略

核心思路：**在 UI 上添加「系统音频开关」，允许用户选择是否采集系统音频。**不修改混音逻辑代码（audio_mixer.cpp），只修改 UI 层和 SaveVideoFileThread 的设备初始化。

### 为什么这样做
- 不碰混音器算法
- 当系统音频关闭时只采麦克风，闭环路径被物理切断
- 当需要游戏直播且推流/播放设备分离时，可以选择开启系统音频
- 最小侵入性修改

---

## 修改清单（3 个文件）

### 修改 1：`recorderdialog.ui` — UI 上添加系统音频复选框

位置：`D:\Video\VideoRecoder\VideoRecoder\recorderdialog.ui`

在现有的麦克风 checkbox（`cb_mic`）旁添加一个新的 checkbox：
- objectName: `cb_systemAudio`
- text: "系统音频"
- 默认勾选

### 修改 2：`recorderdialog.cpp` — 读取 checkbox 状态传入 avFormat

位置：`D:\Video\VideoRecoder\VideoRecoder\recorderdialog.cpp`

在 `on_pb_start_clicked()` 或 `slot_readyData()` 函数中（设置 `STRU_AV_FORMAT` 的地方）：

1. 在 `STRU_AV_FORMAT` 结构体（`savevideofilethread.h` 第 45 行）中添加一个字段：
   ```cpp
   bool hasSystemAudio;  // 是否启用系统音频采集
   ```

2. 在 `slot_setInfo` 调用之前设置：
   ```cpp
   avFormat.hasSystemAudio = ui->cb_systemAudio->isChecked();
   ```

3. 在 `slot_setInfo` 中初始化时重置此标志。

### 修改 3：`savevideofilethread.cpp` — 根据标志决定是否打开系统音频设备

位置：`D:\Video\VideoRecoder\VideoRecoder\savevideofilethread.cpp`

1. **构造函数（第 35-36 行）**：
   目前硬编码两个设备名创建 Audio_Mixer。改为：如果 `hasSystemAudio` 为 false，系统音频设备名传空字符串或只使用麦克风设备。

   Audio_Mixer 构造函数签名是：
   ```cpp
   Audio_Mixer(const QString& systemAudioDevice, const QString& micDevice, QObject *parent)
   ```

   当前调用：
   ```cpp
   m_audioRead = new Audio_Mixer("立体声混音 (Realtek(R) Audio)",
                                  "麦克风阵列 (适用于数字麦克风的英特尔® 智音技术)");
   ```

2. **在 `slot_setInfo`（第 888 行）**：
   根据 `avFormat.hasSystemAudio` 决定是否创建带系统音频的 Audio_Mixer。如果系统音频关闭，传空字符串给第一个参数，然后在 Audio_Mixer 的 `slot_openAudio` 中跳过系统设备。

   更简单的方式：在 `SaveVideoFileThread` 中添加一个 `bool m_hasSystemAudio` 成员变量，在 `slot_setInfo` 中设置，在 `slot_openAudio` 中使用。

### 修改 4（可选但推荐）：`audio_mixer.cpp` — 支持跳过系统设备

位置：`D:\Video\VideoRecoder\VideoRecoder\audio_mixer.cpp`

在 `slot_openAudio()` 函数（第 156 行）中，如果系统设备名为空，只打开麦克风设备，且 `slot_readMore` 中不等待系统设备数据。

**注意：这是唯一需要修改 audio_mixer.cpp 的地方，且只改设备打开逻辑，不改混音算法。** 如果非常坚持不改 audio_mixer.cpp，可以在 SaveVideoFileThread 中做判断——系统音频关闭时创建不同的 Audio_Mixer 实例或直接使用单设备的 Audio_Read。

---

## 验证步骤

1. 编译运行
2. **去掉系统音频勾选**，开始推流
3. 说一句话
4. 如果循环消失 → 根因确认 = 软件闭环
5. 再**勾上系统音频**，循环应该复现
6. 如果步骤 4 循环没消失 → 需进一步排查（但根据诊断数据几乎不可能）

---

## 向后兼容

- 默认勾选系统音频（保持当前行为不变）
- 用户可手动取消勾选来避免反馈回路
- 不破坏现有的录制/推流功能
