#ifndef M_TIME_H
#define M_TIME_H

#include <time.h>

class t_client;
class client_data
{
public:
    sockaddr_in address;
    int sockfd;
    t_client *timer;
};

class t_client
{
public:
    t_client() : pre(nullptr), next(nullptr){};

public:
    time_t livetime;
    void (*cb_func)(client_data *);
    client_data *user_data;
    t_client *pre;
    t_client *next;
};

class t_client_list
{
private:
    /* data */
    void add_timer(t_client *timer, t_client *lst_head);

public:
    t_client_list() : head(nullptr), tail(nullptr){};
    ~t_client_list();
    void add_timer(t_client *timer);
    void adjust_timer(t_client *timer);
    void del_timer(t_client *timer);
    void tick();
    t_client *remove_from_list(t_client *timer);

public:
    t_client *head;
    t_client *tail;
};

t_client_list::~t_client_list()
{
    t_client *temp = head;
    while (temp)
    {
        head = temp->next;
        delete temp;
        temp = head;
    }
}

void t_client_list::tick() //对于列表中的时间事件进行遍历，清空超时的
{
    if (!head)
        return;              //当前列表为空
    time_t cur = time(NULL); //当前时间
    t_client *temp = head;
    while (temp)
    {
        if (cur < temp->livetime) // 如果当前的时间还没到他的关闭时间
        {
            break;
        }
        else
        {
            //如果时间到了
            temp->cb_func(temp->user_data); //关闭连接
        }
        head = temp->next;
        if (head)
            head->pre = NULL;
        delete temp;
        temp = head;
    }
}

void t_client_list::add_timer(t_client *timer) //对于列表中的时间事件进行添加,添加的位置为
{
    if (!timer)
    {
        cout << "not a timer" << endl;
        return;
    }
    if (!head)
    {
        head = timer;
        tail = timer;
        return;
    }
    if (timer->livetime < head->livetime) //如果当前定时器的时间小于头部的定时器时间（更早触发）
    {
        //那就把它放到前面，最早清理
        timer->next = head;
        head->pre = timer;
        head = timer;
        return;
    }
    else
    {
        //放到头部后面的合适的位置
        t_client *temp = head;
        while (temp)
        {
            if (temp->livetime > timer->livetime)
            {
                //如果找到了位置，则进行插入
                t_client *temppre = timer->pre;
                timer->pre = temp->pre;
                timer->next = temp;
                temppre->next = timer;
                temp->pre = timer;
                return;
            }
            temp = temp->next;
        }
        //插入到尾部后面
        tail->next = timer;
        timer->pre = tail;
        tail = timer;
        return;
    }
}
t_client *t_client_list::remove_from_list(t_client *timer)
{
    if (!timer)
        return timer;
    else if ((timer == head) && (timer == tail))
        head = tail = nullptr;
    else if (timer == head)
    {
        head = head->next;
        head->pre = nullptr;
        timer->next = nullptr;
    }
    else if (timer == tail)
    {
        tail = tail->pre;
        tail->next = nullptr;
        timer->pre = nullptr;
    }
    else
    {
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        timer->pre = nullptr;
        timer->next = nullptr;
    }
    return timer;
}
void t_client_list::del_timer(t_client *timer) //对于列表中的时间事件删除,参数中的timer对应的描述符已经关闭了
{
    if (!timer)
        return;
    remove_from_list(timer);
    delete timer;
}

void t_client_list::adjust_timer(t_client *timer) //对于列表中的时间事件进行按照时间顺序的调整再重新插入
{
    if (timer)
    {
        t_client *temp = remove_from_list(timer);
        //cout << "remove ok" << endl;
        add_timer(temp);
        //cout << "add ok" << endl;
    }

}

#endif