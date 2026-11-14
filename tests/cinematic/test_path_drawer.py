"""Stress the real path polyline writer at the dynamic vertex-buffer boundary."""
from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2];out=Path(tempfile.mkdtemp(prefix='hlae-path-drawer-'))
s=(root/'AfxHookSource/CampathDrawer.cpp').read_text()
def function(name):
 a=s.index('void CCampathDrawer::'+name+'(');b=s.index('{',a);level=1;i=b+1
 while level:
  level+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
code=r'''
#include "shared/AfxMath.h"
#include <cassert>
#include <array>
#include <cstdint>
#include <iostream>
using Afx::Math::Vector3;using DWORD=uint32_t;using UINT=unsigned;
constexpr unsigned c_VertexBufferVertexCount=2048;constexpr int D3DPT_TRIANGLESTRIP=0;
struct Vertex{float x,y,z;DWORD diffuse;float t0u,t0v,t0w,t1u,t1v,t1w,t2u,t2v,t2w;};
struct Device{void SetStreamSource(int,void*,int,int){}void DrawPrimitive(int,unsigned,unsigned count){assert(count<=c_VertexBufferVertexCount-2);}}device;
struct CCampathDrawer{
 std::array<Vertex,c_VertexBufferVertexCount+4> Buffer{};Vertex*m_LockedVertexBuffer=nullptr;unsigned m_VertexBufferVertexCount=0;void*m_VertexBuffer=nullptr;Device*m_Device=&device;bool m_PolyLineStarted=true;DWORD m_OldCurrentColor=0;Vector3 m_OldPreviousPolyLinePoint;
 bool LockVertexBuffer(){m_LockedVertexBuffer=Buffer.data();return true;}void UnlockVertexBuffer(){m_LockedVertexBuffer=nullptr;}
 void BuildPolyLinePoint(Vector3,Vector3,DWORD,Vector3,Vertex*);void AutoPolyLinePoint(Vector3,Vector3,DWORD,Vector3);void AutoPolyLineFlush();
};
'''+function('BuildPolyLinePoint')+function('AutoPolyLinePoint')+function('AutoPolyLineFlush')+r'''
int main(){CCampathDrawer d;for(size_t i=c_VertexBufferVertexCount;i<d.Buffer.size();++i)d.Buffer[i].t1u=12345;
for(int i=0;i<100000;++i){d.AutoPolyLinePoint(Vector3(i,0,0),Vector3(i+1,1,0),0,Vector3(i+2,0,0));for(size_t j=c_VertexBufferVertexCount;j<d.Buffer.size();++j)assert(d.Buffer[j].t1u==12345);assert(d.m_VertexBufferVertexCount<=c_VertexBufferVertexCount);}
d.AutoPolyLineFlush();std::cout<<"Dense path vertex-buffer checks passed\n";}
'''
p=out/'draw.cpp';p.write_text(code);subprocess.run(['g++','-std=c++17','-Dabstract=','-O1','-fsanitize=address,undefined','-I'+str(root),'-I'+str(root/'AfxHookSource'),'-include',str(root/'tests/cinematic/msvc_crt_compat.h'),str(p),str(root/'shared/AfxMath.cpp'),'-o',str(out/'test')],check=True);subprocess.run([str(out/'test')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
