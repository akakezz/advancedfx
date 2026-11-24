"""Exercise real OF classification, parent resolution, and opt-in material-cache policy.
The Windows binary preflight is substituted only in the material fixture.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'AfxHookSource/MirvCalcs.cpp').read_text()
category=s[s.index('static bool OfSceneViewmodel('):s.index('void Mirv_Of_CollectSceneEntities(')]
owner=s[s.index('static void SceneResolveAttachmentHosts('):s.index('void Mirv_Tf2_CollectSceneEntities(')]
materials=s[s.index('static void *g_SceneMaterials['):s.index('// The render queue only signals completion.')]
a=materials.index('static bool SceneOfMaterial080SlotPlausible(');b=materials.index('// MaterialSystem082 uses',a)
materials=materials[:a]+'static bool SceneOfMaterial080SlotPlausible(void*) {return abiGood;}\n'+materials[b:]
release=s[s.index('void Mirv_Tf2_ReleaseSceneMaterials()\n{'):s.index('// Shared with the camera target provider:')]
fixture=r'''
#include <array>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <type_traits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iostream>
constexpr int MIRV_TF2_ENTITY_PLAYER=0,MIRV_TF2_ENTITY_PROJECTILE=1,MIRV_TF2_ENTITY_WEAPON=2,MIRV_TF2_ENTITY_COSMETIC=3,MIRV_TF2_ENTITY_RAGDOLL=5,MIRV_TF2_ENTITY_GIB=6,MIRV_TF2_ENTITY_BUILDING=7,MIRV_TF2_ENTITY_PROP=8;
constexpr unsigned SOURCESDK_TF2_ENT_ENTRY_MASK=0x7ff;
struct CMirvTf2ClientUnknown{uint32_t Handle;const void*GetRefEHandle(){return &Handle;}};
struct MirvTf2SceneEntity{struct {int Category;uint64_t Identity,OwnerIdentity;}Snapshot;void*ObservedObject;bool HasMoveParent;uint32_t MoveParent;};
namespace advancedfx {namespace SceneCache {constexpr int CategoryCount=9;std::array<int,3>CategoryColor(int c){return {80+c,220-c,255};}}}
namespace SOURCESDK {namespace L4D2 {struct Alloc {void*IndirectAlloc(size_t n){return malloc(n);}void Free(void*p){free(p);}}alloc,*g_pMemAlloc=&alloc;}namespace TF2 {struct KV {std::map<std::string,int>symbols;void*AllocKeyValuesMemory(size_t n){return malloc(n);}void FreeKeyValuesMemory(void*p){free(p);}int GetSymbolForString(const char*n,bool){auto i=symbols.find(n);if(i!=symbols.end())return i->second;return symbols.emplace(n,symbols.size()+1).first->second;}}kv;KV*KeyValuesSystem(){return &kv;}}}
struct SceneMaterialKeyValues;
struct Material{int refs=1;std::string tint;};std::vector<Material*>created;int ofCalls=0,tfCalls=0;bool abiGood=false,g_OpenFortressLegacyMode=false;
int ofSystem,tfSystem;
void*Factory(const char*n,int*){if(!strcmp(n,"VMaterialSystem080")){++ofCalls;return &ofSystem;}assert(!strcmp(n,"VMaterialSystem082"));++tfCalls;return &tfSystem;}
auto g_AppSystemFactory=Factory;
void*Create(const char*,SceneMaterialKeyValues*);
template<class R,class... Args>R SceneInterfaceCall(void*p,int slot,Args...args){if constexpr(std::is_same_v<R,void*>){assert(slot==70);return Create(args...);}else if constexpr(std::is_same_v<R,bool>){assert(slot==42);return false;}else {assert(slot==13);--static_cast<Material*>(p)->refs;}}
'''
test=r'''
void*Create(const char*name,SceneMaterialKeyValues*root){auto m=new Material;for(auto k=root->m_pSub;k;k=k->m_pPeer)if(k->m_iKeyName==SOURCESDK::TF2::kv.symbols.at("$color2"))m->tint=k->m_sValue;assert(!m->tint.empty());created.push_back(m);SceneDeleteMaterialKeys(root);return m;}
int main(){
 assert(OfSceneViewmodel("cbaseviewmodel","models/x.mdl"));assert(OfSceneViewmodel("cwearablevm","models/x.mdl"));assert(OfSceneCategory("cweaponrocketlauncher","models/weapons/w_rocketlauncher.mdl")==MIRV_TF2_ENTITY_WEAPON);assert(OfSceneCategory("crocketprojectile","models/rocket.mdl")==MIRV_TF2_ENTITY_PROJECTILE);assert(OfSceneCategory("cwearable","models/hats/hat.mdl")==MIRV_TF2_ENTITY_COSMETIC);assert(OfSceneCategory("cparticle","models/particle.mdl")==-1);
 CMirvTf2ClientUnknown host{123};std::vector<MirvTf2SceneEntity>rows={{{MIRV_TF2_ENTITY_PLAYER,1,0},&host,false,0},{{MIRV_TF2_ENTITY_WEAPON,2,0},nullptr,true,123},{{MIRV_TF2_ENTITY_COSMETIC,3,0},nullptr,true,456}};SceneResolveAttachmentHosts(rows);assert(rows.size()==2&&rows[1].Snapshot.OwnerIdentity==1);
 g_OpenFortressLegacyMode=true;assert(!SceneCategoryMaterial(0,false)&&ofCalls==0);assert(!SceneCategoryMaterial(0,true)&&ofCalls==1);abiGood=true;assert(!SceneCategoryMaterial(0,true)&&ofCalls==1);Mirv_Tf2_ReleaseSceneMaterials();auto a=SceneCategoryMaterial(0,true);auto b=SceneCategoryMaterial(1,true);assert(a&&b&&a!=b&&SceneCategoryMaterial(0,true)==a);assert(static_cast<Material*>(a)->tint!=static_cast<Material*>(b)->tint);assert(!SceneCategoryMaterial(0,false));g_OpenFortressLegacyMode=false;auto c=SceneCategoryMaterial(0,false);assert(c&&c!=a&&tfCalls==1);Mirv_Tf2_ReleaseSceneMaterials();for(auto m:created){assert(m->refs==0);delete m;}
 std::cout<<"OF Scene Cache classification, retained attachment ownership, orphan suppression and guarded per-category material policy passed\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'scene.cpp';p.write_text(fixture+category+owner+materials+release+test);exe=Path(d)/'scene';subprocess.run(['g++','-std=c++17','-O1',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
