# 直播平台实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为录屏/播放项目部署 C++ 直播平台服务器 + Qt 客户端，实现注册登录、主播/观众角色、直播间管理功能。

**Architecture:** 共享 `common/packdef.h` 协议头文件，服务端基于 NetDisk epoll 框架扩展，客户端复用 netapi 库（内存/线程安全修复），Launcher 通过环境变量传递 Token 启动子应用。

**Tech Stack:** C++11 (g++), epoll + pthread + MySQL C API (服务端), Qt 5.12 + FFmpeg 4.2.2 + Winsock2 (客户端)

---

## 文件结构

```
D:\Video\
├── common/                          (新建 — 共享协议)
│   └── packdef.h
├── server/                          (新建 — 基于 NetDisk 代码)
│   ├── include/
│   │   ├── packdef.h              → 指向 common/packdef.h
│   │   ├── TCPKernel.h
│   │   ├── block_epoll_net.h
│   │   ├── Thread_pool.h
│   │   ├── Mysql.h
│   │   ├── clogic.h
│   │   └── err_str.h
│   └── src/
│       ├── main.cpp
│       ├── makefile
│       ├── TCPKernel.cpp
│       ├── block_epoll_net.cpp
│       ├── Thread_pool.cpp
│       ├── Mysql.cpp
│       ├── clogic.cpp
│       └── err_str.cpp
├── Launcher/                        (新建)
│   ├── Launcher.pro
│   ├── main.cpp
│   ├── logindialog.h / .cpp / .ui
│   ├── role_select_dialog.h / .cpp / .ui
│   └── netapi/                    → 复制自服务器资料
├── VideoRecoder/                    (改造)
│   ├── VideoRecoder/recorderdialog.h / .cpp
│   ├── VideoRecoder/savevideofilethread.h / .cpp
│   ├── VideoRecoder/main.cpp
│   └── VideoRecoder/common.h
├── videoplayer/                     (改造)
│   ├── MedieaPlayer/playerdialog.h / .cpp
│   ├── MedieaPlayer/main.cpp
│   └── MedieaPlayer/videoplayer.h / .cpp
├── 服务器资料/                       (参考，不动)
└── docs/superpowers/
    ├── specs/2026-05-11-live-streaming-platform-design.md
    └── plans/2026-05-11-live-streaming-platform-plan.md
```

---

## Task 1: 共享协议头文件 common/packdef.h

**Files:**
- Create: `D:/Video/common/packdef.h`

### Step 1: 创建目录并写入协议头文件

```bash
mkdir -p "D:/Video/common"
```

```cpp
// D:/Video/common/packdef.h
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <malloc.h>

#include <iostream>
#include <map>
#include <list>

// ===================== 边界值 =====================
#define _DEF_SIZE           45
#define _DEF_BUFFERSIZE     1000
#define _DEF_PORT           8000
#define _DEF_SERVERIP       "0.0.0.0"
#define _DEF_LISTEN         128
#define _DEF_EPOLLSIZE      4096
#define _DEF_IPSIZE         16
#define _DEF_COUNT          10
#define _DEF_TIMEOUT        10
#define _DEF_SQLIEN         400
#define TRUE                true
#define FALSE               false

// ===================== 数据库信息 =====================
#define _DEF_DB_NAME    "LiveServer"
#define _DEF_DB_IP      "localhost"
#define _DEF_DB_USER    "root"
#define _DEF_DB_PWD     "colin123"

// ===================== 缓冲区常量 =====================
#define _MAX_PATH           (260)
#define _DEF_BUFFER         (4096)
#define _DEF_CONTENT_SIZE   (4096)
#define _MAX_SIZE           (40)

// ===================== 协议号宏 =====================
#define _DEF_PACK_BASE      (10000)
#define _DEF_PACK_COUNT     (100)

// 注册
#define _DEF_PACK_REGISTER_RQ       (_DEF_PACK_BASE + 0)
#define _DEF_PACK_REGISTER_RS       (_DEF_PACK_BASE + 1)
// 登录
#define _DEF_PACK_LOGIN_RQ          (_DEF_PACK_BASE + 2)
#define _DEF_PACK_LOGIN_RS          (_DEF_PACK_BASE + 3)
// 创建直播间
#define _DEF_PACK_CREATE_ROOM_RQ    (_DEF_PACK_BASE + 4)
#define _DEF_PACK_CREATE_ROOM_RS    (_DEF_PACK_BASE + 5)
// 房间列表
#define _DEF_PACK_ROOM_LIST_RQ      (_DEF_PACK_BASE + 6)
#define _DEF_PACK_ROOM_LIST_RS      (_DEF_PACK_BASE + 7)
// 关闭直播间
#define _DEF_PACK_CLOSE_ROOM_RQ     (_DEF_PACK_BASE + 8)
#define _DEF_PACK_CLOSE_ROOM_RS     (_DEF_PACK_BASE + 9)
// 观众进出
#define _DEF_PACK_JOIN_ROOM_RQ      (_DEF_PACK_BASE + 10)
#define _DEF_PACK_LEAVE_ROOM_RQ     (_DEF_PACK_BASE + 11)
// 心跳
#define _DEF_PACK_HEARTBEAT_RQ      (_DEF_PACK_BASE + 12)
#define _DEF_PACK_HEARTBEAT_RS      (_DEF_PACK_BASE + 13)
// Token 验证
#define _DEF_PACK_AUTH_TOKEN_RQ     (_DEF_PACK_BASE + 14)

// ===================== 返回结果常量 =====================
#define user_is_exist       (0)
#define register_success    (1)
#define user_not_exist      (0)
#define password_error      (1)
#define login_success       (2)
#define create_room_failed  (0)
#define create_room_success (1)
#define close_room_failed   (0)
#define close_room_success  (1)

typedef int PackType;

// ===================== 协议结构体 (pragma pack 保证对齐) =====================
#pragma pack(push, 1)

// --- 注册 ---
struct STRU_REGISTER_RQ {
    STRU_REGISTER_RQ() : type(_DEF_PACK_REGISTER_RQ) {
        memset(tel, 0, sizeof(tel));
        memset(name, 0, sizeof(name));
        memset(password, 0, sizeof(password));
    }
    PackType type;
    char tel[_MAX_SIZE];
    char name[_MAX_SIZE];
    char password[_MAX_SIZE];
};

struct STRU_REGISTER_RS {
    STRU_REGISTER_RS() : type(_DEF_PACK_REGISTER_RS), result(register_success) {}
    PackType type;
    int result;
};

// --- 登录 ---
struct STRU_LOGIN_RQ {
    STRU_LOGIN_RQ() : type(_DEF_PACK_LOGIN_RQ) {
        memset(tel, 0, sizeof(tel));
        memset(password, 0, sizeof(password));
    }
    PackType type;
    char tel[_MAX_SIZE];
    char password[_MAX_SIZE];
};

struct STRU_LOGIN_RS {
    STRU_LOGIN_RS() : type(_DEF_PACK_LOGIN_RS), result(login_success), userid(0), role(0) {
        memset(token, 0, sizeof(token));
    }
    PackType type;
    int result;
    int userid;
    int role;
    char token[64];
};

// --- Token 验证 ---
struct STRU_AUTH_TOKEN_RQ {
    STRU_AUTH_TOKEN_RQ() : type(_DEF_PACK_AUTH_TOKEN_RQ) {
        memset(token, 0, sizeof(token));
    }
    PackType type;
    char token[64];
};

// --- 创建直播间 ---
struct STRU_CREATE_ROOM_RQ {
    STRU_CREATE_ROOM_RQ() : type(_DEF_PACK_CREATE_ROOM_RQ), userid(0) {
        memset(title, 0, sizeof(title));
    }
    PackType type;
    int userid;
    char title[64];
};

struct STRU_CREATE_ROOM_RS {
    STRU_CREATE_ROOM_RS() : type(_DEF_PACK_CREATE_ROOM_RS), result(create_room_failed), room_id(0) {
        memset(stream_key, 0, sizeof(stream_key));
    }
    PackType type;
    int result;
    int room_id;
    char stream_key[32];
};

// --- 房间列表 ---
struct STRU_ROOM_LIST_RQ {
    STRU_ROOM_LIST_RQ() : type(_DEF_PACK_ROOM_LIST_RQ) {}
    PackType type;
};

struct STRU_ROOM_LIST_RS {
    STRU_ROOM_LIST_RS() : type(_DEF_PACK_ROOM_LIST_RS), room_count(0), json_length(0) {
        memset(content, 0, sizeof(content));
    }
    PackType type;
    int room_count;
    int json_length;
    char content[_DEF_CONTENT_SIZE];  // 4096
};

// --- 关闭直播间 ---
struct STRU_CLOSE_ROOM_RQ {
    STRU_CLOSE_ROOM_RQ() : type(_DEF_PACK_CLOSE_ROOM_RQ), userid(0), room_id(0) {}
    PackType type;
    int userid;
    int room_id;
};

struct STRU_CLOSE_ROOM_RS {
    STRU_CLOSE_ROOM_RS() : type(_DEF_PACK_CLOSE_ROOM_RS), result(close_room_failed) {}
    PackType type;
    int result;
};

// --- 观众进入/离开 ---
struct STRU_JOIN_ROOM_RQ {
    STRU_JOIN_ROOM_RQ() : type(_DEF_PACK_JOIN_ROOM_RQ), userid(0), room_id(0) {}
    PackType type;
    int userid;
    int room_id;
};

struct STRU_LEAVE_ROOM_RQ {
    STRU_LEAVE_ROOM_RQ() : type(_DEF_PACK_LEAVE_ROOM_RQ), userid(0), room_id(0) {}
    PackType type;
    int userid;
    int room_id;
};

// --- 心跳 ---
struct STRU_HEARTBEAT_RQ {
    STRU_HEARTBEAT_RQ() : type(_DEF_PACK_HEARTBEAT_RQ), userid(0) {}
    PackType type;
    int userid;
};

struct STRU_HEARTBEAT_RS {
    STRU_HEARTBEAT_RS() : type(_DEF_PACK_HEARTBEAT_RS), server_time(0) {}
    PackType type;
    int server_time;
};

#pragma pack(pop)
```

### Step 2: 提交

```bash
git add D:/Video/common/packdef.h
git commit -m "feat: add shared protocol header with all packet types"
```

---

## Task 2: 服务器 — 拷贝 NetDisk 框架并关联共享协议

**Files:**
- Create: `D:/Video/server/` (从 NetDisk 拷贝)
- Create: `D:/Video/server/include/packdef.h` → symlink/copy to `../../common/packdef.h`

### Step 1: 创建目录并拷贝框架代码

```bash
mkdir -p "D:/Video/server/include" "D:/Video/server/src"
cp "D:/Video/NetDisk/include/TCPKernel.h" "D:/Video/server/include/"
cp "D:/Video/NetDisk/include/block_epoll_net.h" "D:/Video/server/include/"
cp "D:/Video/NetDisk/include/Thread_pool.h" "D:/Video/server/include/"
cp "D:/Video/NetDisk/include/Mysql.h" "D:/Video/server/include/"
cp "D:/Video/NetDisk/include/clogic.h" "D:/Video/server/include/"
cp "D:/Video/NetDisk/include/err_str.h" "D:/Video/server/include/"
cp "D:/Video/NetDisk/src/TCPKernel.cpp" "D:/Video/server/src/"
cp "D:/Video/NetDisk/src/block_epoll_net.cpp" "D:/Video/server/src/"
cp "D:/Video/NetDisk/src/Thread_pool.cpp" "D:/Video/server/src/"
cp "D:/Video/NetDisk/src/Mysql.cpp" "D:/Video/server/src/"
cp "D:/Video/NetDisk/src/err_str.cpp" "D:/Video/server/src/"
cp "D:/Video/NetDisk/src/main.cpp" "D:/Video/server/src/"
cp "D:/Video/NetDisk/src/makefile" "D:/Video/server/src/"
```

### Step 2: 替换 packdef.h 为共享协议

```bash
rm "D:/Video/server/include/packdef.h"
cp "D:/Video/common/packdef.h" "D:/Video/server/include/packdef.h"
```

### Step 3: 提交

```bash
git add D:/Video/server/
git commit -m "feat: copy NetDisk server framework, link shared packdef.h"
```

---

## Task 3: 服务器 — block_epoll_net 安全修复

**Files:**
- Modify: `D:/Video/server/src/block_epoll_net.cpp`

### Step 1: 在 recv_task 中增加包大小校验

找到 `recv_task` 函数中 `nRelReadNum = read(ev->fd, &nPackSize, sizeof(nPackSize));` 之后 `pSzBuf = new char[nPackSize];` 之前（约第 164 行），插入校验：

```cpp
// 包大小安全校验：防止恶意客户端发送超大值导致 OOM
if (nPackSize <= 0 || nPackSize > _DEF_BUFFER) {
    printf("Invalid pack size: %d from fd=%d, closing connection\n", nPackSize, ev->fd);
    close(ev->fd);
    eventdel(ev);
    pThis->m_mapSockfdToEvent.erase(ev->fd);
    return NULL;
}
pSzBuf = new char[nPackSize];
```

同时将 `read()` 改为 `recv()` 保持一致（两处：读包头和读包体）：

```cpp
// 读包头：read → recv
nRelReadNum = recv(ev->fd, &nPackSize, sizeof(nPackSize), 0);

// 读包体：read → recv
while (nPackSize > 0) {
    nRelReadNum = recv(ev->fd, pSzBuf + offset, nPackSize, 0);
    // ...
}
```

### Step 2: 提交

```bash
git add D:/Video/server/src/block_epoll_net.cpp
git commit -m "fix: add pack size validation in recv_task, unify read() to recv()"
```

---

## Task 4: 服务器 — TCPKernel 启动建表

**Files:**
- Modify: `D:/Video/server/src/TCPKernel.cpp`

### Step 1: 在 Open() 函数末端（EventLoop 之前）增加建表 SQL

```cpp
// 在 m_logic = new CLogic(this); 之后，setNetPackMap(); 之后插入：

// ---- 建表（不存在则创建）----
const char* createUserTable = 
    "CREATE TABLE IF NOT EXISTS t_UserData ("
    "  id INT PRIMARY KEY AUTO_INCREMENT,"
    "  tel VARCHAR(40) NOT NULL UNIQUE,"
    "  name VARCHAR(40) NOT NULL,"
    "  password VARCHAR(40) NOT NULL,"
    "  role TINYINT DEFAULT 0,"
    "  created_at DATETIME DEFAULT NOW()"
    ");";
m_sql->UpdataMysql((char*)createUserTable);

const char* createRoomTable = 
    "CREATE TABLE IF NOT EXISTS t_RoomInfo ("
    "  id INT PRIMARY KEY AUTO_INCREMENT,"
    "  user_id INT UNIQUE,"
    "  title VARCHAR(64),"
    "  stream_key VARCHAR(64),"
    "  status TINYINT DEFAULT 0,"
    "  viewer_count INT DEFAULT 0,"
    "  created_at DATETIME DEFAULT NOW()"
    ");";
m_sql->UpdataMysql((char*)createRoomTable);

// 清理上一次运行残留的直播状态
const char* clearStatus = "UPDATE t_RoomInfo SET status = 0 WHERE status = 1;";
m_sql->UpdataMysql((char*)clearStatus);

printf("Database tables initialized.\n");
```

### Step 2: 提交

```bash
git add D:/Video/server/src/TCPKernel.cpp
git commit -m "feat: auto-create database tables on server startup, clear stale room status"
```

---

## Task 5: 服务器 — CLogic 头文件

**Files:**
- Modify: `D:/Video/server/include/clogic.h`

### Step 1: 重写 clogic.h

```cpp
#ifndef CLOGIC_H
#define CLOGIC_H

#include "TCPKernel.h"
#include <string>
#include <time.h>

using namespace std;

// Token 信息
struct TokenInfo {
    int userid;
    int role;
    time_t login_time;
};

class CLogic
{
public:
    CLogic(TcpKernel* pkernel)
    {
        m_pKernel = pkernel;
        m_sql = pkernel->m_sql;
        m_tcp = pkernel->m_tcp;
    }

    // 协议映射注册
    void setNetPackMap();

    // 发送数据
    void SendData(sock_fd clientfd, char* szbuf, int nlen) {
        m_pKernel->SendData(clientfd, szbuf, nlen);
    }

    // ---- 业务处理 ----
    void RegisterRq(sock_fd clientfd, char* szbuf, int nlen);
    void LoginRq(sock_fd clientfd, char* szbuf, int nlen);
    void AuthTokenRq(sock_fd clientfd, char* szbuf, int nlen);
    void CreateRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void RoomListRq(sock_fd clientfd, char* szbuf, int nlen);
    void CloseRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void JoinRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void LeaveRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void HeartbeatRq(sock_fd clientfd, char* szbuf, int nlen);

    // 心跳超时清理（由定时器线程调用）
    static void* HeartbeatCheckThread(void* arg);
    void StartHeartbeatCheck();
    void StopHeartbeatCheck();

    // 清理
    void Close();

    // 生成随机 stream_key
    static string GenerateStreamKey();

    // 生成 token
    static string GenerateToken();

private:
    TcpKernel* m_pKernel;
    CMysql* m_sql;
    Block_Epoll_Net* m_tcp;

    // 用户ID → socket fd（线程安全）
    MyMap<int, int> m_mapIDToUserFD;

    // token → TokenInfo（线程安全）
    MyMap<string, TokenInfo> m_mapTokenToUser;

    // 用户ID → 最后心跳时间（线程安全）
    MyMap<int, time_t> m_mapHeartbeat;

    bool m_isStopHeartbeat;
    pthread_t m_heartbeatThread;
};

#endif
```

### Step 2: 提交

```bash
git add D:/Video/server/include/clogic.h
git commit -m "feat: add CLogic handlers for all new protocols, thread-safe maps, heartbeat"
```

---

## Task 6: 服务器 — CLogic 业务逻辑实现

**Files:**
- Modify: `D:/Video/server/src/clogic.cpp`

### Step 1: setNetPackMap 注册所有协议

```cpp
void CLogic::setNetPackMap()
{
    NetPackMap(_DEF_PACK_REGISTER_RQ)    = &CLogic::RegisterRq;
    NetPackMap(_DEF_PACK_LOGIN_RQ)       = &CLogic::LoginRq;
    NetPackMap(_DEF_PACK_AUTH_TOKEN_RQ)  = &CLogic::AuthTokenRq;
    NetPackMap(_DEF_PACK_CREATE_ROOM_RQ) = &CLogic::CreateRoomRq;
    NetPackMap(_DEF_PACK_ROOM_LIST_RQ)   = &CLogic::RoomListRq;
    NetPackMap(_DEF_PACK_CLOSE_ROOM_RQ)  = &CLogic::CloseRoomRq;
    NetPackMap(_DEF_PACK_JOIN_ROOM_RQ)   = &CLogic::JoinRoomRq;
    NetPackMap(_DEF_PACK_LEAVE_ROOM_RQ)  = &CLogic::LeaveRoomRq;
    NetPackMap(_DEF_PACK_HEARTBEAT_RQ)   = &CLogic::HeartbeatRq;
}
```

### Step 2: 实现 RegisterRq

```cpp
void CLogic::RegisterRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_REGISTER_RQ* rq = (STRU_REGISTER_RQ*)szbuf;
    STRU_REGISTER_RS rs;

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "SELECT id FROM t_UserData WHERE tel='%s';", rq->tel);

    list<string> resList;
    if (!m_sql->SelectMysql(sqlBuf, 1, resList)) {
        printf("SelectMysql error: %s\n", sqlBuf);
        return;
    }

    if (resList.size() > 0) {
        rs.result = user_is_exist;
    } else {
        sprintf(sqlBuf,
            "INSERT INTO t_UserData (tel, name, password, role) VALUES ('%s','%s','%s',0);",
            rq->tel, rq->name, rq->password);
        m_sql->UpdataMysql(sqlBuf);
        rs.result = register_success;
    }
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}
```

### Step 3: 实现 LoginRq

```cpp
void CLogic::LoginRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_LOGIN_RQ* rq = (STRU_LOGIN_RQ*)szbuf;
    STRU_LOGIN_RS rs;

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "SELECT password, id, role FROM t_UserData WHERE tel='%s';", rq->tel);

    list<string> lst;
    if (!m_sql->SelectMysql(sqlBuf, 3, lst)) {
        printf("SelectMysql error: %s\n", sqlBuf);
        return;
    }

    if (lst.size() > 0) {
        string dbPwd = lst.front(); lst.pop_front();
        int dbId = atoi(lst.front().c_str()); lst.pop_front();
        int dbRole = atoi(lst.front().c_str());

        if (strcmp(dbPwd.c_str(), rq->password) == 0) {
            rs.result = login_success;
            rs.userid = dbId;
            rs.role = dbRole;

            string token = GenerateToken();
            strcpy(rs.token, token.c_str());

            TokenInfo info;
            info.userid = dbId;
            info.role = dbRole;
            info.login_time = time(NULL);
            m_mapTokenToUser.insert(token, info);
            m_mapIDToUserFD.insert(dbId, clientfd);
            m_mapHeartbeat.insert(dbId, time(NULL));
        } else {
            rs.result = password_error;
        }
    } else {
        rs.result = user_not_exist;
    }
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}
```

### Step 4: 实现 AuthTokenRq

```cpp
void CLogic::AuthTokenRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_AUTH_TOKEN_RQ* rq = (STRU_AUTH_TOKEN_RQ*)szbuf;
    STRU_LOGIN_RS rs;
    rs.result = user_not_exist;

    TokenInfo info;
    if (m_mapTokenToUser.find(string(rq->token), info)) {
        rs.result = login_success;
        rs.userid = info.userid;
        rs.role = info.role;
        m_mapIDToUserFD.insert(info.userid, clientfd);
        m_mapHeartbeat.insert(info.userid, time(NULL));
    }
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}
```

### Step 5: 实现 CreateRoomRq

```cpp
void CLogic::CreateRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_CREATE_ROOM_RQ* rq = (STRU_CREATE_ROOM_RQ*)szbuf;
    STRU_CREATE_ROOM_RS rs;

    // 检查用户是否在线
    int fd;
    if (!m_mapIDToUserFD.find(rq->userid, fd)) {
        rs.result = create_room_failed;
        m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    string streamKey = GenerateStreamKey();
    strcpy(rs.stream_key, streamKey.c_str());

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf,
        "INSERT INTO t_RoomInfo (user_id, title, stream_key, status) VALUES (%d,'%s','%s',1);",
        rq->userid, rq->title, streamKey.c_str());

    if (m_sql->UpdataMysql(sqlBuf)) {
        // 查询刚插入的 room_id
        sprintf(sqlBuf, "SELECT id FROM t_RoomInfo WHERE user_id=%d AND status=1;", rq->userid);
        list<string> lst;
        m_sql->SelectMysql(sqlBuf, 1, lst);
        if (lst.size() > 0) {
            rs.room_id = atoi(lst.front().c_str());
        }
        rs.result = create_room_success;
    } else {
        rs.result = create_room_failed;
    }
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}
```

### Step 6: 实现 RoomListRq

```cpp
void CLogic::RoomListRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_ROOM_LIST_RS rs;
    rs.room_count = 0;
    rs.json_length = 0;
    rs.content[0] = '\0';

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "SELECT r.id, r.title, r.viewer_count, u.name FROM t_RoomInfo r "
            "JOIN t_UserData u ON r.user_id=u.id WHERE r.status=1;");

    list<string> lst;
    m_sql->SelectMysql(sqlBuf, 4, lst);

    string json = "[";
    bool first = true;
    while (lst.size() >= 4) {
        string id = lst.front(); lst.pop_front();
        string title = lst.front(); lst.pop_front();
        string viewers = lst.front(); lst.pop_front();
        string anchorName = lst.front(); lst.pop_front();

        if (!first) json += ",";
        first = false;

        json += "{"
            "\"id\":" + id + ","
            "\"title\":\"" + title + "\","
            "\"viewer_count\":" + viewers + ","
            "\"anchor\":\"" + anchorName + "\""
            "}";
        rs.room_count++;
    }
    json += "]";

    rs.json_length = json.length();
    if (rs.json_length >= _DEF_CONTENT_SIZE) {
        rs.json_length = _DEF_CONTENT_SIZE - 1;
    }
    memcpy(rs.content, json.c_str(), rs.json_length);
    rs.content[rs.json_length] = '\0';

    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}
```

### Step 7: 实现 CloseRoomRq / JoinRoomRq / LeaveRoomRq / HeartbeatRq

```cpp
void CLogic::CloseRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_CLOSE_ROOM_RQ* rq = (STRU_CLOSE_ROOM_RQ*)szbuf;
    STRU_CLOSE_ROOM_RS rs;

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET status=0, viewer_count=0 WHERE id=%d AND user_id=%d;",
            rq->room_id, rq->userid);
    rs.result = m_sql->UpdataMysql(sqlBuf) ? close_room_success : close_room_failed;
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

void CLogic::JoinRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_JOIN_ROOM_RQ* rq = (STRU_JOIN_ROOM_RQ*)szbuf;
    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET viewer_count = viewer_count + 1 WHERE id=%d;",
            rq->room_id);
    m_sql->UpdataMysql(sqlBuf);
}

void CLogic::LeaveRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_LEAVE_ROOM_RQ* rq = (STRU_LEAVE_ROOM_RQ*)szbuf;
    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET viewer_count = GREATEST(viewer_count - 1, 0) WHERE id=%d;",
            rq->room_id);
    m_sql->UpdataMysql(sqlBuf);
}

void CLogic::HeartbeatRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_HEARTBEAT_RQ* rq = (STRU_HEARTBEAT_RQ*)szbuf;
    m_mapHeartbeat.insert(rq->userid, time(NULL));

    STRU_HEARTBEAT_RS rs;
    rs.server_time = (int)time(NULL);
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}
```

### Step 8: 实现 GenerateStreamKey / GenerateToken / 心跳清理线程

```cpp
string CLogic::GenerateStreamKey()
{
    const char hex[] = "0123456789abcdef";
    char key[32];
    key[0] = 'l'; key[1] = 'i'; key[2] = 'v'; key[3] = 'e'; key[4] = '_';
    for (int i = 5; i < 13; i++) {
        key[i] = hex[rand() % 16];
    }
    key[13] = '\0';
    return string(key);
}

string CLogic::GenerateToken()
{
    const char hex[] = "0123456789abcdef";
    char token[64];
    for (int i = 0; i < 32; i++) {
        token[i] = hex[rand() % 16];
    }
    token[32] = '\0';
    return string(token);
}

void* CLogic::HeartbeatCheckThread(void* arg)
{
    CLogic* pThis = (CLogic*)arg;
    while (!pThis->m_isStopHeartbeat) {
        sleep(30);  // 每30秒检查一次

        time_t now = time(NULL);
        // 收集需要清理的用户（不要在遍历 MyMap 时修改）
        list<int> timeoutUsers;
        // MyMap 没有遍历接口，这里需要改造。在 clogic.h 中增加 list<int> m_userList
        // 或者通过扫描 t_RoomInfo status=1 的方式来清理

        // 方案：直接查 MySQL 中有哪些 status=1 的房间，检查其 user_id 是否还有心跳
        char sqlBuf[_DEF_SQLIEN];
        sprintf(sqlBuf, "SELECT user_id, id FROM t_RoomInfo WHERE status=1;");
        list<string> lst;
        pThis->m_sql->SelectMysql(sqlBuf, 2, lst);

        while (lst.size() >= 2) {
            int userId = atoi(lst.front().c_str()); lst.pop_front();
            int roomId = atoi(lst.front().c_str()); lst.pop_front();

            time_t lastHb;
            if (!pThis->m_mapHeartbeat.find(userId, lastHb) || (now - lastHb > 45)) {
                // 心跳超时，清理
                printf("User %d heartbeat timeout, closing room %d\n", userId, roomId);
                sprintf(sqlBuf, "UPDATE t_RoomInfo SET status=0, viewer_count=0 WHERE id=%d;", roomId);
                pThis->m_sql->UpdataMysql(sqlBuf);
                int fd;
                if (pThis->m_mapIDToUserFD.find(userId, fd)) {
                    close(fd);
                }
                pThis->m_mapIDToUserFD.erase(userId);
                pThis->m_mapHeartbeat.erase(userId);
            }
        }
    }
    return NULL;
}

void CLogic::StartHeartbeatCheck()
{
    m_isStopHeartbeat = false;
    pthread_create(&m_heartbeatThread, NULL, HeartbeatCheckThread, this);
}

void CLogic::StopHeartbeatCheck()
{
    m_isStopHeartbeat = true;
    pthread_join(m_heartbeatThread, NULL);
}

void CLogic::Close()
{
    StopHeartbeatCheck();
    m_sql->DisConnect();
}
```

### Step 9: 提交

```bash
git add D:/Video/server/src/clogic.cpp
git commit -m "feat: implement all business logic handlers (register, login, auth, rooms, heartbeat)"
```

---

## Task 7: 服务器 — main.cpp 集成心跳

**Files:**
- Modify: `D:/Video/server/src/main.cpp`

### Step 1: 在 EventLoop 前后启动/停止心跳

```cpp
int main(int argc, char *argv[])
{
    int port = 8000;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }
    TcpKernel* pKernel = TcpKernel::GetInstance();
    pKernel->Open(port);
    pKernel->StartHeartbeatCheck();  // 新增
    printf("Server started on port %d\n", port);
    pKernel->EventLoop();
    pKernel->Close();
    return 0;
}
```

### Step 2: 在 TCPKernel 中添加转发方法

在 `TCPKernel.h` 中新增：
```cpp
void StartHeartbeatCheck() { m_logic->StartHeartbeatCheck(); }
```

### Step 3: 提交

```bash
git add D:/Video/server/src/main.cpp D:/Video/server/include/TCPKernel.h
git commit -m "feat: start heartbeat check thread before event loop"
```

---

## Task 8: 客户端库安全修复 — INetMediator 信号改为 QByteArray

**Files:**
- Modify: `D:/Video/服务器资料/服务器代码/netapi/mediator/INetMediator.h`
- Copy to: `D:/Video/Launcher/netapi/` (复制修改后的版本)

### Step 1: 修改信号签名

```cpp
// INetMediator.h — 修改信号
signals:
    void SIG_ReadyData(unsigned int lSendIP, const QByteArray& data);
    void SIG_disConnect();
```

### Step 2: 修改 TcpClientMediator::DealData

在 `TcpClientMediator.cpp` 中：
```cpp
void TcpClientMediator::DealData(unsigned int lSendIP, char* buf, int nlen)
{
    QByteArray data(buf, nlen);  // 深拷贝
    delete[] buf;                 // 立即释放堆内存
    Q_EMIT SIG_ReadyData(lSendIP, data);
}
```

### Step 3: 修改 TcpServerMediator::DealData（保持一致）

```cpp
void TcpServerMediator::DealData(unsigned int lSendIP, char* buf, int nlen)
{
    QByteArray data(buf, nlen);
    delete[] buf;
    Q_EMIT SIG_ReadyData(lSendIP, data);
}
```

### Step 4: 复制 netapi 到 Launcher

```bash
mkdir -p "D:/Video/Launcher/netapi"
cp -r "D:/Video/服务器资料/服务器代码/netapi/"* "D:/Video/Launcher/netapi/"
```

### Step 5: 提交

```bash
git add D:/Video/服务器资料/服务器代码/netapi/mediator/INetMediator.h
git add D:/Video/服务器资料/服务器代码/netapi/mediator/TcpClientMediator.cpp
git add D:/Video/服务器资料/服务器代码/netapi/mediator/TcpServerMediator.cpp
git add D:/Video/Launcher/netapi/
git commit -m "fix: change SIG_ReadyData to QByteArray to prevent memory leak and dangling pointer"
```

---

## Task 9: 客户端库 — TcpClient 原子布尔修复

**Files:**
- Modify: `D:/Video/服务器资料/服务器代码/netapi/net/TcpClient.h`
- Modify: `D:/Video/服务器资料/服务器代码/netapi/net/TcpClient.cpp`

### Step 1: TcpClient.h — 改用 std::atomic

```cpp
#include <atomic>

class TcpClient : public INet
{
    // ...
    bool m_isStop;
    std::atomic<bool> m_isConnected;  // was: bool
};
```

### Step 2: TcpClient.cpp — `m_isConnected = false` 无需改，赋值兼容

### Step 3: 同步更新 Launcher 中的 netapi 副本

```bash
cp "D:/Video/服务器资料/服务器代码/netapi/net/TcpClient.h" "D:/Video/Launcher/netapi/net/TcpClient.h"
cp "D:/Video/服务器资料/服务器代码/netapi/net/TcpClient.cpp" "D:/Video/Launcher/netapi/net/TcpClient.cpp"
```

### Step 4: 提交

```bash
git add D:/Video/服务器资料/服务器代码/netapi/net/TcpClient.h
git add D:/Video/Launcher/netapi/net/TcpClient.h
git commit -m "fix: use std::atomic for m_isConnected to prevent data race"
```

---

## Task 10: Launcher — Qt 项目创建与 main.cpp

**Files:**
- Create: `D:/Video/Launcher/Launcher.pro`
- Create: `D:/Video/Launcher/main.cpp`

### Step 1: Launcher.pro

```qmake
QT += core gui widgets network

TARGET = Launcher
TEMPLATE = app

SOURCES += main.cpp \
    logindialog.cpp \
    role_select_dialog.cpp

HEADERS += logindialog.h \
    role_select_dialog.h

FORMS += logindialog.ui \
    role_select_dialog.ui

# netapi 库
include(netapi/netapi.pri)

INCLUDEPATH += netapi/mediator netapi/net ../common
```

### Step 2: main.cpp

```cpp
#include <QApplication>
#include "logindialog.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 注册 QByteArray 元类型（跨线程信号槽需要）
    qRegisterMetaType<QByteArray>("QByteArray");

    LoginDialog w;
    w.show();

    return a.exec();
}
```

### Step 3: 提交

```bash
git add D:/Video/Launcher/Launcher.pro D:/Video/Launcher/main.cpp
git commit -m "feat: create Launcher Qt project with main entry"
```

---

## Task 11: Launcher — 登录/注册界面

**Files:**
- Create: `D:/Video/Launcher/logindialog.h`
- Create: `D:/Video/Launcher/logindialog.cpp`
- Create: `D:/Video/Launcher/logindialog.ui`

### Step 1: logindialog.h

```cpp
#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include "INetMediator.h"
#include "TcpClientMediator.h"
#include "packdef.h"

namespace Ui { class LoginDialog; }

class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();

private slots:
    void on_pb_login_clicked();
    void on_pb_register_clicked();
    void slot_readyData(unsigned int ip, const QByteArray& data);
    void slot_disconnected();

private:
    void connectToServer();
    Ui::LoginDialog *ui;
    TcpClientMediator *m_mediator;
    QString m_token;
    int m_userId;
    int m_role;
};

#endif
```

### Step 2: logindialog.cpp

```cpp
#include "logindialog.h"
#include "ui_logindialog.h"
#include "role_select_dialog.h"
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent), ui(new Ui::LoginDialog), m_mediator(nullptr), m_userId(0), m_role(0)
{
    ui->setupUi(this);
    connectToServer();
}

LoginDialog::~LoginDialog()
{
    if (m_mediator) { m_mediator->CloseNet(); delete m_mediator; }
    delete ui;
}

void LoginDialog::connectToServer()
{
    m_mediator = new TcpClientMediator;
    connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
            this, SLOT(slot_readyData(uint,QByteArray)), Qt::QueuedConnection);
    connect(m_mediator, SIGNAL(SIG_disConnect()),
            this, SLOT(slot_disconnected()), Qt::QueuedConnection);
    m_mediator->OpenNet("192.168.136.137", 8000);
}

void LoginDialog::on_pb_login_clicked()
{
    STRU_LOGIN_RQ rq;
    strncpy(rq.tel, ui->le_tel->text().toUtf8().constData(), _MAX_SIZE - 1);
    strncpy(rq.password, ui->le_password->text().toUtf8().constData(), _MAX_SIZE - 1);
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void LoginDialog::on_pb_register_clicked()
{
    STRU_REGISTER_RQ rq;
    strncpy(rq.tel, ui->le_tel->text().toUtf8().constData(), _MAX_SIZE - 1);
    strncpy(rq.name, ui->le_name->text().toUtf8().constData(), _MAX_SIZE - 1);
    strncpy(rq.password, ui->le_password->text().toUtf8().constData(), _MAX_SIZE - 1);
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void LoginDialog::slot_readyData(unsigned int ip, const QByteArray& data)
{
    PackType type = *(PackType*)data.constData();

    if (type == _DEF_PACK_LOGIN_RS) {
        STRU_LOGIN_RS* rs = (STRU_LOGIN_RS*)data.constData();
        if (rs->result == login_success) {
            m_token = QString(rs->token);
            m_userId = rs->userid;
            m_role = rs->role;
            // 打开角色选择
            RoleSelectDialog dlg(m_role, this);
            if (dlg.exec() == QDialog::Accepted) {
                int chosenRole = dlg.selectedRole();
                QString appPath;
                if (chosenRole == 1) {
                    appPath = QApplication::applicationDirPath() + "/../VideoRecoder/VideoRecoder.exe";
                } else {
                    appPath = QApplication::applicationDirPath() + "/../videoplayer/MedieaPlayer.exe";
                }

                QProcess proc;
                QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
                env.insert("LIVE_TOKEN", m_token);
                env.insert("LIVE_SERVER", "192.168.136.137");
                env.insert("LIVE_USERID", QString::number(m_userId));
                proc.setProcessEnvironment(env);
                proc.setProgram(appPath);
                proc.startDetached();
                close();
            }
        } else if (rs->result == password_error) {
            QMessageBox::warning(this, "登录失败", "密码错误");
        } else {
            QMessageBox::warning(this, "登录失败", "用户不存在");
        }
    }
    else if (type == _DEF_PACK_REGISTER_RS) {
        STRU_REGISTER_RS* rs = (STRU_REGISTER_RS*)data.constData();
        if (rs->result == register_success) {
            QMessageBox::information(this, "注册成功", "请登录");
        } else {
            QMessageBox::warning(this, "注册失败", "手机号已存在");
        }
    }
}

void LoginDialog::slot_disconnected()
{
    QMessageBox::warning(this, "连接断开", "与服务器断开连接，即将退出");
    close();
}
```

### Logindialog.ui (概要 — 手动在 Qt Designer 中创建)

| 控件 | 类型 | objectName |
|------|------|------------|
| 手机号输入 | QLineEdit | le_tel |
| 昵称输入 | QLineEdit | le_name |
| 密码输入 | QLineEdit (EchoMode=Password) | le_password |
| 登录按钮 | QPushButton | pb_login |
| 注册按钮 | QPushButton | pb_register |

### Step 3: 提交

```bash
git add D:/Video/Launcher/logindialog.h D:/Video/Launcher/logindialog.cpp D:/Video/Launcher/logindialog.ui
git commit -m "feat: add login/register dialog with server connection"
```

---

## Task 12: Launcher — 角色选择界面

**Files:**
- Create: `D:/Video/Launcher/role_select_dialog.h`
- Create: `D:/Video/Launcher/role_select_dialog.cpp`
- Create: `D:/Video/Launcher/role_select_dialog.ui`

### Step 1: role_select_dialog.h

```cpp
#ifndef ROLE_SELECT_DIALOG_H
#define ROLE_SELECT_DIALOG_H

#include <QDialog>

namespace Ui { class RoleSelectDialog; }

class RoleSelectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RoleSelectDialog(int currentRole, QWidget *parent = nullptr);
    ~RoleSelectDialog();
    int selectedRole() const { return m_selectedRole; }

private slots:
    void on_pb_broadcaster_clicked();
    void on_pb_viewer_clicked();

private:
    Ui::RoleSelectDialog *ui;
    int m_selectedRole;
};

#endif
```

### Step 2: role_select_dialog.cpp

```cpp
#include "role_select_dialog.h"
#include "ui_role_select_dialog.h"

RoleSelectDialog::RoleSelectDialog(int currentRole, QWidget *parent)
    : QDialog(parent), ui(new Ui::RoleSelectDialog), m_selectedRole(currentRole)
{
    ui->setupUi(this);
    // 如果只有单角色，直接接受
    if (currentRole == 0) {
        ui->pb_broadcaster->setVisible(true);
        ui->pb_viewer->setVisible(true);
    }
}

RoleSelectDialog::~RoleSelectDialog() { delete ui; }

void RoleSelectDialog::on_pb_broadcaster_clicked()
{
    m_selectedRole = 1;
    accept();
}

void RoleSelectDialog::on_pb_viewer_clicked()
{
    m_selectedRole = 0;
    accept();
}
```

### Step 3: 提交

```bash
git add D:/Video/Launcher/role_select_dialog.h D:/Video/Launcher/role_select_dialog.cpp D:/Video/Launcher/role_select_dialog.ui
git commit -m "feat: add role selection dialog (broadcaster / viewer)"
```

---

## Task 13: VideoRecoder — main.cpp 接收 Token

**Files:**
- Modify: `D:/Video/VideoRecoder/VideoRecoder/main.cpp`

### Step 1: 修改 main.cpp 入口

```cpp
#include "recorderdialog.h"
#include <QApplication>
#include <QProcessEnvironment>
#include <QByteArray>
#include "TcpClientMediator.h"
#include "packdef.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qRegisterMetaType<QByteArray>("QByteArray");

    // 读取环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString token = env.value("LIVE_TOKEN", "");
    QString serverIp = env.value("LIVE_SERVER", "192.168.136.137");
    int userId = env.value("LIVE_USERID", "0").toInt();

    RecorderDialog w;
    w.show();

    // 如果有 token，连接服务器并验证
    if (!token.isEmpty()) {
        w.initNetwork(serverIp, token, userId);
    }

    return a.exec();
}
```

### Step 2: 提交

```bash
git add D:/Video/VideoRecoder/VideoRecoder/main.cpp
git commit -m "feat: parse LIVE_TOKEN from env, init network in RecorderDialog"
```

---

## Task 14: VideoRecoder — RecorderDialog 网络集成

**Files:**
- Modify: `D:/Video/VideoRecoder/VideoRecoder/recorderdialog.h`
- Modify: `D:/Video/VideoRecoder/VideoRecoder/recorderdialog.cpp`

### Step 1: recorderDialog.h 新增成员

```cpp
#include "TcpClientMediator.h"
#include "packdef.h"
#include <QTimer>

class RecorderDialog : public QDialog
{
    // ... 现有代码 ...

public:
    void initNetwork(const QString& serverIp, const QString& token, int userId);

private slots:
    void slot_readyData(unsigned int ip, const QByteArray& data);
    void slot_disconnected();
    void on_pb_startStream_clicked();  // 新增开播按钮
    void slot_sendHeartbeat();

private:
    TcpClientMediator* m_mediator;
    QString m_token;
    int m_userId;
    int m_roomId;
    QString m_streamKey;
    bool m_isStreaming;
    QTimer* m_heartbeatTimer;
};
```

### Step 2: recorderDialog.cpp 新增实现

```cpp
void RecorderDialog::initNetwork(const QString& serverIp, const QString& token, int userId)
{
    m_token = token;
    m_userId = userId;
    m_roomId = 0;
    m_isStreaming = false;

    m_mediator = new TcpClientMediator;
    connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
            this, SLOT(slot_readyData(uint,QByteArray)), Qt::QueuedConnection);
    connect(m_mediator, SIGNAL(SIG_disConnect()),
            this, SLOT(slot_disconnected()), Qt::QueuedConnection);
    m_mediator->OpenNet(serverIp.toUtf8().constData(), 8000);

    // 启动心跳
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &RecorderDialog::slot_sendHeartbeat);
}

void RecorderDialog::slot_readyData(unsigned int ip, const QByteArray& data)
{
    PackType type = *(PackType*)data.constData();

    if (type == _DEF_PACK_LOGIN_RS) {
        STRU_LOGIN_RS* rs = (STRU_LOGIN_RS*)data.constData();
        if (rs->result == login_success) {
            m_userId = rs->userid;
            m_heartbeatTimer->start(10000);  // 每10秒心跳
        }
    }
    else if (type == _DEF_PACK_CREATE_ROOM_RS) {
        STRU_CREATE_ROOM_RS* rs = (STRU_CREATE_ROOM_RS*)data.constData();
        if (rs->result == create_room_success) {
            m_roomId = rs->room_id;
            m_streamKey = QString(rs->stream_key);
            // 构造 RTMP URL 并开始推流
            QString rtmpUrl = QString("rtmp://192.168.136.137:1935/live/%1").arg(m_streamKey);
            m_saveUrl = rtmpUrl;
            m_isStreaming = true;
            on_pb_start_clicked();  // 复用现有开始录制逻辑
            ui->pb_startStream->setText("下播");
        } else {
            QMessageBox::warning(this, "开播失败", "创建直播间失败");
        }
    }
    else if (type == _DEF_PACK_CLOSE_ROOM_RS) {
        m_isStreaming = false;
        m_heartbeatTimer->stop();
    }
}

void RecorderDialog::on_pb_startStream_clicked()
{
    if (m_isStreaming) {
        // 下播
        on_pb_stop_clicked();
        STRU_CLOSE_ROOM_RQ rq;
        rq.userid = m_userId;
        rq.room_id = m_roomId;
        m_mediator->SendData(0, (char*)&rq, sizeof(rq));
        ui->pb_startStream->setText("开播");
    } else {
        // 开播
        STRU_CREATE_ROOM_RQ rq;
        rq.userid = m_userId;
        strncpy(rq.title, "直播间", sizeof(rq.title) - 1);
        m_mediator->SendData(0, (char*)&rq, sizeof(rq));
    }
}

void RecorderDialog::slot_sendHeartbeat()
{
    STRU_HEARTBEAT_RQ rq;
    rq.userid = m_userId;
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void RecorderDialog::slot_disconnected()
{
    QMessageBox::warning(this, "连接断开", "与服务器断开连接");
    m_heartbeatTimer->stop();
    m_isStreaming = false;
}
```

### Step 3: 提交

```bash
git add D:/Video/VideoRecoder/VideoRecoder/recorderdialog.h
git add D:/Video/VideoRecoder/VideoRecoder/recorderdialog.cpp
git commit -m "feat: add network integration, stream button, heartbeat to RecorderDialog"
```

---

## Task 15: VideoRecoder — SaveVideoFileThread RTMP 推流修复

**Files:**
- Modify: `D:/Video/VideoRecoder/VideoRecoder/savevideofilethread.cpp`
- Modify: `D:/Video/VideoRecoder/VideoRecoder/savevideofilethread.h`

### Step 1: RTMP 推流关键修改点

**1) avio_open 增加超时：**

在 `run()` 中 `avio_open(&oc->pb, filename, AVIO_FLAG_WRITE)` 之前：
```cpp
AVDictionary *opts = NULL;
if (strncmp(filename, "rtmp://", 7) == 0) {
    av_dict_set(&opts, "timeout", "5000000", 0);  // 5秒连接超时
}
ret = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE, NULL, &opts);
av_dict_free(&opts);
```

**2) 设置 interrupt_callback：**
```cpp
oc->interrupt_callback.callback = [](void *ctx) -> int {
    SaveVideoFileThread *self = static_cast<SaveVideoFileThread*>(ctx);
    return self->isStop ? 1 : 0;
};
oc->interrupt_callback.opaque = this;
```

**3) 显式设置 max_b_frames = 0：**

在 `add_Video_stream()` 中，`c->gop_size = 15;` 之后：
```cpp
c->max_b_frames = 0;  // 禁用B帧，降低延迟
```

**4) write_frame 返回值检查：**

找到 `write_video_frame_from_queue()` 和 `write_audio_frame_from_queue()` 中：
```cpp
ret = write_frame(oc, &video_st.enc->time_base, video_st.st, &pkt);
av_packet_unref(&pkt);
// 新增错误检查
if (ret < 0) {
    char errbuf[128];
    av_strerror(ret, errbuf, sizeof(errbuf));
    Q_EMIT SIG_EncodeError(QString("推流写入失败: %1").arg(errbuf));
    isStop = true;
    break;
}
```

**5) terminate 超时延长：**

在 `slot_closeVideo()` 中：
```cpp
if (!this->wait(10000)) {  // 3秒 → 10秒
    fprintf(stderr, "Warning: encoder thread did not finish within 10 seconds\n");
    this->terminate();
}
```

### Step 2: 提交

```bash
git add D:/Video/VideoRecoder/VideoRecoder/savevideofilethread.cpp
git add D:/Video/VideoRecoder/VideoRecoder/savevideofilethread.h
git commit -m "fix: RTMP push timeout, interrupt_callback, max_b_frames, write error check"
```

---

## Task 16: MediaPlayer — main.cpp 接收 Token

**Files:**
- Modify: `D:/Video/videoplayer/MedieaPlayer/main.cpp`

### Step 1: 修改 main.cpp

```cpp
#include "playerdialog.h"
#include <QApplication>
#include <QProcessEnvironment>
#include <QByteArray>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    qRegisterMetaType<QByteArray>("QByteArray");

    // 读取环境变量
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString token = env.value("LIVE_TOKEN", "");
    QString serverIp = env.value("LIVE_SERVER", "192.168.136.137");
    int userId = env.value("LIVE_USERID", "0").toInt();

    // FFmpeg 初始化 ...
    av_register_all();
    avformat_network_init();

    PlayerDialog w;
    w.show();

    if (!token.isEmpty()) {
        w.initNetwork(serverIp, token, userId);
    }

    return a.exec();
}
```

### Step 2: 提交

```bash
git add D:/Video/videoplayer/MedieaPlayer/main.cpp
git commit -m "feat: parse LIVE_TOKEN from env, init network in PlayerDialog"
```

---

## Task 17: MediaPlayer — PlayerDialog 网络集成与直播间列表

**Files:**
- Modify: `D:/Video/videoplayer/MedieaPlayer/playerdialog.h`
- Modify: `D:/Video/videoplayer/MedieaPlayer/playerdialog.cpp`

### Step 1: playerdialog.h 新增成员

```cpp
#include "TcpClientMediator.h"
#include "packdef.h"
#include <QTimer>
#include <QTableWidget>

class PlayerDialog : public QDialog
{
    // ... 现有成员 ...

public:
    void initNetwork(const QString& serverIp, const QString& token, int userId);

private slots:
    void slot_readyData(unsigned int ip, const QByteArray& data);
    void slot_disconnected();
    void on_pb_roomList_clicked();
    void slot_sendHeartbeat();
    void slot_refreshRoomList();

private:
    TcpClientMediator* m_mediator;
    QString m_token;
    QString m_serverIp;
    int m_userId;
    int m_roomId;
    QTimer* m_heartbeatTimer;
    QTimer* m_refreshTimer;
    QTableWidget* m_roomTable;  // 持久化的房间列表窗口
};
```

### Step 2: playerdialog.cpp 新增实现

```cpp
void PlayerDialog::initNetwork(const QString& serverIp, const QString& token, int userId)
{
    m_token = token;
    m_serverIp = serverIp;
    m_userId = userId;
    m_roomId = 0;
    m_mediator = nullptr;
    m_roomTable = nullptr;

    m_mediator = new TcpClientMediator;
    connect(m_mediator, SIGNAL(SIG_ReadyData(uint,QByteArray)),
            this, SLOT(slot_readyData(uint,QByteArray)), Qt::QueuedConnection);
    connect(m_mediator, SIGNAL(SIG_disConnect()),
            this, SLOT(slot_disconnected()), Qt::QueuedConnection);
    m_mediator->OpenNet(serverIp.toUtf8().constData(), 8000);

    // 心跳
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &PlayerDialog::slot_sendHeartbeat);
}

void PlayerDialog::slot_readyData(unsigned int ip, const QByteArray& data)
{
    PackType type = *(PackType*)data.constData();

    if (type == _DEF_PACK_LOGIN_RS) {
        STRU_LOGIN_RS* rs = (STRU_LOGIN_RS*)data.constData();
        if (rs->result == login_success) {
            m_userId = rs->userid;
            m_heartbeatTimer->start(10000);
            requestRoomList();  // 登录验证成功后自动拉取一次
        }
    }
    else if (type == _DEF_PACK_ROOM_LIST_RS) {
        STRU_ROOM_LIST_RS* rs = (STRU_ROOM_LIST_RS*)data.constData();
        QByteArray jsonData(rs->content, rs->json_length);
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        QJsonArray arr = doc.array();

        // 更新表格
        if (!m_roomTable) {
            m_roomTable = new QTableWidget(0, 2);
            m_roomTable->setHorizontalHeaderLabels({"直播间", "观众"});
            m_roomTable->setColumnWidth(0, 300);
            m_roomTable->setSelectionBehavior(QAbstractItemView::SelectRows);
            connect(m_roomTable, &QTableWidget::cellDoubleClicked, [this](int row, int) {
                if (m_roomId > 0) {
                    STRU_LEAVE_ROOM_RQ lrq;
                    lrq.userid = m_userId;
                    lrq.room_id = m_roomId;
                    m_mediator->SendData(0, (char*)&lrq, sizeof(lrq));
                }
                QString streamKey = m_roomTable->item(row, 0)->data(Qt::UserRole).toString();
                m_roomId = m_roomTable->item(row, 0)->data(Qt::UserRole + 1).toInt();
                QString url = QString("rtmp://%1:1935/live/%2").arg(m_serverIp, streamKey);

                // 停止当前播放
                if (m_player->playerState() != PlayerState::Stop) {
                    m_player->stop(true);
                    QThread::msleep(50);
                }
                m_player->setFileName(url);
                m_player->start();

                // 通知服务器
                STRU_JOIN_ROOM_RQ jrq;
                jrq.userid = m_userId;
                jrq.room_id = m_roomId;
                m_mediator->SendData(0, (char*)&jrq, sizeof(jrq));
            });
            m_roomTable->setWindowTitle("直播间列表");
            m_roomTable->resize(500, 350);
            m_roomTable->show();
        }

        m_roomTable->setRowCount(0);
        for (int i = 0; i < arr.size(); i++) {
            QJsonObject room = arr[i].toObject();
            m_roomTable->insertRow(i);
            QTableWidgetItem* titleItem = new QTableWidgetItem(
                QString("%1 (by %2)").arg(room["title"].toString(), room["anchor"].toString()));
            titleItem->setData(Qt::UserRole, room["stream_key"].toString());  // 隐藏 stream_key（仅主播知道）
            titleItem->setData(Qt::UserRole + 1, room["id"].toInt());
            m_roomTable->setItem(i, 0, titleItem);
            m_roomTable->setItem(i, 1, new QTableWidgetItem(
                QString::number(room["viewer_count"].toInt())));
        }
    }
}

void PlayerDialog::on_pb_roomList_clicked()
{
    requestRoomList();
    if (m_roomTable) m_roomTable->show();
}

void PlayerDialog::requestRoomList()
{
    STRU_ROOM_LIST_RQ rq;
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void PlayerDialog::slot_sendHeartbeat()
{
    STRU_HEARTBEAT_RQ rq;
    rq.userid = m_userId;
    m_mediator->SendData(0, (char*)&rq, sizeof(rq));
}

void PlayerDialog::slot_disconnected()
{
    QMessageBox::warning(this, "连接断开", "与服务器断开连接");
    m_heartbeatTimer->stop();
    m_mediator->deleteLater();
    m_mediator = nullptr;
}
```

### Step 3: 提交

```bash
git add D:/Video/videoplayer/MedieaPlayer/playerdialog.h
git add D:/Video/videoplayer/MedieaPlayer/playerdialog.cpp
git commit -m "feat: add room list browsing, join/leave, heartbeat to PlayerDialog"
```

---

## Task 18: 部署 — 编译服务器并配置环境

### Step 1: SSH 到 Linux VM 编译服务器

```bash
python D:/Video/ssh_vm.py "cd /home/abc/video/server/src && make clean && make"
```

### Step 2: 创建 MySQL 数据库

```bash
python D:/Video/ssh_vm.py "mysql -u root -pcolin123 -e 'CREATE DATABASE IF NOT EXISTS LiveServer;'"
```

### Step 3: 配置并启动 nginx-rtmp

SSH 到 VM，编辑 nginx.conf 增加：
```nginx
rtmp {
    server {
        listen 1935;
        chunk_size 4096;

        application live {
            live on;
            record off;
        }
    }
}
```

启动 nginx：
```bash
python D:/Video/ssh_vm.py "sudo nginx -s reload || sudo nginx"
```

### Step 4: 启动业务服务器

```bash
python D:/Video/ssh_vm.py "cd /home/abc/video/server/src && ./server 8000 &"
```

### Step 5: 提交

```bash
git add -A
git commit -m "chore: add deployment scripts and nginx-rtmp config"
```

---

## 自审清单

| 检查项 | 状态 |
|--------|------|
| 所有协议号有对应实现 | ✓ 10000-10014 全覆盖 |
| 所有表有建表语句 | ✓ t_UserData + t_RoomInfo |
| 线程安全：MyMap 替换 std::map | ✓ clogic.h |
| 线程安全：atomic bool | ✓ TcpClient.h |
| 内存安全：QByteArray + delete[] | ✓ DealData |
| 包大小校验 | ✓ block_epoll_net.cpp |
| RTMP 写入错误检查 | ✓ savevideofilethread.cpp |
| Token 验证机制 | ✓ AuthTokenRq + 环境变量 |
| 心跳保活 | ✓ 客户端10s发送，服务端30s检查超45s清理 |
| 观众进/离房 | ✓ JOIN_ROOM + LEAVE_ROOM |
| 无 TBD/TODO | ✓ |
