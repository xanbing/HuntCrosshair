// Real modal API regression using only this test process's native windows.
// A thread-local CBT hook places its own editor offscreen; no desktop input,
// global hooks, other apps or user settings are read or controlled.
#include "../src/pixel_editor.cpp"
#include <cassert>
#include <cstdio>
#undef assert
#define assert(condition) do { if(!(condition)){std::fprintf(stderr,"Modal test failed line %d: %s\n",__LINE__,#condition);std::fflush(stderr);ExitProcess(1);} } while(false)

namespace {
enum class Action { Apply,Cancel,HideOwner,DestroyOwner,Quit };
struct Trial { HWND owner=nullptr,focus=nullptr;Action action=Action::Apply;
    bool sawEditor=false,disabledDuring=false;DWORD thread=0; };
LRESULT CALLBACK PlacementHook(int code,WPARAM wp,LPARAM lp){
    if(code==HCBT_CREATEWND){auto* create=reinterpret_cast<CBT_CREATEWNDW*>(lp);
        const wchar_t* cls=create->lpcs->lpszClass;
        if(cls&&!IS_INTRESOURCE(cls)&&wcscmp(cls,hc::kEditorClass)==0){create->lpcs->x=-20000;create->lpcs->y=-20000;}}
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL CALLBACK FindEditor(HWND window,LPARAM param){
    auto* trial=reinterpret_cast<Trial*>(param);
    if(GetWindow(window,GW_OWNER)!=trial->owner)return TRUE;
    wchar_t cls[128]{};GetClassNameW(window,cls,128);
    if(wcscmp(cls,hc::kEditorClass)!=0)return TRUE;
    trial->sawEditor=true;trial->disabledDuring=!IsWindowEnabled(trial->owner);
    SendMessageW(window,WM_COMMAND,hc::kClear,0);
    switch(trial->action){
    case Action::Apply:PostMessageW(window,WM_COMMAND,IDOK,0);break;
    case Action::Cancel:PostMessageW(window,WM_COMMAND,IDCANCEL,0);break;
    case Action::HideOwner:SendMessageW(trial->owner,WM_APP+1,0,0);PostMessageW(window,WM_COMMAND,IDCANCEL,0);break;
    case Action::DestroyOwner:SendMessageW(trial->owner,WM_CLOSE,0,0);break;
    case Action::Quit:PostThreadMessageW(trial->thread,WM_QUIT,37,0);break;
    }
    return FALSE;
}
LRESULT CALLBACK OwnerProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    auto* trial=reinterpret_cast<Trial*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE){trial=static_cast<Trial*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        trial->owner=window;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)trial);}
    if(msg==WM_APP+1){ShowWindow(window,SW_HIDE);return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
DWORD WINAPI DriveModal(LPVOID pointer){
    auto* trial=static_cast<Trial*>(pointer);
    for(int tick=0;tick<200;tick++){EnumThreadWindows(trial->thread,FindEditor,(LPARAM)trial);if(trial->sawEditor)return 0;Sleep(10);}
    PostThreadMessageW(trial->thread,WM_QUIT,90,0);return 1;
}
void Run(Action action){
    std::printf("Modal action %d starting\n",static_cast<int>(action));std::fflush(stdout);
    Trial trial;trial.action=action;trial.thread=GetCurrentThreadId();HINSTANCE instance=GetModuleHandleW(nullptr);
    HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW,L"HuntCrosshair.ModalOwnerRegression",L"Owned API test",
        WS_POPUP|WS_VISIBLE,-20000,-20000,320,200,nullptr,nullptr,instance,&trial);
    assert(owner&&IsWindowVisible(owner)&&IsWindowEnabled(owner));
    trial.focus=CreateWindowExW(0,L"EDIT",L"previous focus",WS_CHILD|WS_VISIBLE,0,0,160,28,owner,(HMENU)50,instance,nullptr);
    SetActiveWindow(owner);SetFocus(trial.focus);assert(GetFocus()==trial.focus);
    hc::PixelArt art;art.pixels[10*20+10]=255;const auto original=art.pixels;
    HANDLE driver=CreateThread(nullptr,0,DriveModal,&trial,0,nullptr);assert(driver);
    bool accepted=hc::EditPixelArt(owner,instance,art,hc::Color{},L"Test editor");
    std::printf("Modal action %d returned accepted=%d\n",static_cast<int>(action),accepted);std::fflush(stdout);
    assert(WaitForSingleObject(driver,5000)==WAIT_OBJECT_0);CloseHandle(driver);
    assert(trial.sawEditor&&trial.disabledDuring);
    if(action==Action::Apply){assert(accepted&&art.pixels[10*20+10]==0);}
    else{assert(!accepted&&art.pixels==original);}
    if(action==Action::Apply||action==Action::Cancel){
        assert(IsWindow(owner)&&IsWindowEnabled(owner)&&IsWindowVisible(owner));
        assert(GetActiveWindow()==owner&&GetFocus()==trial.focus);
    }else if(action==Action::HideOwner){assert(IsWindow(owner)&&IsWindowEnabled(owner)&&!IsWindowVisible(owner));}
    else if(action==Action::DestroyOwner){assert(!IsWindow(owner));}
    MSG message{};bool quit=false;WPARAM quitValue=0;
    // WM_QUIT is generated after older posted messages have been consumed.
    // Drain this test thread's own notifications rather than filtering them out.
    for(int count=0;count<1000&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++count){
        if(message.message==WM_QUIT){quit=true;quitValue=message.wParam;}
        else{TranslateMessage(&message);DispatchMessageW(&message);}
    }
    if(action==Action::Quit){assert(quit&&quitValue==37);}
    else assert(!quit);
    if(IsWindow(owner)){DestroyWindow(owner);}
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=OwnerProc;
    cls.lpszClassName=L"HuntCrosshair.ModalOwnerRegression";assert(RegisterClassW(&cls)!=0);
    HHOOK placement=SetWindowsHookExW(WH_CBT,PlacementHook,nullptr,GetCurrentThreadId());assert(placement);
    for(Action action:{Action::Apply,Action::Cancel,Action::Apply,Action::HideOwner,Action::DestroyOwner,Action::Quit})Run(action);
    UnhookWindowsHookEx(placement);
    std::puts("Real pixel editor modal API: Apply/Cancel preserve visible enabled owner/focus; hidden/destroyed owner and WM_QUIT remain respected: PASS.");
}
