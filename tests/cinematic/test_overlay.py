from pathlib import Path
import subprocess,re
import argparse,tempfile
parser=argparse.ArgumentParser(description="Run the actual overlay UI headlessly with Dear ImGui, using fake engine services.")
parser.add_argument("--imgui-dir",type=Path,required=True)
parser.add_argument("--build-dir",type=Path,default=Path(tempfile.gettempdir())/"hlae-paper-ui-tests")
parser.add_argument("--sanitize",action="store_true")
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];imgui=args.imgui_dir.resolve();out=args.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
s=(root/'AfxHookSource/CameraOverlay.cpp').read_text();s=s[re.search(r'namespace AfxHookSource\s*\{\s*namespace CameraOverlay\s*\{',s).start():]
native=(root/'AfxHookSource/MirvCommands.cpp').read_text()
native=native[native.index('// Campath preview uses'):native.index('CON_COMMAND(mirv_campath,')]
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cmath>
#include <filesystem>
#include <map>
#include <iostream>
#include <string>
#include <vector>
#define MAX_PATH 260
#define CP_UTF8 65001
#define SW_SHOWNORMAL 1
int GetWindowsDirectoryA(char*,int){return 0;}
int MultiByteToWideChar(int,int,const char*,int,wchar_t*,int){return 0;}
void* ShellExecuteW(void*,const wchar_t*,const wchar_t*,void*,void*,int){return nullptr;}
template<size_t N,class... Args>int sprintf_s(char(&b)[N],const char*f,Args...a){return snprintf(b,N,f,a...);}
template<size_t N>void strncpy_s(char(&b)[N],const char*s,size_t){snprintf(b,N,"%s",s);}
namespace advancedfx {void Message(const char*,...){}void Warning(const char*,...){} }
struct MirvInput{bool Camera=false;float Pose[7]={};double Sens=1;bool GetCameraControlMode(){return Camera;}void SetCameraControlMode(bool b){Camera=b;}void SetTx(float x){Pose[0]=x;}void SetTy(float x){Pose[1]=x;}void SetTz(float x){Pose[2]=x;}void SetRx(float x){Pose[3]=x;}void SetRy(float x){Pose[4]=x;}void SetRz(float x){Pose[5]=x;}void SetFov(float x){Pose[6]=x;}double GetKeyboardSensitivty(){return Sens;}void SetKeyboardSensitivity(double x){Sens=x;}} input;
struct WrpGlobals{double interval_per_tick_get(){return 1.0/66;}} globals;
struct FakeView{CamPath m_CamPath;MirvInput*m_MirvInput=&input;double LastCameraOrigin[3]={100,-100,50},LastCameraAngles[3]={0,135,0},LastCameraFov=130;int LastWidth=1600,LastHeight=1000;bool Menu=false;bool IsInstalled(){return true;}bool IsMenuActive(){return Menu;}WrpGlobals*GetGlobals(){return &globals;}}g_Hook_VClient_RenderView;
struct CCampathDrawer{bool Draw=false;float Index=18;bool Draw_get(){return Draw;}void Draw_set(bool b){Draw=b;}float GetDrawKeyframeIndex(){return Index;}void SetDrawKeyframeIndex(float x){Index=x;}}g_CampathDrawer;
struct Clock{double Time=10;double GetTime(){return Time;}int GetFrameCount(){return int(Time*100);}}g_MirvTime;
std::vector<std::string> commands;
struct DemoInfo{int TotalTicks=12000;int GetDemoPlaybackTotalTicks(){return TotalTicks;}float GetDemoPlaybackTimeScale(){return 1;}}demo;
using WrpVEngineClientDemoInfoEx=DemoInfo;
struct Engine{bool Playing=true,Hltv=true;bool IsPlayingDemo(){return Playing;}bool IsHLTV(){return Hltv;}DemoInfo*GetDemoInfoEx(){return &demo;}void ClientCmd_Unrestricted(const char*c){commands.emplace_back(c);}}engine;Engine*g_VEngineClient=&engine;
bool GetCurrentDemoTick(int&n){n=int(g_MirvTime.Time*66);return true;}bool GetCurrentDemoTime(double&t){t=g_MirvTime.Time;return true;}bool GetDemoTickFromClientTime(double,double t,int&n){n=int(t*66);return true;}
int Mirv_Tf2_GetMaxPlayers(){return 1;}bool Mirv_Tf2_GetPlayerInfo(int i,char*b,int n,int*u){snprintf(b,n,"Scout");*u=42;return i==1;}
constexpr int MIRV_TF2_MAX_BONES=256;
struct MirvTf2BoneName_t{char name[64];int parent;};
int Mirv_Tf2_GetBoneNames(int entity,MirvTf2BoneName_t*out){if(entity!=1)return 0;snprintf(out[0].name,64,"root");snprintf(out[1].name,64,"bip_head");return 2;}
int tracedEntity=1;bool traceAvailable=true;
bool Mirv_Tf2_TraceEntity(const double*,const double*,int&out){out=tracedEntity;return traceAvailable;}
using UINT=unsigned int;using DWORD=unsigned long;using WPARAM=uintptr_t;using LPARAM=intptr_t;
constexpr UINT WM_KEYDOWN=0x100,WM_KEYUP=0x101,WM_SYSKEYDOWN=0x104,WM_SYSKEYUP=0x105;
constexpr UINT VK_F1=0x70,VK_INSERT=0x2D,VK_HOME=0x24,VK_END=0x23,VK_PRIOR=0x21,VK_NEXT=0x22,VK_LEFT=0x25,VK_UP=0x26,VK_RIGHT=0x27,VK_DOWN=0x28,VK_CONTROL=0x11,VK_SHIFT=0x10;
bool controlHeld=false,shiftHeld=false;
short GetKeyState(UINT key){return ((key==VK_CONTROL&&controlHeld)||(key==VK_SHIFT&&shiftHeld))?short(0x8000):0;}
DWORD GetTickCount(){static DWORD tick=0;return tick+=16;}
#define _stricmp strcasecmp
void Tier0_Msg(const char*,...){}void Tier0_Warning(const char*,...){}
namespace AfxHookSource{namespace Gui{bool SetOverlayOpen(bool b){CameraOverlay::SetOpen(b);return true;}}namespace CameraOverlay{namespace Recording{const char*GetName(){return "test";}void SetName(const char*){}bool ScreenEnabled(){return true;}bool IsActive(){return false;}}}}
struct Rect{ImVec2 Min,Max;};std::map<std::string,Rect>widgets;
bool trackButton(const char*l,const ImVec2&s=ImVec2(0,0)){bool b=ImGui::Button(l,s);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackTab(const char*l,bool*p=nullptr,ImGuiTabItemFlags f=0){bool b=ImGui::BeginTabItem(l,p,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackInvisible(const char*l,const ImVec2&s,ImGuiButtonFlags f=0){bool b=ImGui::InvisibleButton(l,s,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
int seqEnd=0,viewStart=0,viewEnd=0;Rect seqRect,graphRect;
bool trackSequencer(const char*id,int32_t*frame,int32_t*start,int32_t*end,const ImVec2&size,ImGuiNeoSequencerFlags flags){seqEnd=*end;seqRect={ImGui::GetCursorScreenPos(),ImGui::GetCursorScreenPos()+size};bool b=ImGui::BeginNeoSequencer(id,frame,start,end,size,flags);if(b)ImGui::GetNeoSequencerView(&viewStart,&viewEnd);return b;}
bool trackSelectable(const char*l,bool selected=false,ImGuiSelectableFlags flags=0,const ImVec2&size=ImVec2(0,0)){bool b=ImGui::Selectable(l,selected,flags,size);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackArrayCombo(const char*l,int*v,const char*const items[],int count,int height=-1){bool b=ImGui::Combo(l,v,items,count,height);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackSlider(const char*l,float*v,float min,float max,const char*format,ImGuiSliderFlags flags=0){bool b=ImGui::SliderFloat(l,v,min,max,format,flags);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackCombo(const char*l,const char*p,ImGuiComboFlags f=0){bool b=ImGui::BeginCombo(l,p,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
'''
s=s.replace('ImGui::Button(', 'trackButton(').replace('ImGui::SmallButton(', 'trackButton(').replace('ImGui::BeginTabItem(', 'trackTab(').replace('ImGui::BeginNeoSequencer(', 'trackSequencer(').replace('ImGui::Selectable(', 'trackSelectable(').replace('ImGui::BeginCombo(', 'trackCombo(').replace('ImGui::InvisibleButton(', 'trackInvisible(').replace('ImGui::Combo(', 'trackArrayCombo(').replace('ImGui::SliderFloat(', 'trackSlider(')
s=s.replace('// Background + border', 'graphRect = {p0,p1};\n// Background + border')
test=r'''
int checks=0;void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
void frame(ImVec2 mouse=ImVec2(-100,-100),bool down=false,float wheel=0,bool middle=false){auto&io=ImGui::GetIO();io.DeltaTime=1.0f/60;io.MousePos=mouse;io.MouseDown[0]=down;io.MouseDown[2]=middle;io.MouseWheel=wheel;widgets.clear();ImGui::NewFrame();AfxHookSource::CameraOverlay::Draw();ImGui::Render();check(ImGui::GetDrawData()!=nullptr,"UI produces draw data");}
void button(const char*l){auto r=widgets.at(l);ImVec2 p((r.Min.x+r.Max.x)/2,(r.Min.y+r.Max.y)/2);frame(p,false);frame(p,true);frame(p,false);frame();}
int main(){try{ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1600,1000);unsigned char*pixels;int w,h;io.Fonts->AddFontDefault();io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 using namespace AfxHookSource::CameraOverlay;
 auto&closedPath=g_Hook_VClient_RenderView.m_CamPath;BeginEdit();closedPath.Add(1,CamPathValue(0,0,0,0,0,0,90));FinishEdit();check(CampathPreview_Enter(),"Native preview enters with overlay closed");controlHeld=true;check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0),"Preview owns undo chord");check(closedPath.GetSize()==0,"History records first console edit before overlay opens");MirvCampathPreview_Update();check(g_CampathPreviewActive,"Preview stays active on empty path so the first capture can be redone");check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',1LL<<30)&&closedPath.GetSize()==0,"Held undo does not repeat");controlHeld=false;check(MirvCampathPreview_KeyEvent(WM_KEYUP,'Z',0),"Undo consumes matching release after Ctrl is released");controlHeld=shiftHeld=true;check(MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0),"Preview owns redo chord");check(closedPath.GetSize()==1,"Closed-overlay edit redoes");controlHeld=shiftHeld=false;MirvCampathPreview_KeyEvent(WM_KEYUP,'Z',0);CampathPreview_Exit();check(!MirvCampathPreview_KeyEvent(WM_KEYDOWN,'Z',0),"Z passes through outside preview");closedPath.Clear();UpdateHistory();
 SetOpen(true);g_Docking=false;frame();frame();check(widgets.count("Add point"),"Campath is the default tab");button("Add point");check(g_Hook_VClient_RenderView.m_CamPath.GetSize()==1,"Overlay captures a full camera key");
 g_MirvTime.Time=12;g_Hook_VClient_RenderView.LastCameraOrigin[0]=200;g_Hook_VClient_RenderView.LastCameraFov=170;button("Add point");auto&path=g_Hook_VClient_RenderView.m_CamPath;check(path.GetSize()==2&&path.CanEval(),"Overlay uses custom interpolation with two keys");
 auto captured=path.CaptureState();button("Undo");check(path.GetSize()==1,"Overlay undo reverses capture");button("Redo");check(path.CaptureState()==captured,"Overlay redo restores pose and tangent metadata");
 auto beforeProfile=path.CaptureState();button("Interpolation");auto*popup=ImGui::FindWindowByName("##Combo_00");check(popup&&popup->Active,"Interpolation chooser opens native ImGui popup");ImVec2 choice(popup->DC.CursorStartPos.x+50,popup->DC.CursorStartPos.y+ImGui::GetTextLineHeightWithSpacing()+ImGui::GetTextLineHeight()/2);frame(choice);frame(choice,true);frame(choice,false);frame();check(path.GetAutoTangentSettings().Method==AutoTangentMethod::CatmullRom,"UI selects whole-path Catmull-Rom");auto profileState=path.CaptureState();button("Undo");check(path.CaptureState()==beforeProfile,"UI profile change undoes atomically");button("Redo");check(path.CaptureState()==profileState,"UI profile change redoes atomically");button("Tension");check(path.GetAutoTangentSettings().Tension>0&&path.GetAutoTangentSettings().Tension<1,"UI tension slider updates live");auto tensionState=path.CaptureState();button("Undo");check(path.CaptureState()==profileState,"Tension drag is one undo step");button("Redo");check(path.CaptureState()==tensionState,"Tension redoes with all channel settings");button("Undo");
 g_ShowSequencer=true;g_ShowCurveEditor=true;g_CurveShow[0]=true;g_CurveShow[3]=true;g_ShowCameraControl=true;g_ShowGizmo=true;g_LastCampathCtx.active=true;g_LastCampathCtx.time=10;g_LastCampathCtx.value=path.GetBegin().GetValue();frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>1000,"Real sequencer, curve editor and gizmo emit geometry with two keys");
 check(seqEnd==12000&&viewStart==0&&viewEnd==12000,"Timeline uses full demo length instead of current tick");auto beforeNav=path.CaptureState();
 ImVec2 seqMouse(seqRect.Min.x+(seqRect.Max.x-seqRect.Min.x)*0.5f,seqRect.Min.y+35);frame(seqMouse);frame(seqMouse,false,4);check(viewEnd-viewStart<12000,"Wheel zooms timeline");int previousStart=viewStart;frame(seqMouse,false,0,true);frame(ImVec2(seqMouse.x-100,seqMouse.y),false,0,true);frame(seqMouse);check(viewStart>previousStart,"Middle drag pans timeline");button("Fit demo");check(viewStart==0&&viewEnd==12000,"Fit restores complete demo view");frame(seqMouse);frame(seqMouse,false,100);check(viewEnd>=viewStart&&viewEnd-viewStart<=1,"Timeline extreme zoom remains a valid single-tick view");frame(seqMouse,false,-200);check(viewStart==0&&viewEnd==12000,"Timeline zoom-out clamps to full demo");demo.TotalTicks=600;frame();check(seqEnd==600&&viewStart==0&&viewEnd==600,"Loading shorter demo resets timeline range and view");demo.TotalTicks=12000;frame();
 ImVec2 graphMouse((graphRect.Min.x+graphRect.Max.x)/2,(graphRect.Min.y+graphRect.Max.y)/2);frame(graphMouse);frame(graphMouse,false,3);check(g_CurveTimeZoom>1,"Wheel zooms graph time");io.AddKeyEvent(ImGuiMod_Ctrl,true);frame(graphMouse,false,2);io.AddKeyEvent(ImGuiMod_Ctrl,false);check(g_CurveValueScale>1,"Ctrl wheel zooms graph values");frame(graphMouse,false,0,true);frame(ImVec2(graphMouse.x-40,graphMouse.y+20),false,0,true);frame();check(g_CurveTimeCenter>0.5&&g_CurveValueOffset>0,"Middle drag pans graph on both axes");check(path.CaptureState()==beforeNav,"Editor navigation leaves camera data untouched");button("Fit graph");check(g_CurveTimeZoom==1&&g_CurveValueScale==1&&g_CurveValueOffset==0,"Fit graph restores all view transforms");
 g_CurveNormalize=false;frame();auto hr=widgets.at("##h1b_0_ch3");ImVec2 handle((hr.Min.x+hr.Max.x)/2,(hr.Min.y+hr.Max.y)/2);frame(handle);frame(handle,true);frame(ImVec2(handle.x,handle.y-20),true);frame(ImVec2(handle.x,handle.y-20),false);frame();auto movedHandle=widgets.at("##h1b_0_ch3");check(std::abs((movedHandle.Min.y+movedHandle.Max.y)/2-(handle.y-20))<1,"Dragging a channel handle follows the pointer on shared value axes");g_CurveNormalize=true;
 path.SelectNone();path.SelectAdd(size_t(0),size_t(0));path.SetTangentMode(CamPath::CH_X,true,true,CamPath::TM_FREE);path.SetTangent(CamPath::CH_X,true,true,7,7);frame();auto edited=path.CaptureState();HistoryUndo(false);check(!(path.CaptureState()==edited),"Curve edit enters shared history");HistoryUndo(true);check(path.CaptureState()==edited,"Curve metadata redoes");
 g_Docking=true;io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>0,"Docked workspace renders through the same UI");g_Docking=false;g_ShowSequencer=g_ShowCameraControl=g_ShowGizmo=false;frame();frame();
 button("Observing");check(widgets.count("1: Scout"),"TF2 player list is visible");check(!widgets.count("View")&&!widgets.count("Attach")&&!widgets.count("Bones"),"Player list has no row buttons");
 g_PreviewFollow=true;path.Enabled_set(true);button("1: Scout");check(commands.back().find("spec_player \"#42\"")!=std::string::npos,"Player click uses explicit user ID, not entity slot or numeric name");check(commands.back().find("spec_autodirector 0")!=std::string::npos&&commands.back().find("spec_mode 4")!=std::string::npos&&commands.back().find("mirv_cam attach clear")!=std::string::npos,"Native spectating clears attachment and selects in-eye view");check(!g_PreviewFollow&&!path.Enabled_get(),"Spectating releases HLAE path overrides");size_t commandsBefore=commands.size();engine.Hltv=false;button("1: Scout");check(commands.size()==commandsBefore,"POV demo reports unsupported native spectating without issuing misleading commands");engine.Hltv=true;check(widgets.count("Bone"),"Selecting player exposes named bones");button("Bone");check(widgets.count("1: bip_head"),"Bone picker lists model bone names");button("1: bip_head");button("Attach selected");check(commands.back().find("mirv_cam attach 1 1")!=std::string::npos,"Named bone selection drives native attachment");button("Trace entity");check(widgets.count("Attach selected"),"Trace selects entity in observing panel");tracedEntity=-1;button("Trace entity");check(widgets.count("Attach selected"),"No-hit trace preserves existing selection");tracedEntity=7;button("Trace entity");button("Attach selected");check(commands.back().find("mirv_cam attach 7 -1")!=std::string::npos,"Trace can select and attach non-player entities");traceAvailable=false;button("Trace entity");check(widgets.count("Attach selected"),"Missing trace hook preserves selected entity");
 button("Recording");check(widgets.count("Start Recording"),"TF2 screen recording panel is available");button("Start Recording");check(!IsOpen(),"Recording closes the overlay before capture");check(commands.back().find("mirv_streams record screen enabled 1")!=std::string::npos,"Recording uses TF2 screen capture");
 ImGui::DestroyContext();std::cout<<checks<<" headless Paper overlay checks passed\n";
 }catch(const std::exception&e){std::cerr<<"After "<<checks<<" UI checks: "<<e.what()<<"\n";return 1;}}
'''
source=out/'overlay_test.cpp';source.write_text(preamble+s+native+test)
cmd=['g++','-std=c++17','-O0','-g','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root),'-I'+str(root/'AfxHookSource'),'-I'+str(imgui),str(source),str(root/'shared/CamPath.cpp'),str(root/'shared/AfxMath.cpp')]+[str(imgui/f) for f in ['imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp']]+[str(root/'shared/overlay/third_party/imgui_neo_sequencer'/f) for f in ['imgui_neo_sequencer.cpp','imgui_neo_internal.cpp']]+[str(root/'shared/overlay/third_party/imguizmo/ImGuizmo.cpp'),'-o',str(out/'overlay_test')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
p=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(p.stdout+p.stderr)
if p.returncode:print(p.stderr[-6500:]);raise SystemExit(p.returncode)
p=subprocess.run([str(out/'overlay_test')],cwd=out,capture_output=True,text=True);print(p.stdout+p.stderr);raise SystemExit(p.returncode)
