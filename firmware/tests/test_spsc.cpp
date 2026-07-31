// Host test: lock-free SPSC queue FIFO / full / empty behaviour.
#include "util/spsc_queue.h"
#include <cassert>
#include <cstdio>

int main()
{
    SpscQueue<int, 4> q; // usable capacity = 3 (one slot reserved)
    int               v = 0;

    assert(!q.Pop(v)); // empty

    assert(q.Push(10));
    assert(q.Push(20));
    assert(q.Push(30));
    assert(!q.Push(40)); // full (capacity-1 usable)

    assert(q.Pop(v) && v == 10); // FIFO order
    assert(q.Pop(v) && v == 20);

    assert(q.Push(40)); // room again after popping
    assert(q.Pop(v) && v == 30);
    assert(q.Pop(v) && v == 40);
    assert(!q.Pop(v)); // empty again

    std::printf("spsc OK\n");
    return 0;
}
