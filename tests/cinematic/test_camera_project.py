"""Round-trip real project persistence and classic baking against isolated engine fixtures."""
from pathlib import Path
from types import SimpleNamespace
import argparse, subprocess, os, re, json
parser=argparse.ArgumentParser()
parser.add_argument('--build-dir',type=Path,default=Path('/tmp/hlae-camera-project-tests'))
parser.add_argument('--sanitize',action='store_true')
parser.add_argument('--project',type=Path,help='Optional real project to reproduce missing tracking coverage')
args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
# Reuse the native camera/aim adapter fixture, replacing only its fake path model
# with the real CamPathCollection. Generate it without invoking its separate tests.
script=(root/'tests/cinematic/test_scene_pose.py').read_text()
mock_process=SimpleNamespace(run=lambda *a,**k:SimpleNamespace(returncode=0,stdout='',stderr=''))
scope=dict(root=root,out=out,args=args,os=os,subprocess=mock_process)
exec(script[script.index('rig_source=(root/'):],scope)
s=(out/'camera_rig_adapter.cpp').read_text();s=s[:s.index('int main(){')]
s=s.replace('std::string MirvCam_BakeStatus(){return {};}', 'std::string MirvCam_BakeStatus();')
s=s.replace('namespace advancedfx {struct ICommandArgs {virtual int ArgC()=0;virtual const char*ArgV(int)=0;};}','')
a=s.index('struct PathKeys');b=s.index('#include <cstdarg>',a)
s=s[:a]+'''struct View{CamPath m_CamPath;CamPathCollection m_CamPaths{m_CamPath};double LastCameraOrigin[3]{1,2,3},LastCameraAngles[3]{},LastCameraFov=65;Input*m_MirvInput=&rigInput;void*GetGlobals(){return this;}}g_Hook_VClient_RenderView;
std::string MirvCam_BakeStatus();bool MirvCam_SaveProject(const wchar_t*);bool MirvCam_LoadProject(const wchar_t*);bool MirvCam_Bake(const wchar_t*,const char*,double,double,double);
'''+s[b:]
s='''#include "shared/CamPath.h"
#include "shared/CommandSystem.h"
#include <sstream>
#include <fstream>
#include <iomanip>
#include <set>
#include <codecvt>
#include <locale>
'''+s
s+='''
bool UTF8StringToWideString(const char*s,std::wstring&w){try{w=std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(s);return true;}catch(...){return false;}}
bool StringIBeginsWith(const char*a,const char*b){return strncasecmp(a,b,strlen(b))==0;}
CommandSystem g_CommandSystem(nullptr,nullptr,nullptr);
bool Mirv_Tf2_SaveCamera(std::ostream&out,IMirvVecAngCalc*c){out<<"fixture-calc "<<c->Entity;for(double v:c->Shift)out<<' '<<v;out<<'\\n';return bool(out);}
IMirvVecAngCalc*Mirv_Tf2_LoadCamera(std::istream&in){std::string tag;if(!(in>>tag))return nullptr;int e=-1;double shift[3]{};if(tag=="fixture-calc"){if(!(in>>e>>shift[0]>>shift[1]>>shift[2]))return nullptr;}else{int mode;std::string name;bool calibrated,captured;double v;try{std::stoi(tag);}catch(...){return nullptr;}if(!(in>>mode>>std::quoted(name)>>calibrated>>captured))return nullptr;for(int i=0;i<9;++i)if(!(in>>v))return nullptr;for(double &x:shift)if(!(in>>x))return nullptr;}auto c=new IMirvVecAngCalc(e);for(int i=0;i<3;++i)c->Shift[i]=shift[i];return c;}

namespace AfxHookSource{namespace CameraOverlay{void FlushRecovery(){}void ResetProjectRecovery(){}bool SampleCameraScene(double,advancedfx::SceneCache::Frame&f){if(!cachedFrame)return false;f=*cachedFrame;return true;}}}
#include "AfxHookSource/CinematicProject.inl"
int main(){
 const auto file=(std::filesystem::temp_directory_path()/"hlae-project-roundtrip.hlae").wstring();
 const auto bake=(std::filesystem::temp_directory_path()/"hlae-project-bake.xml").wstring();
 auto &path=g_Hook_VClient_RenderView.m_CamPath;path.PositionInterpMethod_set(CamPath::DI_LINEAR);path.RotationInterpMethod_set(CamPath::QI_SLINEAR);path.FovInterpMethod_set(CamPath::DI_LINEAR);path.Add(0,CamPathValue(0,0,0,0,0,0,90));path.Add(10,CamPathValue(10,0,0,0,0,0,100));
 command({"mirv_cam","add","A \\"quoted\\" camera"});auto name=g_CameraRigName;auto id=g_CameraRigs.at(name).Id;
 advancedfx::CameraClip clip;clip.Time=0;clip.Target=id;assert(MirvCam_EditClip(clip));
 auto marker=g_CommandSystem.AddTickMarker(50,"A command","echo \\"hello\\"; echo next\\nline");assert(marker);g_CommandSystem.EnableTickMarkers(true);
 assert(MirvCam_SaveProject(file.c_str()));assert(MirvCam_SaveProject(file.c_str()));
 command({"mirv_cam","remove"});path.Clear();g_CommandSystem.ClearTickMarkers();
 assert(MirvCam_LoadProject(file.c_str()));assert(g_CameraRigs.count(name)&&g_CameraRigs.at(name).Id==id);assert(path.GetSize()==2&&g_CameraClips.size()==1);assert(g_CommandSystem.GetTickMarkers().size()==1&&!g_CommandSystem.TickMarkersEnabled()&&!path.Enabled_get());assert(!g_RigPreview&&!g_Hook_VClient_RenderView.m_CamPaths.CanUndo());
 assert(g_CommandSystem.GetTickMarkers().front().Command.find("\\nline")!=std::string::npos);
 const auto saved=g_CameraRigs.size();std::ofstream bad(std::filesystem::path(file),std::ios::trunc);bad<<"HLAE_CINEMATIC 99\\n";bad.close();assert(!MirvCam_LoadProject(file.c_str())&&g_CameraRigs.size()==saved&&path.GetSize()==2);
 assert(MirvCam_Bake(bake.c_str(),std::to_string(id).c_str(),0,1,.1));assert(MirvCam_BakeStatus().find("from camera #"+std::to_string(id))!=std::string::npos);CamPath baked;assert(baked.Load(bake.c_str()));assert(std::abs(baked.Eval(.5).X-1)<1e-6);
 // Bake blends and near-instant cuts using the common source evaluator.
 g_Hook_VClient_RenderView.LastCameraOrigin[0]=100;command({"mirv_cam","add","B"});advancedfx::CameraClip next;next.Time=.5;next.Blend=.2;next.Target=g_CameraRigs.at("B").Id;assert(MirvCam_EditClip(next));
 assert(MirvCam_Bake(bake.c_str(),"sequence",0,1,.1));assert(MirvCam_BakeStatus().find("from whole sequence")!=std::string::npos);assert(baked.Load(bake.c_str())&&std::abs(baked.Eval(.6).X-50.5)<.01);
 next.Blend=0;assert(MirvCam_EditClip(next));assert(MirvCam_Bake(bake.c_str(),"sequence",0,1,.1));assert(baked.Load(bake.c_str())&&std::abs(baked.Eval(.499998).X-1)<.01&&std::abs(baked.Eval(.5).X-100)<.01);g_Hook_VClient_RenderView.LastCameraOrigin[0]=1;
 // Exercise the actual mixed camera -> path -> camera export, including rotation and FOV.
 auto changedCamera=g_CameraRigs.at("B").Rig.Pose;changedCamera.Rotation=Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({0,90,0}));changedCamera.Fov=110;assert(MirvCam_EditCamera(g_CameraRigs.at("B").Id,changedCamera));
 advancedfx::CameraClip pathClip;pathClip.Campath=true;pathClip.Target=g_Hook_VClient_RenderView.m_CamPaths.GetId(0);pathClip.Time=1.2;pathClip.Blend=.1;assert(MirvCam_EditClip(pathClip));advancedfx::CameraClip returnClip;returnClip.Target=g_CameraRigs.at("B").Id;returnClip.Time=1.8;returnClip.Blend=.2;assert(MirvCam_EditClip(returnClip));if(!MirvCam_Bake(bake.c_str(),"sequence",-1,2,.05)){std::cerr<<MirvCam_BakeStatus()<<"\\n";std::abort();}assert(baked.Load(bake.c_str()));assert(std::abs(baked.Eval(-.5).X-1)<.01&&std::abs(baked.Eval(1.4).X-1.4)<.01&&std::abs(baked.Eval(1.9).X-50.95)<.02);assert(std::abs(baked.Eval(2).GetEulerAngles().Yaw-90)<.01&&std::abs(baked.Eval(2).Fov-110)<.01);assert(MirvCam_BakeStatus().find("All sampled poses are identical")==std::string::npos);
 // Attached/aimed settings and identity survive save/load with unavailable live entities.
 advancedfx::CameraPose target;target.Position[0]=100;targets[5]=target;playerUsers[5]=6;
 command({"mirv_cam","attach","5","-1","0","0","0"});const auto attached=g_CameraRigName;const auto identity=g_CameraRigs.at(attached).CacheIdentity;
 assert(aimCommand({"mirv_aim","camera",name.c_str(),"entityIndex","5"}));assert(aimCommand({"mirv_aim","camera",name.c_str(),"active","1"}));assert(aimCommand({"mirv_aim","camera",name.c_str(),"snapTo","0"}));
 assert(MirvCam_SaveProject(file.c_str()));command({"mirv_cam","preview",attached.c_str()});targets.clear();assert(MirvCam_LoadProject(file.c_str())&&!g_RigPreview&&!nativePreview);assert(g_CameraRigs.at(attached).Rig.Attached&&g_CameraRigs.at(attached).CacheIdentity==identity);assert(g_CameraRigs.at(name).Aim->Active&&!g_CameraRigs.at(name).Aim->SnapTo&&g_CameraRigs.at(name).AimTarget);
 assert(!MirvCam_Bake(bake.c_str(),attached.c_str(),0,1,.1));
 auto frame=std::make_shared<advancedfx::SceneCache::Frame>();advancedfx::SceneCache::Entity e;e.Identity=identity;e.Origin[0]=500;frame->Entities.push_back(e);cachedFrame=frame;
 const auto previous= g_CameraRigs.at(attached).LastWorld.Position[0];assert(MirvCam_Bake(bake.c_str(),attached.c_str(),0,1,.1));assert(g_CameraRigs.at(attached).LastWorld.Position[0]==previous&&!g_BakingCamera);
 advancedfx::CameraPose observed;observed.Position[0]=123;observed.Rotation=Afx::Math::Quaternion::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({10,30,0}));observed.Fov=80;frame->Cameras.push_back({g_CameraRigs.at(name).Id,g_CameraRigs.at(name).Definition,observed});assert(MirvCam_Bake(bake.c_str(),name.c_str(),0,1,.1));assert(baked.Load(bake.c_str())&&std::abs(baked.Eval(.5).X-123)<.01&&std::abs(baked.Eval(.5).GetEulerAngles().Yaw-30)<.01);frame->Cameras.clear();
 // Dense classic XML remains loadable independently of the drawing stress fixture.
 CamPath dense;dense.PositionInterpMethod_set(CamPath::DI_LINEAR);dense.RotationInterpMethod_set(CamPath::QI_SLINEAR);dense.FovInterpMethod_set(CamPath::DI_LINEAR);auto denseState=dense.CaptureState();for(int i=0;i<=4096;++i)denseState.Keys.emplace(i*.01,CamPathValue(i*.01,5*std::sin(i*.02),0,0,0,0,90));dense.RestoreState(denseState);assert(dense.ExportClassic(bake.c_str(),.01));CamPath denseLoaded;assert(denseLoaded.Load(bake.c_str())&&denseLoaded.GetSize()>=4097&&std::abs(denseLoaded.Eval(20).X-20)<1e-6);
 // Unsupported custom aim serialization and malformed/truncated input are transactional.
 std::ifstream savedInput(std::filesystem::path(file),std::ios::binary);std::string originalBytes((std::istreambuf_iterator<char>(savedInput)),{});savedInput.close();
 auto custom=new IMirvVecAngCalc(5);g_CameraRigs.at(name).Aim->Source_set(custom);assert(!MirvCam_SaveProject(file.c_str()));std::ifstream unchanged(std::filesystem::path(file),std::ios::binary);std::string unchangedBytes((std::istreambuf_iterator<char>(unchanged)),{});assert(unchangedBytes==originalBytes);unchanged.close();assert(MirvCam_LoadProject(file.c_str()));
 const auto countBefore=g_CameraRigs.size();for(size_t cut:{size_t(0),originalBytes.size()/2,originalBytes.size()-5}){std::ofstream truncated(std::filesystem::path(file),std::ios::binary|std::ios::trunc);truncated.write(originalBytes.data(),cut);truncated.close();assert(!MirvCam_LoadProject(file.c_str())&&g_CameraRigs.size()==countBefore&&path.GetSize()==2);}
 std::filesystem::remove(std::filesystem::path(file));std::filesystem::remove(std::filesystem::path(bake));std::cout<<"Project persistence and classic bake checks passed\\n";
}
'''
if args.project:
 injected=r'''
 cachedFrame.reset();cached=false;assert(MirvCam_LoadProject(PROJECT));g_CameraDemoKey=g_SequenceDemo;
 const double first=g_CameraClips.front().Time,last=g_CameraClips.back().Time+g_CameraClips.back().Blend+1;
 auto previousFile=std::filesystem::path(bake);{std::ofstream sentinel(previousFile);sentinel<<"older export";}
 assert(!MirvCam_Bake(bake.c_str(),"sequence",first,last,.1));std::cout<<"Actual project without target coverage: "<<MirvCam_BakeStatus()<<"\n";assert(MirvCam_BakeStatus().find("Camera 1")!=std::string::npos);{std::ifstream retained(previousFile);std::string value;std::getline(retained,value);assert(value=="older export");}
 auto tracking=std::make_shared<advancedfx::SceneCache::Frame>();auto &trackedRig=g_CameraRigs.at("Camera 1");tracking->Cameras.push_back({trackedRig.Id,trackedRig.Definition,trackedRig.LastWorld});cachedFrame=tracking;
 assert(MirvCam_Bake(bake.c_str(),"sequence",first,last,.1));CamPath result;assert(result.Load(bake.c_str()));const auto start=result.Eval(first),end=result.Eval(last);assert(std::abs(start.X-end.X)>1);std::cout<<"Actual project with recorded camera coverage: "<<MirvCam_BakeStatus()<<"\n";
 std::filesystem::remove(previousFile);
 '''.replace('PROJECT','L'+json.dumps(str(args.project.resolve())))
 s=s.replace(' std::filesystem::remove(std::filesystem::path(file));',injected+' std::filesystem::remove(std::filesystem::path(file));',1)
source=out/'project.cpp';source.write_text(s)
cmd=['g++','-std=c++17','-O1','-g','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root),'-I'+str(root/'AfxHookSource'),str(source),str(root/'shared/CamPath.cpp'),str(root/'shared/AfxMath.cpp'),str(root/'shared/CommandSystem.cpp'),str(root/'shared/AfxConsole.cpp'),'-o',str(out/'project')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
# Cache the large shared translation units between iterations; always rebuild
# the adapter because it includes the implementation being tested.
base=cmd[:cmd.index(str(source))];objects=[]
headers=max(p.stat().st_mtime for p in (root/'shared').rglob('*.h') if p.name not in ('SceneCache.h','CameraRig.h'))
for arg in cmd[cmd.index(str(source))+1:cmd.index('-o')]:
 path=Path(arg);obj=out/(path.name+('.asan.o' if args.sanitize else '.o'))
 if not obj.exists() or obj.stat().st_mtime<max(path.stat().st_mtime,headers):
  r=subprocess.run(base+['-c',str(path),'-o',str(obj)],capture_output=True,text=True)
  if r.returncode:print(r.stderr[-8000:]);raise SystemExit(r.returncode)
 objects.append(str(obj))
cmd=base+[str(source)]+objects+['-o',str(out/'project')]
r=subprocess.run(cmd,capture_output=True,text=True)
if r.returncode:print(r.stderr[-8000:]);raise SystemExit(r.returncode)
r=subprocess.run([str(out/'project')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(r.stdout+r.stderr)
raise SystemExit(r.returncode)
