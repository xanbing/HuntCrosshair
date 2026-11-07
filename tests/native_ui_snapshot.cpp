// Composite our own offscreen main PropertySheet and its owned preview only.
#define HC_NATIVE_UI_NO_TEST_MAIN
#include "native_ui_tests.cpp"
namespace native_fixture {
void PaintChildSurface(HDC dc,HWND child,RECT windowRect,int kind){
    if(!IsWindowVisible(child))return;
    POINT origin{};ClientToScreen(child,&origin);RECT client{};GetClientRect(child,&client);
    int savedDC=SaveDC(dc);SetViewportOrgEx(dc,origin.x-windowRect.left,origin.y-windowRect.top,nullptr);
    IntersectClipRect(dc,0,0,client.right,client.bottom);
    if(kind==0)PaintPreview(child,dc);else if(kind==1)PaintCurve(child,dc);
    else{const auto& color=GetDlgCtrlID(child)==IDC_DOT_SWATCH?cfg.dotColor:cfg.color;Fill(dc,client,RGB(color.r,color.g,color.b));}
    RestoreDC(dc,savedDC);
}
void PaintNativeChild(HDC destination,HWND child,RECT parentWindow){
    if(!IsWindowVisible(child))return;RECT rect{};GetWindowRect(child,&rect);const int width=rect.right-rect.left,height=rect.bottom-rect.top;
    HDC memory=CreateCompatibleDC(destination);HBITMAP bitmap=CreateCompatibleBitmap(destination,width,height);
    Check(memory&&bitmap,"native child snapshot buffer failed");auto old=SelectObject(memory,bitmap);
    RECT all{0,0,width,height};FillRect(memory,&all,GetSysColorBrush(COLOR_BTNFACE));
    if(!PrintWindow(child,memory,0))SendMessageW(child,WM_PRINT,(WPARAM)memory,PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND);
    BitBlt(destination,rect.left-parentWindow.left,rect.top-parentWindow.top,width,height,memory,0,0,SRCCOPY);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
}
void CompositeWindow(HDC destination,HWND window,RECT canvas){
    RECT rect{};GetWindowRect(window,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    HDC memory=CreateCompatibleDC(destination);HBITMAP bitmap=CreateCompatibleBitmap(destination,width,height);
    Check(memory&&bitmap,"window snapshot buffer failed");auto old=SelectObject(memory,bitmap);
    RECT all{0,0,width,height};FillRect(memory,&all,(HBRUSH)GetStockObject(WHITE_BRUSH));
    if(!PrintWindow(window,memory,0))SendMessageW(window,WM_PRINT,(WPARAM)memory,PRF_NONCLIENT|PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    // Production painters fill WM_PAINT-only surfaces; native controls and
    // nonclient borders come from the actual window's PrintWindow output.
    if(window==gPreview){PaintChildSurface(memory,gPreview,rect,0);PaintNativeChild(memory,C(IDC_SIMULATE),rect);}
    else{PaintChildSurface(memory,C(IDC_CURVE_GRAPH),rect,1);PaintChildSurface(memory,C(IDC_CROSS_SWATCH),rect,2);PaintChildSurface(memory,C(IDC_DOT_SWATCH),rect,2);}
    const int savedDestination=SaveDC(destination);HRGN shape=CreateRectRgn(0,0,0,0);
    if(shape&&GetWindowRgn(window,shape)!=ERROR){OffsetRgn(shape,rect.left-canvas.left,rect.top-canvas.top);ExtSelectClipRgn(destination,shape,RGN_AND);}
    BitBlt(destination,rect.left-canvas.left,rect.top-canvas.top,width,height,memory,0,0,SRCCOPY);
    RestoreDC(destination,savedDestination);if(shape)DeleteObject(shape);
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
}
void SaveNativeBitmap(const std::wstring& path){
    RECT main=VisibleWindowBounds(gMain),preview=VisibleWindowBounds(gPreview),canvas{};UnionRect(&canvas,&main,&preview);
    const int width=canvas.right-canvas.left,height=canvas.bottom-canvas.top;
    Check(width>0&&height>0&&width<4096&&height<4096,"combined snapshot dimensions invalid");
    HDC dc=CreateCompatibleDC(nullptr);Check(dc!=nullptr,"snapshot memory DC failed");
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap){DeleteDC(dc);throw std::runtime_error("snapshot DIB creation failed");}
    auto previous=SelectObject(dc,bitmap);Fill(dc,RECT{0,0,width,height},RGB(246,246,246));
    CompositeWindow(dc,gMain,canvas);CompositeWindow(dc,gPreview,canvas);GdiFlush();
    const DWORD bytes=static_cast<DWORD>(width*height*4);
    BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+bytes;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    bool ok=file!=INVALID_HANDLE_VALUE;DWORD written=0;
    if(ok)ok=WriteFile(file,&header,sizeof(header),&written,nullptr)&&written==sizeof(header);
    if(ok)ok=WriteFile(file,&info.bmiHeader,sizeof(info.bmiHeader),&written,nullptr)&&written==sizeof(info.bmiHeader);
    if(ok)ok=WriteFile(file,bits,bytes,&written,nullptr)&&written==bytes;
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);Check(ok,"combined native BMP write failed");
}
int RunSnapshots(){
    int count=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&count);
    std::wstring directory=count>1?args[1]:hc::AppDirectory()+L"\\native-ui-snapshots";if(args)LocalFree(args);
    if(!CreateDirectoryW(directory.c_str(),nullptr))Check(GetLastError()==ERROR_ALREADY_EXISTS,"snapshot directory creation failed");
    Environment environment;Open();hc::Settings shown;shown.dot=true;shown.dotShape=2;shown.dotSize=12;shown.easing=4;
    session.Begin(shown);SyncControls();Check(GetDpiForWindow(gMain)==96,"snapshot did not use 100 percent DPI");
    RECT dock{};GetWindowRect(gPreview,&dock);
    const wchar_t* files[]={L"01-crosshair.bmp",L"02-dot.bmp",L"03-animation.bmp",L"04-position.bmp",L"05-hotkeys.bmp",L"06-settings.bmp"};
    for(int skin=0;skin<2;++skin){
        shown.skin=skin;session.Begin(shown);hc::SetNativeSkin(skin);hc::ApplyNativeSkin(gMain);hc::ApplyNativeSkin(gPreview);SyncControls();
        Pump();GetWindowRect(gPreview,&dock);
        for(int page=0;page<6;++page){Check(PropSheet_SetCurSel(gMain,nullptr,page)!=FALSE,"snapshot tab selection failed");
            Pump();UpdateWindow(gMain);UpdateWindow(pages[page]);UpdateWindow(gPreview);RECT current{};GetWindowRect(gPreview,&current);
            Check(EqualRect(&dock,&current)!=FALSE,"preview dock shifted between snapshot pages");
            CheckNativeChromeAndLayout();
            Check(ComboText(C(IDC_SKIN),0)==L"默认"&&ComboText(C(IDC_SKIN),1)==L"复古","snapshot skin names are not exact labels");
            SaveNativeBitmap(directory+L"\\"+(skin?L"retro-":L"default-")+files[page]);}
    }
    hc::SetNativeSkin(0);
    Check(saves==0&&!overlay.Window(),"snapshot changed persistence or initialized overlay");
    std::wcout<<L"native 100% six-page/two-skin main+preview snapshots: "<<directory<<L"\n";return 0;
}
}
int main(){try{return native_fixture::RunSnapshots();}catch(const std::exception& error){std::cerr<<"native snapshot failure: "<<error.what()<<'\n';return 1;}}
