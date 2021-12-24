#include "Timer.h"

bool Timer::isValid(time_t cur) const
{
    return cur < this->expire;
}

void TimerList::addTimer(shared_ptr<Timer> timer)
{
    active.push(timer);
}

void TimerList::delTimer(shared_ptr<Timer> timer)
{
    timer->setIsClosed(true);
}

void TimerList::adjustTimer(shared_ptr<Timer> timer, time_t expire)
{
    timer->setExpire(expire);
    active.adjust();
}

void TimerList::tick()
{
    if (active.empty()) {
        return;
    }
    time_t cur = time(nullptr);  // 获取当前系统时间
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while (!active.empty()) {
        shared_ptr<Timer> temp = active.top();

        /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
        比较以判断定时器是否到期*/
        if (temp->getIsClosed()) {
            active.pop();
        }
        else if (!temp->isValid(cur)) {
            if (!temp->getUserData().expired())
                temp->getCallback()(temp->getUserData().lock());
            active.pop();
        }
        else
            break;
    }
}