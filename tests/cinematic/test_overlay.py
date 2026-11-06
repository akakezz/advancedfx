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
preamble=r'''
#include "shared/CamPath.h"
#include "AfxHookSource/CameraOverlay.h"
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
struct FakeView{CamPath m_CamPath;MirvInput*m_MirvInput=&input;double LastCameraOrigin[3]={100,-100,50},LastCameraAngles[3]={0,135,0},LastCameraFov=90;int LastWidth=1600,LastHeight=1000;WrpGlobals*GetGlobals(){return &globals;}}g_Hook_VClient_RenderView;
struct CCampathDrawer{bool Draw=false;float Index=18;bool Draw_get(){return Draw;}void Draw_set(bool b){Draw=b;}float GetDrawKeyframeIndex(){return Index;}void SetDrawKeyframeIndex(float x){Index=x;}}g_CampathDrawer;
struct Clock{double Time=10;double GetTime(){return Time;}}g_MirvTime;
std::vector<std::string> commands;
struct Engine{void ClientCmd_Unrestricted(const char*c){commands.emplace_back(c);}}engine;Engine*g_VEngineClient=&engine;
bool GetCurrentDemoTick(int&n){n=int(g_MirvTime.Time*66);return true;}bool GetCurrentDemoTime(double&t){t=g_MirvTime.Time;return true;}bool GetDemoTickFromClientTime(double,double t,int&n){n=int(t*66);return true;}
int Mirv_Tf2_GetMaxPlayers(){return 1;}bool Mirv_Tf2_GetPlayerInfo(int i,char*b,int n,int*u){snprintf(b,n,"Scout");*u=42;return i==1;}
namespace AfxHookSource{namespace Gui{bool SetOverlayOpen(bool b){CameraOverlay::SetOpen(b);return true;}}namespace CameraOverlay{namespace Recording{const char*GetName(){return "test";}void SetName(const char*){}bool ScreenEnabled(){return true;}bool IsActive(){return false;}}}}
struct Rect{ImVec2 Min,Max;};std::map<std::string,Rect>widgets;
bool trackButton(const char*l,const ImVec2&s=ImVec2(0,0)){bool b=ImGui::Button(l,s);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
bool trackTab(const char*l,bool*p=nullptr,ImGuiTabItemFlags f=0){bool b=ImGui::BeginTabItem(l,p,f);widgets[l]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};return b;}
'''
s=s.replace('ImGui::Button(', 'trackButton(').replace('ImGui::BeginTabItem(', 'trackTab(')
test=r'''
int checks=0;void check(bool b,const char*m){++checks;if(!b)throw std::runtime_error(m);}
void frame(ImVec2 mouse=ImVec2(-100,-100),bool down=false){auto&io=ImGui::GetIO();io.DeltaTime=1.0f/60;io.MousePos=mouse;io.MouseDown[0]=down;widgets.clear();ImGui::NewFrame();AfxHookSource::CameraOverlay::Draw();ImGui::Render();check(ImGui::GetDrawData()!=nullptr,"UI produces draw data");}
void button(const char*l){auto r=widgets.at(l);ImVec2 p((r.Min.x+r.Max.x)/2,(r.Min.y+r.Max.y)/2);frame(p,false);frame(p,true);frame(p,false);frame();}
int main(){try{ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1600,1000);unsigned char*pixels;int w,h;io.Fonts->AddFontDefault();io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
 using namespace AfxHookSource::CameraOverlay;SetOpen(true);g_Docking=false;frame();frame();check(widgets.count("Add point"),"Campath is the default tab");button("Add point");check(g_Hook_VClient_RenderView.m_CamPath.GetSize()==1,"Overlay captures a full camera key");
 g_MirvTime.Time=12;g_Hook_VClient_RenderView.LastCameraOrigin[0]=200;button("Add point");auto&path=g_Hook_VClient_RenderView.m_CamPath;check(path.GetSize()==2&&path.CanEval(),"Overlay uses custom interpolation with two keys");
 auto captured=path.CaptureState();button("Undo");check(path.GetSize()==1,"Overlay undo reverses capture");button("Redo");check(path.CaptureState()==captured,"Overlay redo restores pose and tangent metadata");
 g_ShowSequencer=true;g_ShowCurveEditor=true;g_CurveShow[0]=true;g_CurveShow[3]=true;g_ShowCameraControl=true;g_ShowGizmo=true;g_LastCampathCtx.active=true;g_LastCampathCtx.time=10;g_LastCampathCtx.value=path.GetBegin().GetValue();frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>1000,"Real sequencer, curve editor and gizmo emit geometry with two keys");
 path.SelectNone();path.SelectAdd(size_t(0),size_t(0));path.SetTangentMode(CamPath::CH_X,true,true,CamPath::TM_FREE);path.SetTangent(CamPath::CH_X,true,true,7,7);frame();auto edited=path.CaptureState();HistoryUndo(false);check(!(path.CaptureState()==edited),"Curve edit enters shared history");HistoryUndo(true);check(path.CaptureState()==edited,"Curve metadata redoes");
 g_Docking=true;io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;frame();frame();check(ImGui::GetDrawData()->TotalVtxCount>0,"Docked workspace renders through the same UI");g_Docking=false;g_ShowSequencer=g_ShowCameraControl=g_ShowGizmo=false;frame();frame();
 button("Observing");check(widgets.count("Attach"),"TF2 player row is visible");button("Attach");check(commands.back().find("mirv_cam attach 1 -1")!=std::string::npos,"Observer uses existing TF2 attachment command");
 button("Recording");check(widgets.count("Start Recording"),"TF2 screen recording panel is available");button("Start Recording");check(!IsOpen(),"Recording closes the overlay before capture");check(commands.back().find("mirv_streams record screen enabled 1")!=std::string::npos,"Recording uses TF2 screen capture");
 ImGui::DestroyContext();std::cout<<checks<<" headless Paper overlay checks passed\n";
 }catch(const std::exception&e){std::cerr<<"After "<<checks<<" UI checks: "<<e.what()<<"\n";return 1;}}
'''
source=out/'overlay_test.cpp';source.write_text(preamble+s+test)
cmd=['g++','-std=c++17','-O0','-g','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root),'-I'+str(root/'AfxHookSource'),'-I'+str(imgui),str(source),str(root/'shared/CamPath.cpp'),str(root/'shared/AfxMath.cpp')]+[str(imgui/f) for f in ['imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp']]+[str(root/'shared/overlay/third_party/imgui_neo_sequencer'/f) for f in ['imgui_neo_sequencer.cpp','imgui_neo_internal.cpp']]+[str(root/'shared/overlay/third_party/imguizmo/ImGuizmo.cpp'),'-o',str(out/'overlay_test')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
p=subprocess.run(cmd,capture_output=True,text=True);(out/'build.log').write_text(p.stdout+p.stderr)
if p.returncode:print(p.stderr[-6500:]);raise SystemExit(p.returncode)
p=subprocess.run([str(out/'overlay_test')],cwd=out,capture_output=True,text=True);print(p.stdout+p.stderr);raise SystemExit(p.returncode)
