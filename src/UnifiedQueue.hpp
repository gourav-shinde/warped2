#include <iostream>
#include <atomic>
#include <vector>
#include <cmath>
#include <stdexcept>
#include "CircularVectorIterator.hpp"
#include "Event.hpp"

#include <chrono>
#include <thread>



// T is the type of the elements in the queue
// comparator is function that compare_s two elements of type T (doesnt sort anymore)
// and returns true if the first element is smaller than the second
// T: queue_Type
// comparator: compare_ function which return A<B
template <typename T, typename comparator, typename negativeCounterPart>
class UnifiedQueue
/**
 * @file UnifiedQueue.hpp
 * @brief This file contains the implementation of the UnifiedQueue class.
 */

/**
 * @class UnifiedQueue
 * @brief A thread-safe queue implementation with unified markers for tracking active, unprocessed, and free elements.
 *
 * The UnifiedQueue class provides a thread-safe queue implementation with unified markers for tracking the active, unprocessed, and free elements.
 * It supports enqueue and dequeue operations, as well as various utility functions for accessing and modifying the queue.
 */
{
    class Data{
        public:
        Data(T data = T(),bool valid=true):data_(data), valid_(valid){};
        T data_;
        bool valid_;
        T getData(){
            return data_;
        }

        void invalidate(){
            valid_ = false;
        }

        void validate(){
            valid_ = true;
        }

        bool isValid(){
            return valid_;
        }
    };
private:
    std::vector<Data> queue_;
    // 16 bits activeStart_, 1bit unprocessedSign, 16 bits unprocessedStart_, 1 bit freeSign, 16 bits freeStart_
    // freeStart bit 0-16,           0x000000000000FFFF no shifting needed
    // freeSign bit 17,             0x0000000000010000
    // unprocessedStart bit 18-33   0x00000001FFFE0000  shift by 17
    // unprocessedSign bit 34       0x0000000200000000
    // activeStart bit 34-50        0x0003FFFC00000000  shift by 34
    //create variable for shifts too

    //define masks
    std::atomic<uint64_t> enqueue_counter_ = 0;
    std::atomic<uint64_t> dequeue_counter_ = 0;
    std::atomic<uint64_t> rollback_counter_ = 0;
    std::atomic<uint64_t> rollback_function_counter_ = 0;
    std::atomic<uint64_t> invalid_counter_ = 0;
    const uint64_t freeStartMask_ = 0x000000000000FFFF;
    const uint64_t freeSignMask_ = 0x0000000000010000;
    const uint64_t unprocessedStartMask_ = 0x00000001FFFE0000;
    const uint64_t unprocessedSignMask_ = 0x0000000200000000;
    const uint64_t activeStartMask_ = 0x0003FFFC00000000;
    const uint64_t unprocessedStartShift_ = 17;
    const uint64_t activeStartShift_ = 34;

    uint64_t marker_; 
    comparator compare_; //currently not  used, as we are not sorting anymore
    negativeCounterPart negativeCounterPart_; //used to find negative counterpart
    std::mutex lock_;
    uint32_t capacity_;
    // bool sortfunction(Data a, Data b) { return compare_(a.getData(), b.getData()); };
    
public:
    UnifiedQueue(uint16_t capacity=2048){
        if(capacity > 2048){
            throw std::invalid_argument("Capacity should be less than 1024");
        }
        queue_.resize(capacity); 
        capacity_ = capacity;
        //init condition is unprocessedSign
        marker_ = 0x0000000200000000;
    }

    void getlock(){
        lock_.lock();
    }

    void releaseLock(){
        lock_.unlock();
    }

    //--------------------------------------------------------------------------------
    // API functions

    /// @brief returns whether unprocessed zone is empty
    /// @return 1 if unprocessed zone is empty, 0 otherwise
    bool isUnprocessedZoneEmptyLocked(){
        getlock();
        bool ret = (marker_ & unprocessedSignMask_) ? true : false;  
        releaseLock();
        return ret;
    }

    /// @brief return whether unified queue is empty or not
    /// @return 1 if queue is full, 0 otherwise
    bool isFullLocked(){
        getlock();
        bool ret = (marker_ &  freeSignMask_) ? true : false;
        releaseLock();
        return ret;
    }

    //getActiveStart
    uint64_t getActiveStartLocked(){
        getlock();
        uint64_t ret { (marker_ & activeStartMask_) >> activeStartShift_ };
        releaseLock();
        return ret;
    }

    //getUnprocessedStart
    uint64_t getUnprocessedStartLocked(){
        getlock();
        uint64_t ret { (marker_ & unprocessedStartMask_) >> unprocessedStartShift_ };
        releaseLock();
        return ret;
    }

    //getFreeStart
    uint64_t getFreeStartLocked(){
        getlock();
        uint64_t ret {(marker_ & freeStartMask_) };
        releaseLock();
        return ret;
    }


    //--------------------------------------------------------------------------------


    /// @brief returns whether unprocessed zone is empty
    /// @return 1 if unprocessed zone is empty, 0 otherwise
    bool isUnprocessedZoneEmpty(){
        return (marker_ & unprocessedSignMask_) ? true : false;  
    }

    /// @brief return whether unified queue is empty or not
    /// @return 1 if queue is full, 0 otherwise
    bool isFull(){
        return (marker_ &  freeSignMask_) ? true : false;
    }

    //getActiveStart
    uint64_t getActiveStart(){

        return (marker_ & activeStartMask_) >> activeStartShift_;

    }

    //getUnprocessedStart
    uint64_t getUnprocessedStart(){
        return (marker_ & unprocessedStartMask_) >> unprocessedStartShift_ ;
    }

    //getFreeStart
    uint64_t getFreeStart(){
        return (marker_ & freeStartMask_);
    }

    //setfreeSign 
    void setFreeSign(bool sign){
        //set 10th bit to sign
        if(sign){
            marker_ |= freeSignMask_;
        }else{
            marker_ &= ~freeSignMask_;
        }
    }

    //setunprocessedSign
    void setUnprocessedSign(bool sign){
        if(sign){
            marker_ |= unprocessedSignMask_;
        }else{
            marker_ &= ~unprocessedSignMask_;
        }
    }

    //setActiveStart
    void setActiveStart(uint64_t start){
        marker_ &= ~activeStartMask_;
        marker_ |= (start << activeStartShift_);
    }

    //setUnprocessedStart
    void setUnprocessedStart(uint64_t start){
        marker_ &= ~unprocessedStartMask_;
        marker_ |= (start << unprocessedStartShift_);
    }

    //setFreeStart
    void setFreeStart(uint64_t start){
        marker_ &= ~freeStartMask_;
        marker_ |= start;
    }

    //getCapacity
    uint64_t capacity(){
        return capacity_;
    }

    //preIndex
    uint64_t prevIndex(uint64_t index){
        return (index + capacity() - 1) % capacity();
    }

    //nextIndex
    uint64_t nextIndex(uint64_t index){
        return (index + 1) % capacity();
    }

    bool isEmpty(){
        uint64_t marker = marker_;
        if(!FreeSign(marker) && ActiveStart(marker) == FreeStart(marker))
            return true;
        return false;
    }

    

    uint16_t size(){
        if(isEmpty()){
            return 0;
        }
        if(isFull()){
            return capacity();
        }
        if(getFreeStart() > getActiveStart()){
            return getFreeStart() - getActiveStart();
        }
        if(getFreeStart() < getActiveStart()){
            return capacity() - getActiveStart() + getFreeStart();
        }
        return INT16_MAX;// some other condition i do not know of
    }

    void debug(bool debug = false, uint64_t range = 0){
        //print marker_ in hexcode
        
        std::cout << "activeStart: " << getActiveStart();
        std::cout << " unprocessedSign: " << isUnprocessedZoneEmpty();
        std::cout << " unprocessedStart: " << getUnprocessedStart();
        std::cout << " freeSign: " << isFull();
        std::cout << " freeStart: " << getFreeStart();
        std::cout << " size: " << size() << std::endl;

        // int i = getUnprocessedStart();
        // if(!isUnprocessedZoneEmpty()){
        // do {
        //     std::cout << queue_[i].getData().receiveTime_ << " ";
        //     i = nextIndex(i);
        // } while (i != getFreeStart());
        // std::cout << std::endl;
        // }
        // else{
        //     std::cout<<"Unprocessed Events Empty"<<std::endl;
        // }
        // for (auto itr : queue_) {
        //     std::cout << itr.getData().receiveTime_<< " ";
        // }
        // std::cout << std::endl;

        if(debug){
            uint64_t i = getUnprocessedStart();
            for(uint64_t j=0;j<range;j++){
                i = prevIndex(i);
            }
            for(uint64_t j=0;j<range*2;j++){
                std::cout <<"("<<i<<",";
                if(!queue_[i].isValid()){
                    std::cout<<"-";
                }
                if(queue_[i].getData() != nullptr && queue_[i].getData()->event_type_ == warped::EventType::POSITIVE){
                    std::cout<<"+";
                }
                if(queue_[i].getData() != nullptr)
                    std::cout<<queue_[i].getData()->timestamp() << ")";
                else
                    std::cout<<")";
                i = nextIndex(i);
                if(i == getFreeStart()){
                    break;
                }
            }
            std::cout << std::endl;
        }
        else{
            uint16_t i {0};
            for (auto itr : queue_) {
                if(itr.getData() != nullptr){
                    std::cout<<"("<<i<<",";
                    if(!itr.isValid()){
                        std::cout<<"-";
                    }
                    if(itr.getData()->event_type_ == warped::EventType::POSITIVE){
                        std::cout<<"+";
                    }
                    std::cout<<itr.getData()->timestamp() << ")";
                }
                ++i;
            }
        }
         
    }

    //getValues from Marker
    
    uint64_t ActiveStart(uint64_t marker){
        uint64_t activeStart = (marker & activeStartMask_) >> activeStartShift_;
        return activeStart;
    }

    bool UnProcessedSign(uint64_t marker){
        return (marker & unprocessedSignMask_) ? true : false;
    }

    bool FreeSign(uint64_t marker){
        return (marker & freeSignMask_) ? true : false;
    }

    uint64_t UnprocessedStart(uint64_t marker){
        return (marker & unprocessedStartMask_) >> unprocessedStartShift_;
    }

    uint64_t FreeStart(uint64_t marker){
        return (marker & freeStartMask_);
    }

    //setValues for Marker

    //setFreeStart
    void setFreeStartMarker(uint64_t &marker, uint64_t start){
        marker &= ~freeStartMask_;
        marker |= start;
    }

    //setfreeSign
    void setFreeSignMarker(uint64_t &marker, bool sign){
        if(sign){
            marker |= freeSignMask_;
        }else{
            marker &= ~freeSignMask_;
        }
    }

    //setunprocessedSign
    void setUnprocessedSignMarker(uint64_t &marker, bool sign){
        if(sign){
            marker |= unprocessedSignMask_;
        }else{
            marker &= ~unprocessedSignMask_;
        }
    }

    //setActiveStart
    void setActiveStartMarker(uint64_t &marker, uint64_t start){
        marker &= ~activeStartMask_;
        marker |= (start << activeStartShift_);
    }

    //setUnprocessedStart
    void setUnprocessedStartMarker(uint64_t &marker, uint64_t start){
        marker &= ~unprocessedStartMask_;
        marker |= (start << unprocessedStartShift_);
    }

    // main functions

    //no checks for valid indexes is made here as it should be done by the caller

    uint64_t binarySearch(T element, uint64_t low, uint64_t high){
        uint64_t mid;

        // This will never trigger, as this condition is checked in the parent function
        // if (isEmpty())
        //     return getFreeStart();

        while (low < high) {
            mid = ceil((low + high) / 2);

            if (this->compare_(queue_[mid].getData(), element)) {
                low = (mid + 1) % capacity();
            }
            else {
                high = (mid) % capacity(); // very good chance for infinite loop
            }
        }

        return (low) % capacity();
    }

    uint64_t linearSearch(T element, uint64_t low, uint64_t high){
        uint64_t i = low;
        while (i != high) {
            if(queue_[i].getData() == nullptr){
                debug();
                std::cout<<"Queue is corrupted"<<std::endl;
                abort();
            }
            if (this->compare_(element, queue_[i].getData())) {
                return i;
            }
            i = nextIndex(i);
        }
        return i;
    }

    //no checks for valid indexes is made here as it should be done by the caller
    //low: activeStart_
    //high: freeStart_

    uint64_t findInsertPosition(T element, uint64_t low, uint64_t high){
        
        if (low == high && isFull() == 0){
            return getUnprocessedStart();
        }
        return linearSearch(element, low, high);

        // // when there is no rotation in queue
        // if (low < high) {
        //     return binarySearch(element, low, high);
        // }
        // // rotation i.e fossileStart_ < activeStart_
        // else {
        //     if (compare_(element, queue_[capacity() - 1].getData())) {
        //         return binarySearch(element, low, capacity() - 1);
        //     }
        //     else {
        //         return binarySearch(element, 0, high);
        //     }
        // }
    }

    void deleteIndex(uint64_t index){
        queue_[index].validate();
        queue_[index].getData().reset();
    }


    //shift elements from start to end by 1 position to the right
    //no checks for valid indexes is made here as it should be done by the caller
    //Discuss this as it will have ABA problem across threads
    void shiftElements(uint64_t start, uint64_t end) {
        uint64_t i = end;
        while (i != start) {
            queue_[i] = queue_[prevIndex(i)];
            i = prevIndex(i);
        } 
    }

    // we are not handling out of order elements in this queue.
    // This is thread safe
    uint64_t enqueue(T element, bool negative = false){
        
        std::lock_guard<std::mutex> lock(lock_);
       
        enqueue_counter_++;
        uint64_t insertPos = getUnprocessedStart();
        
        if (isFull()){
            //throw message
            this->debug();
            std::cout << "Queue is full" << std::endl;
            // std::__throw_bad_exception();
            std::cout<<"Enqueue Counter: "<<enqueue_counter_<<std::endl;
            std::cout<<"Dequeue Counter: "<<dequeue_counter_<<std::endl;
            std::cout<<"Rollback Counter: "<<rollback_counter_<<std::endl;
            std::cout<<"Rollback Function Counter: "<<rollback_function_counter_<<std::endl;
            abort();

            //reset all invalid events in unprocessed zone and refactor
            // uint63_t unprocessedStart = getUnprocessedStart();
            // uint63_t freeStart = getFreeStart();
            // while(unprocessedStart != freeStart){
            //     if(!queue_[unprocessedStart].isValid()){
            //         queue_[unprocessedStart].validate();
            //     }
            //     unprocessedStart = nextIndex(unprocessedStart);
            // }
        }

        uint64_t marker = marker_;
        uint64_t markerCopy = marker;
        if(nextIndex(FreeStart(marker)) == ActiveStart(marker)){//queue will become full after this insert
            //set freeSign_ to 1
            setFreeSignMarker(marker,1);
        }
        setUnprocessedSignMarker(marker, 0);
        setFreeStartMarker(marker, nextIndex(FreeStart(marker)));
             
        //FOR OUT OF ORDER LOGIC
        if(isEmpty()){//queue is empty
            marker_ = marker;
            queue_[UnprocessedStart(markerCopy)] = element;
            insertPos = UnprocessedStart(markerCopy);
        }
        else{
            // uint64_t UnprocessedStart = UnprocessedStart(markerCopy);
            // uint64_t FreeStart = FreeStart(markerCopy);
            // unused(FreeStart);
            // unused(UnprocessedStart);
            insertPos = findInsertPosition(element, UnprocessedStart(markerCopy), FreeStart(markerCopy));
            
            if(negative){
                if(queue_[insertPos].getData()!=nullptr && negativeCounterPart_(queue_[insertPos].getData(), element)){
                    queue_[insertPos].invalidate();
                    insertPos = INT32_MAX;
                    // std::cout<<"JUMBOOOOOOO";
                }
                else{
                    //didnt find positive counterpart and we insert
                    marker_ = marker;
                    shiftElements(insertPos, FreeStart(markerCopy));
                    queue_[insertPos] = element;
                }
            }
            else{
                marker_ = marker;
                shiftElements(insertPos, FreeStart(markerCopy));
                queue_[insertPos] = element;  
            }
        }

        if(!queue_[prevIndex(FreeStart(marker))].isValid()){
            setFreeStart(prevIndex(FreeStart(marker)));
            setFreeSign(false);
            if(getFreeStart() == getUnprocessedStart()){
                setUnprocessedSign(true);
            }
            
            deleteIndex(getFreeStart());
        }
        

        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return insertPos;
    }


    /// @brief
    /// @return returns the element at the front of the UnprocessStart
    T dequeue(){ 
        std::lock_guard<std::mutex> lock(lock_);
        // std::cout<<"dequeue called "<<std::endl;
        bool success = false;
        while(!success){
            //checks first
            if (isEmpty()){
                //throw message
                // std::cout << "Queue is empty" << std::endl;
                return nullptr;
            }
            if(isUnprocessedZoneEmpty()){
                //throw message
                // std::cout << "unprocessed Queue is empty" << std::endl;
                return nullptr;
            }

            uint64_t marker = marker_;
            uint64_t markerCopy = marker;
            if(nextIndex(UnprocessedStart(marker)) == FreeStart(marker)){
                //set unprocessedSign_ to 1
                setUnprocessedSignMarker(marker, 1);
                
            }
            setUnprocessedStartMarker(marker, nextIndex(UnprocessedStart(marker)));
            T element;
            
            element = queue_[UnprocessedStart(markerCopy)].getData();
            marker_= marker;
            if(queue_[UnprocessedStart(markerCopy)].isValid()){//this will make it so the function retrives next element if invalid element is found
                success = true;
                dequeue_counter_++;
            }
                    
            
            if(success){
                
                return element;
            }
            //call fix position here without updating the markers
            fixPositionInvalid();
            
        }
        return nullptr;
        
    }

    T getValue(uint64_t index){
        return queue_[index].getData();
    }

    bool isDataValid(uint64_t index){
        return queue_[index].isValid();
    }

    /// @brief find function return type
    enum FindStatus {
        ACTIVE,
        UNPROCESSED,
        NOTFOUND
    };

    ///
    /// \brief find element in Unified Queue
    /// \param element
    /// \return FindStatus
    ///
    FindStatus find(T element){
        
        FindStatus found=NOTFOUND;
        
        //checks first

        
        if (isEmpty()){
            //throw message
            std::cout << "Queue is empty" << std::endl;
            return NOTFOUND;
        }
        
        uint64_t marker = marker_;
        uint64_t markerCopy = marker;
        
        #ifdef GTEST_FOUND
            std::cout<<"increamentActiveStart called "<<std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        #endif
        
                
        // uint16_t ActiveIndex = ActiveStart(markerCopy);
        uint16_t UnProcessedIndex = UnprocessedStart(markerCopy);
        uint16_t FreeIndex = FreeStart(markerCopy);
        
        // while(ActiveIndex != UnProcessedIndex){
            
        //     if(queue_[ActiveIndex].getData() == element){
        //         found = ACTIVE; //rollback
        //         break;
        //     }
        //     ActiveIndex = nextIndex(ActiveIndex);
        // }
        
        while(UnProcessedIndex != FreeIndex){
            
            if(negativeCounterPart_(queue_[UnProcessedIndex].getData(), element)){
                found = UNPROCESSED; //Invalidate The element
                queue_[UnProcessedIndex].invalidate();
                break;
            }
            UnProcessedIndex = nextIndex(UnProcessedIndex);
        }
            
            
            
            
        
        return found;

    }


    //sorting a portion of the buffer
    //issue with this is if the unprocessed zone is the whole queue, this sorting doesnt work i am hoping this condition never happens, will put a check
    void sortPortion(uint32_t start, uint32_t end) {
        int sortedRange = (int(end - start) + capacity()) % capacity();
        if(end ==start){
            std::cerr<<"Unprocessed queue is the whole queue rotated sort aborted\n";
        }
        // Custom comparator function for sorting
        auto comp =  [&](Data& a, Data& b) { 
            return (compare_(a.getData(), b.getData()));
        };
        

        
        auto it = make_circular_iterator(queue_, start);
        std::sort(it, it + sortedRange, comp);
    }

    void sortQueue(){
        if(isUnprocessedZoneEmpty()){
            return;
        }
        uint64_t unprocessedStart_ = getUnprocessedStart();
        uint64_t freeStart_ = getFreeStart();

        if(unprocessedStart_ < freeStart_){ //no rotation
            std::sort(queue_.begin() + unprocessedStart_, queue_.begin() + freeStart_, [this](Data a, Data b) { return compare_(a.getData(), b.getData()); });
        }
        else{ //rotation
            sortPortion(unprocessedStart_, freeStart_);
        }
    }


    /// @brief This fixes the position of the events
    /// No Markers change
    bool fixPosition(bool debug = false) {
        rollback_function_counter_++;
        if (getActiveStart() == getUnprocessedStart() && !isFull()) {//active zone is empty
            return false;
        }

        uint64_t marker = marker_;
        uint64_t activeStart = ActiveStart(marker);

#ifdef GTEST_FOUND
        std::cout << "Fixposition called " << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        //find index to swap with

        //get previous valid event from unprocessed start
        uint16_t swap_index_r = prevIndex(UnprocessedStart(marker));
        if(debug){
            std::cout<<"event which is supposed to be swapped\n";
            std::cout<<queue_[swap_index_r].getData()->timestamp()<<std::endl;
            if(queue_[swap_index_r].getData()->event_type_ == warped::EventType::NEGATIVE){
                std::cout<<"NEGATIVE\n";
            }
            else{
                std::cout<<"POSITIVE\n";
            }
        }
        uint16_t swap_index_l = swap_index_r;
        // debug(true, 5);
        if(debug)
        std::cout<<"swap_index_r: "<<swap_index_r<<std::endl;
        while (swap_index_r != activeStart && compare_(queue_[swap_index_r].getData(), queue_[prevIndex(swap_index_r)].getData())) {
            std::swap(queue_[prevIndex(swap_index_r)], queue_[swap_index_r]);
            
            swap_index_r = prevIndex(swap_index_r);
            rollback_counter_++;
            setUnprocessedSign(false);
            setUnprocessedStart(swap_index_r);
            
        }
        
        if(swap_index_r != swap_index_l){
            //we dont call sort here so in case of negative event, we put in before the positive event,
            //this sets unprocessed start to next index of straggler event
            return true;
        }
        
        return false;

        
    }

    


    /// @brief This fixes the position of the events
    /// No Markers change
    bool fixPositionInvalid() {
        // std::cout<<"called invalid fix";
        if (getActiveStart() == getUnprocessedStart() && !isFull()) {//active zone is empty
            return false;
        }

        uint64_t marker = marker_;
        uint64_t activeStart = ActiveStart(marker);


        //get previous valid event from unprocessed start
        uint16_t swap_index_r = prevIndex(UnprocessedStart(marker));
        uint16_t swap_index_l = swap_index_r;
        // debug(true, 5);
        
        while (swap_index_r != activeStart && compare_(queue_[swap_index_r].getData(), queue_[prevIndex(swap_index_r)].getData())) {
            std::swap(queue_[prevIndex(swap_index_r)], queue_[swap_index_r]);
            swap_index_r = prevIndex(swap_index_r); 
        }
        
        if(swap_index_r != swap_index_l){
            return true;
        }
        
        return false;

        
    }


    /// @brief returns previous valid unproceesed event
    /// @return 
    T getPreviousUnprocessedEvent(){
        std::lock_guard<std::mutex> lock(lock_);
        T element = nullptr;
        
        //this is called after a dequeue so we need it to go before it
        uint16_t index=prevIndex(getUnprocessedStart());
        // element=queue_[index].getData();
        do{
            element=queue_[prevIndex(index)].getData();
            
            if(queue_[prevIndex(index)].isValid()){
                break;
            }
            index=prevIndex(index);
           
        }while(getActiveStart()!=index); // this can be Infinite if all elements are invalid
        
        return element;
        
    }

    T getNextUnprocessedEvent(){
        T element = nullptr;
        uint16_t index=nextIndex(getUnprocessedStart());
        do{
                element=queue_[nextIndex(index)].getData();
            index=nextIndex(index);
        }while(!queue_[nextIndex(index)].isValid() && getFreeStart()!=index); // this can be Infinite if all elements are invalid
        return element;
    }

    /// need a -ve comparator
    /// @brief called when we process a -ve event
    /// @param element
    /// @return TriStatus
    FindStatus negativeFind(T element){
        //assuming the negative counterpart is already processed by now
        FindStatus found=NOTFOUND;
       
        if (isEmpty()){
            //throw message
            std::cout << "Queue is empty" << std::endl;
            return NOTFOUND;
        }
        
        
        uint64_t markerCopy = marker_;
        
        
        
                
        uint16_t ActiveIndex = ActiveStart(markerCopy);
        uint16_t UnProcessedIndex = UnprocessedStart(markerCopy);
        uint16_t FreeIndex = FreeStart(markerCopy);
        
        while(ActiveIndex != UnProcessedIndex){
            
            if(this->negativeCounterPart_(queue_[ActiveIndex].getData(), element)){
                found = ACTIVE; //rollback
                
                break;
            }
            ActiveIndex = nextIndex(ActiveIndex);
        }
        while(UnProcessedIndex != FreeIndex && found!=ACTIVE){
            
            if(this->negativeCounterPart_(queue_[UnProcessedIndex].getData(), element)){
                found = UNPROCESSED; //Invalidate The element
                
                queue_[UnProcessedIndex].invalidate();
                break;
            }
            UnProcessedIndex = nextIndex(UnProcessedIndex);
        }
            
            
        return found;

    }

    //invalids the data at unprocessedStart
    bool invalidNegative(){
        uint64_t marker = marker_;
        if(!UnProcessedSign(marker)){
            queue_[UnprocessedStart(marker)].invalidate();
            return true;
        }
        std::cout << "unprocessed Queue is empty" << std::endl;
        return false;
    }

    //invalids the data at index
    void invalidateIndex(uint64_t index){
        invalid_counter_++;
        queue_[index].invalidate();
    }


};
