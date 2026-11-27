#include "shared/LegacyParticleSnapshot.h"
#include <cassert>
#include <iostream>
#include <map>
using namespace advancedfx::LegacyParticles;
int main(){
    auto unique=UniqueMaterialNames({{0x10000,"spark"},{0x20000,"named"},{0x30000,"named"},{0x40000,"other"}});
    assert(unique.at("spark")==0x10000&&unique.at("named")==0&&unique.at("other")==0x40000);
    assert(QueuedMaterialPageAllowed(Material::Spark,0x10000,0,0));
    assert(QueuedMaterialPageAllowed(Material::CombineMuzzle,0x20000,0,0));
    assert(QueuedMaterialPageAllowed(Material::Named,0x10000,0x10000,0x20000));
    assert(QueuedMaterialPageAllowed(Material::Named,0x20000,0x10000,0x20000));
    assert(!QueuedMaterialPageAllowed(Material::Named,0x30000,0x10000,0x20000));
    assert(!QueuedMaterialPageAllowed(Material::Named,0,0,0));
    assert(!QueuedMaterialPageAllowed(Material::Unknown,0x10000,0x10000,0x20000));
    std::map<Address,unsigned char> memory;
    auto put=[&](Address a,const auto &v){const auto *p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t i=0;i<sizeof(v);++i)memory[a+i]=p[i];};
    auto read=[&](Address a,void *out,std::size_t n){auto *p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto it=memory.find(a+i);if(it==memory.end())return false;p[i]=it->second;}return true;};
    constexpr Address binding=0x10000,group=0x20000,sentinel=group+8,particle=0x30000,sub=0x40000,mat=0x50000;
    for(unsigned i=0;i<8;++i)put(binding+0xb0+8*i,Address(0));
    put(binding+0x140,16.f);put(binding+0x144,64.f);
    put(binding+0xb0,group);put(binding+0x90,std::uint16_t(1));
    put(group,mat);put(group+8,particle);put(group+16,particle);put(group+0x30,Address(0));
    put(particle,std::array<unsigned char,0x4c>{});
    put(particle,sentinel);put(particle+8,sentinel);put(particle+0x10,sub);put(sub+0x10,mat);
    put(particle+0x18,std::array<float,3>{1,2,3});put(particle+0x28,std::array<float,3>{4,5,6});
    put(particle+0x34,30.f);put(particle+0x38,1.f);put(particle+0x3c,.2f);
    put(particle+0x40,std::array<unsigned char,8>{10,20,30,255,0,12,1,0});
    put(particle+0x48,5.f);put(sub,std::array<float,4>{0,0,1,1});
    Frame sample;sample.Tick=10;sample.Complete=true;sample.Expected=1;
    assert(CaptureBinding(binding,1,sub,0,read,sample));
    assert(sample.Particles.size()==1&&sample.Particles[0].Texture==Material::Spark&&sample.Particles[0].Color[2]==30);
    Frame unknown;assert(CaptureBinding(binding,1,0,0,read,unknown)&&unknown.UnknownMaterials==1);
    put(particle,Address(0));Frame bad;assert(!CaptureBinding(binding,1,sub,0,read,bad)&&bad.Particles.empty());
    put(particle,sentinel);put(particle+8,particle);assert(!CaptureBinding(binding,1,sub,0,read,bad));
    put(particle+8,sentinel);assert(!CaptureBinding(binding,0,sub,0,read,bad));
    put(binding+0x90,std::uint16_t(0));put(group+8,sentinel);put(group+16,sentinel);
    Frame empty;empty.Tick=11;empty.Complete=true;assert(CaptureBinding(binding,0,0,0,read,empty));
    History history(4096);assert(history.Append(sample)&&history.Append(empty));assert(history.At(10)&&history.At(11)&&!history.At(12));
    auto oldLease=history.ShareForPreview(11);const auto oldBytes=history.Bytes,oldRevision=history.Revision;
    Frame failed;failed.Tick=11;assert(!history.Append(failed)&&history.At(11)&&history.Rejected==1);
    failed.Tick=3;assert(!history.Append(failed)&&history.At(10)&&history.At(11)&&history.Bytes==oldBytes&&history.Revision==oldRevision&&history.ShareForPreview(11)==oldLease);
    assert(history.Append(empty));empty.Tick=3;assert(history.Append(empty)&&!history.At(10)&&history.At(3));
    History budget(sample.Bytes());assert(budget.Append(sample));
    auto oversized=sample;oversized.Tick=3;oversized.Particles.resize(100);
    assert(!budget.Append(oversized)&&budget.At(10)&&budget.Rejected==1);
    empty.Tick=11;assert(budget.Append(empty)&&budget.Evicted==1&&!budget.At(10));
    History gaps;Frame f;f.Complete=true;f.Tick=10;assert(gaps.Append(f));f.Tick=13;assert(gaps.Append(f));f.Tick=17;assert(gaps.Append(f));
    assert(!gaps.At(11)&&gaps.ForPreview(11)->Tick==10&&gaps.ForPreview(12)->Tick==10);
    assert(!gaps.ForPreview(9)&&!gaps.ForPreview(16)&&!gaps.ForPreview(18)&&gaps.ForPreview(13)->Tick==13);
    auto lease=gaps.ShareForPreview(11);
    assert(lease&&lease->Tick==10&&lease==gaps.ShareForPreview(12));
    assert(gaps.ShareForPreview(13)!=lease&&!gaps.ShareForPreview(16));
    gaps.Clear();assert(!gaps.ShareForPreview(11)&&lease->Tick==10);
    f.Tick=10;assert(gaps.Append(f));auto newerLease=gaps.ShareForPreview(10);assert(newerLease&&newerLease!=lease);
    std::cout<<"legacy particle snapshot fixtures passed\n";
}
