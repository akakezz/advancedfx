"""Check the common Scene Cache menu and exercise actual OF fallback setting code.

This is a source-level menu check plus a compiled settings fixture, not a full
Dear ImGui or Windows runtime test.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'AfxHookSource/CameraOverlay.cpp').read_text()
menu = source[source.index('static void DrawSceneCacheMenu(int playhead)'):source.index('static int g_CinematicFileAction')]
assert [menu.index(f'CollapsingHeader("{section}"') for section in ('Capture', 'Preview', 'Memory', 'Diagnostics')] == sorted(menu.index(f'CollapsingHeader("{section}"') for section in ('Capture', 'Preview', 'Memory', 'Diagnostics'))
normal, advanced = menu.split('ImGui::CollapsingHeader("Diagnostics")', 1)
for label in ('Player models', 'Projectiles', 'Weapons', 'Cosmetics', 'Ragdolls', 'Gibs', 'Buildings', 'Props / objectives', 'Draw cached models', 'Draw cached bounds', 'Movement trails'):
    assert f'"{label}"' in normal, label
assert 'Compatibility and fallback' not in normal
assert 'ImGui::BeginDisabled(boundsFallback)' in normal
assert 'ImGui::EndDisabled();' in normal
for cmd in ('ofExtended', 'ofPoses', 'ofReplay', 'ofSkeleton', 'ofColor080'):
    assert f'"{cmd}"' in advanced and f'"{cmd}"' not in normal
assert 'SetOfSceneOption(command, value)' in advanced
console = source[source.index('void SceneCacheConsole('):source.index('static bool g_ShowCurveEditor')]
assert 'SetOfSceneOption(op, value != 0)' in console
assert 'HLAE Camera###Open Fortress Camera' in source
assert 'Experimental OF integration:' not in source
assert 'pose.ModelInfo->Name + " | body " + std::to_string(pose.Body)' in source
assert '[OF replay] preflight epoch=' not in source
assert '[OF replay] unreadable pose' in source

policy = source[source.index('static bool g_SceneOfExtended'):source.index('// Counters only;')]
setter = source[source.index('static const char *SetOfSceneOption('):source.index('void SceneCacheConsole(')]
fixture = r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <strings.h>
#define _stricmp strcasecmp
static std::atomic_ullong g_SceneDrawEpoch{0};
enum class MirvOfSceneStage { Entities, Poses, Replay };
static bool ready = false;
static MirvOfSceneStage lastStage;
static unsigned checks = 0, refreshes = 0;
static const char *Mirv_Of_SceneCompatibilityIssue(MirvOfSceneStage stage) {
    lastStage = stage; ++checks; return ready ? nullptr : "incompatible";
}
static void SceneCacheFrame(bool capture) { assert(!capture); ++refreshes; }
''' + policy + setter + r'''
int main() {
    SetOfSceneDefaults(true);
    assert(g_SceneOfColor080 && g_SceneOfExtended && g_SceneOfPoses && g_SceneOfReplay);
    assert(!g_SceneOfSkeleton);
    assert(!SetOfSceneOption("ofColor080", false));
    assert(!g_SceneOfColor080 && g_SceneDrawEpoch == 1 && !checks && !refreshes);
    assert(!SetOfSceneOption("ofColor080", true) && g_SceneOfColor080);
    SetOfSceneDefaults(false);
    assert(!g_SceneOfColor080 && !g_SceneOfExtended && !g_SceneOfPoses && !g_SceneOfReplay);
    assert(SetOfSceneOption("ofPoses", true) && lastStage == MirvOfSceneStage::Poses);
    assert(!g_SceneOfPoses && !g_SceneOfExtended && !refreshes);
    assert(SetOfSceneOption("ofReplay", true) && lastStage == MirvOfSceneStage::Replay);
    assert(!g_SceneOfReplay);
    assert(!SetOfSceneOption("ofSkeleton", true) && g_SceneOfSkeleton && checks == 2);
    ready = true;
    assert(!SetOfSceneOption("ofPoses", true) && g_SceneOfExtended && g_SceneOfPoses);
    assert(!SetOfSceneOption("ofReplay", true) && g_SceneOfReplay);
    ready = false;
    auto oldChecks = checks;
    assert(!SetOfSceneOption("ofReplay", false) && !g_SceneOfReplay && checks == oldChecks);
    assert(refreshes == 4);
}
'''
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / 'cleanup.cpp'
    exe = Path(folder) / 'cleanup'
    path.write_text(fixture)
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Shared Scene Cache layout, normal/safe color defaults, GUI/console fallback policy and diagnostic cleanup passed')
