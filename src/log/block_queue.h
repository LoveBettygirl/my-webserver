#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <cstdlib>
#include <pthread.h>
#include <sys/time.h>
#include <queue>
#include "../thread/locker.h"
#include "../common.h"

template <typename T>
class BlockQueue {
public:
    explicit BlockQueue(int maxSize = 1000) {
        if (maxSize <= 0) {
            exit(BLOCK_QUEUE_SIZE_ERROR);
        }

        mMaxSize = maxSize;
    }

    void clear() {
        m_mutex.lock();
        std::queue<T> empty;
        std::swap(empty, mQueue);
        m_mutex.unlock();
    }

    ~BlockQueue() {
        m_mutex.lock();
        std::queue<T> empty;
        std::swap(empty, mQueue);
        m_mutex.unlock();
    }

    //判断队列是否满了
    bool full() {
        m_mutex.lock();
        if (mQueue.size() >= mMaxSize) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //判断队列是否为空
    bool empty() {
        m_mutex.lock();
        if (mQueue.empty()) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    //返回队首元素
    bool front(T &value) {
        m_mutex.lock();
        if (mQueue.empty()) {
            m_mutex.unlock();
            return false;
        }
        value = mQueue.front();
        m_mutex.unlock();
        return true;
    }

    //返回队尾元素
    bool back(T &value) {
        m_mutex.lock();
        if (mQueue.empty()) {
            m_mutex.unlock();
            return false;
        }
        value = mQueue.back();
        m_mutex.unlock();
        return true;
    }

    int size() {
        int tmp = 0;

        m_mutex.lock();
        tmp = mQueue.size();

        m_mutex.unlock();
        return tmp;
    }

    int maxSize() {
        int tmp = 0;

        m_mutex.lock();
        tmp = mMaxSize;

        m_mutex.unlock();
        return tmp;
    }

    //往队列添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item) {

        m_mutex.lock();
        if (mQueue.size() >= mMaxSize) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        mQueue.push(item);

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item) {
        m_mutex.lock();
        while (mQueue.empty()) {
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }

        item = mQueue.front();
        mQueue.pop();
        m_mutex.unlock();
        return true;
    }

    //增加了超时处理
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (mQueue.empty()) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timedwait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }

        if (mQueue.empty()) {
            m_mutex.unlock();
            return false;
        }

        item = mQueue.front();
        mQueue.pop();
        m_mutex.unlock();
        return true;
    }

private:
    Locker m_mutex;
    Cond m_cond;
    std::queue<T> mQueue;

    int mMaxSize;
};

#endif
