#pragma once

#include "RefCountedThreadSafe.h"
#include "GrowingBufferPoolThreadSafe.h"
#include "ImageBufferThreadSafe.h"
#include "AfxOutStreams.h"
#include <cmath>

namespace advancedfx {

class COutVideoStreamCreator;
// Only a leaf output, or one sampler directly wrapping a leaf, can bypass
// CPU sampling. Interleaved/nested/custom creators keep the old path.
struct GpuCaptureSettings {
    COutVideoStreamCreator *Output=nullptr;
    bool Sampling=false;
    double InputDuration=1, OutputDuration=1, Exposure=1;
    float Strength=1;
    EasySamplerSettings::Method Method=EasySamplerSettings::ESM_Rectangle;
};

class COutVideoStreamCreator
	: public CRefCountedThreadSafe
{
public:
	virtual TIOutVideoStream<true>* CreateOutVideoStream(const CImageFormat& imageFormat) = 0;
	virtual bool GetGpuCaptureSettings(GpuCaptureSettings &) { return false; }
};

class CClassicRecordingSettingsCreator
	: public COutVideoStreamCreator
{
public:
	CClassicRecordingSettingsCreator(const std::wstring & capturePath, bool bIfZip, bool bFormatBmpAndNotga)
	: m_CapturePath(capturePath)
	, m_bIfZip(bIfZip)
	, m_bFormatBmpAndNotga(bFormatBmpAndNotga) {

	}

	bool GetGpuCaptureSettings(GpuCaptureSettings &out) override { out = {}; out.Output=this; return true; }

	virtual TIOutVideoStream<true>* CreateOutVideoStream(const CImageFormat& imageFormat) override {
		auto result = new COutImageStream<true>(imageFormat, m_CapturePath, m_bIfZip, m_bFormatBmpAndNotga);
		result->AddRef();
		return result;
	}

private:
	std::wstring m_CapturePath;
	bool m_bIfZip;
	bool m_bFormatBmpAndNotga;
};


class CFfmpegRecordingSettingsCreator
	: public COutVideoStreamCreator
{
public:
	CFfmpegRecordingSettingsCreator(const std::wstring& capturePath, const std::wstring& ffmpegOptions, float frameRate, const std::wstring& noAlphaOptions = L"")
		: m_CapturePath(capturePath)
		, m_FfmpegOptions(ffmpegOptions)
        , m_NoAlphaOptions(noAlphaOptions)
		, m_FrameRate(frameRate) {

	}

	bool GetGpuCaptureSettings(GpuCaptureSettings &out) override { out = {}; out.Output=this; return true; }

	virtual TIOutVideoStream<true>* CreateOutVideoStream(const CImageFormat& imageFormat) override {
		auto result = new COutFFMPEGVideoStream<true>(imageFormat, m_CapturePath, imageFormat.Format!=ImageFormat::BGRA && !m_NoAlphaOptions.empty()?m_NoAlphaOptions:m_FfmpegOptions, m_FrameRate);
		result->AddRef();
		return result;
	}

private:
	std::wstring m_CapturePath;
	std::wstring m_FfmpegOptions;
 std::wstring m_NoAlphaOptions;
	float m_FrameRate;
};

class CSamplingRecordingSettingsCreator
	: public COutVideoStreamCreator
{
public:
	CSamplingRecordingSettingsCreator(class COutVideoStreamCreator * outVideoStreamCreator, float frameRate, EasySamplerSettings::Method method, double frameDuration, double exposure, float frameStrength, CGrowingBufferPoolThreadSafe * pImageBufferPool)
		: m_OutVideoStreamCreator(outVideoStreamCreator)
		, m_FrameRate(frameRate)
		, m_Method(method)
		, m_FrameDuration(frameDuration)
		, m_Exposure(exposure)
		, m_FrameStrength(frameStrength)
        , m_pImageBufferPool(pImageBufferPool)
	{
		outVideoStreamCreator->AddRef();
	}

	bool GetGpuCaptureSettings(GpuCaptureSettings &out) override {
        GpuCaptureSettings leaf;
        if (!m_OutVideoStreamCreator->GetGpuCaptureSettings(leaf) || leaf.Sampling || !leaf.Output ||
            !std::isfinite(m_FrameRate) || m_FrameRate <= 0 || !std::isfinite(m_FrameDuration) || m_FrameDuration <= 0 ||
            !std::isfinite(m_Exposure) || m_Exposure < 0 || m_Exposure > 1 ||
            !std::isfinite(m_FrameStrength) || m_FrameStrength < 0 || m_FrameStrength > 1) return false;
        // Extremely high output/input ratios can generate huge bursts of frames.
        if (1.0 / m_FrameRate > m_FrameDuration) return false;
        out=leaf; out.Sampling=true; out.InputDuration=1.0/m_FrameRate;
        out.OutputDuration=m_FrameDuration; out.Exposure=m_Exposure;
        out.Strength=m_FrameStrength; out.Method=m_Method;
        return true;
    }

	virtual TIOutVideoStream<true>* CreateOutVideoStream(const CImageFormat& imageFormat) override {
		auto outVideoStream = m_OutVideoStreamCreator->CreateOutVideoStream(imageFormat);
		auto result = new COutSamplingStream<true>(imageFormat, outVideoStream, m_FrameRate, m_Method, m_FrameDuration, m_Exposure, m_FrameStrength, m_pImageBufferPool);
		result->AddRef();
		if(outVideoStream) outVideoStream->Release();
		return result;		
	}

protected:
	~CSamplingRecordingSettingsCreator() {
		m_OutVideoStreamCreator->Release();
	}

private:
	class COutVideoStreamCreator* m_OutVideoStreamCreator;
	float m_FrameRate;
	EasySamplerSettings::Method m_Method;
	double m_FrameDuration;
	double m_Exposure;
	float m_FrameStrength;
    CGrowingBufferPoolThreadSafe * m_pImageBufferPool;
};

} // namespace advancedfx
