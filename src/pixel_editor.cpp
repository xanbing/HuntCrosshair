#include "pixel_editor.h"
#include "native_theme.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace hc {
namespace {
PixelArt NormalizeArt(const PixelArt& source) {
    PixelArt result; result.width=std::clamp(source.width,1,100);
    result.height=std::clamp(source.height,1,100);
    result.pixels.assign(static_cast<size_t>(result.width)*result.height,0);
    if(source.width>0 && source.height>0)
        for(int y=0;y<result.height && y<source.height;y++)
            for(int x=0;x<result.width && x<source.width;x++) {
                size_t i=static_cast<size_t>(y)*source.width+x;
                if(i<source.pixels.size())result.pixels[static_cast<size_t>(y)*result.width+x]=source.pixels[i];
            }
    return result;
}
bool ClipLine(int& x0,int& y0,int& x1,int& y1,int width,int height) {
    double ax=x0,ay=y0,dx=static_cast<double>(x1)-x0,dy=static_cast<double>(y1)-y0;
    double lo=0,hi=1;
    auto edge=[&](double p,double q){
        if(p==0)return q>=0;
        double t=q/p;
        if(p<0){if(t>hi)return false;lo=std::max(lo,t);}
        else{if(t<lo)return false;hi=std::min(hi,t);}return true;
    };
    if(!edge(-dx,ax)||!edge(dx,width-1-ax)||!edge(-dy,ay)||!edge(dy,height-1-ay))return false;
    x0=std::clamp(static_cast<int>(std::lround(ax+lo*dx)),0,width-1);
    y0=std::clamp(static_cast<int>(std::lround(ay+lo*dy)),0,height-1);
    x1=std::clamp(static_cast<int>(std::lround(ax+hi*dx)),0,width-1);
    y1=std::clamp(static_cast<int>(std::lround(ay+hi*dy)),0,height-1);return true;
}
}

PixelEditorModel::PixelEditorModel(const PixelArt& art):art_(NormalizeArt(art)),strokeStart_(art_){}
void PixelEditorModel::Commit(const PixelArt& before){
    if(undo_.size()>=64)undo_.erase(undo_.begin());
    undo_.push_back(before);redo_.clear();
}
void PixelEditorModel::BeginStroke(){EndStroke();strokeStart_=art_;stroke_=true;changed_=false;}
bool PixelEditorModel::PaintCell(int x,int y,uint8_t alpha){
    if(x<0||y<0||x>=art_.width||y>=art_.height)return false;
    auto& pixel=art_.pixels[static_cast<size_t>(y)*art_.width+x];
    if(pixel==alpha)return false;
    if(!stroke_)BeginStroke();pixel=alpha;changed_=true;return true;
}
bool PixelEditorModel::PaintLine(int x0,int y0,int x1,int y1,uint8_t alpha){
    if(!ClipLine(x0,y0,x1,y1,art_.width,art_.height))return false;
    int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    bool changed=false;
    for(;;){changed=PaintCell(x0,y0,alpha)||changed;if(x0==x1&&y0==y1)break;
        int twice=2*err;if(twice>=dy){err+=dy;x0+=sx;}if(twice<=dx){err+=dx;y0+=sy;}}
    return changed;
}
bool PixelEditorModel::EndStroke(){
    if(!stroke_)return false;bool changed=changed_;stroke_=false;changed_=false;
    if(changed)Commit(strokeStart_);return changed;
}
bool PixelEditorModel::SetCanvasSize(int width,int height){
    EndStroke();width=std::clamp(width,1,100);height=std::clamp(height,1,100);
    if(width==art_.width&&height==art_.height)return false;
    PixelArt before=art_,after;after.width=width;after.height=height;
    after.pixels.assign(static_cast<size_t>(width)*height,0);
    int dx=(width-art_.width)/2,dy=(height-art_.height)/2;
    for(int y=0;y<art_.height;y++)for(int x=0;x<art_.width;x++)
        if(x+dx>=0&&x+dx<width&&y+dy>=0&&y+dy<height)
            after.pixels[static_cast<size_t>(y+dy)*width+x+dx]=art_.pixels[static_cast<size_t>(y)*art_.width+x];
    art_=std::move(after);Commit(before);return true;
}
bool PixelEditorModel::Undo(){EndStroke();if(undo_.empty())return false;
    redo_.push_back(art_);art_=std::move(undo_.back());undo_.pop_back();return true;}
bool PixelEditorModel::Redo(){EndStroke();if(redo_.empty())return false;
    undo_.push_back(art_);art_=std::move(redo_.back());redo_.pop_back();return true;}
bool PixelEditorModel::Clear(){EndStroke();if(std::none_of(art_.pixels.begin(),art_.pixels.end(),[](uint8_t p){return p!=0;}))return false;
    PixelArt before=art_;std::fill(art_.pixels.begin(),art_.pixels.end(),0);Commit(before);return true;}

namespace {
constexpr wchar_t kEditorClass[]=L"HuntCrosshair.PixelEditor.v1";
constexpr int kBrush=201,kErase=202,kUndo=203,kRedo=204,kClear=205,kGrid=206,
              kWidth=207,kHeight=208,kResize=209,kDimensions=210,kHeader=211,kPreview=212;
constexpr DWORD kEditorStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN;
constexpr DWORD kEditorEx=WS_EX_DLGMODALFRAME;
RECT OuterSize(int layoutDpi,int monitorDpi){
    RECT r{0,0,MulDiv(740,layoutDpi,96),MulDiv(624,layoutDpi,96)};
    AdjustWindowRectExForDpi(&r,kEditorStyle,FALSE,kEditorEx,monitorDpi);return r;
}
int FitLayoutDpi(int desired,RECT work){
    for(int density=desired;density>=48;--density){RECT r=OuterSize(density,desired);
        if(r.right-r.left<=work.right-work.left-8&&r.bottom-r.top<=work.bottom-work.top-8)return density;}
    return 48;
}
int EnterCommand(int focusedId){
    if(focusedId==kWidth||focusedId==kHeight)return kResize;
    if(focusedId==IDOK||focusedId==IDCANCEL||(focusedId>=kBrush&&focusedId<=kResize))return focusedId;
    return IDOK;
}
struct EditorControl{HWND hwnd;int x,y,w,h;};
enum class EditorExit { Running, Apply, Cancel, External, Quit };
struct EditorContext {
    explicit EditorContext(const PixelArt& art,Color c):model(art),color(c){}
    PixelEditorModel model;Color color;
    HWND hwnd=nullptr;HINSTANCE instance=nullptr;int dpi=96;
    HFONT font=nullptr,titleFont=nullptr;HBITMAP previewBitmap=nullptr;
    bool done=false,accepted=false,eraser=false,grid=true,drawing=false;
    EditorExit exit=EditorExit::Running;
    int lastX=0,lastY=0;uint8_t ink=255;
    std::vector<EditorControl> controls;
};
int Scale(const EditorContext& c,int n){return MulDiv(n,c.dpi,96);}
RECT Box(const EditorContext& c,int x,int y,int w,int h){return{Scale(c,x),Scale(c,y),Scale(c,x+w),Scale(c,y+h)};}
void FillBox(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void StrokeLine(HDC dc,int x0,int y0,int x1,int y1,COLORREF color,int width=1){
    HPEN pen=CreatePen(PS_SOLID,width,color);auto old=SelectObject(dc,pen);
    MoveToEx(dc,x0,y0,nullptr);LineTo(dc,x1,y1);SelectObject(dc,old);DeleteObject(pen);
}
void MakeFonts(EditorContext& c){
    if(c.font)DeleteObject(c.font);if(c.titleFont)DeleteObject(c.titleFont);
    NONCLIENTMETRICSW metrics{};metrics.cbSize=sizeof(metrics);
    if(!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0,c.dpi)){
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);
        metrics.lfMessageFont.lfHeight=MulDiv(metrics.lfMessageFont.lfHeight,c.dpi,GetDpiForSystem());
    }
    c.font=CreateFontIndirectW(&metrics.lfMessageFont);
    metrics.lfMessageFont.lfWeight=FW_BOLD;c.titleFont=CreateFontIndirectW(&metrics.lfMessageFont);
}
HWND AddControl(EditorContext& c,const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h){
    bool edit=wcscmp(cls,L"EDIT")==0,tab=wcscmp(cls,L"STATIC")!=0 && (style&BS_TYPEMASK)!=BS_GROUPBOX;
    HWND child=CreateWindowExW(edit?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|(tab?WS_TABSTOP:0)|style,Scale(c,x),Scale(c,y),Scale(c,w),Scale(c,h),c.hwnd,(HMENU)(INT_PTR)id,c.instance,nullptr);
    SendMessageW(child,WM_SETFONT,(WPARAM)c.font,TRUE);c.controls.push_back({child,x,y,w,h});return child;
}
void UpdateControls(EditorContext& c){
    const auto& a=c.model.GetArt();wchar_t text[16];swprintf(text,16,L"%d",a.width);SetDlgItemTextW(c.hwnd,kWidth,text);
    swprintf(text,16,L"%d",a.height);SetDlgItemTextW(c.hwnd,kHeight,text);
    EnableWindow(GetDlgItem(c.hwnd,kUndo),c.model.CanUndo());EnableWindow(GetDlgItem(c.hwnd,kRedo),c.model.CanRedo());
    SendDlgItemMessageW(c.hwnd,kBrush,BM_SETCHECK,c.eraser?BST_UNCHECKED:BST_CHECKED,0);
    SendDlgItemMessageW(c.hwnd,kErase,BM_SETCHECK,c.eraser?BST_CHECKED:BST_UNCHECKED,0);
    wchar_t dimensions[72];swprintf(dimensions,72,L"当前 %d × %d 像素；中心线仅供参考。",a.width,a.height);SetDlgItemTextW(c.hwnd,kDimensions,dimensions);
    InvalidateRect(c.hwnd,nullptr,FALSE);
}
struct Board{RECT rect;int cell;};
Board GetBoard(const EditorContext& c){
    const auto& a=c.model.GetArt();int extent=Scale(c,448);
    int cell=std::max(1,extent/std::max(a.width,a.height));
    int width=cell*a.width,height=cell*a.height;
    int left=Scale(c,24)+(extent-width)/2,top=Scale(c,112)+(extent-height)/2;
    return{{left,top,left+width,top+height},cell};
}
POINT BoardCell(const EditorContext& c,int x,int y){Board b=GetBoard(c);
    return{static_cast<LONG>(std::floor((x-b.rect.left)/(double)b.cell)),
           static_cast<LONG>(std::floor((y-b.rect.top)/(double)b.cell))};}
void FinishStroke(EditorContext& c){
    if(!c.drawing)return;c.drawing=false;c.model.EndStroke();
    if(GetCapture()==c.hwnd)ReleaseCapture();UpdateControls(c);
}
void RequestClose(EditorContext& c,bool apply){
    FinishStroke(c);c.accepted=apply;c.exit=apply?EditorExit::Apply:EditorExit::Cancel;c.done=true;
    PostThreadMessageW(GetCurrentThreadId(),WM_NULL,0,0);
    // The modal caller first re-enables its owner, then destroys this window.
    // Destroying an active modal while its owner is disabled loses activation.
}
void UpdatePreview(EditorContext& c){
    HWND preview=GetDlgItem(c.hwnd,kPreview);if(!preview)return;
    constexpr int size=128;std::vector<uint32_t> pixels(size*size);
    const auto& a=c.model.GetArt();double left=(size-a.width)*.5,top=(size-a.height)*.5;
    for(int y=0;y<size;y++)for(int x=0;x<size;x++){
        double alpha=0;int sx=static_cast<int>(std::floor(x-left)),sy=static_cast<int>(std::floor(y-top));
        for(int j=sy-1;j<=sy+1;j++)for(int i=sx-1;i<=sx+1;i++)if(i>=0&&i<a.width&&j>=0&&j<a.height){
            double cover=std::max(0.0,std::min(x+1.0,left+i+1)-std::max((double)x,left+i))*
                         std::max(0.0,std::min(y+1.0,top+j+1)-std::max((double)y,top+j));
            alpha+=cover*a.pixels[static_cast<size_t>(j)*a.width+i]/255.0;
        }
        alpha=std::clamp(alpha,0.0,1.0);int bg=((x/8+y/8)&1)?225:242;
        int r=(int)std::lround(std::clamp(c.color.r,0,255)*alpha+bg*(1-alpha));
        int g=(int)std::lround(std::clamp(c.color.g,0,255)*alpha+bg*(1-alpha));
        int b=(int)std::lround(std::clamp(c.color.b,0,255)*alpha+bg*(1-alpha));
        pixels[static_cast<size_t>(y)*size+x]=(r<<16)|(g<<8)|b;
    }
    BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=size;bi.bmiHeader.biHeight=-size;
    bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=24;bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bitmap||!bits)return;
    auto* bytes=static_cast<uint8_t*>(bits);
    for(size_t i=0;i<pixels.size();++i){bytes[i*3]=(uint8_t)pixels[i];bytes[i*3+1]=(uint8_t)(pixels[i]>>8);bytes[i*3+2]=(uint8_t)(pixels[i]>>16);}
    auto previous=(HBITMAP)SendMessageW(preview,STM_SETIMAGE,IMAGE_BITMAP,(LPARAM)bitmap);
    if(previous)DeleteObject(previous);c.previewBitmap=bitmap;
}
void PaintEditor(EditorContext& c,HDC dc){
    RECT client;GetClientRect(c.hwnd,&client);FillRect(dc,&client,GetSysColorBrush(COLOR_3DFACE));
    Board board=GetBoard(c);const auto& a=c.model.GetArt();
    for(int y=0;y<a.height;y++)for(int x=0;x<a.width;x++){
        int bg=((x+y)&1)?226:244;double alpha=a.pixels[static_cast<size_t>(y)*a.width+x]/255.0;
        auto blend=[&](int color){return(int)std::lround(std::clamp(color,0,255)*alpha+bg*(1-alpha));};
        FillBox(dc,RECT{board.rect.left+x*board.cell,board.rect.top+y*board.cell,
                       board.rect.left+(x+1)*board.cell,board.rect.top+(y+1)*board.cell},
                RGB(blend(c.color.r),blend(c.color.g),blend(c.color.b)));
    }
    if(c.grid){
        for(int x=0;x<=a.width;x++)StrokeLine(dc,board.rect.left+x*board.cell,board.rect.top,board.rect.left+x*board.cell,board.rect.bottom,GetSysColor(COLOR_3DSHADOW));
        for(int y=0;y<=a.height;y++)StrokeLine(dc,board.rect.left,board.rect.top+y*board.cell,board.rect.right,board.rect.top+y*board.cell,GetSysColor(COLOR_3DSHADOW));
    }
    int cx=(board.rect.left+board.rect.right)/2,cy=(board.rect.top+board.rect.bottom)/2;
    StrokeLine(dc,cx,board.rect.top,cx,board.rect.bottom,GetSysColor(COLOR_HIGHLIGHT),std::max(1,Scale(c,2)));
    StrokeLine(dc,board.rect.left,cy,board.rect.right,cy,GetSysColor(COLOR_HIGHLIGHT),std::max(1,Scale(c,2)));
    RECT edge=board.rect;InflateRect(&edge,2,2);DrawEdge(dc,&edge,EDGE_SUNKEN,BF_RECT);
    UpdatePreview(c);
}
void CreateEditorControls(EditorContext& c){
    auto label=[&](const wchar_t* text,int id,int x,int y,int w,int h){return AddControl(c,L"STATIC",text,SS_LEFT|SS_NOPREFIX,id,x,y,w,h);};
    auto button=[&](const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=BS_PUSHBUTTON){return AddControl(c,L"BUTTON",text,style,id,x,y,w,h);};
    HWND header=label(L"像素画板",kHeader,24,20,680,25);SendMessageW(header,WM_SETFONT,(WPARAM)c.titleFont,TRUE);
    label(L"左键绘制，右键擦除；每次拖动可整体撤销。",0,24,50,680,22);
    button(L"画笔",kBrush,24,77,72,29,BS_AUTORADIOBUTTON|WS_GROUP);button(L"橡皮",kErase,104,77,72,29,BS_AUTORADIOBUTTON);
    button(L"撤销",kUndo,184,77,72,29,BS_PUSHBUTTON|WS_GROUP);button(L"重做",kRedo,264,77,72,29);button(L"清空",kClear,344,77,72,29);
    HWND grid=button(L"显示网格",kGrid,500,77,192,29,BS_AUTOCHECKBOX);SendMessageW(grid,BM_SETCHECK,BST_CHECKED,0);
    AddControl(c,L"BUTTON",L"画布尺寸",BS_GROUPBOX,0,490,108,218,164);
    label(L"宽：",0,500,151,34,24);label(L"高：",0,605,151,30,24);
    AddControl(c,L"EDIT",L"",ES_NUMBER|ES_RIGHT|ES_AUTOHSCROLL,kWidth,534,146,60,30);
    AddControl(c,L"EDIT",L"",ES_NUMBER|ES_RIGHT|ES_AUTOHSCROLL,kHeight,638,146,60,30);
    button(L"居中调整 / 裁剪",kResize,500,188,198,30);
    label(L"每轴 1–100 像素。\n缩小会裁剪边缘，可撤销。",0,500,229,198,36);
    AddControl(c,L"BUTTON",L"原图 1:1",BS_GROUPBOX,0,490,282,218,162);
    AddControl(c,L"STATIC",L"",SS_BITMAP|SS_CENTERIMAGE|SS_REALSIZECONTROL,kPreview,500,300,198,132);
    label(L"颜色来自准星设置。\n应用后立即生效并自动保存。\n取消不改变原图。\nCtrl+Z 撤销 / Ctrl+Y 重做",0,500,463,216,77);
    label(L"",kDimensions,24,575,460,24);
    button(L"取消",IDCANCEL,500,578,92,32);button(L"应用",IDOK,606,578,92,32,BS_DEFPUSHBUTTON);
    UpdateControls(c);UpdatePreview(c);
}
void ResizeEditorArt(EditorContext& c){
    BOOL okW=FALSE,okH=FALSE;UINT w=GetDlgItemInt(c.hwnd,kWidth,&okW,FALSE),h=GetDlgItemInt(c.hwnd,kHeight,&okH,FALSE);
    if(!okW||!okH||w<1||w>100||h<1||h>100){MessageBoxW(c.hwnd,L"宽和高请输入 1 到 100 的整数。",L"画布尺寸",MB_OK|MB_ICONINFORMATION);return;}
    FinishStroke(c);c.model.SetCanvasSize((int)w,(int)h);UpdateControls(c);
}
LRESULT CALLBACK EditorProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto* c=reinterpret_cast<EditorContext*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){c=static_cast<EditorContext*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);c->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)c);}
    if(!c)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_CREATE:MakeFonts(*c);CreateEditorControls(*c);ApplyNativeSkin(hwnd);return 0;
    case WM_SIZE:if(wp!=SIZE_MINIMIZED)RefreshNativeFrame(hwnd);break;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(mem,bitmap);
        PaintEditor(*c,mem);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);EndPaint(hwnd,&ps);return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{HDC dc=(HDC)wp;SetTextColor(dc,GetSysColor(COLOR_BTNTEXT));SetBkColor(dc,GetSysColor(COLOR_3DFACE));return(LRESULT)GetSysColorBrush(COLOR_3DFACE);}
    case WM_CTLCOLOREDIT:{HDC dc=(HDC)wp;SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));SetBkColor(dc,GetSysColor(COLOR_WINDOW));return(LRESULT)GetSysColorBrush(COLOR_WINDOW);}
    case WM_COMMAND:switch(LOWORD(wp)){
        case kBrush:FinishStroke(*c);c->eraser=false;UpdateControls(*c);return 0;
        case kErase:FinishStroke(*c);c->eraser=true;UpdateControls(*c);return 0;
        case kUndo:FinishStroke(*c);c->model.Undo();UpdateControls(*c);return 0;
        case kRedo:FinishStroke(*c);c->model.Redo();UpdateControls(*c);return 0;
        case kClear:FinishStroke(*c);c->model.Clear();UpdateControls(*c);return 0;
        case kGrid:c->grid=SendMessageW(GetDlgItem(hwnd,kGrid),BM_GETCHECK,0,0)==BST_CHECKED;InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case kResize:ResizeEditorArt(*c);return 0;
        case IDOK:RequestClose(*c,true);return 0;
        case IDCANCEL:RequestClose(*c,false);return 0;
        }break;
    case WM_LBUTTONDOWN:case WM_RBUTTONDOWN:{POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};Board b=GetBoard(*c);if(!PtInRect(&b.rect,point))return 0;
        FinishStroke(*c);c->model.BeginStroke();c->drawing=true;c->ink=(msg==WM_RBUTTONDOWN||c->eraser)?0:255;
        POINT cell=BoardCell(*c,point.x,point.y);c->lastX=cell.x;c->lastY=cell.y;c->model.PaintCell(cell.x,cell.y,c->ink);
        SetCapture(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_MOUSEMOVE:if(c->drawing){POINT cell=BoardCell(*c,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));c->model.PaintLine(c->lastX,c->lastY,cell.x,cell.y,c->ink);c->lastX=cell.x;c->lastY=cell.y;InvalidateRect(hwnd,nullptr,FALSE);}return 0;
    case WM_LBUTTONUP:case WM_RBUTTONUP:FinishStroke(*c);return 0;
    case WM_CAPTURECHANGED:if(c->drawing){c->drawing=false;c->model.EndStroke();UpdateControls(*c);}return 0;
    case WM_CANCELMODE:FinishStroke(*c);return 0;
    case WM_DPICHANGED:{FinishStroke(*c);RECT* proposed=(RECT*)lp;MONITORINFO info{};info.cbSize=sizeof(info);
        HMONITOR monitor=MonitorFromRect(proposed,MONITOR_DEFAULTTONEAREST);
        if(!GetMonitorInfoW(monitor,&info))SystemParametersInfoW(SPI_GETWORKAREA,0,&info.rcWork,0);
        int actualDpi=HIWORD(wp);c->dpi=FitLayoutDpi(actualDpi,info.rcWork);MakeFonts(*c);
        for(const auto& child:c->controls){SendMessageW(child.hwnd,WM_SETFONT,(WPARAM)(GetDlgCtrlID(child.hwnd)==kHeader?c->titleFont:c->font),TRUE);SetWindowPos(child.hwnd,nullptr,Scale(*c,child.x),Scale(*c,child.y),Scale(*c,child.w),Scale(*c,child.h),SWP_NOZORDER|SWP_NOACTIVATE);}
        RECT r=OuterSize(c->dpi,actualDpi);int width=r.right-r.left,height=r.bottom-r.top;
        int x=std::max<LONG>(info.rcWork.left,std::min<LONG>(proposed->left,info.rcWork.right-width));
        int y=std::max<LONG>(info.rcWork.top,std::min<LONG>(proposed->top,info.rcWork.bottom-height));
        SetWindowPos(hwnd,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);RefreshNativeFrame(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_CLOSE:RequestClose(*c,false);return 0;
    case WM_DESTROY:if(c->previewBitmap){HWND image=GetDlgItem(hwnd,kPreview);if(image)SendMessageW(image,STM_SETIMAGE,IMAGE_BITMAP,0);DeleteObject(c->previewBitmap);c->previewBitmap=nullptr;}if(GetCapture()==hwnd)ReleaseCapture();c->drawing=false;c->model.EndStroke();
        if(c->exit==EditorExit::Running){c->exit=EditorExit::External;c->accepted=false;}c->done=true;PostThreadMessageW(GetCurrentThreadId(),WM_NULL,0,0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}

bool EditPixelArt(HWND owner,HINSTANCE instance,PixelArt& art,Color color,const wchar_t* title){
    EditorContext context(art,color);context.instance=instance;
    context.dpi=owner?(int)GetDpiForWindow(owner):(int)GetDpiForSystem();
    if(context.dpi<=0)context.dpi=96;
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=EditorProc;wc.hInstance=instance;
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=kEditorClass;
    if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    int actualDpi=context.dpi;
    RECT work{};MONITORINFO info{};info.cbSize=sizeof(info);
    HMONITOR monitor=MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST);
    if(GetMonitorInfoW(monitor,&info))work=info.rcWork;else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    context.dpi=FitLayoutDpi(actualDpi,work);RECT r=OuterSize(context.dpi,actualDpi);
    RECT parent=work;if(owner) GetWindowRect(owner,&parent);
    int width=r.right-r.left,height=r.bottom-r.top;
    int x=parent.left+(parent.right-parent.left-width)/2,y=parent.top+(parent.bottom-parent.top-height)/2;
    x=std::max<LONG>(work.left,std::min<LONG>(x,work.right-width));y=std::max<LONG>(work.top,std::min<LONG>(y,work.bottom-height));
    HWND previousFocus=GetFocus();bool wasEnabled=owner&&IsWindowEnabled(owner);
    if(wasEnabled)EnableWindow(owner,FALSE);
    HWND hwnd=CreateWindowExW(kEditorEx,kEditorClass,title?title:L"像素准星编辑器",kEditorStyle,x,y,width,height,owner,nullptr,instance,&context);
    if(!hwnd){if(wasEnabled&&IsWindow(owner))EnableWindow(owner,TRUE);if(context.font)DeleteObject(context.font);if(context.titleFont)DeleteObject(context.titleFont);return false;}
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);SetFocus(GetDlgItem(hwnd,kBrush));
    MSG message{};bool quit=false;int quitCode=0;
    while(!context.done&&IsWindow(hwnd)){
        int result=GetMessageW(&message,nullptr,0,0);
        if(result<=0){quit=result==0;quitCode=(int)message.wParam;context.accepted=false;
            context.exit=quit?EditorExit::Quit:EditorExit::External;context.done=true;break;}
        if(message.message==WM_KEYDOWN){
            bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0;
            if(control&&(message.wParam=='Z'||message.wParam=='Y')){FinishStroke(context);if(message.wParam=='Z')context.model.Undo();else context.model.Redo();UpdateControls(context);continue;}
            if(message.wParam==VK_ESCAPE){SendMessageW(hwnd,WM_COMMAND,IDCANCEL,0);continue;}
            if(message.wParam==VK_RETURN){int command=EnterCommand(GetDlgCtrlID(GetFocus()));
                if(command==kGrid)SendMessageW(GetDlgItem(hwnd,kGrid),BM_CLICK,0,0);
                else SendMessageW(hwnd,WM_COMMAND,command,0);continue;}
        }
        if(!IsDialogMessageW(hwnd,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    bool ownerAlive=owner&&IsWindow(owner);
    bool ownerVisibleAtClose=ownerAlive&&IsWindowVisible(owner);
    bool ownerMinimizedAtClose=ownerAlive&&IsIconic(owner);
    bool editorWasForeground=GetForegroundWindow()==hwnd;
    bool editorWasActive=GetActiveWindow()==hwnd;
    bool normalClose=context.exit==EditorExit::Apply||context.exit==EditorExit::Cancel;
    if(owner&&!ownerAlive)context.accepted=false;
    // Restore the disabled state before destruction so Windows can activate
    // the correct owner naturally. An external owner close/quit stays closed.
    if(wasEnabled&&ownerAlive)EnableWindow(owner,TRUE);
    if(IsWindow(hwnd))DestroyWindow(hwnd);
    if(wasEnabled&&IsWindow(owner)&&normalClose&&!quit&&ownerVisibleAtClose&&!ownerMinimizedAtClose){
        if(!IsWindowVisible(owner)||IsIconic(owner))
            ShowWindow(owner,editorWasForeground?SW_RESTORE:SW_SHOWNOACTIVATE);
        SetWindowPos(owner,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(editorWasForeground||editorWasActive){
            SetActiveWindow(owner);if(editorWasForeground)SetForegroundWindow(owner);
            if(IsWindow(previousFocus)&&(previousFocus==owner||IsChild(owner,previousFocus)))SetFocus(previousFocus);
        }
    }
    if(context.font)DeleteObject(context.font);if(context.titleFont)DeleteObject(context.titleFont);
    if(quit)PostQuitMessage(quitCode);
    if(context.accepted&&!quit){art=context.model.GetArt();return true;}return false;
}
}
