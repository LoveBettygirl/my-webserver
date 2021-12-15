#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <iostream>
#include "locker.h"

// 线程池类，定义成模板类是为了代码复用，T是任务类
// 线程池的本质是一个生产者消费者模型
// 多个线程共享的资源是请求队列。主线程是生产者，其他线程是消费者。
// 互斥锁解决互斥问题，信号量解决同步问题
// 互斥：对请求队列的操作，需要用互斥锁
// 同步：如果请求队列中没有任务，主线程必须先加入新的任务，其他线程才能处理任务
template<typename T>
class ThreadPool {
public:
    ThreadPool(int thread_number = 8, int max_requests = 10000);
    ~ThreadPool();
    bool append(T *request);

private:
    static void *worker(void *arg);
    void run(); // 线程启动
private:
    // 线程的数量
    int m_thread_number;

    // 线程池数组，大小为m_thread_number
    pthread_t *m_threads;

    // 请求队列中最多允许的，等待处理的请求数量
    int m_max_requests;

    // 请求队列（所有线程共享的）
    std::list<T*> m_workqueue;

    // 互斥锁
    Locker m_queuelocker;

    // 信号量，判断是否有任务需要处理
    Semaphore m_queuestat;

    // 是否结束线程
    bool m_stop;
};

template <typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(nullptr) {
    if ((m_thread_number <= 0) || (m_max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 创建thread_number个线程，并将它们设置为线程脱离
    for (int i = 0; i < thread_number; ++i) {
        std::cout << "create the " << i << "th thread" << std::endl;

        // worker必须是静态（成员）函数
        // 传this到worker中，就能在work中操作成员了
        if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
            delete []m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {
            delete []m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
ThreadPool<T>::~ThreadPool() {
    delete []m_threads;
    m_stop = true;
}

template <typename T>
bool ThreadPool<T>::append(T *request) {
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post(); // 增加信号量
    return true;
}

template <typename T>
void *ThreadPool<T>::worker(void *arg) {
    ThreadPool<T> *pool = static_cast<ThreadPool<T>*>(arg);
    pool->run();
    return static_cast<void*>(pool);
}


template <typename T>
void ThreadPool<T>::run() {
    // 一旦一个对象析构，stop设置为true
    // 所有子线程的循环都要结束
    while (!m_stop) {
        m_queuestat.wait(); // 判断有没有任务，没有就阻塞
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T *request = m_workqueue.front(); // 获取第一个任务
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request) { // 没有获取到任务（任务可能被其他线程抢走），也可以去掉
            continue;
        }

        request->process(); // 处理任务
    }
}

#endif