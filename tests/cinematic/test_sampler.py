"""Compile the production sampler on Linux; compare scalar and SSE2 output under sanitizers."""
from pathlib import Path
import argparse, os, shutil, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-sampler');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
r=Path(__file__).resolve().parents[2];out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True);shared=out/'shared';shared.mkdir(exist_ok=True)
for name in ['EasySampler.cpp','EasySampler.h','SamplerKernels.h','ImageFormat.h','TImageBuffer.h','TGrowingBufferPool.h','TRefCounted.h','GrowingBuffer.h','RecordingTiming.h','RecordingLog.h']:
 shutil.copyfile(r/'shared'/name,shared/name)
# Only adapt MSVC syntax and precompiled header; no changes to the sampler logic.
h=shared/'EasySampler.h';h.write_text(h.read_text().replace('virtual void EasySamplerBase::','virtual void '));(shared/'stdafx.h').write_text('#include <cstddef>\n')
flags=['-std=c++17','-O2','-ffp-contract=off','-Dabstract=','-D__declspec(x)=','-I'+str(out),'-I'+str(r)]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-g']
results=[]
for name,extra in [('scalar',['-DAFX_SAMPLER_FORCE_SCALAR']),('simd',[])]:
 exe=out/name
 subprocess.run(['g++',*flags,*extra,str(r/'tests/cinematic/test_sampler.cpp'),str(shared/'EasySampler.cpp'),'-o',str(exe)],check=True)
 results.append(subprocess.check_output([str(exe)],text=True).strip())
assert results[0]==results[1],results
print('Sampler row and timeline equivalence passed; output hash',results[0])

# MirvPgl includes SDK headers that may macro-map snprintf to _snprintf_s.
# Compile the real report block with that macro and a stand-in for the Windows CRT.
header=(r/'shared/AfxOutStreams.h').read_text()
start=header.index('        if (m_SampleTime.Calls) {')
end=header.index('\n        }',start)+len('\n        }')
block=header[start:end]
fixture=out/'sampler_report_macro.cpp'
fixture.write_text(r'''
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstddef>
constexpr size_t _TRUNCATE = static_cast<size_t>(-1);
int _snprintf_s(char* buf, size_t size, size_t count, const char* fmt, ...) {
    assert(count==_TRUNCATE);
    va_list args; va_start(args,fmt); int n=std::vsnprintf(buf,size,fmt,args); va_end(args); return n;
}
#define snprintf _snprintf_s
namespace RecordingTiming {
void Report(const char* source,const char* report) {
    assert(std::strcmp(source,"Sampler timing")==0);
    assert(std::strstr(report,"1920x1080 format 1 | SSE2 CPU sampler | 20 inputs, 1 outputs (0 failed)"));
    assert(std::strstr(report,"Sampler work 30.00 ms (1.500 ms/input)"));
}
}
namespace SamplerKernels { constexpr bool HasSimd=true; }
int main() {
    struct Counter { unsigned long long Calls; double ms; double Milliseconds() const { return ms; } };
    Counter m_SampleTime{20,35}, m_OutputTime{1,5};
    struct { int Width=1920,Height=1080,Format=1; } m_ImageFormat;
    unsigned long long m_OutputFailures=0;
''' + block + '\n}\n')
exe=out/'sampler_report_macro'
subprocess.run(['g++',*flags,str(fixture),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print('Sampler timing report compiles and formats correctly with the SDK snprintf macro')
