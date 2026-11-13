"""Exercise actual Source input hooks, Win32 ownership and final overlay presentation."""
from pathlib import Path
import argparse,subprocess,os,tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-overlay-input');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
root=Path(__file__).resolve().parents[2];out=args.build_dir;out.mkdir(parents=True,exist_ok=True)
gui=(root/'AfxHookSource/Gui.cpp').read_text()
def function(signature):
 a=gui.index(signature);b=gui.index('{',a);end=b+1;depth=1
 while depth:
  depth+=(gui[end]=='{')-(gui[end]=='}');end+=1
 return gui[a:end]
a=gui.index('// Source polls its own input queue');b=gui.index('bool g_OverlayWindowFocused',a);bridge=gui[a:b];bridge=bridge[:bridge.rfind('namespace')]
a=gui.index('bool g_OverlayWindowFocused');owner=gui[a:gui.index('} // namespace',a)]
a=gui.index('bool OnSetCursorPos(');hooks=gui[a:gui.index('bool GetWorldGlyph',a)]
preamble=r'''
#include "shared/OverlayInput.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#define _WIN64 1
#define __thiscall
#define __fastcall
#define __in
#define __out
#define __in_opt
using UINT=unsigned;using DWORD=unsigned;using WPARAM=std::uintptr_t;using LPARAM=std::intptr_t;using BOOL=bool;using HWND=void*;using HCURSOR=void*;
constexpr bool TRUE=true,FALSE=false;
constexpr UINT WM_KEYFIRST=0x100,WM_KEYLAST=0x109,WM_KEYDOWN=0x100,WM_KEYUP=0x101,WM_CHAR=0x102,WM_SYSKEYDOWN=0x104,WM_SYSKEYUP=0x105,WM_SYSCHAR=0x106,WM_INPUT=0xff,WM_SETFOCUS=7,WM_KILLFOCUS=8,WM_ACTIVATE=6,WM_SETCURSOR=0x20,HTCLIENT=1,WA_INACTIVE=0;
constexpr UINT WM_MOUSEMOVE=0x200,WM_LBUTTONDOWN=0x201,WM_LBUTTONUP=0x202,WM_LBUTTONDBLCLK=0x203,WM_RBUTTONDOWN=0x204,WM_RBUTTONUP=0x205,WM_RBUTTONDBLCLK=0x206,WM_MBUTTONDOWN=0x207,WM_MBUTTONUP=0x208,WM_MBUTTONDBLCLK=0x209,WM_MOUSEWHEEL=0x20a,WM_XBUTTONDOWN=0x20b,WM_XBUTTONUP=0x20c,WM_XBUTTONDBLCLK=0x20d,WM_MOUSEHWHEEL=0x20e;
constexpr UINT VK_F8=0x77,VK_SHIFT=0x10,VK_LSHIFT=0xa0,VK_RSHIFT=0xa1,VK_F2=0x71,VK_OEM_3=0xc0,VK_MENU=0x12,VK_TAB=9,VK_F4=0x73,VK_LBUTTON=1,VK_RBUTTON=2,VK_MBUTTON=4,VK_XBUTTON1=5,VK_XBUTTON2=6,XBUTTON1=1;
unsigned LOWORD(std::intptr_t v){return unsigned(v)&65535;}unsigned HIWORD(std::intptr_t v){return (unsigned(v)>>16)&65535;}
struct POINT{int x=0,y=0;};using LPPOINT=POINT*;struct RECT{int left=0,top=0,right=1600,bottom=900;};
struct ImVec2{float x,y;ImVec2(float a=0,float b=0):x(a),y(b){}};
namespace ImGui{struct IO{bool MouseDrawCursor=false,WantCaptureMouse=false;bool MouseDown[5]{},KeysDown[512]{};ImVec2 MousePos;void AddFocusEvent(bool){}}io;IO&GetIO(){return io;}}
bool Supported=true,AltHeld=false;bool IsSupported(){return Supported;}short GetKeyState(unsigned k){return k==VK_MENU&&AltHeld?short(0x8000):0;}
namespace CameraOverlay{bool Open=false,Look=false,Hud=false,DeviceReady=true;int Frames=0,Finishes=0;bool IsOpen(){return Open;}void SetOpen(bool b){Open=b;}bool IsMouseLook(){return Look;}void SetMouseLook(bool b){Look=b;}void FinishEdit(){++Finishes;}bool PreviewHudVisible(){return Hud;}void ResetPreviewHudFrame(){}void Draw(){++Frames;}void SceneCacheDevicePresent(bool b){DeviceReady=b;}}
struct Input{bool Enabled=true;int Clears=0;void ClearMovementInput(){++Clears;}bool GetCameraControlMode(){return Enabled;}}input;
struct View{Input*m_MirvInput=&input;bool Menu=true,Visible=true;int Queries=0;bool IsSourceUiActive(){++Queries;return Menu;}void UiCursorStatus(bool&a,bool&v){++Queries;a=true;v=Visible;}}g_Hook_VClient_RenderView;
void MirvCampathPreview_ReleaseKeys(){}bool MirvCampathPreview_SetActive(bool){return true;}
HWND g_hWnd=(void*)1,Capture=g_hWnd;void*g_FontTexture=(void*)1;bool m_Active=true,m_KeyDownEaten[512]{};
POINT Physical{200,250},m_GameCursorPos;HCURSOR Hardware=(void*)2,m_GameCursor=nullptr;RECT Clip;int Display=-1;
bool GetCursorPos(POINT*p){*p=Physical;return true;}HCURSOR GetCursor(){return Hardware;}HCURSOR SetCursor(HCURSOR c){auto o=Hardware;Hardware=c;return o;}
HWND GetCapture(){return Capture;}HWND SetCapture(HWND c){auto o=Capture;Capture=c;return o;}bool ReleaseCapture(){Capture=nullptr;return true;}
bool GetClipCursor(RECT*r){*r=Clip;return true;}bool ClipCursor(const RECT*r){Clip=r?*r:RECT{};return true;}int ShowCursor(bool b){return Display+=b?1:-1;}bool SetCursorPos(int x,int y){Physical={x,y};return true;}bool ScreenToClient(HWND,POINT*){return true;}
std::vector<UINT> backendKeys;int ImGui_ImplWin32_WndProcHandler(HWND,UINT msg,WPARAM key,LPARAM){if(msg==WM_KEYDOWN||msg==WM_KEYUP)backendKeys.push_back(unsigned(key));return 0;}
UINT g_OverlayFrameMessage=0xc123;std::atomic<bool>g_OverlayFramePending{false};std::atomic<DWORD>g_LastOverlayFrame{0};std::recursive_mutex g_GuiFrameMutex;
DWORD Clock=1000;DWORD GetTickCount(){return Clock;}std::vector<UINT>posted;bool PostMessageW(HWND,UINT m,WPARAM,LPARAM){posted.push_back(m);return true;}
bool SetOverlayOpen(bool);int fallbackFrames=0;bool OnGameFrameRenderEnd(){++fallbackFrames;g_LastOverlayFrame=Clock;return true;}
using Event=advancedfx::OverlayInputEvent;
struct NativeInput{void**Vtable;bool Enabled=true;int Poll=10,Resets=0;std::vector<Event>Events;std::vector<int>Down;}native;
void*table[36]{};
void OriginalEnable(void*self,bool b){static_cast<NativeInput*>(self)->Enabled=b;}
int OriginalCount(void*self){return int(static_cast<NativeInput*>(self)->Events.size());}
const Event*OriginalData(void*self){return static_cast<NativeInput*>(self)->Events.data();}
int OriginalPoll(void*self){return static_cast<NativeInput*>(self)->Poll;}
void OriginalReset(void*self){auto&s=*static_cast<NativeInput*>(self);++s.Resets;for(int b:s.Down)s.Events.push_back({1,s.Poll,b,0,0});s.Down.clear();}
int MapKey(void*,int v){return v;}int MapName(void*,const char*n){return n[5]-'0'+500;}
namespace SOURCESDK{using CreateInterfaceFn=void*(*)(const char*,int*);}
void*Factory(const char*n,int*){return !std::strcmp(n,"InputSystemVersion001")?&native:nullptr;}
void*GetModuleHandleW(const wchar_t*){return (void*)1;}void*GetProcAddress(void*,const char*){return reinterpret_cast<void*>(Factory);}
int HookCall=0,FailHook=-1;bool AfxDetourPtr(void**slot,void*hook,void**original){if(++HookCall==FailHook)return false;*original=*slot;*slot=hook;return true;}
struct D3DVIEWPORT9{unsigned X=3,Y=4,Width=320,Height=240;float MinZ=0,MaxZ=1;};
struct IDirect3DSurface9{int References=1,Writes=0;void Release(){--References;}};
struct Device{IDirect3DSurface9 Back,Texture,Depth;IDirect3DSurface9*Target=&Texture,*Stencil=&Depth;D3DVIEWPORT9 Viewport;bool FailBegin=false,FailDepth=false;int Begins=0,Ends=0;int GetRenderTarget(int,IDirect3DSurface9**p){*p=Target;++Target->References;return 0;}int GetBackBuffer(int,int,int,IDirect3DSurface9**p){*p=&Back;++Back.References;return 0;}int GetViewport(D3DVIEWPORT9*p){*p=Viewport;return 0;}int SetViewport(const D3DVIEWPORT9*p){Viewport=*p;return 0;}int GetDepthStencilSurface(IDirect3DSurface9**p){if(FailDepth)return -1;*p=Stencil;if(*p)++(*p)->References;return 0;}int SetDepthStencilSurface(IDirect3DSurface9*p){Stencil=p;return 0;}int SetRenderTarget(int,IDirect3DSurface9*p){Target=p;Viewport.Width=1600;return 0;}int BeginScene(){++Begins;return FailBegin?-1:0;}int EndScene(){++Ends;return 0;}}device;
Device*g_pd3dDevice=&device;
constexpr int D3DBACKBUFFER_TYPE_MONO=0,D3DERR_NOTFOUND=-2;
#define SUCCEEDED(x) ((x)>=0)
struct Cache{int Draws=0;bool Dx9_Render(){++Draws;assert(device.Target==&device.Back);++device.Target->Writes;return true;}}m_ImGuiRernderCacheManager;
'''
tests=r'''
int main(){
 native.Vtable=table;table[7]=(void*)OriginalEnable;table[16]=(void*)OriginalCount;table[17]=(void*)OriginalData;table[25]=(void*)OriginalReset;table[29]=(void*)MapName;table[32]=(void*)MapKey;table[35]=(void*)OriginalPoll;
 // Installation failure must restore all modified slots.
 FailHook=2;assert(!InstallSourceInput()&&table[7]==(void*)OriginalEnable&&table[16]==(void*)OriginalCount);FailHook=-1;assert(InstallSourceInput());
 native.Events={{0,10,87,0,0},{3,10,1,8,0},{1000,10,'x',0,0},{100,10,0,0,0}};native.Down={87};
 NativeInput_SetCapture(true);assert(!native.Enabled&&native.Resets==1);assert(SourceCountHook(&native)==2);const auto data=SourceDataHook(&native);assert(data[0].Type==100&&data[1].Type==1&&data[1].Data==87);
 // External disable requests survive ownership restoration.
 SourceEnableHook(&native,false);NativeInput_SetCapture(false);assert(!native.Enabled);SourceEnableHook(&native,true);assert(native.Enabled);
 NativeInput_SetCapture(true);NativeInput_EatKey(65);NativeInput_SetCapture(false);++native.Poll;native.Events={{0,11,65,0,0},{1,11,65,0,0}};assert(SourceCountHook(&native)==1&&SourceDataHook(&native)[0].Type==1);
 native.Events.push_back({0,11,66,0,0});assert(SourceCountHook(&native)==2);for(int i=0;i<2;++i)assert(SourceDataHook(&native)[i].Type!=0||SourceDataHook(&native)[i].Data!=65);
 ++native.Poll;native.Events={{0,12,65,0,0}};assert(SourceCountHook(&native)==1&&SourceDataHook(&native)[0].Data==65);
 auto send=[&](UINT msg,WPARAM key=0,LPARAM lp=0){return WndProcHandler(g_hWnd,msg,key,lp);};
 assert(!CameraOverlay::Open);assert(send(WM_KEYDOWN,VK_F8));assert(CameraOverlay::Open&&m_GraphInputOwned&&!native.Enabled);assert(send(WM_KEYUP,VK_F8));
 for(unsigned key:{unsigned('W'),VK_SHIFT,VK_F2,VK_OEM_3}){assert(send(WM_KEYDOWN,key));assert(send(WM_KEYUP,key));}
 assert(send(WM_CHAR,'x')&&send(WM_SYSCHAR,'x')&&send(WM_INPUT));
 assert(backendKeys.size()>=8);assert(send(WM_RBUTTONDOWN)&&!CameraOverlay::Look);assert(send(WM_RBUTTONUP));
 const int queries=g_Hook_VClient_RenderView.Queries;assert(send(WM_SETCURSOR,0,HTCLIENT)&&g_Hook_VClient_RenderView.Queries==queries);
 int frames=CameraOverlay::Frames;DoFrame();assert(CameraOverlay::Frames==frames+1&&m_GraphInputOwned); // Menus do not suppress drawing.
 assert(send(WM_KEYDOWN,'A'));assert(send(WM_KEYDOWN,VK_F8)&&!CameraOverlay::Open);assert(send(WM_KEYUP,VK_F8));assert(send(WM_KEYDOWN,'A',1LL<<30)&&send(WM_KEYUP,'A'));assert(!send(WM_KEYDOWN,'A'));
 assert(send(WM_KEYDOWN,VK_F8));send(WM_KEYUP,VK_F8);g_Hook_VClient_RenderView.Menu=false;g_Hook_VClient_RenderView.Visible=false;
 assert(!send(WM_RBUTTONDOWN)&&CameraOverlay::Look&&!m_GraphInputOwned&&!native.Enabled);assert(!send(WM_RBUTTONUP)&&!CameraOverlay::Look&&m_GraphInputOwned);
 AltHeld=true;assert(!send(WM_SYSKEYDOWN,VK_TAB));AltHeld=false;
 assert(!send(WM_KILLFOCUS)&&!m_GraphInputOwned);send(WM_SETFOCUS);assert(m_GraphInputOwned);
 // Present owns the final backbuffer pass and restores offscreen/depth/viewport state.
 auto oldViewport=device.Viewport;On_Direct3DDevice9_Present_Before();assert(device.Back.Writes==1&&device.Texture.Writes==0&&device.Target==&device.Texture&&device.Stencil==&device.Depth&&device.Viewport.Width==oldViewport.Width&&device.Begins==1&&device.Ends==1);assert(device.Back.References==1&&device.Texture.References==1&&device.Depth.References==1);
 device.FailBegin=true;On_Direct3DDevice9_Present_Before();assert(device.Back.Writes==1&&device.Ends==1&&device.Target==&device.Texture&&device.Stencil==&device.Depth);device.FailBegin=false;
 device.FailDepth=true;On_Direct3DDevice9_Present_Before();assert(device.Back.Writes==1&&device.Target==&device.Texture&&device.Stencil==&device.Depth);device.FailDepth=false;
 // A menu without client render callbacks still requests UI work on the window thread.
 g_LastOverlayFrame=0;On_Direct3DDevice9_Present(false);assert(posted.size()==1&&g_OverlayFramePending);On_Direct3DDevice9_Present(false);assert(posted.size()==1);assert(send(g_OverlayFrameMessage)&&fallbackFrames==1&&!g_OverlayFramePending);Clock+=10;On_Direct3DDevice9_Present(false);assert(posted.size()==1);Clock+=10;On_Direct3DDevice9_Present(false);assert(posted.size()==2);g_OverlayFramePending=false;On_Direct3DDevice9_Present(true);assert(!CameraOverlay::DeviceReady&&!g_OverlayFramePending);
 SetOverlayOpen(false);const int draws=m_ImGuiRernderCacheManager.Draws;On_Direct3DDevice9_Present_Before();assert(m_ImGuiRernderCacheManager.Draws==draws);
 NativeInput_Shutdown();assert(table[7]==(void*)OriginalEnable&&table[16]==(void*)OriginalCount&&table[17]==(void*)OriginalData);
 std::cout<<"Overlay native input isolation, menu ownership and final presentation checks passed\n";
}
'''
forward='bool WndProcHandler(HWND,UINT,WPARAM&,LPARAM&);\n'
code=preamble+bridge+owner+forward+function('bool SetOverlayOpen(bool value)\n{')+function('bool WndProcHandler(HWND hwnd, UINT msg, WPARAM & wParam, LPARAM & lParam)')+function('void DoFrame()\n{')+function('void On_Direct3DDevice9_Present_Before()')+function('void On_Direct3DDevice9_Present(bool deviceLost)')+tests
file=out/'overlay_input_native.cpp';file.write_text(code)
cmd=['g++','-std=c++17','-O1','-g','-I'+str(root),str(file),'-o',str(out/'overlay_input_native')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
r=subprocess.run(cmd,capture_output=True,text=True);print(r.stderr[-6500:] if r.returncode else '',end='');
if r.returncode:raise SystemExit(r.returncode)
r=subprocess.run([str(out/'overlay_input_native')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'));print(r.stdout+r.stderr)
raise SystemExit(r.returncode)
