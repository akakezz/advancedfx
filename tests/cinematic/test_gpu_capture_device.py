"""Exercise the actual GPU initialization device probe using fake COM objects."""
from pathlib import Path
import argparse, os, subprocess, tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-gpu-device');a=p.parse_args()
r=Path(__file__).resolve().parents[2];a.build_dir.mkdir(parents=True,exist_ok=True)
s=(r/'AfxHookSource/GpuScreenCapture.cpp').read_text()
probe=s[s.index('        auto src=AfxGetRenderTargetSurface();'):s.index('        D3DSURFACE_DESC d{};')]
reject=s[s.index('    bool RejectHr('):s.index('    void Fail(')]
assert 'AfxGetDirect3DDevice9Ex()' not in probe
src=r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
using HRESULT=long;
#define FAILED(hr) ((hr)<0)
constexpr size_t _TRUNCATE=static_cast<size_t>(-1);
int _snprintf_s(char* buf,size_t size,size_t, const char* fmt,...) {va_list args;va_start(args,fmt);int n=vsnprintf(buf,size,fmt,args);va_end(args);return n;}
struct IDirect3DDevice9Ex {int refs=1;};
struct IDirect3DDevice9 {int refs=1;HRESULT result=0;IDirect3DDevice9Ex* ex=nullptr;};
template<class T> struct ComPtr {
 T* p=nullptr;~ComPtr(){if(p)--p->refs;} T** GetAddressOf(){return &p;}
 explicit operator bool()const{return p!=nullptr;}
 HRESULT As(ComPtr<IDirect3DDevice9Ex>* out){if(p->result<0)return p->result;out->p=p->ex;if(out->p)++out->p->refs;return p->result;}
};
struct Surface {IDirect3DDevice9* device=nullptr;HRESULT result=0;HRESULT GetDevice(IDirect3DDevice9** out){if(result<0)return result;*out=device;if(device)++device->refs;return result;}};
Surface* active=nullptr;Surface* AfxGetRenderTargetSurface(){return active;}
std::string message;
struct Probe {
 bool Reject(const char* why){message=why;return false;}
'''+reject+r'''
 bool Init(){
'''+probe+r'''
 return true;}
};
int main(){Probe p;Surface surface;IDirect3DDevice9 dev;IDirect3DDevice9Ex ex;
 assert(!p.Init()&&message.find("No tracked render target")!=std::string::npos);
 active=&surface;surface.result=-1;assert(!p.Init()&&message.find("GetDevice failed")!=std::string::npos);
 surface.result=0;assert(!p.Init()&&message.find("GetDevice failed")!=std::string::npos);
 surface.device=&dev;dev.result=-1;assert(!p.Init()&&message.find("does not expose IDirect3DDevice9Ex")!=std::string::npos&&dev.refs==1);
 dev.result=0;assert(!p.Init()&&message.find("does not expose IDirect3DDevice9Ex")!=std::string::npos&&dev.refs==1);
 dev.ex=&ex;assert(p.Init()&&dev.refs==1&&ex.refs==1);
 // A replacement surface can belong to a different device; never use stale Ex state.
 IDirect3DDevice9 replacement;surface.device=&replacement;replacement.result=-1;assert(!p.Init()&&replacement.refs==1&&ex.refs==1);
}
'''
f=a.build_dir/'probe.cpp';f.write_text(src);exe=a.build_dir/'probe'
flags=['-std=c++17','-O1','-g']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
subprocess.run(['g++',*flags,str(f),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Native surface device detection, missing/legacy device fallback and COM ownership checks passed')
