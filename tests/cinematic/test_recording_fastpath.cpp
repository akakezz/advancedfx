#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>
#include "shared/ColorConversionKernels.h"
#include "shared/PackedVideoFrame.h"
using namespace advancedfx;
int main(int argc,char**){
 std::mt19937 rng(921);
 for(size_t n=0;n<1025;++n) for(size_t offset=0;offset<4;++offset){
  std::vector<unsigned char> input(n*4+offset),expected(n*3+offset,0xab),actual=expected;
  for(auto &b:input)b=static_cast<unsigned char>(rng());
  ColorConversion::StripAlphaScalar(input.data()+offset,expected.data()+offset,n);
  ColorConversion::StripAlpha(input.data()+offset,actual.data()+offset,n);
  assert(actual==expected);
 }
 for(auto format:{ImageFormat::BGR,ImageFormat::BGRA,ImageFormat::A,ImageFormat::ZFloat})for(size_t pad:{0u,1u,31u,256u}){
  CImageFormat f(format,127,19);f=CImageFormat(format,127,19,f.Pitch+pad);
  std::vector<unsigned char> input(f.Bytes),scratch,expected;for(auto &b:input)b=static_cast<unsigned char>(rng());
  const size_t row=127*f.GetPixelStride();for(int y=0;y<f.Height;++y)expected.insert(expected.end(),input.begin()+y*f.Pitch,input.begin()+y*f.Pitch+row);
  auto *ptr=static_cast<const unsigned char*>(input.data());size_t bytes=0;assert(PackedVideoFrame(f,ptr,scratch,bytes));assert(bytes==expected.size());assert(std::equal(expected.begin(),expected.end(),ptr));assert((ptr==input.data())==(pad==0));
  f.Bytes--;assert(!PackedVideoFrame(f,ptr,scratch,bytes));
 }
 if(argc>1)for(auto wh:{std::pair<int,int>{1920,1080},{3840,2160}}){
  const size_t n=size_t(wh.first)*wh.second;std::vector<unsigned char> src(n*4,123),dst(n*3);volatile unsigned char check=0;
  auto bench=[&](auto fn){auto start=std::chrono::steady_clock::now();for(int i=0;i<70;++i){fn(src.data(),dst.data(),n);check^=dst[i];}return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/70;};
  std::cout<<wh.first<<"x"<<wh.second<<" scalar "<<bench(ColorConversion::StripAlphaScalar)<<" ms; selected "<<bench(ColorConversion::StripAlpha)<<" ms\n";
 }
 std::cout<<"Color conversion exact; padded/packed rawvideo verified; SSSE3 "<<ColorConversion::HasSsse3()<<"\n";
}
