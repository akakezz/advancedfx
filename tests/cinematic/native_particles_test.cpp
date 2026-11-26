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
    // Restore root list and positions; synthesize the indexed attribute arrays.
    put(root,base+RootVtable);put(coll+XyzStride,Address(12));
    constexpr Address def=0x50000000,name=0x51000000,radius=0x52000000,tint=0x53000000,ids=0x54000000;
    char text[128]="test_effect";
    put(def+DefinitionName,name);put(name,text);
    put(coll+Definition,def);put(child+Definition,def);put(coll+Clock,2.5f);put(child+Clock,1.5f);
    std::array<Address,AttributeCount> pointers{},strides{};
    pointers[Position]=xyz;strides[Position]=12;
    pointers[Radius]=radius;strides[Radius]=4;
    pointers[Tint]=tint;strides[Tint]=0;
    pointers[ParticleId]=ids;strides[ParticleId]=4;
    const float radii[8]={1,2,3,4,5,6,7,8};
    const float colors[12]={.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f,.9f,1,.1f,.2f};
    const std::uint32_t idWords[8]={1,2,3,4,0x7fc00000u,6,7,8};
    put(radius,radii);put(tint,colors);put(ids,idWords);
    put(coll+Xyz,pointers);put(coll+XyzStride,strides);
    strides[Position]=0;put(child+Xyz,pointers);put(child+XyzStride,strides);
    auto app=Capture(base,20,read,true);
    check(app.Complete&&app.AppearanceComplete&&app.Collections.size()==2&&app.NamedCollections==2,"full appearance metadata");
    check(app.Collections[0].Parent==-1&&app.Collections[1].Parent==0&&app.Collections[1].PointStart==5,"frame-local hierarchy");
    check(std::string(app.Collections[0].Name.data())=="test_effect"&&app.Collections[0].HasTime&&app.Collections[0].Time==2.5f,"owned definition name and clock");
    float v=0;std::uint32_t bits=0;
    check(Float(app,app.Collections[0],Radius,4,0,v)&&v==5,"varying scalar second block");
    check(Float(app,app.Collections[0],Tint,4,2,v)&&v==.9f,"constant vec3 repeated across blocks");
    check(Word(app,app.Collections[0],ParticleId,4,0,bits)&&bits==0x7fc00000u,"integer bit pattern retained");
    v=17;check(!Float(app,app.Collections[0],ParticleId,4,0,v)&&v==17,"invalid float preserves caller fallback");
    check(!Word(app,app.Collections[0],Alpha,0,0,bits)&&!Word(app,app.Collections[0],Radius,5,0,bits),"unavailable and out-of-range fields");
    put(radius,99.0f);check(Float(app,app.Collections[0],Radius,0,0,v)&&v==1,"independent attribute storage");
    put(coll+XyzStride+8*Radius,Address(99));auto partial=Capture(base,21,read,true);
    check(partial.Complete&&!partial.AppearanceComplete&&partial.AppearanceRejected&&partial.Points.size()==7,"appearance failure preserves positions");
    auto old=Capture(base,22,read,false);check(old.Complete&&!old.Appearance&&old.Collections.empty()&&old.Words.empty(),"position-only fallback stays opt-in");
    check(app.Bytes()>old.Bytes(),"appearance included in rolling payload budget");
    std::cout<<"Native particle snapshot tests passed\n";
}
