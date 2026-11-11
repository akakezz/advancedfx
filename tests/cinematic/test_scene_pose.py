"""Exercise the actual TF2 pose capture/draw adapter against public-interface fixtures."""
from pathlib import Path
import argparse, os, subprocess, tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, default=Path(tempfile.gettempdir()) / 'hlae-scene-pose-tests')
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
out = args.build_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
calcs = (root / 'AfxHookSource/MirvCalcs.cpp').read_text()
adapter = calcs[calcs.index('// Selected-player scene-pose prototype.'):calcs.index('// End selected-player scene-pose prototype.')]
reader = calcs[calcs.index('static int Mirv_Tf2_ReadBoneNames('):calcs.index('int Mirv_Tf2_GetBoneNames(')]
math = (root / 'deps/release/prop/AfxHookSource/SourceSdkShared.cpp').read_text()
math = math[math.index('void AngleMatrix('):math.index('MdtMatrix::MdtMatrix()', math.index('void AngleMatrix('))]
client = (root / 'AfxHookSource/ClientTools.cpp').read_text()
invert = client[client.index('bool CClientTools::InvertMatrix('):client.index('void CClientTools::CaptureBones(')]
preamble = r'''
#include "shared/SceneCache.h"
#include "shared/AfxMath.h"
#include "AfxHookSource/MaterialSystemHooks.h"
bool Mirv_Tf2_DrawScenePose(const advancedfx::SceneCache::Entity&,std::string&,const advancedfx::SceneCache::Entity* = nullptr);
#include <thread>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <cstdio>
#include <cstdlib>
#define _WIN64 1
#define MIRV_TF2_MAX_BONES 128
#define SOURCESDK_BONE_USED_BY_ANYTHING 0x0007ff00
namespace SOURCESDK {
namespace L4D2 {struct Allocator {void*IndirectAlloc(size_t n){return std::malloc(n);}void Free(void*p){std::free(p);}} allocator;auto*g_pMemAlloc=&allocator;}
namespace TF2 {
struct KeySystem {std::map<std::string,int>Names;int Allocations=0;void*AllocKeyValuesMemory(size_t n){++Allocations;return std::malloc(n);}void FreeKeyValuesMemory(void*p){--Allocations;std::free(p);}int GetSymbolForString(const char*n,bool){auto [it,added]=Names.emplace(n,Names.size());return it->second;}} keySystem;
auto*KeyValuesSystem(){return &keySystem;}
}
struct Vector {float x,y,z;Vector(float a,float b,float c):x(a),y(b),z(c){};float operator[](int i)const{return i==0?x:i==1?y:z;}};
struct QAngle {float x,y,z;QAngle(float a,float b,float c):x(a),y(b),z(c){};float operator[](int i)const{return i==0?x:i==1?y:z;}};
struct matrix3x4_t {float m_flMatVal[3][4];float*operator[](int i){return m_flMatVal[i];}const float*operator[](int i)const{return m_flMatVal[i];}};
inline float DEG2RAD(float x){return x*float(M_PI/180);}
}
class CClientTools {public:static bool InvertMatrix(const SOURCESDK::matrix3x4_t&,SOURCESDK::matrix3x4_t&);};
struct MirvTf2BoneName_t {char name[64];int parent;};
struct Object {void**Vtable;};
struct Fixture {
 unsigned char Header[2048]{};
 bool Dormant=false,Visible=true,Setup=true,Hardware=true,ErrorModel=false,Interfaces=true,Allocate=true;
 void *Override=nullptr;int OverrideType=0;
 int Count=2,Checks=0,SetupCalls=0,DrawCalls=0,Locks=0,Refs=0,Unlocks=0,HeaderLocks=0;
 std::thread::id EngineThread=std::this_thread::get_id();
 std::string ModelName="models/player/scout.mdl";int Body=3;float Origin[3]{1,2,3},Angles[3]{};
 SOURCESDK::matrix3x4_t Observed[128]{},Allocated[128]{},Drawn[128]{};
 void *StudioSlots[49]{},*CacheSlots[42]{},*NetworkSlots[9]{};
 Object Studio{StudioSlots},Cache{CacheSlots},Network{NetworkSlots};
 std::array<float,3> DrawColor{};bool MaterialInterface=true;void*MaterialSlots[43]{},*MaterialSystemSlots[71]{};Object MaterialSystem{MaterialSystemSlots};
 struct Material: Object {int Refs=0;std::string Color;std::array<float,3> Modulation{1,1,1},Tint{1,1,1};};Material Materials[5];int MaterialCount=0;

 void Put(int offset,int value){std::memcpy(Header+offset,&value,4);}
 void InitHeader(){Put(0,0x54534449);Put(4,48);Put(8,123);Put(76,sizeof(Header));Put(156,Count);Put(160,164);for(int i=0;i<Count&&i<8;++i){Put(164+i*216+4,i-1);Put(164+i*216+160,0x400);}}
} fixture;
struct CMirvTf2ClientRenderable {
 const void*GetModel(){return &fixture;}bool ShouldDraw(){return fixture.Visible;}int GetBody(){return fixture.Body;}
 bool SetupBones(SOURCESDK::matrix3x4_t*out,int maximum,int mask,float){++fixture.SetupCalls;assert(maximum==128&&mask==0x7ff00);if(!fixture.Setup)return false;std::memcpy(out,fixture.Observed,sizeof(fixture.Observed));return true;}
} renderable;
struct CMirvTf2ClientUnknown {void*GetClientNetworkable(){return &fixture.Network;}CMirvTf2ClientRenderable*GetClientRenderable(){return &renderable;}} unknown;
void*Mirv_Tf2_DiagnosticEntity(int index){return index==1?&unknown:nullptr;}
struct ModelInfo {const char*GetModelName(const void*){return fixture.ModelName.c_str();}void*GetStudiomodel(const void*){return fixture.Header;}} modelInfo;
ModelInfo*Mirv_Tf2_GetModelInfo(){return &modelInfo;}
struct Globals{float curtime_get(){return 2;}} globals;
struct View{Globals*GetGlobals(){return &globals;}} g_Hook_VClient_RenderView;
void*Factory(const char*name,int*){if(!fixture.Interfaces)return nullptr;if(!strcmp(name,"VStudioRender025"))return &fixture.Studio;if(!strcmp(name,"MDLCache004"))return &fixture.Cache;if(!strcmp(name,"VMaterialSystem082"))return fixture.MaterialInterface?&fixture.MaterialSystem:nullptr;assert(false);return nullptr;}
auto g_AppSystemFactory=Factory;
bool queueAvailable=true;
std::vector<std::unique_ptr<CMaterialSystemFunctor>> retirements;
bool MaterialSystem_CanExecuteOnRenderThread(){return queueAvailable;}
bool MaterialSystem_ExecuteOnRenderThread(CMaterialSystemFunctor*f){assert(std::this_thread::get_id()==fixture.EngineThread);retirements.emplace_back(f);return true;}
void ConsumeRenderQueue(){std::thread worker([]{for(auto&f:retirements)(*f)();retirements.clear();});worker.join();}

'''
fixtures = r'''
namespace {
void SetOverride(void*,void*material,int type){assert(std::this_thread::get_id()==fixture.EngineThread);fixture.Override=material;fixture.OverrideType=type;}
void MaterialInc(void*p){++static_cast<Fixture::Material*>(p)->Refs;}
void MaterialDec(void*p){auto&m=*static_cast<Fixture::Material*>(p);assert(m.Refs>0);--m.Refs;}
bool MaterialError(void*){return false;}
void*CreateMaterial(void*,const char*,SceneMaterialKeyValues*values){
 auto&m=fixture.Materials[fixture.MaterialCount++];m.Vtable=fixture.MaterialSlots;m.Refs=1;
 assert(values->m_iKeyName==SOURCESDK::TF2::keySystem.Names.at("UnlitGeneric"));
 // Parse the shader's separate modulation and persistent model-tint parameters.
 int color= SOURCESDK::TF2::keySystem.GetSymbolForString("$color",true);
 int color2=SOURCESDK::TF2::keySystem.GetSymbolForString("$color2",true);
 for(auto key=values->m_pSub;key;key=key->m_pPeer){
  if(key->m_iKeyName==color||key->m_iKeyName==color2){
   m.Color=key->m_sValue;auto&rgb=key->m_iKeyName==color?m.Modulation:m.Tint;
   assert(std::sscanf(key->m_sValue,"[%f %f %f]",&rgb[0],&rgb[1],&rgb[2])==3);
  }
 }
 assert(!m.Color.empty());SceneDeleteMaterialKeys(values);return &m;
}
bool Dormant(void*){return fixture.Dormant;}
// Public VStudioRender025 slots: LockBoneMatrices=22, UnlockBoneMatrices=23,
// DrawModel=29, GetMaterialOverride=47. Slot 48 in TF2 is the concrete destructor.
void Override(void*,void**material,int*type){assert(std::this_thread::get_id()==fixture.EngineThread);*material=fixture.Override;*type=fixture.OverrideType;}
void Begin(void*){assert(std::this_thread::get_id()==fixture.EngineThread);++fixture.Locks;}void End(void*){--fixture.Locks;assert(fixture.Locks==0);}
unsigned short Find(void*,const char*name){assert(!strcmp(name,"models/player/scout.mdl"));++fixture.Refs;return 42;}
int Release(void*,unsigned short h){assert(h==42&&fixture.Refs>0);return --fixture.Refs;}
void*Header(void*,unsigned short h){assert(h==42);++fixture.HeaderLocks;return fixture.Header;}
void UnlockHeader(void*,unsigned short h){assert(h==42&&fixture.HeaderLocks>0);--fixture.HeaderLocks;}
void*Hardware(void*,unsigned short h){assert(h==42);return fixture.Hardware?&fixture:nullptr;}
bool Error(void*,unsigned short h){assert(h==42);return fixture.ErrorModel;}
SOURCESDK::matrix3x4_t*Allocate(void*,int count){assert(std::this_thread::get_id()==fixture.EngineThread);assert(count==fixture.Count);return fixture.Allocate?fixture.Allocated:nullptr;}
void Unlock(void*){++fixture.Unlocks;}
void Draw(void*,void*results,const SceneStudioInfo&info,SOURCESDK::matrix3x4_t*bones,float*flex,float*delayed,const SOURCESDK::Vector&origin,int flags){
 assert(std::this_thread::get_id()==fixture.EngineThread);
 assert(results==nullptr&&info.Header==fixture.Header&&info.Hardware==&fixture&&info.Body==fixture.Body);
 assert(info.ClientEntity==nullptr&&info.Decals==nullptr&&info.ColorMeshes==nullptr&&info.Lod==0);
 assert(flex==nullptr&&delayed==nullptr&&flags==(0x04|0x20|(fixture.Override?0:0x80)));assert(fixture.Locks==1&&fixture.Refs>=1&&fixture.HeaderLocks==fixture.Refs);
 // Match Source's studio draw: ColorModulate replaces $color, even on
 // an override material, before the shader multiplies it by $color2.
 if(fixture.Override){auto&m=*static_cast<Fixture::Material*>(fixture.Override);m.Modulation={1,1,1};for(int i=0;i<3;++i)fixture.DrawColor[i]=m.Modulation[i]*m.Tint[i];}
 else fixture.DrawColor={1,1,1};
 assert(bones==fixture.Allocated&&fixture.Unlocks>0&&origin.x==20);++fixture.DrawCalls;std::memcpy(fixture.Drawn,bones,sizeof(fixture.Drawn[0])*fixture.Count);
}
template<class T>void Slot(void**table,int i,T function){table[i]=reinterpret_cast<void*>(function);}
}
int main(){
 Slot(fixture.MaterialSystemSlots,70,CreateMaterial);Slot(fixture.MaterialSlots,12,MaterialInc);Slot(fixture.MaterialSlots,13,MaterialDec);Slot(fixture.MaterialSlots,42,MaterialError);Slot(fixture.StudioSlots,33,SetOverride);
 Slot(fixture.NetworkSlots,8,Dormant);Slot(fixture.StudioSlots,47,Override);Slot(fixture.StudioSlots,22,Allocate);Slot(fixture.StudioSlots,23,Unlock);Slot(fixture.StudioSlots,29,Draw);
 Slot(fixture.CacheSlots,6,Find);Slot(fixture.CacheSlots,8,Release);Slot(fixture.CacheSlots,40,Header);Slot(fixture.CacheSlots,41,UnlockHeader);Slot(fixture.CacheSlots,11,Hardware);Slot(fixture.CacheSlots,20,Error);Slot(fixture.CacheSlots,25,Begin);Slot(fixture.CacheSlots,26,End);
 fixture.InitHeader();for(int i=0;i<2;++i){for(int a=0;a<3;++a){fixture.Observed[i][a][a]=2;fixture.Observed[i][a][3]=fixture.Origin[a];}fixture.Observed[i][0][3]+=10+i;}
 using Cache=advancedfx::SceneCache;Cache::Entity entity;entity.Identity=42;std::copy(fixture.Origin,fixture.Origin+3,entity.Origin);
 std::string status;assert(Mirv_Tf2_CaptureScenePose(1,entity,status));auto captured=entity.ModelPose;
 assert(captured&&captured->Body==3&&captured->Local.size()==2&&captured->Bones.empty()&&captured->ModelInfo->Parents[1]==0);
 assert(captured->Local[0].Position[0]==10&&fixture.SetupCalls==1);
 assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&entity.ModelPose->ModelInfo==captured->ModelInfo);
 fixture.ModelName="models/player/soldier.mdl";assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&entity.ModelPose->ModelInfo!=captured->ModelInfo);fixture.ModelName="models/player/scout.mdl";assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&entity.ModelPose->ModelInfo==captured->ModelInfo);
 fixture.Observed[0][0][3]=999;assert(captured->Local[0].Position[0]==10); // Deep copy, not live bone cache.
 fixture.Dormant=true;int setups=fixture.SetupCalls;assert(!Mirv_Tf2_CaptureScenePose(1,entity,status)&&!entity.ModelPose&&fixture.SetupCalls==setups);fixture.Dormant=false;
 fixture.Visible=false;assert(!Mirv_Tf2_CaptureScenePose(1,entity,status));fixture.Visible=true;
 fixture.Setup=false;assert(!Mirv_Tf2_CaptureScenePose(1,entity,status));fixture.Setup=true;
 fixture.Put(156,129);assert(!Mirv_Tf2_CaptureScenePose(1,entity,status));fixture.Put(156,2);
 fixture.Observed[0][0][0]=std::numeric_limits<float>::infinity();assert(!Mirv_Tf2_CaptureScenePose(1,entity,status));fixture.Observed[0][0][0]=2;
 fixture.Observed[0][0][0]=fixture.Observed[0][1][0]=fixture.Observed[0][2][0]=0;assert(!Mirv_Tf2_CaptureScenePose(1,entity,status));fixture.Observed[0][0][0]=2;
 fixture.Put(8,456);assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&entity.ModelPose->ModelInfo!=captured->ModelInfo);fixture.Put(8,123);
 entity.ModelPose=captured;entity.Origin[0]=20;entity.Origin[1]=entity.Origin[2]=0;entity.Angles[1]=90;
 // Rendering must work using cached values alone, even if the source entity is now hidden/unavailable.
 fixture.Visible=false;fixture.Dormant=true;assert(Mirv_Tf2_DrawScenePose(entity,status));
 auto expectColor=[](int category){const auto rgb=Cache::CategoryColor(category);for(int i=0;i<3;++i)assert(std::abs(fixture.DrawColor[i]-rgb[i]/255.f)<1e-5);};expectColor(0);
 assert(fixture.DrawCalls==1&&fixture.Refs==1&&fixture.HeaderLocks==1&&fixture.Locks==0&&retirements.size()==1);
 ConsumeRenderQueue();assert(fixture.Refs==0&&fixture.HeaderLocks==0&&fixture.DrawCalls==1);assert(fixture.MaterialCount==1&&fixture.Materials[0].Refs==1&&!fixture.Override&&SOURCESDK::TF2::keySystem.Allocations==0);
 assert(std::abs(fixture.Drawn[0][0][3]-20)<1e-5&&std::abs(fixture.Drawn[0][1][3]-10)<1e-5&&std::abs(fixture.Drawn[0][2][3])<1e-5);
 double scale2=0;for(int a=0;a<3;++a)scale2+=fixture.Drawn[0][a][0]*fixture.Drawn[0][a][0];assert(std::abs(scale2-4)<1e-5);assert(captured->Local[0].Position[0]==10);
 queueAvailable=false;assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.DrawCalls==1&&fixture.Refs==0&&retirements.empty());queueAvailable=true;
 fixture.Interfaces=false;assert(!Mirv_Tf2_DrawScenePose(entity,status));fixture.Interfaces=true;
 fixture.Override=&fixture;assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.Refs==0&&fixture.Locks==0&&fixture.HeaderLocks==0);fixture.Override=nullptr;
 fixture.Hardware=false;assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.Refs==0&&fixture.Locks==0&&fixture.HeaderLocks==0);fixture.Hardware=true;
 fixture.Put(8,456);assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.Refs==0&&fixture.Locks==0&&fixture.HeaderLocks==0);fixture.Put(8,123);
 fixture.ErrorModel=true;assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.Refs==0&&fixture.Locks==0&&fixture.HeaderLocks==0);fixture.ErrorModel=false;
 fixture.Allocate=false;assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.Refs==0&&fixture.Locks==0&&fixture.HeaderLocks==0);fixture.Allocate=true;
 fixture.Put(156,129);assert(!Mirv_Tf2_DrawScenePose(entity,status)&&fixture.Refs==0&&fixture.Locks==0&&fixture.HeaderLocks==0);fixture.Put(156,2);
 assert(fixture.DrawCalls==1);
 assert(Mirv_Tf2_DrawScenePose(entity,status)&&Mirv_Tf2_DrawScenePose(entity,status));assert(fixture.DrawCalls==3&&fixture.Refs==2&&fixture.HeaderLocks==2&&retirements.size()==2);
 ConsumeRenderQueue();assert(fixture.DrawCalls==3&&fixture.Refs==0&&fixture.HeaderLocks==0&&fixture.Locks==0);
 // Different categories own different immutable materials; the Source override is restored.
 entity.Category=1;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(1);assert(fixture.MaterialCount==2&&!fixture.Override&&fixture.Materials[1].Refs==2&&fixture.Materials[0].Color!=fixture.Materials[1].Color);
 Mirv_Tf2_ReleaseSceneMaterials();assert(fixture.Materials[0].Refs==0&&fixture.Materials[1].Refs==1);ConsumeRenderQueue();assert(fixture.Materials[1].Refs==0);
 fixture.MaterialInterface=false;assert(Mirv_Tf2_DrawScenePose(entity,status)&&!fixture.Override);ConsumeRenderQueue();fixture.MaterialInterface=true;
 entity.Category=2;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(2);entity.Category=3;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(3);assert(!fixture.Override);ConsumeRenderQueue();Mirv_Tf2_ReleaseSceneMaterials();for(int i=0;i<fixture.MaterialCount;++i)assert(fixture.Materials[i].Refs==0);
 assert(SOURCESDK::TF2::keySystem.Allocations==0);
 // Reconstruct a locally interpolated skeleton and bone-merged gear using
 // only captured values, after moving the live entity out of the test.
 auto skeleton=std::make_shared<Cache::Model>();skeleton->Name="test";skeleton->Parents={-1,0};skeleton->Flags={0x40400,0x40400};skeleton->BoneNames={"root","hand"};skeleton->MergeBones={1,0};
 auto ownerPose=std::make_shared<Cache::Pose>();ownerPose->Root[0]=ownerPose->Root[5]=ownerPose->Root[10]=1;ownerPose->ModelInfo=skeleton;ownerPose->Local.resize(2);ownerPose->Local[1].Position[0]=10;
 auto q=Afx::Math::Quaternion::FromQREulerAngles({0,1.5707963267948966,0});ownerPose->Local[0].Rotation={float(q.W),float(q.X),float(q.Y),float(q.Z)};
 Cache::Entity owner;owner.Identity=7;owner.ModelPose=ownerPose;
 auto gearModel=std::make_shared<Cache::Model>(*skeleton);gearModel->Flags={0x400,0x400};gearModel->BoneNames={"HAND","tip"};
 auto gearPose=std::make_shared<Cache::Pose>(*ownerPose);gearPose->ModelInfo=gearModel;gearPose->Local[0]=Cache::Transform{};gearPose->Local[0].Position[0]=200;gearPose->Local[1].Position[0]=2;
 Cache::Entity gear;gear.Identity=(1ull<<63)|50;gear.OwnerIdentity=7;gear.Category=2;gear.ModelPose=gearPose;
 SOURCESDK::matrix3x4_t ownerOut[128],gearOut[128];assert(SceneBuildMatrices(owner,nullptr,ownerOut)&&SceneBuildMatrices(gear,&owner,gearOut));
 for(int r=0;r<3;++r)for(int c=0;c<4;++c)assert(std::abs(gearOut[0][r][c]-ownerOut[1][r][c])<1e-5);
 double childLength=0;for(int r=0;r<3;++r)childLength+=std::pow(gearOut[1][r][3]-gearOut[0][r][3],2);assert(std::abs(childLength-4)<1e-5);
 gear.OwnerIdentity=8;assert(SceneBuildMatrices(gear,&owner,gearOut)&&gearOut[0][0][3]==200);gear.OwnerIdentity=7;
 // A shear cannot be represented as TRS and must survive verbatim.
 Cache::Pose sheared;sheared.ModelInfo=skeleton;sheared.Root=ownerPose->Root;Cache::Matrix shear=sheared.Root;shear[1]=.3f;sheared.Bones={shear,shear};ScenePrepareLocalPose(sheared);assert(sheared.Local.empty()&&sheared.Bones[0]==shear);
 std::cout<<"TF2 scene pose capture/render fixture checks passed\n";
}
'''
source = out / 'scene_pose_native.cpp'
source.write_text(preamble + '\nnamespace SOURCESDK {\n' + math + '\n}\n' + invert + reader + adapter + fixtures)
command = ['g++', '-std=c++17', '-O0', '-g', '-Dabstract=', '-include', str(root / 'tests/cinematic/msvc_crt_compat.h'), '-I'+str(root), '-I'+str(root/'AfxHookSource'), str(source), str(root / 'shared/AfxMath.cpp'), '-o', str(out/'scene_pose_native')]
if args.sanitize:
    command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
result = subprocess.run(command, capture_output=True, text=True)
(out / 'build.log').write_text(result.stdout + result.stderr)
if result.returncode:
    print(result.stderr[-6500:])
    raise SystemExit(result.returncode)
result = subprocess.run([str(out/'scene_pose_native')], capture_output=True, text=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout + result.stderr)
if result.returncode:
    raise SystemExit(result.returncode)
# Exercise the real entity enumeration and its reused classifier separately.
collector = calcs[calcs.index('// Scene entity enumeration shares'):calcs.index('// End scene entity enumeration.')]
classifier = calcs[calcs.index('static int Mirv_Tf2_LabelCategory('):calcs.index('int Mirv_Tf2_CollectEntityLabels(')]
collector_source = r'''
#include "shared/SceneCache.h"
#include <cassert>
#include <cstring>
#include <cctype>
#include <iostream>
enum {MIRV_TF2_ENTITY_PLAYER,MIRV_TF2_ENTITY_PROJECTILE,MIRV_TF2_ENTITY_WEAPON,MIRV_TF2_ENTITY_COSMETIC,MIRV_TF2_ENTITY_OTHER};
constexpr unsigned SOURCESDK_TF2_ENT_ENTRY_MASK=4095;
namespace SOURCESDK {namespace TF2 {constexpr std::uintptr_t HTOOLHANDLE_INVALID=0;}}
struct MirvTf2SceneEntity {int Index=-1;advancedfx::SceneCache::Entity Snapshot;};
struct ClassInfo {void*Create=nullptr,*Event=nullptr;const char*Name="CTFProjectile_Rocket";};
struct Network {void**Vtable;bool Dormant=false;ClassInfo Class;};
void*slots[9]{};
const void*Class(void*p){return &static_cast<Network*>(p)->Class;}
bool Dormant(void*p){return static_cast<Network*>(p)->Dormant;}
struct CMirvTf2ClientRenderable {bool Visible=true;float Origin[3]{},Angles[3]{};bool ShouldDraw(){return Visible;}const void*GetRenderOrigin(){return Origin;}const void*GetRenderAngles(){return Angles;}void GetRenderBounds(float*a,float*b){for(int i=0;i<3;++i){a[i]=-1;b[i]=1;}};};
struct CMirvTf2ClientUnknown {int Index=1,User=42,ViewWeapon=0;bool Player=true,ToolAttached=false;const char*ToolClass=nullptr;CMirvTf2ClientUnknown*Owner=nullptr;std::uint32_t Handle=1;Network Net{slots};CMirvTf2ClientRenderable Render;void*GetClientNetworkable(){return &Net;}auto*GetClientRenderable(){return &Render;}const void*GetRefEHandle(){return &Handle;}};
std::vector<CMirvTf2ClientUnknown*> objects;
struct Tools {void*FirstEntity(){return objects.empty()?nullptr:objects[0];}void*NextEntity(void*p){auto it=std::find(objects.begin(),objects.end(),p);return it==objects.end()||++it==objects.end()?nullptr:*it;}int GetEntIndex(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Index;}bool IsPlayer(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Player;}
 int Attaches=0,Detaches=0;
 void*GetOwnerEntity(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Owner;}
 int GetOwningWeaponEntIndex(int i){for(auto*p:objects)if(p->Index==i)return p->ViewWeapon;return 0;}
 std::uintptr_t GetToolHandleForEntityByIndex(int i){for(auto*p:objects)if(p->Index==i&&p->ToolAttached)return reinterpret_cast<std::uintptr_t>(p);return 0;}
 std::uintptr_t AttachToEntity(void*p){++Attaches;static_cast<CMirvTf2ClientUnknown*>(p)->ToolAttached=true;return reinterpret_cast<std::uintptr_t>(p);}
 void DetachFromEntity(void*p){++Detaches;static_cast<CMirvTf2ClientUnknown*>(p)->ToolAttached=false;}
 const char*GetClassname(std::uintptr_t h){auto*p=reinterpret_cast<CMirvTf2ClientUnknown*>(h);return p->ToolClass?p->ToolClass:p->Net.Class.Name;}
} tools;
namespace SOURCESDK {namespace TF2 {using IClientTools=::Tools;using EntitySearchResult=void*;}}
struct CClientToolsTf2 {static CClientToolsTf2*Instance(){static CClientToolsTf2 i;return &i;}Tools*GetClientTools(){return &tools;}};
int Mirv_Tf2_GetMaxPlayers(){return 32;}
bool Mirv_Tf2_GetPlayerInfo(int i,char*,int,int*u){for(auto*p:objects)if(p->Index==i){*u=p->User;return true;}return false;}
template<typename R,typename...Args>R SceneInterfaceCall(void*p,size_t slot,Args...args){return reinterpret_cast<R(*)(void*,Args...)>((*static_cast<void***>(p))[slot])(p,args...);}
'''+classifier+collector+r'''
int main(){
 slots[2]=reinterpret_cast<void*>(Class);slots[8]=reinterpret_cast<void*>(Dormant);
 CMirvTf2ClientUnknown player,rocket;rocket.Index=40;rocket.Handle=(2<<12)|40;rocket.Player=false;objects={&player,&rocket};
 std::vector<MirvTf2SceneEntity> out;Mirv_Tf2_CollectSceneEntities(false,false,false,out);assert(out.size()==1&&out[0].Snapshot.Identity==42);
 Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==2&&out[1].Snapshot.Category==1);auto id=out[1].Snapshot.Identity;assert(id==((1ull<<63)|rocket.Handle));
 rocket.Handle=(3<<12)|40;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==2&&id!=out[1].Snapshot.Identity);
 rocket.Net.Dormant=true;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1);rocket.Net.Dormant=false;
 rocket.Render.Visible=false;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1);rocket.Render.Visible=true;
 rocket.Handle=0xffffffff;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1);rocket.Handle=41;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1);rocket.Handle=40;
 rocket.Render.Origin[0]=NAN;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1);rocket.Render.Origin[0]=0;
 for(auto name:{"CTFRocketLauncher","CTFGrenadeLauncher","CTFFlareGun","CTFCrossbow","CTFCompoundBow","CTFCleaver","CTFRocketPack","CTFWearable"}){rocket.Net.Class.Name=name;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1);}
 for(auto name:{"CTFProjectile_Rocket","CTFGrenadePipebombProjectile","CTFProjectile_Arrow","CTFProjectile_Flare","CTFProjectile_Cleaver"}){rocket.Net.Class.Name=name;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==2);}
 player.User=0;Mirv_Tf2_CollectSceneEntities(true,false,false,out);assert(out.size()==1&&out[0].Index==40);
 player.User=42;CMirvTf2ClientUnknown other,weapon,hat;
 other.Index=2;other.User=43;weapon.Index=50;weapon.Handle=(3<<12)|50;weapon.Player=false;weapon.Owner=&player;weapon.Net.Class.Name="CTFSniperRifle";weapon.ToolClass="tf_weapon_sniperrifle";
 hat.Index=51;hat.Handle=(5<<12)|51;hat.Player=false;hat.Owner=&player;hat.Net.Class.Name="CTFWearable";
 objects={&player,&other,&rocket,&weapon,&hat};
 Mirv_Tf2_CollectSceneEntities(true,true,true,out);assert(out.size()==5);assert(out[3].Snapshot.Category==2&&out[3].Snapshot.OwnerIdentity==42&&out[4].Snapshot.Category==3&&out[4].Snapshot.OwnerIdentity==42);assert(tools.Attaches==tools.Detaches&&!weapon.ToolAttached);
 Mirv_Tf2_CollectSceneEntities(false,true,false,out);assert(out.size()==3&&out.back().Index==50);
 Mirv_Tf2_CollectSceneEntities(false,false,true,out);assert(out.size()==3&&out.back().Index==51);
 hat.Net.Class.Name="CTFWearableVM";Mirv_Tf2_CollectSceneEntities(false,true,true,out);assert(out.size()==3&&out.back().Index==50);hat.Net.Class.Name="CTFWearable";
 weapon.Render.Visible=false;Mirv_Tf2_CollectSceneEntities(false,true,true,out);assert(out.size()==3&&out.back().Index==51);weapon.Render.Visible=true;
 weapon.ViewWeapon=50;Mirv_Tf2_CollectSceneEntities(false,true,true,out);assert(out.size()==3&&out.back().Index==51);weapon.ViewWeapon=0;
 player.Net.Dormant=true;Mirv_Tf2_CollectSceneEntities(true,true,true,out);assert(out.size()==2);player.Net.Dormant=false;
 weapon.Owner=&other;Mirv_Tf2_CollectSceneEntities(false,true,false,out);assert(out.size()==3&&out.back().Snapshot.OwnerIdentity==43);
 weapon.Owner=&hat;hat.Owner=&weapon;Mirv_Tf2_CollectSceneEntities(false,true,true,out);assert(out.size()==2); // Bounded cyclic ownership.
 hat.Owner=&player;Mirv_Tf2_CollectSceneEntities(false,true,true,out);assert(out.size()==4&&out[2].Snapshot.OwnerIdentity==42); // Nested owner resolves to player.
 weapon.Owner=nullptr;Mirv_Tf2_CollectSceneEntities(false,true,true,out);assert(out.size()==3&&out.back().Index==51); // Dropped gear is outside this pass.
 weapon.Owner=&player;weapon.ToolAttached=true;int detaches=tools.Detaches;Mirv_Tf2_CollectSceneEntities(false,true,false,out);assert(out.size()==3&&weapon.ToolAttached&&tools.Detaches==detaches);weapon.ToolAttached=false;
 objects.clear();Mirv_Tf2_CollectSceneEntities(true,true,true,out);assert(out.empty());
 std::cout<<"TF2 scene entity lifecycle/classification checks passed\n";
}
'''
source = out/'scene_entities_native.cpp'
source.write_text(collector_source)
command = ['g++','-std=c++17','-O0','-g','-Dabstract=','-I'+str(root),str(source),'-o',str(out/'scene_entities_native')]
if args.sanitize:
    command[1:1] = ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
result = subprocess.run(command,capture_output=True,text=True)
(out/'entities-build.log').write_text(result.stdout+result.stderr)
if result.returncode:
    print(result.stderr[-6500:])
    raise SystemExit(result.returncode)
result = subprocess.run([str(out/'scene_entities_native')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout+result.stderr)
raise SystemExit(result.returncode)
