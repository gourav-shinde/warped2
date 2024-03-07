#ifndef TIME_WARP_EVENT_SET_HPP
#define TIME_WARP_EVENT_SET_HPP

/* This class provides the set of APIs needed to handle events in
   an event set.
 */

#include <vector>
#include <deque>
#include <set>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>

#include "config.h"
#include "LogicalProcess.hpp"
#include "Event.hpp"
#include "utility/memory.hpp"
#include "TicketLock.hpp"
#include "LadderQueue.hpp"
#include "SplayTree.hpp"
#include "CircularQueue.hpp"
#include "UnifiedQueue.hpp"

namespace warped {

enum class InsertStatus {
    Success,
    NegativeCancelled,
    PositiveCancelled,
    LpOnly,
    StarvedObject,
    SchedEventSwapSuccess,
    SchedEventSwapFailure
};

//combine process queue and input queue.

class TimeWarpEventSet {
public:
    TimeWarpEventSet() = default;

    //done
    void initialize (const std::vector<std::vector<LogicalProcess*>>& lps,
                     unsigned int num_of_lps,
                     bool is_lp_migration_on,
                     unsigned int num_of_worker_threads);

#ifdef UNIFIED_QUEUE
#else
    void acquireInputQueueLock (unsigned int lp_id);

    void releaseInputQueueLock (unsigned int lp_id);
#endif

    InsertStatus insertEvent (unsigned int lp_id, std::shared_ptr<Event> event);

    std::shared_ptr<Event> getEvent (unsigned int thread_id);

    unsigned int lowestTimestamp (unsigned int thread_id);

    std::shared_ptr<Event> lastProcessedEvent (unsigned int lp_id);

    void rollback (unsigned int lp_id, std::shared_ptr<Event> straggler_event);

    std::unique_ptr<std::vector<std::shared_ptr<Event>>> getEventsForCoastForward (
                        unsigned int lp_id, 
                        std::shared_ptr<Event> straggler_event, 
                        std::shared_ptr<Event> restored_state_event);

    void startScheduling (unsigned int lp_id);

    void replenishScheduler (unsigned int lp_id);

    // we do not seem to use this function anywhere
    bool cancelEvent (unsigned int lp_id, std::shared_ptr<Event> cancel_event);

    //standard done
    void printEvent (std::shared_ptr<Event> event);

    //this is done
    unsigned int fossilCollect (unsigned int fossil_collect_time, unsigned int lp_id);


#ifdef UNIFIED_QUEUE
    void markUnprocessed(unsigned int lp_id, std::shared_ptr<Event> event);
#else
#endif

private:
    // Number of lps
    unsigned int num_of_lps_ = 0;

#ifdef UNIFIED_QUEUE
    // This unifies input_queue and processed_queue
    std::vector<std::unique_ptr<UnifiedQueue<std::shared_ptr<Event>,compareEvents, compareNegativeEvent>>> unified_queue_;
#else
    // Lock to protect the unprocessed queues
    std::unique_ptr<std::mutex []> input_queue_lock_;

    // Queues to hold the unprocessed events for each lp
    std::vector<std::unique_ptr<std::multiset<std::shared_ptr<Event>, 
                                            compareEvents>>> input_queue_;

    // Queues to hold the processed events for each lp
    std::vector<std::unique_ptr<std::deque<std::shared_ptr<Event>>>> 
                                                            processed_queue_;
#endif
    // Number of event schedulers
    unsigned int num_of_schedulers_ = 0;

    // Lock to protect the schedule queues
    //.. what is schedule queue? do we leave the lock structure as it is.?
#ifdef SCHEDULE_QUEUE_SPINLOCKS
    std::unique_ptr<TicketLock []> schedule_queue_lock_;
#else
#endif

    // Queues to hold the scheduled events
#if defined(SORTED_LADDER_QUEUE) || defined(PARTIALLY_SORTED_LADDER_QUEUE)
    std::vector<std::unique_ptr<LadderQueue>> schedule_queue_;
#elif defined(SPLAY_TREE)
    std::vector<std::unique_ptr<SplayTree>> schedule_queue_;
#elif defined(CIRCULAR_QUEUE)
    std::vector<std::unique_ptr<CircularQueue>> schedule_queue_;
#else
    std::vector<std::unique_ptr<std::multiset<std::shared_ptr<Event>, 
                                            relaxedCompareEvents>>> schedule_queue_;
    // modify it so compareEvents only compares timestamps
#endif

    // Map unprocessed queue to a schedule queue
    std::vector<unsigned int> input_queue_scheduler_map_;

    // LP Migration flag
    // you dont need this
    bool is_lp_migration_on_;


    // Event scheduled from all lps
    std::vector<std::shared_ptr<Event>> scheduled_event_pointer_;
};

} // warped namespace

#endif
