"""Exercise the production D3D readback pool's allocation/reuse COM ownership."""
from pathlib import Path
import argparse, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-capture-lifetime');a=p.parse_args()
r=Path(__file__).resolve().parents[2];a.build_dir.mkdir(parents=True,exist_ok=True)
s=(r/'AfxHookSource/d3d9Hooks.cpp').read_text();acquire=s[s.index('class CImageBufferWrapper * AquireOffscreen('):s.index('class CImageBufferWrapper\n\t: public CRefCountedThreadSafe')]
source=r'''
#include <cassert>
#include <atomic>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <iostream>
#define SUCCEEDED(x) ((x)>=0)
constexpr int D3DPOOL_SYSTEMMEM=0;int liveSurfaces=0;
struct IDirect3DSurface9{int Refs=1;IDirect3DSurface9(){++liveSurfaces;}~IDirect3DSurface9(){--liveSurfaces;}void AddRef(){++Refs;}void Release(){assert(Refs>0);if(!--Refs)delete this;}};
struct IDirect3DDevice9{bool Fail=false;int CreateOffscreenPlainSurface(int,int,int,int,IDirect3DSurface9**out,void*){if(Fail)return -1;*out=new IDirect3DSurface9;return 0;}};
struct CRefCountedThreadSafe{int Refs=0;void AddRef(){++Refs;}void Release(){if(!--Refs)delete this;}virtual ~CRefCountedThreadSafe()=default;};
struct IAfxD3D9CaptureBuffer{virtual void AddRef()=0;virtual void Release()=0;};
struct Pool {
 struct CImageBufferWrapper: CRefCountedThreadSafe,IAfxD3D9CaptureBuffer {
  Pool*Owner;IDirect3DSurface9*Surface;
  CImageBufferWrapper(Pool*p,IDirect3DSurface9*s):Owner(p),Surface(s){Surface->AddRef();}
  ~CImageBufferWrapper(){Surface->Release();}
  void AddRef()override{CRefCountedThreadSafe::AddRef();}
  void Release()override{assert(Refs==2);Owner->ReturnOffscreen(this);CRefCountedThreadSafe::Release();}
 };
 std::mutex m_OffscreenSurfacesMutex;std::condition_variable m_OffscreenSurfacesCv;
 size_t m_NumOffscreenSurfaces=0;std::deque<CImageBufferWrapper*>m_OffscreenSurfaces;
 struct Desc{int Width=1920,Height=1080,Format=21;}m_SurfaceDesc;
 int Refs=1;void AddRef(){++Refs;}void Release(){--Refs;}
'''+acquire+r'''
 ~Pool(){assert(Refs==1&&m_NumOffscreenSurfaces==m_OffscreenSurfaces.size());while(!m_OffscreenSurfaces.empty()){static_cast<CRefCountedThreadSafe*>(m_OffscreenSurfaces.front())->Release();m_OffscreenSurfaces.pop_front();}}
};
int main(){IDirect3DDevice9 device;
 for(int take=0;take<100;++take){
  {Pool pool;auto black=pool.AquireOffscreen(&device),white=pool.AquireOffscreen(&device);assert(liveSurfaces==2&&pool.Refs==3);black->Release();white->Release();
   for(int frame=0;frame<720;++frame){auto sample=pool.AquireOffscreen(&device);sample->Release();assert(liveSurfaces==2&&pool.Refs==1);}}
  assert(liveSurfaces==0); // Missing release of CreateOffscreenPlainSurface's reference leaks here.
 }
 device.Fail=true;{Pool pool;assert(!pool.AquireOffscreen(&device)&&pool.m_NumOffscreenSurfaces==0&&liveSurfaces==0);}
 std::cout<<"Production readback pool reuses two surfaces and releases all COM references across 100 takes\n";
}
'''
f=a.build_dir/'capture.cpp';f.write_text(source);cmd=['g++','-std=c++17','-pthread','-O1','-g',str(f),'-o',str(a.build_dir/'capture')]
if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);subprocess.run([str(a.build_dir/'capture')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
