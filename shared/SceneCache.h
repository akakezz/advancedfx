#pragma once
#include "AfxMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
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
	static constexpr int CategoryCount = 9; // Existing categories 0..6, buildings 7, props 8.
	static constexpr size_t MaxBones = 128; // TF2 / Source SDK 2013 studio limit.
	using Matrix = std::array<float, 12>;
	struct Model
	{
		std::string Name;
		std::uint32_t Checksum = 0;
		std::vector<std::array<float, 3>> WireVertices; // BSP submodel vertices, shared across frames.
		std::vector<std::array<unsigned int, 2>> WireEdges;
		std::vector<int> Parents, Flags;
		std::vector<int> MergeBones;        // Bone-name sort order for Source's merge-enabled bones.
		std::vector<std::string> BoneNames; // Bone-merged gear uses the same names as its player.
	};

	// Public Source BSP format (SDK 2013): read only the selected submodel's
	// faces. No BSP pointers or engine mesh buffers survive this operation.
	static std::shared_ptr<const Model> ReadBrushModel(
	    const std::string &name, int index, size_t fileSize,
	    const std::function<bool(size_t, void *, size_t)> &read)
	{
		if (index <= 0 || index > 4096 || fileSize < 1036)
			return {};
		std::array<std::int32_t, 259> header{};
		if (!read(0, header.data(), sizeof(header)) || header[0] != 0x50534256 || header[1] < 19 ||
		    header[1] > 21)
			return {};
		struct Lump
		{
			size_t Offset, Count, Stride;
		};
		auto lump = [&](int id, size_t stride, Lump &out) {
			const auto *p = &header[2 + 4 * id];
			if (p[0] < 1036 || p[1] < 0 || p[3] != 0 || size_t(p[0]) > fileSize ||
			    size_t(p[1]) > fileSize - size_t(p[0]) || size_t(p[1]) % stride)
				return false;
			out = {size_t(p[0]), size_t(p[1]) / stride, stride};
			return true;
		};
		Lump models, faces, surfedges, edges, vertices;
		const int faceLump = header[2 + 4 * 58 + 1] > 0 ? 58 : 7;
		if (!lump(14, 48, models) || !lump(faceLump, 56, faces) || !lump(13, 4, surfedges) ||
		    !lump(12, 4, edges) || !lump(3, 12, vertices) || size_t(index) >= models.Count ||
		    vertices.Count > 65536 || header[2 + 4 * faceLump + 2] != 1)
			return {};
		auto row = [&](const Lump &l, size_t i, void *out, size_t size) {
			return i < l.Count && size <= l.Stride && read(l.Offset + i * l.Stride, out, size);
		};
		std::array<std::int32_t, 12> model{};
		if (!row(models, size_t(index), model.data(), 48) || model[10] < 0 || model[11] <= 0 ||
		    model[11] > 8192 || size_t(model[10]) > faces.Count ||
		    size_t(model[11]) > faces.Count - size_t(model[10]))
			return {};
		auto result = std::make_shared<Model>();
		result->Name = name;
		result->Checksum = std::uint32_t(header[258]);
		result->Parents = {-1};
		result->Flags = {0};
		std::map<unsigned short, unsigned int> vertexMap;
		std::set<std::array<unsigned int, 2>> uniqueEdges;
		size_t inspected = 0;
		for (int f = 0; f < model[11]; ++f)
		{
			std::array<unsigned char, 56> face{};
			if (!row(faces, size_t(model[10] + f), face.data(), face.size()))
				return {};
			std::int32_t first;
			std::int16_t count;
			std::memcpy(&first, face.data() + 4, 4);
			std::memcpy(&count, face.data() + 8, 2);
			if (first < 0 || count < 3 || count > 512 || size_t(first) > surfedges.Count ||
			    size_t(count) > surfedges.Count - size_t(first) || (inspected += count) > 262144)
				return {};
			std::vector<unsigned int> polygon;
			for (int i = 0; i < count; ++i)
			{
				std::int32_t directed;
				std::array<unsigned short, 2> edge;
				if (!row(surfedges, size_t(first + i), &directed, 4))
					return {};
				const auto edgeIndex = size_t(directed < 0 ? -std::int64_t(directed) : directed);
				if (!row(edges, edgeIndex, edge.data(), 4))
					return {};
				const auto vertexIndex = edge[directed < 0 ? 1 : 0];
				auto found = vertexMap.find(vertexIndex);
				if (found == vertexMap.end())
				{
					std::array<float, 3> vertex;
					if (!row(vertices, vertexIndex, vertex.data(), 12))
						return {};
					for (float v : vertex)
						if (!std::isfinite(v) || std::abs(v) > 1048576)
							return {};
					found = vertexMap.emplace(vertexIndex, unsigned(result->WireVertices.size())).first;
					result->WireVertices.push_back(vertex);
				}
				polygon.push_back(found->second);
			}
			// Fan triangulation matches the planar, convex faces emitted by VBSP.
			for (size_t i = 1; i + 1 < polygon.size(); ++i)
				for (auto pair : {std::array<unsigned int, 2>{polygon[0], polygon[i]},
				                  std::array<unsigned int, 2>{polygon[i], polygon[i + 1]},
				                  std::array<unsigned int, 2>{polygon[i + 1], polygon[0]}})
				{
					if (pair[1] < pair[0])
						std::swap(pair[0], pair[1]);
					if (pair[0] != pair[1])
						uniqueEdges.insert(pair);
				}
			if (uniqueEdges.size() > 196608)
				return {};
		}
		result->WireEdges.assign(uniqueEdges.begin(), uniqueEdges.end());
		return result->WireEdges.empty() ? std::shared_ptr<const Model>{} : result;
	}
	struct Transform
	{
		std::array<float, 3> Position{}, Scale{1, 1, 1};
		std::array<float, 4> Rotation{1, 0, 0, 0}; // HLAE quaternion order: W, X, Y, Z.
		Afx::Math::Quaternion Quaternion() const
		{
			return {Rotation[0], Rotation[1], Rotation[2], Rotation[3]};
		}
		Matrix Compose() const
		{
			double rotation[3][3];
			Quaternion().ToRotationMatrix(rotation);
			Matrix result;
			for (int r = 0; r < 3; ++r)
			{
				for (int c = 0; c < 3; ++c)
					result[4 * r + c] = float(rotation[r][c] * Scale[c]);
				result[4 * r + 3] = Position[r];
			}
			return result;
		}
		static bool Decompose(const Matrix &matrix, Transform &out)
		{
			double rotation[3][3];
			for (int c = 0; c < 3; ++c)
			{
				double length = 0;
				for (int r = 0; r < 3; ++r)
					length += double(matrix[4 * r + c]) * matrix[4 * r + c];
				length = std::sqrt(length);
				if (!std::isfinite(length) || length < 1e-6)
					return false;
				out.Scale[c] = float(length);
				if (!std::isfinite(out.Scale[c]))
					return false;
				out.Position[c] = matrix[4 * c + 3];
				if (!std::isfinite(out.Position[c]))
					return false;
				for (int r = 0; r < 3; ++r)
					rotation[r][c] = matrix[4 * r + c] / length;
			}
			// TRS cannot represent shear. Preserve such bone transforms verbatim instead.
			for (int a = 0; a < 3; ++a)
				for (int b = a + 1; b < 3; ++b)
				{
					double dot = 0;
					for (int r = 0; r < 3; ++r)
						dot += rotation[r][a] * rotation[r][b];
					if (std::abs(dot) > 1e-4)
						return false;
				}
			const double determinant =
			    rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1]) -
			    rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0]) +
			    rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0]);
			if (determinant < 0)
			{
				out.Scale[0] = -out.Scale[0];
				for (int r = 0; r < 3; ++r)
					rotation[r][0] = -rotation[r][0];
			}
			const auto q = Afx::Math::Quaternion::FromRotationMatrix(rotation);
			out.Rotation = {float(q.W), float(q.X), float(q.Y), float(q.Z)};
			return true;
		}
	};
	enum class BoneFallback
	{
		UnsupportedTransform,
		MissingParent,
		SingularParent
	};
	struct FixedBone
	{
		int Index = 0;
		bool World = false;
		BoneFallback Reason = BoneFallback::UnsupportedTransform;
		Matrix Value{};
	};
	static const char *FallbackName(BoneFallback reason)
	{
		switch (reason)
		{
		case BoneFallback::MissingParent:
			return "parent bone unavailable";
		case BoneFallback::SingularParent:
			return "singular or unstable parent transform";
		default:
			return "local transform cannot be represented as rotation/translation/scale";
		}
	}
	struct Pose
	{
		std::shared_ptr<const Model> ModelInfo;
		Matrix Root{};
		std::vector<Matrix> Bones;    // Optional complete world-matrix pose (nearest-only).
		std::vector<Transform> Local; // Parent-relative transforms; root bones relative to Root.
		std::vector<FixedBone> Fixed; // Sparse exceptions, sorted by bone index; preserved per bone.
		float BlendFraction = -1;     // Negative means an original/nearest pose.
		unsigned short BlendedBones = 0, HeldBones = 0;
		int Body = 0;
	};
	struct Entity
	{
		std::uint64_t Identity = 0; // Caller-supplied stable identity, never just a recycled entity index.
		std::uint64_t OwnerIdentity =
		    0;            // Stable player/corpse identity for attached gear; 0 for loose items.
		int Category = 0; // MirvTf2EntityCategory_t: player=0, projectile=1, weapon=2, cosmetic=3, other=4,
		                  // ragdoll=5, gib=6, building=7, prop=8.
		float Origin[3]{}, Angles[3]{}, Mins[3]{}, Maxs[3]{};
		std::shared_ptr<const Pose> ModelPose;
	};
	static std::array<std::array<double, 3>, 8> BoundsCorners(const Entity &entity)
	{
		double rotation[3][3];
		const auto angles = Afx::Math::QREulerAngles::FromQEulerAngles(
		    {entity.Angles[0], entity.Angles[1], entity.Angles[2]});
		Afx::Math::Quaternion::FromQREulerAngles(angles).ToRotationMatrix(rotation);
		std::array<std::array<double, 3>, 8> corners;
		for (int i = 0; i < 8; ++i)
			for (int r = 0; r < 3; ++r)
			{
				corners[i][r] = entity.Origin[r];
				for (int c = 0; c < 3; ++c)
					corners[i][r] += rotation[r][c] * ((i & (1 << c)) ? entity.Maxs[c] : entity.Mins[c]);
			}
		return corners;
	}
	static constexpr std::array<unsigned char, 3> CategoryColor(int category)
	{
		return category == 1   ? std::array<unsigned char, 3>{255, 80, 70}
		       : category == 2 ? std::array<unsigned char, 3>{70, 235, 140}
		       : category == 3 ? std::array<unsigned char, 3>{210, 120, 255}
		       : category == 4 ? std::array<unsigned char, 3>{180, 190, 200}
		       : category == 5 ? std::array<unsigned char, 3>{180, 190, 200}
		       : category == 6 ? std::array<unsigned char, 3>{255, 145, 65}
		       : category == 7 ? std::array<unsigned char, 3>{255, 210, 70}
		       : category == 8 ? std::array<unsigned char, 3>{120, 155, 255}
		                       : std::array<unsigned char, 3>{80, 220, 255};
	}
	struct Frame
	{
		double DemoTime = 0, ClientTime = 0;
		std::vector<Entity> Entities;
	};

	struct Line
	{
		std::array<double, 3> A, B;
		int Category;
		unsigned char Alpha = 255;
	};
	struct DrawingOptions
	{
		bool Bounds = false, Meshes = true, Trails = false;
		std::array<bool, CategoryCount> TrailCategories{true, true, true, true, true, true, true, true, true};
		double TrailSeconds = 2;
		bool operator==(const DrawingOptions &b) const
		{
			return Bounds == b.Bounds && Meshes == b.Meshes && Trails == b.Trails &&
			       TrailCategories == b.TrailCategories && TrailSeconds == b.TrailSeconds;
		}
	};
	std::vector<Line> Drawing(const Frame &frame, const DrawingOptions &options) const
	{
		constexpr size_t maxLines = 65536, maxTrails = 16384;
		std::vector<Line> lines;
		for (const auto &entity : frame.Entities)
		{
			if (options.Meshes && entity.ModelPose && !entity.ModelPose->ModelInfo->WireEdges.empty())
			{
				const auto &model = *entity.ModelPose->ModelInfo;
				double rotation[3][3];
				Afx::Math::Quaternion::FromQREulerAngles(
				    Afx::Math::QREulerAngles::FromQEulerAngles(
				        {entity.Angles[0], entity.Angles[1], entity.Angles[2]}))
				    .ToRotationMatrix(rotation);
				auto point = [&](unsigned int index) {
					std::array<double, 3> p;
					for (int r = 0; r < 3; ++r)
					{
						p[r] = entity.Origin[r];
						for (int c = 0; c < 3; ++c)
							p[r] += rotation[r][c] * model.WireVertices[index][c];
					}
					return p;
				};
				for (const auto &edge : model.WireEdges)
				{
					if (lines.size() >= maxLines)
						break;
					lines.push_back({point(edge[0]), point(edge[1]), entity.Category});
				}
			}
			if (options.Bounds && lines.size() + 12 <= maxLines)
			{
				const auto corners = BoundsCorners(entity);
				for (int i = 0; i < 8; ++i)
					for (int axis : {1, 2, 4})
						if (!(i & axis))
							lines.push_back({corners[i], corners[i | axis], entity.Category, 160});
			}
		}
		if (!options.Trails || !std::isfinite(options.TrailSeconds) || options.TrailSeconds < .05 ||
		    options.TrailSeconds > 30 || m_Frames.empty())
			return lines;
		const double start = frame.DemoTime - options.TrailSeconds;
		auto it = std::lower_bound(m_Frames.begin(), m_Frames.end(), start,
		                           [](const Frame &a, double t) { return a.DemoTime < t; });
		if (it != m_Frames.begin())
			--it;
		size_t segments = 0, inspected = 0;
		auto connect = [&](const Frame &a, const Frame &b) {
			const double gap = b.DemoTime - a.DemoTime;
			if (gap <= 0 || gap > .1 || b.DemoTime < start)
				return;
			for (const auto &entity : a.Entities)
			{
				if (++inspected > 250000 || segments >= maxTrails || lines.size() >= maxLines)
					return;
				if (!options.TrailCategories[entity.Category])
					continue;
				auto next =
				    std::lower_bound(b.Entities.begin(), b.Entities.end(), entity.Identity,
					                 [](const Entity &e, std::uint64_t id) { return e.Identity < id; });
				if (next == b.Entities.end() || next->Identity != entity.Identity ||
				    next->Category != entity.Category || next->OwnerIdentity != entity.OwnerIdentity)
					continue;
				double distance = 0;
				Line line{{}, {}, entity.Category};
				for (int c = 0; c < 3; ++c)
				{
					line.A[c] = entity.Origin[c];
					line.B[c] = next->Origin[c];
					distance += (line.B[c] - line.A[c]) * (line.B[c] - line.A[c]);
				}
				if (distance < .0001 || distance > 256 * 256)
					continue;
				if (a.DemoTime < start)
					for (int c = 0; c < 3; ++c)
						line.A[c] += (start - a.DemoTime) / gap * (line.B[c] - line.A[c]);
				line.Alpha = static_cast<unsigned char>(
				    40 + 200 * std::clamp((b.DemoTime - start) / options.TrailSeconds, 0.0, 1.0));
				lines.push_back(line);
				++segments;
			}
		};
		for (; it != m_Frames.end() && it->DemoTime < frame.DemoTime && segments < maxTrails &&
		       inspected <= 250000 && lines.size() < maxLines;
		     ++it)
		{
			auto next = std::next(it);
			connect(*it, next != m_Frames.end() && next->DemoTime <= frame.DemoTime ? *next : frame);
		}
		return lines;
	}

	explicit SceneCache(size_t limit = 64 * 1024 * 1024) : m_Limit(limit)
	{
	}
	std::uint64_t Revision() const
	{
		return m_Revision;
	}
	void Clear()
	{
		++m_Revision;
		m_Frames.clear();
		m_Models.clear();
		m_Poses.clear();
		m_ReusedPoses = m_AllocatedBytes = 0;
		m_Bytes = 0;
	}
	void SetLimit(size_t bytes)
	{
		++m_Revision;
		m_Limit = bytes;
		Trim();
	}
	size_t PoseCount() const
	{
		return m_Poses.size();
	}
	size_t ReusedPoses() const
	{
		return m_ReusedPoses;
	}
	size_t AllocatedBytes() const
	{
		return m_AllocatedBytes;
	}
	size_t Limit() const
	{
		return m_Limit;
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
			if (!entity.Identity || entity.Category < 0 || entity.Category >= CategoryCount ||
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
		// Share only exactly equal recorded poses from the same stable entity/owner.
		// No quantization: small animation changes must remain observable.
		size_t reused = 0;
		if (!m_Frames.empty())
			for (auto &entity : frame.Entities)
			{
				const auto &last = m_Frames.back().Entities;
				auto it = std::lower_bound(last.begin(), last.end(), entity.Identity,
				                           [](const Entity &a, std::uint64_t id) { return a.Identity < id; });
				if (it != last.end() && it->Identity == entity.Identity && it->Category == entity.Category &&
				    it->OwnerIdentity == entity.OwnerIdentity && entity.ModelPose && it->ModelPose &&
				    SamePose(*entity.ModelPose, *it->ModelPose))
				{
					entity.ModelPose = it->ModelPose;
					++reused;
				}
			}
		const size_t bytes = Cost(frame);
		const auto models = Models(frame);
		const auto poses = Poses(frame);
		size_t whole = bytes;
		for (const auto *model : models)
			whole += ModelCost(*model);
		for (const auto *pose : poses)
			whole += PoseCost(*pose);
		if (whole > m_Limit)
			return false; // A single complete frame must fit.
		const size_t before = m_Bytes;
		for (const auto *pose : poses)
		{
			auto result = m_Poses.emplace(pose, ModelUse{0, PoseCost(*pose)});
			if (result.second)
				m_Bytes += result.first->second.Bytes;
			++result.first->second.Frames;
		}
		for (const auto *model : models)
		{
			auto result = m_Models.emplace(model, ModelUse{0, ModelCost(*model)});
			if (result.second)
				m_Bytes += result.first->second.Bytes;
			++result.first->second.Frames;
		}
		m_Bytes += bytes;
		m_AllocatedBytes += m_Bytes - before;
		m_ReusedPoses += reused;
		++m_Revision;
		m_Frames.push_back(std::move(frame));
		Trim();
		return true;
	}
	bool Sample(double time, Frame &out, double maxGap = .1, double maxTravel = 256,
	            bool smoothAnimation = true) const
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
			entity.ModelPose = BlendPose(a.ModelPose, b->ModelPose, f, smoothAnimation);
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
			if (entity.Category == 0 || entity.Category == 5 || entity.Category == 7 || entity.Category == 8)
				owners.push_back(entity.Identity);
		const auto orphan = [&](const Entity &entity) {
			return entity.OwnerIdentity &&
			       !std::binary_search(owners.begin(), owners.end(), entity.OwnerIdentity);
		};
		out.Entities.erase(std::remove_if(out.Entities.begin(), out.Entities.end(), orphan),
		                   out.Entities.end());
		return true;
	}
	static bool ValidPose(const Pose &pose)
	{
		if (!pose.ModelInfo || pose.Body < 0)
			return false;
		const auto &model = *pose.ModelInfo;
		const size_t count = model.Parents.size();
		if (model.Name.empty() || model.Name.size() >= 260 || !count || count > MaxBones ||
		    model.Flags.size() != count || (!model.BoneNames.empty() && model.BoneNames.size() != count) ||
		    !((pose.Bones.size() == count && pose.Local.empty()) ||
		      (pose.Local.size() == count && pose.Bones.empty())))
			return false;
		if (model.WireVertices.empty() != model.WireEdges.empty() || model.WireVertices.size() > 65536 ||
		    model.WireEdges.size() > 196608)
			return false;
		for (const auto &v : model.WireVertices)
			for (float a : v)
				if (!std::isfinite(a))
					return false;
		for (const auto &edge : model.WireEdges)
			for (auto i : edge)
				if (i >= model.WireVertices.size())
					return false;
		for (int i : model.MergeBones)
			if (i < 0 || size_t(i) >= count || model.BoneNames.size() != count || !(model.Flags[i] & 0x40000))
				return false;
		int previous = -1;
		for (const auto &fixed : pose.Fixed)
		{
			if (pose.Local.empty() || fixed.Index <= previous || fixed.Index >= int(count))
				return false;
			previous = fixed.Index;
			for (float value : fixed.Value)
				if (!std::isfinite(value))
					return false;
		}
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
			if (pose.Local.empty())
			{
				for (float value : pose.Bones[i])
					if (!std::isfinite(value))
						return false;
			}
			else
			{
				const auto &t = pose.Local[i];
				for (int a = 0; a < 3; ++a)
					if (!std::isfinite(t.Position[a]) || !std::isfinite(t.Scale[a]) ||
					    std::abs(t.Scale[a]) < 1e-6)
						return false;
				double norm = 0;
				for (float v : t.Rotation)
				{
					if (!std::isfinite(v))
						return false;
					norm += double(v) * v;
				}
				if (std::abs(norm - 1) > 1e-4)
					return false;
			}
		}
		return true;
	}

private:
	static std::shared_ptr<const Pose> BlendPose(const std::shared_ptr<const Pose> &a,
	                                             const std::shared_ptr<const Pose> &b, double f, bool smooth)
	{
		if (!a || !b || a->ModelInfo != b->ModelInfo)
			return nullptr;
		const auto &nearest = f < .5 ? a : b;
		if (!smooth || a == b || a->Local.empty() || b->Local.size() != a->Local.size())
			return nearest;
		auto out = std::make_shared<Pose>();
		out->ModelInfo = a->ModelInfo;
		out->Root = nearest->Root;
		out->Body = nearest->Body;
		out->BlendFraction = float(f);
		out->Local.resize(a->Local.size());
		size_t ai = 0, bi = 0;
		for (size_t i = 0; i < out->Local.size(); ++i)
		{
			const auto &x = a->Local[i], &y = b->Local[i];
			auto &t = out->Local[i];
			const auto *af = ai < a->Fixed.size() && a->Fixed[ai].Index == int(i) ? &a->Fixed[ai++] : nullptr;
			const auto *bf = bi < b->Fixed.size() && b->Fixed[bi].Index == int(i) ? &b->Fixed[bi++] : nullptr;
			bool reflected = false;
			for (int c = 0; c < 3; ++c)
				reflected |= std::signbit(x.Scale[c]) != std::signbit(y.Scale[c]);
			if (af || bf || reflected)
			{
				t = f < .5 ? x : y;
				if (a->ModelInfo->Flags[i] & 0x7ff00)
					++out->HeldBones;
				if (const auto *fixed = f < .5 ? af : bf)
					out->Fixed.push_back(*fixed);
				continue;
			}
			if (a->ModelInfo->Flags[i] & 0x7ff00)
				++out->BlendedBones;
			for (int c = 0; c < 3; ++c)
			{
				t.Position[c] = float(double(x.Position[c]) + f * (double(y.Position[c]) - x.Position[c]));
				t.Scale[c] = float(double(x.Scale[c]) + f * (double(y.Scale[c]) - x.Scale[c]));
			}
			auto qy = y.Quaternion();
			const auto qx = x.Quaternion();
			if (Afx::Math::DotProduct(qx, qy) < 0)
				qy = -1.0 * qy;
			// Source's QuaternionBlend aligns signs, linearly blends, then
			// normalizes when interpolating animation frames (bone_setup.cpp).
			const auto q = ((1 - f) * qx + f * qy).Normalized();
			t.Rotation = {float(q.W), float(q.X), float(q.Y), float(q.Z)};
		}
		return out;
	}
	static size_t ModelCost(const Model &model)
	{
		size_t bytes =
		    sizeof(Model) + model.Name.capacity() + 1 +
		    model.WireVertices.capacity() * sizeof(std::array<float, 3>) +
		    model.WireEdges.capacity() * sizeof(std::array<unsigned int, 2>) +
		    (model.Parents.capacity() + model.Flags.capacity() + model.MergeBones.capacity()) * sizeof(int) +
		    model.BoneNames.capacity() * sizeof(std::string);
		for (const auto &name : model.BoneNames)
			bytes += name.capacity() + 1;
		return bytes;
	}
	static std::vector<const Model *> Models(const Frame &frame)
	{
		std::vector<const Model *> result;
		for (const auto &entity : frame.Entities)
			if (entity.ModelPose)
				result.push_back(entity.ModelPose->ModelInfo.get());
		std::sort(result.begin(), result.end(), std::less<const Model *>());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}
	static bool SamePose(const Pose &a, const Pose &b)
	{
		if (a.ModelInfo != b.ModelInfo || a.Root != b.Root || a.Body != b.Body || a.Bones != b.Bones ||
		    a.Local.size() != b.Local.size() || a.Fixed.size() != b.Fixed.size())
			return false;
		for (size_t i = 0; i < a.Local.size(); ++i)
			if (a.Local[i].Position != b.Local[i].Position || a.Local[i].Rotation != b.Local[i].Rotation ||
			    a.Local[i].Scale != b.Local[i].Scale)
				return false;
		for (size_t i = 0; i < a.Fixed.size(); ++i)
			if (a.Fixed[i].Index != b.Fixed[i].Index || a.Fixed[i].World != b.Fixed[i].World ||
			    a.Fixed[i].Reason != b.Fixed[i].Reason || a.Fixed[i].Value != b.Fixed[i].Value)
				return false;
		return true;
	}
	static std::vector<const Pose *> Poses(const Frame &frame)
	{
		std::vector<const Pose *> result;
		for (const auto &entity : frame.Entities)
			if (entity.ModelPose)
				result.push_back(entity.ModelPose.get());
		std::sort(result.begin(), result.end(), std::less<const Pose *>());
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}
	static size_t PoseCost(const Pose &pose)
	{
		return sizeof(Pose) + pose.Bones.capacity() * sizeof(Matrix) +
		       pose.Local.capacity() * sizeof(Transform) + pose.Fixed.capacity() * sizeof(FixedBone);
	}
	static size_t Cost(const Frame &frame)
	{
		return sizeof(Frame) + frame.Entities.capacity() * sizeof(Entity);
	}
	void Trim()
	{
		while (!m_Frames.empty() && m_Bytes > m_Limit)
		{
			m_Bytes -= Cost(m_Frames.front());
			for (const auto *pose : Poses(m_Frames.front()))
			{
				auto it = m_Poses.find(pose);
				if (--it->second.Frames == 0)
				{
					m_Bytes -= it->second.Bytes;
					m_Poses.erase(it);
				}
			}
			for (const auto *model : Models(m_Frames.front()))
			{
				auto it = m_Models.find(model);
				if (--it->second.Frames == 0)
				{
					m_Bytes -= it->second.Bytes;
					m_Models.erase(it);
				}
			}
			m_Frames.pop_front();
		}
	}
	struct ModelUse
	{
		size_t Frames, Bytes;
	};
	std::map<const Model *, ModelUse> m_Models;
	std::map<const Pose *, ModelUse> m_Poses;
	size_t m_ReusedPoses = 0, m_AllocatedBytes = 0;
	std::deque<Frame> m_Frames;
	std::uint64_t m_Revision = 0;
	size_t m_Limit, m_Bytes = 0; // Payload allocation estimate; excludes deque allocator bookkeeping.
};
} // namespace advancedfx
