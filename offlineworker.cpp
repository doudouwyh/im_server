#include "offlineworker.h"
#include "imserver_config.h"
#include "utility.h"
#include "logger.h"

//全局信息
extern class COnlineInfo     g_OnlineInfo;      //在线信息
extern RedisHandle           redishandle;

//线程初始化
int COfflineThread::init()
{
    return 0;
}

int COfflineThread::start()
{
    if (pthread_create(&pthreadid, NULL, (void *(*) (void *))&run, this) !=0 ) {GLOG_ERROR("thread init: create failed!"); return -1;}
    pthread_detach(pthreadid);

    return 0; 
}

void* COfflineThread::run(void* arg)
{
    COfflineThread* worker = (COfflineThread*)arg;

    while (1) 
    {   
        sleep(5);
        int iret = 0;
        string receiver;
        iret = redishandle.lpop("offlinereceiver",receiver);
        if (iret == false || receiver=="")  continue;
            
        //用户是否在线
        int sockfd;
        pthread_mutex_lock(&g_OnlineInfo.mutex);
        map<string,int>::iterator it = g_OnlineInfo.minfo.find(receiver);
        if (it ==  g_OnlineInfo.minfo.end())
        {
            pthread_mutex_unlock(&g_OnlineInfo.mutex);
            redishandle.lpush("offlinereceiver",receiver.c_str());
            continue;
        }
        sockfd = it->second;
        pthread_mutex_unlock(&g_OnlineInfo.mutex);        

        //遍历
        while (1)
        {
            int len =  0;
            iret = 0;
            iret = redishandle.llen(receiver.c_str(),len);
            if (iret == false)  //如果redis异常，该用户不再push回去
            {
                GLOG_ERROR("redishandle.llen error!,iret:"<<iret);
                break;
            }
            else if (len == 0)
            {
                GLOG_DEBUG("len of "<<receiver.c_str()<<"is zero!");
                break;
            }
            
            string strcontent;
            iret = 0;
            iret = redishandle.lpop_bin(receiver.c_str(),strcontent);
            if (iret == false)
            {
                GLOG_ERROR("lpop_bin offlinereceiver:"<<receiver.c_str()<<"error!");
                break;
            }
            else if (strcontent == "")
            {
                GLOG_DEBUG("receiver:"<<receiver.c_str()<<",content is null");
                continue;
            }       

            int wlen = 0;
            wlen = write(sockfd,strcontent.c_str(),strcontent.length());
            if (wlen != strcontent.size()) 
            {   
                redishandle.lpush_bin(receiver.c_str(),strcontent.c_str(),strcontent.length());
                redishandle.lpush("offlinereceiver",receiver.c_str());
                GLOG_DEBUG("lpush offlinereceiver:"<<receiver.c_str());
                break;
            }               
        }
    }
    return 0;
}


