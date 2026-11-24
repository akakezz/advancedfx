"""Exercise the shared diagnostic reads with OF preflight and optionally check the actual PE32 client.
Does not execute game code or require the Source SDK/ImGui dependencies.
"""
from pathlib import Path
import argparse, struct, subprocess, tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--client',type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
if args.client:
    raw=args.client.read_bytes(); pe=struct.unpack_from('<I',raw,60)[0]
    assert raw[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',raw,pe+4)[0]==0x14c
    assert struct.unpack_from('<I',raw,pe+8)[0]==0x66b4fe85
    base=struct.unpack_from('<I',raw,pe+52)[0]
    assert struct.unpack_from('<I',raw,pe+80)[0]==0xa27000
    image=bytearray(0xa27000)
    for i in range(struct.unpack_from('<H',raw,pe+6)[0]):
        header=pe+24+struct.unpack_from('<H',raw,pe+20)[0]+40*i
        _,rva,size,offset=struct.unpack_from('<IIII',raw,header+8)
        image[rva:rva+size]=raw[offset:offset+size]
    def u(rva): return struct.unpack_from('<I',image,rva)[0]
    assert image[0xbff79:0xbff7e]==bytes.fromhex('e8 22 92 04 00')
    assert image[0xbd7a7:0xbd7ac]==bytes.fromhex('e8 f4 b9 04 00')
    for vtable,offset,slots in [(0x664408,0,{5:0x83090}),(0x6648f0,4,{1:0xd1a20,2:0x2ad950,9:0x83400,16:0xb2ad0}),(0x6649a8,8,{2:0x2ad1e0})]:
        locator=u(vtable-4)-base
        assert u(locator)==0 and u(locator+4)==offset
        descriptor=u(locator+12)-base
        assert image[descriptor+8:descriptor+8+len(b'.?AVC_TFPlayer@@\0')]==b'.?AVC_TFPlayer@@\0'
        for slot,rva in slots.items(): assert u(vtable+slot*4)==base+rva
    assert image[0x83090:0x83093]==bytes.fromhex('8d 41 04')
    assert image[0xb2ad0:0xb2ad5]==bytes.fromhex('55 8b ec 6a ff')
    assert image[0xb358d:0xb3590]==bytes.fromhex('c2 10 00')
    assert b'DT_TFPlayer\0' in image and b'm_flPoseParameter\0' in image
    print('OF client RTTI, player vtables, SetupBones entry/return, interpolation signatures and receive field names verified',flush=True)
s=(root/'AfxHookSource/MirvCalcs.cpp').read_text()
def fn(signature):
    start=s.index(signature); end=s.index('{',start)+1; depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
h=(root/'AfxHookSource/MirvCalcs.h').read_text()
fields=h[h.index('struct MirvTf2DiagnosticField'):h.index('void *Mirv_Tf2_DiagnosticEntity')]
bones=h[h.index('struct MirvTf2DiagnosticBones'):h.index('struct MirvTf2DiagnosticModel')]
observer=s[s.index('namespace\n{\nstruct DiagnosticBoneObserver'):s.index('static bool Mirv_IsUsableBound',s.index('struct DiagnosticBoneObserver'))]
pre=r'''
#include <algorithm>
#include <atomic>
#include <mutex>
#define __fastcall
#define MIRV_TF2_MAX_BONES 128
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <vector>
using SIZE_T=size_t;
bool g_OpenFortressLegacyMode=true,g_OfNativeCommands=true;
struct CMirvTf2ClientRenderable {void**Vtable=nullptr;float Origin[3]={1,2,3},Angles[3]={4,5,6};const void*Model=(void*)1;const float*GetRenderOrigin(){return Origin;}const float*GetRenderAngles(){return Angles;}const void*GetModel(){return Model;}} renderable;
namespace SOURCESDK{struct matrix3x4_t{float m_flMatVal[3][4];};}
struct MirvTf2BoneName_t{};
int Mirv_Tf2_GetBoneNames(int,MirvTf2BoneName_t*){return 2;}
void Mirv_Tf2_DiagnosticBoneStop();
using LONG=long;using PVOID=void*;constexpr LONG NO_ERROR=0;int installs=0;
LONG DetourTransactionBegin(){return 0;} LONG DetourUpdateThread(void*){return 0;} LONG DetourAttach(PVOID*,PVOID){++installs;return 0;} LONG DetourTransactionCommit(){return 0;}void DetourTransactionAbort(){}
void*GetCurrentThread(){return nullptr;} unsigned char clientImage[0xb3590]{}; void*GetModuleHandleA(const char*){return clientImage;}
struct Globals{int framecount_get(){return 12;}float curtime_get(){return .5f;}} globals;
struct View{Globals*GetGlobals(){return &globals;}} g_Hook_VClient_RenderView;
int starts=0;bool Original(void*,void*,SOURCESDK::matrix3x4_t*out,int maximum,int,float time){++starts;for(int i=0;i<maximum;++i){out[i]={};out[i].m_flMatVal[0][3]=time;}return true;}
bool codeMatches=true;
int renderCalls=0;
struct CMirvTf2ClientUnknown {CMirvTf2ClientRenderable*GetClientRenderable(){++renderCalls;return &renderable;}};
struct Entity:CMirvTf2ClientUnknown {float Pitch=42,Pose=.75f;int Sequence=3;} entity,stale;
struct Tools {void*FirstEntity(){return &entity;}void*NextEntity(void*){return nullptr;}int GetEntIndex(void*){return 1;}} tools;
struct CClientToolsTf2 {static CClientToolsTf2*Instance(){static CClientToolsTf2 t;return &t;}Tools*GetClientTools(){return &tools;}};
namespace OfNativeInterfaces {
struct EngineSlot{unsigned Index;uintptr_t Rva;};
bool player=true,render=true,readable=true;
bool ExactClientSlots(void*p,uintptr_t vt,std::initializer_list<EngineSlot> slots){return vt==0x664408 ? player&&p==&entity&&slots.size()==1 : render&&p==&renderable&&vt==0x6648f0&&slots.size()==4;}
bool Read(const void*p,void*out,size_t n){if(p==reinterpret_cast<void*>(Original)&&n==5){const unsigned char code[]={0x55,0x8b,0xec,0x6a,0xff};memcpy(out,code,n);return codeMatches;}if(p==clientImage+0xb358d&&n==3){const unsigned char code[]={0xc2,0x10,0};memcpy(out,code,n);return codeMatches;}auto a=reinterpret_cast<uintptr_t>(p);auto e=reinterpret_cast<uintptr_t>(&entity);auto r=reinterpret_cast<uintptr_t>(&renderable);if(!readable||!((a>=e&&a-e<=sizeof(entity)&&n<=sizeof(entity)-(a-e))||(a>=r&&a-r<=sizeof(renderable)&&n<=sizeof(renderable)-(a-r))))return false;memcpy(out,p,n);return true;}
}
void*GetCurrentProcess(){return nullptr;}
bool ReadProcessMemory(void*,const void*,void*,size_t,size_t*){assert(false);return false;} // OF never enters private vector path
'''
main=r'''
int main(){
 auto offset=[](const void*p){return int(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(&entity));};
 int pitch=offset(&entity.Pitch);float v=-1,a[3]={};
 g_OfNativeCommands=false;Mirv_Tf2_ReadDiagnosticFields(1,&entity,&pitch,&v,1,a);assert(v==-1&&renderCalls==0);
 g_OfNativeCommands=true;OfNativeInterfaces::player=false;Mirv_Tf2_ReadDiagnosticFields(1,&entity,&pitch,&v,1,a);assert(v==-1&&renderCalls==0);
 OfNativeInterfaces::player=true;OfNativeInterfaces::render=false;Mirv_Tf2_ReadDiagnosticFields(1,&entity,&pitch,&v,1,a);assert(v==-1&&renderCalls==1);
 OfNativeInterfaces::render=true;Mirv_Tf2_ReadDiagnosticFields(1,&entity,&pitch,&v,1,a);assert(v==42&&a[0]==4);
 v=-1;Mirv_Tf2_ReadDiagnosticFields(1,&stale,&pitch,&v,1,a);assert(v==-1);
 OfNativeInterfaces::readable=false;Mirv_Tf2_ReadDiagnosticFields(1,&entity,&pitch,&v,1,a);assert(v==-1);OfNativeInterfaces::readable=true;
 MirvTf2DiagnosticField f[3];f[0].Offset=offset(&entity.Pose);f[1].Offset=offset(&entity.Sequence);f[1].Kind=1;f[2].Offset=0;f[2].VectorOffset=1;
 double values[3]={};int status[3]={1,1,1};float origin[3]={};Mirv_Tf2_ReadAnimationDiagnostic(1,&entity,f,3,values,status,origin);
 assert(values[0]==.75&&values[1]==3&&status[0]==0&&status[1]==0&&status[2]==3&&origin[0]==1);
 OfNativeInterfaces::readable=false;Mirv_Tf2_ReadAnimationDiagnostic(1,&entity,f,3,values,status,origin);assert(status[0]==3&&status[1]==3);
 void*vt[17]{};vt[16]=reinterpret_cast<void*>(Original);renderable.Vtable=vt;
 int generation=0;OfNativeInterfaces::player=false;assert(!Mirv_Tf2_DiagnosticBoneConfigure(1,generation)&&installs==0);OfNativeInterfaces::player=true;
 codeMatches=false;assert(!Mirv_Tf2_DiagnosticBoneConfigure(1,generation)&&installs==0);codeMatches=true;
 assert(Mirv_Tf2_DiagnosticBoneConfigure(1,generation)==2&&installs==1);
 SOURCESDK::matrix3x4_t m[2];assert(DiagnosticObserveSetupBones(&renderable,nullptr,m,2,0x7ff00,.5f)&&starts==1);
 MirvTf2DiagnosticBones row;assert(Mirv_Tf2_DiagnosticBonePop(row)&&row.Count==2&&row.Matrix[0][3]==.5f);
 Mirv_Tf2_DiagnosticBoneStop();assert(DiagnosticObserveSetupBones(&renderable,nullptr,m,2,0x7ff00,.5f)&&starts==2&&!Mirv_Tf2_DiagnosticBonePop(row));
 std::cout<<"OF verified-renderable gates, stale identity, bounded reads, scalar animation fields, excluded vectors and natural bone observation passed\n";
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d); cpp=d/'diag.cpp'; exe=d/'diag'
    cpp.write_text(pre+fields+fn('static CMirvTf2ClientRenderable *DiagnosticRenderable(')+fn('void Mirv_Tf2_ReadDiagnosticFields(')+fn('void *Mirv_Tf2_DiagnosticEntity(')+fn('void Mirv_Tf2_ReadAnimationDiagnostic(')+bones+observer+main)
    subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
