// CPU kernel benchmark, not an in-game capture benchmark.  One float
// accumulator and two 8-bit BGRA input images, as in trapezoid sampling.
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cstring>
#include "shared/SamplerKernels.h"
using namespace advancedfx::SamplerKernels;
using Clock=std::chrono::steady_clock;
static volatile float sink;
template<bool New, bool Unequal> double Measure(int width,int height,int rounds){
 const size_t row=size_t(width)*4,n=row*height;
 std::vector<unsigned char>a(n),b(n); std::vector<float>dst(n);
 for(size_t i=0;i<n;++i){a[i]=static_cast<unsigned char>(i*13);b[i]=static_cast<unsigned char>(i*7+11);}
 const float w1=0.00043f,w2=0.00041f;
 auto sample=[&]{
  for(int y=0;y<height;++y){size_t o=y*row;float*d=dst.data()+o;auto*x=a.data()+o;auto*z=b.data()+o;
   if constexpr(New){if constexpr(Unequal)BytesWeightedPair(d,x,z,row,w1,w2);else Bytes<Operation::Pair>(d,x,z,row,w1);}
   else {if constexpr(Unequal){for(size_t i=0;i<row;++i)d[i]=d[i]+w1*x[i];}else for(size_t i=0;i<row;++i)d[i]=d[i]+w1*(unsigned(x[i])+unsigned(z[i]));}
  }
  if constexpr(!New&&Unequal)for(size_t i=0;i<n;++i)dst[i]=dst[i]+w2*b[i];
 };
 sample(); // warm up
 double best=1e9;
 for(int repeat=0;repeat<3;++repeat){
  std::fill(dst.begin(),dst.end(),0.f);auto start=Clock::now();for(int i=0;i<rounds;++i)sample();
  double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count()/rounds;
  best=std::min(best,ms);sink=dst[n/2];
 }
 return best;
}
int main(){
 for(auto size:{std::pair<int,int>{1920,1080},{3840,2160}}){int w=size.first,h=size.second;
  double a=Measure<false,false>(w,h,20),b=Measure<true,false>(w,h,20),c=Measure<false,true>(w,h,20),d=Measure<true,true>(w,h,20);
  std::printf("%dx%d equal: %.3f -> %.3f ms (%.2fx); unequal: %.3f -> %.3f ms (%.2fx)\n",w,h,a,b,a/b,c,d,c/d);
 }
}
