#ifndef TIMER_H
#define TIMER_H

#include "../http/http_conn.h"
#include "../base/priority_queue.h"
#include "../epoll/epoll.h"
#include <functional>
#include <vector>
#include <unordered_map>
#include <cassert>
using namespace std;

// 定时器类
class TimerNode {
public:
    TimerNode(int sockfd, sockaddr_in addr, time_t expire, function<void()> callback)
        : address(addr), sockfd(sockfd), callback(callback), expire(expire) {}
    void setExpire(time_t expire) {
        this->expire = expire;
    }
    time_t getExpire() const {
        return this->expire;
    }
    void setCallback(function<void()> callback) {
        this->callback = callback;
    }
    function<void()> getCallback() const {
        return callback;
    }
    int getSockfd() const {
        return sockfd;
    }
    void setAddr(sockaddr_in addr) {
        this->address = addr;
    }
    bool isValid(time_t cur) const;
private:
    time_t expire;   // 任务超时时间，这里使用绝对时间
    function<void()> callback;
    sockaddr_in address;
    int sockfd;
};

class TimerHeap {
public:
    TimerHeap() {}
    ~TimerHeap() {}

    void addTimer(int id, sockaddr_in addr, time_t expire, function<void()> callback);

    void adjustTimer(int id, time_t expire);

    void doTimer(int id);

    /* SIGALRM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理到期任务。*/
    void tick();

    void clear();

    void pop();

private:
    void del(int i);
    
    void siftup(int i);

    bool siftdown(int index, int n);

    void swapNode(int i, int j);

    std::vector<shared_ptr<TimerNode>> heap;
    std::unordered_map<int, int> ref;
};

#endif