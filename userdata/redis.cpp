#include "redis.h"
#include <map>
redis_clt *redis_clt::m_redis_instance = new redis_clt();

redis_clt *redis_clt::getinstance()
{
    return m_redis_instance;
}

string redis_clt::getReply(string m_command)
{

    m_redisReply = (redisReply *)redisCommand(m_redisContext, m_command.c_str());
    //cout << m_redisReply->type << endl;

    if (m_redisReply->elements == 0 && m_redisReply->type == 1)
    {
        //回复一行
        if (m_redisReply->len > 0)
        {
            string temp = string(m_redisReply->str);
            freeReplyObject(m_redisReply);
            //cout << "reply:   " << temp << endl;
            return temp;
        }
        else
        {

            cout << "return nothing?" << endl;
            freeReplyObject(m_redisReply);
            return "";
        }
    }
    else if (m_redisReply->type == 3)
    {
        //cout << "do code" << endl;
        int tempcode = m_redisReply->integer;
        freeReplyObject(m_redisReply);
        return to_string(tempcode);
    }
    else
    {
        //for post
        string temp;
        temp += "{";
        for (int i = 0; i < m_redisReply->elements; ++i)
        {
            temp += "\"";
            temp += string(m_redisReply->element[i]->str);
            temp += "\"";
            if (i % 2 == 0)
            {
                temp += ":";
            }
            else
            {
                temp += ",";
            }
        }
        temp.pop_back();
        temp += "}";
        freeReplyObject(m_redisReply);
        return temp;
    }
}

redis_clt::redis_clt()
{
    timeout = {2, 0};
    m_redisContext = (redisContext *)redisConnectWithTimeout("127.0.0.1", 6379, timeout);
    m_redisReply = nullptr;
}
string redis_clt::getvoteboard()
{
    string board;
    board = getReply("zrange company 0 -1 withscores");
    return board;
}
void redis_clt::board_exist()
{
    string board;
    board = getReply("EXISTS company");
    if(board != "1")
    {
        //init 
        //回头在vote环节增加校验，没有再添加，而不是直接初始化这些
        getReply("DEL company");
        getReply("zadd company 0 Tencent");
        getReply("zadd company 0 Baidu");
        getReply("zadd company 0 Bytedance");
        getReply("zadd company 0 Huawei");
        getReply("zadd company 0 Alibaba");
        cout << "init ok" << endl;
    }
    //return board;
}
/*
int main()
{
    redis_clt *m_r = redis_clt::getinstance();
    m_r->board_exist();
    //cout << temp << endl;

    return 0;
}
*/