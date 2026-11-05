#include "CinematicPath.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "../deps/release/rapidxml/rapidxml.hpp"
// Forward declarations for the pinned RapidXML printer's two-phase lookup on GCC.
namespace rapidxml
{
namespace internal
{
template <class OutIt, class Ch> inline OutIt print_children(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_element_node(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_data_node(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_cdata_node(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_declaration_node(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_comment_node(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_doctype_node(OutIt, const xml_node<Ch> *, int, int);
template <class OutIt, class Ch> inline OutIt print_pi_node(OutIt, const xml_node<Ch> *, int, int);
} // namespace internal
} // namespace rapidxml
#include "../deps/release/rapidxml/rapidxml_print.hpp"
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

#include <iterator>

namespace cinematic
{
namespace
{
double clamp(double x)
{
	return std::clamp(x, 0.0, 1.0);
}
double smooth(double t)
{
	return t * t * (3 - 2 * t);
}
bool finite(double v)
{
	return std::isfinite(v);
}
void require(bool ok, const char * message)
{
	if (!ok)
		throw std::runtime_error(message);
}
double scalar(const std::vector<ScalarKey> & keys, double t, Interp mode = Interp::Linear)
{
	if (t <= keys.front().time)
		return keys.front().value;
	auto b = std::upper_bound(keys.begin(), keys.end(), t, [](double v, const ScalarKey & k) { return v < k.time; });
	if (b == keys.end())
		return keys.back().value;
	auto a = b - 1;
	double u = (t - a->time) / (b->time - a->time);
	if (mode == Interp::Step)
		u = 0;
	else if (mode == Interp::Smooth)
		u = smooth(u);
	return a->value + (b->value - a->value) * u;
}
void checkVec(Vec p)
{
	require(finite(p.X) && finite(p.Y) && finite(p.Z) && std::abs(p.X) <= 1e9 && std::abs(p.Y) <= 1e9 &&
				std::abs(p.Z) <= 1e9,
			"Coordinates must be finite and within +/-1e9 units");
}
void checkQuat(Quat & q)
{
	require(finite(q.W) && finite(q.X) && finite(q.Y) && finite(q.Z), "Rotation must be finite");
	double n = q.W * q.W + q.X * q.X + q.Y * q.Y + q.Z * q.Z;
	require(finite(n) && n > 1e-20, "Invalid quaternion");
	q = q.Normalized();
}
void checkPose(Pose & p)
{
	checkVec(p.position);
	checkQuat(p.rotation);
	require(finite(p.fov) && p.fov > 1 && p.fov < 179, "FOV must be between 1 and 179 degrees");
}
template <class Key> void checkKeys(const std::vector<Key> & keys)
{
	double last = -1;
	for (const auto & k : keys)
	{
		require(finite(k.time) && k.time >= 0 && k.time <= 1 && k.time > last,
				"Track times must be strictly increasing within 0..1");
		last = k.time;
	}
}
} // namespace
Quat Euler(double pitch, double yaw, double roll)
{
	return Quat::FromQREulerAngles(Afx::Math::QREulerAngles::FromQEulerAngles({pitch, yaw, roll}));
}
Vec Angles(Quat rotation)
{
	auto angles = rotation.ToQREulerAngles().ToQEulerAngles();
	return {angles.Pitch, angles.Yaw, angles.Roll};
}
Vec Rotate(Quat rotation, Vec value)
{
	Quat result = rotation * Quat{0, value.X, value.Y, value.Z} * rotation.Conjugate();
	return {result.X, result.Y, result.Z};
}
Quat Slerp(Quat a, Quat b, double time)
{
	a = a.Normalized();
	b = b.Normalized();
	if (Afx::Math::DotProduct(a, b) < 0)
		b = -1.0 * b;
	return a.Slerp(b, time).Normalized();
}
Pose Blend(const Pose & a, const Pose & b, double t)
{
	return {a.position * (1 - t) + b.position * t, Slerp(a.rotation, b.rotation, t), a.fov * (1 - t) + b.fov * t};
}
Vec Constraints::apply(Vec v) const
{
	if (grid > 0)
	{
		v.X = std::round(v.X / grid) * grid;
		v.Y = std::round(v.Y / grid) * grid;
		v.Z = std::round(v.Z / grid) * grid;
	}
	if (locked[0])
		v.X = plane.X;
	if (locked[1])
		v.Y = plane.Y;
	if (locked[2])
		v.Z = plane.Z;
	return v;
}
Pose Path::toWorld(Pose p) const
{
	p.position = anchor.origin + Rotate(anchor.rotation, p.position * anchor.scale);
	p.rotation = anchor.rotation * p.rotation;
	return p;
}
Pose Path::toLocal(Pose p) const
{
	p.position = Rotate(anchor.rotation.Conjugate(), p.position - anchor.origin) * (1 / anchor.scale);
	p.rotation = anchor.rotation.Conjugate() * p.rotation;
	return p;
}
Pose Path::pointWorld(std::size_t i) const
{
	Pose p = points.at(i).pose;
	p.position = position(double(i));
	return toWorld(p);
}
Vec Path::position(double parameter) const
{
	if (points.empty())
		return {0, 0, 0};
	if (points.size() == 1)
		return points[0].pose.position;
	double t = std::clamp(parameter, 0.0, double(points.size() - 1));
	if (curve == Curve::Circle)
	{
		double a = (circleStart + circleSweep * t / double(points.size() - 1)) * Pi / 180;
		Vec p{circleRadius * std::cos(a), circleRadius * std::sin(a), 0};
		if (circlePlane == 1)
			p = {p.X, 0, p.Y};
		else if (circlePlane == 2)
			p = {0, p.X, p.Y};
		return circleCenter + p;
	}
	std::size_t i = (std::min)(std::size_t(t), points.size() - 2);
	double u = t - double(i);
	Vec a = points[i].pose.position, b = points[i + 1].pose.position;
	if (curve == Curve::Linear)
		return a * (1 - u) + b * u;
	if (curve == Curve::Bezier)
	{
		Vec c = a + points[i].outHandle, d = b + points[i + 1].inHandle;
		double v = 1 - u;
		return a * (v * v * v) + c * (3 * v * v * u) + d * (3 * v * u * u) + b * (u * u * u);
	}
	Vec prev = i ? points[i - 1].pose.position : a, next = i + 2 < points.size() ? points[i + 2].pose.position : b;
	Vec m0 = (b - prev) * ((1 - tension) * 0.5), m1 = (next - a) * ((1 - tension) * 0.5);
	return a * (2 * u * u * u - 3 * u * u + 1) + m0 * (u * u * u - 2 * u * u + u) + b * (-2 * u * u * u + 3 * u * u) +
		   m1 * (u * u * u - u * u);
}
void Path::rebuild()
{
	require(points.size() <= 4096, "Maximum 4096 points per path");
	checkVec(anchor.origin);
	checkQuat(anchor.rotation);
	require(finite(anchor.scale) && anchor.scale > 1e-6 && anchor.scale < 1e6,
			"Scale must be positive and within 0.000001..1000000");
	require(finite(duration) && duration > 0 && duration <= 86400, "Duration must be within 0..86400 seconds");
	require(finite(start) && finite(transition) && transition >= 0 && transition <= duration,
			"Invalid schedule or transition");
	require(finite(start + duration), "Schedule overflows");
	require(finite(tension) && tension >= 0 && tension <= 1, "Tension must be 0..1");
	require(finite(constraints.grid) && constraints.grid >= 0, "Grid must be nonnegative");
	checkVec(constraints.plane);
	require(int(curve) >= 0 && int(curve) <= 3 && int(ease) >= 0 && int(ease) <= 5 && int(rotationInterp) >= 0 &&
				int(rotationInterp) <= 2 && int(fovInterp) >= 0 && int(fovInterp) <= 2,
			"Unknown interpolation mode");
	checkVec(circleCenter);
	checkVec(aimTarget);
	require(finite(circleRadius) && circleRadius > 0 && circleRadius <= 1e9 && finite(circleStart) &&
				finite(circleSweep) && std::abs(circleSweep) <= 36000 && circlePlane >= 0 && circlePlane <= 2,
			"Invalid circle");
	double holds = 0, fixed = 0;
	bool autoSegment = false;
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		auto & p = points[i];
		checkPose(p.pose);
		checkVec(p.inHandle);
		checkVec(p.outHandle);
		require(finite(p.hold) && p.hold >= 0 && finite(p.seconds) && p.seconds >= 0,
				"Holds and segment durations must be nonnegative");
		holds += p.hold;
		if (i + 1 < points.size())
		{
			fixed += p.seconds;
			autoSegment |= p.seconds == 0;
		}
	}
	require(holds + fixed <= duration + 1e-9, "Holds and explicit segments exceed total duration");
	if (points.size() > 1)
	{
		if (autoSegment)
			require(duration - holds - fixed > 1e-9, "No duration remains for automatic segments");
		else
			require(std::abs(duration - holds - fixed) < 1e-7, "Explicit segment times plus holds must equal duration");
	}
	checkKeys(speed);
	require(speed.size() >= 2 && speed.front().time == 0 && speed.back().time == 1 && speed.front().value == 0 &&
				speed.back().value == 1,
			"Speed curve needs endpoints (0,0) and (1,1)");
	double last = -1;
	for (auto k : speed)
	{
		require(finite(k.value) && k.value >= last && k.value >= 0 && k.value <= 1,
				"Speed curve must be monotonic within 0..1");
		last = k.value;
	}
	checkKeys(progress);
	for (auto k : progress)
		require(finite(k.value) && k.value >= 0 && k.value <= 1, "Progress must be 0..1");
	checkKeys(fovs);
	for (auto k : fovs)
		require(finite(k.value) && k.value > 1 && k.value < 179, "FOV must be within 1..179");
	checkKeys(rotations);
	for (auto & k : rotations)
		checkQuat(k.value);
	require(progress.empty() || (holds == 0 && fixed == 0),
			"Custom progress replaces automatic timing: clear holds and segment times first");
	m_Cache = std::make_shared<Cache>();
	m_Cache->arc.clear();
	m_Cache->nodes.assign(points.size(), 0);
	m_Cache->length = 0;
	if (points.empty())
		return;
	m_Cache->arc.push_back({0, 0});
	Vec prev = position(0);
	for (std::size_t i = 0; i + 1 < points.size(); ++i)
	{
		// Bounded arc-length table, independent of timing and world placement.
		int steps = curve == Curve::Linear ? 1 : 128;
		for (int j = 1; j <= steps; ++j)
		{
			double t = double(i) + double(j) / steps;
			Vec p = position(t);
			m_Cache->length += (p - prev).Length();
			require(finite(m_Cache->length), "Path length overflows");
			m_Cache->arc.push_back({t, m_Cache->length});
			prev = p;
		}
		m_Cache->nodes[i + 1] = m_Cache->length;
	}
	if (curve == Curve::Circle && points.size() > 1)
	{
		// Circle distance is analytic; no chord-length approximation.
		m_Cache->length = circleRadius * std::abs(circleSweep) * Pi / 180;
		for (auto & s : m_Cache->arc)
			s.distance = m_Cache->length * s.parameter / double(points.size() - 1);
		for (std::size_t i = 0; i < points.size(); ++i)
			m_Cache->nodes[i] = m_Cache->length * double(i) / double(points.size() - 1);
	}
}
std::vector<TimingSpan> Path::timeline() const
{
	std::vector<TimingSpan> spans(points.size());
	double remaining = duration, weights = 0;
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		remaining -= points[i].hold;
		if (i + 1 < points.size())
		{
			remaining -= points[i].seconds;
			if (points[i].seconds == 0)
				weights += m_Cache->length > 1e-9 ? m_Cache->nodes[i + 1] - m_Cache->nodes[i] : 1;
		}
	}
	// Zero-distance segments still animate orientation/FOV: give them a small
	// weight, or equal weights for a wholly stationary path.
	if (weights <= 1e-12)
		weights = 0;
	double clock = 0;
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		spans[i].arrival = clock;
		clock += points[i].hold;
		spans[i].departure = clock;
		if (i + 1 < points.size())
		{
			double weight = m_Cache->length > 1e-9 ? m_Cache->nodes[i + 1] - m_Cache->nodes[i] : 1;
			spans[i].travel =
				points[i].seconds > 0 ? points[i].seconds : (weights > 0 ? remaining * weight / weights : 0);
			clock += spans[i].travel;
		}
	}
	return spans;
}
double Path::distanceParameter(double fraction) const
{
	if (points.size() < 2)
		return 0;
	if (fraction <= 0)
		return 0;
	if (fraction >= 1)
		return double(points.size() - 1);
	if (m_Cache->length <= 1e-9)
		return clamp(fraction) * double(points.size() - 1);
	double d = clamp(fraction) * m_Cache->length;
	auto b = std::lower_bound(m_Cache->arc.begin(), m_Cache->arc.end(), d,
							  [](const ArcSample & s, double v) { return s.distance < v; });
	if (b == m_Cache->arc.begin())
		return 0;
	if (b == m_Cache->arc.end())
		return double(points.size() - 1);
	auto a = b - 1;
	double delta = b->distance - a->distance;
	return a->parameter + (b->parameter - a->parameter) * (delta > 0 ? (d - a->distance) / delta : 0);
}
Pose Path::atDistance(double fraction) const
{
	require(!points.empty(), "Path has no points");
	double t = distanceParameter(fraction);
	std::size_t i = points.size() > 1 ? (std::min)(std::size_t(t), points.size() - 2) : 0;
	double u = t - double(i);
	Pose p = points[i].pose;
	if (i + 1 < points.size())
	{
		double r = rotationInterp == Interp::Step ? 0 : rotationInterp == Interp::Smooth ? smooth(u) : u;
		double f = fovInterp == Interp::Step ? 0 : fovInterp == Interp::Smooth ? smooth(u) : u;
		p.rotation = Slerp(points[i].pose.rotation, points[i + 1].pose.rotation, r);
		p.fov = points[i].pose.fov + (points[i + 1].pose.fov - points[i].pose.fov) * f;
		if (u >= 1)
			p = points[i + 1].pose;
	}
	p.position = position(t);
	return toWorld(p);
}
double Path::eased(double t, bool a, bool b) const
{
	t = clamp(t);
	// End velocities of 0 (stop) or 1: monotonic cubic Hermite.
	if (a || b)
	{
		double m0 = a ? 0 : 1, m1 = b ? 0 : 1;
		t = (-2 * t * t * t + 3 * t * t) + (t * t * t - 2 * t * t + t) * m0 + (t * t * t - t * t) * m1;
	}
	switch (ease)
	{
	case Ease::In:
		return t * t;
	case Ease::Out:
		return 1 - (1 - t) * (1 - t);
	case Ease::Smooth:
		return smooth(t);
	case Ease::Smoother:
		return t * t * t * (t * (6 * t - 15) + 10);
	case Ease::Custom:
		return scalar(speed, t);
	default:
		return t;
	}
}
Pose Path::evaluate(double seconds) const
{
	require(!points.empty() && finite(seconds), "Cannot evaluate empty path or non-finite time");
	double t = std::clamp(seconds, 0.0, duration), normalized = t / duration;
	Pose p;
	if (!progress.empty())
		p = atDistance(progressAt(normalized));
	else if (t >= duration)
		p = atDistance(1);
	else
	{
		auto spans = timeline();
		double fraction = 1;
		bool found = false;
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			double node = m_Cache->length > 1e-9 ? m_Cache->nodes[i] / m_Cache->length
						  : points.size() > 1	 ? double(i) / double(points.size() - 1)
												 : 0;
			if (t <= spans[i].departure)
			{
				p = pointWorld(i);
				found = true;
				break;
			}
			if (i + 1 < points.size() && t < spans[i].departure + spans[i].travel)
			{
				double u = eased((t - spans[i].departure) / spans[i].travel, points[i].stop || points[i].hold > 0,
								 points[i + 1].stop || points[i + 1].hold > 0);
				double next = m_Cache->length > 1e-9 ? m_Cache->nodes[i + 1] / m_Cache->length
													 : double(i + 1) / double(points.size() - 1);
				fraction = node + (next - node) * u;
				if (std::abs(next - node) < 1e-12)
				{
					double r = rotationInterp == Interp::Step ? 0 : rotationInterp == Interp::Smooth ? smooth(u) : u;
					double f = fovInterp == Interp::Step ? 0 : fovInterp == Interp::Smooth ? smooth(u) : u;
					Pose local = points[i].pose;
					local.rotation = Slerp(local.rotation, points[i + 1].pose.rotation, r);
					local.fov += (points[i + 1].pose.fov - local.fov) * f;
					p = toWorld(local);
					found = true;
				}
				break;
			}
		}
		if (!found)
			p = atDistance(fraction);
	}
	if (!rotations.empty())
	{
		Quat r = rotations.front().value;
		auto b = std::upper_bound(rotations.begin(), rotations.end(), normalized,
								  [](double t, const RotationKey & k) { return t < k.time; });
		if (b == rotations.end())
			r = rotations.back().value;
		else if (b != rotations.begin())
		{
			auto a = b - 1;
			double u = (normalized - a->time) / (b->time - a->time);
			if (rotationInterp == Interp::Step)
				u = 0;
			else if (rotationInterp == Interp::Smooth)
				u = smooth(u);
			r = Slerp(a->value, b->value, u);
		}
		p.rotation = anchor.rotation * r;
	}
	if (!fovs.empty())
		p.fov = scalar(fovs, normalized, fovInterp);
	if (fixedAim)
	{
		Vec d = toWorld({aimTarget, {}, 90}).position - p.position;
		if (d.Length() > 1e-9)
			p.rotation = Euler(-std::atan2(d.Z, std::hypot(d.X, d.Y)) * 180 / Pi, std::atan2(d.Y, d.X) * 180 / Pi, 0);
	}
	return p;
}
double Path::pointDistance(std::size_t i) const
{
	require(i < points.size(), "Invalid point index");
	return m_Cache->length > 1e-9 ? m_Cache->nodes.at(i) / m_Cache->length
		   : points.size() > 1	  ? double(i) / double(points.size() - 1)
								  : 0;
}
double Path::progressAt(double t) const
{
	if (!easeProgress)
		return scalar(progress, t);
	auto b =
		std::upper_bound(progress.begin(), progress.end(), t, [](double v, const ScalarKey & k) { return v < k.time; });
	if (b == progress.begin())
		return b->value;
	if (b == progress.end())
		return progress.back().value;
	auto a = b - 1;
	return a->value + (b->value - a->value) * eased((t - a->time) / (b->time - a->time), false, false);
}
double Path::distanceAtTime(double seconds) const
{
	require(!points.empty() && finite(seconds), "Cannot evaluate empty path or non-finite time");
	double t = std::clamp(seconds, 0.0, duration);
	if (!progress.empty())
		return progressAt(t / duration);
	if (t >= duration)
		return 1;
	auto spans = timeline();
	for (std::size_t i = 0; i < points.size(); ++i)
	{
		if (t <= spans[i].departure)
			return pointDistance(i);
		if (i + 1 < points.size() && t < spans[i].departure + spans[i].travel)
		{
			double u = eased((t - spans[i].departure) / spans[i].travel, points[i].stop || points[i].hold > 0,
							 points[i + 1].stop || points[i + 1].hold > 0);
			return pointDistance(i) + (pointDistance(i + 1) - pointDistance(i)) * u;
		}
	}
	return 1;
}
double Path::pointTime(std::size_t i) const
{
	if (progress.empty())
	{
		require(i < points.size(), "Invalid point index");
		return timeline().at(i).arrival;
	}
	return timeAtDistance(pointDistance(i));
}
double Path::timeAtDistance(double node) const
{
	require(!points.empty() && finite(node) && node >= 0 && node <= 1, "Invalid path position");
	if (progress.empty())
	{
		auto spans = timeline();
		for (std::size_t i = 0; i < points.size(); ++i)
		{
			double a = pointDistance(i);
			if (node == a)
				return spans[i].arrival;
			if (i + 1 < points.size())
			{
				double b = pointDistance(i + 1);
				if (node > a && node <= b && b > a)
				{
					double target = (node - a) / (b - a), lo = 0, hi = 1;
					for (int j = 0; j < 60; ++j)
					{
						double mid = (lo + hi) * 0.5;
						if (eased(mid, points[i].stop || points[i].hold > 0,
								  points[i + 1].stop || points[i + 1].hold > 0) < target)
							lo = mid;
						else
							hi = mid;
					}
					return spans[i].departure + spans[i].travel * (lo + hi) * 0.5;
				}
			}
		}
		return 0;
	}
	if (node == progress.front().value)
		return progress.front().time * duration;
	for (std::size_t j = 1; j < progress.size(); ++j)
	{
		auto a = progress[j - 1], b = progress[j];
		if (node >= (std::min)(a.value, b.value) && node <= (std::max)(a.value, b.value) && a.value != b.value)
		{
			double u = (node - a.value) / (b.value - a.value);
			if (easeProgress)
			{
				double lo = 0, hi = 1;
				for (int step = 0; step < 60; ++step)
				{
					double m = (lo + hi) * 0.5;
					if (eased(m, false, false) < u)
						lo = m;
					else
						hi = m;
				}
				u = (lo + hi) * 0.5;
			}
			return duration * (a.time + (b.time - a.time) * u);
		}
	}
	throw std::runtime_error("This path position is not visited by the timing keys");
}
std::vector<ScalarKey> Path::timeKeys() const
{
	auto keys = progress;
	for (auto & key : keys)
		key.time = start + key.time * duration;
	return keys;
}
void Path::assignTimeKeys(std::vector<ScalarKey> keys)
{
	Path next = *this;
	if (keys.empty())
	{
		next.progress.clear();
		next.easeProgress = false;
		next.scheduled = false;
	}
	else
	{
		next.start = keys.front().time;
		next.duration = keys.size() > 1 ? keys.back().time - next.start : 5;
		for (auto & key : keys)
			key.time = (key.time - next.start) / next.duration;
		next.progress = std::move(keys);
		next.easeProgress = true;
		next.scheduled = true;
		// Explicit time keys replace legacy automatic segment/hold timing. Equal
		// positions at different times express holds without another mechanism.
		for (auto & point : next.points)
		{
			point.hold = point.seconds = 0;
			point.stop = false;
		}
	}
	next.rebuild();
	*this = std::move(next);
}
void Path::setTimeKey(double seconds, double fraction)
{
	require(!points.empty(), "Capture geometry before timing");
	require(finite(seconds) && seconds >= 0 && finite(fraction) && fraction >= 0 && fraction <= 1,
			"Invalid timing key");
	auto keys = timeKeys();
	auto it = std::lower_bound(keys.begin(), keys.end(), seconds,
							   [](const ScalarKey & key, double t) { return key.time < t; });
	if (it != keys.end() && std::abs(it->time - seconds) < 1e-9)
		it->value = fraction;
	else
		keys.insert(it, {seconds, fraction});
	assignTimeKeys(std::move(keys));
}
void Path::replaceTimeKey(std::size_t index, double seconds, double fraction)
{
	require(finite(seconds) && seconds >= 0 && finite(fraction) && fraction >= 0 && fraction <= 1,
			"Invalid timing key");
	auto keys = timeKeys();
	require(index < keys.size(), "Invalid timing key index");
	keys.erase(keys.begin() + index);
	auto it = std::lower_bound(keys.begin(), keys.end(), seconds,
							   [](const ScalarKey & key, double t) { return key.time < t; });
	require(it == keys.end() || std::abs(it->time - seconds) >= 1e-9,
			"Another timing key already occupies this demo time");
	keys.insert(it, {seconds, fraction});
	assignTimeKeys(std::move(keys));
}
void Path::eraseTimeKey(std::size_t index)
{
	auto keys = timeKeys();
	require(index < keys.size(), "Invalid timing key index");
	keys.erase(keys.begin() + index);
	assignTimeKeys(std::move(keys));
}
void Path::capture(Pose world, int index, bool replace)
{
	require(curve != Curve::Circle, "Switch curve to linear/smooth/bezier before editing circle points");
	Pose p = toLocal(world);
	p.position = constraints.apply(p.position);
	if (replace)
	{
		require(index >= 0 && std::size_t(index) < points.size(), "Select a point first");
		points[index].pose = p;
		points[index].inHandle = constraints.apply(p.position + points[index].inHandle) - p.position;
		points[index].outHandle = constraints.apply(p.position + points[index].outHandle) - p.position;
	}
	else
	{
		if (index < 0)
			index = int(points.size());
		require(std::size_t(index) <= points.size(), "Invalid insert index");
		Point v;
		v.pose = p;
		points.insert(points.begin() + index, v);
	}
	rebuild();
}
void Validate(State & s)
{
	require(s.paths.size() <= 128, "Maximum 128 paths");
	std::vector<std::uint64_t> ids;
	std::size_t totalPoints = 0, totalKeys = 0;
	for (auto & p : s.paths)
	{
		totalPoints += p.points.size();
		totalKeys += p.progress.size() + p.rotations.size() + p.fovs.size() + p.speed.size();
		require(totalKeys <= 65536, "Maximum 65536 animation keys across all paths");
		require(totalPoints <= 16384, "Maximum 16384 points across all paths");
		require(p.progress.size() <= 16384 && p.rotations.size() <= 16384 && p.fovs.size() <= 16384 &&
					p.speed.size() <= 16384,
				"Animation track exceeds 16384 keys");
		require(p.id > 0 && p.id < std::numeric_limits<std::uint64_t>::max(), "Invalid path ID");
		require(std::find(ids.begin(), ids.end(), p.id) == ids.end(), "Duplicate path ID");
		ids.push_back(p.id);
		require(p.name.size() <= 256, "Path name too long");
		p.rebuild();
		s.nextId = (std::max)(s.nextId, p.id + 1);
	}
	require(s.selected >= -1 && s.selected < int(s.paths.size()), "Invalid path selection");
	if (s.selected < 0)
		s.point = -1;
	else
		require(s.point >= -1 && s.point < int(s.paths[s.selected].points.size()), "Invalid point selection");
}
Path & Editor::active()
{
	require(state.selected >= 0 && state.selected < int(state.paths.size()), "No path selected");
	return state.paths[state.selected];
}
const Path & Editor::active() const
{
	require(state.selected >= 0 && state.selected < int(state.paths.size()), "No path selected");
	return state.paths[state.selected];
}
Path & Editor::ensurePath()
{
	if (state.selected < 0)
		create();
	return active();
}
void Editor::commit(State next)
{
	Validate(next);
	m_Undo.push_back(state);
	for (auto & p : m_Undo.back().paths)
		p.m_Cache = std::make_shared<Path::Cache>();
	if (m_Undo.size() > 64)
		m_Undo.erase(m_Undo.begin());
	state = std::move(next);
	m_Redo.clear();
}
void Editor::create(std::string name)
{
	State n = state;
	Path p;
	p.id = n.nextId++;
	p.name = name.empty() ? "Path " + std::to_string(p.id) : name;
	n.paths.push_back(p);
	n.selected = int(n.paths.size() - 1);
	n.point = -1;
	commit(std::move(n));
}
void Editor::duplicate()
{
	State n = state;
	Path p = active();
	p.id = n.nextId++;
	p.name += " copy";
	p.scheduled = false;
	n.paths.push_back(p);
	n.selected = int(n.paths.size() - 1);
	commit(std::move(n));
}
void Editor::remove()
{
	active();
	State n = state;
	n.paths.erase(n.paths.begin() + n.selected);
	n.selected = n.paths.empty() ? -1 : (std::min)(n.selected, int(n.paths.size() - 1));
	n.point = -1;
	commit(std::move(n));
}
bool Editor::undo()
{
	if (m_Undo.empty())
		return false;
	m_Redo.push_back(state);
	for (auto & p : m_Redo.back().paths)
		p.m_Cache = std::make_shared<Path::Cache>();
	state = std::move(m_Undo.back());
	Validate(state);
	m_Undo.pop_back();
	return true;
}
bool Editor::redo()
{
	if (m_Redo.empty())
		return false;
	m_Undo.push_back(state);
	for (auto & p : m_Undo.back().paths)
		p.m_Cache = std::make_shared<Path::Cache>();
	state = std::move(m_Redo.back());
	Validate(state);
	m_Redo.pop_back();
	return true;
}
bool Editor::dirty() const
{
	State canonical = state;
	canonical.selected = -1;
	canonical.point = -1;
	return m_Saved.empty() ? !state.paths.empty() : Serialize(canonical) != m_Saved;
}
void Editor::markSaved()
{
	State canonical = state;
	canonical.selected = -1;
	canonical.point = -1;
	m_Saved = Serialize(canonical);
}
bool Editor::evaluate(double time, Pose & pose) const
{
	if (!std::isfinite(time))
		return false;
	const Path *current = nullptr, *previous = nullptr;
	for (const auto & p : state.paths)
		if (p.scheduled && !p.points.empty() && p.start <= time)
		{
			if (!current || p.start > current->start || (p.start == current->start && p.id > current->id))
				current = &p;
		}
	if (!current || (time > current->start + current->duration && !current->holdEnd))
		return false;
	pose = current->evaluate(time - current->start);
	if (current->transition > 0 && time < current->start + current->transition)
	{
		for (const auto & p : state.paths)
			if (p.scheduled && !p.points.empty() && p.start < current->start &&
				(!previous || p.start > previous->start || (p.start == previous->start && p.id > previous->id)))
				previous = &p;
		if (previous)
			pose = Blend(previous->evaluate(time - previous->start), pose,
						 smooth(clamp((time - current->start) / current->transition)));
	}
	return true;
}
std::vector<std::string> Editor::overlaps() const
{
	std::vector<std::string> result;
	for (std::size_t i = 0; i < state.paths.size(); ++i)
		for (std::size_t j = i + 1; j < state.paths.size(); ++j)
		{
			auto & a = state.paths[i];
			auto & b = state.paths[j];
			if (a.scheduled && b.scheduled && a.start <= b.start + b.duration && b.start <= a.start + a.duration)
				result.push_back(a.name + " / " + b.name);
		}
	return result;
}
std::string Editor::xml(bool all) const
{
	if (all)
		return Serialize(state);
	State s;
	s.paths.push_back(active());
	s.selected = 0;
	return Serialize(s);
}
void Editor::loadXml(const std::string & xml, bool replace)
{
	State loaded = Deserialize(xml), n = state;
	require(!loaded.paths.empty(), "XML collection contains no paths");
	if (replace)
		n = std::move(loaded);
	else
	{
		for (auto p : loaded.paths)
		{
			p.id = n.nextId++;
			n.paths.push_back(std::move(p));
		}
		n.selected = int(n.paths.size() - loaded.paths.size());
		n.point = -1;
	}
	commit(std::move(n));
}
// Versioned persistence uses the same RapidXML dependency as classic CamPath.
namespace
{
struct Node
{
	std::string name;
	std::map<std::string, std::string> attrs;
	std::vector<Node> children;
};
[[noreturn]] void fail(const char * why)
{
	throw std::runtime_error(why);
}
Node readNode(rapidxml::xml_node<> * source, int depth, std::size_t & count)
{
	if (depth > 8 || ++count > 300000)
		fail("XML structure exceeds limits");
	Node result;
	result.name = source->name();
	for (std::size_t i = 0; i < source->value_size(); ++i)
		if (source->value()[i] != ' ' && source->value()[i] != '\t' && source->value()[i] != '\r' &&
			source->value()[i] != '\n')
			fail("Unexpected XML text");
	for (auto * a = source->first_attribute(); a; a = a->next_attribute())
		if (!result.attrs.emplace(a->name(), a->value()).second)
			fail("Duplicate XML attribute");
	for (auto * child = source->first_node(); child; child = child->next_sibling())
	{
		if (child->type() != rapidxml::node_element)
			fail("Unexpected XML content");
		result.children.push_back(readNode(child, depth + 1, count));
	}
	return result;
}
Node readXml(const std::string & xml)
{
	if (xml.size() > 16 * 1024 * 1024 || xml.find('\0') != std::string::npos)
		fail("Invalid XML size or embedded null");
	// RapidXML does not resolve external resources. Explicitly reject DTDs and
	// bound nesting before parsing to retain the previous loader's limits.
	std::size_t depth = 0;
	for (std::size_t i = 0; i < xml.size();)
	{
		if (xml.compare(i, 4, "<!--") == 0)
		{
			auto end = xml.find("-->", i + 4);
			if (end == std::string::npos)
				fail("Unclosed XML comment");
			i = end + 3;
			continue;
		}
		if (xml.compare(i, 2, "<!") == 0)
			fail("DTD and declarations are not supported");
		if (xml[i] != '<')
		{
			++i;
			continue;
		}
		bool closing = i + 1 < xml.size() && xml[i + 1] == '/';
		bool instruction = i + 1 < xml.size() && xml[i + 1] == '?';
		char quote = 0;
		std::size_t end = i + 1;
		for (; end < xml.size(); ++end)
		{
			char c = xml[end];
			if (quote)
			{
				if (c == quote)
					quote = 0;
			}
			else if (c == '\'' || c == '"')
				quote = c;
			else if (c == '>')
				break;
		}
		if (end == xml.size())
			fail("Unclosed XML tag");
		if (!instruction)
		{
			if (closing)
			{
				if (!depth)
					fail("Unexpected XML close tag");
				--depth;
			}
			else if (end == 0 || xml[end - 1] != '/')
			{
				if (++depth > 9)
					fail("XML nesting exceeds limits");
			}
		}
		i = end + 1;
	}
	for (std::size_t i = 0; i < xml.size();)
	{
		if (xml.compare(i, 4, "<!--") == 0)
		{
			auto end = xml.find("-->", i + 4);
			i = end + 3;
			continue;
		}
		if (xml[i] == '&')
		{
			auto end = xml.find(';', i + 1);
			if (end == std::string::npos)
				fail("Unclosed XML reference");
			auto reference = xml.substr(i + 1, end - i - 1);
			bool known = reference == "amp" || reference == "lt" || reference == "gt" || reference == "quot" ||
						 reference == "apos";
			if (!known && !reference.empty() && reference[0] == '#')
			{
				bool hex = reference.size() > 1 && reference[1] == 'x';
				std::size_t first = hex ? 2 : 1;
				if (first == reference.size())
					fail("Empty XML character reference");
				unsigned long code = 0;
				for (std::size_t j = first; j < reference.size(); ++j)
				{
					char c = reference[j];
					int digit = c >= '0' && c <= '9'		  ? c - '0'
								: hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
								: hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
															  : -1;
					if (digit < 0 || code > 0x10ffff)
						fail("Invalid XML character reference");
					code = code * (hex ? 16 : 10) + digit;
				}
				known = code == 9 || code == 10 || code == 13 ||
						(code >= 32 && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff) && code != 0xfffe &&
						 code != 0xffff);
			}
			if (!known)
				fail("Unsupported XML reference");
			i = end + 1;
			continue;
		}
		if ((unsigned char)xml[i] < 32 && xml[i] != 9 && xml[i] != 10 && xml[i] != 13)
			fail("Invalid XML character");
		++i;
	}
	std::vector<char> data(xml.begin(), xml.end());
	data.push_back(0);
	rapidxml::xml_document<> document;
	try
	{
		document.parse<rapidxml::parse_validate_closing_tags | rapidxml::parse_no_data_nodes>(data.data());
	}
	catch (const rapidxml::parse_error &)
	{
		fail("Malformed XML");
	}
	auto * root = document.first_node();
	if (!root || root->type() != rapidxml::node_element || root->next_sibling())
		fail("Expected one XML root");
	std::size_t count = 0;
	return readNode(root, 0, count);
}
std::string attr(const Node & n, const char * k, std::string fallback = "")
{
	auto a = n.attrs.find(k);
	return a == n.attrs.end() ? fallback : a->second;
}
double number(const Node & n, const char * k, double fallback = 0, bool required = false)
{
	auto a = n.attrs.find(k);
	if (a == n.attrs.end())
	{
		if (required)
			fail("Missing required numeric XML attribute");
		return fallback;
	}
	std::istringstream in(a->second);
	in.imbue(std::locale::classic());
	double v;
	in >> std::noskipws >> v;
	if (!in || in.peek() != std::char_traits<char>::eof() || !std::isfinite(v))
		fail("Invalid numeric XML attribute");
	return v;
}
int integer(const Node & n, const char * k, int fallback = 0)
{
	double v = number(n, k, fallback);
	if (v != std::floor(v) || v < -1000000 || v > 1000000)
		fail("Invalid integer XML attribute");
	return int(v);
}
bool boolean(const Node & n, const char * k, bool fallback = false)
{
	double v = number(n, k, fallback ? 1 : 0);
	if (v != 0 && v != 1)
		fail("Invalid boolean XML attribute");
	return v != 0;
}
Vec vec(const Node & n, const char * prefix)
{
	std::string p = prefix;
	return {number(n, (p + "x").c_str()), number(n, (p + "y").c_str()), number(n, (p + "z").c_str())};
}
Quat quat(const Node & n)
{
	if (n.attrs.count("qw"))
		return {number(n, "qw", 1), number(n, "qx"), number(n, "qy"), number(n, "qz")};
	return Euler(number(n, "ry"), number(n, "rz"), number(n, "rx"));
}
Pose pose(const Node & n)
{
	return {vec(n, ""), quat(n), number(n, "fov", 90)};
}
class Writer
{
	rapidxml::xml_document<> m_Document;
	rapidxml::xml_node<> * m_Current = nullptr;

public:
	Writer()
	{
		auto * declaration = m_Document.allocate_node(rapidxml::node_declaration);
		declaration->append_attribute(m_Document.allocate_attribute("version", "1.0"));
		declaration->append_attribute(m_Document.allocate_attribute("encoding", "utf-8"));
		m_Document.append_node(declaration);
	}
	void begin(const char * name)
	{
		auto * node = m_Document.allocate_node(rapidxml::node_element, name);
		if (m_Current)
			m_Current->append_node(node);
		else
			m_Document.append_node(node);
		m_Current = node;
	}
	void end()
	{
		m_Current = m_Current->parent();
		if (m_Current == &m_Document)
			m_Current = nullptr;
	}
	void text(const char * key, const std::string & value)
	{
		for (unsigned char c : value)
			if (c < 32 && c != 9 && c != 10 && c != 13)
				fail("Invalid XML character");
		m_Current->append_attribute(
			m_Document.allocate_attribute(m_Document.allocate_string(key), m_Document.allocate_string(value.c_str())));
	}
	void a(const char * key, double value)
	{
		std::ostringstream out;
		out.imbue(std::locale::classic());
		out << std::setprecision(17) << value;
		text(key, out.str());
	}
	void v(const char * prefix, Vec value)
	{
		std::string p = prefix;
		a((p + "x").c_str(), value.X);
		a((p + "y").c_str(), value.Y);
		a((p + "z").c_str(), value.Z);
	}
	void q(Quat value)
	{
		a("qw", value.W);
		a("qx", value.X);
		a("qy", value.Y);
		a("qz", value.Z);
	}
	void p(Pose value)
	{
		v("", value.position);
		q(value.rotation);
		a("fov", value.fov);
	}
	std::string finish()
	{
		std::string xml;
		rapidxml::print(std::back_inserter(xml), m_Document);
		if (xml.size() > 16 * 1024 * 1024)
			fail("Serialized XML exceeds 16 MiB");
		return xml;
	}
};
void scalarTrack(Writer & w, const char * name, const std::vector<ScalarKey> & keys)
{
	for (auto key : keys)
	{
		w.begin(name);
		w.a("t", key.time);
		w.a("value", key.value);
		w.end();
	}
}
} // namespace
std::string Serialize(const State & s)
{
	Writer w;
	w.begin("cinematic");
	w.a("version", 1);
	w.a("selected", s.selected);
	w.a("point", s.point);
	for (const auto & p : s.paths)
	{
		w.begin("path");
		w.text("id", std::to_string(p.id));
		w.text("name", p.name);
		w.a("curve", int(p.curve));
		w.a("tension", p.tension);
		w.a("duration", p.duration);
		w.a("start", p.start);
		w.a("scheduled", p.scheduled);
		w.a("holdEnd", p.holdEnd);
		w.a("transition", p.transition);
		w.a("ease", int(p.ease));
		w.a("rotationInterp", int(p.rotationInterp));
		w.a("fovInterp", int(p.fovInterp));
		w.a("easeProgress", p.easeProgress);
		w.a("fixedAim", p.fixedAim);
		w.v("aim", p.aimTarget);
		w.begin("anchor");
		w.v("", p.anchor.origin);
		w.q(p.anchor.rotation);
		w.a("scale", p.anchor.scale);
		w.end();
		w.begin("constraints");
		w.a("grid", p.constraints.grid);
		w.a("lockX", p.constraints.locked[0]);
		w.a("lockY", p.constraints.locked[1]);
		w.a("lockZ", p.constraints.locked[2]);
		w.v("", p.constraints.plane);
		w.end();
		w.begin("circle");
		w.v("", p.circleCenter);
		w.a("radius", p.circleRadius);
		w.a("start", p.circleStart);
		w.a("sweep", p.circleSweep);
		w.a("plane", p.circlePlane);
		w.end();
		for (const auto & point : p.points)
		{
			w.begin("point");
			w.p(point.pose);
			w.v("in", point.inHandle);
			w.v("out", point.outHandle);
			w.a("hold", point.hold);
			w.a("seconds", point.seconds);
			w.a("stop", point.stop);
			w.end();
		}
		scalarTrack(w, "speed", p.speed);
		scalarTrack(w, "progress", p.progress);
		scalarTrack(w, "fov", p.fovs);
		for (const auto & rotation : p.rotations)
		{
			w.begin("rotation");
			w.a("t", rotation.time);
			w.q(rotation.value);
			w.end();
		}
		w.end();
	}
	w.end();
	return w.finish();
}
State Deserialize(const std::string & xml)
{
	Node root = readXml(xml);
	State s;
	if (root.name == "campath")
	{
		Path p;
		p.id = 1;
		p.name = "Imported campath";
		p.curve = Curve::Linear;
		p.scheduled = true;
		p.holdEnd = boolean(root, "hold");
		std::vector<std::pair<double, Pose>> keys;
		for (auto & group : root.children)
		{
			if (group.name != "points")
				fail("Unexpected legacy campath element");
			for (auto & pt : group.children)
			{
				if (pt.name != "p")
					fail("Unexpected legacy point element");
				keys.push_back({number(pt, "t", 0, true), pose(pt)});
			}
		}
		if (keys.empty())
			fail("Legacy campath has no points");
		std::sort(keys.begin(), keys.end(), [](const auto & a, const auto & b) { return a.first < b.first; });
		double first = keys.front().first, last = keys.back().first;
		p.start = first + number(root, "offset");
		p.duration = last > first ? last - first : 5;
		double traveled = 0;
		std::vector<double> distances{0};
		for (std::size_t i = 0; i < keys.size(); ++i)
		{
			Point pt;
			pt.pose = keys[i].second;
			p.points.push_back(pt);
			if (i)
			{
				if (keys[i].first <= keys[i - 1].first)
					fail("Duplicate legacy key time");
				traveled += (keys[i].second.position - keys[i - 1].second.position).Length();
				distances.push_back(traveled);
			}
		}
		if (keys.size() > 1)
			for (std::size_t i = 0; i < keys.size(); ++i)
			{
				double t = (keys[i].first - first) / p.duration;
				p.progress.push_back(
					{t, traveled > 1e-9 ? distances[i] / traveled : double(i) / double(keys.size() - 1)});
				p.rotations.push_back({t, keys[i].second.rotation});
				p.fovs.push_back({t, keys[i].second.fov});
			}
		s.paths.push_back(std::move(p));
		s.selected = 0;
		Validate(s);
		return s;
	}
	if (root.name != "cinematic" || integer(root, "version", -1) != 1)
		fail("Unsupported cinematic XML version");
	s.selected = integer(root, "selected", -1);
	s.point = integer(root, "point", -1);
	for (auto & n : root.children)
	{
		if (n.name != "path")
			fail("Unexpected cinematic element");
		Path p;
		std::string id = attr(n, "id");
		try
		{
			std::size_t used = 0;
			p.id = std::stoull(id, &used);
			if (used != id.size() || id.empty() || id[0] == '-')
				fail("Invalid path ID");
		}
		catch (...)
		{
			fail("Invalid path ID");
		}
		p.name = attr(n, "name");
		p.curve = Curve(integer(n, "curve", 1));
		p.tension = number(n, "tension");
		p.duration = number(n, "duration", 5);
		p.start = number(n, "start");
		p.scheduled = boolean(n, "scheduled");
		p.holdEnd = boolean(n, "holdEnd");
		p.transition = number(n, "transition");
		p.ease = Ease(integer(n, "ease"));
		p.rotationInterp = Interp(integer(n, "rotationInterp"));
		p.fovInterp = Interp(integer(n, "fovInterp"));
		p.easeProgress = boolean(n, "easeProgress");
		p.fixedAim = boolean(n, "fixedAim");
		p.aimTarget = vec(n, "aim");
		p.speed.clear();
		bool anchor = false, constraint = false, circle = false;
		for (auto & v : n.children)
		{
			if (!v.children.empty())
				fail("Unexpected nested path data");
			if (v.name == "anchor")
			{
				if (anchor)
					fail("Duplicate anchor");
				anchor = true;
				p.anchor = {vec(v, ""), quat(v), number(v, "scale", 1)};
			}
			else if (v.name == "constraints")
			{
				if (constraint)
					fail("Duplicate constraints");
				constraint = true;
				p.constraints.grid = number(v, "grid");
				p.constraints.locked = {{boolean(v, "lockX"), boolean(v, "lockY"), boolean(v, "lockZ")}};
				p.constraints.plane = vec(v, "");
			}
			else if (v.name == "circle")
			{
				if (circle)
					fail("Duplicate circle");
				circle = true;
				p.circleCenter = vec(v, "");
				p.circleRadius = number(v, "radius", 128);
				p.circleStart = number(v, "start");
				p.circleSweep = number(v, "sweep", 360);
				p.circlePlane = integer(v, "plane");
			}
			else if (v.name == "point")
			{
				Point pt;
				pt.pose = pose(v);
				pt.inHandle = vec(v, "in");
				pt.outHandle = vec(v, "out");
				pt.hold = number(v, "hold");
				pt.seconds = number(v, "seconds");
				pt.stop = boolean(v, "stop");
				p.points.push_back(pt);
			}
			else if (v.name == "rotation")
				p.rotations.push_back({number(v, "t", 0, true), quat(v)});
			else if (v.name == "speed" || v.name == "progress" || v.name == "fov")
			{
				auto & keys = v.name == "speed" ? p.speed : v.name == "progress" ? p.progress : p.fovs;
				keys.push_back({number(v, "t", 0, true), number(v, "value", 0, true)});
			}
			else
				fail("Unknown path data element");
		}
		if (p.speed.empty())
			p.speed = {{0, 0}, {1, 1}};
		s.paths.push_back(std::move(p));
	}
	Validate(s);
	return s;
}
std::string Editor::legacyXml() const
{
	const auto & p = active();
	if (p.points.empty())
		fail("Cannot export empty path");
	Writer w;
	w.begin("campath");
	w.text("positionInterp", "linear");
	w.text("rotationInterp", "slinear");
	w.text("fovInterp", "linear");
	w.a("hold", p.holdEnd);
	w.begin("points");
	int samples = (std::min)(20000, (std::max)(1, int(std::ceil(p.duration * 60))));
	for (int i = 0; i <= samples; ++i)
	{
		double time = p.duration * double(i) / samples;
		w.begin("p");
		w.a("t", p.start + time);
		w.p(p.evaluate(time));
		w.end();
	}
	w.end();
	w.end();
	return w.finish();
}
} // namespace cinematic
