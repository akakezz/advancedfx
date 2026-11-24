"""Run the actual TF2 frame-stage hook to guard capture/render ordering."""
from pathlib import Path
import argparse, os, subprocess, tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
main=(root/'AfxHookSource/main.cpp').read_text()
hook=main[main.index('void __fastcall new_CVClient_FrameStageNotify_TF2('):main.index('typedef void(__fastcall* CVClient_FrameStageNotify_CSS_t)')]
fixture=r'''
#include <cassert>
#include <iostream>
#define __fastcall
namespace SOURCESDK {namespace TF2 {enum ClientFrameStage_t {FRAME_START,FRAME_NET_UPDATE_END,FRAME_RENDER_START,FRAME_RENDER_END};}}
using Stage=SOURCESDK::TF2::ClientFrameStage_t;
int captures=0,preferences=0,repairs=0,updates=0,resolutions=0,probes=0;
namespace RecordingResolution{void Frame(){++resolutions;}}
void MirvFix_OfInterpolationProbeFrame(){++probes;}
bool g_OfNativePovReady=false;
bool naturalBonesReady=false,renderComplete=false,afterRender=false,observing=false;
void MirvPovSpectate_FrameBegin(){assert(!naturalBonesReady);observing=true;}
void MirvPovSpectate_FrameEnd(){assert(renderComplete&&observing);observing=false;}
void MirvFix_Update(){++updates;}
void MirvFix_PlayerRepairFrameEnd(){++repairs;}
void MirvFix_PlayerAngleTraceFrame(int){}
void MirvFix_RecorderSoundUpdate(){}
void Shared_BeforeFrameRenderStart(){assert(!naturalBonesReady);}
void Shared_AfterFrameRenderEnd(){assert(captures==1);afterRender=true;}
namespace AfxHookSource {namespace CameraOverlay {
void PreferencesFrame(){++preferences;}
void SceneCacheFrame(){assert(naturalBonesReady&&renderComplete&&!observing);assert(!afterRender);++captures;}
}}
void old_CVClient_FrameStageNotify_TF2(void*,
#ifndef _WIN64
void*,
#endif
Stage stage){if(stage==SOURCESDK::TF2::FRAME_RENDER_END)renderComplete=true;}
'''
test=r'''
int main(){
auto stage=[](Stage s){new_CVClient_FrameStageNotify_TF2(nullptr,
#ifndef _WIN64
nullptr,
#endif
s);};
stage(SOURCESDK::TF2::FRAME_START);assert(preferences==1&&updates==1&&captures==0);
stage(SOURCESDK::TF2::FRAME_NET_UPDATE_END);assert(repairs==1&&captures==0);
stage(SOURCESDK::TF2::FRAME_RENDER_START);assert(captures==0); // The live bone cache remains unmodified before drawing.
naturalBonesReady=true; // The engine renders players using its own interpolated poses.
stage(SOURCESDK::TF2::FRAME_RENDER_END);assert(captures==1&&afterRender);
#ifndef _WIN64
captures=preferences=repairs=updates=resolutions=probes=0;
naturalBonesReady=renderComplete=afterRender=observing=false;
old_CVClient_FrameStageNotify_OpenFortress=old_CVClient_FrameStageNotify_TF2;
auto of=[](Stage s){new_CVClient_FrameStageNotify_OpenFortress(nullptr,nullptr,s);};
of(SOURCESDK::TF2::FRAME_START);assert(resolutions==1&&preferences==1&&updates==1);
of(SOURCESDK::TF2::FRAME_NET_UPDATE_END);assert(repairs==1);
of(SOURCESDK::TF2::FRAME_RENDER_START);assert(!observing&&captures==0);
naturalBonesReady=true;
of(SOURCESDK::TF2::FRAME_RENDER_END);assert(captures==1&&probes==1&&afterRender);
captures=preferences=repairs=updates=resolutions=probes=0;
naturalBonesReady=renderComplete=afterRender=observing=false;
g_OfNativePovReady=true;
of(SOURCESDK::TF2::FRAME_START);of(SOURCESDK::TF2::FRAME_RENDER_START);
assert(observing&&captures==0);naturalBonesReady=true;
of(SOURCESDK::TF2::FRAME_RENDER_END);assert(captures==1&&afterRender&&!observing);
#endif
std::cout<<"Scene capture runs after natural rendering, before frame retirement\n";
}
'''
out=Path(tempfile.mkdtemp(prefix='hlae-capture-stage-'))
source=out/'stage.cpp';source.write_text(fixture+hook+test)
for arch in ['x86','x64']:
 command=['g++','-std=c++17','-O0','-g',str(source),'-o',str(out/arch)]
 if arch=='x64':command.insert(1,'-D_WIN64')
 if args.sanitize:command[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 subprocess.run(command,check=True)
 subprocess.run([str(out/arch)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
