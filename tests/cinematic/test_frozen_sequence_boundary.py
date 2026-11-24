"""Compile the actual sequence boundary evaluator against deterministic camera sources."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'AfxHookSource/MirvCam.cpp').read_text()
body=s[s.index('struct FrozenSequencePose'):s.index('bool EvaluateSequence(double time, advancedfx::CameraPose &pose)\n{',s.index('struct FrozenSequencePose'))]
fixture=r'''
#include <map>
#include <vector>
#include <memory>
#include <string>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cassert>
#include <iostream>
namespace advancedfx {struct CameraPose {double x=0; bool Valid()const{return std::isfinite(x);}}; namespace SceneCache {struct Frame {double ClientTime=0;};}}
struct CameraClip {uint64_t Id,Target;double Start,Blend;bool FreezeOutgoing,Campath=false;int BlendCurve=0;double TransitionStart()const{return Start;}};
struct Aim {bool Active=false,SoftDeactivate=false;std::shared_ptr<Aim> CloneSettings(){return std::make_shared<Aim>(*this);}};
struct NamedCameraRig {struct {bool Attached=false;}Rig;std::shared_ptr<NamedCameraRig> AimTarget;std::shared_ptr<struct Aim> Aim;std::shared_ptr<int>FollowCache;advancedfx::CameraPose LastWorld;};
std::vector<CameraClip>g_CameraClips;std::map<std::string,NamedCameraRig>g_CameraRigs;
auto FindRig(const std::string&key){return g_CameraRigs.find(key);}
bool g_BakingCamera=false,g_InspectingSequence=false,history=false,preview=false;
std::shared_ptr<const advancedfx::SceneCache::Frame>g_BakeFrame;
double g_CameraEvaluationTime=99;int g_CameraEvaluationFrame=7;std::string g_CameraFailureReason="old";
namespace AfxHookSource {namespace CameraOverlay {bool SampleCameraScene(double,advancedfx::SceneCache::Frame&){return history;}bool SceneCachePreviewActive(){return preview;}}}
bool EvaluateClipSource(const CameraClip&clip,double t,advancedfx::CameraPose&out){out.x=clip.Target*100+t;if(!clip.Campath){auto r=FindRig(std::to_string(clip.Target));if(r==g_CameraRigs.end())return false;r->second.LastWorld=out;}return true;}
bool RecoverCachedOutgoing(NamedCameraRig&,double,double,advancedfx::CameraPose&,const CameraClip*){return false;}
bool ApplyClipModifiers(const CameraClip&,double,advancedfx::CameraPose&){return true;}
bool BlendCameraPoses(const advancedfx::CameraPose&a,const advancedfx::CameraPose&b,double t,advancedfx::CameraPose&out,int,const CameraClip*){out.x=a.x+(b.x-a.x)*t;return true;}
'''
test=r'''
int main(){using advancedfx::CameraPose;CameraPose pose;
 g_CameraRigs["1"]={};g_CameraRigs["2"]={};g_CameraClips={{1,1,0,0,false},{2,2,10,4,true}};
 assert(EvaluateSequenceComposite(12,2,pose)&&pose.x==161);assert(g_CameraRigs["1"].LastWorld.x==112);assert(g_CameraEvaluationTime==99&&g_CameraEvaluationFrame==7&&!g_BakingCamera&&!g_InspectingSequence&&g_CameraFailureReason=="old");
 g_SequenceFrozenPoses.clear();g_CameraClips[0].Campath=true;assert(EvaluateSequenceComposite(12,2,pose)&&pose.x==161);
 g_SequenceFrozenPoses.clear();g_CameraClips[0].Campath=false;g_CameraRigs["1"].Rig.Attached=true;assert(EvaluateSequenceComposite(12,2,pose)&&pose.x==212);assert(g_SequenceFrozenPoses.empty());
 assert(EvaluateSequenceComposite(10,2,pose)&&pose.x==110);assert(EvaluateSequenceComposite(12,2,pose)&&pose.x==161);
 g_SequenceFrozenPoses.clear();g_SequencePreviousTime=9.9;g_SequencePreviousPose.x=109.9;assert(EvaluateSequenceComposite(12,2,pose)&&std::abs(pose.x-160.95)<1e-8);
 g_SequenceFrozenPoses.clear();g_SequencePreviousTime=8;assert(EvaluateSequenceComposite(12,2,pose)&&pose.x==212);
 g_SequenceFrozenPoses.clear();history=true;preview=true;assert(EvaluateSequenceComposite(12,2,pose)&&pose.x==161);assert(!g_BakingCamera&&g_CameraRigs["1"].LastWorld.x==112);
 std::cout<<"Frozen release boundaries: deterministic, campath, cached, live, missing and stale history passed\n";
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'boundary.cpp';p.write_text(fixture+body+test);exe=Path(d)/'boundary';subprocess.run(['g++','-std=c++17','-O1',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
