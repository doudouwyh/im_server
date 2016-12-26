#ifndef _QUEUE_H
#define _QUEUE_H

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


#define SIMULTANEOUSLY_CONN_MAX_NUM 20480  

using namespace std;

class CJob
{
    public:
        int sockfd;
        CJob* next;
};

class CJobqueue
{
    public:
        CJobqueue():len(0),front(NULL),rear(NULL)
        {
            pthread_mutex_init(&rwmutex,NULL);
        }

        ~CJobqueue()
        {
            pthread_mutex_destroy(&rwmutex);
            while (front)
            {
                CJob *tmp = front;
                front = front->next;
                delete tmp;
            }
        }

        int isempty()
        {
            return (len == 0)? 1 : 0;
        }


        void  push(CJob newjob)
        {
            CJob* job = new CJob;
            job->sockfd = newjob.sockfd;
            job->next   = NULL;

            pthread_mutex_lock(&rwmutex);

            if (len == 0)
            {
                front = job;
                rear  = job;
            }
            else
            {
                rear->next = job;
                rear       = job;  //从尾端插入           
            }
    
            len++;
    
            pthread_mutex_unlock(&rwmutex);
        }

        CJob* pull()
        {
            pthread_mutex_lock(&rwmutex);
            CJob* job;
            job = front;

            if (len == 0)
            {
                pthread_mutex_unlock(&rwmutex);
                return NULL;       
            }
            else if (len == 1)
            {
                front = NULL;
                rear  = NULL;
                len   = 0;
            }
            else
            {
                front = job->next;
                len--;
            }
   
            job->next = NULL; 
            pthread_mutex_unlock(&rwmutex);

            return job;
        }


    public:
        pthread_mutex_t rwmutex;             
        CJob*    front;                         
        CJob*    rear;                          
        int      len;                           
};

#endif
