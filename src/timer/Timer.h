#ifndef TIMER_H
#define TIMER_H

#include "../http/http_conn.h"
#include "../base/priority_queue.h"
#include "../epoll/epoll.h"
#include <functional>
using namespace std;

class ClientData;
class Timer;

class ClientData {
private:
    sockaddr_in address;
    int sockfd;
    std::shared_ptr<Timer> timer;
public:
    ClientData(sockaddr_in addr, int sockfd): address(addr), sockfd(sockfd) {}
    void setTimer(std::shared_ptr<Timer> timer) {
        this->timer = timer;
    }
    int getSockfd() const {
        return sockfd;
    }
    std::shared_ptr<Timer> getTimer() const {
        return timer;
    }
};

struct Callback {
    Callback(int epollfd): epollfd(epollfd) {}
    void operator()(shared_ptr<ClientData> userData) const {
        if (userData) {
            removefd(epollfd, userData->getSockfd());
            HttpConn::decUserCount();
        }
    }
private:
    int epollfd;
};

// 定时器类
class Timer {
public:
    Timer() : expire(0), isClosed(false) {}
    void setExpire(time_t expire) {
        this->expire = expire;
    }
    time_t getExpire() const {
        return this->expire;
    }
    void setIsClosed(bool isClosed) {
        this->isClosed = isClosed;
    }
    bool getIsClosed() const {
        return this->isClosed;
    }
    void setUserData(shared_ptr<ClientData> data) {
        this->userData = data;
    }
    weak_ptr<ClientData> getUserData() const {
        return this->userData;
    }
    void setCallback(function<void(shared_ptr<ClientData>)> callback) {
        this->callback = callback;
    }
    function<void(shared_ptr<ClientData>)> getCallback() const {
        return callback;
    }
    bool isValid(time_t cur) const;
private:
    time_t expire;   // 任务超时时间，这里使用绝对时间
    std::weak_ptr<ClientData> userData;
    function<void(shared_ptr<ClientData>)> callback;
    bool isClosed;
};

class TimerList {
public:
    TimerList() {}
    ~TimerList() {}

    void addTimer(shared_ptr<Timer> timer);

    void delTimer(shared_ptr<Timer> timer);

    void adjustTimer(shared_ptr<Timer> timer, time_t expire);

    /* SIGALARM 信号每次被触发就在其信号处理函数中执行一次 tick() 函数，以处理到期任务。*/
    void tick();

private:
    struct Cmp {
        bool operator()(const shared_ptr<Timer> &a, const shared_ptr<Timer> &b) const {
            return a->getExpire() > b->getExpire();
        }
    };
    PriorityQueue<shared_ptr<Timer>, vector<shared_ptr<Timer>>, Cmp> active;
};

#endif