#include "stdafx.h"
#include <Windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include <cstring>
#include <chrono>
#include <shared/GpuSamplerSchedule.h>
#include <shared/GpuCaptureFifo.h>
#include <shared/RecordingTiming.h>
#include "AfxStreams.h"
#include "GpuScreenCapture.h"

using Microsoft::WRL::ComPtr;
using namespace advancedfx;
extern CGrowingBufferPoolThreadSafe g_ImageBufferPoolThreadSafe;

namespace {
constexpr unsigned RingSize=3;
constexpr auto WaitLimit=std::chrono::seconds(10);
// UNORM reads reconstruct the original 8-bit value. FP32 accumulation uses
// the old gamma-encoded color space; no implicit sRGB conversion or weight cutoff.
const char AccShader[]=R"hlsl(
Texture2D<float4> a : register(t0);
Texture2D<float4> b : register(t1);
Texture2D<float4> old : register(t2);
RWTexture2D<float4> dest : register(u0);
cbuffer Params : register(b0) { float wa; float wb; float keep; float equalPair; };
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint w,h; dest.GetDimensions(w,h); if(id.x>=w || id.y>=h) return;
    int3 p=int3(id.xy,0);
    precise float4 v=old.Load(p)*keep;
    if(equalPair!=0) v += wa*(round(a.Load(p)*255.0)+round(b.Load(p)*255.0));
    else {
        if(wa!=0) v += wa*round(a.Load(p)*255.0);
        if(wb!=0) v += wb*round(b.Load(p)*255.0);
    }
    dest[id.xy]=v;
}
)hlsl";
const char OutShader[]=R"hlsl(
Texture2D<float4> accum : register(t0);
RWTexture2D<uint4> dest : register(u0);
cbuffer Params : register(b0) { float scale; float p1; float p2; float p3; };
[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint w,h; dest.GetDimensions(w,h); if(id.x>=w || id.y>=h) return;
    // RGBA UINT stores explicit BGRA bytes, matching the existing capture worker.
    precise float4 v=accum.Load(int3(id.xy,0))*scale;
    dest[id.xy]=(uint4)floor(clamp(v.bgra,0.0,255.0));
}
)hlsl";
struct Texture {
    ComPtr<ID3D11Texture2D> Tex;
    ComPtr<ID3D11ShaderResourceView> Srv;
    ComPtr<ID3D11UnorderedAccessView> Uav;
};
struct InputSlot {
    ComPtr<IDirect3DTexture9> Shared9;
    ComPtr<IDirect3DSurface9> Surface9;
    ComPtr<IDirect3DQuery9> Ready9;
    Texture Shared11;
    ComPtr<ID3D11Query> Done11;
    bool Busy11=false;
};
struct OutputSlot {
    ComPtr<ID3D11Texture2D> Staging;
    ComPtr<ID3D11Query> Done;
};
// A completed GPU frame has independent CPU ownership. The existing screen
// worker does BGR conversion and encoding; no game/GPU state on that worker.
class CpuFrame final : public CRefCountedThreadSafe, public IAfxD3D9CaptureBuffer {
public:
    explicit CpuFrame(TImageBuffer<true> *buffer) : m_Buffer(buffer) { buffer->AddRef(); }
    void AddRef() override { CRefCountedThreadSafe::AddRef(); }
    void Release() override { CRefCountedThreadSafe::Release(); }
    IImageBufferThreadSafe *LockCpu() override { m_Buffer->AddRef(); return m_Buffer; }
private:
    ~CpuFrame() override { m_Buffer->Release(); }
    TImageBuffer<true> *m_Buffer;
};
class CaptureImpl final : public GpuScreenCapture, private IAfxD3D9OnRelease, private GpuSamplerSchedule {
public:
    CaptureImpl(const GpuCaptureSettings &s, ICaptureOutput *output)
        : GpuSamplerSchedule(s.InputDuration,s.OutputDuration,s.Exposure,s.Strength,s.Method),
          m_Output(output), m_Sampling(s.Sampling) { m_Output->AddRef(); }
    ~CaptureImpl() override { ReleaseGpu(); if(m_Output) m_Output->Release(); }
    bool Init() {
        RecordingTiming::Scope time{m_InitTime};
        auto native=AfxGetDirect3DDevice9Ex();
        auto src=AfxGetRenderTargetSurface();
        if(!native || !src) return Reject("D3D9Ex render target unavailable");
        D3DSURFACE_DESC d{};
        if(FAILED(src->GetDesc(&d)) || d.Format!=D3DFMT_A8R8G8B8 || d.Pool!=D3DPOOL_DEFAULT || !d.Width || !d.Height || d.Width>8192 || d.Height>8192)
            return Reject("unsupported render target format or size");
        m_W=d.Width; m_H=d.Height;
        // Bound resource allocations for the 32-bit Source engine. Large
        // targets remain on the established CPU path.
        if(static_cast<unsigned long long>(m_W)*m_H > 10000000ull) return Reject("GPU capture size exceeds experimental memory limit");
        m_D9=native;
        D3DDEVICE_CREATION_PARAMETERS cp{};
        ComPtr<IDirect3D9> d9;
        ComPtr<IDirect3D9Ex> d9ex;
        LUID luid{};
        if(FAILED(native->GetCreationParameters(&cp)) || FAILED(native->GetDirect3D(d9.GetAddressOf())) ||
           FAILED(d9.As(&d9ex)) || FAILED(d9ex->GetAdapterLUID(cp.AdapterOrdinal,&luid))) return Reject("could not identify D3D9 adapter");
        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return Reject("DXGI factory creation failed");
        for(UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> candidate;
            if(FAILED(factory->EnumAdapters1(i,candidate.GetAddressOf()))) break;
            DXGI_ADAPTER_DESC1 desc{};
            if(SUCCEEDED(candidate->GetDesc1(&desc)) && desc.AdapterLuid.LowPart==luid.LowPart && desc.AdapterLuid.HighPart==luid.HighPart) { adapter=candidate; break; }
        }
        if(!adapter) return Reject("no matching D3D11 adapter");
        const D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
        if(FAILED(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,&level,1,D3D11_SDK_VERSION,m_D11.GetAddressOf(),nullptr,m_Context.GetAddressOf())))
            return Reject("D3D11 feature level 11_0 unavailable");
        for(auto &s:m_Inputs) {
            HANDLE handle=nullptr;
            if(FAILED(native->CreateTexture(m_W,m_H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,s.Shared9.GetAddressOf(),&handle)) || !handle ||
               FAILED(s.Shared9->GetSurfaceLevel(0,s.Surface9.GetAddressOf())) || FAILED(native->CreateQuery(D3DQUERYTYPE_EVENT,s.Ready9.GetAddressOf())) ||
               FAILED(m_D11->OpenSharedResource(handle,IID_PPV_ARGS(s.Shared11.Tex.GetAddressOf()))) || !NewQuery(s.Done11)) return Reject("D3D9Ex/D3D11 shared texture or event query unavailable");
            D3D11_TEXTURE2D_DESC desc{}; s.Shared11.Tex->GetDesc(&desc);
            if(desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || desc.Width!=m_W || desc.Height!=m_H || desc.SampleDesc.Count!=1 ||
               FAILED(m_D11->CreateShaderResourceView(s.Shared11.Tex.Get(),nullptr,s.Shared11.Srv.GetAddressOf()))) return Reject("shared texture format/view incompatible");
        }
        if(m_Sampling) {
            if(!Compile(AccShader,m_AccShader) || !Compile(OutShader,m_OutShader)) return Reject("compute shader compilation failed");
            D3D11_BUFFER_DESC bd{}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(m_D11->CreateBuffer(&bd,nullptr,m_Params.GetAddressOf()))) return Reject("constant buffer allocation failed");
            for(auto &t:m_Accum) if(!NewTexture(t,DXGI_FORMAT_R32G32B32A32_FLOAT,true,true)) return Reject("FP32 accumulator allocation failed");
            if(!NewTexture(m_Packed,DXGI_FORMAT_R8G8B8A8_UINT,false,true)) return Reject("output texture allocation failed");
            if(NeedsPrevious() && !NewTexture(m_Previous,DXGI_FORMAT_B8G8R8A8_UNORM,true,false)) return Reject("previous sample allocation failed");
            float zero[4]={}; m_Context->ClearUnorderedAccessViewFloat(m_Accum[0].Uav.Get(),zero);
        }
        for(auto &s:m_Outputs) {
            D3D11_TEXTURE2D_DESC desc=Desc(m_Sampling?DXGI_FORMAT_R8G8B8A8_UINT:DXGI_FORMAT_B8G8R8A8_UNORM);
            desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            if(FAILED(m_D11->CreateTexture2D(&desc,nullptr,s.Staging.GetAddressOf())) || !NewQuery(s.Done)) return Reject("staging texture allocation failed");
        }
        // Verify a real game-surface copy before choosing the GPU backend. If
        // this fails, no samples have been consumed and CPU fallback is safe.
        auto &probe=m_Inputs[0];
        if(FAILED(native->StretchRect(src,nullptr,probe.Surface9.Get(),nullptr,D3DTEXF_NONE)) ||
           FAILED(probe.Ready9->Issue(D3DISSUE_END)) || !Wait9(probe.Ready9.Get()))
            return Reject("initial D3D9 shared texture copy failed");
        AfxD3D9OnReleaseAdd(this); m_Registered=true;
        RecordingTiming::Report("GPU capture backend",m_Sampling?"D3D11 FP32 sampler active (experimental).\n":"D3D11 pipelined regular capture active (experimental).\n");
        return true;
    }
    void Capture() override {
        if(m_Failed || !m_Registered) return;
        RecordingTiming::Scope time{m_SubmitTime};
        auto src=AfxGetRenderTargetSurface(); D3DSURFACE_DESC desc{};
        if(!src || FAILED(src->GetDesc(&desc)) || desc.Width!=m_W || desc.Height!=m_H || desc.Format!=D3DFMT_A8R8G8B8) { Fail("render target changed"); return; }
        auto &s=m_Inputs[m_InputFifo.WriteSlot()];
        if(s.Busy11) {
            if(!Wait11(s.Done11.Get())) { Fail("D3D11 input reuse wait failed"); return; }
            s.Busy11=false;
        }
        if(FAILED(m_D9->StretchRect(src,nullptr,s.Surface9.Get(),nullptr,D3DTEXF_NONE)) || FAILED(s.Ready9->Issue(D3DISSUE_END))) { Fail("D3D9 sample copy failed"); return; }
        m_InputFifo.Push();
        if(m_InputFifo.Full()) ProcessOldest();
    }
    void Finish() override {
        auto stop=RecordingTiming::Clock::now();
        if(m_Registered && !m_Failed) {
            while(!m_InputFifo.Empty() && !m_Failed) ProcessOldest();
            while(!m_OutputFifo.Empty() && !m_Failed) DownloadOldest();
        }
        ReleaseGpu();
        if(m_Output) { m_Output->Release(); m_Output=nullptr; }
        const double drainMs=std::chrono::duration<double,std::milli>(RecordingTiming::Clock::now()-stop).count();
        char msg[640];
        ::_snprintf_s(msg,sizeof(msg),_TRUNCATE,"%ux%u | %s | %llu input samples, %llu processed, %llu delivered, failed=%i.\nSubmit %.2f ms; D3D9 wait %.2f ms; D3D11 wait %.2f ms; staging map/copy %.2f ms; worker handoff %.2f ms.\nBackend initialization %.2f ms; final GPU/encoder drain %.2f ms.\n",
          m_W,m_H,m_Sampling?"D3D11 GPU sampler":"D3D11 regular capture",m_InputFifo.Written(),m_InputFifo.Read(),m_OutputFifo.Read(),m_Failed?1:0,
          m_SubmitTime.Milliseconds(),m_Wait9Time.Milliseconds(),m_Wait11Time.Milliseconds(),m_MapTime.Milliseconds(),m_HandoffTime.Milliseconds(),m_InitTime.Milliseconds(),drainMs);
        RecordingTiming::Report("GPU backend timing",msg);
    }
private:
    bool Reject(const char *why) { RecordingTiming::Report("GPU capture fallback",why); return false; }
    void Fail(const char *why) {
        if(m_Failed) return; m_Failed=true;
        RecordingTiming::Report("GPU capture ERROR",why);
        RecordingTiming::Report("GPU capture ERROR","Take incomplete; stop recording and retry with GPU capture disabled.\n");
    }
    void AfxD3D9OnRelease() override { Fail("device reset/release during GPU capture"); ReleaseGpu(); }
    void ReleaseGpu() {
        if(m_Registered) { AfxD3D9OnReleaseRemove(this); m_Registered=false; }
        // Drop D3D11 bindings before releasing shared D3D9 resources. No CPU
        // fallback after reset: the already-written output has its own timebase.
        if(m_Context) { m_Context->ClearState(); m_Context->Flush(); }
        m_Outputs={}; m_Inputs={}; m_Accum={}; m_Previous={}; m_Packed={};
        m_AccShader.Reset(); m_OutShader.Reset(); m_Params.Reset(); m_Context.Reset(); m_D11.Reset(); m_D9.Reset();
    }
    D3D11_TEXTURE2D_DESC Desc(DXGI_FORMAT format) const {
        D3D11_TEXTURE2D_DESC d{}; d.Width=m_W; d.Height=m_H; d.MipLevels=1; d.ArraySize=1;
        d.Format=format; d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT; return d;
    }
    bool NewTexture(Texture &t, DXGI_FORMAT format,bool srv,bool uav) {
        auto d=Desc(format); d.BindFlags=(srv?D3D11_BIND_SHADER_RESOURCE:0)|(uav?D3D11_BIND_UNORDERED_ACCESS:0);
        return SUCCEEDED(m_D11->CreateTexture2D(&d,nullptr,t.Tex.GetAddressOf())) &&
            (!srv || SUCCEEDED(m_D11->CreateShaderResourceView(t.Tex.Get(),nullptr,t.Srv.GetAddressOf()))) &&
            (!uav || SUCCEEDED(m_D11->CreateUnorderedAccessView(t.Tex.Get(),nullptr,t.Uav.GetAddressOf())));
    }
    bool NewQuery(ComPtr<ID3D11Query> &q) { D3D11_QUERY_DESC d{D3D11_QUERY_EVENT,0}; return SUCCEEDED(m_D11->CreateQuery(&d,q.GetAddressOf())); }
    bool Compile(const char *code,ComPtr<ID3D11ComputeShader> &shader) {
        ComPtr<ID3DBlob> blob,errors;
        HRESULT hr=D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS,0,blob.GetAddressOf(),errors.GetAddressOf());
        if(FAILED(hr)) { if(errors) RecordingTiming::Report("GPU shader",static_cast<const char*>(errors->GetBufferPointer())); return false; }
        return SUCCEEDED(m_D11->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,shader.GetAddressOf()));
    }
    bool Wait9(IDirect3DQuery9 *q) {
        RecordingTiming::Scope t{m_Wait9Time}; auto end=RecordingTiming::Clock::now()+WaitLimit;
        for(;;) { HRESULT hr=q->GetData(nullptr,0,D3DGETDATA_FLUSH); if(hr==S_OK) return true; if(hr!=S_FALSE || RecordingTiming::Clock::now()>=end) return false; Sleep(0); }
    }
    bool Wait11(ID3D11Query *q) {
        RecordingTiming::Scope t{m_Wait11Time}; auto end=RecordingTiming::Clock::now()+WaitLimit;
        for(;;) { BOOL done=FALSE; HRESULT hr=m_Context->GetData(q,&done,sizeof(done),0); if(hr==S_OK && done) return true; if(FAILED(hr) || RecordingTiming::Clock::now()>=end) return false; Sleep(0); }
    }
    void ProcessOldest() {
        auto &s=m_Inputs[m_InputFifo.ReadSlot()];
        if(!Wait9(s.Ready9.Get())) { Fail("D3D9 shared sample wait failed"); return; }
        if(m_Sampling) {
            Submit(m_HavePrevious?m_Previous.Srv.Get():nullptr,s.Shared11.Srv.Get());
            if(NeedsPrevious() && !m_Failed) { m_Context->CopyResource(m_Previous.Tex.Get(),s.Shared11.Tex.Get()); m_HavePrevious=true; }
        } else Stage(s.Shared11.Tex.Get());
        if(m_Failed) return;
        m_Context->End(s.Done11.Get()); m_Context->Flush(); s.Busy11=true; m_InputFifo.Pop();
    }
    void Dispatch(ID3D11ComputeShader *shader,ID3D11ShaderResourceView **src,UINT count,ID3D11UnorderedAccessView *dest,const float (&params)[4]) {
        if(m_Failed) return;
        m_Context->UpdateSubresource(m_Params.Get(),0,nullptr,params,0,0);
        auto cb=m_Params.Get(); m_Context->CSSetConstantBuffers(0,1,&cb);
        m_Context->CSSetShader(shader,nullptr,0); m_Context->CSSetShaderResources(0,count,src);
        m_Context->CSSetUnorderedAccessViews(0,1,&dest,nullptr);
        m_Context->Dispatch((m_W+7)/8,(m_H+7)/8,1);
        ID3D11ShaderResourceView *empty[3]={}; ID3D11UnorderedAccessView *nullUav=nullptr;
        m_Context->CSSetShaderResources(0,count,empty); m_Context->CSSetUnorderedAccessViews(0,1,&nullUav,nullptr);
    }
    void Apply(const void *a,const void *b,float wa,float wb,float keep,bool equalPair) {
        if(m_Failed) return;
        ID3D11ShaderResourceView *src[]={static_cast<ID3D11ShaderResourceView*>(const_cast<void*>(a)),static_cast<ID3D11ShaderResourceView*>(const_cast<void*>(b)),m_Accum[m_Active].Srv.Get()};
        float params[]={wa,wb,keep,equalPair?1.0f:0.0f};
        Dispatch(m_AccShader.Get(),src,3,m_Accum[1-m_Active].Uav.Get(),params); m_Active=1-m_Active;
    }
    void Accumulate(const void *a,const void *b,float wa,float wb,bool eq) override { Apply(a,b,wa,wb,1,eq); }
    void Scale(float factor) override {
        if(m_Failed) return;
        if(factor==0) { float zero[4]={}; m_Context->ClearUnorderedAccessViewFloat(m_Accum[m_Active].Uav.Get(),zero); }
        else Apply(nullptr,nullptr,0,0,factor,false);
    }
    void Output(float white) override {
        if(m_Failed) return;
        auto src=m_Accum[m_Active].Srv.Get(); float params[]={white?255.0f/white:0.0f,0,0,0};
        Dispatch(m_OutShader.Get(),&src,1,m_Packed.Uav.Get(),params); Stage(m_Packed.Tex.Get());
    }
    void Stage(ID3D11Texture2D *source) {
        if(m_Failed) return;
        if(m_OutputFifo.Full()) DownloadOldest();
        if(m_Failed) return;
        auto &s=m_Outputs[m_OutputFifo.WriteSlot()];
        m_Context->CopyResource(s.Staging.Get(),source); m_Context->End(s.Done.Get()); m_OutputFifo.Push();
    }
    void DownloadOldest() {
        auto &s=m_Outputs[m_OutputFifo.ReadSlot()];
        if(!Wait11(s.Done.Get())) { Fail("D3D11 staging wait failed"); return; }
        TImageBuffer<true> *buffer=new TImageBuffer<true>(&g_ImageBufferPoolThreadSafe); buffer->AddRef();
        if(!buffer->GrowAlloc(CImageFormat(ImageFormat::BGRA,m_W,m_H))) { buffer->Release(); Fail("CPU output buffer allocation failed"); return; }
        {
            RecordingTiming::Scope time{m_MapTime}; D3D11_MAPPED_SUBRESOURCE map{};
            if(FAILED(m_Context->Map(s.Staging.Get(),0,D3D11_MAP_READ,0,&map))) { buffer->Release(); Fail("D3D11 staging map failed"); return; }
            if(!map.pData || map.RowPitch<size_t(m_W)*4) { m_Context->Unmap(s.Staging.Get(),0); buffer->Release(); Fail("invalid staging row pitch"); return; }
            auto *dst=static_cast<unsigned char*>(buffer->GetImageBufferData()); auto *src=static_cast<const unsigned char*>(map.pData);
            for(UINT y=0;y<m_H;++y) memcpy(dst+size_t(y)*m_W*4,src+size_t(y)*map.RowPitch,size_t(m_W)*4);
            m_Context->Unmap(s.Staging.Get(),0);
        }
        auto *frame=new CpuFrame(buffer); frame->AddRef(); buffer->Release();
        { RecordingTiming::Scope time{m_HandoffTime}; m_Output->OnCapture(frame); }
        frame->Release(); m_OutputFifo.Pop();
    }
    ICaptureOutput *m_Output;
    bool m_Sampling, m_Registered=false,m_Failed=false,m_HavePrevious=false;
    UINT m_W=0,m_H=0,m_Active=0;
    GpuCaptureFifo<RingSize> m_InputFifo,m_OutputFifo;
    ComPtr<IDirect3DDevice9Ex> m_D9;
    ComPtr<ID3D11Device> m_D11;
    ComPtr<ID3D11DeviceContext> m_Context;
    std::array<InputSlot,RingSize> m_Inputs;
    std::array<OutputSlot,RingSize> m_Outputs;
    std::array<Texture,2> m_Accum;
    Texture m_Previous,m_Packed;
    ComPtr<ID3D11ComputeShader> m_AccShader,m_OutShader;
    ComPtr<ID3D11Buffer> m_Params;
    RecordingTiming::Counter m_InitTime,m_SubmitTime,m_Wait9Time,m_Wait11Time,m_MapTime,m_HandoffTime;
};
}
GpuScreenCapture *CreateGpuScreenCapture(const GpuCaptureSettings &settings,ICaptureOutput *output) {
    std::unique_ptr<CaptureImpl> p(new CaptureImpl(settings,output));
    if(!p->Init()) return nullptr;
    return p.release();
}
