#include "pointer_speed.h"
#include <windows.h>

namespace ph {
namespace { constexpr auto keyPath = L"Software\\PrecisionHold\\Recovery"; }
bool WinBackend::get(int& speed) {
    return SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0) != FALSE && speed >= 1 && speed <= 20;
}
bool WinBackend::set(int speed) {
    if (speed < 1 || speed > 20) return false;
    return SystemParametersInfoW(SPI_SETMOUSESPEED, 0, reinterpret_cast<void*>(static_cast<INT_PTR>(speed)), 0) != FALSE;
}
bool WinBackend::save(const Recovery& record) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const auto result = RegSetValueExW(key, L"Record", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&record), sizeof record);
    const auto closed = RegCloseKey(key);
    return result == ERROR_SUCCESS && closed == ERROR_SUCCESS;
}
bool WinBackend::clear() {
    const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, keyPath, L"Record");
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
}
LoadResult WinBackend::load(Recovery& record) {
    DWORD bytes = sizeof record, type{};
    const auto result = RegGetValueW(HKEY_CURRENT_USER, keyPath, L"Record", RRF_RT_REG_BINARY, &type, &record, &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return LoadResult::Missing;
    if (result != ERROR_SUCCESS || bytes != sizeof record || type != REG_BINARY || !record.valid()) return LoadResult::Error;
    return LoadResult::Found;
}
bool SpeedControl::recoverStartup() {
    Recovery record;
    const auto result = backend_.load(record);
    if (result == LoadResult::Missing) return true;
    if (result == LoadResult::Error || !record.valid()) { blocked_ = true; return false; }
    record_ = record;
    return restore();
}
bool SpeedControl::activate(int target) {
    if (blocked_ || record_ || target < 1 || target > 20) return false;
    int baseline{};
    if (!backend_.get(baseline)) return false;
    if (target == baseline) return true;
    Recovery record;
    record.original = static_cast<std::uint32_t>(baseline);
    record.applied = static_cast<std::uint32_t>(target);
    record.seal();
    if (!backend_.save(record)) return false;
    if (!backend_.set(target)) {
        // Retain recovery information if marker cleanup itself fails.
        if (!backend_.clear()) { record_ = record; blocked_ = true; }
        return false;
    }
    record_ = record;
    applied_ = true;
    return true;
}
bool SpeedControl::restore() {
    if (!record_) return !blocked_;
    int current{};
    if (!backend_.get(current)) { blocked_ = true; return false; }
    if (current == static_cast<int>(record_->applied) && !backend_.set(static_cast<int>(record_->original))) {
        blocked_ = true;
        return false;
    }
    applied_ = false;
    if (!backend_.clear()) { blocked_ = true; return false; }
    record_.reset();
    blocked_ = false;
    return true;
}
}
