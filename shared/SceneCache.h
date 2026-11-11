#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace advancedfx
{
// Read-only scene history. Times are explicit demo seconds and corresponding
// client seconds; consumers must not substitute the current playback clock.
class SceneCache
{
public:
	static constexpr size_t MaxBones = 128; // TF2 / Source SDK 2013 studio limit.
	using Matrix = std::array<float, 12>;
	struct Model
	{
		std::string Name;
		std::uint32_t Checksum = 0;
		std::vector<int> Parents, Flags;
	};
	struct Pose
	{
		std::shared_ptr<const Model> ModelInfo;
		Matrix Root{};
		std::vector<Matrix> Bones; // Observed world matrices, including model/bone scale.
		int Body = 0;
	};
	struct Entity
	{
		std::uint64_t Identity = 0; // Caller-supplied stable identity, never just a recycled entity index.
		std::uint64_t OwnerIdentity = 0; // Stable player identity for attached gear; zero for independent entities.
		int Category = 0; // MirvTf2EntityCategory_t: player=0, projectile=1, weapon=2, cosmetic=3, other=4.
		float Origin[3]{}, Angles[3]{}, Mins[3]{}, Maxs[3]{};
		std::shared_ptr<const Pose> ModelPose;
	};
	static constexpr std::array<unsigned char, 3> CategoryColor(int category)
	{
		return category == 1   ? std::array<unsigned char, 3>{255, 80, 70}
		       : category == 2 ? std::array<unsigned char, 3>{255, 210, 70}
		       : category == 3 ? std::array<unsigned char, 3>{210, 120, 255}
		       : category == 4 ? std::array<unsigned char, 3>{180, 190, 200}
		                       : std::array<unsigned char, 3>{80, 220, 255};
	}
	struct Frame
	{
		double DemoTime = 0, ClientTime = 0;
		std::vector<Entity> Entities;
	};
	explicit SceneCache(size_t limit = 64 * 1024 * 1024) : m_Limit(limit)
	{
	}
	void Clear()
	{
		m_Frames.clear();
		m_Bytes = 0;
	}
	void SetLimit(size_t bytes)
	{
		m_Limit = bytes;
		Trim();
	}
	size_t Bytes() const
	{
		return m_Bytes;
	}
	size_t Count() const
	{
		return m_Frames.size();
	}
	const std::deque<Frame> &Frames() const
	{
		return m_Frames;
	}
	bool Append(Frame frame)
	{
		if (!std::isfinite(frame.DemoTime) || !std::isfinite(frame.ClientTime))
			return false;
		for (const auto &entity : frame.Entities)
		{
			if (!entity.Identity || entity.Category < 0 || entity.Category > 4 ||
			    (entity.ModelPose && !ValidPose(*entity.ModelPose)))
				return false;
			for (int i = 0; i < 3; ++i)
				if (!std::isfinite(entity.Origin[i]) || !std::isfinite(entity.Angles[i]) ||
				    !std::isfinite(entity.Mins[i]) || !std::isfinite(entity.Maxs[i]) ||
				    entity.Mins[i] > entity.Maxs[i])
					return false;
		}
		std::sort(frame.Entities.begin(), frame.Entities.end(),
		          [](const Entity &a, const Entity &b) { return a.Identity < b.Identity; });
		for (size_t i = 1; i < frame.Entities.size(); ++i)
			if (frame.Entities[i - 1].Identity == frame.Entities[i].Identity)
				return false;
		// A backward seek starts a new observed interval. Never join two takes.
		if (!m_Frames.empty() && frame.DemoTime < m_Frames.back().DemoTime)
			Clear();
		if (!m_Frames.empty() && frame.DemoTime == m_Frames.back().DemoTime)
			return false;
		const size_t bytes = Cost(frame);
		if (bytes > m_Limit)
			return false;
		m_Bytes += bytes;
		m_Frames.push_back(std::move(frame));
		Trim();
		return true;
	}
	bool Sample(double time, Frame &out, double maxGap = .1, double maxTravel = 256) const
	{
		out = {};
		if (!std::isfinite(time) || !std::isfinite(maxGap) || maxGap <= 0 || !std::isfinite(maxTravel) ||
		    maxTravel < 0 || m_Frames.empty() || time < m_Frames.front().DemoTime ||
		    time > m_Frames.back().DemoTime)
			return false;
		auto right =
		    std::lower_bound(m_Frames.begin(), m_Frames.end(), time,
			                 [](const Frame &frame, double value) { return frame.DemoTime < value; });
		if (right->DemoTime == time)
		{
			out = *right;
			return true;
		}
		const auto &left = *std::prev(right);
		const double gap = right->DemoTime - left.DemoTime;
		if (gap > maxGap)
			return false;
		const double f = (time - left.DemoTime) / gap;
		out.DemoTime = time;
		out.ClientTime = left.ClientTime + f * (right->ClientTime - left.ClientTime);
		for (const auto &a : left.Entities)
		{
			auto b = std::lower_bound(
			    right->Entities.begin(), right->Entities.end(), a.Identity,
			    [](const Entity &entity, std::uint64_t identity) { return entity.Identity < identity; });
			if (b == right->Entities.end() || b->Identity != a.Identity || b->Category != a.Category ||
			    b->OwnerIdentity != a.OwnerIdentity)
				continue;
			double distance = 0;
			for (int i = 0; i < 3; ++i)
				distance += double(b->Origin[i] - a.Origin[i]) * (b->Origin[i] - a.Origin[i]);
			if (distance > maxTravel * maxTravel)
				continue;
			Entity entity = a;
			// Prototype: retain a complete observed pose, never linearly blend
			// world bone matrices (which collapses rotations and stretches limbs).
			// The renderer moves this rigid pose with the interpolated root.
			entity.ModelPose =
			    a.ModelPose && b->ModelPose && a.ModelPose->ModelInfo == b->ModelPose->ModelInfo
			        ? (f < .5 ? a.ModelPose : b->ModelPose)
			        : nullptr;
			for (int i = 0; i < 3; ++i)
			{
				entity.Origin[i] += float(f * (b->Origin[i] - a.Origin[i]));
				entity.Angles[i] += float(f * std::remainder(double(b->Angles[i]) - a.Angles[i], 360.0));
				entity.Mins[i] += float(f * (b->Mins[i] - a.Mins[i]));
				entity.Maxs[i] += float(f * (b->Maxs[i] - a.Maxs[i]));
			}
			out.Entities.push_back(entity);
		}
		// If a player's snapshot cannot be bridged, do not leave its gear
		// floating at an independently interpolated position.
		std::vector<std::uint64_t> owners;
		for (const auto &entity : out.Entities)
			if (entity.Category == 0)
				owners.push_back(entity.Identity);
		const auto orphan = [&](const Entity &entity) {
			return entity.OwnerIdentity &&
			       !std::binary_search(owners.begin(), owners.end(), entity.OwnerIdentity);
		};
		out.Entities.erase(std::remove_if(out.Entities.begin(), out.Entities.end(), orphan), out.Entities.end());
		return true;
	}
	static bool ValidPose(const Pose &pose)
	{
		if (!pose.ModelInfo || pose.Body < 0)
			return false;
		const auto &model = *pose.ModelInfo;
		const size_t count = model.Parents.size();
		if (model.Name.empty() || model.Name.size() >= 260 || !count || count > MaxBones ||
		    model.Flags.size() != count || pose.Bones.size() != count)
			return false;
		for (float value : pose.Root)
			if (!std::isfinite(value))
				return false;
		const auto &r = pose.Root;
		const double determinant = double(r[0]) * (double(r[5]) * r[10] - double(r[6]) * r[9]) -
		                           double(r[1]) * (double(r[4]) * r[10] - double(r[6]) * r[8]) +
		                           double(r[2]) * (double(r[4]) * r[9] - double(r[5]) * r[8]);
		if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
			return false;
		for (size_t i = 0; i < count; ++i)
		{
			if (model.Parents[i] < -1 || model.Parents[i] >= int(i))
				return false;
			for (float value : pose.Bones[i])
				if (!std::isfinite(value))
					return false;
		}
		return true;
	}

private:
	static size_t Cost(const Frame &frame)
	{
		size_t result = sizeof(Frame) + frame.Entities.capacity() * sizeof(Entity);
		for (const auto &entity : frame.Entities)
			if (entity.ModelPose)
			{
				const auto &pose = *entity.ModelPose;
				const auto &model = *pose.ModelInfo;
				// Conservatively charge shared model metadata in every frame.
				result += sizeof(Pose) + pose.Bones.capacity() * sizeof(Matrix) + sizeof(Model) +
				          model.Name.capacity() + 1 +
				          (model.Parents.capacity() + model.Flags.capacity()) * sizeof(int);
			}
		return result;
	}
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
} // namespace advancedfx
