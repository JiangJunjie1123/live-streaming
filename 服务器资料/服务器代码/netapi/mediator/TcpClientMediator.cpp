#include "TcpClientMediator.h"

#include"TcpClient.h"

TcpClientMediator::TcpClientMediator()
{
	//new 网络对象
	m_pNet = new TcpClient(this);
}
TcpClientMediator::~TcpClientMediator()  //使用时, 父类指针指向子类, 使用虚析构
{
	if( m_pNet )
	{
		delete m_pNet;
		m_pNet = NULL;
	}
}
//初始化网络
bool TcpClientMediator::OpenNet(const char *szBufIP, unsigned short port)
{

    strcpy_s( m_szBufIP, sizeof(m_szBufIP), szBufIP);
    m_port = port;

    return m_pNet->InitNet(szBufIP , port);
} 
//关闭网络
void TcpClientMediator::CloseNet()
{
	m_pNet->UnInitNet();
} 
//发送 : 同时兼容tcp udp 
bool TcpClientMediator::SendData( unsigned int lSendIP , char* buf , int nlen )
{
    Q_UNUSED(lSendIP);
    if( IsConnected() )
        return m_pNet->SendData( 0 , buf, nlen);
    else
    {
        m_pNet->UnInitNet();
        delete m_pNet;
        m_pNet = new TcpClient(this);
        if( this->OpenNet( m_szBufIP , m_port ) )
        {
            return m_pNet->SendData( 0,buf,nlen);
        }
        else
        {
            return -1;
        }
    }
}
#include<iostream>
//处理 
void TcpClientMediator::DealData(unsigned int lSendIP , char* buf , int nlen )
{
    QByteArray data(buf, nlen);  // 深拷贝，Qt 管理生命周期
    delete[] buf;                 // 立即释放堆内存（RecvData 中 new 的）
    Q_EMIT SIG_ReadyData( lSendIP , data);
}

void TcpClientMediator::disConnect()
{
    Q_EMIT SIG_disConnect();
}

bool TcpClientMediator::IsConnected()
{
    return m_pNet->IsConnected();
}

