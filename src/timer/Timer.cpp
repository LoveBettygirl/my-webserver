#include "Timer.h"

bool TimerNode::isValid(time_t cur) const
{
    return cur < this->expire;
}

void TimerHeap::addTimer(int id, sockaddr_in addr, time_t expire, function<void()> callback)
{
    assert(id >= 0);
    size_t i;
    if (ref.find(id) == ref.end()) {
        i = heap.size();
        ref[id] = i;
        shared_ptr<TimerNode> timer = make_shared<TimerNode>(id, addr, expire, callback);
        heap.push_back(timer);
        siftup(i);
    }
    else {
        i = ref[id];
        heap[i]->setCallback(callback);
        heap[i]->setExpire(expire);
        heap[i]->setAddr(addr);
        if (!siftdown(i, heap.size())) {
            siftup(i);
        }
    }
}

void TimerHeap::adjustTimer(int id, time_t expire)
{
    assert(!heap.empty() && ref.count(id) > 0);
    heap[ref[id]]->setExpire(expire);
    siftdown(ref[id], heap.size());
}

void TimerHeap::tick()
{
    if (heap.empty()) {
        return;
    }
    time_t cur = time(nullptr);  // 获取当前系统时间
    // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while (!heap.empty()) {
        shared_ptr<TimerNode> temp = heap.front();

        /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
        比较以判断定时器是否到期*/
        if (!temp->isValid(cur)) {
            temp->getCallback()();
            pop();
        }
        else
            break;
    }
}

void TimerHeap::doTimer(int id)
{
    if (heap.empty() || ref.find(id) == ref.end()) {
        return;
    }
    size_t i = ref[id];
    heap[i]->getCallback()();
    del(i);
}

void TimerHeap::clear()
{
    ref.clear();
    heap.clear();
}

void TimerHeap::siftup(size_t i)
{
    assert(i >= 0 && i < heap.size());
    size_t j = (i - 1) / 2;
    while (j >= 0) {
        if (heap[j] < heap[i]) { 
            break;
        }
        swapNode(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

void TimerHeap::swapNode(size_t i, size_t j)
{
    assert(i >= 0 && i < heap.size());
    assert(j >= 0 && j < heap.size());
    std::swap(heap[i], heap[j]);
    ref[heap[i]->getSockfd()] = i;
    ref[heap[j]->getSockfd()] = j;
} 

bool TimerHeap::siftdown(size_t index, size_t n)
{
    assert(index >= 0 && index < heap.size());
    assert(n >= 0 && n <= heap.size());
    size_t i = index;
    size_t j = i * 2 + 1;
    while (j < n) {
        if (j + 1 < n && heap[j + 1] < heap[j])
            j++;
        if (heap[i] < heap[j])
            break;
        swapNode(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

void TimerHeap::del(size_t index)
{
    // 删除指定位置的结点
    assert(!heap.empty() && index >= 0 && index < heap.size());
    // 将要删除的结点换到队尾，然后调整堆
    size_t i = index;
    size_t n = heap.size() - 1;
    assert(i <= n);
    if (i < n) {
        swapNode(i, n);
        if (!siftdown(i, n)) {
            siftup(i);
        }
    }
    // 队尾元素删除
    ref.erase(heap.back()->getSockfd());
    heap.pop_back();
}

void TimerHeap::pop()
{
    assert(!heap.empty());
    del(0);
}