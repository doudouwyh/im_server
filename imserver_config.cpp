#include "imserver_config.h"
#include <vector>
#include <string_tool.h>
#include <iostream>

using namespace std;

int IMConfig::init(const string &config_file)
{
    int rv = 0;
    rv = config.load(config_file);
    if(rv != 0)
    {
        return -1;
    }
    config.get_string_param("SERVER", "ip", listen_ip);
    config.get_int_param("SERVER", "port",  listen_port);
    config.get_int_param("SERVER", "thread_num", thread_num);

    //REDIS
    config.get_string_param("REDIS", "ip", con_ip);
    config.get_int_param("REDIS", "port",  con_port);
    config.get_string_param("REDIS", "passwd",  con_passwd);

    return 0;
}
