#ifndef _EPOLL_WORKER_H
#define _EPOLL_WORKER_H

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h> 
#include <string>
#include <algorithm>

#include "redis_handle.h"
#include "event_listen.h"
#include "queue.h"
#include "workerpool.h"

using namespace std;

//epoll 线程
class CJobqueue;
class CThreadPool;
class CEpollThread
{
    public:
        CEpollThread(){m_iEpollFd = 0; pthreadid = 0; m_pthreadpool = NULL;};
        ~CEpollThread(){};
        
        int     init(CThreadPool* pthreadpool);
        int     start();
        void    destroy() { close(m_iEpollFd); pthread_exit(NULL);}

        static void* run(void* arg);     

        struct epoll_event events[SIMULTANEOUSLY_CONN_MAX_NUM];
        CJobqueue qsockets;
    
    public:
        pthread_t    pthreadid;
        int          m_iEpollFd;
        CThreadPool* m_pthreadpool;
};

#endif
