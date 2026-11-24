"""Exercise the production live attachment sampler with distinct render/absolute poses."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--build-dir', type=Path, default=Path(tempfile.gettempdir()) / 'hlae-attachment-sampling')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
args.build_dir.mkdir(parents=True, exist_ok=True)
source = (root / 'AfxHookSource/MirvCalcs.cpp').read_text()
start = source.index('\tbool GetBase(')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[start:end]
code = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
void*sampledEntity=nullptr;
namespace SOURCESDK {
struct Vector {float x=0,y=0,z=0;};using QAngle=Vector;
struct matrix3x4_t {float m_flMatVal[3][4]{};};
namespace TF2 {struct IClientTools {void*FirstEntity(){return sampledEntity;}void*NextEntity(void*){return nullptr;}};}
}
constexpr int MIRV_TF2_MAX_BONES=4;
struct Globals {float curtime_get(){return 123.f;}} globals;using WrpGlobals=Globals;
struct View {Globals*GetGlobals(){return &globals;}} g_Hook_VClient_RenderView;
float renderOrigin[3]{},absoluteOrigin[3]{},renderAngles[3]{};bool hasRenderOrigin=true,hasRenderable=true,bonesValid=true;
struct CMirvTf2ClientRenderable {
 const void*GetRenderOrigin(){return hasRenderOrigin?renderOrigin:nullptr;}
 const void*GetRenderAngles(){return renderAngles;}
 const void*GetModel(){return this;}
 bool SetupBones(SOURCESDK::matrix3x4_t*out,int count,int,float time){
  assert(time==123.f);for(int b=0;b<count;++b)for(int r=0;r<3;++r){out[b].m_flMatVal[r][r]=1;out[b].m_flMatVal[r][3]=renderOrigin[r]+(r==2?64.f:0.f);}return bonesValid;
 }
} renderable;
struct CMirvTf2ClientUnknown {CMirvTf2ClientRenderable*GetClientRenderable(){return hasRenderable?&renderable:nullptr;}} object;
SOURCESDK::TF2::IClientTools tools;
bool Mirv_Tf2_IsValidBoneMatrix(const float(*)[4]){return true;}
void Mirv_AngleMatrix3(float,float,float,float out[3][3]){for(int r=0;r<3;++r)for(int c=0;c<3;++c)out[r][c]=r==c?1:0;}
void Mirv_MatrixAngles3(const float[3][3],float&x,float&y,float&z){x=y=z=0;}
bool Mirv_Basis3(const float m[3][4],float out[3][3]){for(int r=0;r<3;++r)for(int c=0;c<3;++c)out[r][c]=m[r][c];return true;}
struct Sampler {
 int m_BoneIndex=-1;double m_AnchorRoot[3]{},m_AnchorRootAngles[3]{};void*m_AnchorEntity=nullptr;
 bool Matches(SOURCESDK::TF2::IClientTools*,void*){return true;}
 bool BindModel(void*,const void*){return true;}
 bool CalcEntity(SOURCESDK::TF2::IClientTools*,SOURCESDK::Vector&p,SOURCESDK::QAngle&a){p={absoluteOrigin[0],absoluteOrigin[1],absoluteOrigin[2]};a={};return true;}
''' + method + r'''
};
int main(){
 sampledEntity=&object;Sampler s;SOURCESDK::Vector p;SOURCESDK::QAngle raw,facing;float base[3][3];bool ok;
 // Prediction/interpolation differences must not become false bone animation.
 // Exercise both root-only attachment and bone attachment throughout tick boundaries.
 for(int bone:{-1,1})for(int frame=0;frame<240;++frame){
  s.m_BoneIndex=bone;
  renderOrigin[0]=frame*.7f;renderOrigin[1]=std::sin(frame*.02f)*20;renderOrigin[2]=0;
  absoluteOrigin[0]=std::floor(frame/3.f)*2.1f+(frame%2?4.f:-4.f);absoluteOrigin[1]=0;absoluteOrigin[2]=frame%2?2.f:-2.f;
  assert(s.GetBase(&tools,p,base,raw,facing,ok)&&ok);
  assert(s.m_AnchorEntity==&object);
  assert(std::abs(s.m_AnchorRoot[0]-renderOrigin[0])<1e-5 && std::abs(s.m_AnchorRoot[2]-renderOrigin[2])<1e-5);
  assert(std::abs(p.x-renderOrigin[0])<1e-5 && std::abs(p.z-renderOrigin[2]-(bone<0?0:64))<1e-5);
 }
 // No render origin: retain the existing absolute-origin fallback atomically.
 s.m_BoneIndex=-1;hasRenderOrigin=false;absoluteOrigin[0]=17;absoluteOrigin[1]=23;absoluteOrigin[2]=29;
 assert(s.GetBase(&tools,p,base,raw,facing,ok)&&p.x==17&&p.y==23&&p.z==29);
 hasRenderOrigin=true;renderOrigin[1]=std::numeric_limits<float>::quiet_NaN();
 assert(s.GetBase(&tools,p,base,raw,facing,ok)&&p.x==17&&p.y==23&&p.z==29);
 hasRenderable=false;
 assert(s.GetBase(&tools,p,base,raw,facing,ok)&&p.x==17);
 s.m_BoneIndex=1;assert(!s.GetBase(&tools,p,base,raw,facing,ok));
 hasRenderable=true;renderOrigin[1]=0;bonesValid=false;assert(!s.GetBase(&tools,p,base,raw,facing,ok));
 std::cout<<"Production attachment sampling: coherent render root/bones across prediction differences and safe fallbacks passed\n";
}
'''
cpp = args.build_dir / 'sampling.cpp'
cpp.write_text(code)
exe = args.build_dir / 'sampling'
command = ['g++', '-std=c++17', '-O1', '-g', str(cpp), '-o', str(exe)]
if args.sanitize:
    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
subprocess.run(command, check=True)
subprocess.run([str(exe)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
