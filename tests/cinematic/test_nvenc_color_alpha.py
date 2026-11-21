"""Check production color/mask routing and graph; substitute CPU encoding without a GPU."""
import argparse
import json
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--build-dir', type=Path, default=Path(tempfile.gettempdir()) / 'hlae-color-alpha')
a = p.parse_args()
a.build_dir.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parents[2]
creator = (root / 'shared/OutVideoStreamCreators.h').read_text()
creator = creator[creator.index('class CFfmpegRecordingSettingsCreator'):creator.index('class CSamplingRecordingSettingsCreator')]
fixture = r'''
#include <string>
#include <cassert>
struct CImageFormat { enum Pixel { BGR, BGRA }; Pixel Format; };
namespace ImageFormat { constexpr auto BGRA=CImageFormat::BGRA; }
template<bool> struct TIOutVideoStream { virtual ~TIOutVideoStream()=default; void AddRef(){} };
struct COutVideoStreamCreator { virtual TIOutVideoStream<true>* CreateOutVideoStream(const CImageFormat&)=0; };
template<bool B> struct COutFFMPEGVideoStream:TIOutVideoStream<B> {
 std::wstring Options;
 COutFFMPEGVideoStream(const CImageFormat&,const std::wstring&,const std::wstring&o,float):Options(o){}
};
''' + creator + r'''
int main(){
 CFfmpegRecordingSettingsCreator c(L"path",L"split",60,L"color-only"),legacy(L"path",L"unchanged",60);
 auto test=[](CFfmpegRecordingSettingsCreator&c,CImageFormat::Pixel f,const wchar_t*expected){
  auto p=static_cast<COutFFMPEGVideoStream<true>*>(c.CreateOutVideoStream({f}));assert(p->Options==expected);delete p;
 };
 test(c,CImageFormat::BGRA,L"split");test(c,CImageFormat::BGR,L"color-only");
 test(legacy,CImageFormat::BGR,L"unchanged");test(legacy,CImageFormat::BGRA,L"unchanged");
}
'''
cpp = a.build_dir / 'routing.cpp'
cpp.write_text(fixture)
binary = a.build_dir / 'routing'
subprocess.run(['g++', '-std=c++17', str(cpp), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
settings = (root / 'shared/RecordingSettings.cpp').read_text()
block = settings[settings.index('new CFfmpegRecordingSettings("afxFfmpegNvencColorAlpha"'):]
block = block[:block.index(');')]
options = re.findall(r'"(?:[^"\\]|\\.)*"|,', block)
groups = [[]]
for token in options:
    if token == ',':
        groups.append([])
    else:
        groups[-1].append(json.loads(token))
split, rgb = (''.join(group) for group in groups[-2:])
# All five passes plus beauty: three alpha isolates + World/Depth/screen = six,
# instead of nine NVENC sessions. The mask must remain explicitly lossless.
assert split.count('-c:v h264_nvenc') == 1 and rgb.count('-c:v h264_nvenc') == 1
assert '-c:v libx264 -preset ultrafast -qp 0 -threads 1' in split
assert '-filter_complex_threads 1' in split
assert 3 * split.count('-c:v h264_nvenc') + 3 * rgb.count('-c:v h264_nvenc') == 6
def cpu_options(options, folder):
    options = options.replace('{QUOTE}', '"').replace('{AFX_STREAM_PATH}\\', str(folder) + '/')
    # Encoding is the only substitution: filters, pixel formats and maps are production values.
    return shlex.split(options.replace('h264_nvenc -preset p4 -tune lossless -profile:v high444p', 'libx264 -preset ultrafast -qp 0 -threads 1 -profile:v high444'))
frames, width, height = 6, 32, 16
data = bytes(channel for t in range(frames) for y in range(height) for x in range(width)
             for channel in (30 + t, 90, 180, (0, 128, 255)[x % 3]))
for name, opts, fmt, pixels in [('split', split, 'bgra', data), ('rgb', rgb, 'bgr24', bytes(v for i,v in enumerate(data) if i % 4 != 3))]:
    folder = a.build_dir / name
    folder.mkdir(exist_ok=True)
    for f in folder.glob('*.mp4'):
        f.unlink()
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-threads', '1',
                    '-f', 'rawvideo', '-pixel_format', fmt, '-video_size', f'{width}x{height}',
                    '-framerate', '60', '-i', 'pipe:0', *cpu_options(opts, folder)], input=pixels, check=True)
    for f in folder.glob('*.mp4'):
        probe = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'stream=nb_frames,r_frame_rate', '-of', 'json', str(f)]))['streams'][0]
        assert int(probe['nb_frames']) == frames and probe['r_frame_rate'] == '60/1', probe
    assert (folder / 'color.mp4').exists()
    assert (folder / 'alpha.mp4').exists() == (name == 'split')
alpha = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(a.build_dir / 'split/alpha.mp4'), '-f', 'rawvideo', '-pix_fmt', 'gray', 'pipe:1'])
expected = data[3::4]
assert len(alpha) == len(expected) and max(abs(x-y) for x,y in zip(alpha, expected)) <= 1
print('Production RGB/BGRA routing, split graph, alpha levels and equal 60-FPS frame counts passed (CPU encoder substitute).')
