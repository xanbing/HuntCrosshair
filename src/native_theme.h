#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace hc {
namespace native_theme_detail {
// These APIs only affect this process or its own native windows. Resolve them
// dynamically so the standalone pixel-editor tests need no additional imports.
struct Api {
    using WindowTheme=HRESULT (WINAPI*)(HWND,LPCWSTR,LPCWSTR);
    using AppTheme=void (WINAPI*)(DWORD);
    using AppProperties=DWORD (WINAPI*)();
    using DialogTexture=HRESULT (WINAPI*)(HWND,DWORD);
    using DwmAttribute=HRESULT (WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
    HMODULE theme=LoadLibraryExW(L"uxtheme.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE dwm=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    WindowTheme windowTheme=theme?reinterpret_cast<WindowTheme>(GetProcAddress(theme,"SetWindowTheme")):nullptr;
    AppTheme appTheme=theme?reinterpret_cast<AppTheme>(GetProcAddress(theme,"SetThemeAppProperties")):nullptr;
    AppProperties appProperties=theme?reinterpret_cast<AppProperties>(GetProcAddress(theme,"GetThemeAppProperties")):nullptr;
    DialogTexture dialogTexture=theme?reinterpret_cast<DialogTexture>(GetProcAddress(theme,"EnableThemeDialogTexture")):nullptr;
    DwmAttribute dwmAttribute=dwm?reinterpret_cast<DwmAttribute>(GetProcAddress(dwm,"DwmSetWindowAttribute")):nullptr;
    DWORD originalProperties=appProperties?appProperties():7;
};
struct State {Api api;int skin=0;bool applying=false,frameApplying=false;};
inline State& Current(){static State state;return state;}

constexpr wchar_t FrameInsetProperty[]=L"HuntCrosshair.NativeRetroFrame.Inset";
constexpr wchar_t FrameWidthProperty[]=L"HuntCrosshair.NativeRetroFrame.Width";
constexpr wchar_t FrameHeightProperty[]=L"HuntCrosshair.NativeRetroFrame.Height";
inline int PropertyNumber(HWND window,const wchar_t* name){return static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(window,name)))-1;}
inline void ForgetFrame(HWND window){RemovePropW(window,FrameInsetProperty);RemovePropW(window,FrameWidthProperty);RemovePropW(window,FrameHeightProperty);}
inline bool RememberFrame(HWND window,int inset,int width,int height){
    const bool ok=SetPropW(window,FrameInsetProperty,reinterpret_cast<HANDLE>(static_cast<INT_PTR>(inset)+1))&&
        SetPropW(window,FrameWidthProperty,reinterpret_cast<HANDLE>(static_cast<INT_PTR>(width)+1))&&
        SetPropW(window,FrameHeightProperty,reinterpret_cast<HANDLE>(static_cast<INT_PTR>(height)+1));
    if(!ok)ForgetFrame(window);return ok;
}
inline bool MatchesFrame(HWND window,HRGN current){
    const int inset=PropertyNumber(window,FrameInsetProperty),width=PropertyNumber(window,FrameWidthProperty),height=PropertyNumber(window,FrameHeightProperty);
    if(inset<0||width<=0||height<=0)return false;
    HRGN expected=CreateRectRgn(inset,inset,width-inset,height-inset);
    const bool same=expected&&EqualRgn(current,expected);if(expected)DeleteObject(expected);return same;
}

inline void ApplyWindow(HWND window){
    if(!IsWindow(window))return;
    auto& state=Current();const bool classic=state.skin==1;
    const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    const bool captioned=!(style&WS_CHILD)&&(style&WS_CAPTION)==WS_CAPTION;
    if(state.api.windowTheme)state.api.windowTheme(window,classic?L"":nullptr,classic?L"":nullptr);
    wchar_t name[64]{};GetClassNameW(window,name,64);
    // PropertySheet pages use the system tab texture in the current skin and
    // the system dialog face in classic mode. They remain real dialog pages.
    if(state.api.dialogTexture&&(style&WS_CHILD)&&lstrcmpiW(name,L"#32770")==0)
        state.api.dialogTexture(window,classic?0x00000001:0x00000006); // ETDT_DISABLE / ETDT_ENABLETAB
    if(captioned&&state.api.dwmAttribute){
        // Retain the genuine classic caption, beveled frame and native hit
        // testing. RefreshNativeFrame clips only surplus modern outer padding.
        const int policy=classic?1:0; // DWMNCRP_DISABLED / DWMNCRP_USEWINDOWSTYLE
        state.api.dwmAttribute(window,2,&policy,sizeof(policy)); // DWMWA_NCRENDERING_POLICY
        const int corner=classic?1:0; // DWMWCP_DONOTROUND / DWMWCP_DEFAULT (Win11)
        state.api.dwmAttribute(window,33,&corner,sizeof(corner));
        // Clear v1.8 composed-caption color requests in either mode. Classic
        // nonclient painting uses the system's native caption palette itself.
        const COLORREF captionColor=0xFFFFFFFFu,textColor=0xFFFFFFFFu;
        state.api.dwmAttribute(window,35,&captionColor,sizeof(captionColor)); // DWMWA_CAPTION_COLOR
        state.api.dwmAttribute(window,36,&textColor,sizeof(textColor)); // DWMWA_TEXT_COLOR
    }
    if(lstrcmpiW(name,L"ComboBox")==0){
        COMBOBOXINFO info{sizeof(info)};
        if(GetComboBoxInfo(window,&info)&&info.hwndList&&state.api.windowTheme)
            state.api.windowTheme(info.hwndList,classic?L"":nullptr,classic?L"":nullptr);
    }
}
inline BOOL CALLBACK ApplyChild(HWND child,LPARAM){ApplyWindow(child);return TRUE;}
}

inline int NativeSkin(){return native_theme_detail::Current().skin;}
inline void SetNativeSkin(int skin){
    auto& state=native_theme_detail::Current();state.skin=skin==1?1:0;
    if(state.api.appTheme)state.api.appTheme(state.skin?0:state.api.originalProperties);
}
inline RECT NativeWindowBounds(HWND window){
    RECT bounds{};if(!IsWindow(window)||!GetWindowRect(window,&bounds))return bounds;
    HRGN region=CreateRectRgn(0,0,0,0);if(!region)return bounds;
    if(GetWindowRgn(window,region)!=ERROR){RECT actual{};GetRgnBox(region,&actual);OffsetRect(&actual,bounds.left,bounds.top);bounds=actual;}
    DeleteObject(region);return bounds;
}
inline void RefreshNativeFrame(HWND window){
    if(!IsWindow(window)||IsIconic(window))return;
    auto& state=native_theme_detail::Current();if(state.frameApplying)return;
    const LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    if((style&WS_CHILD)||(style&WS_CAPTION)!=WS_CAPTION)return;
    HRGN current=CreateRectRgn(0,0,0,0);if(!current)return;
    const bool hasRegion=GetWindowRgn(window,current)!=ERROR;
    const bool owned=native_theme_detail::PropertyNumber(window,native_theme_detail::FrameInsetProperty)>=0;
    // Never replace a region supplied by somebody else, including one that
    // replaced our earlier rectangle. Window properties carry only integers;
    // Windows disposes of them and the transferred HRGN with the window.
    if(owned&&(!hasRegion||!native_theme_detail::MatchesFrame(window,current))){native_theme_detail::ForgetFrame(window);DeleteObject(current);return;}
    if(!owned&&hasRegion){DeleteObject(current);return;}
    RECT outer{};POINT client{};
    if(!GetWindowRect(window,&outer)||!ClientToScreen(window,&client)){DeleteObject(current);return;}
    const int width=outer.right-outer.left,height=outer.bottom-outer.top;
    const UINT dpi=GetDpiForWindow(window);
    const int nativeBorder=GetSystemMetricsForDpi((style&WS_THICKFRAME)?SM_CXSIZEFRAME:SM_CXDLGFRAME,dpi?dpi:96);
    const int inset=state.skin==1?static_cast<int>(client.x-outer.left)-nativeBorder:0;
    if(inset<=0||width<=inset*2||height<=inset*2){
        if(owned){state.frameApplying=true;if(SetWindowRgn(window,nullptr,TRUE))native_theme_detail::ForgetFrame(window);state.frameApplying=false;}
        DeleteObject(current);return;
    }
    HRGN desired=CreateRectRgn(inset,inset,width-inset,height-inset);if(!desired){DeleteObject(current);return;}
    if(owned&&EqualRgn(current,desired)){DeleteObject(current);DeleteObject(desired);return;}
    const int priorInset=native_theme_detail::PropertyNumber(window,native_theme_detail::FrameInsetProperty),priorWidth=native_theme_detail::PropertyNumber(window,native_theme_detail::FrameWidthProperty),priorHeight=native_theme_detail::PropertyNumber(window,native_theme_detail::FrameHeightProperty);
    if(!native_theme_detail::RememberFrame(window,inset,width,height)){DeleteObject(current);DeleteObject(desired);return;}
    state.frameApplying=true;
    if(!SetWindowRgn(window,desired,TRUE)){
        DeleteObject(desired);
        if(owned)native_theme_detail::RememberFrame(window,priorInset,priorWidth,priorHeight);else native_theme_detail::ForgetFrame(window);
    }
    state.frameApplying=false;DeleteObject(current);
}
inline void ApplyNativeSkin(HWND window){
    if(!IsWindow(window))return;
    auto& state=native_theme_detail::Current();if(state.applying)return;
    // SetWindowTheme may reset even an application-supplied window region.
    // Preserve that exact shape across the native theme operation, then let
    // RefreshNativeFrame update or remove only our own padding rectangle.
    HRGN preserved=CreateRectRgn(0,0,0,0);
    const bool hasRegion=preserved&&GetWindowRgn(window,preserved)!=ERROR;
    if(!hasRegion&&preserved){DeleteObject(preserved);preserved=nullptr;}
    if(native_theme_detail::PropertyNumber(window,native_theme_detail::FrameInsetProperty)>=0&&
       (!hasRegion||!native_theme_detail::MatchesFrame(window,preserved)))native_theme_detail::ForgetFrame(window);
    state.applying=true;
    state.frameApplying=true;
    native_theme_detail::ApplyWindow(window);
    EnumChildWindows(window,native_theme_detail::ApplyChild,0);
    if(preserved&&!SetWindowRgn(window,preserved,FALSE)){DeleteObject(preserved);native_theme_detail::ForgetFrame(window);}
    state.frameApplying=false;
    RefreshNativeFrame(window);
    state.applying=false;
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
}
}
