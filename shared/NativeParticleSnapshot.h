#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <cstring>
#include <limits>
#include <set>
#include <map>
#include <memory>
#include <type_traits>
#include <string>
#include <vector>
#include "ParticleHistoryCodec.h"

// Read-only, exact-build TF2 x64 experiment. No native function calls, engine
// writes or persistent engine pointers. Offsets verified in the supplied PE.
namespace advancedfx { namespace NativeParticles {
using Address=std::uint64_t;
namespace Layout {
constexpr Address Manager=0x10f5020, ManagerReady=0x10f51a0, RootHead=0x60;
constexpr Address RootVtable=0xb60c70, RootCollection=0x20, RootNext=0x27b8, RootPrev=0x27c0;
constexpr Address Active=0x38, ChildHead=0x22b8, ChildNext=0x2280;
constexpr Address Xyz=0x2310, XyzStride=0x2510;
constexpr Address Definition=0x58, DefinitionName=0x240, Clock=0x34;
constexpr std::size_t AttributeCount=22, WordLimit=2u*1024u*1024u;
constexpr std::size_t MaxCollections=4096, MaxPoints=262144;
constexpr int MaxPerCollection=65536, MaxDetachedActive=5000;
}
using Point=std::array<float,3>;
// SDK attribute indices. Native tables store 32 entries; only 22 documented
// slots have known semantics, and their original 32-bit words are preserved.
enum Attribute : unsigned { Position=0, Life=1, Previous=2, Radius=3, Rotation=4,
    RotationSpeed=5, Tint=6, Alpha=7, Created=8, Sequence=9, TrailLength=10,
    ParticleId=11, Yaw=12, Sequence2=13, HitboxIndex=14, HitboxRelative=15,
    Alpha2=16, TraceP0=17, TraceP1=18, TraceHitT=19, TraceNormal=20, ControlPointIndex=21 };
inline unsigned Components(unsigned a) {
    // Actual x64 allocation mask, verified at RVA 0x8e2b70. SDK names
    // are descriptive only where this build differs (rotation and trace normal).
    return a<32 && (0x68055u & (1u<<a)) ? 3u:1u;
}
inline constexpr const char *AttributeNames[Layout::AttributeCount]={
    "xyz","life","prev_xyz","radius","rotation","rotation_speed","tint","alpha",
    "created","sequence","trail_length","id","yaw","sequence2","hitbox","hitbox_xyz",
    "alpha2","trace_p0","trace_p1","trace_t","trace_normal","cp_index"};
struct AttributeBlock {
    std::uint32_t Start=0, Count=0;
    std::uint8_t Stride=0; // 0: constant lanes, 4: scalar, 12: vec3
};
struct Collection {
    int Parent=-1; // frame-local collection index, never a live pointer
    std::uint32_t Active=0, PointStart=0, PointCount=0, Mask=0;
    float Time=0;
    bool HasTime=false;
    std::array<char,128> Name{};
    std::array<AttributeBlock,Layout::AttributeCount> Attributes{};
};
struct Frame {
    int Tick=-1;
    std::size_t Roots=0, Children=0, Active=0, Empty=0, Rejected=0;
    bool Complete=false;
    std::string Status;
    std::vector<Point> Points;
    bool Appearance=false, AppearanceComplete=false;
    std::size_t AppearanceRejected=0, NamedCollections=0, SharedConstantWords=0;
    std::vector<Collection> Collections;
    std::vector<std::uint32_t> Words;
    std::size_t Bytes() const {return sizeof(Frame)+Points.capacity()*sizeof(Point)+Status.capacity()+
        Collections.capacity()*sizeof(Collection)+Words.capacity()*sizeof(std::uint32_t);}
};
// Bit-preserving access to packed historical attributes, not engine memory.
// Constant data contains one four-wide block; the lane index still matters.
inline bool Word(const Frame &f,const Collection &c,unsigned a,unsigned particle,unsigned component,std::uint32_t &out) {
    if(a>=Layout::AttributeCount||particle>=c.Active||component>=Components(a)||!(c.Mask&(1u<<a))) return false;
    const auto &b=c.Attributes[a];
    const std::size_t offset=std::size_t(particle/4)*b.Stride+(particle&3)+component*4;
    if(offset>=b.Count||std::size_t(b.Start)+offset>=f.Words.size()) return false;
    out=f.Words[std::size_t(b.Start)+offset];return true;
}
inline bool Float(const Frame &f,const Collection &c,unsigned a,unsigned particle,unsigned component,float &out) {
    std::uint32_t bits;
    if(!Word(f,c,a,particle,component,bits)) return false;
    float value;
    std::memcpy(&value,&bits,sizeof(value));
    if(!std::isfinite(value)) return false;
    out=value;return true;
}
// Reader signature: bool(address, destination, size). Caller must establish a
// quiescent engine-thread capture boundary and verify module identity.
template<class Reader>
Frame Capture(Address module,int tick,Reader read,bool appearance=false) {
    using namespace Layout;
    Frame f; f.Tick=tick; f.Appearance=appearance;
    auto get=[&](Address a,auto &v){return a>=0x10000 && read(a,&v,sizeof(v));};
    std::uint32_t ready=0;
    Address root=0;
    if(!get(module+ManagerReady,ready) || !(ready&1) ||
       !get(module+Manager+RootHead,root)) {f.Status="Manager not initialized/readable";return f;}
    f.Complete=true; f.AppearanceComplete=appearance;
    std::set<Address> roots, collections;
    std::vector<float> packed;
    // Intern identical constant SIMD blocks within a snapshot. Keys include
    // width and all lane bits; varying blocks are kept independent.
    std::map<std::array<std::uint32_t,13>,std::uint32_t> constants;
    auto bad=[&](){++f.Rejected;f.Complete=false;};
    auto appearanceBad=[&](){++f.AppearanceRejected;f.AppearanceComplete=false;};
    auto copyAppearance=[&](Address c,Collection &item,Address capturedXyz,Address capturedStride,bool havePosition) {
        Address def=0,name=0;
        if(get(c+Definition,def)&&def>=0x10000&&get(def+DefinitionName,name)&&name>=0x10000) {
            // Fast bounded copy; fallback handles a string near an unreadable page.
            std::array<char,128> nameBytes{};
            bool ok=read(name,nameBytes.data(),nameBytes.size());
            if(!ok) {
                nameBytes.fill(0);
                for(std::size_t i=0;i<nameBytes.size();++i) {
                    if(!get(name+i,nameBytes[i])) break;
                    if(!nameBytes[i]) {ok=true;break;}
                }
            }
            if(ok&&nameBytes[0]&&std::find(nameBytes.begin(),nameBytes.end(),'\0')!=nameBytes.end()) {
                const auto end=std::find(nameBytes.begin(),nameBytes.end(),'\0');
                std::copy(nameBytes.begin(),end,item.Name.begin());++f.NamedCollections;
            } else appearanceBad();
        } else appearanceBad();
        if(get(c+Clock,item.Time)&&std::isfinite(item.Time)) item.HasTime=true;
        else appearanceBad();
        if(!item.Active) return; // empty nodes still preserve hierarchy/name/clock
        std::array<Address,AttributeCount> pointers{},strides{};
        // One bounded read of the two verified attribute tables, with the
        // original narrow fallback for inaccessible intervening bytes.
        constexpr std::size_t TableBytes=std::size_t(XyzStride-Xyz)+sizeof(strides);
        std::array<unsigned char,TableBytes> tables{};
        if(c>=0x10000&&c<=(std::numeric_limits<Address>::max)()-Xyz-TableBytes&&
           read(c+Xyz,tables.data(),tables.size())){
            std::memcpy(pointers.data(),tables.data(),sizeof(pointers));
            std::memcpy(strides.data(),tables.data()+std::size_t(XyzStride-Xyz),sizeof(strides));
        }else if(!get(c+Xyz,pointers)||!get(c+XyzStride,strides)) {appearanceBad();return;}
        for(unsigned a=0;a<AttributeCount;++a) {
            const auto ptr=pointers[a],stride=strides[a];
            const auto width=std::size_t(4*Components(a));
            // Unused slots may be null. Record absence, not invented defaults.
            if(!ptr) {if(stride) appearanceBad();continue;}
            if(ptr<0x10000||(stride!=0&&stride!=width)) {appearanceBad();continue;}
            const auto words=(stride?(std::size_t(item.Active)+3)/4:1)*width;
            // Position was already read for diagnostic points in this
            // quiescent capture. Reuse its exact bytes only when the fresh
            // appearance table independently agrees on address and stride.
            auto copyWords=[&](std::uint32_t *out){
                if(a==Position&&havePosition&&ptr==capturedXyz&&stride==capturedStride&&words==packed.size()){
                    std::memcpy(out,packed.data(),words*sizeof(std::uint32_t));
                    return true;
                }
                return read(ptr,out,words*sizeof(std::uint32_t));
            };
            if(!stride) {
                std::array<std::uint32_t,13> key{};
                key[0]=static_cast<std::uint32_t>(words);
                if(!copyWords(key.data()+1)) {appearanceBad();continue;}
                auto found=constants.find(key);
                std::uint32_t start=0;
                if(found!=constants.end()) {start=found->second;f.SharedConstantWords+=words;}
                else {
                    if(words>WordLimit-f.Words.size()) {appearanceBad();continue;}
                    start=static_cast<std::uint32_t>(f.Words.size());
                    f.Words.insert(f.Words.end(),key.begin()+1,key.begin()+1+words);
                    constants.emplace(key,start);
                }
                item.Attributes[a]={start,static_cast<std::uint32_t>(words),0};
                item.Mask|=1u<<a;
                continue;
            }
            if(words>WordLimit-f.Words.size()) {appearanceBad();continue;}
            const auto start=f.Words.size();
            f.Words.resize(start+words);
            if(!copyWords(f.Words.data()+start)) {
                f.Words.resize(start);appearanceBad();continue;
            }
            item.Attributes[a]={static_cast<std::uint32_t>(start),static_cast<std::uint32_t>(words),static_cast<std::uint8_t>(stride)};
            item.Mask|=1u<<a;
        }
    };
    auto walk=[&](auto &&self,Address c,bool child,unsigned depth,int parent)->void {
        if(!c || depth>32 || collections.size()>=MaxCollections || !collections.insert(c).second) {bad();return;}
        if(child) ++f.Children;
        int count=0; Address children=0;
        if(!get(c+Active,count)||!get(c+ChildHead,children)||count<0||count>MaxPerCollection) {bad();return;}
        f.Active+=std::size_t(count);
        const auto start=f.Points.size();
        Address xyz=0,stride=0;
        bool havePosition=false;
        if(!count) ++f.Empty;
        else {
            // Position uses four-wide SoA blocks, 12 floats per block; a
            // zero stride is the SDK's shared constant-attribute representation.
            if(!get(c+Xyz,xyz)||!get(c+XyzStride,stride)||xyz<0x10000||
               (stride!=12 && stride!=0)||f.Points.size()+std::size_t(count)>MaxPoints) bad();
            else {
                const auto blocks=(std::size_t(count)+3)/4;
                packed.resize((stride?blocks:1)*12);
                if(!read(xyz,packed.data(),packed.size()*sizeof(float))) bad();
                else {
                    havePosition=true;
                    for(int i=0;i<count;++i) {
                        const auto offset=(std::size_t(i)/4)*std::size_t(stride)+(i&3);
                        Point p{packed[offset],packed[offset+4],packed[offset+8]};
                        if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2])) {bad();continue;}
                        f.Points.push_back(p);
                    }
                }
            }
        }
        int node=-1;
        if(appearance) {
            Collection item;
            item.Parent=parent;item.Active=static_cast<std::uint32_t>(count);
            item.PointStart=static_cast<std::uint32_t>(start);
            item.PointCount=static_cast<std::uint32_t>(f.Points.size()-start);
            copyAppearance(c,item,xyz,stride,havePosition);
            node=static_cast<int>(f.Collections.size());
            f.Collections.push_back(std::move(item));
        }
        while(children) {
            if(collections.size()>=MaxCollections || collections.count(children) || depth>=32) {bad();break;}
            Address next=0;
            if(!get(children+ChildNext,next)) {bad();break;}
            self(self,children,true,depth+1,node);
            children=next;
        }
    };
    Address previous=0;
    while(root) {
        if(roots.size()>=MaxCollections||collections.size()>=MaxCollections||!roots.insert(root).second) {bad();break;}
        Address vt=0,next=0,prev=0;
        if(!get(root,vt)||vt!=module+RootVtable||!get(root+RootNext,next)||
           !get(root+RootPrev,prev)||prev!=previous) {bad();break;}
        ++f.Roots;
        walk(walk,root+RootCollection,false,0,-1);
        previous=root;root=next;
    }
    if(!f.Complete) f.AppearanceComplete=false;
    // The geometrically growing capture buffers can retain almost twice
    // their useful payload. Tighten large excess capacities before history.
    if(appearance) {
        auto compact=[](auto &v) {
            if(v.capacity()>v.size()+v.size()/8+64) {
                using V=std::decay_t<decltype(v)>;
                V(v.begin(),v.end()).swap(v);
            }
        };
        compact(f.Points);compact(f.Collections);compact(f.Words);
    }
    f.Status=f.Complete?"Native position snapshot":"Incomplete native position snapshot";
    return f;
}
// A dry-run copy plan for a newly created detached collection. No writes until
// every selected node and buffer is validated. Only varying owned buffers
// are copied; constants are compared, not written. No active count is changed.
struct CopyOp {
    Address Destination=0;
    std::uint32_t Start=0,Count=0;
};
// Merge only adjacent verified ranges within the same native allocation.
// Do not write padding/gaps or constants, and never bridge collection boundaries.
inline void AppendCopy(std::vector<CopyOp> &copies,std::size_t collectionStart,CopyOp op){
    if(copies.size()>collectionStart){
        auto &last=copies.back();
        const auto maxAddress=(std::numeric_limits<Address>::max)();
        const auto maxCount=(std::numeric_limits<std::uint32_t>::max)();
        const auto bytes=Address(last.Count)*sizeof(std::uint32_t);
        if(last.Count&&op.Count&&last.Destination<=maxAddress-bytes&&last.Destination+bytes==op.Destination&&
           last.Start<=maxCount-last.Count&&last.Start+last.Count==op.Start&&last.Count<=maxCount-op.Count){
            last.Count+=op.Count;return;
        }
    }
    copies.push_back(op);
}
struct RestoreBinding {Address Native=0;std::size_t Recorded=0;Address Definition=0;};
// Exact adjacent native clock/active-block fields at +0x20..+0x37.
// The active count at +0x38 is deliberately committed separately, last.
inline std::array<std::uint32_t,6> ActivationPrefix(float time,int active){
    std::array<std::uint32_t,6> words{};std::uint32_t bits=0;std::memcpy(&bits,&time,sizeof(bits));
    words[0]=words[1]=words[2]=words[3]=words[5]=bits;
    words[4]=static_cast<std::uint32_t>((active+3)/4);return words;
}
struct RestorePlan {
    std::vector<CopyOp> Copies;
    std::vector<RestoreBinding> Bindings;
    std::size_t Nodes=0,Particles=0,Constants=0,ConstantDifferences=0,Missing=0;
    std::string Error;
    // Reuse allocations only. No previous native address or result is
    // logically retained when preparing another detached allocation.
    void Reset(){
        Copies.clear();Bindings.clear();Error.clear();
        Nodes=Particles=Constants=ConstantDifferences=Missing=0;
    }
};
// Recorded topology is reused across all detached tree attempts in one callback.
using ChildIndex=std::vector<std::vector<std::size_t>>;
inline bool BuildChildIndex(const Frame &f,ChildIndex &children) {
    children.clear();
    if(f.Collections.size()>Layout::MaxCollections)return false;
    children.resize(f.Collections.size());
    for(std::size_t i=0;i<f.Collections.size();++i){
        const auto parent=f.Collections[i].Parent;
        if(parent==-1)continue;
        if(parent<0||static_cast<std::size_t>(parent)>=i){children.clear();return false;}
        children[parent].push_back(i);
    }
    return true;
}
// A recorded-only rejection pass. These checks mirror conditions already
// enforced by PlanDetached and the native draw gate; they do not assume
// which attributes an unknown renderer actually uses. An incompatible
// ancestor must not prevent a compatible child from being tried separately.
enum class ReplayPrerequisite : unsigned char {Ready,Name,Clock,Capacity,Mask,Block,Descendant};
inline const char *ReplayPrerequisiteReason(ReplayPrerequisite reason){
    switch(reason){
        case ReplayPrerequisite::Name:return "recorded definition name missing or unterminated";
        case ReplayPrerequisite::Clock:return "recorded collection clock missing or invalid";
        case ReplayPrerequisite::Capacity:return "recorded active count exceeds detached capacity";
        case ReplayPrerequisite::Mask:return "recorded appearance attributes missing";
        case ReplayPrerequisite::Block:return "recorded appearance block invalid";
        case ReplayPrerequisite::Descendant:return "recorded descendant cannot be restored";
        default:return "recorded prerequisites satisfied";
    }
}
inline bool BuildReplayPrerequisites(const Frame &f,std::vector<ReplayPrerequisite> &out){
    using namespace Layout;
    out.clear();
    if(!f.Complete||!f.Appearance||f.Collections.size()>MaxCollections||f.Words.size()>WordLimit)return false;
    out.resize(f.Collections.size(),ReplayPrerequisite::Ready);
    constexpr std::uint32_t all=(std::uint32_t(1)<<AttributeCount)-1;
    for(std::size_t i=0;i<f.Collections.size();++i){
        const auto &c=f.Collections[i];
        if(c.Parent!=-1&&(c.Parent<0||static_cast<std::size_t>(c.Parent)>=i)){out.clear();return false;}
        auto &why=out[i];
        if(!c.Name[0]||std::find(c.Name.begin(),c.Name.end(),'\0')==c.Name.end())why=ReplayPrerequisite::Name;
        else if(!c.HasTime||!std::isfinite(c.Time))why=ReplayPrerequisite::Clock;
        else if(c.Active>MaxDetachedActive)why=ReplayPrerequisite::Capacity;
        else if(c.Active){
            if(c.Mask!=all){why=ReplayPrerequisite::Mask;continue;}
            for(unsigned a=0;a<AttributeCount;++a){
                const auto &b=c.Attributes[a];const auto width=4u*Components(a);
                const auto words=(b.Stride?(std::size_t(c.Active)+3)/4:1)*width;
                if((b.Stride&&b.Stride!=width)||b.Count!=words||b.Start>f.Words.size()||words>f.Words.size()-b.Start){
                    why=ReplayPrerequisite::Block;break;
                }
            }
        }
    }
    for(std::size_t i=f.Collections.size();i--;){
        const int parent=f.Collections[i].Parent;
        if(parent>=0&&out[i]!=ReplayPrerequisite::Ready&&out[parent]==ReplayPrerequisite::Ready)
            out[parent]=ReplayPrerequisite::Descendant;
    }
    return true;
}
// Capture completeness alone does not authorize blanket live isolation.
inline bool RecordedIsolationReadyWithPrerequisites(const Frame &f,const std::vector<ReplayPrerequisite> &why){
    if(!f.Complete||!f.Appearance||!f.AppearanceComplete||f.Rejected||f.AppearanceRejected||why.size()!=f.Collections.size())return false;
    std::vector<bool> active(f.Collections.size());
    std::size_t count=0;
    for(std::size_t i=f.Collections.size();i--;){
        const auto &c=f.Collections[i];count+=c.Active;
        active[i]=active[i]||c.Active!=0;
        if(active[i]&&why[i]!=ReplayPrerequisite::Ready)return false;
        if(c.Parent>=0&&active[i])active[c.Parent]=true;
    }
    return count==f.Active&&count==f.Points.size();
}
inline bool RecordedIsolationReady(const Frame &f){
    std::vector<ReplayPrerequisite> why;
    return BuildReplayPrerequisites(f,why)&&RecordedIsolationReadyWithPrerequisites(f,why);
}
inline std::vector<std::size_t> ReplaySubtreeCounts(const Frame &f){
    std::vector<std::size_t> active(f.Collections.size());
    for(std::size_t i=f.Collections.size();i--;){
        active[i]+=f.Collections[i].Active;
        const auto parent=f.Collections[i].Parent;
        if(parent>=0)active[parent]+=active[i];
    }
    return active;
}
template<class Attempt>
void WalkReplayForest(const Frame &f,const ChildIndex &children,const std::vector<std::size_t> &active,Attempt attempt){
    auto walk=[&](auto &&self,std::size_t i,unsigned depth)->void {
        if(depth>32||!active[i])return;
        if(attempt(i,active[i]))return;
        for(auto c:children[i])self(self,c,depth+1);
    };
    for(std::size_t i=0;i<f.Collections.size();++i)if(f.Collections[i].Parent==-1)walk(walk,i,0);
}
// Only derived data from a single immutable decoded lease. No engine pointers,
// native resources, or native gate results are cached. Never keep the lease
// alive just for this preparation (large appearance buffers remain evictable).
class ReplayPreparation {
public:
    explicit ReplayPreparation(const std::shared_ptr<const Frame> &sample):Source(sample){
        if(!sample||!BuildChildIndex(*sample,Children_)||!BuildReplayPrerequisites(*sample,Prerequisites_))return;
        Active_=ReplaySubtreeCounts(*sample);
        IsolationReady_=RecordedIsolationReadyWithPrerequisites(*sample,Prerequisites_);
        Valid_=true;
    }
    bool Matches(const Frame &f)const{return Source.lock().get()==&f;}
    bool Valid()const{return Valid_;}
    bool IsolationReady()const{return Valid_&&IsolationReady_;}
    const ChildIndex &Children()const{return Children_;}
    const std::vector<ReplayPrerequisite> &Prerequisites()const{return Prerequisites_;}
    template<class Attempt>
    void Walk(const Frame &f,Attempt attempt)const{
        if(Valid_&&Matches(f))WalkReplayForest(f,Children_,Active_,attempt);
    }
private:
    std::weak_ptr<const Frame> Source;
    ChildIndex Children_;
    std::vector<ReplayPrerequisite> Prerequisites_;
    std::vector<std::size_t> Active_;
    bool Valid_=false,IsolationReady_=false;
};
template<class Reader>
RestorePlan PlanDetached(const Frame &f,std::size_t index,Address root,Reader read,const ChildIndex *prepared=nullptr,RestorePlan plan={}) {
    using namespace Layout;
    plan.Reset();
    auto get=[&](Address a,auto &v){return a>=0x10000&&read(a,&v,sizeof(v));};
    auto fail=[&](const char *why){if(plan.Error.empty()) plan.Error=why;};
    if(!f.Complete||!f.Appearance||index>=f.Collections.size()||!root) {fail("No recorded appearance collection");return plan;}
    ChildIndex local;
    if(!prepared){if(!BuildChildIndex(f,local)){fail("Recorded hierarchy invalid");return plan;}prepared=&local;}
    if(prepared->size()!=f.Collections.size()){fail("Recorded hierarchy index invalid");return plan;}
    std::set<Address> visited;
    auto walk=[&](auto &&self,std::size_t i,Address c,unsigned depth)->void {
        if(!plan.Error.empty()) return;
        if(!c||depth>32||!visited.insert(c).second||visited.size()>MaxCollections) {fail("Detached hierarchy invalid");return;}
        const auto &old=f.Collections[i];
        Address def=0,name=0,child=0;
        int active=0,capacity=0,maxAllowed=0;
        // These three verified fields are contiguous. Prefer one bounded read;
        // retain the original narrow reads if the platform reader rejects it.
        std::array<unsigned char,16> header{};
        bool headerOk=c>=0x10000&&read(c+Definition,header.data(),header.size());
        if(headerOk){
            std::memcpy(&def,header.data(),sizeof(def));
            std::memcpy(&capacity,header.data()+8,sizeof(capacity));
            std::memcpy(&maxAllowed,header.data()+12,sizeof(maxAllowed));
        }else headerOk=get(c+Definition,def)&&get(c+0x60,capacity)&&get(c+0x64,maxAllowed);
        if(!headerOk||!def||!get(def+DefinitionName,name)||!name||
           !get(c+Active,active)||active!=0||
           maxAllowed<0||maxAllowed>MaxDetachedActive||capacity<maxAllowed||capacity>5008||
           old.Active>static_cast<unsigned>(maxAllowed)||!get(c+ChildHead,child)) {
            fail("Detached collection capacity/state mismatch");return;
        }
        if(!old.Name[0]) {fail("Recorded definition unavailable");return;}
        // Read exactly the recorded name including its NUL.
        const auto end=std::find(old.Name.begin(),old.Name.end(),'\0');
        if(end==old.Name.end()){fail("Recorded definition name unterminated");return;}
        const auto length=std::size_t(end-old.Name.begin())+1;
        std::array<char,128> actual{};
        if(!read(name,actual.data(),length)||!std::equal(actual.begin(),actual.begin()+length,old.Name.begin())){
            fail("Detached definition name mismatch");return;
        }
        ++plan.Nodes;plan.Particles+=old.Active;plan.Bindings.push_back({c,i,def});
        if(old.Active) {
            std::array<Address,32> ptr{},stride{};
            Address memory=0;
            // The allocation base and both 32-entry attribute tables are
            // inside one bounded collection region. Copy only the fields
            // already used by the narrow path; ignore intervening bytes.
            constexpr Address TableBase=0x22e8;
            constexpr std::size_t TableBytes=std::size_t(XyzStride-TableBase)+sizeof(stride);
            static_assert(TableBytes==0x328,"verified detached table span");
            std::array<unsigned char,TableBytes> tables{};
            bool tablesOk=c>=0x10000&&c<=(std::numeric_limits<Address>::max)()-TableBase-TableBytes&&read(c+TableBase,tables.data(),tables.size());
            if(tablesOk){
                std::memcpy(&memory,tables.data(),sizeof(memory));
                std::memcpy(ptr.data(),tables.data()+std::size_t(Xyz-TableBase),sizeof(ptr));
                std::memcpy(stride.data(),tables.data()+std::size_t(XyzStride-TableBase),sizeof(stride));
            }else tablesOk=get(c+Xyz,ptr)&&get(c+XyzStride,stride)&&get(c+TableBase,memory);
            if(!tablesOk||memory<0x10000||
               memory>(std::numeric_limits<Address>::max)()-15) {
                fail("Detached attribute table unavailable");return;
            }
            Address next=(memory+15)&~Address(15);
            const auto collectionStart=plan.Copies.size();
            // Allocation lays out each varying attribute consecutively, using
            // padded capacity and the binary's component mask. Verify all slots,
            // including undocumented slots, before accepting any write.
            for(unsigned a=0;a<32;++a) {
                const auto width=4u*Components(a);
                if(stride[a]) {
                    if(stride[a]!=width||ptr[a]!=next) {fail("Detached varying buffer layout mismatch");return;}
                    const auto bytes=Address(capacity)*Components(a)*sizeof(std::uint32_t);
                    if(next>(std::numeric_limits<Address>::max)()-bytes){fail("Detached varying buffer address overflow");return;}
                    next+=bytes;
                }
                if(a>=AttributeCount) continue;
                if(!(old.Mask&(1u<<a))) {++plan.Missing;continue;}
                const auto &b=old.Attributes[a];
                const auto words=(stride[a]?(std::size_t(old.Active)+3)/4:1)*width;
                if(!ptr[a]||b.Stride!=stride[a]||b.Count!=words||
                   std::size_t(b.Start)>f.Words.size()||words>f.Words.size()-b.Start) {
                    fail("Recorded and detached attribute layouts differ");return;
                }
                if(stride[a]) AppendCopy(plan.Copies,collectionStart,{ptr[a],b.Start,b.Count});
                else {
                    std::array<std::uint32_t,12> current{};
                    if(!read(ptr[a],current.data(),words*sizeof(std::uint32_t))) {fail("Detached constant unreadable");return;}
                    ++plan.Constants;
                    if(!std::equal(current.begin(),current.begin()+words,f.Words.begin()+b.Start)) ++plan.ConstantDifferences;
                }
            }
        }
        for(const auto j:(*prepared)[i]) {
            if(j<=i||j>=f.Collections.size()||f.Collections[j].Parent!=static_cast<int>(i)){fail("Recorded child index mismatch");return;}
            Address next=0;
            if(!child||!get(child+ChildNext,next)) {fail("Detached child missing");return;}
            self(self,j,child,depth+1);child=next;
        }
        if(child) fail("Detached definition has additional children");
    };
    walk(walk,index,root,0);
    if(!plan.Error.empty()) {plan.Copies.clear();plan.Bindings.clear();}
    return plan;
}
// Store explicit fields rather than C++ object representation/padding.
// Byte-plane packing of 32-bit words improves matches among nearby floats
// and repeated integer attributes, without changing a single bit.
namespace HistoryStorage {
using Bytes=HistoryCodec::Bytes;
struct Writer {
    Bytes Data;
    void U(std::uint32_t v) {
        for(unsigned k=0;k<4;++k) Data.push_back(static_cast<std::uint8_t>(v>>(8*k)));
    }
    void F(float v) {std::uint32_t b;std::memcpy(&b,&v,4);U(b);}
    void Text(const char *s,std::size_t n) {
        U(static_cast<std::uint32_t>(n));Data.insert(Data.end(),s,s+n);
    }
};
struct Reader {
    const Bytes &Data;std::size_t Pos=0;bool Ok=true;
    std::uint32_t U() {
        if(!Ok||Data.size()-Pos<4){Ok=false;return 0;}
        std::uint32_t v=0;
        for(unsigned k=0;k<4;++k) v|=std::uint32_t(Data[Pos++])<<(8*k);
        return v;
    }
    float F(){const auto b=U();float v;std::memcpy(&v,&b,4);return v;}
    bool Text(std::string &out,std::size_t max) {
        const auto n=U();
        if(!Ok||n>max||n>Data.size()-Pos){Ok=false;return false;}
        out.assign(reinterpret_cast<const char*>(Data.data()+Pos),n);Pos+=n;return true;
    }
};
inline Bytes Pack(const Frame &f) {
    Writer w;
    w.Data.reserve(f.Points.size()*12+f.Words.size()*4+f.Collections.size()*128+128);
    w.U(static_cast<std::uint32_t>(f.Roots));w.U(static_cast<std::uint32_t>(f.Children));
    w.U(static_cast<std::uint32_t>(f.Active));w.U(static_cast<std::uint32_t>(f.Empty));w.U(static_cast<std::uint32_t>(f.Rejected));
    w.U((f.Complete?1u:0u)|(f.Appearance?2u:0u)|(f.AppearanceComplete?4u:0u));
    w.U(static_cast<std::uint32_t>(f.AppearanceRejected));w.U(static_cast<std::uint32_t>(f.NamedCollections));
    w.U(static_cast<std::uint32_t>(f.SharedConstantWords));
    w.Text(f.Status.data(),f.Status.size());
    w.U(static_cast<std::uint32_t>(f.Points.size()));
    // If all positions can be reproduced bit-for-bit from the packed XYZ
    // attribute, omit the second copy; otherwise keep the diagnostic points.
    bool derive=f.Appearance&&f.Complete&&!f.Collections.empty();
    std::size_t next=0;
    if(derive) for(const auto &c:f.Collections) {
        if(c.PointStart!=next||c.PointCount!=c.Active||next>f.Points.size()||c.Active>f.Points.size()-next){derive=false;break;}
        for(unsigned i=0;i<c.Active&&derive;++i) for(unsigned k=0;k<3;++k) {
            std::uint32_t b=0,p=0;std::memcpy(&p,&f.Points[next+i][k],4);
            if(!Word(f,c,Position,i,k,b)||p!=b){derive=false;break;}
        }
        next+=c.Active;
    }
    derive=derive&&next==f.Points.size();
    w.U(derive?1:0);
    if(!derive) for(const auto &p:f.Points) for(float v:p) w.F(v);
    w.U(static_cast<std::uint32_t>(f.Collections.size()));
    for(const auto &c:f.Collections) {
        w.U(static_cast<std::uint32_t>(c.Parent+1));w.U(c.Active);w.U(c.PointStart);w.U(c.PointCount);w.U(c.Mask);
        w.F(c.Time);w.U(c.HasTime?1:0);
        const auto end=std::find(c.Name.begin(),c.Name.end(),'\0');
        w.Text(c.Name.data(),std::size_t(end-c.Name.begin()));
        for(unsigned a=0;a<Layout::AttributeCount;++a) if(c.Mask&(1u<<a)) {
            const auto &b=c.Attributes[a];w.U(b.Start);w.U(b.Count);w.U(b.Stride);
        }
    }
    w.U(static_cast<std::uint32_t>(f.Words.size()));
    for(unsigned k=0;k<4;++k) for(auto v:f.Words) w.Data.push_back(static_cast<std::uint8_t>(v>>(8*k)));
    return std::move(w.Data);
}
inline bool Unpack(const Bytes &bytes,int tick,Frame &out) {
    Reader r{bytes};Frame f;f.Tick=tick;
    f.Roots=r.U();f.Children=r.U();f.Active=r.U();f.Empty=r.U();f.Rejected=r.U();
    const auto flags=r.U();f.Complete=(flags&1)!=0;f.Appearance=(flags&2)!=0;f.AppearanceComplete=(flags&4)!=0;
    f.AppearanceRejected=r.U();f.NamedCollections=r.U();f.SharedConstantWords=r.U();
    if(!r.Text(f.Status,1024)) return false;
    const auto points=r.U(),derive=r.U();
    if(!r.Ok||points>Layout::MaxPoints||derive>1||(!derive&&std::size_t(points)*12>bytes.size()-r.Pos))return false;
    f.Points.resize(points);
    if(!derive) for(auto &p:f.Points) for(auto &v:p) v=r.F();
    const auto collections=r.U();
    if(!r.Ok||collections>Layout::MaxCollections||collections>(bytes.size()-r.Pos)/32)return false;
    f.Collections.resize(collections);
    for(std::size_t i=0;i<collections;++i) {
        auto &c=f.Collections[i];const auto parent=r.U();
        if(parent>i)return false;
        c.Parent=static_cast<int>(parent)-1;c.Active=r.U();c.PointStart=r.U();c.PointCount=r.U();c.Mask=r.U();
        c.Time=r.F();c.HasTime=r.U()!=0;std::string name;
        if(!r.Text(name,c.Name.size()-1)||c.Active>Layout::MaxPerCollection||c.PointStart>points||c.PointCount>points-c.PointStart||
           c.Mask>>Layout::AttributeCount)return false;
        std::copy(name.begin(),name.end(),c.Name.begin());
        for(unsigned a=0;a<Layout::AttributeCount;++a) if(c.Mask&(1u<<a)) {
            auto &b=c.Attributes[a];b.Start=r.U();b.Count=r.U();const auto stride=r.U();
            if(stride!=0&&stride!=4*Components(a))return false;
            b.Stride=static_cast<std::uint8_t>(stride);
        }
    }
    const auto words=r.U();
    if(!r.Ok||words>Layout::WordLimit||std::size_t(words)*4!=bytes.size()-r.Pos)return false;
    f.Words.resize(words);
    for(unsigned k=0;k<4;++k) for(auto &v:f.Words)v|=std::uint32_t(bytes[r.Pos++])<<(8*k);
    std::size_t next=0;
    for(const auto &c:f.Collections) {
        for(unsigned a=0;a<Layout::AttributeCount;++a) if(c.Mask&(1u<<a)) {
            const auto &b=c.Attributes[a];
            const auto expected=(b.Stride?(std::size_t(c.Active)+3)/4:1)*4*Components(a);
            if(b.Start>words||b.Count>words-b.Start||b.Count!=expected)return false;
        }
        if(derive) {
            if(c.PointStart!=next||c.PointCount!=c.Active||c.Active>points-next)return false;
            for(unsigned i=0;i<c.Active;++i) for(unsigned k=0;k<3;++k) {
                std::uint32_t b;
                if(!Word(f,c,Position,i,k,b))return false;
                std::memcpy(&f.Points[next+i][k],&b,4);
            }
            next+=c.Active;
        }
    }
    if(derive&&next!=points)return false;
    out=std::move(f);return true;
}
struct Stored {
    int Tick=-1;
    std::size_t RawBytes=0,PlainSize=0;
    bool Compressed=false;
    Bytes Data;
    std::size_t BytesUsed()const{return sizeof(Stored)+Data.capacity();}
};
inline Stored Store(const Frame &f) {
    Stored s;s.Tick=f.Tick;s.RawBytes=f.Bytes();
    auto plain=Pack(f);s.PlainSize=plain.size();
    auto compressed=HistoryCodec::Encode(plain);
    s.Compressed=compressed.size()<plain.size();
    const auto &source=s.Compressed?compressed:plain;
    s.Data.assign(source.begin(),source.end());
    return s;
}
inline bool Load(const Stored &s,Frame &f) {
    // Bounded by the capture format's point/word/collection limits.
    constexpr std::size_t MaxPlain=32u*1024u*1024u;
    if(s.PlainSize>MaxPlain)return false;
    if(!s.Compressed)return s.Data.size()==s.PlainSize&&Unpack(s.Data,s.Tick,f);
    Bytes plain;
    return HistoryCodec::Decode(s.Data,s.PlainSize,plain)&&Unpack(plain,s.Tick,f);
}
}
// A root-first fallback traversal. Successful attempts own their entire
// subtree; failed attempts descend to children without double-submitting.
template<class Attempt>
void ReplayForest(const Frame &f,Attempt attempt,const ChildIndex *prepared=nullptr) {
    if(f.Collections.size()>Layout::MaxCollections)return;
    ChildIndex local;
    if(!prepared){if(!BuildChildIndex(f,local))return;prepared=&local;}
    if(prepared->size()!=f.Collections.size())return;
    const auto &children=*prepared;
    // A supplied index must still describe exactly the immutable frame's
    // edges. Check before attempting any native submission.
    std::size_t expectedEdges=0,actualEdges=0;
    for(std::size_t i=0;i<f.Collections.size();++i){
        const auto parent=f.Collections[i].Parent;
        if(parent!=-1){if(parent<0||static_cast<std::size_t>(parent)>=i)return;++expectedEdges;}
        std::size_t previous=i;
        for(auto child:children[i]){
            if(child<=previous||child>=f.Collections.size()||f.Collections[child].Parent!=static_cast<int>(i))return;
            previous=child;++actualEdges;
        }
    }
    if(actualEdges!=expectedEdges)return;
    WalkReplayForest(f,children,ReplaySubtreeCounts(f),attempt);
}
// Exact modern particle manager render-accounting fields: two signed
// 32-bit counters followed by a one-byte over-budget flag. No padding or
// neighboring manager state is included in this snapshot.
using GeometryAccounting=std::array<unsigned char,9>;
inline bool GeometryAccountingValid(const GeometryAccounting &a){
    std::int32_t first=0,second=0;
    std::memcpy(&first,a.data(),4);std::memcpy(&second,a.data()+4,4);
    return first>=0&&second>=0&&a[8]<=1;
}
enum class GeometryAccountingAction {Unchanged,Restore,Uncertain};
inline GeometryAccountingAction CompareGeometryAccounting(const GeometryAccounting &before,const GeometryAccounting &after){
    if(!GeometryAccountingValid(before)||!GeometryAccountingValid(after))return GeometryAccountingAction::Uncertain;
    if(before==after)return GeometryAccountingAction::Unchanged;
    for(unsigned i=0;i<8;i+=4){
        std::int32_t a=0,b=0;std::memcpy(&a,before.data()+i,4);std::memcpy(&b,after.data()+i,4);
        if(b<a)return GeometryAccountingAction::Uncertain; // native reset or overflow
    }
    if(after[8]<before[8])return GeometryAccountingAction::Uncertain;
    return GeometryAccountingAction::Restore;
}
// Read-only census of attempted detached collection shapes during the
// existing immediate-mode benchmark warm-up. None of these counts authorize
// a queued draw: ownership and the remaining renderer call graph still matter.
// A read-only witness from registered collections, before any replay factory
// invocation. The name is owned; addresses never escape this audit.
struct SpriteFactoryShape {
    bool SingleSprite=false,OrientationZero=false,Leaf=false,KnownInitializers=false;
    bool ScratchBounded=false,NoSheet=false,NoPost=false;
    bool operator==(const SpriteFactoryShape &b) const {
        return SingleSprite==b.SingleSprite&&OrientationZero==b.OrientationZero&&Leaf==b.Leaf&&
            KnownInitializers==b.KnownInitializers&&ScratchBounded==b.ScratchBounded&&NoSheet==b.NoSheet&&NoPost==b.NoPost;
    }
    bool Narrow()const{return SingleSprite&&OrientationZero&&Leaf&&KnownInitializers&&ScratchBounded&&NoSheet&&NoPost;}
};
template<class Reader>
std::map<std::string,SpriteFactoryShape> AuditRegisteredSpriteFactories(Address module,Reader read,bool &complete){
    using namespace Layout;
    complete=false;
    std::map<std::string,SpriteFactoryShape> result;
    std::map<std::string,Address> definitions;
    std::set<std::string> ambiguous;
    std::set<Address> visited,roots;
    auto get=[&](Address a,Address off,auto &v){
        return a>=0x10000&&a<=(std::numeric_limits<Address>::max)()-off&&
            a+off<=(std::numeric_limits<Address>::max)()-sizeof(v)&&read(a+off,&v,sizeof(v));
    };
    if(module>(std::numeric_limits<Address>::max)()-0x1400000)return {};
    std::uint32_t ready=0;Address root=0;
    if(!get(module,ManagerReady,ready)||!(ready&1)||!get(module,Manager+RootHead,root))return {};
    auto walk=[&](auto &&self,Address c,unsigned depth)->bool{
        if(depth>32||visited.size()>=MaxCollections||!visited.insert(c).second)return false;
        Address def=0,name=0,child=0;
        if(!get(c,Definition,def)||!get(def,DefinitionName,name)||!get(c,ChildHead,child))return false;
        std::string key;
        for(unsigned i=0;i<128;++i){
            char ch=0;
            if(!get(name,i,ch))return false;
            if(!ch)break;
            key+=ch;
        }
        if(key.empty()||key.size()==128)return false;
        SpriteFactoryShape shape;
        int childCount=-1;Address scratchSize=0,sheet=1,post=1;
        shape.Leaf=get(def,0x318,childCount)&&childCount==0&&!child;
        shape.NoSheet=get(c,0x10,sheet)&&!sheet;
        shape.NoPost=get(c,0x2788,post)&&!post;
        Address sprite=0;bool known=true,bounded=get(def,0x218,scratchSize)&&scratchSize<=0x100000;
        // Six operator initializer lists, in the actual constructor order.
        // Only the exact no-op and sprite initializer are recognized.
        for(unsigned i=0;i<6;++i){
            const Address off=0x248+i*0x20;
            int count=-1;Address array=0,offsets=0;
            if(!get(def,off+0x10,count)||count<0||count>256){known=bounded=false;break;}
            if(!count)continue;
            if(!get(def,off,array)||!get(def,0x328+i*0x20,offsets)){known=bounded=false;break;}
            for(int j=0;j<count;++j){
                Address op=0,vt=0,init=0,render=0,offset=0;
                if(!get(array,Address(j)*8,op)||!get(op,0,vt)||!get(vt,0x10,init)){known=bounded=false;break;}
                const bool exactSprite=i==1&&vt==module+0xe0a2b8&&
                    get(vt,0x48,render)&&render==module+0x8f5a00&&init==module+0x8f5950;
                if(i==1&&count==1&&exactSprite)sprite=op;
                if(!exactSprite&&init!=module+0x621c0)known=false;
                if(!get(offsets,Address(j)*8,offset)||offset>scratchSize||
                    (exactSprite&&scratchSize-offset<0x15))bounded=false;
            }
        }
        shape.SingleSprite=sprite!=0;
        int orientation=-1;
        shape.OrientationZero=sprite&&get(sprite,0x74,orientation)&&orientation==0;
        shape.KnownInitializers=known;shape.ScratchBounded=bounded;
        auto inserted=result.emplace(key,shape);
        auto d=definitions.emplace(key,def);
        if(!inserted.second&&(!(inserted.first->second==shape)||d.first->second!=def))ambiguous.insert(key);
        while(child){
            Address next=0;
            if(!get(child,ChildNext,next)||!self(self,child,depth+1))return false;
            child=next;
        }
        return true;
    };
    Address prev=0;
    while(root){
        Address vt=0,next=0,back=0;
        if(roots.size()>=MaxCollections||!roots.insert(root).second||!get(root,0,vt)||vt!=module+RootVtable||
            !get(root,RootNext,next)||!get(root,RootPrev,back)||back!=prev||
            root>(std::numeric_limits<Address>::max)()-RootCollection||!walk(walk,root+RootCollection,0))return {};
        prev=root;root=next;
    }
    for(const auto &name:ambiguous)result.erase(name);
    complete=true;
    return result;
}
struct QueuedSpriteAudit {
    std::size_t Nodes=0,Particles=0,SingleSprite=0,NoAuxiliary=0,NoPost=0,MaterialReady=0;
    bool FactoryWitnessComplete=false;
    std::size_t WitnessMatched=0,WitnessLeaf=0,WitnessKnownInitializers=0,WitnessScratch=0,WitnessNoSheet=0,WitnessNarrow=0,WitnessParticles=0;
    void ObserveFactory(const Frame &frame,const std::map<std::string,SpriteFactoryShape> &shapes){
        for(const auto &c:frame.Collections){
            if(!c.Active)continue;
            const auto end=std::find(c.Name.begin(),c.Name.end(),'\0');
            if(end==c.Name.end())continue;
            const auto it=shapes.find(std::string(c.Name.begin(),end));
            if(it==shapes.end())continue;
            ++WitnessMatched;const auto &s=it->second;
            if(s.Leaf)++WitnessLeaf;
            if(s.KnownInitializers)++WitnessKnownInitializers;
            if(s.ScratchBounded)++WitnessScratch;
            if(s.NoSheet)++WitnessNoSheet;
            if(s.Narrow()){++WitnessNarrow;WitnessParticles+=c.Active;}
        }
    }
    std::array<std::size_t,4> Orientation{};
    std::size_t OtherOrientation=0,UnknownOrientation=0,NarrowNodes=0,NarrowParticles=0;
    void Observe(std::size_t active,bool singleSprite,int orientation,bool noAuxiliary,bool noPost,bool materialReady){
        if(!active)return;
        ++Nodes;Particles+=active;
        if(!singleSprite)return;
        ++SingleSprite;
        if(orientation<0)++UnknownOrientation;
        else if(orientation<4)++Orientation[static_cast<std::size_t>(orientation)];
        else ++OtherOrientation;
        if(noAuxiliary)++NoAuxiliary;
        if(noPost)++NoPost;
        if(materialReady)++MaterialReady;
        if(orientation==0&&noAuxiliary&&noPost&&materialReady){++NarrowNodes;NarrowParticles+=active;}
    }
    std::string Describe()const{
        return "Attempted active nodes "+std::to_string(Nodes)+", particles "+std::to_string(Particles)+
            "; single exact sprite "+std::to_string(SingleSprite)+
            "; orientation 0/1/2/3/other/unknown "+std::to_string(Orientation[0])+"/"+std::to_string(Orientation[1])+
            "/"+std::to_string(Orientation[2])+"/"+std::to_string(Orientation[3])+
            "/"+std::to_string(OtherOrientation)+"/"+std::to_string(UnknownOrientation)+
            "; no auxiliary scratch "+std::to_string(NoAuxiliary)+", no post callback "+std::to_string(NoPost)+
            ", ordinary precached proxy-free material "+std::to_string(MaterialReady)+
            "; narrow investigation shape "+std::to_string(NarrowNodes)+" nodes / "+std::to_string(NarrowParticles)+
            " particles (not queued eligibility or draw coverage)."+
            " Pre-factory live witness "+std::string(FactoryWitnessComplete?"complete":"unavailable")+
            ": matched "+std::to_string(WitnessMatched)+", leaf "+std::to_string(WitnessLeaf)+
            ", known initializers "+std::to_string(WitnessKnownInitializers)+", scratch bounded "+std::to_string(WitnessScratch)+
            ", no sheet "+std::to_string(WitnessNoSheet)+", narrow "+std::to_string(WitnessNarrow)+
            " nodes / "+std::to_string(WitnessParticles)+" particles (diagnostic only).";
    }
};
struct ReplayResult {
    bool MainView=false;
    std::size_t Attempts=0,Trees=0,Nodes=0,Particles=0,Rejected=0,PreflightRejected=0;
    std::string Status;
    double FactoryMs=0,PlanMs=0,CopyMs=0,RenderMs=0,DestroyMs=0,GateMs=0,ActivationMs=0,ResetMs=0;
    std::size_t FastWrites=0,SystemWrites=0,WriteFaults=0,CleanupFailures=0;
    std::size_t FastReads=0,SystemReads=0,ReadFaults=0;
    bool GeometryAccountingRestored=false;
    std::vector<std::uint32_t> RendererTargets,PostRenderTargets;
    QueuedSpriteAudit SpriteAudit;
};
class History {
public:
    explicit History(std::size_t budget=512u*1024u*1024u):Budget(budget){}
    void Clear(){Frames.clear();Bytes=RawBytes=PlainBytes=0;Evicted=Rejected=0;Invalidate();++Revision;}
    std::size_t BudgetBytes() const {return Budget;}
    std::size_t DecodedBytes() const{return Decoded?Decoded->Bytes():0;}
    void SetBudget(std::size_t budget) {Budget=budget;while(!Frames.empty()&&Bytes>Budget) Evict();Invalidate();++Revision;}
    bool Append(Frame f) {
        // A rejected sample cannot erase a valid interval or decoded lease.
        // Incomplete but storable modern snapshots remain diagnostic data.
        if(f.Tick<0){++Rejected;return false;}
        auto stored=HistoryStorage::Store(f);
        const auto n=stored.BytesUsed();
        if(n>Budget){++Rejected;return false;}
        if(!Frames.empty()&&f.Tick<Frames.back().Tick) Clear();
        Invalidate();
        if(!Frames.empty()&&Frames.back().Tick==f.Tick) {Subtract(Frames.back());Frames.pop_back();}
        while(!Frames.empty()&&(Bytes>Budget-n||Frames.size()>=32768)) Evict();
        Bytes+=n;RawBytes+=stored.RawBytes;PlainBytes+=stored.PlainSize;
        Frames.push_back(std::move(stored));++Revision;return true;
    }
    // Returned sample is valid until the next different sample request or
    // history mutation. All production consumers hold the scene mutex.
    const Frame *At(int tick) const {
        auto it=std::lower_bound(Frames.begin(),Frames.end(),tick,[](const auto &f,int t){return f.Tick<t;});
        return it!=Frames.end()&&it->Tick==tick?Read(*it):nullptr;
    }
    const Frame *ForPreview(int tick,int maxGap=2) const {
        return ShareForPreview(tick,maxGap).get();
    }
    // Immutable owning lease survives later decode, replacement and clear.
    // Obtain under the caller's history mutex.
    std::shared_ptr<const Frame> ShareForPreview(int tick,int maxGap=2) const {
        if(Frames.empty()||tick<Frames.front().Tick||tick>Frames.back().Tick) return nullptr;
        auto it=std::upper_bound(Frames.begin(),Frames.end(),tick,[](int t,const auto &f){return t<f.Tick;});
        if(it==Frames.begin())return nullptr;
        --it;return tick-it->Tick<=maxGap?ReadShared(*it):nullptr;
    }
    std::deque<HistoryStorage::Stored> Frames;
    std::size_t Bytes=0,RawBytes=0,PlainBytes=0,Evicted=0,Rejected=0;
    std::uint64_t Revision=0;
private:
    mutable std::shared_ptr<const Frame> Decoded;
    void Invalidate(){Decoded.reset();}
    std::shared_ptr<const Frame> ReadShared(const HistoryStorage::Stored &s) const {
        if(Decoded&&Decoded->Tick==s.Tick)return Decoded;
        auto next=std::make_shared<Frame>();
        if(!HistoryStorage::Load(s,*next)){Decoded.reset();return {};}
        Decoded=std::move(next);return Decoded;
    }
    const Frame *Read(const HistoryStorage::Stored &s) const {return ReadShared(s).get();}
    void Subtract(const HistoryStorage::Stored &s){Bytes-=s.BytesUsed();RawBytes-=s.RawBytes;PlainBytes-=s.PlainSize;}
    void Evict(){Subtract(Frames.front());Frames.pop_front();++Evicted;}
    std::size_t Budget;
};
}} // namespace
