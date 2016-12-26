#ifndef _OFFLINE_WORKER_H
#define _OFFLINE_WORKER_H

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

using namespace std;

//离线处理线程
class COfflineThread
{
    public:
        COfflineThread(){pthreadid = 0;};
        ~COfflineThread(){};
    
        int     init();
        int     start();
        void    destroy(){pthread_exit(NULL);}

        static void* run(void* arg);    

    private:
        pthread_t pthreadid;
};


#endif
