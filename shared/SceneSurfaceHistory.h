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
#include <cstring>
#include <set>
#include <limits>

namespace advancedfx { namespace SceneSurfaces {
// Exact engine's ordinary clipped decal vertex: xyz, unused word, UV,
// lightmap UV. Preserve the seven consumed float bit patterns losslessly.
using DecalVertex=std::array<std::uint32_t,8>;
static_assert(sizeof(DecalVertex)==32,"native decal vertex stride");
inline bool ValidDecalGeometry(const std::vector<DecalVertex> &v){
    if(v.size()<3||v.size()>64)return false;
    for(const auto &vertex:v)for(unsigned i=0;i<8;++i){
        if(i==3)continue;
        float f=0;std::memcpy(&f,&vertex[i],sizeof(f));
        if(!std::isfinite(f)||std::abs(f)>1e7f)return false;
    }
    return true;
}
struct Decal {
    std::uint16_t Slot=0,Flags=0,NameIndex=0;
    std::uint32_t Surface=0;
    std::array<std::uint32_t,7> Placement{}; // raw position, optional axis, scale
    std::array<unsigned char,3> Color{};
    unsigned char Alpha=255;
    // Native 0x100 fading world decal: retain identity and visibility,
    // not a promise to reproduce nonzero historical opacity.
    float FadeDuration=0,FadeStart=0;
    bool FadeExpired=false;
    unsigned char FadeAlpha=255;
    std::uint32_t LightmapOffset=0;
    int GeometryGroup=-1;
    std::shared_ptr<const std::vector<DecalVertex>> Geometry;
    bool Fading()const{return (Flags&0x100)!=0;}
    bool SetFade(float duration,float start,float now){
        if(!std::isfinite(duration)||duration<=0||!std::isfinite(start)||!std::isfinite(now)||!std::isfinite(start+duration))return false;
        FadeDuration=duration;FadeStart=start;FadeExpired=now>=start+duration;
        const float fraction=(std::max)(0.0f,(std::min)(1.0f,1.0f-(now-start)/duration));
        FadeAlpha=static_cast<unsigned char>(float(Alpha)*fraction);return true;
    }
    unsigned char ReplayAlpha()const{return !Alpha?0:(Fading()?(FadeExpired?0:FadeAlpha):Alpha);}
    bool Visible()const{return ReplayAlpha()!=0;}
    bool HasGeometry()const{return GeometryGroup>=0&&GeometryGroup<3&&Geometry&&ValidDecalGeometry(*Geometry);}
    // For surviving decals, replace only when a historical visible mark
    // differs in evaluated alpha and we have a validated polygon.
    bool NeedsOpacityReplay(const Decal &live)const{
        return Visible()&&Same(live)&&ReplayAlpha()!=live.ReplayAlpha()&&HasGeometry();
    }
    std::string Material;
    bool Same(const Decal &o) const {
        return Slot==o.Slot&&Surface==o.Surface&&Flags==o.Flags&&NameIndex==o.NameIndex&&Placement==o.Placement&&Color==o.Color&&Material==o.Material&&FadeDuration==o.FadeDuration&&FadeStart==o.FadeStart&&LightmapOffset==o.LightmapOffset;
    }
    bool operator==(const Decal &o) const{
        return Same(o)&&Alpha==o.Alpha&&FadeExpired==o.FadeExpired&&FadeAlpha==o.FadeAlpha&&GeometryGroup==o.GeometryGroup&&
            (Geometry==o.Geometry||(Geometry&&o.Geometry&&*Geometry==*o.Geometry));
    }
};
// A planned surviving replacement is submitted only if the original
// geometry was actually gated in this dispatcher, or already invisible.
inline bool ShouldSubmitDetached(bool surviving,bool liveVisible,bool gated){
    return !surviving||!liveVisible||gated;
}
// Queued replacement is restricted to ordinary nonfading survivors;
// fading consumers must still run their native expiration bookkeeping.
// An unsupported current slot never authorizes a detached duplicate.
inline bool CanPlanDetached(bool immediate,unsigned requested,bool surviving,bool uncertainSlot,bool fading=false){
    if(uncertainSlot)return false;
    if(surviving)return (requested&8)&&(immediate||((requested&16)&&!fading));
    return (requested&4)&&(immediate||(requested&16));
}
// The exact queued dispatcher gathers categories 0, 1, 2 in order before
// it copies the filtered pointer vector into queue-owned storage. A
// replacement is authorized only after all three gathers were verified.
struct QueuedReplacementCoverage {
    unsigned Next=0;
    bool Failed=false;
    void Observe(int category,bool success){
        if(!success||category<0||category>2||static_cast<unsigned>(category)!=Next){Failed=true;return;}
        ++Next;
    }
    bool Complete()const{return !Failed&&Next==3;}
    bool CanSubmit(bool suppressed)const{return suppressed&&Complete();}
};
// Native queued gathering has two formats: a plain list of decal pointers
// or a sorted list with zero (outer group) and 0xffffffff (material run)
// delimiters. The consumer looks ahead from a material marker to a real
// record, so never leave an empty material run after filtering.
template<class Hide>
bool FilterQueuedDecalTail(const std::vector<std::uint64_t> &input,bool sorted,Hide hide,
                          std::vector<std::uint64_t> &output,std::size_t &removed){
    output.clear();removed=0;
    if(input.size()>65536)return false;
    constexpr std::uint64_t marker=0xffffffffu;
    std::vector<std::uint64_t> next;next.reserve(input.size());
    std::size_t hidden=0;
    bool outer=false,run=false,runHasInput=false,outerEmitted=false,runEmitted=false;
    for(auto address:input){
        if(sorted&&(address==0||address==marker)){
            // The native sorted collector can emit empty outer groups but
            // an empty material run is not a layout we can safely rewrite.
            if(run&&!runHasInput)return false;
            if(address==0){outer=true;run=false;outerEmitted=false;runEmitted=false;}
            else {if(!outer)return false;run=true;runEmitted=false;runHasInput=false;}
            continue;
        }
        if(address<0x10000||address==marker||(sorted&&!run))return false;
        runHasInput=true;
        if(hide(address)){++hidden;continue;}
        if(sorted){
            if(!outerEmitted){next.push_back(0);outerEmitted=true;}
            if(!runEmitted){next.push_back(marker);runEmitted=true;}
        }
        next.push_back(address);
    }
    if(sorted&&run&&!runHasInput)return false;
    if(hidden==0)next=input; // preserve even harmless empty outer groups
    output.swap(next);removed=hidden;return true;
}
// Read-only adapter for the exact engine's opaque-world render-list
// buckets. Only current-view receiver indices are returned, never pointers.
// A failed or incomplete walk cannot authorize detached submissions.
template<class Read>
bool ResolveWorldReceivers(std::uint64_t list,int group,std::uint64_t surfaces,std::uint32_t surfaceCount,
                           const std::set<std::uint32_t> &wanted,std::set<std::uint32_t> &visible,Read read){
    visible.clear();
    if(group<0||group>=3||!list||!surfaces||!surfaceCount||surfaceCount>1048576)return false;
    constexpr auto Max=(std::numeric_limits<std::uint64_t>::max)();
    if(list>Max-0x200||surfaces>Max-std::uint64_t(surfaceCount)*64)return false;
    struct VectorHeader {
        std::uint64_t Data=0;
        std::int32_t Capacity=0,Grow=0,Count=0;
    };
    auto header=[&](std::uint64_t address,VectorHeader &h){
        return read(address,&h.Data,8)&&read(address+8,&h.Capacity,4)&&read(address+16,&h.Count,4)&&
            h.Capacity>=0&&h.Capacity<=32768&&h.Count>=0&&h.Count<=h.Capacity&&(!h.Count||h.Data);
    };
    auto equal=[](const VectorHeader &a,const VectorHeader &b){return a.Data==b.Data&&a.Capacity==b.Capacity&&a.Count==b.Count;};
    const auto poolAddress=list+0x18,groupAddress=list+0x78+std::uint64_t(group)*0x20;
    VectorHeader pool,buckets;
    if(!header(poolAddress,pool)||!header(groupAddress,buckets))return false;
    if(wanted.empty()||!buckets.Count)return true;
    if(!pool.Count||pool.Data>Max-std::uint64_t(pool.Count)*128||buckets.Data>Max-std::uint64_t(buckets.Count)*8)return false;
    std::vector<std::uint64_t> heads(buckets.Count);
    // One bulk copy per contiguous array. Only linked, valid entries in the
    // temporary pool copy are interpreted, and the copy is never retained.
    using Chunk=std::array<unsigned char,128>;
    std::vector<Chunk> chunks(pool.Count);
    if(!read(buckets.Data,heads.data(),heads.size()*8)||!read(pool.Data,chunks.data(),chunks.size()*sizeof(Chunk)))return false;
    std::vector<unsigned char> visited(pool.Count);
    std::set<std::uint32_t> result;
    for(auto head:heads){
        std::uint16_t next=0xffff;
        if(!head||!read(head,&next,sizeof(next)))return false;
        while(next!=0xffff){
            if(next>=chunks.size()||visited[next])return false;
            visited[next]=1;
            const auto &chunk=chunks[next];
            std::uint16_t count=0;
            std::memcpy(&next,chunk.data(),2);std::memcpy(&count,chunk.data()+2,2);
            if(count>15)return false;
            for(unsigned i=0;i<count;++i){
                std::uint64_t receiver=0;std::memcpy(&receiver,chunk.data()+8+i*8,8);
                if(receiver<surfaces||(receiver-surfaces)%64||(receiver-surfaces)/64>=surfaceCount)return false;
                const auto index=static_cast<std::uint32_t>((receiver-surfaces)/64);
                if(wanted.count(index))result.insert(index);
            }
        }
    }
    VectorHeader afterPool,afterBuckets;
    if(!header(poolAddress,afterPool)||!header(groupAddress,afterBuckets)||!equal(pool,afterPool)||!equal(buckets,afterBuckets))return false;
    visible.swap(result);return true;
}
// Atlas coordinates are historical metadata, not historical texture pixels.
// Only the verified simple material pair can use its original rectangle today.
enum class ShadowTexture { Unknown, Simple, Other, Atlas };
inline bool ValidShadowTextureRect(const std::array<float,4> &r){
    return std::all_of(r.begin(),r.end(),[](float x){return std::isfinite(x)&&x>=0&&x<=1;})&&
        r[2]>0&&r[3]>0&&r[0]+r[2]<=1.0001f&&r[1]+r[3]<=1.0001f;
}
struct Shadow {
    std::array<float,3> Origin{},Direction{},Caster{};
    std::array<float,16> Matrix{};
    std::array<float,2> Size{};
    float Height=0,FalloffOffset=0,FalloffAmount=0;
    unsigned char Bias=0;
    ShadowTexture Texture=ShadowTexture::Unknown;
    std::array<float,4> UV{0,0,1,1};
    bool HasSimpleTexture()const{return Texture==ShadowTexture::Simple&&ValidShadowTextureRect(UV);}
    bool HasAtlasRect()const{return Texture==ShadowTexture::Atlas&&ValidShadowTextureRect(UV);}
    std::array<float,4> ReplayTextureRect()const{return HasSimpleTexture()?UV:std::array<float,4>{0,0,1,1};}
    // Engine projected shadows support four additional world-space planes.
    // Only initialized entries are retained; a known empty list is distinct
    // from a failed capture.
    bool ClipKnown=false;
    std::uint16_t ClipCount=0;
    std::array<std::array<float,4>,4> ClipPlanes{};
    bool SetClipPlanes(unsigned count,const std::array<float,12> &normals,const std::array<float,4> &distances){
        ClipKnown=false;ClipCount=0;ClipPlanes={};
        if(count>4)return false;
        std::array<std::array<float,4>,4> planes{};
        for(unsigned i=0;i<count;++i){
            auto &p=planes[i];p={normals[i*3],normals[i*3+1],normals[i*3+2],distances[i]};
            if(!std::all_of(p.begin(),p.end(),[](float x){return std::isfinite(x)&&std::abs(x)<1e7f;})||
               (p[0]==0&&p[1]==0&&p[2]==0))return false;
        }
        ClipPlanes=planes;ClipCount=static_cast<std::uint16_t>(count);ClipKnown=true;return true;
    }
    bool ValidClipPlanes()const{
        if(!ClipKnown)return ClipCount==0;
        if(ClipCount>4)return false;
        for(unsigned i=0;i<ClipCount;++i){
            const auto &p=ClipPlanes[i];
            if(!std::all_of(p.begin(),p.end(),[](float x){return std::isfinite(x)&&std::abs(x)<1e7f;})||
               (p[0]==0&&p[1]==0&&p[2]==0))return false;
        }
        return true;
    }
    // BSP leaf identities are immutable recorded values, not live addresses.
    // A moving projection can share the same leaf list across many samples.
    std::shared_ptr<const std::vector<int>> Leaves;
    bool SetLeaves(std::vector<int> values,const std::shared_ptr<const std::vector<int>> &previous={}){
        if(values.empty()||values.size()>8192||!std::all_of(values.begin(),values.end(),[](int v){return v>=0&&v<1048576;})){
            Leaves.reset();return false;
        }
        Leaves=previous&&*previous==values?previous:std::make_shared<const std::vector<int>>(std::move(values));
        return true;
    }
    bool Valid() const {
        auto finite=[](const auto &v){return std::all_of(v.begin(),v.end(),[](float x){return std::isfinite(x)&&std::abs(x)<1e7f;});};
        return ValidClipPlanes()&&finite(Origin)&&finite(Direction)&&finite(Caster)&&finite(Matrix)&&finite(Size)&&Size[0]>0&&Size[1]>0&&Size[0]<8192&&Size[1]<8192&&
            std::isfinite(Height)&&Height>0&&Height<32768&&std::isfinite(FalloffOffset)&&std::isfinite(FalloffAmount)&&
            Leaves&&!Leaves->empty()&&Leaves->size()<=8192&&std::all_of(Leaves->begin(),Leaves->end(),[](int v){return v>=0&&v<1048576;});
    }
    bool operator==(const Shadow &o) const {
        return Origin==o.Origin&&Direction==o.Direction&&Caster==o.Caster&&Matrix==o.Matrix&&Size==o.Size&&Height==o.Height&&FalloffOffset==o.FalloffOffset&&FalloffAmount==o.FalloffAmount&&Bias==o.Bias&&Texture==o.Texture&&UV==o.UV&&ClipKnown==o.ClipKnown&&ClipCount==o.ClipCount&&ClipPlanes==o.ClipPlanes&&(Leaves==o.Leaves||(Leaves&&o.Leaves&&*Leaves==*o.Leaves));
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
    std::size_t OutsideWorld=0,Attached=0,Flagged=0,Displacement=0,Material=0,FadeInvalid=0;
    std::size_t Total()const{return OutsideWorld+Attached+Flagged+Displacement+Material+FadeInvalid;}
    std::string Describe()const{
        return "outside world "+std::to_string(OutsideWorld)+", attached "+std::to_string(Attached)+
            ", flags 0x1000 "+std::to_string(Flagged)+", displacement "+std::to_string(Displacement)+
            ", material unavailable "+std::to_string(Material)+", invalid fade "+std::to_string(FadeInvalid);
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
    // Slot identities from a complete traversal whose payload was unsupported.
    // A matching live slot is uncertain, not proof of a future-only mark.
    std::shared_ptr<const std::vector<std::uint16_t>> UncertainDecalSlots;
    bool DecalSlotUncertain(std::uint16_t slot)const{
        return UncertainDecalSlots&&std::binary_search(UncertainDecalSlots->begin(),UncertainDecalSlots->end(),slot);
    }
    std::shared_ptr<const std::vector<Shadow>> Shadows;
};
// Surface systems can be captured independently. Empty ready vectors are
// authoritative; unavailable systems may fall back only within the tick bound.
struct PreviewSample {
    Frame Value;
    int ShadowTick=-1,DecalTick=-1;
    explicit operator bool()const{return ShadowTick>=0||DecalTick>=0;}
};
class History {
    std::size_t Budget;
    static std::size_t Cost(const std::vector<Decal> &v){std::size_t n=sizeof(v)+v.capacity()*sizeof(Decal);for(const auto &d:v)n+=d.Material.capacity();return n;}
    static std::size_t Cost(const std::vector<Shadow> &v){return sizeof(v)+v.capacity()*sizeof(Shadow);}
    static std::size_t Cost(const std::vector<int> &v){return sizeof(v)+v.capacity()*sizeof(int);}
    static std::size_t Cost(const std::vector<DecalVertex> &v){return sizeof(v)+v.capacity()*sizeof(DecalVertex);}
    static std::size_t Cost(const std::vector<std::uint16_t> &v){return sizeof(v)+v.capacity()*sizeof(std::uint16_t);}
    std::map<const void*,std::size_t> Refs;
    template<class T> std::size_t ExtraVector(const std::shared_ptr<const std::vector<T>> &p,bool standalone=false)const{
        if(!p||(!standalone&&Refs.count(p.get())))return 0;
        std::size_t n=Cost(*p);
        if constexpr(std::is_same_v<T,Decal>){
            std::set<const void*> seen;
            for(const auto &d:*p)if(d.Geometry&&(standalone||!Refs.count(d.Geometry.get()))&&seen.insert(d.Geometry.get()).second)n+=Cost(*d.Geometry);
        }else if constexpr(std::is_same_v<T,Shadow>){
            std::set<const void*> seen;
            for(const auto &s:*p)if(s.Leaves&&(standalone||!Refs.count(s.Leaves.get()))&&seen.insert(s.Leaves.get()).second)n+=Cost(*s.Leaves);
        }
        return n;
    }
    template<class T> void AddVector(const std::shared_ptr<const std::vector<T>> &p){
        if(!p)return;
        auto &n=Refs[p.get()];
        if(!n++){
            Bytes+=Cost(*p);
            if constexpr(std::is_same_v<T,Decal>)for(const auto &d:*p)AddVector(d.Geometry);
            else if constexpr(std::is_same_v<T,Shadow>)for(const auto &s:*p)AddVector(s.Leaves);
        }
    }
    template<class T> void DropVector(const std::shared_ptr<const std::vector<T>> &p){
        if(!p)return;
        auto it=Refs.find(p.get());
        if(it!=Refs.end()&&!--it->second){
            Bytes-=Cost(*p);Refs.erase(it);
            if constexpr(std::is_same_v<T,Decal>)for(const auto &d:*p)DropVector(d.Geometry);
            else if constexpr(std::is_same_v<T,Shadow>)for(const auto &s:*p)DropVector(s.Leaves);
        }
    }
    std::size_t Extra(const Frame &f)const{return sizeof(Frame)+ExtraVector(f.Decals)+ExtraVector(f.Shadows)+ExtraVector(f.UncertainDecalSlots);}
    void Drop(bool front){
        Frame f=front?std::move(Frames.front()):std::move(Frames.back());
        if(front)Frames.pop_front();else Frames.pop_back();
        Bytes-=sizeof(Frame);DropVector(f.Decals);DropVector(f.Shadows);DropVector(f.UncertainDecalSlots);
    }
public:
    explicit History(std::size_t budget=64u*1024u*1024u):Budget(budget){}
    std::deque<Frame> Frames;
    std::size_t Bytes=0,Evicted=0,Rejected=0;
    void Clear(){Frames.clear();Refs.clear();Bytes=Evicted=Rejected=0;}
    bool Append(Frame f){
        // A failed/unsupported capture (often World==0) must not erase a
        // valid history or replace a good sample at the same tick.
        // Readiness without a payload is not authoritative, but a ready
        // empty vector is. Merge only unavailable subsystems at the exact
        // same tick and world; a valid new empty sample replaces old data.
        f.DecalsReady=f.DecalsReady&&bool(f.Decals);
        f.ShadowsReady=f.ShadowsReady&&bool(f.Shadows);
        if(!f.DecalsReady){f.Decals.reset();f.UncertainDecalSlots.reset();}
        if(f.UncertainDecalSlots&&(!std::is_sorted(f.UncertainDecalSlots->begin(),f.UncertainDecalSlots->end())||f.UncertainDecalSlots->size()>8192)){++Rejected;return false;}
        if(!f.ShadowsReady)f.Shadows.reset();
        if(f.Tick<0||!f.World||(!f.DecalsReady&&!f.ShadowsReady)){++Rejected;return false;}
        if(!Frames.empty()&&f.Tick==Frames.back().Tick&&f.World==Frames.back().World){
            const auto &last=Frames.back();
            if(!f.DecalsReady&&last.DecalsReady&&last.Decals){
                f.DecalsReady=true;f.Decals=last.Decals;f.DecalStatus=last.DecalStatus;
                f.DecalsUnsupported=last.DecalsUnsupported;f.UnsupportedDecals=last.UnsupportedDecals;f.UncertainDecalSlots=last.UncertainDecalSlots;
            }
            if(!f.ShadowsReady&&last.ShadowsReady&&last.Shadows){
                f.ShadowsReady=true;f.Shadows=last.Shadows;f.ShadowsUnobserved=last.ShadowsUnobserved;
            }
        }
        if(sizeof(Frame)+ExtraVector(f.Decals,true)+ExtraVector(f.Shadows,true)+ExtraVector(f.UncertainDecalSlots,true)>Budget){++Rejected;return false;}
        if(!Frames.empty()&&(f.World!=Frames.back().World||f.Tick<Frames.back().Tick))Clear();
        if(!Frames.empty()&&f.Tick==Frames.back().Tick)Drop(false);
        if(!Frames.empty()){
            const auto &last=Frames.back();
            if(f.Decals&&last.Decals&&*f.Decals==*last.Decals)f.Decals=last.Decals;
            if(f.Shadows&&last.Shadows&&*f.Shadows==*last.Shadows)f.Shadows=last.Shadows;
            if(f.UncertainDecalSlots&&last.UncertainDecalSlots&&*f.UncertainDecalSlots==*last.UncertainDecalSlots)f.UncertainDecalSlots=last.UncertainDecalSlots;
        }
        while(!Frames.empty()&&(Bytes>Budget-Extra(f)||Frames.size()>=32768)){Drop(true);++Evicted;}
        Bytes+=sizeof(Frame);AddVector(f.Decals);AddVector(f.Shadows);AddVector(f.UncertainDecalSlots);Frames.push_back(std::move(f));return true;
    }
    // Same bounded, causal sample policy as modern particle history.
    const Frame *ForPreview(int tick,int maxGap=2) const {
        if(Frames.empty()||tick<Frames.front().Tick||tick>Frames.back().Tick||maxGap<0)return nullptr;
        auto it=std::upper_bound(Frames.begin(),Frames.end(),tick,[](int t,const Frame &f){return t<f.Tick;});
        if(it==Frames.begin())return nullptr;
        --it;return static_cast<std::int64_t>(tick)-it->Tick<=maxGap?&*it:nullptr;
    }
    PreviewSample ResolvePreview(int tick,int maxGap=2)const{
        PreviewSample result;result.Value.Tick=tick;
        const auto *latest=ForPreview(tick,maxGap);
        if(!latest)return result;
        result.Value.World=latest->World;
        auto it=std::upper_bound(Frames.begin(),Frames.end(),tick,[](int t,const Frame &f){return t<f.Tick;});
        while(it!=Frames.begin()){
            --it;
            if(it->World!=result.Value.World||static_cast<std::int64_t>(tick)-it->Tick>maxGap)break;
            if(result.ShadowTick<0&&it->ShadowsReady&&it->Shadows){
                result.ShadowTick=it->Tick;result.Value.ShadowsReady=true;
                result.Value.Shadows=it->Shadows;result.Value.ShadowsUnobserved=it->ShadowsUnobserved;
            }
            if(result.DecalTick<0&&it->DecalsReady&&it->Decals){
                result.DecalTick=it->Tick;result.Value.DecalsReady=true;
                result.Value.Decals=it->Decals;result.Value.DecalStatus=it->DecalStatus;
                result.Value.DecalsUnsupported=it->DecalsUnsupported;result.Value.UnsupportedDecals=it->UnsupportedDecals;result.Value.UncertainDecalSlots=it->UncertainDecalSlots;
            }
            if(result.ShadowTick>=0&&result.DecalTick>=0)break;
        }
        return result;
    }
    const Frame *At(int tick)const{
        auto it=std::lower_bound(Frames.begin(),Frames.end(),tick,[](const Frame &f,int t){return f.Tick<t;});
        return it!=Frames.end()&&it->Tick==tick?&*it:nullptr;
    }
};
}} // namespace
