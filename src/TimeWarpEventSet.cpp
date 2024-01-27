#include <algorithm>
#include <cassert>
#include <string>

#include "TimeWarpEventSet.hpp"
#include "utility/warnings.hpp"

namespace warped {

void TimeWarpEventSet::initialize (const std::vector<std::vector<LogicalProcess*>>& lps,
                                   unsigned int num_of_lps,
                                   bool is_lp_migration_on,
                                   unsigned int num_of_worker_threads) {

    num_of_lps_         = num_of_lps;
    num_of_schedulers_  = lps.size();
    is_lp_migration_on_ = is_lp_migration_on;
#ifdef UNIFIED_QUEUE
    for (unsigned int scheduler_id = 0; scheduler_id < lps.size(); scheduler_id++) {
        for (unsigned int lp_id = 0; lp_id < lps[scheduler_id].size(); lp_id++) {
            unified_queue_.push_back(make_unique<UnifiedQueue<std::shared_ptr<Event>, compareEvents, compareNegativeEvent>>());
            scheduled_event_pointer_.push_back(nullptr);
            input_queue_scheduler_map_.push_back(scheduler_id);
        }
    }
    
#else
    /* Create the input and processed queues and their locks.
       Also create the input queue-scheduler map and scheduled event pointer. */
    input_queue_lock_ = make_unique<std::mutex []>(num_of_lps);
    for (unsigned int scheduler_id = 0; scheduler_id < lps.size(); scheduler_id++) {
        for (unsigned int lp_id = 0; lp_id < lps[scheduler_id].size(); lp_id++) {
            input_queue_.push_back(
                    make_unique<std::multiset<std::shared_ptr<Event>, compareEvents>>());
            processed_queue_.push_back(make_unique<std::deque<std::shared_ptr<Event>>>());
            scheduled_event_pointer_.push_back(nullptr);
            input_queue_scheduler_map_.push_back(scheduler_id);
        }
    }
#endif
    
#ifdef SCHEDULE_QUEUE_SPINLOCKS
    schedule_queue_lock_ = make_unique<TicketLock []>(num_of_schedulers_);
#else
    schedule_queue_lock_ = make_unique<std::mutex []>(num_of_schedulers_);
#endif

    

    /* Create the schedule queues */
    for (unsigned int scheduler_id = 0; scheduler_id < num_of_schedulers_; scheduler_id++) {
#if defined(SORTED_LADDER_QUEUE) || defined(PARTIALLY_SORTED_LADDER_QUEUE)
        schedule_queue_.push_back(make_unique<LadderQueue>());
#elif defined(SPLAY_TREE)
        schedule_queue_.push_back(make_unique<SplayTree>());
#elif defined(CIRCULAR_QUEUE)
        schedule_queue_.push_back(make_unique<CircularQueue>( lps[scheduler_id].size() ));
#else
        schedule_queue_.push_back(
                make_unique<std::multiset<std::shared_ptr<Event>, compareEvents>>());
#endif
    }

    /* Map worker threads to schedule queues. */
    for (unsigned int thread_id = 0; thread_id < num_of_worker_threads; thread_id++) {
        worker_thread_scheduler_map_.push_back(thread_id % num_of_schedulers_);
    }
}



#ifdef UNIFIED_QUEUE
#else
void TimeWarpEventSet::acquireInputQueueLock (unsigned int lp_id) {

    input_queue_lock_[lp_id].lock();
}

void TimeWarpEventSet::releaseInputQueueLock (unsigned int lp_id) {

    input_queue_lock_[lp_id].unlock();
}
#endif

/*
 *  NOTE: caller must always have the input queue lock for the lp with id lp_id
 *
 *  NOTE: scheduled_event_pointer is also protected by the input queue lock
 */
InsertStatus TimeWarpEventSet::insertEvent (
                    unsigned int lp_id, std::shared_ptr<Event> event) {

#ifdef UNIFIED_QUEUE
    unified_queue_[lp_id]->enqueue(event);
    unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
#else
    // Always insert event into input queue
    input_queue_[lp_id]->insert(event);
    unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
#endif
    
    if (scheduled_event_pointer_[lp_id] == nullptr) {
        // If no event is currently scheduled. This can only happen if the thread that handles
        // events for lp with id == lp_id has determined that there are no more events left in
        // its input queue
#ifdef UNIFIED_QUEUE
#else
        assert(input_queue_[lp_id]->size() == 1);
#endif
        schedule_queue_lock_[scheduler_id].lock();
        schedule_queue_[scheduler_id]->insert(event);
        schedule_queue_lock_[scheduler_id].unlock();
        scheduled_event_pointer_[lp_id] = event;
        return InsertStatus::StarvedObject;
    }
#ifdef UNIFIED_QUEUE
    
#else
    auto smallest_event = *input_queue_[lp_id]->begin();
     if (smallest_event == scheduled_event_pointer_[lp_id]) {
        return InsertStatus::LpOnly;
    }

    // If the pointer comparison of the smallest event does not match scheduled event, well
    // that means we should update the schedule queue...
    auto ret = InsertStatus::SchedEventSwapSuccess;
    schedule_queue_lock_[scheduler_id].lock();
#if defined(CIRCULAR_QUEUE)
    if (schedule_queue_[scheduler_id]->deactivate(scheduled_event_pointer_[lp_id])) {
#else
    if (schedule_queue_[scheduler_id]->erase(scheduled_event_pointer_[lp_id])) {
#endif
        // ...but only if the event was successfully erased from the schedule queue. If it is
        // not then the event is already being processed and a rollback will have to occur.
        schedule_queue_[scheduler_id]->insert(smallest_event);
        scheduled_event_pointer_[lp_id] = smallest_event;
    } else {
        ret = InsertStatus::SchedEventSwapFailure;
    }
    schedule_queue_lock_[scheduler_id].unlock();
    return ret;
#endif
    //this is not default behavious correct this below return statements
    auto ret = InsertStatus::SchedEventSwapSuccess;
    return ret;
   
}

/*
 *  NOTE: caller must always have the input queue lock for the lp with id lp_id
 */
std::shared_ptr<Event> TimeWarpEventSet::getEvent (unsigned int thread_id) {

    unsigned int scheduler_id = worker_thread_scheduler_map_[thread_id];

    schedule_queue_lock_[scheduler_id].lock();

#if defined(SORTED_LADDER_QUEUE) || defined(PARTIALLY_SORTED_LADDER_QUEUE)
    auto event = schedule_queue_[scheduler_id]->dequeue();

#elif defined(SPLAY_TREE)
    auto event = schedule_queue_[scheduler_id]->begin();
    if (event != nullptr) {
        schedule_queue_[scheduler_id]->erase(event);
    }

#elif defined(CIRCULAR_QUEUE)
    auto event = schedule_queue_[scheduler_id]->pop_front();

#else  /* STL MultiSet */
    auto event_iterator = schedule_queue_[scheduler_id]->begin();
    auto event = (event_iterator != schedule_queue_[scheduler_id]->end()) ?
                    *event_iterator : nullptr;
    if (event != nullptr) {
        schedule_queue_[scheduler_id]->erase(event_iterator);
    }
#endif

    // NOTE: scheduled_event_pointer is not changed here so that other threads will not schedule new
    // events and this thread can move events into processed queue and update schedule queue correctly.

    // NOTE: Event also remains in input queue until processing done. If this a a negative event
    // then, a rollback will bring the processed positive event back to input queue and they will
    // be cancelled.

    schedule_queue_lock_[scheduler_id].unlock();

    return event;
}

#ifdef PARTIALLY_SORTED_LADDER_QUEUE
/*
 *  NOTE: This is needed only for partially unsorted ladder queue
 */
unsigned int TimeWarpEventSet::lowestTimestamp (unsigned int thread_id) {

    unsigned int scheduler_id = worker_thread_scheduler_map_[thread_id];
    return schedule_queue_[scheduler_id]->lowestTimestamp();
}
#endif

/*
 *  NOTE: caller must have the input queue lock for the lp with id lp_id
 */
std::shared_ptr<Event> TimeWarpEventSet::lastProcessedEvent (unsigned int lp_id) {
#ifdef UNIFIED_QUEUE
    return unified_queue_[lp_id]->getPreviousUnprocessedEvent();
#else
    return ((processed_queue_[lp_id]->size()) ? processed_queue_[lp_id]->back() : nullptr);
#endif
}

/*
 *  NOTE: caller must have the input queue lock for the lp with id lp_id
 */
void TimeWarpEventSet::rollback (unsigned int lp_id, std::shared_ptr<Event> straggler_event) {

    // Every event GREATER OR EQUAL to straggler event must remove from the processed queue and
    // reinserted back into input queue.
    // EQUAL will ensure that a negative message will properly be cancelled out.
#ifdef UNIFIED_QUEUE
    unused(straggler_event);
    unified_queue_[lp_id]->debug();
    unified_queue_[lp_id]->fixPosition(); //the data for this function is locally asseciable in the queue
#else
    auto event_riterator = processed_queue_[lp_id]->rbegin();  // Starting with largest event

    while (event_riterator != processed_queue_[lp_id]->rend() && (**event_riterator >= *straggler_event)){

        auto event = std::move(processed_queue_[lp_id]->back()); // Starting from largest event
        assert(event);
        processed_queue_[lp_id]->pop_back();
        input_queue_[lp_id]->insert(event);
        event_riterator = processed_queue_[lp_id]->rbegin();
    }
#endif
}

/*
 *  NOTE: caller must have the input queue lock for the lp with id lp_id
 */
/// potential performace optimization needed, use call by referance.
std::unique_ptr<std::vector<std::shared_ptr<Event>>> 
    TimeWarpEventSet::getEventsForCoastForward (
                                unsigned int lp_id, 
                                std::shared_ptr<Event> straggler_event, 
                                std::shared_ptr<Event> restored_state_event) {

    // To avoid error if asserts are disabled
    unused(straggler_event);

    // Restored state event is the last event to contribute to the current state of the lpt.
    // All events GREATER THAN this event but LESS THAN the straggler event must be "coast forwarded"
    // so that the state remains consistent.
    //
    // It is assumed that all processed events GREATER THAN OR EQUAL to the straggler event have
    // been moved from the processed queue to the input queue with a call to rollback().
    //
    // All coast forwared events remain in the processed queue.

    // Create empty vector
    auto events = make_unique<std::vector<std::shared_ptr<Event>>>();
#ifdef UNIFIED_QUEUE
    unused(restored_state_event);
    uint64_t unProcessedStart = unified_queue_[lp_id]->getUnprocessedStart();
    while( unProcessedStart != unified_queue_[lp_id]->getFreeStart()){
        unified_queue_[lp_id]->debug();
        std::cout<<"unProcessedStart: "<<unProcessedStart<<"\n";
        auto event = unified_queue_[lp_id]->getValue(unProcessedStart);
        if(event!=nullptr){
            events->push_back(event);
        }
        unProcessedStart=unified_queue_[lp_id]->nextIndex(unProcessedStart);
    }
#else
    auto event_riterator = processed_queue_[lp_id]->rbegin();  // Starting with largest event

    while ((event_riterator != processed_queue_[lp_id]->rend()) && (**event_riterator > *restored_state_event)) {

        assert(*event_riterator);
        assert(**event_riterator < *straggler_event);
        // Events are in order of LARGEST to SMALLEST
        events->push_back(*event_riterator);
        event_riterator++;
    }
#endif
    return events;
}

/*
 *  NOTE: call must always have input queue lock for the lp which corresponds to lp_id
 *
 *  NOTE: This is called in the case of an negative message and no event is processed.
 *
 *  NOTE: This can only be called by the thread that handles events for the lp with id lp_id
 *
 */

//pull out from unprocessed queue and insert into schedule queue
void TimeWarpEventSet::startScheduling (unsigned int lp_id) {

    // Just simply add pointer to next event into the scheduler if input queue is not empty
    // for the given lp, otherwise set to nullptr
    //.. why do we insert it into schedule queue???
#ifdef UNIFIED_QUEUE
    if (!unified_queue_[lp_id]->getUnprocessedSign()) {
        scheduled_event_pointer_[lp_id] = unified_queue_[lp_id]->dequeue();
        unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
        schedule_queue_lock_[scheduler_id].lock();
        schedule_queue_[scheduler_id]->insert(scheduled_event_pointer_[lp_id]);
        schedule_queue_lock_[scheduler_id].unlock();
    } else {
        scheduled_event_pointer_[lp_id] = nullptr;
    }
#else
    if (!input_queue_[lp_id]->empty()) {
        scheduled_event_pointer_[lp_id] = *input_queue_[lp_id]->begin();
        unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
        schedule_queue_lock_[scheduler_id].lock();
        schedule_queue_[scheduler_id]->insert(scheduled_event_pointer_[lp_id]);
        schedule_queue_lock_[scheduler_id].unlock();
    } else {
        scheduled_event_pointer_[lp_id] = nullptr;
    }
#endif
}



//.. we wont need this anymore, invalid function for new queue
/*
 *  NOTE: This can only be called by the thread that handles event for the lp with id lp_id
 *
 *  NOTE: caller must always have the input queue lock for the lp which corresponds to lp_id
 *
 *  NOTE: the scheduled_event_pointer is also protected by input queue lock
 */
void TimeWarpEventSet::replenishScheduler (unsigned int lp_id) {

    // Something is completely wrong if there is no scheduled event because we obviously just
    // processed an event that was scheduled.
    assert(scheduled_event_pointer_[lp_id]);

    // Move the just processed event to the processed queue
#ifdef UNIFIED_QUEUE
    // Map the lp to the next schedule queue (cyclic order)
    // This is supposed to balance the load across all the schedule queues
    // Input queue lock is sufficient to ensure consistency
    unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
    // TODO, do we need to support this
    if (is_lp_migration_on_) {
        scheduler_id = (scheduler_id + 1) % num_of_schedulers_;
        input_queue_scheduler_map_[lp_id] = scheduler_id;
    }

    // Update scheduler with new event for the lp the previous event was executed for
    // NOTE: A pointer to the scheduled event will remain in the input queue
    if (!unified_queue_[lp_id]->getUnprocessedSign()) {
        scheduled_event_pointer_[lp_id] = unified_queue_[lp_id]->dequeue();
        schedule_queue_lock_[scheduler_id].lock();
        schedule_queue_[scheduler_id]->insert(scheduled_event_pointer_[lp_id]);
        schedule_queue_lock_[scheduler_id].unlock();
    } else {
        scheduled_event_pointer_[lp_id] = nullptr;
    }
#else
    auto num_erased = input_queue_[lp_id]->erase(scheduled_event_pointer_[lp_id]);
    assert(num_erased == 1);
    unused(num_erased);

    processed_queue_[lp_id]->push_back(scheduled_event_pointer_[lp_id]);

    // Map the lp to the next schedule queue (cyclic order)
    // This is supposed to balance the load across all the schedule queues
    // Input queue lock is sufficient to ensure consistency
    unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
    if (is_lp_migration_on_) {
        scheduler_id = (scheduler_id + 1) % num_of_schedulers_;
        input_queue_scheduler_map_[lp_id] = scheduler_id;
    }

    // Update scheduler with new event for the lp the previous event was executed for
    // NOTE: A pointer to the scheduled event will remain in the input queue
    if (!input_queue_[lp_id]->empty()) {
        scheduled_event_pointer_[lp_id] = *input_queue_[lp_id]->begin();
        schedule_queue_lock_[scheduler_id].lock();
        schedule_queue_[scheduler_id]->insert(scheduled_event_pointer_[lp_id]);
        schedule_queue_lock_[scheduler_id].unlock();
    } else {
        scheduled_event_pointer_[lp_id] = nullptr;
    }
#endif
}


//for this function, we invalid the event in the unified queue
bool TimeWarpEventSet::cancelEvent (unsigned int lp_id, std::shared_ptr<Event> cancel_event) {

#ifdef UNIFIED_QUEUE
    //cancel negative event
    auto res = unified_queue_[lp_id]->negativeFind(cancel_event); // this invalidates the event in unified queue
    if(res == unified_queue_[lp_id]->FindStatus::UNPROCESSED){
        return true;
    }
    else if(res == unified_queue_[lp_id]->FindStatus::ACTIVE){
        std::cout<<"ERROR: canceling an -ve event in active zone rollback condition, shouldnt occure\n";
        return false;
    }
    else{
        return false;
    }
#else
    bool found = false;
    auto neg_iterator = input_queue_[lp_id]->find(cancel_event);
    assert(neg_iterator != input_queue_[lp_id]->end());
    auto pos_iterator = std::next(neg_iterator);
    assert(pos_iterator != input_queue_[lp_id]->end());

    if (**pos_iterator == **neg_iterator) {
        input_queue_[lp_id]->erase(neg_iterator);
        input_queue_[lp_id]->erase(pos_iterator);
        found = true;
    }

    return found;
#endif
}

// For debugging
void TimeWarpEventSet::printEvent(std::shared_ptr<Event> event) {
    std::cout << "\tSender:     " << event->sender_name_                  << "\n"
              << "\tReceiver:   " << event->receiverName()                << "\n"
              << "\tSend time:  " << event->send_time_                    << "\n"
              << "\tRecv time:  " << event->timestamp()                   << "\n"
              << "\tGeneratrion:" << event->generation_                   << "\n"
              << "\tType:       " << (unsigned int)event->event_type_     << "\n";
}


// .. this gives the time stamp until which we need to increament the activeStart
unsigned int TimeWarpEventSet::fossilCollect (unsigned int fossil_collect_time, unsigned int lp_id) {

    unsigned int count = 0;
#ifdef UNIFIED_QUEUE
    if(unified_queue_[lp_id]->getUnprocessedSign()){
        return count;
    }
    //this is for termination of the warped kernel
    if (fossil_collect_time == (unsigned int)-1) {
        return abs(unified_queue_[lp_id]->getActiveStart()-unified_queue_[lp_id]->getUnprocessedStart());
    }

    //normal, do fossile collection until events smaller than equal to fossil-collection-time
    // discuss this with sounak,
    //going with this route as this is also thread safe atomic operation  
    uint64_t activeStart = unified_queue_[lp_id]->getActiveStart();
    while(unified_queue_[lp_id]->getValue(activeStart)->timestamp() <= fossil_collect_time && 
        unified_queue_[lp_id]->nextIndex(activeStart) != unified_queue_[lp_id]->nextIndex((unified_queue_[lp_id]->getUnprocessedStart()))){
        activeStart++;
        count++;
    }
    unified_queue_[lp_id]->setActiveStart(activeStart);

#else
    if (processed_queue_[lp_id]->empty()) {
        return count;
    }

    if (fossil_collect_time == (unsigned int)-1) {
        count = processed_queue_[lp_id]->size();
        processed_queue_[lp_id]->clear();
        return count;
    }

    auto event_iterator = processed_queue_[lp_id]->begin();
    while ((event_iterator != std::prev(processed_queue_[lp_id]->end())) &&
           ((*event_iterator)->timestamp() < fossil_collect_time)) {
        processed_queue_[lp_id]->pop_front();
        event_iterator = processed_queue_[lp_id]->begin();
        count++;
    }

#endif

    return count;
}

} // namespace warped

