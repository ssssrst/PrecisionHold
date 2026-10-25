#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <map>
#include <set>
#include <vector>
#include "hold_matcher.h"

namespace ph {
using Key = std::uint16_t;
using Device = std::uintptr_t;
using Hotkey = std::vector<Key>;
constexpr Key leftShift=0xa0, rightShift=0xa1, leftCtrl=0xa2, rightCtrl=0xa3, leftAlt=0xa4, rightAlt=0xa5, numpadEnter=0x10d;
inline bool validKey(Key key) { return (key >= 0x08 && key < 0xff && key != 0x10 && key != 0x11 && key != 0x12 && key != 0x13) || key == numpadEnter; }
inline bool validHotkey(const Hotkey& keys) {
    if (keys.empty() || keys.size() > 4) return false;
    for (std::size_t i=0; i<keys.size(); ++i) {
        if (!validKey(keys[i]) || std::find(keys.begin(), keys.begin()+static_cast<std::ptrdiff_t>(i), keys[i]) != keys.begin()+static_cast<std::ptrdiff_t>(i)) return false;
    }
    const auto has = [&](Key k) { return std::find(keys.begin(), keys.end(), k) != keys.end(); };
    return !((has(0x2e) || has(0x6e)) && (has(leftCtrl) || has(rightCtrl)) && (has(leftAlt) || has(rightAlt)));
}
class HotkeyMatcher {
public:
    bool setHotkey(Hotkey keys) {
        if (!validHotkey(keys)) return false;
        std::sort(keys.begin(), keys.end());
        target_ = std::move(keys); interrupt(); return true;
    }
    HoldEdge event(Device device, Key key, bool down, bool enabled=true) {
        if (!validKey(key)) return HoldEdge::None;
        if (down) held_[device].insert(key);
        else if (const auto it=held_.find(device); it!=held_.end()) {
            it->second.erase(key); if (it->second.empty()) held_.erase(it);
        }
        if (!enabled) { matched_=false; armed_=!targetHeld(); return HoldEdge::None; }
        if (!armed_) { if (!targetHeld()) armed_=true; return HoldEdge::None; }
        const bool matched=std::all_of(target_.begin(), target_.end(), [&](Key k) {
            return std::any_of(held_.begin(), held_.end(), [&](const auto& pair) { return pair.second.contains(k); });
        });
        const bool before=matched_; matched_=matched;
        if (!before && matched) return HoldEdge::Pressed;
        if (before && !matched) return HoldEdge::Released;
        return HoldEdge::None;
    }
    void interrupt(bool preserveUnrelated=false) noexcept {
        // A desktop transition can lose unrelated KeyUp events. They must not
        // permanently prevent a new shortcut, including after reconfiguration.
        for (auto device=held_.begin();!preserveUnrelated && device!=held_.end();) {
            std::erase_if(device->second,[&](Key key){return std::find(target_.begin(),target_.end(),key)==target_.end();});
            if(device->second.empty())device=held_.erase(device);else ++device;
        }
        matched_=false; armed_=!targetHeld();
    }
    void remove(Device device) { held_.erase(device); interrupt(); }
    bool released() const { return held_.empty(); }
    bool waitingForRelease() const noexcept { return !armed_; }
private:
    bool targetHeld() const noexcept {
        return std::any_of(target_.begin(),target_.end(),[&](Key key){
            return std::any_of(held_.begin(),held_.end(),[&](const auto& device){return device.second.contains(key);});
        });
    }
    Hotkey target_{0x14};
    std::map<Device,std::set<Key>> held_;
    bool armed_{true}, matched_{};
};
}
