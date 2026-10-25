#pragma once
#include "hotkey_matcher.h"
#include <array>
#include <optional>
namespace ph {
struct Config {
    int speed{4};
    bool enabled{true};
    Hotkey hotkey{0x14}; // Caps Lock. Existing persisted shortcuts remain unchanged.
    bool firstRun{};
};
inline bool validConfig(const Config& config) { return config.speed>=1 && config.speed<=20 && validHotkey(config.hotkey); }
using ConfigRecord=std::array<std::uint32_t,12>;
inline std::uint32_t configChecksum(const ConfigRecord& record){std::uint32_t hash=2166136261U;for(std::size_t i=0;i<11;++i){hash^=record[i];hash*=16777619U;}return hash;}
inline ConfigRecord encodeConfig(const Config& config){
    ConfigRecord record{0x50484331,1,static_cast<std::uint32_t>(config.speed),config.enabled?1U:0U,config.firstRun?1U:0U,static_cast<std::uint32_t>(config.hotkey.size())};
    for(std::size_t i=0;i<config.hotkey.size() && i<4;++i)record[6+i]=config.hotkey[i];record[11]=configChecksum(record);return record;
}
inline std::optional<Config> decodeConfig(const ConfigRecord& record){
    if(record[0]!=0x50484331 || record[1]!=1 || record[2]<1 || record[2]>20 || record[3]>1 || record[4]>1 || record[5]<1 || record[5]>4 || record[10]!=0 || record[11]!=configChecksum(record))return std::nullopt;
    Config config;config.speed=static_cast<int>(record[2]);config.enabled=record[3]!=0;config.firstRun=record[4]!=0;config.hotkey.clear();
    for(std::size_t i=0;i<4;++i){if(i<record[5]){if(record[6+i]>0xffff)return std::nullopt;config.hotkey.push_back(static_cast<Key>(record[6+i]));}else if(record[6+i])return std::nullopt;}
    if(!validConfig(config))return std::nullopt;return config;
}
}
