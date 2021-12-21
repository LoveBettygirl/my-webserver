#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include <queue>

template <typename _Tp, typename _Sequence, typename _Compare>
class PriorityQueue: public std::priority_queue<_Tp, _Sequence, _Compare> {
public:
    void adjust() {
        std::make_heap(this->c.begin(), this->c.end(), this->comp);
    }
};

#endif