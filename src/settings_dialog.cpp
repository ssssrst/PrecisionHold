#include "settings_dialog.h"
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <cwchar>

namespace ph {
namespace {
Gdiplus::Color color(COLORREF value,BYTE alpha=255) {return Gdiplus::Color(alpha,GetRValue(value),GetGValue(value),GetBValue(value));}
void rounded(HDC dc,const RECT& area,float radius,COLORREF fill,COLORREF edge,bool outline=true) {
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const auto x=static_cast<float>(area.left)+.5f,y=static_cast<float>(area.top)+.5f;
    const auto w=static_cast<float>(area.right-area.left)-1,h=static_cast<float>(area.bottom-area.top)-1;
    if(w<=0 || h<=0)return;
    const auto d=(std::min)(radius*2,(std::min)(w,h));
    Gdiplus::GraphicsPath p;p.AddArc(x,y,d,d,180,90);p.AddArc(x+w-d,y,d,d,270,90);
    p.AddArc(x+w-d,y+h-d,d,d,0,90);p.AddArc(x,y+h-d,d,d,90,90);p.CloseFigure();
    Gdiplus::SolidBrush brush(color(fill));g.FillPath(&brush,&p);
    if(outline){Gdiplus::Pen pen(color(edge),1.f);g.DrawPath(&pen,&p);}
}
void text(HDC dc,HFONT font,COLORREF tint,RECT rect,const wchar_t* value,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
    const auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,tint);DrawTextW(dc,value,-1,&rect,flags);SelectObject(dc,old);
}
std::wstring windowText(HWND window){wchar_t value[512]{};GetWindowTextW(window,value,512);return value;}
void setText(HWND window,const std::wstring& value){if(windowText(window)!=value)SetWindowTextW(window,value.c_str());}
void enable(HWND window,bool value){if((IsWindowEnabled(window)!=FALSE)!=value)EnableWindow(window,value);}
void check(HWND window,bool value){if((SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED)!=value){SendMessageW(window,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);InvalidateRect(window,nullptr,FALSE);}}
// Paint a complete surface offscreen, then present it once. Fall back to the
// original DC if GDI allocation fails; never leave the window unpainted.
class PaintBuffer {
public:
    PaintBuffer(HWND window,HDC target):target_(target){
        GetClientRect(window,&area_);memory_=CreateCompatibleDC(target);
        if(memory_)bitmap_=CreateCompatibleBitmap(target,area_.right,area_.bottom);
        if(bitmap_)old_=SelectObject(memory_,bitmap_);
    }
    ~PaintBuffer(){
        if(bitmap_){BitBlt(target_,0,0,area_.right,area_.bottom,memory_,0,0,SRCCOPY);SelectObject(memory_,old_);DeleteObject(bitmap_);}
        if(memory_)DeleteDC(memory_);
    }
    HDC dc() const{return bitmap_?memory_:target_;}
private:HDC target_{},memory_{};HBITMAP bitmap_{};HGDIOBJ old_{};RECT area_{};
};
}
std::wstring hotkeyName(const Hotkey& keys) {
    std::wstring result;
    for(const auto key:keys){
        if(!result.empty())result+=L" + ";
        if(key>=0x60 && key<=0x69){result+=L"小键盘 "+std::to_wstring(key-0x60);continue;}
        if(key==0x6e){result+=L"小键盘 .";continue;}
        switch(key){
        case VK_CAPITAL:result+=L"Caps Lock";break;
        case leftCtrl:result+=L"左 Ctrl";break;case rightCtrl:result+=L"右 Ctrl";break;
        case leftShift:result+=L"左 Shift";break;case rightShift:result+=L"右 Shift";break;
        case leftAlt:result+=L"左 Alt";break;case rightAlt:result+=L"右 Alt";break;
        case numpadEnter:result+=L"小键盘 Enter";break;
        default:{
            wchar_t name[80]{};const auto scan=MapVirtualKeyW(key,MAPVK_VK_TO_VSC_EX);
            const auto bits=static_cast<LONG>((scan&0xff)<<16)|((scan&0xff00)?(1L<<24):0);
            if(scan && GetKeyNameTextW(bits,name,80)>0)result+=name;else result+=L"VK "+std::to_wstring(key);
        }}
    }return result;
}
bool SettingsDialog::create(HINSTANCE instance,HWND owner) {
    instance_=instance;owner_=owner;
    Gdiplus::GdiplusStartupInput startup;
    if(Gdiplus::GdiplusStartup(&graphicsToken_,&startup,nullptr)!=Gdiplus::Ok)return false;
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"PrecisionHold.Settings";cls.lpfnWndProc=procedure;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));
    if(!cls.hCursor || !cls.hIcon || !RegisterClassW(&cls))return false;
    window_=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"PrecisionHold",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,500,570,nullptr,nullptr,instance,this);
    if(!window_ || !controls())return false;
    theme();layout(GetDpiForWindow(window_));refresh();
    for(const auto pair:std::array<std::pair<WPARAM,int>,2>{{{ICON_SMALL,SM_CXSMICON},{ICON_BIG,SM_CXICON}}}){
        const auto size=GetSystemMetrics(pair.second);
        const auto icon=LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_SHARED);
        if(icon)SendMessageW(window_,WM_SETICON,pair.first,reinterpret_cast<LPARAM>(icon));
    }
    return true;
}
bool SettingsDialog::controls() {
    const auto add=[&](const wchar_t* cls,const wchar_t* label,DWORD style,int id,int x,int y,int w,int h){
        const auto child=CreateWindowW(cls,label,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
        if(child){children_.push_back({child,x,y,w,h});if(!SetWindowSubclass(child,controlProcedure,1,reinterpret_cast<DWORD_PTR>(this)))return static_cast<HWND>(nullptr);}
        return child;
    };
    noticeControl_=add(L"STATIC",L"按住切换，松开恢复",0,0,24,66,432,38);
    enabled_=add(L"BUTTON",L"已启用",BS_AUTOCHECKBOX|WS_TABSTOP,23,332,23,124,32);
    hotkey_=add(L"EDIT",L"",ES_READONLY|ES_CENTER|ES_AUTOHSCROLL|WS_TABSTOP,20,216,140,168,24);
    recordButton_=add(L"BUTTON",L"更改快捷键",BS_PUSHBUTTON|WS_TABSTOP,21,398,134,34,36);
    speedLabel_=add(L"STATIC",L"4",SS_RIGHT,32,380,219,52,30);
    add(L"BUTTON",L"速度说明",BS_PUSHBUTTON|WS_TABSTOP,31,180,218,28,28);
    slider_=add(TRACKBAR_CLASSW,L"按住时的 Windows 指针速度档位",TBS_NOTICKS|WS_TABSTOP,22,44,258,392,34);
    SendMessageW(slider_,TBM_SETRANGE,TRUE,MAKELPARAM(1,20));SetWindowTheme(slider_,L"",L"");
    current_=add(L"STATIC",L"系统当前",0,33,48,320,384,22);
    autostart_=add(L"BUTTON",L"登录时启动",BS_AUTOCHECKBOX|WS_TABSTOP,24,386,380,46,32);
    add(L"BUTTON",L"更多",BS_PUSHBUTTON|WS_TABSTOP,30,24,456,82,36);
    apply_=add(L"BUTTON",L"应用",BS_DEFPUSHBUTTON|WS_TABSTOP,26,340,456,116,36);
    retry_=add(L"BUTTON",L"重试",BS_PUSHBUTTON|WS_TABSTOP,28,384,76,72,28);
    ShowWindow(retry_,SW_HIDE);
    // Stable native control IDs remain available to integration tests and accessibility.
    return children_.size()==12 && noticeControl_ && enabled_ && hotkey_ && recordButton_ && slider_ && speedLabel_ && current_ && autostart_ && apply_ && retry_;
}
void SettingsDialog::theme(){
    const auto wasDark=dark_,wasHighContrast=highContrast_;
    HIGHCONTRASTW contrast{sizeof contrast};
    highContrast_=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof contrast,&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON);
    DWORD light=1,bytes=sizeof light;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes)!=ERROR_SUCCESS)light=1;
    dark_=!light && !highContrast_;
    if(backgroundBrush_ && wasDark==dark_ && wasHighContrast==highContrast_ && !highContrast_)return;
    background_=dark_?RGB(32,32,32):RGB(243,243,243);card_=dark_?RGB(43,43,43):RGB(255,255,255);
    border_=dark_?RGB(60,60,60):RGB(228,228,228);foreground_=dark_?RGB(245,245,245):RGB(28,28,28);
    secondary_=dark_?RGB(180,180,180):RGB(100,100,100);accent_=dark_?RGB(96,205,255):RGB(0,103,192);
    keyBackground_=dark_?RGB(51,51,51):RGB(248,248,248);track_=dark_?RGB(105,105,105):RGB(205,208,212);
    if(highContrast_){background_=card_=keyBackground_=GetSysColor(COLOR_WINDOW);foreground_=secondary_=border_=track_=GetSysColor(COLOR_WINDOWTEXT);accent_=GetSysColor(COLOR_HIGHLIGHT);}
    if(backgroundBrush_)DeleteObject(backgroundBrush_);if(cardBrush_)DeleteObject(cardBrush_);if(keyBrush_)DeleteObject(keyBrush_);
    backgroundBrush_=CreateSolidBrush(background_);cardBrush_=CreateSolidBrush(card_);keyBrush_=CreateSolidBrush(keyBackground_);
    const BOOL useDark=dark_;const DWORD corners=2;
    // Cosmetic attributes are optional on Windows 10; failure retains the native frame.
    if(FAILED(DwmSetWindowAttribute(window_,20,&useDark,sizeof useDark)))OutputDebugStringW(L"PrecisionHold: native title bar fallback.\n");
    if(FAILED(DwmSetWindowAttribute(window_,33,&corners,sizeof corners)))OutputDebugStringW(L"PrecisionHold: native corner fallback.\n");
    if(window_)RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void SettingsDialog::layout(UINT dpi) {
    dpi_=dpi?dpi:96;
    const auto make=[&](HFONT& target,int size,int weight){
        const auto next=CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        if(next){if(target)DeleteObject(target);target=next;}
    };
    make(font_,16,FW_NORMAL);make(smallFont_,14,FW_NORMAL);make(titleFont_,26,FW_SEMIBOLD);make(numberFont_,18,FW_SEMIBOLD);
    for(const auto& child:children_){
        SetWindowPos(child.hwnd,nullptr,px(child.x),px(child.y),px(child.w),px(child.h),SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(child.hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(child.hwnd==speedLabel_?numberFont_:((child.hwnd==current_ || child.hwnd==noticeControl_)?smallFont_:font_)),TRUE);
    }
    RECT area{0,0,px(480),px(516)};
    if(AdjustWindowRectExForDpi(&area,static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE)),FALSE,WS_EX_CONTROLPARENT,dpi_))
        SetWindowPos(window_,nullptr,0,0,area.right-area.left,area.bottom-area.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    InvalidateRect(window_,nullptr,FALSE);
    keycapLeft_=208;refresh();
}
void SettingsDialog::paint(HDC dc){
    RECT client{};GetClientRect(window_,&client);FillRect(dc,&client,backgroundBrush_);
    const auto rect=[&](int x,int y,int w,int h){return RECT{px(x),px(y),px(x+w),px(y+h)};};
    text(dc,titleFont_,foreground_,rect(24,22,280,36),L"精准控制");
    for(const auto& card:std::array<RECT,3>{rect(24,112,432,80),rect(24,200,432,156),rect(24,364,432,64)})rounded(dc,card,static_cast<float>(px(8)),card_,border_);
    text(dc,font_,foreground_,rect(76,132,120,36),L"快捷键");
    text(dc,font_,foreground_,rect(76,214,112,36),L"按住时速度");
    text(dc,font_,foreground_,rect(76,378,270,36),L"登录时启动");
    text(dc,smallFont_,secondary_,rect(48,297,50,20),L"慢");
    text(dc,smallFont_,secondary_,rect(382,297,50,20),L"快",DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    rounded(dc,rect(keycapLeft_,134,392-keycapLeft_,36),static_cast<float>(px(4)),keyBackground_,border_);
    const bool dirty=draft_.speed!=saved_.speed || draft_.hotkey!=saved_.hotkey;
    if(dirty)text(dc,smallFont_,secondary_,rect(250,456,76,36),L"未应用",DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(color(foreground_),static_cast<float>(dpi_)/96.f*1.6f);pen.SetStartCap(Gdiplus::LineCapRound);pen.SetEndCap(Gdiplus::LineCapRound);
    // All three glyphs fit an 18-DIP box centered at x=56 and at the
    // corresponding text rectangle's vertical center (150, 232, 396).
    pen.SetWidth(static_cast<float>(dpi_)/96.f*1.3f);
    g.DrawRectangle(&pen,static_cast<float>(px(47)),static_cast<float>(px(144)),static_cast<float>(px(18)),static_cast<float>(px(12)));
    for(int row=0;row<2;++row)for(int col=0;col<4;++col)g.DrawLine(&pen,px(50+col*4),px(147+row*3),px(50+col*4)+1,px(147+row*3));
    g.DrawLine(&pen,px(52),px(153),px(60),px(153));
    const std::array<Gdiplus::Point,7> points{{{px(49),px(223)},{px(49),px(239)},{px(53),px(235)},{px(56),px(241)},{px(59),px(239)},{px(56),px(233)},{px(63),px(233)}}};
    g.DrawPolygon(&pen,points.data(),static_cast<INT>(points.size()));
    g.DrawArc(&pen,static_cast<float>(px(48)),static_cast<float>(px(389)),static_cast<float>(px(16)),static_cast<float>(px(16)),-50.f,280.f);
    g.DrawLine(&pen,px(56),px(387),px(56),px(396));
}
void SettingsDialog::paintButton(HWND control,HDC dc){
    RECT r{};GetClientRect(control,&r);const auto id=GetDlgCtrlID(control);
    const bool toggle=id==23 || id==24,available=IsWindowEnabled(control)!=FALSE;
    const bool hot=hovered_==control,pressed=(SendMessageW(control,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
    const auto parentColor=(id==21 || id==31 || id==24)?card_:background_;
    const auto brush=CreateSolidBrush(parentColor);FillRect(dc,&r,brush);DeleteObject(brush);
    const auto label=windowText(control);
    const auto muted=available?foreground_:secondary_;
    if(toggle){
        const bool on=SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED;
        const int width=px(40),height=px(20),x=r.right-width-px(2),y=(r.bottom-height)/2;
        RECT track{x,y,x+width,y+height};const auto fill=on?accent_:(dark_?RGB(92,92,92):RGB(150,150,150));
        rounded(dc,track,static_cast<float>(height)/2,available?fill:track_,fill,false);
        Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto diameter=static_cast<float>(px(14));const auto left=static_cast<float>(on?x+width-px(17):x+px(3));
        Gdiplus::SolidBrush knob(color(on && dark_?RGB(24,24,24):RGB(255,255,255)));
        g.FillEllipse(&knob,left,static_cast<float>(y+px(3)),diameter,diameter);
        if(id==23){RECT labelRect{0,0,x-px(10),r.bottom};text(dc,font_,muted,labelRect,label.c_str(),DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}
    }else{
        const bool primary=id==26 && available;
        auto fill=primary?accent_:(hot?keyBackground_:parentColor);
        if(pressed)fill=primary?(dark_?RGB(70,170,210):RGB(0,82,155)):track_;
        const bool boxed=id==26 || id==28 || hot;
        if(boxed)rounded(dc,r,static_cast<float>(px(4)),available?fill:keyBackground_,border_,!primary);
        const auto tint=primary?(dark_?RGB(24,24,24):RGB(255,255,255)):muted;
        if(id==21){
            Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::Pen p(color(tint),static_cast<float>(px(1))+0.5f);
            g.DrawLine(&p,px(9),px(26),px(11),px(20));g.DrawLine(&p,px(11),px(20),px(23),px(8));
            g.DrawLine(&p,px(23),px(8),px(27),px(12));g.DrawLine(&p,px(27),px(12),px(15),px(24));g.DrawLine(&p,px(15),px(24),px(9),px(26));
        }else if(id==31){
            Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::Pen p(color(secondary_),static_cast<float>(dpi_)/96.f*1.4f);
            g.DrawEllipse(&p,px(5),px(5),px(18),px(18));g.DrawLine(&p,px(14),px(12),px(14),px(19));
            Gdiplus::SolidBrush dot(color(secondary_));g.FillEllipse(&dot,px(13),px(8),px(2),px(2));
        }
        else if(id==30)text(dc,font_,tint,r,L"···  更多",DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        else text(dc,font_,tint,r,label.c_str(),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    if(GetFocus()==control){RECT focus=r;InflateRect(&focus,-px(2),-px(2));DrawFocusRect(dc,&focus);}
}
void SettingsDialog::paintLabel(HWND control,HDC dc){
    RECT r{};GetClientRect(control,&r);const bool key=control==hotkey_,notice=control==noticeControl_;
    FillRect(dc,&r,notice?backgroundBrush_:(key?keyBrush_:cardBrush_));
    auto tint=key || control==speedLabel_?foreground_:secondary_;
    if(notice && (recoveryFailed_ || noticeError_))tint=dark_?RGB(255,185,100):RGB(154,76,0);
    const auto label=windowText(control);
    text(dc,control==speedLabel_?numberFont_:(key?font_:smallFont_),tint,r,label.c_str(),
        (key?DT_CENTER:(control==speedLabel_?DT_RIGHT:DT_LEFT))|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    if(key && GetFocus()==control){InflateRect(&r,-px(1),-px(1));DrawFocusRect(dc,&r);}
}
LRESULT SettingsDialog::drawSlider(void* value){
    const auto draw=static_cast<NMCUSTOMDRAW*>(value);
    if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
    if(draw->dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
    if(draw->dwItemSpec==TBCD_CHANNEL){
        RECT channel{};SendMessageW(slider_,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
        const auto mid=(channel.top+channel.bottom)/2;RECT line{channel.left,mid-px(2),channel.right,mid+px(2)};
        rounded(draw->hdc,line,static_cast<float>(px(2)),track_,track_,false);
        RECT thumb{};SendMessageW(slider_,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
        line.right=(thumb.left+thumb.right)/2;rounded(draw->hdc,line,static_cast<float>(px(2)),accent_,accent_,false);
        return CDRF_SKIPDEFAULT;
    }
    if(draw->dwItemSpec==TBCD_THUMB){
        const auto x=(draw->rc.left+draw->rc.right)/2,y=(draw->rc.top+draw->rc.bottom)/2;
        Gdiplus::Graphics g(draw->hdc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::SolidBrush outer(color(dark_?RGB(240,240,240):RGB(255,255,255))),inner(color(accent_));
        Gdiplus::Pen edge(color(border_),1.f);
        const auto diameter=static_cast<float>(px(20)),radius=diameter/2.f;
        g.FillEllipse(&outer,static_cast<float>(x)-radius,static_cast<float>(y)-radius,diameter,diameter);
        g.DrawEllipse(&edge,static_cast<float>(x)-radius,static_cast<float>(y)-radius,diameter,diameter);
        const auto dot=static_cast<float>(px(10));g.FillEllipse(&inner,static_cast<float>(x)-dot/2,static_cast<float>(y)-dot/2,dot,dot);
        return CDRF_SKIPDEFAULT;
    }return CDRF_SKIPDEFAULT;
}
void SettingsDialog::refresh(){
    if(!capturing_)setText(hotkey_,hotkeyName(draft_.hotkey));
    if(SendMessageW(slider_,TBM_GETPOS,0,0)!=draft_.speed)SendMessageW(slider_,TBM_SETPOS,TRUE,draft_.speed);
    setText(speedLabel_,std::to_wstring(draft_.speed));
    const bool dirty=draft_.speed!=saved_.speed || draft_.hotkey!=saved_.hotkey;
    enable(apply_,!capturing_ && dirty);
    if(dirty_!=dirty){dirty_=dirty;RECT r{px(250),px(456),px(326),px(492)};InvalidateRect(window_,&r,FALSE);}
    const auto dc=GetDC(hotkey_);SIZE size{};const auto old=SelectObject(dc,font_);const auto label=windowText(hotkey_);
    GetTextExtentPoint32W(dc,label.c_str(),static_cast<int>(label.size()),&size);SelectObject(dc,old);ReleaseDC(hotkey_,dc);
    const auto left=392-std::clamp(MulDiv(size.cx,96,static_cast<int>(dpi_))+24,64,184);
    if(left!=keycapLeft_){
        keycapLeft_=left;SetWindowPos(hotkey_,nullptr,px(left+8),px(140),px(376-left),px(24),SWP_NOZORDER|SWP_NOACTIVATE);
        RECT r{px(208),px(134),px(392),px(170)};InvalidateRect(window_,&r,FALSE);
    }
    refreshNotice();
}
void SettingsDialog::refreshNotice(){
    if(!noticeControl_)return;
    std::wstring subtitle=L"按住切换，松开恢复";
    if(recoveryFailed_)subtitle=L"未能恢复鼠标速度";
    else if(capturing_)subtitle=L"松开完成 · Esc 取消";
    else if(!notice_.empty())subtitle=notice_;
    else if(waiting_)subtitle=L"请先松开快捷键，再重新按下";
    setText(noticeControl_,subtitle);
    RECT r{};GetClientRect(noticeControl_,&r);const auto width=px(recoveryFailed_?344:432);
    if(r.right!=width)SetWindowPos(noticeControl_,nullptr,0,0,width,px(38),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}
Config SettingsDialog::draft() const {auto value=draft_;value.enabled=SendMessageW(enabled_,BM_GETCHECK,0,0)==BST_CHECKED;return value;}
bool SettingsDialog::autostart() const{return SendMessageW(autostart_,BM_GETCHECK,0,0)==BST_CHECKED;}
void SettingsDialog::setAutostart(bool value,bool available){check(autostart_,value);enable(autostart_,available && !capturing_);}
void SettingsDialog::show(const Config& config){draft_=saved_=config;clearNotice();refresh();ShowWindow(window_,SW_SHOWNORMAL);ShowWindow(window_,SW_SHOWNORMAL);SetForegroundWindow(window_);}
void SettingsDialog::accepted(const Config& config){draft_=saved_=config;clearNotice();refresh();}
void SettingsDialog::status(const wchar_t* value,bool error){notice_=value;noticeError_=error;refreshNotice();InvalidateRect(noticeControl_,nullptr,FALSE);}
void SettingsDialog::clearNotice(){if(notice_.empty() && !noticeError_)return;notice_.clear();noticeError_=false;if(window_){refreshNotice();InvalidateRect(noticeControl_,nullptr,FALSE);}}
void SettingsDialog::runtime(bool enabled,bool active,bool waiting,bool failed,int actual,bool recoverable,bool accelerated){
    saved_.enabled=draft_.enabled=enabled;slowed_=active;waiting_=waiting;recoveryFailed_=failed;recoverable_=recoverable;actual_=actual;
    check(enabled_,enabled);
    setText(enabled_,active?(accelerated?L"加速中":L"减速中"):(enabled?L"已启用":L"已暂停"));
    const auto current=actual>=1 && actual<=20?L"系统当前 "+std::to_wstring(actual)+L" 级":L"无法读取系统档位";
    setText(current_,current);if(((GetWindowLongPtrW(retry_,GWL_STYLE)&WS_VISIBLE)!=0)!=failed)ShowWindow(retry_,failed?SW_SHOW:SW_HIDE);
    refreshNotice();
}
void SettingsDialog::recording(bool active,const Hotkey& keys){
    capturing_=active;
    setText(recordButton_,active?L"取消录制":L"更改快捷键");
    setText(hotkey_,active?(keys.empty()?L"请按快捷键…":hotkeyName(keys)):hotkeyName(draft_.hotkey));
    enable(enabled_,!active);enable(autostart_,!active);enable(slider_,!active);refresh();
}
void SettingsDialog::recorded(const Hotkey& keys){draft_.hotkey=keys;recording(false);}
void SettingsDialog::action(UiAction value){SendMessageW(owner_,settingsMessage,static_cast<WPARAM>(value),0);}
void SettingsDialog::help(bool shortcut){
    MessageBoxW(window_,shortcut?L"点击键帽或铅笔录制，全部松开后完成；Esc 取消。\n最多 4 个不同键，左右修饰键分别匹配。\n快捷键会透传；Caps Lock 仍会切换大小写。":
        L"Windows 指针速度档位：1–20，并非百分比。\n目标可高于或低于原速；松开恢复原速。\n只影响系统指针速度，部分软件的原始输入不受影响。",shortcut?L"快捷键":L"速度说明",MB_OK|MB_ICONINFORMATION);
}
void SettingsDialog::moreMenu(){
    const auto menu=CreatePopupMenu();if(!menu){status(L"无法打开菜单");return;}
    const bool ok=AppendMenuW(menu,MF_STRING,25,L"重置快捷键与速度") &&
        AppendMenuW(menu,MF_STRING|(!recoverable_?MF_GRAYED:0),34,L"恢复原速") &&
        AppendMenuW(menu,MF_STRING,35,L"诊断详情") && AppendMenuW(menu,MF_STRING,36,L"快捷键说明") &&
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr) && AppendMenuW(menu,MF_STRING,29,L"退出程序");
    RECT anchor{};GetWindowRect(GetDlgItem(window_,30),&anchor);SetForegroundWindow(window_);SetLastError(0);
    const auto choice=ok?TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_LEFTALIGN|TPM_BOTTOMALIGN,anchor.left,anchor.top-px(4),0,window_,nullptr):0;
    if(!ok || (!choice && GetLastError()))status(L"菜单操作失败");
    if(!DestroyMenu(menu))status(L"菜单关闭失败");
    if(choice)SendMessageW(window_,WM_COMMAND,choice,0);
}
void SettingsDialog::destroy(){
    if(window_ && IsWindow(window_))DestroyWindow(window_);window_=nullptr;children_.clear();
    for(const auto object:std::array<HGDIOBJ,7>{font_,smallFont_,titleFont_,numberFont_,backgroundBrush_,cardBrush_,keyBrush_})if(object)DeleteObject(object);
    font_=smallFont_=titleFont_=numberFont_=nullptr;backgroundBrush_=cardBrush_=keyBrush_=nullptr;
    if(graphicsToken_){Gdiplus::GdiplusShutdown(graphicsToken_);graphicsToken_=0;}
}
LRESULT CALLBACK SettingsDialog::controlProcedure(HWND hwnd,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto* self=reinterpret_cast<SettingsDialog*>(data);const auto id=GetDlgCtrlID(hwnd);
    wchar_t cls[32]{};GetClassNameW(hwnd,cls,32);const bool button=_wcsicmp(cls,L"Button")==0;
    if(hwnd==self->slider_ && message==TBM_SETPOS){
        // Preserve redraw=TRUE: the stock control also uses it to recalculate
        // thumb geometry. Native custom drawing is suppressed below; our full
        // buffered paint replaces its smaller invalidation area.
        const auto result=DefSubclassProc(hwnd,message,w,l);
        if(w)InvalidateRect(hwnd,nullptr,FALSE);
        return result;
    }
    if(message==WM_PAINT || message==WM_PRINTCLIENT){
        const auto draw=[&](HDC dc){
            if(button)self->paintButton(hwnd,dc);
            else if(hwnd==self->slider_){
                RECT area{};GetClientRect(hwnd,&area);FillRect(dc,&area,self->cardBrush_);
                NMCUSTOMDRAW part{};part.hdc=dc;part.dwDrawStage=CDDS_ITEMPREPAINT;part.dwItemSpec=TBCD_CHANNEL;self->drawSlider(&part);
                part.dwItemSpec=TBCD_THUMB;SendMessageW(hwnd,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&part.rc));self->drawSlider(&part);
                if(GetFocus()==hwnd){InflateRect(&area,-self->px(1),-self->px(1));DrawFocusRect(dc,&area);}
            }else self->paintLabel(hwnd,dc);
        };
        if(message==WM_PRINTCLIENT){draw(reinterpret_cast<HDC>(w));return 0;}
        if(hwnd==self->slider_)InvalidateRect(hwnd,nullptr,FALSE);
        PAINTSTRUCT paint{};const auto dc=BeginPaint(hwnd,&paint);
        {PaintBuffer buffer(hwnd,dc);draw(buffer.dc());}EndPaint(hwnd,&paint);return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_SETTEXT){
        // Stock controls may draw text immediately from WM_SETTEXT. Keep their
        // accessibility text, but route all visible rendering through WM_PAINT.
        DefSubclassProc(hwnd,WM_SETREDRAW,FALSE,0);
        const auto result=DefSubclassProc(hwnd,message,w,l);
        DefSubclassProc(hwnd,WM_SETREDRAW,TRUE,0);InvalidateRect(hwnd,nullptr,FALSE);return result;
    }
    if(message==WM_MOUSEMOVE && button){
        if(self->hovered_!=hwnd){if(self->hovered_)InvalidateRect(self->hovered_,nullptr,FALSE);self->hovered_=hwnd;InvalidateRect(hwnd,nullptr,FALSE);TRACKMOUSEEVENT track{sizeof track,TME_LEAVE,hwnd,0};TrackMouseEvent(&track);}
    }
    if(message==WM_MOUSELEAVE && self->hovered_==hwnd){self->hovered_=nullptr;InvalidateRect(hwnd,nullptr,FALSE);}
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE)InvalidateRect(hwnd,nullptr,FALSE);
    if(id==20 && (message==WM_LBUTTONUP || (message==WM_KEYDOWN && (w==VK_RETURN || w==VK_SPACE)))){SendMessageW(self->window_,WM_COMMAND,21,0);return 0;}
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,controlProcedure,1);
    const auto result=DefSubclassProc(hwnd,message,w,l);
    if(hwnd==self->slider_ && (message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP ||
        message==WM_KEYDOWN || message==WM_KEYUP || message==WM_MOUSEWHEEL || message==WM_CAPTURECHANGED))InvalidateRect(hwnd,nullptr,FALSE);
    return result;
}
LRESULT CALLBACK SettingsDialog::procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    auto* self=reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<SettingsDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window_=hwnd;}
    if(self){try{return self->message(message,w,l);}catch(...){self->status(L"界面操作失败，请重试");return 0;}}
    return DefWindowProcW(hwnd,message,w,l);
}
LRESULT SettingsDialog::message(UINT message,WPARAM w,LPARAM l){
    if(message==WM_PAINT){PAINTSTRUCT p{};const auto dc=BeginPaint(window_,&p);{PaintBuffer buffer(window_,dc);paint(buffer.dc());}EndPaint(window_,&p);return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_DPICHANGED){const auto suggested=reinterpret_cast<RECT*>(l);layout(HIWORD(w));if(suggested)SetWindowPos(window_,nullptr,suggested->left,suggested->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
    if(message==WM_SETTINGCHANGE || message==WM_THEMECHANGED || message==WM_SYSCOLORCHANGE){theme();return 0;}
    if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT){
        const auto dc=reinterpret_cast<HDC>(w);const auto child=reinterpret_cast<HWND>(l);
        if(child==noticeControl_){SetBkColor(dc,background_);SetTextColor(dc,(recoveryFailed_||noticeError_)?(dark_?RGB(255,185,100):RGB(154,76,0)):secondary_);return reinterpret_cast<LRESULT>(backgroundBrush_);}
        const auto back=child==hotkey_?keyBackground_:card_;SetBkColor(dc,back);SetTextColor(dc,child==current_?secondary_:foreground_);
        return reinterpret_cast<LRESULT>(child==hotkey_?keyBrush_:cardBrush_);
    }
    // A single buffered WM_PAINT path owns the slider pixels. Prevent the stock
    // trackbar from mixing immediate/custom drawing into the same surface.
    if(message==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR*>(l);if(header->hwndFrom==slider_ && header->code==NM_CUSTOMDRAW)return CDRF_SKIPDEFAULT;}
    if(message==WM_HSCROLL && reinterpret_cast<HWND>(l)==slider_){draft_.speed=static_cast<int>(SendMessageW(slider_,TBM_GETPOS,0,0));clearNotice();refresh();return 0;}
    if(message==WM_CLOSE){action(UiAction::CancelRecord);ShowWindow(window_,SW_HIDE);return 0;}
    if(message==WM_COMMAND){
        switch(LOWORD(w)){
        case 20:return 0;
        case 21:action(capturing_?UiAction::CancelRecord:UiAction::Record);return 0;
        case 23:action(UiAction::ToggleEnabled);return 0;
        case 24:action(UiAction::ToggleAutostart);return 0;
        case 25:draft_.hotkey=Config{}.hotkey;draft_.speed=4;clearNotice();refresh();return 0;
        case 26:action(UiAction::Apply);return 0;
        case 27:SendMessageW(window_,WM_CLOSE,0,0);return 0;
        case 28:action(UiAction::Retry);return 0;
        case 29:action(UiAction::Exit);return 0;
        case 30:moreMenu();return 0;
        case 31:help(false);return 0;
        case 34:action(UiAction::Restore);return 0;
        case 35:action(UiAction::Diagnostics);return 0;
        case 36:help(true);return 0;
        }
    }
    return DefWindowProcW(window_,message,w,l);
}
}
