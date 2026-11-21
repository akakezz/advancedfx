"""Exercise production stencil holdout policy and cleanup, including late occluders."""
from pathlib import Path
import argparse,os,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-player-holdout');a=p.parse_args();a.build_dir.mkdir(parents=True,exist_ok=True)
r=Path(__file__).resolve().parents[2];s=(r/'AfxHookSource/d3d9Hooks.cpp').read_text()
def function(signature):
 at=s.index(signature);begin=s.index('{',at);depth=1;end=begin+1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[at:end]
tag=function('void OverrideBegin_PlayerHoldout(bool selected)')
clear=function('bool AfxD3D9ClearPlayerHoldout(DWORD background)')
raster=function('template<typename Draw> HRESULT DrawWithPlayerHoldout(Draw draw)')
for name in ['DrawPrimitive','DrawIndexedPrimitive','DrawPrimitiveUP','DrawIndexedPrimitiveUP']:
 assert 'STDMETHOD('+name+')(THIS_' in s and 'g_OldDirect3DDevice9->'+name+'(' in s
def loop_after(marker):
 at=s.index('for(const auto &s:m_PlayerHoldout)',s.index(marker));return s[at:s.index(';',at)+1]
redo=loop_after('void Redo(void)');undo=loop_after('void Undo(void)')
at=s.index('auto &holdout=m_OverrideStack.top().m_PlayerHoldout;',s.index('STDMETHOD(SetRenderState)'));prefix=s[at:s.index('if(g_bD3D9DebugPrint)',at)]
source=r'''
#include <cassert>
#include <map>
#include <stack>
#include <vector>
#include <iostream>
using DWORD=unsigned;using D3DRENDERSTATETYPE=int;using HRESULT=int;
struct IDirect3DSurface9 {void Release(){}};
IDirect3DSurface9 mainColor,mainDepth,shadowColor;
IDirect3DSurface9*AfxGetRenderTargetSurface(){return &mainColor;}
IDirect3DSurface9*AfxGetDepthStencilSurface(){return &mainDepth;}
constexpr int TRUE=1,FALSE=0,D3D_OK=0,D3DRS_ZWRITEENABLE=14,D3DRS_STENCILENABLE=52,D3DRS_STENCILFAIL=53,D3DRS_STENCILZFAIL=54,D3DRS_STENCILPASS=55,D3DRS_STENCILFUNC=56,D3DRS_STENCILREF=57,D3DRS_STENCILMASK=58,D3DRS_STENCILWRITEMASK=59,D3DRS_TWOSIDEDSTENCILMODE=185;
constexpr int D3DSTENCILOP_KEEP=1,D3DSTENCILOP_REPLACE=3,D3DCMP_ALWAYS=8,D3DCMP_EQUAL=3,D3DFVF_XYZRHW=4,D3DFVF_DIFFUSE=64;
constexpr int D3DRS_LIGHTING=137,D3DRS_FOGENABLE=28,D3DRS_MULTISAMPLEANTIALIAS=161,D3DRS_ZENABLE=7,D3DRS_ALPHABLENDENABLE=27,D3DRS_ALPHATESTENABLE=15,D3DRS_CULLMODE=22,D3DRS_SCISSORTESTENABLE=174,D3DRS_COLORWRITEENABLE=168,D3DRS_SRGBWRITEENABLE=194,D3DCULL_NONE=1;
constexpr int D3DTSS_COLOROP=1,D3DTOP_SELECTARG1=2,D3DTSS_COLORARG1=2,D3DTA_DIFFUSE=0,D3DTSS_ALPHAOP=4,D3DTSS_ALPHAARG1=5,D3DTOP_DISABLE=1,D3DPT_TRIANGLESTRIP=5;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
struct D3DVIEWPORT9{unsigned X=0,Y=0,Width=4,Height=1;float MinZ=0,MaxZ=1;};
struct Device {
 IDirect3DSurface9*Color=&mainColor,*DepthSurface=&mainDepth;
 int GetRenderTarget(int,IDirect3DSurface9**out){*out=Color;return 0;}
 int GetDepthStencilSurface(IDirect3DSurface9**out){*out=DepthSurface;return 0;}
 std::map<int,DWORD>States;DWORD Pixels[4]={};float Depth[4]={100,100,100,100};DWORD Stencil[4]={5,5,5,5};D3DVIEWPORT9 View;
 int GetRenderState(int state,DWORD*out){*out=States[state];return 0;}int SetRenderState(int state,DWORD value){States[state]=value;return 0;}
 int GetViewport(D3DVIEWPORT9*out){*out=View;return 0;}int SetViewport(const D3DVIEWPORT9*v){View=*v;return 0;}
 void SetVertexShader(void*){}void SetPixelShader(void*){}void SetFVF(int){}void SetTexture(int,void*){}void SetTextureStageState(int,int,int){}
 void Fragment(unsigned x,float z,DWORD color,bool discard=false){
  if(discard || z>Depth[x])return;
  if(States[D3DRS_COLORWRITEENABLE])Pixels[x]=color;
  if(States[D3DRS_ZWRITEENABLE])Depth[x]=z;
  if(States[D3DRS_STENCILENABLE] && States[D3DRS_STENCILPASS]==D3DSTENCILOP_REPLACE){auto mask=States[D3DRS_STENCILWRITEMASK];Stencil[x]=(Stencil[x]&~mask)|(States[D3DRS_STENCILREF]&mask);}
 }
 int DrawPrimitiveUP(int,int,const void*vertices,unsigned){struct Vertex{float x,y,z,w;DWORD color;};const auto color=static_cast<const Vertex*>(vertices)[0].color;
  assert(States[D3DRS_STENCILMASK]==128&&States[D3DRS_STENCILWRITEMASK]==0&&States[D3DRS_STENCILFUNC]==D3DCMP_EQUAL);
  for(unsigned x=0;x<4;++x)if((Stencil[x]&States[D3DRS_STENCILMASK])==States[D3DRS_STENCILREF])Pixels[x]=color;return 0;
 }
}raw,*g_OldDirect3DDevice9=&raw;
Device*AfxGetDirect3DDevice9(){return &raw;}
std::stack<std::map<int,DWORD>>saved;
void AfxD3D9BeginCleanState(){saved.push(raw.States);raw.States.clear();}void AfxD3D9EndCleanState(){raw.States=saved.top();saved.pop();}
struct Adapter {
 bool m_PlayerHoldoutEnabled=false;DWORD m_PlayerHoldoutZEnable=1;
 DWORD m_D3DRS_ZWRITEENABLE=1;std::map<int,DWORD>m_NativeHoldoutStates;
 struct CAfxOverride{Adapter&m_Dev;std::map<int,DWORD>m_PlayerHoldout;CAfxOverride(Adapter&a):m_Dev(a){}
 void Redo(){'''+redo+r'''}void Undo(){'''+undo+r'''} };
 std::stack<CAfxOverride>m_OverrideStack;
 Adapter(){m_OverrideStack.emplace(*this);}
 void Push(){m_OverrideStack.push(m_OverrideStack.top());}
 void Pop(){m_OverrideStack.top().Undo();m_OverrideStack.pop();m_OverrideStack.top().Redo();}
 void AfxPushOverrideState(bool){Push();}void AfxPopOverrideState(){Pop();}
'''+raster+tag+r'''
 int SetRenderState(int State,DWORD Value){'''+prefix+r'''
  if(State==D3DRS_ZWRITEENABLE)m_D3DRS_ZWRITEENABLE=Value;
  return raw.SetRenderState(State,Value);
 }
};
'''+clear+r'''
int main(){
 Adapter a;raw.States[D3DRS_STENCILENABLE]=0;
 auto fragment=[&](bool player,int x,float z,DWORD color,bool zwrite,bool discard=false){
  a.Push();a.OverrideBegin_PlayerHoldout(player);a.SetRenderState(D3DRS_STENCILREF,7); // Native requests survive the override.
  a.SetRenderState(D3DRS_ZWRITEENABLE,zwrite);raw.SetRenderState(D3DRS_COLORWRITEENABLE,player?15:0);
  raw.Fragment(x,z,color,discard);a.Pop();assert(raw.States[D3DRS_STENCILREF]==7&&raw.States[D3DRS_STENCILENABLE]==0);
 };
 for(int x=0;x<4;++x)fragment(true,x,5,0x33ff55,true);
 fragment(false,0,2,0,false); // A transparent occluder must not erase a player.
 assert(raw.Stencil[0]&128);
 // Unhooked brush entity/static batch/custom mesh through the shared GPU draw path.
 a.m_PlayerHoldoutEnabled=true;
 raw.States[D3DRS_COLORWRITEENABLE]=0;a.SetRenderState(D3DRS_ZWRITEENABLE,true);
 a.DrawWithPlayerHoldout([&]{raw.Fragment(0,2,0);return 17;});
 assert(a.m_OverrideStack.size()==1 && raw.States[D3DRS_STENCILENABLE]==0);
 // Depth-disabled overlays are not opaque scene occluders, even with ZWRITE true.
 a.SetRenderState(D3DRS_ZENABLE,false);
 a.DrawWithPlayerHoldout([&]{assert(raw.States[D3DRS_STENCILWRITEMASK]==0);return 0;});
 a.SetRenderState(D3DRS_ZENABLE,true);
 // A shadow/reflection target cannot alter the main pass's holdout.
 raw.Color=&shadowColor;
 a.DrawWithPlayerHoldout([&]{assert(!raw.States[D3DRS_STENCILENABLE]);return 0;});raw.Color=&mainColor;
 // A selected model keeps its selected tag; automatic unselected tagging cannot override it.
 a.Push();a.OverrideBegin_PlayerHoldout(true);
 a.DrawWithPlayerHoldout([&]{assert(raw.States[D3DRS_STENCILREF]==128);return 0;});a.Pop();
 a.m_PlayerHoldoutEnabled=false;
 fragment(false,1,9,0,true); // Behind the player.
 fragment(false,2,2,0,true,true); // Alpha-test discard / gap in a fence.
 fragment(false,3,2,0,false); // Glass, with native depth writes disabled.
 assert(raw.Pixels[0]==0x33ff55 && !(raw.Stencil[0]&128));
 for(int x=1;x<4;++x)assert(raw.Stencil[x]&128);
 const auto states=raw.States;
 assert(AfxD3D9ClearPlayerHoldout(0xff000000));assert(raw.Pixels[0]==0xff000000&&raw.States==states);
 for(int x=1;x<4;++x)assert(raw.Pixels[x]==0x33ff55);
 assert(AfxD3D9ClearPlayerHoldout(0xffffffff));assert(raw.Pixels[0]==0xffffffff&&raw.States==states);
 for(int x=0;x<4;++x)assert((raw.Stencil[x]&127)==5);
 std::cout<<"Production raster holdout covers unhooked brush/entities/batches, removes late opaque occluders, preserves depth failures/cutout gaps/glass and restores state\n";
}
'''
f=a.build_dir/'holdout.cpp';f.write_text(source);cmd=['g++','-std=c++17','-O1','-g',str(f),'-o',str(a.build_dir/'holdout')]
if a.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);subprocess.run([str(a.build_dir/'holdout')],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
