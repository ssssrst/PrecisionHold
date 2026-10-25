#pragma once
#include <windows.h>
#include <string>
#include "config.h"
namespace ph {
enum class ConfigLoad { Missing, Valid, Invalid, Error };
class RegistryStore {
public:
    explicit RegistryStore(std::wstring path=L"Software\\PrecisionHold\\Config"):path_(std::move(path)){}
    ConfigLoad load(Config& config) const;
    bool save(const Config& config) const;
private: std::wstring path_;
};
struct AutostartState{bool enabled{},matches{};};
bool queryAutostart(AutostartState& state);
bool setAutostart(bool enabled);
std::wstring autostartCommand();
}
