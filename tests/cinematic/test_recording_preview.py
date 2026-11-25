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
 assert body.index('SkipRecordingPreview(')<body.index('TryPresentFittedPreview(')<body.index('Afx_Present_After(')
assert '!g_bCaptureAfterSwapBuffers' in s and '!g_ReShadeAdvancedfx.IsConnected()' in s
assert 'm_RecordingPreviewWindowed=ok && pp.Windowed!=FALSE' in s
s=(r/'AfxHookSource/AfxStreams.cpp').read_text()
assert 'RecordingPreview().Begin(previewEligible ? previewFps : 0)' in s and 'RecordingPreview().End()' in s
assert 'Recording::SetPreviewFps' in (r/'AfxHookSource/CameraOverlay.cpp').read_text()
assert 'SetRecordingPreviewFps(static_cast<unsigned>(atoi(value)))' in s
print('Both presentation entry points, lifecycle and shared UI/console routing passed')
