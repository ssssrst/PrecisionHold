#include "registry_store.h"
namespace ph {
ConfigLoad RegistryStore::load(Config& config) const {
    ConfigRecord record{};DWORD bytes=sizeof record,type{};
    const auto result=RegGetValueW(HKEY_CURRENT_USER,path_.c_str(),L"Snapshot",RRF_RT_ANY,&type,record.data(),&bytes);
    if(result==ERROR_FILE_NOT_FOUND || result==ERROR_PATH_NOT_FOUND)return ConfigLoad::Missing;
    if(result==ERROR_MORE_DATA || (result==ERROR_SUCCESS && (type!=REG_BINARY || bytes!=sizeof record)))return ConfigLoad::Invalid;
    if(result!=ERROR_SUCCESS)return ConfigLoad::Error;
    const auto decoded=decodeConfig(record);if(!decoded)return ConfigLoad::Invalid;config=*decoded;return ConfigLoad::Valid;
}
bool RegistryStore::save(const Config& config) const {
    if(!validConfig(config))return false;
    auto record=encodeConfig(config);HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,path_.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const auto result=RegSetValueExW(key,L"Snapshot",0,REG_BINARY,reinterpret_cast<const BYTE*>(record.data()),sizeof record);
    const auto closed=RegCloseKey(key);return result==ERROR_SUCCESS && closed==ERROR_SUCCESS;
}
std::wstring autostartCommand(){
    std::wstring path(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!length || length>=path.size())return {};path.resize(length);return L"\""+path+L"\" --background";
}
bool queryAutostart(AutostartState& state){
    state={};std::wstring value(32768,L'\0');DWORD bytes=static_cast<DWORD>(value.size()*sizeof(wchar_t)),type{};
    const auto result=RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"PrecisionHold",RRF_RT_REG_SZ,&type,value.data(),&bytes);
    if(result==ERROR_FILE_NOT_FOUND || result==ERROR_PATH_NOT_FOUND)return true;
    if(result!=ERROR_SUCCESS || type!=REG_SZ || bytes<sizeof(wchar_t))return false;
    value.resize(wcslen(value.c_str()));const auto command=autostartCommand();if(command.empty())return false;
    state.enabled=true;state.matches=_wcsicmp(value.c_str(),command.c_str())==0;return true;
}
bool setAutostart(bool enabled){
    constexpr auto path=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if(!enabled){const auto result=RegDeleteKeyValueW(HKEY_CURRENT_USER,path,L"PrecisionHold");return result==ERROR_SUCCESS || result==ERROR_FILE_NOT_FOUND || result==ERROR_PATH_NOT_FOUND;}
    const auto command=autostartCommand();if(command.empty())return false;HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,path,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    const auto result=RegSetValueExW(key,L"PrecisionHold",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    const auto closed=RegCloseKey(key);return result==ERROR_SUCCESS && closed==ERROR_SUCCESS;
}
}
