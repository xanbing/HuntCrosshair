#include "platform.h"
#include "hotkeys.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>

namespace hc {
namespace {
constexpr wchar_t kOverlayClass[] = L"HuntCrosshair.Overlay.v1";
constexpr wchar_t kSettingsFile[] = L"HuntCrosshair.ini";
HHOOK gMouseHook = nullptr;
HWND gMouseReceiver = nullptr;

double Bound(double n, double lo, double hi, double fallback) {
    return std::isfinite(n) ? std::max(lo, std::min(hi, n)) : fallback;
}
void SanitizeArt(PixelArt& art) {
    if (art.width<1 || art.width>100 || art.height<1 || art.height>100 ||
        art.pixels.size()!=static_cast<size_t>(art.width)*art.height) art=PixelArt{};
}
void NormalizeEasing(Settings& s) {
    // These v1-v3 definitions are deliberately independent of the current UI
    // preset registry: removing an entry must not change old saved animations.
    struct LegacyCurve {int id;double x1,y1,x2,y2;};
    static constexpr LegacyCurve legacy[]={
        {3,.42,0,.58,1}, {5,.25,.1,.25,1},
        {6,.12,0,.39,0}, {7,.61,1,.88,1}, {8,.37,0,.63,1},
        {9,.32,0,.67,0}, {10,.33,1,.68,1}, {11,.65,0,.35,1}
    };
    for (const auto& curve:legacy) if (s.easing==curve.id) {
        s.easing=4;s.x1=curve.x1;s.y1=curve.y1;s.x2=curve.x2;s.y2=curve.y2;
        break;
    }
    // Bounce/Back were removed in v1.4. Keep the separately stored custom
    // points intact when falling back, including for unknown future IDs.
    if(s.easing!=0 && s.easing!=1 && s.easing!=2 && s.easing!=4 &&
       s.easing!=14 && s.easing!=15) s.easing=0;
}
void Sanitize(Settings& s) {
    Settings d;
    s.length = Bound(s.length, 0, 500, d.length);
    s.gap = Bound(s.gap, 0, 500, d.gap);
    s.thickness = Bound(s.thickness, .25, 64, d.thickness);
    s.opacity = Bound(s.opacity, 0, 1, d.opacity);
    s.angle = Bound(s.angle, -3600, 3600, d.angle);
    s.dotSize = Bound(s.dotSize, 1, 64, d.dotSize);
    s.dotOpacity = Bound(s.dotOpacity, 0, 1, d.dotOpacity);
    s.crossSize=Bound(s.crossSize,0,200,d.crossSize);
    s.dotArtSize=Bound(s.dotArtSize,0,200,d.dotArtSize);
    for (Color* c : {&s.color, &s.dotColor}) {
        c->r = std::clamp(c->r, 0, 255);
        c->g = std::clamp(c->g, 0, 255);
        c->b = std::clamp(c->b, 0, 255);
    }
    s.action = std::clamp(s.action, 0, 2);
    s.trigger = std::clamp(s.trigger, 0, 1);
    s.enterMs = Bound(s.enterMs, 0, 10000, d.enterMs);
    s.leaveMs = Bound(s.leaveMs, 0, 10000, d.leaveMs);
    s.x1 = Bound(s.x1, 0, 1, d.x1); s.y1 = Bound(s.y1, 0, 1, d.y1);
    s.x2 = Bound(s.x2, 0, 1, d.x2); s.y2 = Bound(s.y2, 0, 1, d.y2);
    NormalizeEasing(s);
    s.dotShape=std::clamp(s.dotShape,0,3);
    SanitizeArt(s.crossArt);SanitizeArt(s.dotArt);
    s.monitor = std::clamp(s.monitor, 0, 64);
    s.offsetX = Bound(s.offsetX, -32768, 32768, d.offsetX);
    s.offsetY = Bound(s.offsetY, -32768, 32768, d.offsetY);
    s.offsetStep = std::clamp(s.offsetStep, 0, 2);
    if(s.skin!=0 && s.skin!=1)s.skin=0;
    NormalizeHotkeys(s);
}

LRESULT CALLBACK OverlayProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

BOOL CALLBACK MonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        auto& out = *reinterpret_cast<std::vector<Monitor>*>(param);
        Monitor m;
        m.bounds = info.rcMonitor;
        m.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        m.name = info.szDevice;
        m.name += L"  " + std::to_wstring(m.bounds.right - m.bounds.left) +
                  L" x " + std::to_wstring(m.bounds.bottom - m.bounds.top);
        out.push_back(std::move(m));
    }
    return TRUE;
}

bool IsSettingsPoint(POINT point) {
    HWND hit = WindowFromPoint(point);
    if (hit) {
        wchar_t rootClass[64]{};
        GetClassNameW(GetAncestor(hit, GA_ROOT), rootClass, 64);
        // Opening a tray menu must not toggle the game's right-click state.
        if (lstrcmpW(rootClass, L"Shell_TrayWnd") == 0 ||
            lstrcmpW(rootClass, L"Shell_SecondaryTrayWnd") == 0) return true;
    }
    if (hit && (GetAncestor(hit, GA_ROOT) == gMouseReceiver ||
                GetAncestor(hit, GA_ROOTOWNER) == gMouseReceiver)) return true;
    HWND foreground = GetForegroundWindow();
    if (foreground && (foreground == gMouseReceiver ||
                       GetAncestor(foreground, GA_ROOTOWNER) == gMouseReceiver)) {
        RECT r{};
        if (IsWindowVisible(gMouseReceiver) && !IsIconic(gMouseReceiver) &&
            GetWindowRect(gMouseReceiver, &r) && PtInRect(&r, point)) return true;
    }
    return false;
}

LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && gMouseReceiver) {
        auto* data = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        // Never swallow the input: the game receives the original right click.
        // Always report releases, including releases over the settings window.
        if (wp == WM_RBUTTONUP) PostMessageW(gMouseReceiver, WM_APP + 10, 0, 0);
        else if (wp == WM_RBUTTONDOWN && !IsSettingsPoint(data->pt))
            PostMessageW(gMouseReceiver, WM_APP + 10, 1, 0);
    }
    return CallNextHookEx(gMouseHook, code, wp, lp);
}

std::wstring SettingsPath() { return AppDirectory() + L"\\" + kSettingsFile; }

std::string Trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
}

std::vector<Monitor> Monitors() {
    std::vector<Monitor> out;
    EnumDisplayMonitors(nullptr, nullptr, MonitorCallback, reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const Monitor& a, const Monitor& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.bounds.left != b.bounds.left) return a.bounds.left < b.bounds.left;
        if (a.bounds.top != b.bounds.top) return a.bounds.top < b.bounds.top;
        return a.name < b.name;
    });
    return out;
}

std::wstring AppDirectory() {
    std::vector<wchar_t> buf(32768);
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (!len || len >= buf.size()) return L".";
    std::wstring path(buf.data(), len);
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

bool LoadSettings(Settings& s) {
    HANDLE file = CreateFileW(SettingsPath().c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file, &length) || length.QuadPart < 0 || length.QuadPart > 65536) {
        CloseHandle(file); return false;
    }
    std::string text(static_cast<size_t>(length.QuadPart), '\0');
    DWORD got = 0;
    bool ok = text.empty() || ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &got, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    text.resize(got);
    std::unordered_map<std::string, std::string> values;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        auto sep = line.find('=');
        if (sep != std::string::npos) values[Trim(line.substr(0, sep))] = Trim(line.substr(sep + 1));
    }
    auto number = [&](const char* key, double fallback) {
        auto it = values.find(key);
        if (it == values.end()) return fallback;
        std::istringstream in(it->second); in.imbue(std::locale::classic());
        double v = fallback;
        if (!(in >> v) || !std::isfinite(v)) return fallback;
        std::string trailing;
        return (in >> trailing) ? fallback : v;
    };
    auto integer = [&](const char* key, int fallback, int lo, int hi) {
        return static_cast<int>(Bound(number(key, fallback), lo, hi, fallback));
    };
    auto flag = [&](const char* key, bool fallback) { return number(key, fallback ? 1 : 0) != 0; };
    s.length = number("length", s.length); s.gap = number("gap", s.gap);
    s.thickness = number("thickness", s.thickness); s.opacity = number("opacity", s.opacity);
    s.angle = number("angle", s.angle); s.dot = flag("dot", s.dot);
    s.dotSize = number("dotSize", s.dotSize); s.dotOpacity = number("dotOpacity", s.dotOpacity);
    s.color.r = integer("red", s.color.r, 0, 255);
    s.color.g = integer("green", s.color.g, 0, 255);
    s.color.b = integer("blue", s.color.b, 0, 255);
    s.dotColor.r = integer("dotRed", s.dotColor.r, 0, 255);
    s.dotColor.g = integer("dotGreen", s.dotColor.g, 0, 255);
    s.dotColor.b = integer("dotBlue", s.dotColor.b, 0, 255);
    s.action = integer("action", s.action, 0, 2);
    s.trigger = integer("trigger", s.trigger, 0, 1);
    s.enterMs = number("enterMs", s.enterMs); s.leaveMs = number("leaveMs", s.leaveMs);
    const double easing=number("easing",s.easing);
    s.easing=easing>=0&&easing<=15&&easing==std::floor(easing)?static_cast<int>(easing):0;
    s.x1 = number("x1", s.x1); s.y1 = number("y1", s.y1);
    s.x2 = number("x2", s.x2); s.y2 = number("y2", s.y2);
    s.monitor = integer("monitor", s.monitor, 0, 64);
    s.offsetX = number("offsetX", s.offsetX);
    s.offsetY = number("offsetY", s.offsetY);
    // Version-1 INI files used integer offsets and had no step selection.
    s.offsetStep = integer("offsetStep", 1, 0, 2);
    s.customCrosshair=flag("customCrosshair",false);
    s.dotShape=integer("dotShape",0,0,3);
    // Older versions displayed each canvas at its native size.
    s.crossSize=number("crossSize",0);
    s.dotArtSize=number("dotArtSize",0);
    auto loadArt=[&](const char* prefix) {
        PixelArt art;
        const std::string base=prefix;
        const double width=number((base+"Width").c_str(),0);
        const double height=number((base+"Height").c_str(),0);
        auto data=values.find(base+"Data");
        if (width<1 || width>100 || height<1 || height>100 ||
            width!=std::floor(width) || height!=std::floor(height) || data==values.end()) return art;
        const size_t count=static_cast<size_t>(width)*static_cast<size_t>(height);
        if (data->second.size()!=count*2) return art;
        art.width=static_cast<int>(width);art.height=static_cast<int>(height);
        art.pixels.assign(count,0);
        auto hex=[](char c) {
            if(c>='0'&&c<='9')return c-'0';
            if(c>='a'&&c<='f')return c-'a'+10;
            if(c>='A'&&c<='F')return c-'A'+10;
            return -1;
        };
        for(size_t i=0;i<count;++i){
            int hi=hex(data->second[i*2]),lo=hex(data->second[i*2+1]);
            if(hi<0||lo<0)return PixelArt{};
            art.pixels[i]=static_cast<uint8_t>(hi*16+lo);
        }
        return art;
    };
    s.crossArt=loadArt("crossArt");s.dotArt=loadArt("dotArt");
    s.onlyGame = flag("onlyGame", s.onlyGame); s.enabled = flag("enabled", s.enabled);
    // UI/hotkey settings were introduced in version 6. Missing old-file keys
    // explicitly restore the original shortcuts/current native appearance.
    const double skin=number("skin",0);
    s.skin=skin==1?1:0;
    s.hotkeys=HotkeyDefaults();
    for(int i=0;i<kHotkeyCount;++i){
        const std::string suffix=std::to_string(i+1);
        auto exactInteger=[&](const std::string& key,int fallback){
            if(values.find(key)==values.end())return fallback;
            const double v=number(key.c_str(),std::numeric_limits<double>::quiet_NaN());
            return v>=std::numeric_limits<int>::min() && v<=std::numeric_limits<int>::max() &&
                   v==std::floor(v)?static_cast<int>(v):-1;
        };
        s.hotkeys[i].key=exactInteger("hotkeyKey"+suffix,s.hotkeys[i].key);
        s.hotkeys[i].modifiers=exactInteger("hotkeyModifiers"+suffix,s.hotkeys[i].modifiers);
    }
    Sanitize(s);
    return true;
}

bool SaveSettings(const Settings& original) {
    Settings s = original; Sanitize(s);
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "; HuntCrosshair portable settings. Sizes are physical pixels.\r\n"
           "; A centered nominal 1px dot uses symmetric pixel coverage.\r\n"
           "[HuntCrosshair]\r\nversion=6\r\n";
    auto val = [&](const char* key, auto value) { out << key << '=' << value << "\r\n"; };
    val("length", s.length); val("gap", s.gap); val("thickness", s.thickness);
    val("opacity", s.opacity); val("angle", s.angle);
    val("red", s.color.r); val("green", s.color.g); val("blue", s.color.b);
    val("dot", s.dot ? 1 : 0); val("dotSize", s.dotSize); val("dotOpacity", s.dotOpacity);
    val("dotRed", s.dotColor.r); val("dotGreen", s.dotColor.g); val("dotBlue", s.dotColor.b);
    val("action", s.action); val("trigger", s.trigger);
    val("enterMs", s.enterMs); val("leaveMs", s.leaveMs); val("easing", s.easing);
    val("x1", s.x1); val("y1", s.y1); val("x2", s.x2); val("y2", s.y2);
    val("monitor", s.monitor); val("offsetX", s.offsetX); val("offsetY", s.offsetY);
    val("offsetStep", s.offsetStep);
    val("customCrosshair",s.customCrosshair?1:0);val("dotShape",s.dotShape);
    val("crossSize",s.crossSize);val("dotArtSize",s.dotArtSize);
    auto saveArt=[&](const char* prefix,const PixelArt& art) {
        const std::string base=prefix;
        val((base+"Width").c_str(),art.width);val((base+"Height").c_str(),art.height);
        static constexpr char digits[]="0123456789ABCDEF";
        std::string encoded(art.pixels.size()*2,'0');
        for(size_t i=0;i<art.pixels.size();++i){
            encoded[i*2]=digits[art.pixels[i]>>4];encoded[i*2+1]=digits[art.pixels[i]&15];
        }
        val((base+"Data").c_str(),encoded);
    };
    saveArt("crossArt",s.crossArt);saveArt("dotArt",s.dotArt);
    val("onlyGame", s.onlyGame ? 1 : 0); val("enabled", s.enabled ? 1 : 0);
    out << "[UI]\r\n";
    val("skin",s.skin);
    out << "[Hotkeys]\r\n";
    for(int i=0;i<kHotkeyCount;++i){
        const std::string suffix=std::to_string(i+1);
        val(("hotkeyKey"+suffix).c_str(),s.hotkeys[i].key);
        val(("hotkeyModifiers"+suffix).c_str(),s.hotkeys[i].modifiers);
    }
    const std::string bytes = out.str();
    const std::wstring path = SettingsPath();
    const std::wstring temp = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
              && written == bytes.size();
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

bool Overlay::Create(HINSTANCE instance) {
    if (hwnd_) return true;
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = OverlayProc;
    wc.hInstance = instance; wc.lpszClassName = kOverlayClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                          WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                          kOverlayClass, L"HuntCrosshair Overlay", WS_POPUP,
                          0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) return false;
    dc_ = CreateCompatibleDC(nullptr);
    if (!dc_) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
    return true;
}

void Overlay::Update(const Settings& original, const Frame& frame, bool visible) {
    if (!hwnd_) return;
    if (!visible || !original.enabled) { ShowWindow(hwnd_, SW_HIDE); return; }
    Settings s = original; Sanitize(s);
    auto monitors = Monitors();
    if (monitors.empty()) { ShowWindow(hwnd_, SW_HIDE); return; }
    const auto& monitor = monitors[std::min(static_cast<size_t>(s.monitor), monitors.size() - 1)];
    const double cx = monitor.bounds.left + (monitor.bounds.right - monitor.bounds.left) * .5 + s.offsetX;
    const double cy = monitor.bounds.top + (monitor.bounds.bottom - monitor.bounds.top) * .5 + s.offsetY;
    // Stable conservative padding also contains Back's arm/dot overshoot.
    const double radius = CrosshairRadius(s)*1.5 + 3;
    const int needed = 2 * static_cast<int>(std::ceil(radius));
    if (needed != size_) {
        if (bitmap_) {
            SelectObject(dc_, oldBitmap_); DeleteObject(bitmap_);
            bitmap_ = nullptr; oldBitmap_ = nullptr; bits_ = nullptr; size_ = 0;
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = needed; info.bmiHeader.biHeight = -needed;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!bitmap_) { ShowWindow(hwnd_, SW_HIDE); return; }
        oldBitmap_ = SelectObject(dc_, bitmap_); size_ = needed;
    }
    // Window positions are integers; keep the fractional calibration in the
    // local raster center instead of rounding it away with the window origin.
    POINT position{static_cast<LONG>(std::floor(cx - size_ * .5)),
                   static_cast<LONG>(std::floor(cy - size_ * .5))};
    Rasterize(s, frame, size_, size_, cx - position.x, cy - position.y, pixels_);
    if (pixels_.size() != static_cast<size_t>(size_) * size_) return;
    std::memcpy(bits_, pixels_.data(), pixels_.size() * sizeof(uint32_t));
    POINT source{}; SIZE size{size_, size_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(hwnd_, nullptr, &position, &size, dc_, &source, 0, &blend, ULW_ALPHA)) {
        ShowWindow(hwnd_, SW_HIDE); return;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

void Overlay::Destroy() {
    if (dc_ && bitmap_) { SelectObject(dc_, oldBitmap_); DeleteObject(bitmap_); }
    bitmap_ = nullptr; bits_ = nullptr; oldBitmap_ = nullptr;
    if (dc_) DeleteDC(dc_);
    dc_ = nullptr; size_ = 0;
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr; pixels_.clear();
}

bool StartMouseInput(HWND receiver) {
    StopMouseInput(); gMouseReceiver = receiver;
    gMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
    if (!gMouseHook) gMouseReceiver = nullptr;
    return gMouseHook != nullptr;
}
void StopMouseInput() {
    if (gMouseHook) UnhookWindowsHookEx(gMouseHook);
    gMouseHook = nullptr; gMouseReceiver = nullptr;
}

bool GameIsForeground() {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD pid = 0; GetWindowThreadProcessId(foreground, &pid);
    if (!pid || pid == GetCurrentProcessId()) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t buffer[32768]; DWORD count = 32768;
    bool ok = QueryFullProcessImageNameW(process, 0, buffer, &count) != FALSE;
    CloseHandle(process);
    if (!ok) return false;
    std::wstring name(buffer, count);
    const auto slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name.erase(0, slash + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return std::towlower(c); });
    return name == L"hunt.exe" || (name.rfind(L"huntgame", 0) == 0 &&
           name.size() >= 4 && name.compare(name.size() - 4, 4, L".exe") == 0);
}

double NowMs() {
    static LARGE_INTEGER frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    return frequency.QuadPart ? static_cast<double>(now.QuadPart) * 1000.0 / frequency.QuadPart : GetTickCount64();
}
}
