#pragma once
#include "AfxMath.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace advancedfx
{
// Common output, independent of Source interfaces, keyframes and UI state.
struct CameraPose
{
	double Position[3]{};
	Afx::Math::Quaternion Rotation{1, 0, 0, 0};
	double Fov = 90;
	bool Valid() const
	{
		return std::isfinite(Position[0]) && std::isfinite(Position[1]) && std::isfinite(Position[2]) &&
			   std::abs(Position[0]) <= 1048576 && std::abs(Position[1]) <= 1048576 &&
			   std::abs(Position[2]) <= 1048576 && std::isfinite(Rotation.W) && std::isfinite(Rotation.X) &&
			   std::isfinite(Rotation.Y) && std::isfinite(Rotation.Z) && std::isfinite(Rotation.Norm()) &&
			   Rotation.Norm() > 1e-12 && std::isfinite(Fov) && Fov >= 1 && Fov <= 179;
	}
};
struct CameraRig
{
	CameraPose Pose;
	bool Attached = false;
	bool InheritRotation = true;
	bool Evaluate(const CameraPose *target, CameraPose &out) const
	{
		if (!Pose.Valid() || (Attached && (!target || !target->Valid())))
			return false;
		auto result = Pose;
		if (Attached)
		{
			double rotation[3][3];
			target->Rotation.Normalized().ToRotationMatrix(rotation);
			for (int r = 0; r < 3; ++r)
			{
				result.Position[r] = target->Position[r];
				for (int c = 0; c < 3; ++c)
					result.Position[r] += rotation[r][c] * Pose.Position[c];
			}
			if (InheritRotation)
				result.Rotation = (target->Rotation.Normalized() * Pose.Rotation).Normalized();
		}
		result.Rotation = result.Rotation.Normalized();
		if (!result.Valid())
			return false;
		out = result;
		return true;
	}
	// Capture the current world pose relative to a target without a visual jump.
	bool Attach(const CameraPose &world, const CameraPose &target, bool inheritRotation)
	{
		if (!world.Valid() || !target.Valid())
			return false;
		auto local = world;
		double rotation[3][3];
		target.Rotation.Normalized().ToRotationMatrix(rotation);
		for (int r = 0; r < 3; ++r)
		{
			local.Position[r] = 0;
			for (int c = 0; c < 3; ++c)
				local.Position[r] += rotation[c][r] * (world.Position[c] - target.Position[c]);
		}
		if (inheritRotation)
			local.Rotation = (target.Rotation.Normalized().Conjugate() * world.Rotation).Normalized();
		if (!local.Valid())
			return false;
		Pose = local;
		Attached = true;
		InheritRotation = inheritRotation;
		return true;
	}
};
// Plain-value snapshots: UI and queued drawing never retain registry iterators or target pointers.
struct CameraInfo
{
	std::uint64_t Id = 0;
	std::string Name;
	CameraPose Pose;
	bool Attached = false, Available = true, Selected = false, Viewed = false;
	int EntityAddress = -1, AttachMode = 0;
	bool AimActive = false, AimSnap = true, AimCustomSource = false;
	int AimEntity = -1;
	std::array<double, 3> AimPoint{}, AimOffset{};
	double AimVelocity = 360, AimAcceleration = 90;
};
struct CameraGuideLine
{
	std::array<double, 3> A, B;
};
inline std::vector<CameraGuideLine> CameraGuideLines(const CameraPose &pose, double aspect, bool frustum)
{
	std::vector<CameraGuideLine> lines;
	if (!pose.Valid() || !std::isfinite(aspect) || aspect <= 0)
		return lines;
	lines.reserve(30);
	double rotation[3][3];
	pose.Rotation.Normalized().ToRotationMatrix(rotation);
	auto point = [&](double x, double y, double z)
	{
		std::array<double, 3> p;
		for (int r = 0; r < 3; ++r)
			p[r] = pose.Position[r] + rotation[r][0] * x + rotation[r][1] * y + rotation[r][2] * z;
		return p;
	};
	auto line = [&](const std::array<double, 3> &a, const std::array<double, 3> &b) { lines.push_back({a, b}); };
	std::array<std::array<double, 3>, 8> body;
	for (int i = 0; i < 8; ++i)
		body[i] = point(i & 1 ? -2 : -8, i & 2 ? 4 : -4, i & 4 ? 3 : -3);
	for (int i = 0; i < 8; ++i)
		for (int axis : {1, 2, 4})
			if (!(i & axis))
				line(body[i], body[i | axis]);
	// The lens pyramid and top chevron make orientation readable even without a frustum.
	const auto lens = point(4, 0, 0);
	for (int i : {1, 3, 5, 7})
		line(body[i], lens);
	line(point(-5, -3, 3), point(-5, 0, 6));
	line(point(-5, 0, 6), point(-5, 3, 3));
	if (frustum)
	{
		const double length = 48, halfWidth = length * std::tan(pose.Fov * 3.14159265358979323846 / 360.0),
					 halfHeight = halfWidth / aspect;
		if (!std::isfinite(halfWidth) || !std::isfinite(halfHeight))
			return {};
		std::array<std::array<double, 3>, 4> corners = {
			point(length, -halfWidth, -halfHeight), point(length, halfWidth, -halfHeight),
			point(length, halfWidth, halfHeight), point(length, -halfWidth, halfHeight)};
		const auto eye = point(0, 0, 0);
		for (int i = 0; i < 4; ++i)
		{
			line(eye, corners[i]);
			line(corners[i], corners[(i + 1) % 4]);
		}
	}
	return lines;
}
} // namespace advancedfx
