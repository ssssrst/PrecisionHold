#include "tray_icon.h"
namespace ph {
bool TrayIcon::initialize(HINSTANCE instance,HWND owner) {
    instance_=instance;
    if(!refreshTheme())return false;
    data_.cbSize=sizeof data_;data_.hWnd=owner;data_.uID=1;data_.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP;
    data_.uCallbackMessage=trayMessage;data_.hIcon=icons_[0];wcscpy_s(data_.szTip,L"PrecisionHold");return add();
}
bool TrayIcon::add() {
    present_=Shell_NotifyIconW(NIM_ADD,&data_)!=FALSE;
    if(!present_)present_=Shell_NotifyIconW(NIM_MODIFY,&data_)!=FALSE;
    if(!present_)return false;
    data_.uVersion=NOTIFYICON_VERSION_4;
    if(!Shell_NotifyIconW(NIM_SETVERSION,&data_)){remove();return false;}return true;
}
bool TrayIcon::update(int state,const std::wstring& tip) {
    if(state<0 || state>2)return false;
    state_=state;
    data_.hIcon=icons_[state];wcsncpy_s(data_.szTip,tip.c_str(),_TRUNCATE);
    if(!present_)return add();
    if(!Shell_NotifyIconW(NIM_MODIFY,&data_)){present_=false;return false;}return true;
}
bool TrayIcon::refreshTheme() {
    DWORD light{},bytes=sizeof light;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"SystemUsesLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes)!=ERROR_SUCCESS)light=0;
    const auto base=light?102:105;
    for(int i=0;i<3;++i){
        icons_[i]=reinterpret_cast<HICON>(LoadImageW(instance_,MAKEINTRESOURCEW(base+i),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
        if(!icons_[i])return false;
    }
    data_.hIcon=icons_[state_];return true;
}
void TrayIcon::remove(){if(present_){if(!Shell_NotifyIconW(NIM_DELETE,&data_))OutputDebugStringW(L"PrecisionHold: tray removal failed.\n");present_=false;}}
}
