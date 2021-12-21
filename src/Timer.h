#ifndef TIMER_H
#define TIMER_H

#include "http_conn.h"
#include "base/priority_queue.h"
using namespace std;

class HttpConn;

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
    void setUserData(shared_ptr<HttpConn> data) {
        this->userData = data;
    }
    weak_ptr<HttpConn> getUserData() const {
        return this->userData;
    }
    bool isValid(time_t cur) const;
private:
    time_t expire;   // 任务超时时间，这里使用绝对时间
    weak_ptr<HttpConn> userData;
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