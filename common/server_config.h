#pragma once

// ============================================================
// 服务器地址 — 集中管理，所有模块统一引用此文件
// 修改服务器 IP 只需改这里，无需逐个文件查找替换
// ============================================================

#define SERVER_HOST          "192.168.136.137"
#define SERVER_PORT_TCP      8000
#define SERVER_PORT_RTMP     1935

// 便捷宏（用于拼接 URL 字符串字面量）
// 用法: QString url = RTMP_LIVE_URL + streamKey;
#define RTMP_LIVE_URL       "rtmp://" SERVER_HOST ":" "1935" "/live/"
#define RTMP_VOD_URL        "rtmp://" SERVER_HOST ":" "1935" "/vod/"
#define RTMP_VIDEOTEST_URL  "rtmp://" SERVER_HOST ":" "1935" "/videotest/"
