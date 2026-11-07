#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace hc {
struct Color { int r=104, g=239, b=187; };
struct PixelArt {
    int width=20, height=20;
    std::vector<uint8_t> pixels=std::vector<uint8_t>(400,0);
};
struct Hotkey { int modifiers=0,key=0; };
inline constexpr int kHotkeyCount=7;
// RegisterHotKey modifier values: Alt=1, Ctrl=2, Shift=4. Key 0 is disabled.
// Action order: toggle, settings, exit, left, right, up, down.
inline constexpr std::array<Hotkey,kHotkeyCount> kDefaultHotkeys{{
    {5,'X'},{5,'S'},{5,'Q'},{7,0x25},{7,0x27},{7,0x26},{7,0x28}
}};
struct Settings {
    double length=16, gap=6, thickness=2, opacity=1, angle=0;
    double crossSize=0; // 0: original geometry; otherwise longest unrotated side in px
    Color color;
    bool customCrosshair=false;
    PixelArt crossArt;
    bool dot=true;
    double dotSize=1, dotOpacity=1;
    double dotArtSize=0; // custom point: 0 original canvas, otherwise longest side in px
    Color dotColor;
    int dotShape=0; // square, circle, sharp UP triangle with tip at aim origin, custom art
    PixelArt dotArt;
    int action=2; // 0 unchanged, 1 hide, 2 morph to dot
    int trigger=0; // 0 hold, 1 toggle
    double enterMs=300, leaveMs=300;
    int easing=0; // 0 linear, 1 in, 2 out, 4 editable custom, 14/15 saved user presets
    // Latest editable baseline; saved user curves are separate named presets.
    double x1=0, y1=1, x2=0, y2=1;
    int monitor=0;
    double offsetX=0, offsetY=0;
    int offsetStep=1; // 0: 1px, 1: 0.25px, 2: 0.0625px
    bool onlyGame=false;
    bool enabled=true;
    int skin=0; // 0: current native appearance; 1: native classic/Win98 appearance
    std::array<Hotkey,kHotkeyCount> hotkeys=kDefaultHotkeys;
};
struct Frame { double arms=1, gap=1, alpha=1, dotAlpha=1, dotScale=1; };
double CrosshairRadius(const Settings& s);
double Ease(double t, int kind, double x1, double y1, double x2, double y2);
Frame MakeFrame(const Settings& s, double progress);
// Premultiplied BGRA. cx/cy are pixel-edge coordinates: W/2,H/2 is geometric center.
void Rasterize(const Settings& s, const Frame& f, int w, int h,
               double cx, double cy, std::vector<uint32_t>& pixels);
class Animation {
public:
    void SetTarget(bool active, double nowMs, const Settings& s);
    double Value(double nowMs, const Settings& s) const;
    bool Running(double nowMs) const;
    void Reset();
private:
    double from_=0, to_=0, start_=0, duration_=0;
    int easing_=0;
    double x1_=.25, y1_=.1, x2_=.25, y2_=1;
};
}
