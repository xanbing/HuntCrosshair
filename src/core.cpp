#include "core.h"
#include "easing_presets.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hc {
namespace {
double Limit(double value, double low, double high, double fallback=0) {
    if (!std::isfinite(value)) value=fallback;
    return std::max(low, std::min(high,value));
}
double Unit(double value) {return Limit(value,0,1);}
double Bezier(double t, double a, double b) {
    const double u=1-t;
    return 3*u*u*t*a+3*u*t*t*b+t*t*t;
}
struct Rect {double l,t,r,b;};
double Area(const Rect& r, double x, double y) {
    return std::max(0.0,std::min(r.r,x+1)-std::max(r.l,x))*
           std::max(0.0,std::min(r.b,y+1)-std::max(r.t,y));
}
Rect Intersection(const Rect& a, const Rect& b) {
    return {std::max(a.l,b.l),std::max(a.t,b.t),
            std::min(a.r,b.r),std::min(a.b,b.b)};
}
struct Point { double x,y; };
int Clip(Point* polygon,int count,int axis,double boundary,bool keepGreater) {
    if (!count) return 0;
    Point result[12];int output=0;
    Point previous=polygon[count-1];
    double previousValue=axis?previous.y:previous.x;
    bool previousInside=keepGreater?previousValue>=boundary:previousValue<=boundary;
    for (int i=0;i<count;++i) {
        const Point current=polygon[i];
        const double value=axis?current.y:current.x;
        const bool inside=keepGreater?value>=boundary:value<=boundary;
        if (inside!=previousInside) {
            const double t=(boundary-previousValue)/(value-previousValue);
            Point crossing{previous.x+(current.x-previous.x)*t,
                           previous.y+(current.y-previous.y)*t};
            if (axis) crossing.y=boundary; else crossing.x=boundary;
            result[output++]=crossing;
        }
        if (inside) result[output++]=current;
        previous=current;previousValue=value;previousInside=inside;
    }
    std::copy(result,result+output,polygon);
    return output;
}
double RotatedArea(const Rect& rect,const Point* pixel,double localX,double localY,double extent) {
    if (rect.r<=rect.l || rect.b<=rect.t ||
        localX+extent<=rect.l || localX-extent>=rect.r ||
        localY+extent<=rect.t || localY-extent>=rect.b) return 0;
    if (localX-extent>=rect.l && localX+extent<=rect.r &&
        localY-extent>=rect.t && localY+extent<=rect.b) return 1;
    Point clipped[12];std::copy(pixel,pixel+4,clipped);
    int count=Clip(clipped,4,0,rect.l,true);
    count=Clip(clipped,count,0,rect.r,false);
    count=Clip(clipped,count,1,rect.t,true);
    count=Clip(clipped,count,1,rect.b,false);
    if (count<3) return 0;
    // Rotation preserves area. Triangulate relative to the first vertex to
    // avoid subtracting large coordinate products for far-away cross arms.
    double area=0;
    for (int i=1;i<count-1;++i) {
        const double ax=clipped[i].x-clipped[0].x,ay=clipped[i].y-clipped[0].y;
        const double bx=clipped[i+1].x-clipped[0].x,by=clipped[i+1].y-clipped[0].y;
        area+=ax*by-ay*bx;
    }
    return Unit(std::abs(area)*.5);
}
double PolygonPixelArea(const Point* points,int count,double x,double y) {
    Point polygon[12];
    for(int i=0;i<count;++i)polygon[i]={points[i].x-x,points[i].y-y};
    count=Clip(polygon,count,0,0,true);count=Clip(polygon,count,0,1,false);
    count=Clip(polygon,count,1,0,true);count=Clip(polygon,count,1,1,false);
    if(count<3)return 0;
    double area=0;
    for(int i=1;i<count-1;++i)
        area+=(polygon[i].x-polygon[0].x)*(polygon[i+1].y-polygon[0].y)-
              (polygon[i].y-polygon[0].y)*(polygon[i+1].x-polygon[0].x);
    return Unit(std::abs(area)*.5);
}
double CircleIntegral(double x,double radius) {
    x=Limit(x,0,radius);
    return .5*(x*std::sqrt(std::max(0.0,radius*radius-x*x))+
               radius*radius*std::asin(x/radius));
}
double CircleQuadrant(double x,double y,double radius) {
    x=std::min(std::abs(x),radius);y=std::min(std::abs(y),radius);
    if(x<=0||y<=0)return 0;
    if(x*x+y*y<=radius*radius)return x*y;
    const double boundary=std::sqrt(std::max(0.0,radius*radius-y*y));
    if(x<=boundary)return x*y;
    return boundary*y+CircleIntegral(x,radius)-CircleIntegral(boundary,radius);
}
double CircleArea(double x,double y,double cx,double cy,double radius) {
    if(radius<=0)return 0;
    const double ax=std::abs(x+.5-cx),ay=std::abs(y+.5-cy);
    if(std::hypot(std::max(0.0,ax-.5),std::max(0.0,ay-.5))>=radius)return 0;
    if(std::hypot(ax+.5,ay+.5)<=radius)return 1;
    auto integral=[&](double a,double b) {
        const double sign=(a<0?-1.0:1.0)*(b<0?-1.0:1.0);
        return sign*CircleQuadrant(a,b,radius);
    };
    return Unit(integral(x+1-cx,y+1-cy)-integral(x-cx,y+1-cy)-
                integral(x+1-cx,y-cy)+integral(x-cx,y-cy));
}
bool ValidArt(const PixelArt& art) {
    return art.width>0&&art.height>0&&art.width<=128&&art.height<=128&&
           art.pixels.size()>=static_cast<size_t>(art.width)*art.height;
}
double ArtRadius(const PixelArt& art) {
    return ValidArt(art)?std::hypot(art.width*.5,art.height*.5):0;
}
double TargetScale(double requested,double originalSide) {
    requested=Limit(requested,0,200);
    if(requested<=0||originalSide<=0)return 1;
    return std::max(1.0,requested)/originalSide;
}
double CrossScale(const Settings& s) {
    const double side=s.customCrosshair&&ValidArt(s.crossArt)?
        static_cast<double>(std::max(s.crossArt.width,s.crossArt.height)):
        2*std::max(Limit(s.length,0,1024)+Limit(s.gap,0,1024),
                   Limit(s.thickness,0,128)*.5);
    return TargetScale(s.crossSize,side);
}
double DotArtScale(const Settings& s) {
    return TargetScale(s.dotArtSize,ValidArt(s.dotArt)?
        static_cast<double>(std::max(s.dotArt.width,s.dotArt.height)):0);
}
void RasterArt(const PixelArt& art,double scale,double angle,int w,int h,
               double cx,double cy,std::vector<double>& coverage) {
    coverage.assign(static_cast<size_t>(w)*h,0);
    if(!ValidArt(art)||scale<=0)return;
    const double radians=angle*3.14159265358979323846/180;
    const double co=std::cos(radians),si=std::sin(radians);
    const double extent=(std::abs(co)+std::abs(si))*.5;
    const bool aligned=std::abs(std::sin(2*radians))<1e-10;
    for(int sy=0;sy<art.height;++sy)for(int sx=0;sx<art.width;++sx) {
        const double strength=art.pixels[static_cast<size_t>(sy)*art.width+sx]/255.0;
        if(strength<=0)continue;
        const Rect cell{(sx-art.width*.5)*scale,(sy-art.height*.5)*scale,
                        (sx+1-art.width*.5)*scale,(sy+1-art.height*.5)*scale};
        const Point corners[4]={{cell.l*co-cell.t*si,cell.l*si+cell.t*co},
                                {cell.r*co-cell.t*si,cell.r*si+cell.t*co},
                                {cell.r*co-cell.b*si,cell.r*si+cell.b*co},
                                {cell.l*co-cell.b*si,cell.l*si+cell.b*co}};
        Rect bounds{corners[0].x,corners[0].y,corners[0].x,corners[0].y};
        for(const auto& corner:corners) {
            bounds.l=std::min(bounds.l,corner.x);bounds.r=std::max(bounds.r,corner.x);
            bounds.t=std::min(bounds.t,corner.y);bounds.b=std::max(bounds.b,corner.y);
        }
        const int left=static_cast<int>(std::max(0.0,std::floor(cx+bounds.l)));
        const int right=static_cast<int>(std::min(static_cast<double>(w),std::ceil(cx+bounds.r)));
        const int top=static_cast<int>(std::max(0.0,std::floor(cy+bounds.t)));
        const int bottom=static_cast<int>(std::min(static_cast<double>(h),std::ceil(cy+bounds.b)));
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
            double area=0;
            if(aligned)area=Area(bounds,x-cx,y-cy);
            else {
                const double dx=x+.5-cx,dy=y+.5-cy;
                const double localX=dx*co+dy*si,localY=-dx*si+dy*co;
                const double u=co*.5,v=si*.5;
                const Point pixel[4]={{localX-u-v,localY+v-u},
                                      {localX+u-v,localY-v-u},
                                      {localX+u+v,localY-v+u},
                                      {localX-u+v,localY+v+u}};
                area=RotatedArea(cell,pixel,localX,localY,extent);
            }
            // Source cells tile disjoint regions. Summing covered area avoids
            // seams between adjacent cells while preserving transparent holes.
            coverage[static_cast<size_t>(y)*w+x]+=area*strength;
        }
    }
}
int Byte(double v) {
    // Clipping roundoff must not send symmetric exact half-byte ties to
    // opposite sides of the final 8-bit alpha/color quantization boundary.
    return static_cast<int>(std::floor(Limit(v,0,255)+.5+1e-8));
}
Color CleanColor(Color c) {
    c.r=std::max(0,std::min(255,c.r));
    c.g=std::max(0,std::min(255,c.g));
    c.b=std::max(0,std::min(255,c.b));
    return c;
}
}

double Ease(double t, int kind, double x1, double y1, double x2, double y2) {
    t=Unit(t);
    if (kind==0 || t==0 || t==1) return t;
    if (const EasingPreset* preset=ResolvePreset(kind)) {
        x1=preset->x1;y1=preset->y1;x2=preset->x2;y2=preset->y2;
    } else if (kind!=4) return t;
    x1=Unit(x1); y1=Unit(y1); x2=Unit(x2); y2=Unit(y2);
    // Invert the x component, rather than treating its parameter as time.
    // Binary search remains stable when a control point has a flat tangent.
    double lo=0, hi=1;
    for (int i=0;i<32;++i) {
        const double mid=(lo+hi)*.5;
        if (Bezier(mid,x1,x2)<t) lo=mid; else hi=mid;
    }
    return Unit(Bezier((lo+hi)*.5,y1,y2));
}

Frame MakeFrame(const Settings& s, double progress) {
    const double p=Unit(progress);
    Frame f;
    f.dotScale=1;
    f.dotAlpha=s.dot?1:0;
    if (s.action==1) f.alpha=Unit(1-p);
    else if (s.action==2) {
        f.arms=std::max(0.0,1-p);
        f.gap=f.arms;
        f.dotAlpha=f.dotAlpha+(1-f.dotAlpha)*Unit(p);
    }
    return f;
}

double CrosshairRadius(const Settings& s) {
    const double cross=(s.customCrosshair?ArtRadius(s.crossArt):
        std::hypot(Limit(s.length,0,1024)+Limit(s.gap,0,1024),
                   Limit(s.thickness,0,128)*.5))*CrossScale(s);
    const double dotSize=Limit(s.dotSize,1,256,1);
    double dot=dotSize*std::sqrt(.5);
    if(s.dotShape==1)dot=dotSize*.5;
    else if(s.dotShape==2)dot=std::hypot(dotSize*.5,dotSize*1.5);
    else if(s.dotShape==3)dot=ArtRadius(s.dotArt)*DotArtScale(s);
    return std::max(cross,dot);
}

void Rasterize(const Settings& s, const Frame& f, int w, int h,
               double cx, double cy, std::vector<uint32_t>& pixels) {
    if (w<=0 || h<=0 || w>32768 || h>32768 ||
        static_cast<uint64_t>(w)*h>64ull*1024*1024) {
        pixels.clear(); return;
    }
    pixels.assign(static_cast<size_t>(w)*h,0);
    if (!std::isfinite(cx)||!std::isfinite(cy)) return;
    const double global=Unit(s.opacity)*Unit(f.alpha);
    if (global<=0) return;
    const double sizeScale=CrossScale(s);
    const double armScale=Unit(f.arms)*sizeScale;
    const double length=Limit(s.length,0,1024)*armScale;
    const double gap=Limit(s.gap,0,1024)*Unit(f.gap)*sizeScale;
    const double halfT=Limit(s.thickness,0,128)*.5*sizeScale;
    // Animation changes point opacity only. Its scale and aim anchor are
    // determined exclusively by the user's size/position settings.
    const double dotScale=DotArtScale(s);
    const double dotSize=Limit(s.dotSize,1,256,1);
    const double dotStrength=Unit(s.dotOpacity)*Unit(f.dotAlpha);
    const double outer=gap+length;
    const double crossRadius=s.customCrosshair?ArtRadius(s.crossArt)*armScale:
                             std::hypot(outer,halfT);
    double dotRadius=dotSize*std::sqrt(.5);
    if(s.dotShape==1)dotRadius=dotSize*.5;
    else if(s.dotShape==2)dotRadius=std::hypot(dotSize*.5,dotSize*1.5);
    else if(s.dotShape==3)dotRadius=ArtRadius(s.dotArt)*dotScale;
    const double radius=std::max(crossRadius,dotRadius)+1;
    // Reject far-away centers before converting bounds to int.
    if (cx+radius<0 || cy+radius<0 || cx-radius>w || cy-radius>h) return;
    const int left=static_cast<int>(std::max(0.0,std::floor(cx-radius)));
    const int top=static_cast<int>(std::max(0.0,std::floor(cy-radius)));
    const int right=static_cast<int>(std::min(static_cast<double>(w),std::ceil(cx+radius)));
    const int bottom=static_cast<int>(std::min(static_cast<double>(h),std::ceil(cy+radius)));
    const double radians=Limit(s.angle,-360000,360000)*3.14159265358979323846/180;
    const double co=std::cos(radians), si=std::sin(radians);
    const double pixelExtent=(std::abs(co)+std::abs(si))*.5;
    const bool axisAligned=std::abs(std::sin(2*radians))<1e-10;
    const Rect arms[4]={{gap,-halfT,outer,halfT},
                        {-outer,-halfT,-gap,halfT},
                        {-halfT,gap,halfT,outer},
                        {-halfT,-outer,halfT,-gap}};
    Rect overlaps[4];
    for (int i=0;i<2;++i) for (int j=2;j<4;++j)
        overlaps[i*2+j-2]=Intersection(arms[i],arms[j]);
    const Rect dot={cx-dotSize*.5,cy-dotSize*.5,cx+dotSize*.5,cy+dotSize*.5};
    const Rect triangleBounds{cx-dotSize*.5,cy,
                              cx+dotSize*.5,cy+dotSize*1.5};
    const Point triangle[3]={{cx,cy},{triangleBounds.r,triangleBounds.b},
                             {triangleBounds.l,triangleBounds.b}};
    std::vector<double> crossCoverage,dotCoverage;
    if(s.customCrosshair)
        RasterArt(s.crossArt,armScale,Limit(s.angle,-360000,360000),w,h,cx,cy,crossCoverage);
    if(s.dotShape==3&&dotStrength>0)
        RasterArt(s.dotArt,dotScale,0,w,h,cx,cy,dotCoverage);
    const Color color=CleanColor(s.color), dotColor=CleanColor(s.dotColor);
    for (int y=top;y<bottom;++y) for (int x=left;x<right;++x) {
        double armCoverage=0;
        const size_t index=static_cast<size_t>(y)*w+x;
        if(s.customCrosshair)armCoverage=Unit(crossCoverage[index]);
        else if (length>0 && halfT>0) {
            if (axisAligned) {
                // The fourfold cross is unchanged by 90-degree rotations.
                // Analytic rectangle union gives exact pixel-area coverage.
                for (const auto& arm:arms) armCoverage+=Area(arm,x-cx,y-cy);
                for (const auto& overlap:overlaps)
                    armCoverage-=Area(overlap,x-cx,y-cy);
                armCoverage=Unit(armCoverage);
            } else {
                // Bound the transformed pixel before clipping. Most pixels in
                // the bounding square are empty (especially with a large gap),
                // and pixels wholly within an arm need no clipping.
                const double dx=x+.5-cx, dy=y+.5-cy;
                const double localX=dx*co+dy*si,localY=-dx*si+dy*co;
                const double ax=std::abs(localX),ay=std::abs(localY);
                const bool hitH=ax+pixelExtent>=gap && ax-pixelExtent<=outer &&
                                ay-pixelExtent<=halfT;
                const bool hitV=ay+pixelExtent>=gap && ay-pixelExtent<=outer &&
                                ax-pixelExtent<=halfT;
                const bool fullH=ax-pixelExtent>=gap && ax+pixelExtent<=outer &&
                                 ay+pixelExtent<=halfT;
                const bool fullV=ay-pixelExtent>=gap && ay+pixelExtent<=outer &&
                                 ax+pixelExtent<=halfT;
                if (fullH || fullV) armCoverage=1;
                else if (hitH || hitV) {
                    // Exact polygon/pixel overlap has no sub-sample grid: a
                    // 1/16px translation remains coherent at every angle.
                    const double u=co*.5,v=si*.5;
                    const Point pixel[4]={{localX-u-v,localY+v-u},
                                          {localX+u-v,localY-v-u},
                                          {localX+u+v,localY-v+u},
                                          {localX-u+v,localY+v+u}};
                    for (const auto& arm:arms)
                        armCoverage+=RotatedArea(arm,pixel,localX,localY,pixelExtent);
                    for (const auto& overlap:overlaps)
                        armCoverage-=RotatedArea(overlap,pixel,localX,localY,pixelExtent);
                    armCoverage=Unit(armCoverage);
                }
            }
        }
        double pointCoverage=0;
        if(dotStrength>0) {
            if(s.dotShape==1)pointCoverage=CircleArea(x,y,cx,cy,dotSize*.5);
            else if(s.dotShape==2) {
                if(Area(triangleBounds,x,y)>0)
                    pointCoverage=PolygonPixelArea(triangle,3,x,y);
            } else if(s.dotShape==3)pointCoverage=Unit(dotCoverage[index]);
            else pointCoverage=Area(dot,x,y);
        }
        const double da=pointCoverage*dotStrength;
        const double ca=armCoverage*(1-da);
        const int a=Byte((da+ca)*global*255);
        if (!a) continue;
        const int r=Byte((dotColor.r*da+color.r*ca)*global);
        const int g=Byte((dotColor.g*da+color.g*ca)*global);
        const int b=Byte((dotColor.b*da+color.b*ca)*global);
        pixels[index]=
            (static_cast<uint32_t>(a)<<24)|(static_cast<uint32_t>(r)<<16)|
            (static_cast<uint32_t>(g)<<8)|static_cast<uint32_t>(b);
    }
}

void Animation::SetTarget(bool active, double nowMs, const Settings& s) {
    const double target=active?1:0;
    if (target==to_) return;
    nowMs=Limit(nowMs,-1e15,1e15);
    from_=Value(nowMs,s);
    to_=target;
    start_=nowMs;
    duration_=Limit(active?s.enterMs:s.leaveMs,0,10000)*std::abs(to_-from_);
    easing_=s.easing;
    x1_=s.x1; y1_=s.y1; x2_=s.x2; y2_=s.y2;
    if (duration_<=0) from_=to_;
}

double Animation::Value(double nowMs, const Settings&) const {
    if (duration_<=0) return to_;
    const double t=Unit((nowMs-start_)/duration_);
    return Unit(from_+(to_-from_)*Ease(t,easing_,x1_,y1_,x2_,y2_));
}

bool Animation::Running(double nowMs) const {
    return duration_>0 && nowMs<start_+duration_;
}

void Animation::Reset() {
    from_=to_=start_=duration_=0;
    easing_=0; x1_=.25; y1_=.1; x2_=.25; y2_=1;
}
}
