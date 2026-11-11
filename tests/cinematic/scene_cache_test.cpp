#include "shared/SceneCache.h"
#include <cassert>
#include <limits>
int main()
{
 using Cache=advancedfx::SceneCache;
 Cache cache; Cache::Frame a; a.DemoTime=10;a.ClientTime=110;
 Cache::Entity entity;entity.Identity=101;entity.Angles[1]=179;a.Entities.push_back(entity);
 assert(cache.Append(a));assert(!cache.Append(a));
 auto b=a;b.DemoTime=10.05;b.ClientTime=110.05;b.Entities[0].Origin[0]=10;b.Entities[0].Angles[1]=-179;
 assert(cache.Append(b));Cache::Frame out;
 assert(cache.Sample(10.025,out));assert(std::abs(out.ClientTime-110.025)<1e-8);
 assert(std::abs(out.Entities[0].Origin[0]-5)<.001);assert(std::abs(out.Entities[0].Angles[1]-180)<.001);
 assert(!cache.Sample(9,out));assert(!cache.Sample(11,out));
 b.DemoTime=10.1;b.ClientTime=110.1;b.Entities[0].Identity=102;assert(cache.Append(b));
 assert(cache.Sample(10.075,out)&&out.Entities.empty());
 b.DemoTime=11;assert(cache.Append(b));assert(!cache.Sample(10.5,out));
 b.DemoTime=12;b.Entities[0].Origin[0]=1000;assert(cache.Append(b));
 assert(cache.Sample(11.5,out,2)&&out.Entities.empty());
 b.DemoTime=1;assert(cache.Append(b)&&cache.Count()==1);
 b.DemoTime=2;b.Entities[0].Origin[0]=std::numeric_limits<float>::quiet_NaN();assert(!cache.Append(b));
 cache.SetLimit(0);assert(cache.Count()==0&&cache.Bytes()==0);

 // Poses own all matrices and share only immutable model metadata.
 Cache::Matrix identity{};identity[0]=identity[5]=identity[10]=1;
 auto model=std::make_shared<Cache::Model>();model->Name="models/player/scout.mdl";model->Checksum=123;
 model->Parents={-1,0};model->Flags={0x400,0x400};
 auto pose=std::make_shared<Cache::Pose>();pose->ModelInfo=model;pose->Root=identity;pose->Bones={identity,identity};pose->Bones[1][3]=10;
 assert(Cache::ValidPose(*pose));
 Cache posed;Cache::Frame p;p.DemoTime=0;p.ClientTime=100;entity.Identity=1;entity.ModelPose=pose;p.Entities={entity};assert(posed.Append(p));
 const size_t cost=posed.Bytes();assert(cost>=sizeof(Cache::Pose)+2*sizeof(Cache::Matrix)+sizeof(Cache::Model));
 auto next=std::make_shared<Cache::Pose>(*pose);next->Bones[1][3]=20;
 p.DemoTime=.05;p.ClientTime=100.05;p.Entities[0].Origin[0]=5;p.Entities[0].ModelPose=next;assert(posed.Append(p));
 assert(posed.Sample(.01,out)&&out.Entities[0].ModelPose==pose);assert(posed.Sample(.04,out)&&out.Entities[0].ModelPose==next);
 assert(posed.Sample(.025,out)&&out.Entities[0].Origin[0]==2.5f);
 assert(out.Entities[0].ModelPose->Bones[1][3]==10 || out.Entities[0].ModelPose->Bones[1][3]==20); // No matrix blending.
 auto changed=std::make_shared<Cache::Model>(*model);changed->Checksum++;
 auto changedPose=std::make_shared<Cache::Pose>(*pose);changedPose->ModelInfo=changed;
 p.DemoTime=.1;p.ClientTime=100.1;p.Entities[0].ModelPose=changedPose;assert(posed.Append(p));
 assert(posed.Sample(.075,out)&&!out.Entities[0].ModelPose);assert(posed.Sample(.1,out)&&out.Entities[0].ModelPose==changedPose);
 p.DemoTime=.15;p.ClientTime=100.15;p.Entities[0].ModelPose.reset();assert(posed.Append(p));
 assert(posed.Sample(.125,out)&&!out.Entities[0].ModelPose); // Missing capture cannot reuse an old pose.
 auto bad=std::make_shared<Cache::Pose>(*pose);bad->Bones[0][3]=std::numeric_limits<float>::infinity();
 p.DemoTime=.2;p.Entities[0].ModelPose=bad;assert(!posed.Append(p)&&posed.Count()==4);
 auto badModel=std::make_shared<Cache::Model>(*model);badModel->Parents[0]=1;bad->ModelInfo=badModel;bad->Bones=pose->Bones;assert(!Cache::ValidPose(*bad));
 badModel->Parents={-1,0};badModel->Flags.pop_back();assert(!Cache::ValidPose(*bad));
 badModel->Flags={0x400,0x400};bad->Bones.pop_back();assert(!Cache::ValidPose(*bad));
 bad->Bones=pose->Bones;bad->Root[0]=std::numeric_limits<float>::quiet_NaN();assert(!Cache::ValidPose(*bad));
 bad->Root={};assert(!Cache::ValidPose(*bad));
 Cache bounded(cost);p.DemoTime=0;p.ClientTime=100;p.Entities[0].ModelPose=pose;assert(bounded.Append(p));
 p.DemoTime=.05;p.ClientTime=100.05;assert(bounded.Append(p)&&bounded.Count()==1&&bounded.Bytes()<=cost);
 Cache::Frame retained=bounded.Frames().front();bounded.Clear();assert(retained.Entities[0].ModelPose->Bones[1][3]==10);
 Cache tiny(cost-1);assert(!tiny.Append(p)&&tiny.Count()==0);

 // Transient identities include the entity handle's serial, not just its slot.
 Cache projectiles;Cache::Frame f;f.DemoTime=0;f.ClientTime=0;Cache::Entity rocket;
 rocket.Category=1;rocket.Identity=(1ull<<63)|40;f.Entities={rocket};assert(projectiles.Append(f));
 f.DemoTime=.05;f.ClientTime=.05;f.Entities[0].Identity=(1ull<<63)|(1<<12)|40;assert(projectiles.Append(f));
 assert(projectiles.Sample(.025,out)&&out.Entities.empty());
 f.DemoTime=.1;f.ClientTime=.1;f.Entities.clear();assert(projectiles.Append(f));assert(projectiles.Sample(.075,out)&&out.Entities.empty());
 assert(Cache::CategoryColor(0)!=Cache::CategoryColor(1));
 f.DemoTime=.15;f.Entities={rocket};f.Entities[0].Category=9;assert(!projectiles.Append(f));
 Cache categories;f.DemoTime=0;f.ClientTime=0;f.Entities={rocket};assert(categories.Append(f));
 f.DemoTime=.05;f.ClientTime=.05;f.Entities[0].Category=0;assert(categories.Append(f));assert(categories.Sample(.025,out)&&out.Entities.empty());
}
