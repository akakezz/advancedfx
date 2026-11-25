#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
#include <cmath>
#include "shared/SamplerKernels.h"
#include "shared/EasySampler.h"
#include "shared/RecordingTiming.h"
using namespace advancedfx;
using namespace SamplerKernels;
static uint64_t hash=1469598103934665603ull;
static void Hash(const void *p,size_t n){for(auto c=static_cast<const unsigned char*>(p);n--;){hash^=*c++;hash*=1099511628211ull;}}
template<Operation Op> void CheckRows(){
 std::mt19937 rng(42);
 for(size_t n=0;n<130;++n) for(size_t offset=0;offset<4;++offset){
  std::vector<unsigned char>a(n+offset),b(n+offset);std::vector<float>f(n+offset),g(n+offset),x(n+offset+4),y;
  for(auto &v:a)v=static_cast<unsigned char>(rng());for(auto &v:b)v=static_cast<unsigned char>(rng());
  for(auto &v:f)v=float(rng()%10000)/173.f;for(auto &v:g)v=float(rng()%10000)/113.f;
  for(auto &v:x)v=float(rng()%10000)/19.f;y=x;
  Bytes<Op,false>(x.data()+offset,a.data()+offset,b.data()+offset,n,0.12537f);
  Bytes<Op,true>(y.data()+offset,a.data()+offset,b.data()+offset,n,0.12537f);assert(x==y);
  Floats<Op,false>(x.data()+offset,f.data()+offset,g.data()+offset,n,0.317f);
  Floats<Op,true>(y.data()+offset,f.data()+offset,g.data()+offset,n,0.317f);assert(x==y);
  Bytes<Operation::Weighted,false>(x.data()+offset,a.data()+offset,nullptr,n,0.12537f);
  Bytes<Operation::Weighted,false>(x.data()+offset,b.data()+offset,nullptr,n,0.317f);
  BytesWeightedPair(y.data()+offset,a.data()+offset,b.data()+offset,n,0.12537f,0.317f);assert(x==y);
  Floats<Operation::Weighted,false>(x.data()+offset,f.data()+offset,nullptr,n,0.12537f);
  Floats<Operation::Weighted,false>(x.data()+offset,g.data()+offset,nullptr,n,0.317f);
  FloatsWeightedPair(y.data()+offset,f.data()+offset,g.data()+offset,n,0.12537f,0.317f);assert(x==y);
 }
}
// Drive the real temporal integrator and pixel functions without a Windows buffer pool.
template<class Impl,class Pixel> class Fixture: public Impl {
 CImageFormat fmt; bool trapezoid; std::vector<Pixel> previous, current, output;
 bool hasPrevious=false; float strength;
 public: unsigned frames=0;
 Fixture(CImageFormat f,bool trap,double exposure,float st):Impl(EasySamplerSettings(f,trap?EasySamplerSettings::ESM_Trapezoid:EasySamplerSettings::ESM_Rectangle,1./60,0,exposure,st)),fmt(f),trapezoid(trap),output(f.Bytes/sizeof(Pixel)),strength(st){}
 void Feed(std::vector<Pixel> data,double time){current=std::move(data);EasySamplerBase::Sample(time);previous.swap(current);hasPrevious=true;}
 void SubSample(double a,double b,double c,double d)override{this->Integrator_Fn(this,hasPrevious&&trapezoid?previous.data():nullptr,current.data(),a,b,c,d);}
 void MakeFrame()override{std::fill(output.begin(),output.end(),Pixel{});this->PrintFrame(output.data());Hash(output.data(),fmt.Bytes);this->ClearFrame(strength);++frames;}
};
template<class Impl,class Pixel> void Timeline(ImageFormat format){
 CImageFormat packed(format,17,5);CImageFormat padded(format,17,5,packed.Pitch+8);assert(padded.Pitch>0);
 for(bool trap:{false,true})for(double exposure:{0.,0.25,0.5,1.})for(float strength:{0.4f,1.f}){
  Fixture<Impl,Pixel> fix(padded,trap,exposure,strength);std::mt19937 rng(123);
  for(unsigned i=0;i<213;++i){std::vector<Pixel> pixels(padded.Bytes/sizeof(Pixel));for(auto &v:pixels)v=static_cast<Pixel>(rng()%256);fix.Feed(std::move(pixels),double(i)/1200.);}
  assert(fix.frames==10);
 }
}
int main(){
 CheckRows<Operation::Add>();CheckRows<Operation::Weighted>();CheckRows<Operation::Pair>();
 for(auto f:{ImageFormat::BGR,ImageFormat::BGRA,ImageFormat::RGBA,ImageFormat::A})Timeline<EasyByteSamplerImpl,unsigned char>(f);
 Timeline<EasyFloatSamplerImpl,float>(ImageFormat::ZFloat);
 assert(CImageFormat(ImageFormat::RGBA,17,5).Pitch==68);
 RecordingTiming::Counter c;{RecordingTiming::Scope timer{c};}assert(c.Calls==1&&c.Milliseconds()>=0);
 RecordingTiming::Report("test","timing");uint64_t rev=0;std::vector<RecordingLog::Entry> entries;assert(RecordingLog::Get().Read(rev,entries)&&entries.size()==1&&entries[0].Text=="timing");
 std::printf("%016llx\n",static_cast<unsigned long long>(hash));
}
