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
    assert((Fit({3840,1634},{1920,1040}) == Size{1920,817}));
    assert((Fit({4700,2000},{1904,1000}) == Size{1904,810}));
    assert((Fit({7680,4320},{1920,1040}) == Size{1848,1040}));
    assert((Fit({2160,3840},{1920,1080}) == Size{607,1080}));
    assert((Fit({1080,1080},{1920,1040}) == Size{1040,1040}));
    assert((Fit({1280,720},{1920,1080}) == Size{1280,720}));
    assert((Fit({3840,2160},{0,1080}) == Size{}));
    assert(!Valid({639,480}) && !Valid({640,479}) && !Valid({16385,2160}));
    std::vector<Mode> modes{{800,600,32,60},{1920,1080,32,60},{2560,1440,32,60}};
    const Size original{1920,1080};
    assert(BorrowIndex(modes,{3840,2160},original,original)==2);
    assert(BorrowIndex(modes,original,original,original)==-1);
    assert(BorrowIndex(modes,{3840,2160},{2560,1440},original)==-2);
    assert(modes.size()==3 && modes[2].Width==2560);
    std::vector<Mode> unsorted{{800,600,32,60},{2560,1440,32,60},{1920,1080,32,60}};
    assert(BorrowIndex(unsorted,{3840,2160},original,original)==1);
    assert(BorrowIndex(unsorted,{3840,2160},{2560,1440},original)==-2);
    std::vector<Mode> bad{{1920,1080,0,60}};
    assert(BorrowIndex(bad,{3840,2160},original,original)==-2);
    bad.assign(512,{1920,1080,32,60});
    assert(BorrowIndex(bad,{3840,2160},original,original)==-2);
    assert(BorrowIndex(bad,original,original,original)==-1);
    assert(BorrowIndex(modes,{3840,2160},original,{1280,720})==-2);
    assert(SameMode(modes[0],{800,600,32,60}));
    assert(!SameMode(modes[0],{800,600,32,75}));
    const Size high{3840,2160}, low{1920,1080};
    assert(NativeClient(low,high)==high);
    assert((NativeClient({1904,810},{4700,2000})==Size{4700,2000}));
    assert(NativeClient(low,{})==low);
    assert(NativeClient({},high)==Size{});
    assert(NativeClient(high,high)==high);
    assert((NativeClient({4000,1080},high)==Size{4000,1080}));
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
