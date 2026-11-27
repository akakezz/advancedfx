#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace advancedfx { namespace SceneLighting {
// Exact Source 2013 LightDesc_t, 88 bytes. Keep enum/flags as raw words;
// do not interpret them as float values. No engine pointers.
using Descriptor=std::array<std::uint32_t,22>;
static_assert(sizeof(Descriptor)==88,"Source light descriptor ABI");
inline float FloatWord(const Descriptor &d,unsigned i){
    float f=0;std::memcpy(&f,&d[i],sizeof(f));return f;
}
inline void SetWord(Descriptor &d,unsigned i,float f){std::memcpy(&d[i],&f,sizeof(f));}
// Native world-light conversion leaves type-unused and private derived words
// uninitialized. Copy the meaningful fields, reconstruct derived values using
// the SDK's LightDesc_t rules, and never retain those uninitialized words.
inline bool Normalize(Descriptor &d){
    if(d[0]<1||d[0]>3)return false;
    for(unsigned i=1;i<=14;++i){
        if(i==11&&d[0]!=3){d[i]=0;continue;}
        if(!std::isfinite(FloatWord(d,i)))return false;
    }
    const float range=FloatWord(d,10);
    const double squared=double(range)*range;
    if(range<0||squared>(std::numeric_limits<float>::max)())return false;
    unsigned flags=8;
    for(unsigned i=12;i<=14;++i)if(FloatWord(d,i)!=0)flags|=1u<<(i-12);
    if(d[19]!=flags)return false;
    if(d[0]==3){
        for(unsigned i=15;i<=18;++i)if(!std::isfinite(FloatWord(d,i)))return false;
        const float theta=FloatWord(d,15),phi=FloatWord(d,16),inner=FloatWord(d,17),outer=FloatWord(d,18);
        if(theta<0||phi<theta||phi>6.283186f||std::abs(inner)>1||std::abs(outer)>1)return false;
        const float spread=inner-outer;
        SetWord(d,20,spread>1e-10f?1.f/spread:1.f);
    }else{
        for(unsigned i=15;i<=18;++i)d[i]=0;
        d[20]=0;
    }
    SetWord(d,21,static_cast<float>(squared));
    return true;
}
// Verified TF2 x64 CStudioRenderContext slots, not guessed from overload order.
inline constexpr int AmbientRgbSlot=16,AmbientRgbaSlot=17;
inline constexpr unsigned AmbientWordCount(int slot){return slot==AmbientRgbSlot?18u:slot==AmbientRgbaSlot?24u:0u;}
struct Sample {
    int AmbientSlot=0,Count=0; // slot 16 = six float3, slot 17 = six float4
    std::array<float,24> Ambient{};
    std::array<Descriptor,4> Lights{};
    unsigned AmbientWords()const{return AmbientWordCount(AmbientSlot);}
    bool Valid()const{
        const auto n=AmbientWords();
        if(!n||Count<0||Count>4)return false;
        for(unsigned i=0;i<n;++i)if(!std::isfinite(Ambient[i])||std::abs(Ambient[i])>1e6f)return false;
        for(int i=0;i<Count;++i){auto copy=Lights[i];if(!Normalize(copy)||copy!=Lights[i])return false;}
        return true;
    }
    bool operator==(const Sample &o)const{
        return AmbientSlot==o.AmbientSlot&&Count==o.Count&&
            !std::memcmp(Ambient.data(),o.Ambient.data(),sizeof(Ambient))&&Lights==o.Lights;
    }
    static bool Capture(int slot,const float *ambient,int count,const void *lights,Sample &out){
        Sample next;next.AmbientSlot=slot;next.Count=count;
        const auto n=next.AmbientWords();
        if(!n||!ambient||count<0||count>4||(count&&!lights))return false;
        std::memcpy(next.Ambient.data(),ambient,n*sizeof(float));
        if(count)std::memcpy(next.Lights.data(),lights,std::size_t(count)*sizeof(Descriptor));
        for(int i=0;i<count;++i)if(!Normalize(next.Lights[i]))return false;
        if(!next.Valid())return false;
        out=next;return true;
    }
};
// Read-only model decal inventory for the actually drawn LOD. Values only;
// no native handles, material pointers, geometry or replay authorization.
struct ModelDecalCoverage {
    int Lod=-1;
    std::uint32_t Batches=0,Vertices=0,Indices=0;
    bool Known()const{return Lod>=0&&Lod<32;}
    bool operator==(const ModelDecalCoverage &o)const{return Lod==o.Lod&&Batches==o.Batches&&Vertices==o.Vertices&&Indices==o.Indices;}
};
template<class Read>
bool ReadModelDecalCoverage(Read read,std::uintptr_t renderer,std::uintptr_t hardware,std::uintptr_t handle,int lod,ModelDecalCoverage &out){
    ModelDecalCoverage next;
    if(lod<0||lod>=32||!hardware||!renderer)return false;
    int hardwareLods=0;
    if(!read(hardware+4,hardwareLods)||hardwareLods<=lod||hardwareLods>32)return false;
    next.Lod=lod;
    if(!handle){out=next;return true;}
    std::uintptr_t owner=0,levels=0,pool=0;
    int count=0,capacity=0;
    std::uint16_t active=0,head=0xffff;
    if(!read(handle,owner)||owner!=hardware||!read(handle+8,levels)||!levels||
       !read(handle+0x10,count)||count!=hardwareLods||
       !read(levels+std::uintptr_t(lod)*0x30,head)||
       !read(renderer+0x10,pool)||!read(renderer+0x18,capacity)||capacity<0||capacity>65535||
       !read(renderer+0x26,active)||active>capacity)return false;
    std::array<std::uint16_t,256> visited{};
    auto index=head;
    unsigned traversed=0;
    while(index!=0xffff){
        if(!pool||index>=capacity||traversed>=visited.size()||traversed>=active)return false;
        for(unsigned i=0;i<traversed;++i)if(visited[i]==index)return false;
        visited[traversed++]=index;
        const auto record=pool+std::uintptr_t(index)*0x80;
        std::array<unsigned char,0x80> bytes{};
        if(!read(record,bytes))return false;
        std::uintptr_t material=0,indices=0,vertices=0;
        int indexCount=0,indexCapacity=0,vertexCapacity=0;
        std::uint16_t vertexCount=0;
        std::memcpy(&material,bytes.data(),8);
        std::memcpy(&indices,bytes.data()+8,8);
        std::memcpy(&indexCapacity,bytes.data()+0x10,4);
        std::memcpy(&indexCount,bytes.data()+0x18,4);
        std::memcpy(&vertices,bytes.data()+0x28,8);
        std::memcpy(&vertexCapacity,bytes.data()+0x30,4);
        std::memcpy(&vertexCount,bytes.data()+0x3e,2);
        std::memcpy(&index,bytes.data()+0x7a,2);
        if(indexCount<0||indexCount>indexCapacity||indexCount>65535||indexCount%3||
           vertexCapacity<0||vertexCount>vertexCapacity||
           (indexCount&&(!material||!indices||!vertices||!vertexCount)))return false;
        if(indexCount){++next.Batches;next.Indices+=indexCount;next.Vertices+=vertexCount;}
    }
    // Detect pool/list replacement during observation; this is not a lock.
    std::uintptr_t poolAgain=0,levelsAgain=0,ownerAgain=0;
    std::uint16_t headAgain=0xffff,activeAgain=0;
    int countAgain=0,capacityAgain=0;
    if(!read(handle,ownerAgain)||ownerAgain!=owner||!read(handle+8,levelsAgain)||levelsAgain!=levels||
       !read(handle+0x10,countAgain)||countAgain!=count||
       !read(levels+std::uintptr_t(lod)*0x30,headAgain)||headAgain!=head||
       !read(renderer+0x10,poolAgain)||poolAgain!=pool||!read(renderer+0x18,capacityAgain)||capacityAgain!=capacity||
       !read(renderer+0x26,activeAgain)||activeAgain!=active)return false;
    out=next;return true;
}
struct Key {
    std::uint32_t Checksum=0;
    int Skin=-1,Body=-1;
    std::array<float,3> Origin{};
    bool Matches(std::uint32_t checksum,int skin,int body,const float *origin)const{
        if(!origin||Checksum!=checksum||Skin!=skin||Body!=body)return false;
        for(unsigned i=0;i<3;++i)if(!std::isfinite(Origin[i])||!std::isfinite(origin[i])||std::abs(Origin[i]-origin[i])>.25f)return false;
        return true;
    }
};
}} // namespace
