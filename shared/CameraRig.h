#pragma once
#include "AfxMath.h"
#include "CameraCollisionSettings.h"
#include <optional>
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
// Settings belong to each saved camera; evaluation state is kept separately.
struct CameraMotion
{
	double FollowHalfLife = 0, RotationHalfLife = 0;
	double MaxDistance = 128, TeleportDistance = 1024;
	double BoneHalfLife = 0, TurnRate = 360;
	bool PreserveFraming = true;
	double FramingMargin = .1;
	double BonePositionInfluence = 1, BoneRotationInfluence = 1;
	double BonePositionLimit = 0, BoneRotationLimit = 0;
	static bool MakePreset(const std::string &name, CameraMotion &out)
	{
		CameraMotion value;
		if (name == "stable-head")
		{
			value.BoneHalfLife = .08;
			value.BonePositionInfluence = .25;
			value.BoneRotationInfluence = .25;
			value.BonePositionLimit = 4;
			value.BoneRotationLimit = 10;
		}
		else if (name == "soft-follow")
		{
			value.FollowHalfLife = .15;
            value.RotationHalfLife = .15;
			value.BoneHalfLife = .05;
			value.BonePositionInfluence = .5;
			value.BoneRotationInfluence = .5;
			value.BonePositionLimit = 12;
			value.BoneRotationLimit = 20;
		}
		else if (name != "rigid")
			return false;
		out = value;
		return true;
	}
	bool NeedsBoneControl() const
	{
		return BonePositionInfluence != 1 || BoneRotationInfluence != 1 || BonePositionLimit > 0 ||
			   BoneRotationLimit > 0;
	}
	double OrbitSpeed = 0, OrbitEpoch = 0;
	bool Valid() const
	{
		return std::isfinite(FramingMargin) && FramingMargin >= 0 && FramingMargin <= .45 &&
			   std::isfinite(BonePositionInfluence) && BonePositionInfluence >= 0 &&
			   BonePositionInfluence <= 1 && std::isfinite(BoneRotationInfluence) &&
			   BoneRotationInfluence >= 0 && BoneRotationInfluence <= 1 && std::isfinite(BonePositionLimit) &&
			   BonePositionLimit >= 0 && BonePositionLimit <= 1048576 && std::isfinite(BoneRotationLimit) &&
			   BoneRotationLimit >= 0 && BoneRotationLimit <= 180 && std::isfinite(BoneHalfLife) &&
			   BoneHalfLife >= 0 && BoneHalfLife <= 10 && std::isfinite(TurnRate) && TurnRate >= 0 &&
			   TurnRate <= 1e6 && std::isfinite(FollowHalfLife) && FollowHalfLife >= 0 &&
			   FollowHalfLife <= 10 && std::isfinite(RotationHalfLife) && RotationHalfLife >= 0 &&
			   RotationHalfLife <= 10 && std::isfinite(MaxDistance) && MaxDistance >= 0 &&
			   MaxDistance <= 1048576 && std::isfinite(TeleportDistance) && TeleportDistance >= 0 &&
			   TeleportDistance <= 1048576 && std::isfinite(OrbitSpeed) && std::abs(OrbitSpeed) <= 3600 &&
			   std::isfinite(OrbitEpoch) && std::abs(OrbitEpoch) <= 1e9;
	}
	bool operator==(const CameraMotion &v) const
	{
		return PreserveFraming == v.PreserveFraming && FramingMargin == v.FramingMargin &&
			   BonePositionInfluence == v.BonePositionInfluence &&
			   BoneRotationInfluence == v.BoneRotationInfluence && BonePositionLimit == v.BonePositionLimit &&
			   BoneRotationLimit == v.BoneRotationLimit && BoneHalfLife == v.BoneHalfLife &&
			   TurnRate == v.TurnRate && FollowHalfLife == v.FollowHalfLife &&
			   RotationHalfLife == v.RotationHalfLife && MaxDistance == v.MaxDistance &&
			   TeleportDistance == v.TeleportDistance && OrbitSpeed == v.OrbitSpeed &&
			   OrbitEpoch == v.OrbitEpoch;
	}
	double OrbitAngle(double time) const
	{
		return std::remainder((time - OrbitEpoch) * OrbitSpeed, 360.0);
	}
	static CameraPose RotateLocal(CameraPose pose, double degrees, bool rotateView)
	{
		const double radians = degrees * 3.14159265358979323846 / 180;
		const double x = pose.Position[0], y = pose.Position[1];
		pose.Position[0] = std::cos(radians) * x - std::sin(radians) * y;
		pose.Position[1] = std::sin(radians) * x + std::cos(radians) * y;
		if (rotateView)
			pose.Rotation = (Afx::Math::Quaternion::FromQREulerAngles(
								 Afx::Math::QREulerAngles::FromQEulerAngles({0, degrees, 0})) *
							 pose.Rotation)
								.Normalized();
		return pose;
	}
};
struct CameraRig
{
	CameraPose Pose;
	CameraMotion Motion;
 std::optional<MirvCameraCollisionSettings> CollisionOverride;
	bool Attached = false;
	bool InheritRotation = true;
	bool Evaluate(const CameraPose *target, CameraPose &out, double time = 0,
				  const double *pivot = nullptr) const
	{
		if (!Pose.Valid() || !Motion.Valid() || !std::isfinite(time) ||
			(Attached && (!target || !target->Valid())))
			return false;
		const auto local = Pose;
		auto result = local;
		if (Attached)
		{
			double rotation[3][3];
			target->Rotation.Normalized().ToRotationMatrix(rotation);
			for (int r = 0; r < 3; ++r)
			{
				result.Position[r] = target->Position[r];
				for (int c = 0; c < 3; ++c)
					result.Position[r] += rotation[r][c] * local.Position[c];
			}
			if (InheritRotation)
				result.Rotation = (target->Rotation.Normalized() * local.Rotation).Normalized();
		}
		if (Attached)
		{
			const double *center = pivot ? pivot : target->Position;
			for (int axis = 0; axis < 3; ++axis)
				result.Position[axis] -= center[axis];
			result = CameraMotion::RotateLocal(result, Motion.OrbitAngle(time), InheritRotation);
			for (int axis = 0; axis < 3; ++axis)
				result.Position[axis] += center[axis];
		}
		result.Rotation = result.Rotation.Normalized();
		if (!result.Valid())
			return false;
		out = result;
		return true;
	}
	// Capture the current world pose relative to a target without a visual jump.
	bool Attach(const CameraPose &world, const CameraPose &target, bool inheritRotation, double time = 0,
				const double *pivot = nullptr)
	{
		if (!world.Valid() || !target.Valid() || !Motion.Valid() || !std::isfinite(time))
			return false;
		auto unrotated = world;
		const double *center = pivot ? pivot : target.Position;
		for (int axis = 0; axis < 3; ++axis)
			unrotated.Position[axis] -= center[axis];
		unrotated = CameraMotion::RotateLocal(unrotated, -Motion.OrbitAngle(time), inheritRotation);
		for (int axis = 0; axis < 3; ++axis)
			unrotated.Position[axis] += center[axis];
		auto local = unrotated;
		double rotation[3][3];
		target.Rotation.Normalized().ToRotationMatrix(rotation);
		for (int r = 0; r < 3; ++r)
		{
			local.Position[r] = 0;
			for (int c = 0; c < 3; ++c)
				local.Position[r] += rotation[c][r] * (unrotated.Position[c] - target.Position[c]);
		}
		if (inheritRotation)
			local.Rotation = (target.Rotation.Normalized().Conjugate() * unrotated.Rotation).Normalized();
		if (!local.Valid())
			return false;
		Pose = local;
		Attached = true;
		InheritRotation = inheritRotation;
		return true;
	}
};
struct CameraModifierLink
{
	std::uint64_t Id = 0;
	double Influence = 1;
	bool Enabled = true;
	bool operator==(const CameraModifierLink &o) const
	{
		return Id == o.Id && Influence == o.Influence && Enabled == o.Enabled;
	}
};
// Plain-value snapshots: UI and queued drawing never retain registry iterators or target pointers.
struct CameraInfo
{
	std::uint64_t Id = 0;
	size_t Index = 0;
	std::vector<CameraModifierLink> Modifiers;
	std::string Name;
	CameraPose Pose, BasePose;
	CameraMotion Motion;
 std::optional<MirvCameraCollisionSettings> CollisionOverride;
	bool Attached = false, Available = true, Selected = false, Viewed = false;
	bool CollisionDebug = false, CollisionBlocked = false, CollisionAnchorEmbedded = false,
		 CollisionContact = false;
	double CollisionRadius = 0;
	std::array<double, 3> CollisionAnchor{}, CollisionDesired{}, CollisionPosition{}, CollisionHit{},
		CollisionNormal{};
	int EntityAddress = -1, AttachMode = 0, BoneIndex = -1;
	std::string BoneName;
	bool AimActive = false, AimSnap = true, AimCustomSource = false;
	int AimEntity = -1;
	std::array<double, 3> AimPoint{}, AimOffset{};
	double AimVelocity = 360, AimAcceleration = 90;
};
struct CameraClipModifier : CameraModifierLink
{
	// Seconds relative to clip start; moving the clip carries its envelope.
	double Begin = 0, End = 1, FadeIn = 0, FadeOut = 0;
	bool Valid() const
	{
		return std::isfinite(Influence) && Influence >= 0 && Influence <= 1 && std::isfinite(Begin) &&
			   std::isfinite(End) && Begin >= 0 && End > Begin && End <= 1e9 && std::isfinite(FadeIn) &&
			   std::isfinite(FadeOut) && FadeIn >= 0 && FadeOut >= 0 && FadeIn <= End - Begin &&
			   FadeOut <= End - Begin;
	}
	bool operator==(const CameraClipModifier &o) const
	{
		return CameraModifierLink::operator==(o) && Begin == o.Begin && End == o.End && FadeIn == o.FadeIn &&
			   FadeOut == o.FadeOut;
	}
};
// A sequence adjustment clip has no parent camera. The shared effect definition
// supplies its shape; the instance owns absolute client-clock timing and mix.
struct SequenceModifierClip
{
 static constexpr int MaxTracks = 8;
 static constexpr size_t MaxClips = 256;
 std::uint64_t Id = ~std::uint64_t(0), Definition = 0;
 int Track = 0; // lower tracks evaluate first; higher tracks adjust their result
 double Begin = 0, End = 1, FadeIn = 0, FadeOut = 0, Influence = 1;
 bool Enabled = true;
 bool Valid() const
 {
  return Definition != 0 && Track >= 0 && Track < MaxTracks &&
   std::isfinite(Begin) && std::isfinite(End) && std::abs(Begin) <= 1e9 &&
   std::abs(End) <= 1e9 && End > Begin && End - Begin <= 1e6 && std::isfinite(FadeIn) &&
   std::isfinite(FadeOut) && FadeIn >= 0 && FadeOut >= 0 &&
   FadeIn <= End - Begin && FadeOut <= End - Begin &&
   std::isfinite(Influence) && Influence >= 0 && Influence <= 1;
 }
 bool operator==(const SequenceModifierClip &o) const
 {
  return Id == o.Id && Definition == o.Definition && Track == o.Track &&
   Begin == o.Begin && End == o.End && FadeIn == o.FadeIn && FadeOut == o.FadeOut &&
   Influence == o.Influence && Enabled == o.Enabled;
 }
 bool Active(double time) const { return Enabled && Influence > 0 && Begin <= time && time < End; }
 static bool Less(const SequenceModifierClip &a, const SequenceModifierClip &b)
 {
  if (a.Track != b.Track) return a.Track < b.Track;
  if (a.Begin != b.Begin) return a.Begin < b.Begin;
  return a.Id < b.Id;
 }
 // Stacking is explicit: overlaps occupy different tracks. Half-open ranges
 // allow adjacent clips to share a track without a double-applied boundary.
 bool Overlaps(const SequenceModifierClip &o) const
 { return Track == o.Track && Begin < o.End && o.Begin < End; }
};
struct CameraClip
{
	std::uint64_t Id = ~std::uint64_t(0), Target = 0;
	double Time = 0, Blend = 0;
	// TransitionStart() may precede or follow the clip's editorial Time.
	// Clips stay ordered by edit point; transition starts must also stay ordered.
	double TransitionOffset = 0;
	int BlendCurve = 0; // Position timing: 0 smoothstep, 1 linear, 2 smootherstep.
	int RotationCurve = 0, FovCurve = 0;
	// For historical projects, the default is the original live outgoing evaluation.
	// Newly authored clips in the editor opt into a frozen outgoing pose.
	bool FreezeOutgoing = false;
	bool CurvedPath = false;
	// World-space offsets from the straight-line cubic Bezier control points
	// (at one-third and two-thirds of the endpoint displacement).
	std::array<double, 3> HandleOut{{0, 0, 0}}, HandleIn{{0, 0, 0}};
	bool Campath = false;
	std::vector<CameraClipModifier> Modifiers;
	double TransitionStart() const { return Time + TransitionOffset; }
	bool ValidTransition() const
	{
		if (!std::isfinite(Time) || std::abs(Time) > 1e9 || !std::isfinite(Blend) ||
			Blend < 0 || Blend > 3600 || !std::isfinite(TransitionOffset) ||
			std::abs(TransitionOffset) > 3600 || BlendCurve < 0 || BlendCurve > 2 ||
			RotationCurve < 0 || RotationCurve > 2 || FovCurve < 0 || FovCurve > 2)
			return false;
		for (double value : HandleOut)
			if (!std::isfinite(value) || std::abs(value) > 1e6)
				return false;
		for (double value : HandleIn)
			if (!std::isfinite(value) || std::abs(value) > 1e6)
				return false;
		return std::isfinite(TransitionStart());
	}
	bool operator==(const CameraClip &o) const
	{
		return Id == o.Id && Target == o.Target && Time == o.Time && Blend == o.Blend &&
			   TransitionOffset == o.TransitionOffset && BlendCurve == o.BlendCurve &&
			   RotationCurve == o.RotationCurve && FovCurve == o.FovCurve &&
			   FreezeOutgoing == o.FreezeOutgoing && CurvedPath == o.CurvedPath &&
			   HandleOut == o.HandleOut && HandleIn == o.HandleIn &&
			   Campath == o.Campath && Modifiers == o.Modifiers;
	}
};
struct CameraGuideLine
{
	std::array<double, 3> A, B;
};
inline std::vector<CameraGuideLine> CameraCollisionGuideLines(const CameraInfo &camera)
{
	std::vector<CameraGuideLine> lines;
	if (!camera.CollisionDebug)
		return lines;
	lines.push_back({camera.CollisionDesired, camera.CollisionPosition});
	lines.push_back({camera.CollisionAnchor, camera.CollisionPosition});
	std::array<double, 3> corners[8];
	for (int c = 0; c < 8; ++c)
		for (int a = 0; a < 3; ++a)
			corners[c][a] = camera.CollisionPosition[a] + ((c & (1 << a)) ? 1 : -1) * camera.CollisionRadius;
	for (int c = 0; c < 8; ++c)
		for (int a = 0; a < 3; ++a)
			if (!(c & (1 << a)))
				lines.push_back({corners[c], corners[c | (1 << a)]});
	if (camera.CollisionContact)
	{
		auto end = camera.CollisionHit;
		for (int a = 0; a < 3; ++a)
			end[a] += camera.CollisionNormal[a] * 12;
		lines.push_back({camera.CollisionHit, end});
	}
	return lines;
}
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
