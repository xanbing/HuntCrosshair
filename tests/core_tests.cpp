#include "../src/core.h"
#include "../src/easing_presets.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int Alpha(uint32_t p) {return p>>24;}
bool Close(double a,double b,double eps=1e-7) {return std::abs(a-b)<eps;}
void Symmetry(const std::vector<uint32_t>& pixels,int w,int h) {
    for (int y=0;y<h;++y) for (int x=0;x<w;++x)
        assert(pixels[y*w+x]==pixels[(h-1-y)*w+w-1-x]);
}
void DotCoverage() {
    hc::Settings s; s.length=0; s.dot=true;
    std::vector<uint32_t> p;
    hc::Rasterize(s,hc::MakeFrame(s,0),10,10,5,5,p);
    int count=0,total=0;
    for (auto v:p) {if (Alpha(v)) ++count; total+=Alpha(v);}
    assert(count==4 && std::abs(total-255)<=1);
    assert(Alpha(p[4*10+4])==64 && Alpha(p[5*10+5])==64);
    Symmetry(p,10,10);
    hc::Rasterize(s,hc::MakeFrame(s,0),11,11,5.5,5.5,p);
    count=0; for(auto v:p) if(Alpha(v)) ++count;
    assert(count==1 && Alpha(p[5*11+5])==255);
    s.opacity=.5;
    hc::Rasterize(s,hc::MakeFrame(s,0),10,10,5,5,p);
    assert(Alpha(p[4*10+4])==32);
    s.dotSize=2; s.opacity=1;
    hc::Rasterize(s,hc::MakeFrame(s,0),10,10,5,5,p);
    assert(Alpha(p[4*10+4])==255 && Alpha(p[5*10+5])==255);
}
void Geometry() {
    hc::Settings s;
    std::vector<uint32_t> p;
    for (double angle:{0.0,17.0,45.0,90.0,179.0,270.0}) {
        s.angle=angle;
        for (int size:{80,81}) {
            hc::Rasterize(s,hc::MakeFrame(s,0),size,size,size*.5,size*.5,p);
            Symmetry(p,size,size);
            for (auto v:p) {
                assert(static_cast<int>((v>>16)&255)<=Alpha(v));
                assert(static_cast<int>((v>>8)&255)<=Alpha(v));
                assert(static_cast<int>(v&255)<=Alpha(v));
            }
        }
    }
    s.angle=0; s.dot=false; s.length=10; s.gap=0; s.thickness=8;
    hc::Rasterize(s,hc::MakeFrame(s,0),40,40,20,20,p);
    // Arms overlap at the center: union coverage must stay fully opaque.
    assert(Alpha(p[19*40+19])==255);
    s.opacity=.5;
    hc::Rasterize(s,hc::MakeFrame(s,0),40,40,20,20,p);
    assert(Alpha(p[19*40+19])==128);
    hc::Rasterize(s,hc::MakeFrame(s,0),40,40,1e100,20,p);
    for(auto v:p) assert(v==0);
    s.opacity=std::numeric_limits<double>::quiet_NaN();
    hc::Rasterize(s,hc::MakeFrame(s,0),40,40,20,20,p);
    for(auto v:p) assert(v==0);
}
struct Center {double x,y,mass;};
Center ImageCenter(const std::vector<uint32_t>& pixels,int w,int h,int channel) {
    Center result{};
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        const double weight=(pixels[y*w+x]>>channel)&255;
        result.x+=(x+.5)*weight;result.y+=(y+.5)*weight;result.mass+=weight;
    }
    assert(result.mass>0);
    result.x/=result.mass;result.y/=result.mass;return result;
}
void FractionalGeometry() {
    hc::Settings s;s.color={210,150,95};s.dotColor={80,220,140};
    std::vector<uint32_t> base,current,next;
    for (int size:{80,81}) for (double angle:{0.0,17.0,45.0,73.25}) {
        s.angle=angle;
        const double center=size*.5;
        for (double offset:{.25,.0625,-.1875}) {
            const double cx=center+offset,cy=center-offset*.5;
            hc::Rasterize(s,hc::MakeFrame(s,0),size,size,cx,cy,current);
            // Coverage and each premultiplied color channel remain centered;
            // tolerances account for quantizing coverage/color to 8 bits.
            const Center alpha=ImageCenter(current,size,size,24);
            assert(Close(alpha.x,cx,.008) && Close(alpha.y,cy,.008));
            for (int channel:{0,8,16}) {
                const Center color=ImageCenter(current,size,size,channel);
                assert(Close(color.x,cx,.025) && Close(color.y,cy,.025));
            }
        }
        // Quarter and sixteenth offsets must affect pixels instead of being
        // rounded to integer coordinates or an 8x8 supersampling grid.
        s.dot=false;
        hc::Rasterize(s,hc::MakeFrame(s,0),size,size,center,center,base);
        hc::Rasterize(s,hc::MakeFrame(s,0),size,size,center+.25,center,current);
        assert(base!=current);
        current=base;
        for(int step=1;step<=16;++step) {
            hc::Rasterize(s,hc::MakeFrame(s,0),size,size,
                          center+step/16.0,center,next);
            assert(next!=current);current=next;
        }
        // Sixteen fractional steps equal exactly one whole-pixel translation.
        for(int y=0;y<size;++y) for(int x=1;x<size;++x)
            assert(current[y*size+x]==base[y*size+x-1]);
        s.dot=true;
    }
    // Exact union coverage is preserved when wide rotated arms overlap.
    s.dot=false;s.length=10;s.gap=0;s.thickness=8;
    for(double angle:{0.0,17.0,45.0}) {
        s.angle=angle;
        hc::Rasterize(s,hc::MakeFrame(s,0),80,80,40.0625,39.75,current);
        const Center alpha=ImageCenter(current,80,80,24);
        assert(Close(alpha.mass/255.0,256,.5));
        assert(Close(alpha.x,40.0625,.008) && Close(alpha.y,39.75,.008));
    }
    // A nominal 1px point still has unit integrated coverage at fractional
    // offsets; color/alpha quantization is bounded by four half-byte errors.
    s.length=0;s.dot=true;s.dotSize=1;
    for(int size:{10,11}) for(double offset:{.25,.0625}) {
        const double cx=size*.5+offset,cy=size*.5-offset;
        hc::Rasterize(s,hc::MakeFrame(s,0),size,size,cx,cy,current);
        const Center alpha=ImageCenter(current,size,size,24);
        assert(Close(alpha.mass/255.0,1,2.0/255+1e-8));
        assert(Close(alpha.x,cx,.004) && Close(alpha.y,cy,.004));
    }
}
void Frames() {
    hc::Settings s; s.dot=false;
    auto f=hc::MakeFrame(s,0); assert(f.arms==1 && f.gap==1 && f.dotAlpha==0);
    f=hc::MakeFrame(s,1); assert(f.arms==0 && f.gap==0 && f.dotAlpha==1);
    f=hc::MakeFrame(s,.5); assert(f.arms==.5 && f.gap==.5 && f.dotAlpha==.5);
    s.action=1; f=hc::MakeFrame(s,1); assert(f.alpha==0 && f.arms==1 && f.dotAlpha==0);
    s.action=0; f=hc::MakeFrame(s,1); assert(f.alpha==1 && f.arms==1 && f.dotAlpha==0);
}
void Curves() {
    for(int kind=0;kind<=4;++kind) {
        assert(hc::Ease(0,kind,.25,.1,.25,1)==0);
        assert(hc::Ease(1,kind,.25,.1,.25,1)==1);
        double before=0;
        for (int i=0;i<=100;++i) {
            double v=hc::Ease(i/100.0,kind,.25,.1,.25,1);
            assert(v>=before && v>=0 && v<=1); before=v;
        }
    }
    assert(hc::Ease(.5,1,0,0,0,0)<.5);
    assert(hc::Ease(.5,2,0,0,0,0)>.5);
    assert(Close(hc::Ease(.5,3,0,0,0,0),.5));
    assert(Close(hc::Ease(.4,4,.2,.2,.8,.8),.4));
    // Parametric x != time for this curve. Reference value from CSS ease.
    assert(Close(hc::Ease(.5,4,.25,.1,.25,1),.8024033876,1e-7));
}
void ClassicPresets() {
    static_assert(hc::kEasingPresetCount==4,"In/out and two immutable damping presets");
    static_assert(hc::ResolvePreset(1)->x1==.42,"Legacy saved Ease In ID preserved");
    assert(!hc::ResolvePreset(0) && !hc::ResolvePreset(4));
    assert(!hc::ResolvePreset(-1) && !hc::ResolvePreset(12) && !hc::ResolvePreset(13));
    int legacy=0;
    std::vector<const hc::EasingPreset*> all;
    for(const auto& preset:hc::kEasingPresets)all.push_back(&preset);
    for(const auto& preset:hc::kLegacyEasingPresets)all.push_back(&preset);
    assert(all.size()==12 && hc::kEasingPresets[0].id==1 && hc::kEasingPresets[1].id==2);
    assert(hc::kEasingPresets[2].id==14 && hc::kEasingPresets[3].id==15);
    const auto* damping=hc::ResolvePreset(14);const auto* damping2=hc::ResolvePreset(15);
    assert(damping->x1==.492753623188 && damping->y1==.327586206897 &&
           damping->x2==.728260869565 && damping->y2==.310344827586);
    assert(damping2->x1==0 && damping2->y1==1 && damping2->x2==0 && damping2->y2==1);
    for (const auto* entry:all) {
        const auto& preset=*entry;
        assert(preset.label && preset.label[0]);
        assert(hc::ResolvePreset(preset.id)==&preset);
        if(preset.id>=1 && preset.id<=3)++legacy;
        double before=0;
        for (int i=0;i<=100;++i) {
            const double t=i/100.0;
            // A preset must ignore stale custom controls and evaluate its
            // declared cubic, including x inversion, with identical results.
            const double actual=hc::Ease(t,preset.id,.95,.05,.05,.95);
            const double reference=hc::Ease(t,4,preset.x1,preset.y1,preset.x2,preset.y2);
            assert(Close(actual,reference,1e-12));
            assert(actual>=before && actual>=0 && actual<=1);
            before=actual;
        }
        assert(hc::Ease(0,preset.id,0,0,0,0)==0);
        assert(hc::Ease(1,preset.id,0,0,0,0)==1);
    }
    assert(legacy==3);
    for (int id:{1,6,9}) {
        assert(hc::Ease(.25,id,0,0,0,0)<.25);
        assert(hc::Ease(.75,id,0,0,0,0)<.75);
    }
    for (int id:{2,7,10}) {
        assert(hc::Ease(.25,id,0,0,0,0)>.25);
        assert(hc::Ease(.75,id,0,0,0,0)>.75);
    }
    for (int id:{3,8,11}) {
        assert(hc::Ease(.25,id,0,0,0,0)<.25);
        assert(Close(hc::Ease(.5,id,0,0,0,0),.5));
        assert(hc::Ease(.75,id,0,0,0,0)>.75);
    }
    assert(hc::Ease(.5,5,0,0,0,0)>.5);
    // Presets have distinct timing curves, rather than distinct labels for
    // the same function. Checking multiple samples covers symmetric curves.
    for (size_t a=0;a<all.size();++a)
        for (size_t b=a+1;b<all.size();++b) {
            double distance=0;
            for (double t:{.1,.25,.5,.75,.9})
                distance+=std::abs(hc::Ease(t,all[a]->id,0,0,0,0)-
                                   hc::Ease(t,all[b]->id,0,0,0,0));
            assert(distance>1e-4);
        }
    assert(hc::Ease(.37,-1,.25,.1,.25,1)==.37);
    assert(hc::Ease(.37,16,.25,.1,.25,1)==.37);
    assert(hc::Ease(.37,12,.25,.1,.25,1)==.37);
    assert(hc::Ease(.37,13,.25,.1,.25,1)==.37);
}
double Mass(const std::vector<uint32_t>& pixels) {
    double result=0;for(auto pixel:pixels)result+=Alpha(pixel)/255.0;return result;
}
hc::PixelArt Art(int w,int h,std::initializer_list<uint8_t> pixels) {
    hc::PixelArt art;art.width=w;art.height=h;art.pixels=pixels;return art;
}
void DotShapes() {
    hc::Settings s;s.length=0;s.dotSize=4;s.dotShape=1;
    std::vector<uint32_t> pixels,other;
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16,16,pixels);
    Symmetry(pixels,32,32);
    assert(Close(Mass(pixels),3.14159265358979323846*4,.04));
    assert(Alpha(pixels[14*32+14])>0 && Alpha(pixels[14*32+14])<255);
    assert(Alpha(pixels[15*32+15])==255 && Alpha(pixels[13*32+13])==0);
    s.angle=71;hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16,16,other);
    assert(pixels==other); // Point shapes stay screen aligned.
    s.dotSize=1;
    hc::Rasterize(s,hc::MakeFrame(s,0),33,33,16.5,16.5,pixels);
    assert(Alpha(pixels[16*33+16])==200);

    s.dotSize=4;s.dotShape=2;
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16,16,pixels);
    assert(Close(Mass(pixels),12,.04)); // width4, height6, sharp upward tip at origin.
    double top=0,bottom=0;
    for(int x=0;x<32;++x){top+=Alpha(pixels[16*32+x]);bottom+=Alpha(pixels[21*32+x]);}
    assert(bottom>top*8 && top>0); // Narrow upward tip, broad flat base below it.
    for(int y=0;y<16;++y)for(int x=0;x<32;++x)assert(Alpha(pixels[y*32+x])==0);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)
        assert(pixels[y*32+x]==pixels[y*32+31-x]);
    const Center triangleCenter=ImageCenter(pixels,32,32,24);
    assert(Close(triangleCenter.x,16,.001) && Close(triangleCenter.y,20,.03));
    s.angle=0;hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16,16,other);assert(pixels==other);

    s.dotShape=3;s.dotSize=1;
    s.dotArt=Art(3,3,{255,255,255,255,0,255,255,255,255});
    hc::Rasterize(s,hc::MakeFrame(s,0),31,31,15.5,15.5,pixels);
    assert(Close(Mass(pixels),8) && Alpha(pixels[15*31+15])==0);
    assert(Alpha(pixels[14*31+14])==255 && Alpha(pixels[16*31+16])==255);
    s.dotSize=16;hc::Rasterize(s,hc::MakeFrame(s,0),31,31,15.5,15.5,other);
    assert(pixels==other); // One source cell remains one physical pixel.
    s.dotArt=Art(1,1,{128});
    hc::Rasterize(s,hc::MakeFrame(s,0),31,31,15.5,15.5,pixels);
    assert(Alpha(pixels[15*31+15])==128);
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16,16,pixels);
    assert(Alpha(pixels[15*32+15])==32 && Alpha(pixels[16*32+16])==32);
}
void CanvasGeometry() {
    hc::Settings s;s.customCrosshair=true;s.dot=false;
    s.crossArt=Art(3,3,{255,0,0,128,0,0,255,255,255});
    std::vector<uint32_t> pixels,base;
    hc::Rasterize(s,hc::MakeFrame(s,0),31,31,15.5,15.5,base);
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)
        assert(Alpha(base[(14+y)*31+14+x])==s.crossArt.pixels[y*3+x]);
    s.angle=90;
    hc::Rasterize(s,hc::MakeFrame(s,0),31,31,15.5,15.5,pixels);
    for(int y=0;y<3;++y)for(int x=0;x<3;++x)
        assert(Alpha(pixels[(14+x)*31+14+2-y])==s.crossArt.pixels[y*3+x]);
    s.angle=17.25;
    hc::Rasterize(s,hc::MakeFrame(s,0),40,40,20.0625,19.75,pixels);
    assert(Close(Mass(pixels),4+128.0/255,.06));
    hc::Rasterize(s,hc::MakeFrame(s,0),40,40,20.125,19.75,base);assert(pixels!=base);

    s.angle=0;s.crossArt.width=s.crossArt.height=5;s.crossArt.pixels.assign(25,0);
    for(int y=0;y<5;++y)for(int x=0;x<5;++x)
        if(x==0||x==4||y==0||y==4)s.crossArt.pixels[y*5+x]=255;
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16.75,16.75,pixels);
    assert(Alpha(pixels[16*32+16])==0 && Close(Mass(pixels),16,.04));
    s.angle=45;
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16.5,16.5,pixels);
    assert(Alpha(pixels[16*32+16])==0 && Close(Mass(pixels),16,.08));

    s.angle=0;s.crossArt=Art(2,2,{255,255,255,255});
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16.25,16.25,pixels);
    assert(Alpha(pixels[15*32+15])==255); // Adjacent source cells have no AA seams.
    hc::Frame half=hc::MakeFrame(s,.5);half.dotAlpha=0;
    hc::Rasterize(s,half,32,32,16,16,pixels);assert(Close(Mass(pixels),1,.01));
    hc::Rasterize(s,hc::MakeFrame(s,1),32,32,16,16,pixels);
    assert(Close(Mass(pixels),1,.01)); // Morph endpoint is the normal1px point.
    s.crossArt.width=-1;s.crossArt.pixels.clear();
    hc::Rasterize(s,hc::MakeFrame(s,0),32,32,16,16,pixels);assert(Mass(pixels)==0);
}
struct InkBox {int left,top,right,bottom;};
InkBox Bounds(const std::vector<uint32_t>& pixels,int w,int h) {
    InkBox box{w,h,0,0};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)if(Alpha(pixels[y*w+x])) {
        box.left=std::min(box.left,x);box.top=std::min(box.top,y);
        box.right=std::max(box.right,x+1);box.bottom=std::max(box.bottom,y+1);
    }
    return box;
}
void PointOpacityMatches(const std::vector<uint32_t>& full,
                         const std::vector<uint32_t>& faded,double opacity) {
    assert(full.size()==faded.size());
    for(size_t i=0;i<full.size();++i)for(int shift:{0,8,16,24}) {
        const int expected=static_cast<int>(std::lround(((full[i]>>shift)&255)*opacity));
        const int actual=(faded[i]>>shift)&255;
        assert(std::abs(actual-expected)<=1); // Single-byte coverage quantization.
    }
}
void FixedPointAndSizes() {
    for(int stale:{12,13})for(int i=0;i<=100;++i)
        assert(hc::Ease(i/100.0,stale,0,0,0,0)==i/100.0);
    hc::Settings s;s.length=0;s.gap=0;s.dotSize=4;
    s.dotArt=Art(3,3,{255,255,255,255,0,255,255,255,255});s.dotArtSize=6;
    std::vector<uint32_t> full,faded,other;
    for(int shape=0;shape<4;++shape)for(int easing:{14,15}) {
        s.dotShape=shape;s.action=1;s.dot=true;s.easing=easing;
        hc::Rasterize(s,hc::MakeFrame(s,0),64,64,32.0625,31.75,full);
        hc::Animation fade;fade.SetTarget(true,0,s);
        for(double time:{0.0,60.0,150.0,240.0,300.0}) {
            const double p=fade.Value(time,s);const auto frame=hc::MakeFrame(s,p);
            assert(p>=0 && p<=1 && frame.dotScale==1);
            hc::Rasterize(s,frame,64,64,32.0625,31.75,faded);
            PointOpacityMatches(full,faded,1-p);
        }
        // Even a stale caller-supplied pulse cannot resize a point.
        auto fixed=hc::MakeFrame(s,0);fixed.dotScale=2;
        hc::Rasterize(s,fixed,64,64,32.0625,31.75,other);assert(full==other);
        s.action=2;s.dot=false;
        hc::Rasterize(s,hc::MakeFrame(s,1),64,64,32.0625,31.75,full);
        hc::Animation appear;appear.SetTarget(true,0,s);
        for(double time:{0.0,60.0,150.0,240.0,300.0}) {
            const double p=appear.Value(time,s);const auto frame=hc::MakeFrame(s,p);
            assert(frame.dotScale==1);
            hc::Rasterize(s,frame,64,64,32.0625,31.75,faded);
            PointOpacityMatches(full,faded,p);
        }
    }
    assert(hc::MakeFrame(s,1.1).dotScale==1 && hc::MakeFrame(s,-.1).arms==1);
    assert(hc::MakeFrame(s,std::numeric_limits<double>::quiet_NaN()).dotScale==1);

    s=hc::Settings{};s.dot=false;
    hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,full);
    s.crossSize=44;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);
    assert(full==other); // Zero retains the exact original procedural geometry.
    s.crossSize=20;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,full);
    const InkBox size20=Bounds(full,128,128);
    s.crossSize=40;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);
    const InkBox size40=Bounds(other,128,128);
    assert(size20.right-size20.left==20 && size20.bottom-size20.top==20);
    assert(size40.right-size40.left==40 && size40.bottom-size40.top==40);
    assert(Close(Mass(other)/Mass(full),4,.04)); // Thickness/gap scale with length.

    s.customCrosshair=true;s.crossArt=Art(4,2,{255,255,255,255,255,255,255,255});
    const auto source=s.crossArt.pixels;
    s.crossSize=20;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,full);
    const InkBox wide20=Bounds(full,128,128);
    s.crossSize=40;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);
    const InkBox wide40=Bounds(other,128,128);
    assert(wide20.right-wide20.left==20 && wide20.bottom-wide20.top==10);
    assert(wide40.right-wide40.left==40 && wide40.bottom-wide40.top==20);
    assert(s.crossArt.width==4 && s.crossArt.height==2 && s.crossArt.pixels==source);
    s.angle=90;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);
    const InkBox turned=Bounds(other,128,128);
    assert(turned.right-turned.left==20 && turned.bottom-turned.top==40);
    s.angle=0;
    hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64.0625,63.75,other);
    const Center center=ImageCenter(other,128,128,24);
    assert(Close(center.x,64.0625,.003) && Close(center.y,63.75,.003));
    s.crossSize=0;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,full);
    s.crossSize=4;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);assert(full==other);

    s=hc::Settings{};s.length=0;s.dotShape=3;
    s.dotArt=Art(2,4,{255,255,255,255,255,255,255,255});
    s.dotArtSize=20;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,full);
    const InkBox dot20=Bounds(full,128,128);
    s.dotArtSize=40;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);
    const InkBox dot40=Bounds(other,128,128);
    assert(dot20.right-dot20.left==10 && dot20.bottom-dot20.top==20);
    assert(dot40.right-dot40.left==20 && dot40.bottom-dot40.top==40);
    assert(s.dotArt.width==2 && s.dotArt.height==4 && s.dotArt.pixels.size()==8);
    hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64.0625,63.75,other);
    const Center dotCenter=ImageCenter(other,128,128,24);
    assert(Close(dotCenter.x,64.0625,.003) && Close(dotCenter.y,63.75,.003));
    s.dotArtSize=0;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,full);
    s.dotArtSize=4;hc::Rasterize(s,hc::MakeFrame(s,0),128,128,64,64,other);assert(full==other);

    // The radius includes a triangle's entire below-anchor height, and custom
    // display sizing, independently of the unchanged source canvas matrix.
    s=hc::Settings{};s.length=s.gap=0;s.dotShape=2;s.dotSize=8;
    assert(hc::CrosshairRadius(s)>=12);
    const int size=2*static_cast<int>(std::ceil(hc::CrosshairRadius(s)+3));
    hc::Rasterize(s,hc::MakeFrame(s,0),size,size,size*.5,size*.5,full);
    hc::Rasterize(s,hc::MakeFrame(s,0),512,512,256,256,other);
    const int origin=256-size/2;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x)
        assert(full[y*size+x]==other[(origin+y)*512+origin+x]);
}
void Interruptions() {
    hc::Settings s;
    hc::Animation a;
    assert(a.Value(0,s)==0 && !a.Running(0));
    a.SetTarget(true,0,s);
    assert(a.Running(0) && Close(a.Value(150,s),.5));
    a.SetTarget(true,150,s); // No restart for a repeated target.
    assert(Close(a.Value(225,s),.75));
    a.SetTarget(false,150,s);
    assert(Close(a.Value(150,s),.5));
    assert(Close(a.Value(225,s),.25));
    assert(a.Value(300,s)==0 && !a.Running(300));
    a.SetTarget(true,400,s);
    a.SetTarget(false,475,s);
    a.SetTarget(true,500,s);
    const double start=a.Value(500,s);
    assert(Close(start,1.0/6));
    assert(Close(a.Value(750,s),1));
    a.Reset(); s.enterMs=0;
    a.SetTarget(true,0,s); assert(a.Value(0,s)==1 && !a.Running(0));
    a.Reset(); s.enterMs=300; s.easing=1;
    a.SetTarget(true,0,s);
    double before=a.Value(150,s);
    s.easing=2;
    assert(Close(a.Value(150,s),before)); // Curve is captured at transition start.
    a.SetTarget(false,150,s);
    assert(Close(a.Value(150,s),before));
    // Each preset is captured at transition start, and interrupting it takes
    // the current visual value as the next transition's starting point.
    for (const auto& preset:hc::kEasingPresets) {
        a.Reset();s.enterMs=s.leaveMs=300;s.easing=preset.id;
        a.SetTarget(true,0,s);
        const double value=a.Value(105,s);
        s.easing=preset.id==10?11:10;
        s.x1=.9;s.y1=.8;s.x2=.1;s.y2=.2;
        assert(Close(a.Value(105,s),value));
        a.SetTarget(false,105,s);
        assert(Close(a.Value(105,s),value));
        const double expected=value*(1-hc::Ease(.5,s.easing,s.x1,s.y1,s.x2,s.y2));
        assert(Close(a.Value(105+150*value,s),expected));
        assert(a.Value(105+300*value+1,s)==0);
    }
}
}
int main() {
    DotCoverage(); Geometry(); FractionalGeometry(); DotShapes(); CanvasGeometry();
    Frames(); Curves(); ClassicPresets(); FixedPointAndSizes(); Interruptions();
    std::puts("All fixed-point fades, immutable damping curves, global sizing, analytic shapes and fractional geometry checks passed.");
}
