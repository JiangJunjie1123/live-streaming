#include <TCPKernel.h>



int main(int argc,char *argv[])
{
    int port = 8000;
    if( argc >= 2 )
    {
        port = atoi(argv[1]);
    }
    TcpKernel * pKernel =  TcpKernel::GetInstance();

    //开启服务 给定端口, 可以使用输入的port
    if (!pKernel->Open( port )) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    pKernel->StartHeartbeatCheck();
    cout << "Server running on port:" << port << endl ;
    // 事件循环 : 循环监听事件
    pKernel->EventLoop();

    pKernel->Close();

    return 0;
}
