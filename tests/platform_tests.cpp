// Run the platform tests from a disposable copy of this executable. Settings
// are relative to the executable, so the real application config is untouched.
#include "platform.h"
#include "hotkeys.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
std::wstring TempPrefix() {
    wchar_t buffer[32768]{};
    DWORD count = GetTempPathW(32768, buffer);
    Check(count > 0 && count < 32768, "GetTempPath failed");
    return std::wstring(buffer, count) + L"HuntCrosshair-platform-tests-";
}
void WriteFixture(const std::wstring& path, const char* content) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE, "fixture creation failed");
    const DWORD length=static_cast<DWORD>(lstrlenA(content));
    DWORD written=0;
    const bool wrote=WriteFile(file,content,length,&written,nullptr)!=FALSE;
    CloseHandle(file);
    Check(wrote && written==length, "fixture write failed");
}
bool BlankDefault(const hc::PixelArt& art) {
    return art.width==20 && art.height==20 && art.pixels.size()==400 &&
        std::all_of(art.pixels.begin(),art.pixels.end(),[](uint8_t alpha){return alpha==0;});
}
bool SameBindings(const std::array<hc::Hotkey,hc::kHotkeyCount>& a,
                  const std::array<hc::Hotkey,hc::kHotkeyCount>& b){
    for(int i=0;i<hc::kHotkeyCount;++i)
        if(a[i].key!=b[i].key || a[i].modifiers!=b[i].modifiers)return false;
    return true;
}
void TestHotkeyModel(){
    hc::Settings s;
    Check(SameBindings(s.hotkeys,hc::HotkeyDefaults()),"default shortcuts changed");
    for(const auto& hotkey:s.hotkeys)Check(hc::ValidHotkey(hotkey),"default shortcut is invalid");
    Check(hc::ValidHotkey({0,0})&&hc::ValidHotkey({0,VK_F9})&&
          hc::ValidHotkey({7,VK_RIGHT})&&hc::ValidHotkey({2,'K'}),"valid shortcut rejected");
    for(const auto& bad:{hc::Hotkey{8,'K'},{-1,'K'},{2,256},{2,-1},{0,VK_F12},
                        {1,VK_SHIFT},{2,VK_CONTROL},{4,VK_MENU},{0,VK_LWIN},
                        {0,VK_RWIN},{2,VK_LBUTTON},{0,VK_PACKET},{7,0}})
        Check(!hc::ValidHotkey(bad),"invalid shortcut accepted");
    Check(hc::SameHotkey({7,VK_LEFT},{7,VK_LEFT})&&
          !hc::SameHotkey({7,VK_LEFT},{3,VK_LEFT})&&
          !hc::SameHotkey({7,VK_LEFT},{7,VK_RIGHT}),"shortcut comparison lost key/modifiers");
    s.hotkeys[0]={2,'K'};s.hotkeys[1]={2,'K'};s.hotkeys[2]={7,0};
    s.hotkeys[3]={8,'A'};s.hotkeys[4]={4,VK_SHIFT};
    hc::NormalizeHotkeys(s);
    Check(s.hotkeys[0].key=='K'&&s.hotkeys[1].key==0&&s.hotkeys[1].modifiers==0&&
          s.hotkeys[2].key==0&&s.hotkeys[2].modifiers==0,"duplicate/disabled shortcuts were not canonicalized");
    Check(hc::SameHotkey(s.hotkeys[3],hc::kDefaultHotkeys[3])&&
          hc::SameHotkey(s.hotkeys[4],hc::kDefaultHotkeys[4]),"invalid shortcuts did not restore action defaults");
}
void TestInSandbox(const std::wstring& expected) {
    Check(hc::AppDirectory() == expected, "test executable is outside its sandbox");
    Check(expected.rfind(TempPrefix(), 0) == 0, "test directory is outside temp");
    const std::wstring marker = expected + L"\\.test-sandbox";
    Check(GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES,
          "test sandbox marker is missing");
    TestHotkeyModel();

    hc::Settings a;
    a.length=123.5; a.gap=21; a.thickness=.5;
    a.color={255,17,144}; a.dotColor={1,2,3};
    a.enterMs=314; a.leaveMs=612; a.opacity=.123456;
    a.trigger=1; a.action=1; a.onlyGame=true;
    a.offsetX=-17.25; a.offsetY=29.0625; a.offsetStep=2; a.x1=.37;
    a.skin=1;
    for(int i=0;i<hc::kHotkeyCount;++i)a.hotkeys[i]={i,0x70+i};
    a.hotkeys[2]={};
    Check(hc::SaveSettings(a), "settings save failed");
    hc::Settings b;
    Check(hc::LoadSettings(b), "settings load failed");
    Check(b.length==123.5 && b.gap==21 && b.thickness==.5, "shape roundtrip failed");
    Check(b.color.r==255 && b.color.g==17 && b.color.b==144, "color roundtrip failed");
    Check(b.dotColor.r==1 && b.dotColor.g==2 && b.dotColor.b==3, "dot color roundtrip failed");
    Check(b.enterMs==314 && b.leaveMs==612 && std::abs(b.opacity-.123456)<1e-12,
          "animation/opacity roundtrip failed");
    Check(b.trigger==1 && b.action==1 && b.onlyGame, "mode roundtrip failed");
    Check(b.offsetX==-17.25 && b.offsetY==29.0625 && b.offsetStep==2 && b.x1==.37,
          "fractional offset/step/curve roundtrip failed");
    Check(b.skin==1&&SameBindings(a.hotkeys,b.hotkeys),"custom/disabled shortcut or skin roundtrip failed");
    a.skin=0;Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.skin==0,
                  "current native skin selection did not survive restart");
    a.skin=99;Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.skin==0,
                   "invalid skin did not fall back to current native appearance");

    // Presets do not replace the user's separately saved custom cubic points.
    a.x1=.1234567890123456; a.y1=.83; a.x2=.61; a.y2=.39;
    const int visibleEasings[]={0,1,2,4,14,15};
    for (int easing:visibleEasings) {
        a.easing=easing;
        Check(hc::SaveSettings(a) && hc::LoadSettings(b), "preset save/load failed");
        Check(b.easing==easing, "preset ID was changed or truncated");
        Check(b.x1==a.x1 && b.y1==.83 && b.x2==.61 && b.y2==.39,
              "custom cubic points were lost while saving a preset");
    }
    a.easing=999;
    Check(hc::SaveSettings(a) && hc::LoadSettings(b) && b.easing==0,
          "oversized preset index did not fall back to linear on save");
    a.easing=-999;
    Check(hc::SaveSettings(a) && hc::LoadSettings(b) && b.easing==0,
          "negative preset index was not clamped on save");

    // Native sizing is a separate value from the smallest explicit target.
    for(double crossSize:{0.0,1.0,20.0,123.5,200.0})
        for(double dotArtSize:{0.0,1.0,37.25,200.0}){
            a.crossSize=crossSize;a.dotArtSize=dotArtSize;
            Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.crossSize==crossSize&&
                  b.dotArtSize==dotArtSize,"native/target art scaling roundtrip failed");
        }
    a.crossSize=std::numeric_limits<double>::infinity();
    a.dotArtSize=std::numeric_limits<double>::quiet_NaN();
    Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.crossSize==0&&b.dotArtSize==0,
          "nonfinite target scales were not restored to native on save");
    a.crossSize=-999;a.dotArtSize=999;
    Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.crossSize==0&&b.dotArtSize==200,
          "out-of-range target scales were not clamped on save");
    a.crossSize=0;a.dotArtSize=0;

    a.customCrosshair=true;a.dotShape=3;
    a.crossArt.width=100;a.crossArt.height=100;a.crossArt.pixels.resize(10000);
    a.dotArt.width=100;a.dotArt.height=100;a.dotArt.pixels.resize(10000);
    for(size_t i=0;i<10000;++i){
        a.crossArt.pixels[i]=static_cast<uint8_t>((i*37+13)%256);
        a.dotArt.pixels[i]=static_cast<uint8_t>((i*19+7)%256);
    }
    Check(hc::SaveSettings(a)&&hc::LoadSettings(b),"full-size art save/load failed");
    Check(b.customCrosshair && b.dotShape==3 && b.crossArt.width==100 && b.crossArt.height==100 &&
          b.dotArt.width==100 && b.dotArt.height==100 && b.crossArt.pixels==a.crossArt.pixels &&
          b.dotArt.pixels==a.dotArt.pixels,"100x100 alpha maps were changed or truncated");
    for(int shape=0;shape<=3;++shape){
        a.dotShape=shape;
        Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.dotShape==shape,
              "dot shape selection roundtrip failed");
    }
    a.crossArt.width=1;a.crossArt.height=100;a.crossArt.pixels.assign(100,37);
    a.dotArt.width=100;a.dotArt.height=1;a.dotArt.pixels.assign(100,128);
    Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&b.crossArt.width==1&&b.crossArt.height==100&&
          b.dotArt.width==100&&b.dotArt.height==1&&b.crossArt.pixels==a.crossArt.pixels&&
          b.dotArt.pixels==a.dotArt.pixels,"independent art width/height limits failed");
    a.crossArt.width=2;a.crossArt.height=2;a.crossArt.pixels.assign(3,255);
    a.dotArt.width=101;a.dotArt.height=100;a.dotArt.pixels.assign(10100,255);a.dotShape=999;
    Check(hc::SaveSettings(a)&&hc::LoadSettings(b)&&BlankDefault(b.crossArt)&&BlankDefault(b.dotArt)&&
          b.dotShape==3,"invalid in-memory art size/length or shape was not sanitized");
    a.crossArt=hc::PixelArt{};a.dotArt=hc::PixelArt{};a.customCrosshair=false;a.dotShape=0;

    // Quarter- and sixteenth-pixel calibration must survive save/restart exactly.
    const double offsets[]={.25, .0625, -.0625, -.25, 32767.9375, -32767.9375};
    for (int step=0; step<3; ++step) for (double offset : offsets) {
        a.offsetX=offset; a.offsetY=-offset; a.offsetStep=step;
        Check(hc::SaveSettings(a) && hc::LoadSettings(b), "fractional calibration save/load failed");
        Check(b.offsetX==offset && b.offsetY==-offset && b.offsetStep==step,
              "fractional calibration was rounded or step was lost");
    }

    a.length=std::numeric_limits<double>::infinity();
    a.thickness=-1; a.opacity=9; a.dotSize=0; a.color.r=999;
    a.offsetX=std::numeric_limits<double>::infinity();
    a.offsetY=std::numeric_limits<double>::quiet_NaN(); a.offsetStep=-7;
    Check(hc::SaveSettings(a), "sanitized save failed");
    Check(hc::LoadSettings(b), "sanitized load failed");
    Check(b.length==hc::Settings{}.length && b.thickness==.25, "finite/size validation failed");
    Check(b.opacity==1 && b.dotSize==1 && b.color.r==255, "range validation failed");
    Check(b.offsetX==0 && b.offsetY==0 && b.offsetStep==0,
          "nonfinite calibration or invalid step was not sanitized on save");

    const std::wstring path=expected+L"\\HuntCrosshair.ini";
    const hc::Settings defaults;
    WriteFixture(path,"[HuntCrosshair]\nversion=5\nlength=31\n"
        "x1=0.1234567890123456\n");
    b.skin=1;for(auto& hotkey:b.hotkeys)hotkey={};
    Check(hc::LoadSettings(b)&&b.skin==0&&SameBindings(b.hotkeys,hc::HotkeyDefaults())&&
          b.length==31&&b.x1==.1234567890123456,
          "version-5 migration failed to restore old shortcuts/current skin or changed existing settings");
    WriteFixture(path,"[HuntCrosshair]\nversion=6\nx1=0.1234567890123456\n"
        "[UI]\nskin=1\n[Hotkeys]\n"
        "hotkeyKey1=75\nhotkeyModifiers1=2\n"
        "hotkeyKey2=75\nhotkeyModifiers2=2\n"
        "hotkeyKey3=0\nhotkeyModifiers3=7\n"
        "hotkeyKey4=16\nhotkeyModifiers4=2\n"
        "hotkeyKey5=39\nhotkeyModifiers5=8\n"
        "hotkeyKey6=113.5\nhotkeyModifiers6=3\n"
        "hotkeyKey7=-999999999999999\nhotkeyModifiers7=7\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.skin==1&&b.x1==.1234567890123456&&
          hc::SameHotkey(b.hotkeys[0],{2,'K'})&&b.hotkeys[1].key==0&&
          b.hotkeys[1].modifiers==0&&b.hotkeys[2].key==0&&b.hotkeys[2].modifiers==0,
          "version-6 custom/duplicate/disabled shortcut load failed");
    for(int i=3;i<hc::kHotkeyCount;++i)
        Check(hc::SameHotkey(b.hotkeys[i],hc::kDefaultHotkeys[i]),
              "malformed shortcut key/modifier did not restore its original action binding");
    const auto normalized=b.hotkeys;
    Check(hc::SaveSettings(b)&&hc::LoadSettings(b)&&b.skin==1&&
          SameBindings(b.hotkeys,normalized)&&b.x1==.1234567890123456,
          "normalized shortcut/skin/full-precision values changed after restart");
    WriteFixture(path,"[HuntCrosshair]\nversion=6\n[UI]\nskin=1.5\n"
        "[Hotkeys]\nhotkeyKey1=123\nhotkeyModifiers1=0\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.skin==0&&hc::SameHotkey(b.hotkeys[0],hc::kDefaultHotkeys[0]),
          "fractional skin or reserved F12 did not restore safe defaults");
    for(const char* invalid:{"bad","nan","1e999","2trailing"}){
        const std::string fixture=std::string("[HuntCrosshair]\nversion=6\n[Hotkeys]\n")+
            "hotkeyKey1=75\nhotkeyModifiers1="+invalid+"\n";
        WriteFixture(path,fixture.c_str());b=hc::Settings{};
        Check(hc::LoadSettings(b)&&hc::SameHotkey(b.hotkeys[0],hc::kDefaultHotkeys[0]),
              "invalid shortcut token did not restore the complete original binding");
    }
    Check(defaults.x1==0&&defaults.y1==1&&defaults.x2==0&&defaults.y2==1,
          "latest editable custom baseline was not retained");
    WriteFixture(path,"[HuntCrosshair]\nversion=4\neasing=4\nx1=0.492753623188\n"
        "y1=0.327586206897\nx2=0.728260869565\ny2=0.310344827586\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.easing==4&&b.x1==.492753623188&&b.y1==.327586206897&&
          b.x2==.728260869565&&b.y2==.310344827586,"previous saved custom curve did not load exactly");
    for(int kind:{14,15,4}){
        b.easing=kind;
        Check(hc::SaveSettings(b)&&hc::LoadSettings(b)&&b.easing==kind&&b.x1==.492753623188&&
              b.y1==.327586206897&&b.x2==.728260869565&&b.y2==.310344827586,
              "named damping preset replaced the separately saved custom curve");
    }
    WriteFixture(path,"[HuntCrosshair]\nversion=4\neasing=4\nx1=0\ny1=1\nx2=0\ny2=1\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.easing==4&&b.x1==0&&b.y1==1&&b.x2==0&&b.y2==1,
          "latest saved custom curve did not load exactly");
    for(int removed:{12,13}){
        const std::string fixture="[HuntCrosshair]\nversion=4\neasing="+std::to_string(removed)+
            "\nx1=0.17\ny1=0.83\nx2=0.61\ny2=0.39\nlength=27\nopacity=0.42\noffsetX=-10.0625\n";
        WriteFixture(path,fixture.c_str());b=hc::Settings{};
        Check(hc::LoadSettings(b)&&b.easing==0&&b.x1==.17&&b.y1==.83&&b.x2==.61&&b.y2==.39&&
              b.length==27&&b.opacity==.42&&b.offsetX==-10.0625,
              "removed bounce/back fallback discarded unrelated saved data");
        Check(hc::SaveSettings(b)&&hc::LoadSettings(b)&&b.easing==0&&b.x1==.17&&b.y1==.83&&
              b.x2==.61&&b.y2==.39,"removed bounce/back fallback did not survive restart");
    }
    WriteFixture(path,"[HuntCrosshair]\nversion=4\n");
    b=hc::Settings{};b.crossSize=99;b.dotArtSize=88;
    Check(hc::LoadSettings(b)&&b.crossSize==0&&b.dotArtSize==0,
          "legacy files without target scales did not use native geometry");
    WriteFixture(path,"[HuntCrosshair]\nversion=5\ncrossSize=nan\ndotArtSize=1e999\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.crossSize==0&&b.dotArtSize==0,
          "nonfinite target scales did not load as native");
    WriteFixture(path,"[HuntCrosshair]\nversion=5\ncrossSize=-5\ndotArtSize=999999999999999\n");
    Check(hc::LoadSettings(b)&&b.crossSize==0&&b.dotArtSize==200,
          "target scale load bounds were not enforced");
    struct LegacyCurve{int id;double x1,y1,x2,y2;};
    const LegacyCurve removedCurves[]={
        {3,.42,0,.58,1},{5,.25,.1,.25,1},{6,.12,0,.39,0},{7,.61,1,.88,1},
        {8,.37,0,.63,1},{9,.32,0,.67,0},{10,.33,1,.68,1},{11,.65,0,.35,1}
    };
    for (int version=1; version<=3; ++version) for (int easing=0; easing<=11; ++easing) {
        const std::string legacy="[HuntCrosshair]\nversion="+std::to_string(version)+
            "\neasing="+std::to_string(easing)+"\nx1=0.17\ny1=0.83\nx2=0.61\ny2=0.39\n";
        WriteFixture(path,legacy.c_str());
        b=hc::Settings{};
        Check(hc::LoadSettings(b),"legacy config failed to load");
        const LegacyCurve* migrated=nullptr;
        for(const auto& curve:removedCurves)if(curve.id==easing)migrated=&curve;
        if(migrated){
            Check(b.easing==4&&b.x1==migrated->x1&&b.y1==migrated->y1&&
                  b.x2==migrated->x2&&b.y2==migrated->y2,"removed preset migration changed its cubic");
            for(double t:{.1,.25,.5,.75,.9})
                Check(std::abs(hc::Ease(t,easing,.17,.83,.61,.39)-
                               hc::Ease(t,4,b.x1,b.y1,b.x2,b.y2))<1e-9,
                      "removed preset migration changed animation values");
            Check(hc::SaveSettings(b)&&hc::LoadSettings(b)&&b.easing==4&&b.x1==migrated->x1&&
                  b.y1==migrated->y1&&b.x2==migrated->x2&&b.y2==migrated->y2,
                  "migrated curve did not survive restart");
        }else{
            Check(b.easing==easing,"retained legacy preset ID changed");
            Check(b.x1==.17&&b.y1==.83&&b.x2==.61&&b.y2==.39,
                  "legacy custom cubic points failed to load");
        }
        Check(!b.customCrosshair&&b.dotShape==0&&BlankDefault(b.crossArt)&&BlankDefault(b.dotArt),
              "legacy art/shape defaults were not blank and disabled");
    }
    WriteFixture(path,"[HuntCrosshair]\nversion=3\neasing=4\nx1=0.1234567890123456\n"
        "y1=0.9876543210987654\nx2=0.4567890123456789\ny2=0.6543210987654321\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.easing==4&&b.x1==.1234567890123456&&b.y1==.9876543210987654&&
          b.x2==.4567890123456789&&b.y2==.6543210987654321,
          "current legacy custom curve was reset or rounded during migration");
    Check(hc::SaveSettings(b)&&hc::LoadSettings(b)&&b.easing==4&&b.x1==.1234567890123456&&
          b.y1==.9876543210987654&&b.x2==.4567890123456789&&b.y2==.6543210987654321,
          "current legacy custom curve changed after save/restart");
    WriteFixture(path,"[HuntCrosshair]\nversion=3\neasing=999999999999\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b) && b.easing==0, "oversized preset index load fallback failed");
    WriteFixture(path,"[HuntCrosshair]\nversion=3\neasing=-999999999999\n");
    Check(hc::LoadSettings(b) && b.easing==0, "negative preset index load clamp failed");
    WriteFixture(path,"[HuntCrosshair]\nversion=3\neasing=invalid\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b) && b.easing==0, "malformed preset index fallback failed");
    WriteFixture(path,"[HuntCrosshair]\nversion=1\noffsetX=-17\noffsetY=29\n");
    b=hc::Settings{}; b.offsetStep=2;
    Check(hc::LoadSettings(b), "version-1 integer settings load failed");
    Check(b.offsetX==-17 && b.offsetY==29 && b.offsetStep==1,
          "version-1 integer compatibility or missing-step default failed");

    const char corrupt[]="[HuntCrosshair]\nlength=17junk\ngap=6.75\nopacity=nan\n"
        "monitor=999999999999\nangle=1e999\nred=-8\noffsetX=-999999999999\n"
        "offsetY=999999999999\noffsetStep=999999999999\n";
    WriteFixture(path,corrupt);
    b=hc::Settings{}; Check(hc::LoadSettings(b), "malformed load failed");
    Check(b.length==16 && b.gap==6.75 && b.opacity==1, "malformed numeric fallback failed");
    Check(b.monitor==64 && b.angle==0 && b.color.r==0 && b.offsetX==-32768 &&
          b.offsetY==32768 && b.offsetStep==2, "oversized numeric clamp failed");
    WriteFixture(path,"[HuntCrosshair]\nversion=2\noffsetX=nan\noffsetY=1e999\noffsetStep=bad\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b), "nonfinite calibration fixture load failed");
    Check(b.offsetX==0 && b.offsetY==0 && b.offsetStep==1,
          "nonfinite calibration or malformed step fallback failed");

    WriteFixture(path,"[HuntCrosshair]\nversion=4\ncustomCrosshair=1\ndotShape=3\n"
        "crossArtWidth=1\ncrossArtHeight=2\ncrossArtData=aF00\n"
        "dotArtWidth=2\ndotArtHeight=1\ndotArtData=017f\n");
    b=hc::Settings{};
    Check(hc::LoadSettings(b)&&b.customCrosshair&&b.dotShape==3&&b.crossArt.width==1&&
          b.crossArt.height==2&&b.crossArt.pixels==std::vector<uint8_t>({175,0})&&
          b.dotArt.width==2&&b.dotArt.height==1&&b.dotArt.pixels==std::vector<uint8_t>({1,127}),
          "mixed-case alpha hex decoding failed");
    const char* malformedArt[]={
        "crossArtWidth=2\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=2\ncrossArtHeight=1\ncrossArtData=FFFFFF\n",
        "crossArtWidth=2\ncrossArtHeight=1\ncrossArtData=FFF\n",
        "crossArtWidth=2\ncrossArtHeight=1\ncrossArtData=FFG0\n",
        "crossArtWidth=0\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=101\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=1\ncrossArtHeight=101\ncrossArtData=FF\n",
        "crossArtWidth=-1\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=1.5\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=1\ncrossArtHeight=1.5\ncrossArtData=FF\n",
        "crossArtWidth=999999999999999999999999\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=nan\ncrossArtHeight=1\ncrossArtData=FF\n",
        "crossArtWidth=1\ncrossArtHeight=1e999\ncrossArtData=FF\n",
        "crossArtWidth=1\ncrossArtHeight=1\n",
        "crossArtData=FF\n"
    };
    for(const char* invalid:malformedArt){
        const std::string fixture=std::string("[HuntCrosshair]\nversion=4\ncustomCrosshair=1\n")+invalid+
            "dotArtWidth=1\ndotArtHeight=1\ndotArtData=80\n";
        WriteFixture(path,fixture.c_str());
        b=hc::Settings{};
        Check(hc::LoadSettings(b)&&BlankDefault(b.crossArt),"malformed alpha map did not become blank20x20");
        Check(b.dotArt.width==1&&b.dotArt.height==1&&b.dotArt.pixels==std::vector<uint8_t>({128}),
              "one malformed art map damaged the other valid map");
    }
    const std::string tooMuchArt="[HuntCrosshair]\nversion=4\ncrossArtWidth=100\n"
        "crossArtHeight=100\ncrossArtData="+std::string(20002,'F')+"\n";
    WriteFixture(path,tooMuchArt.c_str());b=hc::Settings{};
    Check(hc::LoadSettings(b)&&BlankDefault(b.crossArt),"oversized art data was accepted");
    const std::string oversizedFile(65537,'X');
    WriteFixture(path,oversizedFile.c_str());b=hc::Settings{};b.x1=.1234567890123456;
    Check(!hc::LoadSettings(b)&&b.x1==.1234567890123456,"oversized INI was read or modified settings");

    auto monitors=hc::Monitors();
    Check(!monitors.empty() && monitors.front().primary, "monitor enumeration failed");
    hc::Overlay overlay;
    Check(overlay.Create(GetModuleHandleW(nullptr)), "overlay creation failed");
    const auto style=GetWindowLongPtrW(overlay.Window(),GWL_EXSTYLE);
    const bool stylesOK=(style&WS_EX_LAYERED) && (style&WS_EX_TRANSPARENT) &&
                        (style&WS_EX_NOACTIVATE) && (style&WS_EX_TOOLWINDOW);
    overlay.Update(b,hc::MakeFrame(b,0),false);
    const bool hidden=!IsWindowVisible(overlay.Window());
    overlay.Destroy();
    Check(stylesOK && hidden, "overlay style/hidden state failed");
    const double before=hc::NowMs(); Sleep(2);
    Check(hc::NowMs()>before, "monotonic clock failed");
    Check(DeleteFileW(path.c_str())!=FALSE, "test config cleanup failed");
    std::cout<<"platform settings roundtrip, malformed input, monitor, overlay styles, clock: PASS\n";
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc==3 && std::wstring(argv[1])==L"--sandbox") {
        try { TestInSandbox(argv[2]); return 0; }
        catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    }
    std::wstring directory, copied, marker;
    HANDLE process=nullptr;
    bool ownsDirectory=false;
    int result=1;
    try {
        directory=TempPrefix()+std::to_wstring(GetCurrentProcessId())+L"-"+
                  std::to_wstring(GetTickCount64());
        Check(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE, "sandbox creation failed");
        ownsDirectory=true;
        marker=directory+L"\\.test-sandbox";
        HANDLE markerFile=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(markerFile!=INVALID_HANDLE_VALUE, "sandbox marker creation failed");
        CloseHandle(markerFile);
        wchar_t own[32768]{};
        Check(GetModuleFileNameW(nullptr,own,32768)>0, "test executable path failed");
        copied=directory+L"\\platform_tests.exe";
        Check(CopyFileW(own,copied.c_str(),TRUE)!=FALSE, "test executable copy failed");
        std::wstring command=L"\""+copied+L"\" --sandbox \""+directory+L"\"";
        STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
        Check(CreateProcessW(copied.c_str(),command.data(),nullptr,nullptr,FALSE,0,
                             nullptr,directory.c_str(),&si,&pi)!=FALSE,
              "isolated test process creation failed");
        process=pi.hProcess; CloseHandle(pi.hThread);
        if (WaitForSingleObject(process,30000)!=WAIT_OBJECT_0) {
            TerminateProcess(process,2); WaitForSingleObject(process,5000);
            throw std::runtime_error("isolated platform tests timed out");
        }
        DWORD code=1; Check(GetExitCodeProcess(process,&code)!=FALSE, "test exit code failed");
        result=static_cast<int>(code);
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; }
    if (process) CloseHandle(process);
    // Only exact files created inside this unique, nonrecursive test directory.
    if (ownsDirectory) {
        DeleteFileW((directory+L"\\HuntCrosshair.ini").c_str());
        if (!copied.empty()) DeleteFileW(copied.c_str());
        if (!marker.empty()) DeleteFileW(marker.c_str());
        RemoveDirectoryW(directory.c_str());
    }
    return result;
}
