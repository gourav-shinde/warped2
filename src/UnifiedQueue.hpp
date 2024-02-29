#include <iostream>
#include <atomic>
#include <vector>
#include <cmath>
#include <stdexcept>


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
    std::atomic<uint32_t> enqueue_counter_ = 0;
    std::atomic<uint32_t> dequeue_counter_ = 0;
    std::atomic<uint32_t> rollback_counter_ = 0;
    std::atomic<uint32_t> rollback_function_counter_ = 0;
    const uint32_t freeStartMask_ = 0x000003FF;
    const uint32_t freeSignMask_ = 0x00000400;
    const uint32_t unprocessedStartMask_ = 0x001FF800;
    const uint32_t unprocessedSignMask_ = 0x00200000;
    const uint32_t activeStartMask_ = 0xFFC00000;
    const uint32_t unprocessedStartShift_ = 11;
    const uint32_t activeStartShift_ = 22;

    std::atomic<uint32_t> marker_; //test with this datatype
    comparator compare_; //currently not  used, as we are not sorting anymore
    negativeCounterPart negativeCounterPart_; //used to find negative counterpart
    
public:
    UnifiedQueue(uint16_t capacity=1024){
        if(capacity > 1024){
            throw std::invalid_argument("Capacity should be less than 1024");
        }
        queue_.resize(capacity); 
        //init condition is 0,0,0
        marker_.store(0, std::memory_order_relaxed);
    }

    bool getUnprocessedSign(){
        return (marker_.load(std::memory_order_relaxed) & unprocessedSignMask_) ? true : false;
    }

    bool getFreeSign(){
        return (marker_.load(std::memory_order_relaxed) &  freeSignMask_) ? true : false;
    }

    //getActiveStart
    uint32_t getActiveStart(){
        return (marker_.load(std::memory_order_relaxed) & activeStartMask_) >> activeStartShift_;
    }

    //getUnprocessedStart
    uint32_t getUnprocessedStart(){
        return (marker_.load(std::memory_order_relaxed) & unprocessedStartMask_) >> unprocessedStartShift_;
    }

    //getFreeStart
    uint32_t getFreeStart(){
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
    void setActiveStart(uint32_t start){
        marker_ &= ~activeStartMask_;
        marker_ |= (start << activeStartShift_);
    }

    //setUnprocessedStart
    void setUnprocessedStart(uint32_t start){
        marker_ &= ~unprocessedStartMask_;
        marker_ |= (start << unprocessedStartShift_);
    }

    //setFreeStart
    void setFreeStart(uint32_t start){
        marker_ &= ~freeStartMask_;
        marker_ |= start;
    }

    //getCapacity
    uint32_t capacity(){
        return queue_.size();
    }

    //preIndex
    uint32_t prevIndex(uint32_t index){
        return (index + capacity() - 1) % capacity();
    }

    //nextIndex
    uint32_t nextIndex(uint32_t index){
        return (index + 1) % capacity();
    }

    bool isEmpty(){
        uint32_t marker = marker_.load(std::memory_order_relaxed);
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

    void debug(){
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
         
    }

    //getValues from Marker
    
    uint32_t ActiveStart(uint32_t marker){
        uint32_t activeStart = (marker & activeStartMask_) >> activeStartShift_;
        return activeStart;
    }

    bool UnProcessedSign(uint32_t marker){
        return (marker & unprocessedSignMask_) ? true : false;
    }

    bool FreeSign(uint32_t marker){
        return (marker & freeSignMask_) ? true : false;
    }

    uint32_t UnprocessedStart(uint32_t marker){
        return (marker & unprocessedStartMask_) >> unprocessedStartShift_;
    }

    uint32_t FreeStart(uint32_t marker){
        return (marker & freeStartMask_);
    }

    //setValues for Marker

    //setFreeStart
    void setFreeStartMarker(uint32_t &marker, uint32_t start){
        marker &= ~freeStartMask_;
        marker |= start;
    }

    //setfreeSign
    void setFreeSignMarker(uint32_t &marker, bool sign){
        if(sign){
            marker |= freeSignMask_;
        }else{
            marker &= ~freeSignMask_;
        }
    }

    //setunprocessedSign
    void setUnprocessedSignMarker(uint32_t &marker, bool sign){
        if(sign){
            marker |= unprocessedSignMask_;
        }else{
            marker &= ~unprocessedSignMask_;
        }
    }

    //setActiveStart
    void setActiveStartMarker(uint32_t &marker, uint32_t start){
        marker &= ~activeStartMask_;
        marker |= (start << activeStartShift_);
    }

    //setUnprocessedStart
    void setUnprocessedStartMarker(uint32_t &marker, uint32_t start){
        marker &= ~unprocessedStartMask_;
        marker |= (start << unprocessedStartShift_);
    }

    // main functions

    //no checks for valid indexes is made here as it should be done by the caller

    uint32_t binarySearch(T element, uint32_t low, uint32_t high){
        uint32_t mid;

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

    //no checks for valid indexes is made here as it should be done by the caller
    //low: activeStart_
    //high: freeStart_

    uint32_t findInsertPosition(T element, uint32_t low, uint32_t high){
        
        //This will never trigger, as this condition is checked in the parent function
        // if (isEmpty())
        //     return freeStart_.load(std::memory_order_relaxed);

        // when there is no rotation in queue
        if (low < high) {
            return binarySearch(element, low, high);
        }
        // rotation i.e fossileStart_ < activeStart_
        else {
            if (compare_(element, queue_[capacity() - 1].getData())) {
                return binarySearch(element, low, capacity() - 1);
            }
            else {
                return binarySearch(element, 0, high);
            }
        }
    }


    //shift elements from start to end by 1 position to the right
    //no checks for valid indexes is made here as it should be done by the caller
    //Discuss this as it will have ABA problem across threads
    void shiftElements(uint32_t start, uint32_t end) {
        uint32_t i = end;
        while (i != start) {
            queue_[i] = queue_[prevIndex(i)];
            i = prevIndex(i);
        }
    }

    // we are not handling out of order elements in this queue.
    bool enqueue(T element){
        enqueue_counter_++;
        bool success = false;
        while(!success){
            //checks first
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

            uint32_t marker = marker_.load(std::memory_order_relaxed);
            uint32_t markerCopy = marker;
            if(nextIndex(FreeStart(marker)) == ActiveStart(marker)){//queue will become full after this insert
                //set freeSign_ to 1
                setFreeSignMarker(marker,1);
            }
            setUnprocessedSignMarker(marker, 0);
            setFreeStartMarker(marker, nextIndex(FreeStart(marker)));
            
            //run follwing code when running gtest, this adds a delay so threads collide making 
            //compare and swap fail
            #ifdef GTEST_FOUND
                // std::cout<<"called "<<element.receiveTime_<<std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            #endif
            
            //make sure marker doesnt change for doing 1+1 operation using compare and swap
        
            while (marker_.compare_exchange_weak(
                    markerCopy, marker,
                    std::memory_order_release, std::memory_order_relaxed)){
                //FOR OUT OF ORDER LOGIC
                // if(!FreeSign(marker) && ActiveStart(marker) == FreeStart(marker)){//queue is empty
                //     queue_[ActiveStart(markerCopy)] = element;
                // }
                // else{
                //     int insertPos = findInsertPosition(element, ActiveStart(markerCopy), FreeStart(markerCopy));
                //     shiftElements(insertPos, FreeStart(markerCopy));
                //     queue_[insertPos] = element;
                // }
                #ifdef GTEST_FOUND
                   // std::cout<<"Inserted "<<element.receiveTime_<<" at "<<FreeStart(markerCopy)<<std::endl;  
                                
                #endif
                queue_[FreeStart(markerCopy)] = Data(element);
                success = true;
            }
        }
           
        
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return true;
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
                return T();
            }
            if(getUnprocessedSign()){
                //throw message
                std::cout << "unprocessed Queue is empty" << std::endl;
                return T();
            }

            uint32_t marker = marker_.load(std::memory_order_relaxed);
            uint32_t markerCopy = marker;
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
            
        }
        return T();
        
    }

    T getValue(uint32_t index){
        return queue_[index].getData();
    }

    bool isDataValid(uint32_t index){
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
            uint32_t marker = marker_.load(std::memory_order_relaxed);
            uint32_t markerCopy = marker;
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
            
            uint32_t marker = marker_.load(std::memory_order_relaxed);
            uint32_t markerCopy = marker;
            
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


    /// @brief This fixes the position of the events
    /// No Markers change
    void fixPosition() {
        rollback_function_counter_++;
        if (getActiveStart() == getUnprocessedStart()) {//active zone is empty
            return;
        }

        uint32_t marker = marker_.load(std::memory_order_relaxed);
        uint32_t activeStart = ActiveStart(marker);

#ifdef GTEST_FOUND
        std::cout << "Fixposition called " << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif
        //find index to swap with

        //get previous valid event from unprocessed start
        uint16_t swap_index_r = prevIndex(UnprocessedStart(marker));

        while (swap_index_r != activeStart && compare_(queue_[swap_index_r].getData(), queue_[prevIndex(swap_index_r)].getData())) {
            std::swap(queue_[prevIndex(swap_index_r)], queue_[swap_index_r]);
            
            swap_index_r = prevIndex(swap_index_r);
            rollback_counter_++;
            setUnprocessedSign(false);
            setUnprocessedStart(swap_index_r);
        }

        
    }


    /// @brief returns previous valid unproceesed event
    /// @return 
    T getPreviousUnprocessedEvent(){
        T element;
        if(getUnprocessedSign())
            return element;
        else{
            //this is called after a dequeue so we need it to go before it
            uint16_t index=prevIndex(getUnprocessedStart());
            do{
                    element=queue_[prevIndex(index)].getData();
                index=prevIndex(index);
            }while(!queue_[prevIndex(index)].isValid() && prevIndex(getActiveStart())!=prevIndex(index)); // this can be Infinite if all elements are invalid
            return element;
        }
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
            
            uint32_t marker = marker_.load(std::memory_order_relaxed);
            uint32_t markerCopy = marker;
            
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
        uint32_t marker = marker_.load(std::memory_order_relaxed);
        if(!UnProcessedSign(marker)){
            queue_[UnprocessedStart(marker)].invalidate();
            return true;
        }
        std::cout << "unprocessed Queue is empty" << std::endl;
        return false;
    }

    //invalids the data at index
    void invalidateIndex(uint32_t index){
        queue_[index].invalidate();
    }


};