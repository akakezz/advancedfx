#pragma once

// Some standard-library headers include math.h; enable Windows constants first.
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

/*

About Quake coordinate system:

https://github.com/ripieces/advancedfx/wiki/Half-Life-coordinate-system


Quake coordinate system Cross Product:

X = Y x Z
Y = Z x X
Z = X x Y

Z x Y = -X
X x Z = -Y
Y x X = -Z

X x X = 0
Y x Y = 0
Z x Z = 0

u = u_1 X + u_2 Y + u_3 Z
v = v_1 X + v_2 Y + v_3 Z

u x v = (u_1 X + u_2 Y + u_3 Z) x (v_1 X + v_2 Y + v_3 Z)
= u_1 v_1 (X x X) +u_1 v_2 (X x Y) +u_1 v_3 (X x Z)
 +u_2 v_1 (Y X X) +u_2 v_2 (Y x Y) +u_2 v_3 (Y x Z)
 +u_3 v_1 (Z x X) +u_3 v_2 (Z x Y) +u_3 v_3 (Z x Z)
= u_1 v_1 0 +u_1 v_2 Z -u_1 v_3 Y
 -u_2 v_1 Z +u_2 v_2 0 +u_2 v_3 X
 +u_3 v_1 Y -u_3 v_2 X +u_3 v_3 0
= (u_2 v_3 -u_3 v_2) X +(u_3 v_1 -u_1 v_3) Y +(u_1 v_2 -u_2 v_1) Z

*/

#include <array>
#include <cmath>
#include <atomic>
#include <map>
#include <algorithm>
#include <mutex>
#include <vector>

namespace Afx {
namespace Math {


#define AFX_MATH_EPS 1.0e-6

double AngleModDeg(double x);

/// <summary>Make vectors from angles in degrees in right-hand-grip rule.</summary>
void MakeVectors(
	double rForward, double rLeft, double rUp,
	double (& outForward)[3], double (& outRight)[3], double (& outUp)[3]
);


bool LUdecomposition(const double matrix[4][4], unsigned char outP[4], unsigned char outQ[4], double outL[4][4], double outU[4][4]);

void SolveWithLU(const double L[4][4], const double U[4][4], const unsigned char P[4], const unsigned char Q[4], const double b[4], double outX[4]);

bool LUdecompositionEx(const double ** matrix, unsigned char *outP, unsigned char *outQ, double **outL, double **outU, int size);

void SolveWithLUEx(const double ** L, const double **U, const unsigned char *P, const unsigned char *Q, const double *b, double *outX, int size4, double *y);

////////////////////////////////////////////////////////////////////////////////

void spline(double x[], double y[], int n, bool y1Natural, double yp1, bool ynNatural, double ypn, double y2[]);

void splint(double xa[], double ya[], double y2a[], int n, double x, double *y);

void qspline_init(
	int n, int maxit, double tol, double wi[], double wf[],
	double x[], double y[][4],
	double h[], double dtheta[], double e[][3], double w[][3]
);

void qspline_interp(
	int n, double xi, double x[], double y[][4],
	double h[], double dtheta[], double e[][3], double w[][3], 
	double q[4], double omega[3], double alpha[3]
);

double getang(double qi[], double qf[], double e[]);

// Vector3 /////////////////////////////////////////////////////////////////////

struct Vector3;

Vector3 operator * (double value, Vector3 x);

struct Vector3
{
public:
	/// <remarks>Data is uninitalized for this constructor.</remarks>
	Vector3();

	Vector3(double x, double y, double z);
	Vector3(double value[3]);

	Vector3(const Vector3 & v);

	Vector3 operator + (const Vector3 & y) const;
	
	void operator += (const Vector3 & y);

	Vector3 operator - (const Vector3 & y) const;

	void operator -= (const Vector3 & y);

	Vector3 operator * (double value) const;

	void operator *= (double value);

	Vector3 operator / (double value) const;

	void operator /= (double value);

	void ToArray(double (& outValue)[3]) const;

	double Length() const;

	Vector3 Normalize();

public:
	double X;
	double Y;
	double Z;

};

// QEulerAngles ////////////////////////////////////////////////////////////////

/// <summary>
/// Quake Euler angles in degrees, applied in order of pitch(Y/left), yaw(Z/up), roll(X/forward), right hand grip rule.
/// </summary>
struct QEulerAngles
{
	double Pitch;
	double Yaw;
	double Roll;

	QEulerAngles(double pitch, double yaw, double roll);
};

// QREulerAngles ///////////////////////////////////////////////////////////////

/// <summary>
/// Quake Euler angles in radians, applied in order of pitch(Y/left), yaw(Z/up), roll(X/forward), right hand grip rule.
/// </summary>
struct QREulerAngles
{
	static QREulerAngles FromQEulerAngles(QEulerAngles a);

	double Pitch;
	double Yaw;
	double Roll;

	QREulerAngles(double pitch, double yaw, double roll);

	QEulerAngles ToQEulerAngles(void);
};

// Quaternion //////////////////////////////////////////////////////////////////

struct Quaternion;

Quaternion operator +(Quaternion a, Quaternion b);

Quaternion operator *(double a, Quaternion b);

Quaternion operator *(Quaternion a, Quaternion b);

double DotProduct(Quaternion a, Quaternion b);

struct Quaternion
{
    static Quaternion FromQREulerAngles(QREulerAngles a);

	double W;
	double X;
	double Y;
	double Z;

	Quaternion();
    Quaternion(double w, double x, double y, double z);

    double Norm() const;

	Quaternion Normalized() const;

	Quaternion Conjugate() const;
	
	/// <returns>slew angle between this and y in radians</returns>
	double GetAng(Quaternion const & y, Vector3 & outEigenAxis) const;

	Quaternion Slerp(Quaternion const & y, double t) const;

	/// <remarks>Don't use this function for security critical things, it may still have some bugs.</remarks>
    QREulerAngles ToQREulerAngles() const;
};

////////////////////////////////////////////////////////////////////////////////

template<class T>
class CInterpolationMap
: public std::map<double, T>
{
};

template<class TMap, class T>
class CInterpolationMapViewIterator
{
public:
	/// <remarks>
	/// Object state will be invalid!
	/// For temporary variable use only (meaning copy constructor will initalize it afterwards).
	/// </remarks>
	CInterpolationMapViewIterator()
	{
	}

	CInterpolationMapViewIterator(typename CInterpolationMap<TMap>::const_iterator mapIterator,
								  T (*selector)(TMap const &value))
		: m_MapIterator(mapIterator), m_Selector(selector)
	{
	}

	double GetTime()
	{
		return m_MapIterator->first;
	}

	T const GetValue()
	{
		return m_Selector(m_MapIterator->second);
	}

	CInterpolationMapViewIterator<TMap, T> & operator ++ ()
	{
		++m_MapIterator;

		return *this;
	}

	CInterpolationMapViewIterator<TMap, T> & operator -- ()
	{
		--m_MapIterator;

		return *this;
	}

	bool operator == (CInterpolationMapViewIterator<TMap, T> const &it) const
	{
		return m_MapIterator == it.m_MapIterator;
	}

	bool operator != (CInterpolationMapViewIterator<TMap, T> const &it) const
	{
		return m_MapIterator != it.m_MapIterator;
	}

private:
	typename CInterpolationMap<TMap>::const_iterator m_MapIterator;
	T (* m_Selector)(TMap const & value);
};

template<class TMap, class T>
class CInterpolationMapView
{
public:
	CInterpolationMapView(CInterpolationMap<TMap> * map, T (* selector)(TMap const & value))
	: m_Map(map)
	, m_Selector(selector)
	{
	}

	CInterpolationMapViewIterator<TMap, T> GetBegin(void)
	{
		return CInterpolationMapViewIterator<TMap, T>(m_Map->begin(), m_Selector);
	}

	CInterpolationMapViewIterator<TMap, T> GetEnd(void)
	{
		return CInterpolationMapViewIterator<TMap, T>(m_Map->end(), m_Selector);
	}

	size_t GetSize() const
	{
		return m_Map->size();
	}

	void GetNearestInterval(double time, CInterpolationMapViewIterator<TMap, T> & outLower, CInterpolationMapViewIterator<TMap, T> & outUpper)
	{
		size_t size = m_Map->size();

		if(size < 2)
		{
			outLower = GetBegin();
			outUpper = outLower;
		}
		else
		{
			// 2 <= size.

			CInterpolationMapViewIterator<TMap, T> end = GetEnd();

			outUpper = CInterpolationMapViewIterator<TMap, T>(m_Map->upper_bound(time), m_Selector);

			if(end == outUpper)
			{
				--outUpper;

				outLower = outUpper;

				--outLower;
			}
			else
			{
				CInterpolationMapViewIterator<TMap, T> begin = GetBegin();

				if(begin == outUpper)
				{
					outLower = outUpper;
					++outUpper;
				}
				else
				{
					outLower = outUpper;
					--outLower;
				}
			}
		}
	}

private:
	CInterpolationMap<TMap> * m_Map;
	T (* m_Selector)(TMap const & value);
};

template<class T>
class CInterpolation abstract
{
public:
	CInterpolation()
	{
	}

	virtual ~CInterpolation()
	{
	}

	virtual void InterpolationMapChanged(void) = 0;

	virtual bool CanEval(void) const = 0;

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	virtual T Eval(double t) = 0;
};

// Weighted Hermite controls are Bezier handles in time/value space. Solve
// their time coordinate so graph handles and playback describe the same curve.
inline void ResolveHermiteWeights(double &outWeight, double &inWeight)
{
	outWeight = (std::max)(0.0001, outWeight);
	inWeight = (std::max)(0.0001, inWeight);
	if (outWeight + inWeight > 3)
	{
		double scale = 3 / (outWeight + inWeight);
		outWeight *= scale;
		inWeight *= scale;
	}
}
inline double EvalWeightedHermite(double progress, double duration, double left, double right,
								  double outSlope, double inSlope, double outWeight, double inWeight)
{
	ResolveHermiteWeights(outWeight, inWeight);
	double a = outWeight / 3, b = 1 - inWeight / 3, t = progress;
	auto cubic = [](double u, double p1, double p2) {
		double v = 1 - u;
		return 3 * v * v * u * p1 + 3 * v * u * u * p2 + u * u * u;
	};
	if (outWeight != 1 || inWeight != 1)
	{
		double lo = 0, hi = 1;
		for (int i = 0; i < 40; ++i)
		{
			double error = cubic(t, a, b) - progress;
			if (error > -1e-13 && error < 1e-13)
				break;
			if (error < 0)
				lo = t;
			else
				hi = t;
			double v = 1 - t, derivative = 3 * v * v * a + 6 * v * t * (b - a) + 3 * t * t * (1 - b);
			double candidate = derivative > 1e-12 ? t - error / derivative : (lo + hi) * 0.5;
			t = candidate > lo && candidate < hi ? candidate : (lo + hi) * 0.5;
		}
	}
	double v = 1 - t;
	return v * v * v * left + 3 * v * v * t * (left + outWeight * duration * outSlope / 3) +
		   3 * v * t * t * (right - inWeight * duration * inSlope / 3) + t * t * t * right;
}

// Whole-path automatic tangent policy, shared by scalar and Euler channels.
enum class AutoTangentMethod
{
	Spline,
	CatmullRom,
	Monotone,
	MinimumJerk,
	Count
};
struct AutoTangentSettings
{
	AutoTangentMethod Method = AutoTangentMethod::Spline;
	double Tension = 0.0;
	bool operator==(const AutoTangentSettings &other) const
	{
		return Method == other.Method && Tension == other.Tension;
	}
};
const char *AutoTangentMethodToString(AutoTangentMethod method);
bool AutoTangentMethodFromString(const char *text, AutoTangentMethod &method);
double EvalQuinticHermite(double u, double h, double left, double right, double v0, double v1, double a0,
						  double a1, double *velocity = nullptr, double *acceleration = nullptr);
void ComputeAutoTangents(double *times, double *values, int count, const AutoTangentSettings &settings,
                         std::vector<double> &slopes);

// Global quintic interpolation minimizing the integral of squared jerk.
// Adjacent intervals share velocity and acceleration. The banded solve is O(n).
class CMinimumJerkCurve
{
  public:
	bool Build(const double *times, const double *values, int count,
			   const std::vector<unsigned char> &constraints = {}, const double *velocities = nullptr,
			   const double *accelerations = nullptr);
	double Eval(double time) const;
	double Velocity(size_t index) const
	{
		return m_Velocities[index];
	}
	double Acceleration(size_t index, bool outgoing) const
	{
		return outgoing ? m_OutA[index] : m_InA[index];
	}
	void OverrideDerivatives(const double *incoming, const double *outgoing, const double *accelIn,
							 const double *accelOut);

  private:
	std::vector<double> m_Times, m_Values, m_Velocities, m_Accelerations, m_InV, m_OutV, m_InA, m_OutA;
};
bool BuildEditableMinimumJerk(const double *times, const double *values, int count, double *incoming,
							  double *outgoing, const double *weightsIn, const double *weightsOut,
							  const std::vector<unsigned char> &constraints,
							  const AutoTangentSettings &settings, CMinimumJerkCurve &curve,
							  const double *accelIn = nullptr, const double *accelOut = nullptr);

// Hermite (per-key slope) interpolation for doubles.
// Uses per-key in/out slopes and modes resolved via provided selectors.
// Selectors are free functions retrieving values from the TMap (e.g. CamPathValue).
//
// Requirements for callers:
// - Provide selectors for: value, tangent-in, tangent-out, mode-in, mode-out.
// - Modes are encoded as uint8_t: 0=Auto, 1=Flat, 2=Linear, 3=Free (caller convention).
// - Interpolation clamps outside range (no extrapolation).
template <class TMap> class CHermiteDoubleInterpolation : public CInterpolation<double>
{
  public:
	CHermiteDoubleInterpolation(CInterpolationMap<TMap> *map, double (*valueSelector)(const TMap &),
								double (*tanInSelector)(const TMap &), double (*tanOutSelector)(const TMap &),
								unsigned char (*modeInSelector)(const TMap &),
								unsigned char (*modeOutSelector)(const TMap &))
		: CInterpolation<double>(), m_Map(map), m_ValueSelector(valueSelector),
		  m_TanInSelector(tanInSelector), m_TanOutSelector(tanOutSelector), m_ModeInSelector(modeInSelector),
		  m_ModeOutSelector(modeOutSelector)
	{
		InterpolationMapChanged();
	}

	CHermiteDoubleInterpolation(CInterpolationMap<TMap> *map, double (*valueSelector)(const TMap &),
								double (*tanInSelector)(const TMap &), double (*tanOutSelector)(const TMap &),
								unsigned char (*modeInSelector)(const TMap &),
								unsigned char (*modeOutSelector)(const TMap &),
								double (*wInSelector)(const TMap &), double (*wOutSelector)(const TMap &),
								const AutoTangentSettings *autoSettings = nullptr,
								double (*accelSelector)(const TMap &, bool) = nullptr)
		: m_Map(map), m_ValueSelector(valueSelector), m_TanInSelector(tanInSelector),
		  m_TanOutSelector(tanOutSelector), m_ModeInSelector(modeInSelector),
		  m_ModeOutSelector(modeOutSelector), m_WInSelector(wInSelector ? wInSelector : &DefaultWeight),
		  m_WOutSelector(wOutSelector ? wOutSelector : &DefaultWeight), m_AutoSettings(autoSettings),
		  m_AccelSelector(accelSelector)
	{
		InterpolationMapChanged();
	}

	virtual ~CHermiteDoubleInterpolation()
	{
		Free();
	}

	virtual void InterpolationMapChanged(void)
	{
		m_Rebuild.store(true, std::memory_order_relaxed);
	}

	virtual bool CanEval(void) const
	{
		return 2 <= m_Map->size();
	}

	virtual double Eval(double t)
	{
		std::lock_guard<std::mutex> _lock(m_Lock);
		size_t n = m_Map->size();
		if (n < 2)
			throw "CHermiteDoubleInterpolation::Eval requires at least 2 points.";

		if (m_Rebuild.load(std::memory_order_relaxed))
		{
			m_Rebuild.store(false, std::memory_order_relaxed);

			Free();

			m_Build.n = (int)n;
			m_Build.T = new double[n];
			m_Build.V = new double[n];
			m_Build.MIn = new double[n];
			m_Build.MOut = new double[n];
			m_Build.WIn = new double[n];
			m_Build.WOut = new double[n];
			m_Build.Has = true;

			// Copy values
			size_t i = 0;
			for (typename CInterpolationMap<TMap>::const_iterator it = m_Map->begin(); it != m_Map->end();
				 ++it)
			{
				m_Build.T[i] = it->first;
				const TMap &val = it->second;
				m_Build.V[i] = m_ValueSelector(val);
				// Raw user-provided tangents (only used for Free mode)
				double rawIn = m_TanInSelector(val);
				double rawOut = m_TanOutSelector(val);
				unsigned char modeIn = m_ModeInSelector(val);
				unsigned char modeOut = m_ModeOutSelector(val);
				// Initialize; actual resolution below (needs neighbors)
				m_Build.MIn[i] = (modeIn == 3 /*Free*/) ? rawIn : 0.0;
				m_Build.MOut[i] = (modeOut == 3 /*Free*/) ? rawOut : 0.0;
				++i;
			}
			for (int k = 0; k < (int)n; ++k)
			{
				const TMap &val = m_Map->find(m_Build.T[k])->second;
				unsigned char modeIn = m_ModeInSelector(val);
				unsigned char modeOut = m_ModeOutSelector(val);
				m_Build.WIn[k] = (modeIn == 3 /*Free*/) ? m_WInSelector(val) : 1.0;
				m_Build.WOut[k] = (modeOut == 3 /*Free*/) ? m_WOutSelector(val) : 1.0;
				if (m_Build.WIn[k] < 0.0)
					m_Build.WIn[k] = 0.0;
				if (m_Build.WOut[k] < 0.0)
					m_Build.WOut[k] = 0.0;
			}

			std::vector<double> autoSlopes;
			ComputeAutoTangents(m_Build.T, m_Build.V, (int)n,
				m_AutoSettings ? *m_AutoSettings : AutoTangentSettings{}, autoSlopes);

			// Resolve modes to effective slopes.
			for (size_t k = 0; k < n; ++k)
			{
				// neighbors
				bool hasPrev = k > 0;
				bool hasNext = k + 1 < n;
				double ti = m_Build.T[k];
				double vi = m_Build.V[k];
				double tim1 = hasPrev ? m_Build.T[k - 1] : ti;
				double tip1 = hasNext ? m_Build.T[k + 1] : ti;
				double vim1 = hasPrev ? m_Build.V[k - 1] : vi;
				double vip1 = hasNext ? m_Build.V[k + 1] : vi;

				const TMap &val = m_Map->find(m_Build.T[k])->second;
				unsigned char modeIn = m_ModeInSelector(val);
				unsigned char modeOut = m_ModeOutSelector(val);

				// In slope
				if (modeIn == 1 /*Flat*/)
				{
					m_Build.MIn[k] = 0.0;
				}
				else if (modeIn == 2 /*Linear*/)
				{
					if (hasPrev)
						m_Build.MIn[k] = (vi - vim1) / (ti - tim1);
					else if (hasNext)
						m_Build.MIn[k] = (vip1 - vi) / (tip1 - ti);
					else
						m_Build.MIn[k] = 0.0;
				}
				else if (modeIn == 0 /*Auto*/)
				{
					m_Build.MIn[k] = autoSlopes[k];
				}
				// else Free already set from raw

				// Out slope
				if (modeOut == 1 /*Flat*/)
				{
					m_Build.MOut[k] = 0.0;
				}
				else if (modeOut == 2 /*Linear*/)
				{
					if (hasNext)
						m_Build.MOut[k] = (vip1 - vi) / (tip1 - ti);
					else if (hasPrev)
						m_Build.MOut[k] = (vi - vim1) / (ti - tim1);
					else
						m_Build.MOut[k] = 0.0;
				}
				else if (modeOut == 0 /*Auto*/)
				{
					m_Build.MOut[k] = autoSlopes[k];
				}
				// else Free already set from raw
			}

			std::vector<unsigned char> constraints(n);
			std::vector<double> accelIn, accelOut;
			size_t keyIndex = 0;
			for (const auto &entry : *m_Map)
			{
				const auto mi = m_ModeInSelector(entry.second), mo = m_ModeOutSelector(entry.second);
				if (m_AccelSelector)
				{
					accelIn.push_back(m_AccelSelector(entry.second, false));
					accelOut.push_back(m_AccelSelector(entry.second, true));
				}
				constraints[keyIndex++] = mi == 1 || mo == 1 ? 1 : mi || mo ? 2 : 0;
			}
			m_UseJerk = BuildEditableMinimumJerk(
				m_Build.T, m_Build.V, (int)n, m_Build.MIn, m_Build.MOut, m_Build.WIn, m_Build.WOut,
				constraints, m_AutoSettings ? *m_AutoSettings : AutoTangentSettings{}, m_Jerk,
				accelIn.empty() ? nullptr : accelIn.data(), accelOut.empty() ? nullptr : accelOut.data());
		}

		if (m_UseJerk)
			return m_Jerk.Eval(t);

		// Clamp for no extrapolation
		if (t <= m_Build.T[0])
			return m_Build.V[0];
		if (t >= m_Build.T[m_Build.n - 1])
			return m_Build.V[m_Build.n - 1];

		// Binary search for interval
		int klo = 0, khi = m_Build.n - 1;
		while (khi - klo > 1)
		{
			int k = (khi + klo) >> 1;
			if (m_Build.T[k] > t)
				khi = k;
			else
				klo = k;
		}

		double t0 = m_Build.T[klo];
		double t1 = m_Build.T[khi];
		double v0 = m_Build.V[klo];
		double v1 = m_Build.V[khi];
		double m0 = m_Build.MOut[klo];
		double m1 = m_Build.MIn[khi];
		double h = t1 - t0;
		if (h <= 0.0)
			return v0; // guard

		double u = (t - t0) / h; // 0..1
		double u2 = u * u;
		double u3 = u2 * u;
		double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
		double h10 = u3 - 2.0 * u2 + u;
		double h01 = -2.0 * u3 + 3.0 * u2;
		double h11 = u3 - u2;

		double w0 = m_Build.WOut[klo]; // OUT weight of left key
		double w1 = m_Build.WIn[khi];  // IN weight of right key
		return EvalWeightedHermite(u, h, v0, v1, m0, m1, w0, w1);
	}

	bool GetTangent(size_t index, bool outgoing, double &slope, double &weight)
	{
		if (index >= m_Map->size() || !CanEval()) return false;
		Eval(m_Map->begin()->first);
		std::lock_guard<std::mutex> lock(m_Lock);
		if (index >= (size_t)m_Build.n) return false;
		slope = outgoing ? m_Build.MOut[index] : m_Build.MIn[index];
		weight = outgoing ? m_Build.WOut[index] : m_Build.WIn[index];
		return true;
	}

	bool GetAcceleration(size_t index, bool outgoing, double &acceleration)
	{
		if (index >= m_Map->size() || !CanEval())
			return false;
		Eval(m_Map->begin()->first);
		std::lock_guard<std::mutex> lock(m_Lock);
		if (!m_UseJerk)
			return false;
		acceleration = m_Jerk.Acceleration(index, outgoing);
		return true;
	}

  private:
	CInterpolationMap<TMap> *m_Map;
	double (*m_ValueSelector)(const TMap &);
	double (*m_TanInSelector)(const TMap &);
	double (*m_TanOutSelector)(const TMap &);
	unsigned char (*m_ModeInSelector)(const TMap &);
	unsigned char (*m_ModeOutSelector)(const TMap &);
	static double DefaultWeight(const TMap &)
	{
		return 1.0;
	}
	double (*m_WInSelector)(const TMap &) = &DefaultWeight;
	double (*m_WOutSelector)(const TMap &) = &DefaultWeight;
	const AutoTangentSettings *m_AutoSettings = nullptr;

	double (*m_AccelSelector)(const TMap &, bool) = nullptr;
	CMinimumJerkCurve m_Jerk;
	bool m_UseJerk = false;

	struct Build_s
	{
		int n = 0;
		bool Has = false;
		double *T = 0;
		double *V = 0;
		double *MIn = 0;
		double *MOut = 0;
		double *WIn = 0;
		double *WOut = 0;
	} m_Build;

	std::atomic_bool m_Rebuild{true};
	std::mutex m_Lock;

	void Free()
	{
		delete[] m_Build.MOut;
		m_Build.MOut = 0;
		delete[] m_Build.MIn;
		m_Build.MIn = 0;
		delete[] m_Build.V;
		m_Build.V = 0;
		delete[] m_Build.T;
		m_Build.T = 0;
		delete[] m_Build.WIn;
		m_Build.WIn = 0;
		delete[] m_Build.WOut;
		m_Build.WOut = 0;
		m_Build.n = 0;
	}
};

// Euler-angle based Hermite interpolation composing a Quaternion.
// Uses per-key tangent in/out + mode for each Euler channel (Pitch/Yaw/Roll).
// Value extraction from map uses Quaternion->Euler conversion and performs
// simple unwrap across keys (minimize 360-degree jumps) per channel.
template <class TMap> class CEulerHermiteQuaternionInterpolation : public CInterpolation<Quaternion>
{
  public:
	CEulerHermiteQuaternionInterpolation(
		CInterpolationMap<TMap> *map,
		// Roll (X)
		double (*tanInRoll)(const TMap &), double (*tanOutRoll)(const TMap &),
		unsigned char (*modeInRoll)(const TMap &), unsigned char (*modeOutRoll)(const TMap &),
		double (*wInRoll)(const TMap &), double (*wOutRoll)(const TMap &),
		// Pitch (Y)
		double (*tanInPitch)(const TMap &), double (*tanOutPitch)(const TMap &),
		unsigned char (*modeInPitch)(const TMap &), unsigned char (*modeOutPitch)(const TMap &),
		double (*wInPitch)(const TMap &), double (*wOutPitch)(const TMap &),
		// Yaw (Z)
		double (*tanInYaw)(const TMap &), double (*tanOutYaw)(const TMap &),
		unsigned char (*modeInYaw)(const TMap &), unsigned char (*modeOutYaw)(const TMap &),
		double (*wInYaw)(const TMap &), double (*wOutYaw)(const TMap &),
		const AutoTangentSettings *autoSettings = nullptr,
		QEulerAngles (*eulerSelector)(const TMap &) = nullptr,
		bool (*exactEulerSelector)(const TMap &) = nullptr,
		double (*accelSelector)(const TMap &, int, bool) = nullptr)
		: m_Map(map), m_TanInRoll(tanInRoll), m_TanOutRoll(tanOutRoll), m_ModeInRoll(modeInRoll),
		  m_ModeOutRoll(modeOutRoll), m_WInRoll(wInRoll), m_WOutRoll(wOutRoll), m_TanInPitch(tanInPitch),
		  m_TanOutPitch(tanOutPitch), m_ModeInPitch(modeInPitch), m_ModeOutPitch(modeOutPitch),
		  m_WInPitch(wInPitch), m_WOutPitch(wOutPitch), m_TanInYaw(tanInYaw), m_TanOutYaw(tanOutYaw),
		  m_ModeInYaw(modeInYaw), m_ModeOutYaw(modeOutYaw), m_WInYaw(wInYaw), m_WOutYaw(wOutYaw),
		  m_AutoSettings(autoSettings), m_EulerSelector(eulerSelector),
		  m_ExactEulerSelector(exactEulerSelector), m_AccelSelector(accelSelector)
	{
		InterpolationMapChanged();
	}

	virtual ~CEulerHermiteQuaternionInterpolation()
	{
		Free();
	}

	virtual void InterpolationMapChanged(void)
	{
		m_Rebuild.store(true, std::memory_order_relaxed);
	}

	virtual bool CanEval(void) const
	{
		return 2 <= m_Map->size();
	}

	virtual Quaternion Eval(double t)
	{
		return Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(EvalAngles(t)));
	}
	QEulerAngles EvalAngles(double t)
	{
		std::lock_guard<std::mutex> _lock(m_Lock);
		size_t n = m_Map->size();
		if (n < 2)
			throw "CEulerHermiteQuaternionInterpolation::Eval requires at least 2 points.";

		if (m_Rebuild.load(std::memory_order_relaxed))
		{
			m_Rebuild.store(false, std::memory_order_relaxed);
			Build();
		}

		if (m_UseJerk[0] && m_UseJerk[1] && m_UseJerk[2])
			return QEulerAngles(m_Jerk[0].Eval(t), m_Jerk[1].Eval(t), m_Jerk[2].Eval(t));
		// Clamp for no extrapolation
		if (t <= m_T[0])
		{
			return QEulerAngles(m_Pitch[0], m_Yaw[0], m_Roll[0]);
		}
		if (t >= m_T[m_n - 1])
		{
			return QEulerAngles(m_Pitch[m_n - 1], m_Yaw[m_n - 1], m_Roll[m_n - 1]);
		}

		// Binary search
		int klo = 0, khi = (int)m_n - 1;
		while (khi - klo > 1)
		{
			int k = (khi + klo) >> 1;
			if (m_T[k] > t)
				khi = k;
			else
				klo = k;
		}

		double t0 = m_T[klo];
		double t1 = m_T[khi];
		double h = t1 - t0;
		if (h <= 0.0)
			h = 1.0; // guard

		auto hermite = [&](double v0, double v1, double m0, double m1, double w0, double w1) -> double {
			double u = (t - t0) / h;
			double u2 = u * u;
			double u3 = u2 * u;
			double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
			double h10 = u3 - 2.0 * u2 + u;
			double h01 = -2.0 * u3 + 3.0 * u2;
			double h11 = u3 - u2;
			return EvalWeightedHermite(u, h, v0, v1, m0, m1, w0, w1);
		};

		double pitch = hermite(m_Pitch[klo], m_Pitch[khi], m_PitchOut[klo], m_PitchIn[khi], m_PitchWOut[klo],
							   m_PitchWIn[khi]);
		double yaw =
			hermite(m_Yaw[klo], m_Yaw[khi], m_YawOut[klo], m_YawIn[khi], m_YawWOut[klo], m_YawWIn[khi]);
		double roll =
			hermite(m_Roll[klo], m_Roll[khi], m_RollOut[klo], m_RollIn[khi], m_RollWOut[klo], m_RollWIn[khi]);

		return QEulerAngles(m_UseJerk[0] ? m_Jerk[0].Eval(t) : pitch, m_UseJerk[1] ? m_Jerk[1].Eval(t) : yaw,
							m_UseJerk[2] ? m_Jerk[2].Eval(t) : roll);
	}

	bool GetTangent(size_t index, int channel, bool outgoing, double &slope, double &weight)
	{
		if (index >= m_Map->size() || !CanEval() || channel < 0 || channel > 2) return false;
		Eval(m_Map->begin()->first);
		std::lock_guard<std::mutex> lock(m_Lock);
		if (index >= (size_t)m_n) return false;
		const double *slopes = channel == 0 ? (outgoing ? m_PitchOut : m_PitchIn)
			: channel == 1 ? (outgoing ? m_YawOut : m_YawIn) : (outgoing ? m_RollOut : m_RollIn);
		const double *weights = channel == 0 ? (outgoing ? m_PitchWOut : m_PitchWIn)
			: channel == 1 ? (outgoing ? m_YawWOut : m_YawWIn) : (outgoing ? m_RollWOut : m_RollWIn);
		slope = slopes[index];
		weight = weights[index];
		return true;
	}

	bool GetAcceleration(size_t index, int channel, bool outgoing, double &acceleration)
	{
		if (index >= m_Map->size() || channel < 0 || channel > 2 || !CanEval())
			return false;
		Eval(m_Map->begin()->first);
		std::lock_guard<std::mutex> lock(m_Lock);
		if (!m_UseJerk[channel])
			return false;
		acceleration = m_Jerk[channel].Acceleration(index, outgoing);
		return true;
	}

  private:
	// Source data
	CInterpolationMap<TMap> *m_Map;

	// Selectors for tangents, modes and weights
	double (*m_TanInRoll)(const TMap &);
	double (*m_TanOutRoll)(const TMap &);
	unsigned char (*m_ModeInRoll)(const TMap &);
	unsigned char (*m_ModeOutRoll)(const TMap &);
	double (*m_WInRoll)(const TMap &);
	double (*m_WOutRoll)(const TMap &);

	double (*m_TanInPitch)(const TMap &);
	double (*m_TanOutPitch)(const TMap &);
	unsigned char (*m_ModeInPitch)(const TMap &);
	unsigned char (*m_ModeOutPitch)(const TMap &);
	double (*m_WInPitch)(const TMap &);
	double (*m_WOutPitch)(const TMap &);

	double (*m_TanInYaw)(const TMap &);
	double (*m_TanOutYaw)(const TMap &);
	unsigned char (*m_ModeInYaw)(const TMap &);
	unsigned char (*m_ModeOutYaw)(const TMap &);
	double (*m_WInYaw)(const TMap &);
	double (*m_WOutYaw)(const TMap &);
	const AutoTangentSettings *m_AutoSettings = nullptr;
	QEulerAngles (*m_EulerSelector)(const TMap &) = nullptr;
	bool (*m_ExactEulerSelector)(const TMap &) = nullptr;

	double (*m_AccelSelector)(const TMap &, int, bool) = nullptr;
	CMinimumJerkCurve m_Jerk[3];
	bool m_UseJerk[3] = {};

	// Cached arrays
	int m_n = 0;
	double *m_T = nullptr;
	double *m_Pitch = nullptr;
	double *m_PitchIn = nullptr;
	double *m_PitchOut = nullptr;
	double *m_PitchWIn = nullptr;
	double *m_PitchWOut = nullptr;
	double *m_Yaw = nullptr;
	double *m_YawIn = nullptr;
	double *m_YawOut = nullptr;
	double *m_YawWIn = nullptr;
	double *m_YawWOut = nullptr;
	double *m_Roll = nullptr;
	double *m_RollIn = nullptr;
	double *m_RollOut = nullptr;
	double *m_RollWIn = nullptr;
	double *m_RollWOut = nullptr;

	std::atomic_bool m_Rebuild{true};
	std::mutex m_Lock;

	static void UnwrapAngles(double *vals, int n)
	{
		if (n <= 1)
			return;
		for (int i = 1; i < n; ++i)
		{
			double prev = vals[i - 1];
			double cur = vals[i];
			while (cur - prev > 180.0)
				cur -= 360.0;
			while (cur - prev < -180.0)
				cur += 360.0;
			vals[i] = cur;
		}
	}

	void Build()
	{
		Free();
		m_n = (int)m_Map->size();
		m_T = new double[m_n];
		m_Pitch = new double[m_n];
		m_PitchIn = new double[m_n];
		m_PitchOut = new double[m_n];
		m_PitchWIn = new double[m_n];
		m_PitchWOut = new double[m_n];
		m_Yaw = new double[m_n];
		m_YawIn = new double[m_n];
		m_YawOut = new double[m_n];
		m_YawWIn = new double[m_n];
		m_YawWOut = new double[m_n];
		m_Roll = new double[m_n];
		m_RollIn = new double[m_n];
		m_RollOut = new double[m_n];
		m_RollWIn = new double[m_n];
		m_RollWOut = new double[m_n];

		// Extract times and Euler angles; setup initial slopes/weights
		int i = 0;
		TMap lastV;
		bool hasLast = false;
		for (typename CInterpolationMap<TMap>::const_iterator it = m_Map->begin(); it != m_Map->end();
			 ++it, ++i)
		{
			m_T[i] = it->first;
			const TMap &v = it->second;
			QEulerAngles a = m_EulerSelector ? m_EulerSelector(v) : v.R.ToQREulerAngles().ToQEulerAngles();
			if (i && !(m_ExactEulerSelector && m_ExactEulerSelector(v)))
			{
				auto unwrap = [](double value, double previous)
				{
					double delta = std::fmod(value - previous, 360.0);
					if (delta > 180)
						delta -= 360;
					if (delta < -180)
						delta += 360;
					return previous + delta;
				};
				a.Pitch = unwrap(a.Pitch, m_Pitch[i - 1]);
				a.Yaw = unwrap(a.Yaw, m_Yaw[i - 1]);
				a.Roll = unwrap(a.Roll, m_Roll[i - 1]);
			}
			m_Pitch[i] = a.Pitch;
			m_Yaw[i] = a.Yaw;
			m_Roll[i] = a.Roll;
		}
		// unwrap each channel to avoid jumps

		// Fill modes, raw/free slopes and weights
		i = 0;
		for (typename CInterpolationMap<TMap>::const_iterator it = m_Map->begin(); it != m_Map->end();
			 ++it, ++i)
		{
			const TMap &v = it->second;
			unsigned char modeInP = m_ModeInPitch(v);
			unsigned char modeOutP = m_ModeOutPitch(v);
			unsigned char modeInY = m_ModeInYaw(v);
			unsigned char modeOutY = m_ModeOutYaw(v);
			unsigned char modeInR = m_ModeInRoll(v);
			unsigned char modeOutR = m_ModeOutRoll(v);

			// default; resolved after cubic auto slope computation
			m_PitchIn[i] = (modeInP == 3) ? m_TanInPitch(v) : 0.0;
			m_PitchOut[i] = (modeOutP == 3) ? m_TanOutPitch(v) : 0.0;
			m_YawIn[i] = (modeInY == 3) ? m_TanInYaw(v) : 0.0;
			m_YawOut[i] = (modeOutY == 3) ? m_TanOutYaw(v) : 0.0;
			m_RollIn[i] = (modeInR == 3) ? m_TanInRoll(v) : 0.0;
			m_RollOut[i] = (modeOutR == 3) ? m_TanOutRoll(v) : 0.0;

			m_PitchWIn[i] = (modeInP == 3) ? m_WInPitch(v) : 1.0;
			m_PitchWOut[i] = (modeOutP == 3) ? m_WOutPitch(v) : 1.0;
			m_YawWIn[i] = (modeInY == 3) ? m_WInYaw(v) : 1.0;
			m_YawWOut[i] = (modeOutY == 3) ? m_WOutYaw(v) : 1.0;
			m_RollWIn[i] = (modeInR == 3) ? m_WInRoll(v) : 1.0;
			m_RollWOut[i] = (modeOutR == 3) ? m_WOutRoll(v) : 1.0;
			if (m_PitchWIn[i] < 0.0)
				m_PitchWIn[i] = 0.0;
			if (m_PitchWOut[i] < 0.0)
				m_PitchWOut[i] = 0.0;
			if (m_YawWIn[i] < 0.0)
				m_YawWIn[i] = 0.0;
			if (m_YawWOut[i] < 0.0)
				m_YawWOut[i] = 0.0;
			if (m_RollWIn[i] < 0.0)
				m_RollWIn[i] = 0.0;
			if (m_RollWOut[i] < 0.0)
				m_RollWOut[i] = 0.0;
		}

		std::vector<double> autoPitch, autoYaw, autoRoll;
		const auto settings = m_AutoSettings ? *m_AutoSettings : AutoTangentSettings{};
		ComputeAutoTangents(m_T, m_Pitch, m_n, settings, autoPitch);
		ComputeAutoTangents(m_T, m_Yaw, m_n, settings, autoYaw);
		ComputeAutoTangents(m_T, m_Roll, m_n, settings, autoRoll);

		// Resolve modes per key
		for (int k = 0; k < m_n; ++k)
		{
			bool hasPrev = (k > 0);
			bool hasNext = (k + 1 < m_n);
			double ti = m_T[k];
			double tim1 = hasPrev ? m_T[k - 1] : ti;
			double tip1 = hasNext ? m_T[k + 1] : ti;
			double vPi = m_Pitch[k];
			double vPim1 = hasPrev ? m_Pitch[k - 1] : vPi;
			double vPip1 = hasNext ? m_Pitch[k + 1] : vPi;
			double vYi = m_Yaw[k];
			double vYim1 = hasPrev ? m_Yaw[k - 1] : vYi;
			double vYip1 = hasNext ? m_Yaw[k + 1] : vYi;
			double vRi = m_Roll[k];
			double vRim1 = hasPrev ? m_Roll[k - 1] : vRi;
			double vRip1 = hasNext ? m_Roll[k + 1] : vRi;

			const TMap &v = m_Map->find(m_T[k])->second;

			// Pitch
			{
				unsigned char mi = m_ModeInPitch(v), mo = m_ModeOutPitch(v);
				if (mi == 1)
					m_PitchIn[k] = 0.0; // Flat
				else if (mi == 2)
					m_PitchIn[k] =
						hasPrev ? (vPi - vPim1) / (ti - tim1) : (hasNext ? (vPip1 - vPi) / (tip1 - ti) : 0.0);
				else if (mi == 0)
					m_PitchIn[k] = autoPitch[k]; // Auto
				// else Free already set
				if (mo == 1)
					m_PitchOut[k] = 0.0;
				else if (mo == 2)
					m_PitchOut[k] =
						hasNext ? (vPip1 - vPi) / (tip1 - ti) : (hasPrev ? (vPi - vPim1) / (ti - tim1) : 0.0);
				else if (mo == 0)
					m_PitchOut[k] = autoPitch[k];
			}
			// Yaw
			{
				unsigned char mi = m_ModeInYaw(v), mo = m_ModeOutYaw(v);
				if (mi == 1)
					m_YawIn[k] = 0.0;
				else if (mi == 2)
					m_YawIn[k] =
						hasPrev ? (vYi - vYim1) / (ti - tim1) : (hasNext ? (vYip1 - vYi) / (tip1 - ti) : 0.0);
				else if (mi == 0)
					m_YawIn[k] = autoYaw[k];
				if (mo == 1)
					m_YawOut[k] = 0.0;
				else if (mo == 2)
					m_YawOut[k] =
						hasNext ? (vYip1 - vYi) / (tip1 - ti) : (hasPrev ? (vYi - vYim1) / (ti - tim1) : 0.0);
				else if (mo == 0)
					m_YawOut[k] = autoYaw[k];
			}
			// Roll
			{
				unsigned char mi = m_ModeInRoll(v), mo = m_ModeOutRoll(v);
				if (mi == 1)
					m_RollIn[k] = 0.0;
				else if (mi == 2)
					m_RollIn[k] =
						hasPrev ? (vRi - vRim1) / (ti - tim1) : (hasNext ? (vRip1 - vRi) / (tip1 - ti) : 0.0);
				else if (mi == 0)
					m_RollIn[k] = autoRoll[k];
				if (mo == 1)
					m_RollOut[k] = 0.0;
				else if (mo == 2)
					m_RollOut[k] =
						hasNext ? (vRip1 - vRi) / (tip1 - ti) : (hasPrev ? (vRi - vRim1) / (ti - tim1) : 0.0);
				else if (mo == 0)
					m_RollOut[k] = autoRoll[k];
			}
		}
		std::vector<unsigned char> constraints[3];
		std::vector<double> accelIn[3], accelOut[3];
		for (const auto &entry : *m_Map)
		{
			const auto &v = entry.second;
			const unsigned char mi[] = {m_ModeInPitch(v), m_ModeInYaw(v), m_ModeInRoll(v)},
								mo[] = {m_ModeOutPitch(v), m_ModeOutYaw(v), m_ModeOutRoll(v)};
			for (int ch = 0; ch < 3; ++ch)
			{
				constraints[ch].push_back(mi[ch] == 1 || mo[ch] == 1 ? 1 : mi[ch] || mo[ch] ? 2 : 0);
				if (m_AccelSelector)
				{
					accelIn[ch].push_back(m_AccelSelector(v, ch, false));
					accelOut[ch].push_back(m_AccelSelector(v, ch, true));
				}
			}
		}
		double *values[] = {m_Pitch, m_Yaw, m_Roll}, *incoming[] = {m_PitchIn, m_YawIn, m_RollIn},
			   *outgoing[] = {m_PitchOut, m_YawOut, m_RollOut}, *win[] = {m_PitchWIn, m_YawWIn, m_RollWIn},
			   *wout[] = {m_PitchWOut, m_YawWOut, m_RollWOut};
		for (int ch = 0; ch < 3; ++ch)
			m_UseJerk[ch] = BuildEditableMinimumJerk(m_T, values[ch], m_n, incoming[ch], outgoing[ch],
													 win[ch], wout[ch], constraints[ch], settings, m_Jerk[ch],
													 accelIn[ch].empty() ? nullptr : accelIn[ch].data(),
													 accelOut[ch].empty() ? nullptr : accelOut[ch].data());
	}

	void Free()
	{
		delete[] m_T;
		m_T = nullptr;
		m_n = 0;
		delete[] m_Pitch;
		delete[] m_PitchIn;
		delete[] m_PitchOut;
		delete[] m_PitchWIn;
		delete[] m_PitchWOut;
		m_Pitch = m_PitchIn = m_PitchOut = m_PitchWIn = m_PitchWOut = nullptr;
		delete[] m_Yaw;
		delete[] m_YawIn;
		delete[] m_YawOut;
		delete[] m_YawWIn;
		delete[] m_YawWOut;
		m_Yaw = m_YawIn = m_YawOut = m_YawWIn = m_YawWOut = nullptr;
		delete[] m_Roll;
		delete[] m_RollIn;
		delete[] m_RollOut;
		delete[] m_RollWIn;
		delete[] m_RollWOut;
		m_Roll = m_RollIn = m_RollOut = m_RollWIn = m_RollWOut = nullptr;
	}
};

template<class TMap>
class CBoolAndInterpolation
: public CInterpolation<bool>
{
public:
	/// <remarks>No extrapolation (will clamp)!</remarks>
	CBoolAndInterpolation(CInterpolationMapView<TMap, bool> * view)
	: CInterpolation<bool>()
	, m_View(view)
	{
		InterpolationMapChanged();
	}

	virtual void InterpolationMapChanged(void)
	{
		// nothing to do here.
	}

	virtual bool CanEval(void) const
	{
		return 2 <= m_View->GetSize();
	}

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	virtual bool Eval(double t)
	{
		CInterpolationMapViewIterator<TMap, bool> itLower;
		CInterpolationMapViewIterator<TMap, bool> itUpper;
		m_View->GetNearestInterval(t, itLower, itUpper);

		double lowerT = itLower.GetTime();
		bool lowerV = itLower.GetValue();

		if(t <= lowerT)
		{
			return lowerV;
		}

		double upperT = itUpper.GetTime();
		bool upperV = itUpper.GetValue();

		if(upperT <= t)
		{
			return upperV;
		}

		return lowerV && upperV;
	}

private:
	CInterpolationMapView<TMap, bool> * m_View;
};

template<class TMap>
class CLinearDoubleInterpolation
: public CInterpolation<double>
{
public:
	/// <remarks>No extrapolation (will clamp)!</remarks>
	CLinearDoubleInterpolation(CInterpolationMapView<TMap, double> * view)
	: CInterpolation<double>()
	, m_View(view)
	{
		InterpolationMapChanged();
	}

	virtual void InterpolationMapChanged(void)
	{
		// nothing to do here.
	}

	virtual bool CanEval(void) const
	{
		return 2 <= m_View->GetSize();
	}

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	virtual double Eval(double t)
	{
		CInterpolationMapViewIterator<TMap, double> itLower;
		CInterpolationMapViewIterator<TMap, double> itUpper;
		m_View->GetNearestInterval(t, itLower, itUpper);

		double lowerT = itLower.GetTime();
		double lowerV = itLower.GetValue();

		if(t <= lowerT)
		{
			return lowerV;
		}

		double upperT = itUpper.GetTime();
		double upperV = itUpper.GetValue();

		if(upperT <= t)
		{
			return upperV;
		}

		double deltaT = upperT -lowerT;

		return (1-(t-lowerT)/deltaT)*lowerV +((t-lowerT)/deltaT)*upperV;
	}

private:
	CInterpolationMapView<TMap, double> * m_View;
};

template<class TMap>
class CCubicDoubleInterpolation
: public CInterpolation<double>
{
public:
	CCubicDoubleInterpolation(CInterpolationMapView<TMap, double> * view)
	: CInterpolation<double>()
	, m_View(view)
	{
		InterpolationMapChanged();
	}

	virtual ~CCubicDoubleInterpolation()
	{
		Free();
	}

	virtual void InterpolationMapChanged(void)
	{
		m_Rebuild = true;
	}

	virtual bool CanEval(void) const
	{
		return 4 <= m_View->GetSize();
	}

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	virtual double Eval(double t)
	{
		int n = m_View->GetSize();

		if(n < 4) throw "CCubicDoubleInterpolation::Eval only allowed with at least 4 points.";

		if(m_Rebuild)
		{
			m_Rebuild = false;

			Free();

			m_Build.T = new double[n];
			m_Build.X = new double[n];
			m_Build.X2 = new double[n];

			{
				int i = 0;
				CInterpolationMapViewIterator<TMap, double> itEnd = m_View->GetEnd();
				for(CInterpolationMapViewIterator<TMap, double> it = m_View->GetBegin(); it != itEnd; ++it)
				{
					m_Build.T[i] = it.GetTime();
					m_Build.X[i] = it.GetValue();
					++i;
				}
			}

			spline(m_Build.T , m_Build.X, n, false, 0.0, false, 0.0, m_Build.X2);
		}

		double result;

		splint(m_Build.T, m_Build.X, m_Build.X2, n, t, &result);

		return result;
	}

	double Derivative(double time, double *acceleration = nullptr)
	{
		Eval(time);
		const int n = m_View->GetSize();
		time = std::clamp(time, m_Build.T[0], m_Build.T[n - 1]);
		int left = 0, right = n - 1;
		while (right - left > 1)
		{
			const int middle = (left + right) / 2;
			if (m_Build.T[middle] > time)
				right = middle;
			else
				left = middle;
		}
		const double h = m_Build.T[right] - m_Build.T[left], a = (m_Build.T[right] - time) / h, b = 1 - a;
		if (acceleration)
			*acceleration = a * m_Build.X2[left] + b * m_Build.X2[right];
		return (m_Build.X[right] - m_Build.X[left]) / h +
			   h * ((1 - 3 * a * a) * m_Build.X2[left] + (3 * b * b - 1) * m_Build.X2[right]) / 6;
	}

private:
	CInterpolationMapView<TMap, double> * m_View;

	struct Build_s
	{
		double * T;
		double * X;
		double * X2;

		Build_s()
		: T(0)
		, X(0)
		, X2(0)
		{
		}
	} m_Build;

	bool m_Rebuild;

	void Free()
	{
		delete[] m_Build.X2;
		m_Build.X2 = 0;
		delete[] m_Build.X;
		m_Build.X = 0;
		delete[] m_Build.T;
		m_Build.T = 0;
	}
};

template<class TMap>
class CSLinearQuaternionInterpolation
: public CInterpolation<Quaternion>
{
public:
	/// <remarks>No extrapolation (will clamp)!</remarks>
	CSLinearQuaternionInterpolation(CInterpolationMapView<TMap, Quaternion> * view)
	: CInterpolation<Quaternion>()
	, m_View(view)
	{
		InterpolationMapChanged();
	}

	virtual void InterpolationMapChanged(void)
	{
		// nothing to do here.
	}

	virtual bool CanEval(void) const
	{
		return 2 <= m_View->GetSize();
	}

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	virtual Quaternion Eval(double t)
	{
		CInterpolationMapViewIterator<TMap, Quaternion> itLower;
		CInterpolationMapViewIterator<TMap, Quaternion> itUpper;
		m_View->GetNearestInterval(t, itLower, itUpper);

		double lowerT = itLower.GetTime();
		Quaternion lowerV = itLower.GetValue();

		if(t <= lowerT)
		{
			return lowerV;
		}

		double upperT = itUpper.GetTime();
		Quaternion upperV = itUpper.GetValue();

		if(upperT <= t)
		{
			return upperV;
		}

		// Make sure we will travel the short way:
		double dotProduct = DotProduct(upperV,lowerV);
		if(dotProduct<0.0)
		{
			upperV = -1.0 * upperV;
		}

		double deltaT = upperT -lowerT;
		
		return lowerV.Slerp(upperV, (t-lowerT)/deltaT);
	}

private:
	CInterpolationMapView<TMap, Quaternion> * m_View;
};

template<class TMap>
class CSCubicQuaternionInterpolation
: public CInterpolation<Quaternion>
{
public:
	// TODO: make thread safe.
	/// <remarks>Currently NOT threadsafe (TODO), because of use of temporary static global variables (slew3_*).</remarks>
	CSCubicQuaternionInterpolation(CInterpolationMapView<TMap, Quaternion> * view)
	: CInterpolation<Quaternion>()
	, m_View(view)
	{
		InterpolationMapChanged();
	}

	virtual ~CSCubicQuaternionInterpolation()
	{
		Free();
	}

	virtual void InterpolationMapChanged(void)
	{
		m_Rebuild = true;
	}

	virtual bool CanEval(void) const
	{
		return 4 <= m_View->GetSize();
	}

	/// <remarks>
	/// Must not be called if CanEval() returns false!<br />
	/// </remarks>
	virtual Quaternion Eval(double t)
	{
		int n = m_View->GetSize();

		if(n < 4) throw "CSCubicQuaternionInterpolation::Eval only allowed with at least 4 points.";

		if(m_Rebuild)
		{
			m_Rebuild = false;

			Free();

			m_Build.T = new double[n];
			m_Build.Q_y = new double[n][4];
			m_Build.Q_h = new double[n-1];
			m_Build.Q_dtheta = new double[n-1];
			m_Build.Q_e = new double[n-1][3];
			m_Build.Q_w = new double[n][3];

			{
				Quaternion QLast;

				int i = 0;
				CInterpolationMapViewIterator<TMap, Quaternion> itEnd = m_View->GetEnd();
				for(CInterpolationMapViewIterator<TMap, Quaternion> it = m_View->GetBegin(); it != itEnd; ++it)
				{
					m_Build.T[i] = it.GetTime();

					Quaternion Q = it.GetValue();
				
					// Make sure we will travel the short way:
					if(0<i)
					{
						// hasLast.
						double dotProduct = DotProduct(Q,QLast);
						if(dotProduct<0.0)
						{
							Q = -1.0 * Q;
						}
					}

					m_Build.Q_y[i][0] = Q.X;
					m_Build.Q_y[i][1] = Q.Y;
					m_Build.Q_y[i][2] = Q.Z;
					m_Build.Q_y[i][3] = Q.W;

					QLast = Q;
					i++;
				}
			}

			double wi[3] = {0.0,0.0,0.0};
			double wf[3] = {0.0,0.0,0.0};
			qspline_init(n, 2, AFX_MATH_EPS, wi, wf, m_Build.T, m_Build.Q_y, m_Build.Q_h, m_Build.Q_dtheta, m_Build.Q_e, m_Build.Q_w);
		}

		double Q[4],dum1[4],dum2[4];

		qspline_interp(n, t, m_Build.T, m_Build.Q_y, m_Build.Q_h, m_Build.Q_dtheta, m_Build.Q_e, m_Build.Q_w, Q, dum1, dum2);

		return Quaternion(Q[3], Q[0], Q[1], Q[2]);
	}

private:
	CInterpolationMapView<TMap, Quaternion> * m_View;

	struct Build_s
	{
		double * T;
		double (*Q_y)[4];
		double * Q_h;
		double * Q_dtheta;
		double (*Q_e)[3];
		double (*Q_w)[3];

		Build_s()
		: T(0)
		, Q_y(0)
		, Q_h(0)
		, Q_dtheta(0)
		, Q_e(0)
		, Q_w(0)
		{
		}
	} m_Build;

	bool m_Rebuild;

	void Free()
	{
		delete [] m_Build.Q_w;
		m_Build.Q_w = 0;
		delete [] m_Build.Q_e;
		m_Build.Q_e = 0;
		delete[] m_Build.Q_dtheta;
		m_Build.Q_dtheta = 0;
		delete[] m_Build.Q_h;
		m_Build.Q_h = 0;
		delete [] m_Build.Q_y;
		m_Build.Q_y = 0;
		delete[] m_Build.T;
		m_Build.T = 0;
	}
};

} // namespace Afx {
} // namespace Math {
