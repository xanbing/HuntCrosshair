#pragma once

namespace hc {
struct EasingPreset {
    int id;
    const wchar_t* label;
    double x1,y1,x2,y2;
};

// These named presets are immutable. The editable custom curve (4) keeps
// separate control points and is never overwritten when a preset is selected.
inline constexpr EasingPreset kEasingPresets[]={
    {1,L"缓入 · Ease In",.42,0,1,1},
    {2,L"缓出 · Ease Out",0,0,.58,1},
    {14,L"阻尼",.492753623188,.327586206897,.728260869565,.310344827586},
    {15,L"阻尼2",0,1,0,1}
};
// Kept for reading/migrating previous saved IDs; these entries are not shown
// in the simplified UI and never replace the user's custom control points.
inline constexpr EasingPreset kLegacyEasingPresets[]={
    {3,L"缓入缓出 · Ease In Out",.42,0,.58,1},
    {5,L"标准缓动 · Ease",.25,.1,.25,1},
    {6,L"正弦缓入 · Sine In",.12,0,.39,0},
    {7,L"正弦缓出 · Sine Out",.61,1,.88,1},
    {8,L"正弦缓入缓出 · Sine In Out",.37,0,.63,1},
    {9,L"三次缓入 · Cubic In",.32,0,.67,0},
    {10,L"三次缓出 · Cubic Out",.33,1,.68,1},
    {11,L"三次缓入缓出 · Cubic In Out",.65,0,.35,1}
};
inline constexpr int kEasingPresetCount=
    static_cast<int>(sizeof(kEasingPresets)/sizeof(kEasingPresets[0]));

inline constexpr const EasingPreset* ResolvePreset(int id) {
    for (const auto& preset:kEasingPresets)
        if (preset.id==id) return &preset;
    for (const auto& preset:kLegacyEasingPresets)
        if (preset.id==id) return &preset;
    return nullptr;
}
}
