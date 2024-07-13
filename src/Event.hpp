#ifndef WARPED_EVENT_HPP
#define WARPED_EVENT_HPP

#include <string>
#include "serialization.hpp"

#include <immintrin.h> // AVX2 intrinsics
#include <array>

namespace warped {

struct compareEvents;

enum class EventType : bool {
    NEGATIVE = 0,
    POSITIVE
};

// Events are passed between objects. They may contain data, and must be
// serializable. See serialization.hpp for info on serializing Events.
class Event {
public:
    Event() {

    };
    virtual ~Event() {}

    bool operator== (const Event &other) {
        return ((this->timestamp() == other.timestamp())
                && (this->send_time_ == other.send_time_)
                && (this->sender_name_ == other.sender_name_)
                && (this->generation_ == other.generation_));
    }

    bool operator< (const Event &other) {
        return  (this->timestamp() < other.timestamp()) ? true :
                ((this->timestamp() != other.timestamp()) ? false :
                  ((this->send_time_ < other.send_time_) ? true :
                  ((this->send_time_ != other.send_time_) ? false :
                    ((this->sender_name_ < other.sender_name_) ? true :
                    ((this->sender_name_ != other.sender_name_) ? false :
                      ((this->generation_ < other.generation_) ? true :
                      ((this->generation_ != other.generation_) ? false : false)))))));
    }

    bool operator<= (const Event &other) {
        return (*this < other) || (*this == other);
    }

    bool operator>= (const Event &other) {
        return !(*this < other);
    }

    bool operator> (const Event &other) {
        return !(*this <= other);
    }

    // The name of the SimualtionObject that should receive this event.
    virtual const std::string& receiverName() const = 0;

    // The timestamp of when the event should be received.
    virtual unsigned int timestamp() const = 0;

    // Size of the model-specific event parameters
    virtual unsigned int size() const = 0;

    // Size of the base event parameters
    unsigned int base_size() {
        unsigned int size = sender_name_.length() +
                            sizeof(event_type_) +
                            sizeof(send_time_) +
                            sizeof(generation_);
        return size;
    }
    void generateHash(){
        senderHashId_ = std::hash<std::string>{}(sender_name_);
        //assign data into array
        data_[0] = timestamp();
        data_[1] = send_time_;
        data_[2] = senderHashId_;
        data_[3] = generation_;
    }

    // The name of the SimualtionObject that sends this event.
    std::string sender_name_;

    // Event type - positive or negative
    EventType event_type_ = EventType::POSITIVE;

    // Send time
    unsigned int send_time_ = 0;

    std::uint64_t senderHashId_{0};
    // For differentiating same events which is caused by
    //  anti-message + regeneration of event.
    unsigned long long generation_ = 0;

    WARPED_REGISTER_SERIALIZABLE_MEMBERS(sender_name_, event_type_, send_time_, generation_)
    static constexpr size_t EVENT_DATA_SIZE = 4;
    std::array<uint64_t, EVENT_DATA_SIZE> data_;
    //receive time/send_time/sender_name/event_type/generation

};

class NegativeEvent : public Event {
public:
    NegativeEvent() = default;
    NegativeEvent(std::shared_ptr<Event> e) {
        receiver_name_ = e->receiverName();
        receive_time_ = e->timestamp();
        sender_name_ = e->sender_name_;
        send_time_ = e->send_time_;
        event_type_ = EventType::NEGATIVE;
        generation_ = e->generation_;
        senderHashId_ = e->senderHashId_;
        //assign data into array
        data_[0] = receive_time_;
        data_[1] = send_time_;
        data_[2] = senderHashId_;
        data_[3] = generation_;
        

    }

    const std::string& receiverName() const {return receiver_name_;}
    unsigned int timestamp() const {return receive_time_;}

    unsigned int size() const {
        return receiver_name_.length() + sizeof(receive_time_);
    }

    std::string receiver_name_;
    unsigned int receive_time_;

    WARPED_REGISTER_SERIALIZABLE_MEMBERS(cereal::base_class<Event>(this), receiver_name_, receive_time_)
};

// Initial event used with the initial state save of all objects
class InitialEvent : public Event {
public:
    InitialEvent() {
        sender_name_ = "";
        send_time_ = 0;
        generation_ = 0;
   }

    const std::string& receiverName() const { return receiver_name_; }
    unsigned int timestamp() const { return 0; }
    unsigned int size() const { return 0; } // receiver_name is a null string

    std::string receiver_name_ = "";
};


struct compareEvents {
public:
    bool operator() (const std::shared_ptr<Event>& first,
                     const std::shared_ptr<Event>& second) const {
        // Create arrays of data to compare
        // Ensure proper alignment
        alignas(32) std::array<uint64_t, 4> a = first->data_;
        alignas(32) std::array<uint64_t, 4> b = second->data_;


        __m256i va = _mm256_load_si256(reinterpret_cast<const __m256i*>(a.data()));
        __m256i vb = _mm256_load_si256(reinterpret_cast<const __m256i*>(b.data()));

        // Compare the vectors
        __m256i cmp_lt = _mm256_cmpgt_epi64(vb, va);
        __m256i cmp_eq = _mm256_cmpeq_epi64(va, vb);

        // Get the comparison results as a mask
        int lt_mask = _mm256_movemask_pd(_mm256_castsi256_pd(cmp_lt));
        int eq_mask = _mm256_movemask_pd(_mm256_castsi256_pd(cmp_eq));

        // If any element in 'a' is less than 'b', return true
        if (lt_mask != 0) {
            return true;
        }

        // If all elements are equal, compare sender_name
        if (eq_mask == 0xF) {
            return first->event_type_ < second->event_type_;
        }

        // If we get here, 'a' is not less than 'b'
        return false;
    }
};

} // namespace warped

#endif