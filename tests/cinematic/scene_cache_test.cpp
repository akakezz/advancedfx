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
}
