"""Compile the existing matte transform and verify opt-in straight-alpha color."""
from pathlib import Path
import argparse, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-image-matte');a=p.parse_args()
r=Path(__file__).resolve().parents[2];a.build_dir.mkdir(parents=True,exist_ok=True)
s=(r/'shared/ImageTransformer.cpp').read_text();matte=s[s.index('class CTransformMatte'):s.index('class CTransformStripAlpha')]
base=s[s.index('    std::mutex g_TransformCompletionMutex'):s.index('class ITransform')]
transform=s[s.index('IImageBufferThreadSafe* Transform('):s.index('/*\nstatic IImageBufferThreadSafe* DummyBuffer')].replace('class CThreadPool *','advancedfx::CThreadPool *')
source=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <vector>
#include <iostream>
#include "shared/ThreadPool.h"
namespace advancedfx {
enum class ImageFormat{BGR,BGRA};
struct CImageFormat {
 ImageFormat Format;int Width,Height;size_t Pitch;int Origin=0;
 CImageFormat(ImageFormat f=ImageFormat::BGR,int w=1,int h=1):Format(f),Width(w),Height(h),Pitch(w*GetPixelStride()){}
 size_t GetPixelStride()const{return Format==ImageFormat::BGR?3:4;}void SetOrigin(int o){Origin=o;}
};
}
struct CGrowingBufferPoolThreadSafe{};
struct IImageBufferThreadSafe {
 advancedfx::CImageFormat Format;std::vector<unsigned char>Pixels;
 IImageBufferThreadSafe(advancedfx::CImageFormat f):Format(f),Pixels(f.Pitch*std::abs(f.Height)){}
 void Release(){delete this;}
 void*GetImageBufferData(){return Pixels.data();}const advancedfx::CImageFormat*GetImageBufferFormat(){return &Format;}
};
using CImageBufferThreadSafe=IImageBufferThreadSafe;
CImageBufferThreadSafe*AquireFormatedImageBuffer(CGrowingBufferPoolThreadSafe*,advancedfx::CImageFormat f){return new CImageBufferThreadSafe(f);}
'''+base+r'''

struct ITransform{virtual ~ITransform()=default;virtual IImageBufferThreadSafe*CreateOutput(CGrowingBufferPoolThreadSafe*)=0;virtual size_t GetTaskSize()=0;virtual CTranformTask*CreateTask(std::atomic_int&,int,int)=0;};
'''+matte+transform+r'''
int main(){
 using advancedfx::CImageFormat;using advancedfx::ImageFormat;
 for(auto bf:{ImageFormat::BGR,ImageFormat::BGRA})for(auto wf:{ImageFormat::BGR,ImageFormat::BGRA}){
  IImageBufferThreadSafe black(CImageFormat(bf,4,2)),white(CImageFormat(wf,4,2));
  const unsigned char b[4][3]={{0,0,0},{0,255,0},{0,0,128},{25,50,100}};
  const unsigned char w[4][3]={{255,255,255},{0,255,0},{127,127,255},{152,177,227}};
  for(int row=0;row<2;++row)for(int x=0;x<4;++x)for(int c=0;c<3;++c){black.Pixels[row*black.Format.Pitch+x*black.Format.GetPixelStride()+c]=b[x][c];white.Pixels[row*white.Format.Pitch+x*white.Format.GetPixelStride()+c]=w[x][c];}
  for(bool straight:{false,true}){
   CTransformMatte transform(&black,&white,straight);auto out=transform.CreateOutput(nullptr);assert(out&&out->Format.Format==ImageFormat::BGRA);
   std::atomic_int jobs{0};for(int row=0;row<2;++row){auto task=transform.CreateTask(jobs,row,1);task->Execute();delete task;}
   for(int row=0;row<2;++row){auto bytes=out->Pixels.data()+row*out->Format.Pitch;
    assert(bytes[3]==0&&bytes[7]==255&&bytes[11]==128&&bytes[15]==128);
    assert(bytes[4]==0&&bytes[5]==255&&bytes[6]==0); // Opaque green survives without keying.
    if(straight){assert(bytes[0]==0&&bytes[1]==0&&bytes[2]==0);assert(bytes[8]==0&&bytes[9]==0&&bytes[10]==255);assert(bytes[12]==50&&bytes[13]==100&&bytes[14]==199);}
    else {assert(bytes[0]==127&&bytes[8]==63&&bytes[10]==191);} // Existing matte clients retain legacy behavior.
   }
   delete out;
  }
 }
 IImageBufferThreadSafe black(advancedfx::CImageFormat(advancedfx::ImageFormat::BGR,2,1)),wrong(advancedfx::CImageFormat(advancedfx::ImageFormat::BGRA,3,1));
 CTransformMatte mismatch(&black,&wrong,true);assert(!mismatch.CreateOutput(nullptr));
 advancedfx::CThreadPool pool(4);std::vector<std::thread>workers;
 for(int worker=0;worker<4;++worker)workers.emplace_back([&]{
  IImageBufferThreadSafe b(CImageFormat(ImageFormat::BGR,8,8)),w(CImageFormat(ImageFormat::BGR,8,8));std::fill(w.Pixels.begin(),w.Pixels.end(),255);
  for(int frame=0;frame<200;++frame){CTransformMatte t(&b,&w,true);auto out=Transform(&pool,nullptr,&t);assert(out);for(auto x:out->Pixels)assert(x==0);delete out;}
 });
 for(auto&worker:workers)worker.join();
 IImageBufferThreadSafe empty(CImageFormat(ImageFormat::BGR,1,0));CTransformMatte zero(&empty,&empty,true);assert(!Transform(&pool,nullptr,&zero));
 std::cout<<"Shared matte transform preserves opaque colors and reconstructs straight-alpha edges; legacy output unchanged\n";
}
'''
f=a.build_dir/'matte.cpp';f.write_text(source);cmd=['g++','-std=c++17','-pthread','-I'+str(r),'-O1','-g',str(f),'-o',str(a.build_dir/'matte')]
if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);subprocess.run([str(a.build_dir/'matte')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
