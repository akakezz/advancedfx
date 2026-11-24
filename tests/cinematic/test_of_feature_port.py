"""Compile the real OF integrated startup policy, Scene Cache defaults and recording support gate."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
controller = (root/'AfxHookSource/RecordingResolution.inl').read_text()
start = controller.index('bool Supported()')
end = controller.index('\n}', start)+2
fixture = '''#include <cassert>
constexpr int SourceSdkVer_TF2=1,SourceSdkVer_TF2Classified=2,SourceSdkVer_HL2MP=3;
int g_SourceSdkVer=SourceSdkVer_HL2MP;
bool g_OpenFortressLegacyMode=false,g_OfNativeResolution=false;
namespace OfNativeInterfaces { bool known=false; bool KnownClient(){return known;} }
''' + controller[start:end] + '''
int main(){
 assert(!Supported());g_OfNativeResolution=true;assert(!Supported());
 g_OpenFortressLegacyMode=true;assert(!Supported());OfNativeInterfaces::known=true;assert(Supported());g_OfNativeResolution=false;assert(!Supported());
 g_OpenFortressLegacyMode=false;g_SourceSdkVer=SourceSdkVer_TF2;assert(Supported());
 g_SourceSdkVer=SourceSdkVer_TF2Classified;assert(Supported());
}
'''
with tempfile.TemporaryDirectory() as folder:
    source=Path(folder)/'gate.cpp'; exe=Path(folder)/'gate'; source.write_text(fixture)
    subprocess.run(['g++','-std=c++17',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
main=(root/'AfxHookSource/main.cpp').read_text()
of=main[main.index('void __fastcall new_CVClient_FrameStageNotify_OpenFortress('):main.index('typedef void(__fastcall* CVClient_FrameStageNotify_CSS_t)')]
assert 'RecordingResolution::Frame();' in of
assert 'MirvFix_OfInterpolationProbeFrame();' in of
assert 'if (g_OfNativePovReady) MirvPovSpectate_FrameBegin();' in of
assert 'if (g_OfNativePovReady) MirvPovSpectate_FrameEnd();' in of
assert 'MirvFix_PlayerRepairFrameEnd();' in of
assert 'MirvFix_RecorderSoundUpdate();' in of
# Exercise the actual startup assignment block and Scene Cache defaults, not a duplicate policy.
overlay=(root/'AfxHookSource/CameraOverlay.cpp').read_text()
policy=main[main.index('const bool ofNative ='):main.index('#endif',main.index('const bool ofNative ='))]
scene=overlay[overlay.index('static bool g_SceneOfExtended'):overlay.index('// Counters only;')]
fixture2=r'''#include <cassert>
#include <atomic>
#include <cwchar>
bool g_OpenFortressLegacyMode=false,g_OfNativeResolution=false,g_OfNativeCollision=false,g_OfNativePasses=false,g_OfNativePov=false,g_OfNativeCommands=false;
struct Cmd {bool safe=false; int FindParam(const wchar_t* arg){assert(!wcscmp(arg,L"-afxOfSafeMode"));return safe?1:0;}} cmd,*g_CommandLine=&cmd;
namespace AfxHookSource { namespace CameraOverlay {
''' + scene + r'''
} }
void Setup(){
''' + policy + r'''
}
void check(bool expected){
 using namespace AfxHookSource::CameraOverlay;
 assert(g_OfNativeResolution==expected && g_OfNativeCollision==expected && g_OfNativePasses==expected && g_OfNativePov==expected && g_OfNativeCommands==expected);
 assert(g_SceneOfExtended==expected && g_SceneOfPoses==expected && g_SceneOfReplay==expected);
 assert(!g_SceneOfSkeleton && g_SceneOfColor080.load()==expected);
}
int main(){Setup();check(false);g_OpenFortressLegacyMode=true;Setup();check(true);cmd.safe=true;Setup();check(false);}
'''
with tempfile.TemporaryDirectory() as folder:
    source=Path(folder)/'policy.cpp'; exe=Path(folder)/'policy'; source.write_text(fixture2)
    subprocess.run(['g++','-std=c++17',str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
assert 'OfNativeInterfaces::Slots(iface, "engine.dll", {3})' in controller
assert 'OfNativeInterfaces::KnownClient()' in main
for opt in ('Resolution','Collision','Passes','Pov','Commands'):
    assert '-afxOfNative'+opt not in main and '-afxOfNative'+opt not in overlay
print('OF integrated defaults, safe-mode fallback, fingerprinted resolution gate and frame-stage separation passed')
