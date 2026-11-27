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
            if(!stride) {
                std::array<std::uint32_t,13> key{};
                key[0]=static_cast<std::uint32_t>(words);
                if(!read(ptr,key.data()+1,words*sizeof(std::uint32_t))) {appearanceBad();continue;}
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
struct RestoreBinding {Address Native=0;std::size_t Recorded=0;};
struct RestorePlan {
    std::vector<CopyOp> Copies;
    std::vector<RestoreBinding> Bindings;
    std::size_t Nodes=0,Particles=0,Constants=0,ConstantDifferences=0,Missing=0;
    std::string Error;
};
template<class Reader>
RestorePlan PlanDetached(const Frame &f,std::size_t index,Address root,Reader read) {
    using namespace Layout;
    RestorePlan plan;
    auto get=[&](Address a,auto &v){return a>=0x10000&&read(a,&v,sizeof(v));};
    auto fail=[&](const char *why){if(plan.Error.empty()) plan.Error=why;};
    if(!f.Complete||!f.Appearance||index>=f.Collections.size()||!root) {fail("No recorded appearance collection");return plan;}
    std::set<Address> visited;
    auto walk=[&](auto &&self,std::size_t i,Address c,unsigned depth)->void {
        if(!plan.Error.empty()) return;
        if(!c||depth>32||!visited.insert(c).second||visited.size()>MaxCollections) {fail("Detached hierarchy invalid");return;}
        const auto &old=f.Collections[i];
        Address def=0,name=0,child=0;
        int active=0,capacity=0,maxAllowed=0;
        if(!get(c+Definition,def)||!def||!get(def+DefinitionName,name)||!name||
           !get(c+Active,active)||active!=0||!get(c+0x60,capacity)||!get(c+0x64,maxAllowed)||
           maxAllowed<0||maxAllowed>5000||capacity<maxAllowed||capacity>5008||
           old.Active>static_cast<unsigned>(maxAllowed)||!get(c+ChildHead,child)) {
            fail("Detached collection capacity/state mismatch");return;
        }
        if(!old.Name[0]) {fail("Recorded definition unavailable");return;}
        for(std::size_t k=0;k<old.Name.size();++k) {
            char ch=0;
            if(!get(name+k,ch)||ch!=old.Name[k]) {fail("Detached definition name mismatch");return;}
            if(!ch) break;
        }
        ++plan.Nodes;plan.Particles+=old.Active;plan.Bindings.push_back({c,i});
        if(old.Active) {
            std::array<Address,32> ptr{},stride{};
            Address memory=0;
            if(!get(c+Xyz,ptr)||!get(c+XyzStride,stride)||!get(c+0x22e8,memory)||memory<0x10000) {
                fail("Detached attribute table unavailable");return;
            }
            Address next=(memory+15)&~Address(15);
            // Allocation lays out each varying attribute consecutively, using
            // padded capacity and the binary's component mask. Verify all slots,
            // including undocumented slots, before accepting any write.
            for(unsigned a=0;a<32;++a) {
                const auto width=4u*Components(a);
                if(stride[a]) {
                    if(stride[a]!=width||ptr[a]!=next) {fail("Detached varying buffer layout mismatch");return;}
                    next+=Address(capacity)*Components(a)*sizeof(std::uint32_t);
                }
                if(a>=AttributeCount) continue;
                if(!(old.Mask&(1u<<a))) {++plan.Missing;continue;}
                const auto &b=old.Attributes[a];
                const auto words=(stride[a]?(std::size_t(old.Active)+3)/4:1)*width;
                if(!ptr[a]||b.Stride!=stride[a]||b.Count!=words||
                   std::size_t(b.Start)>f.Words.size()||words>f.Words.size()-b.Start) {
                    fail("Recorded and detached attribute layouts differ");return;
                }
                if(stride[a]) plan.Copies.push_back({ptr[a],b.Start,b.Count});
                else {
                    std::array<std::uint32_t,12> current{};
                    if(!read(ptr[a],current.data(),words*sizeof(std::uint32_t))) {fail("Detached constant unreadable");return;}
                    ++plan.Constants;
                    if(!std::equal(current.begin(),current.begin()+words,f.Words.begin()+b.Start)) ++plan.ConstantDifferences;
                }
            }
        }
        for(std::size_t j=i+1;j<f.Collections.size();++j) if(f.Collections[j].Parent==static_cast<int>(i)) {
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
void ReplayForest(const Frame &f,Attempt attempt) {
    if(f.Collections.size()>Layout::MaxCollections)return;
    std::vector<std::vector<std::size_t>> children(f.Collections.size());
    std::vector<std::size_t> roots;
    for(std::size_t i=0;i<f.Collections.size();++i) {
        const auto parent=f.Collections[i].Parent;
        if(parent==-1)roots.push_back(i);
        else if(parent>=0&&static_cast<std::size_t>(parent)<i)children[parent].push_back(i);
        else return;
    }
    std::vector<std::size_t> active(f.Collections.size());
    for(std::size_t i=f.Collections.size();i--;) {
        active[i]+=f.Collections[i].Active;
        const auto parent=f.Collections[i].Parent;
        if(parent>=0)active[parent]+=active[i];
    }
    auto walk=[&](auto &&self,std::size_t i,unsigned depth)->void {
        if(depth>32||!active[i])return;
        if(attempt(i,active[i]))return;
        for(auto c:children[i])self(self,c,depth+1);
    };
    for(auto i:roots)walk(walk,i,0);
}
struct ReplayResult {
    bool MainView=false;
    std::size_t Attempts=0,Trees=0,Nodes=0,Particles=0,Rejected=0;
    std::string Status;
};
class History {
public:
    explicit History(std::size_t budget=512u*1024u*1024u):Budget(budget){}
    void Clear(){Frames.clear();Bytes=RawBytes=PlainBytes=0;Evicted=Rejected=0;Invalidate();++Revision;}
    std::size_t BudgetBytes() const {return Budget;}
    std::size_t DecodedBytes() const{return Decoded?Decoded->Bytes():0;}
    void SetBudget(std::size_t budget) {Budget=budget;while(!Frames.empty()&&Bytes>Budget) Evict();Invalidate();++Revision;}
    bool Append(Frame f) {
        if(!Frames.empty()&&f.Tick<Frames.back().Tick) Clear();
        if(f.Tick<0){++Rejected;return false;}
        auto stored=HistoryStorage::Store(f);
        const auto n=stored.BytesUsed();
        if(n>Budget){++Rejected;return false;}
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
