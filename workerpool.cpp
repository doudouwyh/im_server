#include "workerpool.h"
#include "worker.h"
#include "imserver_config.h"
#include "utility.h"
#include "logger.h"


//线程池
CThreadPool::CThreadPool()
{
    num_threads_alive = 0;
    pos = 0;
    pthread_mutex_init(&thcount_lock, NULL);
    pthread_mutex_init(&pos_mutex, NULL);
    pthread_cond_init(&threads_all_idle, NULL);
}

CThreadPool::~CThreadPool()
{
    pthread_mutex_destroy(&thcount_lock);
    pthread_mutex_destroy(&pos_mutex);
    pthread_cond_destroy(&threads_all_idle);
}

int CThreadPool::init(int num_threads)
{
    if ( num_threads <= 0)
    {
        return -1;
    }

    GLOG_DEBUG("num="<<num_threads);

    for (int i = 0; i < num_threads; i++)
    {
        CThread* th = new CThread(this);
        if (th == NULL)
        {
            GLOG_ERROR("new thread failed!");
            return -1;
        }
        if (th->init(i) != 0)
        {
            GLOG_ERROR("thread init failed!");
            delete th;
            return -1;
        }
        m_work_threads.push_back(th);
        
        GLOG_DEBUG("new thread:"<<i);

    }

    for (int i = 0; i<num_threads; i++)
    {
        if (m_work_threads[i]->start() == -1)  {GLOG_ERROR("thread start failed!i="<<i); return -1;}
    }    
   
    //等待所有线程起来
    pthread_mutex_lock(&thcount_lock); 
    while (num_threads_alive != num_threads) 
    {
        pthread_cond_wait(&threads_all_idle, &thcount_lock);
    }
    pthread_mutex_unlock(&thcount_lock);
    return 0;
}


int CThreadPool::add_work(int fd, int flag)
{
    CJob newjob = {fd,NULL};
        
    uint32_t upos = get_pos();
    if (flag == READ)
    {
        m_work_threads[upos]->readqueue.push(newjob);   
        m_work_threads[upos]->notify();
    }    
    else
    {
        m_work_threads[upos]->writequeue.push(newjob);
        m_work_threads[upos]->notify();
    }
    return 0;
}

uint32_t CThreadPool::get_pos()
{
    pthread_mutex_lock(&pos_mutex) ;
    uint32_t upos = pos % num_threads_alive;
    pos++;
    pthread_mutex_unlock(&pos_mutex) ;
    return upos;
}



void CThreadPool::destroy()
{
    int threads_total = num_threads_alive;
    
    for (int i = 0; i < threads_total; i++)
    {
        m_work_threads[i]->destroy();
        delete m_work_threads[i];
    }
}

