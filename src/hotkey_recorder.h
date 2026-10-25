#pragma once
#include "hotkey_matcher.h"
namespace ph {
enum class RecordResult { Pending, Complete, Invalid, Cancelled };
class HotkeyRecorder {
public:
    void reset() { held_.clear(); candidate_.clear(); released_=invalid_=false; }
    RecordResult event(Device device, Key key, bool down) {
        if (down && key==0x1b) { reset(); return RecordResult::Cancelled; }
        if (down) {
            const bool fresh=!held_[device].contains(key);
            if (fresh && released_) invalid_=true;
            held_[device].insert(key);
            candidate_.insert(key);
            if (candidate_.size()>4 || !validKey(key)) invalid_=true;
        } else {
            const auto it=held_.find(device);
            if (it==held_.end() || !it->second.erase(key)) return RecordResult::Pending;
            released_=true;
            if (it->second.empty()) held_.erase(it);
            if (held_.empty()) return !invalid_ && validHotkey(keys()) ? RecordResult::Complete : RecordResult::Invalid;
        }
        return RecordResult::Pending;
    }
    Hotkey keys() const { return {candidate_.begin(), candidate_.end()}; }
private:
    std::map<Device,std::set<Key>> held_;
    std::set<Key> candidate_;
    bool released_{},invalid_{};
};
}
