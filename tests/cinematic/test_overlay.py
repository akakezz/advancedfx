from pathlib import Path
import subprocess,re,os
import argparse,tempfile
parser=argparse.ArgumentParser(description="Run the actual overlay UI headlessly with Dear ImGui, using fake engine services.")
parser.add_argument("--imgui-dir",type=Path,required=True)
parser.add_argument("--build-dir",type=Path,default=Path(tempfile.gettempdir())/"hlae-paper-ui-tests")
parser.add_argument("--sanitize",action="store_true")
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];imgui=args.imgui_dir.resolve();out=args.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
s=(root/'AfxHookSource/CameraOverlay.cpp').read_text();s=s[re.search(r'namespace AfxHookSource\s*\{\s*namespace CameraOverlay\s*\{',s).start():]
native=(root/'AfxHookSource/MirvCommands.cpp').read_text()
fix_api=native[native.index('// TF2 demo interpolation controls.'):native.index('// End TF2 demo interpolation controls.')]
pov_api=native[native.index('// TF2 POV deathcam control.'):native.index('// End TF2 POV deathcam control.')]
pov_command=native[native.index('// TF2 POV deathcam command.'):native.index('// End TF2 POV deathcam command.')]
fix_command=native[native.index('// TF2 interpolation command.'):native.index('// End TF2 interpolation command.')]
fix_command='void callFix(std::initializer_list<const char*> words){struct Args{std::vector<const char*> V;int ArgC(){return (int)V.size();}const char*ArgV(int i){return V[i];}} data;data.V=words;auto *args=&data;int argc=args->ArgC();const char *cmd1=args->ArgV(1);'+pov_command+fix_command+'}\n'
demo_time=native[native.index('bool GetCurrentDemoTime(double &outDemoTime)'):native.index('bool GetDemoTickFromClientTime(')].replace('GetCurrentDemoTime(', 'ActualGetCurrentDemoTime(',1)
native=native[native.index('// Campath preview uses'):native.index('CON_COMMAND(mirv_campath,')]
view=(root/'AfxHookSource/RenderView.cpp').read_text();view=view[view.index('case Override_Input:'):view.index('case Override_Aim:')];view=view.replace('case Override_Input:','').replace('break;','')
view='void applyPreviewView(float&Tx,float&Ty,float&Tz,float&Rx,float&Ry,float&Rz,float&Fov){auto*m_MirvInput=&input;float m_LastFrameTime=0.016f;bool originOrAnglesOverriden=false;auto&m_CamPath=g_Hook_VClient_RenderView.m_CamPath;bool collisionFree=false;const void*attachedIdentity=nullptr;float collisionInputX=Tx,collisionInputY=Ty,collisionInputZ=Tz;const CamPath*collisionPath=nullptr;double collisionTime=0;'+view+'}\n'
preview_api=(root/'AfxHookSource/RenderView.h').read_text().split('// Shared campath preview controls')[1].split('// Hook_VClient_RenderView')[0]
preview_api='// Shared campath preview controls'+preview_api
preamble=r'''
#include "shared/CamPath.h"
#include "AfxHookSource/CameraOverlay.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "shared/overlay/third_party/imgui_filebrowser/imfilebrowser.h"
#include "shared/overlay/third_party/imgui_neo_sequencer/imgui_neo_sequencer.h"
#include "shared/overlay/third_party/imguizmo/ImGuizmo.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdarg>
#include <set>
#include <cstddef>
#define __thiscall
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cmath>
#include <chrono>
#include <limits>
#include <filesystem>
#include <map>
#include <bitset>
#include <tuple>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#define MAX_PATH 260
#define CP_UTF8 65001
#define SW_SHOWNORMAL 1
const char*GetHlaeFolder(){return ".";}
int GetWindowsDirectoryA(char*,int){return 0;}
int MultiByteToWideChar(int,int,const char*,int,wchar_t*,int){return 0;}
void* ShellExecuteW(void*,const wchar_t*,const wchar_t*,void*,void*,int){return nullptr;}
template<size_t N,class... Args>int sprintf_s(char(&b)[N],const char*f,Args...a){return snprintf(b,N,f,a...);}
template<size_t N>void strncpy_s(char(&b)[N],const char*s,size_t){snprintf(b,N,"%s",s);}
namespace advancedfx {void Message(const char*,...){}void Warning(const char*,...){} }
struct MirvInput{int MovementClears=0;void ClearMovementInput(){++MovementClears;}void ResetCameraSmoothing(){}int CollisionAdoptions=0;void AdoptCollisionPosition(float x,float y,float z){++CollisionAdoptions;Pose[0]=x;Pose[1]=y;Pose[2]=z;}bool Override(float,float&x,float&y,float&z,float&a,float&b,float&c,float&f){x=y=z=a=b=c=f=-999;return true;}bool Camera=false;float Pose[7]={};double Sens=1;bool GetCameraControlMode(){return Camera;}void SetCameraControlMode(bool b){Camera=b;}void SetTx(float x){Pose[0]=x;}void SetTy(float x){Pose[1]=x;}void SetTz(float x){Pose[2]=x;}void SetRx(float x){Pose[3]=x;}void SetRy(float x){Pose[4]=x;}void SetRz(float x){Pose[5]=x;}void SetFov(float x){Pose[6]=x;}double GetKeyboardSensitivty(){return Sens;}void SetKeyboardSensitivity(double x){Sens=x;}} input;
struct WrpGlobals{double interval_per_tick_get(){return 1.0/66;}double interpolation_amount_get(){return 0;}} globals;
struct FakeView{CamPath m_CamPath;CamPathCollection m_CamPaths{m_CamPath};MirvInput*m_MirvInput=&input;double LastCameraOrigin[3]={100,-100,50},LastCameraAngles[3]={0,135,0},LastCameraFov=130;int LastWidth=1600,LastHeight=1000;bool Menu=false,GlobalsAvailable=true;bool IsInstalled(){return true;}bool IsMenuActive(){return Menu;}bool IsSourceUiActive(){return Menu;}WrpGlobals*GetGlobals(){return GlobalsAvailable?&globals:nullptr;}}g_Hook_VClient_RenderView;
struct CCampathDrawer{bool Names=true,Holds=true,Durations=true;bool GetDrawNames(){return Names;}bool GetDrawHolds(){return Holds;}bool GetDrawDurations(){return Durations;}void SetDrawNames(bool v){Names=v;}void SetDrawHolds(bool v){Holds=v;}void SetDrawDurations(bool v){Durations=v;}bool Draw=false;float Index=18;bool Draw_get(){return Draw;}void Draw_set(bool b){Draw=b;}float GetDrawKeyframeIndex(){return Index;}void SetDrawKeyframeIndex(float x){Index=x;}}g_CampathDrawer;
struct Clock{double Time=10;double GetTime(){return Time;}int GetFrameCount(){return int(Time*100);}}g_MirvTime;
std::vector<std::string> commands;
struct DemoInfo{int TotalTicks=12000;int GetDemoPlaybackTick(){return int(g_MirvTime.Time*66);}int GetDemoPlaybackTotalTicks(){return TotalTicks;}float GetDemoPlaybackTimeScale(){return 1;}}demo;
using WrpVEngineClientDemoInfoEx=DemoInfo;
struct Engine{bool Playing=true,Hltv=true,HaveInfo=true,Loading=false,InGame=true;bool IsInGame(){return InGame;}int Local=1;int GetLocalPlayer(){return Local;}bool IsPlayingDemo(){return Playing;}bool IsDrawingLoadingImage(){return Loading;}bool IsHLTV(){return Hltv;}DemoInfo*GetDemoInfoEx(){return HaveInfo?&demo:nullptr;}void ClientCmd_Unrestricted(const char*c){commands.emplace_back(c);}}engine;Engine*g_VEngineClient=&engine;
bool GetCurrentDemoTick(int&n){n=int(g_MirvTime.Time*66);return true;}bool GetCurrentDemoTime(double&t){t=g_MirvTime.Time;return true;}bool GetDemoTickFromClientTime(double,double t,int&n){n=int(t*66);return true;}
int Mirv_Tf2_GetMaxPlayers(){return 1;}bool Mirv_Tf2_GetPlayerInfo(int i,char*b,int n,int*u){snprintf(b,n,"Scout");*u=42;return i==1;}
constexpr int MIRV_TF2_MAX_BONES=256;
struct MirvTf2BoneName_t{char name[64];int parent;};
int Mirv_Tf2_GetBoneNames(int entity,MirvTf2BoneName_t*out){if(entity!=1)return 0;snprintf(out[0].name,64,"root");snprintf(out[1].name,64,"bip_head");return 2;}
int MirvCam_GetAttachMode(){return 2;}
int tracedEntity=1;bool traceAvailable=true;
bool Mirv_Tf2_TraceEntity(const double*,const double*,int&out){out=tracedEntity;return traceAvailable;}
using LONG=long;using PVOID=void*;constexpr LONG NO_ERROR=0;LONG povHookError=0;int povHookCommits=0;
void*GetCurrentThread(){return nullptr;}LONG DetourTransactionBegin(){return povHookError;}LONG DetourUpdateThread(void*){return 0;}LONG DetourAttach(PVOID*,PVOID){return 0;}LONG DetourTransactionAbort(){return 0;}LONG DetourTransactionCommit(){++povHookCommits;return 0;}
using UINT=unsigned int;using DWORD=unsigned long;using WPARAM=uintptr_t;using LPARAM=intptr_t;
constexpr UINT WM_KEYDOWN=0x100,WM_KEYUP=0x101,WM_SYSKEYDOWN=0x104,WM_SYSKEYUP=0x105;
constexpr UINT VK_DELETE=0x2E,VK_F1=0x70,VK_INSERT=0x2D,VK_HOME=0x24,VK_END=0x23,VK_PRIOR=0x21,VK_NEXT=0x22,VK_LEFT=0x25,VK_UP=0x26,VK_RIGHT=0x27,VK_DOWN=0x28,VK_CONTROL=0x11,VK_SHIFT=0x10,VK_RCONTROL=0xA3;
bool controlHeld=false,shiftHeld=false;
short GetKeyState(UINT key){return ((key==VK_CONTROL&&controlHeld)||(key==VK_SHIFT&&shiftHeld))?short(0x8000):0;}
DWORD GetTickCount(){static DWORD tick=0;return tick+=16;}
struct PreviewClock{bool GetDemoTickFromClientTime(double,double t,int&tick){return ::GetDemoTickFromClientTime(0,t,tick);}}g_MirvCampath_Time;
#define _stricmp strcasecmp
std::string fixMessages;
void FixMessage(const char*f,va_list a){char b[2048];vsnprintf(b,sizeof(b),f,a);fixMessages+=b;}
void Tier0_Msg(const char*f,...){va_list a;va_start(a,f);FixMessage(f,a);va_end(a);}
void Tier0_Warning(const char*f,...){va_list a;va_start(a,f);FixMessage(f,a);va_end(a);}
enum{SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2};int g_SourceSdkVer=SourceSdkVer_TF2;
struct FakeConVar{float Value=0,Maximum=-1;bool Writable=true;float GetFloat(){return Value;}void SetValue(float value){if(Writable)Value=Maximum<0?value:(std::min)(Maximum,value);}};
struct FakeCvar{std::map<std::string,FakeConVar> Vars{{"cl_interpolate",{1}},{"cl_interp",{.015f}},{"cl_interp_ratio",{1}}};FakeConVar *FindVar(const char*name){auto it=Vars.find(name);return it==Vars.end()?nullptr:&it->second;}}fakeCvar;
namespace SOURCESDK{namespace CSS{FakeCvar*g_pCVar=&fakeCvar;}namespace L4D2{FakeCvar*g_pCVar=nullptr;}}
namespace AfxHookSource{namespace Gui{bool SetOverlayOpen(bool b){CameraOverlay::SetOpen(b);return true;}}namespace CameraOverlay{namespace Recording{const char*GetName(){return "test";}void SetName(const char*){}bool ScreenEnabled(){return true;}bool IsActive(){return false;}}}}
struct Rect{ImVec2 Min,Max;};std::map<std::string,Rect>widgets;
bool trackDouble(const char*l,double*v,double step=0,double fast=0,const char*format="%.6f",ImGuiInputTextFlags flags=0){bool b=ImGui::InputDouble(l,v,step,fast,format,flags);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackHeader(const char*l,ImGuiTreeNodeFlags flags=0){bool b=ImGui::CollapsingHeader(l,flags);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackCheckbox(const char*l,bool*v){bool b=ImGui::Checkbox(l,v);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackButton(const char*l,const ImVec2&s=ImVec2(0,0)){bool b=ImGui::Button(l,s);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackTab(const char*l,bool*p=nullptr,ImGuiTabItemFlags f=0){bool b=ImGui::BeginTabItem(l,p,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackInvisible(const char*l,const ImVec2&s,ImGuiButtonFlags f=0){bool b=ImGui::InvisibleButton(l,s,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
std::map<int,int> sequencerLabels;void TestRecordNeoLabel(int tick,int index){sequencerLabels[tick]=index;}
namespace ImGui{void TestSetNeoView(float zoom,int offset);}
int seqEnd=0,viewStart=0,viewEnd=0,timelineSelected=0;Rect seqRect,graphRect;
struct Span{int Begin,End;ImU32 Color;std::string Caption;};std::vector<Span> spans;
bool trackSpan(int begin,int end,ImU32 color,const char*caption=nullptr){spans.push_back({begin,end,color,caption?caption:""});return ImGui::NeoTimelineSpan(begin,end,color,caption);}

bool trackSequencer(const char*id,int32_t*frame,int32_t*start,int32_t*end,const ImVec2&size,ImGuiNeoSequencerFlags flags){seqEnd=*end;seqRect={ImGui::GetCursorScreenPos(),ImGui::GetCursorScreenPos()+size};bool b=ImGui::BeginNeoSequencer(id,frame,start,end,size,flags);if(b)ImGui::GetNeoSequencerView(&viewStart,&viewEnd);return b;}
bool trackSelectable(const char*l,bool selected=false,ImGuiSelectableFlags flags=0,const ImVec2&size=ImVec2(0,0)){bool b=ImGui::Selectable(l,selected,flags,size);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
void trackKeyframe(int32_t*value){ImGui::NeoKeyframe(value);timelineSelected+=ImGui::IsNeoKeyframeSelected();}
bool trackTimeline(const char*l,bool*open=nullptr,ImGuiNeoTimelineFlags flags=0){bool b=ImGui::BeginNeoTimelineEx(l,open,flags);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackArrayCombo(const char*l,int*v,const char*const items[],int count,int height=-1){bool b=ImGui::Combo(l,v,items,count,height);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackSlider(const char*l,float*v,float min,float max,const char*format,ImGuiSliderFlags flags=0){bool b=ImGui::SliderFloat(l,v,min,max,format,flags);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackCombo(const char*l,const char*p,ImGuiComboFlags f=0){bool b=ImGui::BeginCombo(l,p,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
'''
s=s.replace('ImGui::InputDouble(', 'trackDouble(').replace('ImGui::Checkbox(', 'trackCheckbox(').replace('ImGui::Button(', 'trackButton(').replace('ImGui::SmallButton(', 'trackButton(').replace('ImGui::BeginTabItem(', 'trackTab(').replace('ImGui::BeginNeoSequencer(', 'trackSequencer(').replace('ImGui::Selectable(', 'trackSelectable(').replace('ImGui::BeginCombo(', 'trackCombo(').replace('ImGui::InvisibleButton(', 'trackInvisible(').replace('ImGui::Combo(', 'trackArrayCombo(').replace('ImGui::SliderFloat(', 'trackSlider(').replace('ImGui::BeginNeoTimelineEx(', 'trackTimeline(').replace('ImGui::NeoKeyframe(', 'trackKeyframe(')
s=s.replace('ImGui::NeoTimelineSpan(', 'trackSpan(')
s=s.replace('ImGui::CollapsingHeader(', 'trackHeader(')
# Keep Windows' min/max macros active around the field editor, even on non-Windows hosts.
s=s.replace('static bool ModifierField(', '#define min(a,b) (((a)<(b))?(a):(b))\n#define max(a,b) (((a)>(b))?(a):(b))\nstatic bool ModifierField(')
s=s.replace('static void ApplyModifierFields(', '#undef min\n#undef max\nstatic void ApplyModifierFields(')

s=s.replace('struct CurveSamples\n{', 'static size_t curveSampleBuilds=0;\nstruct CurveSamples\n{').replace('\t\tRevision = path.GetGeometryRevision();', '\t\t++curveSampleBuilds; Revision = path.GetGeometryRevision();',1)
# Count actual recovery I/O in the real editor source, without production instrumentation.
s=s.replace('static bool g_Autosave = true;', 'static size_t recoveryWrites=0, recoveryRemoves=0;\nstatic bool g_Autosave = true;')
s=s.replace('std::filesystem::remove(g_RecoveryFile, error);', '++recoveryRemoves; std::filesystem::remove(g_RecoveryFile, error);')
s=s.replace('!paths.SaveAll(g_RecoveryFile.wstring().c_str(), true)', '!(++recoveryWrites, paths.SaveAll(g_RecoveryFile.wstring().c_str(), true))')
s=s.replace('static void DrawSpeedProfile(', 'static std::vector<double> drawnSpeedKeyProgress;\nstatic void DrawSpeedProfile(').replace('const auto keyProgress = path.GetSpeedKeyProgress();', 'const auto keyProgress = path.GetSpeedKeyProgress(); drawnSpeedKeyProgress=keyProgress;')
s=s.replace('// Background + border', 'graphRect = {p0,p1};\n// Background + border')
drawer=(root/'AfxHookSource/CampathDrawer.cpp').read_text()
layout=drawer[drawer.index('// World-position-aware label slots'):drawer.index('// CCampathDrawer //////////////////')]
# Exercise the actual CPU/render queue with a fake D3D device that rejects producer-thread calls.
collision_header=(root/'AfxHookSource/MirvCalcs.h').read_text()
collision_header=collision_header[collision_header.index('// Optional TF2 camera collision.'):collision_header.index('// End optional TF2 camera collision.')]
collision_source=(root/'AfxHookSource/MirvCalcs.cpp').read_text()
collision_abi=collision_source[collision_source.index('struct alignas(16) MirvTf2Ray_t'):collision_source.index('bool Mirv_Tf2_TraceEntity(')]
collision_core=collision_source[collision_source.index('// Optional camera collision implementation.'):collision_source.index('// End optional camera collision implementation.')]
collision_command=(root/'AfxHookSource/MirvCam.cpp').read_text()
collision_command=collision_command[collision_command.index('// Camera collision console command.'):collision_command.index('// End camera collision console command.')]
collision_stub=r"""
#include <mutex>
std::vector<std::pair<void*,const char*>> collisionClasses;
namespace SOURCESDK { namespace TF2 {using HTOOLHANDLE=size_t;constexpr HTOOLHANDLE HTOOLHANDLE_INVALID=0;struct IClientTools {
 void*GetLocalPlayer(){return (void*)1;}bool IsPlayer(void*p){return p==(void*)1||p==(void*)2;}
 int GetEntIndex(void*p){return int(size_t(p));}HTOOLHANDLE GetToolHandleForEntityByIndex(int i){return i;}
 HTOOLHANDLE AttachToEntity(void*p){return size_t(p);}void DetachFromEntity(void*){}
 const char*GetClassname(HTOOLHANDLE h){for(auto v:collisionClasses)if(size_t(v.first)==h)return v.second;return "prop_dynamic";}
};}};
bool collisionToolsAvailable=true,collisionFactoryAvailable=true;
struct CClientToolsTf2{static CClientToolsTf2*Instance(){static CClientToolsTf2 t;return collisionToolsAvailable?&t:nullptr;}SOURCESDK::TF2::IClientTools*GetClientTools(){static SOURCESDK::TF2::IClientTools t;return &t;}};
void*CollisionFactory(const char*,int*);
void*(*g_AppSystemFactory)(const char*,int*)=CollisionFactory;
struct IWrpCommandArgs{virtual int ArgC()=0;virtual const char*ArgV(int)=0;};
"""
collision_fake=r"""
struct CollisionBox {double Min[3],Max[3];void*Entity=nullptr;};
struct CollisionFakeTrace:CMirvTf2EngineTrace{
 std::vector<CollisionBox>Boxes;int Queries=0;unsigned Mask=0;bool IsRay=true;float Radius=0;
 void _UNUSED_GetPointContents()override{}void _UNUSED_GetPointContents_Collideable()override{}
 void ClipRayToEntity(const MirvTf2Ray_t&r,unsigned mask,void*entity,MirvTf2Trace_t*out)override{
  struct Only:CMirvTf2TraceFilter{void*Entity;Only(void*p):CMirvTf2TraceFilter(nullptr),Entity(p){}bool ShouldHitEntity(void*p,int)override{return p==Entity;}}only(entity);
  auto boxes=Boxes;Boxes.erase(std::remove_if(Boxes.begin(),Boxes.end(),[&](const CollisionBox&b){return b.Entity!=entity;}),Boxes.end());
  TraceRay(r,mask,&only,out);Boxes=boxes;
 }
 void _UNUSED_ClipRayToCollideable()override{}
 void TraceRay(const MirvTf2Ray_t&r,unsigned mask,CMirvTf2TraceFilter*f,MirvTf2Trace_t*out)override{
  ++Queries;Mask=mask;IsRay=r.isRay;Radius=r.extents[0];*out={};out->fraction=1;
  for(const auto&box:Boxes){if(box.Entity&&!f->ShouldHitEntity(box.Entity,mask))continue;
   double lo[3],hi[3];bool inside=true,endInside=true;
   for(int a=0;a<3;++a){lo[a]=box.Min[a]-r.extents[a];hi[a]=box.Max[a]+r.extents[a];inside&=r.start[a]>lo[a]+1e-6&&r.start[a]<hi[a]-1e-6;endInside&=r.start[a]+r.delta[a]>lo[a]&&r.start[a]+r.delta[a]<hi[a];}
   if(inside){out->startSolid=true;out->allSolid=endInside;out->fraction=0;continue;}
   double enter=0,leave=1;int axis=-1;double sign=0;bool hit=true;
   for(int a=0;a<3;++a){if(std::abs(r.delta[a])<1e-12){if(r.start[a]<=lo[a]||r.start[a]>=hi[a])hit=false;continue;}
    double t0=(lo[a]-r.start[a])/r.delta[a],t1=(hi[a]-r.start[a])/r.delta[a];double n=-1;if(t0>t1){std::swap(t0,t1);n=1;}
    if(t0>=enter){enter=t0;axis=a;sign=n;}leave=(std::min)(leave,t1);if(enter>leave)hit=false;}
   if(hit&&axis>=0&&enter>=0&&enter<=out->fraction&&leave>=0){out->fraction=(float)enter;out->plane={};out->plane.normal[axis]=(float)sign;out->entity=box.Entity;}
  }
 }
}collisionTrace;
void*CollisionFactory(const char*name,int*){return collisionFactoryAvailable&&!strcmp(name,"EngineTraceClient003")?&collisionTrace:nullptr;}
void collisionCommand(std::initializer_list<const char*>v){struct Args:IWrpCommandArgs{std::vector<const char*>V;int ArgC()override{return V.size();}const char*ArgV(int i)override{return V[i];}}a;a.V=v;if(a.V.size()>1&&!strcmp(a.V[1],"attach"))MirvCameraAttachFollowCommand(&a);else MirvCameraCollisionCommand(&a);}
"""
collision_code=collision_header+collision_stub+collision_abi+collision_core+collision_command+collision_fake
collision_stage=(root/'AfxHookSource/RenderView.cpp').read_text()
collision_stage=collision_stage[collision_stage.index('// Collision correction is the last position stage'):collision_stage.index('// End collision correction stage.')]
collision_code+='void CollisionView(bool collisionFree,const CamPath*collisionPath,double curTime,double collisionTime,const double*LastCameraOrigin,float&Tx,float&Ty,float&Tz,const double*inputOrigin=nullptr){auto*m_MirvInput=&input;float collisionInputX=inputOrigin?inputOrigin[0]:Tx,collisionInputY=inputOrigin?inputOrigin[1]:Ty,collisionInputZ=inputOrigin?inputOrigin[2]:Tz;'+collision_stage+'}\n'

attached_view_prefix=r"""
void AttachedView(const void*identity,void*entity,const double*pivot,const double*root,double time,int mode,bool useAim,const double*aimFrom,const double*target,float&Tx,float&Ty,float&Tz,float outAngles[3],const float*cachedAimAngles=nullptr,bool softAim=false){
 const void*attachedIdentity=identity;void*attachedEntity=entity;const double*attachedPivot=pivot;const double*attachedRoot=root;
 const int MIRV_TF2_BONEMODE_WORLD=3;int attachedMode=mode;const void*aimAttachment=useAim?identity:nullptr;const void*aimAngleAttachment=(useAim||softAim)?identity:nullptr;
 const double*aimCameraOrigin=aimFrom;float aimCameraAngles[3];for(int i=0;i<3;++i)aimCameraAngles[i]=cachedAimAngles?cachedAimAngles[i]:outAngles[i];
 struct Flags{bool m_SourceUseXRotation=true,m_SourceUseYRotation=true,m_SourceUseZRotation=true;}g_MirvCam;
 struct Aim{Afx::Math::Vector3 target;const Afx::Math::Vector3&GetLastTargetOrigin()const{return target;}}g_Aiming{{target[0],target[1],target[2]}};
 struct Time{double time;double GetCurTime(){return time;}}g_MirvTime{time};
 float&Rx=outAngles[0];float&Ry=outAngles[1];float&Rz=outAngles[2];
"""
attached_stage=(root/'AfxHookSource/RenderView.cpp').read_text()
attached_stage=attached_stage[attached_stage.index('// Attached camera follow and framing.'):attached_stage.index('// End attached camera follow and framing.')]
collision_code+=attached_view_prefix+attached_stage+'}\n'


renderer=(root/'AfxHookSource/Gui.cpp').read_text()
renderer=renderer[renderer.index('class CImGuiRenderCacheManager'):renderer.index('} m_ImGuiRernderCacheManager;')+1]+';'
constants=sorted(set(re.findall(r'\bD3D[A-Z_0-9]+\b',renderer))-{'D3DVIEWPORT9','D3DMATRIX'})
render_preamble=r"""
#include <thread>
#include <functional>
#include <memory>
#include <mutex>
#include <list>
namespace RenderCacheTest {
using DWORD=unsigned int;using LONG=long;using D3DCOLOR=ImU32;
struct RECT{LONG left,top,right,bottom;};struct D3DMATRIX{float m[4][4];};struct D3DVIEWPORT9{DWORD X,Y,Width,Height;float MinZ,MaxZ;};
struct CUSTOMVERTEX{float pos[3];D3DCOLOR col;float uv[2];};
static constexpr int TRUE=1,FALSE=0;
std::thread::id renderThread;std::atomic<int> wrongThread{0},calls{0};
void gpu(){++calls;if(std::this_thread::get_id()!=renderThread)++wrongThread;}
struct Buffer{std::vector<unsigned char> Bytes;bool Locked=false;bool *Fail=nullptr;Buffer(size_t n,bool*f):Bytes(n),Fail(f){}int Lock(unsigned,unsigned,void**out,int){gpu();if(Fail&&*Fail){*Fail=false;return -1;}Locked=true;*out=Bytes.data();return 0;}void Unlock(){gpu();Locked=false;}void Release(){gpu();delete this;}};
using IDirect3DVertexBuffer9=Buffer;using IDirect3DIndexBuffer9=Buffer;using LPDIRECT3DVERTEXBUFFER9=Buffer*;using LPDIRECT3DINDEXBUFFER9=Buffer*;
struct IDirect3DSurface9{int References=1,Writes=0;void Release(){gpu();--References;}};
struct IDirect3DStateBlock9{std::function<int()>Save,Restore;int Capture(){gpu();return Save();}int Apply(){gpu();return Restore();}void Release(){gpu();delete this;}};
struct IDirect3DDevice9{
 bool FailVertex=false,FailIndex=false,FailCapture=false,FailTransform=false,FailTarget=false;int Draws=0,Frames=0;float FirstX=0;Buffer*VB=nullptr,*IB=nullptr;
 using StateMap=std::map<std::pair<int,int>,uintptr_t>;StateMap States;std::map<int,D3DMATRIX>Transforms;
 IDirect3DSurface9 Back,Offscreen;IDirect3DSurface9*Target=&Back;
 int GetRenderTarget(int,IDirect3DSurface9**out){gpu();if(FailTarget)return -1;*out=Target;++Target->References;return 0;}
 int GetBackBuffer(int,int,int,IDirect3DSurface9**out){gpu();*out=&Back;++Back.References;return 0;}
 int CreateVertexBuffer(unsigned n,int,int,int,Buffer**out,void*){gpu();*out=new Buffer(n,&FailVertex);return 0;}
 int CreateIndexBuffer(unsigned n,int,int,int,Buffer**out,void*){gpu();*out=new Buffer(n,&FailIndex);return 0;}
 int CreateStateBlock(int,IDirect3DStateBlock9**out){gpu();++Frames;auto saved=std::make_shared<StateMap>();
  *out=new IDirect3DStateBlock9{[this,saved]{if(FailCapture)return -1;*saved=States;return 0;},[this,saved]{States=*saved;return 0;}};return 0;}
 void SetStreamSource(int,Buffer*v,int,unsigned){gpu();VB=v;States[{1,0}]=(uintptr_t)v;}void SetIndices(Buffer*v){gpu();IB=v;States[{1,1}]=(uintptr_t)v;}
 void DrawIndexedPrimitive(int,int,int,unsigned,int,unsigned){gpu();++Draws;++Target->Writes;FirstX=reinterpret_cast<CUSTOMVERTEX*>(VB->Bytes.data())->pos[0];}
 void SetFVF(int v){gpu();States[{1,2}]=v;}void SetViewport(D3DVIEWPORT9*v){gpu();States[{1,3}]=v->Width;}
 void SetRenderState(int k,int v){gpu();States[{2,k}]=v;}void SetPixelShader(void*v){gpu();States[{3,0}]=(uintptr_t)v;}
 void SetVertexShader(void*v){gpu();States[{3,1}]=(uintptr_t)v;}void SetTextureStageState(int stage,int k,int v){gpu();States[{4,stage*1000+k}]=v;}
 void SetSamplerState(int stage,int k,int v){gpu();States[{5,stage*1000+k}]=v;}
 void SetTransform(int k,const D3DMATRIX*m){gpu();Transforms[k]=*m;}
 int GetTransform(int k,D3DMATRIX*m){gpu();if(FailTransform)return -1;*m=Transforms[k];return 0;}
 void SetTexture(int k,void*v){gpu();States[{6,k}]=(uintptr_t)v;}void SetScissorRect(const RECT*v){gpu();States[{7,0}]=v->left;}
};
static void*g_FontTexture=nullptr;
"""
render_code=render_preamble+'\n'.join('constexpr int '+name+'='+str(i+1)+';' for i,name in enumerate(constants))+'\n'+renderer+'\n}\n'
# Real ownership/cursor hooks, with fake Win32 state. Hot callbacks must not query Source.
gui_source=(root/'AfxHookSource/Gui.cpp').read_text()
a=gui_source.index('bool g_OverlayWindowFocused')
owner=gui_source[a:gui_source.index('} // namespace',a)]
a=gui_source.index('bool OnSetCursorPos(')
hooks=gui_source[a:gui_source.index('bool GetWorldGlyph',a)]
hooks=re.sub(r'\b__(?:in_opt|in|out)\b','',hooks)
a=gui_source.index('// WM_SETCURSOR is a hot/reentrant')
message=gui_source[a:gui_source.index('if (msg == WM_KILLFOCUS',a)]
cursor_preamble=r"""
namespace CursorInputTest {
using HCURSOR=void*;using HWND=void*;using BOOL=bool;
struct POINT{int x=0,y=0;};using LPPOINT=POINT*;struct RECT{int left=0,top=0,right=1600,bottom=1000;};
constexpr bool TRUE=true,FALSE=false;constexpr UINT WM_SETCURSOR=0x20,HTCLIENT=1;
unsigned LOWORD(LPARAM v){return unsigned(v)&65535;}
bool Supported=true;bool IsSupported(){return Supported;}
namespace CameraOverlay{bool Open=true,Look=false;int Finishes=0;bool IsOpen(){return Open;}bool IsMouseLook(){return Look;}void FinishEdit(){++Finishes;}}
struct Input{int Clears=0;void ClearMovementInput(){++Clears;}}cursorInput;
struct View{bool Menu=false,Visible=false;int Queries=0;Input*m_MirvInput=&cursorInput;void UiCursorStatus(bool&a,bool&v){++Queries;a=true;v=Visible;}bool IsSourceUiActive(){++Queries;return Menu;}}g_Hook_VClient_RenderView;
int Releases=0;void MirvCampathPreview_ReleaseKeys(){++Releases;}
HWND g_hWnd=reinterpret_cast<HWND>(1);void*g_FontTexture=reinterpret_cast<void*>(1);
HCURSOR Hardware=reinterpret_cast<HCURSOR>(2),m_GameCursor=nullptr;HWND Capture=g_hWnd;
POINT Physical{200,250},m_GameCursorPos;RECT Clip;
int Display=-1,OsCalls=0,CursorSets=0;
bool GetCursorPos(POINT*p){++OsCalls;*p=Physical;return true;}HCURSOR GetCursor(){++OsCalls;return Hardware;}
HCURSOR SetCursor(HCURSOR c){++OsCalls;++CursorSets;auto old=Hardware;Hardware=c;return old;}
HWND GetCapture(){++OsCalls;return Capture;}HWND SetCapture(HWND c){++OsCalls;auto old=Capture;Capture=c;return old;}
bool ReleaseCapture(){++OsCalls;Capture=nullptr;return true;}
bool GetClipCursor(RECT*r){++OsCalls;*r=Clip;return true;}bool ClipCursor(const RECT*r){++OsCalls;Clip=r?*r:RECT{};return true;}
int ShowCursor(bool show){++OsCalls;return Display+=show?1:-1;}
bool SetCursorPos(int x,int y){++OsCalls;Physical={x,y};return true;}bool ScreenToClient(HWND,POINT*){++OsCalls;return true;}
"""
cursor_code=cursor_preamble+owner+hooks+'bool CursorMessage(UINT msg, LPARAM lParam){'+message+'return false;}\n}\n'
test=r'''
int checks=0;void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
void frame(ImVec2 mouse=ImVec2(-100,-100),bool down=false,float wheel=0,bool middle=false,bool right=false){auto&io=ImGui::GetIO();io.DeltaTime=1.0f/60;io.MousePos=mouse;io.MouseDown[0]=down;io.MouseDown[2]=middle;io.MouseDown[1]=right;io.MouseWheel=wheel;widgets.clear();spans.clear();sequencerLabels.clear();timelineSelected=0;ImGui::NewFrame();AfxHookSource::CameraOverlay::Draw();ImGui::Render();check(ImGui::GetDrawData()!=nullptr,"UI produces draw data");}
void button(const char*l){auto r=widgets.at(l);ImVec2 p((r.Min.x+r.Max.x)/2,(r.Min.y+r.Max.y)/2);frame(p,false);frame(p,true);frame(p,false);frame();}
Tf2FixClientClass *povClasses=nullptr;
Tf2FixClientClass *PovGetClasses(void*){return povClasses;}
int povProxyCalls=0,povObserved=-1;const Tf2FixProxyData*povSeenData=nullptr;void*povSeenObject=nullptr,*povSeenOut=nullptr;
void PovOriginal(const Tf2FixProxyData*d,void*object,void*out){++povProxyCalls;povObserved=d?d->Value.Int:-1;povSeenData=d;povSeenObject=object;povSeenOut=out;}
int main(){try{ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1600,1000);unsigned char*pixels;int w,h;io.Fonts->AddFontDefault();io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 using namespace AfxHookSource::CameraOverlay;g_Autosave=false;
 {
 using namespace CursorInputTest;
 auto&io=ImGui::GetIO();const bool originalDraw=io.MouseDrawCursor;
 UpdateGraphInputOwnership();check(m_GraphInputOwned&&io.MouseDrawCursor&&Hardware==nullptr,"Overlay uses ImGui's cursor and hides the OS cursor once");
 auto os=OsCalls,sets=CursorSets,queries=CursorInputTest::g_Hook_VClient_RenderView.Queries;bool virtualCorrect=true;
 auto arrow=reinterpret_cast<HCURSOR>(3),ibeam=reinterpret_cast<HCURSOR>(4);
 for(int i=0;i<2000;++i){HCURSOR previous=nullptr,current=nullptr;POINT point;int count;
  virtualCorrect&=OnSetCursor(i%2?ibeam:arrow,previous)&&OnGetCursor(current)&&current==(i%2?ibeam:arrow);
  virtualCorrect&=OnSetCursorPos(i,i+1)&&OnGetCursorPos(&point)&&point.x==i&&point.y==i+1;
  virtualCorrect&=OnShowCursor(true,count)&&OnShowCursor(false,count);
 }
 check(virtualCorrect&&OsCalls==os&&CursorInputTest::g_Hook_VClient_RenderView.Queries==queries,"Busy cursor hooks preserve game state without OS calls or re-entering Source");
 for(int i=0;i<2000;++i)virtualCorrect&=CursorMessage(WM_SETCURSOR,HTCLIENT);
 check(virtualCorrect&&CursorSets==sets&&CursorInputTest::g_Hook_VClient_RenderView.Queries==queries,"Repeated WM_SETCURSOR never resets an I-beam or queries Source");
 double fieldValue=1;ImVec2 target;for(int frame=0;frame<3;++frame){io.DeltaTime=1.f/60;if(frame)io.MousePos=target;ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::Begin("Cursor hover test");ImGui::InputDouble("Value",&fieldValue);target=ImGui::GetItemRectMin()+ImVec2(25,8);if(frame==2)check(ImGui::GetMouseCursor()==ImGuiMouseCursor_TextInput,"Text fields still request the I-beam shape for the software cursor");ImGui::End();ImGui::Render();}
 check(CursorSets==sets,"Moving over real numeric inputs does not touch the OS cursor");
 CursorInputTest::g_Hook_VClient_RenderView.Menu=true;UpdateGraphInputOwnership();check(!m_GraphInputOwned&&io.MouseDrawCursor==originalDraw&&Hardware==ibeam&&Display==-1,"Source menu handoff restores its latest cursor and display count");
 HCURSOR ignored=nullptr;check(!OnSetCursor(arrow,ignored)&&!OnGetCursor(ignored),"Game cursor APIs pass through after handoff");
 CursorInputTest::g_Hook_VClient_RenderView.Menu=false;UpdateGraphInputOwnership();CameraOverlay::Look=true;UpdateGraphInputOwnership();check(!m_GraphInputOwned&&!io.MouseDrawCursor,"Freecam mouse-look disables the software cursor");
 CameraOverlay::Look=false;UpdateGraphInputOwnership();g_OverlayWindowFocused=false;UpdateGraphInputOwnership();check(!m_GraphInputOwned&&!io.MouseDrawCursor,"Losing focus releases cursor ownership");
 CameraOverlay::Open=false;Supported=false;check(!OnGetCursor(ignored),"Cursor hooks pass through outside supported overlay ownership");io.MousePos={-100,-100};
 }

 g_GizmoDrag.HasModel=g_GizmoDrag.WasUsing=true;g_GizmoDrag.KeyTime=10;FinishEdit();check(!g_GizmoDrag.HasModel&&!g_GizmoDrag.WasUsing,"Ending an edit clears pending gizmo state even when its key is hidden");



 { // Real receive-table discovery, proxy controller and command body; fake OS detour transaction only.
 bool wasHltv=engine.Hltv;engine.Hltv=false;void*vf[9]{};vf[8]=(void*)PovGetClasses;void**iface=vf;
 Tf2FixRecvProp property{};property.Name="m_iObserverMode";property.Type=0;property.Proxy=PovOriginal;
 Tf2FixRecvTable base{};base.Name="DT_BasePlayer";base.Props=&property;base.Count=1;
 Tf2FixRecvProp inherited{};inherited.Name="baseclass";inherited.Type=6;inherited.Table=&base;
 Tf2FixRecvTable playerTable{};playerTable.Name="DT_TFPlayer";playerTable.Props=&inherited;playerTable.Count=1;
 Tf2FixClientClass playerClass{};playerClass.Table=&playerTable;povClasses=&playerClass;
 MirvFix_InitializeTf2Client(nullptr);check(!MirvFix_SetPovDeathcam(true),"POV fix rejects a missing client interface");
 MirvFix_InitializeTf2Client(&iface);povHookError=5;check(!MirvFix_SetPovDeathcam(true)&&!MirvFix_GetPovDeathcam()&&!g_Tf2ObserverProxy,"Failed hook installation does not enable POV suppression");povHookError=0;
 callFix({"mirv_fix","povDeathcam","1"});check(MirvFix_GetPovDeathcam()&&povHookCommits==1,"POV command resolves inherited receive table and installs once");
 Tf2FixProxyData data{};data.Prop=&property;data.Value.Type=0;data.ObjectId=engine.Local;data.Value.Int=2;int object=7,out=9;
 Tf2FixObserverMode(&data,&object,&out);check(povObserved==7&&data.Value.Int==2&&povProxyCalls==1&&povSeenObject==&object&&povSeenOut==&out,"Local POV freeze mode becomes roaming without changing incoming data or forwarded objects");
 data.Value.Int=1;Tf2FixObserverMode(&data,&object,&out);check(povObserved==7&&g_Tf2DeathcamBlocked==2,"Recorder death camera is suppressed before freeze camera");
 for(int mode:{0,3,4,5,6,7}){data.Value.Int=mode;Tf2FixObserverMode(&data,&object,&out);check(povObserved==mode&&povSeenData==&data,"Ordinary observer modes forward original data");}
 data.Value.Int=2;data.ObjectId=engine.Local+1;Tf2FixObserverMode(&data,&object,&out);check(povObserved==2,"Remote players keep original observer modes");data.ObjectId=engine.Local;
 engine.Hltv=true;Tf2FixObserverMode(&data,&object,&out);check(povObserved==2,"STV playback bypasses POV suppression");engine.Hltv=false;
 engine.Playing=false;Tf2FixObserverMode(&data,&object,&out);check(povObserved==2,"Live play bypasses POV suppression");engine.Playing=true;
 g_SourceSdkVer=0;Tf2FixObserverMode(&data,&object,&out);check(povObserved==2,"Another Source game bypasses POV suppression");g_SourceSdkVer=SourceSdkVer_TF2;
 g_SourceSdkVer=SourceSdkVer_TF2Classified;Tf2FixObserverMode(&data,&object,&out);check(povObserved==7,"TF2C uses its verified roaming mode for the local POV recorder");
 check(MirvFix_SetPovDeathcam(true),"TF2C accepts the VClient017 observer proxy control");g_SourceSdkVer=SourceSdkVer_TF2;
 check(MirvFix_SetPovDeathcam(true)&&povHookCommits==1,"Repeated enable does not stack hooks");
 callFix({"mirv_fix","povDeathcam","0"});Tf2FixObserverMode(&data,&object,&out);check(!MirvFix_GetPovDeathcam()&&povObserved==2,"Disabled POV hook forwards original freeze mode");
 callFix({"mirv_fix","povDeathcam","2"});check(!MirvFix_GetPovDeathcam(),"Invalid POV command does not enable suppression");
 engine.Hltv=wasHltv;MirvFix_InitializeTf2Client(nullptr);
 }
 check(MirvFix_PlayerInterpolationAvailable(),"TF2 interpolation reports available engine variables");const float originalInterpolation=fakeCvar.Vars["cl_interp"].Value;
 callFix({"mirv_fix","playerInterpolation","0.1"});check(std::abs(MirvFix_GetPlayerInterpolation()-.1)<1e-6&&fakeCvar.Vars["cl_interp_ratio"].Value==2,"One command enables native movement interpolation");
 callFix({"mirv_fix","playerInterpolation","nan"});check(std::abs(MirvFix_GetPlayerInterpolation()-.1)<1e-6,"Invalid interpolation command retains active settings");
 callFix({"mirv_fix","playerInterpolation","0.2"});callFix({"mirv_fix","playerInterpolation","default"});check(fakeCvar.Vars["cl_interp"].Value==originalInterpolation&&fakeCvar.Vars["cl_interp_ratio"].Value==1,"Changing the repair window retains the original restore values");
 fakeCvar.Vars["cl_interp"].Maximum=.05;check(!MirvFix_SetPlayerInterpolation(.2)&&fakeCvar.Vars["cl_interp"].Value==originalInterpolation,"Engine-clamped interpolation rejects and rolls back atomically");fakeCvar.Vars["cl_interp"].Maximum=-1;
 check(MirvFix_SetPlayerInterpolation(.1),"Interpolation enables before lifecycle check");engine.Playing=false;MirvFix_Update();check(MirvFix_GetPlayerInterpolation()<0&&fakeCvar.Vars["cl_interp"].Value==originalInterpolation,"Ending a demo restores native interpolation settings");engine.Playing=true;
 MirvFix_SetPlayerInterpolation(.1);fakeCvar.Vars["cl_interp"].Value=.04f;MirvFix_Update();check(MirvFix_GetPlayerInterpolation()<0&&fakeCvar.Vars["cl_interp"].Value==.04f&&fakeCvar.Vars["cl_interp_ratio"].Value==1,"Manual cvar edits are preserved when releasing interpolation ownership");fakeCvar.Vars["cl_interp"].Value=originalInterpolation;

 check(MirvFix_SetPlayerInterpolation(.1),"Enable movement control before a backward-seek reload");
 engine.Loading=true;engine.Playing=false;MirvFix_Update();
 check(MirvFix_GetPlayerInterpolation()>=0&&fakeCvar.Vars["cl_interp"].Value==.1f,"Loading gap preserves interpolation ownership and cvars");
 check(!MirvFix_PlayerInterpolationAvailable()&&!MirvFix_SetPlayerInterpolation(.2),"Loading rejects new edits without discarding active control");
 fixMessages.clear();callFix({"mirv_fix","playerInterpolation","status"});
 check(fixMessages.find("waiting for reload")!=std::string::npos&&fixMessages.find("native settings: match")!=std::string::npos,"Status distinguishes reload from demo ending and verifies native values");
 engine.Loading=false;engine.Playing=true;MirvFix_Update();check(MirvFix_GetPlayerInterpolation()>=0,"Control survives completed reload");
 engine.Loading=true;engine.Playing=false;check(MirvFix_SetPlayerInterpolation(-1)&&MirvFix_GetPlayerInterpolation()<0,"Explicit default remains available during reload");engine.Loading=false;engine.Playing=true;
 MirvFix_SetPlayerInterpolation(.1);fakeCvar.Vars["cl_interp"].Value=.04f;
 check(MirvFix_SetPlayerInterpolation(.2)&&MirvFix_SetPlayerInterpolation(-1)&&fakeCvar.Vars["cl_interp"].Value==.04f,"An immediate edit preserves intervening manual values for restoration");fakeCvar.Vars["cl_interp"].Value=originalInterpolation;
 MirvFix_SetPlayerInterpolation(.1);fakeCvar.Vars["cl_interp"].Writable=false;
 check(!MirvFix_SetPlayerInterpolation(-1)&&MirvFix_GetPlayerInterpolation()>=0,"Rejected restoration retains ownership for retry");
 fakeCvar.Vars["cl_interp"].Writable=true;check(MirvFix_SetPlayerInterpolation(-1)&&fakeCvar.Vars["cl_interp"].Value==originalInterpolation,"Restoration succeeds once the engine accepts it");
 engine.Hltv=false;MirvFix_SetPlayerInterpolation(.1);fixMessages.clear();callFix({"mirv_fix","playerInterpolation","status"});
 check(fixMessages.find("POV/other demo")!=std::string::npos&&fixMessages.find("Native cl_interpolate=1")!=std::string::npos,"POV status reports native settings without claiming measured delay");
 engine.Hltv=true;fixMessages.clear();callFix({"mirv_fix","playerInterpolation","status"});check(fixMessages.find("STV/HLTV")!=std::string::npos,"STV context is distinct in diagnostics");MirvFix_SetPlayerInterpolation(-1);
 SOURCESDK::CSS::g_pCVar=nullptr;SOURCESDK::L4D2::g_pCVar=&fakeCvar;check(MirvFix_SetPlayerInterpolation(.1),"TF2's alternate registered cvar interface is supported");MirvFix_SetPlayerInterpolation(-1);SOURCESDK::L4D2::g_pCVar=nullptr;SOURCESDK::CSS::g_pCVar=&fakeCvar;
 g_SourceSdkVer=0;check(!MirvFix_SetPlayerInterpolation(.1),"Interpolation repair does not affect another game");g_SourceSdkVer=SourceSdkVer_TF2;
 double demoTime=-1;engine.HaveInfo=false;check(!ActualGetCurrentDemoTime(demoTime)&&demoTime==-1,"Demo time rejects absent demo information without dereferencing it");engine.HaveInfo=true;g_Hook_VClient_RenderView.GlobalsAvailable=false;check(!ActualGetCurrentDemoTime(demoTime),"Demo time rejects absent globals");g_Hook_VClient_RenderView.GlobalsAvailable=true;check(ActualGetCurrentDemoTime(demoTime),"Demo time works with valid engine services");
 auto&closedPath=g_Hook_VClient_RenderView.m_CamPath;BeginEdit();closedPath.Add(1,CamPathValue(0,0,0,0,0,0,90));FinishEdit();check(CampathPreview_Enter(),"Native preview enters with overlay closed");controlHeld=true;check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0),"Preview owns undo chord");check(closedPath.GetSize()==0,"History records first console edit before overlay opens");MirvCampathPreview_Update();check(g_CampathPreviewActive,"Preview stays active on empty path so the first capture can be redone");check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',1LL<<30)&&closedPath.GetSize()==0,"Held undo does not repeat");controlHeld=false;check(MirvCampathPreview_KeyEvent(WM_KEYUP,'Z',0),"Undo consumes matching release after Ctrl is released");controlHeld=shiftHeld=true;check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0),"Preview owns redo chord");check(closedPath.GetSize()==1,"Closed-overlay edit redoes");controlHeld=shiftHeld=false;MirvCampathPreview_KeyEvent(WM_KEYUP,'Z',0);CampathPreview_Exit();check(!MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0),"Z passes through outside preview");closedPath.Clear();UpdateHistory();
 SetOpen(true);g_Docking=false;frame();frame();check(widgets.count("Add point"),"Campath is the default tab");auto labelsDrawn=g_CampathDrawer;button("Segment durations");check(!g_CampathDrawer.GetDrawDurations()&&g_CampathDrawer.GetDrawNames()==labelsDrawn.GetDrawNames()&&g_CampathDrawer.GetDrawHolds()==labelsDrawn.GetDrawHolds()&&g_CampathDrawer.Draw_get()==labelsDrawn.Draw_get(),"Segment-duration toggle leaves world paths and other labels unchanged");button("Segment durations");{
 using namespace RenderCacheTest;CImGuiRenderCacheManager manager;IDirect3DDevice9 device;
 auto render=[&](auto work){std::thread worker([&]{renderThread=std::this_thread::get_id();work();});worker.join();};
 render([&]{manager.Dx9_Create(&device);});auto*data=ImGui::GetDrawData();check(data->TotalVtxCount>0,"Queue regression uses actual ImGui geometry");
 const float originalX=data->CmdLists[0]->VtxBuffer[0].pos.x;const int beforeUpload=calls;
 manager.CacheRender(data);check(calls==beforeUpload,"UI producer never allocates, locks or releases a D3D resource");
 data->CmdLists[0]->VtxBuffer[0].pos.x=originalX+100;render([&]{manager.Dx9_Render();});check(device.Draws>0&&device.FirstX==originalX,"Render thread uploads an owned CPU snapshot, not live ImGui buffers");data->CmdLists[0]->VtxBuffer[0].pos.x=originalX;

 auto gameState=device.States;D3DMATRIX gameMatrix{};gameMatrix.m[0][0]=17;gameMatrix.m[3][1]=42;
 render([&]{device.States[{99,1}]=123;device.States[{3,0}]=77;device.States[{3,1}]=88;device.States[{6,0}]=99;device.States[{5,D3DSAMP_MINFILTER}]=123;device.States[{2,D3DRS_FOGENABLE}]=1;device.SetTransform(D3DTS_WORLD,&gameMatrix);device.SetTransform(D3DTS_VIEW,&gameMatrix);device.SetTransform(D3DTS_PROJECTION,&gameMatrix);});gameState=device.States;
 render([&]{manager.Dx9_Render();});check(device.States==gameState,"Explicit state capture restores bindings, shaders, textures and render/sampler state");
 check(device.Transforms[D3DTS_WORLD].m[0][0]==17&&device.Transforms[D3DTS_VIEW].m[3][1]==42&&device.Transforms[D3DTS_PROJECTION].m[0][0]==17,"World/view/projection survive a driver whose state blocks omit transforms");
 auto beforeOffscreen=device.Draws;manager.CacheRender(data);render([&]{device.Target=&device.Offscreen;check(!manager.Dx9_Render(),"Offscreen EndScene is not accepted as the overlay frame");});
 check(device.Draws==beforeOffscreen&&device.Offscreen.Writes==0&&device.States==gameState,"Overlay cannot write UI geometry into a game atlas or shadow target");
 render([&]{device.Target=&device.Back;check(manager.Dx9_Render(),"A later backbuffer scene can render the pending overlay");});check(device.Draws>beforeOffscreen,"Offscreen rejection does not discard queued UI data");
 auto beforeCaptureFail=device.Draws;render([&]{device.FailCapture=true;manager.Dx9_Render();device.FailCapture=false;});check(device.Draws==beforeCaptureFail&&device.States==gameState,"Failed state capture skips drawing without changing game state");
 render([&]{device.FailTransform=true;manager.Dx9_Render();device.FailTransform=false;});check(device.Draws==beforeCaptureFail&&device.States==gameState,"Failed transform backup skips drawing without changing game state");
 render([&]{device.FailTarget=true;manager.Dx9_Render();device.FailTarget=false;});check(device.Draws==beforeCaptureFail,"Unavailable render target skips overlay drawing");
 check(device.Back.References==1&&device.Offscreen.References==1,"Render-target probes release every acquired surface reference");
 auto draws=device.Draws;manager.CacheRender(data);device.FailIndex=true;render([&]{manager.Dx9_Render();});check(device.Draws==draws&&!device.VB->Locked,"Index lock failure unlocks the vertex buffer and skips the frame");
 for(int i=0;i<12;++i){data->CmdLists[0]->VtxBuffer[0].pos.x=originalX+i;manager.CacheRender(data);}data->CmdLists[0]->VtxBuffer[0].pos.x=originalX;auto frames=device.Frames;render([&]{for(int i=0;i<12;++i)manager.Dx9_Render();});check(device.Frames-frames==12,"Renderer retains the newest UI snapshot across render frames without producer updates");check(device.FirstX==originalX+11,"The newest pointer/UI snapshot is the one uploaded");
 frames=device.Frames;manager.CacheRender(nullptr);render([&]{manager.Dx9_Render();manager.Dx9_Render();});check(device.Frames==frames,"An explicit empty UI frame clears the retained geometry without resurrecting old panels");
 manager.CacheRender(data);render([&]{manager.Dx9_Render();});frames=device.Frames;render([&]{manager.Dx9_Destroy();manager.Dx9_Create(&device);manager.Dx9_Render();});check(device.Frames==frames,"Device recreation drops the retained UI snapshot until fresh data arrives");
 render([&]{manager.Dx9_Destroy();manager.Dx9_Create(&device);});manager.CacheRender(data);frames=device.Frames;render([&]{manager.Dx9_Render();manager.Dx9_Destroy();});check(device.Frames==frames+1,"Queued drawing resumes after device recreation");std::atomic<bool> stop{false};std::thread consumer([&]{renderThread=std::this_thread::get_id();manager.Dx9_Create(&device);while(!stop.load()){manager.Dx9_Render();std::this_thread::yield();}manager.Dx9_Render();manager.Dx9_Render();manager.Dx9_Destroy();});for(int i=0;i<200;++i)manager.CacheRender(data);stop=true;consumer.join();
 check(wrongThread==0,"Concurrent production and consumption keep every D3D call on the consumer thread");
 }
 button("Camera collision");check(widgets.count("Free camera collision")&&widgets.count("Campath collision"),"Overlay exposes independent opt-in camera collision switches");button("Free camera collision");check(commands.back()=="mirv_cam collision free 1","Free collision toggle queues the existing camera console command");button("Campath collision");check(commands.back()=="mirv_cam collision campath 1","Campath collision toggle does not enable the free camera");button("Attached camera collision");check(commands.back()=="mirv_cam collision attached 1","Attached camera collision has its own switch");button("Ignore local player");check(commands.back()=="mirv_cam collision ignoreLocal 0","Local-player exclusion can be disabled in the overlay");button("Collide with other players");check(commands.back()=="mirv_cam collision players 0","Player hull filtering is exposed in the overlay");button("Camera collision");frame();frame();
 button("Add point");check(g_Hook_VClient_RenderView.m_CamPath.GetSize()==1,"Overlay captures a full camera key");
 g_MirvTime.Time=12;g_Hook_VClient_RenderView.LastCameraOrigin[0]=200;g_Hook_VClient_RenderView.LastCameraFov=170;button("Add point");auto&path=g_Hook_VClient_RenderView.m_CamPath;check(path.GetSize()==2&&path.CanEval(),"Overlay uses custom interpolation with two keys");
 auto captured=path.CaptureState();button("Undo");check(path.GetSize()==1,"Overlay undo reverses capture");button("Redo");check(path.CaptureState()==captured,"Overlay redo restores pose and tangent metadata");
 auto beforeProfile=path.CaptureState();button("Interpolation");auto*popup=ImGui::FindWindowByName("##Combo_00");check(popup&&popup->Active,"Interpolation chooser opens native ImGui popup");ImVec2 choice(popup->DC.CursorStartPos.x+50,popup->DC.CursorStartPos.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);frame(choice);frame(choice,true);frame(choice,false);frame();check(path.GetAutoTangentSettings().Method==AutoTangentMethod::CatmullRom,"UI selects whole-path Catmull-Rom");auto profileState=path.CaptureState();button("Undo");check(path.CaptureState()==beforeProfile,"UI profile change undoes atomically");button("Redo");check(path.CaptureState()==profileState,"UI profile change redoes atomically");button("Tension");check(path.GetAutoTangentSettings().Tension>0&&path.GetAutoTangentSettings().Tension<1,"UI tension slider updates live");auto tensionState=path.CaptureState();button("Undo");check(path.CaptureState()==profileState,"Tension drag is one undo step");button("Redo");check(path.CaptureState()==tensionState,"Tension redoes with all channel settings");button("Undo");
 g_ShowSequencer=true;g_ShowCurveEditor=true;g_CurveShow[0]=true;g_CurveShow[3]=true;g_ShowCameraControl=true;g_ShowGizmo=true;g_LastCampathCtx.active=true;g_LastCampathCtx.time=10;g_LastCampathCtx.value=path.GetBegin().GetValue();frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>1000,"Real sequencer, curve editor and gizmo emit geometry with two keys");
 const std::string pathName=g_Hook_VClient_RenderView.m_CamPaths.GetName(0);int motionSpans=0;
 for(const auto&span:spans)if(span.Caption==pathName){++motionSpans;check(span.Begin>=660&&span.End<=792,"Ordinary playback span stops at the last key");}
 check(motionSpans==1,"Single path has one finite playback span");
 path.SetHold(true);frame();int extensions=0;motionSpans=0;for(const auto&span:spans){if(span.Caption==pathName){++motionSpans;check(span.Begin>=660&&span.End<=792,"Endpoint hold does not lengthen the motion span");}if(span.Caption==pathName+" (endpoint hold)"){++extensions;check((span.Color>>24)==80,"Endpoint extension is drawn dimly");}}
 check(extensions==2&&motionSpans==1,"Before/after endpoint holds are distinct from actual travel");path.SetHold(false);frame();
 check(seqEnd==12000&&viewStart==0&&viewEnd==12000,"Timeline uses full demo length instead of current tick");check(sequencerLabels[792]==1,"Initial sequencer label uses classic index");ImGui::TestSetNeoView(100,700);frame();frame();check(!sequencerLabels.count(660)&&sequencerLabels.count(792)&&sequencerLabels[792]==1,"Panning/zooming past the first key never renumbers a visible key");button("Fit demo");auto beforeNav=path.CaptureState();
 ImVec2 seqMouse(seqRect.Min.x+(seqRect.Max.x-seqRect.Min.x)*0.5f,seqRect.Min.y+35);frame(seqMouse);frame(seqMouse,false,4);check(viewEnd-viewStart<12000,"Wheel zooms timeline");int previousStart=viewStart;frame(seqMouse,false,0,true);frame(ImVec2(seqMouse.x-100,seqMouse.y),false,0,true);frame(seqMouse);check(viewStart>previousStart,"Middle drag pans timeline");button("Fit demo");check(viewStart==0&&viewEnd==12000,"Fit restores complete demo view");frame(seqMouse);frame(seqMouse,false,100);check(viewEnd>=viewStart&&viewEnd-viewStart<=1,"Timeline extreme zoom remains a valid single-tick view");frame(seqMouse,false,-200);check(viewStart==0&&viewEnd==12000,"Timeline zoom-out clamps to full demo");demo.TotalTicks=600;frame();check(seqEnd==600&&viewStart==0&&viewEnd==600,"Loading shorter demo resets timeline range and view");demo.TotalTicks=12000;frame();
 ImVec2 graphMouse((graphRect.Min.x+graphRect.Max.x)/2,(graphRect.Min.y+graphRect.Max.y)/2);frame(graphMouse);frame(graphMouse,false,3);check(g_CurveTimeZoom>1,"Wheel zooms graph time");io.AddKeyEvent(ImGuiMod_Ctrl,true);frame(graphMouse,false,2);io.AddKeyEvent(ImGuiMod_Ctrl,false);check(g_CurveValueScale>1,"Ctrl wheel zooms graph values");frame(graphMouse,false,0,true);frame(ImVec2(graphMouse.x-40,graphMouse.y+20),false,0,true);frame();check(g_CurveTimeCenter>0.5&&g_CurveValueOffset>0,"Middle drag pans graph on both axes");check(path.CaptureState()==beforeNav,"Editor navigation leaves camera data untouched");button("Fit graph");check(g_CurveTimeZoom==1&&g_CurveValueScale==1&&g_CurveValueOffset==0,"Fit graph restores all view transforms");
 g_CurveNormalize=false;frame();auto hr=widgets.at("##h1b_0_ch3");ImVec2 handle((hr.Min.x+hr.Max.x)/2,(hr.Min.y+hr.Max.y)/2);frame(handle);frame(handle,true);frame(ImVec2(handle.x,handle.y-20),true);frame(ImVec2(handle.x,handle.y-20),false);frame();auto movedHandle=widgets.at("##h1b_0_ch3");check(std::abs((movedHandle.Min.y+movedHandle.Max.y)/2-(handle.y-20))<1,"Dragging a channel handle follows the pointer on shared value axes");g_CurveNormalize=true;


 // Capturing or editing externally during a key drag must retain the newer edit.
 {
 const auto initial=path.CaptureState();const double clock=g_MirvTime.Time;g_CurveFocusChannel=3;frame();auto r=widgets.at("##curve_key_0_ch3");ImVec2 point((r.Min.x+r.Max.x)/2,(r.Min.y+r.Max.y)/2);frame(point);frame(point,true);g_MirvTime.Time=path.GetUpperBound()+1;io.AddKeyEvent(ImGuiKey_E,true);frame(ImVec2(point.x+20,point.y-10),true);io.AddKeyEvent(ImGuiKey_E,false);check(path.GetSize()==initial.Keys.size()+1,"Capture during a graph-key drag is retained");
 io.AddKeyEvent(ImGuiKey_Escape,true);frame(point,true);check(path.GetSize()==initial.Keys.size()+1,"Escape cannot restore the old key-drag snapshot over a capture");io.AddKeyEvent(ImGuiKey_Escape,false);frame(point,false);SetOpen(false);path.RestoreState(initial);g_MirvTime.Time=clock;SetOpen(true);frame();frame();
 r=widgets.at("##curve_key_0_ch3");point=ImVec2((r.Min.x+r.Max.x)/2,(r.Min.y+r.Max.y)/2);frame(point);frame(point,true);path.SetFov(88);auto external=path.CaptureState();frame(ImVec2(point.x+20,point.y-10),true);check(path.CaptureState()==external,"Same-size external edits invalidate a key drag");frame(point,false);path.RestoreState(initial);FinishEdit();frame();
 r=widgets.at("##curve_key_0_ch3");point=ImVec2((r.Min.x+r.Max.x)/2,(r.Min.y+r.Max.y)/2);frame(point);frame(point,true);frame(ImVec2(point.x+10,point.y-10),true);FinishEdit();const auto interrupted=path.CaptureState();frame(ImVec2(point.x+30,point.y-30),true);check(path.CaptureState()==interrupted,"Focus/menu handoff ends a key drag before further mouse updates");frame(point,false);path.RestoreState(initial);FinishEdit();frame();
 }
 auto beforeKeyDrag=path.CaptureState();g_CurveFocusChannel=3;frame();auto kr=widgets.at("##curve_key_0_ch3");ImVec2 keyPoint((kr.Min.x+kr.Max.x)/2,(kr.Min.y+kr.Max.y)/2);frame(keyPoint);frame(keyPoint,true);frame(ImVec2(keyPoint.x+25,keyPoint.y-15),true);frame(ImVec2(keyPoint.x+25,keyPoint.y-15),true);auto movedNode=widgets.at("##curve_key_0_ch3");check(std::abs((movedNode.Min.x+movedNode.Max.x)/2-keyPoint.x-25)<1&&std::abs((movedNode.Min.y+movedNode.Max.y)/2-keyPoint.y+15)<1,"Graph key follows the pointer on both axes using a fixed transform");frame(ImVec2(keyPoint.x+25,keyPoint.y-15),false);frame();check(path.GetBegin().GetTime()>beforeKeyDrag.Keys.begin()->first&&path.GetBegin().GetValue().Fov>beforeKeyDrag.Keys.begin()->second.Fov,"Dragging graph key edits time and its focused value");HistoryUndo(false);check(path.CaptureState().Keys.begin()->first==beforeKeyDrag.Keys.begin()->first,"Graph drag undoes as a single edit");path.RestoreState(beforeKeyDrag);FinishEdit();frame();
 MirvCampathPreview_SetTime(11);check(MirvCampathPreview_IsActive()&&std::abs(input.Pose[0]-path.Eval(11).X)<0.001,"GUI uses native preview controller and evaluator");g_CurveFocusChannel=-1;float px,py,pz,pa,pb,pc,pf;applyPreviewView(px,py,pz,pa,pb,pc,pf);check(std::abs(px-path.Eval(11).X)<0.001&&input.MovementClears>0,"Rendering uses the shared preview pose after free-camera movement and smoothing");auto beforeInsert=path.CaptureState();std::vector<CamPathValue> samples;for(int i=0;i<=100;++i)samples.push_back(path.Eval(10+i/50.0));button("Insert keyframe at preview");check(path.GetSize()==3,"Insert button subdivides at shared preview cursor");for(int i=0;i<=100;++i)check(std::abs(path.Eval(10+i/50.0).X-samples[i].X)<1e-6,"UI insertion preserves existing path shape");HistoryUndo(false);check(path.GetSize()==2,"UI insertion uses shared undo");check(MirvCampathPreview_IsActive()&&MirvCampathPreview_GetTime()==11,"Undo leaves preview navigation unchanged");SetOpen(false);check(MirvCampathPreview_IsActive(),"Closing overlay retains native preview");SetOpen(true);check(MirvCampathPreview_IsActive(),"Reopening overlay shows the existing preview");MirvCampathPreview_SetActive(false);path.RestoreState(beforeInsert);FinishEdit();frame();
 path.SelectNone();path.SelectAdd(size_t(0),size_t(0));path.SetTangentMode(CamPath::CH_X,true,true,CamPath::TM_FREE);path.SetTangent(CamPath::CH_X,true,true,7,7);frame();auto edited=path.CaptureState();HistoryUndo(false);check(!(path.CaptureState()==edited),"Curve edit enters shared history");HistoryUndo(true);check(path.CaptureState()==edited,"Curve metadata redoes");


 path.SelectAll();frame();frame();check(timelineSelected==(int)path.GetSize(),"Native / graph select-all is reflected by the sequencer");path.SelectNone();frame();frame();check(timelineSelected==0,"Clearing native selection clears timeline highlights");
 // Unsupported handles must not pretend to edit Linear/Classic evaluation.
 auto graphBaseline=path.CaptureState();path.PositionInterpMethod_set(CamPath::DI_LINEAR);path.FovInterpMethod_set(CamPath::DI_LINEAR);path.RotationInterpMethod_set(CamPath::QI_SLINEAR);frame();check(!widgets.count("##h1b_0_ch0")&&!widgets.count("##h1b_0_ch3"),"Linear channels expose no inactive custom handles");path.RestoreState(graphBaseline);FinishEdit();frame();
 // Escape restores the whole drag and does not reapply it on release.
 g_CurveFocusChannel=3;ImGui::SetWindowFocus("Campath Graph");frame();auto cancelBaseline=path.CaptureState();auto cancelRect=widgets.at("##curve_key_0_ch3");ImVec2 cancelPoint((cancelRect.Min.x+cancelRect.Max.x)/2,(cancelRect.Min.y+cancelRect.Max.y)/2);frame(cancelPoint);frame(cancelPoint,true);frame(ImVec2(cancelPoint.x+15,cancelPoint.y-10),true);check(!(path.CaptureState()==cancelBaseline),"Graph drag changes a pose before cancellation");io.AddKeyEvent(ImGuiKey_Escape,true);frame(ImVec2(cancelPoint.x+15,cancelPoint.y-10),true);check(path.CaptureState()==cancelBaseline,"Escape cancels the authoring drag exactly");io.AddKeyEvent(ImGuiKey_Escape,false);frame(ImVec2(cancelPoint.x+15,cancelPoint.y-10),false);frame();check(path.CaptureState()==cancelBaseline,"Releasing a cancelled drag does not apply stale edits");g_CurveFocusChannel=-1;

 auto popupState=path.CaptureState();ImGui::SetWindowFocus("Campath Graph");frame();auto popupRect=widgets.at("##curve_key_0_ch3");ImVec2 popupPoint((popupRect.Min.x+popupRect.Max.x)/2,(popupRect.Min.y+popupRect.Max.y)/2);frame(popupPoint);frame(popupPoint,false,0,false,true);frame(popupPoint);check(g_CurveCtxKeyIndex==0,"Graph context menu captures a concrete key");HistoryUndo(false);frame();frame();check(g_CurveCtxKeyIndex<0,"Undo invalidates the old graph key menu");path.RestoreState(popupState);FinishEdit();frame();
 // The seek controller applies its policy after arrival, even without preview.
 const double previousTime=g_MirvTime.Time;check(MirvCampathPreview_SetSeekOptions(0,0),"Default seek mode freezes with timescale zero");int from=int(g_MirvTime.Time*66),target=from+132;check(MirvCampathPreview_SeekTick(target),"Seeking works independently of preview");check(commands.back().find(" 0 1")!=std::string::npos,"Engine seek pauses at arrival before post-seek policy");size_t pendingCommands=commands.size();MirvCampathPreview_Update();check(commands.size()==pendingCommands,"After-seek behavior waits for the target");g_MirvTime.Time=target/66.0;MirvCampathPreview_Update();check(commands.back()=="demo_timescale 0;demo_resume","Timescale is set before resuming at the target");check(MirvCampathPreview_SetSeekOptions(1,0)&&MirvCampathPreview_SeekTick(target-66),"Backward seeks accept pause mode");g_MirvTime.Time=(target-66)/66.0;MirvCampathPreview_Update();check(commands.back()=="demo_timescale 1;demo_pause","Pause behavior is applied after a backward seek");check(MirvCampathPreview_SetSeekOptions(2,0)&&MirvCampathPreview_SeekTick(target),"Continue behavior can be selected");g_MirvTime.Time=target/66.0;MirvCampathPreview_Update();check(commands.back()=="demo_timescale 1;demo_resume","Continue resumes normal playback");check(!MirvCampathPreview_SetSeekOptions(0,std::numeric_limits<double>::quiet_NaN())&&!MirvCampathPreview_SetSeekOptions(3,0),"Invalid seek options preserve the current policy");MirvCampathPreview_SetSeekOptions(0,0);g_MirvTime.Time=previousTime;

 MirvCampathPreview_SetTime(11);size_t beforeCtrl=commands.size();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_RightCtrl,true);frame();check(commands.size()==beforeCtrl,"Right Ctrl waits for release before seeking");io.AddKeyEvent(ImGuiKey_RightCtrl,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();check(commands.size()==beforeCtrl+1&&commands.back().find("demo_gototick")!=std::string::npos,"Right Ctrl alone seeks despite ImGui's mirrored modifier event");g_MirvTime.Time=11;MirvCampathPreview_Update();g_MirvTime.Time=previousTime;MirvCampathPreview_SetActive(false);
 // Numeric indices and full text share the same layout policy.
 g_WorldLabelCells.clear();check(PlaceWorldLabel(100,100,0,0,18)==0,"First world label keeps its anchor");check(std::abs(PlaceWorldLabel(100,100,0,0,18)-1.35f)<1e-6,"Coincident anchors share a fixed world stack");check(std::abs(PlaceWorldLabel(100,100,0,0,18)-2.7f)<1e-6,"Third coincident anchor occupies the next world row");check(PlaceWorldLabel(100,200,0,0,18)==0,"Unrelated world positions cannot displace one another");check(PlaceWorldLabel(100,100,100,0,18)==0,"A different depth does not reserve a stack slot at this anchor");check(PlaceWorldLabel(0,0,0,0,0)==-1,"Invalid world-label scale is rejected");

 auto&document=g_Hook_VClient_RenderView.m_CamPaths;auto oldCount=document.GetCount();g_ShowSequencer=g_ShowGizmo=g_ShowCameraControl=false;frame();frame();button("Duplicate path");check(document.GetCount()==oldCount+1&&document.GetActive()==oldCount&&path.GetSize()==2,"Duplicate button creates a named ordinary native path");check(document.IsMuted(oldCount),"UI duplicates start muted");g_ShowSequencer=true;frame();frame();check(widgets.count("Playback / cuts")&&widgets.count((document.GetName(oldCount)+" (editing)").c_str())&&widgets.count(document.GetName(0).c_str()),"Collection timeline shows named editing/reference tracks and playback cuts");g_ShowSequencer=false;button("Undo");check(document.GetCount()==oldCount,"Creating a path undoes across the document");button("Redo");check(document.GetCount()==oldCount+1,"Named path creation redoes");button("Undo");check(document.IsDirty(),"Unsaved camera edits are marked dirty");SetOpen(false);check(std::filesystem::exists(g_RecoveryFile),"Closing overlay writes recoverable work without marking it saved");SetOpen(true);auto save=RecoveryDirectory()/"test-collection.xml";check(SaveDocument(save.wstring().c_str(),true)&&!document.IsDirty(),"Saving all updates the document savepoint");auto saved=path.CaptureState();
 g_Autosave=true;size_t cleanWrites=recoveryWrites,cleanRemoves=recoveryRemoves;
 for(int i=0;i<50;++i){g_RecoveryNextSave=0;UpdateRecovery(false);}
 check(recoveryWrites==cleanWrites&&recoveryRemoves==cleanRemoves,"Clean idle intervals perform no recovery I/O");g_Autosave=false;
 BeginEdit();path.SelectNone();path.SetFov(70);FinishEdit();check(document.IsDirty(),"Editing after save marks the document dirty");
 g_Autosave=true;g_RecoveryNextSave=0;UpdateRecovery(false);const size_t firstRecovery=recoveryWrites;check(firstRecovery==cleanWrites+1&&g_RecoveryWritten,"Dirty work writes one recovery snapshot");
 for(int i=0;i<50;++i){g_RecoveryNextSave=0;UpdateRecovery(false);}
 check(recoveryWrites==firstRecovery,"Unchanged dirty work is not serialized again on idle intervals");
 path.SelectAll();g_RecoveryNextSave=0;UpdateRecovery(false);check(recoveryWrites==firstRecovery,"Selection changes do not rewrite recovery data");
 path.SetFov(71);g_RecoveryNextSave=0;UpdateRecovery(false);check(recoveryWrites==firstRecovery+1,"New camera edits refresh recovery on the next interval");g_Autosave=false;
 check(LoadDocument(save.wstring().c_str(),true)&&path.CaptureState().Keys==saved.Keys,"Document load uses the same named paths");check(!g_RecoveryCandidates.empty(),"Loading another document retains a backup of prior unsaved work");std::filesystem::remove(save);
 g_Docking=true;io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>0,"Docked workspace renders through the same UI");g_Docking=false;g_ShowSequencer=g_ShowCameraControl=g_ShowGizmo=false;frame();frame();

 g_ShowCurveEditor=false;SetOpen(false);g_MirvTime.Time=14;check(MirvCampathPreview_SetActive(true),"Preview enters for shortcut editing");auto keyCount=path.GetSize();check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'E',0)&&path.GetSize()==keyCount+1,"E captures a full camera key at demo time");MirvCampathPreview_KeyEvent(WM_KEYDOWN,'E',1LL<<30);check(path.GetSize()==keyCount+1,"Holding capture does not repeatedly overwrite keys");MirvCampathPreview_KeyEvent(WM_KEYUP,'E',0);HistoryUndo(false);check(path.GetSize()==keyCount,"Keyboard capture uses the same undo history");controlHeld=true;check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'A',0),"Ctrl+A belongs to preview selection");MirvCampathPreview_KeyEvent(WM_KEYUP,'A',0);for(auto it=path.GetBegin();it!=path.GetEnd();++it)check(it.GetValue().Selected,"Ctrl+A selects every native key");controlHeld=false;path.SelectNone();path.SelectAdd(size_t(0),size_t(0));g_MirvTime.Time=11.5;check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Y',0),"Y captures a hold release");MirvCampathPreview_KeyEvent(WM_KEYUP,'Y',0);check(path.GetBegin().GetValue().HoldDuration==1.5,"Hold release captures current demo time");shiftHeld=true;MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Y',0);MirvCampathPreview_KeyEvent(WM_KEYUP,'Y',0);shiftHeld=false;check(path.GetBegin().GetValue().HoldDuration==0,"Shift+Y clears the selected hold");controlHeld=shiftHeld=true;MirvCampathPreview_KeyEvent(WM_KEYDOWN,'A',0);MirvCampathPreview_KeyEvent(WM_KEYUP,'A',0);controlHeld=shiftHeld=false;check(commands.back().find("mirv_campath edit anchor #0 current")!=std::string::npos,"Anchor shortcut reuses HLAE's existing command");size_t beforeChord=commands.size();controlHeld=true;MirvCampathPreview_KeyEvent(WM_KEYDOWN,VK_RCONTROL,0);MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0);MirvCampathPreview_KeyEvent(WM_KEYUP,'Z',0);MirvCampathPreview_KeyEvent(WM_KEYUP,VK_RCONTROL,0);controlHeld=false;check(commands.size()==beforeChord,"Using Right Ctrl for undo does not seek");HistoryUndo(true);MirvCampathPreview_KeyEvent(WM_KEYDOWN,VK_CONTROL,1LL<<24);check(commands.size()==beforeChord,"Native Right Ctrl also waits for release");MirvCampathPreview_KeyEvent(WM_KEYUP,VK_CONTROL,1LL<<24);check(commands.size()==beforeChord+1&&commands.back().find("demo_gototick")!=std::string::npos,"Extended Win32 Ctrl events trigger the same seek");g_MirvTime.Time=MirvCampathPreview_GetTime()+path.GetOffset();MirvCampathPreview_Update();MirvCampathPreview_SetActive(false);g_MirvTime.Time=previousTime;SetOpen(true);frame();frame();

 // New editing controls use the actual graph and native evaluator.
 auto editorBefore=path.CaptureState();
 g_ShowCurveEditor=true;g_ShowCameraControl=false;g_ShowGizmo=false;g_CurveFocusChannel=0;g_CurveShow[0]=true;g_CurveShow[3]=false;g_ModifierRangeTool=false;
 io.DisplaySize=ImVec2(1600,1400);ImGui::SetWindowSize("Campath Graph",ImVec2(1000,1000));ImGui::SetWindowFocus("Campath Graph");
 path.Clear();path.PositionInterpMethod_set(CamPath::DI_CUSTOM);path.RotationInterpMethod_set(CamPath::QI_CUSTOM);path.FovInterpMethod_set(CamPath::DI_CUSTOM);
 path.Add(10,CamPathValue(0,0,0,0,0,0,90));path.Add(11,CamPathValue(50,0,0,0,20,0,90));path.Add(12,CamPathValue(100,0,0,0,40,0,90));FinishEdit();frame();frame();
 check(widgets.count("Frame selected")&&widgets.count("Delete selected keys")&&widgets.count("Rotation"),"Graph exposes framing, deletion and explicit rotation mode");

 // Box selection retains distinct channels, not just whole-pose selected flags.
 auto crossScene=path.CaptureState();auto crossKeys=crossScene.Keys;crossKeys.at(10).Fov=20;crossKeys.at(11).Fov=80;crossKeys.at(12).Fov=20;for(auto&key:crossKeys)key.second.Selected=false;path.ReplaceKeys(crossKeys);g_GraphChannels.clear();g_GraphSelectionRevision=~0ull;FinishEdit();
 g_CurveFocusChannel=-1;g_CurveNormalize=false;for(auto&show:g_CurveShow)show=false;g_CurveShow[0]=g_CurveShow[3]=true;frame();frame();button("Fit graph");frame();
 auto x1=widgets.at("##curve_key_1_ch0"),x2=widgets.at("##curve_key_2_ch0");ImVec2 boxA(x1.Min.x-10,x2.Min.y-10),boxB(x2.Max.x+10,x1.Max.y+10);
 frame(boxA);frame(boxA,true);frame(boxB,true);frame(boxB,false);frame();if(!(GraphChannelSelected(11,0)&&GraphChannelSelected(11,3)&&GraphChannelSelected(12,0)&&!GraphChannelSelected(12,3)&&!GraphChannelSelected(10,0))){std::cerr<<"box "<<boxA.x<<","<<boxA.y<<" -> "<<boxB.x<<","<<boxB.y<<" graph "<<graphRect.Min.x<<","<<graphRect.Min.y<<" - "<<graphRect.Max.x<<","<<graphRect.Max.y<<"\n";for(auto&e:g_GraphChannels)std::cerr<<e.first<<":"<<e.second<<" ";std::cerr<<"\n";}
 check(GraphChannelSelected(11,0)&&GraphChannelSelected(11,3)&&GraphChannelSelected(12,0)&&!GraphChannelSelected(12,3)&&!GraphChannelSelected(10,0),"One box selects different key/channel pairs without selecting unrelated curves");
 auto crossBefore=path.CaptureState();auto crossHandle=widgets.at("##h2b_0_ch0");ImVec2 crossPoint((crossHandle.Min.x+crossHandle.Max.x)/2,(crossHandle.Min.y+crossHandle.Max.y)/2);
 io.AddKeyEvent(ImGuiMod_Shift,true);frame(crossPoint);frame(crossPoint,true);check(g_TangentDrag.Handles.size()==3,"Shift-drag captures selected handles across multiple channels");frame({crossPoint.x-15,crossPoint.y-12},true);frame({crossPoint.x-15,crossPoint.y-12},false);io.AddKeyEvent(ImGuiMod_Shift,false);frame();
 auto crossAfter=path.CaptureState();check(crossAfter.Keys.at(11).TxWIn>1&&crossAfter.Keys.at(11).TfovWIn>1&&crossAfter.Keys.at(12).TxWIn>1,"All selected channel handles move together");check(crossAfter.Keys.at(12).TfovWIn==crossBefore.Keys.at(12).TfovWIn&&crossAfter.Keys.at(12).TfovModeIn==crossBefore.Keys.at(12).TfovModeIn&&crossAfter.Keys.at(10)==crossBefore.Keys.at(10),"Unselected channels and keys retain their handles");HistoryUndo(false);check(path.CaptureState()==crossBefore,"A multi-channel box-selected handle drag undoes in one step");HistoryUndo(true);check(path.CaptureState()==crossAfter,"Multi-channel handle edits redo atomically");
 path.RestoreState(crossScene);g_GraphChannels.clear();g_GraphSelectionRevision=~0ull;g_CurveFocusChannel=0;g_CurveNormalize=true;g_CurveShow[3]=false;FinishEdit();frame();frame();
 // Group handles use a frozen transform and preserve the selection.
 path.SelectAll();FinishEdit();frame();frame();auto beforeGroup=path.CaptureState();auto groupRect=widgets.at("##h1b_0_ch0");ImVec2 groupPoint((groupRect.Min.x+groupRect.Max.x)/2,(groupRect.Min.y+groupRect.Max.y)/2);
 io.AddKeyEvent(ImGuiMod_Shift,true);frame(groupPoint);frame(groupPoint,true);const auto groupRevision=path.GetRevision();frame({groupPoint.x+15,groupPoint.y-20},true);
 check(path.GetRevision()==groupRevision+1,"A group handle move emits one native notification per frame");
 const auto stationaryRevision=path.GetRevision();frame({groupPoint.x+15,groupPoint.y-20},true);check(path.GetRevision()==stationaryRevision,"A stationary held group handle does not repeat edits");auto groupState=path.CaptureState();auto groupKey=groupState.Keys.begin();check(groupKey->second.Selected&&groupKey->second.TxWOut>1&&groupKey->second.TxModeOut==CamPath::TM_FREE,"Shift-drag moves the first selected outgoing handle");++groupKey;check(groupKey->second.Selected&&groupKey->second.TxWOut>1&&groupKey->second.TxModeOut==CamPath::TM_FREE,"Shift-drag moves matching handles on other selected keys");++groupKey;check(groupKey->second==beforeGroup.Keys.rbegin()->second,"A selected endpoint without that handle stays intact");
 frame({groupPoint.x+15,groupPoint.y-20},false);io.AddKeyEvent(ImGuiMod_Shift,false);frame();auto afterGroup=path.CaptureState();HistoryUndo(false);check(path.CaptureState()==beforeGroup,"Group handle drag is one undo step");HistoryUndo(true);check(path.CaptureState()==afterGroup,"Group handle drag redoes together");HistoryUndo(false);frame();frame();
 double slope0,weight0,slope1,weight1;path.GetTangent(CamPath::CH_X,0,true,slope0,weight0);path.GetTangent(CamPath::CH_X,1,true,slope1,weight1);groupRect=widgets.at("##h1b_0_ch0");groupPoint={(groupRect.Min.x+groupRect.Max.x)/2,(groupRect.Min.y+groupRect.Max.y)/2};
 io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiMod_Shift,true);frame(groupPoint);frame(groupPoint,true);frame({groupPoint.x+20,groupPoint.y+25},true);double newSlope,newWeight;path.GetTangent(CamPath::CH_X,0,true,newSlope,newWeight);check(std::abs(newSlope-slope0)<1e-8&&newWeight>weight0,"Ctrl+Shift adjusts group length while retaining the first slope");path.GetTangent(CamPath::CH_X,1,true,newSlope,newWeight);check(std::abs(newSlope-slope1)<1e-8&&newWeight>weight1,"Length-only control also preserves other selected slopes");
 io.AddKeyEvent(ImGuiKey_Escape,true);frame(groupPoint,true);check(path.CaptureState()==beforeGroup,"Escape restores every handle in a group drag");io.AddKeyEvent(ImGuiKey_Escape,false);frame(groupPoint,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);frame();
 groupRect=widgets.at("##h1b_0_ch0");groupPoint={(groupRect.Min.x+groupRect.Max.x)/2,(groupRect.Min.y+groupRect.Max.y)/2};io.AddKeyEvent(ImGuiMod_Shift,true);frame(groupPoint);frame(groupPoint,true);frame({groupPoint.x+15,groupPoint.y-10},true);SetMouseLook(true);auto handoffState=path.CaptureState();check(g_TangentDrag.Handles.empty(),"Freecam handoff clears pending group handle input");SetMouseLook(false);frame(groupPoint,true);check(path.CaptureState()==handoffState,"Held mouse after handoff cannot replay a stale tangent drag");frame(groupPoint,false);io.AddKeyEvent(ImGuiMod_Shift,false);frame();HistoryUndo(false);frame();

 g_CurveLocked[0]=true;g_LockKeyTimes=true;auto lockedRect=widgets.at("##curve_key_1_ch0");ImVec2 lockedPoint((lockedRect.Min.x+lockedRect.Max.x)/2,(lockedRect.Min.y+lockedRect.Max.y)/2);
 frame(lockedPoint);frame(lockedPoint,true);frame(ImVec2(lockedPoint.x+30,lockedPoint.y-20),true);frame(ImVec2(lockedPoint.x+30,lockedPoint.y-20),false);frame();
 auto lockedKeys=path.CaptureState().Keys;check(lockedKeys.count(11)&&lockedKeys.at(11).X==50,"Channel and time locks protect graph drags");g_CurveLocked[0]=false;g_LockKeyTimes=false;
 path.SelectNone();path.SelectAdd(size_t(1),size_t(1));FinishEdit();frame();button("Frame selected");check(g_CurveTimeZoom>1,"Frame selected zooms to native selection");button("Fit graph");
 const auto beforeDelete=path.CaptureState();button("Delete selected keys");check(path.GetSize()==2,"Graph deletes selected native keys");HistoryUndo(false);check(path.CaptureState()==beforeDelete,"Graph deletion has one exact undo");frame();frame();
 auto *graphWindow=ImGui::FindWindowByName("Campath Graph");graphWindow->StateStorage.SetInt(graphWindow->GetID("Travel speed / distance timing"),1);frame();frame();
 check(widgets.count("Ease start / end"),"Speed shaping controls are visible");button("Ease start / end");check(path.GetDistanceTiming()&&path.GetSpeedProfile().size()==4,"Speed preset performs native whole-path retiming");
 HistoryUndo(false);frame();graphWindow->StateStorage.SetInt(graphWindow->GetID("Travel speed / distance timing"),0);
 graphWindow->StateStorage.SetInt(graphWindow->GetID("Range modifiers"),1);g_ModifierBegin=10.2;g_ModifierEnd=11.8;frame();frame();
 check(widgets.count("Add noise to range"),"Range modifier controls are visible");const auto noiseBase=path.CaptureState().Keys;button("Add noise to range");
 check(path.GetModifiers().size()==1&&path.CaptureState().Keys==noiseBase,"Adding graph noise preserves authored keys");check(std::abs(path.Eval(10.8).X-path.EvalUnmodified(10.8).X)>0.001,"Graph modifier affects native playback");
 graphWindow->StateStorage.SetInt(graphWindow->GetID("Range modifiers"),0);frame();frame();
 auto fadeRect=widgets.at("##modifier_edge_0_2");ImVec2 fadePoint((fadeRect.Min.x+fadeRect.Max.x)/2,(fadeRect.Min.y+fadeRect.Max.y)/2);auto beforeFade=path.CaptureState();
 frame(fadePoint);frame(fadePoint,true);frame(ImVec2(fadePoint.x+25,fadePoint.y),true);check(path.GetModifiers()[0].FadeIn>beforeFade.Modifiers[0].FadeIn,"Envelope fade is draggable in the graph");
 io.AddKeyEvent(ImGuiKey_Escape,true);frame(ImVec2(fadePoint.x+25,fadePoint.y),true);check(path.CaptureState()==beforeFade,"Escape restores a modifier envelope drag");io.AddKeyEvent(ImGuiKey_Escape,false);frame(fadePoint,false);frame();
 path.SetModifiers({});path.SelectAll();SetOpen(false);check(MirvCampathPreview_SetActive(true),"Native preview active for Delete");const auto nativeDelete=path.GetSize();check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,VK_DELETE,0),"Native preview owns Delete");MirvCampathPreview_KeyEvent(WM_KEYUP,VK_DELETE,0);check(path.GetSize()==0,"Native Delete shares selection deletion");HistoryUndo(false);check(path.GetSize()==nativeDelete,"Native Delete uses authoring history");MirvCampathPreview_SetActive(false);SetOpen(true);
 path.RestoreState(editorBefore);g_CurveFocusChannel=-1;g_CurveTimeZoom=1;g_CurveTimeCenter=0.5;g_ShowCurveEditor=false;FinishEdit();frame();frame();

 // Speed-editor regressions: clicks, bounds, locks, external edits and interrupted ownership.
 auto documentBeforeSpeed=g_Hook_VClient_RenderView.m_CamPaths.CaptureState();const double clockBeforeSpeed=g_MirvTime.Time;
 CamPath emptySpeed;path.RestoreState(emptySpeed.CaptureState());path.PositionInterpMethod_set(CamPath::DI_CUSTOM);path.RotationInterpMethod_set(CamPath::QI_CUSTOM);path.FovInterpMethod_set(CamPath::DI_CUSTOM);
 path.Add(10,CamPathValue(0,0,0,0,0,0,90));path.Add(11,CamPathValue(10,0,0,0,20,0,90));path.Add(12,CamPathValue(100,0,0,0,40,0,90));FinishEdit();
 g_ShowCurveEditor=true;g_ShowSequencer=false;g_ShowGizmo=false;g_ShowCameraControl=false;g_ModifierRangeTool=false;
 ImGui::SetWindowSize("Campath Graph",ImVec2(1000,1200));ImGui::SetWindowFocus("Campath Graph");auto *speedWindow=ImGui::FindWindowByName("Campath Graph");speedWindow->StateStorage.SetInt(speedWindow->GetID("Travel speed / distance timing"),1);frame();frame();
 auto speedRect=widgets.at("##speed_profile");check(drawnSpeedKeyProgress==path.GetSpeedKeyProgress()&&drawnSpeedKeyProgress.size()==path.GetSize(),"Speed plot draws one native moving-time marker per keyframe");ImVec2 speedPoint(speedRect.Max.x-8,speedRect.Min.y+102-94/1.15f);auto beforeClick=path.CaptureState();
 frame(speedPoint);frame(speedPoint,true);frame(speedPoint,false);frame();check(path.CaptureState()==beforeClick,"Speed-point click alone does not retime or bake keys");
 g_LockKeyTimes=true;frame(speedPoint);frame(speedPoint,true);frame(ImVec2(speedPoint.x,speedPoint.y-20),true);frame(speedPoint,false);button("Ease start / end");button("Retime by distance");check(path.CaptureState()==beforeClick,"Time lock protects speed drags, presets and duration retiming");g_LockKeyTimes=false;
 check(path.ApplySpeedProfile({{0,1},{.5,1},{1,1}},2),"Prepare interactive speed points");FinishEdit();frame();frame();speedRect=widgets.at("##speed_profile");speedPoint=ImVec2((speedRect.Min.x+speedRect.Max.x)/2,speedRect.Min.y+102-94/1.15f);auto beforeSpeedDrag=path.CaptureState();
 frame(speedPoint);frame(speedPoint,true);frame(ImVec2(speedPoint.x,speedRect.Min.y-150),true);check(path.GetSpeedProfile()[1].Speed<=1.150001,"Upward speed drag is bounded to the visible scale");
 bool plotClip=false;auto *drawData=ImGui::GetDrawData();for(int list=0;list<drawData->CmdListsCount;++list)for(const auto &cmd:drawData->CmdLists[list]->CmdBuffer)if(std::abs(cmd.ClipRect.x-speedRect.Min.x)<.1 && std::abs(cmd.ClipRect.y-speedRect.Min.y)<.1 && std::abs(cmd.ClipRect.z-speedRect.Max.x)<.1 && std::abs(cmd.ClipRect.w-speedRect.Max.y)<.1)plotClip=true;
 check(plotClip,"Speed geometry uses a chart-specific clip rectangle");
 io.AddKeyEvent(ImGuiKey_Escape,true);frame(ImVec2(speedPoint.x,speedRect.Min.y-150),true);check(path.CaptureState()==beforeSpeedDrag,"Escape restores a speed drag exactly");io.AddKeyEvent(ImGuiKey_Escape,false);frame(speedPoint,false);frame();
 frame(speedPoint);frame(speedPoint,true);frame(ImVec2(speedPoint.x,speedPoint.y-10),true);SetOpen(false);frame(ImVec2(-100,-100),false);
 auto &speedDocument=g_Hook_VClient_RenderView.m_CamPaths;check(speedDocument.NewPath("Drag target regression"),"Create distinct drag target");path.PositionInterpMethod_set(CamPath::DI_CUSTOM);path.RotationInterpMethod_set(CamPath::QI_CUSTOM);path.FovInterpMethod_set(CamPath::DI_CUSTOM);path.Add(10,CamPathValue(1000,0,0,0,0,0,90));path.Add(12,CamPathValue(2000,0,0,0,0,0,90));FinishEdit();const auto distinctTarget=path.CaptureState();
 SetOpen(true);frame(speedPoint,true);check(path.CaptureState()==distinctTarget,"Reopening with a held mouse cannot overwrite another shot");frame(speedPoint,false);frame();
 path.ApplySpeedProfile({{0,1},{.5,1},{1,1}},2);FinishEdit();frame();frame();speedRect=widgets.at("##speed_profile");speedPoint=ImVec2((speedRect.Min.x+speedRect.Max.x)/2,speedRect.Min.y+102-94/1.15f);frame(speedPoint);frame(speedPoint,true);const auto countBeforeCapture=path.GetSize();g_MirvTime.Time=11.5;
 io.AddKeyEvent(ImGuiKey_E,true);frame(ImVec2(speedPoint.x,speedPoint.y-10),true);io.AddKeyEvent(ImGuiKey_E,false);check(path.GetSize()==countBeforeCapture+1,"Capture during a speed drag survives external-edit invalidation");io.AddKeyEvent(ImGuiKey_Escape,true);frame(speedPoint,true);check(path.GetSize()==countBeforeCapture+1,"Escape does not restore a stale snapshot over the new capture");io.AddKeyEvent(ImGuiKey_Escape,false);frame(speedPoint,false);frame();

 // Cached evaluated graph work is shared across channels and stays idle until data/view changes.
 path.ApplySpeedProfile({{0,1},{.5,2},{1,1}},2);CamPath::NoiseModifier cachedNoise;cachedNoise.Begin=10;cachedNoise.End=12;cachedNoise.Amplitude[0]=4;path.SetModifiers({cachedNoise});FinishEdit();
 ImGui::SetWindowSize("Campath Graph",ImVec2(1000,1500));for(int ch=0;ch<7;++ch)g_CurveShow[ch]=true;frame();frame();
 check(g_CurveRangeSamples.Revision==path.GetGeometryRevision()&&!g_CurveRangeSamples.Values.empty(),"Graph ranges cache the modified evaluator");check(g_CurveViewSamples.Revision==path.GetGeometryRevision()&&!g_CurveViewSamples.Base.empty(),"Graph shares base and modified samples for all channels");
 const auto rangeStorage=g_CurveRangeSamples.Values.data(),viewStorage=g_CurveViewSamples.Values.data();const auto rangeTimes=g_CurveRangeSamples.Times,viewTimes=g_CurveViewSamples.Times;auto graphDataBefore=path.CaptureState();size_t idleCommands=commands.size(),idleBuilds=curveSampleBuilds;frame();frame();check(curveSampleBuilds==idleBuilds,"Idle graph does not reevaluate cached trajectories");
 check(g_CurveRangeSamples.Values.data()==rangeStorage&&g_CurveViewSamples.Values.data()==viewStorage&&g_CurveRangeSamples.Times==rangeTimes&&g_CurveViewSamples.Times==viewTimes,"Idle graph frames reuse evaluated samples");check(path.CaptureState()==graphDataBefore&&commands.size()==idleCommands,"Graph sampling does not change path data, seek, pause or issue audio commands");
 for(size_t i=0;i<g_CurveViewSamples.Times.size();++i)check(std::abs(g_CurveViewSamples.Values[i].X-path.Eval(g_CurveViewSamples.Times[i]).X)<1e-9,"Graph samples agree with playback evaluator");
 cachedNoise.Amplitude[0]=8;path.SetModifiers({cachedNoise});frame();check(g_CurveRangeSamples.Revision==path.GetGeometryRevision()&&g_CurveViewSamples.Revision==path.GetGeometryRevision(),"Modifier changes refresh cached graph output");
 for(int ch=1;ch<7;++ch)g_CurveShow[ch]=false;ImGui::SetWindowSize("Campath Graph",ImVec2(1000,650));

 // Edit the new range-strength graph through real UI input, including cancellation and bounds.
 path.SetModifiers({});CamPath::Modifier envelopeModifier;envelopeModifier.Begin=10;envelopeModifier.End=12;envelopeModifier.Amplitude[0]=3;envelopeModifier.Envelope={{0,1},{.5,.5},{1,1}};path.SetModifiers({envelopeModifier});g_ModifierSelection=0;g_ModifierRangeTool=false;FinishEdit();
 ImGui::SetWindowPos("Campath Graph",ImVec2(590,20));ImGui::SetWindowSize("Campath Graph",ImVec2(1000,950));auto *envelopeWindow=ImGui::FindWindowByName("Campath Graph");envelopeWindow->StateStorage.SetInt(envelopeWindow->GetID("Travel speed / distance timing"),0);envelopeWindow->StateStorage.SetInt(envelopeWindow->GetID("Range modifiers"),1);frame();frame();
 ImGuiWindow *modifierChild=nullptr;for(auto *window:ImGui::GetCurrentContext()->Windows)if(std::string(window->Name).find("Modifier controls")!=std::string::npos&&window->ParentWindow==envelopeWindow)modifierChild=window;check(modifierChild!=nullptr,"Modifier controls use the existing graph child window");modifierChild->StateStorage.SetInt(modifierChild->GetID("Strength envelope"),1);frame();ImGui::SetScrollY(modifierChild,modifierChild->ScrollMax.y);frame();ImGui::SetScrollY(modifierChild,modifierChild->ScrollMax.y);frame();
 check(widgets.count("##modifier_envelope"),"Modifier strength graph is available in the editor");auto envelopeRect=widgets.at("##modifier_envelope");ImVec2 envelopePoint((envelopeRect.Min.x+envelopeRect.Max.x)/2,(envelopeRect.Min.y+envelopeRect.Max.y)/2);auto beforeEnvelope=path.CaptureState();frame(envelopePoint);frame(envelopePoint,true);frame(ImVec2(envelopePoint.x,envelopeRect.Min.y-100),true);check(path.GetModifiers()[0].Envelope[1].Value==1,"Envelope dragging is clamped to the visible strength range");io.AddKeyEvent(ImGuiKey_Escape,true);frame(envelopePoint,true);check(path.CaptureState()==beforeEnvelope,"Escape restores a modifier-strength edit exactly");io.AddKeyEvent(ImGuiKey_Escape,false);frame(envelopePoint,false);frame();
 ImGui::SetScrollY(modifierChild,0);frame();button("New modifier type");auto *typePopup=ImGui::FindWindowByName("##Combo_00");check(typePopup&&typePopup->Active,"Modifier type chooser opens");ImVec2 typeChoice(typePopup->DC.CursorStartPos.x+50,typePopup->DC.CursorStartPos.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);frame(typeChoice);frame(typeChoice,true);frame(typeChoice,false);frame();g_ModifierBegin=10;g_ModifierEnd=12;ImGui::SetScrollY(modifierChild,0);frame();check(widgets.count("Add oscillation to range"),"Oscillation is exposed as a normal range modifier");button("Add oscillation to range");check(path.GetModifiers().size()==2&&path.GetModifiers()[1].Type==CamPath::ModifierType::Oscillation,"UI creates oscillation through native modifier validation");
 // The visible list follows actual evaluator order; selected parameter edits are atomic and undoable.
 check(widgets.count("0: noise")&&widgets.count("1: oscillation"),"Modifier stack shows both entries in evaluation order");
 auto revealModifierWidget=[&](const char*name){frame();auto r=widgets.at(name);ImGui::SetScrollY(modifierChild,modifierChild->Scroll.y+r.Min.y-modifierChild->InnerRect.Min.y-35);frame();frame();};
 revealModifierWidget("X amplitude (units)");auto xr=widgets.at("X amplitude (units)"),yr=widgets.at("Y amplitude (units)");
 ImVec2 xp(xr.Min.x+30,(xr.Min.y+xr.Max.y)/2),yp(yr.Min.x+30,(yr.Min.y+yr.Max.y)/2);
 auto beforeFieldSelection=path.CaptureState();io.AddKeyEvent(ImGuiMod_Shift,true);frame(xp);frame(xp,true);frame(yp,true);frame(yp,false);io.AddKeyEvent(ImGuiMod_Shift,false);frame();
 check(g_ModifierFields.Selected[8]&&g_ModifierFields.Selected[9]&&!g_ModifierFields.Selected[10],"Shift-drag selects numeric fields crossed by the pointer");check(path.CaptureState()==beforeFieldSelection,"Field selection does not change camera data");
 auto typeField=[&](const char*label,const char*text,bool commit){revealModifierWidget(label);auto r=widgets.at(label);ImVec2 p(r.Min.x+30,(r.Min.y+r.Max.y)/2);frame(p);frame(p,true);frame(p,false);io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddInputCharactersUTF8(text);frame();if(commit){io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();frame();}};
 auto beforeBatch=path.CaptureState();typeField("X amplitude (units)","6.5",false);check(path.CaptureState()==beforeBatch,"Typing does not rebuild modifier playback before Enter");
 io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();frame();check(path.GetModifiers()[1].Amplitude[0]==6.5&&path.GetModifiers()[1].Amplitude[1]==6.5&&path.GetModifiers()[1].Amplitude[2]==0,"Enter sets all selected fields to the typed absolute value");
 HistoryUndo(false);check(path.CaptureState()==beforeBatch,"Undo restores a complete typed multi-field edit in one step");HistoryUndo(true);check(path.GetModifiers()[1].Amplitude[1]==6.5,"Redo restores all typed field values");
 auto beforeCancelType=path.CaptureState();typeField("X amplitude (units)","3",false);io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();check(path.CaptureState()==beforeCancelType,"Escape cancels uncommitted numeric text");
 g_ModifierFields.Selected[0]=true;auto invalidBatch=path.CaptureState();typeField("X amplitude (units)","2",true);check(path.CaptureState()==invalidBatch,"One invalid selected field rejects the complete typed batch");
 g_ModifierFields.Selected[0]=false;typeField("X amplitude (units)","3",false);auto externalMods=path.GetModifiers();externalMods[1].Amplitude[2]=7;path.SetModifiers(externalMods);frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();check(path.GetModifiers()[1].Amplitude[0]==6.5&&path.GetModifiers()[1].Amplitude[2]==7,"An external edit cancels stale numeric input");
 revealModifierWidget("Move up");button("Move up");check(path.GetModifiers()[0].Type==CamPath::ModifierType::Oscillation&&g_ModifierSelection==0,"Moving a stack entry changes native evaluation order and selection together");
 HistoryUndo(false);check(path.GetModifiers()[0].Type==CamPath::ModifierType::Noise,"Stack ordering has ordinary undo");
 // Rejected additions do not leave a ghost entry in the settings draft.
 g_ModifierBegin=g_ModifierEnd=10;ImGui::SetScrollY(modifierChild,0);frame();frame();auto validStack=path.GetModifiers();button("Add oscillation to range");check(path.GetModifiers()==validStack&&!widgets.count("2: oscillation"),"An invalid range cannot create an editable ghost modifier");
 // Presets append ordinary settings, participate in history, and expose editable timing controls.
 g_ModifierBegin=10;g_ModifierEnd=12;revealModifierWidget("Add preset to range");button("Add preset to range");check(path.GetModifiers().size()==3&&path.GetModifiers()[2].Type==CamPath::ModifierType::Noise&&path.GetModifiers()[2].Octaves==3,"Handheld preset appends editable noise settings");
 HistoryUndo(false);check(path.GetModifiers()==validStack,"Preset creation is one native undo step");HistoryUndo(true);check(path.GetModifiers().size()==3,"Preset creation redoes");HistoryUndo(false);
 revealModifierWidget("New modifier type");button("New modifier type");typePopup=ImGui::FindWindowByName("##Combo_00");typeChoice=ImVec2(typePopup->DC.CursorStartPos.x+50,typePopup->DC.CursorStartPos.y+4*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);frame(typeChoice);frame(typeChoice,true);frame(typeChoice,false);frame();
 revealModifierWidget("Add expo to range");button("Add expo to range");check(path.GetModifiers().size()==3&&path.GetModifiers()[2].Type==CamPath::ModifierType::Expo,"UI adds a range-wide Expo timing modifier");
 revealModifierWidget("Easing direction");button("Easing direction");typePopup=ImGui::FindWindowByName("##Combo_00");typeChoice=ImVec2(typePopup->DC.CursorStartPos.x+50,typePopup->DC.CursorStartPos.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);frame(typeChoice);frame(typeChoice,true);frame(typeChoice,false);frame();check(path.GetModifiers()[2].Direction==CamPath::EaseDirection::Out,"Timing direction is editable in the overlay");
 typeField("Expo power","8",true);check(path.GetModifiers()[2].Power==8,"Expo power commits through validated numeric editing");
 path.SetModifiers(validStack);g_ModifierSelection=0;FinishEdit();frame();frame();
 // Control modifiers use the same validated fields and multi-field editing as existing effects.
 CamPath::Modifier control;control.Type=CamPath::ModifierType::Limits;control.Begin=10;control.End=12;control.Amplitude[3]=1;auto controlStack=validStack;controlStack.push_back(control);path.SetModifiers(controlStack);g_ModifierSelection=2;frame();frame();
 typeField("Lower bound","60",true);typeField("Upper bound","110",true);check(path.GetModifiers()[2].Lower==60&&path.GetModifiers()[2].Upper==110,"Value limit controls commit through native validation");
 controlStack=path.GetModifiers();controlStack[2].Type=CamPath::ModifierType::Remap;path.SetModifiers(controlStack);frame();frame();typeField("Value scale","1.5",true);typeField("Value offset","5",true);check(path.GetModifiers()[2].Scale==1.5&&path.GetModifiers()[2].Offset==5,"Value remap exposes editable scale and offset");
 path.SetModifiers(validStack);g_ModifierSelection=0;FinishEdit();frame();frame();
 // Control presets are discoverable in the chooser and append their advertised channel settings.
 g_ModifierBegin=10;g_ModifierEnd=12;revealModifierWidget("Modifier preset");button("Modifier preset");auto *presetPopup=ImGui::FindWindowByName("##Combo_00");check(presetPopup&&presetPopup->Active,"Control preset chooser opens");ImGui::SetScrollY(presetPopup,presetPopup->ScrollMax.y);frame();frame();
 ImVec2 presetChoice(presetPopup->DC.CursorStartPos.x+50,presetPopup->DC.CursorStartPos.y+15*ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);frame(presetChoice);frame(presetChoice,true);frame(presetChoice,false);frame();revealModifierWidget("Add preset to range");button("Add preset to range");check(path.GetModifiers().size()==3&&path.GetModifiers()[2].Type==CamPath::ModifierType::Remap&&path.GetModifiers()[2].Scale==.5&&path.GetModifiers()[2].Amplitude[6]==1&&path.GetModifiers()[2].Amplitude[0]==0,"Half roll preset selects only roll and remains editable");
 HistoryUndo(false);check(path.GetModifiers()==validStack,"Control preset appends as one undoable edit");g_ModifierSelection=0;frame();frame();
 // Large stacks have a bounded strip, and range drawing takes priority over modifier handles.
 std::vector<CamPath::Modifier> largeStack(64);for(auto&m:largeStack){m.Begin=10;m.End=12;m.Enabled=false;}check(path.SetModifiers(largeStack),"Prepare maximum-size modifier stack");g_ModifierSelection=63;frame();frame();size_t visibleEdges=0;for(const auto&w:widgets)if(w.first.find("##modifier_edge_")==0)++visibleEdges;check(visibleEdges<=16&&widgets.count("##modifier_edge_63_0"),"Graph shows a bounded group containing the selected stack entry");
 g_ModifierRangeTool=true;auto beforeRangeDraw=path.CaptureState();frame();visibleEdges=0;for(const auto&w:widgets)if(w.first.find("##modifier_edge_")==0)++visibleEdges;check(visibleEdges==0,"Range tool does not compete with modifier handle hit targets");
 ImVec2 rangeClick(graphRect.Min.x+g_CurvePadding,graphRect.Min.y+10);frame(rangeClick);frame(rangeClick,true);frame(ImVec2(rangeClick.x+60,rangeClick.y),true);frame(rangeClick,false);check(path.CaptureState()==beforeRangeDraw,"Drawing a new range leaves existing stack entries unchanged");g_ModifierRangeTool=false;path.SetModifiers(validStack);g_ModifierSelection=0;
 // Zoomed views do not submit hit targets for offscreen keys.
 ImGui::SetScrollY(modifierChild,0);frame();envelopeWindow->StateStorage.SetInt(envelopeWindow->GetID("Range modifiers"),0);g_CurveFocusChannel=0;g_CurveTimeZoom=10;g_CurveTimeCenter=.5;frame();frame();
 check(!widgets.count("##curve_key_0_ch0")&&!widgets.count(("##curve_key_"+std::to_string(path.GetSize()-1)+"_ch0").c_str()),"Offscreen graph keys do not create interactive widgets");g_CurveTimeZoom=1;frame();
 ImGui::SetWindowPos("Campath Graph",ImVec2(580,50));
 g_Hook_VClient_RenderView.m_CamPaths.RestoreState(documentBeforeSpeed);g_MirvTime.Time=clockBeforeSpeed;g_ShowCurveEditor=false;g_ShowSequencer=true;FinishEdit();frame();frame();
 button("Observing");check(widgets.count("1: Scout"),"TF2 player list is visible");check(!widgets.count("View")&&!widgets.count("Attach")&&!widgets.count("Bones"),"Player list has no row buttons");
 MirvCampathPreview_SetActive(true);path.Enabled_set(true);button("1: Scout");check(commands.back().find("spec_player \"#42\"")!=std::string::npos,"Player click uses explicit user ID, not entity slot or numeric name");check(commands.back().find("spec_autodirector 0")!=std::string::npos&&commands.back().find("spec_mode 4")!=std::string::npos&&commands.back().find("mirv_cam attach clear")!=std::string::npos,"Native spectating clears attachment and selects in-eye view");check(!MirvCampathPreview_IsActive()&&!path.Enabled_get(),"Spectating releases HLAE path overrides");size_t commandsBefore=commands.size();engine.Hltv=false;button("1: Scout");check(commands.size()==commandsBefore,"POV demo reports unsupported native spectating without issuing misleading commands");engine.Hltv=true;check(widgets.count("Bone"),"Selecting player exposes named bones");button("Bone");check(widgets.count("1: bip_head"),"Bone picker lists model bone names");button("1: bip_head");button("Attach orientation");button("world");check(commands.back()=="mirv_cam attachMode world","World orientation selection queues the existing attachment command");button("Attach selected");check(widgets.count("Attach orientation"),"Observing exposes attachment orientation modes");check(commands.back().find("mirv_cam attach 1 1")!=std::string::npos,"Named bone selection drives native attachment");button("Trace entity");check(widgets.count("Attach selected"),"Trace selects entity in observing panel");tracedEntity=-1;button("Trace entity");check(widgets.count("Attach selected"),"No-hit trace preserves existing selection");tracedEntity=7;button("Trace entity");button("Attach selected");check(commands.back().find("mirv_cam attach 7 -1")!=std::string::npos,"Trace can select and attach non-player entities");traceAvailable=false;button("Trace entity");check(widgets.count("Attach selected"),"Missing trace hook preserves selected entity");
 button("Recording");check(widgets.count("Start Recording"),"TF2 screen recording panel is available");button("Start Recording");check(!IsOpen(),"Recording closes the overlay before capture");check(commands.back().find("mirv_streams record screen enabled 1")!=std::string::npos,"Recording uses TF2 screen capture");
 ImGui::DestroyContext();std::cout<<checks<<" headless Paper overlay checks passed\n";
 }catch(const std::exception&e){std::cerr<<"After "<<checks<<" UI checks: "<<e.what()<<"\n";return 1;}}
'''
collision_tests=r"""
void CollisionTests(){
 auto cfg=MirvCameraCollisionGet();check(!cfg.Free&&!cfg.Campath,"Camera collisions default off independently");
 const double seed[3]={0,0,0};int pathId=1;float x=20,y=0,z=0;int before=collisionTrace.Queries;
 check(!MirvCameraCollisionApply(true,nullptr,0,1,1,seed,x,y,z)&&collisionTrace.Queries==before&&x==20,"Disabled free collision never traces or alters the view");
 check(!MirvCameraCollisionApply(false,&pathId,0,1,1,seed,x,y,z)&&collisionTrace.Queries==before,"Disabled campath collision never traces");
 cfg.Free=true;cfg.Radius=1;cfg.Slide=false;collisionFactoryAvailable=false;check(!MirvCameraCollisionSet(cfg),"Missing TF2 trace interface rejects enable");collisionFactoryAvailable=true;
 check(MirvCameraCollisionSet(cfg),"TF2 collision settings enable");collisionTrace.Boxes={{{10,-100,-100},{11,100,100},nullptr}};
 check(MirvCameraCollisionApply(true,nullptr,0,1,1,seed,x,y,z)&&x>8.9&&x<9,"First free-camera frame sweeps a hull through a thin wall");
 check(collisionTrace.Mask==0x200400B&&!collisionTrace.IsRay&&collisionTrace.Radius==1,"Collision reuses the solid hull-trace ABI and clearance");
 x=20;MirvCameraCollisionApply(true,nullptr,0,1.01,1.01,seed,x,y,z);check(x<9,"Repeated input cannot accumulate movement behind a wall");
 cfg.Slide=true;MirvCameraCollisionSet(cfg);x=20;y=5;z=0;MirvCameraCollisionApply(true,nullptr,0,2,2,seed,x,y,z);check(x<9&&std::abs(y-5)<1e-4,"Slide retains motion along a wall");
 cfg.Slide=false;MirvCameraCollisionSet(cfg);MirvCameraCollisionReset();x=20;y=5;z=0;MirvCameraCollisionApply(true,nullptr,0,2,2,seed,x,y,z);check(x<9&&y<3,"Stop response stops the complete movement");
 const float stoppedX=x,stoppedY=y;for(int i=0;i<50;++i){x=20;y=5;z=0;MirvCameraCollisionApply(true,nullptr,0,2,2,seed,x,y,z);}check(std::abs(x-stoppedX)<.001&&std::abs(y-stoppedY)<.001,"A fixed paused target cannot creep along a wall in stop mode");
 cfg.Slide=true;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.push_back({{-100,10,-100},{100,11,100},nullptr});x=20;y=20;z=0;MirvCameraCollisionApply(true,nullptr,0,2,2,seed,x,y,z);if(!(x<9&&y<9))std::cerr<<"Corner pose "<<x<<","<<y<<","<<z<<"\n";check(x<9&&y<9,"Multiple clipping planes keep the camera outside a corner");
 cfg.Free=false;cfg.Campath=true;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.resize(1);x=20;y=z=0;before=collisionTrace.Queries;
 check(!MirvCameraCollisionApply(true,nullptr,0,3,3,seed,x,y,z)&&x==20&&collisionTrace.Queries==before,"Campath switch does not enable free-camera collision");
 MirvCameraCollisionApply(false,&pathId,0,3,3,seed,x,y,z);check(x==20,"A new shot may start at a clear pose without sweeping from the game camera");
 x=0;MirvCameraCollisionApply(false,&pathId,0,3.01,3.01,seed,x,y,z);check(x>12&&x<12.1,"Continuous campath playback sweeps from the previous corrected position");
 cfg.Slide=false;MirvCameraCollisionSet(cfg);x=0;MirvCameraCollisionApply(false,&pathId,0,3.02,3.02,seed,x,y,z);check(x>12,"Changing collision response cannot teleport a blocked path through a wall");
 x=0;MirvCameraCollisionApply(false,&pathId,0,2,2,seed,x,y,z);check(x==0,"Backward seeking discards the old camera collision anchor");
 x=20;MirvCameraCollisionApply(false,&pathId,1,2.01,2.01,seed,x,y,z);check(x==20,"Editing geometry discards stale collision history");
 x=0;MirvCameraCollisionApply(false,&pathId,1,2.02,7,seed,x,y,z);check(x==0,"Preview jumps do not sweep between unrelated cursor positions");
 cfg.Free=true;cfg.Campath=false;cfg.Players=true;MirvCameraCollisionSet(cfg);collisionTrace.Boxes={{{10,-10,-10},{11,10,10},(void*)2}};x=20;y=z=0;MirvCameraCollisionApply(true,nullptr,0,4,4,seed,x,y,z);check(x<9,"Other players use their solid collision hulls");
 cfg.Players=false;MirvCameraCollisionSet(cfg);x=20;MirvCameraCollisionApply(true,nullptr,0,4,4,seed,x,y,z);check(x==20,"Player collision can be disabled while retaining solid geometry");
 collisionTrace.Boxes[0].Entity=(void*)3;MirvCameraCollisionReset();x=20;MirvCameraCollisionApply(true,nullptr,0,4,4,seed,x,y,z);check(x<9,"Disabling players does not exclude solid props");
 collisionTrace.Boxes[0].Entity=(void*)1;cfg.Players=true;MirvCameraCollisionSet(cfg);x=20;MirvCameraCollisionApply(true,nullptr,0,4,4,seed,x,y,z);check(x==20,"The local recorder is ignored so a POV camera can leave its player hull");
 collisionTrace.Boxes.clear();MirvCameraCollisionReset();x=y=z=0;MirvCameraCollisionApply(true,nullptr,0,5,5,seed,x,y,z);collisionTrace.Boxes={{{-.5,-.5,-.5},{.5,.5,.5},(void*)2}};
 MirvCameraCollisionApply(true,nullptr,0,5.01,5.01,seed,x,y,z);check(std::abs(x)>1.5||std::abs(y)>1.5||std::abs(z)>1.5,"A moving player engulfing the camera triggers bounded overlap recovery");
 collisionTrace.Boxes={{{-1000,-1000,-1000},{1000,1000,1000},nullptr}};MirvCameraCollisionReset();x=y=z=0;before=collisionTrace.Queries;MirvCameraCollisionApply(true,nullptr,0,6,6,seed,x,y,z);check(x==0&&y==0&&z==0&&collisionTrace.Queries-before<=26,"Deeply embedded starts have a bounded recovery budget");
 const auto valid=MirvCameraCollisionGet();for(double bad:{0.0,65.0,std::numeric_limits<double>::infinity()}){auto invalid=valid;invalid.Radius=bad;check(!MirvCameraCollisionSet(invalid)&&MirvCameraCollisionGet().Radius==valid.Radius,"Invalid radius rejects atomically");}
 g_SourceSdkVer=0;check(!MirvCameraCollisionSet(valid),"Unsupported Source games reject enabling TF2 collision");g_SourceSdkVer=SourceSdkVer_TF2Classified;check(MirvCameraCollisionSet(valid),"TF2C uses the same verified trace adapter");g_SourceSdkVer=SourceSdkVer_TF2;
 collisionTrace.Boxes.clear();x=1;y=2;z=3;collisionToolsAvailable=false;check(!MirvCameraCollisionApply(true,nullptr,0,7,7,seed,x,y,z)&&x==1,"Missing client tools leaves the view unchanged");collisionToolsAvailable=true;
 collisionCommand({"mirv_cam","collision","free","0"});check(!MirvCameraCollisionGet().Free,"Console independently disables free collision");collisionCommand({"mirv_cam","collision","campath","1"});check(MirvCameraCollisionGet().Campath,"Console independently enables campath collision");collisionCommand({"mirv_cam","collision","radius","nan"});check(MirvCameraCollisionGet().Radius==valid.Radius,"Console rejects non-finite clearance");collisionCommand({"mirv_cam","collision","response","stop"});check(!MirvCameraCollisionGet().Slide,"Console selects stop response");
 cfg.Free=true;cfg.Campath=false;cfg.Slide=true;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.clear();x=3;y=z=0;int adopted=input.CollisionAdoptions;CollisionView(true,nullptr,8,8,seed,x,y,z);check(input.CollisionAdoptions==adopted,"Unobstructed collision does not reset free-camera smoothing or input state");
 collisionTrace.Boxes={{{10,-100,-100},{11,100,100},nullptr}};MirvCameraCollisionReset();x=20;y=z=0;CollisionView(true,nullptr,8,8,seed,x,y,z);check(input.CollisionAdoptions==adopted+1&&input.Pose[0]==x&&x<9,"The final collision stage feeds back only a corrected free-camera position");
 const double beforeOffset[3]={5,0,0};MirvCameraCollisionReset();x=15;y=z=0;CollisionView(true,nullptr,8,8,seed,x,y,z,beforeOffset);check(std::abs(input.Pose[0]+10-x)<.0001,"Collision feedback preserves the pre-offset free-camera origin instead of accumulating camera offsets");
 engine.InGame=false;x=20;before=collisionTrace.Queries;check(!MirvCameraCollisionApply(true,nullptr,0,9,9,seed,x,y,z)&&x==20&&collisionTrace.Queries==before,"Disconnected views do not trace or alter the camera");engine.InGame=true;engine.Loading=true;check(!MirvCameraCollisionApply(true,nullptr,0,9,9,seed,x,y,z)&&x==20,"Loading views suspend collision");engine.Loading=false;
 MirvCameraCollisionReset();x=1000000;y=z=0;check(MirvCameraCollisionApply(true,nullptr,0,10,10,seed,x,y,z)&&x<9,"Large free-camera requests are bounded before the engine trace and cannot tunnel through a thin wall");
 before=collisionTrace.Queries;x=std::numeric_limits<float>::quiet_NaN();check(!MirvCameraCollisionApply(true,nullptr,0,10,10,seed,x,y,z)&&collisionTrace.Queries==before,"Non-finite camera targets are rejected before reaching native tracing");
 collisionCommand({"mirv_cam","collision","free","2"});check(MirvCameraCollisionGet().Free,"Invalid boolean commands retain accepted settings");
 MirvCameraCollisionSettings off;MirvCameraCollisionSet(off);collisionTrace.Boxes.clear();MirvCameraCollisionReset();
}
"""
collision_tests+=r"""
void AttachedFollowTests(){
 const double seed[3]={0,0,0};int path=1,attachment=2;float x=0,y=0,z=0;
 auto cfg=MirvCameraCollisionGet();cfg.Radius=1;cfg.Free=true;cfg.Players=false;cfg.IgnoreLocal=false;cfg.Slide=false;
 check(MirvCameraCollisionSet(cfg),"Local-player collision can be enabled separately from other players");
 collisionTrace.Boxes={{{10,-100,-100},{11,100,100},(void*)1}};MirvCameraCollisionReset();x=20;
 MirvCameraCollisionApply(true,nullptr,0,1,1,seed,x,y,z);check(x<9,"Disabling local exclusion includes the recorder even with other players disabled");
 cfg.IgnoreLocal=true;MirvCameraCollisionSet(cfg);x=20;MirvCameraCollisionApply(true,nullptr,0,1.01,1.01,seed,x,y,z);check(x==20,"Local exclusion remains an independent opt-out");
 cfg.Free=false;cfg.Campath=true;cfg.ReturnHalfLife=.2;cfg.ReturnSpeed=10;MirvCameraCollisionSet(cfg);MirvCameraCollisionReset();collisionTrace.Boxes={{{10,-100,-100},{11,100,100},nullptr}};
 x=0;MirvCameraCollisionApply(false,&path,0,1,1,seed,x,y,z);x=20;MirvCameraCollisionApply(false,&path,0,1.01,1.01,seed,x,y,z);const double blocked=x;check(blocked<9,"Set up a blocked campath for controlled return");
 collisionTrace.Boxes.clear();x=20;MirvCameraCollisionApply(false,&path,0,2,2,seed,x,y,z);check(x>blocked&&x<=blocked+1.001,"A blocked path cannot teleport to its goal after a forward stall");
 const double paused=x;x=20;MirvCameraCollisionApply(false,&path,0,2,2,seed,x,y,z);check(std::abs(x-paused)<1e-6,"Paused campath recovery does not advance");
 collisionTrace.Boxes={{{14,-100,-100},{15,100,100},nullptr}};
 double previous=x;
 for(int i=1;i<=200;++i){if(i==100)collisionTrace.Boxes.clear();x=20;MirvCameraCollisionApply(false,&path,0,2+i*.05,2+i*.05,seed,x,y,z);check(x-previous<=.501&&x>=previous-1e-5,"Return movement obeys its speed cap");if(i<100)check(x<13.001,"Return movement is still collision checked against a second obstacle");previous=x;}
 check(std::abs(x-20)<.001,"Controlled return eventually rejoins the authored path");
 MirvCameraCollisionReset();x=20;MirvCameraCollisionApply(false,&path,0,0,0,seed,x,y,z);check(x==20,"An explicit seek resets old catch-up state");
 cfg.Campath=false;cfg.Attached=true;cfg.IgnoreLocal=false;cfg.Players=false;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();
 collisionTrace.Boxes={{{-1,-1,-1},{1,1,1},(void*)2},{{10,-100,-100},{11,100,100},nullptr}};x=20;y=z=0;
 check(MirvCameraAttachedApply(&attachment,(void*)2,seed,1,x,y,z)&&x>8&&x<9,"An attached camera retracts its boom toward the pivot, ignoring its subject");
 collisionTrace.Boxes[1].Min[0]=5;x=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,1.01,x,y,z);check(x<4,"Attachment contraction is immediate even when the return speed is low");
 const double contracted=x;collisionTrace.Boxes.resize(1);x=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,1.02,x,y,z);check(x>contracted&&x<=contracted+.101,"Attachment extension is gradual after the wall clears");
 const double movingPivot[3]={5,0,0};const double previousOffset=x;x=25;MirvCameraAttachedApply(&attachment,(void*)2,movingPivot,1.03,x,y,z);check(x>5&&x-5<=previousOffset+.101,"Boom extension follows a moving pivot instead of imposing the return cap on subject movement");
 collisionTrace.Boxes={{{10,-100,-100},{11,100,100},(void*)1}};MirvCameraAttachedReset();x=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,2,x,y,z);check(x<9,"Attached collision includes a different local player when requested");
 collisionTrace.Boxes={{{-1,-1,-1},{1,1,1},(void*)1}};MirvCameraAttachedReset();x=20;MirvCameraAttachedApply(&attachment,(void*)1,seed,2,x,y,z);check(x==20,"The local subject does not block its own pivot sweep when the endpoint is clear");
 collisionTrace.Boxes.clear();MirvCameraAttachedReset();x=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,3,x,y,z);const double movedPivot[3]={1,0,0};x=21;MirvCameraAttachedApply(&attachment,(void*)2,movedPivot,3.01,x,y,z);check(x==21,"Collision alone does not add lag to an unobstructed rigid attachment");
 cfg.Attached=false;cfg.FollowHalfLife=.2;cfg.FollowMaxDistance=128;MirvCameraCollisionSet(cfg);const int queries=collisionTrace.Queries;
 auto simulate=[&](int fps){MirvCameraAttachedReset();x=y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,1,x,y,z);for(int i=1;i<=fps;++i){const double p[3]={10.0*i/fps,0,0};x=float(p[0]);MirvCameraAttachedApply(&attachment,(void*)2,p,1+double(i)/fps,x,y,z);}return double(x);};
 const double low=simulate(30),high=simulate(120);check(low>0&&low<10&&std::abs(low-high)<.13,"Smooth follow remains consistent across 30 and 120 FPS target sampling");
 check(collisionTrace.Queries==queries,"Positional follow can run without collision traces");
 const double endPivot[3]={10,0,0};const double frozen=x;for(int i=0;i<10;++i){x=10;MirvCameraAttachedApply(&attachment,(void*)2,endPivot,2,x,y,z);}check(std::abs(x-frozen)<1e-5,"Demo-time positional follow freezes while paused");
 cfg.FollowMaxDistance=2;MirvCameraCollisionSet(cfg);const double farPivot[3]={100,0,0};x=100;MirvCameraAttachedApply(&attachment,(void*)2,farPivot,2.01,x,y,z);check(x>=98,"Maximum positional lag is bounded");
 x=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,1,x,y,z);check(x==0,"Backward seeking resets positional follow");
 int other=3;x=50;MirvCameraAttachedApply(&other,(void*)3,seed,1.01,x,y,z);check(x==50,"Changing the attachment resets positional follow");
 // Rotation is never filtered, whether follow is on or the boom is recovering.
 cfg.FollowHalfLife=.2;cfg.FollowMaxDistance=128;cfg.Attached=false;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();
 x=20;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,4,x,y,z);
 x=0;y=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,4.01,x,y,z);check(x>19&&y>0&&y<2,"Follow turns its orbit gradually at a stationary pivot");
 const double pivot2[3]={10,0,0};x=10;y=20;MirvCameraAttachedApply(&attachment,(void*)2,pivot2,4.02,x,y,z);check(x>0&&x<21&&y>0&&y<5,"Root follow and orbit response advance together without an instantaneous orbit jump");
 cfg.FollowHalfLife=0;cfg.Attached=true;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();
 collisionTrace.Boxes={{{10,-100,-100},{11,100,100},nullptr}};x=20;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,5,x,y,z);const double arm=x;
 collisionTrace.Boxes.clear();x=0;y=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,5.01,x,y,z);check(std::abs(x)<1e-6&&y>arm&&y<=arm+.101,"Collision recovery caps boom length but turns immediately with the entity");
 // Include the attached subject at the endpoint even when all player exclusions are active.
 cfg.IgnoreLocal=true;cfg.Players=false;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();
 collisionTrace.Boxes={{{-2,-2,-2},{2,2,2},(void*)1}};x=1;y=z=0;MirvCameraAttachedApply(&attachment,(void*)1,seed,6,x,y,z);
 MirvTf2Trace_t subject;
 const double endpoint[3]={x,y,z};CameraCollisionTrace(endpoint,endpoint,cfg,subject,nullptr,(void*)1);check(!subject.startSolid&&!subject.allSolid&&x>3,"A close local-subject camera is pushed outside its hull");
 collisionTrace.Boxes={{{-2,-100,-100},{2,100,100},(void*)2},{{4,-100,-100},{5,100,100},nullptr}};MirvCameraAttachedReset();x=20;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,7,x,y,z);
 check(x<3.001,"A trapped camera cannot be pushed through a wall to escape the subject");
 // Open physical doors and invisible player-only barriers have different rules.
 collisionClasses={{(void*)3,"class C_FuncRespawnRoomVisualizer"},{(void*)4,"func_door"}};
 collisionTrace.Boxes={{{8,-100,-100},{9,100,100},(void*)3},{{10,-100,-100},{11,100,100},(void*)4}};MirvCameraAttachedReset();x=20;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,8,x,y,z);check(x>8&&x<9,"Camera ignores the spawn team barrier but collides with the closed physical door");
 collisionTrace.Boxes[1].Min[2]=100;collisionTrace.Boxes[1].Max[2]=200;MirvCameraAttachedReset();x=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,8.01,x,y,z);check(x==20,"An open spawn door no longer leaves its invisible team barrier blocking the camera");
 collisionClasses[0].second="func_respawnroomvisualizer";MirvCameraAttachedReset();x=20;MirvCameraAttachedApply(&attachment,(void*)2,seed,8.02,x,y,z);check(x==20,"Spawn barrier filtering also accepts the mapped classname");
 collisionClasses.clear();collisionTrace.Boxes.clear();
 // A missing snapshot is a step in the target, not a request to jump the camera.
 cfg.Attached=false;cfg.FollowHalfLife=.2;cfg.BoneHalfLife=0;cfg.FollowMaxDistance=1000;MirvCameraCollisionSet(cfg);
 auto snapshot=[&](int fps){MirvCameraAttachedReset();x=y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,10,x,y,z);double old=0;const double goal[3]={20,0,0};for(int i=1;i<=fps;++i){x=20;MirvCameraAttachedApply(&attachment,(void*)2,goal,10+double(i)/fps,x,y,z);check(x>=old&&x<20,"A snapshot correction settles smoothly without oscillation or snapping");old=x;}return double(x);};
 const double step30=snapshot(30),step120=snapshot(120);check(std::abs(step30-step120)<1e-5,"A stationary-target correction has the same response at 30 and 120 FPS");
 // Bone animation has its own filter and can be damped without delaying the entity root.
 cfg.FollowHalfLife=0;cfg.BoneHalfLife=.1;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();
 const double head[3]={0,0,72};x=0;y=0;z=72;MirvCameraAttachedApply(&attachment,(void*)2,head,20,x,y,z,seed);
 const double movedRoot[3]={10,0,0},bobbed[3]={10,0,82};x=10;y=0;z=82;MirvCameraAttachedApply(&attachment,(void*)2,bobbed,20.01,x,y,z,movedRoot);check(x==10&&z>72&&z<73,"Bone damping softens head bob while rigid root translation remains immediate");
 const double bonePaused=z;x=10;z=82;MirvCameraAttachedApply(&attachment,(void*)2,bobbed,20.01,x,y,z,movedRoot);check(std::abs(z-bonePaused)<1e-5,"Bone damping freezes with demo time");
 cfg.FollowHalfLife=.15;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();x=0;z=72;MirvCameraAttachedApply(&attachment,(void*)2,head,21,x,y,z,seed);x=10;z=82;MirvCameraAttachedApply(&attachment,(void*)2,bobbed,21.01,x,y,z,movedRoot);check(x>0&&x<1&&z>72&&z<73,"Root follow and bone damping work independently in combination");
 cfg.FollowTeleportDistance=50;MirvCameraCollisionSet(cfg);const double teleRoot[3]={100,0,0},teleHead[3]={100,0,72};x=100;z=72;MirvCameraAttachedApply(&attachment,(void*)2,teleHead,21.02,x,y,z,teleRoot);check(x==100&&z==72,"A large entity teleport resets both filters");
 cfg.FollowTeleportDistance=0;MirvCameraCollisionSet(cfg);x=0;z=72;MirvCameraAttachedApply(&attachment,(void*)2,head,21.03,x,y,z,seed);check(x>90,"Teleport distance reset can be disabled deliberately");
 MirvCameraAttachedReset();x=0;z=72;MirvCameraAttachedApply(&attachment,(void*)2,head,22,x,y,z,seed);cfg.Attached=true;MirvCameraCollisionSet(cfg);collisionTrace.Boxes={{{8,-100,-100},{9,100,100},nullptr}};x=10;z=82;MirvCameraAttachedApply(&attachment,(void*)2,bobbed,22.01,x,y,z,movedRoot);check(x<=7&&std::isfinite(x),"Actual camera movement cannot teleport through a wall with a moving target");
 collisionTrace.Boxes.clear();cfg.Attached=false;cfg.FollowHalfLife=1e-300;cfg.BoneHalfLife=1e-300;MirvCameraCollisionSet(cfg);x=10;z=82;MirvCameraAttachedApply(&attachment,(void*)2,bobbed,22.02,x,y,z,movedRoot);check(std::isfinite(x)&&std::isfinite(z)&&x==10&&z==82,"Tiny positive smoothing values stay finite and behave as rigid attachment");
 cfg.FollowHalfLife=.2;cfg.BoneHalfLife=0;cfg.FollowTeleportDistance=1024;MirvCameraCollisionSet(cfg);
 // Backstepping/strafe/turn sequences must retain the captured side and composition.
 cfg.Attached=false;cfg.FollowHalfLife=.2;cfg.BoneHalfLife=.15;cfg.FollowMaxDistance=128;cfg.FollowTeleportDistance=1024;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.clear();
 for(int fps:{30,120}){MirvCameraAttachedReset();for(int i=0;i<=2*fps;++i){double t=double(i)/fps;const double r[3]={-300*t,80*std::sin(t*2),0},h[3]={r[0],r[1],72};x=float(r[0]-60);y=float(r[1]);z=72;float a[3]={0,0,0};MirvCameraAttachedApply(&attachment,(void*)2,h,30+t,x,y,z,r,a);const double dx=r[0]-x,dy=r[1]-y;check(dx>=29.99&&std::hypot(dx,dy)<=100.01,"Follow cannot carry a backstepping subject through the captured camera");check(std::abs(std::remainder(a[1]-std::atan2(dy,dx)*57.29577951308232,360.0))<40&&std::abs(a[2])<.001,"Strafing follow keeps the subject in frame without turning root noise into horizon roll");}}
 MirvCameraAttachedReset();for(int i=0;i<=360;++i){double t=i/120.0,yaw=179+i/3.0,angle=yaw/57.29577951308232;const double r[3]={80*t,0,0},h[3]={r[0],0,72};x=float(r[0]-60*std::cos(angle));y=float(-60*std::sin(angle));z=72;float a[3]={0,float(std::remainder(yaw,360.0)),0};MirvCameraAttachedApply(&attachment,(void*)2,h,40+t,x,y,z,r,a);const double dx=r[0]-x,dy=-y;check(dx*std::cos(angle)+dy*std::sin(angle)>29.99,"Turning follow retains its captured side across yaw wrapping");check(std::abs(std::remainder(a[1]-std::atan2(dy,dx)*57.29577951308232,360.0))<30&&std::abs(a[2])<.001,"Turning follow retains framing across yaw wrapping");}
 // A real soft approach must stay continuous, without relying on sudden overlap recovery.
 cfg.FollowHalfLife=0;cfg.BoneHalfLife=0;cfg.Attached=true;cfg.Radius=6;cfg.AttachedSoftDistance=24;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();collisionTrace.Boxes={{{-101,-1000,-1000},{-100,1000,1000},nullptr}};
 double oldCamera=-60,oldStep=-1;bool anticipated=false;
 for(int i=0;i<=70;++i){const double r[3]={-double(i),0,0},h[3]={r[0],0,72};x=float(r[0]-60);y=0;z=72;MirvCameraAttachedApply(&attachment,(void*)2,h,50+i*.01,x,y,z,r);if(i){double step=x-oldCamera;check(std::abs(step)<=1.01&&x>=-93.97,"A steady wall approach stays clear and does not hard-cut inward");if(i>2)check(std::abs(step-oldStep)<.1,"Soft collision contraction changes speed progressively");oldStep=step;}if(i==20)anticipated=x>-79.9;oldCamera=x;}
 check(anticipated,"The soft zone starts contracting before the desired camera reaches the wall");
 // Damping must survive the entire collision pipeline, not only an isolated spring.
 cfg.BoneHalfLife=.15;cfg.Radius=1;MirvCameraCollisionSet(cfg);
 auto bob=[&](bool collide){cfg.Attached=collide;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();collisionTrace.Boxes.clear();if(collide)collisionTrace.Boxes.push_back({{-25,-1000,-1000},{-24,1000,1000},nullptr});double lo=1e9,hi=-1e9;for(int i=0;i<=480;++i){double t=i/120.0;const double h[3]={0,0,72+2*std::sin(6.283185307179586*8*t)};x=-60;y=0;z=float(h[2]);MirvCameraAttachedApply(&attachment,(void*)2,h,60+t,x,y,z,seed);if(i>=240){lo=(std::min)(lo,double(z));hi=(std::max)(hi,double(z));}}return hi-lo;};
 const double unblockedBob=bob(false),blockedBob=bob(true);check(unblockedBob<.25&&blockedBob<=unblockedBob*1.05+.001,"Wall retraction does not reintroduce raw bone bob into a damped camera");
 // No arbitrary sideways search when the boom is trapped against its subject.
 cfg.BoneHalfLife=0;cfg.Attached=true;cfg.Radius=1;cfg.AttachedSoftDistance=0;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();collisionTrace.Boxes={{{-2,-100,-100},{2,100,100},(void*)2},{{4,-100,-100},{5,100,100},nullptr}};x=20;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,70,x,y,z);check(y==0&&z==0&&x<=3,"A trapped attachment cannot teleport along another axis to find framing");
 collisionTrace.Boxes.clear();cfg.Attached=false;cfg.FollowHalfLife=.2;MirvCameraCollisionSet(cfg);
 // Moving snapshot noise must remain attenuated even while the lag limiter is active.
 cfg=MirvCameraCollisionSettings{};cfg.FollowHalfLife=.2;cfg.FollowTeleportDistance=0;MirvCameraCollisionSet(cfg);
 for(double speed:{-300.0,0.0,300.0}){
  auto run=[&](bool noise){MirvCameraAttachedReset();std::vector<double>out;for(int i=0;i<=600;++i){double t=i/120.0;const double r[3]={speed*t+(noise?2*std::sin(50.26548245743669*t):0),0,0},h[3]={r[0],0,72};x=float(r[0]-60);y=0;z=72;MirvCameraAttachedApply(&attachment,(void*)2,h,100+t,x,y,z,r);if(i>=360)out.push_back(x);}return out;};
  auto clean=run(false),noisy=run(true);double lo=1e9,hi=-1e9;for(size_t i=0;i<clean.size();++i){lo=(std::min)(lo,noisy[i]-clean[i]);hi=(std::max)(hi,noisy[i]-clean[i]);}check(hi-lo<1.5,"Moving root noise remains filtered with saturated lag constraints");
 }
 // A long rendered frame is not necessarily a seek.
 MirvCameraAttachedReset();x=-60;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,110,x,y,z,seed);const double slow[3]={1,0,0};x=-59;MirvCameraAttachedApply(&attachment,(void*)2,slow,110.26,x,y,z,slow);check(x<-59.1&&x>-60,"A continuous frame above .25 demo seconds retains follow smoothing");
 cfg=MirvCameraCollisionSettings{};cfg.Attached=true;cfg.Radius=6;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();collisionTrace.Boxes={{{20,6.1,-100},{22,10.1,100},(void*)3}};x=60;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,120,x,y,z);collisionTrace.Boxes[0].Min[1]-=.2;collisionTrace.Boxes[0].Max[1]-=.2;x=60;MirvCameraAttachedApply(&attachment,(void*)2,seed,120+1.0/120,x,y,z);check(x>57&&x<60,"A side-entering occluder retracts gradually while the camera hull is clear");
 collisionTrace.Boxes={{{70,-100,-100},{71,100,100},nullptr}};MirvCameraAttachedReset();x=60;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,121,x,y,z);double displayed=x,capture[3]={x,y,z},captureAngles[3]={0,0,0};MirvCameraAttachedCapturePose(&attachment,capture,captureAngles);check(capture[0]==60,"Recapture recovers the authored pose from the displayed correction");MirvCameraAttachedReset();x=float(capture[0]);MirvCameraAttachedApply(&attachment,(void*)2,seed,121,x,y,z);check(std::abs(x-displayed)<1e-5,"Recapture cannot compress an already corrected boom a second time");
 cfg.Radius=1;cfg.AttachedSoftDistance=0;cfg.Slide=false;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();collisionTrace.Boxes={{{-11.3,-5,-100},{-10.2,-1.2,100},nullptr},{{-2,-2,-2},{2,2,2},(void*)2}};x=-10;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,122,x,y,z);const double overlapRoot[3]={-8,2,0};collisionTrace.Boxes[1]={{-10,0,-2},{-6,4,2},(void*)2};x=-18;y=-3;MirvCameraAttachedApply(&attachment,(void*)2,overlapRoot,122.01,x,y,z,overlapRoot);double recovered[3]={x,y,z};MirvTf2Trace_t bodyHit;CameraCollisionTrace(recovered,recovered,cfg,bodyHit,nullptr,(void*)2);check(!bodyHit.startSolid&&CameraCollisionClear(recovered,cfg,(void*)2),"A subject overlapping the previous camera cannot trap stop-mode recovery inside its hull");
 collisionTrace.Boxes={{{-2,-2,-2},{2,2,2},(void*)2}};MirvCameraAttachedReset();x=y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,123,x,y,z);double zeroRecovered[3]={x,y,z};CameraCollisionTrace(zeroRecovered,zeroRecovered,cfg,bodyHit,nullptr,(void*)2);check(!bodyHit.startSolid,"Zero-offset attachments leave their subject hull when room is available");
 collisionTrace.Boxes.clear();cfg.Attached=false;cfg.FollowHalfLife=.2;MirvCameraCollisionSet(cfg);
 cfg.Attached=true;cfg.FollowHalfLife=0;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();x=y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,124,x,y,z);check(x==0&&y==0&&z==0,"Zero offset stays unchanged when the endpoint is already clear");
 cfg.Attached=false;cfg.FollowHalfLife=.2;MirvCameraCollisionSet(cfg);
 // Abrupt entity turns must move the orbit and view as one shot, with a bounded turn rate.
 cfg=MirvCameraCollisionSettings{};cfg.FollowHalfLife=.2;cfg.OrbitHalfLife=.15;cfg.OrbitSpeed=180;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.clear();
 for(int fps:{30,120}){
  MirvCameraAttachedReset();double previousYaw=0;float a[3]={0,0,0};
  for(int i=0;i<=fps*3;++i){double rawYaw=i==0?0:(i<fps?170:(i<fps*2?(i%2?179:-179):0)),radians=rawYaw/57.29577951308232;x=float(-60*std::cos(radians));y=float(-60*std::sin(radians));z=0;a[0]=0;a[1]=float(rawYaw);a[2]=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,130+double(i)/fps,x,y,z,seed,a);if(i)check(std::abs(std::remainder(a[1]-previousYaw,360.0))<=180.0/fps+.01,"Sharp turns and yaw wrapping obey the configured orbit speed without a view snap");check(std::abs(std::hypot(x,y)-60)<.001&&std::abs(a[0])<.001&&std::abs(a[2])<.001,"A turning shot retains boom length and horizon instead of cutting through the subject");check(std::abs(std::remainder(a[1]-std::atan2(-y,-x)*57.29577951308232,360.0))<.001,"The view follows the smoothed orbit throughout a sharp turn");previousYaw=a[1];}
 }
 // A coincident first-person attachment has no look-at direction, but its authored view must remain stable.
 MirvCameraAttachedReset();x=y=z=0;float firstPerson[3]={0,0,0};MirvCameraAttachedApply(&attachment,(void*)2,seed,133,x,y,z,seed,firstPerson);firstPerson[1]=179;MirvCameraAttachedApply(&attachment,(void*)2,seed,133+1.0/120,x,y,z,seed,firstPerson);check(firstPerson[1]>0&&firstPerson[1]<=1.501&&std::abs(firstPerson[2])<.001,"Zero-offset follow smooths a sharp view turn without solving a singular look-at");
 // Near-vertical booms and transient invalid inputs must not produce an upside-down branch or poison the shot.
 MirvCameraAttachedReset();double oldPoleYaw=0;for(int i=0;i<=120;++i){double yaw=i==0?0:(i%2?179:-179),radians=yaw/57.29577951308232;x=float(-std::cos(radians));y=float(-std::sin(radians));z=-60;float a[3]={float(-std::atan2(60.0,1.0)*57.29577951308232),float(yaw),0};MirvCameraAttachedApply(&attachment,(void*)2,seed,133.1+i/120.0,x,y,z,seed,a);if(i)check(std::abs(std::remainder(a[1]-oldPoleYaw,360.0))<1.51,"Near-vertical orbit avoids yaw branch flips during wrapping");check(std::abs(a[0])<=90&&std::abs(a[2])<.001,"Near-vertical framing remains upright");oldPoleYaw=a[1];}
 float invalidView[3]={0,std::numeric_limits<float>::quiet_NaN(),0};x=-60;y=z=0;check(!MirvCameraAttachedApply(&attachment,(void*)2,seed,134,x,y,z,seed,invalidView),"Invalid source angles cannot poison persistent follow state");float recoveredView[3]={0,0,0};check(MirvCameraAttachedApply(&attachment,(void*)2,seed,134.01,x,y,z,seed,recoveredView)&&std::isfinite(recoveredView[1]),"The next valid source pose initializes a clean shot");
 // A sharp orbit and the collision solver must stay coherent when used together.
 cfg.Attached=true;cfg.Radius=1;cfg.FollowRecoveryDelay=0;MirvCameraCollisionSet(cfg);collisionTrace.Boxes={{{10,-30,-100},{11,30,100},nullptr},{{-2,-2,-2},{2,2,2},(void*)2}};
 for(int fps:{30,120}){MirvCameraAttachedReset();Afx::Math::Quaternion previous;for(int i=0;i<=2*fps;++i){double yaw=i?170:0,radians=yaw/57.29577951308232;x=float(-60*std::cos(radians));y=float(-60*std::sin(radians));z=0;float a[3]={0,float(yaw),0};MirvCameraAttachedApply(&attachment,(void*)2,seed,134+double(i)/fps,x,y,z,seed,a);double position[3]={x,y,z};MirvTf2Trace_t body;CameraCollisionTrace(position,position,cfg,body,nullptr,(void*)2);check(CameraCollisionClear(position,cfg,(void*)2)&&!body.startSolid&&!body.allSolid,"Sharp orbit collision keeps clearance from geometry and its own subject");auto view=Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({a[0],a[1],a[2]}));if(i){if(Afx::Math::DotProduct(previous,view)<0)view=-1.0*view;Afx::Math::Vector3 axis;check(std::abs(previous.GetAng(view,axis))*57.29577951308232<45,"Collision during a sharp orbit cannot choose a briefly reversed view branch");}previous=view;}}
 collisionTrace.Boxes.clear();
 // A soft-deactivating mirv_aim still owns the view; source follow must not fight it.
 cfg.Attached=false;cfg.FollowRecoveryDelay=1;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();x=-60;y=z=0;float softView[3]={0,45,0};double softFrom[3]={x,y,z};AttachedView(&attachment,(void*)2,seed,seed,137,1,false,softFrom,seed,x,y,z,softView,nullptr,true);x=60;softView[0]=0;softView[1]=45;softView[2]=0;softFrom[0]=60;AttachedView(&attachment,(void*)2,seed,seed,137+1.0/120,1,false,softFrom,seed,x,y,z,softView,nullptr,true);check(std::abs(softView[1]-45)<.001,"Coordinated source framing respects mirv_aim soft-deactivation ownership");
 // Smoothing can be disabled without leaving an angular delay behind.
 cfg.FollowHalfLife=0;cfg.Attached=true;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();x=-60;y=z=0;float rigidAngles[3]={0,0,0};MirvCameraAttachedApply(&attachment,(void*)2,seed,134,x,y,z,seed,rigidAngles);x=0;y=-60;rigidAngles[1]=90;MirvCameraAttachedApply(&attachment,(void*)2,seed,134.01,x,y,z,seed,rigidAngles);check(std::abs(x)<.001&&y==-60&&std::abs(rigidAngles[1]-90)<.001,"Follow zero restores rigid offset and view rotation even with collision enabled");
 // A closed door strands a camera with no physical route; recovery is a bounded, optional cut.
 cfg=MirvCameraCollisionSettings{};cfg.Attached=true;cfg.FollowHalfLife=.2;cfg.Radius=1;cfg.AttachedSoftDistance=0;cfg.Slide=false;cfg.FollowTeleportDistance=10;
 auto closedDoor=[&](double delay,double endRoot=120){cfg.FollowRecoveryDelay=delay;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();collisionTrace.Boxes.clear();x=-60;y=z=0;MirvCameraAttachedApply(&attachment,(void*)2,seed,140,x,y,z,seed);bool recovered=false;double firstCut=0,lastCamera=-60;for(int i=1;i<=360;++i){double t=i/120.0;const double r[3]={(std::min)(endRoot,240*t),0,0};collisionTrace.Boxes.clear();if(t>=.3)collisionTrace.Boxes.push_back({{30,-100,-100},{31,100,100},nullptr});collisionTrace.Boxes.push_back({{r[0]-2,-2,-2},{r[0]+2,2,2},(void*)2});x=float(r[0]-60);y=z=0;float a[3]={0,0,0};MirvCameraAttachedApply(&attachment,(void*)2,r,140+t,x,y,z,r,a);double camera[3]={x,y,z};MirvTf2Trace_t ownBody;CameraCollisionTrace(camera,camera,cfg,ownBody,nullptr,(void*)2);check(!ownBody.startSolid&&!ownBody.allSolid,"Door recovery retains clearance from the attached subject");check(CameraCollisionClear(camera,cfg,(void*)2),"Door recovery never places the camera hull inside map geometry");if(recovered)check(std::abs(x-lastCamera)<4,"Recovery rebases the entire shot without a second camera jump");if(t>=.3&&x>32){if(!recovered)firstCut=t;recovered=true;}lastCamera=x;if(i==72){double paused=x;for(int j=0;j<240;++j){x=float(r[0]-60);MirvCameraAttachedApply(&attachment,(void*)2,r,140+t,x,y,z,r,a);}check(std::abs(x-paused)<.001,"Paused rendering cannot advance the blocked recovery timer");}}
  if(delay>0)check(recovered&&firstCut>=delay+.3-1.0/120-.0001&&firstCut<delay+1,"A camera stranded by a closing door recovers after bounded demo time");else check(!recovered&&x<29,"Recovery zero keeps strict collision instead of cutting through a closed door");return double(x);};
 check(closedDoor(1)>32,"Default blocked recovery rejoins the subject despite a small teleport-reset threshold");closedDoor(.05);closedDoor(1,70);closedDoor(0,70);closedDoor(0);
 collisionTrace.Boxes.clear();cfg=MirvCameraCollisionSettings{};cfg.FollowHalfLife=.2;MirvCameraCollisionSet(cfg);
 collisionCommand({"mirv_cam","attach","follow","orbitHalfLife","0.25"});collisionCommand({"mirv_cam","attach","follow","orbitSpeed","240"});collisionCommand({"mirv_cam","attach","follow","recoveryDelay","2"});auto shotSettings=MirvCameraCollisionGet();check(shotSettings.OrbitHalfLife==.25&&shotSettings.OrbitSpeed==240&&shotSettings.FollowRecoveryDelay==2,"Console controls coordinated orbit and blocked recovery independently");shotSettings.OrbitHalfLife=-1;check(!MirvCameraCollisionSet(shotSettings),"Negative orbit half-life rejects atomically");shotSettings=MirvCameraCollisionGet();shotSettings.OrbitSpeed=std::numeric_limits<double>::infinity();check(!MirvCameraCollisionSet(shotSettings),"Non-finite orbit speed rejects");shotSettings=MirvCameraCollisionGet();shotSettings.FollowRecoveryDelay=61;check(!MirvCameraCollisionSet(shotSettings),"Recovery delay has a finite bounded range");
 cfg=MirvCameraCollisionSettings{};cfg.FollowHalfLife=.2;MirvCameraCollisionSet(cfg);
 // Preserve off-centre composition too, not just a camera aimed exactly at the bone.
 const double from[3]={-60,-10,64},to[3]={-40,15,55},target[3]={0,0,72};float original[3]={10,20,15},corrected[3]={10,20,15};
 auto localDirection=[&](const double*p,const float*a){double f[3],r[3],u[3];Afx::Math::MakeVectors(a[2],a[0],a[1],f,r,u);double d[3],l=0;for(int j=0;j<3;++j){d[j]=target[j]-p[j];l+=d[j]*d[j];}std::array<double,3>out{};for(int j=0;j<3;++j){out[0]+=f[j]*d[j]/std::sqrt(l);out[1]+=r[j]*d[j]/std::sqrt(l);out[2]+=u[j]*d[j]/std::sqrt(l);}return out;};
 check(MirvCameraAttachedReframe(from,to,target,corrected),"Final-position framing correction succeeds");check(corrected[2]==original[2],"Off-centre framing retains the authored horizon roll");auto beforeDirection=localDirection(from,original),afterDirection=localDirection(to,corrected);for(int j=0;j<3;++j)check(std::abs(beforeDirection[j]-afterDirection[j])<1e-6,"Framing correction preserves a deliberately off-centre target");
 check(!MirvCameraAttachedReframe(target,to,target,corrected),"A coincident aiming target cannot create invalid angles");
 // Exercise the actual RenderView stage: world mode stays fixed; entity mode and mirv_aim retain framing.
 const double r0[3]={0,0,0},h0[3]={0,0,72},r1[3]={0,10,0},h1[3]={0,10,72},aimFrom0[3]={-60,0,72},aimFrom1[3]={-60,10,72};
 auto viewTest=[&](int mode,bool useAim){MirvCameraAttachedReset();x=-60;y=0;z=72;float a[3]={0,0,0};AttachedView(&attachment,(void*)2,h0,r0,80,mode,useAim,aimFrom0,h0,x,y,z,a);x=-60;y=10;z=72;a[0]=a[1]=a[2]=0;AttachedView(&attachment,(void*)2,h1,r1,80.01,mode,useAim,aimFrom1,h1,x,y,z,a);return double(a[1]);};
 check(viewTest(3,false)==0,"World mode keeps its fixed heading even with positional follow");check(std::abs(viewTest(1,false))<.001,"Entity mode shares a filtered focus with its orbit instead of turning root jumps into view jitter");check(viewTest(3,true)>1,"An existing mirv_aim override uses the corrected camera position even in world mode");
 MirvCameraAttachedReset();x=-60;y=0;z=72;float offsetAim[3]={0,15,0},cachedAim[3]={0,0,0};AttachedView(&attachment,(void*)2,h0,r0,81,1,true,h0,h0,x,y,z,offsetAim,cachedAim);check(std::abs(offsetAim[1]-15)<.001,"Coincident aim-source fallback preserves deliberate post-aim rotation offsets");
 cfg.Attached=false;cfg.FollowHalfLife=.2;cfg.BoneHalfLife=0;cfg.AttachedSoftDistance=24;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.clear();
 collisionCommand({"mirv_cam","collision","attached","1"});check(MirvCameraCollisionGet().Attached,"Console exposes attached-camera collision independently");
 collisionCommand({"mirv_cam","collision","ignoreLocal","0"});check(!MirvCameraCollisionGet().IgnoreLocal,"Console exposes local-player inclusion");
 collisionCommand({"mirv_cam","collision","returnHalfLife","0.4"});collisionCommand({"mirv_cam","collision","returnSpeed","50"});check(MirvCameraCollisionGet().ReturnHalfLife==.4&&MirvCameraCollisionGet().ReturnSpeed==50,"Console configures collision return damping and speed");
 collisionCommand({"mirv_cam","attach","follow","0.3"});collisionCommand({"mirv_cam","attach","follow","maxDistance","256"});check(MirvCameraCollisionGet().FollowHalfLife==.3&&MirvCameraCollisionGet().FollowMaxDistance==256,"Console configures positional follow and lag limit");
 collisionCommand({"mirv_cam","attach","follow","boneHalfLife","0.12"});collisionCommand({"mirv_cam","attach","follow","teleportDistance","512"});check(MirvCameraCollisionGet().BoneHalfLife==.12&&MirvCameraCollisionGet().FollowTeleportDistance==512,"Console configures bone damping and teleport resets independently");
 collisionCommand({"mirv_cam","collision","attachedSoftDistance","32"});check(MirvCameraCollisionGet().AttachedSoftDistance==32,"Console exposes the attached collision soft zone");auto invalidSoft=MirvCameraCollisionGet();invalidSoft.AttachedSoftDistance=-1;check(!MirvCameraCollisionSet(invalidSoft),"Invalid collision soft zone is rejected");
 auto invalid=MirvCameraCollisionGet();invalid.BoneHalfLife=std::numeric_limits<double>::quiet_NaN();check(!MirvCameraCollisionSet(invalid),"Non-finite bone smoothing is rejected");invalid=MirvCameraCollisionGet();invalid.FollowTeleportDistance=-1;check(!MirvCameraCollisionSet(invalid),"Negative teleport reset is rejected");
 const auto good=MirvCameraCollisionGet();auto bad=good;bad.FollowHalfLife=-1;check(!MirvCameraCollisionSet(bad)&&MirvCameraCollisionGet().FollowHalfLife==good.FollowHalfLife,"Invalid follow preferences reject atomically");bad=good;bad.ReturnSpeed=std::numeric_limits<double>::infinity();check(!MirvCameraCollisionSet(bad),"Invalid return speed rejects");
 MirvCameraCollisionSettings off;MirvCameraCollisionSet(off);MirvCameraCollisionReset();collisionTrace.Boxes.clear();
}
"""

# Exercise the actual attachment orientation/capture code with deterministic entity poses.
mode_source=(root/'AfxHookSource/MirvCalcs.cpp').read_text()
mode_class=mode_source[mode_source.index('class CMirvVecAngTf2EntityIndexCalc'):mode_source.index('#ifndef _WIN64\nclass CMirvVecAngHandleAttachmentCalc')]
mode_class=mode_class.replace('Mirv_Tf2_GetBoneNames(m_EntityIndex, names)', 'TestBoneNames(m_EntityIndex, names)')
mode_class=mode_class[:mode_class.index('\tSOURCESDK::TF2::IClientTools * GetTools(')]+r"""
 SOURCESDK::TF2::IClientTools*GetTools(){return &tools;}
 bool GetBase(SOURCESDK::TF2::IClientTools*,SOURCESDK::Vector&pos,float base[3][3],SOURCESDK::QAngle&raw,SOURCESDK::QAngle&facing,bool&ok){
  if(!poseValid||!BindModel((void*)2,poseModel))return false;pos=modelBonePositions.empty()||m_BoneIndex<0?pose:modelBonePositions.at(m_BoneIndex);raw=m_BoneIndex<0?entityAngles:boneAngles;facing=entityAngles;ok=true;
  Mirv_AngleMatrix3(raw.x,raw.y,raw.z,base);m_AnchorEntity=(void*)2;
  m_AnchorRoot[0]=m_AnchorRoot[1]=m_AnchorRoot[2]=0;return true;
 }
};
"""
start=mode_class.index('bool GetAnglesInfo(');body=mode_class.index('{',start);depth=1;end=body+1
while depth:
 depth+=(mode_class[end]=='{')-(mode_class[end]=='}');end+=1
mode_class=mode_class[:body]+'{ return false; }'+mode_class[end:]
mode_math=mode_source[mode_source.index('static void Mirv_AngleMatrix3('):mode_source.index('class CMirvVecAngTf2EntityIndexCalc')]
mode_preamble=r"""
namespace AttachmentModeTest {
namespace SOURCESDK {struct Vector{float x=0,y=0,z=0;};using QAngle=Vector;namespace TF2{using IClientTools=::SOURCESDK::TF2::IClientTools;}}
struct CMirvVecAngCalc{CMirvVecAngCalc(const char*){}virtual void Console_PrintBegin(){}};
struct View{double LastCameraAngles[3]={12,37,-8};}g_Hook_VClient_RenderView;
SOURCESDK::TF2::IClientTools tools;
int g_LevelInitCount=0;const void*poseModel=(void*)1;
std::vector<std::string> boneNames={"root","bip_head"};
std::vector<SOURCESDK::Vector> modelBonePositions;
int TestBoneNames(int,MirvTf2BoneName_t*out){for(size_t i=0;i<boneNames.size();++i)snprintf(out[i].name,64,"%s",boneNames[i].c_str());return int(boneNames.size());}
SOURCESDK::Vector pose{0,0,72};SOURCESDK::QAngle boneAngles{80,170,20},entityAngles{0,90,0};bool poseValid=true;
"""
mode_constants=(root/'AfxHookSource/MirvCalcs.h').read_text();mode_constants=mode_constants[mode_constants.index('#define MIRV_TF2_BONEMODE_BONE'):mode_constants.index('struct MirvTf2Bone_t')]
offset_source=(root/'AfxHookSource/MirvCam.cpp').read_text()
start=offset_source.index('bool CMirvCam::ApplyOffset(');body=offset_source.index('{',start);depth=1;end=body+1
while depth:
 depth+=(offset_source[end]=='{')-(offset_source[end]=='}');end+=1
mode_offset='struct Offset {float m_OffsetForwad=-60,m_OffsetLeft=0,m_OffsetUp=8,m_OffsetForwardRot=0,m_OffseLeftRot=0,m_OffsetUpRot=0;'+offset_source[start:end].replace('CMirvCam::ApplyOffset','ApplyOffset')+'};\n'
mode_tests=r"""
void Run(){
 SOURCESDK::Vector position;SOURCESDK::QAngle angles;float offset[3];
 CMirvVecAngTf2EntityIndexCalc camera(nullptr,2,1,MIRV_TF2_BONEMODE_WORLD);
 check(camera.CalcVecAng(position,angles)&&std::abs(angles.x-12)<.001&&std::abs(angles.y-37)<.001&&std::abs(angles.z+8)<.001,"World mode starts with the current camera heading instead of entity or bone angles");
 for(int i=0;i<80;++i){pose={float(i),float(i*.5),float(72+4*sin(i))};entityAngles={float(i%30),float(i*5),0};boneAngles={float(i*3),float(i*7),float(i*4)};camera.CalcVecAng(position,angles);check(position.x==pose.x&&position.y==pose.y&&position.z==pose.z&&std::abs(angles.y-37)<.001&&std::abs(angles.x-12)<.001&&std::abs(angles.z+8)<.001,"World mode retains head bob and translation without inheriting either rotation");}
 const double origin[3]={pose.x-50,pose.y+20,pose.z+10},heading[3]={-10,99,12};check(camera.CaptureCamera(origin,heading,offset),"World mode captures a relative camera composition");
 for(int i=0;i<80;++i){pose={float(i),float(i*2),float(72+8*cos(i))};boneAngles={float(i*2),float(i*8),float(i*5)};entityAngles.y=float(i*9);camera.CalcVecAng(position,angles);check(std::abs(position.x-pose.x+50)<.001&&std::abs(position.y-pose.y-20)<.001&&std::abs(position.z-pose.z-10)<.001&&std::abs(angles.x+10)<.001&&std::abs(angles.y-99)<.001&&std::abs(angles.z-12)<.001,"Captured world-mode offset does not orbit when the player or bone turns");}
 CMirvVecAngTf2EntityIndexCalc root(nullptr,2,-1,MIRV_TF2_BONEMODE_WORLD);root.CalcVecAng(position,angles);check(std::abs(angles.y-37)<.001,"World orientation also works without a bone");
 CMirvVecAngTf2EntityIndexCalc bone(nullptr,2,1,MIRV_TF2_BONEMODE_BONE);bone.CalcVecAng(position,angles);check(angles.x==boneAngles.x&&angles.y==boneAngles.y,"Existing bone mode retains bone rotation");
 CMirvVecAngTf2EntityIndexCalc entity(nullptr,2,1,MIRV_TF2_BONEMODE_ENTITY);entity.CalcVecAng(position,angles);check(angles.y==entityAngles.y,"Existing entity mode retains entity facing");
 pose={0,0,72};entityAngles={0,0,0};boneAngles={70,120,25};const double entityOrigin[3]={-60,0,80},entityHeading[3]={5,0,0};
 CMirvVecAngTf2EntityIndexCalc capturedEntity(nullptr,2,1,MIRV_TF2_BONEMODE_ENTITY);check(capturedEntity.CaptureCamera(entityOrigin,entityHeading,offset),"Entity mode can capture a camera composition");
 for(int i=0;i<120;++i){pose.z=float(72+2*std::sin(i));boneAngles={float(i*3),float(i*5),float(i*7)};capturedEntity.CalcVecAng(position,angles);check(std::abs(position.x+60)<.001&&std::abs(position.y)<.001&&std::abs(position.z-pose.z-8)<.001&&std::abs(angles.x-5)<.001&&std::abs(angles.y)<.001&&std::abs(angles.z)<.001,"Captured entity mode keeps bone translation but excludes bone pitch, yaw and roll");}
 entityAngles.y=45;capturedEntity.CalcVecAng(position,angles);check(std::abs(position.x+60/std::sqrt(2.0))<.001&&std::abs(position.y+60/std::sqrt(2.0))<.001&&std::abs(angles.y-45)<.001&&std::abs(angles.z)<.001,"Captured entity mode rotates with entity facing after bone changes");
 // Video-like integration: actual captured entity calculation -> final view controller.
 auto cfg=MirvCameraCollisionGet();cfg.Attached=true;cfg.FollowHalfLife=.2;cfg.BoneHalfLife=.15;cfg.Radius=6;cfg.IgnoreLocal=false;cfg.Players=true;cfg.AttachedSoftDistance=24;cfg.FollowMaxDistance=128;MirvCameraCollisionSet(cfg);MirvCameraAttachedReset();
 collisionTrace.Boxes={{{-171,-1000,-1000},{-170,1000,1000},nullptr}};pose={0,0,72};entityAngles={0,0,0};boneAngles={0,0,0};CMirvVecAngTf2EntityIndexCalc videoCamera(nullptr,2,1,MIRV_TF2_BONEMODE_ENTITY);videoCamera.CaptureCamera(entityOrigin,entityHeading,offset);
 for(int i=0;i<=360;++i){double t=i/120.0;const double root[3]={-40*t,12*std::sin(t),8*std::floor(t*8)};pose={float(root[0]),float(root[1]),float(root[2]+72+2*std::sin(t*50))};entityAngles={0,float(60*std::sin(t*1.3)),0};boneAngles={float(70*std::sin(t*10)),float(t*200),float(90*std::sin(t*12))};videoCamera.CalcVecAng(position,angles);float x=position.x,y=position.y,z=position.z,a[3]={angles.x,angles.y,angles.z};const double pivot[3]={pose.x,pose.y,pose.z},from[3]={x,y,z};AttachedView(&videoCamera,(void*)2,pivot,root,100+t,1,false,from,pivot,x,y,z,a);double f[3],r[3],u[3];Afx::Math::MakeVectors(a[2],a[0],a[1],f,r,u);double d[3]={pivot[0]-x,pivot[1]-y,pivot[2]-z},dot=0,length=0;for(int j=0;j<3;++j){dot+=f[j]*d[j];length+=d[j]*d[j];}check(dot/std::sqrt(length)>.9&&std::abs(a[2])<1.5,"Captured entity follow retains framing and a stable horizon during backsteps, stairs, turns, head bob and wall approaches");check(x>=-163.97,"The integrated entity camera stays outside map geometry");}
 // Repeat the full pipeline for every attach mode and both attach styles, with/without bones and aiming.
 for(int mode=0;mode<=3;++mode)for(int boneIndex:{-1,1})for(bool captured:{false,true})for(bool useAim:{false,true})for(int fps:{30,120}){
  MirvCameraAttachedReset();pose={0,0,boneIndex<0?0.0f:72.0f};entityAngles={0,0,0};boneAngles={0,0,0};
  CMirvVecAngTf2EntityIndexCalc controller(nullptr,2,boneIndex,mode);Offset manual;
  const double origin[3]={-60,0,pose.z+8};if(captured)check(controller.CaptureCamera(origin,entityHeading,offset),"Each attach mode accepts captured framing");
  for(int i=0;i<=2*fps;++i){double t=double(i)/fps;const double body[3]={-55*t,12*std::sin(t),8*std::floor(t*8)};pose={float(body[0]),float(body[1]),float(body[2]+(boneIndex<0?0:72+2*std::sin(50*t)))};entityAngles={0,float(50*std::sin(t*1.3)),0};boneAngles={float(12*std::sin(5*t)),float(55*std::sin(t*1.7)),float(10*std::sin(6*t))};
   collisionTrace.Boxes={{{-171,-1000,-1000},{-170,1000,1000},nullptr},{{-85-40*t,-45,0},{-65-40*t,-25,300},(void*)1}};
   controller.CalcVecAng(position,angles);float x=position.x,y=position.y,z=position.z,a[3]={angles.x,angles.y,angles.z};const double aimFrom[3]={x,y,z},pivot[3]={pose.x,pose.y,pose.z};if(useAim){double dx=pivot[0]-x,dy=pivot[1]-y,dz=pivot[2]-z;a[0]=float(std::atan2(-dz,std::hypot(dx,dy))*57.29577951308232);a[1]=float(std::atan2(dy,dx)*57.29577951308232);a[2]=0;}const float cachedAim[3]={a[0],a[1],a[2]};if(!captured)manual.ApplyOffset(x,y,z,a[2],a[0],a[1]);
   const double from[3]={x,y,z};float rawAngles[3]={a[0],a[1],a[2]};
   double wantedF[3],wantedR[3],wantedU[3];Afx::Math::MakeVectors(a[2],a[0],a[1],wantedF,wantedR,wantedU);
   double expectedLocal[3]={},rawDirection[3],rawLength=0;for(int j=0;j<3;++j){rawDirection[j]=pivot[j]-from[j];rawLength+=rawDirection[j]*rawDirection[j];}for(int j=0;j<3;++j){expectedLocal[0]+=wantedF[j]*rawDirection[j]/std::sqrt(rawLength);expectedLocal[1]+=wantedR[j]*rawDirection[j]/std::sqrt(rawLength);expectedLocal[2]+=wantedU[j]*rawDirection[j]/std::sqrt(rawLength);}
   AttachedView(&controller,(void*)2,pivot,body,200+mode*10+t,mode,useAim,aimFrom,pivot,x,y,z,a,cachedAim);
   const double finalCamera[3]={x,y,z};check(CameraCollisionClear(finalCamera,cfg,(void*)2),"Every attach mode stays clear of walls and moving entities during combined follow");
   double f[3],r[3],u[3];Afx::Math::MakeVectors(a[2],a[0],a[1],f,r,u);double d[3],length=0,actualLocal[3]={};for(int j=0;j<3;++j){d[j]=pivot[j]-finalCamera[j];length+=d[j]*d[j];}for(int j=0;j<3;++j){actualLocal[0]+=f[j]*d[j]/std::sqrt(length);actualLocal[1]+=r[j]*d[j]/std::sqrt(length);actualLocal[2]+=u[j]*d[j]/std::sqrt(length);}
   if(useAim)check(actualLocal[0]>.99999,"Existing aiming retains its target after final collision correction in every attach mode");
   else if(mode==MIRV_TF2_BONEMODE_WORLD)check(a[0]==rawAngles[0]&&a[1]==rawAngles[1]&&a[2]==rawAngles[2],"World mode retains fixed orientation with captured or manual offsets, with or without a bone");
   else {double error=0;for(int j=0;j<3;++j)error+=(expectedLocal[j]-actualLocal[j])*(expectedLocal[j]-actualLocal[j]);check(error<.4&&actualLocal[0]>.8,"Bone, entity and aligned modes keep the subject within the shot during combined follow and collision");}
  }
 }
 collisionTrace.Boxes.clear();MirvCameraCollisionSettings off;MirvCameraCollisionSet(off);MirvCameraCollisionReset();
 boneAngles={0,0,0};entityAngles={0,45,0};CMirvVecAngTf2EntityIndexCalc aligned(nullptr,2,1,MIRV_TF2_BONEMODE_ALIGNED);aligned.CalcVecAng(position,angles);check(std::abs(angles.y-45)<.001,"Aligned mode still initializes from entity facing");boneAngles.y=30;aligned.CalcVecAng(position,angles);check(std::abs(angles.y-75)<.001,"Aligned mode still inherits subsequent bone turns");
 // Fast turns stress all native attachment modes and both offset workflows.
 cfg.Attached=false;cfg.FollowHalfLife=.2;cfg.BoneHalfLife=.1;cfg.OrbitHalfLife=.15;cfg.OrbitSpeed=360;MirvCameraCollisionSet(cfg);collisionTrace.Boxes.clear();
 for(int mode=0;mode<=3;++mode)for(bool captured:{false,true})for(bool useAim:{false,true})for(int fps:{30,120}){
  MirvCameraAttachedReset();pose={0,0,72};boneAngles=entityAngles={0,0,0};CMirvVecAngTf2EntityIndexCalc shot(nullptr,2,1,mode);Offset manual;const double origin[3]={-60,0,80},heading[3]={0,0,0};if(captured)shot.CaptureCamera(origin,heading,offset);
  double previousPosition[3]={};Afx::Math::Quaternion previousView;bool previous=false;
  for(int i=0;i<=fps*3;++i){double t=double(i)/fps,yaw=i<fps/2?0:(i<fps*2?(i%2?179:-179):0);const double r[3]={40*t,0,0};pose={float(r[0]),0,float(72+2*std::sin(t*40))};entityAngles={0,float(yaw),0};boneAngles={float(15*std::sin(t*18)),float(yaw),float(10*std::sin(t*20))};shot.CalcVecAng(position,angles);float x=position.x,y=position.y,z=position.z,a[3]={angles.x,angles.y,angles.z};const double aimFrom[3]={x,y,z},pivot[3]={pose.x,pose.y,pose.z};if(useAim){a[0]=float(std::atan2(-(pivot[2]-z),std::hypot(pivot[0]-x,pivot[1]-y))*57.29577951308232);a[1]=float(std::atan2(pivot[1]-y,pivot[0]-x)*57.29577951308232);a[2]=0;}const float aimAngles[3]={a[0],a[1],a[2]};if(!captured)manual.ApplyOffset(x,y,z,a[2],a[0],a[1]);float rawAngles[3]={a[0],a[1],a[2]};AttachedView(&shot,(void*)2,pivot,r,350+t,mode,useAim,aimFrom,pivot,x,y,z,a,aimAngles);
   auto view=Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({a[0],a[1],a[2]}));if(previous){if(Afx::Math::DotProduct(previousView,view)<0)view=-1.0*view;Afx::Math::Vector3 axis;check(std::abs(previousView.GetAng(view,axis))*57.29577951308232<900.0/fps+.1,"All attach modes avoid short-lived orientation flips during flicks and yaw wrapping");double distance=std::sqrt(std::pow(x-previousPosition[0],2)+std::pow(y-previousPosition[1],2)+std::pow(z-previousPosition[2],2));check(distance<900.0/fps+.1,"Captured and manual attachments avoid instant positional orbit jumps on sharp turns");}if(mode==3&&!useAim)check(a[0]==rawAngles[0]&&a[1]==rawAngles[1]&&a[2]==rawAngles[2],"World mode preserves its fixed view during the coordinated-follow stress test");previousView=view;previousPosition[0]=x;previousPosition[1]=y;previousPosition[2]=z;previous=true;
  }
 }
 // Recapturing a displayed pose must preserve both the controller and its full calc pipeline.
 cfg.Attached=true;cfg.FollowHalfLife=.2;cfg.BoneHalfLife=.1;cfg.Radius=6;MirvCameraCollisionSet(cfg);
 collisionTrace.Boxes={{{-81,-100,-100},{-80,100,200},nullptr}};
 for(int mode=0;mode<=3;++mode){
  MirvCameraAttachedReset();pose={0,0,72};boneAngles=entityAngles={0,0,0};CMirvVecAngTf2EntityIndexCalc recapture(nullptr,2,1,mode);const double captureOrigin[3]={-60,15,80},captureHeading[3]={5,0,0};recapture.CaptureCamera(captureOrigin,captureHeading,offset);
  float x=0,y=0,z=0,a[3]={};double pivot[3],body[3]={};
  for(int i=0;i<=30;++i){body[0]=i*.1;pose={float(body[0]),0,float(72+std::sin(i*.1))};pivot[0]=pose.x;pivot[1]=pose.y;pivot[2]=pose.z;recapture.CalcVecAng(position,angles);x=position.x;y=position.y;z=position.z;a[0]=angles.x;a[1]=angles.y;a[2]=angles.z;double raw[3]={x,y,z};AttachedView(&recapture,(void*)2,pivot,body,300+i/120.0,mode,false,raw,pivot,x,y,z,a);}
  const double displayed[3]={x,y,z};const float displayedAngles[3]={a[0],a[1],a[2]};double recovered[3]={x,y,z},heading[3]={a[0],a[1],a[2]};check(MirvCameraAttachedCapturePose(&recapture,recovered,heading),"Every mode can recover its displayed attachment pose for recapture");check(recapture.CaptureCamera(recovered,heading,offset),"Every mode accepts a recovered capture pose");recapture.CalcVecAng(position,angles);x=position.x;y=position.y;z=position.z;a[0]=angles.x;a[1]=angles.y;a[2]=angles.z;double raw[3]={x,y,z};AttachedView(&recapture,(void*)2,pivot,body,300+30/120.0,mode,false,raw,pivot,x,y,z,a);
  check(std::abs(x-displayed[0])+std::abs(y-displayed[1])+std::abs(z-displayed[2])<.001,"Recapture retains collision clearance and current follow lag in all modes");check(std::abs(a[0]-displayedAngles[0])+std::abs(a[1]-displayedAngles[1])+std::abs(a[2]-displayedAngles[2])<.01,"Recapture retains the displayed framing in all modes");
 }
 collisionTrace.Boxes.clear();MirvCameraCollisionSettings disabled;MirvCameraCollisionSet(disabled);MirvCameraCollisionReset();
 // Class changes can reorder bones. Bind by identity and fail closed if it disappears.
 poseModel=(void*)10;boneNames={"root","bip_head"};modelBonePositions={{222,0,0},{111,0,0}};CMirvVecAngTf2EntityIndexCalc rebind(nullptr,2,1,MIRV_TF2_BONEMODE_ENTITY);check(rebind.CalcVecAng(position,angles),"Initial model binds its selected bone");
 poseModel=(void*)11;boneNames={"bip_head","root"};modelBonePositions={{111,0,0},{222,0,0}};check(rebind.CalcVecAng(position,angles)&&position.x==111,"Model changes retain a selected bone after reordering");
 poseModel=(void*)12;boneNames={"root","other"};check(!rebind.CalcVecAng(position,angles),"A missing named bone fails rather than following a different limb");
 poseModel=(void*)13;boneNames={"root","bip_head"};modelBonePositions={{222,0,0},{111,0,0}};check(rebind.CalcVecAng(position,angles)&&position.x==111,"A later compatible model can restore the binding");
 modelBonePositions.clear();boneAngles={0,0,0};entityAngles={0,45,0};CMirvVecAngTf2EntityIndexCalc realigned(nullptr,2,1,MIRV_TF2_BONEMODE_ALIGNED);realigned.CalcVecAng(position,angles);boneAngles.y=30;realigned.CalcVecAng(position,angles);check(std::abs(angles.y-75)<.001,"Aligned attachment follows its original calibrated bone");poseModel=(void*)14;realigned.CalcVecAng(position,angles);check(std::abs(angles.y-45)<.001,"Model changes reset uncaptured aligned calibration");
 poseValid=false;double anchor[3],rootOrigin[3];void*subject=nullptr;check(!camera.CalcVecAng(position,angles)&&!camera.GetAttachAnchor(anchor,subject,rootOrigin),"Missing attachment poses invalidate the anchor rather than reusing stale data");poseValid=true;
}
}
"""
test=test.replace('void frame(',mode_preamble+mode_constants+mode_math+mode_class+mode_offset+mode_tests+'\nvoid frame(',1)

test=test.replace('int main(){try{',collision_tests+'\nint main(){try{CollisionTests();AttachedFollowTests();AttachmentModeTest::Run();',1)

neo=(root/'shared/overlay/third_party/imgui_neo_sequencer/imgui_neo_sequencer.cpp').read_text()
neo='extern void TestRecordNeoLabel(int,int);\n'+neo.replace('snprintf(lbl, sizeof(lbl), "%d", labelIndex);','snprintf(lbl, sizeof(lbl), "%d", labelIndex); TestRecordNeoLabel(*frame,labelIndex);')
neo+='\nnamespace ImGui{void TestSetNeoView(float zoom,int offset){for(auto &entry:sequencerData){entry.second.Zoom=zoom;entry.second.OffsetFrame=offset;}}}\n'
neo_file=out/'neo_labels_test.cpp';neo_file.write_text(neo)
source=out/'overlay_test.cpp';source.write_text(preamble+render_code+cursor_code+preview_api+demo_time+fix_api+pov_api+fix_command+collision_code+s+native+view+layout+test)
cmd=['g++','-std=c++17','-O0','-g','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root),'-I'+str(root/'AfxHookSource'),'-I'+str(imgui),'-I'+str(root/'shared/overlay/third_party/imgui_neo_sequencer'),str(source),str(root/'shared/CamPath.cpp'),str(root/'shared/AfxMath.cpp')]+[str(imgui/f) for f in ['imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp']]+[(str(neo_file) if f=='imgui_neo_sequencer.cpp' else str(root/'shared/overlay/third_party/imgui_neo_sequencer'/f)) for f in ['imgui_neo_sequencer.cpp','imgui_neo_internal.cpp']]+[str(root/'shared/overlay/third_party/imguizmo/ImGuizmo.cpp'),'-o',str(out/'overlay_test')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
p=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(p.stdout+p.stderr)
if p.returncode:print(p.stderr[-6500:]);raise SystemExit(p.returncode)
with tempfile.TemporaryDirectory(prefix='hlae-recovery-test-') as recovery:
 p=subprocess.run([str(out/'overlay_test')],cwd=out,capture_output=True,text=True,env=dict(os.environ,LOCALAPPDATA=recovery))
 print(p.stdout+p.stderr);raise SystemExit(p.returncode)
