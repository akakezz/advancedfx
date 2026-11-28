#pragma once
#include "NativeParticleBillboard.h"

namespace advancedfx { namespace NativeParticles { namespace Billboard {
// Exact TF2 x64 shaderapidx9 CMeshMgr::ComputeVertexDescription subset.
// Position float3, BGRA byte4, texture coordinate float2. Optional native
// padding to 32 bytes; never guess extra streams or compressed attributes.
inline unsigned PackedStride(std::uint64_t format){
    return format==0x40005u?32u:format==0x40205u?24u:0u;
}
struct PackedMesh {
    unsigned Stride=0;
    std::vector<unsigned char> Vertices;
    std::vector<std::uint16_t> Indices;
    void Clear(){Stride=0;Vertices.clear();Indices.clear();}
};
inline bool PackWorld(const Mesh &mesh,const Camera &camera,std::uint64_t format,PackedMesh &out){
    out.Clear();
    const auto stride=PackedStride(format);
    if(!stride||!CameraValid(camera)||mesh.Visible>Layout::MaxDetachedActive||
       mesh.Vertices.size()!=mesh.Visible*4||mesh.Indices.size()!=mesh.Visible*6)return false;
    // World-space vertices avoid pushing the native view matrix or enabling
    // user clip overrides. The submission layer must require identity model.
    for(std::size_t i=0;i<mesh.Indices.size();++i){
        constexpr unsigned order[]={0,1,2,0,2,3};
        if(mesh.Indices[i]!=(i/6)*4+order[i%6])return false;
    }
    out.Vertices.resize(mesh.Vertices.size()*stride,0);
    for(std::size_t i=0;i<mesh.Vertices.size();++i){
        const auto &v=mesh.Vertices[i];
        if(!Finite(v.Position)||!std::all_of(v.UV.begin(),v.UV.end(),[](float f){return std::isfinite(f)&&f>=0&&f<=1;})){
            out.Clear();return false;
        }
        Point p{};
        for(unsigned k=0;k<3;++k){
            const double value=double(camera.Origin[k])+double(camera.View[k])*v.Position[0]+
                double(camera.View[4+k])*v.Position[1]+double(camera.View[8+k])*v.Position[2];
            if(!std::isfinite(value)||std::abs(value)>1e7){out.Clear();return false;}
            p[k]=static_cast<float>(value);
        }
        auto *dst=out.Vertices.data()+i*stride;
        std::memcpy(dst,p.data(),12);
        dst[12]=v.Color[2];dst[13]=v.Color[1];dst[14]=v.Color[0];dst[15]=v.Color[3];
        std::memcpy(dst+16,v.UV.data(),8);
    }
    out.Indices=mesh.Indices;out.Stride=stride;
    return true;
}
// Native LockMesh writes through +0x100 inclusive. Keep the descriptor
// opaque/aligned; decode fields explicitly, not by casting to a guessed SDK
// struct. Queue-owned base pointers are obtained independently from mesh.
using LockDescriptor=std::array<unsigned char,0x110>;
template<class T> inline T DescriptorValue(const LockDescriptor &d,std::size_t off){
    T v{};if(off<=d.size()&&sizeof(v)<=d.size()-off)std::memcpy(&v,d.data()+off,sizeof(v));return v;
}
inline bool ValidLockDescriptor(const LockDescriptor &d,Address vertexBase,Address indexBase,
        std::size_t vertexCount,std::size_t indexCount,unsigned stride){
    if((stride!=24&&stride!=32)||!vertexCount||!indexCount||
       vertexCount>Layout::MaxDetachedActive*4||indexCount>Layout::MaxDetachedActive*6||
       vertexBase<0x10000||indexBase<0x10000||(vertexBase&3)||(indexBase&1))return false;
    const auto max=(std::numeric_limits<Address>::max)();
    const auto vb=vertexCount*stride,ib=indexCount*2;
    if(vertexBase>max-vb||indexBase>max-ib||
       !(vertexBase+vb<=indexBase||indexBase+ib<=vertexBase))return false;
    // All declared streams must be exactly the three supported attributes.
    for(unsigned off=0;off<0x48;off+=4){
        const unsigned expected=off==0||off==0x10||off==0x18?stride:0;
        if(DescriptorValue<unsigned>(d,off)!=expected)return false;
    }
    return DescriptorValue<unsigned>(d,0x48)==stride&&
        DescriptorValue<unsigned>(d,0x4c)==0&&DescriptorValue<unsigned>(d,0x50)==0&&
        DescriptorValue<Address>(d,0x58)==vertexBase&&
        DescriptorValue<Address>(d,0x78)==vertexBase+12&&
        DescriptorValue<Address>(d,0x88)==vertexBase+16&&
        DescriptorValue<Address>(d,0xe8)==0&&DescriptorValue<Address>(d,0xf0)==indexBase&&
        DescriptorValue<Address>(d,0xf8)==0&&DescriptorValue<unsigned char>(d,0x100)==1;
}
}}} // advancedfx::NativeParticles::Billboard
