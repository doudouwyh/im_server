#ifndef _UTILS_H
#define _UTILS_H

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
/*
   author:      Aidy
   last_time£º  2011-12-05
   use:         utils functions
 */

namespace CStaticUtils 
{
    void init_daemon();
    int strfind(const char* ins, const char* str, int s = 0);
};

#endif                          //_UTILS_H
