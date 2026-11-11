#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

namespace advancedfx
{
// Read-only scene history. Times are explicit demo seconds and corresponding
// client seconds; consumers must not substitute the current playback clock.
class SceneCache
{
public:
	struct Entity
	{
		std::uint64_t Identity = 0; // Caller-supplied stable identity, never just a recycled entity index.
		float Origin[3]{}, Angles[3]{}, Mins[3]{}, Maxs[3]{};
	};
	struct Frame
	{
		double DemoTime = 0, ClientTime = 0;
		std::vector<Entity> Entities;
	};
	explicit SceneCache(size_t limit = 64 * 1024 * 1024) : m_Limit(limit) {}
	void Clear() { m_Frames.clear(); m_Bytes = 0; }
	void SetLimit(size_t bytes) { m_Limit = bytes; Trim(); }
	size_t Bytes() const { return m_Bytes; }
	size_t Count() const { return m_Frames.size(); }
	const std::deque<Frame> &Frames() const { return m_Frames; }
	bool Append(Frame frame)
	{
		if (!std::isfinite(frame.DemoTime) || !std::isfinite(frame.ClientTime)) return false;
		for (const auto &entity : frame.Entities)
		{
			if (!entity.Identity) return false;
			for (int i = 0; i < 3; ++i)
				if (!std::isfinite(entity.Origin[i]) || !std::isfinite(entity.Angles[i]) ||
					!std::isfinite(entity.Mins[i]) || !std::isfinite(entity.Maxs[i]) ||
					entity.Mins[i] > entity.Maxs[i]) return false;
		}
		std::sort(frame.Entities.begin(), frame.Entities.end(), [](const Entity &a, const Entity &b) {
			return a.Identity < b.Identity;
		});
		for (size_t i = 1; i < frame.Entities.size(); ++i)
			if (frame.Entities[i-1].Identity == frame.Entities[i].Identity) return false;
		// A backward seek starts a new observed interval. Never join two takes.
		if (!m_Frames.empty() && frame.DemoTime < m_Frames.back().DemoTime) Clear();
		if (!m_Frames.empty() && frame.DemoTime == m_Frames.back().DemoTime) return false;
		const size_t bytes = Cost(frame);
		if (bytes > m_Limit) return false;
		m_Bytes += bytes;
		m_Frames.push_back(std::move(frame));
		Trim();
		return true;
	}
	bool Sample(double time, Frame &out, double maxGap = .1, double maxTravel = 256) const
	{
		out = {};
		if (!std::isfinite(time) || !std::isfinite(maxGap) || maxGap <= 0 ||
			!std::isfinite(maxTravel) || maxTravel < 0 || m_Frames.empty() ||
			time < m_Frames.front().DemoTime || time > m_Frames.back().DemoTime) return false;
		auto right = std::lower_bound(m_Frames.begin(), m_Frames.end(), time,
			[](const Frame &frame, double value) { return frame.DemoTime < value; });
		if (right->DemoTime == time) { out = *right; return true; }
		const auto &left = *std::prev(right);
		const double gap = right->DemoTime - left.DemoTime;
		if (gap > maxGap) return false;
		const double f = (time - left.DemoTime) / gap;
		out.DemoTime = time;
		out.ClientTime = left.ClientTime + f * (right->ClientTime - left.ClientTime);
		for (const auto &a : left.Entities)
		{
			auto b = std::lower_bound(right->Entities.begin(), right->Entities.end(), a.Identity,
				[](const Entity &entity, std::uint64_t identity) { return entity.Identity < identity; });
			if (b == right->Entities.end() || b->Identity != a.Identity) continue;
			double distance = 0;
			for (int i = 0; i < 3; ++i) distance += double(b->Origin[i]-a.Origin[i]) * (b->Origin[i]-a.Origin[i]);
			if (distance > maxTravel * maxTravel) continue;
			Entity entity = a;
			for (int i = 0; i < 3; ++i)
			{
				entity.Origin[i] += float(f * (b->Origin[i] - a.Origin[i]));
				entity.Angles[i] += float(f * std::remainder(double(b->Angles[i])-a.Angles[i], 360.0));
				entity.Mins[i] += float(f * (b->Mins[i] - a.Mins[i]));
				entity.Maxs[i] += float(f * (b->Maxs[i] - a.Maxs[i]));
			}
			out.Entities.push_back(entity);
		}
		return true;
	}
private:
	static size_t Cost(const Frame &frame) { return sizeof(Frame) + frame.Entities.capacity() * sizeof(Entity); }
	void Trim()
	{
		while (!m_Frames.empty() && m_Bytes > m_Limit)
		{
			m_Bytes -= Cost(m_Frames.front());
			m_Frames.pop_front();
		}
	}
	std::deque<Frame> m_Frames;
	size_t m_Limit, m_Bytes = 0; // Payload allocation estimate; excludes deque allocator bookkeeping.
};
}
