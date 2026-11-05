#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include "stdafx.h"

#include "CamPath.h"

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
#include <iterator>
#include <stdio.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <set>
#include <sstream>

#include <math.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

bool CamPathEase::Valid() const
{
	return Type >= Inherit && Type < Count && Mode >= In && Mode <= InOut && std::isfinite(X1) && std::isfinite(Y1) &&
		   std::isfinite(X2) && std::isfinite(Y2) && X1 >= 0 && X2 <= 1 && X1 <= X2 && std::abs(Y1) <= 5 &&
		   std::abs(Y2) <= 5 && std::isfinite(Overshoot) && Overshoot >= 0 && Overshoot <= 10 &&
		   std::isfinite(Amplitude) && Amplitude >= 1 && Amplitude <= 10 && std::isfinite(Period) && Period >= 0.01 &&
		   Period <= 10;
}

bool CamPathEase::operator==(const CamPathEase & b) const
{
	return Type == b.Type && Mode == b.Mode && X1 == b.X1 && Y1 == b.Y1 && X2 == b.X2 && Y2 == b.Y2 &&
		   Overshoot == b.Overshoot && Amplitude == b.Amplitude && Period == b.Period;
}

const char * CamPathEase::ToString(Function type)
{
	static const char * names[] = {"inherit", "linear", "sine", "quad",	  "cubic",	 "quart", "quint",
								   "expo",	  "circ",	"back", "bounce", "elastic", "bezier"};
	return type >= Inherit && type < Count ? names[type] : "inherit";
}

bool CamPathEase::FromString(const char * text, Function & type)
{
	for (int i = Inherit; i < Count; ++i)
		if (!_stricmp(text, ToString(Function(i))))
		{
			type = Function(i);
			return true;
		}
	return false;
}

const char * CamPathEase::ToString(Direction mode)
{
	return mode == In ? "in" : mode == Out ? "out" : "inout";
}

bool CamPathEase::FromString(const char * text, Direction & mode)
{
	for (auto value : {In, Out, InOut})
		if (!_stricmp(text, ToString(value)))
		{
			mode = value;
			return true;
		}
	return false;
}

// The curve's X axis is time; solve X before evaluating its Y coordinate.
double CamPathEase::Eval(double u) const
{
	if (u <= 0)
		return 0;
	if (u >= 1)
		return 1;
	if (Type == Bezier)
	{
		auto cubic = [](double t, double a, double b) {
			double v = 1 - t;
			return 3 * v * v * t * a + 3 * v * t * t * b + t * t * t;
		};
		double lo = 0, hi = 1;
		for (int i = 0; i < 48; ++i)
		{
			double t = (lo + hi) * 0.5;
			if (cubic(t, X1, X2) < u)
				lo = t;
			else
				hi = t;
		}
		return cubic((lo + hi) * 0.5, Y1, Y2);
	}
	auto bounceOut = [](double x) {
		if (x < 1 / 2.75)
			return 7.5625 * x * x;
		if (x < 2 / 2.75)
		{
			x -= 1.5 / 2.75;
			return 7.5625 * x * x + 0.75;
		}
		if (x < 2.5 / 2.75)
		{
			x -= 2.25 / 2.75;
			return 7.5625 * x * x + 0.9375;
		}
		x -= 2.625 / 2.75;
		return 7.5625 * x * x + 0.984375;
	};
	auto easeIn = [&](double x) {
		switch (Type)
		{
		case Sine:
			return 1 - std::cos(x * M_PI / 2);
		case Quad:
			return x * x;
		case Cubic:
			return x * x * x;
		case Quart:
			return x * x * x * x;
		case Quint:
			return x * x * x * x * x;
		case Expo:
			return std::expm1(10 * std::log(2.0) * x) / 1023;
		case Circ:
			return 1 - std::sqrt((std::max)(0.0, 1 - x * x));
		case Back:
			return x * x * ((Overshoot + 1) * x - Overshoot);
		case Bounce:
			return 1 - bounceOut(1 - x);
		case Elastic: {
			if (x == 0 || x == 1)
				return x;
			double phase = Period / (2 * M_PI) * std::asin(1 / Amplitude);
			auto wave = [&](double t) {
				return -Amplitude * std::pow(2.0, 10 * (t - 1)) * std::sin((t - 1 - phase) * 2 * M_PI / Period);
			};
			double initial = wave(0);
			return (wave(x) - initial) / (1 - initial);
		}
		default:
			return x;
		}
	};
	if (Mode == In)
		return easeIn(u);
	if (Mode == Out)
		return 1 - easeIn(1 - u);
	return u < 0.5 ? 0.5 * easeIn(2 * u) : 1 - 0.5 * easeIn(2 - 2 * u);
}

namespace
{
void ValidateKeys(const CInterpolationMap<CamPathValue> & keys)
{
	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		double release = it->first + it->second.HoldDuration;
		if (!std::isfinite(it->first) || !std::isfinite(release) || !std::isfinite(it->second.HoldDuration) ||
			it->second.HoldDuration < 0 || !it->second.Ease.Valid())
			throw std::invalid_argument("Invalid keyframe hold");
		auto next = std::next(it);
		if (it->second.HoldDuration > 0 && next != keys.end() && release >= next->first)
			throw std::invalid_argument("Hold release must precede the next keyframe arrival");
	}
}
bool ReadCampathFile(const wchar_t * file, std::string & xml)
{
	std::ifstream input(std::filesystem::path(file), std::ios::binary | std::ios::ate);
	if (!input)
		return false;
	auto length = input.tellg();
	if (length < 0 || length > 64 * 1024 * 1024)
		return false;
	xml.resize(std::size_t(length));
	input.seekg(0);
	return bool(input.read(xml.data(), std::streamsize(length)));
}
} // namespace
bool CamPathValue::operator==(const CamPathValue & b) const
{
	return X == b.X && Y == b.Y && Z == b.Z && R.W == b.R.W && R.X == b.R.X && R.Y == b.R.Y && R.Z == b.R.Z &&
		   Fov == b.Fov && Selected == b.Selected && InHandle.X == b.InHandle.X && InHandle.Y == b.InHandle.Y &&
		   InHandle.Z == b.InHandle.Z && OutHandle.X == b.OutHandle.X && OutHandle.Y == b.OutHandle.Y &&
		   OutHandle.Z == b.OutHandle.Z && HasInHandle == b.HasInHandle && HasOutHandle == b.HasOutHandle &&
		   HandlesAligned == b.HandlesAligned && HoldDuration == b.HoldDuration && Ease == b.Ease;
}
bool CamPath::State::operator==(const State & b) const
{
	return Keys == b.Keys && Position == b.Position && Fov == b.Fov && Rotation == b.Rotation && Offset == b.Offset &&
		   Enabled == b.Enabled && Hold == b.Hold && Bezier == b.Bezier && CurveEditing == b.CurveEditing &&
		   Name == b.Name && Ease == b.Ease;
}
CamPath::State CamPath::CaptureState() const
{
	State s;
	s.Keys = m_Map;
	s.Position = m_PositionInterpMethod;
	s.Fov = m_FovInterpMethod;
	s.Rotation = m_RotationInterpMethod;
	s.Offset = m_Offset;
	s.Enabled = m_Enabled;
	s.Hold = m_Hold;
	s.Bezier = m_Bezier;
	s.CurveEditing = m_CurveEditing;
	s.Name = m_Name;
	s.Ease = m_Ease;
	return s;
}
void CamPath::RestoreState(const State & s)
{
	ValidateKeys(s.Keys);
	if (s.Name.size() > 256 || !std::isfinite(s.Offset) || !s.Ease.Valid())
		throw std::invalid_argument("Invalid path state");
	m_Map = s.Keys;
	m_Offset = s.Offset;
	m_Enabled = s.Enabled;
	m_Hold = s.Hold;
	m_Bezier = s.Bezier;
	m_CurveEditing = s.CurveEditing;
	m_Name = s.Name;
	m_Ease = s.Ease;
	PositionInterpMethod_set(s.Position);
	RotationInterpMethod_set(s.Rotation);
	FovInterpMethod_set(s.Fov);
	DoInterpolationMapChangedAll();
	Changed();
}
void CamPath::SetName(const std::string & name)
{
	if (name.size() > 256)
		throw std::invalid_argument("Path name exceeds 256 bytes");
	m_Name = name;
	Changed();
}
void CamPath::Validate() const
{
	ValidateKeys(m_Map);
	if (!std::isfinite(m_Offset))
		throw std::invalid_argument("Invalid path offset");
}
bool CamPath::SetKeyHold(double time, double release)
{
	auto it = m_Map.find(time);
	if (it == m_Map.end() || !std::isfinite(time) || !std::isfinite(release) || !std::isfinite(release - time) ||
		release < time)
		return false;
	auto next = std::next(it);
	if (release > time && next != m_Map.end() && release >= next->first)
		return false;
	it->second.HoldDuration = release - time;
	Changed();
	return true;
}

void CamPath::SetEase(const CamPathEase & value, bool clearOverrides)
{
	if (!value.Valid())
		throw std::invalid_argument("Invalid easing controls");
	m_Ease = value;
	if (clearOverrides)
		for (auto & key : m_Map)
			key.second.Ease = CamPathEase();
	Changed();
}

bool CamPath::SetKeyEase(double time, const CamPathEase & value)
{
	if (!std::isfinite(time))
		return false;
	auto key = m_Map.find(time);
	if (key == m_Map.end() || std::next(key) == m_Map.end() || !value.Valid())
		return false;
	key->second.Ease = value;
	Changed();
	return true;
}

CamPathEase CamPath::GetKeyEase(double time, bool forPlayback) const
{
	auto key = m_Map.find(time);
	CamPathEase ease = key != m_Map.end() && key->second.Ease.Type != CamPathEase::Inherit ? key->second.Ease : m_Ease;
	if (forPlayback && key != m_Map.end() && ease.Type == CamPathEase::Bezier && SmoothHolds())
	{
		auto next = std::next(key);
		if (key->second.HoldDuration > 0)
		{
			ease.Y1 = 0;
			ease.X1 = (std::max)(ease.X1, 0.001);
			ease.X2 = (std::max)(ease.X1, ease.X2);
		}
		if (next != m_Map.end() && next->second.HoldDuration > 0)
		{
			ease.Y2 = 1;
			ease.X2 = (std::min)(ease.X2, 0.999);
			ease.X1 = (std::min)(ease.X1, ease.X2);
		}
	}
	return ease;
}

bool CamPath::SmoothHolds() const
{
	return (m_CurveEditing && m_Bezier) || (m_PositionInterpMethod != DI_LINEAR && m_PositionInterpMethod != DI_STEP) ||
		   (m_RotationInterpMethod != QI_SLINEAR && m_RotationInterpMethod != QI_STEP) ||
		   (m_FovInterpMethod != DI_LINEAR && m_FovInterpMethod != DI_STEP);
}

double CamPath::GetSegmentProgress(double time, double at) const
{
	auto key = m_Map.find(time);
	if (key == m_Map.end())
		return 0;
	auto next = std::next(key);
	if (next == m_Map.end())
		return 0;
	double release = time + key->second.HoldDuration;
	if (at <= release)
		return 0;
	if (at >= next->first)
		return 1;
	double u = (at - release) / (next->first - release);
	CamPathEase ease = GetKeyEase(time, true);
	if (SmoothHolds() && ease.Type != CamPathEase::Bezier)
	{
		bool depart = key->second.HoldDuration > 0, arrive = next->second.HoldDuration > 0;
		// Zero velocity/acceleration at a hold, while preserving arrival times.
		auto start = [](double x) { return x * x * x * (6 - 8 * x + 3 * x * x); };
		if (depart && arrive)
			u = u * u * u * (10 + u * (-15 + 6 * u));
		else if (depart)
			u = start(u);
		else if (arrive)
			u = 1 - start(1 - u);
	}
	return ease.Eval(u);
}

bool CamPath::DoubleInterp_FromString(char const * value, DoubleInterp & outValue)
{
	if(!_stricmp(value,"default"))
	{
		outValue = DI_DEFAULT;
		return true;
	}
	else if (!_stricmp(value, "linear"))
	{
		outValue = DI_LINEAR;
		return true;
	}
	else if (!_stricmp(value, "cubic"))
	{
		outValue = DI_CUBIC;
		return true;
	}

	if (!_stricmp(value, "smooth"))
	{
		outValue = DI_SMOOTH;
		return true;
	}
	if (!_stricmp(value, "monotone"))
	{
		outValue = DI_MONOTONE;
		return true;
	}
	if (!_stricmp(value, "step"))
	{
		outValue = DI_STEP;
		return true;
	}

	return false;
}

char const * CamPath::DoubleInterp_ToString(DoubleInterp value)
{
	switch(value)
	{
	case DI_SMOOTH:
		return "smooth";
	case DI_MONOTONE:
		return "monotone";
	case DI_STEP:
		return "step";
	case DI_DEFAULT:
		return "default";
	case DI_LINEAR:
		return "linear";
	case DI_CUBIC:
		return "cubic";
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
	else if (!_stricmp(value, "sLinear"))
	{
		outValue = QI_SLINEAR;
		return true;
	}
	else if (!_stricmp(value, "sCubic"))
	{
		outValue = QI_SCUBIC;
		return true;
	}

	if (!_stricmp(value, "sSmooth"))
	{
		outValue = QI_SMOOTH;
		return true;
	}
	if (!_stricmp(value, "sStep"))
	{
		outValue = QI_STEP;
		return true;
	}

	return false;
}

char const * CamPath::QuaternionInterp_ToString(QuaternionInterp value)
{
	switch(value)
	{
	case QI_SMOOTH:
		return "sSmooth";
	case QI_STEP:
		return "sStep";
	case QI_DEFAULT:
		return "default";
	case QI_SLINEAR:
		return "sLinear";
	case QI_SCUBIC:
		return "sCubic";
	}

	return "[unkown]";
}

CamPathValue::CamPathValue() : X(0.0), Y(0.0), Z(0.0), R(), Fov(90.0), Selected(false)
{
}

CamPathValue::CamPathValue(double x, double y, double z, double pitch, double yaw, double roll, double fov)
	: X(x), Y(y), Z(z),
	  R(Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(pitch, yaw, roll)))), Fov(fov),
	  Selected(false)
{
}

CamPathValue::CamPathValue(double x, double y, double z, double q_w, double q_x, double q_y, double q_z, double fov,
						   bool selected)
	: X(x), Y(y), Z(z), R(Quaternion(q_w, q_x, q_y, q_z)), Fov(fov), Selected(selected)
{
}

CamPathIterator::CamPathIterator(CInterpolationMap<CamPathValue>::const_iterator const & it) : wrapped(it)
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
	  m_FovInterpMethod(DI_DEFAULT), m_XView(&m_Map, XSelector), m_YView(&m_Map, YSelector), m_ZView(&m_Map, ZSelector),
	  m_RView(&m_Map, RSelector), m_FovView(&m_Map, FovSelector), m_SelectedView(&m_Map, SelectedSelector)
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
	m_Hold = value;
}

void CamPath::PositionInterpMethod_set(DoubleInterp value)
{
	delete m_XInterp;
	delete m_YInterp;
	delete m_ZInterp;

	m_PositionInterpMethod = value;

	switch(value)
	{
	case DI_SMOOTH:
	case DI_MONOTONE:
	case DI_STEP: {
		auto mode = value == DI_SMOOTH	   ? CLocalDoubleInterpolation<CamPathValue>::Smooth
					: value == DI_MONOTONE ? CLocalDoubleInterpolation<CamPathValue>::Monotone
										   : CLocalDoubleInterpolation<CamPathValue>::Step;
		m_XInterp = new CLocalDoubleInterpolation<CamPathValue>(&m_XView, mode);
		m_YInterp = new CLocalDoubleInterpolation<CamPathValue>(&m_YView, mode);
		m_ZInterp = new CLocalDoubleInterpolation<CamPathValue>(&m_ZView, mode);
		break;
	}

	case DI_LINEAR:
		m_XInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_XView);
		m_YInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_YView);
		m_ZInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_ZView);
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
	delete m_RInterp;

	m_RotationInterpMethod = value;

	switch(value)
	{
	case QI_SMOOTH:
	case QI_STEP:
		m_RInterp = new CLocalQuaternionInterpolation<CamPathValue>(&m_RView, value == QI_STEP);
		break;

	case QI_SLINEAR:
		m_RInterp = new CSLinearQuaternionInterpolation<CamPathValue>(&m_RView);
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
	delete m_FovInterp;

	m_FovInterpMethod = value;

	switch(value)
	{
	case DI_SMOOTH:
	case DI_MONOTONE:
	case DI_STEP: {
		auto mode = value == DI_SMOOTH	   ? CLocalDoubleInterpolation<CamPathValue>::Smooth
					: value == DI_MONOTONE ? CLocalDoubleInterpolation<CamPathValue>::Monotone
										   : CLocalDoubleInterpolation<CamPathValue>::Step;
		m_FovInterp = new CLocalDoubleInterpolation<CamPathValue>(&m_FovView, mode);
		break;
	}

	case DI_LINEAR:
		m_FovInterp = new CLinearDoubleInterpolation<CamPathValue>(&m_FovView);
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

void CamPath::Add(double time, const CamPathValue & value)
{
	if (!std::isfinite(time) || !std::isfinite(value.HoldDuration) || value.HoldDuration < 0 ||
		!std::isfinite(time + value.HoldDuration) || !value.Ease.Valid())
		throw std::invalid_argument("Invalid keyframe hold/time");
	auto next = m_Map.lower_bound(time);
	auto following = next;
	if (next != m_Map.end() && next->first == time)
		++following;
	if (value.HoldDuration > 0 && following != m_Map.end() && time + value.HoldDuration >= following->first)
		throw std::invalid_argument("Hold release must precede the next keyframe arrival");
	if (next != m_Map.begin())
	{
		auto previous = std::prev(next);
		if (previous->second.HoldDuration > 0 && previous->first + previous->second.HoldDuration >= time)
			throw std::invalid_argument("New arrival overlaps the previous keyframe hold");
	}
	m_Map[time] = value;
	DoInterpolationMapChangedAll();
	Changed();
}

void CamPath::Changed()
{
	for (m_OnChangedIt = m_OnChanged.begin(); m_OnChangedIt != m_OnChanged.end(); m_OnChangedIt++)
	{
		m_OnChangedIt->Notify();
		if (m_OnChangedIt == m_OnChanged.end())
			break;
	}
}

void CamPath::CurveEditing_set(bool enabled)
{
	m_CurveEditing = enabled;
	Changed();
}
void CamPath::Bezier_set(bool enabled)
{
	m_Bezier = enabled;
	Changed();
}
bool CamPath::GetHandle(double time, bool outgoing, Vector3 & offset) const
{
	if (!std::isfinite(time))
		return false;
	auto point = m_Map.find(time);
	if (point == m_Map.end())
		return false;
	const auto & value = point->second;
	if (outgoing ? value.HasOutHandle : value.HasInHandle)
	{
		offset = outgoing ? value.OutHandle : value.InHandle;
		return true;
	}
	auto next = point;
	if (outgoing)
	{
		if (++next == m_Map.end())
		{
			offset = {0, 0, 0};
			return true;
		}
	}
	else
	{
		if (point == m_Map.begin())
		{
			offset = {0, 0, 0};
			return true;
		}
		--next;
	}
	double span = next->first - time;
	if (!m_XInterp->CanEval() || !m_YInterp->CanEval() || !m_ZInterp->CanEval())
	{
		offset = Vector3(next->second.X - value.X, next->second.Y - value.Y, next->second.Z - value.Z) * (1.0 / 3);
		return true;
	}
	// Derive automatic handles from the existing interpolator. Only segments
	// with an explicitly edited handle use Bezier evaluation below.
	double epsilon = (std::min)(std::abs(span) * 1e-4, 1e-3);
	double a = (std::max)(m_Map.cbegin()->first, time - epsilon),
		   b = (std::min)(m_Map.crbegin()->first, time + epsilon);
	if (b <= a)
	{
		offset = {0, 0, 0};
		return true;
	}
	offset = Vector3(m_XInterp->Eval(b) - m_XInterp->Eval(a), m_YInterp->Eval(b) - m_YInterp->Eval(a),
					 m_ZInterp->Eval(b) - m_ZInterp->Eval(a)) *
			 (span / (3 * (b - a)));
	return true;
}
namespace
{
bool ValidCampathHandle(const Vector3 & handle)
{
	return std::isfinite(handle.X) && std::isfinite(handle.Y) && std::isfinite(handle.Z) && std::abs(handle.X) <= 1e9 &&
		   std::abs(handle.Y) <= 1e9 && std::abs(handle.Z) <= 1e9;
}
bool StoreCampathHandle(CamPathValue & value, bool outgoing, const Vector3 & offset, const Vector3 & opposite)
{
	if (!ValidCampathHandle(offset))
		return false;
	double length = std::hypot(offset.X, offset.Y, offset.Z);
	if (value.HandlesAligned && length > 0)
	{
		double otherLength = std::hypot(opposite.X, opposite.Y, opposite.Z);
		Vector3 aligned(-offset.X / length * otherLength, -offset.Y / length * otherLength,
						-offset.Z / length * otherLength);
		if (!ValidCampathHandle(aligned))
			return false;
		(outgoing ? value.InHandle : value.OutHandle) = aligned;
		(outgoing ? value.HasInHandle : value.HasOutHandle) = true;
	}
	(outgoing ? value.OutHandle : value.InHandle) = offset;
	(outgoing ? value.HasOutHandle : value.HasInHandle) = true;
	return true;
}
} // namespace
bool CamPath::SetHandle(double time, bool outgoing, const Vector3 & offset)
{
	if (!std::isfinite(time))
		return false;
	auto point = m_Map.find(time);
	if (!m_CurveEditing || point == m_Map.end())
		return false;
	CamPathValue value = point->second;
	Vector3 opposite;
	GetHandle(time, !outgoing, opposite);
	if (!StoreCampathHandle(value, outgoing, offset, opposite))
		return false;
	point->second = value;
	m_Bezier = true;
	Changed();
	return true;
}
bool CamPath::SetHandleMode(double time, bool aligned)
{
	if (!std::isfinite(time))
		return false;
	auto point = m_Map.find(time);
	if (!m_CurveEditing || point == m_Map.end())
		return false;
	CamPathValue value = point->second;
	value.HandlesAligned = aligned;
	if (aligned && (value.HasInHandle || value.HasOutHandle))
	{
		// Prefer the outgoing direction when aligning an existing corner. Keep
		// both lengths, including the automatic handle when only one was edited.
		bool outgoing = value.HasOutHandle && std::hypot(value.OutHandle.X, value.OutHandle.Y, value.OutHandle.Z) > 0;
		Vector3 offset, opposite;
		GetHandle(time, outgoing, offset);
		GetHandle(time, !outgoing, opposite);
		if (!StoreCampathHandle(value, outgoing, offset, opposite))
			return false;
	}
	point->second = value;
	Changed();
	return true;
}
bool CamPath::ResetHandles(double time)
{
	auto point = m_Map.find(time);
	if (!m_CurveEditing || point == m_Map.end())
		return false;
	point->second.HasInHandle = point->second.HasOutHandle = false;
	Changed();
	return true;
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

	if (selectAll)
		m_Map.clear();

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
	auto last = std::prev(m_Map.cend());
	return last->first + last->second.HoldDuration;
}

bool CamPath::CanEval(void) const
{
	if (m_Map.size() == 1 && m_Map.begin()->second.HoldDuration > 0)
		return true;
	return m_XInterp->CanEval() && m_YInterp->CanEval() && m_ZInterp->CanEval() && m_RInterp->CanEval() &&
		   m_FovInterp->CanEval() && m_SelectedInterp->CanEval();
}

CamPathValue CamPath::Eval(double t)
{
	if (m_Map.size() == 1 && m_Map.begin()->second.HoldDuration > 0)
		return m_Map.begin()->second;
	auto next = m_Map.upper_bound(t);
	if (next == m_Map.begin())
		return EvalBase(t);
	auto key = std::prev(next);
	if (key->second.HoldDuration > 0 && t <= key->first + key->second.HoldDuration)
		return key->second;
	if (next == m_Map.end())
		return EvalBase(t);
	CamPathEase ease = GetKeyEase(key->first);
	if (key->second.HoldDuration == 0 && next->second.HoldDuration == 0 &&
		(ease.Type == CamPathEase::Inherit || ease.Type == CamPathEase::Linear))
		return EvalBase(t);
	double progress = GetSegmentProgress(key->first, t), span = next->first - key->first;
	if (progress >= 0 && progress <= 1)
		return EvalBase(key->first + progress * span);
	// Overshooting easing continues the current segment's endpoint tangent,
	// rather than jumping into another segment of the global interpolator.
	bool before = progress < 0;
	double end = before ? key->first : next->first;
	double sampleTime = end + (before ? 1 : -1) * span * 1e-3;
	CamPathValue value = EvalBase(end), sample = EvalBase(sampleTime);
	double factor = (key->first + progress * span - end) / (sampleTime - end);
	if (m_PositionInterpMethod != DI_STEP || (m_CurveEditing && m_Bezier))
	{
		value.X += (sample.X - value.X) * factor;
		value.Y += (sample.Y - value.Y) * factor;
		value.Z += (sample.Z - value.Z) * factor;
	}
	if (m_FovInterpMethod != DI_STEP)
		value.Fov += (sample.Fov - value.Fov) * factor;
	if (m_RotationInterpMethod != QI_STEP)
	{
		if (DotProduct(value.R, sample.R) < 0)
			sample.R = -1.0 * sample.R;
		value.R = value.R.Slerp(sample.R, factor);
	}
	return value;
}

CamPathValue CamPath::EvalBase(double t)
{
	CamPathValue val;
	
	val.X = m_XInterp->Eval(t);
	val.Y = m_YInterp->Eval(t);
	val.Z = m_ZInterp->Eval(t);
	val.R = m_RInterp->Eval(t);
	val.Fov = m_FovInterp->Eval(t);
	val.Selected = m_SelectedInterp->Eval(t);
	if (m_CurveEditing && m_Bezier && m_Map.size() > 1)
	{
		auto b = m_Map.upper_bound(t);
		if (b != m_Map.begin() && b != m_Map.end())
		{
			auto a = std::prev(b);
			if (a->second.HasOutHandle || b->second.HasInHandle)
			{
				Vector3 out, in;
				GetHandle(a->first, true, out);
				GetHandle(b->first, false, in);
				Vector3 p0(a->second.X, a->second.Y, a->second.Z), p3(b->second.X, b->second.Y, b->second.Z);
				double u = (t - a->first) / (b->first - a->first), v = 1 - u;
				Vector3 pos =
					p0 * (v * v * v) + (p0 + out) * (3 * v * v * u) + (p3 + in) * (3 * v * u * u) + p3 * (u * u * u);
				val.X = pos.X;
				val.Y = pos.Y;
				val.Z = pos.Z;
			}
		}
	}
	return val;
}

char * double2xml(rapidxml::xml_document<> & doc, double value)
{
	char szTmp[196];
	_snprintf_s(szTmp, _TRUNCATE, "%.17g", value);
	return doc.allocate_string(szTmp);
}

namespace
{
void WriteEase(rapidxml::xml_document<> & doc, rapidxml::xml_node<> * node, const CamPathEase & ease)
{
	if (ease == CamPathEase())
		return;
	node->append_attribute(doc.allocate_attribute("ease", CamPathEase::ToString(ease.Type)));
	node->append_attribute(doc.allocate_attribute("easeMode", CamPathEase::ToString(ease.Mode)));
	for (auto item : {std::make_pair("easeX1", ease.X1),
					  {"easeY1", ease.Y1},
					  {"easeX2", ease.X2},
					  {"easeY2", ease.Y2},
					  {"easeOvershoot", ease.Overshoot},
					  {"easeAmplitude", ease.Amplitude},
					  {"easePeriod", ease.Period}})
		node->append_attribute(doc.allocate_attribute(item.first, double2xml(doc, item.second)));
}

CamPathEase ReadEase(rapidxml::xml_node<> * node)
{
	CamPathEase ease;
	if (auto attr = node->first_attribute("ease"))
		if (!CamPathEase::FromString(attr->value(), ease.Type))
			throw std::invalid_argument("Unknown easing function");
	if (auto attr = node->first_attribute("easeMode"))
		if (!CamPathEase::FromString(attr->value(), ease.Mode))
			throw std::invalid_argument("Unknown easing direction");
	for (auto item : {std::make_pair("easeX1", &ease.X1),
					  {"easeY1", &ease.Y1},
					  {"easeX2", &ease.X2},
					  {"easeY2", &ease.Y2},
					  {"easeOvershoot", &ease.Overshoot},
					  {"easeAmplitude", &ease.Amplitude},
					  {"easePeriod", &ease.Period}})
	{
		if (auto attr = node->first_attribute(item.first))
		{
			char * end = nullptr;
			*item.second = std::strtod(attr->value(), &end);
			if (end == attr->value() || *end)
				throw std::invalid_argument("Invalid easing parameter");
		}
	}
	if (!ease.Valid())
		throw std::invalid_argument("Invalid easing controls");
	return ease;
}
} // namespace

std::string CamPath::ToXml()
{
	rapidxml::xml_document<> doc;

	rapidxml::xml_node<> * decl = doc.allocate_node(rapidxml::node_declaration);
	decl->append_attribute(doc.allocate_attribute("version", "1.0"));
	decl->append_attribute(doc.allocate_attribute("encoding", "utf-8"));
	doc.append_node(decl);

	rapidxml::xml_node<> * cam = doc.allocate_node(rapidxml::node_element, "campath");
	WriteEase(doc, cam, m_Ease);
	if (!m_Name.empty())
		cam->append_attribute(doc.allocate_attribute("name", doc.allocate_string(m_Name.c_str())));

	if (m_Bezier)
		cam->append_attribute(doc.allocate_attribute("curve", "bezier"));
	if(DI_DEFAULT != m_PositionInterpMethod)
		cam->append_attribute(doc.allocate_attribute("positionInterp", DoubleInterp_ToString(m_PositionInterpMethod)));
	if(QI_DEFAULT != m_RotationInterpMethod)
		cam->append_attribute(
			doc.allocate_attribute("rotationInterp", QuaternionInterp_ToString(m_RotationInterpMethod)));
	if(DI_DEFAULT != m_FovInterpMethod)
		cam->append_attribute(doc.allocate_attribute("fovInterp", DoubleInterp_ToString(m_FovInterpMethod)));
	if (m_Offset)
		cam->append_attribute(doc.allocate_attribute("offset", double2xml(doc, m_Offset)));
	if (m_Hold)
		cam->append_attribute(doc.allocate_attribute("hold"));
	doc.append_node(cam);

	rapidxml::xml_node<> * pts = doc.allocate_node(rapidxml::node_element, "points");
	cam->append_node(pts);

	rapidxml::xml_node<> * cmt =
		doc.allocate_node(rapidxml::node_comment, 0,
						  "Points are in Quake coordinates, meaning x=forward, y=left, z=up and rotation order is "
						  "first rx, then ry and lastly rz.\n"
		"Rotation direction follows the right-hand grip rule.\n"
		"rx (roll), ry (pitch), rz(yaw) are the Euler angles in degrees.\n"
		"qw, qx, qy, qz are the quaternion values.\n"
		"When read it is sufficient that either rx, ry, rz OR qw, qx, qy, qz are present.\n"
						  "If both are present then qw, qx, qy, qz take precedence.");
	pts->append_node(cmt);

	for(CamPathIterator it = GetBegin(); it != GetEnd(); ++it)
	{
		double time = it.GetTime();
		CamPathValue val = it.GetValue();
		QEulerAngles ang = val.R.ToQREulerAngles().ToQEulerAngles();

		rapidxml::xml_node<> * pt = doc.allocate_node(rapidxml::node_element, "p");
		WriteEase(doc, pt, val.Ease);
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

		pt->append_attribute(doc.allocate_attribute("handleMode", val.HandlesAligned ? "aligned" : "free"));
		if (val.HoldDuration > 0)
			pt->append_attribute(doc.allocate_attribute("holdDuration", double2xml(doc, val.HoldDuration)));

		if (val.HasInHandle)
		{
			pt->append_attribute(doc.allocate_attribute("inX", double2xml(doc, val.InHandle.X)));
			pt->append_attribute(doc.allocate_attribute("inY", double2xml(doc, val.InHandle.Y)));
			pt->append_attribute(doc.allocate_attribute("inZ", double2xml(doc, val.InHandle.Z)));
		}
		if (val.HasOutHandle)
		{
			pt->append_attribute(doc.allocate_attribute("outX", double2xml(doc, val.OutHandle.X)));
			pt->append_attribute(doc.allocate_attribute("outY", double2xml(doc, val.OutHandle.Y)));
			pt->append_attribute(doc.allocate_attribute("outZ", double2xml(doc, val.OutHandle.Z)));
		}

		if(val.Selected)
			pt->append_attribute(doc.allocate_attribute("selected"));

		pts->append_node(pt);
	}

	std::string xmlString;
	rapidxml::print(std::back_inserter(xmlString), doc);

	return xmlString;
}
bool CamPath::Save(wchar_t const * fileName)
{
	std::ofstream output(std::filesystem::path(fileName), std::ios::binary);
	if (!output)
		return false;
	output << ToXml();
	return bool(output);
}

bool CamPath::Load(const wchar_t * fileName)
{
	std::string xml;
	return ReadCampathFile(fileName, xml) && LoadXml(xml);
}
bool CamPath::LoadXml(const std::string & xml)
{
	if (xml.size() > 64 * 1024 * 1024)
		return false;
	bool bOk = true;
	std::vector<char> data(xml.begin(), xml.end());
	data.push_back(0);
	char * pData = data.data();

	CInterpolationMap<CamPathValue> previous;
	CopyMap(previous, m_Map);
	auto position = m_PositionInterpMethod;
	auto rotation = m_RotationInterpMethod;
	auto fov = m_FovInterpMethod;
	double oldOffset = m_Offset;
	std::string oldName = m_Name;
	CamPathEase oldEase = m_Ease;
	bool oldHold = m_Hold, oldBezier = m_Bezier;

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

				m_Ease = ReadEase(cur_node);
				auto name = cur_node->first_attribute("name");
				if (name)
					m_Name = name->value();

				// Loading optional handles preserves the caller's curve-editing capability.
				auto curve = cur_node->first_attribute("curve");
				m_Bezier = curve && !_stricmp(curve->value(), "bezier");

				// Clear current Campath:
				SelectNone();
				Clear();

				rapidxml::xml_attribute<> * positionInterpA = cur_node->first_attribute("positionInterp");
				DoubleInterp positionInterp = DI_DEFAULT;
				if (positionInterpA)
					DoubleInterp_FromString(positionInterpA->value(), positionInterp);
				PositionInterpMethod_set(positionInterp);

				rapidxml::xml_attribute<> * rotationInterpA = cur_node->first_attribute("rotationInterp");
				QuaternionInterp rotationInterp = QI_DEFAULT;
				if (rotationInterpA)
					QuaternionInterp_FromString(rotationInterpA->value(), rotationInterp);
				RotationInterpMethod_set(rotationInterp);

				rapidxml::xml_attribute<> * fovInterpA = cur_node->first_attribute("fovInterp");
				DoubleInterp fovInterp = DI_DEFAULT;
				if (fovInterpA)
					DoubleInterp_FromString(fovInterpA->value(), fovInterp);
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
					if (!timeAttr)
						continue;

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

					CamPathValue r;
					auto handleMode = cur_node->first_attribute("handleMode");
					if (handleMode && _stricmp(handleMode->value(), "aligned") && _stricmp(handleMode->value(), "free"))
						throw std::invalid_argument("Invalid handle mode");
					r.Ease = ReadEase(cur_node);
					r.X = dX;
					r.Y = dY;
					r.Z = dZ;
					r.Fov = dFov;
					r.Selected = selectedA != nullptr;
					auto holdDuration = cur_node->first_attribute("holdDuration");
					if (holdDuration)
					{
						char * end = nullptr;
						r.HoldDuration = strtod(holdDuration->value(), &end);
						if (end == holdDuration->value() || *end || !std::isfinite(r.HoldDuration) ||
							r.HoldDuration < 0)
						{
							bOk = false;
							break;
						}
					}

					if (qwA && qxA && qyA && qzA)
						r.R =
							Quaternion(atof(qwA->value()), atof(qxA->value()), atof(qyA->value()), atof(qzA->value()));
					else
						r.R = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(
							ryA ? atof(ryA->value()) : 0, rzA ? atof(rzA->value()) : 0, rxA ? atof(rxA->value()) : 0)));
					for (bool outgoing : {false, true})
					{
						auto x = cur_node->first_attribute(outgoing ? "outX" : "inX"),
							 y = cur_node->first_attribute(outgoing ? "outY" : "inY"),
							 z = cur_node->first_attribute(outgoing ? "outZ" : "inZ");
						if (!x && !y && !z)
							continue;
						if (!x || !y || !z)
						{
							bOk = false;
							break;
						}
						Vector3 offset;
						double * components[] = {&offset.X, &offset.Y, &offset.Z};
						int axis = 0;
						for (auto attr : {x, y, z})
						{
							char * end = nullptr;
							double number = strtod(attr->value(), &end);
							if (end == attr->value() || *end || !std::isfinite(number) || std::abs(number) > 1e9)
							{
								bOk = false;
								break;
							}
							*components[axis++] = number;
						}
						if (!bOk)
							break;
						if (outgoing)
						{
							r.OutHandle = offset;
							r.HasOutHandle = true;
					}
					else
					{
							r.InHandle = offset;
							r.HasInHandle = true;
						}
					}
					if (!bOk)
						break;
					// Older files keep explicitly authored corners. Unedited native
					// keyframes and newly captured keys use aligned handles.
					r.HandlesAligned =
						handleMode ? !_stricmp(handleMode->value(), "aligned") : !r.HasInHandle && !r.HasOutHandle;
					m_Map[dT] = r;
					}
			} while (false);
		}
		catch (const std::exception &)
		{
			bOk=false;
		}
	}

	if (bOk)
	{
		try
		{
			Validate();
			if (m_Name.size() > 256)
				bOk = false;
		}
		catch (const std::exception &)
		{
			bOk = false;
		}
	}

	if (!bOk)
	{
		m_Name = oldName;
		m_Ease = oldEase;
		CopyMap(m_Map, previous);
		m_Bezier = oldBezier;
		m_Hold = oldHold;
		m_Offset = oldOffset;
		PositionInterpMethod_set(position);
		RotationInterpMethod_set(rotation);
		FovInterpMethod_set(fov);
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
	Changed();

	return m_Map.size();
}

void CamPath::SelectNone()
{
	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = false;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();
}

size_t CamPath::SelectInvert()
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = !it->second.Selected;

		if (it->second.Selected)
			++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

size_t CamPath::SelectAdd(size_t min, size_t max)
{
	size_t i = 0;
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= i && i <= max;

		if (it->second.Selected)
			++selected;

		++i;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

size_t CamPath::SelectAdd(double min, size_t count)
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= it->first && selected < count;

		if (it->second.Selected)
			++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

size_t CamPath::SelectAdd(double min, double max)
{
	size_t selected = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		it->second.Selected = it->second.Selected || min <= it->first && it->first <= max;

		if (it->second.Selected)
			++selected;
	}

	m_SelectedInterp->InterpolationMapChanged();
	Changed();

	return selected;
}

void CamPath::SetStart(double t, bool relative)
{
	if (m_Map.size() < 1)
		return;

	CInterpolationMap<CamPathValue> tempMap;

	bool selectAll = true;
	double first = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				first = it->first;
				break;
			}
		}
	}

	double deltaT = relative ? t : (selectAll ? t -m_Map.begin()->first : t -first);

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if(selectAll || curValue.Selected)
		{
			tempMap[deltaT+curT] = curValue;
		}
		else
		{
			tempMap[curT] = curValue;
		}
	}

	ValidateKeys(tempMap);
	CopyMap(m_Map, tempMap);

	DoInterpolationMapChangedAll();

	Changed();
}
	
void CamPath::SetDuration(double t)
{
	if (m_Map.empty() || (m_Map.size() == 1 && m_Map.begin()->second.HoldDuration == 0))
		return;

	CInterpolationMap<CamPathValue> tempMap;

	CopyMap(tempMap, m_Map);

	bool selectAll = true;
	double first = 0, last = 0;

	for(CInterpolationMap<CamPathValue>::iterator it = m_Map.begin(); it != m_Map.end(); ++it)
	{
		if(it->second.Selected)
		{
			if(selectAll)
			{
				selectAll = false;
				first = it->first;
				last = first + it->second.HoldDuration;
			}
			else
			{
				last = it->first + it->second.HoldDuration;
			}
		}
	}

	double oldDuration = selectAll ? GetDuration() : last -first;

	CInterpolationMap<CamPathValue> result;

	double scale = oldDuration ? t / oldDuration : 0.0;
	bool isFirst = true;
	double firstT = 0;

	for(CInterpolationMap<CamPathValue>::const_iterator it = tempMap.begin(); it != tempMap.end(); ++it)
	{
		double curT = it->first;
		CamPathValue curValue = it->second;

		if (selectAll || curValue.Selected)
		{
			curValue.HoldDuration *= scale;
			if(isFirst)
			{
				result[curT] = curValue;
				firstT = curT;
				isFirst = false;
			}
			else
				result[firstT + scale * (curT - firstT)] = curValue;
		}
		else
			result[curT] = curValue;
	}

	ValidateKeys(result);
	m_Map = std::move(result);
	DoInterpolationMapChangedAll();

	Changed();
}

void CamPath::SetPosition(double x, double y, double z, bool setX, bool setY, bool setZ)
{
	if (m_Map.size() < 1)
		return;

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
			if (setX)
				curValue.X = x + (curValue.X - x0);
			if (setY)
				curValue.Y = y + (curValue.Y - y0);
			if (setZ)
				curValue.Z = z + (curValue.Z - z0);

			it->second = curValue;
		}
	}

	m_XInterp->InterpolationMapChanged();
	m_YInterp->InterpolationMapChanged();
	m_ZInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::SetAngles(double yPitch, double zYaw, double xRoll, bool setY, bool setZ, bool setX)
{
	if (m_Map.size() < 1)
		return;

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
			if (setY && setZ && setX)
			{
				curValue.R =
					Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(yPitch, zYaw, xRoll)));
			}
			else
			{
				QEulerAngles angles = curValue.R.ToQREulerAngles().ToQEulerAngles();
				curValue.R = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(
					(setY ? yPitch : angles.Pitch), (setZ ? zYaw : angles.Yaw), (setX ? xRoll : angles.Roll))));
			}

			it->second = curValue;
		}
	}

	m_RInterp->InterpolationMapChanged();

	Changed();
}

void CamPath::SetFov(double fov)
{
	if (m_Map.size() < 1)
		return;

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

	Changed();
}

void CamPath::Rotate(double yPitch, double zYaw, double xRoll)
{
	if (m_Map.size() < 1)
		return;

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
	Quaternion quatR =
		Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(yPitch, zYaw, xRoll)));

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

			for (Vector3 * handle : {&curValue.InHandle, &curValue.OutHandle})
			{
				Vector3 v = *handle;
				*handle = Vector3(R[0][0] * v.X + R[0][1] * v.Y + R[0][2] * v.Z,
								  R[1][0] * v.X + R[1][1] * v.Y + R[1][2] * v.Z,
								  R[2][0] * v.X + R[2][1] * v.Y + R[2][2] * v.Z);
			}
			// update rotation:
			{
				Quaternion quatQ = curValue.R;

				curValue.R = quatR * quatQ;
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

void CamPath::AnchorTransform(double anchorX, double anchorY, double anchorZ, double anchorYPitch, double anchorZYaw,
							  double anchorXRoll, double destX, double destY, double destZ, double destYPitch,
							  double destZYaw, double destXRoll)
{
	if (m_Map.size() < 1)
		return;

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

	Quaternion quatAnchor = Quaternion::FromQREulerAngles(
		QREulerAngles::FromQEulerAngles(QEulerAngles(anchorYPitch, anchorZYaw, anchorXRoll)));
	Quaternion quatDest =
		Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(destYPitch, destZYaw, destXRoll)));

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

			for (Vector3 * handle : {&curValue.InHandle, &curValue.OutHandle})
			{
				Vector3 v = *handle;
				*handle = Vector3(R[0][0] * v.X + R[0][1] * v.Y + R[0][2] * v.Z,
								  R[1][0] * v.X + R[1][1] * v.Y + R[1][2] * v.Z,
								  R[2][0] * v.X + R[2][1] * v.Y + R[2][2] * v.Z);
			}
			// update rotation:
			{
				Quaternion quatQ = curValue.R;

				curValue.R = quatR * quatQ;
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
	return m_Map.empty() ? 0 : GetUpperBound() - GetLowerBound();
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

void CamPath::OnChangedAdd(CamPathChanged pCamPathChanged, void * pUserData)
{
	m_OnChanged.emplace_back(pCamPathChanged,pUserData);
}

void CamPath::OnChangedRemove(CamPathChanged pCamPathChanged, void * pUserData)
{
	if (m_OnChangedIt != m_OnChanged.end())
	{
		auto it = std::find(m_OnChanged.begin(), m_OnChanged.end(), CamPathChangedData(pCamPathChanged,pUserData));
		if(m_OnChangedIt == it)
			m_OnChangedIt = m_OnChanged.erase(it);
		else
			m_OnChanged.erase(it);
	}
}

std::unique_ptr<CamPath> CamPathCollection::MakePath()
{
	auto path = std::make_unique<CamPath>();
	path->CurveEditing_set(true);
	path->PositionInterpMethod_set(CamPath::DI_SMOOTH);
	path->RotationInterpMethod_set(CamPath::QI_SLINEAR);
	path->FovInterpMethod_set(CamPath::DI_LINEAR);
	path->Enabled_set(true);
	return path;
}
CamPathCollection::CamPathCollection()
{
	New();
}
void CamPathCollection::Changed(void * self)
{
	static_cast<CamPathCollection *>(self)->Notify();
}
void CamPathCollection::Notify()
{
	auto listeners = m_Listeners;
	for (auto & entry : listeners)
		entry.first(entry.second);
}
void CamPathCollection::OnChangedAdd(CamPathChanged callback, void * context)
{
	m_Listeners.emplace_back(callback, context);
}
void CamPathCollection::OnChangedRemove(CamPathChanged callback, void * context)
{
	m_Listeners.erase(std::remove(m_Listeners.begin(), m_Listeners.end(), std::make_pair(callback, context)),
					  m_Listeners.end());
}
void CamPathCollection::Use(std::size_t index)
{
	if (index >= Size())
		throw std::invalid_argument("Invalid path index");
	m_Active = index;
	Notify();
}
void CamPathCollection::New(std::string name)
{
	if (Size() >= 128)
		throw std::invalid_argument("Maximum 128 paths");
	auto exists = [&](const std::string & n) {
		for (auto & path : m_Paths)
			if (path->GetName() == n)
				return true;
		return false;
	};
	if (name.empty())
	{
		int n = 1;
		do
		{
			name = "Path " + std::to_string(n++);
		} while (exists(name));
	}
	if (exists(name) || name.size() > 256)
		throw std::invalid_argument("Path names must be unique and at most 256 bytes");
	auto path = MakePath();
	path->SetName(name);
	path->OnChangedAdd(Changed, this);
	m_Paths.push_back(std::move(path));
	m_Active = Size() - 1;
	Notify();
}
void CamPathCollection::Rename(const std::string & name)
{
	if (name.empty() || name.size() > 256)
		throw std::invalid_argument("A path name must contain 1..256 bytes");
	for (std::size_t i = 0; i < Size(); ++i)
		if (i != m_Active && At(i).GetName() == name)
			throw std::invalid_argument("Path names must be unique");
	Active().SetName(name);
}
void CamPathCollection::Remove(std::size_t index)
{
	if (index >= Size() || Size() == 1)
		throw std::invalid_argument("Keep at least one path; use clear to empty it");
	m_Paths.erase(m_Paths.begin() + index);
	if (m_Active > index)
		--m_Active;
	else if (m_Active >= Size())
		m_Active = Size() - 1;
	Notify();
}
CamPathCollection::Snapshot CamPathCollection::Capture() const
{
	Snapshot result;
	result.Active = m_Active;
	for (auto & path : m_Paths)
		result.Paths.push_back(path->CaptureState());
	return result;
}
void CamPathCollection::Restore(const Snapshot & snapshot)
{
	if (snapshot.Paths.empty() || snapshot.Paths.size() > 128 || snapshot.Active >= snapshot.Paths.size())
		throw std::invalid_argument("Invalid path collection");
	std::vector<std::unique_ptr<CamPath>> paths;
	std::set<std::string> names;
	for (const auto & state : snapshot.Paths)
	{
		if (state.Name.empty() || !names.insert(state.Name).second)
			throw std::invalid_argument("Path names must be nonempty and unique");
		auto path = MakePath();
		path->RestoreState(state);
		path->OnChangedAdd(Changed, this);
		paths.push_back(std::move(path));
	}
	m_Paths = std::move(paths);
	m_Active = snapshot.Active;
	Notify();
}
void CamPathCollection::Record(const Snapshot & before)
{
	if (before == Capture())
		return;
	m_Undo.push_back(before);
	if (m_Undo.size() > 64)
		m_Undo.erase(m_Undo.begin());
	m_Redo.clear();
}
bool CamPathCollection::Undo()
{
	if (m_Undo.empty())
		return false;
	auto state = m_Undo.back();
	auto current = Capture();
	Restore(state);
	m_Undo.pop_back();
	m_Redo.push_back(std::move(current));
	return true;
}
bool CamPathCollection::Redo()
{
	if (m_Redo.empty())
		return false;
	auto state = m_Redo.back();
	auto current = Capture();
	Restore(state);
	m_Redo.pop_back();
	m_Undo.push_back(std::move(current));
	return true;
}
bool CamPathCollection::Eval(double time, CamPathValue & value)
{
	if (!m_Enabled || !std::isfinite(time))
		return false;
	CamPath * chosen = nullptr;
	double start = 0;
	for (auto & entry : m_Paths)
	{
		auto & path = *entry;
		if (!path.Enabled_get() || !path.CanEval())
			continue;
		double at = path.GetLowerBound() + path.GetOffset();
		if (at <= time && (time <= path.GetUpperBound() + path.GetOffset() || path.GetHold()) &&
			(!chosen || at >= start))
		{
			chosen = &path;
			start = at;
		}
	}
	// Preserve classic global hold before the first arrival as well.
	if (!chosen)
		for (auto & entry : m_Paths)
		{
			auto & path = *entry;
			if (!path.Enabled_get() || !path.CanEval() || !path.GetHold())
				continue;
			double at = path.GetLowerBound() + path.GetOffset();
			if (!chosen || at < start)
			{
				chosen = &path;
				start = at;
			}
		}
	if (!chosen)
		return false;
	double t = time - chosen->GetOffset();
	if (chosen->GetHold())
		t = std::clamp(t, chosen->GetLowerBound(), chosen->GetUpperBound());
	if (t < chosen->GetLowerBound() || t > chosen->GetUpperBound())
		return false;
	value = chosen->Eval(t);
	return true;
}
bool CamPathCollection::SaveAll(const wchar_t * file)
{
	rapidxml::xml_document<> doc;
	auto root = doc.allocate_node(rapidxml::node_element, "campaths");
	doc.append_node(root);
	root->append_attribute(doc.allocate_attribute("version", "1"));
	std::string selected = std::to_string(m_Active);
	root->append_attribute(doc.allocate_attribute("active", doc.allocate_string(selected.c_str())));
	// clone_node shares string storage; keep all native XML buffers alive until
	// the outer document is printed. Serialization stays in CamPath::ToXml.
	std::vector<std::string> buffers;
	buffers.reserve(Size());
	for (auto & path : m_Paths)
	{
		buffers.push_back(path->ToXml());
		rapidxml::xml_document<> child;
		child.parse<0>(buffers.back().data());
		auto node = doc.clone_node(child.first_node("campath"));
		node->append_attribute(doc.allocate_attribute("enabled", path->Enabled_get() ? "1" : "0"));
		root->append_node(node);
	}
	std::ofstream out(std::filesystem::path(file), std::ios::binary);
	if (!out)
		return false;
	out << doc;
	return bool(out);
}
bool CamPathCollection::Load(const wchar_t * file)
{
	std::string xml;
	if (!ReadCampathFile(file, xml))
		return false;
	try
	{
		std::vector<char> buffer(xml.begin(), xml.end());
		buffer.push_back(0);
		rapidxml::xml_document<> doc;
		doc.parse<0>(buffer.data());
		Snapshot result;
		if (auto root = doc.first_node("campaths"))
		{
			auto version = root->first_attribute("version");
			if (!version || std::string(version->value()) != "1")
				return false;
			auto active = root->first_attribute("active");
			if (active)
			{
				char * end = nullptr;
				unsigned long number = strtoul(active->value(), &end, 10);
				if (end == active->value() || *end)
					return false;
				result.Active = std::size_t(number);
			}
			for (auto node = root->first_node("campath"); node; node = node->next_sibling("campath"))
			{
				if (result.Paths.size() >= 128)
					return false;
				std::string child;
				rapidxml::print(std::back_inserter(child), *node);
				auto path = MakePath();
				path->SetName("Path " + std::to_string(result.Paths.size() + 1));
				if (!path->LoadXml(child))
					return false;
				auto enabled = node->first_attribute("enabled");
				if (enabled)
				{
					if (std::string(enabled->value()) != "0" && std::string(enabled->value()) != "1")
						return false;
					path->Enabled_set(std::string(enabled->value()) == "1");
				}
				result.Paths.push_back(path->CaptureState());
			}
		}
		else if (doc.first_node("campath"))
		{
			result = Capture();
			auto path = MakePath();
			path->RestoreState(result.Paths[result.Active]);
			if (!path->LoadXml(xml))
				return false;
			result.Paths[result.Active] = path->CaptureState();
		}
		else
			return false;
		Restore(result);
		return true;
	}
	catch (const std::exception &)
	{
		return false;
	}
}
