#include <iostream>
#include <atomic>
#include <vector>
#include <cmath>
#include <stdexcept>
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

        bool isValid(){
            return valid_;
        }
    };
private:
    std::vector<Data> queue_;
    // 10 bits activeStart_, 1bit unprocessedSign, 10 bits unprocessedStart_, 1 bit freeSign, 10 bits freeStart_
    // freeStart bit 0-9,           0x000003FF no shifting needed
    // freeSign bit 10,             0x00000400
    // unprocessedStart bit 11-20   0x001FF800  shift by 11
    // unprocessedSign bit 21       0x00200000
    // activeStart bit 22-31        0xFFC00000  shift by 22
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

    std::atomic<uint64_t> marker_; //test with this datatype
    comparator compare_; //currently not  used, as we are not sorting anymore
    negativeCounterPart negativeCounterPart_; //used to find negative counterpart
    std::mutex lock_;
    // bool sortfunction(Data a, Data b) { return compare_(a.getData(), b.getData()); };
    
public:
    UnifiedQueue(uint16_t capacity=1024){
        if(capacity > 1024){
            throw std::invalid_argument("Capacity should be less than 1024");
        }
        queue_.resize(capacity); 
        //init condition is 0,0,0
        marker_.store(0, std::memory_order_relaxed);
    }

    void getlock(){
        lock_.lock();
    }

    void releaseLock(){
        lock_.unlock();
    }

    bool getUnprocessedSign(){
        return (marker_.load(std::memory_order_relaxed) & unprocessedSignMask_) ? true : false;
    }

    bool getFreeSign(){
        return (marker_.load(std::memory_order_relaxed) &  freeSignMask_) ? true : false;
    }

    //getActiveStart
    uint64_t getActiveStart(){
        return (marker_.load(std::memory_order_relaxed) & activeStartMask_) >> activeStartShift_;
    }

    //getUnprocessedStart
    uint64_t getUnprocessedStart(){
        return (marker_.load(std::memory_order_relaxed) & unprocessedStartMask_) >> unprocessedStartShift_;
    }

    //getFreeStart
    uint64_t getFreeStart(){
        return (marker_.load(std::memory_order_relaxed) & freeStartMask_);
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
        return queue_.size();
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
        uint64_t marker = marker_.load(std::memory_order_relaxed);
        if(!FreeSign(marker) && ActiveStart(marker) == FreeStart(marker))
            return true;
        return false;
    }

    bool isFull(){
        return getFreeSign();
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
        // std::cout << "marker_: " << std::hex << marker_.load(std::memory_order_relaxed) << std::endl;
        std::cout << "activeStart: " << getActiveStart();
        std::cout << " unprocessedSign: " << getUnprocessedSign();
        std::cout << " unprocessedStart: " << getUnprocessedStart();
        std::cout << " freeSign: " << getFreeSign();
        std::cout << " freeStart: " << getFreeStart();
        std::cout << " size: " << size() << std::endl;

        // int i = getUnprocessedStart();
        // if(!getUnprocessedSign()){
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
            for (auto itr : queue_) {
                std::cout<<"(";
                if(!itr.isValid()){
                    std::cout<<"-";
                }
                if(itr.getData()->event_type_ == warped::EventType::POSITIVE){
                    std::cout<<"+";
                }
                if(itr.getData() != nullptr)
                    std::cout<<itr.getData()->timestamp() << ")";
                else
                    std::cout<<")";
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
        
        if (low == high && getFreeSign() == 0){
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
        
        lock_.lock();
       
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
            }

           uint64_t marker = marker_.load(std::memory_order_relaxed);
           uint64_t markerCopy = marker;
           if(nextIndex(FreeStart(marker)) == ActiveStart(marker)){//queue will become full after this insert
                //set freeSign_ to 1
                setFreeSign(1);
            }
            setUnprocessedSignMarker(marker, 0);
            setFreeStartMarker(marker, nextIndex(FreeStart(marker)));
             
        ;
        //FOR OUT OF ORDER LOGIC
        if(isEmpty()){//queue is empty
            while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                queue_[UnprocessedStart(markerCopy)] = element;
                insertPos = UnprocessedStart(markerCopy);
            }
            
            
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
                    while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                        shiftElements(insertPos, FreeStart(markerCopy));
                        queue_[insertPos] = element;
                    }
                    
                }

            }
            else{
                while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                        shiftElements(insertPos, FreeStart(markerCopy));
                        queue_[insertPos] = element;
                }
                
            }
        }
        
        
        

        lock_.unlock();
    
           
        
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return insertPos;
    }


    /// @brief
    /// @return returns the element at the front of the UnprocessStart
    T dequeue(){ 
        
        // std::cout<<"dequeue called "<<std::endl;
        bool success = false;
        while(!success){
            //checks first
            if (isEmpty()){
                //throw message
                std::cout << "Queue is empty" << std::endl;
                return nullptr;
            }
            if(getUnprocessedSign()){
                //throw message
                std::cout << "unprocessed Queue is empty" << std::endl;
                return nullptr;
            }

            uint64_t marker = marker_.load(std::memory_order_relaxed);
            uint64_t markerCopy = marker;
            if(nextIndex(UnprocessedStart(marker)) == FreeStart(marker)){
                //set unprocessedSign_ to 1
                setUnprocessedSignMarker(marker, 1);
                
            }
            setUnprocessedStartMarker(marker, nextIndex(UnprocessedStart(marker)));
            T element;
            #ifdef GTEST_FOUND
                std::cout<<"dequeue called "<<std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            #endif
            while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                    
                    element = queue_[UnprocessedStart(markerCopy)].getData();
                    #ifdef GTEST_FOUND
                        std::cout<<"dequeue success at "<<UnprocessedStart(markerCopy)<<std::endl;
                    #endif
                    if(queue_[UnprocessedStart(markerCopy)].isValid()){//this will make it so the function retrives next element if invalid element is found
                        success = true;
                        dequeue_counter_++;
                    }
                    
            }
            if(success)
                return element;
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

    /// @brief fossil collect dummy function
    /// TODO pass a count to jump to
    bool increamentActiveStart(){
        bool success = false;
        //checks first

        while(!success){
            if (isEmpty()){
                //throw message
                std::cout << "Queue is empty" << std::endl;
                return false;
            }
            if(getActiveStart() == getUnprocessedStart()){
                std::cout << "Active Zone is Empty" << std::endl;
                return false;
            }
            uint64_t marker = marker_.load(std::memory_order_relaxed);
            uint64_t markerCopy = marker;
            setFreeSignMarker(marker, 0);
            setActiveStartMarker(marker, nextIndex(ActiveStart(marker)));
            #ifdef GTEST_FOUND
                std::cout<<"increamentActiveStart called "<<std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            #endif
            while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                    #ifdef GTEST_FOUND
                        std::cout<<"increamentActiveStart success at "<<ActiveStart(markerCopy)<<std::endl;
                    #endif
                    success = true;
            }
        }
        return true;
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
        bool success = false;
        //checks first

        while(!success){
            if (isEmpty()){
                //throw message
                std::cout << "Queue is empty" << std::endl;
                return NOTFOUND;
            }
            
            uint64_t marker = marker_.load(std::memory_order_relaxed);
            uint64_t markerCopy = marker;
            
            #ifdef GTEST_FOUND
                std::cout<<"increamentActiveStart called "<<std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            #endif
            while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                    
                    uint16_t ActiveIndex = ActiveStart(markerCopy);
                    uint16_t UnProcessedIndex = UnprocessedStart(markerCopy);
                    uint16_t FreeIndex = FreeStart(markerCopy);
                    #ifdef GTEST_FOUND
                        std::cout<<"find success at "<<ActiveIndex<<
                        " "<<UnProcessedIndex<<" "<<FreeIndex<<std::endl;
                    #endif
                    while(ActiveIndex != UnProcessedIndex){
                        
                        if(queue_[ActiveIndex].getData() == element){
                            found = ACTIVE; //rollback
                            break;
                        }
                        ActiveIndex = nextIndex(ActiveIndex);
                    }
                    while(UnProcessedIndex != FreeIndex && found!=ACTIVE){
                        
                        if(queue_[UnProcessedIndex].getData() == element){
                            found = UNPROCESSED; //Invalidate The element
                            queue_[UnProcessedIndex].invalidate();
                            break;
                        }
                        UnProcessedIndex = nextIndex(UnProcessedIndex);
                    }
                    
                    success = true;
                    break;
            }
        }
        return found;

    }

    void sortQueue(){
        uint64_t unprocessedStart_ = getUnprocessedStart();
        uint64_t freeStart_ = getFreeStart();

        if(unprocessedStart_ < freeStart_){ //no rotation
            std::sort(queue_.begin() + unprocessedStart_, queue_.begin() + freeStart_ - 1, [this](Data a, Data b) { return compare_(a.getData(), b.getData()); });
        }
        else{ //rotation
            // vector<Data> tempQueue ;
            // std::copy(queue_.begin() + unprocessedStart_, queue_.end(), tempQueue.begin());
            // std::copy(queue_.begin(), queue_.begin() + freeStart_, tempQueue.end());
            // std::sort(tempQueue.begin(), tempQueue.end(), sortfunction);
            // //place the sorted queue back
            // std::copy(tempQueue.begin(), tempQueue.begin()+(capacity()-unprocessedStart_), queue_.begin() + unprocessedStartMask_);
            // std::copy(tempQueue.begin()+(capacity()-unprocessedStart_), tempQueue.end(), queue_.begin());
        }
    }


    /// @brief This fixes the position of the events
    /// No Markers change
    bool fixPosition(bool debug = false) {
        rollback_function_counter_++;
        if (getActiveStart() == getUnprocessedStart() && !getFreeSign()) {//active zone is empty
            return false;
        }

        uint64_t marker = marker_.load(std::memory_order_relaxed);
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
            //sort call here
            
            sortQueue();
            return true;
        }
        if(debug)
        std::cout<<"swap index after"<<swap_index_r<<std::endl;
        return false;

        
    }

    


    /// @brief This fixes the position of the events
    /// No Markers change
    bool fixPositionInvalid() {
        // std::cout<<"called invalid fix";
        if (getActiveStart() == getUnprocessedStart() && !getFreeSign()) {//active zone is empty
            return false;
        }

        uint64_t marker = marker_.load(std::memory_order_relaxed);
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
        T element = nullptr;
        
        //this is called after a dequeue so we need it to go before it
        uint16_t index=prevIndex(getUnprocessedStart());
        // element=queue_[index].getData();
        do{
                element=queue_[prevIndex(index)].getData();
            index=prevIndex(index);
        }while(!queue_[prevIndex(index)].isValid() && getActiveStart()!=index); // this can be Infinite if all elements are invalid
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
        bool success = false;
        //checks first

        while(!success){
            if (isEmpty()){
                //throw message
                std::cout << "Queue is empty" << std::endl;
                return NOTFOUND;
            }
            
            uint64_t marker = marker_.load(std::memory_order_relaxed);
            uint64_t markerCopy = marker;
            
            #ifdef GTEST_FOUND
                std::cout<<"increamentActiveStart called "<<std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            #endif
            while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                    
                    uint16_t ActiveIndex = ActiveStart(markerCopy);
                    uint16_t UnProcessedIndex = UnprocessedStart(markerCopy);
                    uint16_t FreeIndex = FreeStart(markerCopy);
                    #ifdef GTEST_FOUND
                        std::cout<<"find success at "<<ActiveIndex<<
                        " "<<UnProcessedIndex<<" "<<FreeIndex<<std::endl;
                    #endif
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
                    
                    success = true;
                    break;
            }
        }
        return found;

    }

    //invalids the data at unprocessedStart
    bool invalidNegative(){
        uint64_t marker = marker_.load(std::memory_order_relaxed);
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
