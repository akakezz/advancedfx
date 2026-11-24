#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
namespace advancedfx::Tf2Offscreen
{
inline bool CompatibleSize(int w,int h,int outW,int outH)
{
 return w>0 && h>0 && outW>w && outH>h && outW<=8192 && outH<=8192 &&
    std::int64_t(w)*outH==std::int64_t(h)*outW;
}
struct View
{
 int X,UnscaledX,Y,UnscaledY,Width,UnscaledWidth,Height,Stereo,UnscaledHeight;
 unsigned char Ortho,Pad0[3];float OrthoBounds[4],Fov,FovViewmodel,Origin[3],Angles[3],Near,Far,NearVm,FarVm;
 unsigned char Subrect,Pad1[3];float Aspect;
 unsigned char OffCenter,Pad2[3];float OffCenterBounds[4];
 unsigned char Bloom,Cache,ProjectionOverride,Pad3;
 float Matrix[16];
};
static_assert(sizeof(View)==200 && offsetof(View,Near)==88 && offsetof(View,Aspect)==108 &&
    offsetof(View,Bloom)==132,"SDK 2013 view layout");
inline bool Prepare(void *data,std::size_t bytes,int w,int h)
{
 if(!data || bytes<sizeof(View))return false;
 View v{};std::memcpy(&v,data,sizeof(v));
 if(v.Stereo || v.Ortho || v.OffCenter || v.ProjectionOverride || v.Subrect ||
    v.X || v.Y || v.UnscaledX || v.UnscaledY ||
    v.Width!=v.UnscaledWidth || v.Height!=v.UnscaledHeight ||
    !CompatibleSize(v.Width,v.Height,w,h) || !std::isfinite(v.Fov) || v.Fov<=0 || v.Fov>=180 ||
    !std::isfinite(v.Near) || !std::isfinite(v.Far) || v.Near<=0 || v.Far<=v.Near ||
    !std::isfinite(v.Aspect) || (v.Aspect!=0 && std::abs(v.Aspect-float(v.Width)/v.Height)>0.01f))
  return false;
 v.Width=v.UnscaledWidth=w;v.Height=v.UnscaledHeight=h;v.Aspect=float(w)/h;v.Bloom=0;
 std::memcpy(data,&v,sizeof(v));return true;
}
}
