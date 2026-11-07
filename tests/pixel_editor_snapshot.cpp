// Renders this source editor in an isolated hidden window, without opening the
// modal API, activating another app, installing input hooks or saving settings.
#include "../src/pixel_editor.cpp"
#include <cstdio>
#include <cassert>

static bool WriteBitmap(const wchar_t* path,int width,int height,const void* bits){
    BITMAPFILEHEADER f{};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(BITMAPINFOHEADER);f.bfSize=f.bfOffBits+width*height*4;
    BITMAPINFOHEADER bi{};bi.biSize=sizeof(bi);bi.biWidth=width;bi.biHeight=-height;bi.biPlanes=1;bi.biBitCount=32;
    FILE* out=_wfopen(path,L"wb");if(!out)return false;
    bool ok=fwrite(&f,sizeof(f),1,out)==1&&fwrite(&bi,sizeof(bi),1,out)==1&&fwrite(bits,(size_t)width*height*4,1,out)==1;
    fclose(out);return ok;
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    assert(hc::EnterCommand(IDCANCEL)==IDCANCEL);
    assert(hc::EnterCommand(hc::kUndo)==hc::kUndo&&hc::EnterCommand(hc::kClear)==hc::kClear);
    assert(hc::EnterCommand(hc::kWidth)==hc::kResize&&hc::EnterCommand(hc::kHeight)==hc::kResize);
    RECT smallWork{0,0,1366,728};int fitted=hc::FitLayoutDpi(120,smallWork);RECT fittedSize=hc::OuterSize(fitted,120);
    assert(fitted<120&&fittedSize.right-fittedSize.left<=1358&&fittedSize.bottom-fittedSize.top<=720);
    HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=hc::EditorProc;
    wc.lpszClassName=L"HuntCrosshair.HiddenPixelEditorSnapshot";if(!RegisterClassW(&wc))return 1;
    for(int dpi:{96,144})for(int size:{20,100}){
        hc::PixelArt art;art.width=art.height=size;art.pixels.assign(size*size,0);
        hc::PixelEditorModel pattern(art);pattern.BeginStroke();
        if(size==20){pattern.PaintLine(3,9,16,9);pattern.PaintLine(3,10,16,10);pattern.PaintLine(9,3,9,16);pattern.PaintLine(10,3,10,16);}
        else{pattern.PaintLine(10,10,89,89);pattern.PaintLine(10,89,89,10);}
        pattern.EndStroke();hc::EditorContext context(pattern.GetArt(),hc::Color{});context.instance=instance;context.dpi=dpi;
        int width=hc::Scale(context,740),height=hc::Scale(context,624);
        HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"Hidden source editor fixture",WS_POPUP,0,0,width,height,nullptr,nullptr,instance,&context);
        if(!hwnd)return 2;
        for(int id:{hc::kBrush,hc::kErase,hc::kUndo,hc::kRedo,hc::kClear,hc::kResize,IDOK,IDCANCEL})
            assert((GetWindowLongPtrW(GetDlgItem(hwnd,id),GWL_STYLE)&BS_TYPEMASK)!=BS_OWNERDRAW);
        assert((GetWindowLongPtrW(GetDlgItem(hwnd,hc::kBrush),GWL_STYLE)&BS_TYPEMASK)==BS_AUTORADIOBUTTON);
        assert(SendDlgItemMessageW(hwnd,hc::kBrush,BM_GETCHECK,0,0)==BST_CHECKED);
        SendMessageW(hwnd,WM_COMMAND,hc::kErase,0);
        assert(context.eraser&&SendDlgItemMessageW(hwnd,hc::kErase,BM_GETCHECK,0,0)==BST_CHECKED);
        SendMessageW(hwnd,WM_COMMAND,hc::kBrush,0);assert(!context.eraser);
        assert(GetWindowLongPtrW(GetDlgItem(hwnd,hc::kWidth),GWL_EXSTYLE)&WS_EX_CLIENTEDGE);
        assert((GetWindowLongPtrW(GetDlgItem(hwnd,hc::kPreview),GWL_STYLE)&SS_TYPEMASK)==SS_BITMAP);
        assert(SendDlgItemMessageW(hwnd,hc::kPreview,STM_GETIMAGE,IMAGE_BITMAP,0)!=0);
        HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
        void* bits=nullptr;HBITMAP bmp=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);if(!bmp||!bits)return 3;
        auto old=SelectObject(dc,bmp);LARGE_INTEGER frequency{},before{},after{};
        QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&before);
        hc::PaintEditor(context,dc);GdiFlush();QueryPerformanceCounter(&after);
        std::printf("Editor %dx%d at %d%% source paint %.3f ms\n",size,size,dpi==96?100:150,
            (after.QuadPart-before.QuadPart)*1000.0/frequency.QuadPart);
        for(const auto& child:context.controls){RECT r;GetWindowRect(child.hwnd,&r);POINT origin{r.left,r.top};ScreenToClient(hwnd,&origin);
            int saved=SaveDC(dc);SetViewportOrgEx(dc,origin.x,origin.y,nullptr);IntersectClipRect(dc,0,0,r.right-r.left,r.bottom-r.top);
            SendMessageW(child.hwnd,WM_PRINT,(WPARAM)dc,PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND|PRF_CHILDREN);RestoreDC(dc,saved);}
        GdiFlush();wchar_t name[100];swprintf(name,100,L"build\\pixel-editor-%d-%dpct.bmp",size,dpi==96?100:150);
        if(!WriteBitmap(name,width,height,bits))return 4;std::wprintf(L"Rendered %ls\n",name);
        SelectObject(dc,old);DeleteObject(bmp);DeleteDC(dc);DestroyWindow(hwnd);
        DeleteObject(context.font);DeleteObject(context.titleFont);
    }
}
