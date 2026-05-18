#include "clogic.h"
#include <sys/stat.h>
#include <errno.h>

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

#define _DEF_COUT_FUNC_    cout << "clientfd:" << clientfd << " " << __func__ << endl;

// ===================== 注册 =====================
void CLogic::RegisterRq(sock_fd clientfd, char* szbuf, int nlen)
{
    _DEF_COUT_FUNC_
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

        // 创建用户文件目录
        char path[_MAX_PATH];
        sprintf(path, "/home/colin/video/flv/%s/", rq->tel);
        mkdir(path, 0755);
    }
    cout << "Register: tel=" << rq->tel << " result=" << rs.result << endl;
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== 登录 =====================
void CLogic::LoginRq(sock_fd clientfd, char* szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    STRU_LOGIN_RQ* rq = (STRU_LOGIN_RQ*)szbuf;
    STRU_LOGIN_RS rs;
    rs.result = user_not_exist;
    rs.userid = 0;
    rs.role = 0;
    memset(rs.token, 0, sizeof(rs.token));

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "SELECT password, id, role FROM t_UserData WHERE tel='%s';", rq->tel);

    list<string> lst;
    if (!m_sql->SelectMysql(sqlBuf, 3, lst)) {
        printf("SelectMysql error: %s\n", sqlBuf);
        return;
    }

    if (lst.size() >= 3) {
        string dbPwd = lst.front(); lst.pop_front();
        int dbId = atoi(lst.front().c_str()); lst.pop_front();
        int dbRole = atoi(lst.front().c_str());

        if (strcmp(dbPwd.c_str(), rq->password) == 0) {
            rs.result = login_success;
            rs.userid = dbId;
            rs.role = dbRole;

            string token = GenerateToken();
            strncpy(rs.token, token.c_str(), sizeof(rs.token) - 1);

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
    }
    cout << "Login: tel=" << rq->tel << " result=" << rs.result << " userid=" << rs.userid << " role=" << rs.role << endl;
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== Token 验证（子应用启动时调用） =====================
void CLogic::AuthTokenRq(sock_fd clientfd, char* szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    STRU_AUTH_TOKEN_RQ* rq = (STRU_AUTH_TOKEN_RQ*)szbuf;
    STRU_LOGIN_RS rs;
    rs.result = user_not_exist;
    rs.userid = 0;
    rs.role = 0;
    memset(rs.token, 0, sizeof(rs.token));

    TokenInfo info;
    string token(rq->token);
    if (m_mapTokenToUser.find(token, info)) {
        rs.result = login_success;
        rs.userid = info.userid;
        rs.role = info.role;
        m_mapIDToUserFD.insert(info.userid, clientfd);
        m_mapHeartbeat.insert(info.userid, time(NULL));
        printf("AuthToken success: userid=%d, role=%d\n", info.userid, info.role);
    } else {
        printf("AuthToken failed: token not found\n");
    }
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== 创建直播间 =====================
void CLogic::CreateRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    STRU_CREATE_ROOM_RQ* rq = (STRU_CREATE_ROOM_RQ*)szbuf;
    STRU_CREATE_ROOM_RS rs;
    rs.room_id = 0;
    memset(rs.stream_key, 0, sizeof(rs.stream_key));

    // 检查用户是否在线
    int fd;
    if (!m_mapIDToUserFD.find(rq->userid, fd)) {
        rs.result = create_room_failed;
        m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    string streamKey = GenerateStreamKey();
    strncpy(rs.stream_key, streamKey.c_str(), sizeof(rs.stream_key) - 1);

    char sqlBuf[_DEF_SQLIEN] = "";

    // 先关闭该用户之前的活跃房间（user_id 有 UNIQUE 约束）
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET status=0 WHERE user_id=%d AND status=1;",
        rq->userid);
    m_sql->UpdataMysql(sqlBuf);

    // 插入新房间
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
        printf("Room created: id=%d, stream_key=%s\n", rs.room_id, streamKey.c_str());
    } else {
        rs.result = create_room_failed;
        printf("Room creation failed for user %d (may already have active room)\n", rq->userid);
    }
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== 房间列表 =====================
void CLogic::RoomListRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_ROOM_LIST_RS rs;
    rs.room_count = 0;
    rs.json_length = 0;
    memset(rs.content, 0, sizeof(rs.content));

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "SELECT r.id, r.title, r.viewer_count, u.name, r.stream_key FROM t_RoomInfo r "
            "JOIN t_UserData u ON r.user_id=u.id WHERE r.status=1;");

    list<string> lst;
    if (!m_sql->SelectMysql(sqlBuf, 5, lst)) {
        printf("RoomList SelectMysql error\n");
        m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
        return;
    }

    string json = "[";
    bool first = true;
    while (lst.size() >= 5) {
        string id = lst.front(); lst.pop_front();
        string title = lst.front(); lst.pop_front();
        string viewers = lst.front(); lst.pop_front();
        string anchorName = lst.front(); lst.pop_front();
        string streamKey = lst.front(); lst.pop_front();

        if (!first) json += ",";
        first = false;

        // 转义标题中的双引号
        size_t pos = 0;
        string escapedTitle = title;
        while ((pos = escapedTitle.find('"', pos)) != string::npos) {
            escapedTitle.insert(pos, "\\");
            pos += 2;
        }

        json += "{"
            "\"id\":" + id + ","
            "\"title\":\"" + escapedTitle + "\","
            "\"viewer_count\":" + viewers + ","
            "\"anchor\":\"" + anchorName + "\","
            "\"stream_key\":\"" + streamKey + "\""
            "}";
        rs.room_count++;
    }
    json += "]";

    rs.json_length = json.length();
    if (rs.json_length >= _DEF_CONTENT_SIZE) {
        rs.json_length = _DEF_CONTENT_SIZE - 1;
        printf("WARNING: Room list JSON truncated (size=%zu, max=%d)\n", json.length(), _DEF_CONTENT_SIZE);
    }
    memcpy(rs.content, json.c_str(), rs.json_length);
    rs.content[rs.json_length] = '\0';

    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== 关闭直播间 =====================
void CLogic::CloseRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    _DEF_COUT_FUNC_
    STRU_CLOSE_ROOM_RQ* rq = (STRU_CLOSE_ROOM_RQ*)szbuf;
    STRU_CLOSE_ROOM_RS rs;

    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET status=0, viewer_count=0 WHERE id=%d AND user_id=%d;",
            rq->room_id, rq->userid);
    rs.result = m_sql->UpdataMysql(sqlBuf) ? close_room_success : close_room_failed;
    printf("Room %d closed by user %d, result=%d\n", rq->room_id, rq->userid, rs.result);
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== 观众进入直播间 =====================
void CLogic::JoinRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_JOIN_ROOM_RQ* rq = (STRU_JOIN_ROOM_RQ*)szbuf;
    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET viewer_count = viewer_count + 1 WHERE id=%d;",
            rq->room_id);
    m_sql->UpdataMysql(sqlBuf);
    printf("User %d joined room %d\n", rq->userid, rq->room_id);
}

// ===================== 观众离开直播间 =====================
void CLogic::LeaveRoomRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_LEAVE_ROOM_RQ* rq = (STRU_LEAVE_ROOM_RQ*)szbuf;
    char sqlBuf[_DEF_SQLIEN] = "";
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET viewer_count = GREATEST(viewer_count - 1, 0) WHERE id=%d;",
            rq->room_id);
    m_sql->UpdataMysql(sqlBuf);
    printf("User %d left room %d\n", rq->userid, rq->room_id);
}

// ===================== 心跳 =====================
void CLogic::HeartbeatRq(sock_fd clientfd, char* szbuf, int nlen)
{
    STRU_HEARTBEAT_RQ* rq = (STRU_HEARTBEAT_RQ*)szbuf;
    m_mapHeartbeat.insert(rq->userid, time(NULL));

    STRU_HEARTBEAT_RS rs;
    rs.server_time = (int)time(NULL);
    m_tcp->SendData(clientfd, (char*)&rs, sizeof(rs));
}

// ===================== 心跳超时清理线程 =====================
void* CLogic::HeartbeatCheckThread(void* arg)
{
    CLogic* pThis = (CLogic*)arg;
    while (!pThis->m_isStopHeartbeat) {
        sleep(30);  // 每30秒检查一次

        time_t now = time(NULL);
        char sqlBuf[_DEF_SQLIEN];

        // 查询所有 status=1 的房间
        sprintf(sqlBuf, "SELECT user_id, id FROM t_RoomInfo WHERE status=1;");
        list<string> lst;
        if (!pThis->m_sql->SelectMysql(sqlBuf, 2, lst)) {
            continue;
        }

        while (lst.size() >= 2) {
            int userId = atoi(lst.front().c_str()); lst.pop_front();
            int roomId = atoi(lst.front().c_str()); lst.pop_front();

            time_t lastHb;
            if (!pThis->m_mapHeartbeat.find(userId, lastHb) || (now - lastHb > 45)) {
                // 心跳超时，清理房间
                printf("Heartbeat timeout: user=%d, room=%d, closing\n", userId, roomId);
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
    printf("Heartbeat check thread started\n");
}

void CLogic::StopHeartbeatCheck()
{
    m_isStopHeartbeat = true;
    pthread_join(m_heartbeatThread, NULL);
    printf("Heartbeat check thread stopped\n");
}

// ===================== 工具方法 =====================
string CLogic::GenerateStreamKey()
{
    const char hex[] = "0123456789abcdef";
    char key[14];
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
    char token[33];
    for (int i = 0; i < 32; i++) {
        token[i] = hex[rand() % 16];
    }
    token[32] = '\0';
    return string(token);
}

// ===================== 清理 =====================
void CLogic::Close()
{
    StopHeartbeatCheck();
    // 关闭所有在线房间
    char sqlBuf[_DEF_SQLIEN];
    sprintf(sqlBuf, "UPDATE t_RoomInfo SET status=0, viewer_count=0 WHERE status=1;");
    m_sql->UpdataMysql(sqlBuf);
    m_sql->DisConnect();
    printf("CLogic closed, all rooms cleared\n");
}
