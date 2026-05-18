#include "clogic.h"

void CLogic::setNetPackMap()
{
    NetPackMap(_DEF_PACK_REGISTER_RQ)    = &CLogic::RegisterRq;
    NetPackMap(_DEF_PACK_LOGIN_RQ)       = &CLogic::LoginRq;
    NetPackMap(_DEF_PACK_UPLOAD_RQ)       = &CLogic::UploadRq;
    NetPackMap(_DEF_PACK_FILE_BLOCK_RQ)       = &CLogic::UploadBlockRq;
}

#define _DEF_COUT_FUNC_    cout << "clientfd:"<< clientfd << __func__ << endl;
#define RootPath "/home/colin/video/"


//t_UserData id , name , password , food ,funny ,ennegy ,dance , music,  video,  outside , edu

void CLogic::Close()
{
    for(std::map<int,FileInfo*>::iterator ite=m_mapFileIDToFileInfo.begin();ite!=m_mapFileIDToFileInfo.end();++ite)
    {
        delete ite->second;
    }
    m_mapFileIDToFileInfo.clear();
    m_mapIDToUserFD.clear();
    m_sql->DisConnect();

}

//注册
void CLogic::RegisterRq(sock_fd clientfd,char* szbuf,int nlen)
{
    //cout << "clientfd:"<< clientfd << __func__ << endl;
    _DEF_COUT_FUNC_
    STRU_REGISTER_RQ * rq=(STRU_REGISTER_RQ*)szbuf;
    STRU_REGISTER_RS rs;
    char sqlBuf[_DEF_SQLIEN]="";
    sprintf(sqlBuf,"select name from t_UserData where name='%s';",rq->name);
    list<string>resList;
    bool  res=m_sql->SelectMysql(sqlBuf,1,resList);
    if(!res)
    {
        cout<<"SelectMysql error"<<sqlBuf<<endl;
        return;
    }
    if(resList.size()>0)
    {
        rs.result=user_is_exist;
    }else{
        char sqlBuf[_DEF_SQLIEN]="";
        sprintf(sqlBuf,"insert into t_UserData(name , password , food ,funny ,ennegy,"
                "dance , music,  video,  outside , edu) values ('%s','%s',%d,%d,%d,%d,%d,%d,%d,%d);",
                rq->name,rq->password,rq->food,rq->funny,rq->ennegy,rq->dance,rq->music,rq->video,
                rq->outside,rq->edu);
        m_sql->UpdataMysql(sqlBuf);

        sprintf(sqlBuf,"select name from t_UserData where name='%s';",rq->name);
        list<string> resID;
        m_sql->SelectMysql(sqlBuf,1,resID);
        int id=0;
        if(resID.size()>0)
        {
            id=atoi(resID.front().c_str());
        }
          // rs.id=id;
           char path[_MAX_PATH];
           sprintf(path,"%sflv/%s/",RootPath,rq->name);// home/colin/video/flv/userName
           mkdir(path,S_IRWXU);
           rs.result=register_success;
    }
    m_tcp->SendData(clientfd,(char*)&rs,sizeof(rs));
}

//登录
void CLogic::LoginRq(sock_fd clientfd ,char* szbuf,int nlen)
{
//    cout << "clientfd:"<< clientfd << __func__ << endl;
    _DEF_COUT_FUNC_
    STRU_LOGIN_RQ * rq= (STRU_LOGIN_RQ*) szbuf;
    STRU_LOGIN_RS rs;
    char buf[_DEF_SQLIEN];
    list<string>resList;
    sprintf(buf,"select password,id from t_UserData where name ='%s';",rq->name);
    bool  res=m_sql->SelectMysql(buf,2,resList);
    if(!res)
    {
        cout<<"SelectMysql error"<<buf<<endl;
        return;
    }
    if(resList.size()>0)
    {
        if(strcmp(resList.front().c_str(),rq->password)==0)
        {
            rs.result=login_success;
            resList.pop_front();
            rs.userid=atoi(resList.front().c_str());
            this->m_mapIDToUserFD[rs.userid]=clientfd;
        }else{
            rs.result=password_error;
        }
    }else{
        rs.result=user_not_exist;
    }
    m_tcp->SendData( clientfd , (char*)&rs , sizeof (rs) );
}

void CLogic::UploadRq(sock_fd clientfd ,char* szbuf,int nlen)
{
    printf("clientfd::%d UploadRq\n",clientfd);
       STRU_UPLOAD_RQ *rq=(STRU_UPLOAD_RQ *)szbuf;

       FileInfo * info=new FileInfo;
       info->m_nPos = 0;
       info->m_nFileSize = rq->m_nFileSize;
       memcpy(info->m_Hobby, rq->m_szHobby, _DEF_HOBBY_COUNT * sizeof(int)); // 注意：之前少了sizeof(int)
       info->m_nUserId = rq->m_UserId;
       info->m_nFileID = rq->m_nFileId;
       info->m_VideoID = 0;

       strcpy(info->m_szFileName, rq->m_szFileName);
       info->m_szFileName[_MAX_PATH - 1] = '\0'; // 强制添加字符串终止符
       strcpy(info->m_szFileType, rq->m_szFileType);

       // 1. 查询用户名
       char sqlstr[_DEF_SQLIEN]="";
       sprintf(sqlstr,"select name from t_UserData where id=%d;",info->m_nUserId);
       list<string> resList;
       if(!m_sql->SelectMysql(sqlstr,1,resList))
       {
           cout<<"SelectMysql error"<<sqlstr<<endl;
           delete info;
           return;
       }
       if(resList.size()<=0)
       {
           delete info;
           return;
       }
       strcpy(info->m_UserName, resList.front().c_str());

       // 2. 创建目录（关键：先建目录，再建文件）
       char dirPath[_MAX_PATH] = {0};
       sprintf(dirPath, "%sflv/%s/", RootPath, info->m_UserName);
       // 创建多级目录（0755是目录权限，EEXIST表示目录已存在，无需报错）
       if (mkdir(dirPath, 0755) == -1 && errno != EEXIST) {
           printf("目录创建失败：%s，错误码：%d\n", dirPath, errno);
           delete info;
           return;
       }

       // 3. 赋值文件路径
       sprintf(info->m_szFilePath,"%sflv/%s/%s", RootPath, info->m_UserName, info->m_szFileName);
       sprintf(info->m_szRtmp,"//%s/%s", info->m_UserName, info->m_szFileName);

       // 4. 处理gif信息
       if(strcmp(rq->m_szFileType,"gif")!=0)
       {
           strcpy(info->m_szGifName, rq->m_szGifName);
           sprintf(info->m_szGifPath,"%sflv/%s/%s", RootPath, info->m_UserName, info->m_szGifName);
       }

       // 5. 打开文件（校验是否成功）
       info->pFile = fopen(info->m_szFilePath, "w"); // 用wb（二进制写入），避免文本模式乱码
       if (info->pFile == NULL) {
           printf("文件打开失败：%s，错误码：%d\n", info->m_szFilePath, errno);
           delete info;
           return;
       }

       // 6. 存入map
       m_mapFileIDToFileInfo[info->m_nFileID] = info;
       printf("UploadRq成功：FileID=%d，文件名=%s，文件大小=%lld\n", info->m_nFileID, info->m_szFileName, info->m_nFileSize);
}


void CLogic::UploadBlockRq(sock_fd clientfd ,char* szbuf,int nlen)
{
    printf("clientfd::%d UploadBlockRq\n",clientfd);
        STRU_FILE_BLOCK_RQ* rq=(STRU_FILE_BLOCK_RQ*)szbuf;

        // 1. 查找FileInfo
        map<int,FileInfo*>::iterator it = m_mapFileIDToFileInfo.find(rq->m_nFileId);
        if(it == m_mapFileIDToFileInfo.end()) {
            printf("FileID=%d 不存在\n", rq->m_nFileId);
            return;
        }
        FileInfo * info = it->second;

        // 2. 校验文件指针
        if (info->pFile == NULL) {
            printf("FileID=%d 文件指针为空\n", rq->m_nFileId);
            m_mapFileIDToFileInfo.erase(it);
            delete info;
            return;
        }

        // 3. 写入文件（校验写入结果）
        int64_t res = fwrite(rq->m_szBlockData, 1, rq->m_nBlockLen, info->pFile);
        if (res != rq->m_nBlockLen) {
            printf("FileID=%d 写入失败：预期%lld字节，实际%lld字节\n", rq->m_nFileId, rq->m_nBlockLen, res);
            fclose(info->pFile);
            m_mapFileIDToFileInfo.erase(it);
            delete info;
            return;
        }

        // 4. 更新已写入位置
        info->m_nPos += res;

        // 5. 打印关键日志（用于排查）
        printf("【FileID=%d】块长度=%lld，已写入=%lld，文件总大小=%lld\n",
               rq->m_nFileId, rq->m_nBlockLen, info->m_nPos, info->m_nFileSize);

        // 6. 优化判断逻辑（优先以已写入位置为准，彻底解决满块问题）
        bool isUploadComplete = (info->m_nPos >= info->m_nFileSize) || (rq->m_nBlockLen <= 0);
        if (isUploadComplete)
        {
            cout<<"1234"<<endl;
            fclose(info->pFile);

            // 非gif文件入库
            if(strcmp(info->m_szFileType,"gif")!=0)
            {
                cout<<"1234"<<endl;
                char sqlstr[_DEF_SQLIEN]="";
                sprintf(sqlstr,"insert into t_VideoInfo (userId,videoName,picName,videoPath,picPath,rtmp,food,funny,ennegy,dance,music,video,outside,edu,hotdegree)values(%d,'%s','%s','%s','%s','%s',%d,%d,%d,%d,%d,%d,%d,%d,%d);",
                        rq->m_nUseId, info->m_szFileName, info->m_szGifName, info->m_szFilePath, info->m_szGifPath, info->m_szRtmp,
                        info->m_Hobby[0], info->m_Hobby[1], info->m_Hobby[2], info->m_Hobby[3], info->m_Hobby[4], info->m_Hobby[5],
                        info->m_Hobby[6], info->m_Hobby[7], 0);
                printf("%s\n",sqlstr);
                if(!m_sql->UpdataMysql(sqlstr))
                {
                    cout<<"SelectMysql error"<<sqlstr<<endl;
                }

                // 发送响应
                STRU_UPLOAD_RS rs;
                memset(&rs, 0, sizeof(rs));
                rs.m_lResult=1;
                m_tcp->SendData(clientfd,(char*)&rs,sizeof(rs));
            }

            // 清理资源
            m_mapFileIDToFileInfo.erase(it);
            delete info;
            info = NULL;
            printf("FileID=%d 上传完成，资源已清理\n", rq->m_nFileId);
        }
}
