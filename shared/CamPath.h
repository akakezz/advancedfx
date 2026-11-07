#pragma once

#include "AfxMath.h"

#include <list>
#include <string>
#include <limits>
#include <memory>

using namespace Afx;
using namespace Afx::Math;

struct CamPathValue
{
	double X;
	double Y;
	double Z;

	Quaternion R;

	double Fov;

	bool Selected;
	// Existing keys retain their original quaternion representation. Authored Euler
	// values preserve winding when subdividing or editing Euler curves.
	bool HasEuler = false;
	double EulerPitch = 0, EulerYaw = 0, EulerRoll = 0;
	double HoldDuration = 0;
	unsigned char AccelerationMask = 0;
	double AccelerationIn[7] = {}, AccelerationOut[7] = {};
	QEulerAngles GetEulerAngles() const;
	void SetEulerAngles(const QEulerAngles &angles);
	void SetOrientation(const Quaternion &orientation);

	// Per-key tangents and modes (for Hermite interpolation)
	double TxIn, TxOut;
	unsigned char TxModeIn, TxModeOut;
	double TyIn, TyOut;
	unsigned char TyModeIn, TyModeOut;
	double TzIn, TzOut;
	unsigned char TzModeIn, TzModeOut;
	double TfovIn, TfovOut;
	unsigned char TfovModeIn, TfovModeOut;
	// weights
	double TxWIn, TxWOut;
	double TyWIn, TyWOut;
	double TzWIn, TzWOut;
	double TfovWIn, TfovWOut;

	// Rotation (Euler) per-key tangents, modes and weights for custom rotation backend
	// Order/semantics follow Quake convention: Pitch=Y axis, Yaw=Z axis, Roll=X axis
	double TRxIn, TRxOut;
	unsigned char TRxModeIn, TRxModeOut; // Roll (X)
	double TRyIn, TRyOut;
	unsigned char TRyModeIn, TRyModeOut; // Pitch (Y)
	double TRzIn, TRzOut;
	unsigned char TRzModeIn, TRzModeOut; // Yaw (Z)
	double TRxWIn, TRxWOut;				 // weights for Free mode handles
	double TRyWIn, TRyWOut;
	double TRzWIn, TRzWOut;

	bool operator==(const CamPathValue &other) const;
	CamPathValue();

	CamPathValue(double x, double y, double z, double pitch, double yaw, double roll, double fov);

	CamPathValue(double x, double y, double z, double q_w, double q_x, double q_y, double q_z, double fov, bool selected);

};

struct CamPathIterator
{
public:
	CInterpolationMap<CamPathValue>::const_iterator wrapped;

	CamPathIterator(CInterpolationMap<CamPathValue>::const_iterator it);

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
  enum DoubleInterp
  {
		DI_DEFAULT = 0,
		DI_LINEAR = 1,
		DI_CUBIC = 2,
	  DI_CUSTOM = 3,
	  _DI_COUNT = 4
	};

	enum QuaternionInterp {
		QI_DEFAULT = 0,
		QI_SLINEAR = 1,
		QI_SCUBIC = 2,
		QI_CUSTOM = 3,
		_QI_COUNT = 4,
	};

	static bool DoubleInterp_FromString(char const * value, DoubleInterp & outValue);
	static char const * DoubleInterp_ToString(DoubleInterp value);

	static bool QuaternionInterp_FromString(char const *value, QuaternionInterp &outValue);
	static char const *QuaternionInterp_ToString(QuaternionInterp value);

	// Tangent editing support (for custom Hermite interpolation)
	enum TangentMode : unsigned char
	{
		TM_AUTO = 0,
		TM_FLAT = 1,
		TM_LINEAR = 2,
		TM_FREE = 3
	};

	static bool TangentMode_FromString(char const *value, unsigned char &outValue);
	static char const *TangentMode_ToString(unsigned char value);

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

	const AutoTangentSettings &GetAutoTangentSettings() const { return m_AutoTangentSettings; }
	bool SetAutoTangentSettings(const AutoTangentSettings &settings);

	struct SpeedKey
	{
		double Time = 0, Speed = 1;
		bool operator==(const SpeedKey &b) const
		{
			return Time == b.Time && Speed == b.Speed;
		}
	};
	enum class ModifierType
	{
		Noise,
		Oscillation,
		Smooth,
		Spring
	};
	struct ModifierKey
	{
		double Time = 0, Value = 1;
		bool operator==(const ModifierKey &b) const
		{
			return Time == b.Time && Value == b.Value;
		}
	};
	struct Modifier
	{
		ModifierType Type = ModifierType::Noise;
		double Strength = 1, Phase = 0, Roughness = .5, Window = .25, Damping = .7;
		unsigned int Octaves = 1;
		std::vector<ModifierKey> Envelope;
		double Begin = 0, End = 1, FadeIn = 0.2, FadeOut = 0.2, Frequency = 1;
		double Amplitude[7] = {};
		unsigned int Seed = 1;
		bool Enabled = true, CameraSpace = false;
		bool operator==(const Modifier &b) const;
	};
	using NoiseModifier = Modifier; // Preserve the existing branch API for noise callers.
	struct State
	{
		CInterpolationMap<CamPathValue> Keys;
		DoubleInterp Position, Fov;
		QuaternionInterp Rotation;
		bool Enabled, Hold;
		double Offset;
		AutoTangentSettings AutoTangents;
		bool DistanceTiming = false;
		bool DistanceTimingPose = false;
		std::vector<SpeedKey> SpeedProfile;
		std::vector<NoiseModifier> Modifiers;
		bool operator==(const State &other) const;
	};
	State CaptureState() const;
	unsigned long long GetRevision() const
	{
		return m_Revision;
	}
	void RestoreState(const State &state);
	void Add(double time, const CamPathValue & value);

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
	CamPathValue EvalUnmodified(double time);

	bool Save(wchar_t const * fileName);
	bool Load(wchar_t const *fileName);
	bool SaveToString(std::string &xml);
	bool LoadFromString(const std::string &xml);

	/// <remarks>In the current implementation if points happen to fall on the same time value, then the last point's value will be used (no interpolation).</remarks>
	/// <param name="relative">If t is an relative offset (true), or absolute value (false).</param>
	void SetStart(double t, bool relative = false);
	
	/// <remarks>In the current implementation if points happen to fall on the same time value, then the last point's value will be used (no interpolation).
	/// Setting duration for a path with less than 2 points will do nothing.</remarks>
	void SetDuration(double t);

	void SetPosition(double x, double y, double z, bool setX = true, bool setY = true, bool setZ = true);

	void SetAngles(double yPitch, double zYaw, double xRoll, bool setY = true, bool setZ = true, bool setX = true);

	void SetFov(double fov);

	// Custom tangent editing APIs (apply to selected keys if any, otherwise all)
	enum Channel
	{
		CH_X = 0,
		CH_Y = 1,
		CH_Z = 2,
		CH_FOV = 3,
		CH_RPITCH = 4, // Euler pitch (Y)
		CH_RYAW = 5,   // Euler yaw (Z)
		CH_RROLL = 6   // Euler roll (X)
	};
	bool ReplaceKeys(const CInterpolationMap<CamPathValue> &keys);
	bool UpdateKey(double oldTime, double newTime, const CamPathValue &value);
	bool ReplacePose(double time, const CamPathValue &pose, unsigned int channels = 127);
	bool DeleteSelected();
	bool SetKeyHold(size_t index, double releaseTime);
	bool HasKeyHolds() const;
	bool Insert(double time);
	bool BakeTangents();
	bool MakeScalarCurvesEditable();
	double GetSpeed(double time);
	std::vector<std::pair<double, CamPathValue>> SampleInterval(double begin, double end, double tolerance,
																size_t maxPoints);
	bool RetimeByDistance(double duration);
	const std::vector<SpeedKey> &GetSpeedProfile() const
	{
		return m_SpeedProfile;
	}
	bool ApplySpeedProfile(const std::vector<SpeedKey> &keys, double duration);
	double GetProfileSpeed(double time);
	const std::vector<NoiseModifier> &GetModifiers() const
	{
		return m_Modifiers;
	}
	bool SetModifiers(const std::vector<NoiseModifier> &modifiers);
	static double ModifierInfluence(const NoiseModifier &modifier, double time);
	static const char *ModifierTypeName(ModifierType type);
	static bool ModifierTypeFromString(const char *name, ModifierType &type);
	static double ModifierSampleRate(const NoiseModifier &modifier);

	bool GetDistanceTiming() const
	{
		return m_DistanceTiming;
	}
	void SetDistanceTiming(bool enabled);
	bool GetDistanceTimingPose() const
	{
		return m_DistanceTimingPose;
	}
	void SetDistanceTimingPose(bool enabled);
	bool ExportClassic(const wchar_t *fileName, double sampleInterval);
	unsigned long long GetPositionRevision() const
	{
		return m_PositionRevision;
	}
	unsigned long long GetGeometryRevision() const
	{
		return m_GeometryRevision;
	}
	static double GetChannelValue(const CamPathValue &value, Channel channel);
	static void SetChannelValue(CamPathValue &value, Channel channel, double component);
	bool GetTangent(Channel ch, size_t index, bool outgoing, double &slope, double &weight);
	bool GetAcceleration(Channel ch, size_t index, bool outgoing, double &acceleration);
	struct TangentEdit
	{
		size_t Index;
		Channel Component;
		bool Outgoing;
		double Slope, Weight;
	};
	bool EditTangents(const std::vector<TangentEdit> &edits, bool linked, bool editWeight,
					  bool selectEdited = false);
	bool EditTangent(size_t index, Channel channel, bool outgoing, double slope, double weight, bool linked,
					 bool editWeight);
	void SetTangent(Channel ch, bool setIn, bool setOut, double slopeIn, double slopeOut);
	void SetTangentMode(Channel ch, bool setIn, bool setOut, unsigned char mode);
	void SetTangentWeight(Channel ch, bool setIn, bool setOut, double wIn, double wOut);

	bool Rotate(double yPitch, double zYaw, double xRoll);

	bool AnchorTransform(double anchorX, double anchorY, double anchorZ, double anchorYPitch,
						 double anchorZYaw, double anchorXRoll, double destX, double destY, double destZ,
						 double destYPitch, double destZYaw, double destXRoll);

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

	bool SetOffset(double value);

	double GetOffset() const;

private:
  bool Transform(const double matrix[3][3], const Quaternion &orientation, const double source[3],
				 const double destination[3]);
  friend class CamPathCollection;
  bool LoadInternal(std::string xml);
  struct CamPathChangedData
  {
		CamPathChanged pFn;
	  void *pUserData;

	  CamPathChangedData(CamPathChanged pFn, void *pUserData) : pFn(pFn), pUserData(pUserData)
	  {
		}

	  void Notify()
	  {
			pFn(pUserData);
		}

	  bool operator==(const CamPathChangedData &other) const
	  {
			return this->pFn == other.pFn && this->pUserData == other.pUserData;
		}
	};

	std::list<struct CamPathChangedData> m_OnChanged;
	std::list<struct CamPathChangedData>::iterator m_OnChangedIt;

	static double AccelerationSelector(const CamPathValue &v, int ch, bool outgoing)
	{
		return v.AccelerationMask & (1u << ch) ? (outgoing ? v.AccelerationOut[ch] : v.AccelerationIn[ch])
											   : (std::numeric_limits<double>::quiet_NaN)();
	}
	static double XAcceleration(const CamPathValue &v, bool outgoing)
	{
		return AccelerationSelector(v, 0, outgoing);
	}
	static double YAcceleration(const CamPathValue &v, bool outgoing)
	{
		return AccelerationSelector(v, 1, outgoing);
	}
	static double ZAcceleration(const CamPathValue &v, bool outgoing)
	{
		return AccelerationSelector(v, 2, outgoing);
	}
	static double FovAcceleration(const CamPathValue &v, bool outgoing)
	{
		return AccelerationSelector(v, 3, outgoing);
	}
	static double EulerAcceleration(const CamPathValue &v, int ch, bool outgoing)
	{
		return AccelerationSelector(v, ch + 4, outgoing);
	}
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

	static bool SelectedSelector(CamPathValue const &value)
	{
		return value.Selected;
	}

	// Tangent selectors for Hermite interpolation (double channels)
	static double XTanInSelector(CamPathValue const &v)
	{
		return v.TxIn;
	}
	static double XTanOutSelector(CamPathValue const &v)
	{
		return v.TxOut;
	}
	static unsigned char XTanModeInSelector(CamPathValue const &v)
	{
		return v.TxModeIn;
	}
	static unsigned char XTanModeOutSelector(CamPathValue const &v)
	{
		return v.TxModeOut;
	}

	static double YTanInSelector(CamPathValue const &v)
	{
		return v.TyIn;
	}
	static double YTanOutSelector(CamPathValue const &v)
	{
		return v.TyOut;
	}
	static unsigned char YTanModeInSelector(CamPathValue const &v)
	{
		return v.TyModeIn;
	}
	static unsigned char YTanModeOutSelector(CamPathValue const &v)
	{
		return v.TyModeOut;
	}

	static double ZTanInSelector(CamPathValue const &v)
	{
		return v.TzIn;
	}
	static double ZTanOutSelector(CamPathValue const &v)
	{
		return v.TzOut;
	}
	static unsigned char ZTanModeInSelector(CamPathValue const &v)
	{
		return v.TzModeIn;
	}
	static unsigned char ZTanModeOutSelector(CamPathValue const &v)
	{
		return v.TzModeOut;
	}

	static double FovTanInSelector(CamPathValue const &v)
	{
		return v.TfovIn;
	}
	static double FovTanOutSelector(CamPathValue const &v)
	{
		return v.TfovOut;
	}
	static unsigned char FovTanModeInSelector(CamPathValue const &v)
	{
		return v.TfovModeIn;
	}
	static unsigned char FovTanModeOutSelector(CamPathValue const &v)
	{
		return v.TfovModeOut;
	}

	// Weight selectors (used by interpolation / UI)
	static double XTanWInSelector(CamPathValue const &v)
	{
		return v.TxWIn;
	}
	static double XTanWOutSelector(CamPathValue const &v)
	{
		return v.TxWOut;
	}
	static double YTanWInSelector(CamPathValue const &v)
	{
		return v.TyWIn;
	}
	static double YTanWOutSelector(CamPathValue const &v)
	{
		return v.TyWOut;
	}
	static double ZTanWInSelector(CamPathValue const &v)
	{
		return v.TzWIn;
	}
	static double ZTanWOutSelector(CamPathValue const &v)
	{
		return v.TzWOut;
	}
	static double FovTanWInSelector(CamPathValue const &v)
	{
		return v.TfovWIn;
	}
	static double FovTanWOutSelector(CamPathValue const &v)
	{
		return v.TfovWOut;
	}

	// Rotation (Euler) tangent and mode selectors for custom rotation interpolation
	static double RTanIn_Roll_Selector(CamPathValue const &v)
	{
		return v.TRxIn;
	}
	static double RTanOut_Roll_Selector(CamPathValue const &v)
	{
		return v.TRxOut;
	}
	static unsigned char RTanModeIn_Roll_Selector(CamPathValue const &v)
	{
		return v.TRxModeIn;
	}
	static unsigned char RTanModeOut_Roll_Selector(CamPathValue const &v)
	{
		return v.TRxModeOut;
	}
	static double RTanWIn_Roll_Selector(CamPathValue const &v)
	{
		return v.TRxWIn;
	}
	static double RTanWOut_Roll_Selector(CamPathValue const &v)
	{
		return v.TRxWOut;
	}

	static double RTanIn_Pitch_Selector(CamPathValue const &v)
	{
		return v.TRyIn;
	}
	static double RTanOut_Pitch_Selector(CamPathValue const &v)
	{
		return v.TRyOut;
	}
	static unsigned char RTanModeIn_Pitch_Selector(CamPathValue const &v)
	{
		return v.TRyModeIn;
	}
	static unsigned char RTanModeOut_Pitch_Selector(CamPathValue const &v)
	{
		return v.TRyModeOut;
	}
	static double RTanWIn_Pitch_Selector(CamPathValue const &v)
	{
		return v.TRyWIn;
	}
	static double RTanWOut_Pitch_Selector(CamPathValue const &v)
	{
		return v.TRyWOut;
	}

	static double RTanIn_Yaw_Selector(CamPathValue const &v)
	{
		return v.TRzIn;
	}
	static double RTanOut_Yaw_Selector(CamPathValue const &v)
	{
		return v.TRzOut;
	}
	static unsigned char RTanModeIn_Yaw_Selector(CamPathValue const &v)
	{
		return v.TRzModeIn;
	}
	static unsigned char RTanModeOut_Yaw_Selector(CamPathValue const &v)
	{
		return v.TRzModeOut;
	}
	static double RTanWIn_Yaw_Selector(CamPathValue const &v)
	{
		return v.TRzWIn;
	}
	static double RTanWOut_Yaw_Selector(CamPathValue const &v)
	{
		return v.TRzWOut;
	}

	bool m_Enabled;
	bool m_Hold = false;
	AutoTangentSettings m_AutoTangentSettings;
	DoubleInterp m_PositionInterpMethod;
	QuaternionInterp m_RotationInterpMethod;
	DoubleInterp m_FovInterpMethod;
	double m_Offset;

	CInterpolationMap<CamPathValue> m_Map;
	CInterpolationMap<CamPathValue> m_EvalMap;
	CamPathValue EvalRaw(double time);
	std::vector<SpeedKey> m_SpeedProfile;
	std::vector<NoiseModifier> m_Modifiers;
	struct ModifierCurve
	{
		std::vector<double> Values[7], Slopes[7];
	};
	std::vector<double> m_ModifierTimes;
	std::vector<ModifierCurve> m_ModifierCurves;
	unsigned long long m_ModifierRevision = ~0ull;
	// One bounded input snapshot reuses the unchanged prefix while editing a stack.
	unsigned long long m_BaseRevision = 0, m_ModifierInputRevision = ~0ull;
	size_t m_ModifierPrefix = 0;
	std::vector<Modifier> m_ModifierInputs;
	std::vector<CamPathValue> m_ModifierInputValues;
	void BuildModifierCurves();
	std::vector<double> m_SpeedArea;
	double m_SpeedIntegral = 1;
	bool m_SpeedDirty = true;
	void BuildSpeedProfile();
	double ProfileDistance(double time);
	double ProfileTime(double distance);
	bool m_DistanceTiming = false, m_DistanceTimingPose = false, m_ArcDirty = true;
	struct ArcSegment
	{
		double Begin, End, Length = 0, TravelBegin = 0;
		bool StopBefore = false, StopAfter = false;
		std::vector<double> Times, Distances;
	};
	std::vector<ArcSegment> m_Arcs;
	double m_TravelDuration = 0;
	void BuildArcs();
	double MapDistanceTime(double time);
	static QEulerAngles EulerSelector(const CamPathValue &value)
	{
		return value.GetEulerAngles();
	}
	static bool ExactEulerSelector(const CamPathValue &value)
	{
		return value.HasEuler;
	}

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

	unsigned long long m_Revision = 0;
	unsigned long long m_GeometryRevision = 0, m_PositionRevision = 0;
	std::vector<double> m_KeyTimes;
	std::vector<size_t> m_KeyArrival, m_KeyRelease;
	bool HasPositionModifiers() const;
	static bool ValidModifiers(const std::vector<NoiseModifier> &modifiers);
	void Changed(bool geometry = true, bool invalidateArcs = true, bool position = true,
				 bool baseCurves = true);
	void CopyMap(CInterpolationMap<CamPathValue> & dst, CInterpolationMap<CamPathValue> & src);

	void DoInterpolationMapChangedAll(bool invalidateArcs = true);
};

// A document keeps ordinary CamPath shots and one native editable path. The
// editor exposes only names; playback automatically chooses the latest start.
class CamPathCollection
{
  public:
	struct Entry
	{
		unsigned long long Id = 0;
		std::string Name;
		std::shared_ptr<const CamPath::State> Data;
		std::wstring FileName;
		bool Muted = false, Solo = false, Visible = true;
	};
	struct State
	{
		size_t Active = 0;
		std::vector<Entry> Paths;
	};
	explicit CamPathCollection(CamPath &active);
	State CaptureState() const;
	void RestoreState(const State &state);
	size_t GetCount() const
	{
		return m_Paths.size();
	}
	size_t GetActive() const
	{
		return m_ActiveIndex;
	}
	unsigned long long GetId(size_t index) const
	{
		return m_Paths.at(index).Id;
	}
	const std::string &GetName(size_t index) const
	{
		return m_Paths.at(index).Name;
	}
	const std::wstring &GetFileName() const
	{
		return m_Paths.at(m_ActiveIndex).FileName;
	}
	const std::wstring &GetCollectionFileName() const
	{
		return m_FileName;
	}
	bool Select(size_t index);
	bool NewPath(const std::string &name = std::string(), bool duplicate = false);
	bool Rename(const std::string &name);
	bool Remove(size_t index);
	CamPath *GetPath(size_t index);
	CamPath *GetPlaybackPath(double clientTime);
	size_t GetPlaybackIndex(double clientTime, bool respectEnabled = true);
	bool SetFlags(size_t index, bool muted, bool solo, bool visible);
	bool IsMuted(size_t index) const
	{
		return m_Paths.at(index).Muted;
	}
	bool IsSolo(size_t index) const
	{
		return m_Paths.at(index).Solo;
	}
	bool IsVisible(size_t index) const
	{
		return m_Paths.at(index).Visible;
	}
	bool SaveOne(const wchar_t *fileName);
	bool LoadOne(const wchar_t *fileName);
	bool SaveAll(const wchar_t *fileName, bool recovery = false);
	bool LoadAll(const wchar_t *fileName, bool recovery = false);
	bool IsDirty() const;
	bool IsDirty(size_t index) const;
	void BeginEdit();
	void Observe();
	void FinishEdit();
	bool Undo(bool redo = false);
	bool CanUndo(bool redo = false) const;
	static bool SameContent(const State &a, const State &b);

  private:
	CamPath &m_ActivePath;
	std::vector<Entry> m_Paths;
	size_t m_ActiveIndex = 0;
	unsigned long long m_NextId = 1;
	std::wstring m_FileName;
	State m_Saved, m_Last, m_Before;
	bool m_HistoryReady = false, m_Editing = false;
	std::vector<State> m_Undo, m_Redo;
	std::vector<std::unique_ptr<CamPath>> m_Caches;
	std::vector<std::shared_ptr<const CamPath::State>> m_CacheStates;
	mutable unsigned long long m_LastActiveRevision = ~0ull;
	mutable std::shared_ptr<const CamPath::State> m_CurrentState;
	// Snapshots are immutable; selection-only changes need one comparison, not one per frame.
	struct DirtyCache
	{
		std::weak_ptr<const CamPath::State> Current, Saved;
		bool Dirty = false;
	};
	mutable std::vector<DirtyCache> m_DirtyCaches;
	const std::shared_ptr<const CamPath::State> &CurrentState() const;
	bool NameAvailable(const std::string &name, size_t except) const;
	void Sync();
	void Notify();
	void MarkSaved(bool all);
	static void PushHistory(std::vector<State> &history, const State &state);
};
