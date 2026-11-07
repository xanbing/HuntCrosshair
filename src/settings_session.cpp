#include "settings_session.h"
#include <cmath>
#include <utility>

namespace hc {
namespace {
bool EqualValue(double a,double b) noexcept {return a==b||(std::isnan(a)&&std::isnan(b));}
bool EqualValue(int a,int b) noexcept {return a==b;}
bool EqualValue(bool a,bool b) noexcept {return a==b;}
bool EqualValue(const Color& a,const Color& b) noexcept {return a.r==b.r&&a.g==b.g&&a.b==b.b;}
bool EqualValue(const PixelArt& a,const PixelArt& b) noexcept {
    return a.width==b.width&&a.height==b.height&&a.pixels==b.pixels;
}
bool EqualValue(const std::array<Hotkey,kHotkeyCount>& a,const std::array<Hotkey,kHotkeyCount>& b) noexcept {
    for(int i=0;i<kHotkeyCount;++i)
        if(a[i].modifiers!=b[i].modifiers || a[i].key!=b[i].key)return false;
    return true;
}

// Keep this list in sync with Settings. Sharing it between comparison and merge
// ensures a runtime change cannot accidentally discard an unrelated draft field.
#define HC_SESSION_FIELDS(F) \
    F(length) F(gap) F(thickness) F(opacity) F(angle) F(crossSize) \
    F(color) F(customCrosshair) F(crossArt) F(dot) F(dotSize) F(dotOpacity) \
    F(dotArtSize) F(dotColor) F(dotShape) F(dotArt) F(action) F(trigger) \
    F(enterMs) F(leaveMs) F(easing) F(x1) F(y1) F(x2) F(y2) \
    F(monitor) F(offsetX) F(offsetY) F(offsetStep) F(onlyGame) F(enabled) F(skin) F(hotkeys)

Settings MergeRuntimeChanges(const Settings& draft,const Settings& before,const Settings& after) {
    Settings result=draft;
#define HC_MERGE(field) if(!EqualValue(before.field,after.field))result.field=after.field;
    HC_SESSION_FIELDS(HC_MERGE)
#undef HC_MERGE
    return result;
}
}

bool SettingsEqual(const Settings& a,const Settings& b) noexcept {
#define HC_COMPARE(field) if(!EqualValue(a.field,b.field))return false;
    HC_SESSION_FIELDS(HC_COMPARE)
#undef HC_COMPARE
    return true;
}
#undef HC_SESSION_FIELDS

void SettingsSession::Begin(const Settings& applied) {
    applied_=applied;
    draft_=applied_;
}
bool SettingsSession::Apply(const SaveCallback& save) {
    if(!save)return false;
    Settings snapshot=draft_;
    if(!save(snapshot))return false;
    applied_=std::move(snapshot);
    return true;
}
void SettingsSession::Cancel() {draft_=applied_;}
void SettingsSession::ResetDefaults() {draft_=Settings{};}
bool SettingsSession::CommitRuntimeChange(const RuntimeMutator& change,const SaveCallback& save) {
    if(!change)return false;
    Settings candidate=applied_;
    change(candidate);
    if(save&&!save(candidate))return false;
    Settings merged=MergeRuntimeChanges(draft_,applied_,candidate);
    applied_=std::move(candidate);
    draft_=std::move(merged);
    return true;
}
}
