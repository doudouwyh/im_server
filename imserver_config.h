#ifndef __IMSERVER_CONFIG_H__
#define __IMSERVER_CONFIG_H__

#include "common_define.h"
#include <singleton.h>
#include <appframe/config.hpp>
#include <string>

class IMConfig;

#define CONFIG Singleton<IMConfig>::instance()

class IMConfig
{
public:
    int init(const std::string &config_file);
    
public:
    Config config;
    
    //SERVER
    std::string listen_ip;
    int  listen_port;
    int thread_num;

    //REDIS
    std::string con_ip;
    int         con_port;
    std::string con_passwd;
};

#endif
