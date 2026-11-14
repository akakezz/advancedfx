#include "shared/SceneCache.h"
#include <cassert>
#include <limits>

static std::vector<unsigned char> BrushFixture()
{
 std::vector<unsigned char> data(1036);auto put=[&](size_t at,const void*p,size_t n){std::memcpy(data.data()+at,p,n);};
 auto integer=[&](size_t at,int v){put(at,&v,4);};integer(0,0x50534256);integer(4,20);integer(1032,17);
 auto lump=[&](int id,size_t bytes,int version){size_t at=data.size();data.resize(at+bytes);integer(8+id*16,int(at));integer(12+id*16,int(bytes));integer(16+id*16,version);return at;};
 size_t verts=lump(3,48,0);float positions[12]={0,0,0,2,0,0,2,2,0,0,2,0};put(verts,positions,sizeof(positions));
 size_t edges=lump(12,16,0);unsigned short indices[8]={0,1,1,2,3,2,3,0};put(edges,indices,sizeof(indices));
 size_t surf=lump(13,16,0);int directed[4]={0,1,-2,3};put(surf,directed,sizeof(directed));
 size_t faces=lump(7,56,1);short count=4;put(faces+8,&count,2);
 size_t models=lump(14,96,0);integer(models+48+44,1);
 return data;
}

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
 assert(posed.Bytes()<2*cost); // Shared model metadata is charged once.
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

 // Exact pose reuse charges shared allocations once and never crosses ownership/model boundaries.
 Cache reuse;Cache::Frame repeated;p.DemoTime=0;p.ClientTime=0;p.Entities[0].ModelPose=pose;p.Entities[0].OwnerIdentity=0;assert(reuse.Append(p));auto firstPose=reuse.Frames().front().Entities[0].ModelPose;size_t firstBytes=reuse.Bytes();
 auto copy = std::make_shared<Cache::Pose>(*pose);
 p.DemoTime = .05;
 p.ClientTime = .05;
 p.Entities[0].ModelPose = copy;
 assert(reuse.Append(p));
 assert(reuse.PoseCount() == 1 && reuse.ReusedPoses() == 1 &&
		reuse.Frames().back().Entities[0].ModelPose == firstPose);
 assert(reuse.Bytes() - firstBytes ==
		sizeof(Cache::Frame) + reuse.Frames().back().Entities.capacity() * sizeof(Cache::Entity));
 copy=std::make_shared<Cache::Pose>(*pose);copy->Bones[0][3]+=.001f;p.DemoTime=.1;p.ClientTime=.1;p.Entities[0].ModelPose=copy;assert(reuse.Append(p)&&reuse.PoseCount()==2);assert(reuse.Frames().back().Entities[0].ModelPose==copy);
 copy=std::make_shared<Cache::Pose>(*copy);p.DemoTime=.15;p.ClientTime=.15;p.Entities[0].OwnerIdentity=2;p.Entities[0].ModelPose=copy;assert(reuse.Append(p)&&reuse.PoseCount()==3);assert(reuse.ReusedPoses()==1);
 copy=std::make_shared<Cache::Pose>(*copy);copy->Root[3]+=1;p.DemoTime=.2;p.ClientTime=.2;p.Entities[0].ModelPose=copy;assert(reuse.Append(p)&&reuse.PoseCount()==4);
 auto allocated=reuse.AllocatedBytes();assert(allocated==reuse.Bytes());auto revision=reuse.Revision();reuse.SetLimit(firstBytes);assert(reuse.Bytes()<=firstBytes&&reuse.PoseCount()==1&&reuse.AllocatedBytes()==allocated&&reuse.Revision()>revision);reuse.Clear();assert(!reuse.PoseCount()&&!reuse.Bytes()&&!reuse.AllocatedBytes()&&!reuse.ReusedPoses());
 // Per-bone exception values and local tangents are part of equality, too.
 Cache localReuse;auto l=std::make_shared<Cache::Pose>();l->ModelInfo=model;l->Root=identity;l->Local.resize(2);p.DemoTime=0;p.ClientTime=0;p.Entities[0].ModelPose=l;assert(localReuse.Append(p));auto lc=std::make_shared<Cache::Pose>(*l);lc->Local[1].Position[0]=.01f;p.DemoTime=.05;p.ClientTime=.05;p.Entities[0].ModelPose=lc;assert(localReuse.Append(p)&&localReuse.PoseCount()==2);
 auto fixedPose=std::make_shared<Cache::Pose>(*lc);Cache::FixedBone reuseFixed;reuseFixed.Index=1;reuseFixed.Value=identity;fixedPose->Fixed.push_back(reuseFixed);p.DemoTime=.1;p.ClientTime=.1;p.Entities[0].ModelPose=fixedPose;assert(localReuse.Append(p)&&localReuse.PoseCount()==3);auto fixedCopy=std::make_shared<Cache::Pose>(*fixedPose);fixedCopy->Fixed[0].Value[3]=.01f;p.DemoTime=.15;p.ClientTime=.15;p.Entities[0].ModelPose=fixedCopy;assert(localReuse.Append(p)&&localReuse.PoseCount()==4);
 fixedCopy=std::make_shared<Cache::Pose>(*fixedCopy);fixedCopy->Body++;p.DemoTime=.2;p.ClientTime=.2;p.Entities[0].ModelPose=fixedCopy;assert(localReuse.Append(p)&&localReuse.PoseCount()==5);fixedCopy=std::make_shared<Cache::Pose>(*fixedCopy);p.DemoTime=.25;p.ClientTime=.25;p.Entities[0].Category=1;p.Entities[0].ModelPose=fixedCopy;assert(localReuse.Append(p)&&localReuse.PoseCount()==6);

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

 // Gear follows stable owner identity and cannot interpolate across transfers
 // or remain floating when the parent's movement cannot be bridged.
 Cache attached;Cache::Frame gearFrame;Cache::Entity owner;owner.Identity=1;Cache::Entity gear;gear.Identity=(1ull<<63)|50;gear.OwnerIdentity=1;gear.Category=2;
 gearFrame.Entities={owner,gear};assert(attached.Append(gearFrame));gearFrame.DemoTime=.05;gearFrame.ClientTime=.05;gearFrame.Entities[0].Origin[0]=10;gearFrame.Entities[1].Origin[0]=10;assert(attached.Append(gearFrame));
 assert(attached.Sample(.025,out)&&out.Entities.size()==2&&out.Entities[1].OwnerIdentity==1&&out.Entities[1].Origin[0]==5);
 gearFrame.DemoTime=.1;gearFrame.ClientTime=.1;gearFrame.Entities[1].OwnerIdentity=2;assert(attached.Append(gearFrame));assert(attached.Sample(.075,out)&&out.Entities.size()==1);
 attached.Clear();gearFrame.DemoTime=0;gearFrame.ClientTime=0;gearFrame.Entities[1].OwnerIdentity=1;gearFrame.Entities[0].Origin[0]=0;assert(attached.Append(gearFrame));gearFrame.DemoTime=.05;gearFrame.ClientTime=.05;gearFrame.Entities[0].Origin[0]=1000;assert(attached.Append(gearFrame));assert(attached.Sample(.025,out)&&out.Entities.empty());

 // Rotation interpolation preserves a rotating limb's length, including at
 // half turns and the quaternion sign boundary. No world-matrix lerp.
 for(int axis=0;axis<3;++axis)for(double angle:{0.,.000001,1.5707963267948966,3.141592653589793,3.141592653589793-.000001})
 {
  double matrix[3][3]{};for(int i=0;i<3;++i)matrix[i][i]=1;int j=(axis+1)%3,k=(axis+2)%3;matrix[j][j]=matrix[k][k]=std::cos(angle);matrix[j][k]=-std::sin(angle);matrix[k][j]=std::sin(angle);
  const auto quat=Afx::Math::Quaternion::FromRotationMatrix(matrix);double roundtrip[3][3];quat.ToRotationMatrix(roundtrip);for(int r=0;r<3;++r)for(int c=0;c<3;++c)assert(std::abs(matrix[r][c]-roundtrip[r][c])<1e-8);
 }
 Cache::Transform transform;transform.Position={3,-4,5};transform.Scale={-2,3,4};auto rotation=Afx::Math::Quaternion::FromQREulerAngles({.2,2.9,-1.4});transform.Rotation={float(rotation.W),float(rotation.X),float(rotation.Y),float(rotation.Z)};
 auto matrix=transform.Compose();Cache::Transform recovered;assert(Cache::Transform::Decompose(matrix,recovered));auto roundtrip=recovered.Compose();for(int i=0;i<12;++i)assert(std::abs(matrix[i]-roundtrip[i])<1e-5);
 Cache::Matrix tooLarge=identity;tooLarge[0]=tooLarge[1]=tooLarge[5]=2.8e38f;tooLarge[4]=-2.8e38f;assert(!Cache::Transform::Decompose(tooLarge,recovered));
 Cache::Matrix shear=identity;shear[1]=.2f;assert(!Cache::Transform::Decompose(shear,recovered));shear=identity;shear[0]=0;assert(!Cache::Transform::Decompose(shear,recovered));
 auto localA=std::make_shared<Cache::Pose>();localA->ModelInfo=model;localA->Root=identity;localA->Local.resize(2);localA->Local[1].Position[0]=10;
 auto localB=std::make_shared<Cache::Pose>(*localA);auto turn=Afx::Math::Quaternion::FromQREulerAngles({0,1.5707963267948966,0});localB->Local[0].Rotation={float(turn.W),float(turn.X),float(turn.Y),float(turn.Z)};localB->Local[0].Scale={2,2,2};
 Cache animated;Cache::Frame localFrame;localFrame.Entities={owner};localFrame.Entities[0].ModelPose=localA;assert(animated.Append(localFrame));localFrame.DemoTime=.05;localFrame.ClientTime=.05;localFrame.Entities[0].ModelPose=localB;assert(animated.Append(localFrame));
 assert(animated.Sample(.025,out));auto blended=out.Entities[0].ModelPose;assert(blended!=localA&&blended!=localB&&Cache::ValidPose(*blended));auto rootMatrix=blended->Local[0].Compose();assert(std::abs(rootMatrix[0]-1.5/std::sqrt(2.))<1e-5&&std::abs(rootMatrix[4]-1.5/std::sqrt(2.))<1e-5);assert(blended->Local[1].Position[0]==10);
 assert(animated.Sample(.025,out,.1,256,false)&&out.Entities[0].ModelPose==localB);assert(animated.Sample(0,out)&&out.Entities[0].ModelPose==localA);
 auto invalid=std::make_shared<Cache::Pose>(*localA);invalid->Local[0].Rotation={0,0,0,0};assert(!Cache::ValidPose(*invalid));invalid->Local[0]=Cache::Transform{};invalid->Local[0].Scale[0]=0;assert(!Cache::ValidPose(*invalid));invalid->Local[0]=Cache::Transform{};invalid->Local[0].Position[0]=NAN;assert(!Cache::ValidPose(*invalid));
 auto opposite=std::make_shared<Cache::Pose>(*localA);opposite->Local[0].Rotation={-1,0,0,0};localFrame.DemoTime=.1;localFrame.ClientTime=.1;localFrame.Entities[0].ModelPose=opposite;assert(animated.Append(localFrame));assert(animated.Sample(.075,out)&&Cache::ValidPose(*out.Entities[0].ModelPose));
 opposite=std::make_shared<Cache::Pose>(*localA);opposite->Local[0].Scale[0]=-1;localFrame.DemoTime=.15;localFrame.ClientTime=.15;localFrame.Entities[0].ModelPose=opposite;assert(animated.Append(localFrame));assert(animated.Sample(.125,out)&&out.Entities[0].ModelPose->Local[0].Scale[0]==-1&&out.Entities[0].ModelPose->HeldBones==1&&out.Entities[0].ModelPose->BlendedBones==1); // Only the reflected bone uses nearest.

 // Retention releases a model's metadata only after its final referencing frame.
 Cache accounting,onlyLast;Cache::Frame modelFrame;modelFrame.Entities={entity};modelFrame.Entities[0].ModelPose=pose;assert(accounting.Append(modelFrame));
 modelFrame.DemoTime=.05;modelFrame.ClientTime=.05;modelFrame.Entities[0].ModelPose=changedPose;assert(accounting.Append(modelFrame)&&onlyLast.Append(modelFrame));accounting.SetLimit(onlyLast.Bytes());assert(accounting.Count()==1&&accounting.Bytes()==onlyLast.Bytes());accounting.Clear();assert(accounting.Bytes()==0);assert(accounting.Append(modelFrame)&&accounting.Bytes()==onlyLast.Bytes());

 Cache wrap;auto wrapA=std::make_shared<Cache::Pose>(*localA),wrapB=std::make_shared<Cache::Pose>(*localA);auto qa=Afx::Math::Quaternion::FromQREulerAngles({0,179*3.141592653589793/180,0}),qb=Afx::Math::Quaternion::FromQREulerAngles({0,-179*3.141592653589793/180,0});wrapA->Local[0].Rotation={float(qa.W),float(qa.X),float(qa.Y),float(qa.Z)};wrapB->Local[0].Rotation={float(qb.W),float(qb.X),float(qb.Y),float(qb.Z)};modelFrame.DemoTime=0;modelFrame.Entities[0].ModelPose=wrapA;assert(wrap.Append(modelFrame));modelFrame.DemoTime=.05;modelFrame.Entities[0].ModelPose=wrapB;assert(wrap.Append(modelFrame));assert(wrap.Sample(.025,out));auto midpoint=out.Entities[0].ModelPose->Local[0].Compose();assert(std::abs(midpoint[0]+1)<1e-5&&std::abs(midpoint[4])<1e-5);

 auto bodyChange=std::make_shared<Cache::Pose>(*opposite);bodyChange->Body=3;localFrame.DemoTime=.2;localFrame.ClientTime=.2;localFrame.Entities[0].ModelPose=bodyChange;assert(animated.Append(localFrame));assert(animated.Sample(.19,out)&&out.Entities[0].ModelPose->Body==3&&out.Entities[0].ModelPose->BlendedBones==2);

 // One unsupported child does not freeze its supported, rotating parent.
 auto mixedA=std::make_shared<Cache::Pose>(*localA),mixedB=std::make_shared<Cache::Pose>(*localB);mixedB->Local[0].Scale={1,1,1};
 Cache::FixedBone fixed;fixed.Index=1;fixed.Value=identity;fixed.Value[1]=.3f;fixed.Value[3]=10;mixedA->Fixed={fixed};fixed.Value[3]=20;mixedB->Fixed={fixed};
 Cache mixed;modelFrame.DemoTime=0;modelFrame.Entities[0].ModelPose=mixedA;assert(mixed.Append(modelFrame));modelFrame.DemoTime=.05;modelFrame.Entities[0].ModelPose=mixedB;assert(mixed.Append(modelFrame));
 assert(mixed.Sample(.0125,out));auto partial=out.Entities[0].ModelPose;assert(partial->BlendedBones==1&&partial->HeldBones==1&&partial->Fixed.size()==1&&partial->Fixed[0].Value[3]==10&&Cache::ValidPose(*partial));
 // Reference result of Source QuaternionBlend(identity, yaw90, .25).
 auto sourceBlend=partial->Local[0].Compose();assert(std::abs(sourceBlend[0]-.929788301)<1e-6&&std::abs(sourceBlend[4]-.368094710)<1e-6);
 assert(mixed.Sample(.04,out)&&out.Entities[0].ModelPose->Fixed[0].Value[3]==20);
 // A bone can enter/leave fallback without disabling the rest of the rig.
 mixedB=std::make_shared<Cache::Pose>(*mixedB);mixedB->Fixed.clear();modelFrame.DemoTime=.1;modelFrame.Entities[0].ModelPose=mixedB;assert(mixed.Append(modelFrame));assert(mixed.Sample(.09,out)&&out.Entities[0].ModelPose->Fixed.empty()&&out.Entities[0].ModelPose->HeldBones==1&&out.Entities[0].ModelPose->BlendedBones==1);
 auto invalidFixed=std::make_shared<Cache::Pose>(*mixedA);invalidFixed->Fixed.push_back(invalidFixed->Fixed[0]);assert(!Cache::ValidPose(*invalidFixed));invalidFixed->Fixed.resize(1);invalidFixed->Fixed[0].Index=128;assert(!Cache::ValidPose(*invalidFixed));
 // Corpse/debris poses use the same local-space smoothing, but cannot bridge
 // disappearance, category changes or a newly spawned object in a reused slot.
 for(int debrisCategory:{5,6}){
  Cache debrisCache;Cache::Frame debrisFrame;Cache::Entity debrisEntity;debrisEntity.Identity=(1ull<<63)|(7<<12)|2050;debrisEntity.Category=debrisCategory;debrisEntity.ModelPose=localA;debrisFrame.Entities={debrisEntity};assert(debrisCache.Append(debrisFrame));
  debrisFrame.DemoTime=.05;debrisFrame.ClientTime=.05;debrisFrame.Entities[0].ModelPose=localB;assert(debrisCache.Append(debrisFrame));assert(debrisCache.Sample(.025,out)&&out.Entities.size()==1&&out.Entities[0].ModelPose->BlendFraction==.5f);assert(debrisCache.Sample(.025,out,.1,256,false)&&out.Entities[0].ModelPose==localB);
  debrisFrame.DemoTime=.1;debrisFrame.ClientTime=.1;debrisFrame.Entities.clear();assert(debrisCache.Append(debrisFrame));assert(debrisCache.Sample(.075,out)&&out.Entities.empty());
  debrisEntity.Identity+=(1<<12);debrisFrame.DemoTime=.15;debrisFrame.ClientTime=.15;debrisFrame.Entities={debrisEntity};assert(debrisCache.Append(debrisFrame));assert(debrisCache.Sample(.125,out)&&out.Entities.empty());
  debrisFrame.DemoTime=.2;debrisFrame.ClientTime=.2;debrisFrame.Entities[0].Identity+=(1<<12);assert(debrisCache.Append(debrisFrame));assert(debrisCache.Sample(.175,out)&&out.Entities.empty());
  assert(Cache::CategoryColor(debrisCategory)!=Cache::CategoryColor(0)&&Cache::CategoryColor(5)!=Cache::CategoryColor(6));
 }

 // A corpse can be the retained bone-merge host; its disappearance also removes gear.
 {Cache corpseGear;Cache::Frame frame;Cache::Entity corpse;corpse.Category=5;corpse.Identity=(1ull<<63)|(3<<12)|50;Cache::Entity hat;hat.Category=3;hat.Identity=(1ull<<63)|(4<<12)|51;hat.OwnerIdentity=corpse.Identity;frame.Entities={corpse,hat};assert(corpseGear.Append(frame));frame.DemoTime=.05;frame.ClientTime=.05;frame.Entities[0].Origin[0]=10;frame.Entities[1].Origin[0]=10;assert(corpseGear.Append(frame));assert(corpseGear.Sample(.025,out)&&out.Entities.size()==2);frame.DemoTime=.1;frame.ClientTime=.1;frame.Entities.erase(frame.Entities.begin());assert(corpseGear.Append(frame));assert(corpseGear.Sample(.075,out)&&out.Entities.empty());}

 // Brush-door proxy bounds remain in the recorded entity's local frame, then rotate into world space.
 {Cache::Entity door;door.Category=8;door.Origin[0]=10;door.Origin[1]=20;door.Origin[2]=30;door.Mins[0]=-1;door.Mins[1]=-2;door.Mins[2]=-3;door.Maxs[0]=1;door.Maxs[1]=2;door.Maxs[2]=3;door.Angles[1]=90;auto corners=Cache::BoundsCorners(door);assert(std::abs(corners[7][0]-8)<1e-8&&std::abs(corners[7][1]-21)<1e-8&&std::abs(corners[7][2]-33)<1e-8);door.Angles[1]=0;corners=Cache::BoundsCorners(door);assert(corners[0][0]==9&&corners[0][1]==18&&corners[0][2]==27);}
 for(int hostCategory:{7,8}){Cache objects;Cache::Frame f;Cache::Entity host;host.Category=hostCategory;host.Identity=(1ull<<63)|(10<<12)|100;Cache::Entity attachedItem;attachedItem.Category=8;attachedItem.Identity=(1ull<<63)|(11<<12)|101;attachedItem.OwnerIdentity=host.Identity;f.Entities={host,attachedItem};assert(objects.Append(f));f.DemoTime=.05;f.ClientTime=.05;f.Entities[0].Origin[0]=10;f.Entities[1].Origin[0]=10;assert(objects.Append(f));assert(objects.Sample(.025,out)&&out.Entities.size()==2&&out.Entities[0].Origin[0]==5);f.DemoTime=.1;f.ClientTime=.1;f.Entities.erase(f.Entities.begin());assert(objects.Append(f));assert(objects.Sample(.075,out)&&out.Entities.empty());}

 // Parse actual BSP face/surfedge winding into a triangulated wireframe.
 auto bsp=BrushFixture();auto read=[&](size_t at,void*out,size_t n){if(at>bsp.size()||n>bsp.size()-at)return false;std::memcpy(out,bsp.data()+at,n);return true;};
 auto brush=Cache::ReadBrushModel("maps/test.bsp*1",1,bsp.size(),read);assert(brush&&brush->WireVertices.size()==4&&brush->WireEdges.size()==5&&brush->Checksum==17);
 assert(!Cache::ReadBrushModel("world",0,bsp.size(),read));assert(!Cache::ReadBrushModel("missing",2,bsp.size(),read));
 auto validBsp=bsp;int invalidOffset=-1;std::memcpy(bsp.data()+8+14*16,&invalidOffset,4);assert(!Cache::ReadBrushModel("bad",1,bsp.size(),read));bsp=validBsp;
 int compressed=100;std::memcpy(bsp.data()+20+3*16,&compressed,4);assert(!Cache::ReadBrushModel("compressed",1,bsp.size(),read));bsp=validBsp;
 int vertexOffset;std::memcpy(&vertexOffset,bsp.data()+8+3*16,4);float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(bsp.data()+vertexOffset,&nan,4);assert(!Cache::ReadBrushModel("bad vertex",1,bsp.size(),read));bsp=validBsp;
 // Every truncation rejects safely, without trusting directory offsets/counts.
 for(size_t size=0;size<bsp.size();size+=7)assert(!Cache::ReadBrushModel("truncated",1,size,read));
 auto brushPose=std::make_shared<Cache::Pose>();brushPose->ModelInfo=brush;brushPose->Root=identity;brushPose->Local.resize(1);assert(Cache::ValidPose(*brushPose));
 Cache brushCache;Cache::Frame brushFrame;brushFrame.Entities.resize(1);auto&door=brushFrame.Entities[0];door.Identity=201;door.Category=8;door.Origin[0]=10;door.Angles[1]=90;door.ModelPose=brushPose;assert(brushCache.Append(brushFrame));
 Cache::DrawingOptions draw;assert(!draw.Bounds&&!draw.Trails);auto meshLines=brushCache.Drawing(brushFrame,draw);assert(meshLines.size()==5);for(const auto&line:meshLines){assert(line.Category==8);assert(line.A[0]>=7.999&&line.A[0]<=10.001&&line.A[1]>=-.001&&line.A[1]<=2.001);}
 draw.Bounds=true;assert(brushCache.Drawing(brushFrame,draw).size()==17);draw.Meshes=false;assert(brushCache.Drawing(brushFrame,draw).size()==12);draw.Bounds=false;assert(brushCache.Drawing(brushFrame,draw).empty());
 const auto meshBytes=brushCache.Bytes();brushCache.SetLimit(1);assert(brushCache.Count()==0&&brushCache.Bytes()==0&&meshBytes>brush->WireVertices.size()*12+brush->WireEdges.size()*8);
 // Trail geometry is independent of campaths/models and covers fractional cursors.
 Cache trails;Cache::Frame moving;moving.Entities.resize(2);moving.Entities[0].Identity=1;moving.Entities[1].Identity=2;moving.Entities[1].Category=1;
 for(int i=0;i<=10;++i){moving.DemoTime=moving.ClientTime=i*.015;moving.Entities[0].Origin[0]=i;moving.Entities[1].Origin[1]=i*2;assert(trails.Append(moving));}
 Cache::Frame fractional;assert(trails.Sample(.1275,fractional));Cache::DrawingOptions trailOptions;trailOptions.Meshes=false;trailOptions.Trails=true;trailOptions.TrailSeconds=.1;auto trailLines=trails.Drawing(fractional,trailOptions);assert(trailLines.size()==16);assert(std::abs(trailLines.back().B[1]-17)<1e-5);for(const auto&line:trailLines)assert(line.Alpha<=240&&line.Alpha>=40);
 trailOptions.TrailCategories[1]=false;assert(trails.Drawing(fractional,trailOptions).size()==8);trailOptions.Trails=false;assert(trails.Drawing(fractional,trailOptions).empty());
 trailOptions.Trails=true;trailOptions.TrailSeconds=30;moving.DemoTime=.165;moving.Entities[0].Origin[0]=1000;moving.Entities[1].Identity=3;assert(trails.Append(moving));auto beforeJump=trails.Drawing(moving,trailOptions);assert(beforeJump.size()==10); // no teleport or recycled-identity connection
 moving.DemoTime=.3;moving.Entities[0].Origin[0]=1001;assert(trails.Append(moving));assert(trails.Drawing(moving,trailOptions).size()==10); // no missing-update bridge
 moving.DemoTime=.315;moving.Entities.clear();assert(trails.Append(moving));moving.DemoTime=.33;moving.Entities.resize(1);moving.Entities[0].Identity=1;assert(trails.Append(moving));assert(trails.Drawing(moving,trailOptions).size()==10); // no despawn bridge

 // Dense drawings respect fixed submission budgets.
 auto denseModel=std::make_shared<Cache::Model>(*brush);denseModel->WireEdges.assign(70000,{0,1});auto densePose=std::make_shared<Cache::Pose>(*brushPose);densePose->ModelInfo=denseModel;auto denseFrame=brushFrame;denseFrame.Entities[0].ModelPose=densePose;assert(brushCache.Drawing(denseFrame,Cache::DrawingOptions{}).size()==65536);
 Cache denseTrails;Cache::Frame crowd;crowd.Entities.resize(100);for(int i=0;i<100;++i)crowd.Entities[i].Identity=i+1;for(int t=0;t<180;++t){crowd.DemoTime=t*.01;for(auto&e:crowd.Entities)e.Origin[0]=float(t);assert(denseTrails.Append(crowd));}assert(denseTrails.Drawing(crowd,trailOptions).size()==16384);

 // Bounds can be limited to categories, missing meshes, and stable identities.
 Cache::DrawingOptions scoped;scoped.Meshes=false;scoped.Bounds=true;scoped.BoundCategories.fill(false);scoped.BoundCategories[8]=true;assert(brushCache.Drawing(brushFrame,scoped).size()==12);scoped.BoundCategories[8]=false;assert(brushCache.Drawing(brushFrame,scoped).empty());scoped.BoundCategories[8]=true;scoped.BoundsMissingOnly=true;assert(brushCache.Drawing(brushFrame,scoped).empty());auto proxyFrame=brushFrame;proxyFrame.Entities[0].ModelPose.reset();assert(brushCache.Drawing(proxyFrame,scoped).size()==12);scoped.BoundsSelectedOnly=true;assert(brushCache.Drawing(proxyFrame,scoped).empty());scoped.BoundEntities.insert(201);assert(brushCache.Drawing(proxyFrame,scoped).size()==12);proxyFrame.Entities[0].Identity=202;assert(brushCache.Drawing(proxyFrame,scoped).empty());auto scopedCopy=scoped;scopedCopy.BoundEntities.insert(202);assert(!(scoped==scopedCopy));

 // Tracking outputs interpolate only across the same authored camera definition.
 Cache cameraCache;
 Cache::Frame c0, c1;
 c0.ClientTime = c0.DemoTime = 0;
 c1.ClientTime = c1.DemoTime = .05;
 Cache::CameraSample camera;
 camera.Id = 0;
 camera.Definition = 1;
 camera.Pose.Position[0] = 10;
 c0.Cameras.push_back(camera);
 camera.Pose.Position[0] = 20;
 camera.Pose.Fov = 110;
 c1.Cameras.push_back(camera);
 assert(cameraCache.Append(c0) && cameraCache.Append(c1));
 Cache::Frame cameraBlendResult;
 assert(cameraCache.Sample(.025, cameraBlendResult) && cameraBlendResult.Cameras.size() == 1 &&
		std::abs(cameraBlendResult.Cameras[0].Pose.Position[0] - 15) < 1e-6 &&
		std::abs(cameraBlendResult.Cameras[0].Pose.Fov - 100) < 1e-6);
 const auto cameraBytes = cameraCache.Bytes();
 c1.Cameras[0].Definition = 2;
 cameraCache.Clear();
 assert(cameraCache.Append(c0) && cameraCache.Append(c1) && cameraCache.Sample(.025, cameraBlendResult) &&
		cameraBlendResult.Cameras.empty());
 assert(cameraBytes >= 2 * sizeof(Cache::CameraSample));
 cameraCache.SetLimit(0);
 assert(cameraCache.Count() == 0 && cameraCache.Bytes() == 0);
}
