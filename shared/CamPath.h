#pragma once

#include "AfxMath.h"

#include <array>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace Afx;
using namespace Afx::Math;

// Timing along an existing camera segment; this does not move its endpoints.
struct CamPathEase
{
	enum Function
	{
		Inherit,
		Linear,
		Sine,
		Quad,
		Cubic,
		Quart,
		Quint,
		Expo,
		Circ,
		Back,
		Bounce,
		Elastic,
		Bezier,
		Count
	};
	enum Channel
	{
		All,
		Position,
		Angles,
		Fov,
		ChannelCount
	};
	static const char * ChannelToString(Channel value);
	static bool ChannelFromString(const char * text, Channel & value);
	enum Direction
	{
		In,
		Out,
		InOut
	};
	Function Type = Inherit;
	Direction Mode = InOut;
	double X1 = 1.0 / 3, Y1 = 1.0 / 3, X2 = 2.0 / 3, Y2 = 2.0 / 3;
	double Overshoot = 1.70158, Amplitude = 1, Period = 0.3;
	bool Valid() const;
	bool operator==(const CamPathEase & other) const;
	double Eval(double progress) const;
	static const char * ToString(Function value);
	static bool FromString(const char * text, Function & value);
	static const char * ToString(Direction value);
	static bool FromString(const char * text, Direction & value);
};

struct CamPathValue
{
	double X;
	double Y;
	double Z;

	Quaternion R;

	double Fov;

	bool Selected;

	// World-space offsets from this keyframe. Unset handles follow the native
	// position interpolator until explicitly edited.
	Vector3 InHandle{0, 0, 0}, OutHandle{0, 0, 0};
	bool HasInHandle = false, HasOutHandle = false;
	bool HandlesAligned = true; // Opposite directions, independent lengths.

	double HoldDuration = 0; // arrival + duration is the release time in path coordinates
	std::array<std::optional<CamPathEase>, 3> ChannelEase;
	CamPathEase Ease; // outgoing segment; Inherit uses the path setting
	bool operator==(const CamPathValue & other) const;

	CamPathValue();

	CamPathValue(double x, double y, double z, double pitch, double yaw, double roll, double fov);

	CamPathValue(double x, double y, double z, double q_w, double q_x, double q_y, double q_z, double fov, bool selected);

};

struct CamPathIterator
{
public:
	CInterpolationMap<CamPathValue>::const_iterator wrapped;

	CamPathIterator(CInterpolationMap<CamPathValue>::const_iterator const & it);

	double GetTime() const;

	CamPathValue GetValue() const;

	CamPathIterator& operator ++ ();

	bool operator == (CamPathIterator const &it) const;

	bool operator != (CamPathIterator const &it) const;

};

typedef void (*CamPathChanged)(void * pUserData);

class CamPath
{
public:
	enum DoubleInterp {
		DI_DEFAULT = 0,
		DI_LINEAR = 1,
		DI_CUBIC = 2,
		DI_SMOOTH = 3,
		DI_MONOTONE = 4,
		DI_STEP = 5,
		_DI_COUNT = 6
	};

	enum QuaternionInterp {
		QI_DEFAULT = 0,
		QI_SLINEAR = 1,
		QI_SCUBIC = 2,
		QI_SMOOTH = 3,
		QI_STEP = 4,
		_QI_COUNT = 5,
	};

	static bool DoubleInterp_FromString(char const * value, DoubleInterp & outValue);
	static char const * DoubleInterp_ToString(DoubleInterp value);

	static bool QuaternionInterp_FromString(char const * value, QuaternionInterp & outValue);
	static char const * QuaternionInterp_ToString(QuaternionInterp value);

	struct State
	{
		CInterpolationMap<CamPathValue> Keys;
		DoubleInterp Position = DI_DEFAULT, Fov = DI_DEFAULT;
		QuaternionInterp Rotation = QI_DEFAULT;
		double Offset = 0;
		bool Enabled = false, Hold = false, Bezier = false, CurveEditing = false, ContinuousSpeed = false;
		std::string Name;
		CamPathEase Ease;
		std::array<std::optional<CamPathEase>, 3> ChannelEase;
		double BlendStrength = 1;
		bool operator==(const State & other) const;
	};
	State CaptureState() const;
	void RestoreState(const State & state);
	const std::string & GetName() const
	{
		return m_Name;
	}
	void SetName(const std::string & name);
	bool SetKeyHold(double time, double releaseTime);
	const CamPathEase & GetEase(CamPathEase::Channel channel = CamPathEase::All) const;
	void SetEase(const CamPathEase & value, bool clearOverrides = true,
				 CamPathEase::Channel channel = CamPathEase::All);
	bool SetKeyEase(double time, const CamPathEase & value, CamPathEase::Channel channel = CamPathEase::All);
	CamPathEase GetKeyEase(double time, bool forPlayback = false,
						   CamPathEase::Channel channel = CamPathEase::All) const;
	bool SmoothHolds() const;
	double GetSegmentProgress(double keyTime, double time, bool applySpeed = true,
							  CamPathEase::Channel channel = CamPathEase::All) const;
	void BlendStrength_set(double value);
	double BlendStrength_get() const
	{
		return m_BlendStrength;
	}
	bool GetSegmentMidpoint(double time, Vector3 & position) const;
	void SpeedContinuous_set(bool enabled);
	bool SpeedContinuous_get() const
	{
		return m_ContinuousSpeed;
	}
	void Validate() const;
	std::string ToXml();
	bool LoadXml(const std::string & xml);

	CamPath();
	
	~CamPath();

	void Enabled_set(bool enable);
	bool Enabled_get(void) const;

	bool GetHold(void) const;
	void SetHold(bool value);

	void PositionInterpMethod_set(DoubleInterp value);
	DoubleInterp PositionInterpMethod_get(void) const;

	void RotationInterpMethod_set(QuaternionInterp value);
	QuaternionInterp RotationInterpMethod_get(void) const;

	void FovInterpMethod_set(DoubleInterp value);
	DoubleInterp FovInterpMethod_get(void) const;

	void Add(double time, const CamPathValue & value);

	void CurveEditing_set(bool enabled);
	bool CurveEditing_get() const
	{
		return m_CurveEditing;
	}
	void Bezier_set(bool enabled);
	bool Bezier_get() const
	{
		return m_Bezier;
	}
	bool GetHandle(double time, bool outgoing, Vector3 & offset) const;
	bool SetHandle(double time, bool outgoing, const Vector3 & offset);
	bool SetHandleMode(double time, bool aligned);
	bool ResetHandles(double time);

	void Remove(double time);
	void Clear();

	size_t GetSize() const;
	CamPathIterator GetBegin();
	CamPathIterator GetEnd();
	double GetDuration() const;

	/// <remarks>Must not be called if GetSize is less than 1!</remarks>
	double GetLowerBound() const;

	/// <remarks>Must not be called if GetSize is less than 1!</remarks>
	double GetUpperBound() const;

	bool CanEval(void) const;

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	CamPathValue Eval(double t);

	bool Save(wchar_t const * fileName);
	bool Load(wchar_t const * fileName);
	
	/// <remarks>In the current implementation if points happen to fall on the same time value, then the last point's value will be used (no interpolation).</remarks>
	/// <param name="relative">If t is an relative offset (true), or absolute value (false).</param>
	void SetStart(double t, bool relative = false);
	
	/// <remarks>In the current implementation if points happen to fall on the same time value, then the last point's value will be used (no interpolation).
	/// Setting duration for a path with less than 2 points will do nothing.</remarks>
	void SetDuration(double t);

	void SetPosition(double x, double y, double z, bool setX = true, bool setY = true, bool setZ = true);

	void SetAngles(double yPitch, double zYaw, double xRoll, bool setY = true, bool setZ = true, bool setX = true);

	void SetFov(double fov);

	void Rotate(double yPitch, double zYaw, double xRoll);

	void AnchorTransform(double anchorX, double anchorY, double anchorZ, double anchorYPitch, double anchorZYaw, double anchorXRoll, double destX, double destY, double destZ, double destYPitch, double destZYaw, double destXRoll);

	size_t SelectAll();

	void SelectNone();

	size_t SelectInvert();

	/// <summary>Adds a range of key frames to the selection.</summary>
	/// <param name="min">Index of first keyframe to add to selection.</param>
	/// <param name="max">Index of last keyframe to add to selection.</param>
	/// <returns>Number of selected keyframes.</returns>
	size_t SelectAdd(size_t min, size_t max);

	/// <summary>Adds a range of key frames to the selection.</summary>
	/// <param name="min">Lower bound to start adding selection at.</param>
	/// <param name="count">Number of keyframes to select.</param>
	/// <returns>Number of selected keyframes.</returns>
	size_t SelectAdd(double min, size_t count);

	/// <summary>Adds a range of key frames to the selection.</summary>
	/// <param name="min">Lower bound to start adding selection at.</param>
	/// <param name="max">Upper bound to end adding selection at.</param>
	/// <returns>Number of selected keyframes.</returns>
	size_t SelectAdd(double min, double max);
	
	void OnChangedAdd(CamPathChanged pCamPathChanged, void * pUserData);

	void OnChangedRemove(CamPathChanged pCamPathChanged, void * pUserData);

	void SetOffset(double value);

	double GetOffset() const;

private:
	CamPathEase m_Ease;
	CamPathValue EvalBase(double time);
	struct CamPathChangedData {
		CamPathChanged pFn;
		void * pUserData;

		CamPathChangedData(CamPathChanged pFn, void * pUserData)
		: pFn(pFn), pUserData(pUserData) {

		}

		void Notify() {
			pFn(pUserData);
		}

		bool operator==(const CamPathChangedData& other) const {
			return this->pFn == other.pFn && this->pUserData == other.pUserData;
		}
	};

	std::list<struct CamPathChangedData> m_OnChanged;
	std::list<struct CamPathChangedData>::iterator m_OnChangedIt;

	static double XSelector(CamPathValue const & value)
	{
		return value.X;
	}

	static double YSelector(CamPathValue const & value)
	{
		return value.Y;
	}

	static double ZSelector(CamPathValue const & value)
	{
		return value.Z;
	}

	static Quaternion RSelector(CamPathValue const & value)
	{
		return value.R;
	}

	static double FovSelector(CamPathValue const & value)
	{
		return value.Fov;
	}

	static bool SelectedSelector(CamPathValue const & value)
	{
		return value.Selected;
	}

	struct MotionSegment
	{
		double Time, End, Length;
		std::array<Vector3, 4> Points;
		std::array<double, 65> Distances;
		std::vector<double> ProfileDistance, ProfileSpeed;
		Vector3 Point(double u) const;
		double DistanceProgress(double parameter) const;
		double ProfileProgress(double u) const;
		Vector3 Derivative(double u) const;
		double ArcLength(double from, double to) const;
		double Parameter(double distance) const;
	};
	void BuildMotionCache() const;
	Vector3 EvalPosition(double time) const;
	bool IsStepSegment(double time) const;
	std::pair<double, double> TravelSlopes(std::size_t index) const;
	void BuildBlendProfile(std::size_t index) const;
	double BlendedTravelProgress(double time, double u) const;
	Vector3 EvalTravelPosition(double time, double progress) const;
	double TravelDistanceProgress(double time, double progress) const;
	double TravelParameter(double time, double progress) const;
	bool UseContinuousSpeed(double time) const;
	CamPathValue EvalTimed(double time, CamPathEase::Channel channel);
	CamPathValue EvalChannelBase(double time, CamPathEase::Channel channel);
	std::array<std::optional<CamPathEase>, 3> m_ChannelEase;
	double m_BlendStrength = 1;
	bool m_ContinuousSpeed = false;
	mutable bool m_MotionCacheValid = false;
	mutable std::vector<MotionSegment> m_MotionSegments;
	std::string m_Name;
	bool m_CurveEditing = false, m_Bezier = false;
	bool m_Enabled;
	bool m_Hold = false;
	DoubleInterp m_PositionInterpMethod;
	QuaternionInterp m_RotationInterpMethod;
	DoubleInterp m_FovInterpMethod;
	double m_Offset;
	
	CInterpolationMap<CamPathValue> m_Map;

	CInterpolationMapView<CamPathValue, double> m_XView;
	CInterpolationMapView<CamPathValue, double> m_YView;
	CInterpolationMapView<CamPathValue, double> m_ZView;
	CInterpolationMapView<CamPathValue, Quaternion> m_RView;
	CInterpolationMapView<CamPathValue, double> m_FovView;
	CInterpolationMapView<CamPathValue, bool> m_SelectedView;

	CInterpolation<double> * m_XInterp;
	CInterpolation<double> * m_YInterp;
	CInterpolation<double> * m_ZInterp;
	CInterpolation<Quaternion> * m_RInterp;
	CInterpolation<double> * m_FovInterp;
	CInterpolation<bool> * m_SelectedInterp;

	void Changed();
	void CopyMap(CInterpolationMap<CamPathValue> & dst, CInterpolationMap<CamPathValue> & src);

	void DoInterpolationMapChangedAll(void);
};

// A small owner/history for ordinary CamPath instances. No second evaluator or
// timing model is introduced; each entry keeps native timestamped keyframes.
class CamPathCollection
{
public:
	struct Snapshot
	{
		std::vector<CamPath::State> Paths;
		std::size_t Active = 0;
		bool operator==(const Snapshot & other) const
		{
			return Active == other.Active && Paths == other.Paths;
		}
	};
	CamPathCollection();
	std::size_t Size() const
	{
		return m_Paths.size();
	}
	std::size_t ActiveIndex() const
	{
		return m_Active;
	}
	CamPath & Active()
	{
		return *m_Paths.at(m_Active);
	}
	CamPath & At(std::size_t index)
	{
		return *m_Paths.at(index);
	}
	void Use(std::size_t index);
	void New(std::string name = "");
	void Rename(const std::string & name);
	void Remove(std::size_t index);
	Snapshot Capture() const;
	void Restore(const Snapshot & state);
	void Record(const Snapshot & before);
	bool Undo();
	bool Redo();
	bool Enabled_get() const
	{
		return m_Enabled;
	}
	void Enabled_set(bool value)
	{
		m_Enabled = value;
		Notify();
	}
	bool Eval(double time, CamPathValue & value);
	bool SaveAll(const wchar_t * file);
	bool Load(const wchar_t * file);
	void OnChangedAdd(CamPathChanged callback, void * context);
	void OnChangedRemove(CamPathChanged callback, void * context);

private:
	std::vector<std::unique_ptr<CamPath>> m_Paths;
	std::size_t m_Active = 0;
	bool m_Enabled = false;
	std::vector<Snapshot> m_Undo, m_Redo;
	std::vector<std::pair<CamPathChanged, void *>> m_Listeners;
	std::unique_ptr<CamPath> MakePath();
	void Notify();
	static void Changed(void * self);
};
