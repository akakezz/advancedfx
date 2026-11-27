#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <type_traits>
#include <utility>

namespace advancedfx { namespace SceneSurfaces {
struct Decal {
    std::uint16_t Slot=0,Flags=0,NameIndex=0;
    std::uint32_t Surface=0;
    std::array<std::uint32_t,7> Placement{}; // raw position, optional axis, scale
    std::array<unsigned char,3> Color{};
    unsigned char Alpha=255;
    std::string Material;
    bool Same(const Decal &o) const {
        return Slot==o.Slot&&Surface==o.Surface&&Flags==o.Flags&&NameIndex==o.NameIndex&&Placement==o.Placement&&Color==o.Color&&Material==o.Material;
    }
    bool operator==(const Decal &o) const{return Same(o)&&Alpha==o.Alpha;}
};
struct Shadow {
    std::array<float,3> Origin{},Direction{},Caster{};
    std::array<float,16> Matrix{};
    std::array<float,2> Size{};
    float Height=0,FalloffOffset=0,FalloffAmount=0;
    unsigned char Bias=0;
    std::vector<int> Leaves;
    bool Valid() const {
        auto finite=[](const auto &v){return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x)&&std::abs(x)<1e7f;});};
        return finite(Origin)&&finite(Direction)&&finite(Caster)&&finite(Matrix)&&finite(Size)&&Size[0]>0&&Size[1]>0&&Size[0]<8192&&Size[1]<8192&&
            std::isfinite(Height)&&Height>0&&Height<32768&&std::isfinite(FalloffOffset)&&std::isfinite(FalloffAmount)&&
            !Leaves.empty()&&Leaves.size()<=8192&&std::all_of(Leaves.begin(),Leaves.end(),[](int v){return v>=0&&v<1048576;});
    }
    bool operator==(const Shadow &o) const {
        return Origin==o.Origin&&Direction==o.Direction&&Caster==o.Caster&&Matrix==o.Matrix&&Size==o.Size&&Height==o.Height&&FalloffOffset==o.FalloffOffset&&FalloffAmount==o.FalloffAmount&&Bias==o.Bias&&Leaves==o.Leaves;
    }
};
enum class DecalRead { NotRun, Guard, Table, Record, Slot, Surface, Changed, Ready };
inline const char *DecalReadLabel(DecalRead value){
    switch(value){
    case DecalRead::Guard:return "native guard mismatch";
    case DecalRead::Table:return "invalid slot table";
    case DecalRead::Record:return "unreadable record";
    case DecalRead::Slot:return "slot identity mismatch";
    case DecalRead::Surface:return "unreadable surface";
    case DecalRead::Changed:return "world or slot table changed";
    case DecalRead::Ready:return "complete slot traversal (unsupported types excluded)";
    default:return "not run";
    }
}
// Exclusive observed rejection categories, not a claim of complete decal coverage.
struct DecalCoverage {
    std::size_t OutsideWorld=0,Attached=0,Flagged=0,Displacement=0,Material=0;
    std::size_t Total()const{return OutsideWorld+Attached+Flagged+Displacement+Material;}
    std::string Describe()const{
        return "outside world "+std::to_string(OutsideWorld)+", attached "+std::to_string(Attached)+
            ", flags 0x1100 "+std::to_string(Flagged)+", displacement "+std::to_string(Displacement)+
            ", material unavailable "+std::to_string(Material);
    }
};
struct Frame {
    int Tick=-1;
    std::uint64_t World=0; // session generation, never a native pointer
    bool DecalsReady=false,ShadowsReady=false;
    DecalRead DecalStatus=DecalRead::NotRun;
    std::size_t DecalsUnsupported=0,ShadowsUnobserved=0;
    DecalCoverage UnsupportedDecals;
    std::shared_ptr<const std::vector<Decal>> Decals;
    std::shared_ptr<const std::vector<Shadow>> Shadows;
};
class History {
    std::size_t Budget;
    static std::size_t Cost(const std::vector<Decal> &v){std::size_t n=sizeof(v)+v.capacity()*sizeof(Decal);for(const auto &d:v)n+=d.Material.capacity();return n;}
    static std::size_t Cost(const std::vector<Shadow> &v){std::size_t n=sizeof(v)+v.capacity()*sizeof(Shadow);for(const auto &s:v)n+=s.Leaves.capacity()*sizeof(int);return n;}
    std::map<const void*,std::size_t> Refs;
    template<class T> std::size_t ExtraVector(const std::shared_ptr<const std::vector<T>> &p)const{
        return p&&!Refs.count(p.get())?Cost(*p):0;
    }
    template<class T> void AddVector(const std::shared_ptr<const std::vector<T>> &p){
        if(p){auto &n=Refs[p.get()];if(!n++)Bytes+=Cost(*p);}
    }
    template<class T> void DropVector(const std::shared_ptr<const std::vector<T>> &p){
        if(p){auto it=Refs.find(p.get());if(it!=Refs.end()&&!--it->second){Bytes-=Cost(*p);Refs.erase(it);}}
    }
    std::size_t Extra(const Frame &f)const{return sizeof(Frame)+ExtraVector(f.Decals)+ExtraVector(f.Shadows);}
    void Drop(bool front){
        Frame f=front?std::move(Frames.front()):std::move(Frames.back());
        if(front)Frames.pop_front();else Frames.pop_back();
        Bytes-=sizeof(Frame);DropVector(f.Decals);DropVector(f.Shadows);
    }
public:
    explicit History(std::size_t budget=64u*1024u*1024u):Budget(budget){}
    std::deque<Frame> Frames;
    std::size_t Bytes=0,Evicted=0,Rejected=0;
    void Clear(){Frames.clear();Refs.clear();Bytes=Evicted=Rejected=0;}
    bool Append(Frame f){
        if(f.Tick<0){++Rejected;return false;}
        if(!Frames.empty()&&(f.World!=Frames.back().World||f.Tick<Frames.back().Tick))Clear();
        if(!Frames.empty()&&f.Tick==Frames.back().Tick)Drop(false);
        if(!f.DecalsReady&&!f.ShadowsReady){++Rejected;return false;}
        if(!Frames.empty()){
            const auto &last=Frames.back();
            if(f.Decals&&last.Decals&&*f.Decals==*last.Decals)f.Decals=last.Decals;
            if(f.Shadows&&last.Shadows&&*f.Shadows==*last.Shadows)f.Shadows=last.Shadows;
        }
        if(sizeof(Frame)+(f.Decals?Cost(*f.Decals):0)+(f.Shadows?Cost(*f.Shadows):0)>Budget){++Rejected;return false;}
        while(!Frames.empty()&&(Bytes>Budget-Extra(f)||Frames.size()>=32768)){Drop(true);++Evicted;}
        Bytes+=sizeof(Frame);AddVector(f.Decals);AddVector(f.Shadows);Frames.push_back(std::move(f));return true;
    }
    // Same bounded, causal sample policy as modern particle history.
    const Frame *ForPreview(int tick,int maxGap=2) const {
        if(Frames.empty()||tick<Frames.front().Tick||tick>Frames.back().Tick||maxGap<0)return nullptr;
        auto it=std::upper_bound(Frames.begin(),Frames.end(),tick,[](int t,const Frame &f){return t<f.Tick;});
        if(it==Frames.begin())return nullptr;
        --it;return static_cast<std::int64_t>(tick)-it->Tick<=maxGap?&*it:nullptr;
    }
    const Frame *At(int tick)const{
        auto it=std::lower_bound(Frames.begin(),Frames.end(),tick,[](const Frame &f,int t){return f.Tick<t;});
        return it!=Frames.end()&&it->Tick==tick?&*it:nullptr;
    }
};
}} // namespace
