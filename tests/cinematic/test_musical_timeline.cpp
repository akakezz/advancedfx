// Reproduce the SDK macro environment that MirvPgl.cpp inherits on Windows.
#ifdef AFX_TEST_SDK_SNPRINTF_MACRO
#include <cstdio>
#define snprintf _snprintf_s
#endif
#include "shared/MusicalTimeline.h"
#ifdef AFX_TEST_SDK_SNPRINTF_MACRO
#undef snprintf
#endif
#include <cassert>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <limits>
#include <iostream>
int main(){
 using namespace advancedfx;
 MusicalTimeline m;assert(m.Valid());assert(m.SnapTime(3.123)==3.123);
 m.Mode=2;m.BPM=128;m.Origin=690.123;m.OffsetMs=-2.5;m.Division=4;
 assert(std::abs(m.Quarter()-.46875)<1e-14);
 for(int i=-1000;i<100000;++i){double t=m.Downbeat()+i*m.Step();assert(std::abs(m.SnapTime(t+0.1*m.Step())-t)<1e-9);}
 char label[48];m.Position(m.Downbeat(),label,sizeof label);assert(!strcmp(label,"1.1"));m.Position(m.Downbeat()+m.Bar()+m.Beat(),label,sizeof label);assert(!strcmp(label,"2.2"));m.Position(m.Downbeat()-m.Beat(),label,sizeof label);assert(!strcmp(label,"-1.4"));
 char small[3]={'x','x','x'};m.Position(m.Downbeat(),small,sizeof small);assert(!strcmp(small,"1."));
 char empty='x';m.Position(m.Downbeat(),&empty,1);assert(empty=='\0');
 empty='x';m.Position(m.Downbeat(),&empty,0);assert(empty=='x');m.Position(m.Downbeat(),nullptr,0);
 m.Position(std::numeric_limits<double>::quiet_NaN(),label,sizeof label);assert(!strcmp(label,"--"));
 m.Position(m.Downbeat()+100000*m.Bar(),label,sizeof label);assert(!strcmp(label,"100001.1"));
 m.Numerator=7;m.Denominator=8;m.Division=16;m.Tuplet=3;assert(std::abs(m.Bar()-1.640625)<1e-14);assert(std::abs(m.Step()-.078125)<1e-14);
 assert(TimelineRulerStep(m,10000,8)==m.Step());assert(TimelineRulerStep(m,1,8)>=m.Bar());assert(m.Step()==.078125);
 m.Division=4; // quarter triplets do not divide the eighth-note beat in 7/8.
 const auto marks=TimelineRulerMarks(m,m.Downbeat(),m.Downbeat()+m.Bar()*2,1000,8);
 assert(std::any_of(marks.begin(),marks.end(),[&](const auto &mark){return mark.Kind==0&&std::abs(mark.Time-(m.Downbeat()+m.Step()))<1e-9;}));
 assert(std::any_of(marks.begin(),marks.end(),[&](const auto &mark){return mark.Kind==2&&std::abs(mark.Time-(m.Downbeat()+m.Bar()))<1e-9;}));
 const auto coarse=TimelineRulerMarks(m,m.Downbeat(),m.Downbeat()+m.Bar()*100,1,8);assert(!coarse.empty()&&std::all_of(coarse.begin(),coarse.end(),[](auto mark){return mark.Kind==2;}));m.Division=16;

 std::stringstream ss;ss<<std::setprecision(17);assert(m.Write(ss));MusicalTimeline loaded;assert(MusicalTimeline::Read(ss,loaded));assert(loaded.Origin==m.Origin&&loaded.BPM==m.BPM&&loaded.OffsetMs==m.OffsetMs&&loaded.Tuplet==3&&loaded.Numerator==7&&loaded.Denominator==8&&loaded.Division==16);
 std::stringstream invalid("2 0 0 0 4 4 8 1 1");assert(!MusicalTimeline::Read(invalid,loaded)&&loaded.BPM==128);
 m.Snap=false;assert(m.SnapTime(3.123)==3.123);m.Mode=1;assert(TimelineRulerStep(m,100,8)>=.1);m.BPM=std::numeric_limits<double>::quiet_NaN();assert(!m.Valid());
 std::cout<<"Musical timeline math, long sequence drift, meter, triplets, zoom and persistence passed\n";
}
