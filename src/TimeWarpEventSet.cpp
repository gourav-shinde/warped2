#include <algorithm>
#include <cassert>
#include <string>

#include "TimeWarpEventSet.hpp"
#include "utility/warnings.hpp"


namespace warped
{

    /// @brief Lambda function which compares for its positive couterpart
    /// @param first Positive event
    /// @param second Negative event
    auto compareEventsLambda = [](const std::shared_ptr<Event> &first,
                                  const std::shared_ptr<Event> &second)
    {
        return ((first->timestamp() == second->timestamp()) && (first->send_time_ == second->send_time_) && (first->sender_name_ == second->sender_name_) && (first->generation_ == second->generation_) && (first->event_type_ == EventType::POSITIVE));
    };

    void TimeWarpEventSet::acquireUnifiedQueueLock(unsigned int lp_id)
    {
        unified_queue_[lp_id]->getlock();
    }

    void TimeWarpEventSet::releaseUnifiedQueueLock(unsigned int lp_id)
    {
        unified_queue_[lp_id]->releaseLock();
    }

    void TimeWarpEventSet::initialize(const std::vector<std::vector<LogicalProcess *>> &lps,
                                      unsigned int num_of_lps,
                                      bool is_lp_migration_on,
                                      unsigned int num_of_worker_threads)
    {
        unused(num_of_worker_threads);
        num_of_lps_ = num_of_lps;
        num_of_schedulers_ = lps.size();
        is_lp_migration_on_ = is_lp_migration_on;

        for (unsigned int scheduler_id = 0; scheduler_id < lps.size(); scheduler_id++)
        {
            for (unsigned int lp_id = 0; lp_id < lps[scheduler_id].size(); lp_id++)
            {
                
                unified_queue_.push_back(make_unique<UnifiedQueue<std::shared_ptr<Event>, compareEvents, compareNegativeEvent>>());
                scheduled_event_pointer_.push_back(nullptr);
                input_queue_scheduler_map_.push_back(scheduler_id);
            }
            lp_size_per_thread.push_back(lps[scheduler_id].size());
        }
        
        for(unsigned int i = 1; i < num_of_schedulers_; i++){
            lp_size_per_thread[i] += lp_size_per_thread[i-1];
        }
        

        /* Create the schedule queues */
        for (unsigned int scheduler_id = 0; scheduler_id < num_of_schedulers_; scheduler_id++)
        {
#if defined(SORTED_LADDER_QUEUE) || defined(PARTIALLY_SORTED_LADDER_QUEUE)
            schedule_queue_.push_back(make_unique<LadderQueue>());
#elif defined(SPLAY_TREE)
            schedule_queue_.push_back(make_unique<SplayTree>());
#elif defined(CIRCULAR_QUEUE)
            schedule_queue_.push_back(make_unique<CircularQueue>(lps[scheduler_id].size()));
#else
            schedule_queue_.push_back(
                make_unique<std::multiset<std::shared_ptr<Event>, relaxedCompareEvents>>());
#endif
        }

        schedule_queue_lock_ = make_unique<std::mutex []>(num_of_schedulers_);

        /* Create the data structure for holding the lowest event timestamp */

        for (unsigned int i = 0; i <= num_of_worker_threads; i++)
        {
            // zero is not used as its main thread
            schedule_cycle_.push_back(std::make_shared<ThreadMin>(INT32_MAX));
        }
    }
    

    std::shared_ptr<Event> TimeWarpEventSet::getUnprocessedStartValue(unsigned int lp_id)
    {
        return unified_queue_[lp_id]->getValue(unified_queue_[lp_id]->getUnprocessedStart());
    }
    

    void TimeWarpEventSet::resetThreadMin(unsigned int thread_id)
    {
        std::cout<<INT32_MAX<<"\n";
        schedule_cycle_[thread_id]->min.store(std::numeric_limits<uint32_t>::max());
    }

    void TimeWarpEventSet::reportEvent(std::shared_ptr<Event> event, uint16_t thread_id)
    {
        // std::cout<<thread_id<<"\n";
        // assert(event!= nullptr);
        // assert(schedule_cycle_[thread_id] != nullptr);
        if (event!=nullptr)
        {
            schedule_cycle_[thread_id]->min.store(std::min(schedule_cycle_[thread_id]->min.load(), event->timestamp()));
        }
        else{
            schedule_cycle_[thread_id]->min.store(std::min(schedule_cycle_[thread_id]->min.load(), (uint32_t)0));
        }
    }

    void TimeWarpEventSet::debugLPQueue(unsigned int lp_id)
    {
        // unused(lp_id);
        unified_queue_[lp_id]->debug();
    }

    /*
     *  NOTE: caller must always have the input queue lock for the lp with id lp_id
     *
     *  NOTE: scheduled_event_pointer is also protected by the input queue lock
     */
    InsertStatus TimeWarpEventSet::insertEvent(
        unsigned int lp_id, std::shared_ptr<Event> event, uint32_t thread_id)
    {

        auto ret = InsertStatus::Success;

        
        uint64_t insertPos{0};
        if (event->event_type_ == EventType::NEGATIVE)
        {
            insertPos = unified_queue_[lp_id]->enqueue(event, true);
        }
        else
        {
            insertPos = unified_queue_[lp_id]->enqueue(event);
        }
        if(insertPos == INT32_MAX){
            ret = InsertStatus::Cancelled;
        }
        

        // if(lp_id == 24){
        //     std::cout<<"\ninserting event\n";
        //     unified_queue_[lp_id]->debug();
        // }
    

        reportEvent(unified_queue_[lp_id]->getValue(unified_queue_[lp_id]->getUnprocessedStart()), thread_id);


        if (scheduled_event_pointer_[lp_id] == nullptr)
        {
            startScheduling(lp_id, thread_id);

            if(ret == InsertStatus::Cancelled){
                ret = InsertStatus::StarvAndCancelled;
            }
            else{
                ret = InsertStatus::StarvedObject;
            }
        }

        return ret;
    }

    /*
     *  NOTE: caller must always have the input queue lock for the lp with id lp_id
     */
    std::shared_ptr<Event> TimeWarpEventSet::getEvent(unsigned int thread_id)
    {

#if defined(SORTED_LADDER_QUEUE) || defined(PARTIALLY_SORTED_LADDER_QUEUE)
        auto event = schedule_queue_[scheduler_id]->dequeue();

#elif defined(SPLAY_TREE)
        auto event = schedule_queue_[scheduler_id]->begin();
        if (event != nullptr)
        {
            schedule_queue_[scheduler_id]->erase(event);
        }

#elif defined(CIRCULAR_QUEUE)
    auto event = schedule_queue_[scheduler_id]->pop_front();

#else /* STL MultiSet */
    schedule_queue_lock_[thread_id].lock();
    auto event_iterator = schedule_queue_[thread_id]->begin();
    auto event = (event_iterator != schedule_queue_[thread_id]->end()) ? *event_iterator : nullptr;
    
    if (event != nullptr)
    {
        reportEvent(event, thread_id);
        assert(event_iterator!=schedule_queue_[thread_id]->end());
        assert(schedule_queue_[thread_id] != nullptr);
        assert(event != nullptr);
        assert(event->timestamp() != 0);
        // std::cerr<<"event "<<event->timestamp()<<" ";
        schedule_queue_[thread_id]->erase(event_iterator);
    }
    schedule_queue_lock_[thread_id].unlock();
#endif

        // NOTE: scheduled_event_pointer is not changed here so that other threads will not schedule new
        // events and this thread can move events into processed queue and update schedule queue correctly.

        // NOTE: Event also remains in input queue until processing done. If this a a negative event
        // then, a rollback will bring the processed positive event back to input queue and they will
        // be cancelled.

        return event;
    }

    uint32_t TimeWarpEventSet::lowestTimestamp(unsigned int thread_id)
    {
        if(schedule_cycle_[thread_id]->min.load() == INT32_MAX){
            return 0;
        }
        return schedule_cycle_[thread_id]->min.load();
    }

    /*
     *  NOTE: caller must have the input queue lock for the lp with id lp_id
     */
    std::shared_ptr<Event> TimeWarpEventSet::lastProcessedEvent(unsigned int lp_id)
    {
       
        return unified_queue_[lp_id]->getPreviousUnprocessedEvent();
    }

    /*
     *  NOTE: caller must have the input queue lock for the lp with id lp_id
     */
    std::shared_ptr<Event> TimeWarpEventSet::nextUnprocessedEvent(unsigned int lp_id)
    {
        return unified_queue_[lp_id]->getNextUnprocessedEvent();
    }

    void TimeWarpEventSet::reportLastUnprocessedEvent(uint32_t lp_id, uint32_t thread_id)
    {   
        reportEvent(unified_queue_[lp_id]->getPreviousUnprocessedEvent(), thread_id); 
    }
    
    /*
     *  NOTE: caller must have the input queue lock for the lp with id lp_id
     */
    void TimeWarpEventSet::rollback(unsigned int lp_id, std::shared_ptr<Event> straggler_event)
    {

        // Every event GREATER OR EQUAL to straggler event must remove from the processed queue and
        // reinserted back into input queue.
        // EQUAL will ensure that a negative message will properly be cancelled out.
        unified_queue_[lp_id]->rollback(straggler_event);
        
    }

    /*
     *  NOTE: caller must have the input queue lock for the lp with id lp_id
     */
    /// this also changes the unProcessedmarker to staggler event
    std::unique_ptr<std::vector<std::shared_ptr<Event>>>
    TimeWarpEventSet::getEventsForCoastForward(
        unsigned int lp_id,
        std::shared_ptr<Event> straggler_event,
        std::shared_ptr<Event> restored_state_event)
    {
        
        // To avoid error if asserts are disabled
        // unused(straggler_event);

        // // Restored state event is the last event to contribute to the current state of the lpt.
        // // All events GREATER THAN this event but LESS THAN the straggler event must be "coast forwarded"
        // // so that the state remains consistent.
        // //
        // // It is assumed that all processed events GREATER THAN OR EQUAL to the straggler event have
        // // been moved from the processed queue to the input queue with a call to rollback().
        // //
        // // All coast forwared events remain in the processed queue.

        // // Create empty vector
        // // if(lp_id == 24){
        // //     std::cout<<"coastforward called\n";
        // // }
        // auto events = make_unique<std::vector<std::shared_ptr<Event>>>();
        // unused(restored_state_event);
        
        // uint64_t unProcessedStart = unified_queue_[lp_id]->getUnprocessedStart();
        // // uint32_t freeStart = unified_queue_[lp_id]->getFreeStart();
        // uint64_t activeStart = unified_queue_[lp_id]->getActiveStart();

        // if(activeStart == unProcessedStart){
        //     //means active zone is empty
        //     // std::cerr<<"ERROR: active zone is empty\n";
        //     // std::cout<<"lp_id: "<<lp_id<<"\n";
        //     // unified_queue_[lp_id]->debug(true, 10);
        //     // printEvent(restored_state_event);
        //     // printEvent(straggler_event);
        //     // if(lp_id ==24){
        //     //     std::cout<<"coastforward Empty\n";
        //     // }

        //     return events;
        // }
        
        // unProcessedStart = unified_queue_[lp_id]->prevIndex(unProcessedStart);
        // if(straggler_event->event_type_ == EventType::POSITIVE){
        // //     //becoz we increament it in rollback if event is positive straggler
        // unProcessedStart = unified_queue_[lp_id]->prevIndex(unProcessedStart);
        // }

        

        // compareEvents compare;
        
        // while (unified_queue_[lp_id]->getValue(unProcessedStart)!=nullptr && compare(restored_state_event ,unified_queue_[lp_id]->getValue(unProcessedStart)))
        // {
            
        //     if(unified_queue_[lp_id]->isDataValid(unProcessedStart) &&  compare(straggler_event, unified_queue_[lp_id]->getValue(unProcessedStart))){
        //         std::cerr<<"ERROR: straggler event is bigger than event in coast forward\n";
        //         printEvent(straggler_event);
        //         printEvent(unified_queue_[lp_id]->getValue(unProcessedStart));
        //         printEvent(restored_state_event);
        //         std::cerr<<unProcessedStart<<"\n";
        //         unified_queue_[lp_id]->debug(true, 20);
        //         unified_queue_[lp_id]->debug();
        //         abort();
        //     }
            
        //     // std::cout<<"unProcessedStart: "<<unProcessedStart<<"\n";
        //     if (unified_queue_[lp_id]->isDataValid(unProcessedStart))
        //     {
        //         if(straggler_event == unified_queue_[lp_id]->getValue(unProcessedStart)){
        //             std::cerr<<"equal event in coast forward\n";
        //             printEvent(straggler_event);
        //             printEvent(unified_queue_[lp_id]->getValue(unProcessedStart));
        //             printEvent(restored_state_event);
        //             std::cerr<<lp_id<<"\n";
        //             unified_queue_[lp_id]->debug();
        //             abort();
        //         }

        //         // if(lp_id == 24){
        //         //     std::cout<<"hmm";
        //         //     std::cout<<unified_queue_[lp_id]->getValue(unProcessedStart)->timestamp()<<" ";
        //         // }

        //         events->push_back(unified_queue_[lp_id]->getValue(unProcessedStart));

        //         if (unified_queue_[lp_id]->getValue(unProcessedStart)->event_type_ == EventType::NEGATIVE)
        //         {
        //             std::cout << "ERROR: negative event in coast forward\n";
        //             std::cout << "lp_id: " << lp_id << "\n";
        //             std::cout << "timestamp " << unified_queue_[lp_id]->getValue(unProcessedStart)->timestamp() << "\n";
        //             unified_queue_[lp_id]->debug(true, 10);
        //             abort();
        //         }

        //     }
        //     if(unProcessedStart == activeStart){
        //         //worth taking a look at
        //         break;
        //     }
            
        //     unProcessedStart = unified_queue_[lp_id]->prevIndex(unProcessedStart);

            
        // }
        // // if(lp_id == 24){
        // //     std::cout<<"\n";
        // // }

        // // if(lp_id ==24){
        // //         std::cout<<"returning coast forwards\n";
        // // }
        
        
        
        // return events;
        return unified_queue_[lp_id]->getCoastEvent(straggler_event, restored_state_event, lp_id);
    }

    /*
     *  NOTE: call must always have input queue lock for the lp which corresponds to lp_id
     *
     *  NOTE: This is called in the case of an negative message and no event is processed.
     *
     *  NOTE: This can only be called by the thread that handles events for the lp with id lp_id
     *
     */

    // pull out from unprocessed queue and insert into schedule queue
    void TimeWarpEventSet::startScheduling(unsigned int lp_id, uint32_t thread_id)
    {

        // Just simply add pointer to next event into the scheduler if input queue is not empty
        // for the given lp, otherwise set to nullptr
        //.. why do we insert it into schedule queue???

        
            scheduled_event_pointer_[lp_id] = unified_queue_[lp_id]->dequeue();
            // if(lp_id == 24){
            //     std::cout<<"\nscheduled Event\n";
            //     unified_queue_[lp_id]->debug();
            // }
            unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
            // if(scheduled_event_pointer_[lp_id] == nullptr){
            //     std::cerr<<"ERROR: scheduled event pointer is null\n";
            //     std::cerr<<"lp_id: "<<lp_id<<"\n";
            //     unified_queue_[lp_id]->debug(true, 10);
            //     abort();
            // }
            if(scheduled_event_pointer_[lp_id] !=nullptr){
                // if(lp_id == 6715){
                //     std::cout<<"dequeue "<<lp_id<<" ustart"<<unified_queue_[lp_id]->getUnprocessedStart()<<" fstart"<<unified_queue_[lp_id]->getFreeStart()<<std::endl;
                // }
                assert(scheduled_event_pointer_[lp_id] != nullptr);
                assert(schedule_queue_[scheduler_id]!=nullptr);
                schedule_queue_lock_[scheduler_id].lock();
                schedule_queue_[scheduler_id]->insert(scheduled_event_pointer_[lp_id]);
                schedule_queue_lock_[scheduler_id].unlock();

                reportEvent(scheduled_event_pointer_[lp_id], thread_id);
            }

    }

    //.. we wont need this anymore, invalid function for new queue
    /*
     *  NOTE: This can only be called by the thread that handles event for the lp with id lp_id
     *
     *  NOTE: caller must always have the input queue lock for the lp which corresponds to lp_id
     *
     *  NOTE: the scheduled_event_pointer is also protected by input queue lock
     */
    void TimeWarpEventSet::replenishScheduler(unsigned int lp_id, uint32_t thread_id)
    {

        // Something is completely wrong if there is no scheduled event because we obviously just
        // processed an event that was scheduled.
        assert(scheduled_event_pointer_[lp_id]);

        // Move the just processed event to the processed queue

        // Map the lp to the next schedule queue (cyclic order)
        // This is supposed to balance the load across all the schedule queues
        // Input queue lock is sufficient to ensure consistency
        unsigned int scheduler_id = input_queue_scheduler_map_[lp_id];
        // TODO, do we need to support this
        if (is_lp_migration_on_)
        {
            scheduler_id = (scheduler_id + 1) % num_of_schedulers_;
            input_queue_scheduler_map_[lp_id] = scheduler_id;
        }

        // Update scheduler with new event for the lp the previous event was executed for
        // NOTE: A pointer to the scheduled event will remain in the input queue
        
        scheduled_event_pointer_[lp_id] = unified_queue_[lp_id]->dequeue();

        // if(lp_id == 24){
        //     std::cout<<"\nreplinished event\n";
        //     unified_queue_[lp_id]->debug();
        // }
        
        if(scheduled_event_pointer_[lp_id] !=nullptr){
            // if(lp_id == 6715){
            //        std::cout<<"dequeue "<<lp_id<<" ustart"<<unified_queue_[lp_id]->getUnprocessedStart()<<" fstart"<<unified_queue_[lp_id]->getFreeStart()<<std::endl;
            // }
            assert(scheduled_event_pointer_[lp_id] != nullptr);
            assert(schedule_queue_[scheduler_id]!=nullptr);
            schedule_queue_lock_[scheduler_id].lock();
            schedule_queue_[scheduler_id]->insert(scheduled_event_pointer_[lp_id]);
            schedule_queue_lock_[scheduler_id].unlock();
            reportEvent(scheduled_event_pointer_[lp_id], thread_id);
        }

    }

    // for this function, we invalid the event in the unified queue
    bool TimeWarpEventSet::cancelEvent(unsigned int lp_id, std::shared_ptr<Event> cancel_event)
    {

#ifdef UNIFIED_QUEUE
        // cancel negative event
        auto res = unified_queue_[lp_id]->negativeFind(cancel_event); // this invalidates the event in unified queue
        if (res == unified_queue_[lp_id]->FindStatus::UNPROCESSED)
        {
            return true;
        }
        else if (res == unified_queue_[lp_id]->FindStatus::ACTIVE)
        {
            std::cout << "ERROR: canceling an -ve event in active zone rollback condition, shouldnt occure\n";
            return false;
        }
        else
        {
            return false;
        }
#else
        bool found = false;
        auto neg_iterator = input_queue_[lp_id]->find(cancel_event);
        assert(neg_iterator != input_queue_[lp_id]->end());
        auto pos_iterator = std::next(neg_iterator);
        assert(pos_iterator != input_queue_[lp_id]->end());

        if (**pos_iterator == **neg_iterator)
        {
            input_queue_[lp_id]->erase(neg_iterator);
            input_queue_[lp_id]->erase(pos_iterator);
            found = true;
        }

        return found;
#endif
    }

    // For debugging
    void TimeWarpEventSet::printEvent(std::shared_ptr<Event> event)
    {
        std::cout << "\tSender:     " << event->sender_name_ << "\n"
                  << "\tReceiver:   " << event->receiverName() << "\n"
                  << "\tSend time:  " << event->send_time_ << "\n"
                  << "\tRecv time:  " << event->timestamp() << "\n"
                  << "\tGeneratrion:" << event->generation_ << "\n"
                  << "\tType:       " << (unsigned int)event->event_type_ << "\n";
    }

    // .. this gives the time stamp until which we need to increament the activeStart
    unsigned int TimeWarpEventSet::fossilCollect(unsigned int fossil_collect_time, unsigned int lp_id)
    {
        return unified_queue_[lp_id]->fossilCollect(fossil_collect_time, lp_id);
    }

} // namespace warped
