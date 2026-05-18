# VideoRecoder XMind 思维导图提示词 【2026.4.8 更新】

## 使用说明

将以下内容复制到 XMind AI 助手的提示词输入框，或根据此结构手动创建思维导图。

---

## 提示词内容

```
请为 VideoRecoder 视频录制项目创建一个完整的思维导图，包含以下内容结构：

中心主题：VideoRecoder 音视频录制系统 【2026.4.8更新：新增音频功能】

一级分支：
1. 技术栈架构
2. 功能模块
3. 数据流
4. 问题与解决方案
5. 扩展规划

各分支详细内容：

【1. 技术栈架构】
- GUI 层：Qt 5.12.11 (Widgets, 信号槽, 跨平台)
- 视频采集层：OpenCV 4.2.0 (VideoCapture, 图像处理)
- 音频采集层：Qt Multimedia 5.12.11 (QAudioInput) 【2026.4.8新增】
- 视频编码层：FFmpeg 4.2.2 (libavcodec H.264, libavformat, libswscale)
- 音频编码层：FFmpeg 4.2.2 (libavcodec AAC, libswresample) 【2026.4.8新增】
- 构建层：MinGW 7.3.0 + qmake

【2. 功能模块】
- 用户界面模块
  - RecorderDialog（主界面）
  - PictureWidget（画中画窗口）
  - 控制按钮（开始/停止/设置URL）
  - 设备选择（摄像头/音频复选框）
  
- 视频采集模块 (PicInPic_Read)
  - 摄像头捕获 (OpenCV)
  - 桌面截图 (QScreen)
  - 画中画合成 (QImage::scaled)
  - 定时器控制 (QTimer, 25fps)
  
- 音频采集模块 (Audio_Read) 【2026.4.8新增】
  - QAudioInput 设备采集
  - PCM S16 格式 (44.1kHz, 16bit, 立体声)
  - 定时读取 (20ms间隔)
  - swresample 重采样 (S16→FLTP)
  
- 编码保存模块 (SaveVideoFileThread)
  - 视频编码：颜色空间转换 (RGB→YUV420P) → H.264
  - 音频编码：重采样 (S16→FLTP) → AAC 【2026.4.8新增】
  - 音视频同步：PTS时间戳对齐 【2026.4.8新增】
  - FLV 封装复用 (av_interleaved_write_frame)
  - 文件写入

【3. 数据流】 【2026.4.8更新：新增音频流】
视频流：
摄像头 → OpenCV → QImage
桌面 → QScreen → QImage
两者合成 → SIG_sendVideoFrame
信号分发 → 3个方向：
  1. 主界面显示 (RecorderDialog::slot_setImage)
  2. 悬浮窗显示 (PictureWidget::slot_setImage)
  3. 编码保存 (SaveVideoFileThread::slot_writeVideoFrame)
视频编码流程：QImage → sws_scale → YUV420P → avcodec_encode_video2 → H.264包

音频流： 【2026.4.8新增】
麦克风 → QAudioInput → PCM S16
定时读取 → swr_convert → FLTP → SIG_sendAudioFrameData
→ SaveVideoFileThread::slot_writeAudioFrameData
→ avcodec_encode_audio2 → AAC包

音视频同步： 【2026.4.8新增】
视频PTS = (elapsedMs * 25) / 1000
音频PTS = samples_count / 44100
→ av_interleaved_write_frame (按PTS排序)
→ 文件输出

【4. 问题与解决方案】
- 播放速度过快
  - 原因：PTS计算错误
  - 解决：基于时间戳计算 pts = (elapsedMs * frame_rate) / 1000
  
- 程序崩溃
  - 原因：frameBuffer 重复释放
  - 解决：AVFrame 内部管理，不单独释放
  
- 文件为空
  - 原因：信号槽未连接
  - 解决：正确连接 SIG_sendVideoFrame 到 slot_writeVideoFrame

【5. 扩展规划】 【2026.4.8更新：音频已完成】
- ✅ 音频录制 (Qt Multimedia + FFmpeg AAC) - 已完成
- RTMP 直播推流 (FFmpeg 推流协议)
- 多摄像头支持 (OpenCV 多实例)

样式要求：
- 技术栈用蓝色系
- 数据流用绿色箭头连接
- 问题解决方案用橙色标记
- 模块用不同颜色区分
- 【2026.4.8新增】内容用紫色高亮标记
```

---

## 手动创建指南

如果你选择手动创建思维导图，请参考以下层级结构：

### 中心节点
**VideoRecoder 音视频录制系统** 🎬 【2026.4.8更新】

### 第一层节点（5个）

#### 1. 技术栈架构 🔵
- **Qt 5.12.11** (GUI框架)
  - 信号槽机制
  - 跨平台支持
  - Widgets组件
- **OpenCV 4.2.0** (视频采集)
  - VideoCapture
  - 图像处理
  - 格式转换
- **Qt Multimedia 5.12.11** (音频采集) 🟣【2026.4.8新增】
  - QAudioInput
  - PCM采集
  - 设备管理
- **FFmpeg 4.2.2** (音视频编码)
  - libavcodec (H.264视频编码)
  - libavcodec (AAC音频编码) 🟣【2026.4.8新增】
  - libavformat (FLV封装)
  - libswscale (视频颜色转换)
  - libswresample (音频重采样) 🟣【2026.4.8新增】
- **MinGW + qmake** (构建工具)

#### 2. 功能模块 🟢
- **UI模块**
  - RecorderDialog 主界面
  - PictureWidget 画中画
  - 控制面板（视频/音频复选框）
- **视频采集模块** (PicInPic_Read)
  - 摄像头捕获
  - 桌面截图
  - 画中画合成
  - QTimer定时器
- **音频采集模块** (Audio_Read) 🟣【2026.4.8新增】
  - QAudioInput设备采集
  - PCM S16格式 (44.1kHz)
  - 20ms定时读取
  - swresample重采样 (S16→FLTP)
- **编码保存模块** (SaveVideoFileThread)
  - 视频编码 (RGB→YUV→H.264)
  - 音频编码 (S16→FLTP→AAC) 🟣【2026.4.8新增】
  - 音视频同步 (PTS对齐) 🟣【2026.4.8新增】
  - FLV复用封装

#### 3. 数据流 🟡（重点）【2026.4.8更新】
```
┌─────────────────────────────────────────────────────────────┐
│  视频输入源                                                  │
│  ├─ 摄像头 → OpenCV VideoCapture → QImage (BGR→RGB)        │
│  └─ 桌面 → QScreen::grabWindow → QImage                    │
└──────────────────────────┬──────────────────────────────────┘
                           ↓
                    画中画合成
                    (320x240叠加)
                           ↓
              SIG_sendVideoFrame(QImage)
                           ↓
        ┌──────────────────┼──────────────────┐
        ↓                  ↓                  ↓
   主界面显示         悬浮窗显示          视频编码
   (RecorderDialog)  (PictureWidget)   (SaveVideoFileThread)
                                            ↓
                                    sws_scale (RGB→YUV420P)
                                            ↓
                                    avcodec_encode_video2
                                            ↓
                                    H.264视频包 ─┐
                                               │
┌──────────────────────────────────────────────┼─────────────┐
│  音频输入源 🟣【2026.4.8新增】                │             │
│  └─ 麦克风 → QAudioInput → PCM S16          │             │
│                           ↓                  │             │
│                    定时读取 (20ms)           │             │
│                           ↓                  │             │
│                    swr_convert (S16→FLTP)    │             │
│                           ↓                  │             │
│              SIG_sendAudioFrameData          │             │
│                           ↓                  │             │
│              slot_writeAudioFrameData        │             │
│                           ↓                  │             │
│                    avcodec_encode_audio2     │             │
│                           ↓                  │             │
│                    AAC音频包 ────────────────┘             │
│                                                            │
│  音视频同步 🟣【2026.4.8新增】                             │
│  ├─ 视频PTS = elapsedMs * 25 / 1000                       │
│  └─ 音频PTS = samples_count / 44100                       │
│                           ↓                                │
│              av_interleaved_write_frame                    │
│              (按PTS从小到大排序)                            │
│                           ↓                                │
│                      文件输出                              │
└────────────────────────────────────────────────────────────┘
```

#### 4. 问题与解决方案 🔴
- **播放速度过快**
  - 现象：VLC播放像快进
  - 根因：PTS计算错误
  - 方案：基于时间戳计算
- **程序崩溃**
  - 现象：停止时异常终止
  - 根因：重复释放内存
  - 方案：AVFrame自动管理
- **文件为空**
  - 现象：只有1KB
  - 根因：信号未连接
  - 方案：connect信号槽

#### 5. 扩展规划 🟣 【2026.4.8更新】
- **✅ 音频录制** - 已完成 🎉
  - Qt Multimedia采集
  - FFmpeg AAC编码
  - 音视频同步
- **RTMP推流**
  - 直播功能
  - FLV over RTMP
- **多摄像头**
  - 设备枚举
  - 多路采集

---

## 图标建议

| 节点类型 | 建议图标 |
|---------|---------|
| 技术栈 | 💻 电脑 |
| 功能模块 | ⚙️ 齿轮 |
| 数据流 | 🔄 循环箭头 |
| 问题 | ⚠️ 警告 |
| 解决方案 | ✅ 对勾 |
| 扩展规划 | 🚀 火箭 |
| 输入 | 📥 输入 |
| 输出 | 📤 输出 |
| 音频相关 | 🎤 麦克风 |
| 视频相关 | 🎥 摄像机 |
| 已完成 | ✓ 完成标记 |
| 新增功能 | 🆕 新功能 |

---

## 颜色编码建议

| 类别 | 颜色 | 用途 |
|-----|------|------|
| 原有内容 | 蓝色/绿色 | 视频相关功能 |
| 新增内容【2026.4.8】 | 紫色/粉色 | 音频相关功能 |
| 数据流 | 黄色/橙色 | 数据流向 |
| 问题 | 红色 | 问题标记 |
| 解决方案 | 绿色 | 解决标记 |

---

## 连接线说明

- **实线箭头**：数据流向
- **虚线箭头**：信号槽连接
- **双向箭头**：双向通信
- **彩色线条**：不同模块区分
- **🟣紫色线条**：音频数据流（新增）

---

**提示词版本：** v1.1  
**更新日期：** 2026-04-08  
**更新内容：** 新增音频采集与AAC编码模块  
**适用工具：** XMind / MindMaster / 亿图脑图
