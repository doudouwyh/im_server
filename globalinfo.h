#ifndef _GLOBAL_INFO_H_
#define _GLOBAL_INFO_H_

#include <map>
#include <string>
#include <pthread.h>


using namespace std;

//全局消息类
class COnlineInfo
{
    public:
        COnlineInfo(){pthread_mutex_init(&mutex,NULL);}
        ~COnlineInfo(){pthread_mutex_destroy(&mutex);}
        pthread_mutex_t mutex;
        map<string,int> minfo;
};

class CBufferInfo
{
    public:
        CBufferInfo(){pthread_mutex_init(&mutex,NULL);}
        ~CBufferInfo(){pthread_mutex_destroy(&mutex);}
        pthread_mutex_t mutex;
        map<int,vector<string> > mbuffer;
};


class CHalfPackInfo
{
    public:
        CHalfPackInfo(){pthread_mutex_init(&mutex,NULL);}
        ~CHalfPackInfo(){pthread_mutex_destroy(&mutex);}
        pthread_mutex_t mutex;
        map<int, string> mpackinfo;    
};
#endif



