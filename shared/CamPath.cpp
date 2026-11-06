#define _USE_MATH_DEFINES
#include "stdafx.h"

#include "CamPath.h"

#include "../deps/release/rapidxml/rapidxml.hpp"
#include "../deps/release/rapidxml/rapidxml_print.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <chrono>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <stdio.h>

#include <math.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace
{
bool ValidValue(const CamPathValue &v)
{
	return std::isfinite(v.HoldDuration) && v.HoldDuration >= 0 &&
		   (!v.HasEuler ||
			(std::isfinite(v.EulerPitch) && std::isfinite(v.EulerYaw) && std::isfinite(v.EulerRoll))) &&
		   std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z) && std::isfinite(v.Fov) &&
		   std::isfinite(v.TxIn) && std::isfinite(v.TxOut) && std::isfinite(v.TyIn) &&
		   std::isfinite(v.TyOut) && std::isfinite(v.TzIn) && std::isfinite(v.TzOut) &&
		   std::isfinite(v.TfovIn) && std::isfinite(v.TfovOut) && std::isfinite(v.TxWIn) &&
		   std::isfinite(v.TxWOut) && std::isfinite(v.TyWIn) && std::isfinite(v.TyWOut) &&
		   std::isfinite(v.TzWIn) && std::isfinite(v.TzWOut) && std::isfinite(v.TfovWIn) &&
		   std::isfinite(v.TfovWOut) && std::isfinite(v.TRxIn) && std::isfinite(v.TRxOut) &&
		   std::isfinite(v.TRyIn) && std::isfinite(v.TRyOut) && std::isfinite(v.TRzIn) &&
		   std::isfinite(v.TRzOut) && std::isfinite(v.TRxWIn) && std::isfinite(v.TRxWOut) &&
		   std::isfinite(v.TRyWIn) && std::isfinite(v.TRyWOut) && std::isfinite(v.TRzWIn) &&
		   std::isfinite(v.TRzWOut) && std::isfinite(v.R.W) && std::isfinite(v.R.X) && std::isfinite(v.R.Y) &&
		   std::isfinite(v.R.Z) && v.TxModeIn <= CamPath::TM_FREE && v.TxModeOut <= CamPath::TM_FREE &&
		   v.TyModeIn <= CamPath::TM_FREE && v.TyModeOut <= CamPath::TM_FREE &&
		   v.TzModeIn <= CamPath::TM_FREE && v.TzModeOut <= CamPath::TM_FREE &&
		   v.TfovModeIn <= CamPath::TM_FREE && v.TfovModeOut <= CamPath::TM_FREE &&
		   v.TRxModeIn <= CamPath::TM_FREE && v.TRxModeOut <= CamPath::TM_FREE &&
		   v.TRyModeIn <= CamPath::TM_FREE && v.TRyModeOut <= CamPath::TM_FREE &&
		   v.TRzModeIn <= CamPath::TM_FREE && v.TRzModeOut <= CamPath::TM_FREE && v.TxWIn >= 0 &&
		   v.TxWOut >= 0 && v.TyWIn >= 0 && v.TyWOut >= 0 && v.TzWIn >= 0 && v.TzWOut >= 0 &&
		   v.TfovWIn >= 0 && v.TfovWOut >= 0 && v.TRxWIn >= 0 && v.TRxWOut >= 0 && v.TRyWIn >= 0 &&
		   v.TRyWOut >= 0 && v.TRzWIn >= 0 && v.TRzWOut >= 0 && v.R.Norm() > 1e-12;
}
} // namespace

namespace
{
using ValueMember = double CamPathValue::*;
using ModeMember = unsigned char CamPathValue::*;
const ValueMember TangentIn[] = {&CamPathValue::TxIn,	&CamPathValue::TyIn,  &CamPathValue::TzIn,
								 &CamPathValue::TfovIn, &CamPathValue::TRyIn, &CamPathValue::TRzIn,
								 &CamPathValue::TRxIn};
const ValueMember TangentOut[] = {&CamPathValue::TxOut,	  &CamPathValue::TyOut,	 &CamPathValue::TzOut,
								  &CamPathValue::TfovOut, &CamPathValue::TRyOut, &CamPathValue::TRzOut,
								  &CamPathValue::TRxOut};
const ValueMember WeightIn[] = {&CamPathValue::TxWIn,	&CamPathValue::TyWIn,  &CamPathValue::TzWIn,
								&CamPathValue::TfovWIn, &CamPathValue::TRyWIn, &CamPathValue::TRzWIn,
								&CamPathValue::TRxWIn};
const ValueMember WeightOut[] = {&CamPathValue::TxWOut,	  &CamPathValue::TyWOut,  &CamPathValue::TzWOut,
								 &CamPathValue::TfovWOut, &CamPathValue::TRyWOut, &CamPathValue::TRzWOut,
								 &CamPathValue::TRxWOut};
const ModeMember ModeIn[] = {&CamPathValue::TxModeIn,	&CamPathValue::TyModeIn,  &CamPathValue::TzModeIn,
							 &CamPathValue::TfovModeIn, &CamPathValue::TRyModeIn, &CamPathValue::TRzModeIn,
							 &CamPathValue::TRxModeIn};
const ModeMember ModeOut[] = {&CamPathValue::TxModeOut,	 &CamPathValue::TyModeOut,
							  &CamPathValue::TzModeOut,	 &CamPathValue::TfovModeOut,
							  &CamPathValue::TRyModeOut, &CamPathValue::TRzModeOut,
							  &CamPathValue::TRxModeOut};
bool SamePose(const CamPathValue &a, const CamPathValue &b)
{
	return (!a.HasEuler || !b.HasEuler ||
			(a.EulerPitch == b.EulerPitch && a.EulerYaw == b.EulerYaw && a.EulerRoll == b.EulerRoll)) &&
		   a.X == b.X && a.Y == b.Y && a.Z == b.Z && a.Fov == b.Fov &&
		   std::abs(DotProduct(a.R, b.R) / (a.R.Norm() * b.R.Norm())) > 1.0 - 1e-12;
}
bool ValidKeys(const CInterpolationMap<CamPathValue> &keys)
{
	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		if (!std::isfinite(it->first) || !ValidValue(it->second) ||
			!std::isfinite(it->first + it->second.HoldDuration))
			return false;
		auto next = std::next(it);
		if (next != keys.end())
		{
			const double release = it->first + it->second.HoldDuration;
			if (release > next->first || (release == next->first && !SamePose(it->second, next->second)))
				return false;
		}
	}
	return true;
}
} // namespace

bool CamPath::HasKeyHolds() const
{
	for (const auto &key : m_Map)
		if (key.second.HoldDuration > 0)
			return true;
	return false;
}
bool CamPath::ReplaceKeys(const CInterpolationMap<CamPathValue> &keys)
{
	if (!ValidKeys(keys))
		return false;
	bool geometry = m_Map.size() != keys.size();
	if (!geometry)
	{
		auto previous = m_Map.begin();
		for (const auto &key : keys)
		{
			auto a = key.second, b = previous->second;
			a.Selected = b.Selected = false;
			if (key.first != previous->first || !(a == b))
			{
				geometry = true;
				break;
			}
			++previous;
		}
	}
	m_Map = keys;
	if (geometry)
		DoInterpolationMapChangedAll();
	else
		m_SelectedInterp->InterpolationMapChanged();
	Changed(geometry);
	return true;
}
bool CamPath::UpdateKey(double oldTime, double newTime, const CamPathValue &value)
{
	if (m_Map.find(oldTime) == m_Map.end() || (oldTime != newTime && m_Map.count(newTime)))
		return false;
	auto keys = m_Map;
	keys.erase(oldTime);
	keys[newTime] = value;
	return ReplaceKeys(keys);
}
bool CamPath::SetKeyHold(size_t index, double releaseTime)
{
	if (index >= m_Map.size() || !std::isfinite(releaseTime))
		return false;
	auto it = m_Map.begin();
	std::advance(it, index);
	if (releaseTime < it->first)
		return false;
	auto value = it->second;
	value.HoldDuration = releaseTime - it->first;
	return UpdateKey(it->first, it->first, value);
}
double CamPath::GetChannelValue(const CamPathValue &value, Channel channel)
{
	if (channel <= CH_FOV)
		return channel == CH_X ? value.X : channel == CH_Y ? value.Y : channel == CH_Z ? value.Z : value.Fov;
	auto angles = value.GetEulerAngles();
	return channel == CH_RPITCH ? angles.Pitch : channel == CH_RYAW ? angles.Yaw : angles.Roll;
}
void CamPath::SetChannelValue(CamPathValue &value, Channel channel, double component)
{
	if (channel == CH_X)
		value.X = component;
	else if (channel == CH_Y)
		value.Y = component;
	else if (channel == CH_Z)
		value.Z = component;
	else if (channel == CH_FOV)
		value.Fov = component;
	else
	{
		auto angles = value.GetEulerAngles();
		if (channel == CH_RPITCH)
			angles.Pitch = component;
		else if (channel == CH_RYAW)
			angles.Yaw = component;
		else
			angles.Roll = component;
		value.SetEulerAngles(angles);
	}
}
bool CamPath::BakeTangents()
{
	if (!CanEval() || (m_RotationInterpMethod != QI_CUSTOM && m_RotationInterpMethod != QI_SLINEAR))
		return false;
	// Classic quaternion splines cannot be represented exactly by Euler Beziers.
	if ((m_PositionInterpMethod != DI_CUSTOM && m_PositionInterpMethod != DI_LINEAR) ||
		(m_FovInterpMethod != DI_CUSTOM && m_FovInterpMethod != DI_LINEAR))
		return false;
	auto keys = m_Map;
	size_t index = 0;
	for (auto &key : keys)
	{
		if (m_RotationInterpMethod == QI_CUSTOM)
		{
			auto q = key.second.R;
			key.second.SetEulerAngles(
				static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(m_RInterp)->EvalAngles(
					key.first));
			key.second.R = q;
		}
		for (int ch = 0; ch < 7; ++ch)
		{
			double in, out, wi, wo;
			if (GetTangent((Channel)ch, index, false, in, wi) &&
				GetTangent((Channel)ch, index, true, out, wo))
			{
				key.second.*TangentIn[ch] = in;
				key.second.*TangentOut[ch] = out;
				key.second.*WeightIn[ch] = wi;
				key.second.*WeightOut[ch] = wo;
				key.second.*ModeIn[ch] = key.second.*ModeOut[ch] = TM_FREE;
			}
		}
		++index;
	}
	return ReplaceKeys(keys);
}
bool CamPath::Insert(double time)
{
	if (!std::isfinite(time) || !CanEval() || m_Map.count(time) || time <= GetLowerBound() ||
		time >= GetUpperBound())
		return false;
	const auto original = CaptureState();
	const double positionCurveTime = MapDistanceTime(time);
	const auto pose = Eval(time);
	if (!BakeTangents())
		return false;
	auto keys = m_Map;
	auto right = keys.upper_bound(time), left = std::prev(right);
	if (right == keys.end())
		left = std::prev(keys.end());
	const double release = left->first + left->second.HoldDuration;
	auto inserted = pose;
	inserted.Selected = true;
	if (time < release)
	{
		inserted.HoldDuration = release - time;
		left->second.HoldDuration = time - left->first;
		keys[time] = inserted;
		return ReplaceKeys(keys);
	}
	if (right == keys.end())
	{
		RestoreState(original);
		return false;
	}
	const double duration = right->first - release;
	// Subdivide each time/value Bezier at the requested time. Channels may have
	// different time weights and therefore different Bezier parameters.
	for (int ch = 0; ch < 7; ++ch)
	{
		const double curveTime = ch < 3 || m_DistanceTimingPose ? positionCurveTime : time;
		const bool custom = ch < 3	  ? m_PositionInterpMethod == DI_CUSTOM
							: ch == 3 ? m_FovInterpMethod == DI_CUSTOM
									  : m_RotationInterpMethod == QI_CUSTOM;
		if (!custom)
			continue;
		double wo = left->second.*WeightOut[ch], wi = right->second.*WeightIn[ch];
		ResolveHermiteWeights(wo, wi);
		struct Point
		{
			double t, v;
		};
		auto mix = [](Point a, Point b, double u)
		{ return Point{a.t + (b.t - a.t) * u, a.v + (b.v - a.v) * u}; };
		const double v0 = GetChannelValue(left->second, (Channel)ch),
					 v1 = GetChannelValue(right->second, (Channel)ch);
		Point p0{release, v0},
			p1{release + wo * duration / 3, v0 + wo * duration * (left->second.*TangentOut[ch]) / 3};
		Point p3{right->first, v1},
			p2{right->first - wi * duration / 3, v1 - wi * duration * (right->second.*TangentIn[ch]) / 3};
		double low = 0, high = 1;
		for (int i = 0; i < 60; ++i)
		{
			double u = (low + high) / 2;
			auto a = mix(p0, p1, u), b = mix(p1, p2, u), c = mix(p2, p3, u);
			if (mix(mix(a, b, u), mix(b, c, u), u).t < curveTime)
				low = u;
			else
				high = u;
		}
		const double u = (low + high) / 2;
		auto a = mix(p0, p1, u), b = mix(p1, p2, u), c = mix(p2, p3, u);
		auto d = mix(a, b, u), e = mix(b, c, u), middle = mix(d, e, u);
		SetChannelValue(inserted, (Channel)ch, middle.v);
		auto setHandle = [&](CamPathValue &value, bool outgoing, Point origin, Point handle,
							 double sourceSpan, double targetSpan)
		{
			const double dt = handle.t - origin.t;
			value.*(outgoing ? TangentOut[ch] : TangentIn[ch]) =
				std::abs(dt) > 1e-15 ? (handle.v - origin.v) / dt * sourceSpan / targetSpan : 0;
			value.*(outgoing ? WeightOut[ch] : WeightIn[ch]) = 3 * std::abs(dt) / sourceSpan;
			value.*(outgoing ? ModeOut[ch] : ModeIn[ch]) = TM_FREE;
		};
		setHandle(left->second, true, p0, a, curveTime - release, time - release);
		setHandle(inserted, false, middle, d, curveTime - release, time - release);
		setHandle(inserted, true, middle, e, right->first - curveTime, right->first - time);
		setHandle(right->second, false, p3, c, right->first - curveTime, right->first - time);
	}
	keys[time] = inserted;
	if (ReplaceKeys(keys))
		return true;
	RestoreState(original);
	return false;
}
double CamPath::GetSpeed(double time)
{
	if (!CanEval())
		return 0;
	const double eps = (std::max)(1e-6, (GetUpperBound() - GetLowerBound()) * 1e-5);
	const double a = (std::max)(GetLowerBound(), time - eps), b = (std::min)(GetUpperBound(), time + eps);
	if (b <= a)
		return 0;
	const auto left = Eval(a), right = Eval(b);
	const double x = right.X - left.X, y = right.Y - left.Y, z = right.Z - left.Z;
	return std::sqrt(x * x + y * y + z * z) / (b - a);
}

void CamPath::SetDistanceTiming(bool enabled)
{
	if (m_DistanceTiming == enabled)
		return;
	m_DistanceTiming = enabled;
	Changed();
}
void CamPath::SetDistanceTimingPose(bool enabled)
{
	if (m_DistanceTimingPose == enabled)
		return;
	m_DistanceTimingPose = enabled;
	Changed(false);
}
void CamPath::BuildArcs()
{
	if (!m_ArcDirty)
		return;
	m_ArcDirty = false;
	m_Arcs.clear();
	if (!CanEval() || m_Map.size() < 2)
		return;
	auto sample = [&](double time)
	{
		CamPathValue value;
		value.X = m_XInterp->Eval(time);
		value.Y = m_YInterp->Eval(time);
		value.Z = m_ZInterp->Eval(time);
		return value;
	};
	auto distance = [](const CamPathValue &a, const CamPathValue &b)
	{
		const double x = a.X - b.X, y = a.Y - b.Y, z = a.Z - b.Z;
		return std::sqrt(x * x + y * y + z * z);
	};
	for (auto left = m_Map.begin(), right = std::next(left); right != m_Map.end(); ++left, ++right)
	{
		ArcSegment arc;
		arc.Begin = left->first + left->second.HoldDuration;
		arc.End = right->first;
		arc.StopBefore = left->second.HoldDuration > 0;
		arc.StopAfter = right->second.HoldDuration > 0;
		arc.Times.push_back(arc.Begin);
		arc.Distances.push_back(0);
		if (arc.End > arc.Begin)
		{
			std::function<void(double, const CamPathValue &, double, const CamPathValue &, int)> subdivide;
			subdivide = [&](double a, const CamPathValue &va, double b, const CamPathValue &vb, int depth)
			{
				const double middle = (a + b) * 0.5;
				const auto vm = sample(middle);
				const double leftLength = distance(va, vm), rightLength = distance(vm, vb);
				const double direct = distance(va, vb), halves = leftLength + rightLength;
				if (depth < 9 && (halves - direct > (std::max)(1e-6, halves * 1e-5) ||
								  std::abs(leftLength - rightLength) > (std::max)(1e-6, halves * 0.002) ||
								  (b - a) > (arc.End - arc.Begin) / 64))
				{
					subdivide(a, va, middle, vm, depth + 1);
					subdivide(middle, vm, b, vb, depth + 1);
				}
				else
				{
					arc.Length += halves;
					arc.Times.push_back(b);
					arc.Distances.push_back(arc.Length);
				}
			};
			for (int part = 0; part < 8; ++part)
			{
				const double a = arc.Begin + (arc.End - arc.Begin) * part / 8;
				const double b = arc.Begin + (arc.End - arc.Begin) * (part + 1) / 8;
				subdivide(a, sample(a), b, sample(b), 0);
			}
		}
		m_Arcs.push_back(std::move(arc));
	}
}
double CamPath::MapDistanceTime(double time)
{
	if (!m_DistanceTiming)
		return time;
	BuildArcs();
	for (const auto &arc : m_Arcs)
		if (arc.Begin < time && time < arc.End && arc.Length > 1e-9)
		{
			double u = (time - arc.Begin) / (arc.End - arc.Begin);
			// Preserve smooth stops at holds without imposing stops at ordinary keys.
			if (arc.StopBefore && arc.StopAfter)
				u = u * u * (3 - 2 * u);
			else if (arc.StopBefore)
				u = u * u * (2 - u);
			else if (arc.StopAfter)
				u = u + u * u - u * u * u;
			const double target = arc.Length * u;
			auto found = std::lower_bound(arc.Distances.begin(), arc.Distances.end(), target);
			const size_t index = (size_t)std::distance(arc.Distances.begin(), found);
			if (!index || index >= arc.Times.size())
				return time;
			const double before = arc.Distances[index - 1], after = arc.Distances[index];
			const double factor = after > before ? (target - before) / (after - before) : 0;
			return arc.Times[index - 1] + factor * (arc.Times[index] - arc.Times[index - 1]);
		}
	return time;
}
bool CamPath::RetimeByDistance(double duration)
{
	if (!std::isfinite(duration) || duration <= 0 || m_Map.size() < 2 || !CanEval())
		return false;
	const auto original = CaptureState();
	if (!BakeTangents())
		return false;
	BuildArcs();
	double length = 0, fixed = 0;
	for (const auto &key : m_Map)
		fixed += key.second.HoldDuration;
	for (const auto &arc : m_Arcs)
		if (arc.Length > 1e-9)
			length += arc.Length;
		else
			fixed += arc.End - arc.Begin;
	const double travelDuration = duration - fixed;
	if (length <= 1e-9 || travelDuration <= 1e-9)
	{
		RestoreState(original);
		return false;
	}
	std::vector<double> arrival, oldSpan, newSpan;
	auto sourceKey = m_Map.begin();
	double cursor = GetLowerBound();
	arrival.push_back(cursor);
	for (const auto &arc : m_Arcs)
	{
		oldSpan.push_back(arc.End - arc.Begin);
		const double span = arc.Length > 1e-9 ? travelDuration * arc.Length / length : arc.End - arc.Begin;
		newSpan.push_back(span);
		cursor += sourceKey->second.HoldDuration;
		++sourceKey;
		cursor += span;
		arrival.push_back(cursor);
	}
	auto keys = m_Map;
	CInterpolationMap<CamPathValue> retimed;
	size_t index = 0;
	for (auto &key : keys)
	{
		for (int ch = 0; ch < 7; ++ch)
		{
			if (index && newSpan[index - 1] > 0)
				key.second.*TangentIn[ch] *= oldSpan[index - 1] / newSpan[index - 1];
			if (index < newSpan.size() && newSpan[index] > 0)
				key.second.*TangentOut[ch] *= oldSpan[index] / newSpan[index];
		}
		retimed.emplace(arrival[index++], key.second);
	}
	m_DistanceTiming = true;
	if (ReplaceKeys(retimed))
		return true;
	RestoreState(original);
	return false;
}

bool CamPath::DoubleInterp_FromString(char const * value, DoubleInterp & outValue)
{
	if(!_stricmp(value,"default"))
	{
		outValue = DI_DEFAULT;
		return true;
	}
	else
	if(!_stricmp(value,"linear"))
	{
		outValue = DI_LINEAR;
		return true;
	}
	else
	if(!_stricmp(value,"cubic"))
	{
		outValue = DI_CUBIC;
		return true;
	}
	else if (!_stricmp(value, "custom"))
	{
		outValue = DI_CUSTOM;
		return true;
	}

	return false;
}

char const * CamPath::DoubleInterp_ToString(DoubleInterp value)
{
	switch(value)
	{
	case DI_DEFAULT:
		return "default";
	case DI_LINEAR:
		return "linear";
	case DI_CUBIC:
		return "cubic";
	case DI_CUSTOM:
		return "custom";
	}

	return "[unkown]";
}

bool CamPath::QuaternionInterp_FromString(char const * value, QuaternionInterp & outValue)
{
	if(!_stricmp(value,"default"))
	{
		outValue = QI_DEFAULT;
		return true;
	}
	else
	if(!_stricmp(value,"sLinear"))
	{
		outValue = QI_SLINEAR;
		return true;
	}
	else
	if(!_stricmp(value,"sCubic"))
	{
		outValue = QI_SCUBIC;
		return true;
	}
	else if (!_stricmp(value, "custom"))
	{
		outValue = QI_CUSTOM;
		return true;
	}

	return false;
}

char const * CamPath::QuaternionInterp_ToString(QuaternionInterp value)
{
	switch(value)
	{
	case QI_DEFAULT:
		return "default";
	case QI_SLINEAR:
		return "sLinear";
	case QI_SCUBIC:
		return "sCubic";
	case QI_CUSTOM:
		return "custom";
	}

	return "[unkown]";
}

bool CamPath::TangentMode_FromString(char const *value, unsigned char &outValue)
{
	if (!_stricmp(value, "auto"))
	{
		outValue = (unsigned char)TM_AUTO;
		return true;
	}
	if (!_stricmp(value, "flat"))
	{
		outValue = (unsigned char)TM_FLAT;
		return true;
	}
	if (!_stricmp(value, "linear"))
	{
		outValue = (unsigned char)TM_LINEAR;
		return true;
	}
	if (!_stricmp(value, "free"))
	{
		outValue = (unsigned char)TM_FREE;
		return true;
	}
	return false;
}

char const *CamPath::TangentMode_ToString(unsigned char value)
{
	switch ((TangentMode)value)
	{
	case TM_AUTO:
		return "auto";
	case TM_FLAT:
		return "flat";
	case TM_LINEAR:
		return "linear";
	case TM_FREE:
		return "free";
	}
	return "[unknown]";
}

CamPathValue::CamPathValue()
	: X(0.0), Y(0.0), Z(0.0), R(), Fov(90.0), Selected(false), TxIn(0.0), TxOut(0.0), TxModeIn(0),
	  TxModeOut(0), TyIn(0.0), TyOut(0.0), TyModeIn(0), TyModeOut(0), TzIn(0.0), TzOut(0.0), TzModeIn(0),
	  TzModeOut(0), TfovIn(0.0), TfovOut(0.0), TfovModeIn(0), TfovModeOut(0), TxWIn(1.0), TxWOut(1.0),
	  TyWIn(1.0), TyWOut(1.0), TzWIn(1.0), TzWOut(1.0), TfovWIn(1.0), TfovWOut(1.0), TRxIn(0.0), TRxOut(0.0),
	  TRxModeIn(0), TRxModeOut(0), TRyIn(0.0), TRyOut(0.0), TRyModeIn(0), TRyModeOut(0), TRzIn(0.0),
	  TRzOut(0.0), TRzModeIn(0), TRzModeOut(0), TRxWIn(1.0), TRxWOut(1.0), TRyWIn(1.0), TRyWOut(1.0),
	  TRzWIn(1.0), TRzWOut(1.0)
{
}

CamPathValue::CamPathValue(double x, double y, double z, double pitch, double yaw, double roll, double fov)
	: X(x), Y(y), Z(z),
	  R(Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(pitch, yaw, roll)))),
	  Fov(fov), Selected(false), TxIn(0.0), TxOut(0.0), TxModeIn(0), TxModeOut(0), TyIn(0.0), TyOut(0.0),
	  TyModeIn(0), TyModeOut(0), TzIn(0.0), TzOut(0.0), TzModeIn(0), TzModeOut(0), TfovIn(0.0), TfovOut(0.0),
	  TfovModeIn(0), TfovModeOut(0), TxWIn(1.0), TxWOut(1.0), TyWIn(1.0), TyWOut(1.0), TzWIn(1.0),
	  TzWOut(1.0), TfovWIn(1.0), TfovWOut(1.0), TRxIn(0.0), TRxOut(0.0), TRxModeIn(0), TRxModeOut(0),
	  TRyIn(0.0), TRyOut(0.0), TRyModeIn(0), TRyModeOut(0), TRzIn(0.0), TRzOut(0.0), TRzModeIn(0),
	  TRzModeOut(0), TRxWIn(1.0), TRxWOut(1.0), TRyWIn(1.0), TRyWOut(1.0), TRzWIn(1.0), TRzWOut(1.0)
{
	if (std::abs(pitch) > 90 || std::abs(yaw) > 180 || std::abs(roll) > 180)
		SetEulerAngles(QEulerAngles(pitch, yaw, roll));
}

CamPathValue::CamPathValue(double x, double y, double z, double q_w, double q_x, double q_y, double q_z,
						   double fov, bool selected)
	: X(x), Y(y), Z(z), R(Quaternion(q_w, q_x, q_y, q_z)), Fov(fov), Selected(selected), TxIn(0.0),
	  TxOut(0.0), TxModeIn(0), TxModeOut(0), TyIn(0.0), TyOut(0.0), TyModeIn(0), TyModeOut(0), TzIn(0.0),
	  TzOut(0.0), TzModeIn(0), TzModeOut(0), TfovIn(0.0), TfovOut(0.0), TfovModeIn(0), TfovModeOut(0),
	  TxWIn(1.0), TxWOut(1.0), TyWIn(1.0), TyWOut(1.0), TzWIn(1.0), TzWOut(1.0), TfovWIn(1.0), TfovWOut(1.0),
	  TRxIn(0.0), TRxOut(0.0), TRxModeIn(0), TRxModeOut(0), TRyIn(0.0), TRyOut(0.0), TRyModeIn(0),
	  TRyModeOut(0), TRzIn(0.0), TRzOut(0.0), TRzModeIn(0), TRzModeOut(0), TRxWIn(1.0), TRxWOut(1.0),
	  TRyWIn(1.0), TRyWOut(1.0), TRzWIn(1.0), TRzWOut(1.0)
{
}

QEulerAngles CamPathValue::GetEulerAngles() const
{
	return HasEuler ? QEulerAngles(EulerPitch, EulerYaw, EulerRoll) : R.ToQREulerAngles().ToQEulerAngles();
}
void CamPathValue::SetEulerAngles(const QEulerAngles &angles)
{
	HasEuler = true;
	EulerPitch = angles.Pitch;
	EulerYaw = angles.Yaw;
	EulerRoll = angles.Roll;
	R = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(angles));
}

void CamPathValue::SetOrientation(const Quaternion &orientation)
{
	const auto result = orientation;
	if (!HasEuler)
	{
		R = result;
		return;
	}
	const auto previous = GetEulerAngles(), base = result.ToQREulerAngles().ToQEulerAngles();
	auto nearestEquivalent = [](double angle, double reference)
	{ return reference + std::remainder(angle - reference, 360.0); };
	auto candidate = [&](QEulerAngles a)
	{
		return QEulerAngles(nearestEquivalent(a.Pitch, previous.Pitch),
							nearestEquivalent(a.Yaw, previous.Yaw),
							nearestEquivalent(a.Roll, previous.Roll));
	};
	auto first = candidate(base),
		 second = candidate(QEulerAngles(180 - base.Pitch, base.Yaw + 180, base.Roll + 180));
	auto distance = [&](const QEulerAngles &a)
	{
		double p = a.Pitch - previous.Pitch, y = a.Yaw - previous.Yaw, r = a.Roll - previous.Roll;
		return p * p + y * y + r * r;
	};
	SetEulerAngles(distance(first) <= distance(second) ? first : second);
	R = result;
}

CamPathIterator::CamPathIterator(CInterpolationMap<CamPathValue>::const_iterator it) : wrapped(it)
{
}

double CamPathIterator::GetTime() const
{
	return wrapped->first;
}

CamPathValue CamPathIterator::GetValue() const
{
	return wrapped->second;
}

CamPathIterator& CamPathIterator::operator ++ ()
{
	wrapped++;
	return *this;
}

bool CamPathIterator::operator == (CamPathIterator const &it) const
{
	return wrapped == it.wrapped;
}

bool CamPathIterator::operator != (CamPathIterator const &it) const
{
	return !(*this == it);
}

// CamPath /////////////////////////////////////////////////////////////////////

CamPath::CamPath()
	: m_Offset(0), m_Enabled(false), m_PositionInterpMethod(DI_DEFAULT), m_RotationInterpMethod(QI_DEFAULT),
	  m_FovInterpMethod(DI_DEFAULT), m_XView(&m_EvalMap, XSelector), m_YView(&m_EvalMap, YSelector),
	  m_ZView(&m_EvalMap, ZSelector), m_RView(&m_EvalMap, RSelector), m_FovView(&m_EvalMap, FovSelector),
	  m_SelectedView(&m_Map, SelectedSelector)
{
	m_OnChangedIt = m_OnChanged.end();

	m_XInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_XView);
	m_YInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_YView);
	m_ZInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_ZView);
	m_RInterp = new CSCubicQuaternionInterpolation<CamPathValue>(&m_RView);
	m_FovInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_FovView);
	m_SelectedInterp = new CBoolAndInterpolation<CamPathValue>(&m_SelectedView);
}

CamPath::~CamPath()
{
	m_Map.clear();

	delete m_SelectedInterp;
	delete m_FovInterp;
	delete m_RInterp;
	delete m_ZInterp;
	delete m_YInterp;
	delete m_XInterp;
}

void CamPath::DoInterpolationMapChangedAll(void)
{
	m_ArcDirty = true;
	m_EvalMap.clear();
	for (auto &key : m_Map)
	{
		auto &value = key.second;
		if (value.HasEuler)
		{
			const auto authored =
				Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(value.GetEulerAngles()));
			if (std::abs(DotProduct(authored, value.R) / value.R.Norm()) < 1.0 - 1e-10)
				value.HasEuler = false;
		}
		m_EvalMap[key.first] = value;
	}
	for (const auto &key : m_Map)
		if (key.second.HoldDuration > 0 && std::isfinite(key.first + key.second.HoldDuration))
		{
			auto held = key.second;
			held.TxModeIn = held.TxModeOut = held.TyModeIn = held.TyModeOut = held.TzModeIn = held.TzModeOut =
				TM_FLAT;
			held.TfovModeIn = held.TfovModeOut = held.TRxModeIn = held.TRxModeOut = TM_FLAT;
			held.TRyModeIn = held.TRyModeOut = held.TRzModeIn = held.TRzModeOut = TM_FLAT;
			m_EvalMap[key.first] = held;
			m_EvalMap[key.first + held.HoldDuration] = held;
		}
	// Holds need editable endpoint derivatives; linear remains linear.
	if (HasKeyHolds())
	{
		if (m_PositionInterpMethod != DI_LINEAR && m_PositionInterpMethod != DI_CUSTOM)
			PositionInterpMethod_set(DI_CUSTOM);
		if (m_FovInterpMethod != DI_LINEAR && m_FovInterpMethod != DI_CUSTOM)
			FovInterpMethod_set(DI_CUSTOM);
		if (m_RotationInterpMethod != QI_SLINEAR && m_RotationInterpMethod != QI_CUSTOM)
			RotationInterpMethod_set(QI_CUSTOM);
	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();
	m_RInterp->InterpolationMapChanged();
	m_FovInterp->InterpolationMapChanged();
	m_SelectedInterp->InterpolationMapChanged();
}

void CamPath::Enabled_set(bool enable)
{
	m_Enabled = enable;
}

bool CamPath::Enabled_get(void) const
{
	return m_Enabled;
}

bool CamPath::GetHold(void) const
{
	return m_Hold;
}

void CamPath::SetHold(bool value)
{
	if (m_Hold == value)
		return;
	m_Hold = value;
	Changed();
}

bool CamPath::SetAutoTangentSettings(const AutoTangentSettings &settings)
{
	if (settings.Method < AutoTangentMethod::Spline || settings.Method >= AutoTangentMethod::Count ||
	    !std::isfinite(settings.Tension) || settings.Tension < 0 || settings.Tension > 1)
		return false;
	if (m_AutoTangentSettings == settings)
		return true;
	m_AutoTangentSettings = settings;
	DoInterpolationMapChangedAll();
	Changed();
	return true;
}

void CamPath::PositionInterpMethod_set(DoubleInterp value)
{
	if (HasKeyHolds() && value != DI_LINEAR)
		value = DI_CUSTOM;
	delete m_XInterp;
	delete m_YInterp;
	delete m_ZInterp;

	m_PositionInterpMethod = value;

	switch(value)
	{
	case DI_LINEAR:
		m_XInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_XView);
		m_YInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_YView);
		m_ZInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_ZView);
		break;
	case DI_CUSTOM:
		m_XInterp = new CHermiteDoubleInterpolation<CamPathValue>(
			&m_EvalMap, XSelector, XTanInSelector, XTanOutSelector, XTanModeInSelector, XTanModeOutSelector,
			XTanWInSelector, XTanWOutSelector, &m_AutoTangentSettings);

		m_YInterp = new CHermiteDoubleInterpolation<CamPathValue>(
			&m_EvalMap, YSelector, YTanInSelector, YTanOutSelector, YTanModeInSelector, YTanModeOutSelector,
			YTanWInSelector, YTanWOutSelector, &m_AutoTangentSettings);

		m_ZInterp = new CHermiteDoubleInterpolation<CamPathValue>(
			&m_EvalMap, ZSelector, ZTanInSelector, ZTanOutSelector, ZTanModeInSelector, ZTanModeOutSelector,
			ZTanWInSelector, ZTanWOutSelector, &m_AutoTangentSettings);

		break;
	default:
		m_XInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_XView);
		m_YInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_YView);
		m_ZInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_ZView);
		break;
	}

	Changed();
}

CamPath::DoubleInterp CamPath::PositionInterpMethod_get(void) const
{
	return m_PositionInterpMethod;
}

void CamPath::RotationInterpMethod_set(QuaternionInterp value)
{
	if (HasKeyHolds() && value != QI_SLINEAR)
		value = QI_CUSTOM;
	delete m_RInterp;

	m_RotationInterpMethod = value;

	switch (value)
	{
	case QI_SLINEAR:
		m_RInterp = new CSLinearQuaternionInterpolation<CamPathValue>(&m_RView);
		break;
	case QI_CUSTOM:
		// Euler-based Hermite interpolation with per-key tangents (pitch/yaw/roll)
		m_RInterp = new CEulerHermiteQuaternionInterpolation<CamPathValue>(
			&m_EvalMap,
			// Roll (X)
			RTanIn_Roll_Selector, RTanOut_Roll_Selector, RTanModeIn_Roll_Selector, RTanModeOut_Roll_Selector,
			RTanWIn_Roll_Selector, RTanWOut_Roll_Selector,
			// Pitch (Y)
			RTanIn_Pitch_Selector, RTanOut_Pitch_Selector, RTanModeIn_Pitch_Selector,
			RTanModeOut_Pitch_Selector, RTanWIn_Pitch_Selector, RTanWOut_Pitch_Selector,
			// Yaw (Z)
			RTanIn_Yaw_Selector, RTanOut_Yaw_Selector, RTanModeIn_Yaw_Selector, RTanModeOut_Yaw_Selector,
			RTanWIn_Yaw_Selector, RTanWOut_Yaw_Selector, &m_AutoTangentSettings, EulerSelector,
			ExactEulerSelector);
		break;
	default:
		m_RInterp = new CSCubicQuaternionInterpolation<CamPathValue>(&m_RView);
		break;
	}

	Changed();
}

CamPath::QuaternionInterp CamPath::RotationInterpMethod_get(void) const
{
	return m_RotationInterpMethod;
}

void CamPath::FovInterpMethod_set(DoubleInterp value)
{
	if (HasKeyHolds() && value != DI_LINEAR)
		value = DI_CUSTOM;
	delete m_FovInterp;

	m_FovInterpMethod = value;

	switch(value)
	{
	case DI_LINEAR:
		m_FovInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_FovView);
		break;
	case DI_CUSTOM:
		m_FovInterp = new CHermiteDoubleInterpolation<CamPathValue>(
			&m_EvalMap, FovSelector, FovTanInSelector, FovTanOutSelector, FovTanModeInSelector,
			FovTanModeOutSelector, FovTanWInSelector, FovTanWOutSelector, &m_AutoTangentSettings);

		break;
	default:
		m_FovInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_FovView);
		break;
	}

	Changed();
}

CamPath::DoubleInterp CamPath::FovInterpMethod_get(void) const
{
	return m_FovInterpMethod;
}

bool CamPathValue::operator==(const CamPathValue &other) const
{
	return HoldDuration == other.HoldDuration && HasEuler == other.HasEuler &&
		   (!HasEuler ||
			(EulerPitch == other.EulerPitch && EulerYaw == other.EulerYaw && EulerRoll == other.EulerRoll)) &&
		   X == other.X && Y == other.Y && Z == other.Z && Fov == other.Fov && Selected == other.Selected &&
		   TxIn == other.TxIn && TxOut == other.TxOut && TxModeIn == other.TxModeIn &&
		   TxModeOut == other.TxModeOut && TyIn == other.TyIn && TyOut == other.TyOut &&
		   TyModeIn == other.TyModeIn && TyModeOut == other.TyModeOut && TzIn == other.TzIn &&
		   TzOut == other.TzOut && TzModeIn == other.TzModeIn && TzModeOut == other.TzModeOut &&
		   TfovIn == other.TfovIn && TfovOut == other.TfovOut && TfovModeIn == other.TfovModeIn &&
		   TfovModeOut == other.TfovModeOut && TxWIn == other.TxWIn && TxWOut == other.TxWOut &&
		   TyWIn == other.TyWIn && TyWOut == other.TyWOut && TzWIn == other.TzWIn && TzWOut == other.TzWOut &&
		   TfovWIn == other.TfovWIn && TfovWOut == other.TfovWOut && TRxIn == other.TRxIn &&
		   TRxOut == other.TRxOut && TRxModeIn == other.TRxModeIn && TRxModeOut == other.TRxModeOut &&
		   TRyIn == other.TRyIn && TRyOut == other.TRyOut && TRyModeIn == other.TRyModeIn &&
		   TRyModeOut == other.TRyModeOut && TRzIn == other.TRzIn && TRzOut == other.TRzOut &&
		   TRzModeIn == other.TRzModeIn && TRzModeOut == other.TRzModeOut && TRxWIn == other.TRxWIn &&
		   TRxWOut == other.TRxWOut && TRyWIn == other.TRyWIn && TRyWOut == other.TRyWOut &&
		   TRzWIn == other.TRzWIn && TRzWOut == other.TRzWOut && R.W == other.R.W && R.X == other.R.X &&
		   R.Y == other.R.Y && R.Z == other.R.Z;
}
bool CamPath::State::operator==(const State &other) const
{
	return Keys == other.Keys && Position == other.Position && Fov == other.Fov &&
		   Rotation == other.Rotation && Enabled == other.Enabled && Hold == other.Hold &&
		   Offset == other.Offset && AutoTangents == other.AutoTangents &&
		   DistanceTiming == other.DistanceTiming && DistanceTimingPose == other.DistanceTimingPose;
}
CamPath::State CamPath::CaptureState() const
{
	return {m_Map,	  m_PositionInterpMethod, m_FovInterpMethod, m_RotationInterpMethod, m_Enabled, m_Hold,
			m_Offset, m_AutoTangentSettings,  m_DistanceTiming,	 m_DistanceTimingPose};
}
void CamPath::RestoreState(const State &state)
{
	m_Map = state.Keys;
	m_Offset = state.Offset;
	m_Enabled = state.Enabled;
	m_Hold = state.Hold;
	m_AutoTangentSettings = state.AutoTangents;
	m_DistanceTiming = state.DistanceTiming;
	m_DistanceTimingPose = state.DistanceTimingPose;
	PositionInterpMethod_set(state.Position);
	RotationInterpMethod_set(state.Rotation);
	FovInterpMethod_set(state.Fov);
	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::Add(double time, const CamPathValue &value)
{
	if (!std::isfinite(time) || !ValidValue(value))
		return;
	auto keys = m_Map;
	keys[time] = value;
	ReplaceKeys(keys);
}

void CamPath::Changed(bool geometry)
{
	++m_Revision;
	if (geometry)
	{
		++m_GeometryRevision;
		m_ArcDirty = true;
	}
	for(m_OnChangedIt = m_OnChanged.begin(); m_OnChangedIt != m_OnChanged.end(); m_OnChangedIt++) {
		m_OnChangedIt->Notify();
		if(m_OnChangedIt == m_OnChanged.end()) break;
	}
}

void CamPath::Remove(double time)
{
	m_Map.erase(time);
	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::Clear()
{
	bool selectAll = true;

	CInterpolationMap<CamPathValue>::iterator last = m_Map.end();
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end();)
	{
		CInterpolationMap<CamPathValue>::iterator itNext = it;
		++itNext;

		if(it->second.Selected)
		{
			selectAll = false;
			m_Map.erase(it);
		}

		it = itNext;
	}

	if(selectAll) m_Map.clear();

	m_Offset = 0;

	DoInterpolationMapChangedAll();
	Changed();
}

size_t CamPath::GetSize() const
{
	return m_Map.size();
}

CamPathIterator CamPath::GetBegin()
{
	return CamPathIterator(m_Map.begin());
}

CamPathIterator CamPath::GetEnd()
{
	return CamPathIterator(m_Map.end());
}

double CamPath::GetLowerBound() const
{
	return m_Map.cbegin()->first;
}

double CamPath::GetUpperBound() const
{
	return m_Map.rbegin()->first + m_Map.rbegin()->second.HoldDuration;
}

bool CamPath::CanEval(void) const
{
	if (m_Map.size() == 1 && HasKeyHolds())
		return true;
	return
		m_XInterp->CanEval()
		&& m_YInterp->CanEval()
		&& m_ZInterp->CanEval()
		&& m_RInterp->CanEval()
		&& m_FovInterp->CanEval()
		&& m_SelectedInterp->CanEval();
}

CamPathValue CamPath::EvalRaw(double t)
{
	if (m_Map.size() == 1)
		return m_Map.begin()->second;
	CamPathValue val;

	val.X = m_XInterp->Eval(t);
	val.Y = m_YInterp->Eval(t);
	val.Z = m_ZInterp->Eval(t);
	if (m_RotationInterpMethod == QI_CUSTOM)
		val.SetEulerAngles(
			static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(m_RInterp)->EvalAngles(t));
	else
		val.R = m_RInterp->Eval(t);
	val.Fov = m_FovInterp->Eval(t);
	val.Selected = m_SelectedInterp->Eval(t);

	return val;
}

CamPathValue CamPath::Eval(double time)
{
	if (!m_DistanceTiming || m_DistanceTimingPose)
		return EvalRaw(MapDistanceTime(time));
	auto value = EvalRaw(time);
	const double positionTime = MapDistanceTime(time);
	value.X = m_XInterp->Eval(positionTime);
	value.Y = m_YInterp->Eval(positionTime);
	value.Z = m_ZInterp->Eval(positionTime);
	return value;
}

char * double2xml(rapidxml::xml_document<> & doc, double value)
{
	char szTmp[196];
	_snprintf_s(szTmp, _TRUNCATE, "%.17g", value);
	return doc.allocate_string(szTmp);
}

bool CamPath::SaveToString(std::string &xmlString)
{
	rapidxml::xml_document<> doc;

	rapidxml::xml_node<> * decl = doc.allocate_node(rapidxml::node_declaration);
	decl->append_attribute(doc.allocate_attribute("version", "1.0"));
	decl->append_attribute(doc.allocate_attribute("encoding", "utf-8"));
	doc.append_node(decl);

	rapidxml::xml_node<> * cam = doc.allocate_node(rapidxml::node_element, "campath");
	if(DI_DEFAULT != m_PositionInterpMethod)
		cam->append_attribute(doc.allocate_attribute("positionInterp", DoubleInterp_ToString(m_PositionInterpMethod)));
	if(QI_DEFAULT != m_RotationInterpMethod)
		cam->append_attribute(doc.allocate_attribute("rotationInterp", QuaternionInterp_ToString(m_RotationInterpMethod)));
	if(DI_DEFAULT != m_FovInterpMethod)
		cam->append_attribute(doc.allocate_attribute("fovInterp", DoubleInterp_ToString(m_FovInterpMethod)));
	if (m_DistanceTiming)
		cam->append_attribute(doc.allocate_attribute("distanceTiming", "1"));
	// Missing mode in earlier files means the original whole-pose clock.
	cam->append_attribute(
		doc.allocate_attribute("distanceTimingMode", m_DistanceTimingPose ? "pose" : "position"));
	if (m_AutoTangentSettings.Method != AutoTangentMethod::Spline)
		cam->append_attribute(doc.allocate_attribute("autoInterp", AutoTangentMethodToString(m_AutoTangentSettings.Method)));
	if (m_AutoTangentSettings.Tension != 0)
		cam->append_attribute(doc.allocate_attribute("autoTension", double2xml(doc, m_AutoTangentSettings.Tension)));
	if (m_Offset)
		cam->append_attribute(doc.allocate_attribute("offset", double2xml(doc, m_Offset)));
	if (m_Hold)
		cam->append_attribute(doc.allocate_attribute("hold"));
	doc.append_node(cam);

	rapidxml::xml_node<> * pts = doc.allocate_node(rapidxml::node_element, "points");
	cam->append_node(pts);

	rapidxml::xml_node<> * cmt = doc.allocate_node(rapidxml::node_comment,0,
		"Points are in Quake coordinates, meaning x=forward, y=left, z=up and rotation order is first rx, then ry and lastly rz.\n"
		"Rotation direction follows the right-hand grip rule.\n"
		"rx (roll), ry (pitch), rz(yaw) are the Euler angles in degrees.\n"
		"qw, qx, qy, qz are the quaternion values.\n"
		"When read it is sufficient that either rx, ry, rz OR qw, qx, qy, qz are present.\n"
		"If both are present then qw, qx, qy, qz take precedence."
	);
	pts->append_node(cmt);

	for(CamPathIterator it = GetBegin(); it != GetEnd(); ++it)
	{
		double time = it.GetTime();
		CamPathValue val = it.GetValue();
		QEulerAngles ang = val.R.ToQREulerAngles().ToQEulerAngles();

		rapidxml::xml_node<> * pt = doc.allocate_node(rapidxml::node_element, "p");
		pt->append_attribute(doc.allocate_attribute("t", double2xml(doc,time)));
		pt->append_attribute(doc.allocate_attribute("x", double2xml(doc,val.X)));
		pt->append_attribute(doc.allocate_attribute("y", double2xml(doc,val.Y)));
		pt->append_attribute(doc.allocate_attribute("z", double2xml(doc,val.Z)));
		pt->append_attribute(doc.allocate_attribute("fov", double2xml(doc,val.Fov)));
		pt->append_attribute(doc.allocate_attribute("rx", double2xml(doc,ang.Roll)));
		pt->append_attribute(doc.allocate_attribute("ry", double2xml(doc,ang.Pitch)));
		pt->append_attribute(doc.allocate_attribute("rz", double2xml(doc,ang.Yaw)));
		pt->append_attribute(doc.allocate_attribute("qw", double2xml(doc,it.wrapped->second.R.W)));
		pt->append_attribute(doc.allocate_attribute("qx", double2xml(doc,it.wrapped->second.R.X)));
		pt->append_attribute(doc.allocate_attribute("qy", double2xml(doc,it.wrapped->second.R.Y)));
		pt->append_attribute(doc.allocate_attribute("qz", double2xml(doc,it.wrapped->second.R.Z)));

		// Tangents and modes (optional; always saved for completeness)
		pt->append_attribute(doc.allocate_attribute("tx_in", double2xml(doc, it.wrapped->second.TxIn)));
		pt->append_attribute(doc.allocate_attribute("tx_out", double2xml(doc, it.wrapped->second.TxOut)));
		pt->append_attribute(doc.allocate_attribute(
			"tx_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TxModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"tx_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TxModeOut))));

		pt->append_attribute(doc.allocate_attribute("ty_in", double2xml(doc, it.wrapped->second.TyIn)));
		pt->append_attribute(doc.allocate_attribute("ty_out", double2xml(doc, it.wrapped->second.TyOut)));
		pt->append_attribute(doc.allocate_attribute(
			"ty_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TyModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"ty_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TyModeOut))));

		pt->append_attribute(doc.allocate_attribute("tz_in", double2xml(doc, it.wrapped->second.TzIn)));
		pt->append_attribute(doc.allocate_attribute("tz_out", double2xml(doc, it.wrapped->second.TzOut)));
		pt->append_attribute(doc.allocate_attribute(
			"tz_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TzModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"tz_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TzModeOut))));

		pt->append_attribute(doc.allocate_attribute("tfov_in", double2xml(doc, it.wrapped->second.TfovIn)));
		pt->append_attribute(doc.allocate_attribute("tfov_out", double2xml(doc, it.wrapped->second.TfovOut)));
		pt->append_attribute(doc.allocate_attribute(
			"tfov_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TfovModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"tfov_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TfovModeOut))));

		pt->append_attribute(doc.allocate_attribute("tx_w_in", double2xml(doc, it.wrapped->second.TxWIn)));
		pt->append_attribute(doc.allocate_attribute("tx_w_out", double2xml(doc, it.wrapped->second.TxWOut)));

		pt->append_attribute(doc.allocate_attribute("ty_w_in", double2xml(doc, it.wrapped->second.TyWIn)));
		pt->append_attribute(doc.allocate_attribute("ty_w_out", double2xml(doc, it.wrapped->second.TyWOut)));

		pt->append_attribute(doc.allocate_attribute("tz_w_in", double2xml(doc, it.wrapped->second.TzWIn)));
		pt->append_attribute(doc.allocate_attribute("tz_w_out", double2xml(doc, it.wrapped->second.TzWOut)));

		pt->append_attribute(
			doc.allocate_attribute("tfov_w_in", double2xml(doc, it.wrapped->second.TfovWIn)));
		pt->append_attribute(
			doc.allocate_attribute("tfov_w_out", double2xml(doc, it.wrapped->second.TfovWOut)));

		// Rotation (Euler) tangent data for custom rotation interpolation
		pt->append_attribute(doc.allocate_attribute("trx_in", double2xml(doc, it.wrapped->second.TRxIn)));
		pt->append_attribute(doc.allocate_attribute("trx_out", double2xml(doc, it.wrapped->second.TRxOut)));
		pt->append_attribute(doc.allocate_attribute(
			"trx_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TRxModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"trx_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TRxModeOut))));
		pt->append_attribute(doc.allocate_attribute("trx_w_in", double2xml(doc, it.wrapped->second.TRxWIn)));
		pt->append_attribute(
			doc.allocate_attribute("trx_w_out", double2xml(doc, it.wrapped->second.TRxWOut)));

		pt->append_attribute(doc.allocate_attribute("try_in", double2xml(doc, it.wrapped->second.TRyIn)));
		pt->append_attribute(doc.allocate_attribute("try_out", double2xml(doc, it.wrapped->second.TRyOut)));
		pt->append_attribute(doc.allocate_attribute(
			"try_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TRyModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"try_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TRyModeOut))));
		pt->append_attribute(doc.allocate_attribute("try_w_in", double2xml(doc, it.wrapped->second.TRyWIn)));
		pt->append_attribute(
			doc.allocate_attribute("try_w_out", double2xml(doc, it.wrapped->second.TRyWOut)));

		pt->append_attribute(doc.allocate_attribute("trz_in", double2xml(doc, it.wrapped->second.TRzIn)));
		pt->append_attribute(doc.allocate_attribute("trz_out", double2xml(doc, it.wrapped->second.TRzOut)));
		pt->append_attribute(doc.allocate_attribute(
			"trz_mode_in", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TRzModeIn))));
		pt->append_attribute(doc.allocate_attribute(
			"trz_mode_out", doc.allocate_string(TangentMode_ToString(it.wrapped->second.TRzModeOut))));
		pt->append_attribute(doc.allocate_attribute("trz_w_in", double2xml(doc, it.wrapped->second.TRzWIn)));
		pt->append_attribute(
			doc.allocate_attribute("trz_w_out", double2xml(doc, it.wrapped->second.TRzWOut)));

		if (val.HoldDuration > 0)
			pt->append_attribute(doc.allocate_attribute("holdDuration", double2xml(doc, val.HoldDuration)));
		if (val.HasEuler)
		{
			pt->append_attribute(doc.allocate_attribute("eulerPitch", double2xml(doc, val.EulerPitch)));
			pt->append_attribute(doc.allocate_attribute("eulerYaw", double2xml(doc, val.EulerYaw)));
			pt->append_attribute(doc.allocate_attribute("eulerRoll", double2xml(doc, val.EulerRoll)));
		}

		if(val.Selected)
			pt->append_attribute(doc.allocate_attribute("selected"));

		pts->append_node(pt);
	}

	xmlString.clear();
	rapidxml::print(std::back_inserter(xmlString), doc);
	return true;
}

namespace
{
bool ReadPathFile(const std::filesystem::path &fileName, std::string &xml)
{
	std::ifstream file(fileName, std::ios::binary | std::ios::ate);
	if (!file)
		return false;
	const auto length = file.tellg();
	if (length < 0 || length > 64 * 1024 * 1024)
		return false;
	xml.resize((size_t)length);
	file.seekg(0);
	file.read(xml.data(), (std::streamsize)xml.size());
	return bool(file);
}
bool WritePathFile(const std::filesystem::path &fileName, const std::string &xml)
{
	if (xml.size() > 64 * 1024 * 1024)
		return false;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	auto temporary = fileName;
	temporary += ".hlae-writing-" + std::to_string(suffix);
	std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
	if (!file)
		return false;
	file.write(xml.data(), (std::streamsize)xml.size());
	file.flush();
	bool success = bool(file);
	file.close();
	std::error_code error;
	if (success)
	{
		std::filesystem::rename(temporary, fileName, error);
		success = !error;
	}
	if (!success)
		std::filesystem::remove(temporary, error);
	return success;
}
} // namespace
bool CamPath::Save(wchar_t const *fileName)
{
	std::string xml;
	return SaveToString(xml) && WritePathFile(std::filesystem::path(fileName), xml);
}
bool CamPath::ExportClassic(const wchar_t *fileName, double interval)
{
	if (m_Map.size() > 100000 || !CanEval() || !std::isfinite(interval) || interval <= 0 ||
		GetDuration() / interval > 100000)
		return false;
	std::map<double, CamPathValue> samples;
	const double begin = GetLowerBound(), end = GetUpperBound();
	for (size_t i = 0; i <= (size_t)std::ceil((end - begin) / interval); ++i)
	{
		const double time = (std::min)(end, begin + i * interval);
		samples[time] = Eval(time);
	}
	for (const auto &key : m_Map)
	{
		samples[key.first] = Eval(key.first);
		if (key.second.HoldDuration > 0)
			samples[key.first + key.second.HoldDuration] = Eval(key.first + key.second.HoldDuration);
	}
	if (samples.size() > 100000)
		return false;
	std::vector<double> intervals;
	for (const auto &sample : samples)
		intervals.push_back(sample.first);
	bool valid = true;
	std::function<void(double, double, int)> refine = [&](double a, double b, int depth)
	{
		const auto left = Eval(a), right = Eval(b);
		if (!ValidValue(left) || !ValidValue(right))
		{
			valid = false;
			return;
		}
		const auto la = left.GetEulerAngles(), ra = right.GetEulerAngles();
		const bool turns = (left.HasEuler && right.HasEuler) &&
						   ((std::max)({std::abs(ra.Pitch - la.Pitch), std::abs(ra.Yaw - la.Yaw),
										std::abs(ra.Roll - la.Roll)}) > 90);
		const double dot = std::abs(DotProduct(left.R, right.R) / (left.R.Norm() * right.R.Norm()));
		if (!turns && dot >= 0.70710678118)
			return;
		if (depth >= 20 || samples.size() >= 100000)
		{
			valid = false;
			return;
		}
		const double middle = (a + b) * 0.5;
		if (middle <= a || middle >= b)
		{
			valid = false;
			return;
		}
		samples[middle] = Eval(middle);
		refine(a, middle, depth + 1);
		refine(middle, b, depth + 1);
	};
	for (size_t i = 1; i < intervals.size() && valid; ++i)
		refine(intervals[i - 1], intervals[i], 0);
	if (!valid)
		return false;

	rapidxml::xml_document<> doc;
	auto *root = doc.allocate_node(rapidxml::node_element, "campath");
	doc.append_node(root);
	root->append_attribute(doc.allocate_attribute("positionInterp", "linear"));
	root->append_attribute(doc.allocate_attribute("rotationInterp", "sLinear"));
	root->append_attribute(doc.allocate_attribute("fovInterp", "linear"));
	if (m_Offset)
		root->append_attribute(doc.allocate_attribute("offset", double2xml(doc, m_Offset)));
	if (m_Hold)
		root->append_attribute(doc.allocate_attribute("hold"));
	auto *points = doc.allocate_node(rapidxml::node_element, "points");
	root->append_node(points);
	for (const auto &sample : samples)
	{
		auto *point = doc.allocate_node(rapidxml::node_element, "p");
		points->append_node(point);
		const auto &value = sample.second;
		const char *names[] = {"t", "x", "y", "z", "qw", "qx", "qy", "qz", "fov"};
		const double values[] = {sample.first, value.X,	  value.Y,	 value.Z,  value.R.W,
								 value.R.X,	   value.R.Y, value.R.Z, value.Fov};
		for (int i = 0; i < 9; ++i)
			point->append_attribute(doc.allocate_attribute(names[i], double2xml(doc, values[i])));
	}
	std::string xml;
	rapidxml::print(std::back_inserter(xml), doc);
	return WritePathFile(std::filesystem::path(fileName), xml);
}
bool CamPath::Load(wchar_t const *fileName)
{
	std::string xml;
	return ReadPathFile(std::filesystem::path(fileName), xml) && LoadFromString(xml);
}

bool CamPath::LoadFromString(const std::string &xml)
{
	auto backup = m_Map;
	const auto autoBackup = m_AutoTangentSettings;
	const bool distanceBackup = m_DistanceTiming;
	const bool distancePoseBackup = m_DistanceTimingPose;
	auto position = m_PositionInterpMethod;
	auto rotation = m_RotationInterpMethod;
	auto fov = m_FovInterpMethod;
	double offset = m_Offset;
	bool hold = m_Hold;
	bool ok = xml.size() <= 64 * 1024 * 1024 && LoadInternal(xml);
	if (!std::isfinite(m_Offset) || !ValidKeys(m_Map))
		ok = false;
	if (ok)
	{
		for (const auto &item : m_Map)
		{
			const auto &v = item.second;
			if (!std::isfinite(item.first) || !ValidValue(v))
			{
				ok = false;
				break;
			}
		}
	}
	if (!ok)
	{
		m_Map = std::move(backup);
		m_AutoTangentSettings = autoBackup;
		m_DistanceTiming = distanceBackup;
		m_DistanceTimingPose = distancePoseBackup;
		m_Offset = offset;
		m_Hold = hold;
		PositionInterpMethod_set(position);
		RotationInterpMethod_set(rotation);
		FovInterpMethod_set(fov);
		DoInterpolationMapChangedAll();
		Changed();
	}
	return ok;
}

bool CamPath::LoadInternal(std::string xml)
{
	bool bOk = true;
	xml.push_back(0);
	char *pData = xml.data();
	if(bOk)
	{
		try
		{
			do
			{
				rapidxml::xml_document<> doc;
				doc.parse<0>(pData);

				rapidxml::xml_node<> * cur_node = doc.first_node("campath");
				if (!cur_node)
				{
					bOk = false;
					break;
				}

				m_DistanceTiming = cur_node->first_attribute("distanceTiming") != nullptr;
				auto *distanceMode = cur_node->first_attribute("distanceTimingMode");
				if (distanceMode && strcmp(distanceMode->value(), "pose") &&
					strcmp(distanceMode->value(), "position"))
				{
					bOk = false;
					break;
				}
				m_DistanceTimingPose =
					distanceMode ? !strcmp(distanceMode->value(), "pose") : m_DistanceTiming;
				AutoTangentSettings autoSettings;
				if (auto *method = cur_node->first_attribute("autoInterp"))
					if (!AutoTangentMethodFromString(method->value(), autoSettings.Method))
					{ bOk = false; break; }
				if (auto *tension = cur_node->first_attribute("autoTension"))
				{
					char *end = nullptr;
					autoSettings.Tension = std::strtod(tension->value(), &end);
					if (end == tension->value() || *end || !std::isfinite(autoSettings.Tension) ||
						autoSettings.Tension < 0 || autoSettings.Tension > 1)
					{ bOk = false; break; }
				}
				SetAutoTangentSettings(autoSettings);

				// Clear current Campath:
				SelectNone();
				Clear();

				rapidxml::xml_attribute<> * positionInterpA = cur_node->first_attribute("positionInterp");
				DoubleInterp positionInterp = DI_DEFAULT;
				if(positionInterpA) DoubleInterp_FromString(positionInterpA->value(), positionInterp);
				PositionInterpMethod_set(positionInterp);

				rapidxml::xml_attribute<> * rotationInterpA = cur_node->first_attribute("rotationInterp");
				QuaternionInterp rotationInterp = QI_DEFAULT;
				if(rotationInterpA) QuaternionInterp_FromString(rotationInterpA->value(), rotationInterp);
				RotationInterpMethod_set(rotationInterp);

				rapidxml::xml_attribute<> * fovInterpA = cur_node->first_attribute("fovInterp");
				DoubleInterp fovInterp = DI_DEFAULT;
				if(fovInterpA) DoubleInterp_FromString(fovInterpA->value(), fovInterp);
				FovInterpMethod_set(fovInterp);

				rapidxml::xml_attribute<>* offsetA = cur_node->first_attribute("offset");
				double offset = offsetA ? atof(offsetA->value()) : 0.0;
				SetOffset(offset);

				rapidxml::xml_attribute<> * holdA = cur_node->first_attribute("hold");
				bool bHold = nullptr != holdA;
				SetHold(bHold);

				cur_node = cur_node->first_node("points");
				if (!cur_node)
				{
					bOk = false;
					break;
				}

				for(cur_node = cur_node->first_node("p"); cur_node; cur_node = cur_node->next_sibling("p"))
				{
					rapidxml::xml_attribute<> * timeAttr = cur_node->first_attribute("t");
					if(!timeAttr) continue;

					rapidxml::xml_attribute<> * xA = cur_node->first_attribute("x");
					rapidxml::xml_attribute<> * yA = cur_node->first_attribute("y");
					rapidxml::xml_attribute<> * zA = cur_node->first_attribute("z");
					rapidxml::xml_attribute<> * fovA = cur_node->first_attribute("fov");
					rapidxml::xml_attribute<> * rxA = cur_node->first_attribute("rx");
					rapidxml::xml_attribute<> * ryA = cur_node->first_attribute("ry");
					rapidxml::xml_attribute<> * rzA = cur_node->first_attribute("rz");
					rapidxml::xml_attribute<> * qwA = cur_node->first_attribute("qw");
					rapidxml::xml_attribute<> * qxA = cur_node->first_attribute("qx");
					rapidxml::xml_attribute<> * qyA = cur_node->first_attribute("qy");
					rapidxml::xml_attribute<> * qzA = cur_node->first_attribute("qz");
					rapidxml::xml_attribute<> * selectedA = cur_node->first_attribute("selected");

					double dT = atof(timeAttr->value());
					double dX = xA ? atof(xA->value()) : 0.0;
					double dY = yA ? atof(yA->value()) : 0.0;
					double dZ = zA ? atof(zA->value()) : 0.0;
					double dFov = fovA ? atof(fovA->value()) : 90.0;

					if(qwA && qxA && qyA && qzA)
					{
						CamPathValue r;
						r.X = dX;
						r.Y = dY;
						r.Z = dZ;
						r.R.W = atof(qwA->value());
						r.R.X = atof(qxA->value());
						r.R.Y = atof(qyA->value());
						r.R.Z = atof(qzA->value());
						r.Fov = dFov;
						r.Selected = 0 != selectedA;

						// Optional tangents/modes
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_in"))
							r.TxIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_out"))
							r.TxOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_mode_in"))
							TangentMode_FromString(a->value(), r.TxModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_mode_out"))
							TangentMode_FromString(a->value(), r.TxModeOut);

						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_in"))
							r.TyIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_out"))
							r.TyOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_mode_in"))
							TangentMode_FromString(a->value(), r.TyModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_mode_out"))
							TangentMode_FromString(a->value(), r.TyModeOut);

						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_in"))
							r.TzIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_out"))
							r.TzOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_mode_in"))
							TangentMode_FromString(a->value(), r.TzModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_mode_out"))
							TangentMode_FromString(a->value(), r.TzModeOut);

						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_in"))
							r.TfovIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_out"))
							r.TfovOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_mode_in"))
							TangentMode_FromString(a->value(), r.TfovModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_mode_out"))
							TangentMode_FromString(a->value(), r.TfovModeOut);

						if (auto *a = cur_node->first_attribute("tx_w_in"))
							r.TxWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("tx_w_out"))
							r.TxWOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("ty_w_in"))
							r.TyWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("ty_w_out"))
							r.TyWOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("tz_w_in"))
							r.TzWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("tz_w_out"))
							r.TzWOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("tfov_w_in"))
							r.TfovWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("tfov_w_out"))
							r.TfovWOut = atof(a->value());

						// Rotation (Euler) tangents / modes / weights (optional)
						if (auto *a = cur_node->first_attribute("trx_in"))
							r.TRxIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trx_out"))
							r.TRxOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("trx_mode_in"))
							TangentMode_FromString(a->value(), r.TRxModeIn);
						if (auto *a = cur_node->first_attribute("trx_mode_out"))
							TangentMode_FromString(a->value(), r.TRxModeOut);
						if (auto *a = cur_node->first_attribute("trx_w_in"))
							r.TRxWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trx_w_out"))
							r.TRxWOut = atof(a->value());

						if (auto *a = cur_node->first_attribute("try_in"))
							r.TRyIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("try_out"))
							r.TRyOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("try_mode_in"))
							TangentMode_FromString(a->value(), r.TRyModeIn);
						if (auto *a = cur_node->first_attribute("try_mode_out"))
							TangentMode_FromString(a->value(), r.TRyModeOut);
						if (auto *a = cur_node->first_attribute("try_w_in"))
							r.TRyWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("try_w_out"))
							r.TRyWOut = atof(a->value());

						if (auto *a = cur_node->first_attribute("trz_in"))
							r.TRzIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trz_out"))
							r.TRzOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("trz_mode_in"))
							TangentMode_FromString(a->value(), r.TRzModeIn);
						if (auto *a = cur_node->first_attribute("trz_mode_out"))
							TangentMode_FromString(a->value(), r.TRzModeOut);
						if (auto *a = cur_node->first_attribute("trz_w_in"))
							r.TRzWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trz_w_out"))
							r.TRzWOut = atof(a->value());

						auto readNumber = [](const char *text, double &value)
						{
							char *end = nullptr;
							value = std::strtod(text, &end);
							return end != text && !*end && std::isfinite(value);
						};
						if (auto *hold = cur_node->first_attribute("holdDuration"))
							if (!readNumber(hold->value(), r.HoldDuration))
							{
								bOk = false;
								break;
							}
						if (!cur_node->first_attribute("eulerPitch") &&
							(cur_node->first_attribute("eulerYaw") || cur_node->first_attribute("eulerRoll")))
						{
							bOk = false;
							break;
						}
						if (auto *pitch = cur_node->first_attribute("eulerPitch"))
						{
							auto *yaw = cur_node->first_attribute("eulerYaw"),
								 *roll = cur_node->first_attribute("eulerRoll");
							if (!yaw || !roll)
							{
								bOk = false;
								break;
							}
							auto storedRotation = r.R;
							double p, y, z;
							if (!readNumber(pitch->value(), p) || !readNumber(yaw->value(), y) ||
								!readNumber(roll->value(), z))
							{
								bOk = false;
								break;
							}
							r.SetEulerAngles(QEulerAngles(p, y, z));
							if (std::abs(DotProduct(r.R, storedRotation) / storedRotation.Norm()) >
								1.0 - 1e-10)
								r.R = storedRotation;
						}
						// Add point:
						m_Map[dT] = r;
					}
					else
					{
						double dRXroll = rxA ? atof(rxA->value()) : 0.0;
						double dRYpitch = ryA ? atof(ryA->value()) : 0.0;
						double dRZyaw = rzA ? atof(rzA->value()) : 0.0;

						CamPathValue r;
						r.X = dX;
						r.Y = dY;
						r.Z = dZ;
						r.R = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(dRYpitch, dRZyaw, dRXroll)));
						r.Fov = dFov;
						r.Selected = 0 != selectedA;

						// Optional tangents/modes
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_in"))
							r.TxIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_out"))
							r.TxOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_mode_in"))
							TangentMode_FromString(a->value(), r.TxModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tx_mode_out"))
							TangentMode_FromString(a->value(), r.TxModeOut);

						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_in"))
							r.TyIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_out"))
							r.TyOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_mode_in"))
							TangentMode_FromString(a->value(), r.TyModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("ty_mode_out"))
							TangentMode_FromString(a->value(), r.TyModeOut);

						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_in"))
							r.TzIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_out"))
							r.TzOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_mode_in"))
							TangentMode_FromString(a->value(), r.TzModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tz_mode_out"))
							TangentMode_FromString(a->value(), r.TzModeOut);

						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_in"))
							r.TfovIn = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_out"))
							r.TfovOut = atof(a->value());
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_mode_in"))
							TangentMode_FromString(a->value(), r.TfovModeIn);
						if (rapidxml::xml_attribute<> *a = cur_node->first_attribute("tfov_mode_out"))
							TangentMode_FromString(a->value(), r.TfovModeOut);

						if (auto *a = cur_node->first_attribute("tx_w_in"))
							r.TxWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("tx_w_out"))
							r.TxWOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("ty_w_in"))
							r.TyWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("ty_w_out"))
							r.TyWOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("tz_w_in"))
							r.TzWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("tz_w_out"))
							r.TzWOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("tfov_w_in"))
							r.TfovWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("tfov_w_out"))
							r.TfovWOut = atof(a->value());

						// Rotation (Euler) tangents / modes / weights (optional)
						if (auto *a = cur_node->first_attribute("trx_in"))
							r.TRxIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trx_out"))
							r.TRxOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("trx_mode_in"))
							TangentMode_FromString(a->value(), r.TRxModeIn);
						if (auto *a = cur_node->first_attribute("trx_mode_out"))
							TangentMode_FromString(a->value(), r.TRxModeOut);
						if (auto *a = cur_node->first_attribute("trx_w_in"))
							r.TRxWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trx_w_out"))
							r.TRxWOut = atof(a->value());

						if (auto *a = cur_node->first_attribute("try_in"))
							r.TRyIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("try_out"))
							r.TRyOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("try_mode_in"))
							TangentMode_FromString(a->value(), r.TRyModeIn);
						if (auto *a = cur_node->first_attribute("try_mode_out"))
							TangentMode_FromString(a->value(), r.TRyModeOut);

						if (auto *a = cur_node->first_attribute("try_w_in"))
							r.TRyWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("try_w_out"))
							r.TRyWOut = atof(a->value());

						if (auto *a = cur_node->first_attribute("trz_in"))
							r.TRzIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trz_out"))
							r.TRzOut = atof(a->value());
						if (auto *a = cur_node->first_attribute("trz_mode_in"))
							TangentMode_FromString(a->value(), r.TRzModeIn);
						if (auto *a = cur_node->first_attribute("trz_mode_out"))
							TangentMode_FromString(a->value(), r.TRzModeOut);
						if (auto *a = cur_node->first_attribute("trz_w_in"))
							r.TRzWIn = atof(a->value());
						if (auto *a = cur_node->first_attribute("trz_w_out"))
							r.TRzWOut = atof(a->value());

						auto readNumber = [](const char *text, double &value)
						{
							char *end = nullptr;
							value = std::strtod(text, &end);
							return end != text && !*end && std::isfinite(value);
						};
						if (auto *hold = cur_node->first_attribute("holdDuration"))
							if (!readNumber(hold->value(), r.HoldDuration))
							{
								bOk = false;
								break;
							}
						if (!cur_node->first_attribute("eulerPitch") &&
							(cur_node->first_attribute("eulerYaw") || cur_node->first_attribute("eulerRoll")))
						{
							bOk = false;
							break;
						}
						if (auto *pitch = cur_node->first_attribute("eulerPitch"))
						{
							auto *yaw = cur_node->first_attribute("eulerYaw"),
								 *roll = cur_node->first_attribute("eulerRoll");
							if (!yaw || !roll)
							{
								bOk = false;
								break;
							}
							auto storedRotation = r.R;
							double p, y, z;
							if (!readNumber(pitch->value(), p) || !readNumber(yaw->value(), y) ||
								!readNumber(roll->value(), z))
							{
								bOk = false;
								break;
							}
							r.SetEulerAngles(QEulerAngles(p, y, z));
							if (std::abs(DotProduct(r.R, storedRotation) / storedRotation.Norm()) >
								1.0 - 1e-10)
								r.R = storedRotation;
						}
						// Add point:
						m_Map[dT] = r;
					}
				}
			}
			while (false);
		}
		catch(rapidxml::parse_error &)
		{
			bOk=false;
		}
	}


	DoInterpolationMapChangedAll();
	Changed();

	return bOk;
}

size_t CamPath::SelectAll()
{
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = true;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed(false);

	return m_Map.size();
}

void CamPath::SelectNone()
{
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = false;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed(false);
}

size_t CamPath::SelectInvert()
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = !it->second.Selected;

		if(it->second.Selected) ++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed(false);

	return selected;
}

size_t CamPath::SelectAdd(size_t min, size_t max)
{
	size_t i = 0;
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= i && i <= max;

		if(it->second.Selected) ++selected;

		++i;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed(false);

	return selected;
}

size_t CamPath::SelectAdd(double min, size_t count)
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= it->first && selected < count;

		if(it->second.Selected) ++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed(false);

	return selected;
}

size_t CamPath::SelectAdd(double min, double max)
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= it->first && it->first <= max;

		if(it->second.Selected) ++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed(false);

	return selected;
}

void CamPath::SetStart(double t, bool relative)
{
	if (m_Map.empty() || !std::isfinite(t))
		return;
	bool all = true;
	double first = m_Map.begin()->first;
	for (const auto &key : m_Map)
		if (key.second.Selected)
		{
			all = false;
			first = key.first;
			break;
		}
	const double offset = relative ? t : t - first;
	CInterpolationMap<CamPathValue> keys;
	for (const auto &key : m_Map)
		if (!keys.emplace(key.first + ((all || key.second.Selected) ? offset : 0), key.second).second)
			return;
	ReplaceKeys(keys);
}

void CamPath::SetDuration(double t)
{
	if (m_Map.size() < 2 || !std::isfinite(t) || t <= 0)
		return;
	bool all = true;
	double first = 0, last = 0;
	for (const auto &key : m_Map)
		if (key.second.Selected)
		{
			if (all)
			{
				first = key.first;
				all = false;
			}
			last = key.first + key.second.HoldDuration;
		}
	if (all)
	{
		first = GetLowerBound();
		last = GetUpperBound();
	}
	if (last <= first)
		return;
	const double scale = t / (last - first);
	CInterpolationMap<CamPathValue> keys;
	for (auto key : m_Map)
	{
		double time = key.first;
		if (all || key.second.Selected)
		{
			time = first + (time - first) * scale;
			key.second.HoldDuration *= scale;
			for (int ch = 0; ch < 7; ++ch)
			{
				key.second.*TangentIn[ch] /= scale;
				key.second.*TangentOut[ch] /= scale;
			}
		}
		if (!keys.emplace(time, key.second).second)
			return;
	}
	ReplaceKeys(keys);
}

void CamPath::SetPosition(double x, double y, double z, bool setX, bool setY, bool setZ)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	// calcualte mid:

	double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
	bool first = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(selectAll || it->second.Selected)
		{
			CamPathValue curValue = it->second;

			if(first)
			{
				minX = curValue.X;
				minY = curValue.Y;
				minZ = curValue.Z;
				maxX = curValue.X;
				maxY = curValue.Y;
				maxZ = curValue.Z;
				first = false;
			}
			else
			{
				minX = std::min(minX, curValue.X);
				minY = std::min(minY, curValue.Y);
				minZ = std::min(minZ, curValue.Z);
				maxX = std::max(maxX, curValue.X);
				maxY = std::max(maxY, curValue.Y);
				maxZ = std::max(maxZ, curValue.Z);
			}
		}
	}

	double x0 = (maxX +minX) / 2;
	double y0 = (maxY +minY) / 2;
	double z0 = (maxZ +minZ) / 2;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			if(setX) curValue.X = x +(curValue.X -x0);
			if(setY) curValue.Y = y +(curValue.Y -y0);
			if(setZ) curValue.Z = z +(curValue.Z -z0);

			it->second = curValue;
		}
	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();

	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::SetAngles(double yPitch, double zYaw, double xRoll, bool setY, bool setZ, bool setX)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			const auto angles = curValue.GetEulerAngles();
			curValue.SetEulerAngles(QEulerAngles(setY ? yPitch : angles.Pitch, setZ ? zYaw : angles.Yaw,
												 setX ? xRoll : angles.Roll));

			it->second = curValue;
		}

	}

	m_RInterp->InterpolationMapChanged();

	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::SetFov(double fov)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			curValue.Fov = fov;

			it->second = curValue;
		}

	}

	m_FovInterp->InterpolationMapChanged();

	DoInterpolationMapChangedAll();
	Changed();
}

bool CamPath::GetTangent(Channel ch, size_t index, bool outgoing, double &slope, double &weight)
{
	if (index >= m_Map.size())
		return false;
	auto key = m_Map.begin();
	std::advance(key, index);
	const double time = key->first + (outgoing ? key->second.HoldDuration : 0.0);
	auto expanded = m_EvalMap.find(time);
	if (expanded == m_EvalMap.end())
		return false;
	index = (size_t)std::distance(m_EvalMap.begin(), expanded);

	if (ch >= CH_X && ch <= CH_FOV)
	{
		const bool custom =
		    ch == CH_FOV ? m_FovInterpMethod == DI_CUSTOM : m_PositionInterpMethod == DI_CUSTOM;
		if (!custom)
			return false;
		auto *interp = ch == CH_X ? m_XInterp : ch == CH_Y ? m_YInterp : ch == CH_Z ? m_ZInterp : m_FovInterp;
		return static_cast<CHermiteDoubleInterpolation<CamPathValue> *>(interp)->GetTangent(index, outgoing,
		                                                                                    slope, weight);
	}
	if (ch >= CH_RPITCH && ch <= CH_RROLL && m_RotationInterpMethod == QI_CUSTOM)
		return static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(m_RInterp)->GetTangent(
		    index, ch - CH_RPITCH, outgoing, slope, weight);
	return false;
}

void CamPath::SetTangent(Channel ch, bool setIn, bool setOut, double slopeIn, double slopeOut)
{
	if (!std::isfinite(slopeIn) || !std::isfinite(slopeOut))
		return;
	if (m_Map.size() < 1)
		return;

	bool selectAll = true;
	for (CInterpolationMap<CamPathValue>::const_iterator itc = m_Map.begin(); itc != m_Map.end(); ++itc)
	{
		if (itc->second.Selected)
		{
			selectAll = false;
			break;
		}
	}

	for (CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		CamPathValue cur = it->second;
		if (selectAll || cur.Selected)
		{
			switch (ch)
			{
			case CH_X:
				if (setIn)
					cur.TxIn = slopeIn;
				if (setOut)
					cur.TxOut = slopeOut;
				break;
			case CH_Y:
				if (setIn)
					cur.TyIn = slopeIn;
				if (setOut)
					cur.TyOut = slopeOut;
				break;
			case CH_Z:
				if (setIn)
					cur.TzIn = slopeIn;
				if (setOut)
					cur.TzOut = slopeOut;
				break;
			case CH_FOV:
				if (setIn)
					cur.TfovIn = slopeIn;
				if (setOut)
					cur.TfovOut = slopeOut;
				break;
			case CH_RPITCH:
				if (setIn)
					cur.TRyIn = slopeIn;
				if (setOut)
					cur.TRyOut = slopeOut;
				break;
			case CH_RYAW:
				if (setIn)
					cur.TRzIn = slopeIn;
				if (setOut)
					cur.TRzOut = slopeOut;
				break;
			case CH_RROLL:
				if (setIn)
					cur.TRxIn = slopeIn;
				if (setOut)
					cur.TRxOut = slopeOut;
				break;
			}
			it->second = cur;
		}
	}

	// Invalidate caches
	if (ch == CH_X || ch == CH_Y || ch == CH_Z)
	{
		if (m_PositionInterpMethod == DI_CUSTOM)
		{
			m_XInterp->InterpolationMapChanged();
			m_YInterp->InterpolationMapChanged();
			m_ZInterp->InterpolationMapChanged();
		}
	}
	if (ch == CH_FOV && m_FovInterpMethod == DI_CUSTOM)
	{
		m_FovInterp->InterpolationMapChanged();
	}
	// rotation custom -> update
	if ((ch == CH_RPITCH || ch == CH_RYAW || ch == CH_RROLL) && m_RotationInterpMethod == QI_CUSTOM)
	{
		m_RInterp->InterpolationMapChanged();
	}

	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::SetTangentMode(Channel ch, bool setIn, bool setOut, unsigned char mode)
{
	if (m_Map.size() < 1)
		return;

	bool selectAll = true;
	for (CInterpolationMap<CamPathValue>::const_iterator itc = m_Map.begin(); itc != m_Map.end(); ++itc)
	{
		if (itc->second.Selected)
		{
			selectAll = false;
			break;
		}
	}

	for (CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		CamPathValue cur = it->second;
		if (selectAll || cur.Selected)
		{
			switch (ch)
			{
			case CH_X:
				if (setIn)
					cur.TxModeIn = mode;
				if (setOut)
					cur.TxModeOut = mode;
				break;
			case CH_Y:
				if (setIn)
					cur.TyModeIn = mode;
				if (setOut)
					cur.TyModeOut = mode;
				break;
			case CH_Z:
				if (setIn)
					cur.TzModeIn = mode;
				if (setOut)
					cur.TzModeOut = mode;
				break;
			case CH_FOV:
				if (setIn)
					cur.TfovModeIn = mode;
				if (setOut)
					cur.TfovModeOut = mode;
				break;
			case CH_RPITCH:
				if (setIn)
					cur.TRyModeIn = mode;
				if (setOut)
					cur.TRyModeOut = mode;
				break;
			case CH_RYAW:
				if (setIn)
					cur.TRzModeIn = mode;
				if (setOut)
					cur.TRzModeOut = mode;
				break;
			case CH_RROLL:
				if (setIn)
					cur.TRxModeIn = mode;
				if (setOut)
					cur.TRxModeOut = mode;
				break;
			}
			it->second = cur;
		}
	}

	if (ch == CH_X || ch == CH_Y || ch == CH_Z)
	{
		if (m_PositionInterpMethod == DI_CUSTOM)
		{
			m_XInterp->InterpolationMapChanged();
			m_YInterp->InterpolationMapChanged();
			m_ZInterp->InterpolationMapChanged();
		}
	}
	if (ch == CH_FOV && m_FovInterpMethod == DI_CUSTOM)
	{
		m_FovInterp->InterpolationMapChanged();
	}

	if ((ch == CH_RPITCH || ch == CH_RYAW || ch == CH_RROLL) && m_RotationInterpMethod == QI_CUSTOM)
	{
		m_RInterp->InterpolationMapChanged();
	}

	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::SetTangentWeight(Channel ch, bool setIn, bool setOut, double wIn, double wOut)
{
	if (!std::isfinite(wIn) || !std::isfinite(wOut) || wIn < 0 || wOut < 0)
		return;
	if (m_Map.empty())
		return;

	bool selectAll = true;
	for (auto it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if (it->second.Selected)
		{
			selectAll = false;
			break;
		}
	}

	for (auto it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		CamPathValue cur = it->second;
		if (selectAll || cur.Selected)
		{
			switch (ch)
			{
			case CH_X:
				if (setIn)
					cur.TxWIn = wIn;
				if (setOut)
					cur.TxWOut = wOut;
				break;
			case CH_Y:
				if (setIn)
					cur.TyWIn = wIn;
				if (setOut)
					cur.TyWOut = wOut;
				break;
			case CH_Z:
				if (setIn)
					cur.TzWIn = wIn;
				if (setOut)
					cur.TzWOut = wOut;
				break;
			case CH_FOV:
				if (setIn)
					cur.TfovWIn = wIn;
				if (setOut)
					cur.TfovWOut = wOut;
				break;
			case CH_RPITCH:
				if (setIn)
					cur.TRyWIn = wIn;
				if (setOut)
					cur.TRyWOut = wOut;
				break;
			case CH_RYAW:
				if (setIn)
					cur.TRzWIn = wIn;
				if (setOut)
					cur.TRzWOut = wOut;
				break;
			case CH_RROLL:
				if (setIn)
					cur.TRxWIn = wIn;
				if (setOut)
					cur.TRxWOut = wOut;
				break;
			}
			it->second = cur;
		}
	}

	// weights affect only DI_CUSTOM paths (position/fov)
	if (ch == CH_X || ch == CH_Y || ch == CH_Z)
	{
		if (m_PositionInterpMethod == DI_CUSTOM)
		{
			m_XInterp->InterpolationMapChanged();
			m_YInterp->InterpolationMapChanged();
			m_ZInterp->InterpolationMapChanged();
		}
	}
	if (ch == CH_FOV && m_FovInterpMethod == DI_CUSTOM)
	{
		m_FovInterp->InterpolationMapChanged();
	}

	if ((ch == CH_RPITCH || ch == CH_RYAW || ch == CH_RROLL) && m_RotationInterpMethod == QI_CUSTOM)
	{
		m_RInterp->InterpolationMapChanged();
	}

	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::Rotate(double yPitch, double zYaw, double xRoll)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	// calcualte mid:

	double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
	bool first = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(selectAll || it->second.Selected)
		{
			CamPathValue curValue = it->second;

			if(first)
			{
				minX = curValue.X;
				minY = curValue.Y;
				minZ = curValue.Z;
				maxX = curValue.X;
				maxY = curValue.Y;
				maxZ = curValue.Z;
				first = false;
			}
			else
			{
				minX = std::min(minX, curValue.X);
				minY = std::min(minY, curValue.Y);
				minZ = std::min(minZ, curValue.Z);
				maxX = std::max(maxX, curValue.X);
				maxY = std::max(maxY, curValue.Y);
				maxZ = std::max(maxZ, curValue.Z);
			}
		}
	}

	double x0 = (maxX +minX) / 2;
	double y0 = (maxY +minY) / 2;
	double z0 = (maxZ +minZ) / 2;

	// build rotation matrix:
	double R[3][3];
	{
		double angle;
		double sr, sp, sy, cr, cp, cy;

		angle = zYaw * (M_PI*2 / 360);
		sy = sin(angle);
		cy = cos(angle);
		angle = yPitch * (M_PI*2 / 360);
		sp = sin(angle);
		cp = cos(angle);
		angle = xRoll * (M_PI*2 / 360);
		sr = sin(angle);
		cr = cos(angle);

		// R = YAW * (PITCH * ROLL)
		R[0][0] = cy*cp;
		R[0][1] = cy*sp*sr -sy*cr;
		R[0][2] = cy*sp*cr +sy*sr;
		R[1][0] = sy*cp;
		R[1][1] = sy*sp*sr +cy*cr;
		R[1][2] = sy*sp*cr +cy*-sr;
		R[2][0] = -sp;
		R[2][1] = cp*sr;
		R[2][2] = cp*cr;
	}
	Quaternion quatR = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(yPitch, zYaw, xRoll)));

	// rotate:

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			// update position:
			{
				// translate into origin:
				double x = curValue.X -x0;
				double y = curValue.Y -y0;
				double z = curValue.Z -z0;

				// rotate:
				double Rx = R[0][0]*x +R[0][1]*y +R[0][2]*z;
				double Ry = R[1][0]*x +R[1][1]*y +R[1][2]*z;
				double Rz = R[2][0]*x +R[2][1]*y +R[2][2]*z;

				// translate back:
				curValue.X = Rx +x0;
				curValue.Y = Ry +y0;
				curValue.Z = Rz +z0;
			}

			// update rotation:
			{
				curValue.SetOrientation(quatR * curValue.R);
			}

			// update:
			it->second = curValue;
		}

	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();
	m_RInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::AnchorTransform(double anchorX, double anchorY, double anchorZ, double anchorYPitch, double anchorZYaw, double anchorXRoll, double destX, double destY, double destZ, double destYPitch, double destZYaw, double destXRoll)
{
	if(m_Map.size()<1) return;

	bool selectAll = true;

	for(CInterpolationMap<CamPathValue>::const_iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				break;
			}
		}
	}

	Quaternion quatAnchor = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(anchorYPitch, anchorZYaw, anchorXRoll)));
	Quaternion quatDest = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(destYPitch, destZYaw, destXRoll)));

	// Make sure we take the shortest path:
	double dotProduct = DotProduct(quatDest, quatAnchor);
	if (dotProduct<0.0)
	{
		quatAnchor = -1.0 * quatAnchor;
	}

	Quaternion quatR = quatDest * (/*(1.0 / quatAnchor.Norm()) * */quatAnchor.Conjugate());

	QEulerAngles angles = quatR.ToQREulerAngles().ToQEulerAngles();

	double yPitch = angles.Pitch;
	double zYaw = angles.Yaw;
	double xRoll = angles.Roll;

	// build rotation matrix:
	double R[3][3];
	{
		double angle;
		double sr, sp, sy, cr, cp, cy;

		angle = zYaw * (M_PI*2 / 360);
		sy = sin(angle);
		cy = cos(angle);
		angle = yPitch * (M_PI*2 / 360);
		sp = sin(angle);
		cp = cos(angle);
		angle = xRoll * (M_PI*2 / 360);
		sr = sin(angle);
		cr = cos(angle);

		// R = YAW * (PITCH * ROLL)
		R[0][0] = cy*cp;
		R[0][1] = cy*sp*sr -sy*cr;
		R[0][2] = cy*sp*cr +sy*sr;
		R[1][0] = sy*cp;
		R[1][1] = sy*sp*sr +cy*cr;
		R[1][2] = sy*sp*cr +cy*-sr;
		R[2][0] = -sp;
		R[2][1] = cp*sr;
		R[2][2] = cp*cr;
	}

	// rotate:

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			// update position:
			{
				// translate into anchor:
				double x = curValue.X -anchorX;
				double y = curValue.Y -anchorY;
				double z = curValue.Z -anchorZ;

				// rotate:
				double Rx = R[0][0]*x +R[0][1]*y +R[0][2]*z;
				double Ry = R[1][0]*x +R[1][1]*y +R[1][2]*z;
				double Rz = R[2][0]*x +R[2][1]*y +R[2][2]*z;

				// translate into destination:
				curValue.X = Rx +destX;
				curValue.Y = Ry +destY;
				curValue.Z = Rz +destZ;
			}

			// update rotation:
			{
				curValue.SetOrientation(quatR * curValue.R);
			}

			// update:
			it->second = curValue;
		}

	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();
	m_RInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::CopyMap(CInterpolationMap<CamPathValue> & dst, CInterpolationMap<CamPathValue> & src)
{
	dst.clear();

	for(CInterpolationMap<CamPathValue>::const_iterator it = src.begin(); it != src.end(); ++it)
	{
		dst[it->first] = it->second;
	}
}

double CamPath::GetDuration() const
{
	if (m_Map.empty())
		return 0.0;

	return GetUpperBound() - GetLowerBound();
}

void CamPath::SetOffset(double value)
{
	m_Offset = value;

	Changed();
}

double CamPath::GetOffset() const
{
	return m_Offset;
}

void CamPath::OnChangedAdd(CamPathChanged pCamPathChanged, void * pUserData) {
	m_OnChanged.emplace_back(pCamPathChanged,pUserData);
}

void CamPath::OnChangedRemove(CamPathChanged pCamPathChanged, void * pUserData) {
	if(m_OnChangedIt != m_OnChanged.end()) {
		auto it = std::find(m_OnChanged.begin(), m_OnChanged.end(), CamPathChangedData(pCamPathChanged,pUserData));
		if(m_OnChangedIt == it)
			m_OnChangedIt = m_OnChanged.erase(it);
		else
			m_OnChanged.erase(it);
	}
}

namespace
{
bool SamePathContent(const CamPath::State &a, const CamPath::State &b)
{
	if (a.Position != b.Position || a.Fov != b.Fov || a.Rotation != b.Rotation || a.Hold != b.Hold ||
		a.Offset != b.Offset || !(a.AutoTangents == b.AutoTangents) || a.DistanceTiming != b.DistanceTiming ||
		a.DistanceTimingPose != b.DistanceTimingPose || a.Keys.size() != b.Keys.size())
		return false;
	auto right = b.Keys.begin();
	for (const auto &left : a.Keys)
	{
		if (left.first != right->first)
			return false;
		auto lv = left.second, rv = right->second;
		lv.Selected = rv.Selected = false;
		if (!(lv == rv))
			return false;
		++right;
	}
	return true;
}
rapidxml::xml_node<> *CloneOwnedNode(rapidxml::xml_document<> &doc, const rapidxml::xml_node<> *source)
{
	auto *copy =
		doc.allocate_node(source->type(), doc.allocate_string(source->name(), source->name_size() + 1),
						  doc.allocate_string(source->value(), source->value_size() + 1), source->name_size(),
						  source->value_size());
	for (auto *attr = source->first_attribute(); attr; attr = attr->next_attribute())
		copy->append_attribute(
			doc.allocate_attribute(doc.allocate_string(attr->name(), attr->name_size() + 1),
								   doc.allocate_string(attr->value(), attr->value_size() + 1),
								   attr->name_size(), attr->value_size()));
	for (auto *child = source->first_node(); child; child = child->next_sibling())
		copy->append_node(CloneOwnedNode(doc, child));
	return copy;
}
std::string UniquePathName(const CamPathCollection &paths)
{
	for (size_t i = 1;; ++i)
	{
		const auto candidate = "Path " + std::to_string(i);
		bool exists = false;
		for (size_t j = 0; j < paths.GetCount(); ++j)
			if (paths.GetName(j) == candidate)
				exists = true;
		if (!exists)
			return candidate;
	}
}
} // namespace
CamPathCollection::CamPathCollection(CamPath &active) : m_ActivePath(active)
{
	m_Paths.push_back({m_NextId++, "Path 1", std::make_shared<CamPath::State>(active.CaptureState()), {}});
	m_Saved = CaptureState();
}
const std::shared_ptr<const CamPath::State> &CamPathCollection::CurrentState() const
{
	if (!m_CurrentState || m_LastActiveRevision != m_ActivePath.GetRevision())
	{
		m_CurrentState = std::make_shared<CamPath::State>(m_ActivePath.CaptureState());
		m_LastActiveRevision = m_ActivePath.GetRevision();
	}
	return m_CurrentState;
}
void CamPathCollection::Sync()
{
	m_Paths[m_ActiveIndex].Data = CurrentState();
}
void CamPathCollection::Notify()
{
	// GetPath refreshes cached states by immutable state identity, including after reordering.
	m_ActivePath.Changed(false);
}
CamPathCollection::State CamPathCollection::CaptureState() const
{
	State state{m_ActiveIndex, m_Paths};
	state.Paths[state.Active].Data = CurrentState();
	return state;
}
void CamPathCollection::RestoreState(const State &state)
{
	if (state.Paths.empty() || state.Active >= state.Paths.size())
		return;
	const bool enabled = m_ActivePath.Enabled_get();
	m_Paths = state.Paths;
	m_ActiveIndex = state.Active;
	for (const auto &entry : m_Paths)
		m_NextId = (std::max)(m_NextId, entry.Id + 1);
	m_ActivePath.RestoreState(*m_Paths[m_ActiveIndex].Data);
	m_ActivePath.Enabled_set(enabled);
	Notify();
}
bool CamPathCollection::SameContent(const State &a, const State &b)
{
	if (a.Paths.size() != b.Paths.size())
		return false;
	for (size_t i = 0; i < a.Paths.size(); ++i)
		if (a.Paths[i].Id != b.Paths[i].Id || a.Paths[i].Name != b.Paths[i].Name ||
			a.Paths[i].Muted != b.Paths[i].Muted || a.Paths[i].Solo != b.Paths[i].Solo ||
			a.Paths[i].Visible != b.Paths[i].Visible ||
			(a.Paths[i].Data != b.Paths[i].Data && !SamePathContent(*a.Paths[i].Data, *b.Paths[i].Data)))
			return false;
	return true;
}
bool CamPathCollection::NameAvailable(const std::string &name, size_t except) const
{
	if (name.empty() || name.size() > 128)
		return false;
	for (unsigned char ch : name)
		if (ch < 32)
			return false;
	for (size_t i = 0; i < m_Paths.size(); ++i)
		if (i != except && !_stricmp(name.c_str(), m_Paths[i].Name.c_str()))
			return false;
	return true;
}
bool CamPathCollection::Select(size_t index)
{
	if (index >= m_Paths.size())
		return false;
	if (index == m_ActiveIndex)
		return true;
	Sync();
	const bool enabled = m_ActivePath.Enabled_get();
	m_ActiveIndex = index;
	m_ActivePath.RestoreState(*m_Paths[index].Data);
	m_ActivePath.Enabled_set(enabled);
	Notify();
	return true;
}
bool CamPathCollection::NewPath(const std::string &name, bool duplicate)
{
	const auto title = name.empty() ? UniquePathName(*this) : name;
	if (m_Paths.size() >= 128 || !NameAvailable(title, m_Paths.size()))
		return false;
	Sync();
	auto state = m_ActivePath.CaptureState();
	if (!duplicate)
	{
		state.Keys.clear();
		state.Offset = 0;
		state.Hold = false;
		state.DistanceTiming = false;
		state.DistanceTimingPose = false;
	}
	m_Paths.push_back({m_NextId++, title, std::make_shared<CamPath::State>(std::move(state)), {}});
	m_Paths.back().Muted = duplicate;
	m_Paths.back().Solo = false;
	return Select(m_Paths.size() - 1);
}
bool CamPathCollection::Rename(const std::string &name)
{
	if (!NameAvailable(name, m_ActiveIndex))
		return false;
	m_Paths[m_ActiveIndex].Name = name;
	Notify();
	return true;
}
bool CamPathCollection::Remove(size_t index)
{
	if (index >= m_Paths.size())
		return false;
	Sync();
	if (m_Paths.size() == 1)
	{
		auto empty = m_ActivePath.CaptureState();
		empty.Keys.clear();
		empty.Offset = 0;
		m_ActivePath.RestoreState(empty);
		m_Paths[0].FileName.clear();
		Notify();
		return true;
	}
	const bool enabled = m_ActivePath.Enabled_get();
	m_Paths.erase(m_Paths.begin() + index);
	if (m_ActiveIndex > index)
		--m_ActiveIndex;
	else if (m_ActiveIndex == index)
		m_ActiveIndex = (std::min)(index, m_Paths.size() - 1);
	m_ActivePath.RestoreState(*m_Paths[m_ActiveIndex].Data);
	m_ActivePath.Enabled_set(enabled);
	Notify();
	return true;
}
CamPath *CamPathCollection::GetPath(size_t index)
{
	if (index >= m_Paths.size())
		return nullptr;
	if (index == m_ActiveIndex)
		return &m_ActivePath;
	if (m_Caches.size() != m_Paths.size())
	{
		m_Caches.resize(m_Paths.size());
		m_CacheStates.resize(m_Paths.size());
	}
	if (!m_Caches[index] || m_CacheStates[index] != m_Paths[index].Data)
	{
		if (!m_Caches[index])
			m_Caches[index] = std::make_unique<CamPath>();
		m_Caches[index]->RestoreState(*m_Paths[index].Data);
		m_CacheStates[index] = m_Paths[index].Data;
	}
	m_Caches[index]->Enabled_set(m_ActivePath.Enabled_get());
	return m_Caches[index].get();
}
bool CamPathCollection::SetFlags(size_t index, bool muted, bool solo, bool visible)
{
	if (index >= m_Paths.size())
		return false;
	auto &entry = m_Paths[index];
	if (entry.Muted == muted && entry.Solo == solo && entry.Visible == visible)
		return true;
	entry.Muted = muted;
	entry.Solo = solo;
	entry.Visible = visible;
	m_ActivePath.Changed(false);
	return true;
}
size_t CamPathCollection::GetPlaybackIndex(double clientTime, bool respectEnabled)
{
	if (respectEnabled && !m_ActivePath.Enabled_get())
		return m_Paths.size();
	const auto &active = *CurrentState();
	bool solo = false;
	for (const auto &entry : m_Paths)
		solo |= entry.Solo;
	size_t best = m_Paths.size();
	double latest = -std::numeric_limits<double>::infinity();
	for (size_t i = 0; i < m_Paths.size(); ++i)
	{
		if (m_Paths[i].Muted || (solo && !m_Paths[i].Solo))
			continue;
		const auto &state = i == m_ActiveIndex ? active : *m_Paths[i].Data;
		if (state.Keys.empty())
			continue;
		const double start = state.Keys.begin()->first + state.Offset;
		const double end =
			state.Keys.rbegin()->first + state.Keys.rbegin()->second.HoldDuration + state.Offset;
		if (clientTime >= start && (clientTime <= end || state.Hold) && start >= latest &&
			GetPath(i)->CanEval())
		{
			best = i;
			latest = start;
		}
	}
	if (best == m_Paths.size() && m_Paths.size() == 1 && !m_Paths[0].Muted && active.Hold &&
		m_ActivePath.CanEval())
		return 0;
	return best;
}
CamPath *CamPathCollection::GetPlaybackPath(double clientTime)
{
	const auto index = GetPlaybackIndex(clientTime);
	return index < m_Paths.size() ? GetPath(index) : nullptr;
}
bool CamPathCollection::IsDirty(size_t index) const
{
	if (index >= m_Paths.size())
		return false;
	const auto &current = index == m_ActiveIndex ? *CurrentState() : *m_Paths[index].Data;
	for (const auto &saved : m_Saved.Paths)
		if (saved.Id == m_Paths[index].Id)
			return saved.Name != m_Paths[index].Name || saved.Muted != m_Paths[index].Muted ||
				   saved.Solo != m_Paths[index].Solo || saved.Visible != m_Paths[index].Visible ||
				   !SamePathContent(*saved.Data, current);
	return true;
}
bool CamPathCollection::IsDirty() const
{
	return !SameContent(CaptureState(), m_Saved);
}
void CamPathCollection::MarkSaved(bool all)
{
	Sync();
	if (all)
		m_Saved = CaptureState();
	else
	{
		bool found = false;
		for (auto &saved : m_Saved.Paths)
			if (saved.Id == m_Paths[m_ActiveIndex].Id)
			{
				saved = m_Paths[m_ActiveIndex];
				found = true;
				break;
			}
		if (!found)
			m_Saved.Paths.push_back(m_Paths[m_ActiveIndex]);
	}
}
bool CamPathCollection::SaveOne(const wchar_t *fileName)
{
	std::string xml;
	if (!m_ActivePath.SaveToString(xml))
		return false;
	try
	{
		rapidxml::xml_document<> doc;
		xml.push_back(0);
		doc.parse<0>(xml.data());
		auto *root = doc.first_node("campath");
		root->append_attribute(
			doc.allocate_attribute("name", doc.allocate_string(GetName(m_ActiveIndex).c_str())));
		const auto &entry = m_Paths[m_ActiveIndex];
		root->append_attribute(doc.allocate_attribute("muted", entry.Muted ? "1" : "0"));
		root->append_attribute(doc.allocate_attribute("solo", entry.Solo ? "1" : "0"));
		root->append_attribute(doc.allocate_attribute("visible", entry.Visible ? "1" : "0"));
		std::string output;
		rapidxml::print(std::back_inserter(output), doc);
		if (!WritePathFile(std::filesystem::path(fileName), output))
			return false;
	}
	catch (const rapidxml::parse_error &)
	{
		return false;
	}
	m_Paths[m_ActiveIndex].FileName = fileName;
	MarkSaved(false);
	return true;
}
bool CamPathCollection::LoadOne(const wchar_t *fileName)
{
	std::string xml;
	if (!ReadPathFile(std::filesystem::path(fileName), xml))
		return false;
	CamPath loaded;
	if (!loaded.LoadFromString(xml))
		return false;
	bool muted = false, solo = false, visible = true;
	std::string name = std::filesystem::path(fileName).stem().u8string();
	try
	{
		auto input = xml;
		input.push_back(0);
		rapidxml::xml_document<> doc;
		doc.parse<0>(input.data());
		if (auto *tag = doc.first_node("campath")->first_attribute("name"))
			name = tag->value();
		for (const char *flag : {"muted", "solo", "visible"})
			if (auto *attr = doc.first_node("campath")->first_attribute(flag))
			{
				if (strcmp(attr->value(), "0") && strcmp(attr->value(), "1"))
					return false;
				bool value = !strcmp(attr->value(), "1");
				if (!strcmp(flag, "muted"))
					muted = value;
				else if (!strcmp(flag, "solo"))
					solo = value;
				else
					visible = value;
			}
	}
	catch (const rapidxml::parse_error &)
	{
		return false;
	}
	if (!NameAvailable(name, m_ActiveIndex))
		name = UniquePathName(*this);
	const bool enabled = m_ActivePath.Enabled_get();
	m_ActivePath.RestoreState(loaded.CaptureState());
	m_ActivePath.Enabled_set(enabled);
	m_Paths[m_ActiveIndex].Name = name;
	m_Paths[m_ActiveIndex].FileName = fileName;
	SetFlags(m_ActiveIndex, muted, solo, visible);
	Notify();
	MarkSaved(false);
	return true;
}
bool CamPathCollection::SaveAll(const wchar_t *fileName, bool recovery)
{
	const auto state = CaptureState();
	rapidxml::xml_document<> doc;
	auto *root = doc.allocate_node(rapidxml::node_element, "campaths");
	root->append_attribute(doc.allocate_attribute("version", "1"));
	root->append_attribute(
		doc.allocate_attribute("active", doc.allocate_string(std::to_string(state.Active).c_str())));
	doc.append_node(root);
	for (const auto &entry : state.Paths)
	{
		CamPath path;
		path.RestoreState(*entry.Data);
		std::string xml;
		path.SaveToString(xml);
		xml.push_back(0);
		rapidxml::xml_document<> pathDoc;
		pathDoc.parse<0>(xml.data());
		auto *copy = CloneOwnedNode(doc, pathDoc.first_node("campath"));
		copy->append_attribute(doc.allocate_attribute("name", doc.allocate_string(entry.Name.c_str())));
		copy->append_attribute(doc.allocate_attribute("muted", entry.Muted ? "1" : "0"));
		copy->append_attribute(doc.allocate_attribute("solo", entry.Solo ? "1" : "0"));
		copy->append_attribute(doc.allocate_attribute("visible", entry.Visible ? "1" : "0"));
		copy->append_attribute(
			doc.allocate_attribute("id", doc.allocate_string(std::to_string(entry.Id).c_str())));
		root->append_node(copy);
	}
	std::string output;
	rapidxml::print(std::back_inserter(output), doc);
	if (output.size() > 64 * 1024 * 1024 || !WritePathFile(std::filesystem::path(fileName), output))
		return false;
	if (!recovery)
	{
		m_FileName = fileName;
		MarkSaved(true);
	}
	return true;
}
bool CamPathCollection::LoadAll(const wchar_t *fileName, bool recovery)
{
	std::string xml;
	if (!ReadPathFile(std::filesystem::path(fileName), xml))
		return false;
	State state;
	try
	{
		xml.push_back(0);
		rapidxml::xml_document<> doc;
		doc.parse<0>(xml.data());
		auto *root = doc.first_node("campaths");
		if (!root)
			return false;
		auto *version = root->first_attribute("version");
		if (!version || strcmp(version->value(), "1"))
			return false;
		if (auto *active = root->first_attribute("active"))
		{
			char *end = nullptr;
			auto value = std::strtoull(active->value(), &end, 10);
			if (end == active->value() || *end || value >= 128)
				return false;
			state.Active = (size_t)value;
		}
		for (auto *node = root->first_node("campath"); node; node = node->next_sibling("campath"))
		{
			if (state.Paths.size() >= 128)
				return false;
			auto *name = node->first_attribute("name");
			if (!name)
				return false;
			std::string title = name->value();
			if (title.empty() || title.size() > 128)
				return false;
			for (unsigned char ch : title)
				if (ch < 32)
					return false;
			for (const auto &entry : state.Paths)
				if (!_stricmp(entry.Name.c_str(), title.c_str()))
					return false;
			std::string data;
			rapidxml::print(std::back_inserter(data), *node);
			CamPath path;
			if (!path.LoadFromString(data))
				return false;
			unsigned long long id = state.Paths.size() + 1;
			if (auto *tag = node->first_attribute("id"))
			{
				char *end = nullptr;
				id = std::strtoull(tag->value(), &end, 10);
				if (end == tag->value() || *end || !id || id > 1000000000)
					return false;
			}
			for (const auto &entry : state.Paths)
				if (entry.Id == id)
					return false;
			state.Paths.push_back({id, title, std::make_shared<CamPath::State>(path.CaptureState()), {}});
			for (const char *flag : {"muted", "solo", "visible"})
				if (auto *attr = node->first_attribute(flag))
				{
					if (strcmp(attr->value(), "0") && strcmp(attr->value(), "1"))
						return false;
					bool value = !strcmp(attr->value(), "1");
					if (!strcmp(flag, "muted"))
						state.Paths.back().Muted = value;
					else if (!strcmp(flag, "solo"))
						state.Paths.back().Solo = value;
					else
						state.Paths.back().Visible = value;
				}
		}
	}
	catch (const rapidxml::parse_error &)
	{
		return false;
	}
	if (state.Paths.empty() || state.Active >= state.Paths.size())
		return false;
	RestoreState(state);
	if (recovery)
	{
		m_FileName.clear();
		m_Saved.Paths.clear();
	}
	else
	{
		m_FileName = fileName;
		MarkSaved(true);
	}
	return true;
}
void CamPathCollection::PushHistory(std::vector<State> &history, const State &state)
{
	history.push_back(state);
	size_t bytes = 0;
	for (const auto &snapshot : history)
		for (const auto &entry : snapshot.Paths)
			bytes += entry.Data->Keys.size() * (sizeof(CamPathValue) + 64) + entry.Name.size() + 256;
	while (history.size() > 1 && (history.size() > 64 || bytes > 64 * 1024 * 1024))
	{
		for (const auto &entry : history.front().Paths)
			bytes -= entry.Data->Keys.size() * (sizeof(CamPathValue) + 64) + entry.Name.size() + 256;
		history.erase(history.begin());
	}
}
void CamPathCollection::Observe()
{
	auto state = CaptureState();
	if (!m_HistoryReady)
	{
		m_Last = state;
		m_HistoryReady = true;
	}
	if (!SameContent(state, m_Last) && !m_Editing)
	{
		m_Before = m_Last;
		m_Editing = true;
	}
	m_Last = std::move(state);
}
void CamPathCollection::BeginEdit()
{
	Observe();
}
void CamPathCollection::FinishEdit()
{
	Observe();
	if (m_Editing && !SameContent(m_Before, m_Last))
	{
		PushHistory(m_Undo, m_Before);
		m_Redo.clear();
	}
	m_Editing = false;
}
bool CamPathCollection::CanUndo(bool redo) const
{
	return !(redo ? m_Redo : m_Undo).empty();
}
bool CamPathCollection::Undo(bool redo)
{
	FinishEdit();
	auto &from = redo ? m_Redo : m_Undo, &to = redo ? m_Undo : m_Redo;
	if (from.empty())
		return false;
	PushHistory(to, CaptureState());
	const auto state = from.back();
	from.pop_back();
	RestoreState(state);
	m_Last = CaptureState();
	return true;
}
