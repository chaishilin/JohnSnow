#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <iostream>
#include <string.h>
using namespace std;

class redis_clt
{
private:
    static redis_clt *m_redis_instance;
    struct timeval timeout;
    redisContext *m_redisContext;
    redisReply *m_redisReply;

private:
    string getReply(string m_command);
    redis_clt();

public:
    string setUserpasswd(string username, string passwd)
    {
        return getReply("set " + username + " " + passwd);
    }

    string getUserpasswd(string username)
    {
        return getReply("get " + username);
    }
    void vote(string votename)
    {
        getReply("ZINCRBY company 1 " + votename);
        //zrange company 0 -1 withscores
    }
    string getvoteboard();
    void board_exist();

    static redis_clt *getinstance();
};

#endif
