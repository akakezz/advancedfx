#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <set>
#include <map>
#include <string>
#include <iterator>
#include <vector>
#include <utility>
#include <algorithm>

namespace advancedfx { namespace LegacyParticles {
using Address=std::uint64_t;
enum class Material : unsigned char { Unknown, Spark, CombineMuzzle, Named };
struct Particle {
    std::array<float,3> Position{},Velocity{};
    float Roll=0,DieTime=0,Lifetime=0,RollDelta=0,NearClip=16,FarClip=64;
    std::array<unsigned char,3> Color{};
    unsigned char StartAlpha=0,EndAlpha=0,StartSize=0,EndSize=0,Flags=0;
    Material Texture=Material::Unknown;
    std::uint16_t MaterialIndex=0xffff;
    std::array<float,4> UV{};
};
struct Frame {
    int Tick=-1;
    bool Complete=false;
    std::size_t Emitters=0,Expected=0,UnknownMaterials=0,Rejected=0;
    std::vector<Particle> Particles;
    std::vector<std::string> Materials;
    std::size_t OtherEmitters=0,OtherRejected=0;
    std::size_t Bytes() const {std::size_t n=sizeof(Frame)+Particles.capacity()*sizeof(Particle)+Materials.capacity()*sizeof(std::string);for(const auto &s:Materials)n+=s.capacity();return n;}
};
// Capture only the exact named CSimpleEmitter dissolve type, with the binding
// layout checked by the caller. No addresses are returned or kept in history.
template<class Reader>
bool CaptureBinding(Address binding,unsigned expected,Address spark,Address glow,Reader read,Frame &frame,const std::map<Address,std::string> *names=nullptr) {
    auto get=[&](Address a,auto &v){return a>=0x10000&&read(a,&v,sizeof(v));};
    if(expected>2048||frame.Particles.size()+expected>2048)return false;
    float nearClip=16,farClip=64;
    if(!get(binding+0x140,nearClip)||!get(binding+0x144,farClip)||!std::isfinite(nearClip)||!std::isfinite(farClip)||nearClip<0||farClip<=nearClip)return false;
    std::vector<Particle> local;local.reserve(expected);
    std::set<Address> materials,particles;
    std::size_t unknown=0;
    auto materialNames=frame.Materials;
    for(unsigned bucket=0;bucket<8;++bucket) {
        Address group=0;
        if(!get(binding+0xb0+8*bucket,group))return false;
        while(group) {
            Address materialGroup=0,prev=0,next=0,hashNext=0;
            if(materials.size()>=2048||!materials.insert(group).second||
               !get(group,materialGroup)||!materialGroup||
               !get(group+8,prev)||!get(group+16,next)||!get(group+0x30,hashNext))return false;
            const Address sentinel=group+8,first=next,last=prev;
            Address prior=sentinel;
            while(next!=sentinel) {
                // Copy contiguous particle and subtexture records in two
                // bounded reads, rather than one process-memory read per field.
                std::array<unsigned char,0x4c> raw{};
                std::array<unsigned char,0x18> tex{};
                auto copy=[](const auto &bytes,std::size_t off,auto &v){std::memcpy(&v,bytes.data()+off,sizeof(v));};
                Address back=0,forward=0,subtexture=0,subGroup=0;
                if(local.size()>=expected||!particles.insert(next).second||!get(next,raw))return false;
                copy(raw,0,back);copy(raw,8,forward);copy(raw,0x10,subtexture);
                if(back!=prior||!get(subtexture,tex))return false;
                copy(tex,0x10,subGroup);if(subGroup!=materialGroup)return false;
                Particle p;p.NearClip=nearClip;p.FarClip=farClip;
                copy(raw,0x18,p.Position);copy(raw,0x28,p.Velocity);
                copy(raw,0x34,p.Roll);copy(raw,0x38,p.DieTime);copy(raw,0x3c,p.Lifetime);
                copy(raw,0x40,p.Color);copy(raw,0x43,p.StartAlpha);copy(raw,0x44,p.EndAlpha);
                copy(raw,0x45,p.StartSize);copy(raw,0x46,p.EndSize);copy(raw,0x47,p.Flags);
                copy(raw,0x48,p.RollDelta);copy(tex,0,p.UV);
                for(float v:p.Position)if(!std::isfinite(v))return false;
                for(float v:p.Velocity)if(!std::isfinite(v))return false;
                for(float v:p.UV)if(!std::isfinite(v))return false;
                if(!std::isfinite(p.Roll)||!std::isfinite(p.RollDelta)||!std::isfinite(p.DieTime)||
                   !std::isfinite(p.Lifetime)||p.DieTime<=0||p.Lifetime<0)return false;
                if(spark&&subtexture==spark)p.Texture=Material::Spark;
                else if(glow&&subtexture==glow)p.Texture=Material::CombineMuzzle;
                else if(names) {
                    auto found=names->find(subtexture);
                    if(found!=names->end()) {
                        auto it=std::find(materialNames.begin(),materialNames.end(),found->second);
                        if(it==materialNames.end()&&materialNames.size()<1024){materialNames.push_back(found->second);it=std::prev(materialNames.end());}
                        if(it!=materialNames.end()){p.Texture=Material::Named;p.MaterialIndex=static_cast<std::uint16_t>(it-materialNames.begin());}
                    }
                    if(p.Texture==Material::Unknown)++unknown;
                } else ++unknown;
                local.push_back(p);prior=next;next=forward;
            }
            Address checkFirst=0,checkLast=0,checkHash=0;
            if(prior!=last||!get(group+8,checkLast)||checkLast!=last||
               !get(group+16,checkFirst)||checkFirst!=first||
               !get(group+0x30,checkHash)||checkHash!=hashNext)return false;
            group=hashNext;
        }
    }
    std::uint16_t count=0;
    if(local.size()!=expected||!get(binding+0x90,count)||count!=expected)return false;
    frame.Materials=std::move(materialNames);
    frame.UnknownMaterials+=unknown;
    frame.Particles.insert(frame.Particles.end(),local.begin(),local.end());
    return true;
}
class History {
public:
    explicit History(std::size_t budget=64u*1024u*1024u):Budget(budget){}
    std::deque<Frame> Frames;
    std::size_t Bytes=0,Evicted=0,Rejected=0;
    std::uint64_t Revision=0;
    void Clear(){Frames.clear();Bytes=Evicted=Rejected=0;++Revision;}
    bool Append(Frame frame) {
        ++Revision;
        if(frame.Tick<0){++Rejected;return false;}
        if(!Frames.empty()&&frame.Tick<Frames.back().Tick)Clear();
        if(!Frames.empty()&&frame.Tick==Frames.back().Tick){Bytes-=Frames.back().Bytes();Frames.pop_back();}
        if(!frame.Complete||frame.Bytes()>Budget){++Rejected;return false;}
        while(!Frames.empty()&&(Bytes>Budget-frame.Bytes()||Frames.size()>=32768)){
            Bytes-=Frames.front().Bytes();Frames.pop_front();++Evicted;
        }
        Bytes+=frame.Bytes();Frames.push_back(std::move(frame));return true;
    }
    const Frame *At(int tick) const {
        auto it=std::lower_bound(Frames.begin(),Frames.end(),tick,[](const Frame &f,int t){return f.Tick<t;});
        return it!=Frames.end()&&it->Tick==tick?&*it:nullptr;
    }
private:
    std::size_t Budget;
};
}} // namespace
