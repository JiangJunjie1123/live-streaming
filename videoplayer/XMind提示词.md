# XMind 思维导图提示词

## 提示词

请根据以下技术文档内容，生成一个结构清晰、层次分明的 XMind 思维导图。要求使用中文，包含详细的节点展开和代码示例标注。

```
中心主题：视频播放器项目技术架构

├── 一、项目概述
│   ├── 技术栈
│   │   ├── Qt 5.12.11（GUI框架）
│   │   ├── FFmpeg 4.2.2（音视频处理）
│   │   ├── SDL2 2.0.10（音频/线程）
│   │   └── OpenGL（硬件渲染）
│   └── 核心功能
│       ├── 本地视频播放
│       ├── 网络流媒体（HLS/RTMP/RTSP）
│       ├── 音视频同步
│       └── 硬件加速渲染
│
├── 二、FFmpeg核心技术原理（重点展开）
│   ├── 架构组成
│   │   ├── libavformat（格式封装）
│   │   ├── libavcodec（编解码）
│   │   ├── libavutil（工具库）
│   │   ├── libswscale（图像转换）
│   │   └── libswresample（音频转换）
│   │
│   ├── 音视频编解码原理
│   │   ├── 编码流程
│   │   │   ├── 帧类型判断（I/P/B帧）
│   │   │   ├── 运动估计
│   │   │   ├── DCT变换
│   │   │   ├── 量化处理
│   │   │   └── 熵编码
│   │   ├── 解码流程
│   │   │   ├── avcodec_find_decoder()
│   │   │   ├── avcodec_alloc_context3()
│   │   │   ├── avcodec_open2()
│   │   │   ├── avcodec_send_packet()
│   │   │   └── avcodec_receive_frame()
│   │   └── 核心数据结构
│   │       ├── AVCodecContext（编解码上下文）
│   │       ├── AVCodec（编解码器）
│   │       ├── AVFrame（原始帧）
│   │       └── AVPacket（压缩包）
│   │
│   ├── 格式解析原理
│   │   ├── 封装格式概念
│   │   │   ├── MP4/MKV/FLV/AVI
│   │   │   └── TS/M3U8（HLS）
│   │   ├── 解封装流程
│   │   │   ├── 读取文件头
│   │   │   ├── 解析流信息
│   │   │   └── 读取数据包
│   │   └── 核心API
│   │       ├── AVFormatContext（格式上下文）
│   │       ├── AVStream（流信息）
│   │       ├── avformat_open_input()
│   │       ├── avformat_find_stream_info()
│   │       └── av_read_frame()
│   │
│   ├── 网络流处理
│   │   ├── 支持协议
│   │   │   ├── HTTP/HTTPS
│   │   │   ├── HLS（HTTP Live Streaming）
│   │   │   ├── RTMP
│   │   │   └── RTSP
│   │   ├── HLS协议详解
│   │   │   ├── 服务器端切片
│   │   │   ├── M3U8播放列表
│   │   │   └── 客户端下载播放
│   │   └── 网络配置选项
│   │       ├── stimeout（超时）
│   │       ├── max_delay（延迟）
│   │       ├── buffer_size（缓冲区）
│   │       ├── live_start_index（起始位置）
│   │       └── seekable（是否可拖动）
│   │
│   ├── 像素格式与图像转换
│   │   ├── YUV色彩空间
│   │   │   ├── Y（亮度）
│   │   │   ├── U（色度/蓝差）
│   │   │   └── V（色度/红差）
│   │   ├── YUV420P内存布局
│   │   │   ├── Y平面（全分辨率）
│   │   │   ├── U平面（1/4分辨率）
│   │   │   └── V平面（1/4分辨率）
│   │   └── SwsScale转换
│   │       ├── sws_getContext()
│   │       └── sws_scale()
│   │
│   └── 时间基与同步
│       ├── 时间基概念
│       │   └── AVRational（分子/分母）
│       ├── 时间转换公式
│       │   └── 时间(秒) = 时间戳 × (num/den)
│       ├── 不同级别时间基
│       │   ├── AVFormatContext（微秒）
│       │   └── AVStream（流级别）
│       └── 时间戳转换
│           └── av_rescale_q()
│
├── 三、SDL2核心技术原理（重点展开）
│   ├── 架构组成
│   │   ├── 视频（Video）
│   │   ├── 音频（Audio）
│   │   ├── 输入（Events）
│   │   ├── 线程（Threads）
│   │   └── 定时器（Timer）
│   │
│   ├── 音频播放原理
│   │   ├── 音频基础概念
│   │   │   ├── 采样率（44100/48000 Hz）
│   │   │   ├── 采样格式（S16/F32）
│   │   │   ├── 声道数（单声道/立体声）
│   │   │   └── 缓冲区大小
│   │   ├── 音频播放流程
│   │   │   ├── 初始化音频设备
│   │   │   ├── SDL请求音频数据（回调）
│   │   │   ├── 应用程序填充数据
│   │   │   └── SDL播放音频
│   │   ├── 音频回调机制
│   │   │   ├── 回调函数原型
│   │   │   ├── SDL_memset()清零
│   │   │   ├── 解码音频数据
│   │   │   └── SDL_MixAudioFormat()混音
│   │   └── 核心API
│   │       ├── SDL_AudioSpec（音频规格）
│   │       ├── SDL_OpenAudioDevice()
│   │       ├── SDL_PauseAudioDevice()
│   │       └── SDL_CloseAudioDevice()
│   │
│   ├── 线程管理
│   │   ├── SDL线程模型
│   │   │   ├── SDL_CreateThread()
│   │   │   ├── SDL_WaitThread()
│   │   │   └── SDL_DetachThread()
│   │   ├── 线程同步机制
│   │   │   ├── 互斥锁（Mutex）
│   │   │   │   ├── SDL_CreateMutex()
│   │   │   │   ├── SDL_LockMutex()
│   │   │   │   └── SDL_UnlockMutex()
│   │   │   └── 条件变量（Cond）
│   │   │       ├── SDL_CreateCond()
│   │   │       ├── SDL_CondWait()
│   │   │       ├── SDL_CondSignal()
│   │   │       └── SDL_CondBroadcast()
│   │   └── 生产者-消费者模式
│   │       ├── PacketQueue结构
│   │       ├── packet_queue_init()
│   │       ├── packet_queue_put()
│   │       └── packet_queue_get()
│   │
│   ├── 定时器原理
│   │   ├── 时间函数
│   │   │   ├── SDL_GetTicks()（毫秒）
│   │   │   ├── SDL_GetPerformanceCounter()（高精度）
│   │   │   └── SDL_Delay()（延迟）
│   │   ├── SDL定时器
│   │   │   ├── SDL_AddTimer()
│   │   │   └── SDL_RemoveTimer()
│   │   └── 高精度定时策略
│   │       ├── 粗略延迟（SDL_Delay）
│   │       └── 忙等待精确延迟
│   │
│   └── 事件系统
│       ├── 事件类型
│       │   ├── SDL_QUIT（退出）
│       │   ├── SDL_KEYDOWN/UP（键盘）
│       │   ├── SDL_MOUSEMOTION（鼠标移动）
│       │   └── SDL_MOUSEBUTTONDOWN/UP（鼠标点击）
│       ├── 事件处理
│       │   └── SDL_PollEvent()轮询
│       └── 自定义事件
│           ├── SDL_USEREVENT
│           └── SDL_PushEvent()
│
├── 四、音视频同步原理
│   ├── 同步策略对比
│   │   ├── 音频同步（最常用）
│   │   ├── 视频同步
│   │   └── 外部时钟
│   ├── 音频时钟为主
│   │   ├── 音频自然播放
│   │   ├── 视频根据音频调整
│   │   └── 视频超前则等待
│   └── 同步实现
│       ├── 获取音频时钟
│       ├── 计算时间差
│       ├── 调整视频延迟
│       └── schedule_refresh()
│
├── 五、项目架构
│   ├── 文件结构
│   │   ├── MedieaPlayer.pro（项目配置）
│   │   ├── main.cpp（入口）
│   │   ├── playerdialog.h/cpp（主窗口）
│   │   ├── videoplayer.h/cpp（播放核心）
│   │   ├── PacketQueue.h/cpp（数据队列）
│   │   └── opengl/（硬件渲染）
│   └── 类关系
│       ├── PlayerDialog（主窗口）
│       ├── VideoPlayer（播放线程）
│       ├── VideoState（状态结构）
│       ├── PacketQueue（队列）
│       └── MyOpenGLWidget（渲染）
│
├── 六、核心模块详解
│   ├── VideoPlayer类
│   │   ├── 继承QThread
│   │   ├── 多线程架构
│   │   │   ├── 读取线程
│   │   │   ├── 视频解码线程
│   │   │   └── 音频回调
│   │   └── 核心方法
│   │       ├── setFileName()
│   │       ├── play()/pause()/stop()
│   │       ├── seek()
│   │       └── setVolume()
│   ├── PacketQueue类
│   │   ├── 线程安全队列
│   │   ├── SDL_mutex互斥锁
│   │   ├── SDL_cond条件变量
│   │   └── 生产者-消费者模式
│   └── PlayerDialog类
│       ├── 用户界面
│       ├── 进度条拖拽
│       ├── 音量控制
│       └── 键盘快捷键
│
├── 七、关键技术实现
│   ├── 网络流媒体支持
│   │   ├── HTTP/HTTPS
│   │   ├── HLS（M3U8/TS）
│   │   ├── RTMP
│   │   └── RTSP
│   ├── Seek功能
│   │   ├── av_seek_frame()
│   │   ├── 清空队列
│   │   ├── FLUSH_DATA标记
│   │   └── 重置时钟
│   └── HLS优化
│       ├── live_start_index=0
│       ├── seekable=1
│       ├── buffer_size=65536
│       └── max_delay=500000
│
└── 八、编译与运行
    ├── 依赖库
    │   ├── Qt 5.12.11+
    │   ├── FFmpeg 4.2.2
    │   └── SDL2 2.0.10
    ├── 编译步骤
    │   ├── qmake
    │   └── make
    └── 运行要求
        └── DLL文件
            ├── avcodec-58.dll
            ├── avformat-58.dll
            ├── avutil-56.dll
            └── SDL2.dll
```

## 样式要求

1. **颜色方案**:
   - 中心主题：深蓝色 (#1E3A5F)
   - FFmpeg模块：橙色 (#FF6B35)
   - SDL2模块：绿色 (#2ECC71)
   - 项目架构：紫色 (#9B59B6)
   - 关键实现：红色 (#E74C3C)

2. **图标使用**:
   - 技术栈：💻
   - FFmpeg：🎬
   - SDL2：🔊
   - 编解码：⚙️
   - 网络流：🌐
   - 同步：⏱️
   - 架构：🏗️

3. **代码标注**:
   - 在关键节点添加代码片段注释
   - 使用等宽字体显示API名称
   - 重要参数用粗体标注

4. **连接线样式**:
   - 一级分支：粗实线
   - 二级分支：细实线
   - 三级及以上：虚线

## 导出格式

请导出为以下格式：
1. XMind 文件 (.xmind)
2. PNG 图片（高清，300dpi）
3. PDF 文档
