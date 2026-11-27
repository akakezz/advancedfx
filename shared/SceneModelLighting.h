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
