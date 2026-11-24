from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = r'''
#include "shared/RenderResolution.h"
#include <cassert>
using namespace advancedfx::RenderResolution;
int main()
{
    assert((Fit({3840,2160},{1920,1080}) == Size{1920,1080}));
    assert((Fit({7680,4320},{1920,1040}) == Size{1848,1040}));
    assert((Fit({2160,3840},{1920,1080}) == Size{607,1080}));
    assert((Fit({1080,1080},{1920,1040}) == Size{1040,1040}));
    assert((Fit({1280,720},{1920,1080}) == Size{1280,720}));
    assert((Fit({3840,2160},{0,1080}) == Size{}));
    assert(!Valid({639,480}) && !Valid({640,479}) && !Valid({16385,2160}));
    std::vector<Mode> modes{{800,600,32,60},{1920,1080,32,60},{2560,1440,32,60}};
    bool inserted = false;
    assert(Plan(modes,{2000,1000},inserted) && inserted && modes.size()==4);
    assert(modes[2].Width==2000 && modes[2].Height==1000 && modes[2].Bits==32);
    assert(modes[3].Width==2560 && modes[1].Width==1920);
    assert(Plan(modes,{1920,1080},inserted) && !inserted && modes.size()==4);
    assert(Plan(modes,{3840,2160},inserted) && inserted && modes.back().Width==3840);
    std::vector<Mode> bad{{1920,1080,32,60},{800,600,32,60}};
    assert(!Plan(bad,{3840,2160},inserted) && bad.size()==2);
    bad={{1920,1080,0,60}};
    assert(!Plan(bad,{3840,2160},inserted));
    bad.assign(512,{1920,1080,32,60});
    assert(!Plan(bad,{3840,2160},inserted) && bad.size()==512);
    assert(Plan(bad,{1920,1080},inserted) && !inserted);
    const Size high{3840,2160}, low{1920,1080};
    assert(Ready(high,high,high,high));
    assert(!Ready(high,low,high,high));
    assert(!Ready(high,high,low,high));
    assert(!Ready(high,high,high,low));
}
'''
with tempfile.TemporaryDirectory(prefix="hlae-record-resolution-") as d:
    cpp=Path(d)/"test.cpp"
    binary=Path(d)/"test"
    cpp.write_text(source,encoding="utf8")
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(root),str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Custom mode planner, aspect-preserving preview fit, capture size verification passed.")
