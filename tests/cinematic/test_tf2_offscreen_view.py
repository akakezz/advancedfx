from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=r'''
#include "shared/Tf2OffscreenView.h"
#include <cassert>
#include <cstring>
using namespace advancedfx::Tf2Offscreen;
int main() {
 assert(ClampedViewport(0,0,1920,1080,1920,1080,3840,2160));
 assert(!ClampedViewport(0,0,3840,2160,1920,1080,3840,2160));
 assert(!ClampedViewport(10,0,1920,1080,1920,1080,3840,2160));
 assert(!ClampedViewport(0,0,960,540,1920,1080,3840,2160));
 assert(!ClampedViewport(0,0,1920,1080,1920,1080,1920,1080));
 View v{};
 v.Width=v.UnscaledWidth=1920;v.Height=v.UnscaledHeight=1080;
 v.Fov=90;v.FovViewmodel=54;v.Near=4;v.Far=32768;v.Aspect=1920.f/1080;
 v.Origin[0]=123;v.Angles[1]=45;v.Bloom=1;
 const auto original=v;
 assert(Prepare(&v,sizeof(v),3840,2160));
 assert(v.Width==3840 && v.UnscaledWidth==3840 && v.Height==2160 && v.UnscaledHeight==2160);
 assert(v.Fov==original.Fov && v.Near==original.Near && v.Far==original.Far);
 assert(v.Origin[0]==123 && v.Angles[1]==45 && !v.Bloom);
 assert(!CompatibleSize(1920,1080,3840,1600));
 assert(!CompatibleSize(1920,1080,1920,1080));
 assert(!CompatibleSize(1920,1080,9600,5400));
 assert(CompatibleSize(1920,1080,2560,1440));
 v=original;v.Stereo=1;auto saved=v;assert(!Prepare(&v,sizeof(v),3840,2160));assert(!std::memcmp(&v,&saved,sizeof(v)));
 v=original;v.X=10;assert(!Prepare(&v,sizeof(v),3840,2160));
 v=original;v.Subrect=1;assert(!Prepare(&v,sizeof(v),3840,2160));
 v=original;v.ProjectionOverride=1;assert(!Prepare(&v,sizeof(v),3840,2160));
 v=original;v.UnscaledWidth=960;assert(!Prepare(&v,sizeof(v),3840,2160));
 v=original;assert(!Prepare(&v,sizeof(v)-1,3840,2160));
}
'''
with tempfile.TemporaryDirectory(prefix='hlae-offscreen-') as directory:
 p=Path(directory)/'test.cpp';p.write_text(source)
 exe=Path(directory)/'test'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('Offscreen view layout, native projection preservation and bounds passed.')
