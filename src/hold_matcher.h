#pragma once
#include <cstdint>
#include <set>

namespace ph {
enum class HoldEdge { None, Pressed, Released };
// Stage A: events have already been filtered to F8 by the Raw Input adapter.
// Track each keyboard independently so one device cannot release another.
class HoldMatcher {
public:
    using Device = std::uintptr_t;
    HoldEdge event(Device device, bool down, bool enabled = true) {
        if (!enabled || !quarantined_.empty()) {
            if (down) quarantined_.insert(device);
            else quarantined_.erase(device);
            return HoldEdge::None;
        }
        const bool before = !held_.empty();
        if (down) held_.insert(device);
        else held_.erase(device);
        const bool after = !held_.empty();
        if (!before && after) return HoldEdge::Pressed;
        if (before && !after) return HoldEdge::Released;
        return HoldEdge::None;
    }
    void interrupt() {
        quarantined_.merge(held_);
        held_.clear();
    }
    void remove(Device device) {
        held_.erase(device);
        quarantined_.erase(device);
        interrupt();
    }
private:
    std::set<Device> held_, quarantined_;
};
}
