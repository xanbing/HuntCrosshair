#include "settings_session.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
int checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
hc::Settings SavedState(){
    hc::Settings s;
    s.length=29;s.gap=7;s.thickness=3;s.opacity=.73;s.angle=37;s.crossSize=123.5;
    s.color={31,207,149};s.customCrosshair=true;
    s.crossArt.width=100;s.crossArt.height=100;s.crossArt.pixels.resize(10000);
    s.dot=false;s.dotSize=9;s.dotOpacity=.46;s.dotArtSize=83.25;s.dotColor={254,111,21};s.dotShape=3;
    s.dotArt.width=7;s.dotArt.height=3;s.dotArt.pixels.resize(21);
    for(size_t i=0;i<s.crossArt.pixels.size();++i)s.crossArt.pixels[i]=static_cast<uint8_t>((i*37+13)%256);
    for(size_t i=0;i<s.dotArt.pixels.size();++i)s.dotArt.pixels[i]=static_cast<uint8_t>((i*19+7)%256);
    s.action=1;s.trigger=1;s.enterMs=417;s.leaveMs=682;s.easing=4;
    s.x1=.1234567890123456;s.y1=.9876543210987654;s.x2=.4567890123456789;s.y2=.6543210987654321;
    s.monitor=2;s.offsetX=-17.0625;s.offsetY=8.25;s.offsetStep=2;s.onlyGame=true;s.enabled=false;
    s.skin=1;s.hotkeys[0]={2,0x78};s.hotkeys[2]={};
    return s;
}
void TestAllFields(){
    using Edit=hc::SettingsSession::RuntimeMutator;
    std::vector<Edit> edits={
        [](auto& s){s.length+=1;},[](auto& s){s.gap+=1;},[](auto& s){s.thickness+=1;},
        [](auto& s){s.opacity=.15;},[](auto& s){s.angle+=1;},[](auto& s){s.crossSize+=1;},
        [](auto& s){s.color.r+=1;},[](auto& s){s.color.g+=1;},[](auto& s){s.color.b+=1;},
        [](auto& s){s.customCrosshair=!s.customCrosshair;},
        [](auto& s){s.crossArt.width-=1;},[](auto& s){s.crossArt.height-=1;},
        [](auto& s){s.crossArt.pixels[877]=15;},[](auto& s){s.crossArt.pixels.pop_back();},
        [](auto& s){s.dot=!s.dot;},[](auto& s){s.dotSize+=1;},[](auto& s){s.dotOpacity=.11;},
        [](auto& s){s.dotArtSize+=1;},[](auto& s){s.dotColor.r-=1;},
        [](auto& s){s.dotColor.g-=1;},[](auto& s){s.dotColor.b-=1;},[](auto& s){s.dotShape=1;},
        [](auto& s){s.dotArt.width-=1;},[](auto& s){s.dotArt.height-=1;},
        [](auto& s){s.dotArt.pixels[10]=15;},[](auto& s){s.dotArt.pixels.push_back(0);},
        [](auto& s){s.action=2;},[](auto& s){s.trigger=0;},
        [](auto& s){s.enterMs+=1;},[](auto& s){s.leaveMs+=1;},[](auto& s){s.easing=15;},
        [](auto& s){s.x1=std::nextafter(s.x1,1.0);},[](auto& s){s.y1=std::nextafter(s.y1,1.0);},
        [](auto& s){s.x2=std::nextafter(s.x2,1.0);},[](auto& s){s.y2=std::nextafter(s.y2,1.0);},
        [](auto& s){s.monitor+=1;},[](auto& s){s.offsetX+=.0625;},[](auto& s){s.offsetY+=.0625;},
        [](auto& s){s.offsetStep=1;},[](auto& s){s.onlyGame=!s.onlyGame;},[](auto& s){s.enabled=!s.enabled;},
        [](auto& s){s.skin=0;}
    };
    for(int i=0;i<hc::kHotkeyCount;++i){
        edits.emplace_back([i](auto& s){s.hotkeys[i].key=0x70+i;});
        edits.emplace_back([i](auto& s){s.hotkeys[i].modifiers^=2;});
    }
    const auto saved=SavedState();hc::SettingsSession session(saved);
    for(const auto& edit:edits){
        Check(!session.HasChanges(),"fresh/cancelled draft was dirty");
        edit(session.Draft());
        Check(session.HasChanges(),"a Settings scalar, color, mask or exact double edit was missed");
        Check(hc::SettingsEqual(session.Applied(),saved),"draft edit mutated applied state");
        session.Cancel();
        Check(hc::SettingsEqual(session.Draft(),saved),"cancel did not restore a field");
    }
    auto source=saved;session.Begin(source);source.crossArt.pixels[0]^=255;
    Check(!session.HasChanges()&&hc::SettingsEqual(session.Applied(),saved),"Begin retained input mask alias");
    session.Draft().dotArt.pixels[0]^=255;
    Check(session.Applied().dotArt.pixels[0]==saved.dotArt.pixels[0],"draft/applied masks share writable storage");
    session.Begin(session.Draft());
    Check(!session.HasChanges(),"Begin from own draft failed");
}
void TestApplyCancel(){
    hc::SettingsSession session(SavedState());
    const auto original=session.Applied();
    session.Draft().crossArt.pixels[55]=73;session.Draft().dotArt.pixels[4]=218;
    session.Draft().x1=.3333333333333333;session.Draft().crossSize=177.25;
    session.Draft().dotArtSize=199.5;
    auto pending=session.Draft();int saves=0;hc::Settings persisted;
    Check(!session.Apply({}),"empty save callback committed a draft");
    Check(!session.Apply([&](const auto& candidate){++saves;persisted=candidate;return false;}),
          "failed save reported success");
    Check(saves==1&&hc::SettingsEqual(persisted,pending),"save callback did not receive full draft");
    Check(hc::SettingsEqual(session.Applied(),original)&&hc::SettingsEqual(session.Draft(),pending)&&
          session.HasChanges(),"failed save altered draft or baseline");
    Check(session.Apply([&](const auto& candidate){++saves;persisted=candidate;return true;}),"successful apply failed");
    Check(!session.HasChanges()&&hc::SettingsEqual(session.Applied(),pending),"apply did not advance baseline");
    session.Draft().crossArt.pixels[55]=0;session.Draft().dotArt.pixels[4]=0;
    session.Draft().x1=0;session.Draft().crossSize=0;session.Draft().dotArtSize=0;
    Check(hc::SettingsEqual(session.Applied(),persisted),"post-apply edit aliased saved state");
    session.Cancel();
    Check(!session.HasChanges()&&hc::SettingsEqual(session.Draft(),pending),
          "cancel after apply lost last committed masks, precision or scales");
    session.ResetDefaults();
    Check(saves==2&&session.HasChanges()&&hc::SettingsEqual(session.Draft(),hc::Settings{})&&
          hc::SettingsEqual(session.Applied(),pending),"reset defaults persisted or changed baseline");
    session.Cancel();Check(hc::SettingsEqual(session.Draft(),pending),"cancel after reset failed");
    session.ResetDefaults();
    Check(session.Apply([&](const auto& s){++saves;persisted=s;return true;})&&saves==3&&
          !session.HasChanges()&&hc::SettingsEqual(session.Applied(),hc::Settings{}),
          "explicit apply did not commit reset defaults");
}
void TestRuntimeMerge(){
    hc::SettingsSession session(SavedState());
    session.Draft().color.r=119;session.Draft().crossArt.pixels[918]=224;
    session.Draft().x2=.1111111111111111;session.Draft().dotArtSize=112.75;
    session.Draft().offsetX=99; // A hotkey deliberately supersedes this field only.
    session.Draft().skin=0;session.Draft().hotkeys[4]={2,'R'};
    const auto pending=session.Draft(),oldApplied=session.Applied();int mutations=0,saves=0;
    Check(session.CommitRuntimeChange([&](auto& s){++mutations;s.enabled=!s.enabled;s.offsetX+=.25;},
          [&](const auto& candidate){++saves;Check(candidate.color.r==oldApplied.color.r,
          "runtime save accidentally included pending color");return true;}),"runtime commit failed");
    Check(mutations==1&&saves==1,"runtime mutator/save was called more than once");
    Check(session.Applied().enabled&&session.Draft().enabled&&
          session.Applied().offsetX==oldApplied.offsetX+.25&&session.Draft().offsetX==session.Applied().offsetX,
          "runtime fields did not reach both applied and draft");
    Check(session.Draft().color.r==pending.color.r&&session.Draft().crossArt.pixels[918]==224&&
          session.Draft().x2==pending.x2&&session.Draft().dotArtSize==112.75&&
          session.Draft().skin==0&&session.Draft().hotkeys[4].modifiers==2&&
          session.Draft().hotkeys[4].key=='R'&&session.HasChanges(),
          "runtime hotkey discarded unrelated pending draft data");
    const auto beforeDraft=session.Draft(),beforeApplied=session.Applied();
    Check(!session.CommitRuntimeChange([](auto& s){s.offsetY+=1;},[](const auto&){return false;}),
          "failed runtime save committed");
    Check(hc::SettingsEqual(session.Draft(),beforeDraft)&&hc::SettingsEqual(session.Applied(),beforeApplied),
          "failed runtime save changed state");
    Check(!session.CommitRuntimeChange({}),"empty runtime mutator succeeded");
    Check(session.CommitRuntimeChange([](auto& s){s.offsetY-=.0625;}),"memory-only runtime change failed");
    const auto runtime=session.Applied();session.Cancel();
    Check(!session.HasChanges()&&hc::SettingsEqual(session.Draft(),runtime),"cancel reverted a committed runtime change");
}
void TestSnapshotsAndNumbers(){
    hc::SettingsSession session;
    session.Draft().length=33;
    Check(session.Apply([&](const auto& saved){Check(saved.length==33,"save snapshot incorrect");
        session.Draft().length=44;return true;}),"snapshot apply failed");
    Check(session.Applied().length==33&&session.Draft().length==44&&session.HasChanges(),
          "apply discarded edits made after its snapshot");
    session.Cancel();session.Draft().length=55;
    const auto before=session.Applied();
    bool threw=false;try{session.Apply([](const auto&)->bool{throw std::runtime_error("save error");});}
    catch(const std::runtime_error&){threw=true;}
    Check(threw&&hc::SettingsEqual(session.Applied(),before)&&session.Draft().length==55,
          "throwing save changed baseline or draft");
    hc::Settings unusual;unusual.x1=std::numeric_limits<double>::quiet_NaN();
    unusual.offsetX=-0.0;unusual.enterMs=std::numeric_limits<double>::infinity();session.Begin(unusual);
    Check(!session.HasChanges(),"copied NaN/Infinity made baseline dirty");
    session.Draft().offsetX=0.0;Check(!session.HasChanges(),"signed zero was treated as a visual change");
    session.Draft().enterMs=-std::numeric_limits<double>::infinity();
    Check(session.HasChanges(),"different infinite values were missed");
}
}
int main(){
    try{TestAllFields();TestApplyCancel();TestRuntimeMerge();TestSnapshotsAndNumbers();
        std::cout<<"settings sessions: "<<checks<<" checks PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<"settings session failure: "<<e.what()<<'\n';return 1;}
}
