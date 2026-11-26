#include "shared/SceneEffects.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace advancedfx::SceneEffects;
static void check(bool b,const char *s) {if(!b){std::cerr<<s<<"\n";std::exit(1);}}
int main() {
    Journal j(6); Event e;
    char name[]="rockettrail";
    Wire::Create create{7,name,1.0f,42};
    check(Decode("ParticleSystem_Create",&create,100,e)==DecodeResult::Valid,"create ABI");
    j.Push(e); name[0]='x';
    Wire::Position cp{7,1.1f,0,{1,2,3}};
    check(Decode("ParticleSystem_SetControlPointPosition",&cp,101,e)==DecodeResult::Valid,"position ABI");
    j.Push(e); cp.Value[0]=99;
    Wire::Object owner{7,1.1f,0,123};
    check(Decode("ParticleSystem_SetControlPointObject",&owner,101,e)==DecodeResult::Valid,"object ABI");j.Push(e);
    Wire::Orientation orient{7,1.1f,0,{0,0,0,1}};
    check(Decode("ParticleSystem_SetControlPointOrientation",&orient,101,e)==DecodeResult::Valid,"orientation ABI");j.Push(e);
    Wire::Stop stop{7,1.2f,1};
    check(Decode("ParticleSystem_StopEmission",&stop,102,e)==DecodeResult::Valid,"stop ABI");j.Push(e);
    auto before=j.At(100);check(before.Systems.at(7).Name[0]=='r'&&before.Systems.at(7).Points.empty(),"copy name and no future CP");
    auto during=j.At(101);const auto &p=during.Systems.at(7).Points.at(0);
    check(p.HasPosition&&p.Position[0]==1&&p.HasObject&&p.Object==123&&p.HasOrientation&&p.Orientation[3]==1,"copy all CP fields");
    check(j.At(102).Systems.at(7).StopReported&&j.At(102).Systems.at(7).InfiniteOnly,"stop is not destruction");
    Wire::Destroy destroy{7,1.3f};
    check(Decode("ParticleSystem_Destroy",&destroy,103,e)==DecodeResult::Valid,"destroy ABI");j.Push(e);
    check(j.At(103).Systems.empty()&&j.At(101).Systems.size()==1,"random access independent of query order");
    e.Tick=104;j.Push(e);check(j.Dropped==1&&j.At(104).Truncated&&!j.At(103).Truncated,"cap marks incomplete samples");
    check(!j.At(105).InWindow&&!j.At(99).InWindow,"no extrapolation");
    j.Interrupt();check(j.At(102).Interrupted,"interruption visible");
    j.Observe(90);check(j.Events.empty()&&j.First==90&&!j.Interrupted&&j.Dropped==0,"backward seek resets history");
    cp.Point=64;check(Decode("ParticleSystem_SetControlPointPosition",&cp,90,e)==DecodeResult::Invalid,"invalid control point");
    cp.Point=0;cp.Value[0]=std::numeric_limits<float>::quiet_NaN();
    check(Decode("ParticleSystem_SetControlPointPosition",&cp,90,e)==DecodeResult::Invalid,"nonfinite control point");
    check(Decode("ParticleSystem_Create",nullptr,90,e)==DecodeResult::Invalid,"null payload");
    check(Decode("ParticleSystem_Future",nullptr,90,e)==DecodeResult::Unknown,"unknown message");
    check(Decode("entity_state",nullptr,90,e)==DecodeResult::Ignore,"ignore unrelated messages");
    destroy.Id=100;check(Decode("ParticleSystem_Destroy",&destroy,91,e)==DecodeResult::Valid,"unmatched destroy payload");j.Push(e);
    stop.Id=100;check(Decode("ParticleSystem_StopEmission",&stop,92,e)==DecodeResult::Valid,"unknown update payload");j.Push(e);
    check(j.At(92).UnmatchedDestroys==1&&j.At(92).UnknownUpdates==1&&j.At(92).Systems.empty(),"never invent systems");
    Journal compact(32);
    create.Id=7; create.Time=1;
    check(Decode("ParticleSystem_Create",&create,100,e)==DecodeResult::Valid,"compaction create");compact.Push(e);
    cp.Id=7;cp.Point=0;cp.Value[0]=1;
    check(Decode("ParticleSystem_SetControlPointPosition",&cp,101,e)==DecodeResult::Valid,"compaction CP");
    compact.Push(e);
    for(int i=0;i<10000;++i) compact.Push(e);
    check(compact.Events.size()==2&&compact.Unchanged==10000,"paused setter flood compacted");
    e.Value[0]=2;compact.Push(e);e.Value[0]=3;compact.Push(e);
    check(compact.Events.size()==2&&compact.Coalesced==2&&compact.At(101).Systems.at(7).Points.at(0).Position[0]==3,"same-tick final state");
    e.Tick=102;compact.Push(e);
    check(compact.Events.size()==2&&compact.Last==102&&compact.LastRetained==101,"unchanged across ticks extends observed window");
    e.Value[0]=4;compact.Push(e);
    check(compact.At(101).Systems.at(7).Points.at(0).Position[0]==3&&compact.At(102).Systems.at(7).Points.at(0).Position[0]==4,"preserve historical changes");
    destroy.Id=7;destroy.Time=7.4f;
    check(Decode("ParticleSystem_Destroy",&destroy,103,e)==DecodeResult::Valid&&e.SuspectDestroy,"time-like destroy");
    compact.Push(e);check(compact.At(103).Systems.count(7)&&compact.At(103).SuspectDestroys==1,"ambiguous destroy does not erase");
    destroy.Time=8.4f;
    check(Decode("ParticleSystem_Destroy",&destroy,104,e)==DecodeResult::Valid&&!e.SuspectDestroy,"nonambiguous destroy");
    compact.Push(e);check(compact.At(104).Systems.empty(),"ordinary destroy");
    check(Decode("ParticleSystem_Create",&create,104,e)==DecodeResult::Valid,"same-tick recreated system");compact.Push(e);
    cp.Value[0]=4;check(Decode("ParticleSystem_SetControlPointPosition",&cp,104,e)==DecodeResult::Valid,"same CP new lifetime");compact.Push(e);
    check(compact.At(104).Systems.at(7).Points.at(0).HasPosition,"never dedup across lifetimes");
    check(compact.RawCounts[4]==10006,"raw counts include unchanged/coalesced");
    compact.Clear();check(compact.RawCounts[4]==0&&compact.Unchanged==0&&compact.Coalesced==0&&compact.LastRetained==-1,"clear compaction state");
    std::cout<<"Particle event probe tests passed\n";
}
