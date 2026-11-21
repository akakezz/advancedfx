"""Exercise native TF2 observer scopes and pass dispatch using the actual adapters."""
from pathlib import Path
import argparse, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-tf2-render-tests');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
r=Path(__file__).resolve().parents[2];o=a.build_dir;o.mkdir(parents=True,exist_ok=True)
commands=(r/'AfxHookSource/MirvCommands.cpp').read_text();structs=commands[commands.index('struct Tf2FixRecvTable;'):commands.index('static_assert(sizeof(Tf2FixRecvProp)')]
# Compile the real native observer controller with object/receive-table fixtures.
pov=r'''
#include <map>
#include <set>
#include <functional>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <iostream>
#define _WIN64 1
'''+structs+r'''
constexpr int SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2;int g_SourceSdkVer=1;
struct Engine{bool Demo=true,Hltv=false;int Local=1;bool IsPlayingDemo(){return Demo;}bool IsHLTV(){return Hltv;}int GetLocalPlayer(){return Local;}}engine,*g_VEngineClient=&engine;
struct Entity {void **Vtable;void **Network;unsigned Serial;int Mode;unsigned Target;};
Entity local{},target{};std::map<int,Entity*>entities{{1,&local},{2,&target}};
void *Mirv_Tf2_DiagnosticEntity(int index){auto i=entities.find(index);return i==entities.end()?nullptr:i->second;}
unsigned Mirv_Tf2_EntityHandle(const void *e){return static_cast<const Entity*>(e)->Serial;}
bool Mirv_Tf2_GetPlayerInfo(int index,char*,int,int *user){if(!entities.count(index))return false;*user=index+100;return true;}
int Mirv_Tf2_FindCameraTarget(std::uint64_t user,const std::string&){return user==102&&entities.count(2)?2:-1;}
Tf2FixRecvProp props[2];Tf2FixRecvTable table{};Tf2FixClientClass cls{};
void *Network(void *e){return &static_cast<Entity*>(e)->Network;}
void *GetClass(void*){return &cls;}bool dormant=false;bool Dormant(void*){return dormant;}
#include "AfxHookSource/Tf2PovSpectate.inl"
int main(){
 void *unknownTable[5]={};unknownTable[4]=reinterpret_cast<void*>(Network);void *networkTable[9]={};networkTable[2]=reinterpret_cast<void*>(GetClass);networkTable[8]=reinterpret_cast<void*>(Dormant);
 local={unknownTable,networkTable,1234,0,88};target={unknownTable,networkTable,5678,0,99};
 props[0].Name="m_iObserverMode";props[0].Type=0;props[0].Offset=offsetof(Entity,Mode);
 props[1].Name="m_hObserverTarget";props[1].Type=0;props[1].Offset=offsetof(Entity,Target);table.Props=props;table.Count=2;cls.Table=&table;
 assert(MirvPovSpectate_SetPlayer(2));assert(local.Mode==0&&local.Target==88);
 MirvPovSpectate_FrameBegin();assert(local.Mode==4&&local.Target==5678);
 MirvPovSpectate_FrameEnd();assert(local.Mode==0&&local.Target==88);
 // Every frame saves current recorder data, rather than restoring a stale initial snapshot.
 local.Mode=7;local.Target=999;MirvPovSpectate_FrameBegin();MirvPovSpectate_FrameEnd();assert(local.Mode==7&&local.Target==999);
 MirvPovSpectate_FrameBegin();assert(MirvPovSpectate_SetPlayer(0));assert(local.Mode==7&&local.Target==999&&MirvPovSpectate_Player()==0);
 assert(MirvPovSpectate_SetPlayer(2));entities.erase(2);MirvPovSpectate_FrameBegin();assert(local.Mode==7&&local.Target==999);entities[2]=&target;
 dormant=true;MirvPovSpectate_FrameBegin();assert(local.Mode==7&&local.Target==999);dormant=false;
 MirvPovSpectate_FrameBegin();local.Serial++;local.Mode=17;local.Target=19;MirvPovSpectate_FrameEnd();assert(local.Mode==17&&local.Target==19);
 engine.Hltv=true;assert(!MirvPovSpectate_SetPlayer(2));engine.Hltv=false;engine.Demo=false;assert(!MirvPovSpectate_SetPlayer(2));engine.Demo=true;
 props[1].Name="unknown";assert(!MirvPovSpectate_SetPlayer(2));props[1].Name="m_hObserverTarget";
 assert(MirvPovSpectate_SetPlayer(1)&&MirvPovSpectate_Player()==0);
 std::cout<<"Native POV observer scope/identity/restoration checks passed\n";
}
'''
render=r'''
#include <vector>
#include <map>
#include <cstring>
#include <cassert>
#include <iostream>
#include <cstddef>
#include <functional>
#include "AfxHookSource/Tf2RenderPass.h"
#define _WIN64 1
#define __fastcall
using PVOID=void*;
bool AfxDetourPtr(PVOID *slot,PVOID replacement,PVOID *original){*original=*slot;*slot=replacement;return true;}
struct CMaterialSystemFunctor{virtual ~CMaterialSystemFunctor(){};virtual void operator()()=0;};
std::vector<int> colorStack;int mask=15;
void AfxD3D9PushOverrideState(bool){colorStack.push_back(mask);}
void AfxD3D9OverrideBegin_D3DRS_COLORWRITEENABLE(int value){mask=value;}
void AfxD3D9OverrideEnd_D3DRS_COLORWRITEENABLE(){mask=15;}
void AfxD3D9PopOverrideState(){mask=colorStack.back();colorStack.pop_back();}
bool queued=false;std::vector<std::function<void()>> gpuQueue;
void OnGpu(std::function<void()> f){if(queued)gpuQueue.push_back(std::move(f));else f();}
void FlushGpu(){for(auto &f:gpuQueue)f();gpuQueue.clear();}
bool MaterialSystem_ExecuteOnRenderThread(CMaterialSystemFunctor*f){OnGpu([f]{(*f)();delete f;});return true;}
struct Globals{int Count=1;int framecount_get(){return Count;}}globals;
struct View{Globals*GetGlobals(){return &globals;}}g_Hook_VClient_RenderView;
struct Streams {
 bool Recording=true;bool Enabled[5]={true,true,true,true,true};
 std::vector<Tf2RenderPass> Begins,Ends;
 bool Tf2PassRecording(Tf2RenderPass p){return Recording&&Enabled[unsigned(p)];}
 void EngineThread_PassBegin(Tf2RenderPass p){Begins.push_back(p);}
 void EngineThread_PassEnd(Tf2RenderPass p){Ends.push_back(p);}
}g_AfxStreams;
int player,gear,vm,prop;
void Mirv_Tf2_RenderGroups(std::map<const void*,unsigned>&groups){groups={{&player,1},{&gear,1},{&vm,2},{&prop,0}};}
#include "AfxHookSource/Tf2RenderPass.inl"
std::map<Tf2RenderPass,std::vector<const void*>> visible;
int ordinaryFrames=0,renderCalls=0,hudWrites=0;
void Model(void*,const void*,const void *p,void*){auto pointer=static_cast<const Tf2ModelInfoPrefix*>(p)->Renderable;auto pass=g_Tf2ActivePass;OnGpu([pointer,pass]{if(mask)visible[pass].push_back(pointer);});}
void Render(void *self,const void*,int clear,int draw){
 ++renderCalls;assert(clear==2);assert(draw&4);
 for(auto pointer:{&player,&gear,&vm,&prop}){Tf2ModelInfoPrefix info{};info.Renderable=pointer;Tf2PassDrawModel(self,nullptr,&info,nullptr);}
 if(draw&2){OnGpu([]{assert(mask==0);});Tf2RenderPassHud(true);OnGpu([]{assert(mask==15);++hudWrites;});Tf2RenderPassHud(false);OnGpu([]{assert(mask==0);});}
}
void Frame(void*,void*){++ordinaryFrames;assert(mask==15&&colorStack.empty());}
bool PlayerView(void*,void *data){std::memset(data,0,200);return true;}
int main(){
 void *clientSlots[60]={};clientSlots[26]=reinterpret_cast<void*>(Frame);clientSlots[27]=reinterpret_cast<void*>(Render);clientSlots[59]=reinterpret_cast<void*>(PlayerView);void **client=clientSlots;
 void *modelSlots[20]={};modelSlots[19]=reinterpret_cast<void*>(Model);void **model=modelSlots;
 int rect[4]={0,0,1280,720};Tf2RenderPassInstallClient(&client);assert(!Tf2RenderPassSupported());Tf2RenderPassInstallModel(&model);assert(Tf2RenderPassSupported());
 assert(clientSlots[27]==reinterpret_cast<void*>(Render)); // Normal-frame interception, not the tools-only entry.
 Tf2PassRenderFrame(&client,rect);FlushGpu();
 assert(ordinaryFrames==1&&renderCalls==5&&hudWrites==1&&mask==15&&colorStack.empty());
 assert(g_AfxStreams.Begins.size()==5&&g_AfxStreams.Begins==g_AfxStreams.Ends);
 assert((visible[Tf2RenderPass::World]==std::vector<const void*>{&prop}));
 assert((visible[Tf2RenderPass::Players]==std::vector<const void*>{&player,&gear}));
 assert((visible[Tf2RenderPass::Viewmodel]==std::vector<const void*>{&vm}));
 assert(visible[Tf2RenderPass::Hud].empty()&&visible[Tf2RenderPass::Depth].empty());
 Tf2PassRenderFrame(&client,rect);assert(ordinaryFrames==2&&renderCalls==5); // One capture batch per frame.
 globals.Count++;queued=true;visible.clear();Tf2PassRenderFrame(&client,rect);assert(!gpuQueue.empty());FlushGpu();assert(mask==15&&colorStack.empty()&&hudWrites==2);
 assert((visible[Tf2RenderPass::World]==std::vector<const void*>{&prop}));assert((visible[Tf2RenderPass::Players]==std::vector<const void*>{&player,&gear}));assert((visible[Tf2RenderPass::Viewmodel]==std::vector<const void*>{&vm}));
 globals.Count++;g_AfxStreams.Recording=false;Tf2PassRenderFrame(&client,rect);assert(ordinaryFrames==4&&renderCalls==10);
 std::cout<<"TF2 normal-frame pass dispatch, isolation and state restoration checks passed\n";
}
'''
header=(r/'AfxHookSource/AfxStreams.h').read_text();writer_class=header[header.index('class CDrawingRecordScreenOutput'):header.index('class CDrawingRecordScreen  {')]
cpp=(r/'AfxHookSource/AfxStreams.cpp').read_text();writer_method=cpp[cpp.index('void CAfxStreams::CDrawingRecordScreenOutput::ProcessingThreadFunc()'):cpp.index('const char *CAfxStreams::GetRecordScreenSettingsName()')]
writer=r'''
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <list>
#include <cassert>
#include <iostream>
namespace advancedfx {
class CRefCountedThreadSafe {std::atomic<int> Refs{0};public:void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}protected:virtual ~CRefCountedThreadSafe()=default;};
struct Format{};
struct IImageBufferThreadSafe {Format F;std::atomic<int> Refs{1};void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}Format*GetImageBufferFormat(){return &F;}};
std::atomic<int> Created{0},Frames{0},Destroyed{0};
template<bool>struct TIOutVideoStream {void Release(){delete this;}bool SupplyImageBuffer(void*,IImageBufferThreadSafe*){++Frames;return true;}};
class COutVideoStreamCreator:public CRefCountedThreadSafe {public:COutVideoStreamCreator(){++Created;}~COutVideoStreamCreator(){++Destroyed;}TIOutVideoStream<true>*CreateOutVideoStream(const Format&){return new TIOutVideoStream<true>;}};
namespace ImageTransformer {IImageBufferThreadSafe*StripAlpha(void*,void*,IImageBufferThreadSafe*b){b->AddRef();return b;}}
}
struct IAfxD3D9CaptureBuffer {std::atomic<int>Refs{1};advancedfx::IImageBufferThreadSafe*LockCpu(){return new advancedfx::IImageBufferThreadSafe;}void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}};
struct ICaptureOutput {virtual void AddRef()=0;virtual void Release()=0;virtual void OnCapture(IAfxD3D9CaptureBuffer*)=0;};
void*g_pThreadPool=nullptr;int g_ImageBufferPoolThreadSafe=0;void Tier0_Warning(const char*,...){}
class CAfxStreams {public:
'''+writer_class+'};\n'+writer_method+r'''
int main(){
 for(int take=0;take<8;++take){
  auto creator=new advancedfx::COutVideoStreamCreator;creator->AddRef();
  auto output=new CAfxStreams::CDrawingRecordScreenOutput(creator);output->AddRef();creator->Release();
  for(int frame=0;frame<4;++frame){auto capture=new IAfxD3D9CaptureBuffer;output->OnCapture(capture);capture->Release();}
  output->Release(); // Joins and drains worker; its one creator reference is released exactly once.
 }
 assert(advancedfx::Created==8&&advancedfx::Destroyed==8&&advancedfx::Frames==32);
 std::cout<<"Shared recording worker drains frames and releases creator once across repeated takes\n";
}
'''
for name,source in [('pov',pov),('passes',render),('writer',writer)]:
 file=o/(name+'.cpp');file.write_text(source);cmd=['g++','-std=c++17','-O1','-g','-pthread','-I'+str(r),str(file),'-o',str(o/name)]
 if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 q=subprocess.run(cmd,capture_output=True,text=True)
 if q.returncode:print(q.stderr);raise SystemExit(q.returncode)
 q=subprocess.run([str(o/name)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True);print(q.stdout+q.stderr)
 if q.returncode:raise SystemExit(q.returncode)
