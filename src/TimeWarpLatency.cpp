#include "TimeWarpLatency.hpp"
#include "Latency.hpp"

namespace warped{

template <unsigned I>
void TimeWarpedLatency::formatLatency(latency_stats_index<I> j, const char* title){
    auto latency=latency_stats_[j].estimate();
    // 99, 90 and 50
    std::cout<<title<<"\n"
             <<"p99: "<<latency.p99<<"\n"
             <<"p90: "<<latency.p90<<"\n"
             <<"p50: "<<latency.p50<<"\n";
}
void TimeWarpedLatency::printLatencyStats(){
    std::cout<<"Latency Stats\n";
    formatLatency(PROCESS_EVENT_LATENCY,"ProcessEvent");
    formatLatency(COMPARE_EVENT_LATENCY, "CompareEvent");
    std::cout<<"\n";
}

}