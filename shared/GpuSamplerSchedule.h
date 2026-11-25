#pragma once
#include "EasySampler.h"

// GPU backend uses the exact same temporal integration as the CPU sampler.
// Only the storage and per-pixel operations differ. Tokens are backend-owned
// image views, valid for the duration of Submit().
class GpuSamplerSchedule : private EasySamplerBase, private ISampleFns {
public:
    GpuSamplerSchedule(double inputDuration, double outputDuration, double exposure,
                       float strength, EasySamplerSettings::Method method)
        : EasySamplerBase(outputDuration, 0.0, exposure), m_InputDuration(inputDuration),
          m_Strength(strength), m_Trapezoid(method == EasySamplerSettings::ESM_Trapezoid) {}
    void Submit(const void *previous, const void *current) {
        m_Previous = m_Trapezoid ? previous : nullptr;
        m_Current = current;
        EasySamplerBase::Sample(m_Time);
        m_Time += m_InputDuration;
        m_Previous = m_Current = nullptr;
    }
    bool NeedsPrevious() const { return m_Trapezoid; }
protected:
    // Equal-weight pair must retain the CPU operation order w*(a+b).
    virtual void Accumulate(const void *a, const void *b, float wa, float wb, bool equalPair) = 0;
    virtual void Output(float whitePoint) = 0;
    virtual void Scale(float factor) = 0;
private:
    void SubSample(double a, double b, double lo, double hi) override {
        Integrator_Fn(this, m_Previous, m_Current, a,b,lo,hi);
    }
    void Fn_1(const void *a) override { Accumulate(a,nullptr,1,0,false); m_WhitePoint += 255.0f; }
    void Fn_2(const void *a, float w) override { Accumulate(a,nullptr,w,0,false); m_WhitePoint += w*255.0f; }
    void Fn_4(const void *a,const void *b,float w) override { Accumulate(a,b,w,w,true); m_WhitePoint += w*2.0f*255.0f; }
    void Fn_5(const void *a,const void *b,float wa,float wb) override {
        Accumulate(a,b,wa,wb,false); m_WhitePoint += wa*255.0f; m_WhitePoint += wb*255.0f;
    }
    void MakeFrame() override {
        Output(m_WhitePoint);
        const float factor = 1.0f-m_Strength;
        if (m_WhitePoint*factor != m_WhitePoint) {
            m_WhitePoint *= factor;
            Scale(m_WhitePoint == 0 ? 0 : factor);
        }
    }
    double m_InputDuration, m_Time=0;
    float m_Strength, m_WhitePoint=0;
    bool m_Trapezoid;
    const void *m_Previous=nullptr, *m_Current=nullptr;
};
