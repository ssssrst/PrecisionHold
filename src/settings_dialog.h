#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "config.h"
namespace ph {
constexpr UINT settingsMessage=WM_APP+10;
enum class UiAction { Apply=1, Record, CancelRecord, Exit, Retry, ToggleEnabled, ToggleAutostart, Restore, Diagnostics };
std::wstring hotkeyName(const Hotkey& keys);
class SettingsDialog {
public:
    bool create(HINSTANCE instance, HWND owner);
    void destroy();
    void show(const Config& config);
    void status(const wchar_t* text, bool error=true);
    void clearNotice();
    void runtime(bool enabled,bool active,bool waiting,bool failed,int actual,bool recoverable,bool accelerated=false);
    void accepted(const Config& config);
    void recording(bool active, const Hotkey& keys={});
    void recorded(const Hotkey& keys);
    HWND window() const { return window_; }
    Config draft() const;
    bool autostart() const;
    void setAutostart(bool enabled, bool available);
private:
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK controlProcedure(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    LRESULT message(UINT,WPARAM,LPARAM);
    bool controls();
    void layout(UINT dpi);
    void refresh();
    void refreshNotice();
    void theme();
    void paint(HDC dc);
    void paintButton(HWND control,HDC dc);
    void paintLabel(HWND control,HDC dc);
    LRESULT drawSlider(void* notification);
    void moreMenu();
    void help(bool shortcut);
    void action(UiAction value);
    int px(int value) const { return MulDiv(value,static_cast<int>(dpi_),96); }
    struct Control { HWND hwnd; int x,y,w,h; };
    HWND window_{},owner_{},hotkey_{},slider_{},speedLabel_{},enabled_{},recordButton_{},autostart_{},current_{},apply_{},retry_{},hovered_{},noticeControl_{};
    HINSTANCE instance_{};
    HFONT font_{},smallFont_{},titleFont_{},numberFont_{};
    HBRUSH backgroundBrush_{},cardBrush_{},keyBrush_{};
    ULONG_PTR graphicsToken_{};
    std::vector<Control> children_;
    Config draft_,saved_;
    std::wstring notice_;
    UINT dpi_{96};
    COLORREF background_{},card_{},border_{},foreground_{},secondary_{},accent_{},keyBackground_{},track_{};
    int actual_{},keycapLeft_{208};
    bool capturing_{},dark_{},highContrast_{},slowed_{},waiting_{},recoveryFailed_{},recoverable_{},noticeError_{},dirty_{};
};
}
