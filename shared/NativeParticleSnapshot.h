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
#include <string>
#include <vector>

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
constexpr int MaxPerCollection=65536;
}
using Point=std::array<float,3>;
// SDK attribute indices. Native tables store 32 entries; only 22 documented
// slots have known semantics, and their original 32-bit words are preserved.
enum Attribute : unsigned { Position=0, Life=1, Previous=2, Radius=3, Rotation=4,
    RotationSpeed=5, Tint=6, Alpha=7, Created=8, Sequence=9, TrailLength=10,
    ParticleId=11, Yaw=12, Sequence2=13, HitboxIndex=14, HitboxRelative=15,
    Alpha2=16, TraceP0=17, TraceP1=18, TraceHitT=19, TraceNormal=20, ControlPointIndex=21 };
inline unsigned Components(unsigned a) {
    return a==Position||a==Previous||a==Tint||a==HitboxRelative||a==TraceP0||
           a==TraceP1||a==TraceNormal ? 3u:1u;
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
    std::size_t AppearanceRejected=0, NamedCollections=0;
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
    auto bad=[&](){++f.Rejected;f.Complete=false;};
    auto appearanceBad=[&](){++f.AppearanceRejected;f.AppearanceComplete=false;};
    auto copyAppearance=[&](Address c,Collection &item) {
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
        if(!get(c+Xyz,pointers)||!get(c+XyzStride,strides)) {appearanceBad();return;}
        for(unsigned a=0;a<AttributeCount;++a) {
            const auto ptr=pointers[a],stride=strides[a];
            const auto width=std::size_t(4*Components(a));
            // Unused slots may be null. Record absence, not invented defaults.
            if(!ptr) {if(stride) appearanceBad();continue;}
            if(ptr<0x10000||(stride!=0&&stride!=width)) {appearanceBad();continue;}
            const auto words=(stride?(std::size_t(item.Active)+3)/4:1)*width;
            if(words>WordLimit-f.Words.size()) {appearanceBad();continue;}
            const auto start=f.Words.size();
            f.Words.resize(start+words);
            if(!read(ptr,f.Words.data()+start,words*sizeof(std::uint32_t))) {
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
        if(!count) ++f.Empty;
        else {
            Address xyz=0,stride=0;
            // Position uses four-wide SoA blocks, 12 floats per block; a
            // zero stride is the SDK's shared constant-attribute representation.
            if(!get(c+Xyz,xyz)||!get(c+XyzStride,stride)||xyz<0x10000||
               (stride!=12 && stride!=0)||f.Points.size()+std::size_t(count)>MaxPoints) bad();
            else {
                const auto blocks=(std::size_t(count)+3)/4;
                packed.resize((stride?blocks:1)*12);
                if(!read(xyz,packed.data(),packed.size()*sizeof(float))) bad();
                else {
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
            copyAppearance(c,item);
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
    f.Status=f.Complete?"Native position snapshot":"Incomplete native position snapshot";
    return f;
}
class History {
public:
    explicit History(std::size_t budget=64u*1024u*1024u):Budget(budget){}
    void Clear(){Frames.clear();Bytes=0;++Revision;}
    bool Append(Frame f) {
        if(!Frames.empty()&&f.Tick<Frames.back().Tick) Clear();
        if(f.Tick<0||f.Bytes()>Budget) return false;
        if(!Frames.empty()&&Frames.back().Tick==f.Tick) {Bytes-=Frames.back().Bytes();Frames.pop_back();}
        const auto n=f.Bytes();
        while(!Frames.empty()&&(Bytes>Budget-n||Frames.size()>=4096)) {Bytes-=Frames.front().Bytes();Frames.pop_front();}
        Frames.push_back(std::move(f));Bytes+=Frames.back().Bytes();++Revision;return true;
    }
    const Frame *At(int tick) const {
        auto it=std::lower_bound(Frames.begin(),Frames.end(),tick,[](const Frame &f,int t){return f.Tick<t;});
        return it!=Frames.end()&&it->Tick==tick?&*it:nullptr;
    }
    std::deque<Frame> Frames;
    std::size_t Bytes=0;
    std::uint64_t Revision=0;
private:
    std::size_t Budget;
};
}} // namespace
