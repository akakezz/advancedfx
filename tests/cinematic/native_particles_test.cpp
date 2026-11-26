#include "shared/NativeParticleSnapshot.h"
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
using namespace advancedfx::NativeParticles;
static void check(bool b,const char *s){if(!b){std::cerr<<s<<"\n";std::exit(1);}}
int main(){
    using namespace Layout;
    constexpr Address base=0x10000000, root=0x20000000, child=0x30000000, xyz=0x40000000;
    std::map<Address,unsigned char> bytes;
    auto put=[&](Address a,const auto &v){const auto *p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i)bytes[a+i]=p[i];};
    auto read=[&](Address a,void *out,std::size_t n){auto *p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto it=bytes.find(a+i);if(it==bytes.end())return false;p[i]=it->second;}return true;};
    put(base+ManagerReady,std::uint32_t(1));put(base+Manager+RootHead,root);
    put(root,base+RootVtable);put(root+RootNext,Address(0));put(root+RootPrev,Address(0));
    const auto coll=root+RootCollection;
    put(coll+Active,5);put(coll+ChildHead,child);put(coll+Xyz,xyz);put(coll+XyzStride,Address(12));
    const float p[24]={1,2,3,4,10,20,30,40,100,200,300,400,5,6,7,8,50,60,70,80,500,600,700,800};
    put(xyz,p);
    put(child+Active,2);put(child+ChildHead,Address(0));put(child+ChildNext,Address(0));put(child+Xyz,xyz);put(child+XyzStride,Address(0));
    auto f=Capture(base,10,read);
    check(f.Complete&&f.Roots==1&&f.Children==1&&f.Active==7&&f.Points.size()==7,"root and child coverage");
    check(f.Points[4]==Point{5,50,500}&&f.Points[6]==Point{2,20,200},"four-wide xyz and constant stride");
    History h(4096);check(h.Append(f)&&h.At(10)&&!h.At(11),"exact tick history");
    put(xyz,0.0f);auto newer=Capture(base,11,read);check(h.Append(newer)&&h.At(10)->Points[0][0]==1&&h.At(11)->Points[0][0]==0,"owned history");
    put(child+ChildNext,child);auto cycle=Capture(base,12,read);check(!cycle.Complete&&cycle.Rejected,"child cycle bounded");
    put(child+ChildNext,Address(0));put(coll+XyzStride,Address(99));auto invalid=Capture(base,12,read);check(!invalid.Complete&&invalid.Points.size()==2,"unexpected stride rejected");
    put(root,Address(0));check(!Capture(base,13,read).Complete,"root vtable guard");
    check(h.Append(f)&&!h.At(11),"backward tick resets history");
    History small(f.Bytes()+8);check(small.Append(f)&&small.Append(newer)&&small.Frames.size()==1&&small.At(11),"rolling budget");
    std::cout<<"Native particle snapshot tests passed\n";
}
