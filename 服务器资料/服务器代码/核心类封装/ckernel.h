#ifndef CKERNEL_H
#define CKERNEL_H

#include <QObject>
#include"packdef.h"


//核心处理类
//单例
//1.构造 拷贝构造 析构 私有化  2.提供静态的公有的获取对象的方法

//协议映射表

//类成员函数指针
class CKernel;
typedef void (CKernel::*PFUN)(unsigned int lSendIP , char* buf , int nlen );

//#include<INetMediator.h>
class INetMediator;

class CKernel : public QObject
{
    Q_OBJECT
private:
    explicit CKernel(QObject *parent = nullptr);
    explicit CKernel( const CKernel & kernel){}
    ~CKernel(){}

    void loadIniFile();
    void setNetPackMap();

signals:

public:
    static CKernel* GetInstance(){
        static CKernel kernel;
        return &kernel;
    }
private slots:
    /// 普通槽函数
    void slot_destory();


    ///网络响应槽函数
    void slot_dealClientData(unsigned int lSendIP , char* buf , int nlen );

private:

    void SendData( char* buf , int len);
private:

    QString m_ip;
    QString m_port;


    INetMediator *m_tcpClient;

    PFUN m_netPackMap[_DEF_PACK_COUNT];
};

#endif // CKERNEL_H
