"""Exercise native TF2 observer scopes and pass dispatch using the actual adapters."""
from pathlib import Path
import argparse, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-tf2-render-tests');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
r=Path(__file__).resolve().parents[2];o=a.build_dir;o.mkdir(parents=True,exist_ok=True)
sdk=(r/'deps/release/prop/AfxHookSource/SourceInterfaces.h').read_text();prefix=sdk.split('class IVEngineClient_013')[1].split('GetViewAngles')[0];assert prefix.count('virtual')-1==19, 'Review TF2 engine ABI before updating adapter fixtures'
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
#include <cmath>
#define __declspec(x)
#define __forceinline inline
#define __int16 short
#define __int32 int
#define __int64 long long
#include "deps/release/prop/AfxHookSource/SourceSdkShared.h"
struct Engine{SOURCESDK::QAngle Angles{3,12,0};void GetViewAngles(SOURCESDK::QAngle&a){a=Angles;}void SetViewAngles(const SOURCESDK::QAngle&a){Angles=a;}bool Demo=true,Hltv=false;int Local=1,FirstPersonRequests=0;void ExecuteClientCmd(const char*c){assert(std::string(c)=="firstperson");++FirstPersonRequests;}bool IsPlayingDemo(){return Demo;}bool IsHLTV(){return Hltv;}int GetLocalPlayer(){return Local;}}engine,*g_VEngineClient=&engine;
struct TauntVar {float Value=0;float GetFloat(){return Value;}void SetValue(float v){Value=v;}}tauntVar;
TauntVar bodyVar,viewmodelVar,demoVar;
struct Vars{TauntVar*FindVar(const char*name){if(!std::strcmp(name,"tf_taunt_first_person"))return &tauntVar;if(!std::strcmp(name,"cl_first_person_uses_world_model"))return &bodyVar;if(!std::strcmp(name,"r_drawviewmodel"))return &viewmodelVar;if(!std::strcmp(name,"cl_demoviewoverride"))return &demoVar;return nullptr;}}vars;
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
void ClientEngineView(void*,SOURCESDK::QAngle&a){a=engine.Angles;a.y+=90;}
void *GetClass(void*){return &cls;}bool dormant=false;bool Dormant(void*){return dormant;}
#include "AfxHookSource/Tf2PovSpectate.inl"
int main(){
 viewmodelVar.Value=demoVar.Value=1;
 void *engineTable[20]={};engineTable[19]=reinterpret_cast<void*>(EngineView);void **rawEngine=engineTable;MirvPovSpectate_InstallEngine(&rawEngine);
 void *clientTable[20]={};clientTable[19]=reinterpret_cast<void*>(ClientEngineView);void **clientEngine=clientTable;
 MirvPovSpectate_InstallEngine(&clientEngine);MirvPovSpectate_InstallEngine(&clientEngine); // Duplicate/aliased factory results must not recurse.
 auto clientAngles=[&]{SOURCESDK::QAngle a;reinterpret_cast<void(*)(void*,SOURCESDK::QAngle&)>(clientTable[19])(&clientEngine,a);return a;};
 auto nativeAngles=[&]{SOURCESDK::QAngle a;reinterpret_cast<void(*)(void*,SOURCESDK::QAngle&)>(engineTable[19])(&rawEngine,a);return a;};
 void *unknownTable[5]={};unknownTable[4]=reinterpret_cast<void*>(Network);void *networkTable[9]={};networkTable[2]=reinterpret_cast<void*>(GetClass);networkTable[8]=reinterpret_cast<void*>(Dormant);
 local={unknownTable,networkTable,1234,0,88};target={unknownTable,networkTable,5678,0,99};
 props[0].Name="m_iObserverMode";props[0].Type=0;props[0].Offset=offsetof(Entity,Mode);
 props[1].Name="m_hObserverTarget";props[1].Type=0;props[1].Offset=offsetof(Entity,Target);table.Props=props;table.Count=2;cls.Table=&table;
 assert(MirvPovSpectate_SetPlayer(2));assert(local.Mode==0&&local.Target==88&&tauntVar.Value==1&&local.Taunting);
 assert(!MirvPovSpectate_DrawRecorderWorldModel());
 MirvPovSpectate_FrameBegin();assert(local.Mode==4&&local.Target==5678&&MirvPovSpectate_DrawRecorderWorldModel()&&MirvPovSpectate_HiddenPlayer()==2);
 MirvPovSpectate_ViewDone(nullptr);assert(MirvPovSpectate_DrawRecorderWorldModel()); // Visibility stays scoped through drawing.
 MirvPovSpectate_FrameEnd();assert(local.Mode==0&&local.Target==88&&!MirvPovSpectate_DrawRecorderWorldModel()&&clientAngles().y==102);
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
 MirvPovSpectate_FrameBegin();assert(local.Mode==5&&engine.Angles.y==12&&MirvPovSpectate_HiddenPlayer()==0);assert(local.Fov==0&&local.FovStart==90&&local.DefaultFov==90&&local.Punch[0]==0&&local.ViewOffset[2]==72&&local.Taunting&&tauntVar.Value==1);
 target.Pitch=11;target.Yaw=71;auto observed=nativeAngles();assert(observed.x==11&&observed.y==71&&observed.z==0&&engine.Angles.x==3&&engine.Angles.y==12);
 assert(clientAngles().y==71);engine.Angles.y=-179;assert(clientAngles().y==71);engine.Angles.y=179;assert(clientAngles().y==71);engine.Angles.y=12;
 SOURCESDK::QAngle finalView{0,12,0};MirvPovSpectate_ViewDone(&finalView);assert(finalView.y==12&&nativeAngles().y==12&&clientAngles().y==102&&bodyVar.Value==1&&viewmodelVar.Value==0&&demoVar.Value==0&&engine.FirstPersonRequests>0);assert(local.Mode==5&&local.Fov==20&&local.FovStart==10&&local.DefaultFov==75&&local.Punch[0]==3&&local.ViewOffset[2]==24);
 MirvPovSpectate_FrameEnd();assert(local.Mode==0&&local.Target==88&&engine.Angles.x==3&&engine.Angles.y==12);
 MirvPovSpectate_FrameBegin();assert(nativeAngles().y==71);assert(MirvPovSpectate_SetPlayer(0));assert(engine.Angles.y==12&&local.Mode==0&&tauntVar.Value==0&&local.Taunting&&bodyVar.Value==0&&viewmodelVar.Value==1&&demoVar.Value==1);
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
void AfxD3D9OverrideBegin_PlayerHoldout(bool){}void AfxD3D9OverrideBegin_D3DRS_ZWRITEENABLE(bool){}void AfxD3D9OverrideEnd_D3DRS_ZWRITEENABLE(){}
void AfxD3D9PopOverrideState(){mask=colorStack.back();colorStack.pop_back();}
bool queued=false;std::vector<std::function<void()>> gpuQueue;
void OnGpu(std::function<void()> f){if(queued)gpuQueue.push_back(std::move(f));else f();}
void FlushGpu(){for(auto &f:gpuQueue)f();gpuQueue.clear();}
bool MaterialSystem_ExecuteOnRenderThread(CMaterialSystemFunctor*f){OnGpu([f]{(*f)();delete f;});return true;}
struct Globals{int Count=1;int framecount_get(){return Count;}}globals;
struct View{Globals*GetGlobals(){return &globals;}}g_Hook_VClient_RenderView;
struct Streams {
 bool Recording=true,Alpha=false;bool Tf2PassAlpha(){return Alpha;}std::vector<int>Samples;bool Enabled[5]={true,true,true,true,true};
 std::vector<Tf2RenderPass> Begins,Ends;
 bool DrawingThread_Tf2White(bool){return true;}bool DrawingThread_BindTf2PassTarget(){return true;}
 bool Tf2PassRecording(Tf2RenderPass p){return Recording&&Enabled[unsigned(p)];}
 void EngineThread_PassBegin(Tf2RenderPass p,int sample){Samples.push_back(sample);Begins.push_back(p);}
 void EngineThread_PassCapture(Tf2RenderPass p){assert(p==Tf2RenderPass::Players);}
 void EngineThread_PassEnd(Tf2RenderPass p,float near,float far){assert(near==3&&far==28000);Ends.push_back(p);}
}g_AfxStreams;
int player,gear,vm,prop;
int hiddenPlayer=0;int MirvPovSpectate_HiddenPlayer(){return hiddenPlayer;}
void Mirv_Tf2_RenderGroups(std::map<const void*,unsigned>&groups,int hidden){groups={{&player,1u|(hidden?4u:0u)},{&gear,1u|(hidden?4u:0u)},{&vm,2},{&prop,0}};}
#include "AfxHookSource/Tf2RenderPass.inl"
std::map<Tf2RenderPass,std::vector<const void*>> visible,depthDraws;std::map<Tf2RenderPass,int> hudByPass;
int ordinaryFrames=0,renderCalls=0,hudWrites=0;
void Model(void*,const void*,const void *p,void*){auto pointer=static_cast<const Tf2ModelInfoPrefix*>(p)->Renderable;auto pass=g_Tf2ActivePass;OnGpu([pointer,pass]{depthDraws[pass].push_back(pointer);if(mask)visible[pass].push_back(pointer);});}
void Render(void *self,const void*,int clear,int draw){
 ++renderCalls;assert(clear==2);assert(draw&4);
 for(auto pointer:{&player,&gear,&vm,&prop}){Tf2ModelInfoPrefix info{};info.Renderable=pointer;Tf2PassDrawModel(self,nullptr,&info,nullptr);}
 Tf2PassSceneEnd(self);
 if(draw&2){const bool base=g_Tf2ActivePass==Tf2RenderPass::World;OnGpu([base]{assert(mask==(base?15:0));});Tf2PassPaint(self,2);OnGpu([base]{assert(mask==(base?15:0));});}
}
void Frame(void*self,void*){++ordinaryFrames;assert(mask==15&&colorStack.empty());
 for(auto pointer:{&player,&gear,&vm,&prop}){Tf2ModelInfoPrefix info{};info.Renderable=pointer;Tf2PassDrawModel(self,nullptr,&info,nullptr);}
}
void SceneEnd(void*){}
void Paint(void*,int mode){assert(mode==2);const auto pass=g_Tf2ActivePass;OnGpu([pass]{assert(mask==15);++hudWrites;++hudByPass[pass];});}
bool PlayerView(void*,void *data){std::memset(data,0,200);auto&v=*static_cast<Tf2ViewProjectionPrefix*>(data);v.Near=3;v.Far=28000;return true;}
int main(){
 void *clientSlots[60]={};clientSlots[26]=reinterpret_cast<void*>(Frame);clientSlots[27]=reinterpret_cast<void*>(Render);clientSlots[59]=reinterpret_cast<void*>(PlayerView);void **client=clientSlots;
 void *modelSlots[20]={};modelSlots[19]=reinterpret_cast<void*>(Model);void **model=modelSlots;
 int rect[4]={0,0,1280,720};Tf2RenderPassInstallClient(&client);assert(!Tf2RenderPassSupported());Tf2RenderPassInstallModel(&model);assert(!Tf2RenderPassSupported());
 void *paintSlots[37]={};paintSlots[9]=reinterpret_cast<void*>(SceneEnd);paintSlots[36]=reinterpret_cast<void*>(Paint);void **paint=paintSlots;Tf2RenderPassInstallRenderView(&paint);assert(Tf2RenderPassSupported());
 assert(clientSlots[27]==reinterpret_cast<void*>(Render)); // Normal-frame interception, not the tools-only entry.
 Tf2PassRenderFrame(&client,rect);FlushGpu();
 assert(ordinaryFrames==1&&renderCalls==5&&hudWrites==1&&mask==15&&colorStack.empty());
 assert(g_AfxStreams.Begins.size()==5&&g_AfxStreams.Begins==g_AfxStreams.Ends);
 assert((visible[Tf2RenderPass::World]==std::vector<const void*>{&prop}));
 assert((depthDraws[Tf2RenderPass::World]==std::vector<const void*>{&prop}));
 assert((depthDraws[Tf2RenderPass::Players]==std::vector<const void*>{&player,&gear,&vm,&prop}));
 assert((visible[Tf2RenderPass::Players]==std::vector<const void*>{&player,&gear}));
 assert((visible[Tf2RenderPass::Viewmodel]==std::vector<const void*>{&vm}));
 assert(visible[Tf2RenderPass::Hud].empty()&&visible[Tf2RenderPass::Depth].empty());
 Tf2PassRenderFrame(&client,rect);assert(ordinaryFrames==2&&renderCalls==5); // One capture batch per frame.
 globals.Count++;queued=true;visible.clear();Tf2PassRenderFrame(&client,rect);assert(!gpuQueue.empty());FlushGpu();assert(mask==15&&colorStack.empty()&&hudWrites==2);
 assert((visible[Tf2RenderPass::World]==std::vector<const void*>{&prop}));assert((visible[Tf2RenderPass::Players]==std::vector<const void*>{&player,&gear}));assert((visible[Tf2RenderPass::Viewmodel]==std::vector<const void*>{&vm}));
 globals.Count++;g_AfxStreams.Recording=false;Tf2PassRenderFrame(&client,rect);assert(ordinaryFrames==4&&renderCalls==10);
 hiddenPlayer=2;FlushGpu();visible.clear();Tf2PassRenderFrame(&client,rect);FlushGpu();
 assert((visible[Tf2RenderPass::Count]==std::vector<const void*>{&vm,&prop}));assert(g_Tf2RenderGroups.empty());
 hiddenPlayer=0;
 globals.Count++;g_AfxStreams.Recording=true;g_AfxStreams.Alpha=true;g_AfxStreams.Samples.clear();int calls=renderCalls;
 Tf2PassRenderFrame(&client,rect);FlushGpu();assert(renderCalls==calls+5);
 assert((g_AfxStreams.Samples==std::vector<int>{-1,0,0,0,-1}));assert(mask==15&&colorStack.empty());
 // Every subset produces the complement in Base, including native HUD paint.
 for(bool deferred:{false,true})for(unsigned selected=0;selected<16;++selected){
  queued=deferred;g_AfxStreams.Alpha=false;g_AfxStreams.Enabled[0]=true;
  for(unsigned i=1;i<5;++i)g_AfxStreams.Enabled[i]=bool(selected&(1<<(i-1)));
  visible.clear();depthDraws.clear();hudByPass.clear();++globals.Count;
  Tf2PassRenderFrame(&client,rect);FlushGpu();
  std::vector<const void*> expected;
  if(!(selected&1)){expected.push_back(&player);expected.push_back(&gear);}
  if(!(selected&2))expected.push_back(&vm);expected.push_back(&prop);
  assert(visible[Tf2RenderPass::World]==expected);
  assert(depthDraws[Tf2RenderPass::World]==expected); // No invisible-player z silhouettes.
  assert(hudByPass[Tf2RenderPass::World]==((selected&4)?0:1));
  assert((visible[Tf2RenderPass::Count]==std::vector<const void*>{&player,&gear,&vm,&prop})); // Beauty unchanged.
  assert(mask==15&&colorStack.empty());
 }
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
struct Format{bool Alpha=false;};
struct IImageBufferThreadSafe {int Sample=0;Format F;std::atomic<int> Refs{1};void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}Format*GetImageBufferFormat(){return &F;}};
std::atomic<int> Created{0},Frames{0},Destroyed{0},AlphaFrames{0};
template<bool>struct TIOutVideoStream {void Release(){delete this;}bool SupplyImageBuffer(void*,IImageBufferThreadSafe*){++Frames;return true;}};
class COutVideoStreamCreator:public CRefCountedThreadSafe {public:COutVideoStreamCreator(){++Created;}~COutVideoStreamCreator(){++Destroyed;}TIOutVideoStream<true>*CreateOutVideoStream(const Format&){return new TIOutVideoStream<true>;}};
namespace ImageTransformer {IImageBufferThreadSafe*Matte(void*,void*,IImageBufferThreadSafe*b,IImageBufferThreadSafe*w,bool straight){assert(straight&&b->Sample%2==0&&w->Sample==b->Sample+1);auto out=new IImageBufferThreadSafe;out->F.Alpha=true;++AlphaFrames;return out;}IImageBufferThreadSafe*StripAlpha(void*,void*,IImageBufferThreadSafe*b){b->AddRef();return b;}}
}
struct IAfxD3D9CaptureBuffer {int Sample=0;std::atomic<int>Refs{1};advancedfx::IImageBufferThreadSafe*LockCpu(){if(Sample<0)return nullptr;auto b=new advancedfx::IImageBufferThreadSafe;b->Sample=Sample;return b;}void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}};
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
 for(bool failed:{false,true}){
  auto creator=new advancedfx::COutVideoStreamCreator;creator->AddRef();auto output=new CAfxStreams::CDrawingRecordScreenOutput(creator,true);output->AddRef();creator->Release();
  auto sample=[&](int id){if(id==-2){output->OnCapture(nullptr);return;}auto c=new IAfxD3D9CaptureBuffer;c->Sample=id;output->OnCapture(c);c->Release();};
  if(!failed)for(int i=0;i<8;++i)sample(i);
  else {sample(-2);sample(1);sample(2);sample(-2);sample(4);sample(5);sample(6);sample(-1);sample(8);sample(9);sample(10);}
  output->Release();
 }
 assert(advancedfx::Created==10&&advancedfx::Destroyed==10&&advancedfx::Frames==38&&advancedfx::AlphaFrames==6);
 for(int take=0;take<6;++take){
  auto creator=new advancedfx::COutVideoStreamCreator;creator->AddRef();auto output=new CAfxStreams::CDrawingRecordScreenOutput(creator,true);output->AddRef();creator->Release();
  for(int i=0;i<1440;++i){auto c=new IAfxD3D9CaptureBuffer;c->Sample=i;output->OnCapture(c);c->Release();}
  output->Release();
 }
 assert(advancedfx::Created==16&&advancedfx::Destroyed==16&&advancedfx::Frames==4358&&advancedfx::AlphaFrames==4326);
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
shader=(r/'shaders/afx_depth_ps20.fxc').read_text()
axial_shader=shader.split('#elif 3 == AFXDEPTHMODE || 4 == AFXDEPTHMODE')[1].split('\n#endif\n\n\n#endif')[0]+'\n#endif\n'
# Compile the production shader's scalar axial/log conversion with CPU texture samples.
shader_conversion = '#define AFXDEPTHMODE 4\nfloat ConvertDepth(float depth,float g_Afx_zNear,float g_Afx_zFar,float g_Afx_OutNear,float g_Afx_OutFar){\n'+axial_shader+'\nreturn depth;}\n'
capture=r'''
#include <cassert>
#include <iostream>
#include <cstdint>
#include <vector>
#include <cmath>
#include "AfxHookSource/Tf2RenderPass.h"
using DWORD=unsigned;constexpr unsigned D3DCLEAR_TARGET=1,D3DCLEAR_ZBUFFER=2,D3DCLEAR_STENCIL=4;constexpr unsigned D3DFMT_D24S8=75,D3DFMT_D24FS8=83,D3DFMT_D15S1=73;constexpr bool FALSE=false;
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
struct D3DVIEWPORT9{unsigned X=0,Y=0,Width=1280,Height=720;float MinZ=0,MaxZ=1;};
struct D3DSURFACE_DESC{unsigned Width=1280,Height=720,MultiSampleType=0,MultiSampleQuality=0,Format=D3DFMT_D24S8;};
struct Surface{unsigned Samples;int GetDesc(D3DSURFACE_DESC*d){d->MultiSampleType=Samples;return 0;}int Refs=0;unsigned Pixels=0;void AddRef(){++Refs;}void Release(){assert(Refs>0);--Refs;}};Surface original{4},msaa{4},single{0},intz{0},other{8},white{4},playerDepth{4};
using IDirect3DSurface9=Surface;
Surface *color=&original,*depth=&original,*trackedColor=&original,*trackedDepth=&original;
Surface*AfxGetRenderTargetSurface(){return trackedColor;}Surface*AfxGetDepthStencilSurface(){return trackedDepth;}unsigned clears=0,lastClear=0,draws=0,captures=0;bool failWhiteReadback=false;
struct Device{
 D3DVIEWPORT9 Viewport;
 int Clear(unsigned,void*,unsigned,unsigned value,float,unsigned){assert(color&&depth&&color->Samples==depth->Samples);color->Pixels=0;lastClear=value;++clears;return 0;}
 int GetViewport(D3DVIEWPORT9*out){*out=Viewport;return 0;}
 int SetViewport(const D3DVIEWPORT9*in){Viewport=*in;return 0;}
 int GetRenderTarget(unsigned,Surface**out){*out=color;if(color)color->AddRef();return color?0:-1;}
 int GetDepthStencilSurface(Surface**out){*out=depth;if(depth)depth->AddRef();return depth?0:-1;}
 int SetRenderTarget(unsigned,Surface*in){color=in;Viewport.Width=1280;Viewport.Height=720;Viewport.MinZ=0;Viewport.MaxZ=1;return 0;}
 int SetDepthStencilSurface(Surface*in){depth=in;return 0;}
 int CreateDepthStencilSurface(unsigned,unsigned,unsigned,unsigned samples,unsigned,bool,Surface**out,void*){playerDepth.Samples=samples;playerDepth.Refs=1;*out=&playerDepth;return 0;}
}device;
Device*AfxGetDirect3DDevice9(){return &device;}
std::vector<Surface*>targetStack,depthStack;
void AfxD3d9PushDepthStencilEx(Surface*s){depthStack.push_back(trackedDepth);trackedDepth=s;}
void AfxD3d9PopDepthStencil(){trackedDepth=depthStack.back();depthStack.pop_back();}
void AfxD3D9SetPlayerHoldoutEnabled(bool){}
bool AfxD3D9ClearPlayerHoldout(unsigned){return true;}
void AfxD3d9PushRenderTargetEx(Surface*s){targetStack.push_back(trackedColor);trackedColor=s;}
void AfxD3d9PopRenderTarget(bool){trackedColor=targetStack.back();targetStack.pop_back();}
Surface*AfxCreateCompatibleRenderTarget(Surface*s,bool){white.Samples=s->Samples;white.Refs=1;return &white;}
'''+shader_conversion+r'''
constexpr int AfxDrawDepthEncode_Gray=0,AfxDrawDepthMode_PyramidalLogE=4;
void AfxDrawDepth(int,int mode,bool,float outNear,float outFar,unsigned,unsigned,unsigned,unsigned,float zNear,float zFar,void*){
 assert(color->Samples==0&&depth==&intz&&mode==4&&outNear==0&&outFar==4096&&zNear==3&&zFar==28000);
 // The production shader's logarithmic branch must distinguish normal scene distances.
 float previous=-1;
 for(float distance:{3.0f,100.0f,1000.0f,4096.0f}){
  float hardware=(zFar-zFar*zNear/distance)/(zFar-zNear);
  float gray=ConvertDepth(hardware,zNear,zFar,outNear,outFar);
  assert(std::isfinite(gray)&&gray>previous);previous=gray;
 }
}
void Tier0_Warning(const char*,...){}
struct CMaterialSystemFunctor{virtual ~CMaterialSystemFunctor(){};virtual void operator()()=0;};
namespace advancedfx {struct COutVideoStreamCreator {int Refs=1;void AddRef(){++Refs;}void Release(){--Refs;}};}
struct CCaptureInputRenderTarget{};
struct CDrawingRecordScreenOutput{CDrawingRecordScreenOutput(advancedfx::COutVideoStreamCreator*,bool){}};
struct CCaptureNode{
 CCaptureInputRenderTarget *Input;CDrawingRecordScreenOutput *Output;
 CCaptureNode(CCaptureInputRenderTarget*i,CDrawingRecordScreenOutput*o):Input(i),Output(o){}
 void AddRef(){}void Release(){delete Input;delete Output;delete this;}void GpuRelease(){}
 void GpuCaptureSkipped(){}bool GpuCapture(){assert(color==trackedColor&&color->Samples==depth->Samples&&color->Pixels>0);if(failWhiteReadback&&color==&white)return false;++captures;return true;}
};
struct CAfxStreams{
 CCaptureNode*m_Tf2PassNodes[5]={};bool m_Tf2PassActive[5]={},m_Tf2PassPixelsValid[5]={};bool Allocate=true;
 Surface *m_Tf2PassPreviousColor[5]={},*m_Tf2PassPreviousDepth[5]={};D3DVIEWPORT9 m_Tf2PassPreviousViewport[5]={};
 unsigned long long m_Tf2PassAttempts[5]={},m_Tf2PassCaptured[5]={},m_Tf2PassFailed[5]={};bool m_Tf2PassMatte[5]={};Surface*m_Tf2WhiteSurface=nullptr;bool m_Tf2WhiteBound=false;Surface*m_Tf2PlayerDepth=nullptr;bool m_Tf2PassCapturedEarly[5]={};unsigned m_Tf2PassMatteColor[5]={};
 bool DrawingThread_Tf2White(bool);bool DrawingThread_BindTf2PassTarget();void DrawingThread_RestoreTf2PassBindings(Tf2RenderPass pass);
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
  if(g_AfxStreams.m_Tf2PlayerDepth){g_AfxStreams.m_Tf2PlayerDepth->Release();g_AfxStreams.m_Tf2PlayerDepth=nullptr;}
  auto pass=Tf2RenderPass(i);CAfxStreams::CPassCaptureFunctor create(pass,CAfxStreams::CPassCaptureFunctor::Create,&creator);create();
  CAfxStreams::CPassCaptureFunctor begin(pass,CAfxStreams::CPassCaptureFunctor::Begin);begin();
  assert(color==trackedColor&&color->Samples==depth->Samples);++draws;++color->Pixels; // D3D9 rejects incompatible geometry targets.
  if(pass==Tf2RenderPass::Players||pass==Tf2RenderPass::Viewmodel||pass==Tf2RenderPass::Hud)assert(lastClear==0xff00ff00u);else assert(lastClear==0);
  CAfxStreams::CPassCaptureFunctor end(pass,CAfxStreams::CPassCaptureFunctor::End,nullptr,0,3,28000);end();assert(color==prior&&depth==prior&&device.Viewport.Width==640&&device.Viewport.Height==480);
  CAfxStreams::CPassCaptureFunctor destroy(pass,CAfxStreams::CPassCaptureFunctor::Destroy);destroy();
 }
 }
 }
 assert(draws==30&&captures==30&&clears==30&&original.Refs==0&&other.Refs==0);color=depth=&original;
 {
 auto pass=Tf2RenderPass::Players;CAfxStreams::CPassCaptureFunctor create(pass,CAfxStreams::CPassCaptureFunctor::Create,&creator);create();
 CAfxStreams::CPassCaptureFunctor begin(pass,CAfxStreams::CPassCaptureFunctor::Begin,nullptr,0xff0000ffu);begin();assert(lastClear==0xff0000ffu);++color->Pixels;
 CAfxStreams::CPassCaptureFunctor end(pass,CAfxStreams::CPassCaptureFunctor::End,nullptr,0,3,28000);end();CAfxStreams::CPassCaptureFunctor destroy(pass,CAfxStreams::CPassCaptureFunctor::Destroy);destroy();
 }
 {
  auto pass=Tf2RenderPass::Players;CAfxStreams::CPassCaptureFunctor create(pass,CAfxStreams::CPassCaptureFunctor::Create,&creator,0,1,32768,true);create();
  CAfxStreams::CPassCaptureFunctor begin(pass,CAfxStreams::CPassCaptureFunctor::Begin,nullptr,0xff000000u);begin();assert(color==&msaa&&white.Samples==msaa.Samples);++color->Pixels;
  color=&other;assert(!g_AfxStreams.DrawingThread_Tf2White(true)&&targetStack.empty());color=&msaa;device.Viewport.MaxZ=0.1f;assert(g_AfxStreams.DrawingThread_Tf2White(true));assert(color==&white&&device.Viewport.MaxZ==0.1f);++color->Pixels;g_AfxStreams.DrawingThread_Tf2White(false);
  CAfxStreams::CPassCaptureFunctor snapshot(pass,CAfxStreams::CPassCaptureFunctor::Capture);snapshot();assert(g_AfxStreams.m_Tf2PassCapturedEarly[1]&&g_AfxStreams.m_Tf2PassActive[1]);
  CAfxStreams::CPassCaptureFunctor end(pass,CAfxStreams::CPassCaptureFunctor::End);end();assert(!g_AfxStreams.m_Tf2PassActive[1]);assert(g_AfxStreams.m_Tf2PassAttempts[1]==1&&g_AfxStreams.m_Tf2PassCaptured[1]==1&&g_AfxStreams.m_Tf2PassFailed[1]==0);assert(captures==33&&color==&original&&!g_AfxStreams.m_Tf2WhiteBound&&targetStack.empty());
  begin();++color->Pixels;g_AfxStreams.DrawingThread_Tf2White(true);++color->Pixels;g_AfxStreams.DrawingThread_Tf2White(false);failWhiteReadback=true;end();failWhiteReadback=false;
  assert(g_AfxStreams.m_Tf2PassAttempts[1]==2&&g_AfxStreams.m_Tf2PassCaptured[1]==1&&g_AfxStreams.m_Tf2PassFailed[1]==1);
  CAfxStreams::CPassCaptureFunctor destroy(pass,CAfxStreams::CPassCaptureFunctor::Destroy);destroy();g_AfxStreams.m_Tf2WhiteSurface->Release();g_AfxStreams.m_Tf2WhiteSurface=nullptr;
 }
 if(g_AfxStreams.m_Tf2PlayerDepth){g_AfxStreams.m_Tf2PlayerDepth->Release();g_AfxStreams.m_Tf2PlayerDepth=nullptr;}
 g_AfxStreams.Allocate=false;CAfxStreams::CPassCaptureFunctor begin(Tf2RenderPass::World,CAfxStreams::CPassCaptureFunctor::Begin);begin();CAfxStreams::CPassCaptureFunctor end(Tf2RenderPass::World,CAfxStreams::CPassCaptureFunctor::End);end();assert(color==&original&&depth==&original&&captures==34&&g_AfxStreams.m_Tf2PassFailed[0]==1);
 std::cout<<"Actual capture functor keeps MSAA color/depth compatible and chroma mattes green\n";
}
'''
calcs=(r/'AfxHookSource/MirvCalcs.cpp').read_text()
model_category=calcs[calcs.index('static int Mirv_Tf2_ModelCategory('):calcs.index('static bool Mirv_Tf2_LabelDebris(')]
label_category=calcs[calcs.index('static int Mirv_Tf2_LabelCategory('):calcs.index('// Share the tools-classname fallback')]
group_method=calcs[calcs.index('void Mirv_Tf2_RenderGroups('):]
enum_source=(r/'AfxHookSource/MirvCalcs.h').read_text();enum_source=enum_source[enum_source.index('enum MirvTf2EntityCategory_t'):enum_source.index('struct MirvTf2EntityLabel_t')].replace('advancedfx::SceneCache::CategoryCount','9')
groups=r'''
#include <map>
#include <vector>
#include <cstring>
#include <cctype>
#include <cassert>
#include <iostream>
'''+enum_source+r'''
struct CMirvTf2ClientRenderable{const char *Path;const void*GetModel(){return this;}};
struct Info {const char*GetModelName(const void*p){return static_cast<const CMirvTf2ClientRenderable*>(p)->Path;}int GetModelType(const void*){return 3;}}info;
Info*Mirv_Tf2_GetModelInfo(){return &info;}
struct CMirvTf2ClientUnknown{CMirvTf2ClientRenderable Renderable;const char*Name;unsigned Handle,Parent;bool Player=false,Debris=false;CMirvTf2ClientRenderable*GetClientRenderable(){return &Renderable;}void*GetClientNetworkable(){return this;}};
std::vector<CMirvTf2ClientUnknown>objects;
struct Iface {
 void*FirstEntity(){return objects.empty()?nullptr:&objects[0];}
 void*NextEntity(void*p){auto n=static_cast<CMirvTf2ClientUnknown*>(p)+1;return n==objects.data()+objects.size()?nullptr:n;}
 bool IsPlayer(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Player;}
 int GetEntIndex(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Handle;}
 void*GetOwnerEntity(void*p){auto parent=static_cast<CMirvTf2ClientUnknown*>(p)->Parent;for(auto &o:objects)if(o.Handle==parent)return &o;return nullptr;}
}iface;
struct CClientToolsTf2{static CClientToolsTf2*Instance(){static CClientToolsTf2 t;return &t;}Iface*GetClientTools(){return &iface;}};
unsigned Mirv_Tf2_EntityHandle(void*p){return static_cast<CMirvTf2ClientUnknown*>(p)->Handle;}
template<typename T>T SceneInterfaceCall(void*p,int){return static_cast<T>(p);}
void Mirv_Tf2_SceneMoveParent(void*p,const void*,unsigned&out){out=static_cast<CMirvTf2ClientUnknown*>(p)->Parent;}
'''+label_category+model_category+r'''
int Mirv_Tf2_ToolCategory(Iface*,void*p,int,bool player,bool scene){return Mirv_Tf2_LabelCategory(static_cast<CMirvTf2ClientUnknown*>(p)->Name,player,scene);}
bool Mirv_Tf2_LabelDebris(CMirvTf2ClientUnknown*p){return p->Debris;}
'''+group_method+r'''
int main(){
 objects={
  {{"models/player/demo.mdl"},"CTFPlayer",1,0,true},
  {{"models/player/demo.mdl"},"CTFRagdoll",2,0},
  {{"models/player/gibs/demogib006.mdl"},"CDynamicProp",3,0},
  {{"models/unknown.mdl"},"CPhysicsProp",4,0,false,true},
  {{"models/weapons/w_shotgun.mdl"},"CPhysicsProp",5,0},
  {{"models/player/items/demo/hat.mdl"},"CPhysicsProp",6,0,false,true},
  {{"models/generic.mdl"},"CTFWearable",7,0},
  {{"models/v_weapon.mdl"},"CBaseViewModel",8,1},
  {{"models/gear.mdl"},"CGeneric",9,8}, // generic child of a viewmodel stays separate
  {{"models/decals.mdl"},"CDecal",10,0},
  {{"models/props/crate.mdl"},"CPhysicsProp",11,0},
  {{"models/generic.mdl"},"CGeneric",12,2}, // corpse-attached gear
  {{"models/generic.mdl"},"CGeneric",13,14}, // nested gear resolves independently of list order
  {{"models/generic.mdl"},"CGeneric",14,1}
 };
 std::map<const void*,unsigned>result;Mirv_Tf2_RenderGroups(result,0);assert(result.size()==objects.size());
 for(unsigned i=0;i<objects.size();++i){unsigned expected=i==7||i==8?2:i==9||i==10?0:1;assert(result[&objects[i].Renderable]==expected);}
 Mirv_Tf2_RenderGroups(result,2);assert(result[&objects[1].Renderable]==5&&result[&objects[11].Renderable]==5&&result[&objects[0].Renderable]==1);
 Mirv_Tf2_RenderGroups(result,1);assert(result[&objects[0].Renderable]==5&&result[&objects[7].Renderable]==6&&result[&objects[8].Renderable]==6&&result[&objects[12].Renderable]==5);
 // Shared fallback keeps cosmetics classified as cosmetics even when they are debris.
 assert(Mirv_Tf2_ModelCategory(&objects[5].Renderable,false)==MIRV_TF2_ENTITY_COSMETIC);
 std::cout<<"Player pass includes loose weapons/cosmetics/ragdolls/gibs; decals and props remain world; viewmodels stay separate\n";
}
'''
for name,source in [('pov',pov),('passes',render),('writer',writer),('capture',capture),('groups',groups)]:
 file=o/(name+'.cpp');file.write_text(source);cmd=['g++','-std=c++17','-O1','-g','-pthread','-I'+str(r),str(file),'-o',str(o/name)]
 if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 q=subprocess.run(cmd,capture_output=True,text=True)
 if q.returncode:print(q.stderr);raise SystemExit(q.returncode)
 q=subprocess.run([str(o/name)],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True,text=True);print(q.stdout+q.stderr)
 if q.returncode:raise SystemExit(q.returncode)
