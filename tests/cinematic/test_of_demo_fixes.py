"""Source-extracted OF demo-fix gates, real repair math and expected-error binding.
Windows module/Detours preflight is simulated; this is not a native ABI test.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'AfxHookSource/MirvCommands.cpp').read_text()
types=s[s.index('struct Tf2FixRecvTable;'):s.index('bool g_Tf2PovDeathcam =')]
state=s[s.index('using PlayerRepair = advancedfx::PlayerRepair;'):s.index('// Short, opt-in diagnostic.')]
repair=s[s.index('void RepairFlush()'):s.index('} // namespace\nadvancedfx::PlayerRepair &MirvFix_PlayerRepairModel()')]
identity=s[s.index('struct RepairDemoIdentity'):s.index('void BindRepairDemoIdentity(')]
prop=s[s.index('namespace {\nusing Tf2ComputePropOpacity'):s.index('CON_COMMAND(mirv_fix,')]
model=(root/'shared/PlayerRepair.cpp').read_text();model=model[model.index('namespace advancedfx'):model.index('bool PlayerRepair::Save(')]+'}\n'
pre=r'''
#include "shared/PlayerRepair.h"
#include "shared/AfxMath.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#define _WIN64 1
#define __thiscall
#define __fastcall
using LONG=long; using PVOID=void*;
constexpr LONG NO_ERROR=0;
int installs=0;bool detourFailure=false;
LONG DetourTransactionBegin(){return 0;}LONG DetourUpdateThread(void*){return 0;}
LONG DetourAttach(PVOID*,PVOID){++installs;return detourFailure?1:0;}
LONG DetourTransactionCommit(){return 0;}void DetourTransactionAbort(){}
void*GetCurrentThread(){return nullptr;}void*GetModuleHandleA(const char*){return (void*)1;}
constexpr int SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2,SourceSdkVer_HL2MP=3;
int g_SourceSdkVer=3;bool g_OpenFortressLegacyMode=true,g_OfNativeCommands=false,known=false,slots=false,executable=false;
bool OfDemoFixCandidate(){return g_OpenFortressLegacyMode&&g_SourceSdkVer==3&&g_OfNativeCommands&&known;}
namespace OfNativeInterfaces{bool Slots(void*p,const char*,std::initializer_list<unsigned>){return p&&slots;}bool Read(const void*p,void*out,size_t n){if(!p)return false;memcpy(out,p,n);return true;}bool Executable(void*p,void*){return p&&executable;}}
struct Engine{bool Playing=true,Loading=false;std::string Dir;bool IsPlayingDemo(){return Playing;}bool IsDrawingLoadingImage(){return Loading;}const char*GetGameDirectory(){return Dir.c_str();}}engine,*g_VEngineClient=&engine;
int tick=15;int user=42;bool GetCurrentDemoTick(int&out){out=tick;return true;}bool Mirv_Tf2_GetPlayerInfo(int index,char*,int,int*out){*out=user;return index==1;}
void*Factory(const char*,int*);auto g_AppSystemFactory=Factory;
'''
mid=r'''
Tf2FixClientClass live{},other{};const void*selected=&live;
const void*Mirv_Of_PlayerRecvClass(int){return selected;}
Tf2FixClientClass*Classes(void*){return &live;}
struct {bool Active=false;}g_PlayerAngleTrace;
bool AngleTraceTarget(int){return false;}void AngleTraceTick(int){}
void AngleTraceReceive(const RepairProperty&,const Tf2FixProxyData*,const Tf2FixProxyData*,void*,void*,int){}
float forwarded=0;
void Original(const Tf2FixProxyData*d,void*,void*){forwarded=d->Value.Float;}
float propFactor=0;void PropOriginal(void*,const float*,float factor){propFactor=factor;}
void*propSlots[6]{};void**propObject=propSlots;
void*Factory(const char*name,int*){return !strcmp(name,"StaticPropMgrClient004")?&propObject:nullptr;}
'''
main=r'''
int main(int argc,char**argv){
    // Actual selected class need not be called DT_TFPlayer. Only the required
    // network position, eye-angle and life-state fields are hooked.
    Tf2FixRecvProp fields[3]{};fields[0].Name="m_vecOrigin";fields[0].Type=2;fields[1].Name="m_angEyeAngles";fields[1].Type=3;fields[2].Name="m_lifeState";fields[2].Type=0;
    for(auto&p:fields)p.Proxy=Original;
    Tf2FixRecvTable root{fields,3,nullptr,"DT_CustomOFPlayer",true,true};live.Table=&root;
    Tf2FixRecvTable another=root;other.Table=&another;
    void*clientSlots[9]{};clientSlots[8]=(void*)Classes;void**clientObject=clientSlots;g_Tf2FixClient=&clientObject;
    assert(!InstallRepairProxies(1)&&installs==0);
    g_OfNativeCommands=true;known=true;slots=true;
    selected=nullptr;assert(!InstallRepairProxies(1)&&installs==0);selected=&live;
    engine.Loading=true;assert(!InstallRepairProxies(1)&&installs==0);engine.Loading=false;
    root.Count=2;assert(!InstallRepairProxies(1)&&installs==0);root.Count=3;
    assert(!InstallRepairProxies(1)&&installs==0);executable=true;
    assert(InstallRepairProxies(1)&&g_RepairHooks&&installs==1&&g_RepairProperties.size()==3);
    selected=&other;assert(!InstallRepairProxies(1)&&installs==1);selected=&live;
    assert(InstallRepairProxies(1)&&installs==1);
    // Real model and receive-substitution path: bad angle at tick 15 is replaced
    // by interpolation between original samples, while other entities pass through.
    g_RepairDemo=g_PlayerRepair.Demo="same";g_PlayerRepair.UserId=42;g_RepairEntity=1;
    PlayerRepair::Sample a,b;a.Mask=b.Mask=PlayerRepair::All;a.Value[3]=10;b.Value[3]=30;
    assert(g_PlayerRepair.Observe(10,a)&&g_PlayerRepair.Observe(20,b));
    PlayerRepair::Range r;r.First=r.Last=15;r.Mask=PlayerRepair::Angles;r.Smooth=false;assert(g_PlayerRepair.Exclude(r));
    g_RepairEnabled=true;Tf2FixProxyData data{};data.Prop=&fields[1];data.Value.Vector[0]=90;data.ObjectId=1;
    RepairReceive(0,&data,nullptr,nullptr);assert(forwarded==20&&data.Value.Vector[0]==90&&g_RepairChanged==1);
    data.ObjectId=2;RepairReceive(0,&data,nullptr,nullptr);assert(forwarded==90);
    data.ObjectId=1;user=43;RepairReceive(0,&data,nullptr,nullptr);assert(forwarded==90);user=42;
    g_OfNativeCommands=false;RepairReceive(0,&data,nullptr,nullptr);assert(forwarded==90);
    // Static prop uses the shared versioned interface; no activation on live play/loading.
    propSlots[5]=(void*)PropOriginal;assert(!MirvFix_StaticPropFadeSupported());g_OfNativeCommands=true;
    slots=false;assert(!MirvFix_StaticPropFadeSupported());slots=true;
    assert(MirvFix_StaticPropFadeSupported()&&!MirvFix_GetNoStaticPropFade());
    engine.Playing=false;assert(!MirvFix_SetNoStaticPropFade(true));engine.Playing=true;
    engine.Loading=true;assert(!MirvFix_SetNoStaticPropFade(true));engine.Loading=false;
    assert(MirvFix_SetNoStaticPropFade(true));Tf2DemoComputePropOpacity(&propObject,nullptr,2.f);assert(propFactor==-1.f);
    engine.Playing=false;Tf2DemoComputePropOpacity(&propObject,nullptr,2.f);assert(propFactor==2.f);
    MirvFix_SetNoStaticPropFade(false);engine.Playing=true;Tf2DemoComputePropOpacity(&propObject,nullptr,2.f);assert(propFactor==2.f);
    // Invalid paths and headers are expected, nonthrowing errors. Valid protocol-3
    // header identity is independent of whether event indexing supports the network stream.
    engine.Dir=argv[1];RepairDemoIdentity id;std::string error;
    assert(!ReadRepairDemoIdentity("missing",id,error)&&!error.empty());
    auto file=std::filesystem::path(engine.Dir)/"example.dem";unsigned char h[1072]{};memcpy(h,"HL2DEMO",7);h[8]=3;h[12]=99;h[1060]=20;memcpy(h+536,"dm_test",7);
    {std::ofstream out(file,std::ios::binary);out.write((char*)h,sizeof(h));}
    assert(ReadRepairDemoIdentity("example",id,error)&&id.Ticks==20&&id.Map=="dm_test"&&error.empty());
    h[8]=4;{std::ofstream out(file,std::ios::binary);out.write((char*)h,sizeof(h));}
    assert(!ReadRepairDemoIdentity("example",id,error)&&!error.empty());
    std::cout<<"OF selected-class repair, shared substitution, static prop gates and demo binding passed\n";
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d);cpp=d/'fix.cpp';exe=d/'fix';cpp.write_text(pre+types+state+mid+repair+identity+prop+model+main)
    subprocess.run(['g++','-std=c++17','-Dabstract=','-DAFX_CINEMATIC_NO_XML','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root/'AfxHookSource'),'-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(root),str(cpp),str(root/'shared/AfxMath.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(d)],check=True)
