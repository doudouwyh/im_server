#ifndef _WORKER_H
#define _WORKER_H

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

#define READ    0
#define WRITE   1 

using namespace std;

//工作线程
class CThreadPool;
class CThread
{
    public:
        CThread(CThreadPool* thpoolp);
        ~CThread();

        int     init(int id);
        int     start();
        void    destroy();       

        static  void*   run(void* arg);

        int ReadProcess(int fd);
        int WriteProcess(int fd);
        int Sign(string,int fd);
        int Logout(string);
        int Chat(string);
        int GroupChat(string);
        void GroupSend(string strSender,string strContent,set<string> smembers);        

        int Process(string scontent, int sockfd);
        int CmdProcess(string strcmd,string scontent,int fd);

        int Write(int fd, const char* buf, int maxlen);
        int Read(int fd, char* buf, int maxlen);

        void DeleteGlobalInfo(int sockfd);
        void Combine(string strbuf, int fd, char* buf, int& len);
        void AddTimestamp(string oldpack, string& newpack);
        void StoreHalfPack(int fd, string halfpack);        
        void StoreWriteFailMap(string strContent,int fd, int wlen, int len);        

        int  GetPackLenField(const char* src, const char* str);
        int  GetPackCmdField(string pack,string& cmd);
        int  GetPackReceiverField(string pack, string& receiver);
        int  GetPackSenderField(string pack, string& sender);
        int  GetPackCmdOrReceiverField(string pack, const char* sp, int splen, string& field);

        void wait(long timeout = 0LL);
        void notify();


    public:
        int           id;                       
        pthread_t     pthreadid;                  
        CThreadPool*  thpool_p;            
        CJobqueue     writequeue;
        CJobqueue     readqueue;        
        
        pthread_mutex_t pthread_mutex_empty;
        pthread_cond_t  pthread_cond_empty;
};

#endif
