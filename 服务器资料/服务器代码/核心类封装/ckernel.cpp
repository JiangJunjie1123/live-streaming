#include "ckernel.h"
#include<QDebug>
#include"TcpClientMediator.h"
#include"TcpServerMediator.h"
#include<QMessageBox>


#define NetMap( a )  m_netPackMap[ a - _DEF_PACK_BASE ]
void CKernel::setNetPackMap()
{
    memset( m_netPackMap , 0 , sizeof(PFUN)*_DEF_PACK_COUNT );

    //协议映射表  key  协议头偏移量  value  函数指针
    //通过 协议头 找到 对应处理函数
    //m_netPackMap[_DEF_PACK_LOGIN_RS - _DEF_PACK_BASE ] = &CKernel::slot_dealLoginRs;
    //NetMap( _DEF_PACK_REGISTER_RS ) = &CKernel::slot_dealRegisterRs;

}



void CKernel::SendData(char *buf, int len)
{
    m_tcpClient->SendData( 0 , buf , len);
}


CKernel::CKernel(QObject *parent) : QObject(parent)
{
    //设置协议映射
    setNetPackMap();
    //加载配置文件
    loadIniFile();


    m_tcpClient = new TcpClientMediator;
    connect( m_tcpClient , SIGNAL(SIG_ReadyData(uint,char*,int))
             , this , SLOT( slot_dealClientData(uint,char*,int) ) );

    //客户端连接真实地址
    m_tcpClient->setIpAndPort(  m_ip.toStdString().c_str() , m_port.toInt() );

}


//配置文件   什么位置?  exe同级目录
//思路 根据目录
//看文件是否存在 存在加载 不存在创建并且写入默认值
//  .ini
//格式
//[组名]
//key=value

//例如
//[net]
//ip=192.168.5.198
//port=8004

#include<QCoreApplication>
#include<QFileInfo>
//配置文件使用的类
#include<QSettings>
void CKernel::loadIniFile()
{
    //默认值
    m_ip = "192.168.5.198";
    m_port = "8004";

    //获取exe 目录   C:/build-debug
    QString path = QCoreApplication::applicationDirPath() + "/config.ini";
    //根据目录
    //看文件是否存在 存在加载 不存在创建并且写入默认值
    QFileInfo info( path );
    if( info.exists() ){
        //存在
        QSettings setting( path , QSettings::IniFormat );
        //打开组
        setting.beginGroup( "net" );
        QVariant strIP = setting.value( "ip" , "" );
        QVariant strPort = setting.value( "port" , "" );
        if( !strIP.toString().isEmpty() ) m_ip = strIP.toString();
        if( !strPort.toString().isEmpty() ) m_port = strPort.toString();
        //关闭组
        setting.endGroup();
    }else{
        //不存在
        QSettings setting( path , QSettings::IniFormat ); //没有会创建
        //打开组
        setting.beginGroup( "net" );
        //设置 key value
        setting.setValue( "ip" , m_ip );
        setting.setValue( "port" , m_port );
        //关闭组
        setting.endGroup();
    }
    qDebug() << "ip:"<<m_ip <<" port:"<< m_port;
}

void CKernel::slot_destory()
{
    qDebug() << __func__ ;
    m_tcpClient->CloseNet();
    delete m_tcpClient;

}


#include<QDebug>
//客户端处理数据
void CKernel::slot_dealClientData(unsigned int lSendIP, char *buf, int nlen)
{
//    QString str = QString("来自服务端:%1").arg( QString::fromStdString( buf ) );
//    QMessageBox::about(  NULL , "提示" , str ); //about 阻塞的 模态窗口
    int type = *(int*)buf;
    qDebug() << __func__ ;

//    switch( type )
//    {
//    case _DEF_PACK_LOGIN_RS:
//        slot_dealLoginRs( lSendIP , buf , nlen);
//        break;
//    }
    //通过协议头 拿到处理函数 并执行
    if( type >= _DEF_PACK_BASE && type < _DEF_PACK_BASE + _DEF_PACK_COUNT ){
        PFUN  pf = NetMap( type );
        if( pf ){
            (this->*pf)( lSendIP , buf , nlen );
        }
    }

    //回收空间
    delete[] buf;
}





