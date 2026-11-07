#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <string>
#include <vector>
#include "core.h"

namespace hc {
struct Monitor { RECT bounds; std::wstring name; bool primary; };
std::vector<Monitor> Monitors();
std::wstring AppDirectory();
bool LoadSettings(Settings& s);
bool SaveSettings(const Settings& s);
class Overlay {
public:
    bool Create(HINSTANCE instance);
    void Update(const Settings& s, const Frame& frame, bool visible);
    void Destroy();
    HWND Window() const {return hwnd_;}
private:
    HWND hwnd_=nullptr;
    HDC dc_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ oldBitmap_=nullptr;
    void* bits_=nullptr;
    int size_=0;
    std::vector<uint32_t> pixels_;
};
// Hooks send WM_APP+10 (right button transition) with wParam 1/0.
bool StartMouseInput(HWND receiver);
void StopMouseInput();
bool GameIsForeground();
double NowMs();
}
