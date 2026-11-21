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
#define __fastcall
using PVOID=void*;
bool AfxDetourPtr(PVOID*slot,PVOID replacement,PVOID*original){*original=*slot;*slot=replacement;return true;}
'''+structs+r'''
constexpr int SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2;int g_SourceSdkVer=1;
namespace SOURCESDK {struct QAngle{float x=0,y=0,z=0;};}
struct Engine{SOURCESDK::QAngle Angles{3,12,0};void GetViewAngles(SOURCESDK::QAngle&a){a=Angles;}void SetViewAngles(const SOURCESDK::QAngle&a){Angles=a;}bool Demo=true,Hltv=false;int Local=1;bool IsPlayingDemo(){return Demo;}bool IsHLTV(){return Hltv;}int GetLocalPlayer(){return Local;}}engine,*g_VEngineClient=&engine;
struct TauntVar {float Value=0;float GetFloat(){return Value;}void SetValue(float v){Value=v;}}tauntVar;
struct Vars{TauntVar*FindVar(const char*name){return !std::strcmp(name,"tf_taunt_first_person")?&tauntVar:nullptr;}}vars;
namespace SOURCESDK{namespace L4D2{Vars*g_pCVar=nullptr;}namespace CSS{Vars*g_pCVar=&vars;}}
struct Entity {void **Vtable;void **Network;unsigned Serial;int Mode;unsigned Target;float Pitch=0,Yaw=0;int Fov=0,FovStart=0,DefaultFov=90;float Punch[3]={},ViewOffset[3]={0,0,64};bool Taunting=true;};
Entity local{},target{};std::map<int,Entity*>entities{{1,&local},{2,&target}};
void *Mirv_Tf2_DiagnosticEntity(int index){auto i=entities.find(index);return i==entities.end()?nullptr:i->second;}
unsigned Mirv_Tf2_EntityHandle(const void *e){return static_cast<const Entity*>(e)->Serial;}
bool Mirv_Tf2_GetPlayerInfo(int index,char*,int,int *user){if(!entities.count(index))return false;*user=index+100;return true;}
int Mirv_Tf2_FindCameraTarget(std::uint64_t user,const std::string&){return user==102&&entities.count(2)?2:-1;}
Tf2FixRecvProp props[9];Tf2FixRecvTable table{};Tf2FixClientClass cls{};
void *Network(void *e){return &static_cast<Entity*>(e)->Network;}
void EngineView(void*,SOURCESDK::QAngle&a){a=engine.Angles;}
void *GetClass(void*){return &cls;}bool dormant=false;bool Dormant(void*){return dormant;}
#include "AfxHookSource/Tf2PovSpectate.inl"
int main(){
 void *engineTable[19]={};engineTable[18]=reinterpret_cast<void*>(EngineView);void **rawEngine=engineTable;MirvPovSpectate_InstallEngine(&rawEngine);
 auto nativeAngles=[&]{SOURCESDK::QAngle a;reinterpret_cast<void(*)(void*,SOURCESDK::QAngle&)>(engineTable[18])(&rawEngine,a);return a;};
 void *unknownTable[5]={};unknownTable[4]=reinterpret_cast<void*>(Network);void *networkTable[9]={};networkTable[2]=reinterpret_cast<void*>(GetClass);networkTable[8]=reinterpret_cast<void*>(Dormant);
 local={unknownTable,networkTable,1234,0,88};target={unknownTable,networkTable,5678,0,99};
 props[0].Name="m_iObserverMode";props[0].Type=0;props[0].Offset=offsetof(Entity,Mode);
 props[1].Name="m_hObserverTarget";props[1].Type=0;props[1].Offset=offsetof(Entity,Target);table.Props=props;table.Count=2;cls.Table=&table;
 assert(MirvPovSpectate_SetPlayer(2));assert(local.Mode==0&&local.Target==88&&tauntVar.Value==1&&local.Taunting);
 MirvPovSpectate_FrameBegin();assert(local.Mode==4&&local.Target==5678);
 MirvPovSpectate_FrameEnd();assert(local.Mode==0&&local.Target==88);
 // Native chase must use interpolated TARGET angles, not the recorder's angles.
 props[2].Name="m_angEyeAngles[0]";props[2].Type=1;props[2].Offset=offsetof(Entity,Pitch);
 props[3].Name="m_angEyeAngles[1]";props[3].Type=1;props[3].Offset=offsetof(Entity,Yaw);table.Count=9;
 props[4].Name="m_iFOV";props[4].Type=0;props[4].Offset=offsetof(Entity,Fov);
 props[5].Name="m_iFOVStart";props[5].Type=0;props[5].Offset=offsetof(Entity,FovStart);
 props[6].Name="m_iDefaultFOV";props[6].Type=0;props[6].Offset=offsetof(Entity,DefaultFov);
 props[7].Name="m_vecPunchAngle";props[7].Type=2;props[7].Offset=offsetof(Entity,Punch);
 props[8].Name="m_vecViewOffset";props[8].Type=2;props[8].Offset=offsetof(Entity,ViewOffset);
 local.Fov=20;local.FovStart=10;local.DefaultFov=75;local.Punch[0]=3;local.ViewOffset[2]=24;
 target.DefaultFov=90;target.ViewOffset[2]=72;
 assert(MirvPovSpectate_SetMode(true));target.Pitch=10;target.Yaw=70;
 MirvPovSpectate_FrameBegin();assert(local.Mode==5&&engine.Angles.y==12);assert(local.Fov==0&&local.FovStart==90&&local.DefaultFov==90&&local.Punch[0]==0&&local.ViewOffset[2]==72&&local.Taunting&&tauntVar.Value==1);
 target.Pitch=11;target.Yaw=71;auto observed=nativeAngles();assert(observed.x==11&&observed.y==71&&engine.Angles.x==3&&engine.Angles.y==12);
 MirvPovSpectate_ViewDone();assert(local.Mode==5&&local.Fov==20&&local.FovStart==10&&local.DefaultFov==75&&local.Punch[0]==3&&local.ViewOffset[2]==24);
 MirvPovSpectate_FrameEnd();assert(local.Mode==0&&local.Target==88&&engine.Angles.x==3&&engine.Angles.y==12);
 MirvPovSpectate_FrameBegin();assert(nativeAngles().y==71);assert(MirvPovSpectate_SetPlayer(0));assert(engine.Angles.y==12&&local.Mode==0&&tauntVar.Value==0&&local.Taunting);
 assert(MirvPovSpectate_SetMode(true)&&MirvPovSpectate_SetPlayer(2));table.Count=2;MirvPovSpectate_FrameBegin();assert(local.Mode==0&&nativeAngles().y==12);
 assert(MirvPovSpectate_SetMode(false));table.Count=4;
 // Every frame saves current recorder data, rather than restoring a stale initial snapshot.
 local.Mode=7;local.Target=999;MirvPovSpectate_FrameBegin();MirvPovSpectate_FrameEnd();assert(local.Mode==7&&local.Target==999);
 MirvPovSpectate_FrameBegin();assert(MirvPovSpectate_SetPlayer(0));assert(local.Mode==7&&local.Target==999&&MirvPovSpectate_Player()==0);
 assert(MirvPovSpectate_SetPlayer(2));entities.erase(2);MirvPovSpectate_FrameBegin();assert(local.Mode==7&&local.Target==999);entities[2]=&target;
 dormant=true;MirvPovSpectate_FrameBegin();assert(local.Mode==7&&local.Target==999);dormant=false;
 MirvPovSpectate_FrameBegin();local.Serial++;local.Mode=17;local.Target=19;MirvPovSpectate_FrameEnd();assert(local.Mode==17&&local.Target==19);
 engine.Hltv=true;assert(!MirvPovSpectate_SetPlayer(2));engine.Hltv=false;engine.Demo=false;assert(!MirvPovSpectate_SetPlayer(2));engine.Demo=true;
 props[1].Name="unknown";assert(!MirvPovSpectate_SetPlayer(2));props[1].Name="m_hObserverTarget";
 assert(MirvPovSpectate_SetMode(true));assert(MirvPovSpectate_SetPlayer(1)&&MirvPovSpectate_Player()==0&&MirvPovSpectate_ThirdPerson());assert(MirvPovSpectate_SetPlayer(0)&&!MirvPovSpectate_ThirdPerson());
 tauntVar.Value=2;assert(MirvPovSpectate_SetPlayer(2)&&tauntVar.Value==1);assert(MirvPovSpectate_SetPlayer(0)&&tauntVar.Value==2);
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
capture_at=header.index('class CPassCaptureFunctor :')
brace=header.index('{',capture_at);depth=1;end=brace+1
while depth:
 if header[end]=='{':depth+=1
 elif header[end]=='}':depth-=1
 end+=1
capture_class=header[capture_at:end+1]
capture_method=cpp[cpp.index('bool CAfxStreams::DrawingThread_BindTf2PassTarget()'):cpp.index('void CAfxStreams::Console_RecordPass(')]
capture=r'''
#include <cassert>
#include <iostream>
#include <cstdint>
#include "AfxHookSource/Tf2RenderPass.h"
using DWORD=unsigned;constexpr unsigned D3DCLEAR_TARGET=1,D3DCLEAR_ZBUFFER=2;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
struct D3DVIEWPORT9{unsigned X=0,Y=0,Width=1280,Height=720;};
struct Surface{unsigned Samples;int Refs=0;unsigned Pixels=0;void AddRef(){++Refs;}void Release(){assert(Refs>0);--Refs;}};Surface original{4},msaa{4},single{0},intz{0},other{8};
using IDirect3DSurface9=Surface;
Surface *color=&original,*depth=&original,*trackedColor=&original,*trackedDepth=&original;
Surface*AfxGetRenderTargetSurface(){return trackedColor;}Surface*AfxGetDepthStencilSurface(){return trackedDepth;}unsigned clears=0,lastClear=0,draws=0,captures=0;
struct Device{
 D3DVIEWPORT9 Viewport;
 int Clear(unsigned,void*,unsigned,unsigned value,float,unsigned){assert(color&&depth&&color->Samples==depth->Samples);color->Pixels=0;lastClear=value;++clears;return 0;}
 int GetViewport(D3DVIEWPORT9*out){*out=Viewport;return 0;}
 int SetViewport(const D3DVIEWPORT9*in){Viewport=*in;return 0;}
 int GetRenderTarget(unsigned,Surface**out){*out=color;if(color)color->AddRef();return color?0:-1;}
 int GetDepthStencilSurface(Surface**out){*out=depth;if(depth)depth->AddRef();return depth?0:-1;}
 int SetRenderTarget(unsigned,Surface*in){color=in;Viewport.Width=1280;Viewport.Height=720;return 0;}
 int SetDepthStencilSurface(Surface*in){depth=in;return 0;}
}device;
Device*AfxGetDirect3DDevice9(){return &device;}
constexpr int AfxDrawDepthEncode_Gray=0,AfxDrawDepthMode_Inverse=0;
void AfxDrawDepth(int,int,bool,float,float,unsigned,unsigned,unsigned,unsigned,float,float,void*){assert(color->Samples==0&&depth==&intz);}
void Tier0_Warning(const char*,...){}
struct CMaterialSystemFunctor{virtual ~CMaterialSystemFunctor(){};virtual void operator()()=0;};
namespace advancedfx {struct COutVideoStreamCreator {int Refs=1;void AddRef(){++Refs;}void Release(){--Refs;}};}
struct CCaptureInputRenderTarget{};
struct CDrawingRecordScreenOutput{CDrawingRecordScreenOutput(advancedfx::COutVideoStreamCreator*){}};
struct CCaptureNode{
 CCaptureInputRenderTarget *Input;CDrawingRecordScreenOutput *Output;
 CCaptureNode(CCaptureInputRenderTarget*i,CDrawingRecordScreenOutput*o):Input(i),Output(o){}
 void AddRef(){}void Release(){delete Input;delete Output;delete this;}void GpuRelease(){}
 void GpuCapture(){assert(color==trackedColor&&color->Samples==depth->Samples&&color->Pixels>0);++captures;}
};
struct CAfxStreams{
 CCaptureNode*m_Tf2PassNodes[5]={};bool m_Tf2PassActive[5]={},m_Tf2PassPixelsValid[5]={};bool Allocate=true;
 Surface *m_Tf2PassPreviousColor[5]={},*m_Tf2PassPreviousDepth[5]={};D3DVIEWPORT9 m_Tf2PassPreviousViewport[5]={};
 bool DrawingThread_BindTf2PassTarget();void DrawingThread_RestoreTf2PassBindings(Tf2RenderPass pass);
 unsigned Tf2PassBackground(){return 0xff00ff00u;}
 Surface*DrawingThread_GetOrCreateRenderTargetSurfaceNoMssaa(){return Allocate?&single:nullptr;}
 Surface*DrawingThread_GetOrCreateRenderTargetSurface(){return Allocate?&msaa:nullptr;}
 Surface*DrawingThread_GetOrCreateIntZTextureSurface(){return Allocate?&intz:nullptr;}
 void DrawingThread_SetRenderTargetNoMsaa(){if(color==trackedColor)color=&single;trackedColor=&single;}
 void DrawingThread_SetRenderTarget(){if(color==trackedColor)color=&msaa;trackedColor=&msaa;}
 void DrawingThread_SetIntZTextureSurface(){if(depth==trackedDepth)depth=&intz;trackedDepth=&intz;}
 void DrawingThread_UnsetIntZTextureSurface(){trackedDepth=&original;if(depth==&intz)depth=&original;}
 void DrawingThread_UnsetRenderTargetNoMsaa(bool){trackedColor=&original;if(color==&single)color=&original;}
 void DrawingThread_UnsetRenderTarget(bool){trackedColor=&original;if(color==&msaa)color=&original;}
'''+capture_class+'\n}g_AfxStreams;\n'+capture_method+r'''
int main(){
 advancedfx::COutVideoStreamCreator creator;
 for(unsigned samples:{0u,2u,4u}){original.Samples=msaa.Samples=samples;for(bool differentTarget:{false,true}){auto prior=differentTarget?&other:&original;color=depth=prior;device.Viewport.Width=640;device.Viewport.Height=480;for(unsigned i=0;i<5;++i){
  auto pass=Tf2RenderPass(i);CAfxStreams::CPassCaptureFunctor create(pass,CAfxStreams::CPassCaptureFunctor::Create,&creator);create();
  CAfxStreams::CPassCaptureFunctor begin(pass,CAfxStreams::CPassCaptureFunctor::Begin);begin();
  assert(color==trackedColor&&color->Samples==depth->Samples);++draws;++color->Pixels; // D3D9 rejects incompatible geometry targets.
  if(pass==Tf2RenderPass::Players||pass==Tf2RenderPass::Viewmodel||pass==Tf2RenderPass::Hud)assert(lastClear==0xff00ff00u);else assert(lastClear==0);
  CAfxStreams::CPassCaptureFunctor end(pass,CAfxStreams::CPassCaptureFunctor::End);end();assert(color==prior&&depth==prior&&device.Viewport.Width==640&&device.Viewport.Height==480);
  CAfxStreams::CPassCaptureFunctor destroy(pass,CAfxStreams::CPassCaptureFunctor::Destroy);destroy();
 }
 }
 }
 assert(draws==30&&captures==30&&clears==30&&original.Refs==0&&other.Refs==0);color=depth=&original;
 {
 auto pass=Tf2RenderPass::Players;CAfxStreams::CPassCaptureFunctor create(pass,CAfxStreams::CPassCaptureFunctor::Create,&creator);create();
 CAfxStreams::CPassCaptureFunctor begin(pass,CAfxStreams::CPassCaptureFunctor::Begin,nullptr,0xff0000ffu);begin();assert(lastClear==0xff0000ffu);++color->Pixels;
 CAfxStreams::CPassCaptureFunctor end(pass,CAfxStreams::CPassCaptureFunctor::End);end();CAfxStreams::CPassCaptureFunctor destroy(pass,CAfxStreams::CPassCaptureFunctor::Destroy);destroy();
 }
 g_AfxStreams.Allocate=false;CAfxStreams::CPassCaptureFunctor begin(Tf2RenderPass::World,CAfxStreams::CPassCaptureFunctor::Begin);begin();CAfxStreams::CPassCaptureFunctor end(Tf2RenderPass::World,CAfxStreams::CPassCaptureFunctor::End);end();assert(color==&original&&depth==&original&&captures==31);
 std::cout<<"Actual capture functor keeps MSAA color/depth compatible and chroma mattes green\n";
}
'''
for name,source in [('pov',pov),('passes',render),('writer',writer),('capture',capture)]:
 file=o/(name+'.cpp');file.write_text(source);cmd=['g++','-std=c++17','-O1','-g','-pthread','-I'+str(r),str(file),'-o',str(o/name)]
 if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 q=subprocess.run(cmd,capture_output=True,text=True)
 if q.returncode:print(q.stderr);raise SystemExit(q.returncode)
 q=subprocess.run([str(o/name)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True);print(q.stdout+q.stderr)
 if q.returncode:raise SystemExit(q.returncode)
