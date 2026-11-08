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
	for (int ch = 0; ch < 7; ++ch)
		if (!std::isfinite(v.AccelerationIn[ch]) || !std::isfinite(v.AccelerationOut[ch]))
			return false;
	return v.AccelerationMask <= 127 && std::isfinite(v.HoldDuration) && v.HoldDuration >= 0 &&
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
bool SamePosition(const CamPathValue &a, const CamPathValue &b)
{
	if (a.X != b.X || a.Y != b.Y || a.Z != b.Z || a.HoldDuration != b.HoldDuration ||
		(a.AccelerationMask & 7) != (b.AccelerationMask & 7))
		return false;
	for (int ch = 0; ch < 3; ++ch)
		if (a.*TangentIn[ch] != b.*TangentIn[ch] || a.*TangentOut[ch] != b.*TangentOut[ch] ||
			a.*ModeIn[ch] != b.*ModeIn[ch] || a.*ModeOut[ch] != b.*ModeOut[ch] ||
			a.*WeightIn[ch] != b.*WeightIn[ch] || a.*WeightOut[ch] != b.*WeightOut[ch] ||
			a.AccelerationIn[ch] != b.AccelerationIn[ch] || a.AccelerationOut[ch] != b.AccelerationOut[ch])
			return false;
	return true;
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
	bool geometry = m_Map.size() != keys.size(), position = geometry;
	if (!geometry)
	{
		auto previous = m_Map.begin();
		for (const auto &key : keys)
		{
			auto a = key.second, b = previous->second;
			a.Selected = b.Selected = false;
			position |= key.first != previous->first || !SamePosition(a, b);
			geometry |= key.first != previous->first || !(a == b);
			++previous;
		}
	}
	m_Map = keys;
	if (geometry)
		DoInterpolationMapChangedAll(position);
	else
		m_SelectedInterp->InterpolationMapChanged();
	Changed(geometry, position, position || (geometry && HasPositionModifiers()));
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
bool CamPath::Retime(const std::vector<double> &arrivals)
{
	if (arrivals.size() != m_Map.size())
		return false;
	size_t i = 0;
	bool moved = false;
	for (const auto &key : m_Map)
	{
		if (!std::isfinite(arrivals[i]) || (i && arrivals[i] <= arrivals[i - 1]))
			return false;
		moved |= arrivals[i++] != key.first;
	}
	if (!moved)
		return true;
	CamPath work;
	work.RestoreState(CaptureState());
	// Freeze effective derivatives before changing time. Keeping degrees/second
	// or units/second unchanged stretches handles and can create whole turns.
	if (m_RotationInterpMethod == QI_CUSTOM && work.CanEval() && !work.MakeScalarCurvesEditable())
		return false;
	if (work.CanEval())
		work.BakeTangents();
	auto keys = work.m_Map;
	CInterpolationMap<CamPathValue> result;
	i = 0;
	for (auto key = keys.begin(); key != keys.end(); ++key, ++i)
	{
		auto &value = key->second;
		if (m_RotationInterpMethod == QI_CUSTOM && work.m_RInterp->CanEval())
			value.SetEulerAngles(
			    static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(work.m_RInterp)
			        ->EvalAngles(key->first));
		for (int side = 0; side < 2; ++side)
		{
			if ((!side && !i) || (side && i + 1 == arrivals.size()))
				continue;
			const auto left = side ? key : std::prev(key), right = std::next(left);
			const size_t a = side ? i : i - 1;
			const double oldSpan = right->first - left->first - left->second.HoldDuration;
			const double newSpan = arrivals[a + 1] - arrivals[a] - left->second.HoldDuration;
			if (newSpan < 0 || (oldSpan > 0 && newSpan <= 0))
				return false;
			const double scale = newSpan > 0 ? oldSpan / newSpan : 1;
			for (int ch = 0; ch < 7; ++ch)
			{
				value.*(side ? TangentOut[ch] : TangentIn[ch]) *= scale;
				(side ? value.AccelerationOut[ch] : value.AccelerationIn[ch]) *= scale * scale;
			}
		}
		result.emplace(arrivals[i], value);
	}
	if (!work.ReplaceKeys(result))
		return false;
	if (work.m_PositionInterpMethod != m_PositionInterpMethod || work.m_FovInterpMethod != m_FovInterpMethod)
		RestoreState(work.CaptureState());
	else
		return ReplaceKeys(result);
	return true;
}

bool CamPath::AddPose(double time, const CamPathValue &pose)
{
	if (!std::isfinite(time) || !ValidValue(pose))
		return false;
	auto value = pose;
	// A captured camera orientation is not an authored revolution count. In
	// particular, Source/free-camera angles can accumulate past +/-180 degrees.
	value.HasEuler = false;
	if (!m_Map.empty())
	{
		// Resolve existing winding even if the user temporarily switched to a
		// quaternion backend. Preserve exact authored turns; unwrap other keys.
		auto previous = m_Map.begin();
		auto reference = previous->second.GetEulerAngles();
		for (auto next = std::next(previous); next != m_Map.end() && time > previous->first; ++next)
		{
			const auto angles = next->second.HasEuler
			                        ? next->second.GetEulerAngles()
			                        : NearestEquivalentEuler(next->second.GetEulerAngles(), reference);
			if (time <= next->first)
			{
				const double u = (time - previous->first) / (next->first - previous->first);
				reference = QEulerAngles(reference.Pitch + u * (angles.Pitch - reference.Pitch),
				                         reference.Yaw + u * (angles.Yaw - reference.Yaw),
				                         reference.Roll + u * (angles.Roll - reference.Roll));
				break;
			}
			reference = angles;
			previous = next;
		}
		if (m_RotationInterpMethod == QI_CUSTOM && m_RInterp->CanEval())
		{
			// Use raw rotation time: scalar minimum-key requirements, distance
			// timing and modifiers must not change this branch reference.
			const double referenceTime =
			    (std::max)(m_Map.begin()->first, (std::min)(time, m_Map.rbegin()->first));
			reference =
			    static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(m_RInterp)->EvalAngles(
			        referenceTime);
		}
		value.SetEulerAngles(reference);
		value.SetOrientation(pose.R);
	}
	auto keys = m_Map;
	keys[time] = value;
	return ReplaceKeys(keys);
}

bool CamPath::ReplacePose(double time, const CamPathValue &pose, unsigned int channels)
{
	auto found = m_Map.find(time);
	if (found == m_Map.end())
		return false;
	auto value = found->second;
	if ((channels & 0x70) && m_RotationInterpMethod == QI_CUSTOM && CanEval() && !value.HasEuler)
		value.SetEulerAngles(EvalUnmodified(time).GetEulerAngles());
	// Camera poses are physical orientations. Keep the existing Euler branch/winding
	// instead of treating a wrapped capture as an intentional full revolution.
	auto aligned = value;
	if (channels & 0x70)
	{
		aligned.SetEulerAngles(value.GetEulerAngles());
		aligned.SetOrientation(pose.R);
	}
	for (int ch = 0; ch < 4; ++ch)
		if (channels & (1u << ch))
			SetChannelValue(value, (Channel)ch, GetChannelValue(pose, (Channel)ch));
	if ((channels & 0x70) == 0x70)
		value.SetOrientation(pose.R);
	else
		for (int ch = 4; ch < 7; ++ch)
			if (channels & (1u << ch))
				SetChannelValue(value, (Channel)ch, GetChannelValue(aligned, (Channel)ch));
	return UpdateKey(time, time, value);
}
bool CamPath::DeleteSelected()
{
	auto keys = m_Map;
	for (auto it = keys.begin(); it != keys.end();)
		if (it->second.Selected)
			it = keys.erase(it);
		else
			++it;
	return keys.size() != m_Map.size() && ReplaceKeys(keys);
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
bool CamPath::MakeScalarCurvesEditable()
{
	const bool position = m_PositionInterpMethod != DI_CUSTOM && m_PositionInterpMethod != DI_LINEAR,
			   fov = m_FovInterpMethod != DI_CUSTOM && m_FovInterpMethod != DI_LINEAR;
	if (!position && !fov)
		return true;
	if ((position && !m_XInterp->CanEval()) || (fov && !m_FovInterp->CanEval()))
		return false;
	auto keys = m_Map;
	for (auto &key : keys)
		for (int ch = 0; ch < 4; ++ch)
			if (ch < 3 ? position : fov)
			{
				auto *interp = ch == 0 ? m_XInterp : ch == 1 ? m_YInterp : ch == 2 ? m_ZInterp : m_FovInterp;
				double acceleration;
				const double slope =
					static_cast<CCubicDoubleInterpolation<CamPathValue> *>(interp)->Derivative(key.first,
																							   &acceleration);
				key.second.*TangentIn[ch] = key.second.*TangentOut[ch] = slope;
				key.second.*WeightIn[ch] = key.second.*WeightOut[ch] = 1;
				key.second.*ModeIn[ch] = key.second.*ModeOut[ch] = TM_FREE;
				key.second.AccelerationMask |= 1u << ch;
				key.second.AccelerationIn[ch] = key.second.AccelerationOut[ch] = acceleration;
			}
	if (position)
		PositionInterpMethod_set(DI_CUSTOM);
	if (fov)
		FovInterpMethod_set(DI_CUSTOM);
	return ReplaceKeys(keys);
}
bool CamPath::BakeTangents()
{
	if (!CanEval())
		return false;
	auto keys = m_Map;
	CInterpolation<double> *scalars[] = {m_XInterp, m_YInterp, m_ZInterp, m_FovInterp};
	auto *rotation = m_RotationInterpMethod == QI_CUSTOM
						 ? static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(m_RInterp)
						 : nullptr;
	// Walk the expanded map once. Public index lookup is for individual editor
	// requests; using it for each channel of every key made baking quadratic.
	auto expanded = m_EvalMap.begin();
	size_t expandedIndex = 0;
	for (auto &key : keys)
	{
		while (expanded != m_EvalMap.end() && expanded->first < key.first)
		{
			++expanded;
			++expandedIndex;
		}
		const size_t arrivalIndex = expandedIndex;
		const double release = key.first + key.second.HoldDuration;
		while (expanded != m_EvalMap.end() && expanded->first < release)
		{
			++expanded;
			++expandedIndex;
		}
		const size_t releaseIndex = expandedIndex;
		if (rotation)
		{
			const auto quaternion = key.second.R;
			key.second.SetEulerAngles(rotation->EvalAngles(key.first));
			key.second.R = quaternion;
		}
		for (int ch = 0; ch < 7; ++ch)
		{
			if (ch < 3	  ? m_PositionInterpMethod != DI_CUSTOM
				: ch == 3 ? m_FovInterpMethod != DI_CUSTOM
						  : !rotation)
				continue;
			double incoming, outgoing, wi, wo, ai, ao;
			const bool tangents =
				ch < 4 ? static_cast<CHermiteDoubleInterpolation<CamPathValue> *>(scalars[ch])
								 ->GetTangent(arrivalIndex, false, incoming, wi) &&
							 static_cast<CHermiteDoubleInterpolation<CamPathValue> *>(scalars[ch])
								 ->GetTangent(releaseIndex, true, outgoing, wo)
					   : rotation->GetTangent(arrivalIndex, ch - 4, false, incoming, wi) &&
							 rotation->GetTangent(releaseIndex, ch - 4, true, outgoing, wo);
			if (!tangents)
				continue;
			key.second.*TangentIn[ch] = incoming;
			key.second.*TangentOut[ch] = outgoing;
			key.second.*WeightIn[ch] = wi;
			key.second.*WeightOut[ch] = wo;
			key.second.*ModeIn[ch] = key.second.*ModeOut[ch] = TM_FREE;
			const bool acceleration =
				ch < 4 ? static_cast<CHermiteDoubleInterpolation<CamPathValue> *>(scalars[ch])
								 ->GetAcceleration(arrivalIndex, false, ai) &&
							 static_cast<CHermiteDoubleInterpolation<CamPathValue> *>(scalars[ch])
								 ->GetAcceleration(releaseIndex, true, ao)
					   : rotation->GetAcceleration(arrivalIndex, ch - 4, false, ai) &&
							 rotation->GetAcceleration(releaseIndex, ch - 4, true, ao);
			if (acceleration)
			{
				key.second.AccelerationMask |= 1u << ch;
				key.second.AccelerationIn[ch] = ai;
				key.second.AccelerationOut[ch] = ao;
			}
			else
				key.second.AccelerationMask &= ~(1u << ch);
		}
	}
	return ReplaceKeys(keys);
}
bool CamPath::Insert(double time)
{
	if (!std::isfinite(time) || !CanEval() ||
		(m_RotationInterpMethod != QI_CUSTOM && m_RotationInterpMethod != QI_SLINEAR) || m_Map.count(time) ||
		time <= GetLowerBound() || time >= GetUpperBound())
		return false;
	const auto original = CaptureState();
	const double positionCurveTime = MapDistanceTime(time);
	const auto pose = EvalUnmodified(time);
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
		if (m_AutoTangentSettings.Method == AutoTangentMethod::MinimumJerk &&
			(left->second.AccelerationMask & (1u << ch)) && (right->second.AccelerationMask & (1u << ch)))
		{
			double velocity, acceleration;
			const double component = EvalQuinticHermite(
				(curveTime - release) / duration, duration, GetChannelValue(left->second, (Channel)ch),
				GetChannelValue(right->second, (Channel)ch), left->second.*TangentOut[ch],
				right->second.*TangentIn[ch], left->second.AccelerationOut[ch],
				right->second.AccelerationIn[ch], &velocity, &acceleration);
			SetChannelValue(inserted, (Channel)ch, component);
			inserted.AccelerationMask |= 1u << ch;
			const double scaleLeft = (curveTime - release) / (time - release),
						 scaleRight = (right->first - curveTime) / (right->first - time);
			inserted.*TangentIn[ch] = velocity * scaleLeft;
			inserted.*TangentOut[ch] = velocity * scaleRight;
			inserted.AccelerationIn[ch] = acceleration * scaleLeft * scaleLeft;
			inserted.AccelerationOut[ch] = acceleration * scaleRight * scaleRight;
			left->second.*TangentOut[ch] *= scaleLeft;
			left->second.AccelerationOut[ch] *= scaleLeft * scaleLeft;
			right->second.*TangentIn[ch] *= scaleRight;
			right->second.AccelerationIn[ch] *= scaleRight * scaleRight;
			inserted.*ModeIn[ch] = inserted.*ModeOut[ch] = TM_FREE;
			continue;
		}
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

std::vector<std::pair<double, CamPathValue>> CamPath::SampleInterval(double begin, double end,
																	 double tolerance, size_t maxPoints)
{
	std::vector<std::pair<double, CamPathValue>> result;
	if (!CanEval() || !std::isfinite(begin) || !std::isfinite(end) || end <= begin ||
		!std::isfinite(end - begin) || !std::isfinite(tolerance) || tolerance <= 0 || maxPoints < 2 ||
		maxPoints > 65536)
		return result;
	// Seed several subdivisions to catch symmetric loops; effects additionally
	// bound temporal sampling so flat endpoint/midpoint tests cannot hide waves.
	double seedCount = 8;
	for (const auto &m : m_Modifiers)
		if (m.Enabled && m.Begin < end && m.End > begin &&
			(m.Amplitude[0] != 0 || m.Amplitude[1] != 0 || m.Amplitude[2] != 0))
			seedCount = (std::max)(seedCount, std::ceil((end - begin) * ModifierSampleRate(m) * 8));
	const size_t seeds = (size_t)(std::min)(double(maxPoints - 1), seedCount);
	result.emplace_back(begin, Eval(begin));
	auto error = [](const CamPathValue &a, const CamPathValue &b, const CamPathValue &v, double u)
	{
		const double x = v.X - (a.X + (b.X - a.X) * u), y = v.Y - (a.Y + (b.Y - a.Y) * u),
					 z = v.Z - (a.Z + (b.Z - a.Z) * u);
		return std::sqrt(x * x + y * y + z * z);
	};
	std::function<void(double, const CamPathValue &, double, const CamPathValue &, size_t)> subdivide;
	subdivide = [&](double a, const CamPathValue &va, double b, const CamPathValue &vb, size_t budget)
	{
		if (budget > 1)
		{
			const double mid = a + (b - a) * .5;
			const auto vm = Eval(mid);
			if (error(va, vb, vm, .5) > tolerance ||
				error(va, vb, Eval(a + (b - a) * .25), .25) > tolerance ||
				error(va, vb, Eval(a + (b - a) * .75), .75) > tolerance)
			{
				subdivide(a, va, mid, vm, budget / 2);
				subdivide(mid, vm, b, vb, budget - budget / 2);
				return;
			}
		}
		result.emplace_back(b, vb);
	};
	for (size_t i = 0; i < seeds; ++i)
	{
		const auto a = result.back();
		const double b = begin + (end - begin) * (i + 1) / seeds;
		const size_t budget = (maxPoints - result.size()) / (seeds - i);
		subdivide(a.first, a.second, b, Eval(b), budget);
	}
	return result;
}

void CamPath::SetDistanceTiming(bool enabled)
{
	if (m_DistanceTiming == enabled)
		return;
	m_DistanceTiming = enabled;
	Changed(true, false);
}
void CamPath::SetDistanceTimingPose(bool enabled)
{
	if (m_DistanceTimingPose == enabled)
		return;
	m_DistanceTimingPose = enabled;
	Changed(true, false, false);
}
void CamPath::BuildArcs()
{
	if (!m_ArcDirty)
		return;
	m_ArcDirty = false;
	m_Arcs.clear();
	m_TravelDuration = 0;
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
				if (depth < 9 &&
					(halves - direct > (std::max)(1e-6, halves * 1e-5) ||
					 std::abs(leftLength - rightLength) > (std::max)(1e-6, halves * 0.002) || depth < 3))
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
		arc.TravelBegin = m_TravelDuration;
		if (arc.Length > 1e-9)
			m_TravelDuration += arc.End - arc.Begin;
		m_Arcs.push_back(std::move(arc));
	}
}
double CamPath::MapDistanceTime(double time)
{
	if (!m_DistanceTiming)
		return time;
	BuildArcs();
	auto foundArc = std::upper_bound(m_Arcs.begin(), m_Arcs.end(), time,
									 [](double t, const ArcSegment &arc) { return t < arc.Begin; });
	if (foundArc == m_Arcs.begin())
		return time;
	const auto &arc = *std::prev(foundArc);
	if (arc.Begin < time && time < arc.End && arc.Length > 1e-9)
	{
		double u = (time - arc.Begin) / (arc.End - arc.Begin);
		if (!m_SpeedProfile.empty() && m_TravelDuration > 0)
		{
			const double begin = ProfileDistance(arc.TravelBegin / m_TravelDuration),
						 end = ProfileDistance((arc.TravelBegin + arc.End - arc.Begin) / m_TravelDuration);
			u = end > begin
					? (ProfileDistance((arc.TravelBegin + time - arc.Begin) / m_TravelDuration) - begin) /
						  (end - begin)
					: u;
		}
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
		if (!index)
			return arc.Begin;
		if (index >= arc.Times.size())
			return arc.End;
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
	if (!MakeScalarCurvesEditable() || !BakeTangents())
	{
		RestoreState(original);
		return false;
	}
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
	double cumulativeLength = 0, previousProgress = 0;
	for (const auto &arc : m_Arcs)
	{
		oldSpan.push_back(arc.End - arc.Begin);
		cumulativeLength += arc.Length;
		const double progress = ProfileTime(cumulativeLength / length);
		const double span =
			arc.Length > 1e-9 ? travelDuration * (progress - previousProgress) : arc.End - arc.Begin;
		if (arc.Length > 1e-9)
			previousProgress = progress;
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
			{
				const double scale = oldSpan[index - 1] / newSpan[index - 1];
				key.second.*TangentIn[ch] *= scale;
				key.second.AccelerationIn[ch] *= scale * scale;
			}
			if (index < newSpan.size() && newSpan[index] > 0)
			{
				const double scale = oldSpan[index] / newSpan[index];
				key.second.*TangentOut[ch] *= scale;
				key.second.AccelerationOut[ch] *= scale * scale;
			}
		}
		if (!retimed.emplace(arrival[index++], key.second).second)
		{
			RestoreState(original);
			return false;
		}
	}
	m_DistanceTiming = true;
	auto arcs = std::move(m_Arcs);
	if (ReplaceKeys(retimed))
	{
		auto key = retimed.begin();
		m_TravelDuration = 0;
		for (size_t i = 0; i < arcs.size(); ++i, ++key)
		{
			auto &arc = arcs[i];
			const double previousBegin = arc.Begin;
			arc.Begin = key->first + key->second.HoldDuration;
			arc.End = std::next(key)->first;
			arc.TravelBegin = m_TravelDuration;
			if (arc.Length > 1e-9)
				m_TravelDuration += arc.End - arc.Begin;
			for (auto &time : arc.Times)
				time = arc.Begin + (oldSpan[i] > 0 ? (time - previousBegin) * newSpan[i] / oldSpan[i] : 0);
		}
		m_Arcs = std::move(arcs);
		m_ArcDirty = false;
		return true;
	}
	m_Arcs = std::move(arcs);
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
	SetEulerAngles(NearestEquivalentEuler(result.ToQREulerAngles().ToQEulerAngles(), GetEulerAngles()));
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

void CamPath::DoInterpolationMapChangedAll(bool invalidateArcs)
{
	if (invalidateArcs)
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
			held.AccelerationMask = 127;
			std::fill(held.AccelerationIn, held.AccelerationIn + 7, 0);
			std::fill(held.AccelerationOut, held.AccelerationOut + 7, 0);
			held.TxModeIn = held.TxModeOut = held.TyModeIn = held.TyModeOut = held.TzModeIn = held.TzModeOut =
				TM_FLAT;
			held.TfovModeIn = held.TfovModeOut = held.TRxModeIn = held.TRxModeOut = TM_FLAT;
			held.TRyModeIn = held.TRyModeOut = held.TRzModeIn = held.TRzModeOut = TM_FLAT;
			m_EvalMap[key.first] = held;
			m_EvalMap[key.first + held.HoldDuration] = held;
		}
	m_KeyTimes.clear();
	m_KeyArrival.clear();
	m_KeyRelease.clear();
	auto expanded = m_EvalMap.begin();
	size_t expandedIndex = 0;
	for (const auto &key : m_Map)
	{
		m_KeyTimes.push_back(key.first);
		while (expanded != m_EvalMap.end() && expanded->first < key.first)
		{
			++expanded;
			++expandedIndex;
		}
		m_KeyArrival.push_back(expandedIndex);
		while (expanded != m_EvalMap.end() && expanded->first < key.first + key.second.HoldDuration)
		{
			++expanded;
			++expandedIndex;
		}
		m_KeyRelease.push_back(expandedIndex);
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
			XTanWInSelector, XTanWOutSelector, &m_AutoTangentSettings, XAcceleration);

		m_YInterp = new CHermiteDoubleInterpolation<CamPathValue>(
			&m_EvalMap, YSelector, YTanInSelector, YTanOutSelector, YTanModeInSelector, YTanModeOutSelector,
			YTanWInSelector, YTanWOutSelector, &m_AutoTangentSettings, YAcceleration);

		m_ZInterp = new CHermiteDoubleInterpolation<CamPathValue>(
			&m_EvalMap, ZSelector, ZTanInSelector, ZTanOutSelector, ZTanModeInSelector, ZTanModeOutSelector,
			ZTanWInSelector, ZTanWOutSelector, &m_AutoTangentSettings, ZAcceleration);

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
			ExactEulerSelector, EulerAcceleration);
		break;
	default:
		m_RInterp = new CSCubicQuaternionInterpolation<CamPathValue>(&m_RView);
		break;
	}

	Changed(true, false, HasPositionModifiers());
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
			FovTanModeOutSelector, FovTanWInSelector, FovTanWOutSelector, &m_AutoTangentSettings,
			FovAcceleration);

		break;
	default:
		m_FovInterp = new CCubicDoubleInterpolation<CamPathValue>(&m_FovView);
		break;
	}

	Changed(true, false, false);
}

CamPath::DoubleInterp CamPath::FovInterpMethod_get(void) const
{
	return m_FovInterpMethod;
}

bool CamPathValue::operator==(const CamPathValue &other) const
{
	return AccelerationMask == other.AccelerationMask &&
		   std::equal(AccelerationIn, AccelerationIn + 7, other.AccelerationIn) &&
		   std::equal(AccelerationOut, AccelerationOut + 7, other.AccelerationOut) &&
		   HoldDuration == other.HoldDuration && HasEuler == other.HasEuler &&
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
		   DistanceTiming == other.DistanceTiming && DistanceTimingPose == other.DistanceTimingPose &&
		   SpeedProfile == other.SpeedProfile && Modifiers == other.Modifiers;
}
CamPath::State CamPath::CaptureState() const
{
	return {m_Map,
			m_PositionInterpMethod,
			m_FovInterpMethod,
			m_RotationInterpMethod,
			m_Enabled,
			m_Hold,
			m_Offset,
			m_AutoTangentSettings,
			m_DistanceTiming,
			m_DistanceTimingPose,
			m_SpeedProfile,
			m_Modifiers};
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
	m_SpeedProfile = state.SpeedProfile;
	m_Modifiers = state.Modifiers;
	m_SpeedDirty = true;
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

void CamPath::Changed(bool geometry, bool invalidateArcs, bool position, bool baseCurves)
{
	++m_Revision;
	if (geometry)
	{
		++m_GeometryRevision;
		if (baseCurves)
			++m_BaseRevision;
		if (position)
			++m_PositionRevision;
		if (invalidateArcs)
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

	if (m_Map.empty())
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

CamPathValue CamPath::EvalUnmodified(double time)
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

bool CamPath::Modifier::operator==(const Modifier &b) const
{
	if (Type != b.Type || Strength != b.Strength || Phase != b.Phase || Octaves != b.Octaves ||
	    Roughness != b.Roughness || Window != b.Window || Damping != b.Damping || Envelope != b.Envelope ||
	    Direction != b.Direction || Power != b.Power || Lower != b.Lower || Upper != b.Upper ||
		Softness != b.Softness || Rate != b.Rate || Scale != b.Scale || Offset != b.Offset || Pivot != b.Pivot || PingPong != b.PingPong)
		return false;
	if (Begin != b.Begin || End != b.End || FadeIn != b.FadeIn || FadeOut != b.FadeOut ||
		Frequency != b.Frequency || Seed != b.Seed || Enabled != b.Enabled || CameraSpace != b.CameraSpace)
		return false;
	for (int ch = 0; ch < 7; ++ch)
		if (Amplitude[ch] != b.Amplitude[ch])
			return false;
	return true;
}

bool CamPath::HasPositionModifiers() const
{
	for (const auto &m : m_Modifiers)
		if (m.Enabled && (m.Amplitude[0] != 0 || m.Amplitude[1] != 0 || m.Amplitude[2] != 0))
			return true;
	return false;
}
bool CamPath::ValidModifiers(const std::vector<NoiseModifier> &modifiers)
{
	if (modifiers.size() > 64)
		return false;
	size_t activeFilters = 0;
	for (const auto &m : modifiers)
	{
		if (m.Enabled && int(m.Type) >= 2 && ++activeFilters > 8)
			return false;
		if (int(m.Type) < 0 || int(m.Type) >= int(ModifierType::Count) || !std::isfinite(m.Strength) ||
		    m.Strength < 0 || m.Strength > 1 || !std::isfinite(m.Phase) || std::abs(m.Phase) > 1e6 ||
		    m.Octaves < 1 || m.Octaves > 8 || !std::isfinite(m.Roughness) || m.Roughness < 0 ||
		    m.Roughness > 1 || !std::isfinite(m.Window) || m.Window <= 0 || m.Window > 10 ||
		    !std::isfinite(m.Damping) || m.Damping < .05 || m.Damping > 2 || !std::isfinite(m.Power) ||
		    m.Power < 0 || m.Power > 16 || int(m.Direction) < 0 || int(m.Direction) > 2 ||
		    (IsTimingModifier(m.Type) && m.Frequency > 16) || m.Envelope.size() > 128 ||
			(int(m.Type) >= 2 && m.CameraSpace))
			return false;
		if (!std::isfinite(m.Lower) || !std::isfinite(m.Upper) || std::abs(m.Lower) > 1e9 ||
		    std::abs(m.Upper) > 1e9 || m.Lower > m.Upper || !std::isfinite(m.Softness) || m.Softness < 0 ||
		    m.Softness > (m.Upper - m.Lower) * .5 || !std::isfinite(m.Rate) || m.Rate <= 0 || m.Rate > 1e6 ||
		    !std::isfinite(m.Scale) || std::abs(m.Scale) > 1000 || !std::isfinite(m.Offset) ||
		    std::abs(m.Offset) > 1e6 || !std::isfinite(m.Pivot) || std::abs(m.Pivot) > 1e6)
			return false;
		if (!m.Envelope.empty() &&
			(m.Envelope.size() < 2 || m.Envelope.front().Time != 0 || m.Envelope.back().Time != 1))
			return false;
		for (size_t i = 0; i < m.Envelope.size(); ++i)
			if (!std::isfinite(m.Envelope[i].Time) || !std::isfinite(m.Envelope[i].Value) ||
				m.Envelope[i].Value < 0 || m.Envelope[i].Value > 1 ||
				(i && m.Envelope[i].Time <= m.Envelope[i - 1].Time))
				return false;
		if (!std::isfinite(ModifierSampleRate(m)) || (m.End - m.Begin) * ModifierSampleRate(m) > 1e12)
			return false;
		if (!std::isfinite(m.Begin) || !std::isfinite(m.End) || m.End <= m.Begin ||
			!std::isfinite(m.End - m.Begin) || !std::isfinite((m.End - m.Begin) * m.Frequency) ||
			(m.End - m.Begin) * m.Frequency > 1e12 || !std::isfinite(m.FadeIn) || !std::isfinite(m.FadeOut) ||
			m.FadeIn < 0 || m.FadeOut < 0 || m.FadeIn > m.End - m.Begin || m.FadeOut > m.End - m.Begin ||
			!std::isfinite(m.Frequency) || m.Frequency <= 0 || m.Frequency > 1000 || m.Seed > 1000000000)
			return false;
		for (double amplitude : m.Amplitude)
			if (!std::isfinite(amplitude) || std::abs(amplitude) > 1000000 ||
				(int(m.Type) >= 2 && (amplitude < 0 || amplitude > 1)))
				return false;
	}
	return true;
}

bool CamPath::SetModifiers(const std::vector<NoiseModifier> &modifiers)
{
	if (!ValidModifiers(modifiers))
		return false;
	if (modifiers == m_Modifiers)
		return true;
	auto dependentPosition = [](const auto &stack)
	{
		return std::any_of(stack.begin(), stack.end(),
						   [](const auto &m)
						   {
							   return m.Enabled && (m.CameraSpace || int(m.Type) >= 2) &&
									  (m.Amplitude[0] != 0 || m.Amplitude[1] != 0 || m.Amplitude[2] != 0);
						   });
	};
	auto positionOnly = [](const auto &stack)
	{
		std::vector<Modifier> result;
		for (auto m : stack)
			if (m.Enabled && (m.Amplitude[0] != 0 || m.Amplitude[1] != 0 || m.Amplitude[2] != 0))
			{
				for (int ch = 3; ch < 7; ++ch)
					m.Amplitude[ch] = 0;
				result.push_back(std::move(m));
			}
		return result;
	};
	// Camera-space translation depends on earlier angles; spatial filters share the sampling grid.
	const bool positionChanged = dependentPosition(m_Modifiers) || dependentPosition(modifiers) ||
								 positionOnly(m_Modifiers) != positionOnly(modifiers);
	m_Modifiers = modifiers;
	// Modifiers follow distance mapping; base spatial arcs remain unchanged.
	Changed(true, false, positionChanged, false);
	if (std::none_of(m_Modifiers.begin(), m_Modifiers.end(),
					 [](const auto &m) { return m.Enabled && int(m.Type) >= 2; }))
	{
		m_ModifierCurves.clear();
		std::vector<CamPathValue>().swap(m_ModifierInputValues);
		m_ModifierInputs.clear();
	}
	return true;
}
namespace
{
double SmoothStep5(double u)
{
	u = std::clamp(u, 0.0, 1.0);
	return u * u * u * (10 + u * (-15 + 6 * u));
}
double SmoothNoise(double time, unsigned int seed)
{
	// Hash integer lattice samples; no frame counters or mutable random generator.
	auto sample = [&](double cell)
	{
		const double wrapped = std::fmod(cell, 1048576.0);
		unsigned int x = static_cast<unsigned int>(wrapped < 0 ? wrapped + 1048576.0 : wrapped) ^ seed;
		x ^= x >> 16;
		x *= 0x7feb352du;
		x ^= x >> 15;
		x *= 0x846ca68bu;
		x ^= x >> 16;
		return double(x) / 4294967295.0 * 2 - 1;
	};
	const double cell = std::floor(time), u = SmoothStep5(time - cell);
	return sample(cell) + (sample(cell + 1) - sample(cell)) * u;
}
} // namespace
const char *CamPath::ModifierTypeName(ModifierType type)
{
	switch (type)
	{
	case ModifierType::Noise:
		return "noise";
	case ModifierType::Oscillation:
		return "oscillation";
	case ModifierType::Smooth:
		return "smooth";
	case ModifierType::Spring:
		return "spring";
	case ModifierType::Expo:
		return "expo";
	case ModifierType::Bounce:
		return "bounce";
	case ModifierType::Elastic:
		return "elastic";
	case ModifierType::Limits: return "limits";
	case ModifierType::Slew: return "slew";
	case ModifierType::Stepped: return "stepped";
	case ModifierType::Repeat: return "repeat";
	case ModifierType::Remap: return "remap";
	case ModifierType::Count:
		break;
	}
	return "unknown";
}
bool CamPath::ModifierTypeFromString(const char *name, ModifierType &type)
{
	for (int i = 0; i < int(ModifierType::Count); ++i)
		if (!_stricmp(name, ModifierTypeName((ModifierType)i)))
		{
			type = (ModifierType)i;
			return true;
		}
	return false;
}
double CamPath::ModifierEase(const Modifier &m, double progress)
{
	const double u = std::clamp(progress, 0.0, 1.0);
	if (u == 0 || u == 1)
		return u;
	// Each function is evaluated once across the range, never once per key.
	auto out = [&](double t) {
		if (m.Type == ModifierType::Expo)
			return m.Power < 1e-8 ? t : -std::expm1(-m.Power * t) / -std::expm1(-m.Power);
		if (m.Type == ModifierType::Bounce)
			return 1 - std::pow(1 - t, 1 + 3 * m.Damping) * std::abs(std::cos(M_PI * m.Frequency * t));
		if (m.Type == ModifierType::Elastic)
		{
			const double decay = 6 * m.Damping;
			return (1 - std::exp(-decay * t) * std::cos(2 * M_PI * m.Frequency * t)) /
			       (1 - std::exp(-decay) * std::cos(2 * M_PI * m.Frequency));
		}
		return t;
	};
	if (m.Direction == EaseDirection::Out)
		return out(u);
	if (m.Direction == EaseDirection::In)
		return 1 - out(1 - u);
	return u < .5 ? .5 * (1 - out(1 - 2 * u)) : .5 + .5 * out(2 * u - 1);
}

bool CamPath::MakeModifierPreset(const char *name, double begin, double end, Modifier &out)
{
	Modifier m;
	m.Begin = begin;
	m.End = end;
	m.FadeIn = m.FadeOut = (std::max)(0.0, (end - begin) * .15);
	if (!_stricmp(name, "handheld"))
	{
		m.Frequency = 2;
		m.Octaves = 3;
		m.CameraSpace = true;
		m.Amplitude[0] = .3;
		m.Amplitude[1] = .2;
		m.Amplitude[2] = .15;
		m.Amplitude[4] = .2;
		m.Amplitude[5] = .35;
		m.Amplitude[6] = .2;
	}
	else if (!_stricmp(name, "impact"))
	{
		m.Type = ModifierType::Oscillation;
		m.Frequency = 6;
		m.Amplitude[2] = .8;
		m.Amplitude[3] = 2.5;
		m.Amplitude[4] = 1.2;
		m.Envelope = {{0, 0}, {.08, 1}, {.35, .3}, {1, 0}};
	}
	else if (!_stricmp(name, "fov-punch"))
	{
		m.Type = ModifierType::Oscillation;
		m.Frequency = .5 / (end - begin);
		m.Amplitude[3] = 8;
	}
	else if (!_stricmp(name, "stabilize"))
	{
		m.Type = ModifierType::Smooth;
		m.Window = .25;
		m.Amplitude[4] = m.Amplitude[5] = m.Amplitude[6] = .6;
	}
	else if (!_stricmp(name, "expo-in") || !_stricmp(name, "expo-out") || !_stricmp(name, "expo-in-out"))
	{
		m.Type = ModifierType::Expo;
		m.Direction = !_stricmp(name, "expo-in")
		                  ? EaseDirection::In
		                  : (!_stricmp(name, "expo-out") ? EaseDirection::Out : EaseDirection::InOut);
		std::fill(m.Amplitude, m.Amplitude + 7, 1);
	}
	else if (!_stricmp(name, "bounce-out") || !_stricmp(name, "elastic-out"))
	{
		m.Type = !_stricmp(name, "bounce-out") ? ModifierType::Bounce : ModifierType::Elastic;
		m.Direction = EaseDirection::Out;
		m.Frequency = 3;
		m.Amplitude[m.Type == ModifierType::Bounce ? 2 : 3] = 1;
	}
	else if (!_stricmp(name, "fov-safe") || !_stricmp(name, "pitch-safe"))
	{
		m.Type = ModifierType::Limits;
		m.FadeIn = m.FadeOut = 0;
		const bool fov = !_stricmp(name, "fov-safe");
		m.Lower = fov ? 35 : -85;
		m.Upper = fov ? 110 : 85;
		m.Softness = 5;
		m.Amplitude[fov ? 3 : 4] = 1;
	}
	else if (!_stricmp(name, "slow-pan"))
	{
		m.Type = ModifierType::Slew;
		m.Rate = 45;
		m.Amplitude[5] = 1;
	}
	else if (!_stricmp(name, "stepped-12fps"))
	{
		m.Type = ModifierType::Stepped;
		m.Frequency = 12;
		m.FadeIn = m.FadeOut = 0;
		std::fill(m.Amplitude, m.Amplitude + 7, 1);
	}
	else if (!_stricmp(name, "loop-position") || !_stricmp(name, "ping-pong-position"))
	{
		m.Type = ModifierType::Repeat;
		m.PingPong = !_stricmp(name, "ping-pong-position");
		m.Frequency = m.PingPong ? 1 : 2;
		m.FadeIn = m.FadeOut = 0;
		m.Amplitude[0] = m.Amplitude[1] = m.Amplitude[2] = 1;
	}
	else if (!_stricmp(name, "reduce-roll") || !_stricmp(name, "widen-fov"))
	{
		m.Type = ModifierType::Remap;
		const bool roll = !_stricmp(name, "reduce-roll");
		m.Scale = roll ? .5 : 1;
		m.Offset = roll ? 0 : 10;
		m.Amplitude[roll ? 6 : 3] = 1;
	}
	else
		return false;
	if (!ValidModifiers({m}))
		return false;
	out = m;
	return true;
}

double CamPath::ModifierSampleRate(const NoiseModifier &m)
{
	if (m.Type == ModifierType::Noise)
		return m.Frequency * std::pow(2.0, m.Octaves - 1);
	if (m.Type == ModifierType::Smooth)
		return 1 / m.Window;
	if (IsTimingModifier(m.Type) || m.Type == ModifierType::Repeat)
		return (std::max)(m.Frequency, m.Type == ModifierType::Repeat ? 0.0 : m.Power) / (m.End - m.Begin);
	return m.Frequency;
}
double CamPath::ModifierInfluence(const NoiseModifier &m, double time)
{
	if (!m.Enabled || (m.Type >= ModifierType::Limits ? (time < m.Begin || time > m.End) : (time <= m.Begin || time >= m.End)))
		return 0;
	double envelope = m.Strength;
	if (!m.Envelope.empty())
	{
		const double t = (time - m.Begin) / (m.End - m.Begin);
		auto right = std::upper_bound(m.Envelope.begin(), m.Envelope.end(), t,
									  [](double value, const ModifierKey &key) { return value < key.Time; });
		if (right == m.Envelope.end())
			envelope *= m.Envelope.back().Value;
		else if (right == m.Envelope.begin())
			envelope *= right->Value;
		else
		{
			const auto &a = *std::prev(right), &b = *right;
			envelope *= a.Value + (b.Value - a.Value) * SmoothStep5((t - a.Time) / (b.Time - a.Time));
		}
	}
	return envelope * (m.FadeIn > 0 ? SmoothStep5((time - m.Begin) / m.FadeIn) : 1) *
		   (m.FadeOut > 0 ? SmoothStep5((m.End - time) / m.FadeOut) : 1);
}
namespace
{
void ApplyAdditiveModifier(CamPathValue &value, const CamPath::NoiseModifier &m, double time)
{
	const double influence = CamPath::ModifierInfluence(m, time);
	if (influence == 0)
		return;
	double offset[7] = {};
	for (int ch = 0; ch < 7; ++ch)
		if (m.Amplitude[ch] != 0)
		{
			double sample = 0, weight = 1, total = 0, frequency = m.Frequency;
			if (m.Type == CamPath::ModifierType::Oscillation)
				sample = std::sin(2 * M_PI * std::remainder((time - m.Begin) * frequency + m.Phase, 1.0));
			else
			{
				for (unsigned int octave = 0; octave < m.Octaves; ++octave)
				{
					sample +=
						weight * SmoothNoise((time - m.Begin) * frequency,
											 m.Seed + unsigned(ch) * 0x9e3779b9u + octave * 0x85ebca6bu);
					total += weight;
					weight *= m.Roughness;
					frequency *= 2;
				}
				sample /= total;
			}
			offset[ch] = m.Amplitude[ch] * influence * sample;
		}
	if (m.CameraSpace)
	{
		const auto angles = value.GetEulerAngles();
		double forward[3], right[3], up[3];
		MakeVectors(angles.Roll, angles.Pitch, angles.Yaw, forward, right, up);
		value.X += forward[0] * offset[0] + right[0] * offset[1] + up[0] * offset[2];
		value.Y += forward[1] * offset[0] + right[1] * offset[1] + up[1] * offset[2];
		value.Z += forward[2] * offset[0] + right[2] * offset[1] + up[2] * offset[2];
	}
	else
	{
		value.X += offset[0];
		value.Y += offset[1];
		value.Z += offset[2];
	}
	if (offset[3] != 0)
		value.Fov =
			std::clamp(value.Fov + offset[3], (std::min)(.01, value.Fov), (std::max)(179.0, value.Fov));
	if (offset[4] != 0 || offset[5] != 0 || offset[6] != 0)
		value.SetOrientation(value.R * Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(
										   QEulerAngles(offset[4], offset[5], offset[6]))));
}
// Positive rate integration keeps Expo monotone even when fades/envelopes are
// used. A shared normalization offset conserves the range duration; zero-weight
// regions retain unit rate. The table is built on edits, never per-frame.
std::vector<double> ExpoProgress(const CamPath::Modifier &m, double amount)
{
	constexpr size_t count = 512;
	std::vector<double> weight(count + 1), logRate(count + 1), rate(count + 1), progress(count + 1);
	for (size_t i = 0; i <= count; ++i)
	{
		const double u = double(i) / count;
		weight[i] = CamPath::ModifierInfluence(m, m.Begin + (m.End - m.Begin) * u) * amount;
		const double exponent = m.Direction == CamPath::EaseDirection::In
		                            ? 1 - u
		                            : (m.Direction == CamPath::EaseDirection::Out ? u : std::abs(2 * u - 1));
		logRate[i] = m.Power < 1e-8 ? 0 : std::log(m.Power / -std::expm1(-m.Power)) - m.Power * exponent;
	}
	auto area = [&](double offset) {
		double sum = 0;
		for (size_t i = 0; i <= count; ++i)
		{
			rate[i] = std::exp(weight[i] * (logRate[i] - offset));
			sum += rate[i] * ((i == 0 || i == count) ? .5 : 1);
		}
		return sum / count;
	};
	double lo = -32, hi = 32;
	for (int i = 0; i < 36; ++i)
	{
		const double mid = (lo + hi) * .5;
		if (area(mid) > 1)
			lo = mid;
		else
			hi = mid;
	}
	const double total = area((lo + hi) * .5);
	for (size_t i = 1; i <= count; ++i)
		progress[i] = progress[i - 1] + (rate[i - 1] + rate[i]) / (2 * count * total);
	progress.back() = 1;
	return progress;
}

void ApplyFilterModifier(CamPathValue &value, const CamPath::NoiseModifier &m, double time,
						 const double target[7])
{
	const double influence = CamPath::IsTimingModifier(m.Type)
	                             ? (m.Enabled && m.Strength > 0 && m.Begin < time && time < m.End ? 1.0 : 0.0)
	                             : CamPath::ModifierInfluence(m, time);
	if (influence == 0)
		return;
	auto angles = value.GetEulerAngles();
	if (!value.HasEuler && (m.Amplitude[4] != 0 || m.Amplitude[5] != 0 || m.Amplitude[6] != 0))
	{
		auto aligned = value;
		aligned.SetEulerAngles(QEulerAngles(target[4], target[5], target[6]));
		aligned.SetOrientation(value.R);
		angles = aligned.GetEulerAngles();
	}
	double *angle[3] = {&angles.Pitch, &angles.Yaw, &angles.Roll};
	bool rotated = false;
	for (int ch = 0; ch < 7; ++ch)
	{
		const double mix = CamPath::IsTimingModifier(m.Type)
		                       ? influence * (m.Amplitude[ch] != 0 ? 1 : 0)
		                       : influence * m.Amplitude[ch];
		if (mix == 0)
			continue;
		if (ch < 4)
		{
			const auto channel = (CamPath::Channel)ch;
			const double base = CamPath::GetChannelValue(value, channel);
			double result = base + (target[ch] - base) * mix;
			if (ch == 3)
				result = std::clamp(result, (std::min)(.01, base), (std::max)(179.0, base));
			CamPath::SetChannelValue(value, channel, result);
		}
		else
		{
			*angle[ch - 4] += (target[ch] - *angle[ch - 4]) * mix;
			rotated = true;
		}
	}
	if (rotated)
		value.SetEulerAngles(angles);
}

} // namespace
void CamPath::BuildModifierCurves()
{
	if (m_ModifierRevision == m_GeometryRevision)
		return;
	auto previousCurves = std::move(m_ModifierCurves);
	const auto previousTimes = std::move(m_ModifierTimes);
	m_ModifierCurves.resize(m_Modifiers.size());
	m_ModifierTimes.clear();
	const double begin = GetLowerBound(), end = GetUpperBound(), duration = end - begin;
	double sampleRate = 120;
	for (const auto &m : m_Modifiers)
		if (m.Enabled)
			sampleRate = (std::max)(sampleRate, ModifierSampleRate(m) * 8);
	const size_t resolution = (size_t)std::clamp(std::ceil(duration * sampleRate), 256.0, 16384.0);
	for (size_t i = 0; i <= resolution; ++i)
		m_ModifierTimes.push_back(begin + duration * i / resolution);
	auto add = [&](double time) {
		if (begin <= time && time <= end)
			m_ModifierTimes.push_back(time);
	};
	const size_t keyStride = (m_Map.size() + 8191) / 8192;
	size_t keyIndex = 0;
	for (const auto &key : m_Map)
		if (keyIndex++ % keyStride == 0)
		{
			add(key.first);
			add(key.first + key.second.HoldDuration);
		}
	for (const auto &m : m_Modifiers)
	{
		if (m.Enabled && m.Type == ModifierType::Stepped && m.Begin < end && m.End > begin)
		{
			const double first =
			    (std::max)(0.0, std::ceil(((std::max)(begin, m.Begin) - m.Begin) * m.Frequency));
			const double last = std::floor(((std::min)(end, m.End) - m.Begin) * m.Frequency);
			const double stride = (std::max)(1.0, std::ceil((last - first + 1) / 2048));
			for (double k = first; k <= last; k += stride)
				add(m.Begin + k / m.Frequency);
		}
		add(m.Begin);
		add(m.End);
		add(m.Begin + m.FadeIn);
		add(m.End - m.FadeOut);
		for (const auto &key : m.Envelope)
			add(m.Begin + (m.End - m.Begin) * key.Time);
	}
	std::sort(m_ModifierTimes.begin(), m_ModifierTimes.end());
	m_ModifierTimes.erase(std::unique(m_ModifierTimes.begin(), m_ModifierTimes.end()), m_ModifierTimes.end());
	const bool sameInput = m_ModifierInputRevision == m_BaseRevision && previousTimes == m_ModifierTimes;
	size_t firstChanged = 0;
	if (sameInput)
		while (firstChanged < m_ModifierInputs.size() && firstChanged < m_Modifiers.size() &&
			   m_ModifierInputs[firstChanged] == m_Modifiers[firstChanged])
			++firstChanged;
	std::vector<CamPathValue> values;
	size_t start = 0;
	if (sameInput && m_ModifierPrefix <= firstChanged &&
		m_ModifierInputValues.size() == m_ModifierTimes.size())
	{
		values = m_ModifierInputValues;
		start = m_ModifierPrefix;
	}
	else
	{
		m_ModifierInputValues.clear();
		m_ModifierPrefix = 0;
		values.reserve(m_ModifierTimes.size());
		for (double time : m_ModifierTimes)
			values.push_back(EvalUnmodified(time));
	}
	for (size_t index = 0; index < start; ++index)
		m_ModifierCurves[index] = std::move(previousCurves[index]);
	const size_t n = values.size();
	for (size_t index = start; index < m_Modifiers.size(); ++index)
	{
		if (index == firstChanged && n * sizeof(CamPathValue) <= 16 * 1024 * 1024)
		{
			m_ModifierInputValues = values;
			m_ModifierPrefix = index;
		}
		const auto &m = m_Modifiers[index];
		if (!m.Enabled)
			continue;
		if (int(m.Type) < 2)
		{
			for (size_t i = 0; i < n; ++i)
				ApplyAdditiveModifier(values[i], m, m_ModifierTimes[i]);
			continue;
		}
		auto &curve = m_ModifierCurves[index];
		bool reuse = sameInput && index <= firstChanged && index < previousCurves.size() &&
					 index < m_ModifierInputs.size();
		if (reuse)
		{
			const auto &old = m_ModifierInputs[index];
			reuse =
				old.Enabled && old.Type == m.Type &&
				(m.Type == ModifierType::Smooth ? old.Window == m.Window
												: old.Frequency == m.Frequency && old.Damping == m.Damping);
			if (IsTimingModifier(m.Type) || m.Type >= ModifierType::Limits)
				reuse = reuse && old == m;
			for (int ch = 0; ch < 7; ++ch)
			{
				if ((ch >= 4 || m.Amplitude[ch] != 0) && previousCurves[index].Values[ch].size() != n)
					reuse = false;
				// Zero-strength angle channels retain an unfiltered Euler branch guide.
				if (ch >= 4 && (old.Amplitude[ch] == 0) != (m.Amplitude[ch] == 0))
					reuse = false;
			}
		}
		if (reuse)
			curve = std::move(previousCurves[index]);
		else
		{
			std::vector<QEulerAngles> angles;
			angles.reserve(n);
			for (size_t i = 0; i < n; ++i)
			{
				auto sample = values[i];
				if (i && !sample.HasEuler)
				{
					sample.SetEulerAngles(angles.back());
					sample.SetOrientation(values[i].R);
				}
				angles.push_back(sample.GetEulerAngles());
			}
			std::map<double, std::vector<double>> expoMaps;
			for (int ch = 0; ch < 7; ++ch)
			{
				if (ch < 4 && m.Amplitude[ch] == 0)
					continue;
				auto &v = curve.Values[ch];
				v.resize(n);
				for (size_t i = 0; i < n; ++i)
				{
					v[i] = ch < 4    ? GetChannelValue(values[i], (Channel)ch)
						   : ch == 4 ? angles[i].Pitch
						   : ch == 5 ? angles[i].Yaw
									 : angles[i].Roll;
				}
				if (m.Type == ModifierType::Smooth && m.Amplitude[ch] != 0)
				{
					for (size_t i = 1; i < n; ++i)
					{
						const double a =
							-std::expm1(-(m_ModifierTimes[i] - m_ModifierTimes[i - 1]) / m.Window);
						v[i] = v[i - 1] + a * (v[i] - v[i - 1]);
					}
					for (size_t i = n - 1; i > 0; --i)
					{
						const double a =
							-std::expm1(-(m_ModifierTimes[i] - m_ModifierTimes[i - 1]) / m.Window);
						v[i - 1] = v[i] + a * (v[i - 1] - v[i]);
					}
				}
				else if (m.Type == ModifierType::Spring && m.Amplitude[ch] != 0)
				{
					double position = v[0], velocity = 0;
					for (size_t i = 1; i < n; ++i)
					{
						Afx::Math::SpringStep(position, velocity, v[i], m_ModifierTimes[i] - m_ModifierTimes[i - 1],
								   m.Frequency, m.Damping);
						v[i] = position;
					}
				}
				else if (IsTimingModifier(m.Type) && m.Amplitude[ch] != 0)
				{
					const auto source = v;
					std::vector<double> slopes;
					AutoTangentSettings policy;
					policy.Method = AutoTangentMethod::Monotone;
					ComputeAutoTangents(m_ModifierTimes.data(), v.data(), (int)n, policy, slopes);
					auto sample = [&](double time) {
						if (time <= m_ModifierTimes.front())
							return source.front();
						if (time >= m_ModifierTimes.back())
							return source.back();
						const size_t b = std::lower_bound(m_ModifierTimes.begin(), m_ModifierTimes.end(),
						                                  time) -
						                 m_ModifierTimes.begin(),
						             a = b - 1;
						const double h = m_ModifierTimes[b] - m_ModifierTimes[a];
						return EvalWeightedHermite((time - m_ModifierTimes[a]) / h, h, source[a], source[b],
						                           slopes[a], slopes[b], 1, 1);
					};
					const double left = sample(m.Begin), right = sample(m.End);
					const std::vector<double> *expo = nullptr;
					if (m.Type == ModifierType::Expo)
					{
						auto &map = expoMaps[m.Amplitude[ch]];
						if (map.empty())
							map = ExpoProgress(m, m.Amplitude[ch]);
						expo = &map;
					}
					for (size_t i = 0; i < n; ++i)
						if (m.Begin < m_ModifierTimes[i] && m_ModifierTimes[i] < m.End)
						{
							const double t = (m_ModifierTimes[i] - m.Begin) / (m.End - m.Begin);
							double u = t + ModifierInfluence(m, m_ModifierTimes[i]) * m.Amplitude[ch] *
							                   (ModifierEase(m, t) - t);
							if (expo)
							{
								const double x = t * (expo->size() - 1);
								const size_t a = (std::min)(size_t(x), expo->size() - 2);
								u = (*expo)[a] + ((*expo)[a + 1] - (*expo)[a]) * (x - a);
							}
							// Elastic overshoot extends the range's endpoint displacement;
							// do not evaluate outside the underlying path or recurse into Eval.
							v[i] = u < 0   ? left + (right - left) * u
							       : u > 1 ? right + (right - left) * (u - 1)
							               : sample(m.Begin + (m.End - m.Begin) * u);
						}
				}
				else if (m.Type >= ModifierType::Limits && m.Amplitude[ch] != 0)
				{
					const auto source = v;
					auto sample = [&](double time) {
						auto right = std::lower_bound(m_ModifierTimes.begin(), m_ModifierTimes.end(), time);
						if (right == m_ModifierTimes.begin())
							return source.front();
						if (right == m_ModifierTimes.end())
							return source.back();
						const size_t b = right - m_ModifierTimes.begin(), a = b - 1;
						const double u =
						    (time - m_ModifierTimes[a]) / (m_ModifierTimes[b] - m_ModifierTimes[a]);
						return source[a] + (source[b] - source[a]) * u;
					};
					for (size_t i = 0; i < n; ++i)
					{
						if ((m.Type == ModifierType::Stepped || m.Type == ModifierType::Repeat) &&
						    (m_ModifierTimes[i] < m.Begin || m_ModifierTimes[i] > m.End))
							continue;
						if (m.Type == ModifierType::Limits)
						{
							double x = std::clamp(source[i], m.Lower, m.Upper);
							if (m.Softness > 0)
							{
								if (x < m.Lower + m.Softness)
									x = m.Lower + m.Softness * std::exp((source[i] - m.Lower - m.Softness) /
									                                    m.Softness);
								else if (x > m.Upper - m.Softness)
									x = m.Upper - m.Softness * std::exp((m.Upper - m.Softness - source[i]) /
									                                    m.Softness);
							}
							v[i] = x;
						}
						else if (m.Type == ModifierType::Slew && i)
						{
							const double step = m.Rate * (m_ModifierTimes[i] - m_ModifierTimes[i - 1]);
							v[i] = std::clamp(source[i], v[i - 1] - step, v[i - 1] + step);
						}
						else if (m.Type == ModifierType::Stepped)
							v[i] = sample(m.Begin +
							              std::floor((m_ModifierTimes[i] - m.Begin) * m.Frequency + 1e-9) /
							                  m.Frequency);
						else if (m.Type == ModifierType::Repeat)
						{
							const double cycles =
							    (m_ModifierTimes[i] - m.Begin) / (m.End - m.Begin) * m.Frequency;
							double u = cycles - std::floor(cycles);
							if (m.PingPong)
							{
								const double p = 2 * (cycles - std::floor(cycles));
								u = p <= 1 ? p : 2 - p;
							}
							v[i] = sample(m.Begin + u * (m.End - m.Begin));
						}
						else if (m.Type == ModifierType::Remap)
							v[i] = m.Pivot + (source[i] - m.Pivot) * m.Scale + m.Offset;
					}
				}
				AutoTangentSettings settings;
				settings.Method = AutoTangentMethod::Monotone;
				ComputeAutoTangents(m_ModifierTimes.data(), v.data(), (int)n, settings, curve.Slopes[ch]);
			}
		}
		// The final target is sampled by Eval; its output is only needed by later modifiers.
		if (index + 1 == m_Modifiers.size())
			continue;
		for (size_t i = 0; i < n; ++i)
		{
			double target[7];
			for (int ch = 0; ch < 7; ++ch)
				target[ch] = curve.Values[ch].empty() ? 0 : curve.Values[ch][i];
			ApplyFilterModifier(values[i], m, m_ModifierTimes[i], target);
		}
	}
	m_ModifierInputRevision = m_BaseRevision;
	m_ModifierInputs = m_Modifiers;
	m_ModifierRevision = m_GeometryRevision;
}

CamPathValue CamPath::Eval(double time)
{
	auto value = EvalUnmodified(time);
	for (size_t index = 0; index < m_Modifiers.size(); ++index)
	{
		const auto &m = m_Modifiers[index];
		if (IsTimingModifier(m.Type)
		        ? (!m.Enabled || m.Strength == 0 || time <= m.Begin || time >= m.End)
				: ModifierInfluence(m, time) == 0)
			continue;
		if (int(m.Type) < 2)
		{
			ApplyAdditiveModifier(value, m, time);
			continue;
		}
		BuildModifierCurves();
		const auto &curve = m_ModifierCurves[index];
		if (m_ModifierTimes.empty())
			continue;
		auto right = std::lower_bound(m_ModifierTimes.begin(), m_ModifierTimes.end(), time);
		size_t b = size_t(right - m_ModifierTimes.begin());
		if (b >= m_ModifierTimes.size())
			b = m_ModifierTimes.size() - 1;
		const size_t a = b ? b - 1 : 0;
		double target[7];
		for (int ch = 0; ch < 7; ++ch)
		{
			if (curve.Values[ch].empty())
			{
				target[ch] = 0;
				continue;
			}
			if (m.Type == ModifierType::Stepped)
				target[ch] = curve.Values[ch][time == m_ModifierTimes[b] ? b : a];
			else if (m.Type == ModifierType::Slew && a != b)
			{
				const double u = (time - m_ModifierTimes[a]) / (m_ModifierTimes[b] - m_ModifierTimes[a]);
				target[ch] = curve.Values[ch][a] + u * (curve.Values[ch][b] - curve.Values[ch][a]);
			}
			else if (a == b)
				target[ch] = curve.Values[ch][b];
			else
			{
				const double h = m_ModifierTimes[b] - m_ModifierTimes[a],
							 u = std::clamp((time - m_ModifierTimes[a]) / h, 0.0, 1.0);
				target[ch] = EvalWeightedHermite(u, h, curve.Values[ch][a], curve.Values[ch][b],
												 curve.Slopes[ch][a], curve.Slopes[ch][b], 1, 1);
			}
		}
		ApplyFilterModifier(value, m, time, target);
	}
	return value;
}

void CamPath::BuildSpeedProfile()
{
	if (!m_SpeedDirty)
		return;
	m_SpeedDirty = false;
	m_SpeedArea.assign(m_SpeedProfile.empty() ? 2 : m_SpeedProfile.size(), 0);
	if (m_SpeedProfile.empty())
	{
		m_SpeedIntegral = 1;
		m_SpeedArea.back() = 1;
		return;
	}
	for (size_t i = 1; i < m_SpeedProfile.size(); ++i)
		m_SpeedArea[i] = m_SpeedArea[i - 1] + (m_SpeedProfile[i].Time - m_SpeedProfile[i - 1].Time) *
												  (m_SpeedProfile[i].Speed + m_SpeedProfile[i - 1].Speed) / 2;
	m_SpeedIntegral = m_SpeedArea.back();
	if (m_SpeedIntegral > 1e-9)
		for (auto &area : m_SpeedArea)
			area /= m_SpeedIntegral;
}
double CamPath::GetProfileSpeed(double time)
{
	if (m_SpeedProfile.empty())
		return 1;
	time = std::clamp(time, 0.0, 1.0);
	auto right = std::upper_bound(m_SpeedProfile.begin(), m_SpeedProfile.end(), time,
								  [](double t, const SpeedKey &key) { return t < key.Time; });
	if (right == m_SpeedProfile.begin())
		return right->Speed;
	if (right == m_SpeedProfile.end())
		return m_SpeedProfile.back().Speed;
	const auto left = std::prev(right);
	const double u = SmoothStep5((time - left->Time) / (right->Time - left->Time));
	return left->Speed + (right->Speed - left->Speed) * u;
}
double CamPath::ProfileDistance(double time)
{
	BuildSpeedProfile();
	time = std::clamp(time, 0.0, 1.0);
	if (m_SpeedProfile.empty())
		return time;
	if (time <= 0)
		return 0;
	if (time >= 1)
		return 1;
	auto right = std::upper_bound(m_SpeedProfile.begin(), m_SpeedProfile.end(), time,
								  [](double t, const SpeedKey &key) { return t < key.Time; });
	const size_t i = size_t(right - m_SpeedProfile.begin() - 1);
	const auto &left = m_SpeedProfile[i];
	const double h = right->Time - left.Time, u = (time - left.Time) / h;
	// Exact integral of the quintic smoothstep used by the speed curve.
	const double integral = u * u * u * u * (2.5 + u * (-3 + u));
	return m_SpeedArea[i] + h * (left.Speed * u + (right->Speed - left.Speed) * integral) / m_SpeedIntegral;
}
double CamPath::ProfileTime(double distance)
{
	BuildSpeedProfile();
	if (distance <= 0)
		return 0;
	if (distance >= 1)
		return 1;
	if (m_SpeedProfile.empty())
		return distance;
	auto found = std::lower_bound(m_SpeedArea.begin(), m_SpeedArea.end(), distance);
	const size_t i = size_t(found - m_SpeedArea.begin());
	if (!i)
		return 0;
	if (i >= m_SpeedArea.size())
		return 1;
	double low = m_SpeedProfile[i - 1].Time, high = m_SpeedProfile[i].Time;
	for (int iteration = 0; iteration < 55; ++iteration)
	{
		const double middle = (low + high) * 0.5;
		if (ProfileDistance(middle) < distance)
			low = middle;
		else
			high = middle;
	}
	return (low + high) * 0.5;
}
std::vector<double> CamPath::GetSpeedKeyProgress()
{
	BuildArcs();
	std::vector<double> result;
	result.reserve(m_Map.size());
	size_t index = 0;
	for (const auto &key : m_Map)
	{
		const double progress =
			m_TravelDuration > 0
				? (index < m_Arcs.size() ? m_Arcs[index].TravelBegin / m_TravelDuration : 1.0)
				: (GetDuration() > 0 ? (key.first - GetLowerBound()) / GetDuration() : 0.0);
		result.push_back(std::clamp(progress, 0.0, 1.0));
		++index;
	}
	return result;
}

bool CamPath::ApplySpeedProfile(const std::vector<SpeedKey> &keys, double duration)
{
	if (keys.size() > 128 ||
		(!keys.empty() && (keys.size() < 2 || keys.front().Time != 0 || keys.back().Time != 1)))
		return false;
	double area = 0;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		if (!std::isfinite(keys[i].Time) || !std::isfinite(keys[i].Speed) || keys[i].Speed < 0 ||
			keys[i].Speed > 100 || (i && keys[i].Time <= keys[i - 1].Time))
			return false;
		if (i)
			area += (keys[i].Time - keys[i - 1].Time) * (keys[i].Speed + keys[i - 1].Speed) / 2;
	}
	if (!keys.empty() && area <= 1e-9)
		return false;
	const auto original = CaptureState();
	m_SpeedProfile = keys;
	m_SpeedDirty = true;
	if (RetimeByDistance(duration))
		return true;
	RestoreState(original);
	return false;
}

char * double2xml(rapidxml::xml_document<> & doc, double value)
{
	char szTmp[196];
	_snprintf_s(szTmp, _TRUNCATE, "%.17g", value);
	return doc.allocate_string(szTmp);
}

bool CamPath::SaveToString(std::string &xmlString)
{
	if (!std::isfinite(m_Offset) || !ValidKeys(m_Map) || !ValidModifiers(m_Modifiers))
		return false;
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
	if (!m_SpeedProfile.empty())
	{
		auto *profile = doc.allocate_node(rapidxml::node_element, "speedProfile");
		cam->append_node(profile);
		for (const auto &key : m_SpeedProfile)
		{
			auto *p = doc.allocate_node(rapidxml::node_element, "p");
			p->append_attribute(doc.allocate_attribute("t", double2xml(doc, key.Time)));
			p->append_attribute(doc.allocate_attribute("speed", double2xml(doc, key.Speed)));
			profile->append_node(p);
		}
	}
	if (!m_Modifiers.empty())
	{
		auto *modifiers = doc.allocate_node(rapidxml::node_element, "modifiers");
		cam->append_node(modifiers);
		for (const auto &m : m_Modifiers)
		{
			auto *p = doc.allocate_node(rapidxml::node_element, ModifierTypeName(m.Type));
			const char *names[] = {"begin", "end",	   "fadeIn",	  "fadeOut", "frequency",
								   "seed",	"enabled", "cameraSpace", "x",		 "y",
								   "z",		"fov",	   "pitch",		  "yaw",	 "roll"};
			const double values[] = {m.Begin,
									 m.End,
									 m.FadeIn,
									 m.FadeOut,
									 m.Frequency,
									 double(m.Seed),
									 m.Enabled ? 1.0 : 0.0,
									 m.CameraSpace ? 1.0 : 0.0,
									 m.Amplitude[0],
									 m.Amplitude[1],
									 m.Amplitude[2],
									 m.Amplitude[3],
									 m.Amplitude[4],
									 m.Amplitude[5],
									 m.Amplitude[6]};
			for (int i = 0; i < 15; ++i)
				p->append_attribute(doc.allocate_attribute(names[i], double2xml(doc, values[i])));
			const char *extraNames[] = {"strength", "phase", "octaves", "roughness", "window",
				"damping", "power", "direction", "lower", "upper", "softness", "rate", "scale", "offset", "pivot", "pingPong"};
			const double extra[] = {m.Strength, m.Phase, double(m.Octaves), m.Roughness, m.Window,
				m.Damping, m.Power, double(m.Direction), m.Lower, m.Upper, m.Softness, m.Rate, m.Scale, m.Offset, m.Pivot, m.PingPong ? 1.0 : 0.0},
						 defaults[] = {1, 0, 1, .5, .25, .7, 5, 2, 0, 179, 0, 90, 1, 0, 0, 0};
			for (int i = 0; i < 16; ++i)
				if (extra[i] != defaults[i])
					p->append_attribute(doc.allocate_attribute(extraNames[i], double2xml(doc, extra[i])));
			if (!m.Envelope.empty())
			{
				auto *envelope = doc.allocate_node(rapidxml::node_element, "envelope");
				p->append_node(envelope);
				for (const auto &key : m.Envelope)
				{
					auto *point = doc.allocate_node(rapidxml::node_element, "p");
					point->append_attribute(doc.allocate_attribute("t", double2xml(doc, key.Time)));
					point->append_attribute(doc.allocate_attribute("value", double2xml(doc, key.Value)));
					envelope->append_node(point);
				}
			}
			modifiers->append_node(p);
		}
	}

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

		for (int ch = 0; ch < 7; ++ch)
			if (val.AccelerationMask & (1u << ch))
			{
				const auto in = "accel_in_" + std::to_string(ch), out = "accel_out_" + std::to_string(ch);
				pt->append_attribute(doc.allocate_attribute(doc.allocate_string(in.c_str()),
															double2xml(doc, val.AccelerationIn[ch])));
				pt->append_attribute(doc.allocate_attribute(doc.allocate_string(out.c_str()),
															double2xml(doc, val.AccelerationOut[ch])));
			}
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
	for (const auto &m : m_Modifiers)
		if (m.Enabled)
		{
			const double a = (std::max)(begin, m.Begin), b = (std::min)(end, m.End);
			if (b <= a)
				continue;
			for (size_t key = 1; key < m.Envelope.size(); ++key)
				for (int sample = 0; sample <= 8; ++sample)
				{
					const double u = m.Envelope[key - 1].Time +
									 (m.Envelope[key].Time - m.Envelope[key - 1].Time) * sample / 8;
					const double t = m.Begin + (m.End - m.Begin) * u;
					if (begin <= t && t <= end)
						samples[t] = Eval(t);
				}
			const double step = (std::min)(interval, 1.0 / (ModifierSampleRate(m) * 8));
			if ((b - a) / step > 100000 || samples.size() + (size_t)std::ceil((b - a) / step) > 100000)
				return false;
			for (size_t i = 0; i <= (size_t)std::ceil((b - a) / step); ++i)
			{
				const double t = (std::min)(b, a + i * step);
				samples[t] = Eval(t);
			}
			for (double t : {m.Begin + m.FadeIn, m.End - m.FadeOut})
				if (t >= a && t <= b)
					samples[t] = Eval(t);
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
	const auto speedBackup = m_SpeedProfile;
	const auto modifiersBackup = m_Modifiers;
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
		m_SpeedProfile = speedBackup;
		m_Modifiers = modifiersBackup;
		m_SpeedDirty = true;
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

				m_SpeedProfile.clear();
				m_Modifiers.clear();
				m_SpeedDirty = true;
				auto number = [](rapidxml::xml_node<> *node, const char *name, double &value)
				{
					auto *attr = node->first_attribute(name);
					if (!attr)
						return false;
					char *end = nullptr;
					value = std::strtod(attr->value(), &end);
					return end != attr->value() && !*end && std::isfinite(value);
				};
				if (auto *profile = cur_node->first_node("speedProfile"))
				{
					for (auto *p = profile->first_node("p"); p; p = p->next_sibling("p"))
					{
						SpeedKey key;
						if (m_SpeedProfile.size() >= 128 || !number(p, "t", key.Time) ||
							!number(p, "speed", key.Speed) || key.Speed < 0 || key.Speed > 100 ||
							(!m_SpeedProfile.empty() && key.Time <= m_SpeedProfile.back().Time))
						{
							bOk = false;
							break;
						}
						m_SpeedProfile.push_back(key);
					}
					if (m_SpeedProfile.size() < 2 || m_SpeedProfile.front().Time != 0 ||
						m_SpeedProfile.back().Time != 1)
						bOk = false;
					BuildSpeedProfile();
					if (m_SpeedArea.back() < 0.5)
						bOk = false;
				}
				if (auto *modifiers = cur_node->first_node("modifiers"))
				{
					std::vector<NoiseModifier> parsed;
					for (auto *p = modifiers->first_node(); p; p = p->next_sibling())
					{
						ModifierType type;
						if (!ModifierTypeFromString(p->name(), type) || parsed.size() >= 64)
						{
							bOk = false;
							break;
						}
						NoiseModifier m;
						m.Type = type;
						double seed, enabled, local;
						bOk = number(p, "begin", m.Begin) && number(p, "end", m.End) &&
							  number(p, "fadeIn", m.FadeIn) && number(p, "fadeOut", m.FadeOut) &&
							  number(p, "frequency", m.Frequency) && number(p, "seed", seed) &&
							  number(p, "enabled", enabled) && number(p, "cameraSpace", local);
						if (!bOk || seed < 0 || seed > 1000000000 || seed != std::floor(seed) ||
							(enabled != 0 && enabled != 1) || (local != 0 && local != 1))
						{
							bOk = false;
							break;
						}
						m.Seed = (unsigned int)seed;
						m.Enabled = enabled != 0;
						m.CameraSpace = local != 0;
						const char *names[] = {"x", "y", "z", "fov", "pitch", "yaw", "roll"};
						for (int ch = 0; ch < 7; ++ch)
							if (!number(p, names[ch], m.Amplitude[ch]))
								bOk = false;
						if (!bOk)
							break;
						const char *extraNames[] = {"strength",	 "phase",  "octaves",
													"roughness", "window", "damping"};
						double octaves = m.Octaves;
						double *extra[] = {&m.Strength,	 &m.Phase,	&octaves,
										   &m.Roughness, &m.Window, &m.Damping};
						for (int i = 0; i < 6; ++i)
							if (p->first_attribute(extraNames[i]) && !number(p, extraNames[i], *extra[i]))
								bOk = false;
						if (octaves < 1 || octaves > 8 || octaves != std::floor(octaves))
							bOk = false;
						else
							m.Octaves = (unsigned)octaves;
						double direction = double(m.Direction);
						if ((p->first_attribute("power") && !number(p, "power", m.Power)) ||
							(p->first_attribute("direction") && !number(p, "direction", direction)) ||
							direction < 0 || direction > 2 || direction != std::floor(direction))
							bOk = false;
						else
							m.Direction = (EaseDirection)int(direction);
						const char *controlNames[] = {"lower", "upper", "softness", "rate", "scale", "offset", "pivot"};
						double *control[] = {&m.Lower, &m.Upper, &m.Softness, &m.Rate, &m.Scale, &m.Offset, &m.Pivot};
						for (int c = 0; c < 7; ++c) if (p->first_attribute(controlNames[c]) && !number(p, controlNames[c], *control[c])) bOk = false;
						double pingPong = 0;
						if (p->first_attribute("pingPong") && (!number(p, "pingPong", pingPong) || (pingPong != 0 && pingPong != 1))) bOk = false;
						m.PingPong = pingPong != 0;
						if (auto *envelope = p->first_node("envelope"))
						{
							for (auto *point = envelope->first_node(); point; point = point->next_sibling())
							{
								ModifierKey key;
								if (strcmp(point->name(), "p") || m.Envelope.size() >= 128 ||
									!number(point, "t", key.Time) || !number(point, "value", key.Value))
								{
									bOk = false;
									break;
								}
								m.Envelope.push_back(key);
							}
							if (m.Envelope.size() < 2)
								bOk = false;
						}
						if (!bOk)
							break;
						parsed.push_back(m);
					}
					if (!SetModifiers(parsed))
						bOk = false;
				}
				if (!bOk)
					break;
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
				double offset = 0;
				if (offsetA)
				{
					char *end = nullptr;
					offset = std::strtod(offsetA->value(), &end);
					if (end == offsetA->value() || *end || !std::isfinite(offset))
					{
						bOk = false;
						break;
					}
				}
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
						for (int ch = 0; ch < 7; ++ch)
						{
							const auto in = "accel_in_" + std::to_string(ch),
									   out = "accel_out_" + std::to_string(ch);
							auto *ai = cur_node->first_attribute(in.c_str()),
								 *ao = cur_node->first_attribute(out.c_str());
							if (ai || ao)
							{
								if (!ai || !ao)
								{
									bOk = false;
									break;
								}
								char *e = nullptr;
								r.AccelerationIn[ch] = std::strtod(ai->value(), &e);
								if (e == ai->value() || *e)
								{
									bOk = false;
									break;
								}
								r.AccelerationOut[ch] = std::strtod(ao->value(), &e);
								if (e == ao->value() || *e)
								{
									bOk = false;
									break;
								}
								r.AccelerationMask |= 1u << ch;
							}
						}
						if (!bOk)
							break;
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
						for (int ch = 0; ch < 7; ++ch)
						{
							const auto in = "accel_in_" + std::to_string(ch),
									   out = "accel_out_" + std::to_string(ch);
							auto *ai = cur_node->first_attribute(in.c_str()),
								 *ao = cur_node->first_attribute(out.c_str());
							if (ai || ao)
							{
								if (!ai || !ao)
								{
									bOk = false;
									break;
								}
								char *e = nullptr;
								r.AccelerationIn[ch] = std::strtod(ai->value(), &e);
								if (e == ai->value() || *e)
								{
									bOk = false;
									break;
								}
								r.AccelerationOut[ch] = std::strtod(ao->value(), &e);
								if (e == ao->value() || *e)
								{
									bOk = false;
									break;
								}
								r.AccelerationMask |= 1u << ch;
							}
						}
						if (!bOk)
							break;
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
				key.second.AccelerationIn[ch] /= scale * scale;
				key.second.AccelerationOut[ch] /= scale * scale;
			}
		}
		if (!keys.emplace(time, key.second).second)
			return;
	}
	ReplaceKeys(keys);
}

void CamPath::SetPosition(double x, double y, double z, bool setX, bool setY, bool setZ)
{
	if (m_Map.size() < 1)
		return;
	auto keys = m_Map;

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

	for (CInterpolationMap<CamPathValue>::iterator it = keys.begin(); it != keys.end(); ++it)
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

	ReplaceKeys(keys);
}

void CamPath::SetAngles(double yPitch, double zYaw, double xRoll, bool setY, bool setZ, bool setX)
{
	if (m_Map.size() < 1)
		return;
	auto keys = m_Map;

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

	for (CInterpolationMap<CamPathValue>::iterator it = keys.begin(); it != keys.end(); ++it)
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

	ReplaceKeys(keys);
}

void CamPath::SetFov(double fov)
{
	if (m_Map.size() < 1)
		return;
	auto keys = m_Map;

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

	for (CInterpolationMap<CamPathValue>::iterator it = keys.begin(); it != keys.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			curValue.Fov = fov;

			it->second = curValue;
		}

	}

	ReplaceKeys(keys);
}

bool CamPath::GetAcceleration(Channel ch, size_t index, bool outgoing, double &acceleration)
{
	if (ch < CH_X || ch > CH_RROLL || index >= m_KeyArrival.size())
		return false;
	index = outgoing ? m_KeyRelease[index] : m_KeyArrival[index];
	if (ch <= CH_FOV)
	{
		if ((ch == CH_FOV ? m_FovInterpMethod : m_PositionInterpMethod) != DI_CUSTOM)
			return false;
		auto *interp = ch == CH_X ? m_XInterp : ch == CH_Y ? m_YInterp : ch == CH_Z ? m_ZInterp : m_FovInterp;
		return static_cast<CHermiteDoubleInterpolation<CamPathValue> *>(interp)->GetAcceleration(
			index, outgoing, acceleration);
	}
	return m_RotationInterpMethod == QI_CUSTOM &&
		   static_cast<CEulerHermiteQuaternionInterpolation<CamPathValue> *>(m_RInterp)->GetAcceleration(
			   index, ch - CH_RPITCH, outgoing, acceleration);
}
bool CamPath::GetTangent(Channel ch, size_t index, bool outgoing, double &slope, double &weight)
{
	if (ch < CH_X || ch > CH_RROLL || index >= m_KeyArrival.size())
		return false;
	index = outgoing ? m_KeyRelease[index] : m_KeyArrival[index];

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

bool CamPath::EditTangent(size_t index, Channel channel, bool outgoing, double slope, double weight,
						  bool linked, bool editWeight)
{
	return EditTangents({{index, channel, outgoing, slope, weight}}, linked, editWeight, true);
}

bool CamPath::EditTangents(const std::vector<TangentEdit> &edits, bool linked, bool editWeight,
						   bool selectEdited)
{
	if (edits.empty())
		return false;
	auto keys = m_Map;
	if (selectEdited)
		for (auto &key : keys)
			key.second.Selected = false;
	for (const auto &edit : edits)
	{
		const auto channel = edit.Component;
		if (channel < CH_X || channel > CH_RROLL || edit.Index >= m_KeyTimes.size() ||
			!std::isfinite(edit.Slope) || !std::isfinite(edit.Weight) || edit.Weight < 0)
			return false;
		if (channel < CH_FOV	? m_PositionInterpMethod != DI_CUSTOM
			: channel == CH_FOV ? m_FovInterpMethod != DI_CUSTOM
								: m_RotationInterpMethod != QI_CUSTOM)
			return false;
		auto &value = keys.at(m_KeyTimes[edit.Index]);
		if (selectEdited)
			value.Selected = true;
		if (!edit.Outgoing || linked)
		{
			value.*TangentIn[channel] = edit.Slope;
			value.*ModeIn[channel] = TM_FREE;
		}
		if (edit.Outgoing || linked)
		{
			value.*TangentOut[channel] = edit.Slope;
			value.*ModeOut[channel] = TM_FREE;
		}
		if (editWeight)
			value.*(edit.Outgoing ? WeightOut[channel] : WeightIn[channel]) = edit.Weight;
		value.AccelerationMask &= ~(1u << channel);
	}
	return ReplaceKeys(keys);
}

void CamPath::SetTangent(Channel ch, bool setIn, bool setOut, double slopeIn, double slopeOut)
{
	if (ch < CH_X || ch > CH_RROLL || !std::isfinite(slopeIn) || !std::isfinite(slopeOut) || m_Map.empty())
		return;
	auto keys = m_Map;
	const bool all =
		std::none_of(keys.begin(), keys.end(), [](const auto &key) { return key.second.Selected; });
	for (auto &key : keys)
		if (all || key.second.Selected)
		{
			if (setIn)
				key.second.*TangentIn[ch] = slopeIn;
			if (setOut)
				key.second.*TangentOut[ch] = slopeOut;
		}
	ReplaceKeys(keys);
}
void CamPath::SetTangentMode(Channel ch, bool setIn, bool setOut, unsigned char mode)
{
	if (ch < CH_X || ch > CH_RROLL || mode > TM_FREE || m_Map.empty())
		return;
	auto keys = m_Map;
	const bool all =
		std::none_of(keys.begin(), keys.end(), [](const auto &key) { return key.second.Selected; });
	for (auto &key : keys)
		if (all || key.second.Selected)
		{
			key.second.AccelerationMask &= ~(1u << ch);
			if (setIn)
				key.second.*ModeIn[ch] = mode;
			if (setOut)
				key.second.*ModeOut[ch] = mode;
		}
	ReplaceKeys(keys);
}
void CamPath::SetTangentWeight(Channel ch, bool setIn, bool setOut, double incoming, double outgoing)
{
	if (ch < CH_X || ch > CH_RROLL || !std::isfinite(incoming) || !std::isfinite(outgoing) || incoming < 0 ||
		outgoing < 0 || m_Map.empty())
		return;
	auto keys = m_Map;
	const bool all =
		std::none_of(keys.begin(), keys.end(), [](const auto &key) { return key.second.Selected; });
	for (auto &key : keys)
		if (all || key.second.Selected)
		{
			if (setIn)
				key.second.*WeightIn[ch] = incoming;
			if (setOut)
				key.second.*WeightOut[ch] = outgoing;
		}
	ReplaceKeys(keys);
}

bool CamPath::Transform(const double matrix[3][3], const Quaternion &orientation, const double source[3],
						const double destination[3])
{
	if (m_Map.empty())
		return false;
	const auto original = CaptureState();
	CamPath work;
	work.RestoreState(original);
	// Use a temporary native path: an unsupported transform or an invalid hold
	// must leave modes, derivatives and all original data untouched.
	const bool evaluated = work.CanEval();
	if (evaluated && (!work.MakeScalarCurvesEditable() || !work.BakeTangents()))
		return false;
	auto state = work.CaptureState();
	auto &keys = state.Keys;
	state.Fov = original.Fov;
	state.Rotation = original.Rotation;
	bool all = true;
	for (const auto &key : keys)
		if (key.second.Selected)
			all = false;
	// An unweighted cubic can be represented exactly as a quintic as well. This
	// allows mixing it with a minimum-jerk axis without dropping higher derivatives.
	if (evaluated && state.Position == DI_CUSTOM &&
		state.AutoTangents.Method == AutoTangentMethod::MinimumJerk)
		for (int ch = 0; ch < 3; ++ch)
		{
			bool unit = true;
			for (const auto &key : keys)
				unit &= std::abs(key.second.*WeightIn[ch] - 1) < 1e-10 &&
						std::abs(key.second.*WeightOut[ch] - 1) < 1e-10;
			if (!unit)
				continue;
			for (auto key = keys.begin(); key != keys.end(); ++key)
			{
				if (key->second.AccelerationMask & (1u << ch))
					continue;
				double ai = 0, ao = 0;
				if (key != keys.begin())
				{
					const auto prev = std::prev(key);
					const double h = key->first - prev->first - prev->second.HoldDuration;
					if (h > 0)
						ai = -6 *
								 (GetChannelValue(key->second, (Channel)ch) -
								  GetChannelValue(prev->second, (Channel)ch)) /
								 (h * h) +
							 (2 * (prev->second.*TangentOut[ch]) + 4 * (key->second.*TangentIn[ch])) / h;
				}
				const auto next = std::next(key);
				if (next != keys.end())
				{
					const double h = next->first - key->first - key->second.HoldDuration;
					if (h > 0)
						ao = 6 *
								 (GetChannelValue(next->second, (Channel)ch) -
								  GetChannelValue(key->second, (Channel)ch)) /
								 (h * h) -
							 (4 * (key->second.*TangentOut[ch]) + 2 * (next->second.*TangentIn[ch])) / h;
				}
				key->second.AccelerationMask |= 1u << ch;
				key->second.AccelerationIn[ch] = ai;
				key->second.AccelerationOut[ch] = ao;
			}
		}
	for (auto &key : keys)
	{
		const auto &authored = original.Keys.at(key.first);
		key.second.AccelerationMask = (key.second.AccelerationMask & 7) | (authored.AccelerationMask & 0x78);
		for (int ch = 3; ch < 7; ++ch)
		{
			key.second.*TangentIn[ch] = authored.*TangentIn[ch];
			key.second.*TangentOut[ch] = authored.*TangentOut[ch];
			key.second.*ModeIn[ch] = authored.*ModeIn[ch];
			key.second.*ModeOut[ch] = authored.*ModeOut[ch];
			key.second.*WeightIn[ch] = authored.*WeightIn[ch];
			key.second.*WeightOut[ch] = authored.*WeightOut[ch];
			key.second.AccelerationIn[ch] = authored.AccelerationIn[ch];
			key.second.AccelerationOut[ch] = authored.AccelerationOut[ch];
		}
		key.second.R = authored.R;
		key.second.HasEuler = authored.HasEuler;
		key.second.EulerPitch = authored.EulerPitch;
		key.second.EulerYaw = authored.EulerYaw;
		key.second.EulerRoll = authored.EulerRoll;
	}
	bool constant[3] = {true, true, true};
	for (int ch = 0; ch < 3; ++ch)
		for (const auto &key : keys)
			constant[ch] &= GetChannelValue(key.second, (Channel)ch) ==
								GetChannelValue(keys.begin()->second, (Channel)ch) &&
							key.second.*TangentIn[ch] == 0 && key.second.*TangentOut[ch] == 0 &&
							(!(key.second.AccelerationMask & (1u << ch)) ||
							 (key.second.AccelerationIn[ch] == 0 && key.second.AccelerationOut[ch] == 0));
	for (auto &key : keys)
		if (all || key.second.Selected)
		{
			const auto before = key.second;
			const auto &authored = original.Keys.at(key.first);
			double incoming[3] = {}, outgoing[3] = {}, ai[3] = {}, ao[3] = {};
			const double position[] = {before.X - source[0], before.Y - source[1], before.Z - source[2]};
			double result[3] = {};
			unsigned char accelerationMask = 0;
			for (int row = 0; row < 3; ++row)
			{
				int basis = -1;
				for (int ch = 0; ch < 3; ++ch)
				{
					result[row] += matrix[row][ch] * position[ch];
					incoming[row] += matrix[row][ch] * (before.*TangentIn[ch]);
					outgoing[row] += matrix[row][ch] * (before.*TangentOut[ch]);
					if (before.AccelerationMask & (1u << ch))
					{
						ai[row] += matrix[row][ch] * before.AccelerationIn[ch];
						ao[row] += matrix[row][ch] * before.AccelerationOut[ch];
					}
					if (std::abs(matrix[row][ch]) < 1e-10 || constant[ch])
						continue;
					if (basis >= 0 && evaluated && state.Position == DI_CUSTOM &&
						(std::abs(before.*WeightIn[ch] - before.*WeightIn[basis]) > 1e-10 ||
						 std::abs(before.*WeightOut[ch] - before.*WeightOut[basis]) > 1e-10))
						return false;
					basis = ch;
				}
				key.second.*WeightIn[row] = basis >= 0 ? before.*WeightIn[basis] : 1;
				key.second.*WeightOut[row] = basis >= 0 ? before.*WeightOut[basis] : 1;
				if (basis >= 0 ? bool(before.AccelerationMask & (1u << basis))
							   : evaluated && state.AutoTangents.Method == AutoTangentMethod::MinimumJerk)
					accelerationMask |= 1u << row;
			}
			key.second.X = result[0] + destination[0];
			key.second.Y = result[1] + destination[1];
			key.second.Z = result[2] + destination[2];
			for (int ch = 0; ch < 3; ++ch)
			{
				key.second.*TangentIn[ch] = incoming[ch];
				key.second.*TangentOut[ch] = outgoing[ch];
				key.second.AccelerationIn[ch] = ai[ch];
				key.second.AccelerationOut[ch] = ao[ch];
			}
			key.second.AccelerationMask = (authored.AccelerationMask & 0x78) | accelerationMask;
			key.second.SetOrientation(orientation * key.second.R);
		}
	if (!ValidKeys(keys))
		return false;
	RestoreState(state);
	return true;
}
namespace
{
void OrientationMatrix(const Quaternion &orientation, double matrix[3][3])
{
	const auto angles = orientation.ToQREulerAngles().ToQEulerAngles();
	double forward[3], right[3], up[3];
	MakeVectors(angles.Roll, angles.Pitch, angles.Yaw, forward, right, up);
	for (int row = 0; row < 3; ++row)
	{
		matrix[row][0] = forward[row];
		matrix[row][1] = -right[row];
		matrix[row][2] = up[row];
	}
}
} // namespace
bool CamPath::Rotate(double pitch, double yaw, double roll)
{
	if (m_Map.empty() || !std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(roll))
		return false;
	bool all = true;
	for (const auto &key : m_Map)
		if (key.second.Selected)
			all = false;
	double low[3] = {}, high[3] = {};
	bool first = true;
	for (const auto &key : m_Map)
		if (all || key.second.Selected)
		{
			const double p[] = {key.second.X, key.second.Y, key.second.Z};
			for (int ch = 0; ch < 3; ++ch)
			{
				if (first)
					low[ch] = high[ch] = p[ch];
				else
				{
					low[ch] = (std::min)(low[ch], p[ch]);
					high[ch] = (std::max)(high[ch], p[ch]);
				}
			}
			first = false;
		}
	double center[3];
	for (int ch = 0; ch < 3; ++ch)
		center[ch] = low[ch] * 0.5 + high[ch] * 0.5;
	const auto orientation =
		Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(pitch, yaw, roll)));
	double matrix[3][3];
	OrientationMatrix(orientation, matrix);
	return Transform(matrix, orientation, center, center);
}
bool CamPath::AnchorTransform(double ax, double ay, double az, double ap, double ayaw, double ar, double dx,
							  double dy, double dz, double dp, double dyaw, double dr)
{
	for (double value : {ax, ay, az, ap, ayaw, ar, dx, dy, dz, dp, dyaw, dr})
		if (!std::isfinite(value))
			return false;
	const auto anchor =
				   Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(ap, ayaw, ar))),
			   destination =
				   Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(dp, dyaw, dr)));
	const auto orientation = destination * anchor.Conjugate();
	double matrix[3][3];
	OrientationMatrix(orientation, matrix);
	const double source[] = {ax, ay, az}, target[] = {dx, dy, dz};
	return Transform(matrix, orientation, source, target);
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

bool CamPath::SetOffset(double value)
{
	if (!std::isfinite(value))
		return false;
	if (value == m_Offset)
		return true;
	m_Offset = value;
	Changed(false, false, false);
	return true;
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
		a.DistanceTimingPose != b.DistanceTimingPose || a.SpeedProfile != b.SpeedProfile ||
		a.Modifiers != b.Modifiers || a.Keys.size() != b.Keys.size())
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
		state.SpeedProfile.clear();
		state.Modifiers.clear();
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
		CamPath empty;
		const bool enabled = m_ActivePath.Enabled_get();
		m_ActivePath.RestoreState(empty.CaptureState());
		m_ActivePath.Enabled_set(enabled);
		m_Paths[0].FileName.clear();
		m_Paths[0].Muted = m_Paths[0].Solo = false;
		m_Paths[0].Visible = true;
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
	if (best == m_Paths.size())
	{
		double earliest = std::numeric_limits<double>::infinity();
		for (size_t i = 0; i < m_Paths.size(); ++i)
		{
			const auto &state = i == m_ActiveIndex ? active : *m_Paths[i].Data;
			if (m_Paths[i].Muted || (solo && !m_Paths[i].Solo) || !state.Hold || state.Keys.empty())
				continue;
			const double start = state.Keys.begin()->first + state.Offset;
			if (clientTime < start && start < earliest && GetPath(i)->CanEval())
			{
				earliest = start;
				best = i;
			}
		}
	}
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
	const auto &entry = m_Paths[index];
	const auto &current = index == m_ActiveIndex ? CurrentState() : entry.Data;
	for (const auto &saved : m_Saved.Paths)
		if (saved.Id == entry.Id)
		{
			if (saved.Name != entry.Name || saved.Muted != entry.Muted || saved.Solo != entry.Solo ||
				saved.Visible != entry.Visible)
				return true;
			m_DirtyCaches.resize(m_Paths.size());
			auto &cache = m_DirtyCaches[index];
			if (cache.Current.lock() != current || cache.Saved.lock() != saved.Data)
			{
				cache.Current = current;
				cache.Saved = saved.Data;
				cache.Dirty = current != saved.Data && !SamePathContent(*saved.Data, *current);
			}
			return cache.Dirty;
		}
	return true;
}
bool CamPathCollection::IsDirty() const
{
	if (m_Paths.size() != m_Saved.Paths.size())
		return true;
	for (size_t i = 0; i < m_Paths.size(); ++i)
		if (m_Paths[i].Id != m_Saved.Paths[i].Id || IsDirty(i))
			return true;
	return false;
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
		if (!path.SaveToString(xml))
			return false;
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
	auto estimate = [](const auto &entry)
	{
		size_t size = entry.Data->Keys.size() * (sizeof(CamPathValue) + 64) +
					  entry.Data->Modifiers.size() * sizeof(CamPath::NoiseModifier) +
					  entry.Data->SpeedProfile.size() * sizeof(CamPath::SpeedKey) + entry.Name.size() + 256;
		for (const auto &modifier : entry.Data->Modifiers)
			size += modifier.Envelope.size() * sizeof(CamPath::ModifierKey);
		return size;
	};
	for (const auto &snapshot : history)
		for (const auto &entry : snapshot.Paths)
			bytes += estimate(entry);
	while (history.size() > 1 && (history.size() > 64 || bytes > 64 * 1024 * 1024))
	{
		for (const auto &entry : history.front().Paths)
			bytes -= estimate(entry);
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
	auto state = from.back();
	from.pop_back();
	for (auto &restored : state.Paths)
		for (const auto &current : m_Paths)
			if (restored.Id == current.Id)
				restored.FileName = current.FileName;
	RestoreState(state);
	m_Last = CaptureState();
	return true;
}
