# -*- coding: utf-8 -*-
import re

f = r"C:\Users\DELL\Desktop\面试准备\八股\0515模拟面总结.md"
with open(f, 'r', encoding='utf-8') as fh:
    c = fh.read()

fills = {}

fills["## 2. 项目中用的什么编解码"] = """
视频编码用H.264——软件编码走libx264，硬件编码做了三层回退：先尝试NVENC（NVIDIA显卡），不行换AMF（AMD显卡），都不可用回退libx264软件编码兜底，保证任何机器上都能跑。libx264的预设选superfast、调优zerolatency，GOP设15帧、max_b_frames=0完全关B帧，码率8Mbps。音频编码用AAC——FFmpeg原生AAC编码器，采样率44100Hz、立体声、64kbps，采集端PCM S16LE交错格式过swr_convert转成FLTP平面浮点格式再送编码器。封装格式FLV，通过RTMP推到nginx-rtmp服务器。选H.264+AAC+FLV组合因为这是RTMP直播的事实标准，CDN支持最广、播放器兼容性最好。

视频编码：**H.264 (AVC)**，通过FFmpeg的libx264软件编码器实现，同时也支持NVIDIA NVENC和AMD AMF硬件编码器做GPU加速。

音频编码：**AAC (Advanced Audio Coding)**，通过FFmpeg原生AAC编码器，64kbps比特率，44100Hz采样率，立体声FLTP（Planar Float）采样格式。

封装格式：**FLV**容器通过**RTMP**协议推流到nginx-rtmp服务器。

选择H.264+AAC+FLV组合的原因：这是RTMP直播的事实标准组合，CDN支持最广泛，浏览器和播放器兼容性最好。
"""

fills["## 15. YUV三个分量分别代表什么"] = """
- **Y（Luma，亮度）**：表示图像的明暗程度，即灰度信息。Y分量决定图像的轮廓和细节，是人眼感知图像质量的核心
- **U（Cb，蓝色色度偏差）**：表示蓝色分量与亮度的差值（Cb = Blue - Y），反映蓝-黄色轴上的颜色信息
- **V（Cr，红色色度偏差）**：表示红色分量与亮度的差值（Cr = Red - Y），反映红-青色轴上的颜色信息

可以理解为：Y分量画出一张黑白照片，U和V分量给这张照片"上色"。
"""

fills["## 22. 项目功能介绍"] = """
Whiteboard是一个**基于浏览器的实时协作白板系统**。主要功能：

- **41种图形工具**：基础图形（矩形/圆形/直线/三角形/星形/菱形/多边形等）、箭头系统（单向/双向/曲线/折线拼写箭头/虚线箭头等）、流程图元素（流终结符/文档/数据库/云/延迟/预定义流程等）、UML图形（类框/角色/生命线）、批注工具（标注框/括号/高亮）
- **Connector智能连线系统**：9个锚点/形状，拖拽端点自动吸附到最近锚点，支持正交路径自动路由（A*风格避障算法），形状拖动时连线自动跟随
- **Transform变换系统**：选中图形后8锚点缩放+旋转+移动，形状特定的变换函数保证缩放后几何正确
- **实时协作**：WebSocket多用户同步，画布状态+光标位置实时广播，用户在线列表+颜色分配
- **用户系统**：邮箱注册/登录（PBKDF2-SHA256密码哈希，10万次迭代），JWT token认证（HS256，24h过期），支持匿名访客模式
- **房间系统**：创建/加入房间，房间ID分享链接，房间内独立画布
- **画布操作**：无限画布（Space+拖拽平移，Ctrl+滚轮缩放0.1x-5x），点阵网格背景，Ctrl+Z撤销，导出PNG/JPG
- **拖拽创建**：侧边栏图形可直接拖拽到画布上创建（drag-and-drop）
"""

fills["## 24. Whiteboard技术难点"] = """
**1. Connector智能连线系统**：这是整个项目最复杂的部分。连线需要在9个锚点中自动选择最优锚点，支持端点拖拽重新吸附，支持正交路径自动路由（计算1折/2折候选路径，通过碰撞检测评分选择最优路径），且当被连线形状发生变换时，所有附着连线需要实时重新计算锚点和路径。难点在于处理大量多边形碰撞检测的性能优化——使用AABB包围盒做快速初筛，只在候选锚点附近做精确检测。

**2. Transform变换的几何正确性**：Konva的Transformer操作的是渲染层坐标（scaled/rotated），但存储层使用AABB points模型。需要编写8个形状特定的变换函数（computeTransformedPoints），将Konva的scale/rotation/position正确还原为未变换的AABB坐标。旋转后的矩形不能简单取最小包围盒——需要保留旋转角并正确还原四个顶点。

**3. 实时同步的一致性**：采用乐观更新策略——本地先绘制，再广播。这带来三个问题：(a)并发编辑冲突——两个用户同时操作同一图形；(b)网络断开重连后的状态恢复；(c)消息顺序保证。解决方式：每个操作携带userId，同一用户的操作时间序由时间戳保证；重连时发送request_sync请求全量状态恢复。

**4. 41种图形工具的扩展性**：如果采用switch-case派发，每种操作（draw/transform/render/export）都需要修改N处代码。解决方案是Shape Registry模式——每种图形自注册，携带renderer/updatePoints/transform/defaultStyle等全部元信息，新增图形只需添加一个文件导入即可，不需要修改任何核心代码。

**5. WebSocket重连机制**：网络断开后自动重连（2秒间隔），重连成功后自动重发join_room恢复房间状态。难点在于重连期间用户可能继续在本地操作——需要缓存这些操作并在重连后重放。
"""

fills["## 26. 方案是怎么来的"] = """
方案来源三个渠道：
1. **竞品分析**：参考Excalidraw、Miro、Draw.io等成熟白板产品的交互设计和功能体系
2. **设计文档驱动**：每个大版本都有完整的设计spec（在docs/superpowers/specs/目录下），包含功能目标、技术方案、接口定义和验收标准
3. **迭代验证**：每个版本完成后写implementation review，对比计划与实际差异，总结教训指导下一版本
"""

fills["## 27. 开发中遇到的困难"] = """
- **transform卸载时序问题**：点击空白区域时应该失焦（卸载Transformer），但如果onMouseDown和Transformer的onClick事件顺序不对，会导致"点一下空白处选中消失了但图形没有失焦"。解决方式：在Stage的onMouseDown中对点击空白处做击中测试，如果没有命中任何图形就显式setSelectedId(null)

- **connector锚点吸附的抖动**：拖动连线端点时，每帧都在重新计算最近的锚点。当鼠标在24px临界位置时，两个等距锚点之间会快速切换（抖动）。解决：加入迟滞逻辑——一旦吸附到某个锚点，除非鼠标离开该锚点缓冲区域（28px而非24px），否则保持吸附

- **WebSocket连接建立时序**：用户登录后需要先连接WebSocket再发送join_room，但如果网络延迟导致WebSocket尚未完全open就发送消息会失败。解决：在SyncManager中维护pending队列，WebSocket的onopen回调中自动发送排队的消息
"""

fills["## 28. 大版本迭代"] = """
**v1.0 (MVP)**：基础画布 + 5种图形（矩形/圆形/线条/箭头/画笔）+ 房间 + WebSocket同步。验证协作白板的核心可行性。

**v2.0**：10种图形 + Transform变换系统（8锚点缩放+旋转+移动）+ Sidebar侧边栏布局 + Docker部署。白板开始具备可用性。

**v3.0**：扩展到41种图形 + Shape Registry模式重构（消灭switch-case）+ 无限画布（Space+拖拽平移，Ctrl+滚轮缩放）+ 蓝色主题 + 用户系统（注册/登录/JWT/访客模式）+ 导出PNG/JPG。白板功能趋于完整。

**v4.0（规划中）**：数据库持久化（PostgreSQL）+ 云部署 + 权限系统 + Redo支持 + 模板系统。
"""

fills["## 31. 项目交接与文档"] = """
当项目需要交接给其他人时，通过以下方式保证可接手性：

- **CLAUDE.md**：项目根目录下的项目说明文件，记录了技术栈、目录结构、启动命令、编码规范等关键信息。使用/init命令可以自动生成
- **设计文档**：每个大版本有完整的设计spec（在docs/superpowers/specs/），记录了方案设计、技术决策和当时的权衡考虑
- **实现计划和review**：plans目录记录每次实现的具体步骤，review总结记录了计划与实际差异
- **工作日志**：docs下的work-log按日期记录了每天的改动摘要

一个新开发者只需按顺序阅读：README → CLAUDE.md → 最新版本的设计spec → 最新版本的review总结，就能在半天内理解项目全貌。
"""

fills["## 32. AI产出与预期不符的处理"] = """
当AI生成的代码不符合预期时，通常的排查和修复流程：

1. **检查spec是否明确**：80%的问题是因为需求描述不够精确。重新审视提示词，补充接口签名、边界条件、异常处理要求
2. **缩小范围重试**：将大任务拆分为更小的独立子任务，让AI聚焦在单一问题上
3. **提供反例**：不只告诉AI"要什么"，也告诉它"不要什么"——给出不符合预期的输出样例，解释为什么不对
4. **验证-修正循环**：AI产出后立即在浏览器/编译器中验证，发现问题后把错误信息反馈给AI（AI能从错误信息中学习）
5. **手动修复+分析根因**：如果AI多次无法产出正确结果，手动修复后把正确代码和错误代码做对比，分析是理解问题还是技术方案问题
"""

fills["## 37. 设计模式及项目应用"] = """
| 设计模式 | 项目中的应用 |
|----------|------------|
| **单例模式** | 服务端TcpKernel::GetInstance()，使用C++11局部静态变量（Meyer's Singleton），线程安全 |
| **中介者模式** | INetMediator -> TcpClientMediator，解耦网络层与业务层，原始数据->QByteArray->Qt信号 |
| **观察者模式** | Qt信号槽，采集线程通过SIG_sendVideoFrame信号通知编码线程 |
| **策略模式** | INet抽象接口 -> TcpClient/TcpServer/UdpNet多态切换，上层代码无需修改 |
| **生产者-消费者** | 服务端线程池（pthread_cond_t条件变量），编码线程有界队列（QMutex） |
| **适配器模式** | MyMap<K,V>封装std::map+pthread_mutex_t，提供线程安全接口 |
| **工厂方法模式** | Whiteboard的Shape Registry模式，每种图形自注册创建 |
"""

fills["## 40. 智能指针"] = """
| 智能指针 | 所有权 | 核心机制 |
|----------|--------|----------|
| `unique_ptr` | 独占所有权 | 禁止拷贝（=delete），只支持移动语义。无额外开销（大小=裸指针） |
| `shared_ptr` | 共享所有权 | 引用计数（控制块含强引用计数+弱引用计数），引用计数归零时delete对象 |
| `weak_ptr` | 无所有权/观察者 | 不增加强引用计数，配合shared_ptr使用。通过lock()获取临时shared_ptr来访问 |

**make_shared vs new**：
- `make_shared<T>(args)`：一次内存分配（对象+控制块连续存储），缓存友好，异常安全
- `new shared_ptr<T>(new T(args))`：两次内存分配，对象和控制块分离。且如果第二个new失败会导致内存泄漏（C++17前）
- **结论**：始终优先使用make_shared，除非需要自定义deleter或从已有的裸指针构造

**shared_ptr的线程安全性**：
- 引用计数增减是**原子操作**，多线程拷贝/销毁同一个shared_ptr是安全的
- 但访问shared_ptr**管理的对象**不是线程安全的，仍需互斥锁等同步机制
- 同一个shared_ptr对象被多个线程同时写入（如赋值）不是线程安全的（涉及修改shared_ptr本身而非引用计数）
"""

fills["## 47. Release vs Debug"] = """
| 维度 | Debug | Release |
|------|-------|---------|
| 优化 | 关闭（/Od） | 开启（/O2或/O3） |
| 符号信息 | 完整调试符号(.pdb) | 可选剥离 |
| 运行时检查 | 栈溢出检查、未初始化变量警告 | 无额外检查 |
| 断言 | assert有效 | assert编译为no-op |
| 内联 | 不内联 | 积极内联小函数 |
| 迭代器 | 边界检查(checked iterators) | 无检查 |
| 二进制大小 | 大(符号+未优化代码) | 小(优化+LTCG) |
| 性能 | 慢(可能差10-100倍) | 全速 |
"""

fills["## 49. 虚函数实现多态的原理"] = """
1. 包含虚函数的类，编译器为其生成一个**虚函数表（vtable）**——该类所有虚函数地址的数组，存储在只读数据段（.rodata）
2. 该类的每个对象包含一个隐藏成员——**虚函数指针（vptr）**，指向该类的vtable。vptr通常位于对象首部（取决于ABI）
3. 构造函数中：编译器在构造函数开头插入代码，将vptr设置为指向当前类的vtable（祖宗->父->子，逐层覆盖vptr）
4. 虚函数调用时：`pBase->virtualFunc()` -> 通过vptr找到vtable -> 在vtable中查找virtualFunc的偏移 -> 取出函数地址 -> 调用

**vtable属于类**（一个类一个vtable），**vptr属于对象**（每个对象一个vptr）。vptr在构造函数中自动初始化，程序员无需干预。
"""

fills["## 51. HTTP几个版本的迭代"] = """
| 版本 | 关键特性 |
|------|----------|
| HTTP/0.9 | 仅GET请求，纯文本，无Header，响应仅为HTML |
| HTTP/1.0 | 引入Header、POST/HEAD方法、状态码、Content-Type。每次请求新建TCP连接（短连接） |
| HTTP/1.1 | **持久连接**（Connection: keep-alive，默认复用TCP）、**管道化**（Pipelining，不等待响应即可发下一个请求，但响应必须按序返回->Head-of-Line阻塞）、Host头（虚拟主机）、分块传输编码（Chunked）、缓存控制（Cache-Control） |
| HTTP/2 | **二进制帧层**、**多路复用**（单TCP连接并发多流，解决HOL阻塞）、头部压缩（HPACK）、服务器推送（Server Push）、流优先级。但底层TCP仍存在TCP级HOL阻塞——一个丢包阻塞所有流 |
| HTTP/3 | **基于QUIC（UDP）**而非TCP、连接迁移（切换网络连接不中断）、0-RTT建连（更快）、完全消除TCP级HOL阻塞（QUIC使用独立流）。内置TLS 1.3加密 |
"""

fills["## 54. 四次挥手丢包场景"] = """
- **第1次FIN丢失**：客户端重传FIN（重传超时机制）
- **第2次ACK丢失**：服务器端的ACK丢失，客户端收不到ACK会重传FIN（因为FIN_WAIT_1有重传计时器）
- **第3次FIN丢失**：服务器的FIN丢失，服务器重传FIN（LAST_ACK状态有超时重传）。客户端在FIN_WAIT_2状态没有超时限制（可无限等待），但实际实现通常有超时（Linux默认的tcp_fin_timeout为60秒）
- **第4次ACK丢失**：客户端发送的最后一个ACK丢失。客户端已进入TIME_WAIT，服务器在LAST_ACK状态收不到ACK会**重传FIN**。客户端在TIME_WAIT中收到重传的FIN后，**重置2MSL计时器并重发ACK**，直到服务器收到ACK正常关闭
"""

fills["## 60. OpenSpec"] = """
OpenSpec是一种AI辅助开发的规范文档格式，用于定义项目的接口、数据结构和行为约定。它通过结构化的规范文件，让AI和开发者都能理解项目的契约边界。与传统的API文档不同，OpenSpec更强调机器可读性和AI友好——AI可以直接根据spec生成符合接口约定的代码，而无需从自然语言文档中推测。可以类比为OpenAPI（REST接口描述）在更广泛软件工程领域的扩展。
"""

# Apply fills
for heading, content in fills.items():
    idx = c.find(heading)
    if idx >= 0:
        end = c.find('\n', idx) + 1
        # Find next section
        next_sec = c.find('\n## ', end)
        if next_sec < 0:
            next_sec = len(c)
        existing = c[end:next_sec].strip()
        if not existing:
            c = c[:end] + '\n' + content.strip() + '\n' + c[end:]

# Special fixes for sections that need content added (not empty but need supplement)
# Q2.5 项目全链路 - add summary paragraph
old = "## 2.5. 你们的音视频项目全链路是怎么跑的\n\n"
new = "## 2.5. 你们的音视频项目全链路是怎么跑的\n\n整个推流直播平台的数据流可以分为三条并行链路：\n\n**链路一：音视频数据流（核心通路）**——从采集到播放共六步。第一步采集：桌面QScreen::grabWindow截屏、摄像头OpenCV VideoCapture采集、音频Qt QAudioInput同时读取系统立体声混音和麦克风两路在Audio_Mixer中做S16域混音。第二步编码：视频H.264硬编优先软编兜底，音频AAC 64kbps。第三步封装推流：FFmpeg将H.264+AAC封装成FLV通过avio_open2走RTMP推到nginx。第四步分发：nginx-rtmp收到推流立即转发给所有拉流端。第五步拉流解码：MediaPlayer通过avformat_open_input打开RTMP地址解封装分包进PacketQueue，视频解码线程和SDL音频回调各自消费。第六步渲染：视频YUV转RGB32上OpenGL纹理，音频SDL混音输出。\n\n**链路二：业务信令流**——所有客户端通过TCP 8000端口与C++ Server通信，自定义二进制协议（4字节长度+结构体），17种协议类型处理注册、登录、创建房间、房间列表、心跳等业务逻辑。\n\n**链路三：进程启动流**——Launcher登录后通过QProcess::startDetached启动子进程（主播->VideoRecoder，观众->MediaPlayer），token和userId通过环境变量传递。\n\n"
c = c.replace(old, new)

# Q13 YUV转RGB - add project usage paragraph
old = "## 13. 格式转换：YUV转RGB\n\n"
new = "## 13. 格式转换：YUV转RGB\n\n在我们的MediaPlayer中，FFmpeg解码出来的视频帧是YUV420P三平面格式——不能直接给屏幕渲染，需要转成RGB。做法是video_thread初始化时创建一个SwsContext（颜色空间转换器），每解码出一帧就调sws_scale把YUV420P转成RGB32。转换后的像素数据构造QImage，再上传到OpenGL纹理渲染到屏幕。SwsContext只创建一次后面每帧复用。VideoRecoder编码端方向相反——采集到的RGB24通过sws_scale转成YUV420P再送入H.264编码器。\n\n"
c = c.replace(old, new)

# Q18 后端服务器架构 - add summary
old = "## 18. 后端服务器架构\n\n"
new = "## 18. 后端服务器架构\n\n服务端纯C++11，不用Qt——Linux服务器不需要GUI。整体分三层：底层网络层是epoll事件驱动+线程池异步处理，EPOLLONESHOT保证同一socket同一时刻只被一个线程处理；中间调度层是TcpKernel单例，核心设计用了函数指针数组m_NetPackMap做O(1)协议分发；上层业务层CLogic处理十几种协议。数据库MySQL两张表t_UserData和t_RoomInfo启动自动建表。nginx-rtmp作为独立流媒体服务器跑1935端口只做流转发不参与业务逻辑。\n\n"
c = c.replace(old, new)

# Q20 多线程 - add project usage
old = "## 20. 多线程操作详解\n\n"
new = "## 20. 多线程操作详解\n\n这个项目从客户端到服务端是多层次的线程模型。VideoRecoder端四个线程：主线程Qt事件循环+25fps定时器做桌面和摄像头采集、编码线程SaveVideoFileThread（QThread）做H.264+AAC编码+FLV封装+RTMP推流、网络线程_beginthreadex做TCP信令收发、混音线程readyRead事件驱动做S16域双设备混音。MediaPlayer端也有四个：主线程Qt事件循环+av_read_frame解封装、video_thread（SDL_CreateThread）视频解码+音画同步、SDL音频回调线程AAC解码+swr_convert重采样+混音输出、UI线程OpenGL纹理渲染。服务端四个：epoll主线程、线程池工作线程（min=10 max=200自动扩缩容）、Manager管理线程、心跳检查线程。跨线程同步用了QMutex保护帧队列、Qt::QueuedConnection跨线程信号投递、SDL_mutex+SDL_cond保护PacketQueue、pthread_mutex_t保护MyMap和数据库、EPOLLONESHOT保证socket独占。\n\n"
c = c.replace(old, new)

# Q21.5 推流拉流 - add summary
old = "## 21.5. 你项目的推流和拉流完整流程\n\n"
new = "## 21.5. 你项目的推流和拉流完整流程\n\n**推流端**：主播在Launcher登录选主播角色->QProcess::startDetached启动VideoRecoder传token和userId环境变量->连C++ Server做二次鉴权->点Go Live发CREATE_ROOM_RQ->Server在MySQL建房间生成stream_key返回->客户端拼推流地址rtmp://server:1935/live/stream_key->SaveVideoFileThread::run里调avio_open2打开RTMP连接->编码主循环从视频队列取QImage编码H.264、从音频队列取FLTP数据编码AAC->每编完一帧调av_interleaved_write_frame推流->每10秒心跳保活->停止时发CLOSE_ROOM_RQ。\n\n**拉流端**：观众选观众角色->Launcher启动MediaPlayer->鉴权后自动拉房间列表（Server查MySQL所有status=1房间拼JSON）->双击房间取stream_key拼拉流地址->VideoPlayer::run调avformat_open_input打开RTMP->av_read_frame循环读包->按stream_index分发videoq和audioq->video_thread取H.264包解码YUV转RGB上OpenGL渲染->SDL音频回调取AAC包解码S16混音播放->每10秒心跳+JOIN_ROOM_RQ更新观看数。nginx-rtmp在中间只做透明流转发不转码。\n\n"
c = c.replace(old, new)

with open(f, 'w', encoding='utf-8') as fh:
    fh.write(c)

# Verify
with open(f, 'r', encoding='utf-8') as fh:
    c2 = fh.read()
sections = re.findall(r'## (\d+[\.\d]*)\s+(.+?)\n', c2)
missing = 0
for num, title in sections:
    pattern = rf'## {re.escape(num)}\.?\s+{re.escape(title)}\n(.*?)(?=\n## |$)'
    m = re.search(pattern, c2, re.DOTALL)
    content_len = len(m.group(1).strip()) if m else 0
    if content_len < 20:
        print(f'  STILL MISSING: {num}. {title}')
        missing += 1
if missing == 0:
    print(f'All {len(sections)} sections have content!')
print(f'Total lines: {len(c2.splitlines())}')
