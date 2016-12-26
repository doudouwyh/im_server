#include <sys/socket.h>
#include "logger.h"
#include "event_listen.h"
#include "imserver_config.h"
#include "utility.h"
#include "utils.h"

//全局信息
class COnlineInfo     g_OnlineInfo;      //在线信息
class CBufferInfo     g_BufferInfo;      //写信息
class CHalfPackInfo   g_HalfPacketInfo;  //不完整包信息
vector<RedisHandle>   g_vRedisHandle;
RedisHandle           redishandle;

CEventListen::~CEventListen()
{
    if (m_iSocketFd >= 0)  close(m_iSocketFd);
    if (m_epollthread != NULL)   {m_epollthread->destroy(); delete m_epollthread;}
    if (m_offlinethread != NULL) {m_offlinethread->destroy(); delete m_offlinethread;}
    if (m_thpool != NULL)        {m_thpool->destroy(); delete m_thpool;}
}


int CEventListen::Init()
{
    //daemon
    CStaticUtils::init_daemon();

    //log
    GLOGGER_INIT("log.conf");

    //读取配置文件
    if (CONFIG.init("imserver.conf") != 0) {GLOG_ERROR("config failed!"); return -1;}
    
    GLOG_DEBUG("listen_port:"<<CONFIG.listen_port<<",listen_ip:"<<CONFIG.listen_ip.c_str());

    //端口监听
    if (Listen(CONFIG.listen_port,CONFIG.listen_ip) != 0) { GLOG_ERROR("listen failed!"); return -1;}

    GLOG_DEBUG("redis_port:"<<CONFIG.con_port<<",redis_ip:"<<CONFIG.con_ip.c_str()<<"redis_passwd:"<<CONFIG.con_passwd.c_str());

    //初始化redis连接句柄
    const char* passwd = (strlen(CONFIG.con_passwd.c_str()) == 0)?NULL:(CONFIG.con_passwd.c_str());
    if (redishandle.connect(CONFIG.con_ip.c_str(),CONFIG.con_port,passwd,0) == false){GLOG_ERROR("thread init: connect failed"); return -1;} 

    //redis全局句柄
    g_vRedisHandle.resize(CONFIG.thread_num);
    for (int i=0; i<CONFIG.thread_num; i++)
    {
        if (g_vRedisHandle.at(i).connect(CONFIG.con_ip.c_str(),CONFIG.con_port,passwd,0) == false)
        {
            GLOG_ERROR("thread init: connect failed");
            return -1;
        }
    }

    //工作线程池初始化 
    m_thpool = new CThreadPool();
    if (m_thpool == NULL)  {GLOG_ERROR("new pool failed!"); return -1;}
    if (m_thpool->init(CONFIG.thread_num) != 0) { GLOG_ERROR("pool init failed!"); return -1;}

    //创建epoll监听线程
    m_epollthread = new CEpollThread();
    if (m_epollthread == NULL)  { GLOG_ERROR("new epollthread failed!"); return -1;}
    if ((m_epollthread->init(m_thpool) != 0) || (m_epollthread->start() == -1)) { GLOG_ERROR("epollthread init or start failed!"); return -1;}

    //创建离线处理线程
    m_offlinethread = new COfflineThread();
    if (m_offlinethread == NULL)  { GLOG_ERROR("new offlinethread failed!"); return -1;}
    if ((m_offlinethread->init() != 0) || (m_offlinethread->start() == -1)) { GLOG_ERROR("offlinethread init or start failed!"); return -1;}

    //保存epollfd用于在工作线程中使用
    m_thpool->set_epollfd(m_epollthread->m_iEpollFd);

    return 0;
}


int CEventListen::Listen(uint16_t uiPort, const string &sAddr)
{

    struct  sockaddr_in stAddr;
    struct  linger      stLinger;

    memset(&stAddr, 0, sizeof(stAddr));

    stAddr.sin_family = AF_INET;
    stAddr.sin_port   = htons((unsigned short)uiPort);
    if("" == sAddr)
    {
        stAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        stAddr.sin_addr.s_addr = inet_addr(sAddr.c_str());
    }

    if((m_iSocketFd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        GLOG_ERROR("socket Error:"<<strerror(errno));
        return -1;
    }

    int iReuseAddr = 1;
    if(setsockopt(m_iSocketFd, SOL_SOCKET, SO_REUSEADDR, (char *)&iReuseAddr, sizeof(iReuseAddr)) != 0)
    {
        GLOG_ERROR("setsockopt Error:"<<strerror(errno));
        close(m_iSocketFd);
        return -1;
    }

    stLinger.l_onoff  = 1;
    stLinger.l_linger = 0;

    if(setsockopt(m_iSocketFd, SOL_SOCKET, SO_LINGER, (char *)&stLinger, sizeof(stLinger)) != 0)
    {
        GLOG_ERROR("setsockopt Error:"<<strerror(errno));
        close(m_iSocketFd);
        return -1;
    }

    int iFlags;
    iFlags = fcntl(m_iSocketFd, F_GETFL, 0);
    iFlags |= O_NONBLOCK;
    fcntl(m_iSocketFd, F_SETFL, iFlags);

    if(bind(m_iSocketFd, (struct sockaddr*)&stAddr, sizeof(stAddr)) < 0)
    {
        GLOG_ERROR("bind Error:"<<strerror(errno));
        close(m_iSocketFd);
        return -1;
    }

    if(listen(m_iSocketFd, SOMAXCONN) < 0)
    {
        GLOG_ERROR("listen Error:"<<strerror(errno));
        close(m_iSocketFd);
        return -1;
    }

    // 创建epoll句柄
    m_iEpollFd = epoll_create(MAX_EVENTS);
    if(m_iEpollFd == -1)
    {
        GLOG_ERROR("epoll_create error:"<<strerror(errno));
        close(m_iSocketFd);
        return -1;
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = m_iSocketFd;

    if(epoll_ctl(m_iEpollFd,EPOLL_CTL_ADD,m_iSocketFd,&ev) == -1)
    {
       GLOG_ERROR("epoll_ctl error:"<<strerror(errno));
       close(m_iSocketFd);
       return -1;
    }

    return 0;

}

void CEventListen::Accept()
{
    int nfds = 0;

    while (1)
    {
        nfds = epoll_wait(m_iEpollFd,events,SIMULTANEOUSLY_CONN_MAX_NUM,1);

        for (int i = 0; i < nfds; i++)
        {
            // 客户端有新的连接请求
            if(events[i].data.fd == m_iSocketFd)
            {
                struct sockaddr_in remote_addr;
                int sin_size = sizeof(struct sockaddr_in);
                int client_sockfd;

                for (;;)
                {
                    client_sockfd = accept(m_iSocketFd,(struct sockaddr *)&remote_addr,(socklen_t*)&sin_size);
                    if(-1 == client_sockfd)
                    {
                        if(errno == EINTR)
                        {
                            continue;
                        }
                        else
                        {
                            GLOG_ERROR("accept failed! errno:"<<errno<<"errinfo"<<strerror(errno));
                            break;
                        }
                    }
                    GLOG_DEBUG("accept new connect! fd="<<client_sockfd);

                    //新sock设置成非阻塞
                    int flags = fcntl(client_sockfd, F_GETFL, 0);
                    fcntl(client_sockfd, F_SETFL, flags | O_NONBLOCK);

                    //将新连接sock丢给epoll监听线程
                    CJob job = {client_sockfd,NULL};
                    m_epollthread->qsockets.push(job);                
                } 
          }
       }
    }
}



