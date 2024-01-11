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
template <typename T, typename comparator>
class UnifiedQueue
{
private:
    std::vector<T> queue_;
    // 10 bits activeStart_, 1bit unprocessedSign, 10 bits unprocessedStart_, 1 bit freeSign, 10 bits freeStart_
    std::atomic<uint32_t> marker_; //test with this datatype
    comparator compare_; //currently not  used, as we are not sorting anymore
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
        return (marker_.load(std::memory_order_relaxed) & 0x80000000) >> 31;
    }

    bool getFreeSign(){
        return (marker_.load(std::memory_order_relaxed) & 0x40000000) >> 30;
    }

    //getActiveStart
    uint32_t getActiveStart(){
        return (marker_.load(std::memory_order_relaxed) & 0x3FFC0000) >> 18;
    }

    //getUnprocessedStart
    uint32_t getUnprocessedStart(){
        return (marker_.load(std::memory_order_relaxed) & 0x0003FFC0) >> 6;
    }

    //getFreeStart
    uint32_t getFreeStart(){
        return (marker_.load(std::memory_order_relaxed) & 0x0000003F);
    }

    //setfreeSign
    void setFreeSign(bool sign){
        if(sign){
            marker_ |= 0x40000000;
        }else{
            marker_ &= 0xBFFFFFFF;
        }
    }

    //setunprocessedSign
    void setUnprocessedSign(bool sign){
        if(sign){
            marker_ |= 0x80000000;
        }else{
            marker_ &= 0x7FFFFFFF;
        }
    }

    //setActiveStart
    void setActiveStart(uint32_t start){
        marker_ &= 0xC003FFFF;
        marker_ |= (start << 18);
    }

    //setUnprocessedStart
    void setUnprocessedStart(uint32_t start){
        marker_ &= 0xFFFFC03F;
        marker_ |= (start << 6);
    }

    //setFreeStart
    void setFreeStart(uint32_t start){
        marker_ &= 0xFFFFFFC0;
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
        std::cout << "activeStart: " << getActiveStart();
        std::cout << " unprocessedSign: " << getUnprocessedSign();
        std::cout << " unprocessedStart: " << getUnprocessedStart();
        std::cout << " freeSign: " << getFreeSign();
        std::cout << " freeStart: " << getFreeStart();
        std::cout << " size: " << size() << std::endl;

        int i = getUnprocessedStart();
        if(!getUnprocessedSign()){
        do {
            std::cout << queue_[i].receiveTime_ << " ";
            i = nextIndex(i);
        } while (i != getFreeStart());
        std::cout << std::endl;
        }
        else{
            std::cout<<"Unprocessed Events Empty"<<std::endl;
        }
        for (auto itr : queue_) {
            std::cout << itr.receiveTime_ << " ";
        }
        std::cout << std::endl;
         
    }

    //getValues from Marker
    
    uint32_t ActiveStart(uint32_t marker){
        return (marker & 0x3FFC0000) >> 18;
    }

    bool UnProcessedSign(uint32_t marker){
        return (marker & 0x80000000) >> 31;
    }

    bool FreeSign(uint32_t marker){
        return (marker & 0x40000000) >> 30;
    }

    uint32_t UnprocessedStart(uint32_t marker){
        return (marker & 0x0003FFC0) >> 6;
    }

    uint32_t FreeStart(uint32_t marker){
        return (marker & 0x0000003F);
    }

    //setValues for Marker

    //setFreeStart
    void setFreeStartMarker(uint32_t &marker, uint32_t start){
        marker &= 0xFFFFFFC0;
        marker |= start;
    }

    //setfreeSign
    void setFreeSignMarker(uint32_t &marker, bool sign){
        if(sign){
            marker |= 0x40000000;
        }else{
            marker &= 0xBFFFFFFF;
        }
    }

    //setunprocessedSign
    void setUnprocessedSignMarker(uint32_t &marker, bool sign){
        if(sign){
            marker |= 0x80000000;
        }else{
            marker &= 0x7FFFFFFF;
        }
    }

    //setActiveStart
    void setActiveStartMarker(uint32_t &marker, uint32_t start){
        marker &= 0xC003FFFF;
        marker |= (start << 18);
    }

    //setUnprocessedStart
    void setUnprocessedStartMarker(uint32_t &marker, uint32_t start){
        marker &= 0xFFFFC03F;
        marker |= (start << 6);
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

            if (this->compare_(queue_[mid], element)) {
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
            if (compare_(element, queue_[capacity() - 1])) {
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
        bool success = false;
        while(!success){
            //checks first
            if (isFull()){
                //throw message
                std::cout << "Queue is full" << std::endl;
                // std::__throw_bad_exception();
                return false;
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
                std::cout<<"called "<<element.receiveTime_<<std::endl;
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
                    std::cout<<"Inserted "<<element.receiveTime_<<" at "<<FreeStart(markerCopy)<<std::endl;  
                                
                #endif
                queue_[FreeStart(markerCopy)] = element;
                success = true;
            }
        }
           
        

        return true;
    }


    //this is done
    //change to use compare and swap
    T dequeue(){
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
                    element = queue_[UnprocessedStart(markerCopy)];
                    #ifdef GTEST_FOUND
                        std::cout<<"dequeue success at "<<UnprocessedStart(markerCopy)<<std::endl;
                    #endif
                    success = true;
            }
            if(success)
                return element;
            
        }
        return T();
        
    }

    //this is done 
    //change to use compare and swap
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

};
