# 直播平台服务器设计文档

> 版本：v2（经服务器/主播端/观众端三方工程师交叉审查修订）

## 项目目标

为现有 VideoRecoder（录屏/推流）和 MediaPlayer（拉流播放）项目部署服务器，添加用户注册/登录、主播端/观众端角色管理、直播间管理功能。

## 技术决策

- **服务器语言**：C++（Linux epoll + 线程池）
- **数据库**：MySQL
- **客户端网络**：Qt + 复用服务器资料的 netapi 库（TcpClientMediator）
- **音视频流转**：nginx-rtmp（推流/拉流走 RTMP，不经过业务服务器）
- **部署目标**：Linux VM (192.168.136.137)
- **客户端架构**：新建统一 Launcher 登录后通过 QProcess 启动子应用

## 系统架构

```
                    Linux VM (192.168.136.137)
                    ┌──────────────────────────────────┐
                    │  MySQL                           │
                    │  · t_UserData (用户)              │
                    │  · t_RoomInfo (直播间)            │
                    │       │                           │
                    │  C++ Epoll Server (:8000)         │
                    │  · 注册 / 登录 / Token 验证        │
                    │  · 房间管理（创建/列表/关闭）       │
                    │  · 心跳检测 / 在线状态清理         │
                    │       │                           │
                    │  nginx-rtmp (:1935)               │
                    │  · RTMP 推流接收                  │
                    │  · RTMP 拉流分发                  │
                    └──────────────┬───────────────────┘
                                   │
              TCP 二进制协议       │      RTMP 音视频流
          ┌────────────────────────┼───────────────────┐
          │                        │                   │
    ┌─────┴─────┐          ┌──────┴──────┐      ┌─────┴─────┐
    │  Launcher  │          │VideoRecoder │      │MediaPlayer │
    │ 登录/注册   │──QProcess→│  主播端      │      │  观众端     │
    └───────────┘          └─────────────┘      └───────────┘
```

**三条通道：**
- **TCP 业务通道** (port 8000)：客户端 ↔ C++ 服务器，二进制协议（4字节头 + 结构体）
- **RTMP 推流通道** (port 1935)：VideoRecoder → nginx-rtmp
- **RTMP 拉流通道** (port 1935)：nginx-rtmp → MediaPlayer

### 目录结构

```
D:\Video\
├── VideoRecoder/          (已有，需改)
├── videoplayer/           (已有，需改)
├── Launcher/              (新建 Qt 项目)
├── server/                (新建 C++ epoll 服务器)
│   ├── include/
│   └── src/
├── common/                (共享协议头文件)
│   └── packdef.h
└── docs/
    └── future-features.md
```

---

## 数据库设计

### t_UserData（用户表）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT PK AUTO_INCREMENT | 用户ID |
| tel | VARCHAR(40) UNIQUE NOT NULL | 手机号（登录凭证） |
| name | VARCHAR(40) NOT NULL | 昵称 |
| password | VARCHAR(40) NOT NULL | 密码 |
| role | TINYINT DEFAULT 0 | 0=观众, 1=主播 |
| created_at | DATETIME DEFAULT NOW() | 注册时间 |

### t_RoomInfo（直播间表）

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT PK AUTO_INCREMENT | 房间ID |
| user_id | INT UNIQUE | 主播用户ID（唯一约束，防止并发重复创建） |
| title | VARCHAR(64) | 直播间标题 |
| stream_key | VARCHAR(64) | 推流密钥 |
| status | TINYINT DEFAULT 0 | 0=离线, 1=直播中 |
| viewer_count | INT DEFAULT 0 | 观众数 |
| created_at | DATETIME DEFAULT NOW() | 创建时间 |

> **关键约束：** `user_id` 设 UNIQUE，防止并发 CreateRoom 竞态。重复插入由数据库层拒绝，应用层捕获错误返回。

### 启动建表

`TCPKernel::Open()` 执行：
```sql
CREATE TABLE IF NOT EXISTS t_UserData (...);
CREATE TABLE IF NOT EXISTS t_RoomInfo (...);
UPDATE t_RoomInfo SET status = 0 WHERE status = 1;  -- 清理残留状态
```

---

## 协议设计

协议格式：4字节包长度 + 结构体数据。客户端和服务器端 **共享同一份 `packdef.h`**，统一使用 `#pragma pack(push, 1)` 确保对齐一致。

### 包大小安全校验

服务器 `recv_task` 读取 `nPackSize` 后：
```cpp
if (nPackSize <= 0 || nPackSize > _DEF_BUFFER) {
    close(fd);
    break;
}
```
防止恶意客户端发送超大值导致 OOM。

### 协议清单

| 协议号 | 宏定义 | 方向 | 用途 |
|--------|--------|------|------|
| 10000 | `_DEF_PACK_REGISTER_RQ` | C→S | 注册请求 |
| 10001 | `_DEF_PACK_REGISTER_RS` | S→C | 注册结果 |
| 10002 | `_DEF_PACK_LOGIN_RQ` | C→S | 登录请求 |
| 10003 | `_DEF_PACK_LOGIN_RS` | S→C | 登录结果（含 token） |
| 10004 | `_DEF_PACK_CREATE_ROOM_RQ` | C→S | 创建直播间 |
| 10005 | `_DEF_PACK_CREATE_ROOM_RS` | S→C | 返回流密钥 |
| 10006 | `_DEF_PACK_ROOM_LIST_RQ` | C→S | 请求直播间列表 |
| 10007 | `_DEF_PACK_ROOM_LIST_RS` | S→C | 返回列表 JSON（content[4096]） |
| 10008 | `_DEF_PACK_CLOSE_ROOM_RQ` | C→S | 主播关闭直播间 |
| 10009 | `_DEF_PACK_CLOSE_ROOM_RS` | S→C | 关闭结果 |
| 10010 | `_DEF_PACK_JOIN_ROOM_RQ` | C→S | 观众进入直播间 |
| 10011 | `_DEF_PACK_LEAVE_ROOM_RQ` | C→S | 观众离开直播间 |
| 10012 | `_DEF_PACK_HEARTBEAT_RQ` | C→S | 心跳保活 |
| 10013 | `_DEF_PACK_HEARTBEAT_RS` | S→C | 心跳应答 |
| 10014 | `_DEF_PACK_AUTH_TOKEN_RQ` | C→S | 子应用 Token 验证 |

### 结构体定义

```cpp
// ===================== 注册（不变） =====================
struct STRU_REGISTER_RQ {
    PackType type;           // 10000
    char tel[_MAX_SIZE];     // 手机号
    char name[_MAX_SIZE];    // 昵称
    char password[_MAX_SIZE];// 密码
};
struct STRU_REGISTER_RS {
    PackType type;           // 10001
    int result;              // user_is_exist=0, register_success=1
};

// ===================== 登录（增加 token） =====================
struct STRU_LOGIN_RQ {
    PackType type;           // 10002
    char tel[_MAX_SIZE];
    char password[_MAX_SIZE];
};
struct STRU_LOGIN_RS {
    PackType type;           // 10003
    int result;              // user_not_exist=0, password_error=1, login_success=2
    int userid;
    int role;                // 0=观众, 1=主播
    char token[64];          // session token（UUID格式），子应用用此验证身份
};

// ===================== Token 验证（新增） =====================
struct STRU_AUTH_TOKEN_RQ {
    PackType type;           // 10014
    char token[64];
};
// 复用 STRU_LOGIN_RS 作为响应（含 userid + role）

// ===================== 创建房间 =====================
struct STRU_CREATE_ROOM_RQ {
    PackType type;           // 10004
    int userid;
    char title[64];
};
struct STRU_CREATE_ROOM_RS {
    PackType type;           // 10005
    int result;
    int room_id;
    char stream_key[32];     // "live_a3f8c2b1"
};

// ===================== 房间列表 =====================
struct STRU_ROOM_LIST_RQ {
    PackType type;           // 10006
};
struct STRU_ROOM_LIST_RS {
    PackType type;           // 10007
    int room_count;
    int json_length;         // JSON 实际字节数（新增）
    char content[4096];      // JSON 数组（从 1024 扩大到 4096）
};

// ===================== 关闭房间 =====================
struct STRU_CLOSE_ROOM_RQ {
    PackType type;           // 10008
    int userid;
    int room_id;
};
struct STRU_CLOSE_ROOM_RS {
    PackType type;           // 10009
    int result;
};

// ===================== 观众进入/离开 =====================
struct STRU_JOIN_ROOM_RQ {
    PackType type;           // 10010
    int userid;
    int room_id;
};
struct STRU_LEAVE_ROOM_RQ {
    PackType type;           // 10011
    int userid;
    int room_id;
};

// ===================== 心跳 =====================
struct STRU_HEARTBEAT_RQ {
    PackType type;           // 10012
    int userid;
};
struct STRU_HEARTBEAT_RS {
    PackType type;           // 10013
    int server_time;         // 服务器时间戳
};
```

### 协议设计要点

1. **房间列表不返回 stream_key**（观众通过服务器给的 RTMP URL 拉流，无需知道密钥）
2. **所有 struct 必须 `#pragma pack(push, 1)`**，客户端和服务器端 packdef.h 同步
3. **content 增加 `json_length` 字段**，客户端据此读取有效 JSON 长度

---

## 服务器模块设计

### 文件清单

| 文件 | 改动 |
|------|------|
| `include/packdef.h` | 新增 10004-10014 协议 + 结构体，LOGIN_RS 增加 token/role，DB 名 `LiveServer`，content 4096 |
| `include/clogic.h` | 新增 handler 声明，`map<int,int>` 改为 `MyMap<int,int>`（线程安全） |
| `include/TCPKernel.h` | 不变 |
| `include/block_epoll_net.h` | recv_task 增加 `nPackSize` 范围校验 |
| `include/Thread_pool.h` | 不变 |
| `include/Mysql.h` | 不变 |
| `src/clogic.cpp` | 实现全部 handler（见下方），setNetPackMap 注册新协议 |
| `src/TCPKernel.cpp` | Open() 增加 CREATE TABLE IF NOT EXISTS + 清理残留 status |
| `src/main.cpp` | 不变 |

### 服务端业务逻辑

**RegisterRq：**
1. 解析 STRU_REGISTER_RQ
2. `SELECT id FROM t_UserData WHERE tel = ?`
3. 已存在 → `result = user_is_exist`
4. 不存在 → `INSERT INTO t_UserData (tel, name, password, role) VALUES (...)` → `result = register_success`
5. 返回 STRU_REGISTER_RS

**LoginRq：**
1. 解析 STRU_LOGIN_RQ
2. `SELECT id, password, role FROM t_UserData WHERE tel = ?`
3. 密码比对 → 成功时生成 UUID token，`m_mapTokenToUser[token] = {userid, role}`，`m_mapIDToUserFD[userid] = clientfd`
4. 返回 STRU_LOGIN_RS { result, userid, role, token }

**AuthTokenRq（子应用验证）：**
1. 用 token 查 `m_mapTokenToUser`（用 MyMap 查）
2. 存在 → 返回 { login_success, userid, role }
3. 不存在/过期 → 返回 { user_not_exist }

**CreateRoomRq：**
1. 验证 userid 对应 socket 是否在线（查 m_mapIDToUserFD）
2. 生成 stream_key = `"live_"` + 8位随机十六进制
3. `INSERT INTO t_RoomInfo (user_id, title, stream_key, status=1)` — user_id UNIQUE 约束防并发重复
4. 插入失败（重复）→ `result = 0`
5. 插入成功 → `result = 1`，返回 STRU_CREATE_ROOM_RS { room_id, stream_key }

**RoomListRq：**
1. `SELECT id, user_id, title, viewer_count FROM t_RoomInfo WHERE status=1`
2. 拼 JSON 数组（不含 stream_key）：`[{"id":1,"title":"xxx","viewer_count":5},...]`
3. JSON 末尾保证 `\0`，`json_length = strlen(content)`

**JoinRoomRq / LeaveRoomRq：**
1. `UPDATE t_RoomInfo SET viewer_count = viewer_count + 1 WHERE id = ?`
2. `UPDATE t_RoomInfo SET viewer_count = GREATEST(viewer_count - 1, 0) WHERE id = ?`

**CloseRoomRq：**
1. `UPDATE t_RoomInfo SET status=0, viewer_count=0 WHERE id=? AND user_id=?`
2. 返回 STRU_CLOSE_ROOM_RS

**心跳 HeartbeatRq：**
1. 更新该用户的最后心跳时间
2. 返回 HeartbeatRS { server_time }

**心跳超时清理（服务器定时器线程）：**
1. 每 30 秒扫描 `m_mapIDToUserFD` + 心跳时间
2. 超过 45 秒未心跳 → 判定离线
3. `UPDATE t_RoomInfo SET status=0 WHERE user_id=? AND status=1`
4. 从 `m_mapIDToUserFD` 移除
5. `close(clientfd)`

> **注意：** 需新增一个简单的定时器线程做心跳检测，或在 epoll EventLoop 中增加定时检查逻辑。

### 线程安全措施（汇总）

| 数据结构 | 保护方式 |
|----------|---------|
| `m_mapIDToUserFD` | 改用 `MyMap<int, int>` |
| `m_mapTokenToUser` | 改用 `MyMap<string, TokenInfo>` |
| `t_RoomInfo` 并发插入 | `user_id` UNIQUE 约束 |
| `CMysql` 操作 | 已有 `pthread_mutex_t` |
| Socket recv | `EPOLLONESHOT` 保证同 socket 串行 |

---

## 客户端模块设计

### 线程安全修复（三个客户端共同遵守）

**问题：** netapi 的 `TcpClient::RecvData()` 运行在 `_beginthreadex` 创建的 Win32 线程中，`buf = new char[]` 通过 `SIG_ReadyData(char* buf)` 跨线程传递，存在内存泄露和悬空指针风险。

**修复：** 修改 `INetMediator` 信号签名为 `QByteArray`：

```cpp
// INetMediator.h
signals:
    void SIG_ReadyData(unsigned int lSendIP, const QByteArray &data);
    void SIG_disConnect();

// TcpClientMediator::DealData
void TcpClientMediator::DealData(unsigned int lSendIP, char* buf, int nlen) {
    QByteArray data(buf, nlen);  // 深拷贝
    delete[] buf;                 // 立即释放
    Q_EMIT SIG_ReadyData(lSendIP, data);
}

// 客户端槽函数必须显式 QueuedConnection
connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
        this, SLOT(slot_onReadyData(uint,QByteArray)),
        Qt::QueuedConnection);
```

### 新建项目：Launcher

```
Launcher/
├── Launcher.pro
├── main.cpp
├── logindialog.h/cpp/ui        # 登录/注册界面
├── role_select_dialog.h/cpp/ui # 角色选择界面
└── netapi/                     # 复用服务器资料 netapi（信号改为 QByteArray）
```

**工作流：**
1. 启动 → LoginDialog（手机号 + 密码登录 / 注册）
2. 登录成功 → 获取 token + userid + role
3. RoleSelectDialog → QProcess::startDetached() 启动子应用
4. 子应用通过环境变量传递敏感信息：

```cpp
QProcess proc;
QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
env.insert("LIVE_TOKEN", token);    // token 不暴露在命令行
env.insert("LIVE_SERVER", "192.168.136.137");
proc.setProcessEnvironment(env);
proc.setProgram("VideoRecoder.exe");
proc.setArguments({"--role=1"});
proc.startDetached();
```

### 改造项目：VideoRecoder

**需要修改的文件：**

| 文件 | 改动 |
|------|------|
| `recorderdialog.h/cpp` | 新增网络成员、开播按钮、状态栏 |
| `savevideofilethread.h/cpp` | 支持 RTMP URL 输出、检查 write_frame 返回值、增加 max_b_frames=0 |
| `main.cpp` | 解析环境变量 token，启动时 AuthTokenRq 验证 |

**改造要点：**

1. **启动验证**：`main()` 中读取环境变量 `LIVE_TOKEN` 和 `LIVE_SERVER`，连接服务器后发送 `AUTH_TOKEN_RQ` 验证身份
2. **开播流程**：点击"开播" → `CreateRoomRq` → 获取 stream_key → 构造 `rtmp://192.168.136.137:1935/live/<stream_key>` → 启动 SaveVideoFileThread 推流
3. **RTMP 推流修改**：
   - `av_dict_set(&opts, "timeout", "5000000", 0)` — 5秒连接超时
   - 所有编码路径显式 `c->max_b_frames = 0`
   - `write_frame()` 返回值必须检查，`ret < 0` 时停止推流 + 通知 UI
   - `avformat_alloc_output_context2` 上设置 `interrupt_callback` 加速停止
4. **推流状态栏**：显示推流时长、实时码率、连接状态指示灯
5. **下播**：点击"下播" → `CloseRoomRq` → 停止编码线程 → 等待完成（超时延长到 10 秒）
6. **心跳**：每 10 秒发送 `HEARTBEAT_RQ`，如连续 3 次未收到响应 → 提示"与服务器断开"

### 改造项目：MediaPlayer

**需要修改的文件：**

| 文件 | 改动 |
|------|------|
| `playerdialog.h/cpp` | 新增网络成员、直播间列表按钮/弹窗、信息栏 |
| `main.cpp` | 解析环境变量 token，启动时 AuthTokenRq 验证 |

**改造要点：**

1. **启动验证**：同 VideoRecoder，通过 token 验证
2. **直播间列表**：
   - 点击按钮 → `RoomListRq` → 解析 JSON → `QTableWidget` 弹窗（标题 + 观众数）
   - 双击行 → 填入 `rtmp://192.168.136.137:1935/live/<stream_key>` 播放
   - 自动 30 秒刷新（QTimer）
3. **进房/离房**：开始播放时 `JoinRoomRq`，切换直播间或停止时 `LeaveRoomRq`
4. **播放器信息栏**：显示当前直播间标题、观众数
5. **兼容性**：本地文件播放流程完全不受影响（网络功能仅在收到 server 参数时激活）
6. **心跳**：同 VideoRecoder，10 秒间隔

---

## 完整数据流

```
Launcher
  │ ① TCP: REGISTER_RQ / LOGIN_RQ
  └──────→ 服务器 ──→ MySQL
              │ ② LOGIN_RS { token, userid, role }
              │
  QProcess.startDetached(env: LIVE_TOKEN, LIVE_SERVER)
  │
  ├─→ VideoRecoder (主播)
  │     │ ③ TCP: AUTH_TOKEN_RQ { token }
  │     │ ④ TCP: CREATE_ROOM_RQ → CREATE_ROOM_RS { stream_key }
  │     │ ⑤ RTMP 推流 → nginx-rtmp (:1935)
  │     │ ⑥ TCP: HEARTBEAT_RQ (每10秒)
  │     │ ⑦ TCP: CLOSE_ROOM_RQ (下播)
  │
  └─→ MediaPlayer (观众)
        │ ③ TCP: AUTH_TOKEN_RQ { token }
        │ ④ TCP: ROOM_LIST_RQ → ROOM_LIST_RS { JSON }
        │ ⑤ TCP: JOIN_ROOM_RQ
        │ ⑥ RTMP 拉流 ← nginx-rtmp (:1935)
        │ ⑦ TCP: LEAVE_ROOM_RQ (切换/停止)
```

---

## 未来功能规划

以下功能不在 MVP 范围，记录供后续实现：

1. **关注系统**：t_Follows 表（follower_id, followed_id），上线/开播通知
2. **弹幕聊天**：服务器转发 UDP 弹幕帧到同房间观众
3. **观看人数实时广播**：服务器在 `JoinRoomRq`/`LeaveRoomRq` 后主动向房间观众广播人数变化
4. **直播回放**：nginx-rtmp 录制 FLV → 点播列表
5. **密码哈希**：前端 SHA256 → 服务器加盐存储（替换明文）
6. **Web 管理后台**
7. **推流前预览模式**：采集和推流分离，先预览再开播
8. **自适应码率**：根据观众网络动态调整
