#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <set>
#include <vector>
#include <utility>
#include <algorithm>

namespace advancedfx { namespace LegacyParticles {
using Address=std::uint64_t;
enum class Material : unsigned char { Unknown, Spark, CombineMuzzle };
struct Particle {
    std::array<float,3> Position{},Velocity{};
    float Roll=0,DieTime=0,Lifetime=0,RollDelta=0;
    std::array<unsigned char,3> Color{};
    unsigned char StartAlpha=0,EndAlpha=0,StartSize=0,EndSize=0,Flags=0;
    Material Texture=Material::Unknown;
    std::array<float,4> UV{};
};
struct Frame {
    int Tick=-1;
    bool Complete=false;
    std::size_t Emitters=0,Expected=0,UnknownMaterials=0,Rejected=0;
    std::vector<Particle> Particles;
    std::size_t Bytes() const {return sizeof(Frame)+Particles.capacity()*sizeof(Particle);}
};
// Capture only the exact named CSimpleEmitter dissolve type, with the binding
// layout checked by the caller. No addresses are returned or kept in history.
template<class Reader>
bool CaptureBinding(Address binding,unsigned expected,Address spark,Address glow,Reader read,Frame &frame) {
    auto get=[&](Address a,auto &v){return a>=0x10000&&read(a,&v,sizeof(v));};
    if(expected>2048||frame.Particles.size()+expected>2048)return false;
    std::vector<Particle> local;local.reserve(expected);
    std::set<Address> materials,particles;
    std::size_t unknown=0;
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
                Address back=0,forward=0,subtexture=0,subGroup=0;
                if(local.size()>=expected||!particles.insert(next).second||
                   !get(next,back)||back!=prior||!get(next+8,forward)||
                   !get(next+0x10,subtexture)||!get(subtexture+0x10,subGroup)||
                   subGroup!=materialGroup)return false;
                Particle p;
                if(!get(next+0x18,p.Position)||!get(next+0x28,p.Velocity)||
                   !get(next+0x34,p.Roll)||!get(next+0x38,p.DieTime)||!get(next+0x3c,p.Lifetime)||
                   !get(next+0x40,p.Color)||!get(next+0x43,p.StartAlpha)||!get(next+0x44,p.EndAlpha)||
                   !get(next+0x45,p.StartSize)||!get(next+0x46,p.EndSize)||!get(next+0x47,p.Flags)||
                   !get(next+0x48,p.RollDelta)||!get(subtexture,p.UV))return false;
                for(float v:p.Position)if(!std::isfinite(v))return false;
                for(float v:p.Velocity)if(!std::isfinite(v))return false;
                for(float v:p.UV)if(!std::isfinite(v))return false;
                if(!std::isfinite(p.Roll)||!std::isfinite(p.RollDelta)||!std::isfinite(p.DieTime)||
                   !std::isfinite(p.Lifetime)||p.DieTime<=0||p.Lifetime<0)return false;
                if(spark&&subtexture==spark)p.Texture=Material::Spark;
                else if(glow&&subtexture==glow)p.Texture=Material::CombineMuzzle;
                else ++unknown;
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
    frame.UnknownMaterials+=unknown;
    frame.Particles.insert(frame.Particles.end(),local.begin(),local.end());
    return true;
}
class History {
public:
    explicit History(std::size_t budget=64u*1024u*1024u):Budget(budget){}
    std::deque<Frame> Frames;
    std::size_t Bytes=0,Evicted=0,Rejected=0;
    void Clear(){Frames.clear();Bytes=Evicted=Rejected=0;}
    bool Append(Frame frame) {
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
