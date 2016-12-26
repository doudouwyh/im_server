#include <sys/types.h>
#include <sys/stat.h>
#include "event_listen.h"

int main(int argc, char** argv)
{
    int iRet = 0;

    CEventListen el;
    iRet = el.Init();
    if (iRet != 0)  exit(1);        

    el.Accept();

    return 0;
}

