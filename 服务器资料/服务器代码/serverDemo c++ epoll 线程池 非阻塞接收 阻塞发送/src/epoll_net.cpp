#include "epoll_net.h"


bool Epoll_Net::InitNet(int port, void (*recv_callback)(int, char *, int))
{
    m_recv_callback = recv_callback;
    InitThreadPool();

    int flags = 1;
    int ret = 0;
    m_listenEv = new myevent_s(this);

    //creat a tcp socket
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if ( m_listenfd  == -1 ){
        perror("create socket error");
        return false;
    }

    //set REUSERADDR
    ret = setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&flags, sizeof(flags));
    if ( ret == -1 ){
        perror("setsockopt error");
        return false;
    }

    //监听套接字m_listenfd 采用 LT 非阻塞模式
    //set NONBLOCK
    setNonBlockFd( m_listenfd );

    struct sockaddr_in local_addr;
    bzero( &local_addr , sizeof(sockaddr_in) );
    //set address
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    //bind addr
    ret = bind(m_listenfd, (struct sockaddr *)&local_addr, sizeof(struct sockaddr_in));
    if( ret == -1 ) {
        perror("bind error");
        close(m_listenfd);
        return false;
    }

    if (listen(m_listenfd, 128) == -1 ){
        perror("listen error");
        close(m_listenfd);
        return false;
    }

    //create epoll
    m_epoll_fd = epoll_create( MAX_EVENTS );

    m_listenEv->eventset(m_listenfd ,m_epoll_fd );
    //将监听套接字 添加到epoll中 , 模式LT 非阻塞
    m_listenEv->eventadd( EPOLLIN);


    return true;
}

bool Epoll_Net::InitThreadPool()
{

    m_threadpool = new thread_pool;

    //创建拥有10个线程的线程池 最大线程数200 环形队列最大值50000
    if( (m_threadpool->Pool_create(200,10,50000)) == false )
    {
        perror("Create Thread_Pool Failed:");
        exit(-1);
    }

    return true;
}

void Epoll_Net::EventLoop()
{
    printf("EventLoop:server running\n");
    int  i = 0;
    while (1) {

        /* 等待事件发生 */
        int nfd = epoll_wait( m_epoll_fd, events, MAX_EVENTS+1, 1000);
        if (nfd < 0) {
            printf("epoll_wait error, exit\n");
//            break;
            continue;
        }
        for (i = 0; i < nfd; i++) {
            struct myevent_s *ev = (struct myevent_s *)events[i].data.ptr;
            int fd = ev->fd;
            if ( (events[i].events & EPOLLIN) ) {
                if( fd == m_listenfd )
                    accept_event();
                else
                    recv_event( ev );
            }
            if ((events[i].events & EPOLLOUT) ) {
                epollout_event( ev );
            }
        }
    }
}

void Epoll_Net::accept_event()
{
    struct sockaddr_in caddr;
    socklen_t len = sizeof(caddr);
    int clientfd ;
    if ((clientfd = accept(m_listenfd, (struct sockaddr *)&caddr, &len)) == -1) {
        if (errno != EAGAIN && errno != EINTR) {
            /* 暂时不做出错处理 */
        }
        printf("%s: accept, %s\n", __func__, strerror(errno));
        return;
    }
//    //客户端接收使用阻塞套接字 更简单 适合入门
//    //设置非阻塞
//    setNonBlockFd( clientfd );

    //设置接收缓冲区大小
    setRecvBufSize( clientfd );
    //设置发送缓冲区大小
    setSendBufSize( clientfd );
    //设置 无延迟
    setNoDelay( clientfd );

    myevent_s * clientEv = new myevent_s(this);
    clientEv->eventset( clientfd , m_epoll_fd );
    // 连接客户端套接字 EPOLL 监听 ET事件
    clientEv->eventadd(  EPOLLIN|EPOLLET );

    //m_mapSockfdToEvent[clientfd] = clientEv;
    m_mapSockfdToEvent.insert(clientfd , clientEv);

    printf("new connect [%s:%d][time:%ld] \n",
           inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port), time(NULL) );
    return;
}




void Epoll_Net::recv_event( myevent_s *ev)
{
    //多个套接字接收在一个线程
    recv_task(ev);
}

void* Epoll_Net::recv_task(void* arg)
{
    myevent_s * ev = (myevent_s*)arg;
    //利用全局指针 方便操作
    Epoll_Net * pthis = ev->pNet;

    //接收和处理分离
    int nRelReadNum = 0;
    int nPackSize = 0;

    while( 1 ){ //循环接收数据直到 EAGAIN 或 EWOULDBLOCK
        //首先看当前是接收包大小, 还是数据包
        if( ev->recvBuffer.len == 0 && ev->recvBuffer.pos == 0 ){
            //接收包大小
            //首先看接收是否足够 4字节
            int nBytesAvailable = 0;
            ioctl(ev->fd, FIONREAD, &nBytesAvailable);
            if( nBytesAvailable < 4 )//不够 退出 一般够
                break;

                //够 读取 包大小 并按照包大小创建空间 pos len 赋值
                nRelReadNum = recv(ev->fd,&nPackSize,sizeof(nPackSize) , MSG_DONTWAIT ); //非阻塞接收
                if (nRelReadNum < 0) {
                    //下一次接收
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return NULL;
                    }
                    ev->eventdel();
                    close(ev->fd);
                    //回收event结构
                    pthis->m_mapSockfdToEvent.erase( ev->fd );
                    delete ev;
                    return NULL;
                } else if (nRelReadNum == 0) {
                    ev->eventdel();
                    close(ev->fd);
                    //回收event结构
                    pthis->m_mapSockfdToEvent.erase( ev->fd );
                    delete ev;
                    return NULL;
                } else {
                    //读取 包大小 并按照包大小创建空间 pos len 赋值
                    if( nPackSize > 0 ){
                        ev->recvBuffer.len = nPackSize;
                        ev->recvBuffer.pos = 0;
                        ev->recvBuffer.buf = new char[ev->recvBuffer.len];
                    }
                }
        }
        //接收数据包
        while( ev->recvBuffer.pos < ev->recvBuffer.len ){
            int waitRead = ev->recvBuffer.len - ev->recvBuffer.pos;
            nRelReadNum = recv(ev->fd, ev->recvBuffer.buf + ev->recvBuffer.pos , waitRead , MSG_DONTWAIT );
            if (nRelReadNum < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return NULL;
                }
                //出现错误
                ev->eventdel();
                close(ev->fd);
                //回收event结构
                pthis->m_mapSockfdToEvent.erase( ev->fd );
                delete ev;
                return NULL;
            } else if (nRelReadNum == 0) { //断开连接
                ev->eventdel();
                close(ev->fd);
                //回收event结构
                pthis->m_mapSockfdToEvent.erase( ev->fd );
                delete ev;
                return NULL;
            } else {
                ev->recvBuffer.pos += nRelReadNum;
                if( ev->recvBuffer.pos == ev->recvBuffer.len  ) //凑成一个包
                {
                    //接收和处理分离 跑到线程池里其他线程处理 , 避免处理影响接收
                    DataBuffer * buffer = new DataBuffer(ev->pNet , ev->fd , ev->recvBuffer.buf , ev->recvBuffer.len );
                    pthis->m_threadpool->Producer_add(  Buffer_Deal , (void*) buffer );
                    ev->recvBuffer.buf = nullptr;//这里不可回收数据 多线程要在执行完 销毁
                    ev->recvBuffer.pos = ev->recvBuffer.len = 0;
                }
            }
        }
    }
    return NULL;
}

void * Epoll_Net::Buffer_Deal( void * arg )
{
    DataBuffer * buffer = (DataBuffer *)arg;
    if( !buffer ) return NULL;

    buffer->pNet->m_recv_callback(buffer->sockfd,buffer->buf,buffer->nlen);

    if(buffer->buf != NULL)
    {
        delete [] buffer->buf;
        buffer->buf = NULL;
    }
    delete buffer;
    return 0;
}

void Epoll_Net::epollout_event( myevent_s *ev )
{
    // epoll LT模式 阻塞模式 发送阻塞 , 不用监听EPOLLOUT事件
}



int Epoll_Net::SendData(int fd, char *szbuf, int nlen)
{
    // 发送不可分割 避免多线程并发
    // 先包大小, 再数据包 , 一次放入缓冲区

    /*
     +--------------+------------------+---------------------+
     |<-- 4bytes -->|<-- 4bytes协议头 ->|<--  协议其他内容    ->|
     +--------------+------------------+---------------------+
     |<- packsize ->|<------------ 数据包 struct ------------>|
     * */

    int nPackSize = nlen + 4;
    vector<char> vecbuf( nPackSize , 0);
    //vecbuf.resize( nPackSize );

    char* buf = &* vecbuf.begin();
    char* tmp = buf;
    *(int*)tmp = nlen;//按四个字节int写入
    tmp += sizeof(int );
    memcpy( tmp , szbuf , nlen );

    int res = send( fd,(const char *)buf, nPackSize ,0);

    return res;
}

void Epoll_Net::setNonBlockFd(int fd)
{
    int flags = 0;
    flags = fcntl(fd, F_GETFL, 0);
    int ret = fcntl(fd, F_SETFL, flags|O_NONBLOCK);
    if( ret == -1)
        perror("setNonBlockFd fail:");
}

void Epoll_Net::setRecvBufSize( int fd)
{
    //接收缓冲区
    int nRecvBuf = 256*1024;//设置为 256 K
    setsockopt(fd,SOL_SOCKET,SO_RCVBUF,(const char*)&nRecvBuf,sizeof(int));
}

void Epoll_Net::setSendBufSize( int fd)
{
    //发送缓冲区
    int nSendBuf=128*1024;//设置为 128 K
    setsockopt(fd,SOL_SOCKET,SO_SNDBUF,(const char*)&nSendBuf,sizeof(int));
}

#include<netinet/tcp.h>
void Epoll_Net::setNoDelay( int fd)
{
    //nodelay
    int value = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY ,(char *)&value, sizeof(int));
}
