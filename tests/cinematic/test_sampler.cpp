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
#include "shared/GpuSamplerSchedule.h"
#include "shared/GpuCaptureFifo.h"
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

// CPU emulation of the GPU shader arithmetic. Compare the real sampler to the
// actual GPU scheduling adapter, including frame boundaries, shutter gaps and
// residual frame strength. This does not claim shader/driver execution.
struct Reference : EasyByteSamplerImpl {
 bool trap; float strength; const unsigned char *prev=nullptr,*cur=nullptr;
 std::vector<std::vector<unsigned char>> frames;
 Reference(double out,double exp,float st,bool t):EasyByteSamplerImpl(EasySamplerSettings(CImageFormat(ImageFormat::BGRA,3,2),t?EasySamplerSettings::ESM_Trapezoid:EasySamplerSettings::ESM_Rectangle,out,0,exp,st)),trap(t),strength(st){}
 void Feed(const unsigned char *p,double time){cur=p;EasySamplerBase::Sample(time);prev=p;}
 void SubSample(double a,double b,double c,double d) override {Integrator_Fn(this,trap?prev:nullptr,cur,a,b,c,d);}
 void MakeFrame() override {frames.emplace_back(24);PrintFrame(frames.back().data());ClearFrame(strength);}
};
struct GpuReference : GpuSamplerSchedule {
 std::vector<float> accum=std::vector<float>(24,0);std::vector<std::vector<unsigned char>> frames;
 GpuReference(double in,double out,double exp,float st,bool t):GpuSamplerSchedule(in,out,exp,st,t?EasySamplerSettings::ESM_Trapezoid:EasySamplerSettings::ESM_Rectangle){}
 void Accumulate(const void *a,const void *b,float wa,float wb,bool pair) override {
  auto x=static_cast<const unsigned char*>(a),y=static_cast<const unsigned char*>(b);
  for(size_t i=0;i<accum.size();++i){if(pair)accum[i]+=wa*(unsigned(x[i])+unsigned(y[i]));else{if(wa)accum[i]+=wa*x[i];if(wb)accum[i]+=wb*y[i];}}
 }
 void Output(float white) override {frames.emplace_back(24);for(size_t i=0;i<accum.size();++i) frames.back()[i]=static_cast<unsigned char>(white?accum[i]*(255.f/white):0.f);}
 void Scale(float f) override {for(auto &v:accum)v*=f;}
};
void CheckGpuSchedule(){
 for(double fps:{60.,120.,239.76,600.,1200.,2400.}) for(double output:{24.,30.,60.}) for(bool trap:{false,true}) for(double exp:{0.,0.2,0.5,0.99,1.}) for(float strength:{0.f,0.35f,1.f}){
  Reference ref(1/output,exp,strength,trap); GpuReference gpu(1/fps,1/output,exp,strength,trap);
  std::mt19937 rng(51);std::vector<unsigned char> previous(24),cur(24);double t=0;
  for(int i=0;i<173;++i){for(auto &c:cur)c=static_cast<unsigned char>(rng());ref.Feed(cur.data(),t);gpu.Submit(i?previous.data():nullptr,cur.data());previous.swap(cur);t+=1/fps;}
  assert(ref.frames==gpu.frames);
 }
}
void CheckGpuFifo(){
 GpuCaptureFifo<3> fifo;unsigned long long slots[3]={};std::mt19937 rng(7);
 for(unsigned i=0;i<10000;++i){
  if(!fifo.Full()&&(fifo.Empty()||rng()%2)){slots[fifo.WriteSlot()]=fifo.Written();fifo.Push();}
  else {assert(slots[fifo.ReadSlot()]==fifo.Read());fifo.Pop();}
 }
 while(!fifo.Empty()){assert(slots[fifo.ReadSlot()]==fifo.Read());fifo.Pop();}
 assert(fifo.Read()==fifo.Written());
}
int main(){
 CheckGpuSchedule();
 CheckGpuFifo();
 CheckRows<Operation::Add>();CheckRows<Operation::Weighted>();CheckRows<Operation::Pair>();
 for(auto f:{ImageFormat::BGR,ImageFormat::BGRA,ImageFormat::RGBA,ImageFormat::A})Timeline<EasyByteSamplerImpl,unsigned char>(f);
 Timeline<EasyFloatSamplerImpl,float>(ImageFormat::ZFloat);
 assert(CImageFormat(ImageFormat::RGBA,17,5).Pitch==68);
 RecordingTiming::Counter c;{RecordingTiming::Scope timer{c};}assert(c.Calls==1&&c.Milliseconds()>=0);
 RecordingTiming::Report("test","timing");uint64_t rev=0;std::vector<RecordingLog::Entry> entries;assert(RecordingLog::Get().Read(rev,entries)&&entries.size()==1&&entries[0].Text=="timing");
 std::printf("%016llx\n",static_cast<unsigned long long>(hash));
}
