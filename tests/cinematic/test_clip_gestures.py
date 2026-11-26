"""Portable regression of the real clip-track handlers with a deterministic UI event fixture.

This checks gesture state and edit dispatch; actual ImGui hit-testing remains covered
by the separate headless overlay suite and in-game testing.
"""
from pathlib import Path
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[2]
s = (root / 'AfxHookSource/CameraOverlay.cpp').read_text()
focus = s[s.index('enum class ClipInspectorFocus'):s.index('static bool g_ShowTransitionGuide')]
code = s[s.index('static int FreeModifierTrack('):s.index('static void DrawTransitionTrajectoryGraph(')]
fixture = r'''
#include "shared/CameraRig.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <iostream>
using advancedfx::CameraClip;
using advancedfx::SequenceModifierClip;
constexpr auto none = ~std::uint64_t(0);
#define IM_COL32(r,g,b,a) ((unsigned(r))|(unsigned(g)<<8)|(unsigned(b)<<16)|(unsigned(a)<<24))
constexpr int ImGuiHoveredFlags_AllowWhenBlockedByActiveItem=0,ImGuiNeoTimelineFlags_None=0,ImGuiKey_Escape=0;
namespace ImGui {
 using FrameIndexType=int;
 struct IO{bool KeyShift=false,KeyAlt=false,KeyCtrl=false;}io;
 IO& GetIO(){return io;}
 bool down=false,click=false,release=false,right=false,escape=false;
 int tick=0,focusCount=0;
 std::string row,mouseRow,popup,menu;
 std::vector<std::string> ids;
 struct Span{int Begin,End;std::string Caption;bool Interactive;};
 std::vector<Span> spans;
 bool GetNeoSequencerMouseFrame(int *out,int){*out=tick;return true;}
 bool GetNeoTimelineMouseFrame(int*out){*out=tick;return true;}
 bool IsMouseDown(int){return down;}bool IsMouseReleased(int){return release;}
 bool IsMouseClicked(int b){return b==0?click:right;}bool IsKeyPressed(int){return escape;}
 bool BeginNeoTimelineEx(const char *label,void*,int){row=label;return true;}
 bool BeginNeoGroup(const char *label,bool*){row=label;return false;}
 void EndNeoTimeLine(){}void EndNeoGroup(){}void NeoCancelSelection(){}void MarkIniSettingsDirty(){}
 void SetKeyboardFocusHere(){++focusCount;}
 bool NeoTimelineSpan(int a,int b,unsigned,const char*caption=nullptr,bool*hover=nullptr,bool=false,bool=false,int=-1,bool=false,bool interactive=true){spans.push_back({a,b,caption?caption:"",interactive});const bool hit=interactive&&row==mouseRow&&(a==b?std::abs(tick-a)<=2:tick>=a&&tick<b);if(hover)*hover=hit;return hit&&click;}
 void PushID(const char*id){ids.emplace_back(id);}void PopID(){ids.pop_back();}
 void OpenPopup(const char*){popup=ids.back();}bool BeginPopup(const char*){return popup==ids.back();}
 bool MenuItem(const char*label){if(menu==label){menu.clear();popup.clear();return true;}return false;}
 void EndPopup(){}
}
std::uint64_t g_SelectedSequenceModifier=none,g_SelectedCameraClip=none,g_SelectedModifierDefinition=0;
bool g_FocusModifierLibrary=false,g_OpenCameraClips=false,g_LockKeyTimes=false,g_SequencerSourcesOpen=false;
std::string g_DocumentStatus;
std::vector<SequenceModifierClip> adjustments;
std::vector<CameraClip> cameras;
struct Definition{std::uint64_t Id;std::string Name;};
std::vector<Definition> MirvCam_Modifiers(){return {{1,"Shake"}};}
std::vector<SequenceModifierClip> MirvCam_SequenceModifiers(){return adjustments;}
std::vector<CameraClip> MirvCam_Clips(){return cameras;}
struct Marker{int Tick;};struct Commands{std::vector<Marker> markers;const auto& GetTickMarkers(){return markers;}}g_CommandSystem;
bool SceneCachePreviewActive(){return true;}double SceneTickInterval(){return .01;}
// Offset cached client time from demo time to expose clock-domain errors.
bool CinematicClock(double from,bool toDemo,double &to,bool){to=from+(toDemo?-100:100);return true;}
std::string CameraClipSourceName(bool,std::uint64_t){return "Camera";}
bool MirvCam_EditSequenceModifier(SequenceModifierClip &c,std::uint64_t*){
 if(!c.Valid())return false;for(auto &other:adjustments)if(other.Id!=c.Id&&c.Overlaps(other))return false;
 if(c.Id==none){static std::uint64_t id=20;c.Id=id++;adjustments.push_back(c);return true;}
 for(auto &other:adjustments)if(other.Id==c.Id){other=c;return true;}return false;
}
bool MirvCam_RemoveSequenceModifier(std::uint64_t id){auto it=std::find_if(adjustments.begin(),adjustments.end(),[&](const auto &c){return c.Id==id;});if(it==adjustments.end())return false;adjustments.erase(it);return true;}
bool MirvCam_EditClip(CameraClip &c){if(!c.ValidTransition())return false;for(auto &other:cameras)if(other.Id!=c.Id&&std::abs(other.Time-c.Time)<1e-9)return false;
 if(c.Id==none){static std::uint64_t id=20;c.Id=id++;cameras.push_back(c);}else{for(auto &other:cameras)if(other.Id==c.Id)other=c;}std::sort(cameras.begin(),cameras.end(),[](auto&a,auto&b){return a.Time<b.Time;});return true;}
bool MirvCam_RemoveClip(std::uint64_t id){auto it=std::find_if(cameras.begin(),cameras.end(),[&](const auto&c){return c.Id==id;});if(it==cameras.end())return false;cameras.erase(it);return true;}
'''
tests = r'''
int main(){
 using namespace ImGui;
 auto toTick=[](double t){return int(std::llround((t-100)*100));};
 auto frame=[&](bool camera,int pos,bool held=false,bool pressed=false,bool released=false,bool alt=false,bool shift=false,bool ctrl=false,bool cancel=false,bool rc=false){
  tick=pos;down=held;click=pressed;release=released;io.KeyAlt=alt;io.KeyShift=shift;io.KeyCtrl=ctrl;escape=cancel;right=rc;spans.clear();mouseRow=camera?"Camera sources":"Modifier track 1";
  bool contextOpened=false;if(camera)DrawCameraClipTrack(toTick,0,1800,contextOpened);else DrawSequenceModifierTracks(toTick,0,1800,contextOpened);
  if(rc)assert(contextOpened);
 };
 SequenceModifierClip a;a.Id=1;a.Definition=1;a.Begin=101;a.End=103;a.FadeIn=.3;a.FadeOut=.4;adjustments={a};g_SelectedSequenceModifier=1;
 frame(false,180);for(auto label:{"Fade in","Fade in end","Fade out","Fade out start"})assert(std::any_of(spans.begin(),spans.end(),[&](const Span&s){return s.Caption==label&&!s.Interactive;}));
 frame(false,180,true,true,false,true);frame(false,180,true,false,false,true);frame(false,180,false,false,true,true);assert(adjustments.size()==1&&adjustments[0]==a);
 frame(false,180,true,true,false,true);frame(false,230,true,false,false,true,true);assert(adjustments.size()==1&&adjustments[0]==a);frame(false,230,false,false,true,true,true);assert(adjustments.size()==2&&adjustments[0]==a&&adjustments[1].Track==1&&std::abs(adjustments[1].Begin-101.5)<1e-9&&adjustments[1].FadeIn==a.FadeIn&&g_SelectedSequenceModifier==adjustments[1].Id);
 adjustments={a};g_SelectedSequenceModifier=1;frame(false,180,true,true,false,true);frame(false,240,true,false,false,true,true);frame(false,240,true,false,false,true,true,false,true);frame(false,240,false,false,true);assert(adjustments.size()==1&&adjustments[0]==a);
 // Ctrl-drag is fade editing, not a move/duplicate. Shift bypasses snapping.
 frame(false,130,true,true,false,false,false,true);frame(false,145,true,false,false,false,false,true);frame(false,145,false,false,true,false,false,true);assert(adjustments.size()==1&&std::abs(adjustments[0].FadeIn-.45)<1e-9&&adjustments[0].Begin==a.Begin);adjustments={a};
 // Both zero-duration handles are draggable, including the right edge outside the block.
 auto zero=a;zero.FadeIn=zero.FadeOut=0;adjustments={zero};g_SelectedCameraClip=77;g_SelectedSequenceModifier=1;
 frame(false,100,true,true,false,false,false,true);frame(false,115,true,false,false,false,false,true);frame(false,115,false,false,true,false,false,true);
 assert(std::abs(adjustments[0].FadeIn-.15)<1e-9&&adjustments[0].Begin==a.Begin&&g_SelectedCameraClip==77);
 frame(false,300,true,true,false,false,false,true);frame(false,280,true,false,false,false,false,true);frame(false,280,false,false,true,false,false,true);
 assert(std::abs(adjustments[0].FadeOut-.2)<1e-9&&adjustments[0].End==a.End&&g_SelectedCameraClip==77);
 auto beforeNoHandle=adjustments[0];frame(false,200,true,true,false,false,false,true);frame(false,220,true,false,false,false,false,true);frame(false,220,false,false,true,false,false,true);assert(adjustments[0]==beforeNoHandle);
 adjustments={a};
 g_CommandSystem.markers={{250}};io.KeyShift=false;assert(std::abs(SnapClipDemoTime(2.46,0,1800,false,1)-2.5)<1e-9);io.KeyShift=true;assert(SnapClipDemoTime(2.46,0,1800,false,1)==2.46);
 // The beat grid uses demo time, including a fractional downbeat and cached client offset.
 auto &grid=advancedfx::TimelineTimebase();grid.Mode=2;grid.BPM=128;grid.Origin=.123;grid.Division=4;
 io.KeyShift=false;assert(std::abs(SnapClipDemoTime(2.46,0,1800,false,1)-2.46675)<1e-9);
 frame(false,180,true,true);frame(false,221,true);frame(false,221,false,false,true);
 assert(std::abs(adjustments[0].Begin-(100+grid.SnapTime(1.41)))<1e-9&&std::abs(adjustments[0].End-adjustments[0].Begin-2)<1e-9);
 adjustments={a};g_SelectedSequenceModifier=1;grid={};
 frame(false,180,false,false,false,false,false,false,false,true);assert(g_SelectedSequenceModifier==1&&!popup.empty());menu="Edit duration / end";frame(false,180);assert(g_ClipInspectorFocus==ClipInspectorFocus::AdjustmentEnd);FocusClipField(ClipInspectorFocus::AdjustmentEnd);assert(focusCount==1&&g_ClipInspectorFocus==ClipInspectorFocus::None);
 frame(false,180,false,false,false,false,false,false,false,true);menu="Disable effect";frame(false,180);assert(!adjustments[0].Enabled);frame(false,180,false,false,false,false,false,false,false,true);menu="Delete adjustment";frame(false,180);assert(adjustments.empty());
 CameraClip c;c.Id=1;c.Time=101;c.Target=5;c.Blend=.5;cameras={c};g_SelectedCameraClip=1;g_CommandSystem.markers.clear();
 frame(true,200,true,true,false,true);frame(true,250,true,false,false,true,true);assert(cameras.size()==1&&cameras[0]==c);frame(true,250,false,false,true,true,true);assert(cameras.size()==2&&cameras[0]==c&&std::abs(cameras[1].Time-101.5)<1e-9&&cameras[1].Target==5&&cameras[1].Blend==.5);
 // Ctrl-drag the second clip's transition edge without moving its edit point.
 frame(true,198,true,true,false,false,false,true);frame(true,218,true,false,false,false,false,true);frame(true,218,false,false,true,false,false,true);assert(std::abs(cameras[1].Time-101.5)<1e-9&&std::abs(cameras[1].Blend-.7)<1e-9);
 auto noBlend=cameras[1];noBlend.Blend=0;assert(MirvCam_EditClip(noBlend));
 frame(true,150,true,true,false,false,false,true);frame(true,170,true,false,false,false,false,true);frame(true,170,false,false,true,false,false,true);assert(std::abs(cameras[1].Blend-.2)<1e-9&&std::abs(cameras[1].Time-101.5)<1e-9);
 frame(true,200,false,false,false,false,false,false,false,true);assert(!popup.empty());menu="Edit transition duration";frame(true,200);assert(g_ClipInspectorFocus==ClipInspectorFocus::CameraTransition);FocusClipField(ClipInspectorFocus::CameraTransition);assert(focusCount==2);
 frame(true,200,false,false,false,false,false,false,false,true);menu="Delete camera clip";frame(true,200);assert(cameras.size()==1);
 std::cout<<"Clip gestures, cancellation, shared fades, snapping and context dispatch passed\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source=Path(tmp)/'gesture.cpp'; exe=Path(tmp)/'gesture'
    source.write_text(fixture+focus+code+tests)
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-Dabstract=','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(root),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
