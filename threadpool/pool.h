#ifndef POOL_H
#define POOL_H

#include "../lock/lock.h"

#include <iostream>
#include <list>
#include <pthread.h>
#include <vector>
using namespace std;

/*
一个线程池类应该拥有的：
线程池数组，用于初始化多个线程
请求队列，将请求入队
将请求入队的append方法
线程执行的函数worker和run，各工作线程在run函数处进行竞争互斥锁
监听线程和工作线程对请求队列进行操作时，需要加锁
信号量，记录是否有请求要处理，没请求时，在wait阻塞。
*/
template <typename T>
class pool // 线程池对象
{
private:
    list<T *> request_list;        //request list
    
    vector<pthread_t> thread_list; //thread list
    locker m_lock;                 // lock for working thread
    sem m_sem;                     // sem for main thread and working thread
    int m_thread_num;
    int m_max_request;

private:
    static void *worker(void *arg); //工作线程的执行函数，因此是静态函数，对象通过参数传入
    void run();

public: //三个函数
    pool(int thread_num = 10, int max_request = 100);
    ~pool();
    bool append(T *requset);
};

template <typename T>
pool<T>::pool(int thread_num, int max_request) : m_thread_num(thread_num), m_max_request(max_request)
{
    //cout << "creat pool" << endl;
    thread_list = vector<pthread_t>(thread_num, 0);
    //cout << "create thread_list" << endl;
    //start working threads
    for (int i = 0; i < m_thread_num; ++i)
    {
        //cout << "creat thread:" << i <<"is:"<<thread_list[i]<< endl;
        if (pthread_create(&thread_list[i], NULL, worker, this) != 0)
        {
            cout << "some thing wrong!" << endl;
        }

        if (pthread_detach(thread_list[i])!= 0)
        {
            cout << "detach failed!" << endl;
        }
    }
}

template <typename T>
pool<T>::~pool()
{
    ;
}

template <typename T>
bool pool<T>::append(T *requset)
{

    //cout << "want to get lock" << endl;
    m_lock.dolock();
    if (request_list.size() > m_max_request)
    {
        m_lock.unlock();
        //cout << "too many request_list" << endl;
        return false;
    }
    request_list.push_back(requset);
    m_lock.unlock();
    m_sem.post();
    //cout << "http request appended" << endl;
    return true;
}

template <typename T>
void* pool<T>::worker(void *arg)
{
    pool<T> *pool_ptr = static_cast<pool<T> *>(arg);
    pool_ptr->run();
    return pool_ptr;
}

template <typename T>
void pool<T>::run()
{
    while (1)
    {
        m_sem.wait();    //信号量
        m_lock.dolock(); //加锁,竞争条件
        //cout << "request num :  " << request_list.size() << endl;
        if (request_list.size() <= 0)
        {
            m_lock.unlock();
        }
        else
        {
            T* request = request_list.front();
            request_list.pop_front(); //请求队列出队
            //cout << "some thread get the request" << endl;
            m_lock.unlock();
            if (!request)
            {
                continue;
            }
            request->process();
            //delete request;//非常奇怪咯
            //request = nullptr;
        }
    }
}
#endif