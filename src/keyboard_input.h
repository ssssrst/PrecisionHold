#pragma once
#include <optional>
#include "hotkey_matcher.h"
namespace ph {
inline std::optional<Key> normalizeKey(std::uint16_t vk, std::uint16_t scan, std::uint16_t flags) {
    if (!vk || vk>=0xff || scan==0xff || (flags&4) || vk==0x13) return std::nullopt;
    if ((flags&2) && (vk==0x10 || vk==leftShift || vk==rightShift)) return std::nullopt; // PrintScreen's fake E0 Shift.
    if (vk==0x10) { if (scan==0x2a) return leftShift; if (scan==0x36) return rightShift; return std::nullopt; }
    if (vk==0x11) return (flags&2) ? rightCtrl : leftCtrl;
    if (vk==0x12) return (flags&2) ? rightAlt : leftAlt;
    if (vk==0x0d && (flags&2)) return numpadEnter;
    if (!(flags&2)) {
        switch(scan) {
        case 0x47:return Key{0x67};case 0x48:return Key{0x68};case 0x49:return Key{0x69};
        case 0x4b:return Key{0x64};case 0x4c:return Key{0x65};case 0x4d:return Key{0x66};
        case 0x4f:return Key{0x61};case 0x50:return Key{0x62};case 0x51:return Key{0x63};
        case 0x52:return Key{0x60};case 0x53:return Key{0x6e};default:break;
        }
    }
    if (!validKey(vk)) return std::nullopt;
    return vk;
}
}
