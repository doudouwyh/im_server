#ifndef _EVENT_LISTEN_H_
#define _EVENT_LISTEN_H_

#include <map>
#include <string>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "workerpool.h"
#include "redis_handle.h"
#include "globalinfo.h"
#include "epollworker.h"
#include "offlineworker.h"

#define SIMULTANEOUSLY_CONN_MAX_NUM 20480 
#define BUFFER_SIZE 40960         //暂定一条消息最大40k
#define MAX_EVENTS  20000

using namespace std;

class CThreadPool;
class COfflineThread;
class CEpollThread;

class CEventListen
{
    public:
        CEventListen(){};
        ~CEventListen();

        int     Init();
        int     Listen(uint16_t port,const string& saddr);
        void    Accept();
        
    private:

        int m_iSocketFd;
        int m_iEpollFd;

        struct epoll_event events[SIMULTANEOUSLY_CONN_MAX_NUM];

        CEpollThread*    m_epollthread;
        COfflineThread*  m_offlinethread;
        CThreadPool*     m_thpool;
};

#endif



