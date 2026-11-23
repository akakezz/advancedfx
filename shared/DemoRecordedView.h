#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <istream>
#include <stdexcept>
#include <vector>

namespace advancedfx {
struct RecordedViewSample {
	int tick;
	bool cut;
	std::array<float, 6> pose;
};

// Source protocol 3 dem_packet metadata. Payloads stay on disk.
inline std::vector<RecordedViewSample> ReadRecordedViews(std::istream &in)
{
	auto read = [&](void *dst, size_t bytes) {
		if (!in.read(static_cast<char *>(dst), bytes))
			throw std::runtime_error("Incomplete demo packet.");
	};
	auto integer = [&]() { int32_t value; read(&value, 4); return value; };
	char header[1072];
	read(header, sizeof(header));
	if (std::memcmp(header, "HL2DEMO\0", 8) || std::memcmp(header + 8, "\3\0\0\0", 4))
		throw std::runtime_error("Expected a Source protocol-3 demo.");
	std::vector<RecordedViewSample> samples;
	for (;;) {
		uint8_t cmd;
		read(&cmd, 1);
		if (cmd == 7) break;
		int tick = integer();
		if (cmd == 3) continue;
		if (cmd == 1 || cmd == 2) {
			int flags = integer();
			float views[18];
			read(views, sizeof(views));
			in.seekg(8, std::ios::cur); // packet sequences
			if (cmd == 2 && tick >= 0) {
				RecordedViewSample sample{tick, (flags & 4) != 0, {}};
				std::copy_n(views + ((flags & 1) ? 9 : 0), 3, sample.pose.begin());
				std::copy_n(views + ((flags & 2) ? 12 : 3), 3, sample.pose.begin()+3);
				for (float value : sample.pose)
					if (!std::isfinite(value)) throw std::runtime_error("Invalid recorded pose.");
				if (!samples.empty() && tick < samples.back().tick)
					throw std::runtime_error("Non-monotonic packet ticks.");
				if (!samples.empty() && tick == samples.back().tick) {
					sample.cut |= samples.back().cut;
					samples.back() = sample;
				} else samples.push_back(sample);
			}
		} else if (cmd == 5) in.seekg(4, std::ios::cur);
		else if (cmd != 4 && cmd != 6 && cmd != 8)
			throw std::runtime_error("Unknown demo command.");
		int length = integer();
		if (length < 0 || !in.seekg(length, std::ios::cur))
			throw std::runtime_error("Invalid demo command length.");
	}
	if (samples.size() < 2) throw std::runtime_error("Insufficient recorded views.");
	return samples;
}

inline bool SampleRecordedView(const std::vector<RecordedViewSample> &samples, double tick,
                               std::array<float, 6> &result)
{
	if (samples.empty() || !std::isfinite(tick) || tick < samples.front().tick || tick > samples.back().tick)
		return false;
	auto upper = std::upper_bound(samples.begin(), samples.end(), tick,
	    [](double t, const RecordedViewSample &s) { return t < s.tick; });
	const auto &lower = *(upper - 1);
	result = lower.pose;
	if (upper == samples.end() || upper->cut) return true;
	double fraction = (tick - lower.tick) / (upper->tick - lower.tick);
	for (size_t i = 0; i < result.size(); ++i) {
		double delta = double(upper->pose[i]) - lower.pose[i];
		result[i] += float(fraction * (i < 3 ? delta : std::remainder(delta, 360.0)));
	}
	return true;
}
}
