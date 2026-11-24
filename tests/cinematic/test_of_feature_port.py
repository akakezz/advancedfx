"""Compile the real recording support gate; guard OF opt-in and native hook separation."""
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
''' + controller[start:end] + '''
int main(){
 assert(!Supported());g_OfNativeResolution=true;assert(!Supported());
 g_OpenFortressLegacyMode=true;assert(Supported());g_OfNativeResolution=false;assert(!Supported());
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
assert 'MirvPovSpectate_FrameBegin();' not in of and 'MirvFix_PlayerRepairFrameEnd();' not in of
assert 'MirvFix_RecorderSoundUpdate();' not in of
print('OF resolution opt-in and frame-stage engine-hook separation passed')
