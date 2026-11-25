#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <charconv>
#include <cstddef>
#include <istream>
#include <ostream>
#include <vector>

namespace advancedfx {
// Editorial timebase only. Native demo and client timestamps never change on mode/tempo edits.
struct MusicalTimeline {
 int Mode=0; // 0 ticks, 1 seconds, 2 bars/beats
 double BPM=120, Origin=0, OffsetMs=0; // Origin in demo seconds, not client time.
 int Numerator=4, Denominator=4, Division=8, Tuplet=1;
 bool Snap=true;
 bool Valid() const {
  return Mode>=0 && Mode<=2 && std::isfinite(BPM) && BPM>=20 && BPM<=400 &&
   std::isfinite(Origin) && Origin>=0 && Origin<=1e9 && std::isfinite(OffsetMs) && std::abs(OffsetMs)<=60000 &&
   Numerator>=1 && Numerator<=32 && (Denominator==1 || Denominator==2 || Denominator==4 || Denominator==8 || Denominator==16) &&
   (Division==1 || Division==2 || Division==4 || Division==8 || Division==16 || Division==32) && (Tuplet==1 || Tuplet==3);
 }
 double Downbeat() const {return Origin+OffsetMs*.001;}
 double Quarter() const {return 60/BPM;}
 double Beat() const {return Quarter()*4/Denominator;}
 double Bar() const {return Beat()*Numerator;}
 double Step() const {return Quarter()*4/Division*(Tuplet==3 ? 2.0/3 : 1.0);}
 double SnapTime(double t) const {return Mode==2 && Snap && Valid() && std::isfinite(t) ? (std::max)(0.,Downbeat()+std::round((t-Downbeat())/Step())*Step()) : t;}
 void Position(double t,char*buf,size_t n) const {
  if (!buf || !n) return;
  // Source SDK headers can redefine snprintf as _snprintf_s. Format the
  // integer ruler labels without printf macros, heap allocation or locale.
  char label[48]; // two signed 64-bit integers and a separator fit.
  char *end=label;
  if (!Valid() || !std::isfinite(t) || std::abs(t)>1e9) {
   *end++='-'; *end++='-';
  } else {
   const double b=(t-Downbeat())/Beat();
   const auto beat=static_cast<long long>(std::floor(b+1e-9));
   const auto bar=static_cast<long long>(std::floor(double(beat)/Numerator));
   const auto within=beat-bar*Numerator;
   const auto first=std::to_chars(label,label+sizeof(label)-1,bar>=0?bar+1:bar);
   if (first.ec!=std::errc{}) {buf[0]='\0';return;}
   end=first.ptr;
   *end++='.';
   const auto second=std::to_chars(end,label+sizeof(label),within+1);
   if (second.ec!=std::errc{}) {buf[0]='\0';return;}
   end=second.ptr;
  }
  const auto count=(std::min)(n-1,static_cast<size_t>(end-label));
  std::copy_n(label,count,buf);
  buf[count]='\0';
 }
 bool Write(std::ostream &out) const {if(!Valid())return false;out<<Mode<<' '<<BPM<<' '<<Origin<<' '<<OffsetMs<<' '<<Numerator<<' '<<Denominator<<' '<<Division<<' '<<Tuplet<<' '<<Snap<<'\n';return bool(out);}
 static bool Read(std::istream &in,MusicalTimeline &out) {MusicalTimeline next;if(!(in>>next.Mode>>next.BPM>>next.Origin>>next.OffsetMs>>next.Numerator>>next.Denominator>>next.Division>>next.Tuplet>>next.Snap)||!next.Valid())return false;out=next;return true;}
};
// All consumers use the same editor settings; only the project loader and editor write them.
inline MusicalTimeline &TimelineTimebase(){static MusicalTimeline value;return value;}
// Choose display spacing independently of snap resolution. All marks are computed
// directly from their index (no accumulated rounding). At wide zoom show whole bars.
inline double TimelineRulerStep(const MusicalTimeline &m,double pixelsPerSecond,double minPixels){
 if (!m.Valid() || !(pixelsPerSecond>0) || !std::isfinite(pixelsPerSecond)) return 0;
 if(m.Mode==1){double step=.001;while(step*pixelsPerSecond<minPixels && step<1e10)step*=10;return step;}
 if(m.Mode!=2)return 0;
 double step=(std::min)(m.Step(),m.Beat());
 if(step*pixelsPerSecond<minPixels)step=m.Beat();
 if(step*pixelsPerSecond<minPixels)step=m.Bar();
 while(step*pixelsPerSecond<minPixels && step<1e10)step*=2;
 return step;
}
struct TimelineMark { double Time; int Kind; }; // 0 subdivision, 1 beat, 2 bar/seconds label
inline std::vector<TimelineMark> TimelineRulerMarks(const MusicalTimeline &m,double start,double end,double pps,double minPixels){
 std::vector<TimelineMark> marks;
 if(!m.Valid() || m.Mode==0 || !std::isfinite(start) || !std::isfinite(end) || end<start || !std::isfinite(pps) || !(pps>0) || !(minPixels>0))return marks;
 const double origin=m.Mode==2?m.Downbeat():0.;
 auto add=[&](double step,int kind){
  if(!(step>0) || !std::isfinite(step))return;
  const double first=std::ceil((start-origin)/step-1e-9);
  for(int i=0;i<8192;++i){double t=origin+(first+i)*step;if(t>end+1e-8)break;if(t>=start-1e-8)marks.push_back({t,kind});}
 };
 if(m.Mode==1)add(TimelineRulerStep(m,pps,minPixels),2);
 else {
  // Draw the actual snap subdivision even when it does not divide the meter
  // beat (e.g. quarter-note triplets in 7/8). Beat and bar marks are separate.
  if(m.Step()*pps>=minPixels)add(m.Step(),0);
  if(m.Beat()*pps>=minPixels)add(m.Beat(),1);
  double bar=m.Bar();while(bar*pps<minPixels&&bar<1e10)bar*=2;
  add(bar,2);
 }
 std::sort(marks.begin(),marks.end(),[](const TimelineMark&a,const TimelineMark&b){return a.Time<b.Time;});
 size_t n=0;
 for(const auto &mark:marks){if(n && std::abs(mark.Time-marks[n-1].Time)<1e-7){if(mark.Kind>marks[n-1].Kind)marks[n-1]=mark;}else marks[n++]=mark;}
 marks.resize(n);return marks;
}

}
