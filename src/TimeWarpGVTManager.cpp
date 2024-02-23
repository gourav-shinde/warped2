#include "TimeWarpGVTManager.hpp"
#include "utility/memory.hpp"           // for make_unique

namespace warped {

void TimeWarpGVTManager::initialize() {
    gvt_state_ = GVTState::IDLE;
    gvt_start = std::chrono::steady_clock::now();
    gvt_stop = std::chrono::steady_clock::now();
    //2 states behind
    gvTValues_.emplace_back(0);
    gvTValues_.emplace_back(0);
}

void TimeWarpGVTManager::checkProgressGVT() {

    if ((gvt_state_ == GVTState::IDLE) && readyToStart()) {
        gvt_state_ = GVTState::LOCAL;
        gvt_start = std::chrono::steady_clock::now();
    }

    progressGVT();
}

} // namespace warped
