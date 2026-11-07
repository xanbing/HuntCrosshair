// Real, modeless native PropertySheet tests. This translation unit substitutes
// only persistence/editor/foreground boundaries and never initializes runtime
// hooks, global hotkeys, tray icons or the actual overlay.
#include "platform.h"
#include "pixel_editor.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>

namespace native_fixture {
int saves=0,editorCalls=0,dismissedBoxes=0,checks=0;
bool saveSuccess=true,editorAccept=true,dismissErrors=false;
hc::Settings saved;
hc::PixelArt editedArt;
struct Registration {bool active=false;UINT modifiers=0,key=0;};
std::array<Registration,7> registrations{};
UINT deniedKey=0;
int registrationCalls=0,unregistrationCalls=0;
}
namespace hc {
bool FixtureSaveSettings(const Settings& value){++native_fixture::saves;native_fixture::saved=value;return native_fixture::saveSuccess;}
bool FixtureEditPixelArt(HWND,HINSTANCE,PixelArt& art,Color,const wchar_t*){
    ++native_fixture::editorCalls;
    if(native_fixture::editorAccept)art=native_fixture::editedArt;
    return native_fixture::editorAccept;
}
bool FixtureGameIsForeground(){return false;}
}
int WINAPI FixtureMessageBoxW(HWND,LPCWSTR,LPCWSTR,UINT){++native_fixture::dismissedBoxes;return IDOK;}
BOOL WINAPI FixtureRegisterHotKey(HWND,int id,UINT modifiers,UINT key){
    using namespace native_fixture;++registrationCalls;
    if(id<1||id>7||key==deniedKey){SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);return FALSE;}
    for(int i=0;i<7;++i)if(i!=id-1&&registrations[i].active&&registrations[i].key==key&&
        (registrations[i].modifiers&7)==(modifiers&7)){SetLastError(ERROR_HOTKEY_ALREADY_REGISTERED);return FALSE;}
    registrations[id-1]={true,modifiers,key};return TRUE;
}
BOOL WINAPI FixtureUnregisterHotKey(HWND,int id){
    using namespace native_fixture;++unregistrationCalls;if(id<1||id>7)return FALSE;
    bool registered=registrations[id-1].active;registrations[id-1]={};return registered?TRUE:FALSE;
}
#define MessageBoxW FixtureMessageBoxW
#define RegisterHotKey FixtureRegisterHotKey
#define UnregisterHotKey FixtureUnregisterHotKey
#define SaveSettings FixtureSaveSettings
#define EditPixelArt FixtureEditPixelArt
#define GameIsForeground FixtureGameIsForeground
#define wWinMain NativeFixtureApplicationMain
#include "../src/main.cpp"
#undef wWinMain
#undef GameIsForeground
#undef EditPixelArt
#undef SaveSettings
#undef MessageBoxW
#undef RegisterHotKey
#undef UnregisterHotKey

namespace native_fixture {
HHOOK windowHook=nullptr;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::wstring ClassOf(HWND hwnd){wchar_t text[128]{};GetClassNameW(hwnd,text,128);return text;}
LRESULT CALLBACK OffscreenWindows(int code,WPARAM wp,LPARAM lp){
    if(code==HCBT_MOVESIZE&&(HWND)wp==gMain&&((RECT*)lp)->left>-10000)return 1;
    if(code==HCBT_CREATEWND){
        auto* create=reinterpret_cast<CBT_CREATEWNDW*>(lp);
        if(!(create->lpcs->style&WS_CHILD)){
            create->lpcs->x=-20000;create->lpcs->y=-20000;
            create->lpcs->dwExStyle|=WS_EX_NOACTIVATE;
            if(dismissErrors&&create->lpcs->hwndParent==gMain&&ClassOf((HWND)wp)==L"#32770"){
                ++dismissedBoxes;PostMessageW((HWND)wp,WM_COMMAND,IDOK,0);
            }
        }
    }
    if(code==HCBT_ACTIVATE){
        HWND hwnd=(HWND)wp;
        SetWindowPos(hwnd,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(dismissErrors&&hwnd!=gMain&&GetWindow(hwnd,GW_OWNER)==gMain&&ClassOf(hwnd)==L"#32770")
            PostMessageW(hwnd,WM_COMMAND,IDOK,0);
        return 1; // Own test windows must not steal the user's keyboard focus.
    }
    return CallNextHookEx(windowHook,code,wp,lp);
}
void Pump(bool closeFinished=true){
    MSG message{};int limit=10000;
    while(limit--&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
        if(message.message==WM_QUIT)continue;
        if(!HandleSettingsKey(message)&&(!IsWindow(gMain)||!PropSheet_IsDialogMessage(gMain,&message))){
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(closeFinished)CloseFinishedSheet();
    }
    Check(limit>0,"native message queue did not become idle");
}
void WaitAndPump(DWORD milliseconds){
    ULONGLONG deadline=GetTickCount64()+milliseconds;
    do{Pump();Sleep(5);}while(GetTickCount64()<deadline);
}
hc::Settings Baseline(){
    hc::Settings s;s.monitor=0;s.enabled=true;s.dot=false;s.offsetStep=1;
    s.color={31,193,127};s.dotColor={251,114,47};s.crossSize=81;s.dotArtSize=23;
    s.easing=4;s.x1=.1234567890123456;s.y1=.9876543210987654;
    s.x2=.4567890123456789;s.y2=.6543210987654321;
    s.crossArt.width=3;s.crossArt.height=2;s.crossArt.pixels={0,37,255,128,17,0};
    s.dotArt.width=2;s.dotArt.height=3;s.dotArt.pixels={255,0,64,128,0,17};
    return s;
}
struct Environment {
    Environment(){
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_HOTKEY_CLASS};
        Check(InitCommonControlsEx(&common)!=FALSE,"common controls initialization failed");
        windowHook=SetWindowsHookExW(WH_CBT,OffscreenWindows,nullptr,GetCurrentThreadId());
        Check(windowHook!=nullptr,"offscreen test hook failed");
        Check(RegisterNativeClasses(GetModuleHandleW(nullptr)),"native class registration failed");
        gHost=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,HostClass,L"Native UI fixture host",
            WS_OVERLAPPED,-20000,-20000,0,0,nullptr,nullptr,gInstance,nullptr);
        Check(IsWindow(gHost),"hidden host creation failed");
        Check(!overlay.Window(),"test unexpectedly initialized actual overlay");
        session.Begin(Baseline());saves=editorCalls=dismissedBoxes=0;saveSuccess=true;
        registrations={};deniedKey=0;registrationCalls=unregistrationCalls=0;
    }
    ~Environment(){
        StopDemo();if(IsWindow(gMain))DestroyWindow(gMain);
        if(IsWindow(gHost))DestroyWindow(gHost);gHost=nullptr;
        if(windowHook)UnhookWindowsHookEx(windowHook);windowHook=nullptr;
        MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){}
    }
};
HWND Open(){
    HWND sheet=CreateSettings();Check(IsWindow(sheet),"real PropertySheet creation failed");
    SetWindowPos(sheet,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(sheet,SW_SHOWNOACTIVATE);DockPreview();Pump();return sheet;
}
void Edit(int id,const wchar_t* text){
    HWND field=C(id);Check(IsWindow(field)&&ClassOf(field)==L"Edit","numeric native Edit control missing");
    SetWindowTextW(field,text);Pump();
}
void Choose(int id,int index){
    HWND combo=C(id);Check(IsWindow(combo)&&ClassOf(combo)==L"ComboBox","native ComboBox missing");
    Check(SendMessageW(combo,CB_SETCURSEL,index,0)!=CB_ERR,"combo selection failed");
    SendMessageW(GetParent(combo),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),(LPARAM)combo);Pump();
}
void Click(int id){HWND button=C(id);Check(IsWindow(button),"button missing");SendMessageW(button,BM_CLICK,0,0);Pump();}
BOOL CALLBACK CheckNativeButtons(HWND hwnd,LPARAM){
    if(ClassOf(hwnd)==L"Button")Check((GetWindowLongPtrW(hwnd,GWL_STYLE)&BS_TYPEMASK)!=BS_OWNERDRAW,
        "a settings button still uses owner drawing");
    return TRUE;
}
std::wstring WindowText(HWND hwnd){wchar_t text[256]{};GetWindowTextW(hwnd,text,256);return text;}
std::wstring ComboText(HWND combo,int index){wchar_t text[128]{};Check(SendMessageW(combo,CB_GETLBTEXTLEN,index,0)<128,
    "combo item text exceeds fixture buffer");Check(SendMessageW(combo,CB_GETLBTEXT,index,(LPARAM)text)!=CB_ERR,"combo item text missing");return text;}
RECT VisibleWindowBounds(HWND window){
    RECT outer{};GetWindowRect(window,&outer);HRGN region=CreateRectRgn(0,0,0,0);RECT box{};
    if(region&&GetWindowRgn(window,region)!=ERROR&&GetRgnBox(region,&box)!=NULLREGION){
        box.left+=outer.left;box.right+=outer.left;box.top+=outer.top;box.bottom+=outer.top;outer=box;
    }
    if(region)DeleteObject(region);return outer;
}
void CheckWindowRegion(HWND window,bool retro){
    HRGN region=CreateRectRgn(0,0,0,0);Check(region!=nullptr,"window-region fixture allocation failed");
    const int kind=GetWindowRgn(window,region);
    if(retro){RECT outer{},box{},client{};POINT origin{};GetWindowRect(window,&outer);GetRgnBox(region,&box);GetClientRect(window,&client);ClientToScreen(window,&origin);
        Check(kind!=ERROR&&kind!=NULLREGION&&box.left>=0&&box.top>=0&&box.right<=outer.right-outer.left&&box.bottom<=outer.bottom-outer.top,
              "retro window lacks a valid trimmed native-frame region");
        HRGN clientRegion=CreateRectRgn(origin.x-outer.left,origin.y-outer.top,origin.x-outer.left+client.right,origin.y-outer.top+client.bottom);
        HRGN clipped=CreateRectRgn(0,0,0,0);Check(clientRegion&&clipped,"client-region fixture allocation failed");
        Check(CombineRgn(clipped,clientRegion,region,RGN_DIFF)==NULLREGION,"retro region clipped actual native client content");
        DeleteObject(clientRegion);DeleteObject(clipped);
    }else Check(kind==ERROR,"default skin retained the application's retro clipping region");
    DeleteObject(region);
}
void CheckNativeChromeAndLayout(){
    Check(!(GetWindowLongPtrW(gMain,GWL_EXSTYLE)&WS_EX_CONTEXTHELP),"title context-help question mark is still enabled");
    RECT client{};GetClientRect(gMain,&client);
    for(HWND child:{PropSheet_GetTabControl(gMain),PropSheet_GetCurrentPageHwnd(gMain),GetDlgItem(gMain,IDOK),GetDlgItem(gMain,IDCANCEL)}){
        RECT r{};Check(IsWindow(child)&&GetWindowRect(child,&r),"native layout control missing");MapWindowPoints(nullptr,gMain,(POINT*)&r,2);
        Check(r.left>=client.left&&r.top>=client.top&&r.right<=client.right&&r.bottom<=client.bottom,
              "skin change cropped a native page/tab/footer outside the client area");
    }
    RECT main=VisibleWindowBounds(gMain),preview{},page{};GetWindowRect(gPreview,&preview);GetWindowRect(pages[0],&page);
    Check(preview.left==main.right&&preview.top==page.top&&preview.right-preview.left==240&&preview.bottom-preview.top==240,
          "skin change broke external square preview docking");
}
void FlushDebounce(){SendMessageW(gHost,WM_TIMER,T_SAVE,0);Pump();}
void Reopen(){ShowSettings();SetWindowPos(gMain,nullptr,-20000,-20000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);DockPreview();Pump();}
void Tray(){HWND original=gMain;SendMessageW(gMain,WM_COMMAND,MAKEWPARAM(IDCANCEL,BN_CLICKED),(LPARAM)GetDlgItem(gMain,IDCANCEL));Pump();
    Check(IsWindow(original)&&gMain==original&&!IsWindowVisible(gMain)&&!IsWindowVisible(gPreview)&&IsWindow(gHost),"Tray destroyed settings/host or left preview visible");}
int previewCount=0;
BOOL CALLBACK CountPreviews(HWND hwnd,LPARAM){if(ClassOf(hwnd)==PreviewClass)++previewCount;return TRUE;}
void TestStructureAndDock(){
    std::cerr<<"native: narrow structure/external preview\n";Open();
    Check(GetDpiForWindow(gMain)==96,"fixture is not 100 percent DPI");
    RECT main{},preview{},previewClient{};GetWindowRect(gMain,&main);GetWindowRect(gPreview,&preview);GetClientRect(gPreview,&previewClient);
    Check(main.right-main.left>=405&&main.right-main.left<=430&&main.bottom-main.top==668,"main did not retain narrow width/original height");
    Check(IsWindow(gPreview)&&!IsChild(gMain,gPreview)&&GetWindow(gPreview,GW_OWNER)==gMain,"preview is not one external owned window");
    Check(preview.right-preview.left==preview.bottom-preview.top&&previewClient.right==previewClient.bottom,"external preview is not fixed square");
    Check((GetWindowLongPtrW(gPreview,GWL_EXSTYLE)&WS_EX_CLIENTEDGE)&&previewClient.right==234&&previewClient.bottom==234,
          "preview does not have the requested native three-pixel frame");
    Check(preview.left==VisibleWindowBounds(gMain).right,"external preview is not docked to visible right edge");
    std::cout<<"native dimensions: main "<<main.right-main.left<<'x'<<main.bottom-main.top
             <<", preview "<<preview.right-preview.left<<'x'<<preview.bottom-preview.top<<'\n';
    const int dx=73,dy=41;
    Check(SetWindowPos(gMain,nullptr,main.left+dx,main.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
          "owner move failed");Pump();RECT movedMain{},movedPreview{};GetWindowRect(gMain,&movedMain);GetWindowRect(gPreview,&movedPreview);
    Check(movedMain.left==main.left+dx&&movedMain.top==main.top+dy,"owner move was not applied");
    Check(movedPreview.left==preview.left+dx&&movedPreview.top==preview.top+dy&&
          movedPreview.right-movedPreview.left==movedPreview.bottom-movedPreview.top,
          "external square preview did not follow owner movement");
    Check(SetWindowPos(gMain,nullptr,main.left,main.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
          "owner move restore failed");Pump();GetWindowRect(gPreview,&movedPreview);
    Check(EqualRect(&preview,&movedPreview)!=FALSE,"external preview did not restore its dock after owner move");
    HWND tab=PropSheet_GetTabControl(gMain);
    Check(ClassOf(tab)==L"SysTabControl32"&&TabCtrl_GetItemCount(tab)==6,"six system tabs missing");
    for(HWND page:pages)Check(IsWindow(page)&&ClassOf(page)==L"#32770","resource page missing");
    EnumChildWindows(gMain,CheckNativeButtons,0);
    Check(WindowText(GetDlgItem(gMain,IDOK))==L"退出"&&WindowText(GetDlgItem(gMain,IDCANCEL))==L"最小化到托盘","footer still exposes OK/Cancel semantics");
    Check(!IsWindowVisible(GetDlgItem(gMain,IDC_PROPERTY_APPLY)),"Apply button is still visible");
    for(int id=IDC_CURVE_X1;id<=IDC_CURVE_Y2;++id)Check(!C(id),"Bezier numeric input was not removed");
    for(int id:{IDC_CROSS_PREVIEW,IDC_DOT_PREVIEW,IDC_ANIMATION_PREVIEW})Check(!C(id),"in-page preview was not removed");
    Check(ClassOf(C(IDC_CROSS_SWATCH))==SwatchClass&&ClassOf(C(IDC_DOT_SWATCH))==SwatchClass,"color rectangle swatches missing");
    Check(bindings.size()==12&&IsWindow(C(IDC_CURVE_GRAPH)),"native bindings/graph missing");
    HWND simulate=C(IDC_SIMULATE);Check(IsWindow(simulate)&&GetParent(simulate)==gPreview&&ClassOf(simulate)==L"Button"&&WindowText(simulate)==L"右键",
          "right-button simulation is not the small native preview button");
    const LONG_PTR simulateStyle=GetWindowLongPtrW(simulate,GWL_STYLE);
    Check((simulateStyle&BS_TYPEMASK)==BS_AUTOCHECKBOX&&(simulateStyle&BS_PUSHLIKE),"preview simulation is not a native checkable pushlike button");
    RECT button{};GetWindowRect(simulate,&button);MapWindowPoints(nullptr,gPreview,(POINT*)&button,2);
    Check(button.right-button.left==56&&button.bottom-button.top==28&&previewClient.right-button.right==8&&previewClient.bottom-button.bottom==8,
          "preview simulation button size or bottom-right margin changed");
    for(HWND page:pages)Check(!GetDlgItem(page,IDC_SIMULATE),"old animation-page simulation checkbox still exists");
    CheckNativeChromeAndLayout();
    Check(GetParent(C(IDC_OFFSET_X))==pages[3]&&GetParent(C(IDC_HOTKEY_BASE))==pages[4]&&
          GetParent(C(IDC_SKIN))==pages[5]&&GetParent(C(IDC_RESET_DEFAULTS))==pages[5],"position/shortcut/settings controls are not on separate pages");
    for(int i=0;i<7;++i)Check(ClassOf(C(IDC_HOTKEY_BASE+i))==L"msctls_hotkey32"&&
        ClassOf(C(IDC_HOTKEY_CLEAR_BASE+i))==L"Button","shortcut field/clear is not native");
    for(int page=0;page<6;++page){PropSheet_SetCurSel(gMain,nullptr,page);Pump();RECT r;GetWindowRect(gPreview,&r);
        Check(EqualRect(&r,&preview)!=FALSE,"preview moved/resized on page selection");}
    previewCount=0;EnumThreadWindows(GetCurrentThreadId(),CountPreviews,0);Check(previewCount==1,"there is more than one external preview");
}
uint64_t PreviewPixels(){
    RECT client{};GetClientRect(gPreview,&client);HDC dc=CreateCompatibleDC(nullptr);Check(dc!=nullptr,"preview fixture DC allocation failed");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=client.right;info.bmiHeader.biHeight=-client.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);Check(bitmap&&bits,"preview fixture bitmap allocation failed");
    auto old=SelectObject(dc,bitmap);PaintPreview(gPreview,dc);GdiFlush();uint64_t hash=1469598103934665603ULL;
    for(size_t i=0;i<(size_t)client.right*client.bottom;i++){hash^=static_cast<uint32_t*>(bits)[i];hash*=1099511628211ULL;}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return hash;
}
void TestPreviewSimulation(){
    std::cerr<<"native: external preview right-button simulation\n";
    const auto prior=cfg;auto shown=cfg;shown.customCrosshair=false;shown.crossSize=0;shown.length=16;shown.gap=6;shown.thickness=2;
    shown.dot=true;shown.dotShape=0;shown.dotSize=3;shown.action=2;shown.enterMs=shown.leaveMs=0;shown.offsetX=shown.offsetY=0;
    session.Begin(shown);testActive=rightActive=false;animation.Reset();SyncControls();Target();
    for(int page=0;page<6;++page){PropSheet_SetCurSel(gMain,nullptr,page);Pump();
        const auto idle=PreviewPixels();Click(IDC_SIMULATE);
        Check(testActive&&SendMessageW(C(IDC_SIMULATE),BM_GETCHECK,0,0)==BST_CHECKED,"preview button did not enable right-button simulation");
        const auto active=PreviewPixels();if(page!=1)Check(active!=idle,"right-button simulation did not change the preview on this settings page");
        Click(IDC_SIMULATE);Check(!testActive&&SendMessageW(C(IDC_SIMULATE),BM_GETCHECK,0,0)==BST_UNCHECKED&&PreviewPixels()==idle,
            "preview button did not restore the idle crosshair image/state");
    }
    Click(IDC_SIMULATE);Tray();Check(!testActive&&SendMessageW(C(IDC_SIMULATE),BM_GETCHECK,0,0)==BST_UNCHECKED,
        "tray hide left preview right-button simulation checked");Reopen();
    session.Begin(prior);testActive=rightActive=false;animation.Reset();SyncControls();Target();
}
void TestRealtime(){
    std::cerr<<"native: realtime controls/debounce\n";
    int previous=saves;Edit(IDC_LENGTH,L"33");Edit(IDC_GAP,L"9");
    Check(cfg.length==33&&cfg.gap==9&&saves==previous,"edit did not become live immediately or wrote without debounce");
    WaitAndPump(100);Check(saves==previous,"300ms debounce saved too early");
    WaitAndPump(350);Check(saves==previous+1&&!session.HasChanges()&&saved.length==33&&saved.gap==9,"debounce did not coalesce/save live state");
    Edit(IDC_THICKNESS,L"4");Edit(IDC_CROSS_SIZE,L"143");Edit(IDC_OPACITY,L"73");Edit(IDC_ANGLE,L"45");
    Edit(IDC_DOT_SIZE,L"11");Edit(IDC_DOT_OPACITY,L"62");Edit(IDC_ENTER_DURATION,L"420");Edit(IDC_LEAVE_DURATION,L"680");
    Edit(IDC_OFFSET_X,L"-0.0625");Edit(IDC_OFFSET_Y,L"0.25");
    Check(cfg.thickness==4&&cfg.crossSize==143&&cfg.opacity==.73&&cfg.angle==45&&cfg.dotSize==11&&cfg.dotOpacity==.62&&
          cfg.enterMs==420&&cfg.leaveMs==680&&cfg.offsetX==-.0625&&cfg.offsetY==.25,"live numeric routing failed");
    const auto own=cfg;
    for(int kind:{0,1,2,14,15,4}){SelectCurve(kind);HandleCommand(IDC_EASING,CBN_SELCHANGE);Pump();
        Check(cfg.easing==kind&&SelectedCurve()==kind,"preset itemdata routing failed");
        Check(cfg.x1==own.x1&&cfg.y1==own.y1&&cfg.x2==own.x2&&cfg.y2==own.y2,"preset overwrote full-precision custom points");}
    Check(SendMessageW(C(IDC_EASING),CB_GETCOUNT,0,0)==6,"curve choices changed");FlushDebounce();
    Check(!session.HasChanges()&&hc::SettingsEqual(cfg,saved),"live control/preset values were not saved together");
    UpdateCurvePoint(0,.321,.876,false);Check(cfg.easing==4&&cfg.x1==.321&&cfg.y1==.876,"graph editing did not update live curve");
    cfg.color={3,7,11};cfg.dotColor={17,23,29};SyncControls();Changed();FlushDebounce();
    Check(saved.color.r==3&&saved.dotColor.b==29&&saved.x1==.321,"live graph/color changes were not persisted");
}
void TestPixelsAndFailures(){
    std::cerr<<"native: live pixel masks/save retry\n";
    const bool dotWasEnabled=cfg.dot;editedArt.width=2;editedArt.height=3;editedArt.pixels={1,37,255,128,0,64};editorAccept=true;
    Click(IDC_EDIT_CROSS);Check(cfg.customCrosshair&&cfg.crossArt.pixels==editedArt.pixels,"crosshair pixel apply was not live");
    Click(IDC_EDIT_DOT);Check(cfg.dotShape==3&&cfg.dotArt.pixels==editedArt.pixels&&cfg.dot==dotWasEnabled,"point apply changed mask or normal visibility");
    Check(!IsWindowEnabled(C(IDC_LENGTH))&&!IsWindowEnabled(C(IDC_GAP))&&!IsWindowEnabled(C(IDC_THICKNESS)),"custom art still exposes native geometry fields");
    double nativeSize=cfg.dotSize;Edit(IDC_DOT_SIZE,L"91");Check(cfg.dotArtSize==91&&cfg.dotSize==nativeSize,"custom point size rebound incorrectly");
    Choose(IDC_DOT_SHAPE,1);Edit(IDC_DOT_SIZE,L"17");Choose(IDC_DOT_SHAPE,3);
    Check(cfg.dotArtSize==91&&cfg.dotSize==17,"native/custom point sizes were not independent");
    Choose(IDC_CROSS_MODE,0);Check(IsWindowEnabled(C(IDC_LENGTH)),"geometry fields did not re-enable");Choose(IDC_CROSS_MODE,1);FlushDebounce();
    const auto before=cfg;editorAccept=false;Click(IDC_EDIT_CROSS);Check(hc::SettingsEqual(cfg,before),"cancelled pixel dialog changed live mask");editorAccept=true;
    saveSuccess=false;Edit(IDC_OPACITY,L"41");const auto baseline=session.Applied();FlushDebounce();
    Check(!savedOK&&cfg.opacity==.41&&session.HasChanges()&&hc::SettingsEqual(session.Applied(),baseline),"failed save reverted realtime data or changed saved baseline");
    Check(WindowText(gMain).find(L"保存失败")!=std::wstring::npos,"save failure is not visible across all tabs");
    Edit(IDC_OPACITY,Number(baseline.opacity*100,0).c_str());const int failedSaves=saves;FlushDebounce();
    Check(savedOK&&!session.HasChanges()&&hc::SettingsEqual(cfg,baseline)&&saves==failedSaves&&
          WindowText(gMain)==L"准星属性","reverting to the saved baseline retained a stale save error or rewrote settings");
    Edit(IDC_OPACITY,L"41");FlushDebounce();Check(!savedOK&&session.HasChanges(),"repeat failure fixture did not retain its live edit");
    saveSuccess=true;FlushDebounce();Check(savedOK&&!session.HasChanges()&&saved.opacity==.41,"realtime failed-save retry lost pending values");
}
void TestTrayCloseAndKeys(){
    std::cerr<<"native: tray/X/Escape/Enter\n";
    Edit(IDC_ANGLE,L"67");const auto live=cfg;int previous=saves;Tray();
    Check(hc::SettingsEqual(cfg,live)&&hc::SettingsEqual(saved,live)&&saves==previous+1,"Tray reverted or did not flush latest live edit");
    HWND retained=gMain;Reopen();Check(gMain==retained&&IsWindowVisible(gMain)&&IsWindowVisible(gPreview),"Tray reopen did not restore same sheet/preview");
    Edit(IDC_CROSS_SIZE,L"111");SendMessageW(gMain,WM_CLOSE,0,0);Pump();
    Check(IsWindow(gMain)&&!IsWindowVisible(gMain)&&!IsWindowVisible(gPreview)&&saved.crossSize==111,"X destroyed/reverted settings instead of tray");
    Reopen();PropSheet_SetCurSel(gMain,nullptr,3);Pump();HWND field=C(IDC_OFFSET_X);SetFocus(field);
    MSG enter{};enter.hwnd=field;enter.message=WM_KEYDOWN;enter.wParam=VK_RETURN;
    Check(HandleSettingsKey(enter),"numeric Enter was not intercepted before native default Exit");Pump();
    Check(IsWindow(gHost)&&IsWindowVisible(gMain),"Enter in numeric field exited or hid settings");
    PropSheet_SetCurSel(gMain,nullptr,2);Pump();SetFocus(C(IDC_CURVE_GRAPH));enter.hwnd=C(IDC_CURVE_GRAPH);
    Check(HandleSettingsKey(enter),"graph Enter was not intercepted");Pump();Check(IsWindow(gHost)&&IsWindowVisible(gMain),"graph Enter exited app");
    MSG escape{};escape.hwnd=gMain;escape.message=WM_KEYDOWN;escape.wParam=VK_ESCAPE;
    Check(HandleSettingsKey(escape),"Escape was not intercepted");Pump();
    Check(IsWindow(gMain)&&!IsWindowVisible(gMain)&&!IsWindowVisible(gPreview),"Escape did not minimize both windows to tray");Reopen();
}
void RecordHotkey(int index,int modifiers,int key){
    const int flags=((modifiers&MOD_CONTROL)?HOTKEYF_CONTROL:0)|((modifiers&MOD_SHIFT)?HOTKEYF_SHIFT:0)|
        ((modifiers&MOD_ALT)?HOTKEYF_ALT:0)|((key>=VK_LEFT&&key<=VK_DOWN)?HOTKEYF_EXT:0);
    bool before=syncing;syncing=true;SendMessageW(C(IDC_HOTKEY_BASE+index),HKM_SETHOTKEY,MAKEWORD(key,flags),0);syncing=before;
    HandleCommand(IDC_HOTKEY_BASE+index,EN_CHANGE);Pump();
}
int RegisteredCount(){int count=0;for(const auto& value:registrations)if(value.active)++count;return count;}
void TestCustomHotkeysAndSkins(){
    std::cerr<<"native: custom global hotkeys/capture/native skins\n";
    PropSheet_SetCurSel(gMain,nullptr,4);Pump();Click(IDC_RESET_HOTKEYS);FlushDebounce();
    const auto defaults=cfg.hotkeys;runtimeInitialized=true;hotkeyCapture=false;RebindHotkeys();
    Check(RegisteredCount()==7&&hotkeysOK,"default custom global hotkeys did not register");
    RecordHotkey(0,MOD_CONTROL|MOD_SHIFT,VK_F8);
    Check(cfg.hotkeys[0].key==VK_F8&&cfg.hotkeys[0].modifiers==(MOD_CONTROL|MOD_SHIFT)&&
          registrations[0].key==VK_F8,"hotkey edit did not apply/register live");
    const bool wasEnabled=cfg.enabled;
    SendMessageW(gHost,WM_HOTKEY,1,MAKELPARAM(defaults[0].modifiers,defaults[0].key));Pump();
    Check(cfg.enabled==wasEnabled,"obsolete hotkey message still triggered after rebinding");
    SendMessageW(gHost,WM_HOTKEY,1,MAKELPARAM(MOD_CONTROL|MOD_SHIFT,VK_F8));Pump();
    Check(cfg.enabled!=wasEnabled,"newly rebound global hotkey did not trigger its action");
    SendMessageW(gHost,WM_HOTKEY,1,MAKELPARAM(MOD_CONTROL|MOD_SHIFT,VK_F8));Pump();
    const auto unchanged=cfg.hotkeys[1];RecordHotkey(1,MOD_CONTROL|MOD_SHIFT,VK_F8);
    Check(cfg.hotkeys[1].key==unchanged.key&&cfg.hotkeys[1].modifiers==unchanged.modifiers,
          "duplicate hotkey overwrote a valid binding");
    Click(IDC_HOTKEY_CLEAR_BASE+2);Check(cfg.hotkeys[2].key==0&&cfg.hotkeys[2].modifiers==0&&!registrations[2].active,
          "clear did not disable/unregister the selected shortcut");
    Click(IDC_RESET_HOTKEYS);for(int i=0;i<7;++i)Check(cfg.hotkeys[i].key==defaults[i].key&&
        cfg.hotkeys[i].modifiers==defaults[i].modifiers,"restore shortcuts changed a default binding");
    Check(RegisteredCount()==7,"restore shortcuts did not register all enabled defaults");
    HWND recorder=C(IDC_HOTKEY_BASE);SetFocus(recorder);SendMessageW(recorder,WM_SETFOCUS,0,0);Pump();
    Check(hotkeyCapture&&RegisteredCount()==0,"recording did not suspend global hotkeys");
    RecordHotkey(0,MOD_CONTROL|MOD_SHIFT,VK_F10);
    Check(cfg.hotkeys[0].key==VK_F10&&RegisteredCount()==0,"editing while recording re-enabled a global shortcut");
    MSG key{};key.hwnd=recorder;key.message=WM_KEYDOWN;key.wParam=VK_ESCAPE;
    Check(!HandleSettingsKey(key)&&IsWindowVisible(gMain),"shortcut recorder Escape was consumed as minimize-to-tray");
    key.wParam=VK_RETURN;Check(!HandleSettingsKey(key),"shortcut recorder Enter was consumed as dialog Exit");
    SetFocus(C(IDC_RESET_HOTKEYS));SendMessageW(recorder,WM_KILLFOCUS,(WPARAM)C(IDC_RESET_HOTKEYS),0);Pump();FinishHotkeyCapture();
    Check(!hotkeyCapture&&RegisteredCount()==7&&registrations[0].key==VK_F10,"leaving recorder did not restore edited registrations");
    deniedKey=VK_F9;RecordHotkey(0,MOD_CONTROL|MOD_SHIFT,VK_F9);
    Check(!hotkeysOK&&hotkeyFailed[0]&&!registrations[0].active&&cfg.hotkeys[0].key==VK_F9,
          "occupied global shortcut was not retained/reported as unavailable");
    FlushDebounce();Check(saved.hotkeys[0].key==VK_F9,"custom hotkey was not persisted with live settings");
    deniedKey=0;RebindHotkeys();Check(hotkeysOK&&RegisteredCount()==7,"shortcut registration did not recover after conflict cleared");
    SuspendHotkeys();runtimeInitialized=false;hotkeyCapture=false;
    PropSheet_SetCurSel(gMain,nullptr,5);Pump();Check(SendMessageW(C(IDC_SKIN),CB_GETCOUNT,0,0)==2,"skin presets missing");
    Check(ComboText(C(IDC_SKIN),0)==L"默认"&&ComboText(C(IDC_SKIN),1)==L"复古","skin preset names are not exact defaults");
    RECT before{},dock{},clientBefore{};POINT originBefore{};
    GetWindowRect(gMain,&before);GetWindowRect(gPreview,&dock);GetClientRect(gMain,&clientBefore);ClientToScreen(gMain,&originBefore);
    Choose(IDC_SKIN,1);Check(cfg.skin==1&&hc::NativeSkin()==1,"Win98 native skin did not apply live");
    CheckNativeChromeAndLayout();
    EnumChildWindows(gMain,CheckNativeButtons,0);
    Check(GetThemeAppProperties()==0&&GetWindowTheme(gMain)==nullptr,
          "retro mode did not retain the actual classic native caption and controls");
    Check(GetWindowTheme(C(IDC_RESET_DEFAULTS))==nullptr&&GetWindowTheme(PropSheet_GetTabControl(gMain))==nullptr,
          "classic native controls still use themed handles");
    RECT after{},newDock{},clientAfter{};POINT originAfter{};GetWindowRect(gMain,&after);GetWindowRect(gPreview,&newDock);
    GetClientRect(gMain,&clientAfter);ClientToScreen(gMain,&originAfter);
    Check(EqualRect(&before,&after)&&newDock.right-newDock.left==dock.right-dock.left&&
          newDock.bottom-newDock.top==dock.bottom-dock.top,"native skin switch moved/resized outer settings or square preview");
    Check(EqualRect(&clientBefore,&clientAfter)&&originBefore.x==originAfter.x&&originBefore.y==originAfter.y,
          "retro frame changed native client padding or cropped its layout");
    CheckWindowRegion(gMain,true);
    HRGN priorRegion=CreateRectRgn(0,0,0,0),reappliedRegion=CreateRectRgn(0,0,0,0);
    Check(priorRegion&&reappliedRegion&&GetWindowRgn(gMain,priorRegion)!=ERROR,"retro region fixture capture failed");
    hc::ApplyNativeSkin(gMain);Pump();
    Check(GetWindowRgn(gMain,reappliedRegion)!=ERROR&&EqualRgn(priorRegion,reappliedRegion),"reapplying native retro skin lost its trimmed frame region");
    DeleteObject(priorRegion);DeleteObject(reappliedRegion);
    Check(SendMessageW(gMain,WM_NCHITTEST,0,MAKELPARAM(after.left+80,after.top+18))==HTCAPTION,
          "trimmed classic native title no longer supports caption hit testing");
    FlushDebounce();Check(saved.skin==1,"selected native skin was not saved");
    const auto retro=cfg;DestroyWindow(gMain);Pump();session.Begin(saved);Open();
    Check(hc::SettingsEqual(cfg,retro)&&cfg.skin==1&&hc::NativeSkin()==1,"reopening saved retro skin lost settings or native mode");
    CheckNativeChromeAndLayout();
    CheckWindowRegion(gMain,true);
    Check(ComboText(C(IDC_SKIN),0)==L"默认"&&ComboText(C(IDC_SKIN),1)==L"复古","reopened skin preset labels changed");
    Choose(IDC_SKIN,0);Check(cfg.skin==0&&hc::NativeSkin()==0,"current native skin did not restore");FlushDebounce();
    CheckNativeChromeAndLayout();
    CheckWindowRegion(gMain,false);
    Check(saved.skin==0,"restored native skin was not saved");
}
void TestRuntimeRawDestroyAndExit(){
    std::cerr<<"native: runtime/live raw destroy/footer Exit\n";
    auto base=Baseline();session.Begin(base);SyncControls();Choose(IDC_OFFSET_STEP,2);int previous=saves;
    double x=cfg.offsetX,y=cfg.offsetY;SendMessageW(gHost,WM_HOTKEY,5,0);Pump();Check(cfg.offsetX==x+.0625,"global Right/step failed");
    SendMessageW(gHost,WM_HOTKEY,7,0);Pump();Check(cfg.offsetY==y+.0625,"global Down/step failed");
    SendMessageW(gHost,WM_HOTKEY,4,0);Pump();Check(cfg.offsetX==x,"global Left failed");SendMessageW(gHost,WM_HOTKEY,6,0);Pump();Check(cfg.offsetY==y,"global Up failed");
    SendMessageW(gHost,WM_HOTKEY,1,0);Pump();Check(cfg.enabled==!base.enabled,"runtime toggle did not affect live state");FlushDebounce();
    Check(saves==previous+1&&saved.enabled==cfg.enabled&&saved.offsetStep==2,"runtime changes did not coalesce into live save");
    Edit(IDC_DOT_OPACITY,L"33");const auto pending=cfg;previous=saves;HWND old=gMain;DestroyWindow(old);Pump();
    Check(!gMain&&!IsWindow(gPreview)&&IsWindow(gHost)&&hc::SettingsEqual(cfg,pending)&&hc::SettingsEqual(saved,pending)&&saves==previous+1,
          "raw sheet destruction reverted or failed to flush live settings");
    Open();Check(hc::SettingsEqual(cfg,pending),"recreated settings lost realtime state");
    HandleCommand(IDC_RESET_DEFAULTS,BN_CLICKED);Check(hc::SettingsEqual(cfg,hc::Settings{}),"reset defaults did not take live effect");FlushDebounce();
    Check(hc::SettingsEqual(saved,hc::Settings{}),"reset defaults did not autosave");
    Choose(IDC_CROSS_MODE,0);Edit(IDC_LENGTH,L"59");saveSuccess=false;
    SendMessageW(gMain,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),(LPARAM)GetDlgItem(gMain,IDOK));Pump();
    Check(IsWindow(gHost)&&IsWindow(gMain)&&IsWindowVisible(gMain)&&cfg.length==59&&session.HasChanges()&&dismissedBoxes>0,
          "failed-save Exit discarded live state instead of keeping app open by default");
    saveSuccess=true;previous=saves;
    SendMessageW(gMain,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),(LPARAM)GetDlgItem(gMain,IDOK));Pump();
    Check(!IsWindow(gHost)&&!gMain&&!IsWindow(gPreview)&&saved.length==59&&saves==previous+1,"footer Exit did not flush and terminate host");
}
int RunTests(){Environment environment;TestStructureAndDock();TestPreviewSimulation();TestRealtime();TestPixelsAndFailures();TestTrayCloseAndKeys();TestCustomHotkeysAndSkins();TestRuntimeRawDestroyAndExit();
    Check(!overlay.Window(),"tests initialized actual external overlay");std::cout<<"native realtime UI: "<<checks<<" checks PASS\n";return 0;}
}
#ifndef HC_NATIVE_UI_NO_TEST_MAIN
int main(){try{return native_fixture::RunTests();}catch(const std::exception& error){std::cerr<<"native UI test failure: "<<error.what()<<'\n';return 1;}}
#endif
