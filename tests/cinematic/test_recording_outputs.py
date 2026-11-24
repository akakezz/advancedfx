"""Exercise production Base/isolate/reference routing and mutation gates."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-output-policy');a=p.parse_args();a.build_dir.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parents[2];s=(r/'AfxHookSource/AfxStreams.cpp').read_text();h=(r/'AfxHookSource/AfxStreams.h').read_text()
methods=s[s.index('bool CAfxStreams::Tf2PassEnabled('):s.index('void CAfxStreams::EngineThread_PassBegin(')]
at=h.index('bool GetRecordScreenEnabled() const');end=h.index('\n\t}',at)+3;getter=h[at:end]
source=r'''
#include <atomic>
#include <cassert>
#include <iostream>
#include "AfxHookSource/Tf2RenderPass.h"
bool g_OpenFortressLegacyMode=false,g_OfNativePasses=false;
bool supported=true;bool Tf2RenderPassSupported(){return supported;}bool Hook_MaterialSystem(){return supported;}
struct Engine{bool Console=false,Demo=true,Skipping=false,Loading=false;bool Con_IsVisible(){return Console;}bool IsPlayingDemo(){return Demo;}bool IsSkippingDemo(){return Skipping;}bool IsDrawingLoadingImage(){return Loading;}}engine,*g_VEngineClient=&engine;
class CAfxStreams {public:
 struct Screen{bool Enabled=true;}screen;Screen*m_RecordScreen=&screen;
 bool m_Recording=false;std::atomic<bool>m_Tf2SeparateConfigured{false},m_Tf2FullReference{false};std::atomic<bool>m_Tf2PassEnabled[5]={};
 bool Tf2PassEnabled(Tf2RenderPass)const;bool SetTf2PassEnabled(Tf2RenderPass,bool);bool SetTf2FullReference(bool);bool ResetTf2Outputs();bool Tf2PassRecording(Tf2RenderPass)const;
'''+getter+r'''
};
'''+methods+r'''
int main(){
 CAfxStreams s;assert(s.GetRecordScreenEnabled()&&s.Tf2PassEnabled(Tf2RenderPass::World));
 assert(s.SetTf2PassEnabled(Tf2RenderPass::Players,false)&&!s.m_Tf2SeparateConfigured);
 assert(s.SetTf2PassEnabled(Tf2RenderPass::Hud,true));
 assert(s.m_Tf2SeparateConfigured&&s.Tf2PassEnabled(Tf2RenderPass::World)&&!s.GetRecordScreenEnabled());
 s.m_Recording=true;assert(s.Tf2PassRecording(Tf2RenderPass::World)&&s.Tf2PassRecording(Tf2RenderPass::Hud));
 assert(!s.SetTf2FullReference(true)&&!s.ResetTf2Outputs()&&!s.SetTf2PassEnabled(Tf2RenderPass::Hud,false));
 g_OpenFortressLegacyMode=true;assert(!s.Tf2PassRecording(Tf2RenderPass::World));g_OfNativePasses=true;assert(s.Tf2PassRecording(Tf2RenderPass::World));
 engine.Demo=false;assert(!s.Tf2PassRecording(Tf2RenderPass::World));engine.Demo=true;engine.Skipping=true;assert(!s.Tf2PassRecording(Tf2RenderPass::World));engine.Skipping=false;engine.Loading=true;assert(!s.Tf2PassRecording(Tf2RenderPass::World));engine.Loading=false;g_OpenFortressLegacyMode=false;
 engine.Console=true;assert(!s.Tf2PassRecording(Tf2RenderPass::World));engine.Console=false;s.m_Recording=false;
 assert(s.SetTf2FullReference(true)&&s.GetRecordScreenEnabled());
 assert(s.SetTf2FullReference(false)&&!s.GetRecordScreenEnabled());
 assert(s.SetTf2PassEnabled(Tf2RenderPass::World,false)&&!s.Tf2PassEnabled(Tf2RenderPass::World)&&s.Tf2PassEnabled(Tf2RenderPass::Hud));
 assert(s.ResetTf2Outputs()&&!s.m_Tf2SeparateConfigured&&s.GetRecordScreenEnabled()&&!s.Tf2PassEnabled(Tf2RenderPass::Hud));
 supported=false;assert(!s.SetTf2PassEnabled(Tf2RenderPass::Players,true)&&!s.m_Tf2SeparateConfigured&&s.GetRecordScreenEnabled());
 std::cout<<"Production recording policy: complementary base by default, optional reference, isolate-only, normal reset, console and recording gates passed\n";
}
'''
f=a.build_dir/'outputs.cpp';f.write_text(source);cmd=['g++','-std=c++17','-O1','-g','-I'+str(r),str(f),'-o',str(a.build_dir/'outputs')]
if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);subprocess.run([str(a.build_dir/'outputs')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
