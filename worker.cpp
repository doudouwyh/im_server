#include "workerpool.h"
#include "worker.h"
#include "imserver_config.h"
#include "utility.h"
#include "logger.h"
#include "utils.h"

const string CMD_LOGIN      ="1000";
const string CMD_LOGOUT     ="1001";
const string CMD_CHAT       ="1002";
const string CMD_FILE       ="1003";
const string CMD_GROUPCHAT  ="1004";

//全局信息
extern class COnlineInfo     g_OnlineInfo;      //在线信息
extern class CBufferInfo     g_BufferInfo;      //写信息
extern class CHalfPackInfo   g_HalfPacketInfo;  //不完整包信息
extern vector<RedisHandle>   g_vRedisHandle;

//map
class map_value_finder
{
    public:
       map_value_finder(int& cmp):m_i_cmp(cmp){}
       bool operator ()(const std::map<string, int>::value_type &pair)
       {
            return pair.second == m_i_cmp;
       }
    private:
        int& m_i_cmp;                    
};

CThread::CThread(CThreadPool* thpoolp)
{
    pthread_mutex_init(&pthread_mutex_empty, NULL);
    pthread_cond_init(&pthread_cond_empty, NULL);
    
    thpool_p = thpoolp;
}

CThread::~CThread()
{
    pthread_mutex_destroy(&pthread_mutex_empty);
    pthread_cond_destroy(&pthread_cond_empty);
}


//timeout: millisecond
void CThread::wait(long timeout)
{
    if(timeout == 0L)
    {   
        int iret = pthread_cond_wait(&pthread_cond_empty, &pthread_mutex_empty);
        assert(iret == 0); 
    }   
    else
    {   
        struct timespec abstime;

        long now = 0;
        struct timeval outtime;
        gettimeofday(&outtime, NULL);

        now = outtime.tv_sec * 1000 + outtime.tv_usec / 1000;  //毫秒

        abstime.tv_sec = (now + timeout) / 1000;   
        abstime.tv_nsec = ( (now + timeout) % 1000) * 1000;  //纳秒

        int result = pthread_cond_timedwait(&pthread_cond_empty, &pthread_mutex_empty, &abstime);
        if(result == ETIMEDOUT)
        {   
        }   
    }   
}

void CThread::notify()
{
    int iret = pthread_cond_signal(&pthread_cond_empty);
    assert(iret == 0); 
}




//线程初始化
int CThread::init(int tid)
{
    id  = tid;
    GLOG_DEBUG("init success!");

    return 0;
}

int CThread::start()
{
    if (pthread_create(&pthreadid, NULL, (void *(*) (void *))&run, this) !=0 ) {GLOG_ERROR("thread init: create failed!"); return -1;}

    pthread_detach(pthreadid);

    return 0; 
}

void* CThread::run(void* arg)
{
    CThread* pThread = (CThread*)arg;

    //启动进程数增1
    pthread_mutex_lock(&pThread->thpool_p->thcount_lock);
    pThread->thpool_p->num_threads_alive++;
    pthread_cond_signal(&pThread->thpool_p->threads_all_idle);
    pthread_mutex_unlock(&pThread->thpool_p->thcount_lock);

    while(1)
    {
        while(pThread->readqueue.isempty() && pThread->writequeue.isempty())
        {
            pThread->wait(0);           
        }

        //read
        if (!(pThread->readqueue.isempty()))
        {
            CJob* readjob = NULL;
            readjob = pThread->readqueue.pull();
            if (readjob != NULL)
            {   
                pThread->ReadProcess(readjob->sockfd);
                delete readjob;
            }             
        }
        
        //write
        if (!(pThread->writequeue.isempty()))
        {
            CJob* writejob = NULL;
            writejob = pThread->writequeue.pull();
            if (writejob != NULL)
            {
               pThread->WriteProcess(writejob->sockfd);
               delete writejob;
            }
        }
    }
    return 0;
}

//登录  保存 token-sockfd
int CThread::Sign(string strContent,int fd)
{

    vector<string> vStr;
    string sp="##";
    StringTool::split(strContent,sp,vStr);
    if (vStr.size() != 4) return -1; 

    pthread_mutex_lock(&(g_OnlineInfo.mutex));
    map<string,int>::iterator it = g_OnlineInfo.minfo.find(vStr[2]);
    if (it != g_OnlineInfo.minfo.end()) //重复登录
    {
        int sockfd = it->second;
        epoll_ctl(thpool_p->epoll_fd, EPOLL_CTL_DEL, sockfd, NULL);
        close(sockfd);       
    }
    g_OnlineInfo.minfo[vStr[2]] = fd;
    pthread_mutex_unlock(&(g_OnlineInfo.mutex));
       
    return 0;
}


//退出  token
int CThread::Logout(string strContent)
{
    int sockfd = -1;
    vector<string> vStr;
    string sp="##";
    StringTool::split(strContent,sp,vStr);
    if (vStr.size() != 4) return -1;

    //清除登录信息
    pthread_mutex_lock(&(g_OnlineInfo.mutex));
    map<string,int>::iterator it = g_OnlineInfo.minfo.find(vStr[2]);       
    if (it != g_OnlineInfo.minfo.end())
    {
        sockfd = it->second;   
        epoll_ctl(thpool_p->epoll_fd, EPOLL_CTL_DEL, sockfd, NULL);
        close(sockfd);
        g_OnlineInfo.minfo.erase(it);
    } 
    pthread_mutex_unlock(&(g_OnlineInfo.mutex));

    if (sockfd != -1)
    {    
        //清除写失败的信息,暂不存入离线
        pthread_mutex_lock(&(g_BufferInfo.mutex));
        map<int,vector<string> >::iterator itor = g_BufferInfo.mbuffer.find(sockfd);
        if (itor != g_BufferInfo.mbuffer.end())
        {   
            g_BufferInfo.mbuffer.erase(itor);
        }    
        pthread_mutex_unlock(&(g_BufferInfo.mutex));

        //清除读到的不完整包信息
        pthread_mutex_lock(&(g_HalfPacketInfo.mutex));
        map<int,string>::iterator iter =  g_HalfPacketInfo.mpackinfo.find(sockfd);
        if (iter != g_HalfPacketInfo.mpackinfo.end())
        {   
            g_HalfPacketInfo.mpackinfo.erase(iter);
        }   
        pthread_mutex_unlock(&(g_HalfPacketInfo.mutex));
    }

    return 0;   
}

//聊天 或 文件，  strContent: len##cmd##sender$$receiver$$content 
int CThread::Chat(string strContent)
{

    //获取receiver
    string strreceiver;
    if (GetPackReceiverField(strContent,strreceiver) != 0)
    {
        GLOG_ERROR("packet wrong, can not get receiver! strContent:"<<strContent.c_str());
        return -1;
    }
    GLOG_DEBUG("Chat...receiver:"<<strreceiver.c_str());

    //如果receiver离线则写入redis
    int sockfd;
    pthread_mutex_lock(&(g_OnlineInfo.mutex));
    map<string,int>::iterator it = g_OnlineInfo.minfo.find(strreceiver);
    if (it == g_OnlineInfo.minfo.end())
    {
        GLOG_DEBUG("can not find the login info ,token:"<<strreceiver.c_str());

        pthread_mutex_unlock(&(g_OnlineInfo.mutex));
        char buf[BUFFER_SIZE] = {0};
        memcpy(buf,strContent.c_str(),strContent.length());
        g_vRedisHandle.at(id).lpush("offlinereceiver",strreceiver.c_str());
        g_vRedisHandle.at(id).lpush_bin(strreceiver.c_str(),buf,strContent.length());
        return 0;
    }    
    else
    {
        //获取sockfd
        sockfd = it->second;
        pthread_mutex_unlock(&(g_OnlineInfo.mutex)); 
    }

    //往receiver写
    int len = strContent.length();
    char scontent[BUFFER_SIZE] = {0};
    memcpy(scontent,strContent.c_str(),len);

    int wlen = Write(sockfd,scontent,len);
    if (wlen != len)
    {
        StoreWriteFailMap(strContent,sockfd,wlen,len);
    }
    return 0;
}

int CThread::GroupChat(string strContent)
{
    //获取receiver
    string strreceiver;
    if (GetPackReceiverField(strContent,strreceiver) != 0)
    {
        GLOG_ERROR("GroupChat, packet wrong, can not get receiver! strContent:"<<strContent.c_str());
        return -1;
    }
    GLOG_DEBUG("GroupChat...receiver:"<<strreceiver.c_str());
   
    //获取sender
    string strsender;
    if (GetPackSenderField(strContent,strsender) != 0)
    {
        GLOG_ERROR("GroupChat, packet wrong, can not get sender! strContent:"<<strContent.c_str());
        return -1;
    }

    //查询redis
    set<string> smembers;
    if (g_vRedisHandle.at(id).smembers(strreceiver.c_str(),smembers) == false)
    {
        GLOG_ERROR("Redis handle error!");
        return -1;
    }
    

    //给每个人发
    GroupSend(strsender,strContent,smembers);
 
      
    return 0;
}

void CThread::GroupSend(string strSender,string strContent, set<string> smembers)
{
    char buf[BUFFER_SIZE] = {0};
    int  maxlen = strContent.length();
    memcpy(buf,strContent.c_str(),maxlen);

    set<string>::iterator it = smembers.begin();
    while (it != smembers.end())
    {
        //首字符标识是否为群主等
        string receiver(it->c_str()+1, it->length()-1);

        //发送者本身不需要再发送了
        if (strSender.compare(receiver) == 0) { it++; continue;}

        pthread_mutex_lock(&(g_OnlineInfo.mutex));
        map<string,int>::iterator itor = g_OnlineInfo.minfo.find(receiver);    
        if (itor != g_OnlineInfo.minfo.end())
        {   
            //发送
            int sockfd = itor->second;
            pthread_mutex_unlock(&(g_OnlineInfo.mutex));
            int wlen = Write(sockfd, buf, maxlen);
            if (wlen != maxlen) //写失败
            {
                StoreWriteFailMap(strContent,sockfd,wlen,maxlen);
            }             
        }   
        else //离线，存入redis
        {
            pthread_mutex_unlock(&(g_OnlineInfo.mutex));
            g_vRedisHandle.at(id).lpush("offlinereceiver",receiver.c_str());
            g_vRedisHandle.at(id).lpush_bin(receiver.c_str(),buf,maxlen);             
        }
        it++;
    }   
}


void CThread::Combine(string strbuf,int fd, char* buf, int& len)
{
    //先查看上次读到的不完整包
    string strHalfpack;
    pthread_mutex_lock(&(g_HalfPacketInfo.mutex));
    map<int,string>::iterator it =  g_HalfPacketInfo.mpackinfo.find(fd);
    if (it == g_HalfPacketInfo.mpackinfo.end())
    {   
        pthread_mutex_unlock(&(g_HalfPacketInfo.mutex));
    }   
    else
    {    
        strHalfpack = it->second;
        g_HalfPacketInfo.mpackinfo.erase(it);//删除该socket
        pthread_mutex_unlock(&(g_HalfPacketInfo.mutex));
    }   

    if (strHalfpack.length() != 0)
    {   
        memcpy(buf,strHalfpack.c_str(),strHalfpack.length());
    }    
    memcpy(buf+strHalfpack.length(),strbuf.c_str(),strbuf.length());   
    len = strHalfpack.length()+strbuf.length();
}

void CThread::StoreHalfPack(int fd, string halfpack)
{
    pthread_mutex_lock(&(g_HalfPacketInfo.mutex));
    g_HalfPacketInfo.mpackinfo.insert(map<int,string>::value_type(fd,halfpack));
    pthread_mutex_unlock(&(g_HalfPacketInfo.mutex));
}

int CThread::GetPackLenField(const char* src, const char* str)
{
    //分割，获取长度
    //len##cmd##sender$$receiver$$content
    char lenfield[100] = {0};
    int index = CStaticUtils::strfind(src,str);
    if (index == -1) {GLOG_ERROR("sfrfind error!"); return -1;}
    strncpy(lenfield,src,index);    

    return strtoul(lenfield,NULL,0);    
}

int CThread::GetPackCmdField(string pack, string& cmd)
{
    return GetPackCmdOrReceiverField(pack,"##",2,cmd);
}

int CThread::GetPackReceiverField(string pack, string& receiver)
{   
    return GetPackCmdOrReceiverField(pack,"$$",2,receiver);
}

int CThread::GetPackSenderField(string pack, string& sender)
{
    int idx = CStaticUtils::strfind(pack.c_str(),"$$");
    if (idx == -1) {GLOG_DEBUG("stffind error! pack:"<<pack.c_str()); return -1;}
    string strhalfpack(pack.c_str(),idx);
    vector<string> vstr;
    string sp="##";
    StringTool::split(strhalfpack,sp,vstr);
    if (vstr.size() != 3) {GLOG_DEBUG("stffind error! halfpack:"<<strhalfpack.c_str()); return -1;}
    
    sender = vstr[2];

    return 0;
}

int CThread::GetPackCmdOrReceiverField(string pack, const char* sp,int splen,string& field)
{
    int idx = CStaticUtils::strfind(pack.c_str(),sp);
    if (idx == -1) {GLOG_DEBUG("stffind error! pack:"<<pack.c_str()); return -1;}
    int start = idx + splen; 
    int end   = CStaticUtils::strfind(pack.c_str()+start,sp);
    if (end == -1) {GLOG_DEBUG("stffind error! pack:"<<pack.c_str()); return -1;}

    char sfield[100] = {0};
    memcpy(sfield,pack.c_str()+start,end);
    string strfield(sfield,end);
    field = strfield;
    return 0;    
}


void CThread::AddTimestamp(string oldpack, string& newpack)
{
    //获取时戳
    time_t now = time(0);
    string timestamp;
    stringstream ss;
    ss << "##"<<now;
    timestamp = ss.str();


    //获取最终len
    string slen;
    int packlen = GetPackLenField(oldpack.c_str(),"##");
    int templen = packlen + timestamp.length();
    ss.clear();
    ss.str("");
    ss << templen;
    slen = ss.str();

    int index = 0;
    ss.clear();
    ss.str("");
    ss << packlen;
    index = ss.str().length();
    int newlen = templen + slen.length() - index; //打上时戳后最终长度

    ss.clear();
    ss.str("");
    ss << newlen;
    string snewlen;
    snewlen = ss.str();

    GLOG_DEBUG("pack newlen:"<<newlen<<",timestamp:"<<timestamp.c_str()<<",snewlen:"<<snewlen.c_str());

    //组装新包
    char stmp[BUFFER_SIZE] = {0};
    memcpy(stmp,snewlen.c_str(),snewlen.length());
    memcpy(stmp+snewlen.length(),oldpack.c_str()+index, packlen-index);
    memcpy(stmp+snewlen.length()+packlen-index,timestamp.c_str(),timestamp.length());
    string npack(stmp,newlen);
    newpack = npack;
}

int CThread::CmdProcess(string strCmd, string scontent,int fd)
{
    GLOG_DEBUG("cmd:"<<strCmd.c_str());
    if (strCmd.compare(CMD_LOGIN) == 0)
    {   
        //login
        return Sign(scontent,fd);
    }   
    else if (strCmd.compare(CMD_LOGOUT) == 0)
    {   
        //logout
        return Logout(scontent);
    }   
    else if ( (strCmd.compare(CMD_CHAT) == 0) || (strCmd.compare(CMD_FILE)==0))
    {   
        //chat or file
        return Chat(scontent);
    }   
    else if (strCmd.compare(CMD_GROUPCHAT) == 0)
    {   
        //group chat
        return GroupChat(scontent);
    } 

    else //other    
    {   
        GLOG_ERROR("cmd error! cmd:"<<strCmd.c_str());   
    }   
    return 0;
}


int CThread::Process(string strbuf, int fd)
{
    char buffer[BUFFER_SIZE] = {0};
    int  bufferlen = 0;

    //与上次读到的不完整内容合并
    Combine(strbuf,fd,buffer,bufferlen);
    
    uint32_t packlen = GetPackLenField(buffer,"##");
    GLOG_DEBUG("packlen="<<packlen<<",bufferlen="<<bufferlen);
    //乱包,丢弃
    if (packlen <= 0)
    {
        GLOG_ERROR("pack error! packlen:"<<packlen<<"buffer:"<<buffer);
        return 0;
    }

    //不够一个包,存入map
    if (packlen > bufferlen)
    {
        string strHalfpack(buffer,bufferlen);
        StoreHalfPack(fd,strHalfpack);
        return 0;
    }

    //读取的内容超过了一个包
    if (packlen < bufferlen)
    {
        string strPack(buffer,packlen);
        char halfpack[BUFFER_SIZE] = {0};
        memcpy(halfpack,buffer+packlen,bufferlen - packlen);
        string strHalfPack(halfpack,bufferlen - packlen);

        //打上时戳
        string scontent;
        AddTimestamp(strPack,scontent);

        //cmd
        string strCmd;
        if (GetPackCmdField(scontent,strCmd) == 0) 
        {
            CmdProcess(strCmd,scontent,fd);
        }
        
        //循环 
        Process(strHalfPack,fd);
    }
    else //恰好一个整包
    {
        string strPack(buffer,bufferlen);

        //打上时戳
        string scontent;
        AddTimestamp(strPack,scontent);

        //cmd
        string strCmd;
        if (GetPackCmdField(scontent,strCmd) != 0) {GLOG_ERROR("cmd error! scontent:"<<scontent); return -1;}

        CmdProcess(strCmd,scontent,fd);
    }
}


//写失败则放入全局写map中,并注册写事件
void CThread::StoreWriteFailMap(string strContent, int sockfd, int wlen, int len)
{
    GLOG_DEBUG("StoreWriteFailMap, strContent:"<<strContent<<",wlen:"<<wlen<<",len:"<<len);
    string strWrite;
    if (wlen == 0)
    {
        strWrite = strContent;
    }
    else //写了一部分, 先不考虑写失败的部分和读取的超过一个包的那部分的先后时序
    {
        strWrite = strContent.substr(wlen,len-wlen);
    }

    pthread_mutex_lock(&(g_BufferInfo.mutex));
    map<int, vector<string> >::iterator it = g_BufferInfo.mbuffer.find(sockfd);
    if (it == g_BufferInfo.mbuffer.end())
    {
        vector<string> v;
        v.push_back(strWrite);
        g_BufferInfo.mbuffer.insert(map<int,vector<string> >::value_type(sockfd,v));
        pthread_mutex_unlock(&(g_BufferInfo.mutex));
    }
    else //map 中原来就有该fd的信息，则在尾端插入
    {
        it->second.push_back(strWrite);
        pthread_mutex_unlock(&(g_BufferInfo.mutex));
    }
    //epoll 注册写事件 
    struct epoll_event ev;
    ev.events  = EPOLLOUT | EPOLLIN | EPOLLET;
    ev.data.fd = sockfd;
    epoll_ctl(thpool_p->epoll_fd,EPOLL_CTL_MOD,sockfd,&ev);
}



int CThread::Write(int sockfd, const char* buf, int maxlen)
{
    int wlen = 0;
    while(1)
    {   
        int rem = (wlen < maxlen)?(maxlen-wlen):0;
        if (rem == 0) break;

        int n = write(sockfd,buf+wlen,rem);
        if(n < 0)
        {   
            if(errno == EAGAIN)
            {   
                break;
            }   
            else if(errno == EINTR)
            {   
                continue;
            }   
            else
            {   
                break;
            }   
        }   
        if(n == 0)
        {   
            GLOG_ERROR("fd:"<<sockfd<<"write return"<<n<<"a special unkown err, errno:"<<errno);
            break;
        }   
        if(n > 0)
        {   
            wlen += n;
        }       
    } 
    return wlen;
}


int CThread::Read(int sockfd, char* buf,int maxlen)
{

    int readlen = 0;
    while(1)
    {   
        int rem = (maxlen > readlen)?(maxlen - readlen):0;
        if (rem == 0) break;

        int n = read(sockfd,buf+readlen,rem);
        if (n == -1) 
        {   
            GLOG_DEBUG("fd:"<<sockfd<<", after read, errno:"<<errno<<",error is:"<<strerror(errno));
            if(errno == EAGAIN)
            {   
                break;
            }   
            else if(errno == EINTR)
            {
                continue;
            }
            else
            {
                break;
            }
        }
        if (n > 0)
        {
            readlen += n;
        }       
         else //读失败,删除用户登录信息并关闭sock,丢弃已读取的数据
        {
            DeleteGlobalInfo(sockfd);
            readlen = 0;
            break;
        }
    }
    return readlen;
}

void CThread::DeleteGlobalInfo(int sockfd)
{
    //清除登录信息
    pthread_mutex_lock(&(g_OnlineInfo.mutex));
    map<string,int>::iterator it = g_OnlineInfo.minfo.end();
    it = find_if(g_OnlineInfo.minfo.begin(), g_OnlineInfo.minfo.end(), map_value_finder(sockfd));
    if (it != g_OnlineInfo.minfo.end())
    {
        g_OnlineInfo.minfo.erase(it);
    }
    pthread_mutex_unlock(&(g_OnlineInfo.mutex));

    //清除写失败的信息
    pthread_mutex_lock(&(g_BufferInfo.mutex));
    map<int,vector<string> >::iterator itor = g_BufferInfo.mbuffer.find(sockfd);
    if (itor != g_BufferInfo.mbuffer.end())
    {   
        g_BufferInfo.mbuffer.erase(itor);
    }     
    pthread_mutex_unlock(&(g_BufferInfo.mutex));

    //清除读到的不完整包信息
    pthread_mutex_lock(&(g_HalfPacketInfo.mutex));
    map<int,string>::iterator iter =  g_HalfPacketInfo.mpackinfo.find(sockfd);
    if (iter != g_HalfPacketInfo.mpackinfo.end())
    {
        g_HalfPacketInfo.mpackinfo.erase(iter);
    }
    pthread_mutex_unlock(&(g_HalfPacketInfo.mutex));

    close(sockfd);
    epoll_ctl(thpool_p->epoll_fd, EPOLL_CTL_DEL, sockfd, NULL);
}


int CThread::ReadProcess(int sockfd)
{
    char buf[BUFFER_SIZE] = {0};

    int readlen = Read(sockfd,buf,BUFFER_SIZE);
    if (readlen != 0)
    {
        string strBuf(buf,readlen);
        GLOG_DEBUG("Read...len="<<strBuf.length()<<",readlen="<<readlen<<"strBuf:"<<strBuf.c_str());
        Process(strBuf,sockfd);
    }

    return 0;      
}

int CThread::WriteProcess(int sockfd)
{
    //sendertoken$$receivertokern$$content
 
    pthread_mutex_lock(&(g_BufferInfo.mutex));
    map<int,vector<string> >::iterator it = g_BufferInfo.mbuffer.find(sockfd);
    if (it == g_BufferInfo.mbuffer.end())
    {
        pthread_mutex_unlock(&(g_BufferInfo.mutex));
        return 0;
    }
    
    //遍历
    vector<string> vFail;
    vector<string>::iterator itor = it->second.begin();
    while (itor != it->second.end())
    {
        int len = write(sockfd,itor->c_str(),itor->size());
        if (len != itor->size())  vFail.push_back(*itor);
        it->second.erase(itor);
    }
    if (vFail.size() != 0)
    {
        it->second = vFail;
    }
    else
    {
        g_BufferInfo.mbuffer.erase(it);
        //epoll 更新 
        struct epoll_event ev; 
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = sockfd;
        epoll_ctl(thpool_p->epoll_fd,EPOLL_CTL_MOD,sockfd,&ev); 
    }
    pthread_mutex_unlock(&(g_BufferInfo.mutex));
    
    return 0;
}

void CThread::destroy()
{
    pthread_exit(NULL);
}

