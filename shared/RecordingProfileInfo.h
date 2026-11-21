#pragma once
#include <string>
#include <vector>

namespace advancedfx
{
// A value snapshot of the live CRecordingSettings registry; no duplicated UI state.
struct RecordingProfileInfo
{
	enum class Kind { Other, Sampler, Ffmpeg, FfmpegEx, Multi, Interleave, InterleaveInput };
	std::string Name;
	bool Protected = true;
	Kind Type = Kind::Other;
	std::string OutputSettings;
 std::vector<std::string> Outputs;
 int InputCount = 0, InputIndex = 0;
	float OutputFps = 30.0f;
	int Method = 1; // 0 = rectangle, 1 = trapezoid.
	double Exposure = 1.0;
	float Strength = 1.0f;
	std::string FfmpegOptions;
 std::string FfmpegOptionsNoAlpha; // Optional color-only variant for split-alpha profiles.
};
} // namespace advancedfx
