"""Round-trip real project persistence and classic baking against isolated engine fixtures."""
from pathlib import Path
from types import SimpleNamespace
import argparse, subprocess, os, re
parser=argparse.ArgumentParser()
parser.add_argument('--build-dir',type=Path,default=Path('/tmp/hlae-camera-project-tests'))
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args();root=Path(__file__).resolve().parents[2];out=args.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
# Reuse the native camera/aim adapter fixture, replacing only its fake path model
# with the real CamPathCollection. Generate it without invoking its separate tests.
script=(root/'tests/cinematic/test_scene_pose.py').read_text()
mock_process=SimpleNamespace(run=lambda *a,**k:SimpleNamespace(returncode=0,stdout='',stderr=''))
scope=dict(root=root,out=out,args=args,os=os,subprocess=mock_process)
exec(script[script.index('rig_source=(root/'):],scope)
s=(out/'camera_rig_adapter.cpp').read_text();s=s[:s.index('int main(){')]
s=s.replace('namespace advancedfx {struct ICommandArgs {virtual int ArgC()=0;virtual const char*ArgV(int)=0;};}','')
a=s.index('struct PathKeys');b=s.index('#include <cstdarg>',a)
s=s[:a]+'''struct View{CamPath m_CamPath;CamPathCollection m_CamPaths{m_CamPath};double LastCameraOrigin[3]{1,2,3},LastCameraAngles[3]{},LastCameraFov=65;Input*m_MirvInput=&rigInput;void*GetGlobals(){return this;}}g_Hook_VClient_RenderView;
bool MirvCam_SaveProject(const wchar_t*);bool MirvCam_LoadProject(const wchar_t*);bool MirvCam_Bake(const wchar_t*,const char*,double,double,double);
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
bool Mirv_Tf2_SaveCamera(std::ostream&out,IMirvVecAngCalc*c){out<<c->Entity;for(double v:c->Shift)out<<' '<<v;out<<'\\n';return bool(out);}
IMirvVecAngCalc*Mirv_Tf2_LoadCamera(std::istream&in){int e;if(!(in>>e))return nullptr;auto c=new IMirvVecAngCalc(e);for(auto &v:c->Shift)if(!(in>>v)){delete c;return nullptr;}return c;}
namespace AfxHookSource{namespace CameraOverlay{void FlushRecovery(){}void ResetProjectRecovery(){}bool SampleCameraScene(double,advancedfx::SceneCache::Frame&f){if(!cachedFrame)return false;f=*cachedFrame;return true;}}}
#include "AfxHookSource/CinematicProject.inl"
int main(){
 const auto file=(std::filesystem::temp_directory_path()/"hlae-project-roundtrip.hlae").wstring();
 const auto bake=(std::filesystem::temp_directory_path()/"hlae-project-bake.xml").wstring();
 auto &path=g_Hook_VClient_RenderView.m_CamPath;path.Add(0,CamPathValue(0,0,0,0,0,0,90));path.Add(10,CamPathValue(10,0,0,0,0,0,100));
 command({"mirv_cam","add","A \\"quoted\\" camera"});auto name=g_CameraRigName;auto id=g_CameraRigs.at(name).Id;
 advancedfx::CameraClip clip;clip.Time=0;clip.Target=id;assert(MirvCam_EditClip(clip));
 auto marker=g_CommandSystem.AddTickMarker(50,"A command","echo \\"hello\\"; echo next\\nline");assert(marker);g_CommandSystem.EnableTickMarkers(true);
 assert(MirvCam_SaveProject(file.c_str()));assert(MirvCam_SaveProject(file.c_str()));
 command({"mirv_cam","remove"});path.Clear();g_CommandSystem.ClearTickMarkers();
 assert(MirvCam_LoadProject(file.c_str()));assert(g_CameraRigs.count(name)&&g_CameraRigs.at(name).Id==id);assert(path.GetSize()==2&&g_CameraClips.size()==1);assert(g_CommandSystem.GetTickMarkers().size()==1&&!g_CommandSystem.TickMarkersEnabled()&&!path.Enabled_get());assert(!g_RigPreview&&!g_Hook_VClient_RenderView.m_CamPaths.CanUndo());
 assert(g_CommandSystem.GetTickMarkers().front().Command.find("\\nline")!=std::string::npos);
 const auto saved=g_CameraRigs.size();std::ofstream bad(std::filesystem::path(file),std::ios::trunc);bad<<"HLAE_CINEMATIC 99\\n";bad.close();assert(!MirvCam_LoadProject(file.c_str())&&g_CameraRigs.size()==saved&&path.GetSize()==2);
 assert(MirvCam_Bake(bake.c_str(),std::to_string(id).c_str(),0,1,.1));CamPath baked;assert(baked.Load(bake.c_str()));assert(std::abs(baked.Eval(.5).X-1)<1e-6);
 // Bake blends and near-instant cuts using the common source evaluator.
 g_Hook_VClient_RenderView.LastCameraOrigin[0]=100;command({"mirv_cam","add","B"});advancedfx::CameraClip next;next.Time=.5;next.Blend=.2;next.Target=g_CameraRigs.at("B").Id;assert(MirvCam_EditClip(next));
 assert(MirvCam_Bake(bake.c_str(),"sequence",0,1,.1));assert(baked.Load(bake.c_str())&&std::abs(baked.Eval(.6).X-50.5)<.01);
 next.Blend=0;assert(MirvCam_EditClip(next));assert(MirvCam_Bake(bake.c_str(),"sequence",0,1,.1));assert(baked.Load(bake.c_str())&&std::abs(baked.Eval(.499998).X-1)<.01&&std::abs(baked.Eval(.5).X-100)<.01);g_Hook_VClient_RenderView.LastCameraOrigin[0]=1;
 // Attached/aimed settings and identity survive save/load with unavailable live entities.
 advancedfx::CameraPose target;target.Position[0]=100;targets[5]=target;playerUsers[5]=6;
 command({"mirv_cam","attach","5","-1","0","0","0"});const auto attached=g_CameraRigName;const auto identity=g_CameraRigs.at(attached).CacheIdentity;
 assert(aimCommand({"mirv_aim","camera",name.c_str(),"entityIndex","5"}));assert(aimCommand({"mirv_aim","camera",name.c_str(),"active","1"}));assert(aimCommand({"mirv_aim","camera",name.c_str(),"snapTo","0"}));
 assert(MirvCam_SaveProject(file.c_str()));command({"mirv_cam","preview",attached.c_str()});targets.clear();assert(MirvCam_LoadProject(file.c_str())&&!g_RigPreview&&!nativePreview);assert(g_CameraRigs.at(attached).Rig.Attached&&g_CameraRigs.at(attached).CacheIdentity==identity);assert(g_CameraRigs.at(name).Aim->Active&&!g_CameraRigs.at(name).Aim->SnapTo&&g_CameraRigs.at(name).AimTarget);
 assert(!MirvCam_Bake(bake.c_str(),attached.c_str(),0,1,.1));
 auto frame=std::make_shared<advancedfx::SceneCache::Frame>();advancedfx::SceneCache::Entity e;e.Identity=identity;e.Origin[0]=500;frame->Entities.push_back(e);cachedFrame=frame;
 const auto previous= g_CameraRigs.at(attached).LastWorld.Position[0];assert(MirvCam_Bake(bake.c_str(),attached.c_str(),0,1,.1));assert(g_CameraRigs.at(attached).LastWorld.Position[0]==previous&&!g_BakingCamera);
 // Unsupported custom aim serialization and malformed/truncated input are transactional.
 std::ifstream savedInput(std::filesystem::path(file),std::ios::binary);std::string originalBytes((std::istreambuf_iterator<char>(savedInput)),{});savedInput.close();
 auto custom=new IMirvVecAngCalc(5);g_CameraRigs.at(name).Aim->Source_set(custom);assert(!MirvCam_SaveProject(file.c_str()));std::ifstream unchanged(std::filesystem::path(file),std::ios::binary);std::string unchangedBytes((std::istreambuf_iterator<char>(unchanged)),{});assert(unchangedBytes==originalBytes);unchanged.close();assert(MirvCam_LoadProject(file.c_str()));
 const auto countBefore=g_CameraRigs.size();for(size_t cut:{size_t(0),originalBytes.size()/2,originalBytes.size()-5}){std::ofstream truncated(std::filesystem::path(file),std::ios::binary|std::ios::trunc);truncated.write(originalBytes.data(),cut);truncated.close();assert(!MirvCam_LoadProject(file.c_str())&&g_CameraRigs.size()==countBefore&&path.GetSize()==2);}
 std::filesystem::remove(std::filesystem::path(file));std::filesystem::remove(std::filesystem::path(bake));std::cout<<"Project persistence and classic bake checks passed\\n";
}
'''
source=out/'project.cpp';source.write_text(s)
cmd=['g++','-std=c++17','-O1','-g','-Dabstract=','-include',str(root/'tests/cinematic/msvc_crt_compat.h'),'-I'+str(root),'-I'+str(root/'AfxHookSource'),str(source),str(root/'shared/CamPath.cpp'),str(root/'shared/AfxMath.cpp'),str(root/'shared/CommandSystem.cpp'),str(root/'shared/AfxConsole.cpp'),'-o',str(out/'project')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
# Cache the large shared translation units between iterations; always rebuild
# the adapter because it includes the implementation being tested.
base=cmd[:cmd.index(str(source))];objects=[]
headers=max(p.stat().st_mtime for p in (root/'shared').rglob('*.h'))
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
