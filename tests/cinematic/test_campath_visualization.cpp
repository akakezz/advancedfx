#include "shared/CamPath.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

static void Linear(CamPath &p)
{
    p.PositionInterpMethod_set(CamPath::DI_LINEAR);
    p.RotationInterpMethod_set(CamPath::QI_SLINEAR);
    p.FovInterpMethod_set(CamPath::DI_LINEAR);
}
static double Distance(const CamPathValue &a, const CamPathValue &b)
{
    return std::hypot(std::hypot(a.X-b.X, a.Y-b.Y), a.Z-b.Z);
}
static void CheckSamples(CamPath &p, double begin, double end, size_t budget, double tolerance)
{
    const auto samples = p.SampleInterval(begin, end, tolerance, budget);
    assert(samples.size() >= 2 && samples.size() <= budget);
    assert(samples.front().first == begin && samples.back().first == end);
    for (size_t i=0; i<samples.size(); ++i)
    {
        assert(!i || samples[i-1].first < samples[i].first);
        assert(Distance(samples[i].second,p.Eval(samples[i].first)) < 1e-8);
    }
}
int main()
{
    CamPath path; Linear(path);
    path.Add(0, CamPathValue(0,0,0,0,0,0,90));
    auto key = CamPathValue(500,0,0,0,0,0,90);
    key.Selected = true; key.HoldDuration = .1;
    path.Add(5,key); path.Add(10,CamPathValue(1000,0,0,0,0,0,90));
    CamPath::Modifier wave;
    wave.Type = CamPath::ModifierType::Oscillation;
    wave.Begin=4; wave.End=6; wave.FadeIn=wave.FadeOut=0;
    wave.Frequency=.75; wave.Phase=0;
    wave.Amplitude[1]=30; wave.Amplitude[4]=20; wave.Amplitude[6]=5;
    assert(path.SetModifiers({wave}));
    const auto before = path.CaptureState();
    auto it=path.GetBegin(); ++it;
    const auto displayed = path.EvaluateKeyframePose(it.GetTime(),it.GetValue());
    const auto evaluated = path.Eval(it.GetTime());
    assert(Distance(displayed,evaluated)<1e-8);
    assert(Distance(displayed,it.GetValue())>1);
    assert(std::abs(displayed.Fov-evaluated.Fov)<1e-9);
    assert(std::abs(Afx::Math::DotProduct(displayed.R,evaluated.R))>1-1e-9);
    assert(displayed.Selected && displayed.HoldDuration==.1);
    assert(path.CaptureState()==before);
    CheckSamples(path,0,5,8192,.1); CheckSamples(path,5,10,8192,.1);
    // Short active windows must not disappear inside long key intervals.
    CamPath shortPath; Linear(shortPath);
    shortPath.Add(0,CamPathValue(0,0,0,0,0,0,90));
    shortPath.Add(100,CamPathValue(100,0,0,0,0,0,90));
    wave.Begin=42.013; wave.End=42.113; wave.Frequency=200; wave.Amplitude[1]=50;
    wave.Amplitude[4]=wave.Amplitude[6]=0;
    assert(shortPath.SetModifiers({wave}));
    const auto samples = shortPath.SampleInterval(0,100,.25,8192);
    assert(samples.size() <= 8192);
    double peak=0;
    for (const auto &s:samples) peak=(std::max)(peak,std::abs(s.second.Y));
    assert(peak>45);
    for (int i=1;i<1000;++i)
    {
        const double t=wave.Begin+(wave.End-wave.Begin)*i/1000;
        const auto right=std::lower_bound(samples.begin(),samples.end(),t,[](const auto &s,double x){return s.first<x;});
        assert(right!=samples.begin()&&right!=samples.end());
        const auto left=std::prev(right);
        const double u=(t-left->first)/(right->first-left->first);
        assert(std::abs(shortPath.Eval(t).Y-(left->second.Y+(right->second.Y-left->second.Y)*u))<1.0);
    }
    // Over-budget periodic effects should not vanish through phase aliasing.
    wave.Begin=0; wave.End=100; wave.Frequency=1000;
    assert(shortPath.SetModifiers({wave}));
    const auto saturated=shortPath.SampleInterval(0,100,.25,64);
    peak=0;
    for (const auto &s:saturated) peak=(std::max)(peak,std::abs(s.second.Y));
    assert(saturated.size()<=64 && peak>30);
    CamPath::Modifier step;
    step.Type=CamPath::ModifierType::Stepped;step.Begin=0;step.End=100;
    step.Frequency=1;step.FadeIn=step.FadeOut=0;step.Amplitude[0]=1;
    assert(shortPath.SetModifiers({step}));
    const auto stepped=shortPath.SampleInterval(0,100,.25,8192);
    size_t jumps=0;
    for(size_t i=1;i<stepped.size();++i)
        if(std::nextafter(stepped[i-1].first,stepped[i].first)==stepped[i].first && Distance(stepped[i-1].second,stepped[i].second)>.5) ++jumps;
    assert(jumps>=90);
    for (size_t budget : {2u,3u,8u,32u,1024u}) CheckSamples(shortPath,0,100,budget,.25);
    assert(shortPath.SampleInterval(1,1,.25,64).empty());
    CamPath incomplete; key.HoldDuration=0; incomplete.Add(1,key);
    assert(!incomplete.CanEval());
    assert(Distance(incomplete.EvaluateKeyframePose(1,key),key)==0);
    CamPath held; Linear(held); key.HoldDuration=2; held.Add(0,key);
    wave.Begin=0; wave.End=2; wave.Frequency=.75;
    assert(held.SetModifiers({wave}) && held.CanEval());
    CheckSamples(held,0,2,1024,.1);
    // Existing unmodified and noise-sampling accuracy expectations.
    CamPath line; Linear(line); line.Add(0,CamPathValue(0,0,0,0,0,0,90));
    line.Add(2,CamPathValue(100,0,0,0,0,0,90));
    const auto straight=line.SampleInterval(0,2,.01,1024);
    assert(straight.size()>=2 && straight.size()<32);
    CamPath::Modifier noise; noise.Begin=0; noise.End=2; noise.Amplitude[1]=5;
    assert(line.SetModifiers({noise}));
    const auto curved=line.SampleInterval(0,2,.02,512);
    assert(curved.size()<=512 && curved.size()>straight.size());
    for(int i=1;i<400;++i){const double t=i*.005;
        const auto r=std::lower_bound(curved.begin(),curved.end(),t,[](const auto &s,double x){return s.first<x;});
        const auto l=std::prev(r); const double u=(t-l->first)/(r->first-l->first);
        assert(std::abs(line.Eval(t).Y-(l->second.Y+(r->second.Y-l->second.Y)*u))<.05);
    }
    std::cout << "Evaluated markers, narrow modifier ranges, bounded sampling and " << jumps << " stepped discontinuities passed\n";
}
