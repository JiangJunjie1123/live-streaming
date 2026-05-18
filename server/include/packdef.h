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
#define _DEF_DB_USER    "liveapp"
#define _DEF_DB_PWD     "abc"

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

// ===================== 协议结构体 =====================
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
    char content[_DEF_CONTENT_SIZE];
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

// --- 观众进入 ---
struct STRU_JOIN_ROOM_RQ {
    STRU_JOIN_ROOM_RQ() : type(_DEF_PACK_JOIN_ROOM_RQ), userid(0), room_id(0) {}
    PackType type;
    int userid;
    int room_id;
};

// --- 观众离开 ---
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
