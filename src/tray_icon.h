#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
namespace ph {
constexpr UINT trayMessage=WM_APP+20;
class TrayIcon {
public:
    bool initialize(HINSTANCE instance,HWND owner);
    bool add();
    bool update(int state,const std::wstring& tooltip);
    bool refreshTheme();
    void remove();
private:
    NOTIFYICONDATAW data_{};
    HICON icons_[3]{};
    HINSTANCE instance_{};
    int state_{};
    bool present_{};
};
}
