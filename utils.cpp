#include "utils.h"

/*
   author:      Aidy
   last_time：  2011-12-05
   use:         utils functions
 */

void CStaticUtils::init_daemon()
{   
    pid_t pid;
    pid = fork();
    if (pid > 0)
        exit(0); 
    else if (pid < 0)
    {   
        exit(1);
    }
    
    //创建会话
    if(setsid() == - 1)
    {   
        perror("setsid error!");
        exit(1);
    }
    setpgid(0, 0);
    
    //umask 
    umask(0027);
    
    //忽略信号
    signal(SIGHUP, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGURG, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGALRM, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    
    pid = fork();
    if (pid > 0)
        exit(0); 
    else if (pid < 0)
    {   
        perror("fail to fork.");
        exit(1);
    }
    close(0);
}


int CStaticUtils::strfind(const char* ins, const char* str, int s)
{
    const char* in = ins + s;
    char c;
    size_t len;
    int pos = -1;
    c = *str++;
    if(!c)
        return -1;
    len = strlen(str);
    do
    {
        char sc;
        do
        {
            sc = *in++;
            pos++;
            if(!sc)
                return -1;
        }
        while(sc != c);
    }
    while(strncmp(in, str, len) != 0);
    return pos;
}


