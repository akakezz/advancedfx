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
