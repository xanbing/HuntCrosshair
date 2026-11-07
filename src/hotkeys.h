#pragma once
#include "core.h"

namespace hc {
inline constexpr std::array<Hotkey,kHotkeyCount> HotkeyDefaults() noexcept {
    return kDefaultHotkeys;
}
inline bool SameHotkey(const Hotkey& a,const Hotkey& b) noexcept {
    return a.key==b.key && (!a.key || a.modifiers==b.modifiers);
}
inline bool ValidHotkey(const Hotkey& hotkey) noexcept {
    if(hotkey.modifiers<0 || (hotkey.modifiers&~7)!=0)return false;
    if(!hotkey.key)return !hotkey.modifiers;
    if(hotkey.key<8 || hotkey.key>254)return false;
    // Mouse buttons and bare modifier keys are not keyboard shortcuts. F12 is
    // permanently reserved for the debugger by the RegisterHotKey API.
    if((hotkey.key>=0x10 && hotkey.key<=0x12) ||
       hotkey.key==0x5B || hotkey.key==0x5C || hotkey.key==0x7B ||
       (hotkey.key>=0xA0 && hotkey.key<=0xA5) ||
       hotkey.key==0xE5 || hotkey.key==0xE7)return false;
    return true;
}
inline void NormalizeHotkeys(Settings& settings) noexcept {
    for(int i=0;i<kHotkeyCount;++i){
        auto& hotkey=settings.hotkeys[i];
        if(!hotkey.key)hotkey.modifiers=0;
        if(!ValidHotkey(hotkey))hotkey=kDefaultHotkeys[i];
        // Keep the first requested action; later duplicates are disabled so
        // loading malformed settings cannot produce ambiguous global actions.
        for(int j=0;hotkey.key && j<i;++j)
            if(SameHotkey(hotkey,settings.hotkeys[j]))hotkey={};
    }
}
}
