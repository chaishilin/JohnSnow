#include "./http/http_conn.h"
#include "./lock/lock.h"
#include "./threadpool/pool.h"
#include "./time/m_time.h"
#include <assert.h>
#include <iostream>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <vector>


using namespace std;

extern void addfd(int epollfd, int socketfd);
extern void setnonblocking(int socketfd);

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int MAX_WAIT_TIME = 1;
static int pipefd[2];
static int epollfd = 0; //全局静态变量
static t_client_list m_timer_list;

template <typename T>
void destroy_user_list(T user_list) //用模板改写下，销毁两个列表
{
    int count = 0;
    for (auto i : user_list)
    {
        if (i)
        {
            count++;
            delete i;
        }
    }
    cout << "delete " << count << " items" << endl;
}

void addfd_lt(int epollfd, int socketfd)
{
    epoll_event event;
    event.data.fd = socketfd;
    event.events = EPOLLIN | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &event);
}

void cb_func(client_data *c_data) //定时器回调函数，真正地关闭非活动连接,仅仅在定时器事件中被触发，不会主动调用
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, c_data->sockfd, 0);
    close(c_data->sockfd);
    http_conn::m_user_count--;
}

void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

void sig_handler(int sig)
{
    //cout << "sig_handler get: " << sig << endl;
    int errnotemp = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0); //写入当前是哪个信号发出的信号,管道的属性：向后面的写，前面的就读出来
    errno = errnotemp;                   //保证可重入性
}

void timer_handler()
{
    m_timer_list.tick(); //这里是异步的吗？
    alarm(MAX_WAIT_TIME);
}
/*
主函数中：
1.创建线程池
2.根据最大描述符来创建http请求数组，用来根据不同请求的不同描述符来记录各个http请求
3.socket,bind,listen,epool_wait,得到io事件event
4.对于新的连接请求，接受，保存入http请求数组进行初始化
5.对于读写请求，请求入队
*/

int main(int argc, char *agrv[])
{
    int port;
    if (argc <= 1)
    {
        cout << "open default port : 12345" << endl;
        port = 12345;
    }
    else
    {
        port = atoi(agrv[1]);
    }

    pool<http_conn> m_threadpool(10, 10000);             //创建线程池对象
    vector<http_conn *> http_user_list(MAX_FD, nullptr); //连接数量取决于描述符的数量,这个太奇怪了，析构什么的
    int user_count = 0;                                  //用户数为0

    int listenfd = socket(PF_INET, SOCK_STREAM, 0); //socket
    struct sockaddr_in address;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    int flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    bind(listenfd, (struct sockaddr *)&address, sizeof(address));

    listen(listenfd, 5);
    //cout << "listen, listenfd : " << listenfd << endl;

    //创建内核事件表
    epoll_event event_list[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    addfd_lt(epollfd, listenfd);
    http_conn::m_epollfd = epollfd; //更新http_conn类中的epoll描述符

    //创建管道 用于定时器和其他信号消息的通知
    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd); //域协议
    setnonblocking(pipefd[1]);                   //写入是非阻塞的？？？？为什么
    addfd(epollfd, pipefd[0]);                   //监听前一个管道描述符

    addsig(SIGALRM, sig_handler, false); //监听定时信号
    addsig(SIGTERM, sig_handler, false); //监听定时信号

    addsig(SIGINT, sig_handler, false);                      //监听定时信号
    vector<client_data *> client_data_list(MAX_FD, nullptr); //创建用户信息列表，其中包含定时器信息

    alarm(MAX_WAIT_TIME); //开始定时
    bool timeout = false;
    bool stop_server = false;
    while (!stop_server)
    {
        int number = epoll_wait(epollfd, event_list, MAX_EVENT_NUMBER, -1);
        for (int i = 0; i < number; ++i)
        {
            int socketfd = event_list[i].data.fd;
            if (socketfd == listenfd)
            {
                //cout << "new client" << endl;
                //新的连接
                struct sockaddr_in client_addr;
                socklen_t client_addr_length = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr *)(&client_addr), &client_addr_length);
                if (connfd < 0)
                {
                    //printf("%s\n", strerror(errno));
                    continue; //跳过这一轮
                }
                if (http_conn::m_user_count >= MAX_FD)
                    continue;
                if (http_user_list[connfd] == nullptr)
                {
                    cout << "new http  " << connfd << endl;
                    http_user_list[connfd] = new http_conn();
                }
                http_user_list[connfd]->init(connfd, client_addr);

                //开始初始化client_data
                //创建定时器
                if (client_data_list[connfd] == nullptr)
                {
                    cout << "new timer  " << connfd << endl;
                    client_data_list[connfd] = new client_data;
                }
                client_data_list[connfd]->address = client_addr;
                client_data_list[connfd]->sockfd = connfd;

                t_client *timer = new t_client;
                timer->user_data = client_data_list[connfd];
                timer->cb_func = cb_func;
                time_t time_now = time(nullptr);
                timer->livetime = time_now + MAX_WAIT_TIME;
                client_data_list[connfd]->timer = timer;
                m_timer_list.add_timer(timer);
            }
            else if (event_list[i].events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR))
            {
                //printf("%s\n", strerror(errno));
                http_user_list[socketfd]->close_conn("error!");
                //关闭对应的计时器
                t_client *timer = client_data_list[socketfd]->timer;
                if (timer)
                    m_timer_list.del_timer(timer);
            }
            else if ((socketfd == pipefd[0]) && (event_list[i].events & EPOLLIN)) //管道前面的描述符可写的话
            {
                int sig;
                char signals[1024];
                int retmsg = recv(socketfd, signals, sizeof(signals), 0);
                if (retmsg <= 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < retmsg; ++i)
                    {
                        if (signals[i] == SIGALRM)
                        {
                            timeout = true;
                            break;
                        }
                        else if (signals[i] == SIGTERM || signals[i] == SIGINT)
                        {
                            stop_server = true;
                        }
                    }
                }
            }

            else if (event_list[i].events & EPOLLIN)
            {
                t_client *timer = client_data_list[socketfd]->timer;
                //客户的数据要读进来
                if (http_user_list[socketfd]->read())
                {
                    //如果读到了一些数据（已经读到了读缓存区里）,就把这个请求放到工作线程的请求队列中
                    m_threadpool.append(http_user_list[socketfd]);

                    //针对这个活跃的连接，更新他的定时器里规定的存活事件
                    if (timer)
                    {
                        time_t timenow = time(NULL);
                        timer->livetime = timenow + MAX_WAIT_TIME;
                        m_timer_list.adjust_timer(timer); //调整当前timer的位置（因为很活跃）
                    }
                    else
                    {
                        cout << "did not creat timer for socket:" << socketfd << endl;
                    }
                }
                else
                {
                    //没有读到数据的话
                    http_user_list[socketfd]->close_conn("read nothing");
                    if (timer)
                        m_timer_list.del_timer(timer);
                }
            }
            else if (event_list[i].events & EPOLLOUT)
            {
                t_client *timer = client_data_list[socketfd]->timer;
                //cout << "want to write" << endl;
                if (http_user_list[socketfd]->write())
                {
                    //长连接的话，不关闭
                    if (timer)
                    {
                        cout << "long connect" << endl;
                        time_t timenow = time(NULL);
                        timer->livetime = timenow + MAX_WAIT_TIME;
                        m_timer_list.adjust_timer(timer); //调整当前timer的位置（因为很活跃）
                    }
                    else
                    {
                        cout << "did not creat timer for socket:" << socketfd << endl;
                    }
                }
                else
                {
                    //短连接，关闭连接
                    //cout << "short connect and close timer" << endl;
                    http_user_list[socketfd]->close_conn("write over");
                    if (timer)
                        m_timer_list.del_timer(timer);
                }
            }
            if (timeout)
            {
                timer_handler();
                timeout = false;
            }
        }
    }
    close(epollfd);
    close(listenfd);
    destroy_user_list<vector<http_conn *>>(http_user_list);
    destroy_user_list<vector<client_data *>>(client_data_list);
    cout << "close ok" << endl;

    return 0;
}