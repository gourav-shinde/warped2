#include "Latency.hpp"
#include <tuple>
namespace warped{

template <unsigned I>
struct latency_stats_index{
    latency_stats_index(){}
    static unsigned const value = I;
};

struct LatencyStats {

    std::tuple<
        util::PercentileStats,          //ProcessEvent
        util::PercentileStats          //CompareEvent
    > latency_stats_;   

    template<unsigned I>
    auto operator[](latency_stats_index<I>) -> decltype(std::get<I>(latency_stats_)) {
        return std::get<I>(latency_stats_);
    }
};

const latency_stats_index<0>  PROCESS_EVENT_LATENCY;
const latency_stats_index<1>  COMPARE_EVENT_LATENCY;

class TimeWarpedLatency{
public:
    template <unsigned I>
    void formatLatency(latency_stats_index<I> j, const char *title);

    LatencyStats latency_stats_;

    void printLatencyStats();

    static TimeWarpedLatency& getInstance(){
        static TimeWarpedLatency instance;
        return instance;
    }
    
private:
    TimeWarpedLatency(){}

public:
    TimeWarpedLatency(const TimeWarpedLatency& obj)=delete;

};

}