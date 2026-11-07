#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>
#include "platform.h"
#include "easing_presets.h"
#include "pixel_editor.h"
#include "settings_session.h"
#include "resource.h"
#include "hotkeys.h"
#include "native_theme.h"

using namespace hc;
namespace {
constexpr wchar_t HostClass[]=L"HuntCrosshair.Settings.v1";
constexpr wchar_t PreviewClass[]=L"HuntCrosshair.Preview";
constexpr wchar_t CurveClass[]=L"HuntCrosshair.Curve";
constexpr wchar_t SwatchClass[]=L"HuntCrosshair.Swatch";
constexpr UINT WM_TRAY=WM_APP+1,WM_MOUSE_EVENT=WM_APP+10,WM_SHOW_APP=WM_APP+20,WM_CAPTURE_END=WM_APP+21;
constexpr UINT T_HEART=1,T_ANIM=2,T_SAVE=3,T_DEMO=4;
HINSTANCE gInstance=nullptr;
HWND gHost=nullptr,gMain=nullptr,gPreview=nullptr;
constexpr int PageCount=6;
HWND pages[PageCount]{};
SettingsSession session;
Settings& cfg=session.Draft();
Overlay overlay;
Animation animation,demoAnimation;
bool syncing=false,rightActive=false,testActive=false,artEditing=false;
bool demoPlaying=false,quitting=false,mouseOK=true,overlayOK=true,hotkeysOK=true,trayOK=true,savedOK=true;
int demoStage=0,selectedPoint=0,lastPage=0;
double demoReturnAt=0;
HWND lastForeground=nullptr;
bool lastVisible=false;
UINT taskbarCreated=0;
NOTIFYICONDATAW tray{};
COLORREF customColors[16]{};
std::vector<Monitor> monitors;
bool runtimeInitialized=false,hotkeyCapture=false;
std::array<bool,kHotkeyCount> hotkeyRegistered{},hotkeyFailed{};
std::array<Hotkey,kHotkeyCount> registeredHotkeys{};
std::wstring hotkeyMessage;
constexpr const wchar_t* HotkeyLabels[]={L"显示／隐藏准星",L"打开设置",L"退出程序",L"向左微调",L"向右微调",L"向上微调",L"向下微调"};

struct Binding {int id;HWND slider,edit;double* value;double scale;int min,max;};
std::vector<Binding> bindings;
HWND C(int id){for(HWND page:pages)if(IsWindow(page))if(HWND control=GetDlgItem(page,id))return control;if(IsWindow(gPreview))return GetDlgItem(gPreview,id);return nullptr;}
int D(HWND hwnd,int value){UINT dpi=GetDpiForWindow(hwnd);return MulDiv(value,dpi?dpi:96,96);}
void Fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void Stroke(HDC dc,int x1,int y1,int x2,int y2,COLORREF color,int width=1){HPEN p=CreatePen(PS_SOLID,width,color);auto old=SelectObject(dc,p);MoveToEx(dc,x1,y1,nullptr);LineTo(dc,x2,y2);SelectObject(dc,old);DeleteObject(p);}
HFONT DialogFont(HWND hwnd){HFONT f=(HFONT)SendMessageW(GetParent(hwnd),WM_GETFONT,0,0);return f?f:(HFONT)GetStockObject(DEFAULT_GUI_FONT);}
void Text(HDC dc,const wchar_t* text,RECT r,COLORREF color,HFONT font,UINT format=DT_LEFT|DT_VCENTER|DT_SINGLELINE){auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text,-1,&r,format);SelectObject(dc,old);}
std::wstring Number(double value,int decimals=4){if(std::abs(value)<.00005)value=0;wchar_t text[64];swprintf(text,64,L"%.*f",decimals,value);std::wstring result=text;if(decimals){while(!result.empty()&&result.back()==L'0')result.pop_back();if(!result.empty()&&result.back()==L'.')result.pop_back();}return result;}
bool IsOffset(int id){return id==IDC_OFFSET_X||id==IDC_OFFSET_Y;}
double Canonical(const Binding& b,double displayed){double v=std::clamp(displayed,(double)b.min,(double)b.max);v=IsOffset(b.id)?std::round(v*16)/16:std::round(v);return v==0?0:v;}
double OffsetStep(){constexpr double steps[]={1,.25,.0625};return steps[std::clamp(cfg.offsetStep,0,2)];}
void BindingText(const Binding& b){double value=*b.value*b.scale;std::wstring text=Number(value,IsOffset(b.id)?4:0);SetWindowTextW(b.edit,text.c_str());SendMessageW(b.slider,TBM_SETPOS,TRUE,(LPARAM)std::lround(value));}
Binding* FindBinding(int id){for(auto& b:bindings)if(b.id==id)return &b;return nullptr;}
void Bind(HWND page,int id,double* value,double scale,int min,int max){
 Binding b{id,GetDlgItem(page,id+1000),GetDlgItem(page,id),value,scale,min,max};
 SendMessageW(b.slider,TBM_SETRANGE,TRUE,MAKELPARAM(min,max));SendMessageW(b.slider,TBM_SETPAGESIZE,0,std::max(1,(max-min)/10));bindings.push_back(b);
}
void AddChoices(HWND combo,std::initializer_list<const wchar_t*> entries){for(auto entry:entries)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)entry);}
void SelectCurve(int kind){HWND combo=C(IDC_EASING);int count=(int)SendMessageW(combo,CB_GETCOUNT,0,0);for(int i=0;i<count;i++)if(SendMessageW(combo,CB_GETITEMDATA,i,0)==kind){SendMessageW(combo,CB_SETCURSEL,i,0);return;}}
int SelectedCurve(){HWND combo=C(IDC_EASING);int i=(int)SendMessageW(combo,CB_GETCURSEL,0,0);return i>=0?(int)SendMessageW(combo,CB_GETITEMDATA,i,0):0;}
void EffectiveCurve(double& x1,double& y1,double& x2,double& y2){if(const auto* p=ResolvePreset(cfg.easing)){x1=p->x1;y1=p->y1;x2=p->x2;y2=p->y2;}else if(cfg.easing==0){x1=y1=0;x2=y2=1;}else{x1=cfg.x1;y1=cfg.y1;x2=cfg.x2;y2=cfg.y2;}}
void SyncCurve(){if(IsWindow(pages[2]))CheckRadioButton(pages[2],IDC_POINT1,IDC_POINT2,selectedPoint?IDC_POINT2:IDC_POINT1);}
void MakeCustom(){if(cfg.easing!=4){EffectiveCurve(cfg.x1,cfg.y1,cfg.x2,cfg.y2);cfg.easing=4;SelectCurve(4);}}
void RefreshMonitors(){monitors=Monitors();HWND combo=C(IDC_MONITOR);if(!combo)return;SendMessageW(combo,CB_RESETCONTENT,0,0);for(const auto& monitor:monitors){std::wstring name=monitor.name+(monitor.primary?L"（主显示器）":L"");SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)name.c_str());}if(!monitors.empty())cfg.monitor=std::clamp(cfg.monitor,0,(int)monitors.size()-1);SendMessageW(combo,CB_SETCURSEL,cfg.monitor,0);}
void SyncPointBinding(){if(auto* b=FindBinding(IDC_DOT_SIZE)){b->value=cfg.dotShape==3?&cfg.dotArtSize:&cfg.dotSize;b->min=cfg.dotShape==3?0:1;b->max=cfg.dotShape==3?200:64;SendMessageW(b->slider,TBM_SETRANGE,TRUE,MAKELPARAM(b->min,b->max));}}
void UpdateStatus(){const wchar_t* text=!overlayOK?L"覆盖层未能启动。":!mouseOK?L"右键监听未能启动。":!hotkeysOK?L"部分快捷键被其他程序占用。":!trayOK?L"托盘不可用，可用快捷键打开设置。":!savedOK?L"自动保存失败，请将程序放到可写目录。":session.HasChanges()?L"已实时生效，正在自动保存。":L"已实时生效并自动保存。";if(HWND status=C(IDC_STATUS))SetWindowTextW(status,text);if(IsWindow(gMain))SetWindowTextW(gMain,savedOK?L"准星属性":L"准星属性 · 保存失败");}
void UpdateDirty(){UpdateStatus();}
int HotkeyIndex(HWND hwnd){int id=GetDlgCtrlID(hwnd);return IsWindow(hwnd)&&id>=IDC_HOTKEY_BASE&&id<IDC_HOTKEY_BASE+kHotkeyCount?id-IDC_HOTKEY_BASE:-1;}
WORD HotkeyControlValue(const Hotkey& key){BYTE flags=0;if(key.modifiers&MOD_ALT)flags|=HOTKEYF_ALT;if(key.modifiers&MOD_CONTROL)flags|=HOTKEYF_CONTROL;if(key.modifiers&MOD_SHIFT)flags|=HOTKEYF_SHIFT;if(key.key>=VK_PRIOR&&key.key<=VK_DOWN)flags|=HOTKEYF_EXT;return MAKEWORD(key.key,flags);}
void SyncHotkeyControls(){bool before=syncing;syncing=true;for(int i=0;i<kHotkeyCount;i++)if(HWND field=C(IDC_HOTKEY_BASE+i))SendMessageW(field,HKM_SETHOTKEY,HotkeyControlValue(cfg.hotkeys[i]),0);syncing=before;}
void UpdateHotkeyStatus(){std::wstring text=hotkeyMessage;if(text.empty()){if(hotkeyCapture)text=L"录入期间暂停全局快捷键，离开输入框后恢复。";else{for(int i=0;i<kHotkeyCount;i++)if(hotkeyFailed[i]){text=L"“"+std::wstring(HotkeyLabels[i])+L"”快捷键被占用或不可用，请更换组合。";break;}if(text.empty())text=L"快捷键已生效；清除后可停用对应操作。";}}if(HWND status=C(IDC_HOTKEY_STATUS))SetWindowTextW(status,text.c_str());}
void UnregisterHotkeys(){if(runtimeInitialized)for(int i=0;i<kHotkeyCount;i++)if(hotkeyRegistered[i])UnregisterHotKey(gHost,i+1);hotkeyRegistered.fill(false);}
void RebindHotkeys(){
 UnregisterHotkeys();hotkeyFailed.fill(false);hotkeysOK=true;
 if(runtimeInitialized&&!hotkeyCapture)for(int i=0;i<kHotkeyCount;i++){const auto& key=cfg.hotkeys[i];if(!key.key)continue;UINT flags=key.modifiers|(i<3?MOD_NOREPEAT:0);hotkeyRegistered[i]=RegisterHotKey(gHost,i+1,flags,key.key)!=FALSE;hotkeyFailed[i]=!hotkeyRegistered[i];hotkeysOK=hotkeysOK&&hotkeyRegistered[i];registeredHotkeys[i]=key;}
 UpdateHotkeyStatus();UpdateStatus();
}
void SuspendHotkeys(){hotkeyCapture=true;hotkeyMessage.clear();UnregisterHotkeys();UpdateHotkeyStatus();}
void FinishHotkeyCapture(){if(IsWindowVisible(gMain)&&HotkeyIndex(GetFocus())>=0)return;hotkeyCapture=false;RebindHotkeys();}
LRESULT CALLBACK HotkeySubclass(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
 if(msg==WM_SETFOCUS)SuspendHotkeys();else if(msg==WM_KILLFOCUS){if(gHost)PostMessageW(gHost,WM_CAPTURE_END,0,0);}else if(msg==WM_GETDLGCODE){auto* key=(MSG*)lp;bool plainTab=key&&key->wParam==VK_TAB&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000);if(!key||!plainTab)return DLGC_WANTALLKEYS|DLGC_WANTARROWS|DLGC_WANTCHARS;return 0;}else if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,HotkeySubclass,2);
 return DefSubclassProc(hwnd,msg,wp,lp);
}
void Changed();
void HotkeyRecordChanged(int index){
 if(syncing||index<0||index>=kHotkeyCount)return;WORD packed=(WORD)SendMessageW(C(IDC_HOTKEY_BASE+index),HKM_GETHOTKEY,0,0);BYTE flags=HIBYTE(packed);Hotkey key{((flags&HOTKEYF_ALT)?MOD_ALT:0)|((flags&HOTKEYF_CONTROL)?MOD_CONTROL:0)|((flags&HOTKEYF_SHIFT)?MOD_SHIFT:0),LOBYTE(packed)};if(!key.key)key.modifiers=0;
 hotkeyMessage.clear();if(!ValidHotkey(key))hotkeyMessage=L"此按键不能注册为全局快捷键，请更换组合。";else if(key.key)for(int i=0;i<kHotkeyCount;i++)if(i!=index&&SameHotkey(key,cfg.hotkeys[i])){hotkeyMessage=L"此组合已用于“"+std::wstring(HotkeyLabels[i])+L"”，请更换组合。";break;}
 if(!hotkeyMessage.empty()){SyncHotkeyControls();UpdateHotkeyStatus();return;}cfg.hotkeys[index]=key;if(!hotkeyCapture)RebindHotkeys();UpdateHotkeyStatus();Changed();
}
void SyncControls(){
 bool before=syncing;syncing=true;SyncPointBinding();for(auto& b:bindings)BindingText(b);
 if(IsWindow(pages[0]))CheckDlgButton(pages[0],IDC_ENABLE,cfg.enabled?BST_CHECKED:BST_UNCHECKED);if(IsWindow(pages[1]))CheckDlgButton(pages[1],IDC_DOT_ENABLED,cfg.dot?BST_CHECKED:BST_UNCHECKED);if(IsWindow(pages[3]))CheckDlgButton(pages[3],IDC_ONLY_GAME,cfg.onlyGame?BST_CHECKED:BST_UNCHECKED);if(IsWindow(gPreview))CheckDlgButton(gPreview,IDC_SIMULATE,testActive?BST_CHECKED:BST_UNCHECKED);
 SendMessageW(C(IDC_CROSS_MODE),CB_SETCURSEL,cfg.customCrosshair?1:0,0);SendMessageW(C(IDC_DOT_SHAPE),CB_SETCURSEL,cfg.dotShape,0);SendMessageW(C(IDC_ACTION),CB_SETCURSEL,cfg.action,0);SendMessageW(C(IDC_TRIGGER),CB_SETCURSEL,cfg.trigger,0);SendMessageW(C(IDC_OFFSET_STEP),CB_SETCURSEL,cfg.offsetStep,0);SelectCurve(cfg.easing);SyncCurve();
 for(int id:{IDC_LENGTH,IDC_GAP,IDC_THICKNESS}){EnableWindow(C(id),!cfg.customCrosshair);EnableWindow(C(id+1000),!cfg.customCrosshair);}
 for(int id:{IDC_CROSS_SWATCH,IDC_DOT_SWATCH})if(HWND swatch=C(id))InvalidateRect(swatch,nullptr,FALSE);
 if(C(IDC_MONITOR))SendMessageW(C(IDC_MONITOR),CB_SETCURSEL,cfg.monitor,0);SendMessageW(C(IDC_SKIN),CB_SETCURSEL,cfg.skin,0);SyncHotkeyControls();syncing=before;UpdateDirty();UpdateHotkeyStatus();
}
void InvalidatePreviews(){if(IsWindow(gPreview))InvalidateRect(gPreview,nullptr,FALSE);}
void StopDemo(){demoPlaying=false;demoStage=0;if(gHost)KillTimer(gHost,T_DEMO);}
void Render(){double now=NowMs();bool visible=cfg.enabled&&!artEditing&&(!cfg.onlyGame||GameIsForeground());if(overlay.Window())overlay.Update(cfg,MakeFrame(cfg,animation.Value(now,cfg)),visible);lastVisible=visible;InvalidatePreviews();if(gHost){if(animation.Running(now))SetTimer(gHost,T_ANIM,16,nullptr);else KillTimer(gHost,T_ANIM);}}
void Target(){StopDemo();animation.SetTarget(rightActive||testActive,NowMs(),cfg);Render();}
void StartDemo(){demoAnimation.Reset();demoPlaying=true;demoStage=1;double now=NowMs();demoAnimation.SetTarget(true,now,cfg);demoReturnAt=now+cfg.enterMs+180;SetTimer(gHost,T_DEMO,16,nullptr);InvalidatePreviews();}
void TickDemo(double now){if(demoPlaying&&demoStage==1&&now>=demoReturnAt){demoAnimation.SetTarget(false,now,cfg);demoStage=2;}if(demoPlaying&&demoStage==2&&!demoAnimation.Running(now))StopDemo();InvalidatePreviews();}
bool SaveLive(){if(gHost)KillTimer(gHost,T_SAVE);savedOK=!session.HasChanges()||session.Apply([](const Settings& value){return SaveSettings(value);});UpdateStatus();return savedOK;}
void Changed(){if(gHost)SetTimer(gHost,T_SAVE,300,nullptr);UpdateStatus();Render();if(HWND graph=C(IDC_CURVE_GRAPH))InvalidateRect(graph,nullptr,FALSE);}
void Nudge(int id,bool runtime=false){double nextX=cfg.offsetX,nextY=cfg.offsetY;double delta=OffsetStep();if(id==IDC_X_MINUS)nextX-=delta;else if(id==IDC_X_PLUS)nextX+=delta;else if(id==IDC_Y_MINUS)nextY-=delta;else nextY+=delta;nextX=std::clamp(nextX,-500.0,500.0);nextY=std::clamp(nextY,-500.0,500.0);
 cfg.offsetX=nextX;cfg.offsetY=nextY;
 SyncControls();Changed();
}
void ToggleRuntime(){cfg.enabled=!cfg.enabled;SyncControls();Changed();}
struct PausePreviewInput {HWND window=gPreview;bool enabled=IsWindowEnabled(window)!=FALSE;PausePreviewInput(){if(enabled)EnableWindow(window,FALSE);}~PausePreviewInput(){if(enabled&&IsWindow(window))EnableWindow(window,TRUE);}};
void PickColor(bool dot){PausePreviewInput pause;Color& color=dot?cfg.dotColor:cfg.color;CHOOSECOLORW cc{sizeof(cc)};cc.hwndOwner=gMain;cc.rgbResult=RGB(color.r,color.g,color.b);cc.lpCustColors=customColors;cc.Flags=CC_FULLOPEN|CC_RGBINIT;if(ChooseColorW(&cc)){color={GetRValue(cc.rgbResult),GetGValue(cc.rgbResult),GetBValue(cc.rgbResult)};SyncControls();Changed();}}
void OpenPixelEditor(bool dot){PausePreviewInput pause;StopDemo();artEditing=true;Render();PixelArt& art=dot?cfg.dotArt:cfg.crossArt;bool accepted=EditPixelArt(gMain,gInstance,art,dot?cfg.dotColor:cfg.color,dot?L"绘制点准星":L"绘制准星");artEditing=false;if(accepted){if(dot)cfg.dotShape=3;else cfg.customCrosshair=true;SyncControls();Changed();}else{SyncControls();Render();}}

RECT CurvePlot(HWND hwnd,RECT rect){int marginX=D(hwnd,28),top=D(hwnd,18),bottom=D(hwnd,28);return RECT{marginX,top,rect.right-D(hwnd,15),rect.bottom-bottom};}
void UpdateCurvePoint(int point,double x,double y,bool replay=true){MakeCustom();selectedPoint=point;x=std::round(std::clamp(x,0.0,1.0)*1000)/1000;y=std::round(std::clamp(y,0.0,1.0)*1000)/1000;if(point==0){cfg.x1=x;cfg.y1=y;}else{cfg.x2=x;cfg.y2=y;}SyncCurve();Changed();if(replay)StartDemo();}
void PaintCurve(HWND hwnd,HDC dc){
 RECT client;GetClientRect(hwnd,&client);Fill(dc,client,GetSysColor(COLOR_WINDOW));RECT plot=CurvePlot(hwnd,client);int width=plot.right-plot.left,height=plot.bottom-plot.top;if(width<1||height<1)return;
 auto px=[&](double value){return plot.left+(int)std::lround(value*width);};auto py=[&](double value){return plot.bottom-(int)std::lround(value*height);};COLORREF grid=GetSysColor(COLOR_3DFACE),ink=GetSysColor(COLOR_WINDOWTEXT),accent=GetSysColor(COLOR_HIGHLIGHT);HFONT font=DialogFont(hwnd);
 for(int i=0;i<=4;i++){int x=plot.left+width*i/4,y=plot.top+height*i/4;Stroke(dc,x,plot.top,x,plot.bottom,grid);Stroke(dc,plot.left,y,plot.right,y,grid);}
 double x1,y1,x2,y2;EffectiveCurve(x1,y1,x2,y2);Stroke(dc,px(0),py(0),px(x1),py(y1),GetSysColor(COLOR_GRAYTEXT));Stroke(dc,px(x1),py(y1),px(x2),py(y2),GetSysColor(COLOR_GRAYTEXT));Stroke(dc,px(x2),py(y2),px(1),py(1),GetSysColor(COLOR_GRAYTEXT));
 HPEN pen=CreatePen(PS_SOLID,std::max(1,D(hwnd,2)),accent);auto old=SelectObject(dc,pen);MoveToEx(dc,px(0),py(0),nullptr);for(int i=1;i<=160;i++){double t=i/160.0;LineTo(dc,px(t),py(Ease(t,cfg.easing,cfg.x1,cfg.y1,cfg.x2,cfg.y2)));}SelectObject(dc,old);DeleteObject(pen);
 for(int i=0;i<2;i++){int x=px(i?1:0),y=py(i?1:0);Fill(dc,RECT{x-D(hwnd,2),y-D(hwnd,2),x+D(hwnd,3),y+D(hwnd,3)},ink);}
 for(int i=0;i<2;i++){int x=px(i?x2:x1),y=py(i?y2:y1),radius=D(hwnd,selectedPoint==i?5:3);HBRUSH brush=CreateSolidBrush(i?RGB(177,101,36):accent);auto previous=SelectObject(dc,brush);Ellipse(dc,x-radius,y-radius,x+radius+1,y+radius+1);SelectObject(dc,previous);DeleteObject(brush);int lx=x>client.right-D(hwnd,42)?x-D(hwnd,25):x+D(hwnd,7),ly=y<D(hwnd,36)?y+D(hwnd,5):y-D(hwnd,21);if(i&&std::hypot((x1-x2)*width,(y1-y2)*height)<D(hwnd,20))ly+=y<client.bottom/2?D(hwnd,16):-D(hwnd,16);Text(dc,i?L"P2":L"P1",RECT{lx,ly,lx+D(hwnd,24),ly+D(hwnd,18)},ink,font);}
 Text(dc,L"P0",RECT{D(hwnd,2),plot.bottom+D(hwnd,5),D(hwnd,45),client.bottom},ink,font);Text(dc,L"P3",RECT{client.right-D(hwnd,34),0,client.right,plot.top},ink,font,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);Text(dc,L"时间 →",RECT{plot.left,plot.bottom+D(hwnd,5),plot.right,client.bottom},ink,font,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 if(GetFocus()==hwnd){RECT focus=client;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}
}
LRESULT CALLBACK CurveProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){static int drag=-1;
 switch(msg){case WM_ERASEBKGND:return 1;case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);PaintCurve(hwnd,dc);EndPaint(hwnd,&ps);return 0;}case WM_GETDLGCODE:return DLGC_WANTARROWS;case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(hwnd,nullptr,FALSE);return 0;
 case WM_KEYDOWN:if(wp==VK_LEFT||wp==VK_RIGHT||wp==VK_UP||wp==VK_DOWN){double x1,y1,x2,y2;EffectiveCurve(x1,y1,x2,y2);double step=(GetKeyState(VK_SHIFT)&0x8000)?.001:.01,x=selectedPoint?x2:x1,y=selectedPoint?y2:y1;if(wp==VK_LEFT)x-=step;else if(wp==VK_RIGHT)x+=step;else if(wp==VK_UP)y+=step;else y-=step;UpdateCurvePoint(selectedPoint,x,y);return 0;}break;
 case WM_LBUTTONDOWN:{RECT client;GetClientRect(hwnd,&client);RECT r=CurvePlot(hwnd,client);double x=(GET_X_LPARAM(lp)-r.left)/(double)std::max<LONG>(1,r.right-r.left),y=1-(GET_Y_LPARAM(lp)-r.top)/(double)std::max<LONG>(1,r.bottom-r.top);double x1,y1,x2,y2;EffectiveCurve(x1,y1,x2,y2);double d1=std::hypot((x-x1)*(r.right-r.left),(y-y1)*(r.bottom-r.top)),d2=std::hypot((x-x2)*(r.right-r.left),(y-y2)*(r.bottom-r.top));drag=std::abs(d1-d2)<1?selectedPoint:d1<d2?0:1;SetCapture(hwnd);SetFocus(hwnd);StopDemo();UpdateCurvePoint(drag,x,y,false);return 0;}
 case WM_MOUSEMOVE:if(drag>=0&&(wp&MK_LBUTTON)){RECT client;GetClientRect(hwnd,&client);RECT r=CurvePlot(hwnd,client);double x=(GET_X_LPARAM(lp)-r.left)/(double)std::max<LONG>(1,r.right-r.left),y=1-(GET_Y_LPARAM(lp)-r.top)/(double)std::max<LONG>(1,r.bottom-r.top);UpdateCurvePoint(drag,x,y,false);}return 0;
 case WM_LBUTTONUP:{bool changed=drag>=0;drag=-1;if(GetCapture()==hwnd)ReleaseCapture();if(changed)StartDemo();return 0;}case WM_CAPTURECHANGED:drag=-1;return 0;case WM_DPICHANGED_AFTERPARENT:case WM_THEMECHANGED:InvalidateRect(hwnd,nullptr,FALSE);return 0;}
 return DefWindowProcW(hwnd,msg,wp,lp);
}
void PaintPreview(HWND hwnd,HDC dc){
 RECT rect;GetClientRect(hwnd,&rect);int width=rect.right,height=rect.bottom;if(width<1||height<1)return;Settings s=cfg;double p=demoPlaying?demoAnimation.Value(NowMs(),cfg):animation.Value(NowMs(),cfg);if(lastPage==1){s.length=s.gap=0;s.customCrosshair=false;s.dot=true;}
 double cx=std::floor(width/2.0),cy=std::floor(height/2.0);if(cfg.monitor>=0&&cfg.monitor<(int)monitors.size()){const RECT& monitor=monitors[cfg.monitor].bounds;cx+=((monitor.right-monitor.left)&1)*.5;cy+=((monitor.bottom-monitor.top)&1)*.5;}
 std::vector<uint32_t> pixels;Rasterize(s,MakeFrame(s,p),width,height,cx,cy,pixels);
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){size_t at=(size_t)y*width+x;uint32_t v=pixels[at];unsigned a=v>>24;unsigned bg=(x%24==0||y%24==0)?48:32;if(x==(int)cx||y==(int)cy)bg=56;unsigned b=(v&255)+(bg*(255-a)+127)/255,g=((v>>8)&255)+(bg*(255-a)+127)/255,r=((v>>16)&255)+(bg*(255-a)+127)/255;pixels[at]=std::min(255u,b)|(std::min(255u,g)<<8)|(std::min(255u,r)<<16);}
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;SetDIBitsToDevice(dc,0,0,width,height,0,0,0,height,pixels.data(),&info,DIB_RGB_COLORS);
}
void HandleCommand(int id,int code);
void LayoutPreviewButton(HWND hwnd){if(HWND button=GetDlgItem(hwnd,IDC_SIMULATE)){RECT r;GetClientRect(hwnd,&r);int width=D(hwnd,56),height=D(hwnd,28),margin=D(hwnd,8);SetWindowPos(button,nullptr,r.right-margin-width,r.bottom-margin-height,width,height,SWP_NOZORDER|SWP_NOACTIVATE);HFONT font=(HFONT)SendMessageW(gMain,WM_GETFONT,0,0);SendMessageW(button,WM_SETFONT,(WPARAM)(font?font:GetStockObject(DEFAULT_GUI_FONT)),TRUE);}}
LRESULT CALLBACK PreviewProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){if(msg==WM_ERASEBKGND)return 1;if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(msg==WM_COMMAND){HandleCommand(LOWORD(wp),HIWORD(wp));return 0;}if(msg==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);PaintPreview(hwnd,dc);EndPaint(hwnd,&ps);return 0;}if(msg==WM_SIZE||msg==WM_DPICHANGED||msg==WM_DPICHANGED_AFTERPARENT){LayoutPreviewButton(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;}if(msg==WM_THEMECHANGED){InvalidateRect(hwnd,nullptr,FALSE);return 0;}if(msg==WM_NCDESTROY&&gPreview==hwnd)gPreview=nullptr;return DefWindowProcW(hwnd,msg,wp,lp);}
LRESULT CALLBACK SwatchProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){case WM_ERASEBKGND:return 1;case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);const Color& color=GetDlgCtrlID(hwnd)==IDC_DOT_SWATCH?cfg.dotColor:cfg.color;Fill(dc,r,RGB(color.r,color.g,color.b));if(GetFocus()==hwnd){InflateRect(&r,-2,-2);DrawFocusRect(dc,&r);}EndPaint(hwnd,&ps);return 0;}
 case WM_LBUTTONUP:SetFocus(hwnd);PickColor(GetDlgCtrlID(hwnd)==IDC_DOT_SWATCH);return 0;case WM_KEYDOWN:if(wp==VK_SPACE||wp==VK_RETURN){PickColor(GetDlgCtrlID(hwnd)==IDC_DOT_SWATCH);return 0;}break;case WM_SETFOCUS:case WM_KILLFOCUS:case WM_THEMECHANGED:InvalidateRect(hwnd,nullptr,FALSE);return 0;}
 return DefWindowProcW(hwnd,msg,wp,lp);
}

void DockPreview(){
 if(!IsWindow(gMain)||!IsWindow(gPreview))return;RECT main=NativeWindowBounds(gMain);int top=main.top+D(gMain,64);if(IsWindow(pages[0])){RECT page;GetWindowRect(pages[0],&page);top=page.top;}
 int side=D(gMain,240);SetWindowPos(gPreview,nullptr,main.right,top,side,side,SWP_NOACTIVATE|SWP_NOZORDER);LayoutPreviewButton(gPreview);
 if(IsWindowVisible(gMain)&&!IsIconic(gMain))ShowWindow(gPreview,SW_SHOWNOACTIVATE);else ShowWindow(gPreview,SW_HIDE);
}
void LayoutFooter(HWND hwnd){
 RECT client,button;GetClientRect(hwnd,&client);HWND exit=GetDlgItem(hwnd,IDOK),minimize=GetDlgItem(hwnd,IDCANCEL);GetWindowRect(minimize,&button);MapWindowPoints(nullptr,hwnd,(POINT*)&button,2);int height=button.bottom-button.top,gap=D(hwnd,8),right=client.right-D(hwnd,14),trayWidth=D(hwnd,136),exitWidth=D(hwnd,88);
 SetWindowPos(minimize,nullptr,right-trayWidth,button.top,trayWidth,height,SWP_NOZORDER|SWP_NOACTIVATE);SetWindowPos(exit,nullptr,right-trayWidth-gap-exitWidth,button.top,exitWidth,height,SWP_NOZORDER|SWP_NOACTIVATE);
 SendMessageW(exit,BM_SETSTYLE,BS_PUSHBUTTON,TRUE);SendMessageW(hwnd,DM_SETDEFID,0,0);
}
void HideSettings(){SaveLive();StopDemo();testActive=false;Target();if(IsWindow(gPreview)){CheckDlgButton(gPreview,IDC_SIMULATE,BST_UNCHECKED);ShowWindow(gPreview,SW_HIDE);}if(IsWindow(gMain))ShowWindow(gMain,SW_HIDE);FinishHotkeyCapture();}
bool HandleSettingsKey(MSG& msg){
 if(!IsWindow(gMain)||!IsWindowEnabled(gMain)||!IsWindowVisible(gMain)||msg.message!=WM_KEYDOWN)return false;
 if(msg.hwnd!=gMain&&!IsChild(gMain,msg.hwnd)&&msg.hwnd!=gPreview&&!IsChild(gPreview,msg.hwnd))return false;
 if(HotkeyIndex(msg.hwnd)>=0||HotkeyIndex(GetFocus())>=0)return false;
 if(msg.wParam==VK_ESCAPE){HideSettings();return true;}if(msg.wParam!=VK_RETURN)return false;
 HWND focus=GetFocus();wchar_t cls[64]{};GetClassNameW(focus,cls,64);
 if(_wcsicmp(cls,L"Button")==0){SendMessageW(focus,BM_CLICK,0,0);return true;}
 if(_wcsicmp(cls,L"ComboBox")==0){SendMessageW(focus,CB_SHOWDROPDOWN,FALSE,0);return true;}
 if(_wcsicmp(cls,SwatchClass)==0){SendMessageW(focus,WM_KEYDOWN,VK_RETURN,0);return true;}
 if(auto* b=FindBinding(GetDlgCtrlID(focus))){bool before=syncing;syncing=true;BindingText(*b);syncing=before;}SaveLive();return true;
}

void InitializePage(HWND hwnd,int index){
 pages[index]=hwnd;EnableThemeDialogTexture(hwnd,cfg.skin?ETDT_DISABLE:ETDT_ENABLETAB);
 if(index==0){AddChoices(GetDlgItem(hwnd,IDC_CROSS_MODE),{L"十字准星",L"自绘准星"});Bind(hwnd,IDC_LENGTH,&cfg.length,1,0,120);Bind(hwnd,IDC_GAP,&cfg.gap,1,0,80);Bind(hwnd,IDC_THICKNESS,&cfg.thickness,1,1,32);Bind(hwnd,IDC_CROSS_SIZE,&cfg.crossSize,1,0,200);Bind(hwnd,IDC_OPACITY,&cfg.opacity,100,0,100);Bind(hwnd,IDC_ANGLE,&cfg.angle,1,0,360);}
 else if(index==1){AddChoices(GetDlgItem(hwnd,IDC_DOT_SHAPE),{L"方形",L"圆形",L"尖三角（尖端居中）",L"自绘"});SetDlgItemTextW(hwnd,613,L"自绘尺寸 0 为原大小；三角尖端居中。");Bind(hwnd,IDC_DOT_SIZE,&cfg.dotSize,1,1,64);Bind(hwnd,IDC_DOT_OPACITY,&cfg.dotOpacity,100,0,100);}
 else if(index==2){AddChoices(GetDlgItem(hwnd,IDC_ACTION),{L"保持原样",L"渐隐隐藏",L"收拢为中心点"});AddChoices(GetDlgItem(hwnd,IDC_TRIGGER),{L"按住生效，松开恢复",L"点按切换，再次点按恢复"});Bind(hwnd,IDC_ENTER_DURATION,&cfg.enterMs,1,0,2000);Bind(hwnd,IDC_LEAVE_DURATION,&cfg.leaveMs,1,0,2000);HWND combo=GetDlgItem(hwnd,IDC_EASING);auto add=[&](const wchar_t* label,int kind){int i=(int)SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)label);SendMessageW(combo,CB_SETITEMDATA,i,kind);};add(L"线性",0);for(const auto& p:kEasingPresets)add(p.label,p.id);add(L"自定义",4);SendMessageW(combo,CB_SETMINVISIBLE,6,0);}
 else if(index==3){Bind(hwnd,IDC_OFFSET_X,&cfg.offsetX,1,-500,500);Bind(hwnd,IDC_OFFSET_Y,&cfg.offsetY,1,-500,500);AddChoices(GetDlgItem(hwnd,IDC_OFFSET_STEP),{L"1 px",L"0.25 px（四分之一像素）",L"0.0625 px（十六分之一像素）"});RefreshMonitors();}
 else if(index==4){for(int i=0;i<kHotkeyCount;i++){HWND field=GetDlgItem(hwnd,IDC_HOTKEY_BASE+i);SendMessageW(field,HKM_SETRULES,0,0);SetWindowSubclass(field,HotkeySubclass,2,0);}}
 else if(index==5)AddChoices(GetDlgItem(hwnd,IDC_SKIN),{L"默认",L"复古"});
 SyncControls();
}
void HandleCommand(int id,int code){
 if(syncing)return;
 if(code==EN_CHANGE&&id>=IDC_HOTKEY_BASE&&id<IDC_HOTKEY_BASE+kHotkeyCount){HotkeyRecordChanged(id-IDC_HOTKEY_BASE);return;}
 if(code==EN_CHANGE){if(auto* b=FindBinding(id)){wchar_t text[64];GetWindowTextW(b->edit,text,64);wchar_t* end;double v=wcstod(text,&end);if(end!=text&&*end==0&&std::isfinite(v)){v=Canonical(*b,v);*b->value=v/b->scale;SendMessageW(b->slider,TBM_SETPOS,TRUE,(LPARAM)std::lround(v));Changed();if(id==IDC_ENTER_DURATION||id==IDC_LEAVE_DURATION)StartDemo();}return;}}
 if(code==EN_KILLFOCUS){if(auto* b=FindBinding(id)){bool before=syncing;syncing=true;BindingText(*b);syncing=before;return;}}
 if(code==CBN_SELCHANGE){int selected=(int)SendMessageW(C(id),CB_GETCURSEL,0,0);if(id==IDC_CROSS_MODE)cfg.customCrosshair=selected==1;else if(id==IDC_DOT_SHAPE)cfg.dotShape=selected;else if(id==IDC_ACTION)cfg.action=selected;else if(id==IDC_TRIGGER){cfg.trigger=selected;rightActive=false;Target();}else if(id==IDC_EASING)cfg.easing=SelectedCurve();else if(id==IDC_MONITOR)cfg.monitor=selected;else if(id==IDC_OFFSET_STEP)cfg.offsetStep=selected;else if(id==IDC_SKIN){cfg.skin=selected==1?1:0;SetNativeSkin(cfg.skin);ApplyNativeSkin(gMain);ApplyNativeSkin(gPreview);DockPreview();}SyncControls();Changed();if(id==IDC_ACTION||id==IDC_EASING)StartDemo();return;}
 if(code!=BN_CLICKED)return;
 if(id>=IDC_HOTKEY_CLEAR_BASE&&id<IDC_HOTKEY_CLEAR_BASE+kHotkeyCount){cfg.hotkeys[id-IDC_HOTKEY_CLEAR_BASE]={};hotkeyMessage.clear();SyncHotkeyControls();RebindHotkeys();Changed();return;}
 switch(id){case IDC_ENABLE:cfg.enabled=IsDlgButtonChecked(pages[0],id)==BST_CHECKED;Changed();break;case IDC_DOT_ENABLED:cfg.dot=IsDlgButtonChecked(pages[1],id)==BST_CHECKED;Changed();break;case IDC_ONLY_GAME:cfg.onlyGame=IsDlgButtonChecked(pages[3],id)==BST_CHECKED;Changed();break;case IDC_SIMULATE:testActive=IsDlgButtonChecked(gPreview,id)==BST_CHECKED;Target();break;
 case IDC_CROSS_COLOR:PickColor(false);break;case IDC_DOT_COLOR:PickColor(true);break;case IDC_EDIT_CROSS:OpenPixelEditor(false);break;case IDC_EDIT_DOT:OpenPixelEditor(true);break;
 case IDC_X_MINUS:case IDC_X_PLUS:case IDC_Y_MINUS:case IDC_Y_PLUS:Nudge(id);break;case IDC_REFRESH_MONITORS:RefreshMonitors();Changed();break;
 case IDC_POINT1:case IDC_POINT2:selectedPoint=id-IDC_POINT1;SyncCurve();SetFocus(C(IDC_CURVE_GRAPH));InvalidateRect(C(IDC_CURVE_GRAPH),nullptr,FALSE);break;case IDC_REPLAY:StartDemo();break;case IDC_RESET_CURVE:cfg.easing=4;SelectCurve(4);SyncCurve();Changed();StartDemo();break;
 case IDC_RESET_HOTKEYS:cfg.hotkeys=HotkeyDefaults();hotkeyMessage.clear();SyncHotkeyControls();RebindHotkeys();Changed();break;
 case IDC_RESET_DEFAULTS:StopDemo();session.ResetDefaults();rightActive=testActive=false;animation.Reset();hotkeyMessage.clear();RebindHotkeys();SetNativeSkin(cfg.skin);ApplyNativeSkin(gMain);ApplyNativeSkin(gPreview);RefreshMonitors();SyncControls();Changed();break;case IDC_EXIT:PostMessageW(gHost,WM_CLOSE,0,0);break;}
}
INT_PTR CALLBACK PageProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 if(msg==WM_INITDIALOG){auto* page=(PROPSHEETPAGEW*)lp;int index=(int)page->lParam;SetWindowLongPtrW(hwnd,GWLP_USERDATA,index+1);InitializePage(hwnd,index);return TRUE;}
 int index=(int)GetWindowLongPtrW(hwnd,GWLP_USERDATA)-1;
 if(msg==WM_COMMAND){HandleCommand(LOWORD(wp),HIWORD(wp));return TRUE;}
 if(msg==WM_HSCROLL&&!syncing){for(auto& b:bindings)if(b.slider==(HWND)lp){double value=SendMessageW(b.slider,TBM_GETPOS,0,0)/b.scale;if(IsOffset(b.id))value+=*b.value-std::round(*b.value);*b.value=Canonical(b,value*b.scale)/b.scale;bool before=syncing;syncing=true;BindingText(b);syncing=before;Changed();if(b.id==IDC_ENTER_DURATION||b.id==IDC_LEAVE_DURATION)StartDemo();break;}return TRUE;}
 if(msg==WM_NOTIFY){auto* note=(NMHDR*)lp;if(note->code==PSN_SETACTIVE){lastPage=index;StopDemo();InvalidatePreviews();return TRUE;}if(note->code==PSN_QUERYCANCEL){HideSettings();SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,TRUE);return TRUE;}if(note->code==PSN_APPLY){SaveLive();SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,PSNRET_NOERROR);return TRUE;}}
 return FALSE;
}
LRESULT CALLBACK SheetSubclass(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
 if(msg==WM_COMMAND){if(LOWORD(wp)==IDOK){PostMessageW(gHost,WM_CLOSE,0,0);return 0;}if(LOWORD(wp)==IDCANCEL){HideSettings();return 0;}}
 if(msg==WM_CLOSE||(msg==WM_SYSCOMMAND&&(wp&0xFFF0)==SC_MINIMIZE)||(msg==WM_SIZE&&wp==SIZE_MINIMIZED)){HideSettings();return 0;}
 if(msg==WM_NCDESTROY){SaveLive();LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);RemoveWindowSubclass(hwnd,SheetSubclass,1);gMain=nullptr;gPreview=nullptr;for(auto& page:pages)page=nullptr;bindings.clear();StopDemo();testActive=false;if(!quitting)FinishHotkeyCapture();Target();return result;}
 if(msg==WM_WINDOWPOSCHANGED||msg==WM_SHOWWINDOW||msg==WM_DPICHANGED){LRESULT result=DefSubclassProc(hwnd,msg,wp,lp);RefreshNativeFrame(hwnd);if(msg==WM_DPICHANGED)LayoutFooter(hwnd);DockPreview();return result;}
 return DefSubclassProc(hwnd,msg,wp,lp);
}
int CALLBACK SheetCallback(HWND hwnd,UINT msg,LPARAM){if(msg==PSCB_INITIALIZED){gMain=hwnd;SetWindowSubclass(hwnd,SheetSubclass,1,0);HICON icon=(HICON)LoadImageW(gInstance,MAKEINTRESOURCEW(1),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_SHARED);if(icon){SendMessageW(hwnd,WM_SETICON,ICON_BIG,(LPARAM)icon);SendMessageW(hwnd,WM_SETICON,ICON_SMALL,(LPARAM)icon);}SetWindowTextW(hwnd,L"准星属性");SetWindowTextW(GetDlgItem(hwnd,IDOK),L"退出");SetWindowTextW(GetDlgItem(hwnd,IDCANCEL),L"最小化到托盘");if(HWND apply=GetDlgItem(hwnd,IDC_PROPERTY_APPLY))ShowWindow(apply,SW_HIDE);LayoutFooter(hwnd);UpdateStatus();}return 0;}
HWND CreateSettings(){
 if(IsWindow(gMain))return gMain;SetNativeSkin(cfg.skin);bindings.clear();for(auto& page:pages)page=nullptr;testActive=false;StopDemo();
 PROPSHEETPAGEW specs[PageCount]{};constexpr int ids[]={IDD_CROSSHAIR,IDD_DOT,IDD_ANIMATION,IDD_POSITION,IDD_HOTKEYS,IDD_SETTINGS};const wchar_t* titles[]={L"准星",L"点准星",L"动画",L"位置",L"快捷键",L"设置"};HPROPSHEETPAGE handles[PageCount]{};
 for(int i=0;i<PageCount;i++){specs[i].dwSize=sizeof(PROPSHEETPAGEW);specs[i].dwFlags=PSP_USETITLE|PSP_PREMATURE;specs[i].hInstance=gInstance;specs[i].pszTemplate=MAKEINTRESOURCEW(ids[i]);specs[i].pszTitle=titles[i];specs[i].pfnDlgProc=PageProc;specs[i].lParam=i;handles[i]=CreatePropertySheetPageW(&specs[i]);if(!handles[i]){for(int j=0;j<i;j++)DestroyPropertySheetPage(handles[j]);return nullptr;}}
 PROPSHEETHEADERW sheet{};sheet.dwSize=sizeof(sheet);sheet.dwFlags=PSH_MODELESS|PSH_USECALLBACK|PSH_USEHICON|PSH_NOAPPLYNOW|PSH_NOCONTEXTHELP;sheet.hwndParent=gHost;sheet.hInstance=gInstance;sheet.pszCaption=L"准星属性";sheet.hIcon=(HICON)LoadImageW(gInstance,MAKEINTRESOURCEW(1),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_SHARED);sheet.nPages=PageCount;sheet.nStartPage=std::clamp(lastPage,0,PageCount-1);sheet.phpage=handles;sheet.pfnCallback=SheetCallback;
 HWND result=(HWND)PropertySheetW(&sheet);if((INT_PTR)result==-1)return nullptr;gMain=result;SyncControls();gPreview=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_CLIENTEDGE,PreviewClass,L"准星实时预览",WS_POPUP|WS_BORDER|WS_CLIPCHILDREN,0,0,D(result,240),D(result,240),result,nullptr,gInstance,nullptr);if(gPreview)CreateWindowExW(0,L"Button",L"右键",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX|BS_PUSHLIKE,0,0,D(result,56),D(result,28),gPreview,(HMENU)(INT_PTR)IDC_SIMULATE,gInstance,nullptr);ApplyNativeSkin(gMain);ApplyNativeSkin(gPreview);DockPreview();return result;
}
void ShowSettings(){HWND window=CreateSettings();if(window){RECT r,area;GetWindowRect(window,&r);MONITORINFO monitor{sizeof(monitor)};if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)){area=monitor.rcWork;int total=r.right-r.left+D(window,240),left=std::max(area.left,std::min(r.left,area.right-total)),top=std::max(area.top,std::min(r.top,area.bottom-(r.bottom-r.top)));SetWindowPos(window,nullptr,left,top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);}ShowWindow(window,SW_RESTORE);DockPreview();SetForegroundWindow(window);}}
void CloseFinishedSheet(){if(IsWindow(gMain)&&!PropSheet_GetCurrentPageHwnd(gMain))DestroyWindow(gMain);}
void AddTray(){trayOK=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;if(trayOK){tray.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&tray);}}
void TrayMenu(){POINT point;GetCursorPos(&point);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"打开设置");AppendMenuW(menu,MF_STRING,2,cfg.enabled?L"隐藏准星":L"显示准星");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,L"退出");SetForegroundWindow(gHost);int action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,gHost,nullptr);DestroyMenu(menu);PostMessageW(gHost,WM_NULL,0,0);if(action==1)ShowSettings();else if(action==2)ToggleRuntime();else if(action==3)PostMessageW(gHost,WM_CLOSE,0,0);}
LRESULT CALLBACK HostProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 if(taskbarCreated&&msg==taskbarCreated){AddTray();if(!trayOK)ShowSettings();return 0;}
 switch(msg){case WM_MOUSE_EVENT:if(wp&&cfg.onlyGame&&!GameIsForeground())return 0;if(cfg.trigger==0)rightActive=wp!=0;else if(wp)rightActive=!rightActive;Target();return 0;
 case WM_TIMER:if(wp==T_ANIM)Render();else if(wp==T_SAVE)SaveLive();else if(wp==T_DEMO)TickDemo(NowMs());else if(wp==T_HEART){HWND foreground=GetForegroundWindow();bool visible=cfg.enabled&&!artEditing&&(!cfg.onlyGame||GameIsForeground());if(foreground!=lastForeground||visible!=lastVisible){lastForeground=foreground;Render();}}return 0;
 case WM_HOTKEY:if(hotkeyCapture||(runtimeInitialized&&(wp<1||wp>kHotkeyCount||!hotkeyRegistered[wp-1])))return 0;if(runtimeInitialized){const auto& key=registeredHotkeys[wp-1];if(HIWORD(lp)!=key.key||(LOWORD(lp)&7)!=key.modifiers)return 0;}if(wp==1)ToggleRuntime();else if(wp==2)ShowSettings();else if(wp==3)PostMessageW(hwnd,WM_CLOSE,0,0);else if(wp>=4&&wp<=7){constexpr int actions[]={IDC_X_MINUS,IDC_X_PLUS,IDC_Y_MINUS,IDC_Y_PLUS};Nudge(actions[wp-4],true);}return 0;
 case WM_CAPTURE_END:FinishHotkeyCapture();return 0;
 case WM_SHOW_APP:ShowSettings();return 0;case WM_TRAY:if(LOWORD(lp)==NIN_SELECT||LOWORD(lp)==WM_LBUTTONDBLCLK)ShowSettings();else if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==WM_RBUTTONUP)TrayMenu();return 0;
 case WM_DISPLAYCHANGE:RefreshMonitors();Changed();DockPreview();return 0;case WM_CLOSE:if(!SaveLive()&&session.HasChanges()){ShowSettings();if(MessageBoxW(gMain,L"设置已生效，但无法保存到程序目录。\n仍要退出并丢弃本次未保存的设置吗？",L"无法保存设置",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return 0;}quitting=true;StopDemo();if(IsWindow(gMain))DestroyWindow(gMain);DestroyWindow(hwnd);return 0;
 case WM_DESTROY:KillTimer(hwnd,T_HEART);KillTimer(hwnd,T_ANIM);KillTimer(hwnd,T_SAVE);KillTimer(hwnd,T_DEMO);StopMouseInput();overlay.Destroy();if(tray.hWnd)Shell_NotifyIconW(NIM_DELETE,&tray);UnregisterHotkeys();runtimeInitialized=false;PostQuitMessage(0);return 0;
 case WM_QUERYENDSESSION:SaveLive();return TRUE;}
 return DefWindowProcW(hwnd,msg,wp,lp);
}
bool RegisterNativeClasses(HINSTANCE instance){gInstance=instance;WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpfnWndProc=HostProc;cls.lpszClassName=HostClass;if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;cls.lpfnWndProc=PreviewProc;cls.lpszClassName=PreviewClass;if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;cls.lpfnWndProc=SwatchProc;cls.lpszClassName=SwatchClass;if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;cls.lpfnWndProc=CurveProc;cls.lpszClassName=CurveClass;cls.hCursor=LoadCursorW(nullptr,IDC_CROSS);return RegisterClassExW(&cls)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS;}
void InitializeRuntime(){
 overlayOK=overlay.Create(gInstance);mouseOK=StartMouseInput(gHost);runtimeInitialized=true;RebindHotkeys();
 tray.cbSize=sizeof(tray);tray.hWnd=gHost;tray.uID=1;tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;tray.uCallbackMessage=WM_TRAY;tray.hIcon=(HICON)LoadImageW(gInstance,MAKEINTRESOURCEW(1),IMAGE_ICON,0,0,LR_DEFAULTSIZE|LR_SHARED);if(!tray.hIcon)tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"Hunt Crosshair · 双击打开设置");AddTray();SetTimer(gHost,T_HEART,100,nullptr);Render();
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\HuntCrosshair.Portable.v1");if(GetLastError()==ERROR_ALREADY_EXISTS){HWND other=FindWindowW(HostClass,nullptr);if(other)PostMessageW(other,WM_SHOW_APP,0,0);if(mutex)CloseHandle(mutex);return 0;}
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_HOTKEY_CLASS};InitCommonControlsEx(&controls);Settings loaded;LoadSettings(loaded);session.Begin(loaded);SetNativeSkin(cfg.skin);taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");if(!RegisterNativeClasses(instance))return 1;gHost=CreateWindowExW(WS_EX_TOOLWINDOW,HostClass,L"Hunt Crosshair Runtime",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,nullptr);if(!gHost)return 1;InitializeRuntime();ShowSettings();if(!gMain){MessageBoxW(nullptr,L"无法创建原生属性窗口。",L"Hunt Crosshair",MB_OK|MB_ICONERROR);DestroyWindow(gHost);return 1;}
 MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!HandleSettingsKey(msg)&&(!IsWindow(gMain)||!PropSheet_IsDialogMessage(gMain,&msg))){TranslateMessage(&msg);DispatchMessageW(&msg);}CloseFinishedSheet();}if(mutex)CloseHandle(mutex);return 0;
}
