#pragma once

// Engine-independent cinematic camera model. All time evaluation is pure: seeking
// never changes a path, and placement never rewrites captured geometry.
#include "AfxMath.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cinematic
{
constexpr double Pi = 3.14159265358979323846;
using Vec = Afx::Math::Vector3;
using Quat = Afx::Math::Quaternion;
// Coordinate-convention adapters; all math is provided by shared/AfxMath.
Quat Euler(double pitch, double yaw, double roll);
Vec Angles(Quat rotation);
Vec Rotate(Quat rotation, Vec value);
Quat Slerp(Quat a, Quat b, double time);
struct Pose
{
	Vec position{0, 0, 0};
	Quat rotation;
	double fov = 90;
};
Pose Blend(const Pose & a, const Pose & b, double t);
enum class Curve
{
	Linear,
	Smooth,
	Bezier,
	Circle
};
enum class Interp
{
	Linear,
	Smooth,
	Step
};
enum class Ease
{
	Linear,
	In,
	Out,
	Smooth,
	Smoother,
	Custom
};
struct ScalarKey
{
	double time = 0, value = 0;
}; // normalized time 0..1
struct RotationKey
{
	double time = 0;
	Quat value;
};
struct Point
{
	Pose pose;
	Vec inHandle{0, 0, 0}, outHandle{0, 0, 0}; // relative to point, in local path coordinates
	double hold = 0; // seconds; INCLUDED in total duration
	double seconds = 0; // outgoing segment: 0 = distribute remaining time by length
	bool stop = false;
};
struct Placement
{
	Vec origin{0, 0, 0};
	Quat rotation;
	double scale = 1;
};
struct Constraints
{
	double grid = 0;
	std::array<bool, 3> locked{{false, false, false}};
	Vec plane{0, 0, 0};
	Vec apply(Vec v) const;
};
struct TimingSpan
{
	double arrival = 0, departure = 0, travel = 0;
};
struct ArcSample
{
	double parameter = 0, distance = 0;
};
struct Path
{
	std::uint64_t id = 0;
	std::string name;
	std::vector<Point> points;
	Placement anchor;
	Constraints constraints;
	Curve curve = Curve::Smooth;
	double tension = 0;
	Vec circleCenter{0, 0, 0};
	double circleRadius = 128, circleStart = 0, circleSweep = 360;
	int circlePlane = 0; // XY, XZ, YZ
	double duration = 5, start = 0;
	bool scheduled = false, holdEnd = false;
	double transition = 0; // crossfade from previous scheduled path, within this duration
	Ease ease = Ease::Linear;
	Interp rotationInterp = Interp::Linear, fovInterp = Interp::Linear;
	std::vector<ScalarKey> speed{{0, 0}, {1, 1}}; // per-segment cumulative speed curve
	bool easeProgress = false; // time-key easing; older motion tracks retain linear interpolation
	std::vector<ScalarKey> progress; // optional whole-movement time -> distance, supports reversals
	std::vector<RotationKey> rotations; // independent normalized time -> local rotation
	std::vector<ScalarKey> fovs; // independent normalized time -> FOV
	bool fixedAim = false;
	Vec aimTarget{0, 0, 0}; // local coordinates, follows placement

	void rebuild(); // after editing, validates and rebuilds arc-length lookup
	std::vector<TimingSpan> timeline() const;
	Pose evaluate(double seconds) const;
	Pose atDistance(double fraction) const;
	Pose pointWorld(std::size_t index) const;
	Pose toLocal(Pose world) const;
	Pose toWorld(Pose local) const;
	Vec position(double parameter) const;
	double Length() const
	{
		return m_Cache->length * anchor.scale;
	}
	double pointTime(std::size_t index) const;
	double pointDistance(std::size_t index) const;
	double timeAtDistance(double fraction) const;
	double distanceAtTime(double seconds) const;
	std::vector<ScalarKey> timeKeys() const; // absolute demo seconds -> path fraction
	void setTimeKey(double demoSeconds, double fraction);
	void replaceTimeKey(std::size_t index, double demoSeconds, double fraction);
	void eraseTimeKey(std::size_t index);
	void capture(Pose world, int index = -1, bool replace = false);

private:
	friend class Editor;
	struct Cache
	{
		std::vector<ArcSample> arc;
		std::vector<double> nodes;
		double length = 0;
	};
	std::shared_ptr<Cache> m_Cache = std::make_shared<Cache>();
	double distanceParameter(double fraction) const;
	double progressAt(double normalized) const;
	void assignTimeKeys(std::vector<ScalarKey> keys);
	double eased(double t, bool stopA, bool stopB) const;
};
struct State
{
	std::vector<Path> paths;
	int selected = -1, point = -1;
	std::uint64_t nextId = 1;
};
class Editor
{
public:
	State state;
	Path & active();
	const Path & active() const;
	Path & ensurePath();
	void create(std::string name = "");
	void duplicate();
	void remove();
	void commit(State next); // transactional validation + bounded undo history
	bool undo();
	bool redo();
	bool dirty() const;
	void markSaved();
	bool evaluate(double demoTime, Pose & pose) const; // latest start wins overlap
	std::vector<std::string> overlaps() const;
	std::string xml(bool all = true) const;
	void loadXml(const std::string & xml, bool replace); // validates before committing
	std::string legacyXml() const; // sampled classic HLAE campath, explicit lossy export
private:
	std::vector<State> m_Undo, m_Redo;
	std::string m_Saved;
};
std::string Serialize(const State & state);
State Deserialize(const std::string & xml);
void Validate(State & state);
} // namespace cinematic
