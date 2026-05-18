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
#include "err_str.h"
#include <malloc.h>

#include<iostream>
#include<map>
#include<list>


//边界值
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



/*-------------数据库信息-----------------*/
#define _DEF_DB_NAME    "VideoServer"
#define _DEF_DB_IP      "localhost"
#define _DEF_DB_USER    "root"
#define _DEF_DB_PWD     "colin123"
/*--------------------------------------*/
#define _MAX_PATH           (260)
#define _DEF_BUFFER         (4096)
#define _DEF_CONTENT_SIZE	(1024)
#define _MAX_SIZE           (40)
#define _DEF_HOBBY_COUNT    (8)
//自定义协议   先写协议头 再写协议结构
//登录 注册 获取好友信息 添加好友 聊天 发文件 下线请求
#define _DEF_PACK_BASE	(10000)
#define _DEF_PACK_COUNT (100)

//注册
#define _DEF_PACK_REGISTER_RQ	(_DEF_PACK_BASE + 0 )
#define _DEF_PACK_REGISTER_RS	(_DEF_PACK_BASE + 1 )
//登录
#define _DEF_PACK_LOGIN_RQ	(_DEF_PACK_BASE + 2 )
#define _DEF_PACK_LOGIN_RS	(_DEF_PACK_BASE + 3 )
//上传文件
#define _DEF_PACK_UPLOAD_RQ	(_DEF_PACK_BASE + 4 )
#define _DEF_PACK_UPLOAD_RS	(_DEF_PACK_BASE + 5 )
//文件块
#define _DEF_PACK_FILE_BLOCK_RQ	(_DEF_PACK_BASE + 6 )
//推荐视频
#define _DEF_PACK_RECOMMEND_RQ	(_DEF_PACK_BASE + 7 )
#define _DEF_PACK_RECOMMEND_RS	(_DEF_PACK_BASE + 8 )
//下载视频
#define _DEF_PACK_DOWNLOAD_RQ	(_DEF_PACK_BASE + 9 )
#define _DEF_PACK_DOWNLOAD_RS	(_DEF_PACK_BASE + 10 )
//下载块
#define _DEF_PACK_DOWNLOAD_BLOCK_RQ	(_DEF_PACK_BASE + 11 )
#define _DEF_PACK_DOWNLOAD_BLOCK_RS	(_DEF_PACK_BASE + 12 )

//返回的结果
//注册请求的结果
#define user_is_exist		(0)
#define register_success	(1)
//登录请求的结果
#define user_not_exist		(0)
#define password_error		(1)
#define login_success		(2)

//上传文件请求的结果
#define upload_failed		(0)
#define upload_success		(1)
//推荐视频请求的结果
#define recommend_failed	(0)
#define recommend_success	(1)
//下载视频请求的结果
#define download_failed		(0)
#define download_success	(1)


typedef int PackType;

//协议结构
//注册
#pragma pack(push, 1)
typedef struct STRU_REGISTER_RQ
{
    STRU_REGISTER_RQ():type(_DEF_PACK_REGISTER_RQ)
    {
        memset( name  , 0, sizeof(name));
        memset( password , 0, sizeof(password) );
        dance    =0;
        edu      =0;
        ennegy   =0;
        food     =0;
        funny    =0;
        music    =0;
        outside  =0;
        video    =0;
    }
    //需要手机号码 , 密码, 昵称
    PackType type;
    char name[_MAX_SIZE];
    char password[_MAX_SIZE];
    int dance ;
    int edu   ;
    int ennegy;
    int food  ;
    int funny ;
    int music ;
    int outside;
    int video;

}STRU_REGISTER_RQ;

typedef struct STRU_REGISTER_RS
{
    //回复结果
    STRU_REGISTER_RS(): type(_DEF_PACK_REGISTER_RS) , result(register_success)
    {
    }
    PackType type;
    int result;
}STRU_REGISTER_RS;

//登录
typedef struct STRU_LOGIN_RQ
{
    //登录需要: 手机号 密码
    STRU_LOGIN_RQ():type(_DEF_PACK_LOGIN_RQ)
    {
        memset( name  , 0, sizeof(name));
        memset( password , 0, sizeof(password) );
    }
    PackType type;
    char name[_MAX_SIZE];
    char password[_MAX_SIZE];

}STRU_LOGIN_RQ;

typedef struct STRU_LOGIN_RS
{
    // 需要 结果 , 用户的id
    STRU_LOGIN_RS(): type(_DEF_PACK_LOGIN_RS) , result(login_success),userid(0)
    {
    }
    PackType type;
    int result;
    int userid;

}STRU_LOGIN_RS;


//上传文件请求
typedef struct STRU_UPLOAD_RQ
{
    STRU_UPLOAD_RQ():m_nType(_DEF_PACK_UPLOAD_RQ)
    {
        m_nFileId = 0;
        m_UserId = 0;
        m_nFileSize = 0;
        memset(m_szFileType, 0, sizeof(m_szFileType));
        memset(m_szGifName, 0, sizeof(m_szGifName));
        memset(m_szFileName, 0, sizeof(m_szFileName));
        memset(m_szHobby, 0, sizeof(m_szHobby));
    }
    //包类型
    PackType m_nType;
    //用于查数据库,获取用户名字,拼接路径
    int m_UserId;
    //区分不同文件,可采用 md5 或 随机数 用户同时只能传一个所以相同 概率较低
    int m_nFileId;
    //文件大小,用于文件传输结束
    int64_t m_nFileSize;
    //喜好标签
    char m_szHobby[_DEF_HOBBY_COUNT];
    //文件名,用于存储文件
    char m_szFileName[_MAX_PATH];
    //动画名字,方便直接写入数据库
    char m_szGifName[_MAX_PATH];
    //用于区分视频和图片
    char m_szFileType[_MAX_SIZE];
}STRU_UPLOAD_RQ;


//上传文件回复

typedef struct STRU_UPLOAD_RS
{
    STRU_UPLOAD_RS(): m_nType(_DEF_PACK_UPLOAD_RS), m_lResult(upload_success)
    {
    }
    PackType m_nType;
    int m_lResult;
}STRU_UPLOAD_RS;


//文件块请求

typedef struct STRU_FILE_BLOCK_RQ
{
    STRU_FILE_BLOCK_RQ():m_nType(_DEF_PACK_FILE_BLOCK_RQ)
    {
        m_nFileId = 0;
        m_nUseId=0;
        m_nFileId=0;
        m_nBlockLen=0;
      memset(m_szBlockData,0,_DEF_BUFFER);
    }
    //包类型
    PackType m_nType;
    int m_nUseId;
    //文件ID
    int m_nFileId;
    //块大小
    int m_nBlockLen;
    //块数据
    char m_szBlockData[_DEF_BUFFER];
}STRU_FILE_BLOCK_RQ;

typedef struct STRU_FILEINFO
{
 public:
    STRU_FILEINFO():m_nFileID(0),m_VideoID(0),m_nFileSize(0),m_nPos(0),m_nUserId(0),pFile(0)
   {
        memset(m_szFilePath,0,_MAX_PATH);
        memset(m_szFileName,0,_MAX_PATH);
        memset(m_szGifPath,0,_MAX_PATH);
        memset(m_szGifName,0,_MAX_PATH);
        memset(m_Hobby,0,_DEF_HOBBY_COUNT);
        memset(m_UserName,0,_MAX_SIZE);
        memset(m_szRtmp,0,_MAX_PATH);
    }
  int m_nFileID;
  int m_VideoID;
  int64_t m_nFileSize;
  int64_t m_nPos;
  int m_nUserId;
  FILE *pFile;
  char m_szFilePath[_MAX_PATH];
  char m_szFileName[_MAX_PATH];
  char m_szGifPath[_MAX_PATH];
  char m_szGifName[_MAX_PATH];
  char m_szFileType[_MAX_SIZE];
  char m_Hobby[_DEF_HOBBY_COUNT];
  char m_UserName[_MAX_SIZE];
  char m_szRtmp[_MAX_PATH];
}FileInfo;

#pragma pack(pop)

