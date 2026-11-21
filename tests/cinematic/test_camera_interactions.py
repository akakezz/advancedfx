from pathlib import Path
import subprocess
import argparse,os,tempfile
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,default=Path(tempfile.gettempdir())/'hlae-camera-interactions');p.add_argument('--sanitize',action='store_true');args=p.parse_args()
r=Path(__file__).resolve().parents[2];out=args.build_dir;out.mkdir(parents=True,exist_ok=True)
s=(r/'AfxHookSource/MirvCalcs.cpp').read_text();h=(r/'AfxHookSource/MirvCalcs.h').read_text()
def f(sig):
 at=s.index(sig);end=s.index('{',at)+1;d=1
 while d:
  d+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[at:end]
structs=h[h.index('struct MirvCameraAttachedState'):h.index('MirvCameraCollisionSettings MirvCameraCollisionGet();')]
code=r'''
#include "shared/CameraRig.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace Afx::Math;
'''+structs+r'''
struct MirvTf2Trace_t {bool startSolid=false,allSolid=false;float fraction=1;struct {float normal[3]{};}plane;};
double wall=-1e8;int returns=0;
bool CameraCollisionClear(const double*p,const MirvCameraCollisionSettings&s,void*=nullptr);
bool CameraCollisionClear(const double*p,const MirvCameraCollisionSettings&s,void*){return p[0]-s.Radius>=wall;}
bool CameraCollisionTrace(const double*from,const double*to,const MirvCameraCollisionSettings&s,MirvTf2Trace_t&hit,void*ignore=nullptr,void*only=nullptr){
 hit={};if(only)return true;
 const double plane=wall+s.Radius;
 if(from[0]<plane){hit.startSolid=true;hit.allSolid=to[0]<plane;hit.fraction=0;}
 else if(to[0]<plane){hit.fraction=float((from[0]-plane)/(from[0]-to[0]));hit.plane.normal[0]=1;}
 return true;
}
bool CameraCollisionMove(const double*,const double*to,const MirvCameraCollisionSettings&s,double*out,void*=nullptr);
bool CameraCollisionMove(const double*,const double*to,const MirvCameraCollisionSettings&s,double*out,void*){std::copy(to,to+3,out);out[0]=std::max(out[0],wall+s.Radius+.001);return true;}
'''+f('bool CameraCollisionSubjectClear(')+f('bool CameraCollisionFrustumSettings(')+f('bool MirvCameraCollisionConstrain(')+f('void CameraReturnTarget(')+f('bool MirvCameraAttachedReframe(')+f('bool MirvCameraAttachedEvaluate(')+r'''
int main(){
 // Bone filtering must not be undone by a raw-pivot aiming correction.
 for(double follow:{0.,.5}){MirvCameraAttachedState state;MirvCameraCollisionSettings cfg;cfg.FollowHalfLife=follow;cfg.PreserveFraming=true;cfg.BoneHalfLife=.1;wall=-1e8;
 double peak=0;for(int i=0;i<240;++i){double root[3]={0,0,0},pivot[3]={0,i%2?4.:-4.,64};float x=-100,y=float(pivot[1]),z=64,a[3]={};assert(MirvCameraAttachedEvaluate(state,cfg,1,(void*)1,(void*)2,pivot,i/60.,x,y,z,root,a));if(i>60)peak=std::max(peak,std::abs(double(a[1])));}
 assert(peak<.1);std::cout<<"Damped bone framing peak yaw "<<peak<<" degrees\n";
 }
 // Framing must not undo the orbit half-life on alternating bone yaw.
 {MirvCameraAttachedState h;MirvCameraCollisionSettings c;c.PreserveFraming=true;c.FollowHalfLife=.5;c.OrbitHalfLife=.15;double peak=0;
  for(int i=0;i<240;++i){double root[3]={},pivot[3]={0,0,64};const double yaw=i%2?10.:-10.;const double radians=yaw*3.141592653589793/180;float x=float(-100*std::cos(radians)),y=float(-100*std::sin(radians)),z=64,a[3]={0,float(yaw),0};
   assert(MirvCameraAttachedEvaluate(h,c,1,(void*)1,(void*)2,pivot,i/60.,x,y,z,root,a));if(i>120)peak=std::max(peak,std::abs(double(a[1])));}
  assert(peak<2);std::cout<<"Damped orbit framing peak yaw "<<peak<<" degrees\n";
 }
 // An explicit aim owner must not get positional corrections from a raw bone-view cone.
 {auto sample=[](bool preserve){MirvCameraAttachedState h;MirvCameraCollisionSettings c;c.FollowHalfLife=.5;c.PreserveFraming=preserve;c.AimOwnsAngles=true;float y=0;
 for(int i=0;i<2;++i){double root[3]={0,i?200.:0.,0},pivot[3]={0,root[1],64};float x=-100,z=64,a[3]={};y=float(root[1]);assert(MirvCameraAttachedEvaluate(h,c,1,(void*)1,(void*)2,pivot,i*.1,x,y,z,root,a));}return y;};
 assert(std::abs(sample(true)-sample(false))<1e-5);}
 // Repeated obstruction/release, framing changes and pause on one stable identity.
 for(bool framing:{false,true}){
  MirvCameraAttachedState state;MirvCameraCollisionSettings cfg;cfg.Attached=true;cfg.PreserveFraming=framing;cfg.FollowHalfLife=.5;cfg.ReturnHalfLife=.5;cfg.ReturnSpeed=30;cfg.FollowRecoveryDelay=0;
  double pivot[3]={0,0,64},root[3]={0,0,0};float previous=-100;
  for(int i=0;i<180;++i){
   wall=(i<10 || (i>=80&&i<90))?-60:-200;
   if(i==80){cfg.PreserveFraming=!cfg.PreserveFraming;}
   float x=-100,y=0,z=64,angles[3]={};
   assert(MirvCameraAttachedEvaluate(state,cfg,1,(void*)1,(void*)2,pivot,i/60.,x,y,z,root,angles));
   if(i==10||i==90)assert(x<previous && previous-x<=.5001 && x>-60);
   if(i>10 && i<80)assert(x<=previous+.001 && previous-x<=.5001);
   previous=x;
   float duplicate=-100,dy=0,dz=64,da[3]={};
   assert(MirvCameraAttachedEvaluate(state,cfg,1,(void*)1,(void*)2,pivot,i/60.,duplicate,dy,dz,root,da));assert(std::abs(duplicate-x)<1e-4);
  }
  assert(previous<-85);
 }
 // Post-modifier displacement uses the same swept collision primitive.
 {wall=-60;MirvCameraCollisionSettings cfg;cfg.Attached=true;double from[3]={-30,0,64},position[3]={-150,0,64};assert(MirvCameraCollisionConstrain(cfg,nullptr,from,position,nullptr));assert(position[0]>=-54);}
 std::cout<<"Obstructed framing/follow return, repeated sampling, toggles, and final modifier safety passed\n";
}

'''
code=code.replace('void*=nullptr','void* =nullptr');(out/'trace.cpp').write_text(code)
cmd=['g++','-std=c++17','-Dabstract=','-O1','-g','-I'+str(r),'-I'+str(r/'AfxHookSource'),'-include',str(r/'tests/cinematic/msvc_crt_compat.h'),str(out/'trace.cpp'),str(r/'shared/AfxMath.cpp'),'-o',str(out/'trace')]
if args.sanitize:cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
subprocess.run(cmd,check=True);subprocess.run([str(out/'trace')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
