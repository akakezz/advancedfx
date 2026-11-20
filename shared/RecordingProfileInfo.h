#pragma once
#include <string>

namespace advancedfx
{
// A value snapshot of the live CRecordingSettings registry; no duplicated UI state.
struct RecordingProfileInfo
{
	enum class Kind { Other, Sampler, Ffmpeg, FfmpegEx };
	std::string Name;
	bool Protected = true;
	Kind Type = Kind::Other;
	std::string OutputSettings;
	float OutputFps = 30.0f;
	int Method = 1; // 0 = rectangle, 1 = trapezoid.
	double Exposure = 1.0;
	float Strength = 1.0f;
	std::string FfmpegOptions;
};
} // namespace advancedfx
