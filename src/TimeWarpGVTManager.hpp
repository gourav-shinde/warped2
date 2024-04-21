#ifndef GVT_MANAGER_HPP
#define GVT_MANAGER_HPP

#include <memory> // for shared_ptr
#include <mutex>
#include <iostream>
#include <vector>
#include <queue>

#include "TimeWarpEventDispatcher.hpp"
#include "TimeWarpKernelMessage.hpp"

namespace warped {

class MinQueue {
private:
    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> pq; // Using min-heap for ascending order
    uint32_t maxSize;

public:
    MinQueue(uint32_t size = 5) : maxSize(size) {
        push(0);//Initialize the queue with 0
    }

    void push(uint32_t item) {
        if (pq.size() < maxSize) {
            std::cout<<"Pushing "<<item<<" to the queue\n";
            pq.push(item);
        } else {
            if (item > pq.top()) {
                pq.pop();
                pq.push(item);
            }
        }
    }

    uint32_t getGVT() {
        uint32_t minElement = pq.top();
        return minElement;
    }

    uint32_t size() {
        return pq.size();
    }

    uint32_t isFull() {
        return pq.size() == maxSize;
    }
};

enum class GVTState { IDLE, LOCAL, GLOBAL };
enum class Color { WHITE, RED };

class TimeWarpGVTManager {
public:
    TimeWarpGVTManager(std::shared_ptr<TimeWarpCommunicationManager> comm_manager, unsigned int period,
        unsigned int num_worker_threads)
        : comm_manager_(comm_manager), gvt_period_(period), num_worker_threads_(num_worker_threads) {}

    virtual void initialize();
    virtual ~TimeWarpGVTManager() = default;

    void checkProgressGVT();

    virtual bool readyToStart() = 0;

    virtual void progressGVT() = 0;

    virtual void receiveEventUpdate(std::shared_ptr<Event>& event, Color color) = 0;

    virtual Color sendEventUpdate(std::shared_ptr<Event>& event) = 0;

    virtual void reportThreadMin(unsigned int timestamp, unsigned int thread_id,
                                 unsigned int local_gvt_flag) = 0;

    virtual void reportThreadSendMin(unsigned int timestamp, unsigned int thread_id) = 0;

    virtual unsigned int getLocalGVTFlag() = 0;

    virtual bool gvtUpdated() = 0;

    virtual int64_t getMessageCount() = 0;

    unsigned int getGVT() { return gVT_; }

protected:
    const std::shared_ptr<TimeWarpCommunicationManager> comm_manager_;

    unsigned int gVT_ = 0;
    // MinQueue gvT_;
    std::chrono::time_point<std::chrono::steady_clock> gvt_start;

    std::chrono::time_point<std::chrono::steady_clock> gvt_stop;

    unsigned int gvt_period_;

    GVTState gvt_state_;

    unsigned int num_worker_threads_;

};

}

#endif
