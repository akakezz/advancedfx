#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <deque>
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
constexpr std::size_t MaxCollections=4096, MaxPoints=262144;
constexpr int MaxPerCollection=65536;
}
using Point=std::array<float,3>;
struct Frame {
    int Tick=-1;
    std::size_t Roots=0, Children=0, Active=0, Empty=0, Rejected=0;
    bool Complete=false;
    std::string Status;
    std::vector<Point> Points;
    std::size_t Bytes() const {return sizeof(Frame)+Points.capacity()*sizeof(Point)+Status.capacity();}
};
// Reader signature: bool(address, destination, size). Caller must establish a
// quiescent engine-thread capture boundary and verify module identity.
template<class Reader>
Frame Capture(Address module,int tick,Reader read) {
    using namespace Layout;
    Frame f; f.Tick=tick;
    auto get=[&](Address a,auto &v){return a>=0x10000 && read(a,&v,sizeof(v));};
    std::uint32_t ready=0;
    Address root=0;
    if(!get(module+ManagerReady,ready) || !(ready&1) ||
       !get(module+Manager+RootHead,root)) {f.Status="Manager not initialized/readable";return f;}
    f.Complete=true;
    std::set<Address> roots, collections;
    std::vector<float> packed;
    auto bad=[&](){++f.Rejected;f.Complete=false;};
    auto walk=[&](auto &&self,Address c,bool child,unsigned depth)->void {
        if(!c || depth>32 || collections.size()>=MaxCollections || !collections.insert(c).second) {bad();return;}
        if(child) ++f.Children;
        int count=0; Address children=0;
        if(!get(c+Active,count)||!get(c+ChildHead,children)||count<0||count>MaxPerCollection) {bad();return;}
        f.Active+=std::size_t(count);
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
        while(children) {
            if(collections.size()>=MaxCollections || collections.count(children) || depth>=32) {bad();break;}
            Address next=0;
            if(!get(children+ChildNext,next)) {bad();break;}
            self(self,children,true,depth+1);
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
        walk(walk,root+RootCollection,false,0);
        previous=root;root=next;
    }
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
