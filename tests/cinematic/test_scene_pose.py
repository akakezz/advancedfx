"""Exercise the actual TF2 pose capture/draw adapter against public-interface fixtures."""
from pathlib import Path
import argparse, os, subprocess, tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', type=Path, default=Path(tempfile.gettempdir()) / 'hlae-scene-pose-tests')
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--windows-abi-compiler', type=Path, help='Optional Zig compiler for real MSVC ABI probes')
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
bool Mirv_Tf2_CaptureScenePose(int,advancedfx::SceneCache::Entity&,std::string&,void* = nullptr);
bool Mirv_Tf2_DrawScenePose(const advancedfx::SceneCache::Entity&,std::string&,const advancedfx::SceneCache::Entity* = nullptr);
void Mirv_Tf2_SceneRenderFrame();
#include <thread>
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <cfloat>
#include <map>
#include <cstdio>
#include <cstdlib>
#define _WIN64 1
#define MIRV_TF2_MAX_BONES 128
constexpr int MIRV_TF2_ENTITY_PLAYER=0,MIRV_TF2_ENTITY_WEAPON=2,MIRV_TF2_ENTITY_COSMETIC=3,MIRV_TF2_ENTITY_RAGDOLL=5,MIRV_TF2_ENTITY_BUILDING=7,MIRV_TF2_ENTITY_PROP=8;
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
 int ModelType=3;bool Dormant=false,Visible=true,Setup=true,Hardware=true,ErrorModel=false,Interfaces=true,Allocate=true;
 void *Override=nullptr;int OverrideType=0;void*FileSystem=nullptr;
 int Count=2,Checks=0,SetupCalls=0,DrawCalls=0,Locks=0,Refs=0,Unlocks=0,HeaderLocks=0;
 std::thread::id EngineThread=std::this_thread::get_id();
 std::string ModelName="models/player/scout.mdl";int Body=3;float Origin[3]{1,2,3},Angles[3]{};
 SOURCESDK::matrix3x4_t Observed[128]{},Allocated[128]{},Drawn[128]{};
 void *StudioSlots[49]{},*CacheSlots[42]{},*NetworkSlots[9]{};
 Object Studio{StudioSlots},Cache{CacheSlots},Network{NetworkSlots};
 std::array<float,3> DrawColor{};bool MaterialInterface=true;void*MaterialSlots[43]{},*MaterialSystemSlots[71]{};Object MaterialSystem{MaterialSystemSlots};
 struct Material: Object {int Refs=0;std::string Color;std::array<float,3> Modulation{1,1,1},Tint{1,1,1};};Material Materials[10];int MaterialCount=0;

 void Put(int offset,int value){std::memcpy(Header+offset,&value,4);}
 void InitHeader(){Put(0,0x54534449);Put(4,48);Put(8,123);Put(76,sizeof(Header));Put(156,Count);Put(160,164);for(int i=0;i<Count&&i<8;++i){Put(164+i*216+4,i-1);Put(164+i*216+160,0x400);}}
} fixture;

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

struct CMirvTf2ClientRenderable {
 const void*GetModel(){return &fixture;}bool ShouldDraw(){return fixture.Visible;}int GetBody(){return fixture.Body;}
 bool SetupBones(SOURCESDK::matrix3x4_t*out,int maximum,int mask,float){++fixture.SetupCalls;assert(maximum==128&&mask==0x7ff00);if(!fixture.Setup)return false;std::memcpy(out,fixture.Observed,sizeof(fixture.Observed));return true;}
} renderable;
struct CMirvTf2ClientUnknown {std::uint32_t Handle=(3<<12)|2050;const void*GetRefEHandle(){return &Handle;}bool HasNetwork=true;void*GetClientNetworkable(){return HasNetwork?&fixture.Network:nullptr;}CMirvTf2ClientRenderable*GetClientRenderable(){return &renderable;}} unknown;
void*Mirv_Tf2_DiagnosticEntity(int index){return index==1?&unknown:nullptr;}
struct ModelInfo {int GetModelType(const void*){return fixture.ModelType;}const char*GetModelName(const void*){return fixture.ModelName.c_str();}void*GetStudiomodel(const void*){return fixture.Header;}} modelInfo;
ModelInfo*Mirv_Tf2_GetModelInfo(){return &modelInfo;}
struct Globals{float curtime_get(){return 2;}} globals;
struct View{Globals*GetGlobals(){return &globals;}} g_Hook_VClient_RenderView;
void*Factory(const char*name,int*){if(!fixture.Interfaces)return nullptr;if(!strcmp(name,"VStudioRender025"))return &fixture.Studio;if(!strcmp(name,"MDLCache004"))return &fixture.Cache;if(!strcmp(name,"VMaterialSystem082"))return fixture.MaterialInterface?&fixture.MaterialSystem:nullptr;if(!strcmp(name,"VBaseFileSystem011"))return fixture.FileSystem;assert(false);return nullptr;}
auto g_AppSystemFactory=Factory;
struct BrushEngine {std::string Level,Directory;const char*GetLevelName(){return Level.c_str();}const char*GetGameDirectory(){return Directory.c_str();}} brushEngine;auto*g_VEngineClient=&brushEngine;
bool queueAvailable=true;
std::vector<std::unique_ptr<CMaterialSystemFunctor>> retirements;
bool MaterialSystem_CanExecuteOnRenderThread(){return queueAvailable;}
bool MaterialSystem_ExecuteOnRenderThread(CMaterialSystemFunctor*f){assert(std::this_thread::get_id()==fixture.EngineThread);retirements.emplace_back(f);return true;}
void ConsumeRenderQueue(bool drain=true,bool discard=false){std::thread worker([&]{if(!discard)for(auto&f:retirements)(*f)();retirements.clear();});worker.join();if(drain)Mirv_Tf2_SceneRenderFrame();}

'''
fixtures = r'''
namespace {
void SetOverride(void*,void*material,int type){assert(std::this_thread::get_id()==fixture.EngineThread);fixture.Override=material;fixture.OverrideType=type;}
void MaterialInc(void*p){++static_cast<Fixture::Material*>(p)->Refs;}
void MaterialDec(void*p){assert(std::this_thread::get_id()==fixture.EngineThread);auto&m=*static_cast<Fixture::Material*>(p);assert(m.Refs>0);--m.Refs;}
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
int Release(void*,unsigned short h){assert(std::this_thread::get_id()==fixture.EngineThread);assert(h==42&&fixture.Refs>0);return --fixture.Refs;}
void*Header(void*,unsigned short h){assert(h==42);++fixture.HeaderLocks;return fixture.Header;}
void UnlockHeader(void*,unsigned short h){assert(std::this_thread::get_id()==fixture.EngineThread);assert(h==42&&fixture.HeaderLocks>0);--fixture.HeaderLocks;}
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

struct BrushFileSystemFixture : SceneBaseFileSystem
{
 std::vector<unsigned char> Data;size_t At=0;int Opens=0,Closes=0;
 int Read(void*out,int n,void*)override{if(n<0||At>Data.size()||size_t(n)>Data.size()-At)return 0;std::memcpy(out,Data.data()+At,n);At+=n;return n;}
 int Write(const void*,int,void*)override{return 0;}
 void*Open(const char*,const char*options,const char*path)override{assert(!strcmp(options,"rb")&&!strcmp(path,"GAME"));++Opens;At=0;return this;}
 void Close(void*handle)override{assert(handle==this);++Closes;}
 void Seek(void*,int at,int mode)override{assert(at>=0&&mode==0);At=size_t(at);}
 unsigned int Tell(void*)override{return unsigned(At);}
 unsigned int Size(void*)override{return unsigned(Data.size());}
 unsigned int Size(const char*,const char*)override{assert(false&&"filename overload must not serve handle queries");return 0;}
};
int main(){
 Slot(fixture.MaterialSystemSlots,70,CreateMaterial);Slot(fixture.MaterialSlots,12,MaterialInc);Slot(fixture.MaterialSlots,13,MaterialDec);Slot(fixture.MaterialSlots,42,MaterialError);Slot(fixture.StudioSlots,33,SetOverride);
 Slot(fixture.NetworkSlots,8,Dormant);Slot(fixture.StudioSlots,47,Override);Slot(fixture.StudioSlots,22,Allocate);Slot(fixture.StudioSlots,23,Unlock);Slot(fixture.StudioSlots,29,Draw);
 Slot(fixture.CacheSlots,6,Find);Slot(fixture.CacheSlots,8,Release);Slot(fixture.CacheSlots,40,Header);Slot(fixture.CacheSlots,41,UnlockHeader);Slot(fixture.CacheSlots,11,Hardware);Slot(fixture.CacheSlots,20,Error);Slot(fixture.CacheSlots,25,Begin);Slot(fixture.CacheSlots,26,End);
 fixture.InitHeader();for(int i=0;i<2;++i){for(int a=0;a<3;++a){fixture.Observed[i][a][a]=2;fixture.Observed[i][a][3]=fixture.Origin[a];}fixture.Observed[i][0][3]+=10+i;}
 BrushFileSystemFixture vertexFs;auto&vertexFixture=vertexFs.Data;fixture.FileSystem=&vertexFs;
 float labelAnchor[3];int savedSetups=fixture.SetupCalls;assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==12&&labelAnchor[2]==3);fixture.Put(172,1);fixture.Put(176,1040);fixture.Put(1044,1);fixture.Put(1048,12);fixture.Put(1052,1);float hitbox[6]={0,0,0,2,2,2};std::memcpy(fixture.Header+1060,hitbox,24);assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==14&&labelAnchor[1]==4&&labelAnchor[2]==7);fixture.Put(1048,999999);assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==12);fixture.Put(172,0);fixture.Put(232,1);fixture.Put(236,1100);fixture.Put(1104,2);fixture.Put(1108,1);fixture.Put(1112,16);fixture.Put(1116+148+72,1);fixture.Put(1116+148+76,148);float meshCenter[3]={20,30,40};std::memcpy(fixture.Header+1116+296+36,meshCenter,12);for(int r=0;r<3;++r){float identity[4]={};identity[r]=1;std::memcpy(fixture.Header+164+96+r*16,identity,16);}assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==12&&labelAnchor[1]==2&&labelAnchor[2]==3);fixture.ModelName="models/player/gibs/offset_piece.mdl";vertexFixture.resize(160);auto vput=[&](int at,int value){std::memcpy(vertexFixture.data()+at,&value,4);};vput(0,0x56534449);vput(4,4);vput(8,123);vput(12,1);vput(16,2);vput(56,64);fixture.Put(1116+148+80,2);fixture.Put(1116+148+84,0);for(int r=0;r<3;++r){float bind[4]={};bind[r]=1;bind[3]=r==0?-5:0;std::memcpy(fixture.Header+164+216+96+r*16,bind,16);}for(int i=0;i<2;++i){float weight=1,point[3]={20.f+2*i,30.f+2*i,40.f+2*i};std::memcpy(vertexFixture.data()+64+i*48,&weight,4);vertexFixture[64+i*48+12]=1;vertexFixture[64+i*48+15]=1;std::memcpy(vertexFixture.data()+64+i*48+16,point,12);}assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==44&&labelAnchor[1]==64&&labelAnchor[2]==85);auto rawVertices=vertexFixture;vertexFixture.resize(184);std::memcpy(vertexFixture.data()+88,rawVertices.data()+64,96);vput(48,2);vput(52,64);vput(56,88);vput(64,0);vput(68,1);vput(72,1);vput(76,0);vput(80,0);vput(84,1);fixture.Put(1116+148+80,1);fixture.ModelName="models/player/gibs/fixup_piece.mdl";assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==46&&labelAnchor[1]==66&&labelAnchor[2]==87);vput(8,999);fixture.ModelName="models/player/gibs/mismatched_piece.mdl";assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==12);std::string vertexFailure;assert(!Mirv_Tf2_GibVertexAnchor(&renderable,fixture.Header,2,fixture.Observed,labelAnchor,&vertexFailure)&&vertexFailure.find("checksum")!=std::string::npos);vput(8,123);assert(Mirv_Tf2_GibVertexAnchor(&renderable,fixture.Header,2,fixture.Observed,labelAnchor,&vertexFailure)&&vertexFailure.empty());vertexFixture.clear();fixture.ModelName="models/player/scout.mdl";fixture.Body=0;assert(Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor)&&labelAnchor[0]==12);fixture.Body=3;fixture.Put(232,0);fixture.Setup=false;assert(!Mirv_Tf2_GibLabelAnchor(&renderable,labelAnchor));fixture.Setup=true;fixture.SetupCalls=savedSetups;assert(vertexFs.Opens==vertexFs.Closes);fixture.FileSystem=nullptr;
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
 // A slow or discarded render queue retains ownership without unbounded growth.
 for(unsigned i=0;i<SceneMaxOutstanding;++i)assert(Mirv_Tf2_DrawScenePose(entity,status));
 assert(!Mirv_Tf2_DrawScenePose(entity,status));unsigned pending=0,peak=0;Mirv_Tf2_SceneRenderStatus(pending,peak);assert(pending==SceneMaxOutstanding&&peak==SceneMaxOutstanding);
 ConsumeRenderQueue(false,true);assert(fixture.Refs==int(SceneMaxOutstanding)&&fixture.HeaderLocks==int(SceneMaxOutstanding));
 Mirv_Tf2_SceneRenderFrame();Mirv_Tf2_SceneRenderStatus(pending,peak);assert(!pending&&!fixture.Refs&&!fixture.HeaderLocks);
 assert(Mirv_Tf2_DrawScenePose(entity,status));ConsumeRenderQueue(false);assert(fixture.Refs==1&&fixture.HeaderLocks==1);Mirv_Tf2_SceneRenderFrame();assert(!fixture.Refs&&!fixture.HeaderLocks);
 // Repeated idle draws return to the same live resource baseline.
 for(int frame=0;frame<100;++frame){for(int model=0;model<32;++model)assert(Mirv_Tf2_DrawScenePose(entity,status));ConsumeRenderQueue();assert(!fixture.Refs&&!fixture.HeaderLocks&&!fixture.Override);}
 entity.Category=2;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(2);entity.Category=3;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(3);entity.Category=5;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(5);entity.Category=6;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(6);entity.Category=7;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(7);entity.Category=8;assert(Mirv_Tf2_DrawScenePose(entity,status));expectColor(8);assert(!fixture.Override);ConsumeRenderQueue();Mirv_Tf2_ReleaseSceneMaterials();for(int i=0;i<fixture.MaterialCount;++i)assert(fixture.Materials[i].Refs==0);
 assert(SOURCESDK::TF2::keySystem.Allocations==0);
 // Reconstruct a locally interpolated skeleton and bone-merged gear using
 // only captured values, after moving the live entity out of the test.
 auto skeleton=std::make_shared<Cache::Model>();skeleton->Name="test";skeleton->Parents={-1,0};skeleton->Flags={0x40400,0x40400};skeleton->BoneNames={"root","hand"};skeleton->MergeBones={1,0};
 auto ownerPose=std::make_shared<Cache::Pose>();ownerPose->Root[0]=ownerPose->Root[5]=ownerPose->Root[10]=1;ownerPose->ModelInfo=skeleton;ownerPose->Local.resize(2);ownerPose->Local[1].Position[0]=10;
 auto q=Afx::Math::Quaternion::FromQREulerAngles({0,1.5707963267948966,0});ownerPose->Local[0].Rotation={float(q.W),float(q.X),float(q.Y),float(q.Z)};
 Cache::Entity owner;owner.Identity=7;owner.ModelPose=ownerPose;
 auto gearModel=std::make_shared<Cache::Model>(*skeleton);gearModel->Flags={0x400,0x400};gearModel->MergeBones.clear();gearModel->BoneNames={"HAND","tip"};
 auto gearPose=std::make_shared<Cache::Pose>(*ownerPose);gearPose->ModelInfo=gearModel;gearPose->Local[0]=Cache::Transform{};gearPose->Local[0].Position[0]=200;gearPose->Local[1].Position[0]=2;
 Cache::Entity gear;gear.Identity=(1ull<<63)|50;gear.OwnerIdentity=7;gear.Category=2;gear.ModelPose=gearPose;
 SOURCESDK::matrix3x4_t ownerOut[128],gearOut[128];assert(SceneBuildMatrices(owner,nullptr,ownerOut)&&SceneBuildMatrices(gear,&owner,gearOut));
 SOURCESDK::matrix3x4_t cachedBone;assert(SceneCachedBoneFrame(gear,&owner,"hand",cachedBone));for(int r=0;r<3;++r)for(int c=0;c<4;++c)assert(std::abs(cachedBone[r][c]-gearOut[0][r][c])<1e-5);assert(!SceneCachedBoneFrame(gear,&owner,"missing",cachedBone)&&!SceneCachedBoneFrame(gear,&owner,"",cachedBone));auto absentPose=gear;absentPose.ModelPose.reset();assert(!SceneCachedBoneFrame(absentPose,&owner,"hand",cachedBone));

 for(int r=0;r<3;++r)for(int c=0;c<4;++c)assert(std::abs(gearOut[0][r][c]-ownerOut[1][r][c])<1e-5);
 double childLength=0;for(int r=0;r<3;++r)childLength+=std::pow(gearOut[1][r][3]-gearOut[0][r][3],2);assert(std::abs(childLength-4)<1e-5);
 gear.OwnerIdentity=8;assert(SceneBuildMatrices(gear,&owner,gearOut)&&gearOut[0][0][3]==200);gear.OwnerIdentity=7;owner.Category=5;assert(SceneBuildMatrices(gear,&owner,gearOut));for(int r=0;r<3;++r)for(int c=0;c<4;++c)assert(std::abs(gearOut[0][r][c]-ownerOut[1][r][c])<1e-5);for(int hostCategory:{7,8}){owner.Category=hostCategory;assert(SceneBuildMatrices(gear,&owner,gearOut));for(int r=0;r<3;++r)for(int c=0;c<4;++c)assert(std::abs(gearOut[0][r][c]-ownerOut[1][r][c])<1e-5);}owner.Category=0;
 // A shear cannot be represented as TRS and must survive verbatim.
 Cache::Pose sheared;sheared.ModelInfo=skeleton;sheared.Root=ownerPose->Root;Cache::Matrix shear=sheared.Root;shear[1]=.3f;sheared.Bones={shear,shear};ScenePrepareLocalPose(sheared);assert(sheared.Local.size()==2&&sheared.Bones.empty()&&sheared.Fixed.size()==1&&sheared.Fixed[0].Index==0&&sheared.Fixed[0].Value==shear&&Cache::ValidPose(sheared));
 // Relative shear is retained while its descendants reconstruct normally.
 Cache::Entity mixedEntity;mixedEntity.ModelPose=std::make_shared<Cache::Pose>(sheared);SOURCESDK::matrix3x4_t mixedOut[128];assert(SceneBuildMatrices(mixedEntity,nullptr,mixedOut));for(int r=0;r<3;++r)for(int c=0;c<4;++c)assert(std::abs(mixedOut[0][r][c]-shear[r*4+c])<1e-5);
 // Singular-parent exceptions retain world space and follow root retargeting.
 Cache::Pose singular;singular.ModelInfo=skeleton;singular.Root=ownerPose->Root;Cache::Matrix zero=singular.Root;zero[5]=0;singular.Bones={zero,ownerPose->Root};ScenePrepareLocalPose(singular);assert(singular.Fixed.size()==2&&singular.Fixed[1].World&&singular.Fixed[1].Reason==Cache::BoneFallback::SingularParent&&Cache::ValidPose(singular));mixedEntity.ModelPose=std::make_shared<Cache::Pose>(singular);mixedEntity.Origin[0]=25;assert(SceneBuildMatrices(mixedEntity,nullptr,mixedOut)&&mixedOut[1][0][3]==25);
 fixture.Visible=true;fixture.Dormant=false;fixture.ModelType=1;int brushSetups=fixture.SetupCalls;assert(!Mirv_Tf2_CaptureScenePose(1,entity,status)&&fixture.SetupCalls==brushSetups&&status.find("Brush model")!=std::string::npos);fixture.ModelType=3;
 // Brush capture reads file geometry once and never calls studio SetupBones.
 auto brushBsp=BrushFixture();auto brushDir=std::filesystem::temp_directory_path()/"hlae-scene-brush-fixture";std::filesystem::create_directories(brushDir/"maps");auto brushPath=brushDir/"maps/test.bsp";{std::ofstream output(brushPath,std::ios::binary);output.write(reinterpret_cast<const char*>(brushBsp.data()),brushBsp.size());}
 brushEngine.Directory=brushDir.string();brushEngine.Level="maps/test.bsp";fixture.ModelType=1;fixture.ModelName="*1";assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&fixture.SetupCalls==brushSetups&&entity.ModelPose->ModelInfo->WireEdges.size()==5&&Cache::ValidPose(*entity.ModelPose));auto brushMetadata=entity.ModelPose->ModelInfo;std::filesystem::remove(brushPath);assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&entity.ModelPose->ModelInfo==brushMetadata);assert(!Mirv_Tf2_DrawScenePose(entity,status));BrushFileSystemFixture brushFs;brushFs.Data=brushBsp;fixture.FileSystem=&brushFs;brushEngine.Level="maps/test-fs.bsp";assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&brushFs.Opens==1&&brushFs.Closes==1&&entity.ModelPose->ModelInfo->WireEdges.size()==5);auto fsMetadata=entity.ModelPose->ModelInfo;assert(Mirv_Tf2_CaptureScenePose(1,entity,status)&&brushFs.Opens==1&&entity.ModelPose->ModelInfo==fsMetadata);fixture.FileSystem=nullptr;std::filesystem::remove_all(brushDir);fixture.ModelType=3;fixture.ModelName="models/player/scout.mdl";
 // Client-only gibs can have no networkable and share entindex -1. Capture uses the observed object.
 unknown.HasNetwork=false;fixture.Visible=true;fixture.Dormant=true;entity.Identity=(1ull<<63)|unknown.Handle;entity.Category=6;
 assert(Mirv_Tf2_CaptureScenePose(-1,entity,status,&unknown)&&entity.ModelPose&&Cache::ValidPose(*entity.ModelPose));
 auto nativeCaptured=entity.ModelPose;unknown.Handle+=(1<<12);int oldSetups=fixture.SetupCalls;assert(!Mirv_Tf2_CaptureScenePose(-1,entity,status,&unknown)&&fixture.SetupCalls==oldSetups&&!entity.ModelPose);
 entity.Identity=(1ull<<63)|unknown.Handle;fixture.Visible=false;assert(!Mirv_Tf2_CaptureScenePose(-1,entity,status,&unknown));fixture.Visible=true;
 assert(!Mirv_Tf2_CaptureScenePose(-1,entity,status));unknown.HasNetwork=true;fixture.Dormant=false;
 std::cout<<"TF2 scene pose capture/render fixture checks passed\n";
}
'''
source = out / 'scene_pose_native.cpp'
gib_anchor=calcs[calcs.index('// VVD vertices retain'):calcs.index('int Mirv_Tf2_CollectEntityLabels(')]
source.write_text(preamble + '\nnamespace SOURCESDK {\n' + math + '\n}\n' + invert + reader + gib_anchor + adapter + fixtures)
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
classifier = calcs[calcs.index('static int Mirv_Tf2_LabelCategory('):calcs.index('static bool Mirv_Tf2_LabelDebris(')]
collector_source = r'''
#include "shared/SceneCache.h"
#include <cassert>
#include <cstring>
#include <cctype>
#include <iostream>
enum {MIRV_TF2_ENTITY_PLAYER,MIRV_TF2_ENTITY_PROJECTILE,MIRV_TF2_ENTITY_WEAPON,MIRV_TF2_ENTITY_COSMETIC,MIRV_TF2_ENTITY_OTHER,MIRV_TF2_ENTITY_RAGDOLL,MIRV_TF2_ENTITY_GIB,MIRV_TF2_ENTITY_BUILDING,MIRV_TF2_ENTITY_PROP};
constexpr unsigned SOURCESDK_TF2_ENT_ENTRY_MASK=4095;
namespace SOURCESDK {namespace TF2 {constexpr std::uintptr_t HTOOLHANDLE_INVALID=0;}}
struct MirvTf2SceneEntity {int Index=-1;void*ObservedObject=nullptr;bool HasMoveParent=false;std::uint32_t MoveParent=0xffffffff;advancedfx::SceneCache::Entity Snapshot;};
struct ClassInfo {void*Create=nullptr,*Event=nullptr;const char*Name="CTFProjectile_Rocket";};
struct Network {void**Vtable;bool Dormant=false;ClassInfo Class;};
void*slots[9]{};
const void*Class(void*p){return &static_cast<Network*>(p)->Class;}
bool Dormant(void*p){return static_cast<Network*>(p)->Dormant;}

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

struct CMirvTf2ClientRenderable {const char*ModelName="models/props/crate.mdl";const void*GetModel(){return ModelName;}bool Visible=true;float Origin[3]{},Angles[3]{};bool ShouldDraw(){return Visible;}const void*GetRenderOrigin(){return Origin;}const void*GetRenderAngles(){return Angles;}void GetRenderBounds(float*a,float*b){for(int i=0;i<3;++i){a[i]=-1;b[i]=1;}};};
struct CMirvTf2ClientUnknown {int Index=1,User=42,ViewWeapon=0;bool Player=true,ToolAttached=false,HasNetwork=true,Alive=true,NoDraw=false,ParentKnown=false;std::uint32_t MoveParent=0xffffffff;const char*ToolClass=nullptr;CMirvTf2ClientUnknown*Owner=nullptr;std::uint32_t Handle=1;Network Net{slots};CMirvTf2ClientRenderable Render;void*GetClientNetworkable(){return HasNetwork?&Net:nullptr;}auto*GetClientRenderable(){return &Render;}const void*GetRefEHandle(){return &Handle;}};
std::vector<CMirvTf2ClientUnknown*> objects;
struct Tools {void*FirstEntity(){return objects.empty()?nullptr:objects[0];}void*NextEntity(void*p){auto it=std::find(objects.begin(),objects.end(),p);return it==objects.end()||++it==objects.end()?nullptr:*it;}int GetEntIndex(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Index;}bool IsPlayer(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Player;}
 int Attaches=0,Detaches=0;
 void*GetOwnerEntity(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Owner;}
 int GetOwningWeaponEntIndex(int i){for(auto*p:objects)if(p->Index==i)return p->ViewWeapon;return 0;}
 int GetNumRecordables(){int n=0;for(auto*p:objects)if(p->ToolAttached)++n;return n;}std::uintptr_t GetRecordable(int i){for(auto*p:objects)if(p->ToolAttached&&i--==0)return reinterpret_cast<std::uintptr_t>(p);return 0;}
 std::uintptr_t GetToolHandleForEntityByIndex(int i){for(auto*p:objects)if(int(p->Handle&4095)==i&&p->ToolAttached)return reinterpret_cast<std::uintptr_t>(p);return 0;}
 std::uintptr_t AttachToEntity(void*p){++Attaches;static_cast<CMirvTf2ClientUnknown*>(p)->ToolAttached=true;return reinterpret_cast<std::uintptr_t>(p);}
 void DetachFromEntity(void*p){++Detaches;static_cast<CMirvTf2ClientUnknown*>(p)->ToolAttached=false;}
 const char*GetClassname(std::uintptr_t h){auto*p=reinterpret_cast<CMirvTf2ClientUnknown*>(h);return p->ToolClass?p->ToolClass:p->Net.Class.Name;}
} tools;
namespace SOURCESDK {namespace TF2 {using IClientTools=::Tools;using EntitySearchResult=void*;}}
struct CClientToolsTf2 {static CClientToolsTf2*Instance(){static CClientToolsTf2 i;return &i;}Tools*GetClientTools(){return &tools;}};
struct ModelInfo{int GetModelType(const void*model){auto n=static_cast<const char*>(model);return n[0]=='*'?1:strstr(n,".vmt")?2:3;}const char*GetModelName(const void*model){return static_cast<const char*>(model);}} modelInfo;ModelInfo*Mirv_Tf2_GetModelInfo(){return &modelInfo;}
bool Mirv_Tf2_SceneEntityVisible(const void*object,const void*){return !static_cast<const CMirvTf2ClientUnknown*>(object)->NoDraw;}
bool Mirv_Tf2_SceneMoveParent(const void*object,const void*,std::uint32_t&parent){auto*p=static_cast<const CMirvTf2ClientUnknown*>(object);parent=p->MoveParent;return p->ParentKnown;}
bool Mirv_Tf2_ScenePlayerVisible(const void*object,const void*){auto*p=static_cast<const CMirvTf2ClientUnknown*>(object);return p->Alive&&!p->NoDraw;}
int Mirv_Tf2_GetMaxPlayers(){return 32;}
bool Mirv_Tf2_GetPlayerInfo(int i,char*,int,int*u){for(auto*p:objects)if(p->Index==i){*u=p->User;return true;}return false;}
template<typename R,typename...Args>R SceneInterfaceCall(void*p,size_t slot,Args...args){return reinterpret_cast<R(*)(void*,Args...)>((*static_cast<void***>(p))[slot])(p,args...);}
'''+calcs[calcs.index('std::uintptr_t Mirv_Tf2_AttachToolEntity('):calcs.index('int Mirv_Tf2_EntityAddress(')]+classifier+collector+r'''
int main(){
 slots[2]=reinterpret_cast<void*>(Class);slots[8]=reinterpret_cast<void*>(Dormant);
 CMirvTf2ClientUnknown player,rocket;rocket.Index=40;rocket.Handle=(2<<12)|40;rocket.Player=false;objects={&player,&rocket};
 std::vector<MirvTf2SceneEntity> out;Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,false,false,out);assert(out.size()==1&&out[0].Snapshot.Identity==42);
 Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==2&&out[1].Snapshot.Category==1);auto id=out[1].Snapshot.Identity;assert(id==((1ull<<63)|rocket.Handle));
 rocket.Handle=(3<<12)|40;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==2&&id!=out[1].Snapshot.Identity);
 rocket.Net.Dormant=true;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1);rocket.Net.Dormant=false;
 rocket.Render.Visible=false;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1);rocket.Render.Visible=true;
 rocket.Handle=0xffffffff;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1);rocket.Handle=41;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1);rocket.Handle=40;
 rocket.Render.Origin[0]=NAN;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1);rocket.Render.Origin[0]=0;
 for(auto name:{"CTFRocketLauncher","CTFGrenadeLauncher","CTFFlareGun","CTFCrossbow","CTFCompoundBow","CTFCleaver","CTFRocketPack","CTFWearable"}){rocket.Net.Class.Name=name;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1);}
 for(auto name:{"CTFProjectile_Rocket","CTFGrenadePipebombProjectile","CTFProjectile_Arrow","CTFProjectile_Flare","CTFProjectile_Cleaver"}){rocket.Net.Class.Name=name;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==2);}
 player.User=0;Mirv_Tf2_CollectSceneEntities(true,false,false,false,false,false,false,out);assert(out.size()==1&&out[0].Index==40);
 player.User=42;CMirvTf2ClientUnknown other,weapon,hat;
 other.Index=2;other.User=43;weapon.Index=50;weapon.Handle=(3<<12)|50;weapon.Player=false;weapon.Owner=&player;weapon.Net.Class.Name="CTFSniperRifle";weapon.ToolClass="tf_weapon_sniperrifle";
 hat.Index=51;hat.Handle=(5<<12)|51;hat.Player=false;hat.Owner=&player;hat.Net.Class.Name="CTFWearable";
 objects={&player,&other,&rocket,&weapon,&hat};
 Mirv_Tf2_CollectSceneEntities(true,true,true,false,false,false,false,out);assert(out.size()==5);assert(out[3].Snapshot.Category==2&&out[3].Snapshot.OwnerIdentity==42&&out[4].Snapshot.Category==3&&out[4].Snapshot.OwnerIdentity==42);assert(tools.Attaches==tools.Detaches&&!weapon.ToolAttached);
 Mirv_Tf2_CollectSceneEntities(true,true,true,false,false,true,true,out);assert(out.size()==5&&out[3].Snapshot.Category==2&&out[4].Snapshot.Category==3); // Prop fallback must not reclassify held TF weapons.
 Mirv_Tf2_CollectSceneEntities(false,true,false,false,false,false,false,out);assert(out.size()==3&&out.back().Index==50);
 Mirv_Tf2_CollectSceneEntities(false,false,true,false,false,false,false,out);assert(out.size()==3&&out.back().Index==51);
 hat.Net.Class.Name="CTFWearableVM";Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==3&&out.back().Index==50);hat.Net.Class.Name="CTFWearable";
 weapon.Render.Visible=false;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==3&&out.back().Index==51);weapon.Render.Visible=true;
 weapon.ViewWeapon=50;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==3&&out.back().Index==51);weapon.ViewWeapon=0;
 player.Net.Dormant=true;Mirv_Tf2_CollectSceneEntities(true,true,true,false,false,false,false,out);assert(out.size()==2);player.Net.Dormant=false;
 weapon.Owner=&other;Mirv_Tf2_CollectSceneEntities(false,true,false,false,false,false,false,out);assert(out.size()==3&&out.back().Snapshot.OwnerIdentity==43);
 weapon.Owner=&hat;hat.Owner=&weapon;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==2); // Bounded cyclic ownership.
 hat.Owner=&player;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==4&&out[2].Snapshot.OwnerIdentity==42); // Nested owner resolves to player.
 weapon.Owner=nullptr;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==4&&out[2].Index==50&&!out[2].Snapshot.OwnerIdentity); // Dropped gear becomes independent.
 weapon.Owner=&player;weapon.ToolAttached=true;int detaches=tools.Detaches;Mirv_Tf2_CollectSceneEntities(false,true,false,false,false,false,false,out);assert(out.size()==3&&weapon.ToolAttached&&tools.Detaches==detaches);weapon.ToolAttached=false;
 // Player bodies and attached gear end at death/hidden state; spawned corpse remains independent.
 objects={&player,&weapon,&hat};player.Alive=false;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.empty());player.Alive=true;
 player.NoDraw=true;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.empty());player.NoDraw=false;
 player.Render.Visible=false;Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.empty());player.Render.Visible=true;
 Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==3); // Respawn/visibility return, original player and gear can capture again.
 // Networked ragdolls, client-only gibs and model-path fallback; no player owner is required.
 CMirvTf2ClientUnknown corpse,gib,fragment,prop;
 corpse.Player=false;corpse.Index=60;corpse.Handle=(8<<12)|60;corpse.Net.Class.Name="CTFRagdoll";
 gib.Player=false;gib.Index=-1;gib.Handle=(9<<12)|2050;gib.HasNetwork=false;gib.ToolClass="class C_Gib";
 fragment.Player=false;fragment.Index=-1;fragment.Handle=(10<<12)|2051;fragment.HasNetwork=false;fragment.ToolClass="class C_PhysPropClientside";fragment.Render.ModelName="MODELS\\PLAYER\\GIBS\\heavygib001.mdl";
 prop.Player=false;prop.Index=-1;prop.Handle=(11<<12)|2052;prop.HasNetwork=false;prop.ToolClass="class C_PhysPropClientside";
 objects={&player,&weapon,&hat,&corpse,&gib,&fragment};player.Alive=false;
 Mirv_Tf2_CollectSceneEntities(false,true,true,true,true,false,false,out);assert(out.size()==3);for(auto&row:out)assert(row.Snapshot.Category==5||row.Snapshot.Category==6); // Death replaces the body/gear with independent debris.
 player.Alive=true;Mirv_Tf2_CollectSceneEntities(false,true,true,true,true,false,false,out);assert(out.size()==6); // Respawn does not remove an older corpse.
 objects={&corpse,&gib,&fragment,&prop};
 Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,false,false,out);assert(out.empty());
 Mirv_Tf2_CollectSceneEntities(false,false,false,true,false,false,false,out);assert(out.size()==1&&out[0].Snapshot.Category==5&&out[0].ObservedObject==&corpse);
 Mirv_Tf2_CollectSceneEntities(false,false,false,false,true,false,false,out);assert(out.size()==2&&out[0].Snapshot.Category==6&&out[1].Snapshot.Category==6&&out[0].ObservedObject==&gib&&out[1].ObservedObject==&fragment);auto gibId=out[0].Snapshot.Identity;
 gib.Handle+=(1<<12);Mirv_Tf2_CollectSceneEntities(false,false,false,true,true,false,false,out);assert(out.size()==3&&out[1].Snapshot.Identity!=gibId);
 gib.ToolAttached=true;int oldDetach=tools.Detaches;Mirv_Tf2_CollectSceneEntities(false,false,false,true,true,false,false,out);assert(gib.ToolAttached&&tools.Detaches==oldDetach+2);gib.ToolAttached=false; // Only other temporary attachments detach.
 gib.Handle=0xffffffff;fragment.Render.Visible=false;Mirv_Tf2_CollectSceneEntities(false,false,false,true,true,false,false,out);assert(out.size()==1);gib.Handle=(9<<12)|2050;fragment.Render.Visible=true;
 corpse.Net.Dormant=true;Mirv_Tf2_CollectSceneEntities(false,false,false,true,false,false,false,out);assert(out.empty());corpse.Net.Dormant=false;
 for(auto name:{"class C_TFRagdoll","CTFRagdoll","prop_ragdoll"})assert(Mirv_Tf2_LabelCategory(name,false,true)==5&&Mirv_Tf2_LabelCategory(name,false)==6);
 for(auto name:{"class C_Gib","CTFGib","class C_TFGib","CTFHeadGib","C_TFHeadGib","gib","tf_gib"})assert(Mirv_Tf2_LabelCategory(name,false,true)==6&&Mirv_Tf2_LabelCategory(name,false)==5);
 // Dropped weapons / cosmetics survive gameplay ownership, and corpse-parented gear follows the corpse.
 objects={&player,&weapon,&hat,&corpse};weapon.ParentKnown=hat.ParentKnown=true;player.Alive=false;weapon.MoveParent=hat.MoveParent=0xffffffff;
 Mirv_Tf2_CollectSceneEntities(false,true,true,true,false,false,false,out);assert(out.size()==3&&out[0].Snapshot.Category==2&&!out[0].Snapshot.OwnerIdentity&&out[1].Snapshot.Category==3&&!out[1].Snapshot.OwnerIdentity);
 hat.NoDraw=true;Mirv_Tf2_CollectSceneEntities(false,true,true,true,false,false,false,out);assert(out.size()==2&&out[0].Snapshot.Category==2);hat.NoDraw=false;
 hat.MoveParent=corpse.Handle;Mirv_Tf2_CollectSceneEntities(false,true,true,true,false,false,false,out);assert(out.size()==3&&out[1].Snapshot.OwnerIdentity==((1ull<<63)|corpse.Handle));
 Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==1&&out[0].Snapshot.Category==2); // No floating corpse gear if that corpse is not retained.
 player.Alive=true;weapon.MoveParent=player.Handle;hat.MoveParent=player.Handle;Mirv_Tf2_CollectSceneEntities(false,true,true,true,false,false,false,out);assert(out.size()==4&&out[1].Snapshot.OwnerIdentity==42&&out[2].Snapshot.OwnerIdentity==42);
 CMirvTf2ClientUnknown looseHat,looseWeapon;looseHat.Index=looseWeapon.Index=-1;looseHat.HasNetwork=looseWeapon.HasNetwork=false;looseHat.Player=looseWeapon.Player=false;looseHat.Handle=(12<<12)|2055;looseWeapon.Handle=(13<<12)|2056;looseHat.ToolClass=looseWeapon.ToolClass="class C_PhysPropClientside";looseHat.Render.ModelName="models/player/items/scout/hat.mdl";looseWeapon.Render.ModelName="models/weapons/w_models/w_minigun.mdl";looseHat.Owner=looseWeapon.Owner=&player;player.Alive=false;objects={&looseHat,&looseWeapon};
 Mirv_Tf2_CollectSceneEntities(false,true,true,false,false,false,false,out);assert(out.size()==2&&out[0].Snapshot.Category==3&&out[1].Snapshot.Category==2&&!out[0].Snapshot.OwnerIdentity&&!out[1].Snapshot.OwnerIdentity);
 // Buildings and model/brush props are independent of the engineer or camera target.
 CMirvTf2ClientUnknown building,cart,intel,door,viewmodel;
 building.Player=false;building.Index=80;building.Handle=(14<<12)|80;building.Net.Class.Name="CObjectSentrygun";
 cart.Player=false;cart.Index=81;cart.Handle=(15<<12)|81;cart.Net.Class.Name="CDynamicProp";cart.Render.ModelName="models/props_trainyard/cart.mdl";
 intel.Player=false;intel.Index=82;intel.Handle=(16<<12)|82;intel.Net.Class.Name="CCaptureFlag";intel.Render.ModelName="models/flag/briefcase.mdl";
 door.Player=false;door.Index=83;door.Handle=(17<<12)|83;door.Net.Class.Name="CBaseDoor";door.Render.ModelName="*12";
 viewmodel.Player=false;viewmodel.Index=84;viewmodel.Handle=(18<<12)|84;viewmodel.Net.Class.Name="CBaseViewModel";viewmodel.Render.ModelName="models/weapons/v_minigun.mdl";
 objects={&building,&cart,&intel,&door,&viewmodel};Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,false,out);assert(out.size()==1&&out[0].Snapshot.Category==7);
 Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,false,true,out);assert(out.size()==3);for(auto&row:out)assert(row.Snapshot.Category==8);
 Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,true,out);assert(out.size()==4);auto buildingId=out[0].Snapshot.Identity;building.Handle+=(1<<12);Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,true,out);assert(out[0].Snapshot.Identity!=buildingId);
 building.Net.Dormant=true;door.NoDraw=true;Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,true,out);assert(out.size()==2);building.Net.Dormant=false;door.NoDraw=false;
 for(auto name:{"CObjectSentrygun","CObjectDispenser","CObjectTeleporter","CObjectSapper","CObjectCartDispenser","obj_sentrygun","class C_ObjectTeleporter"})assert(Mirv_Tf2_LabelCategory(name,false,true)==7);
 for(auto name:{"CBaseViewModel","class C_ViewmodelAttachmentModel","CTFWearableVM"})assert(Mirv_Tf2_LabelCategory(name,false,true)<0);
 cart.Render.ModelName="materials/sprites/test.vmt";Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,true,out);assert(out.size()==3); // Sprite effects stay outside pose history.
 cart.Render.ModelName="models/buildables/dispenser.mdl";Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,false,out);assert(out.size()==2&&out[1].Snapshot.Category==7);
 intel.ParentKnown=true;intel.MoveParent=building.Handle;Mirv_Tf2_CollectSceneEntities(false,false,false,false,false,true,true,out);assert(out.size()==4&&out[2].Snapshot.OwnerIdentity==out[0].Snapshot.Identity);
 objects.clear();Mirv_Tf2_CollectSceneEntities(true,true,true,false,false,false,false,out);assert(out.empty());
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
if result.returncode: raise SystemExit(result.returncode)
# Exercise the actual dispatcher independently of Direct3D: scene fallbacks must
# survive Show path being off or the active path being hidden, without duplication.
drawer=(root/'AfxHookSource/CampathDrawer.cpp').read_text()
dispatch=drawer[drawer.index('void CCampathDrawer::OnPostRenderAllTools_EngineThread()'):drawer.index('void CCampathDrawer::OnPostRenderAllTools_DrawingThread(')]
proxy_fixture=r'''
#include "shared/SceneCache.h"
#include <cassert>
#include <string>
#include <iostream>
bool guideDrawing=false,recording=false;size_t cameraCount=1;size_t MirvCam_CameraCount(){return cameraCount;}
struct CamPath {};
struct Paths {
 struct Entry{unsigned long long Id;std::shared_ptr<const int>Data;};struct State{std::vector<Entry>Paths;};
 CamPath Path[2];bool Visible[2]{true,false};size_t GetActive(){return 0;}size_t GetCount(){return 2;}bool IsVisible(size_t i){return Visible[i];}CamPath*GetPath(size_t i){return &Path[i];}std::string GetName(size_t){return "path";}State CaptureState(){return {{{1,{}},{2,{}}}};}
};
struct View {CamPath m_CamPath;Paths m_CamPaths;}g_Hook_VClient_RenderView;
namespace AfxHookSource{namespace CameraOverlay{bool CameraGuidesEnabled(){return guideDrawing;}namespace Recording{bool IsActive(){return recording;}}std::shared_ptr<const advancedfx::SceneCache::Frame>view;bool bounds=false;std::shared_ptr<const std::vector<advancedfx::SceneCache::Line>> overrideLines;auto SceneCacheLines(){if(overrideLines)return overrideLines;if(!view||!bounds)return std::shared_ptr<const std::vector<advancedfx::SceneCache::Line>>{};static advancedfx::SceneCache cache;advancedfx::SceneCache::DrawingOptions options;options.Bounds=bounds;options.Meshes=false;return std::make_shared<const std::vector<advancedfx::SceneCache::Line>>(cache.Drawing(*view,options));}}}
std::vector<int>g_WorldLabelCells;
struct CCampathDrawer{
 struct CLessDynamicProperties{int Refs=0;CLessDynamicProperties(CCampathDrawer*,CamPath*,CLessDynamicProperties* = nullptr){}void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}};
 struct CCampathDrawerFunctor{bool SceneOnly;CLessDynamicProperties*Props;CCampathDrawerFunctor(CCampathDrawer*,CamPath*,const std::string&,CLessDynamicProperties*p,bool only=false):SceneOnly(only),Props(p){p->AddRef();}~CCampathDrawerFunctor(){Props->Release();}};
 struct CachedPath{unsigned long long Id;std::shared_ptr<const int>State;CLessDynamicProperties*Properties;};
 bool m_Draw=false,m_PropertiesDirty=true;CLessDynamicProperties*m_LessDynamicProperties=nullptr,*m_SceneProperties=nullptr;std::vector<CachedPath>m_OtherPaths;
 ~CCampathDrawer(){if(m_LessDynamicProperties)m_LessDynamicProperties->Release();if(m_SceneProperties)m_SceneProperties->Release();for(auto&p:m_OtherPaths)if(p.Properties)p.Properties->Release();}
 void OnPostRenderAllTools_EngineThread();
};
std::vector<std::unique_ptr<CCampathDrawer::CCampathDrawerFunctor>>queued;
bool MaterialSystem_ExecuteOnRenderThread(CCampathDrawer::CCampathDrawerFunctor*f){queued.emplace_back(f);return true;}
'''+dispatch+r'''
int main(){
 using Cache=advancedfx::SceneCache;using namespace AfxHookSource::CameraOverlay;
 CCampathDrawer drawer;auto snapshot=std::make_shared<Cache::Frame>();Cache::Entity door;door.Category=8;door.Identity=1;snapshot->Entities={door};view=snapshot;drawer.OnPostRenderAllTools_EngineThread();assert(queued.empty());bounds=true;
 drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==1&&queued[0]->SceneOnly);auto emptyProps=queued[0]->Props;queued.clear();drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==1&&queued[0]->Props==emptyProps);queued.clear();
 g_Hook_VClient_RenderView.m_CamPaths.Visible[0]=false;drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==1&&queued[0]->SceneOnly);queued.clear();
 drawer.m_Draw=true;g_Hook_VClient_RenderView.m_CamPaths.Visible[1]=true;drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==2&&queued[0]->SceneOnly&&!queued[1]->SceneOnly);queued.clear();
 g_Hook_VClient_RenderView.m_CamPaths.Visible[0]=true;g_Hook_VClient_RenderView.m_CamPaths.Visible[1]=false;drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==1&&!queued[0]->SceneOnly);queued.clear();
 drawer.m_Draw=false;bounds=false;auto meshes=std::make_shared<Cache::Frame>(*snapshot);meshes->Entities[0].ModelPose=std::make_shared<Cache::Pose>();view=meshes;drawer.OnPostRenderAllTools_EngineThread();assert(queued.empty());view.reset();drawer.OnPostRenderAllTools_EngineThread();assert(queued.empty());auto lines=std::make_shared<std::vector<Cache::Line>>();lines->push_back({{0,0,0},{1,1,1},8});overrideLines=lines;drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==1&&queued[0]->SceneOnly);queued.clear();
 overrideLines.reset();guideDrawing=true;drawer.OnPostRenderAllTools_EngineThread();assert(queued.size()==1&&queued[0]->SceneOnly);queued.clear();recording=true;drawer.OnPostRenderAllTools_EngineThread();assert(queued.empty());recording=false;cameraCount=0;drawer.OnPostRenderAllTools_EngineThread();assert(queued.empty());
 std::cout<<"Scene proxy dispatch checks passed\n";
}
'''
source=out/'scene_proxy_dispatch.cpp';source.write_text(proxy_fixture)
command=['g++','-std=c++17','-O0','-g','-Dabstract=','-I'+str(root),'-I'+str(root/'AfxHookSource'),str(source),str(root/'shared/AfxMath.cpp'),'-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-o',str(out/'scene_proxy_dispatch')]
if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
result=subprocess.run(command,capture_output=True,text=True)
(out/'proxy-build.log').write_text(result.stdout+result.stderr)
if result.returncode:print(result.stderr[-6500:]);raise SystemExit(result.returncode)
result=subprocess.run([str(out/'scene_proxy_dispatch')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)

# Exercise the actual shared line renderer: batching must preserve independent
# quads, flush boundaries, failed locks and reset cleanup without stray joins.
import re
def line_function(marker):
    a=drawer.index(marker);b=drawer.index('{',a);end=b+1;depth=1
    while depth:
        if drawer[end]=='{':depth+=1
        if drawer[end]=='}':depth-=1
        end+=1
    return drawer[a:end]
line_methods='\n'.join(line_function(name) for name in [
    'void CCampathDrawer::AutoSingleLine(', 'void CCampathDrawer::AutoSingleLineFlush()',
    'void CCampathDrawer::BuildSingleLine(Vector3', 'void CCampathDrawer::BuildSingleLine(DWORD',
    'void CCampathDrawer::UnloadVertexBuffer()'])
line_capacity=re.search(r'c_VertexBufferVertexCount = (\d+)',drawer)[1]
line_fixture=r'''#include "shared/AfxMath.h"
#include <vector>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using Vector3=Afx::Math::Vector3;using DWORD=unsigned int;using UINT=unsigned int;
constexpr int D3DPT_TRIANGLESTRIP=5;
struct Vertex{float x,y,z;DWORD diffuse;float t0u,t0v,t0w,t1u,t1v,t1w,t2u,t2v,t2w;};
static constexpr UINT c_VertexBufferVertexCount=CAPACITY;
struct Buffer{int Releases=0;void Release(){++Releases;}};
struct Device{std::array<Vertex,c_VertexBufferVertexCount>*Vertices=nullptr;size_t Calls=0,Triangles=0;void SetStreamSource(int,void*,int,size_t){};void DrawPrimitive(int type,UINT start,UINT count){assert(type==D3DPT_TRIANGLESTRIP&&count>=2&&start+count+2<=Vertices->size());++Calls;auto equal=[](const Vertex&a,const Vertex&b){return !std::memcmp(&a,&b,sizeof(Vertex));};for(UINT i=0;i<count;++i){const auto&a=(*Vertices)[start+i],&b=(*Vertices)[start+i+1],&c=(*Vertices)[start+i+2];if(equal(a,b)||equal(a,c)||equal(b,c))continue;assert(a.diffuse==b.diffuse&&a.diffuse==c.diffuse);++Triangles;}}};
struct CCampathDrawer{Device* m_Device;Buffer Resource;Buffer*m_VertexBuffer=nullptr;UINT m_VertexBufferVertexCount=0;Vertex*m_LockedVertexBuffer=nullptr;std::array<Vertex,c_VertexBufferVertexCount>Data{};bool AllowLock=true;CCampathDrawer(Device&d):m_Device(&d){d.Vertices=&Data;}bool LockVertexBuffer(){if(!AllowLock)return false;m_VertexBuffer=&Resource;m_LockedVertexBuffer=Data.data();return true;}void UnlockVertexBuffer(){m_LockedVertexBuffer=nullptr;}void UnloadVertexBuffer();void AutoSingleLine(Vector3,DWORD,Vector3,DWORD);void AutoSingleLineFlush();void BuildSingleLine(Vector3,Vector3,Vertex*);void BuildSingleLine(DWORD,DWORD,Vertex*);};
IMPLEMENTATION
int main(){Device device;CCampathDrawer draw(device);draw.AutoSingleLineFlush();assert(device.Calls==0);for(int i=0;i<16384;++i)draw.AutoSingleLine(Vector3(i,0,0),DWORD(i+1),Vector3(i+1,1,0),DWORD(i+1));draw.AutoSingleLineFlush();assert(device.Triangles==32768);std::cout<<device.Calls<<" draw calls for 16384 independent lines\n";assert(device.Calls==49);draw.AutoSingleLine(Vector3(0,0,0),1,Vector3(1,0,0),1);draw.UnloadVertexBuffer();assert(!draw.m_VertexBufferVertexCount&&!draw.m_LockedVertexBuffer&&!draw.m_VertexBuffer&&draw.Resource.Releases==1);
Device failed;CCampathDrawer unavailable(failed);unavailable.AllowLock=false;unavailable.AutoSingleLine(Vector3(0,0,0),1,Vector3(1,1,0),1);unavailable.AutoSingleLineFlush();assert(!failed.Calls&&!unavailable.m_VertexBufferVertexCount);unavailable.AllowLock=true;unavailable.AutoSingleLine(Vector3(0,0,0),1,Vector3(1,1,0),1);unavailable.AutoSingleLineFlush();assert(failed.Calls==1&&failed.Triangles==2);}
'''
source=out/'scene_line_batch.cpp';source.write_text(line_fixture.replace('CAPACITY',line_capacity).replace('IMPLEMENTATION',line_methods))
command=['g++','-std=c++17','-O1','-g','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root),'-I'+str(root/'AfxHookSource'),str(source),str(root/'shared/AfxMath.cpp'),'-o',str(out/'scene_line_batch')]
if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
result=subprocess.run(command,capture_output=True,text=True);(out/'line-build.log').write_text(result.stdout+result.stderr)
if result.returncode:print(result.stderr[-6500:]);raise SystemExit(result.returncode)
result=subprocess.run([str(out/'scene_line_batch')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)

# Shared numeric client addresses must not depend on a particular handle bit layout.
identity=calcs[calcs.index('// TF2 entity slot identifiers'):calcs.index('// End TF2 entity slot identifiers.')]
labels=calcs[calcs.index('int Mirv_Tf2_CollectEntityLabels('):calcs.index('IMirvVecAngCalc * CMirvVecAngCalcs::NewTf2EntityIndexCalc',calcs.index('int Mirv_Tf2_CollectEntityLabels('))]
identity_fixture=r'''
#include <algorithm>
#include <map>
#include <cstdint>
#include <set>
#include <vector>
#include <limits>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <cassert>
#include <iostream>
int g_LevelInitCount=0;
struct CMirvTf2ClientRenderable {bool Posed=false;const char*ModelName=nullptr;const void*GetModel(){return ModelName;}bool Visible=true;float Origin[3]{1,2,3};bool ShouldDraw(){return Visible;}const void*GetRenderOrigin(){return Origin;}void GetRenderBoundsWorldspace(float*a,float*b){for(int i=0;i<3;++i){a[i]=0;b[i]=4;}}};
struct CMirvTf2ClientUnknown {unsigned int Handle=0xffffffffu;int Index=-1,LabelCategory=4;bool Player=false,Debris=false;CMirvTf2ClientRenderable Render;const void*GetRefEHandle()const{return &Handle;}auto GetClientRenderable(){return &Render;}};
std::vector<CMirvTf2ClientUnknown*>objects;
struct Tools {std::uintptr_t AttachToEntity(void*){return 0;}int GetNumRecordables(){return 0;}std::uintptr_t GetRecordable(int){return 0;}void*FirstEntity(){return objects.empty()?nullptr:objects.front();}void*NextEntity(void*p){for(size_t i=0;i<objects.size();++i)if(objects[i]==p)return i+1<objects.size()?objects[i+1]:nullptr;return nullptr;}int GetEntIndex(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Index;}bool IsPlayer(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Player;}} tools;
struct CClientToolsTf2 {static auto Instance(){static CClientToolsTf2 singleton;return &singleton;}auto GetClientTools(){return &tools;}};
namespace SOURCESDK{namespace TF2{using IClientTools=Tools;using HTOOLHANDLE=std::uintptr_t;constexpr std::uintptr_t HTOOLHANDLE_INVALID=0;}}
struct MirvTf2EntityLabel_t {int category,index;float origin[3];};
constexpr int MIRV_TF2_ENTITY_OTHER=4,MIRV_TF2_ENTITY_PLAYER=0,MIRV_TF2_ENTITY_COSMETIC=3;struct LabelModelInfo{const char*GetModelName(const void*m){return static_cast<const char*>(m);}}labelModelInfo;auto*Mirv_Tf2_GetModelInfo(){return &labelModelInfo;}
bool Mirv_Tf2_LabelDebris(CMirvTf2ClientUnknown*p){return p->Debris;}bool Mirv_Tf2_GibLabelAnchor(CMirvTf2ClientRenderable*r,float*out){if(!r->Posed)return false;out[0]=10;out[1]=20;out[2]=30;return true;}
int Mirv_Tf2_ToolCategory(Tools*,void*p,int,bool player){return player?0:static_cast<CMirvTf2ClientUnknown*>(p)->LabelCategory;}
bool Mirv_IsUsableBound(float v){return std::isfinite(v);}
'''+identity+labels+r'''
int main(){CMirvTf2ClientUnknown player,gib,other;player.Index=5;player.Player=true;player.Handle=(1u<<16)|5;gib.LabelCategory=5;gib.Handle=(3u<<12)|2050;other.Handle=(7u<<16)|5000;objects={&player,&gib,&other};assert(Mirv_Tf2_EntityAddress(&player)==5);int a=Mirv_Tf2_EntityAddress(&gib),b=Mirv_Tf2_EntityAddress(&other);assert(a>=2048&&b>a);assert(Mirv_Tf2_HandleForAddress(a)==gib.Handle&&Mirv_Tf2_HandleForAddress(b)==other.Handle);assert(Mirv_Tf2_EntityAddress(&gib)==a);MirvTf2EntityLabel_t found[8];assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[1].index==a&&found[2].index==b);other.Debris=true;other.LabelCategory=3;other.Render.ModelName="models/hats/c_gibus.mdl";assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[2].category==3);other.Debris=false;other.LabelCategory=1;other.Render.ModelName="models/weapons/sticky_piece.mdl";other.Debris=true;assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[2].category==5);other.Debris=false;assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[2].category==1);other.Render.ModelName="models/weapons/stickybomb_gib1.mdl";assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[2].category==5);other.LabelCategory=4;other.Render.ModelName="models/player/GIBS/head.mdl";assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[2].category==5&&found[2].origin[0]==2);other.Render.ModelName=nullptr;assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3);gib.Render.Posed=true;assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3&&found[1].origin[0]==10&&found[1].origin[1]==20&&found[1].origin[2]==30);gib.Render.Posed=false;assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==3);assert(found[1].category==5&&found[1].origin[0]==2&&found[1].origin[1]==2&&found[1].origin[2]==4&&found[2].origin[0]==1);assert(Mirv_Tf2_CollectEntityLabels(true,found,8)==1&&found[0].index==5);unsigned int old=gib.Handle;gib.Handle+=(1u<<12);assert(Mirv_Tf2_HandleForAddress(a)==0xffffffffu);int replacement=Mirv_Tf2_EntityAddress(&gib);assert(replacement!=a&&Mirv_Tf2_HandleForAddress(replacement)==gib.Handle);gib.Render.Visible=false;assert(Mirv_Tf2_CollectEntityLabels(false,found,8)==2);gib.Handle=0xffffffffu;assert(Mirv_Tf2_EntityAddress(&gib)==-1);objects={&player,&other};assert(Mirv_Tf2_HandleForAddress(b)==other.Handle);++g_LevelInitCount;assert(Mirv_Tf2_HandleForAddress(b)==0xffffffffu);assert(Mirv_Tf2_EntityAddress(&other)==2048);std::cout<<"Client entity address and label checks passed\n";}
'''
source=out/'client_entity_address.cpp';source.write_text(identity_fixture)
command=['g++','-std=c++17','-O1','-g',str(source),'-o',str(out/'client_entity_address')]
if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
result=subprocess.run(command,capture_output=True,text=True);(out/'identity-build.log').write_text(result.stdout+result.stderr)
if result.returncode:print(result.stderr[-6500:]);raise SystemExit(result.returncode)
result=subprocess.run([str(out/'client_entity_address')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)

# Actual label layout with mixed digit sizes, including extreme user settings.
entity_drawer=(root/'AfxHookSource/EntityIndexDrawer.cpp').read_text()
def entity_function(marker):
    a=entity_drawer.index(marker);b=entity_drawer.index('{',a);end=b+1;depth=1
    while depth:
        depth+=(entity_drawer[end]=='{')-(entity_drawer[end]=='}');end+=1
    return entity_drawer[a:end]
layout=entity_drawer[entity_drawer.index('\tstruct ScreenLabel'):entity_drawer.index('\t// Reuse the campath digit texture.')].replace('return;','return {};')
(out/'d3d9.h').write_text('class IDirect3DDevice9;class IDirect3DTexture9;\n')
label_test=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include "AfxHookSource/EntityIndexDrawer.h"
'''+entity_function('static bool EntityIndex_Project(')+entity_function('void CEntityIndexDrawer::CategoryColor_set(')+entity_function('void CEntityIndexDrawer::CategoryColor_get(')+r'''
std::vector<std::array<float,6>> Layout(const CEntityIndexDrawer::DrawData&data){using Label=CEntityIndexDrawer::Label;struct{unsigned X=0,Y=0,Width=1000,Height=1000;}vp;
'''+layout+r'''
 std::vector<std::array<float,6>> result;for(const auto&item:labels)result.push_back({item.left,item.top,item.right,item.bottom,item.y,float(item.label->category)});return result;}
int main(){CEntityIndexDrawer drawer;assert(drawer.Size_get()==32&&drawer.GibSize_get()==20);drawer.FilterAll_set(false);drawer.Filter_set(5,true);assert(drawer.Filter_get(5)&&!drawer.Filter_get(4));drawer.Size_set(48);assert(drawer.GibSize_get()==20);drawer.GibSize_set(12);assert(drawer.Size_get()==48);drawer.GibSize_set(1);assert(drawer.GibSize_get()==6);drawer.GibSize_set(500);assert(drawer.GibSize_get()==200);int r,g,b;drawer.CategoryColor_get(5,r,g,b);assert(r==255&&g==160&&b==64);drawer.Filter_set(6,true);assert(drawer.Filter_get(6));drawer.CategoryColor_set(6,90,100,110);drawer.CategoryColor_get(6,r,g,b);assert(r==90&&g==100&&b==110);drawer.CategoryColor_set(5,10,20,30);drawer.CategoryColor_get(5,r,g,b);assert(r==10&&g==20&&b==30);drawer.CategoryColor_get(4,r,g,b);assert(r==255&&g==230&&b==0);
 CEntityIndexDrawer::DrawData data;data.worldToScreen[0][0]=data.worldToScreen[1][1]=data.worldToScreen[3][3]=1;data.labels={{1,4,0,0,0},{2048,5,0,0,0}};data.stackLabels=false;auto plain=Layout(data);assert(plain.size()==2&&plain[0][3]-plain[0][1]==32&&plain[1][3]-plain[1][1]==20&&plain[0][4]==plain[1][4]);data.stackLabels=true;auto stacked=Layout(data);assert(stacked[1][3]<stacked[0][1]);data.size=200;data.gibSize=6;stacked=Layout(data);assert(stacked[1][3]<stacked[0][1]);data.labels[1].z=100;auto separate=Layout(data);assert(separate[0][4]==separate[1][4]);std::cout<<"Gib filter, size and mixed-label layout checks passed\n";}
'''
source=out/'gib_label_layout.cpp';source.write_text(label_test)
command=['g++','-std=c++17','-O1','-g','-I'+str(out),'-I'+str(root),str(source),'-o',str(out/'gib_label_layout')]
if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
result=subprocess.run(command,capture_output=True,text=True);(out/'gib-layout-build.log').write_text(result.stdout+result.stderr)
if result.returncode:print(result.stderr[-6500:]);raise SystemExit(result.returncode)
result=subprocess.run([str(out/'gib_label_layout')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)

# Compile the production interface under Microsoft's ABI; native fixtures alone
# cannot detect MSVC's different ordering of overloaded virtual methods.
if args.windows_abi_compiler:
    begin = calcs.index('class SceneBaseFileSystem\n')
    end = calcs.index('\n};', begin) + 3
    probe = out / 'filesystem_msvc_abi.cpp'
    probe.write_text(calcs[begin:end] + '\nunsigned Probe(SceneBaseFileSystem*f,void*h){return f->Size(h); }\n')
    for arch, offset in [('x86_64', 56), ('x86', 28)]:
        assembly = out / (arch + '-filesystem_msvc_abi.s')
        subprocess.run([str(args.windows_abi_compiler), 'c++', '-target', arch + '-windows-msvc',
                        '-O1', '-g0', '-S', str(probe), '-o', str(assembly)], check=True)
        import re
        assert re.search(r'\b' + str(offset) + r'\(', assembly.read_text()), assembly.read_text()
    print('Real MSVC x86/x64 filesystem Size(handle) slot checks passed')

# Exercise the actual rig registry, console operations and controller selection.
rig_source=(root/'AfxHookSource/MirvCam.cpp').read_text()
rig_adapter=rig_source[rig_source.rfind('namespace\n{',0,rig_source.index('struct NamedCameraRig')):rig_source.index('CON_COMMAND(mirv_cam,')]
rig_preamble=r'''
#include "shared/CameraRig.h"
#include "shared/SceneCache.h"
#include <mutex>
#include <filesystem>
#include <chrono>
#include <cassert>
#include <climits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>
#include <strings.h>
#define _stricmp strcasecmp
namespace advancedfx {struct ICommandArgs {virtual int ArgC()=0;virtual const char*ArgV(int)=0;};}
void Tier0_Msg(const char*,...){} void Tier0_Warning(const char*,...){}
static bool MirvCam_ReleaseLiveAttachment();
namespace SOURCESDK {struct Vector{float x=0,y=0,z=0;};struct QAngle{float x=0,y=0,z=0;};}
std::map<int,advancedfx::CameraPose> targets;int liveCalcs=0,liveTargetReads=0,cachedTargetReads=0;int g_LevelInitCount=0;
std::map<int,int>playerUsers{{5,6}};std::map<int,std::string>targetModels;
std::map<int,unsigned> generations;void*Mirv_Tf2_DiagnosticEntity(int e){auto f=targets.find(e);return f==targets.end()?nullptr:&f->second;}unsigned Mirv_Tf2_EntityHandle(const void*p){for(auto&e:targets)if(&e.second==p)return unsigned(e.first)*100+generations[e.first];return 0xffffffffu;}unsigned Mirv_Tf2_HandleForAddress(int e){return Mirv_Tf2_EntityHandle(Mirv_Tf2_DiagnosticEntity(e));}

struct IMirvVecAngCalc {double Shift[3]{};int Refs=0,Entity;IMirvVecAngCalc(int e):Entity(e){++liveCalcs;}void AddRef(){++Refs;}void Release(){if(!--Refs){--liveCalcs;delete this;}}bool CalcVecAng(SOURCESDK::Vector&o,SOURCESDK::QAngle&a){++liveTargetReads;auto f=targets.find(Entity);if(f==targets.end())return false;o.x=f->second.Position[0]+Shift[0];o.y=f->second.Position[1]+Shift[1];o.z=f->second.Position[2]+Shift[2];auto r=f->second.Rotation.ToQREulerAngles().ToQEulerAngles();a.x=r.Pitch;a.y=r.Yaw;a.z=r.Roll;return true;}};
struct Calcs{int Entity=-1,Bone=-2,Mode=-1;IMirvVecAngCalc*NewTf2EntityIndexCalc(const char*,int e,int b,int m){Entity=e;Bone=b;Mode=m;return new IMirvVecAngCalc(e);}}g_MirvVecAngCalcs;
constexpr int SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2,MIRV_TF2_BONEMODE_BONE=0;int g_SourceSdkVer=1;
bool Mirv_Tf2_ParseBoneArg(int,const char*s,int&bone){bone=std::atoi(s);return bone>=-1;}
constexpr unsigned VK_TAB=9,VK_DELETE=0x2e,VK_UP=0x26,VK_DOWN=0x28,VK_HOME=0x24,VK_END=0x23,VK_ESCAPE=0x1b;
constexpr int MIRV_TF2_BONEMODE_ENTITY=1,MIRV_TF2_BONEMODE_ALIGNED=2,MIRV_TF2_BONEMODE_WORLD=3;
int g_MirvCamAttachMode=2;bool nativePreview=false;
bool MirvCam_RigPreviewActive();void MirvCam_EndRigPreview();bool MirvCam_CameraCommand(advancedfx::ICommandArgs*);bool MirvCam_SetPreviewNavigation(bool);bool MirvCam_TogglePreviewNavigation();
bool MirvCampathPreview_IsActive(){return nativePreview||MirvCam_RigPreviewActive();}
bool MirvCampathPreview_SetActive(bool active){nativePreview=active;return true;}
struct Input{double X=0,Y=0,Z=0,P=0,Yaw=0,R=0,Fov=90;bool Enabled=false;void SetTx(float v){X=v;}void SetTy(float v){Y=v;}void SetTz(float v){Z=v;}void SetRx(float v){P=v;}void SetRy(float v){Yaw=v;}void SetRz(float v){R=v;}void SetFov(float v){Fov=v;}void SetCameraControlMode(bool v){Enabled=v;}}rigInput;
bool Mirv_Tf2_GetPlayerInfo(int,char*,size_t,int*);
std::shared_ptr<const advancedfx::SceneCache::Frame>cachedFrame;
bool cached=false;namespace AfxHookSource{namespace CameraOverlay{bool guides=false,frustums=true;void SetCameraGuides(bool v){guides=v;}void SetCameraFrustums(bool v){frustums=v;}bool CameraGuidesEnabled(){return guides;}bool CameraFrustumsEnabled(){return frustums;}bool SceneCachePreviewActive(){return cached;}std::shared_ptr<const advancedfx::SceneCache::Frame> SceneCacheView(){return cachedFrame;}}}
bool Mirv_Tf2_CalcCachedCamera(IMirvVecAngCalc*c,const advancedfx::SceneCache::Entity&e,const advancedfx::SceneCache::Entity*,SOURCESDK::Vector&o,SOURCESDK::QAngle&a){++cachedTargetReads;o.x=e.Origin[0]+c->Shift[0];o.y=e.Origin[1]+c->Shift[1];o.z=e.Origin[2]+c->Shift[2];a.x=e.Angles[0];a.y=e.Angles[1];a.z=e.Angles[2];return true;}
bool MirvCam_SelectedCameraAction(const char*);
bool MirvCam_CameraAction(const char*,std::uint64_t);

struct Engine{bool Playing=true;bool IsPlayingDemo(){return Playing;}}engine;Engine*g_VEngineClient=&engine;
struct PathKeys{size_t Count=2;size_t GetSize(){return Count;}};
struct View{PathKeys m_CamPath;double LastCameraOrigin[3]{1,2,3},LastCameraAngles[3]{},LastCameraFov=65;Input*m_MirvInput=&rigInput;}g_Hook_VClient_RenderView;
'''
# Run the actual legacy attachment command body too, not a second imitation of its grammar.
attach_start=rig_source.index('else if (0 == _stricmp("attach", arg1))')
attach_end=rig_source.index('else if (0 == _stricmp("attachMode", arg1))',attach_start)
attach_body=rig_source[attach_start:attach_end]
attach_body=attach_body[attach_body.index('{'):attach_body.rindex('}')+1]
legacy_methods=rig_source[rig_source.index('bool CMirvCam::ApplyOffset('):rig_source.index('bool CMirvCam::ApplyFov(')]
legacy_fixture=r'''
struct LegacyCam {
IMirvVecAngCalc*Source=nullptr;
float m_OffsetForwad=0,m_OffsetLeft=0,m_OffsetUp=0,m_OffsetForwardRot=0,m_OffseLeftRot=0,m_OffsetUpRot=0;
void Source_set(IMirvVecAngCalc*p){if(p)p->AddRef();if(Source)Source->Release();Source=p;}
IMirvVecAngCalc*Source_get(){return Source;}
bool ApplyOffset(float&,float&,float&,float&,float&,float&);
}g_MirvCam;
IMirvVecAngCalc*g_MirvCamAttachCalc=nullptr;
void MirvCam_SetAttachCalc(IMirvVecAngCalc*p){if(p)p->AddRef();if(g_MirvCamAttachCalc)g_MirvCamAttachCalc->Release();g_MirvCamAttachCalc=p;}
const char*MirvCam_AttachModeName(int){return "test";}
void MirvCameraAttachedReset(){}void MirvCameraAttachFollowCommand(advancedfx::ICommandArgs*){}
bool MirvCameraAttachedCapturePose(IMirvVecAngCalc*,const double*,const double*){return false;}
bool Mirv_Tf2_CaptureCamera(IMirvVecAngCalc*c,const double*o,const double*,float*offset){auto f=targets.find(c->Entity);if(f==targets.end())return false;for(int i=0;i<3;++i)offset[i]=c->Shift[i]=o[i]-f->second.Position[i];return true;}
int Mirv_Tf2_GetMaxPlayers(){return 8;}bool Mirv_Tf2_GetPlayerInfo(int e,char*name,size_t n,int*user){if(!playerUsers.count(e))return false;if(name&&n)snprintf(name,n,"Subject");if(user)*user=playerUsers[e];return true;}
'''
legacy_fixture+=r'''
std::string Mirv_Tf2_CameraTargetModel(int e){return targetModels.count(e)?targetModels[e]:"models/test.mdl";}
int Mirv_Tf2_FindCameraTarget(std::uint64_t id,const std::string&model){for(auto &e:targets){if(!(id>>63)){if(playerUsers.count(e.first)&&std::uint64_t(playerUsers[e.first])==id)return e.first;}else if(Mirv_Tf2_EntityHandle(&e.second)==unsigned(id)&&Mirv_Tf2_CameraTargetModel(e.first)==model)return e.first;}return -1;}
IMirvVecAngCalc*Mirv_Tf2_RebindCamera(IMirvVecAngCalc*old,int e){auto c=new IMirvVecAngCalc(e);for(int i=0;i<3;++i)c->Shift[i]=old->Shift[i];return c;}
'''
legacy_methods=rig_source[rig_source.index('static bool MirvCam_ReleaseLiveAttachment()'):rig_source.index('static char const * MirvCam_AttachModeName')]+legacy_methods.replace('CMirvCam::','LegacyCam::')
attach_command='void LegacyAttachCommand(advancedfx::ICommandArgs*args){int argc=args->ArgC();'+attach_body+'}\n'
rig_tests=r'''
struct Args:advancedfx::ICommandArgs{std::vector<const char*>v;int ArgC()override{return int(v.size());}const char*ArgV(int i)override{return v.at(i);}};
bool command(std::initializer_list<const char*>words){Args a;a.v=words;if(words.size()>=2&&!std::strcmp(*(words.begin()+1),"attach")){LegacyAttachCommand(&a);return true;}return MirvCam_CameraCommand(&a);}
int main(){advancedfx::CameraPose pose;
using namespace AfxHookSource::CameraOverlay;
assert(command({"mirv_cam","draw","enabled","1"})&&guides&&frustums);
assert(command({"mirv_cam","draw","frustums","0"})&&guides&&!frustums);
command({"mirv_cam","draw"});command({"mirv_cam","draw","frustums"});command({"mirv_cam","draw","invalid"});command({"mirv_cam","draw","frustums","2"});assert(guides&&!frustums);
command({"mirv_cam","draw","enabled","0"});command({"mirv_cam","draw","frustums","1"});assert(!guides&&frustums);

command({"mirv_cam","add"});assert(FindRig("0")!=g_CameraRigs.end()&&g_CameraSource==CameraSource::Legacy&&rigInput.Enabled);
command({"mirv_cam","add","Wide"});assert(FindRig("1")!=g_CameraRigs.end()&&g_CameraSource==CameraSource::Legacy);

const auto metadataReads=liveTargetReads;auto infos=MirvCam_Cameras(false);assert(infos.size()==2&&infos[0].Id==0&&infos[1].Id==1&&MirvCam_CameraCount()==2&&liveTargetReads==metadataReads);
assert(MirvCam_SelectCamera(0)&&!g_RigPreview&&MirvCam_Cameras(false)[0].Selected);assert(!MirvCam_SelectCamera(999)&&!MirvCam_CameraAction("remove",999));
command({"mirv_cam","preview","0"});assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==1);
assert(MirvCam_SelectCamera(1)&&g_RigPreview&&MirvCam_Cameras(false)[1].Viewed);assert(MirvCam_SelectCamera(0));MirvCam_PreviewKey(VK_UP,true,false);assert(g_CameraRigName=="Wide");MirvCam_PreviewKey(VK_DOWN,true,false);assert(FindRig(g_CameraRigName)->second.Id==0);
MirvCam_PreviewKey(VK_UP,true,true);assert(FindRig(g_CameraRigName)->second.Id==0);
command({"mirv_cam","preview","toggle"});assert(!g_RigNavigation&&nativePreview);
command({"mirv_cam","preview","toggle"});assert(g_RigPreview&&g_RigNavigation);
g_Hook_VClient_RenderView.m_CamPath.Count=0;assert(!MirvCam_TogglePreviewNavigation()&&g_RigPreview&&g_RigNavigation);g_Hook_VClient_RenderView.m_CamPath.Count=2;
command({"mirv_cam","edit","0"});assert(!g_RigPreview&&rigInput.Enabled);
g_Hook_VClient_RenderView.LastCameraOrigin[0]=75;command({"mirv_cam","capture","0"});command({"mirv_cam","rename","0","Hero"});assert(FindRig("0")->first=="Hero"&&g_EditingCameraName.empty());
command({"mirv_cam","preview","0"});assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==75);
command({"mirv_cam","edit","0","40","50","60","0","90","0","80"});assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==40&&pose.Fov==80);
command({"mirv_cam","edit","0","inf","0","0","0","0","0","90"});assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==40);
assert(!command({"mirv_cam","pose","0","0","0","0","0","0","0","90"}));assert(!command({"mirv_cam","rig","new","Removed"})&&!command({"mirv_cam","fixed","0"})&&g_CameraRigs.size()==2);
// Numeric tab edits and console edits share the same validated update path.
auto edited=pose;edited.Position[1]=123;edited.Fov=105;assert(MirvCam_EditCamera(0,edited));assert(MirvCam_Cameras(false)[0].Pose.Position[1]==123&&MirvCam_Cameras(false)[0].Pose.Fov==105);
auto invalid=edited;invalid.Fov=180;assert(!MirvCam_EditCamera(0,invalid)&&!MirvCam_EditCamera(999,edited));assert(MirvCam_Cameras(false)[0].Pose.Fov==105);
// Legacy attach creates a camera at the target, honoring offsets and bone/mode arguments.
advancedfx::CameraPose target;target.Position[0]=100;targets[5]=target;
command({"mirv_cam","attach","5","1","-20","4","3"});assert(g_CameraRigs.size()==3&&FindRig("2")->second.Rig.Attached&&FindRig("2")->second.AttachMode==2&&!rigInput.Enabled&&g_MirvVecAngCalcs.Bone==1&&g_MirvVecAngCalcs.Entity==5);
command({"mirv_cam","preview","2"});assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==80&&std::abs(pose.Position[1]-4)<1e-5&&pose.Position[2]==3);
targets[5].Position[0]=200;assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==180);
++generations[5];assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==180&&g_RigUnavailable);assert(!MirvCam_CameraAction("capture",2));auto retained=MirvCam_Cameras(false)[2].Pose;assert(!MirvCam_EditCamera(2,edited)&&MirvCam_Cameras(false)[2].Pose.Position[0]==retained.Position[0]);
command({"mirv_cam","edit","2"});command({"mirv_cam","attach","Subject","1","-10","0","0"});assert(g_CameraRigs.size()==3&&FindRig("2")->second.Rig.Attached);
command({"mirv_cam","preview","2"});assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==190&&!g_RigUnavailable);
cached=true;targets[5].Position[0]=300;assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==190&&g_RigUnavailable);
// Cached evaluation is independent of the live entity, handle lookup and seek order.
auto frame=std::make_shared<advancedfx::SceneCache::Frame>();advancedfx::SceneCache::Entity snapshot;snapshot.Identity=6;snapshot.Origin[0]=450;frame->Entities={snapshot};cachedFrame=frame;
const int reads=liveTargetReads;auto savedLive=targets[5];targets.erase(5);
assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==440&&!g_RigUnavailable&&liveTargetReads==reads);

const auto previousShift=FindRig("2")->second.Target->Shift[0];g_Hook_VClient_RenderView.LastCameraOrigin[0]=445;command({"mirv_cam","attach","capture"});assert(FindRig("2")->second.Target->Shift[0]==previousShift&&liveTargetReads==reads);assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==445);
g_Hook_VClient_RenderView.LastCameraOrigin[0]=440;command({"mirv_cam","attach","capture"});g_Hook_VClient_RenderView.LastCameraOrigin[0]=75;
frame->Entities[0].Origin[0]=470;assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==460);
frame->Entities[0].Origin[0]=450;assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==440);
frame->Entities[0].Identity=7;assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==440&&g_RigUnavailable);
cachedFrame.reset();assert(MirvCam_EvaluateRig(0,pose)&&g_RigUnavailable&&liveTargetReads==reads);
targets[5]=savedLive;cached=false;
// Clear replaces fixed; it freezes the selected attachment at its evaluated pose.
command({"mirv_cam","attach","clear"});assert(!FindRig("2")->second.Rig.Attached&&!g_MirvCam.Source_get());assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==290);
// All modes and entity-only attachments go through the same existing command.
for(int mode=0;mode<4;++mode){g_MirvCamAttachMode=mode;command({"mirv_cam","edit","2"});command({"mirv_cam","attach","5","-1","0","0","0"});assert(FindRig("2")->second.AttachMode==mode&&g_CameraRigs.size()==3);}
// Failed targets do not allocate cameras or replace the current source.
auto current=g_MirvCam.Source_get();const auto count=g_CameraRigs.size();command({"mirv_cam","attach","999","-1","10","20","30"});assert(g_MirvCam.Source_get()==current&&g_CameraRigs.size()==count&&g_MirvCam.m_OffsetForwad==0&&g_MirvCam.m_OffsetLeft==0&&g_MirvCam.m_OffsetUp==0);
command({"mirv_cam","preview","0"});command({"mirv_cam","attach","capture"});assert(!FindRig("0")->second.Rig.Attached&&g_MirvCam.Source_get()==current);command({"mirv_cam","edit","2"});command({"mirv_cam","attach","capture"});command({"mirv_cam","preview","2"});assert(MirvCam_EvaluateRig(0,pose)&&std::abs(pose.Position[0]-75)<1e-5);
command({"mirv_cam","rename","2","123"});assert(FindRig("2")->first=="Camera 2");
auto retainedInfo=MirvCam_Cameras(false);command({"mirv_cam","remove","1"});assert(retainedInfo.size()==3&&retainedInfo[1].Id==1&&retainedInfo[1].Name=="Wide");assert(FindRig("1")==g_CameraRigs.end()&&FindRig("2")->second.Id==2);
command({"mirv_cam","preview","off"});assert(!MirvCam_RigPreviewActive()&&g_RigNavigation);
command({"mirv_cam","remove","2"});assert(!g_MirvCam.Source_get()&&!g_MirvCamAttachCalc);command({"mirv_cam","attach","5"});assert(FindRig("3")!=g_CameraRigs.end());auto custom=new IMirvVecAngCalc(5);g_MirvCam.Source_set(custom);command({"mirv_cam","remove","3"});assert(g_MirvCam.Source_get()==custom&&!g_MirvCamAttachCalc);
command({"mirv_cam","preview","0"});++g_LevelInitCount;assert(!MirvCam_EvaluateRig(0,pose));

// Management actions share the console path and preserve stable IDs.
command({"mirv_cam","preview","off"});assert(MirvCam_SelectCamera(0)&&MirvCam_RenameSelectedCamera("Selected name"));assert(FindRig("0")->first=="Selected name");assert(!MirvCam_RenameSelectedCamera("123"));
assert(MirvCam_CameraAction("edit",0)&&g_EditingCameraName=="Selected name");assert(MirvCam_CameraAction("capture",0)&&g_EditingCameraName.empty());
// Adding/recapturing does not silently arm an old camera for the next attachment.
command({"mirv_cam","add","Independent"});const auto existingCount=g_CameraRigs.size();assert(g_EditingCameraName.empty());command({"mirv_cam","attach","5"});assert(g_CameraRigs.size()==existingCount+1&&!g_CameraRigs.at("Independent").Rig.Attached);
command({"mirv_cam","edit","Independent"});command({"mirv_cam","attach","5"});assert(g_CameraRigs.size()==existingCount+1&&g_CameraRigs.at("Independent").Rig.Attached);
// Client-only objects use full handles in the cache instead of player user IDs.
targets[2050]=target;command({"mirv_cam","attach","2050"});const auto gibName=g_CameraRigName;const auto gibIdentity=g_CameraRigs.at(gibName).CacheIdentity;
assert(gibIdentity==((std::uint64_t(1)<<63)|RigEntityHandle(2050)));
command({"mirv_cam","preview",gibName.c_str()});cached=true;frame->Entities[0].Identity=gibIdentity;frame->Entities[0].Origin[0]=900;cachedFrame=frame;targets.erase(2050);assert(MirvCam_EvaluateRig(0,pose)&&pose.Position[0]==900);
cached=false;cachedFrame.reset();

// Same-demo reload reconnects by user ID, preserves captured framing, and retries late targets.
g_MirvCam.Source_set(nullptr);MirvCam_SetAttachCalc(nullptr);MirvCam_EndRigPreview();g_CameraRigs.clear();targets.clear();playerUsers.clear();
MirvCam_NotifyDemoFile("shot","/game/tf");targets[5]=target;playerUsers[5]=6;
command({"mirv_cam","attach","5","-1","-20","4","3"});const auto reloadName=g_CameraRigName;const auto reloadId=g_CameraRigs.at(reloadName).Id;
g_Hook_VClient_RenderView.LastCameraOrigin[0]=150;g_Hook_VClient_RenderView.LastCameraOrigin[1]=8;g_Hook_VClient_RenderView.LastCameraOrigin[2]=9;command({"mirv_cam","attach","capture"});
const auto localBefore=g_CameraRigs.at(reloadName).Rig.Pose;command({"mirv_cam","preview",reloadName.c_str()});
MirvCam_NotifyDemoFile("tf/shot.dem","/game/tf");++g_LevelInitCount;targets.clear();playerUsers.clear();assert(MirvCam_EvaluateRig(0,pose)&&g_RigUnavailable&&!g_CameraRigs.at(reloadName).Available);
targets[5]=target;targets[5].Position[0]=999;playerUsers[5]=99;g_CameraRigs.at(reloadName).NextRebind={};assert(MirvCam_EvaluateRig(0,pose)&&g_RigUnavailable); // Wrong occupant must not win.
targets[7]=target;targets[7].Position[0]=200;playerUsers[7]=6;g_CameraRigs.at(reloadName).NextRebind={};assert(MirvCam_EvaluateRig(0,pose)&&!g_RigUnavailable&&std::abs(pose.Position[0]-250)<1e-5&&std::abs(pose.Position[1]-8)<1e-5&&std::abs(pose.Position[2]-9)<1e-5);
assert(g_CameraRigs.size()==1&&g_CameraRigs.at(reloadName).Id==reloadId&&g_CameraRigs.at(reloadName).EntityAddress==7);for(int i=0;i<3;++i)assert(g_CameraRigs.at(reloadName).Rig.Pose.Position[i]==localBefore.Position[i]);
++g_LevelInitCount;assert(MirvCam_EvaluateRig(0,pose)&&!g_RigUnavailable&&std::abs(pose.Position[0]-250)<1e-5);
MirvCam_NotifyDemoFile("different.dem","/game/tf");++g_LevelInitCount;MirvCam_EvaluateRig(0,pose);command({"mirv_cam","preview",reloadName.c_str()});assert(MirvCam_EvaluateRig(0,pose)&&g_RigUnavailable);
MirvCam_NotifyDemoFile("/game/tf/shot.dem","/game/tf");++g_LevelInitCount;assert(MirvCam_EvaluateRig(0,pose)&&!g_RigUnavailable&&std::abs(pose.Position[0]-250)<1e-5);
// Non-player restoration requires the full handle and matching model.
targets[12]=target;command({"mirv_cam","attach","12","-1","0","0","0"});const auto propName=g_CameraRigName;command({"mirv_cam","preview",propName.c_str()});++g_LevelInitCount;++generations[12];assert(MirvCam_EvaluateRig(0,pose)&&g_RigUnavailable);--generations[12];targetModels[12]="models/other.mdl";g_CameraRigs.at(propName).NextRebind={};assert(MirvCam_EvaluateRig(0,pose)&&g_RigUnavailable);targetModels.erase(12);g_CameraRigs.at(propName).NextRebind={};assert(MirvCam_EvaluateRig(0,pose)&&!g_RigUnavailable);
g_MirvCam.Source_set(nullptr);MirvCam_SetAttachCalc(nullptr);g_CameraRigs.clear();assert(liveCalcs==0);g_RigNavigation=false;nativePreview=true;assert(!MirvCam_TogglePreviewNavigation()&&nativePreview&&!g_RigNavigation);
std::cout<<"Consolidated camera commands, legacy attach registration and preview switching checks passed\n";}
'''
source=out/'camera_rig_adapter.cpp';source.write_text(rig_preamble+legacy_fixture+rig_adapter+legacy_methods+attach_command+rig_tests)
command=['g++','-std=c++17','-O1','-g','-Dabstract=','-I'+str(root),'-I'+str(root/'AfxHookSource'),'-include',str(root/'tests/cinematic/msvc_crt_compat.h'),str(source),str(root/'shared/AfxMath.cpp'),'-o',str(out/'camera_rig_adapter')]
if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
result=subprocess.run(command,capture_output=True,text=True)
if result.returncode:print(result.stderr[-6500:]);raise SystemExit(result.returncode)
result=subprocess.run([str(out/'camera_rig_adapter')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print(result.stdout+result.stderr)
if result.returncode:raise SystemExit(result.returncode)
