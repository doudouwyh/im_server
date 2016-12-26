#ifndef _WORKER_POOL_H
#define _WORKER_POOL_H

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

#define READ    0
#define WRITE   1 

using namespace std;

//工作线程
class CThread;
class CThreadPool
{
    public:
        CThreadPool();
        ~CThreadPool();

        typedef vector<CThread*> threads;

        int       init(int num_threads);
        int       add_work(int fd,int flag);
        uint32_t  get_pos();
        void      destroy();
    
        void      set_epollfd(int fd) {epoll_fd = fd;}

    public:

        //工作线程    
        threads   m_work_threads;                  
        int num_threads_alive;      
        int pos;

        pthread_mutex_t  thcount_lock;       
        pthread_cond_t   threads_all_idle;
        pthread_mutex_t  pos_mutex;
    
        //epoll fd
        int epoll_fd;
};


#endif
