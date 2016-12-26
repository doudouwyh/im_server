#include "epollworker.h"
#include "imserver_config.h"
#include "utility.h"
#include "logger.h"

//全局信息
extern class COnlineInfo     g_OnlineInfo;      //在线信息
extern class CBufferInfo     g_BufferInfo;      //写信息
extern class CHalfPackInfo   g_HalfPacketInfo;  //不完整包信息
extern vector<RedisHandle>   g_vRedisHandle;


//线程初始化
int CEpollThread::init(CThreadPool* pthreadpool)
{
    // 创建epoll句柄
    m_pthreadpool = pthreadpool; 

    m_iEpollFd = epoll_create(MAX_EVENTS);

    if(m_iEpollFd == -1) { GLOG_ERROR("epoll_create error:"<<strerror(errno)); return -1;}
    
    return 0;
}

int CEpollThread::start()
{
    if (pthread_create(&pthreadid, NULL, (void *(*) (void *))&run, this) !=0 ) {GLOG_ERROR("thread init: create failed!"); return -1;}
    pthread_detach(pthreadid);

    return 0; 
}

void* CEpollThread::run(void* arg)
{
    CEpollThread* worker = (CEpollThread*) arg;
    while(1)
    {
        while (!(worker->qsockets.isempty()))
        {
            CJob* ejob = NULL;
            ejob = worker->qsockets.pull();
            if (ejob == NULL) break;

            struct epoll_event ev;
            ev.events  = EPOLLIN | EPOLLET;
            ev.data.fd = ejob->sockfd;

            if(epoll_ctl(worker->m_iEpollFd,EPOLL_CTL_ADD,ejob->sockfd,&ev) == -1)
            {
                GLOG_ERROR("epoll_ctl error:"<<strerror(errno));
                delete ejob;
                continue;
            }
            delete ejob;
        }

        int nfds = epoll_wait(worker->m_iEpollFd, worker->events, SIMULTANEOUSLY_CONN_MAX_NUM, 1);

        for(int i = 0; i < nfds; ++i)  
        {
            //读事件触发
            if (worker->events[i].events & EPOLLIN == EPOLLIN)
            {   
                //GLOG_DEBUG("read event! fd="<<worker->events[i].data.fd);
                worker->m_pthreadpool->add_work(worker->events[i].data.fd, READ);
            }
            //写事件触发
            else if (worker->events[i].events & EPOLLOUT == EPOLLOUT)
            {   
                //GLOG_DEBUG("write event! fd="<<worker->events[i].data.fd);
                worker->m_pthreadpool->add_work(worker->events[i].data.fd, WRITE);
            }
            //异常 
            else if ((worker->events[i].events & EPOLLERR == EPOLLERR) || (worker->events[i].events & EPOLLHUP == EPOLLHUP))
            {
                GLOG_WARN("EPOLLHUP or EPOLLERROR event happened! fd="<<worker->events[i].data.fd);
                if (epoll_ctl(worker->m_iEpollFd, EPOLL_CTL_DEL, worker->events[i].data.fd, NULL) == -1)
                {
                    GLOG_ERROR("EPOLL_CTL_DEL ERROR!");
                }       
            }
 
        }      
    }

    return 0;
}


