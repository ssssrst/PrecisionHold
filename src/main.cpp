#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <commctrl.h>
#include <cstddef>
#include <vector>
#include "pointer_speed.h"
#include "keyboard_input.h"
#include "hotkey_recorder.h"
#include "settings_dialog.h"
#include "tray_icon.h"
#include "registry_store.h"

namespace {
constexpr auto windowClass=L"PrecisionHold.MessageWindow";
constexpr UINT showMessage=WM_APP+1;
ph::WinBackend backend;
ph::SpeedControl speed(backend);
ph::HotkeyMatcher matcher;
ph::HotkeyRecorder recorder;
ph::Config config;
ph::SettingsDialog settings;
ph::TrayIcon tray;
ph::RegistryStore store;
HWND messageWindow{};
UINT taskbarCreated{};
bool inputRegistered{},sessionRegistered{},suspended{},errorShown{},capturing{},captureWaiting{},trayReady{};
void status(bool ok=true) {
    if(!settings.window()) return;
    std::wstring text;
    if(!ok || speed.blocked()) text=L"系统操作或恢复失败。请重试恢复；恢复信息已尽可能保留。";
    else if(capturing) text=L"正在录制；减速已暂停。Esc 取消，全部松开后确认。";
    else if(speed.active()) text=std::wstring(speed.accelerated()?L"正在加速至 ":L"正在减速至 ")+std::to_wstring(config.speed)+L" 级；释放热键恢复原速。";
    else if(!config.enabled) text=L"已暂停；快捷键不会改变指针速度。";
    else if(matcher.waitingForRelease()) text=L"等待快捷键全部松开，然后重新按下；当前未减速。";
    else text=L"已启用 · 按住 "+ph::hotkeyName(config.hotkey)+L" 切换至 "+std::to_wstring(config.speed)+L" 级。";
    int actual{};
    if(backend.get(actual))text+=L" 系统当前："+std::to_wstring(actual)+L" 级。";
    else text+=L" 无法读取系统当前档位。";
    settings.runtime(config.enabled,speed.active(),matcher.waitingForRelease(),!ok || speed.blocked(),actual,speed.recoverable(),speed.accelerated());
    if(trayReady && !tray.update(!config.enabled?2:(speed.active()?1:0),L"PrecisionHold · "+text))
        OutputDebugStringW(L"PrecisionHold: tray update failed.\n");
    if(!ok && !errorShown) {errorShown=true;MessageBeep(MB_ICONWARNING);settings.show(config);}
}
bool interruptInput(bool preserveHeld=false) {
    matcher.interrupt(preserveHeld);
    if(capturing){capturing=false;captureWaiting=false;recorder.reset();settings.recording(false);}
    const bool ok=speed.restore();status(ok);return ok;
}
void showSettings() {
    settings.show(config);status();ph::AutostartState state;
    if(!ph::queryAutostart(state)){settings.setAutostart(false,false);settings.status(L"读取开机启动项失败，请检查当前用户注册表权限。");}
    else{settings.setAutostart(state.enabled,true);if(state.enabled && !state.matches)settings.status(L"启动路径已变化，请关闭后重新开启登录启动。");}
}
void inputEvent(ph::Device device,ph::Key key,bool down) {
    if(capturing) {
        matcher.event(device,key,down,false);
        if(key==VK_ESCAPE && down){interruptInput();return;}
        if(captureWaiting){if(matcher.released())captureWaiting=false;return;}
        const auto result=recorder.event(device,key,down);
        settings.recording(true,recorder.keys());
        if(result==ph::RecordResult::Complete){
            const auto keys=recorder.keys();capturing=false;matcher.interrupt();settings.recorded(keys);status();
            settings.status(L"快捷键已录制，点击应用保存",false);
        } else if(result==ph::RecordResult::Invalid || result==ph::RecordResult::Cancelled){
            interruptInput();settings.status(L"录制已取消或组合无效；最多 4 键，不支持 Ctrl+Alt+Del。");
        }
        return;
    }
    const auto waiting=matcher.waitingForRelease();
    const auto edge=matcher.event(device,key,down,config.enabled && !suspended && !speed.blocked());
    if(edge==ph::HoldEdge::Pressed)status(speed.activate(config.speed));
    if(edge==ph::HoldEdge::Released)status(speed.restore());
    if(edge==ph::HoldEdge::None && waiting!=matcher.waitingForRelease())status();
}
void rawInput(LPARAM value) {
    UINT size{};const auto handle=reinterpret_cast<HRAWINPUT>(value);
    if(GetRawInputData(handle,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=0 || size<sizeof(RAWINPUTHEADER) || size>65536){interruptInput();return;}
    std::vector<RAWINPUT> storage((size+sizeof(RAWINPUT)-1)/sizeof(RAWINPUT));UINT bytes=size;
    const auto got=GetRawInputData(handle,RID_INPUT,storage.data(),&bytes,sizeof(RAWINPUTHEADER));
    if(got==static_cast<UINT>(-1) || got!=size || bytes!=size){interruptInput();return;}
    const auto& input=storage.front();
    if(input.header.dwSize!=size){interruptInput();return;}
    if(input.header.dwType!=RIM_TYPEKEYBOARD)return;
    if(size<offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)){interruptInput();return;}
    const auto& k=input.data.keyboard;
    if(k.MakeCode==KEYBOARD_OVERRUN_MAKE_CODE){interruptInput();return;}
    const auto key=ph::normalizeKey(k.VKey,k.MakeCode,k.Flags);
    if(key)inputEvent(reinterpret_cast<ph::Device>(input.header.hDevice),*key,(k.Flags&RI_KEY_BREAK)==0);
}
void applySettings() {
    auto next=settings.draft();
    if(!ph::validConfig(next)){settings.status(L"设置无效：档位必须为 1–20，热键为 1–4 个有效不同键。");return;}
    if(!interruptInput())return;
    if(!store.save(next)){settings.status(L"配置保存失败；当前运行设置未改变。");return;}
    config=std::move(next);matcher.setHotkey(config.hotkey);errorShown=false;settings.accepted(config);status();
}
void toggleEnabled() {
    const bool desired=settings.draft().enabled;
    if(!interruptInput()){status(false);return;}
    auto next=config;next.enabled=desired;
    if(!store.save(next)){status();settings.status(L"启用状态保存失败，已保留原设置");return;}
    config=std::move(next);settings.clearNotice();status();
}
void toggleAutostart() {
    const bool desired=settings.autostart();
    if(!ph::setAutostart(desired)){
        ph::AutostartState actual;
        const bool readable=ph::queryAutostart(actual);settings.setAutostart(actual.enabled,readable);
        settings.status(L"登录启动保存失败，请重试");return;
    }
    settings.setAutostart(desired,true);settings.clearNotice();
}
void diagnostics() {
    int actual{};const bool readable=backend.get(actual);
    std::wstring detail=L"PrecisionHold 0.10.1\n\n快捷键："+ph::hotkeyName(config.hotkey)+L"\n目标档位："+std::to_wstring(config.speed)+
        L"\n系统当前："+(readable?std::to_wstring(actual):L"读取失败")+L"\n功能："+(config.enabled?L"已启用":L"已暂停")+
        L"\n输入："+(inputRegistered?L"已注册":L"未注册")+L"\n恢复记录："+(speed.recoverable()?L"待恢复或清理":L"无")+
        L"\n\n快捷键不会拦截原软件输入。\n仅调整 Windows 指针速度，部分软件的原始输入不受影响。";
    MessageBoxW(settings.window(),detail.c_str(),L"诊断详情",MB_OK|MB_ICONINFORMATION);
}
void menu() {
    const auto handle=CreatePopupMenu();if(!handle){status(false);return;}
    bool ok=AppendMenuW(handle,MF_STRING,100,config.enabled?L"暂停功能":L"启用功能")!=FALSE;
    ph::AutostartState startup;const bool startupOk=ph::queryAutostart(startup);
    ok=ok && AppendMenuW(handle,MF_STRING,101,L"设置…") && AppendMenuW(handle,MF_STRING|(speed.recoverable()?0:MF_GRAYED),102,L"恢复鼠标原速") &&
        AppendMenuW(handle,MF_STRING|(startup.enabled?MF_CHECKED:0)|(startupOk?0:MF_GRAYED),103,L"开机自启动") &&
        AppendMenuW(handle,MF_SEPARATOR,0,nullptr) && AppendMenuW(handle,MF_STRING,104,L"退出程序");
    POINT point{};
    if(ok && GetCursorPos(&point)) {
        SetForegroundWindow(messageWindow);SetLastError(0);
        const auto command=TrackPopupMenu(handle,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,point.x,point.y,0,messageWindow,nullptr);
        if(command)SendMessageW(messageWindow,WM_COMMAND,command,0);
        else if(GetLastError()!=0)status(false);
        PostMessageW(messageWindow,WM_NULL,0,0);
    } else status(false);
    if(!DestroyMenu(handle))status(false);
}
LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    try {
        if(taskbarCreated && message==taskbarCreated){trayReady=tray.add();if(!trayReady)showSettings();status();return 0;}
        switch(message) {
        case WM_INPUT:rawInput(l);return DefWindowProcW(hwnd,message,w,l);
        case WM_SETTINGCHANGE:case WM_THEMECHANGED:
            if(trayReady && !tray.refreshTheme())OutputDebugStringW(L"PrecisionHold: tray theme update failed.\n");status();return 0;
        case WM_INPUT_DEVICE_CHANGE:if(w==GIDC_REMOVAL){matcher.remove(static_cast<ph::Device>(l));interruptInput();}return 0;
        case ph::trayMessage:
            if(LOWORD(l)==NIN_SELECT || LOWORD(l)==NIN_KEYSELECT)showSettings();
            else if(LOWORD(l)==WM_CONTEXTMENU)menu();return 0;
        case ph::settingsMessage:
            switch(static_cast<ph::UiAction>(w)) {
            case ph::UiAction::Apply:applySettings();break;
            case ph::UiAction::Record:if(interruptInput(true)){capturing=true;captureWaiting=!matcher.released();recorder.reset();settings.recording(true);status();}break;
            case ph::UiAction::CancelRecord:if(capturing)interruptInput();break;
            case ph::UiAction::Exit:SendMessageW(hwnd,WM_CLOSE,0,0);break;
            case ph::UiAction::Retry:errorShown=false;interruptInput();break;
            case ph::UiAction::ToggleEnabled:toggleEnabled();break;
            case ph::UiAction::ToggleAutostart:toggleAutostart();break;
            case ph::UiAction::Restore:errorShown=false;interruptInput();break;
            case ph::UiAction::Diagnostics:diagnostics();break;
            }return 0;
        case WM_COMMAND:
            switch(LOWORD(w)) {
            case 100:{interruptInput();auto next=config;next.enabled=!next.enabled;if(store.save(next)){config=std::move(next);status();}else settings.status(L"暂停状态保存失败，请重试。");break;}
            case 101:showSettings();break;
            case 102:errorShown=false;interruptInput();break;
            case 103:{ph::AutostartState startup;if(!ph::queryAutostart(startup) || !ph::setAutostart(!startup.enabled))status(false);else settings.setAutostart(!startup.enabled,true);break;}
            case 104:SendMessageW(hwnd,WM_CLOSE,0,0);break;
            }return 0;
        case WM_WTSSESSION_CHANGE:
            if(w==WTS_SESSION_LOCK || w==WTS_SESSION_LOGOFF || w==WTS_CONSOLE_DISCONNECT || w==WTS_REMOTE_DISCONNECT){suspended=true;interruptInput();}
            if(w==WTS_SESSION_UNLOCK || w==WTS_SESSION_LOGON || w==WTS_CONSOLE_CONNECT || w==WTS_REMOTE_CONNECT){interruptInput();suspended=false;}return 0;
        case WM_POWERBROADCAST:
            if(w==PBT_APMSUSPEND){suspended=true;interruptInput();}
            if(w==PBT_APMRESUMEAUTOMATIC || w==PBT_APMRESUMESUSPEND){interruptInput();suspended=false;}return TRUE;
        case WM_QUERYENDSESSION:suspended=true;interruptInput();return TRUE;
        case WM_ENDSESSION:if(w)SendMessageW(hwnd,WM_CLOSE,0,0);else suspended=false;return 0;
        case showMessage:showSettings();return 0;
        case WM_CLOSE:
            suspended=true;
            if(!interruptInput()){suspended=false;showSettings();MessageBoxW(settings.window(),L"未能确认恢复成功，请重试。强制结束后，下次启动会依据恢复记录再次尝试。",L"PrecisionHold",MB_OK|MB_ICONWARNING);return 0;}
            PostQuitMessage(0);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        }
    }catch(...){suspended=true;matcher.interrupt();capturing=false;speed.restore();settings.status(L"发生内部错误，请恢复原速并重新启动程序。");if(message==WM_INPUT)return DefWindowProcW(hwnd,message,w,l);return 0;}
    return DefWindowProcW(hwnd,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    const auto mutex=CreateMutexW(nullptr,TRUE,L"Local\\PrecisionHold.Instance");if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){if(const auto existing=FindWindowW(windowClass,nullptr))PostMessageW(existing,showMessage,0,0);CloseHandle(mutex);return 0;}
    int result=1;
    do {
        if(!speed.recoverStartup()){MessageBoxW(nullptr,L"恢复记录读取或恢复失败，本次未启用输入。请检查 Windows 鼠标设置及恢复记录，稍后重新运行。",L"PrecisionHold",MB_OK|MB_ICONERROR);break;}
        const auto loaded=store.load(config);const bool configError=loaded==ph::ConfigLoad::Invalid || loaded==ph::ConfigLoad::Error;
        if(configError)config=ph::Config{};
        if(!matcher.setHotkey(config.hotkey))break;
        INITCOMMONCONTROLSEX common{sizeof common,ICC_BAR_CLASSES};if(!InitCommonControlsEx(&common))break;
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpfnWndProc=windowProc;cls.lpszClassName=windowClass;if(!RegisterClassW(&cls))break;
        messageWindow=CreateWindowExW(WS_EX_TOOLWINDOW,windowClass,L"PrecisionHold",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
        if(!messageWindow || !settings.create(instance,messageWindow))break;
        taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");if(!taskbarCreated)break;
        trayReady=tray.initialize(instance,messageWindow);
        sessionRegistered=WTSRegisterSessionNotification(messageWindow,NOTIFY_FOR_THIS_SESSION)!=FALSE;
        if(!sessionRegistered){MessageBoxW(settings.window(),L"会话通知注册失败，未启用输入。",L"PrecisionHold",MB_OK|MB_ICONERROR);break;}
        RAWINPUTDEVICE device{0x01,0x06,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,messageWindow};inputRegistered=RegisterRawInputDevices(&device,1,sizeof device)!=FALSE;
        if(!inputRegistered){MessageBoxW(settings.window(),L"Raw Input 注册失败，未启用输入。",L"PrecisionHold",MB_OK|MB_ICONERROR);break;}
        bool background=config.firstRun;int argc{};const auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
        if(args){for(int i=1;i<argc;++i)if(wcscmp(args[i],L"--background")==0)background=true;else if(wcscmp(args[i],L"--settings")==0)background=false;LocalFree(args);}
        status();
        if(!background || !trayReady || configError){showSettings();if(configError)settings.status(L"配置无效或读取失败，已使用安全默认值；请检查后应用。");}
        if(!config.firstRun && !configError){config.firstRun=true;if(!store.save(config)){showSettings();settings.status(L"首次运行状态保存失败；下次运行可能再次显示设置。");}}
        MSG message{};BOOL next{};
        while((next=GetMessageW(&message,nullptr,0,0))>0){if(!IsDialogMessageW(settings.window(),&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        result=next==-1?1:0;
    }while(false);
    suspended=true;if(!speed.restore())result=2;
    if(inputRegistered){RAWINPUTDEVICE device{0x01,0x06,RIDEV_REMOVE,nullptr};if(!RegisterRawInputDevices(&device,1,sizeof device))result=2;}
    if(sessionRegistered && !WTSUnRegisterSessionNotification(messageWindow))result=2;
    tray.remove();settings.destroy();if(messageWindow && IsWindow(messageWindow))DestroyWindow(messageWindow);
    if(!ReleaseMutex(mutex))result=2;if(!CloseHandle(mutex))result=2;return result;
}
