#ifndef WARPED_EVENT_HPP
#define WARPED_EVENT_HPP

#include <string>
#include "serialization.hpp"
#include <tuple>
#include <memory>

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
    Event() = default;
    virtual ~Event() {}

    bool operator== (const Event &other) {
        return ((this->timestamp() == other.timestamp())
                && (this->send_time_ == other.send_time_)
                && (this->sender_name_ == other.sender_name_));
    }

    bool operator< (const Event &other) const {
        return  (this->timestamp() < other.timestamp()) ? true :
                ((this->timestamp() != other.timestamp()) ? false :
                ((this->send_time_ < other.send_time_) ? true :
                ((this->send_time_ != other.send_time_) ? false :
                    ((this->sender_name_ < other.sender_name_) ? true :
                    ((this->sender_name_ != other.sender_name_) ? false : false)))));
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
                            sizeof(send_time_);
        return size;
    }

    // The name of the SimualtionObject that sends this event.
    std::string sender_name_;

    // Event type - positive or negative
    EventType event_type_ = EventType::POSITIVE;

    // Send time
    unsigned int send_time_ = 0;

    // For differentiating same events which is caused by
    //  anti-message + regeneration of event.
    // unsigned long long generation_ = 0;

    // WARPED_REGISTER_SERIALIZABLE_MEMBERS(sender_name_, event_type_, send_time_, generation_)
    WARPED_REGISTER_SERIALIZABLE_MEMBERS(sender_name_, event_type_, send_time_)

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
        // generation_ = e->generation_;
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
        // generation_ = 0;
   }

    const std::string& receiverName() const { return receiver_name_; }
    unsigned int timestamp() const { return 0; }
    unsigned int size() const { return 0; } // receiver_name is a null string

    std::string receiver_name_ = "";
};

/* Compares two events to see if one has a receive time less than to the other */
// struct compareEvents {
// public:
//     bool operator() (const std::shared_ptr<Event>& first,
//                      const std::shared_ptr<Event>& second) const {
//         return std::make_tuple(first->timestamp(), first->send_time_, first->sender_name_, first->generation_, first->event_type_) <
//                std::make_tuple(second->timestamp(), second->send_time_, second->sender_name_, second->generation_, second->event_type_);
//     }
// };


// struct compareEvents {
// public:
//     bool operator() (const std::shared_ptr<Event>& first,
//                      const std::shared_ptr<Event>& second) const {
//         // Compare the most significant field first
//         if (first->timestamp() != second->timestamp())
//             return first->timestamp() < second->timestamp();

//         // Use std::tie for the remaining fields
//         return std::tie(first->send_time_, first->sender_name_, first->generation_, first->event_type_) <
//                std::tie(second->send_time_, second->sender_name_, second->generation_, second->event_type_);
//     }
// };


struct compareEvents {
public:
    bool operator() (const std::shared_ptr<Event>& first,
                     const std::shared_ptr<Event>& second) const {
        // Compare timestamp first
        if (first->timestamp() != second->timestamp())
            return first->timestamp() < second->timestamp();

        // Compare send_time_
        if (first->send_time_ != second->send_time_)
            return first->send_time_ < second->send_time_;

        // Compare sender_name_
        if (first->sender_name_ != second->sender_name_)
            return first->sender_name_ < second->sender_name_;

        // Compare generation_
        // if (first->generation_ != second->generation_)
        //     return first->generation_ < second->generation_;

        // Compare event_type_
        return first->event_type_ < second->event_type_;
    }
};

} // namespace warped

#endif
