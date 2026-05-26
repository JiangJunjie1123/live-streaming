#ifndef CLOGIC_H
#define CLOGIC_H

#include "TCPKernel.h"
#include "packdef.h"
#include <string>
#include <set>
#include <time.h>

using namespace std;

// Token 信息：子应用用 token 验证身份，而不是直接传 userid
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
        m_isStopHeartbeat = false;
    }

    // 协议映射注册
    void setNetPackMap();

    // 发送数据快捷方法
    void SendData(sock_fd clientfd, char* szbuf, int nlen) {
        m_pKernel->SendData(clientfd, szbuf, nlen);
    }

    // ---- 业务处理 handler ----
    void RegisterRq(sock_fd clientfd, char* szbuf, int nlen);
    void LoginRq(sock_fd clientfd, char* szbuf, int nlen);
    void AuthTokenRq(sock_fd clientfd, char* szbuf, int nlen);
    void CreateRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void RoomListRq(sock_fd clientfd, char* szbuf, int nlen);
    void CloseRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void JoinRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void LeaveRoomRq(sock_fd clientfd, char* szbuf, int nlen);
    void HeartbeatRq(sock_fd clientfd, char* szbuf, int nlen);
    void DanmakuSendRq(sock_fd clientfd, char* szbuf, int nlen);

    // ---- 心跳超时清理 ----
    static void* HeartbeatCheckThread(void* arg);
    void StartHeartbeatCheck();
    void StopHeartbeatCheck();

    // ---- 工具方法 ----
    static string GenerateStreamKey();
    static string GenerateToken();

    // ---- 清理 ----
    void Close();

private:
    TcpKernel* m_pKernel;
    CMysql* m_sql;
    Block_Epoll_Net* m_tcp;

    // 用户ID → socket fd（线程安全 MyMap）
    MyMap<int, int> m_mapIDToUserFD;

    // token 字符串 → TokenInfo（线程安全 MyMap）
    MyMap<string, TokenInfo> m_mapTokenToUser;

    // 用户ID → 最后心跳时间（线程安全 MyMap）
    MyMap<int, time_t> m_mapHeartbeat;

    // 房间成员: room_id → set<client_fd> (用于弹幕广播)
    MyMap<int, std::set<int>> m_mapRoomMembers;
    // fd → room_id (用于离开/断线时快速定位房间)
    MyMap<int, int> m_mapFdToRoom;

    // 心跳线程控制
    bool m_isStopHeartbeat;
    pthread_t m_heartbeatThread;
};

#endif
