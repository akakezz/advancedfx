from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'preview'
 subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-pthread','-I'+str(r),str(r/'tests/cinematic/test_recording_preview.cpp'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
s=(r/'AfxHookSource/d3d9Hooks.cpp').read_text()
for name in ['Present','PresentEx']:
 body=s[s.index('STDMETHOD('+name+')(THIS_ CONST RECT* pSourceRect'):]
 assert body.index('SkipRecordingPreview(')<body.index('StartPresent()')<body.index('TryPresentFittedPreview(')<body.index('Afx_Present_After(')<body.index('FinishPresent(')
assert 'if(g_bCaptureAfterSwapBuffers) reasons|=Preview::AfterSwap' in s
assert 'if(g_ReShadeAdvancedfx.IsConnected()) reasons|=Preview::ReShade' in s
assert 'm_RecordingPreviewWindowed=ok && pp.Windowed!=FALSE' in s
s=(r/'AfxHookSource/AfxStreams.cpp').read_text()
assert 'RecordingPreview().Begin(previewFps, previewEligible)' in s and 'RecordingPreview().End()' in s
assert 'ParseRecordingPreviewFps(args->ArgV(2),fps)' in s and 'SetRecordingPreviewFps(fps)' in s
assert 'Completed presents' in s and 'Bypass reasons' in s
ui=(r/'AfxHookSource/CameraOverlay.cpp').read_text()
assert 'ImGui::InputInt("Recording preview FPS"' in ui and 'Recording::SetPreviewFps(static_cast<unsigned>(previewFps))' in ui
assert 'fps!=15' not in (r/'AfxHookSource/AfxStreams.h').read_text()
print('Both presentation entry points, lifecycle, profiling and shared UI/console routing passed')
