#pragma once
#include "NativeParticleSnapshot.h"

namespace advancedfx { namespace NativeParticles {
// Factory-independent CPU geometry for the ordinary, no-sheet billboard
// subset. All inputs/output are owned values, never native collection,
// renderer, material or mesh pointers. Material selection and mesh submission
// are separate responsibilities. Not a pixel-equivalent native renderer.
namespace Billboard {
struct Camera {
    // Row-major world-to-view affine matrix (native GetMatrix(0) convention).
    std::array<float,16> View{};
    Point Origin{},Forward{};
};
struct Particle {
    Point Position{};
    float Radius=0,Rotation=0;
    std::array<std::uint8_t,4> Color{}; // RGBA
    std::uint32_t Index=0;
};
struct Vertex {
    Point Position{}; // view space; caller must use identity model/view
    std::array<float,2> UV{};
    std::array<std::uint8_t,4> Color{};
};
struct Mesh {
    std::vector<Vertex> Vertices;
    std::vector<std::uint16_t> Indices;
    std::size_t Visible=0,Culled=0;
    void Clear(){Vertices.clear();Indices.clear();Visible=Culled=0;}
};
inline bool Finite(const Point &p){return std::all_of(p.begin(),p.end(),[](float v){return std::isfinite(v);});}
inline double Dot(const Point &a,const Point &b){return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2];}
inline bool CameraValid(const Camera &c){
    if(!Finite(c.Origin)||!Finite(c.Forward)||
       !std::all_of(c.View.begin(),c.View.end(),[](float v){return std::isfinite(v);}))return false;
    if(std::abs(c.View[12])>1e-4f||std::abs(c.View[13])>1e-4f||
       std::abs(c.View[14])>1e-4f||std::abs(c.View[15]-1)>1e-4f||
       std::abs(Dot(c.Forward,c.Forward)-1)>1e-3)return false;
    const Point r[3]={{c.View[0],c.View[1],c.View[2]},{c.View[4],c.View[5],c.View[6]},{c.View[8],c.View[9],c.View[10]}};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        if(std::abs(Dot(r[i],r[j])-(i==j?1.:0.))>1e-3)return false;
    for(unsigned i=0;i<3;++i)
        if(std::abs(Dot(r[i],c.Origin)+c.View[i*4+3])>0.1)return false;
    // Source view space looks down negative Z. Require the independently
    // supplied forward to agree with the transform.
    return Dot(r[2],c.Forward)<-0.999;
}
inline bool BlockValid(const Frame &f,const Collection &c,unsigned a){
    if(!(c.Mask&(1u<<a)))return false;
    const auto &b=c.Attributes[a];
    const auto width=4u*Components(a);
    const std::size_t words=(b.Stride?(std::size_t(c.Active)+3)/4:1)*width;
    return (b.Stride==0||b.Stride==width)&&b.Count==words&&
        b.Start<=f.Words.size()&&words<=f.Words.size()-b.Start;
}
inline std::uint8_t ColorByte(float v){
    return static_cast<std::uint8_t>(std::floor(std::clamp(v,0.f,1.f)*255.f+0.5f));
}
// Decode once per immutable sample/collection if desired. Reject the entire
// collection on missing or malformed data; do not invent radius or alpha.
inline bool Prepare(const Frame &f,std::size_t index,std::vector<Particle> &out){
    out.clear();
    if(!f.Complete||!f.Appearance||index>=f.Collections.size())return false;
    const auto &c=f.Collections[index];
    if(c.Active>Layout::MaxDetachedActive)return false;
    if(!c.Active)return true;
    for(unsigned a:{unsigned(Position),unsigned(Radius),unsigned(Rotation),unsigned(Tint),unsigned(Alpha),unsigned(Alpha2)})
        if(!BlockValid(f,c,a))return false;
    out.reserve(c.Active);
    for(unsigned i=0;i<c.Active;++i){
        Particle p;p.Index=i;float rgb[3]{},alpha=0,alpha2=0;
        bool ok=true;
        for(unsigned k=0;k<3;++k)
            ok=ok&&Float(f,c,Position,i,k,p.Position[k])&&Float(f,c,Tint,i,k,rgb[k]);
        ok=ok&&Float(f,c,Radius,i,0,p.Radius)&&Float(f,c,Rotation,i,0,p.Rotation)&&
            Float(f,c,Alpha,i,0,alpha)&&Float(f,c,Alpha2,i,0,alpha2);
        const float combined=alpha*alpha2;
        if(!ok||!std::isfinite(combined)||p.Radius<0||alpha<0||alpha2<0||
           std::any_of(rgb,rgb+3,[](float v){return v<0||v>1;})){
            out.clear();return false;
        }
        p.Color={ColorByte(rgb[0]),ColorByte(rgb[1]),ColorByte(rgb[2]),ColorByte(combined)};
        out.push_back(p);
    }
    return true;
}
// Native ordinary CPU geometry has the same four-corner structure. This
// independent implementation uses standard sin/cos and explicit rounding,
// center-based culling and a deterministic distance sort. Native visibility,
// approximate trig and overdraw policy may differ. No sprite sheet, normal,
// trail, rope, shader orientation or z-offset support.
inline bool Build(const std::vector<Particle> &particles,const Camera &camera,bool sort,Mesh &out){
    out.Clear();
    if(particles.size()>Layout::MaxDetachedActive||!CameraValid(camera))return false;
    struct Item{const Particle *P;double Distance;};
    std::vector<Item> visible;visible.reserve(particles.size());
    for(const auto &p:particles){
        if(!Finite(p.Position)||!std::isfinite(p.Radius)||!std::isfinite(p.Rotation)||p.Radius<0){out.Clear();return false;}
        const Point delta={p.Position[0]-camera.Origin[0],p.Position[1]-camera.Origin[1],p.Position[2]-camera.Origin[2]};
        if(!Finite(delta)){out.Clear();return false;}
        if(!p.Color[3]||p.Radius==0||Dot(delta,camera.Forward)<0){++out.Culled;continue;}
        visible.push_back({&p,Dot(delta,delta)});
    }
    if(sort)std::stable_sort(visible.begin(),visible.end(),[](const Item &a,const Item &b){return a.Distance>b.Distance;});
    out.Vertices.reserve(visible.size()*4);out.Indices.reserve(visible.size()*6);
    constexpr std::array<std::array<float,2>,4> uv{{{0,1},{0,0},{1,0},{1,1}}};
    for(const auto &item:visible){
        const auto &p=*item.P;
        Point center{};
        for(unsigned k=0;k<3;++k)
            center[k]=static_cast<float>(double(camera.View[4*k])*p.Position[0]+double(camera.View[4*k+1])*p.Position[1]+
                double(camera.View[4*k+2])*p.Position[2]+camera.View[4*k+3]);
        const float s=std::sin(p.Rotation),c=std::cos(p.Rotation);
        const float a=(s-c)*p.Radius,b=(s+c)*p.Radius;
        const std::array<Point,4> corners{{{center[0]+a,center[1]-b,center[2]},
            {center[0]-b,center[1]-a,center[2]},{center[0]-a,center[1]+b,center[2]},
            {center[0]+b,center[1]+a,center[2]}}};
        for(const auto &v:corners)if(!Finite(v)){out.Clear();return false;}
        const auto base=static_cast<std::uint16_t>(out.Vertices.size());
        for(unsigned i=0;i<4;++i)out.Vertices.push_back({corners[i],uv[i],p.Color});
        for(unsigned i:{0u,1u,2u,0u,2u,3u})out.Indices.push_back(static_cast<std::uint16_t>(base+i));
        ++out.Visible;
    }
    return true;
}
} // Billboard
}} // advancedfx::NativeParticles
