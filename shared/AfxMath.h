#pragma once

#include <cmath>

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

#include <map>

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

	CInterpolationMapViewIterator(typename CInterpolationMap<TMap>::const_iterator const & mapIterator, T (* selector)(TMap const & value))
	: m_MapIterator(mapIterator)
	, m_Selector(selector)
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
		return 1 <= m_View->GetSize();
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

// Local Hermite and step interpolation reuse the existing map/view interface.
// Smooth and monotone modes need two keys, without a global spline rebuild.
template <class TMap> class CLocalDoubleInterpolation : public CInterpolation<double>
{
public:
	enum Mode
	{
		Smooth,
		Monotone,
		Step
	};
	CLocalDoubleInterpolation(CInterpolationMapView<TMap, double> * view, Mode mode) : m_View(view), m_Mode(mode)
	{
	}
	void InterpolationMapChanged() override
	{
	}
	bool CanEval() const override
	{
		return m_View->GetSize() >= (m_Mode == Step ? 1u : 2u);
	}
	double Eval(double time) override
	{
		CInterpolationMapViewIterator<TMap, double> a, b;
		m_View->GetNearestInterval(time, a, b);
		double t0 = a.GetTime(), t1 = b.GetTime(), v0 = a.GetValue(), v1 = b.GetValue();
		if (time <= t0)
			return v0;
		if (time >= t1)
			return v1;
		if (m_Mode == Step)
			return v0;
		double h = t1 - t0, d = (v1 - v0) / h, m0 = d, m1 = d;
		if (a != m_View->GetBegin())
		{
			auto prev = a;
			--prev;
			double hp = t0 - prev.GetTime(), dp = (v0 - prev.GetValue()) / hp;
			m0 = m_Mode == Smooth ? (v1 - prev.GetValue()) / (h + hp) : Slope(dp, d, hp, h);
		}
		auto next = b;
		++next;
		if (next != m_View->GetEnd())
		{
			double hn = next.GetTime() - t1, dn = (next.GetValue() - v1) / hn;
			m1 = m_Mode == Smooth ? (next.GetValue() - v0) / (h + hn) : Slope(d, dn, h, hn);
		}
		double u = (time - t0) / h, u2 = u * u, u3 = u2 * u;
		return (2 * u3 - 3 * u2 + 1) * v0 + (u3 - 2 * u2 + u) * h * m0 + (-2 * u3 + 3 * u2) * v1 + (u3 - u2) * h * m1;
	}

private:
	static double Slope(double a, double b, double ha, double hb)
	{
		if (a * b <= 0)
			return 0;
		double w0 = 2 * hb + ha, w1 = hb + 2 * ha;
		return (w0 + w1) / (w0 / a + w1 / b);
	}
	CInterpolationMapView<TMap, double> * m_View;
	Mode m_Mode;
};
template <class TMap> class CLocalQuaternionInterpolation : public CInterpolation<Quaternion>
{
public:
	CLocalQuaternionInterpolation(CInterpolationMapView<TMap, Quaternion> * view, bool step)
		: m_View(view), m_Step(step)
	{
	}
	void InterpolationMapChanged() override
	{
	}
	bool CanEval() const override
	{
		return m_View->GetSize() >= (m_Step ? 1u : 2u);
	}
	Quaternion Eval(double time) override
	{
		CInterpolationMapViewIterator<TMap, Quaternion> a, b;
		m_View->GetNearestInterval(time, a, b);
		if (time <= a.GetTime())
			return a.GetValue();
		if (time >= b.GetTime())
			return b.GetValue();
		if (m_Step)
			return a.GetValue();
		double span = b.GetTime() - a.GetTime(), u = (time - a.GetTime()) / span;
		Quaternion q = a.GetValue().Normalized(), r = b.GetValue().Normalized();
		Vector3 velocityA = Log(q, r) * (1 / span), velocityB = Log(r, q) * (-1 / span);
		if (a != m_View->GetBegin())
		{
			auto previous = a;
			--previous;
			velocityA = (Log(q, r) - Log(q, previous.GetValue())) * (1 / (b.GetTime() - previous.GetTime()));
		}
		auto next = b;
		++next;
		if (next != m_View->GetEnd())
			velocityB = (Log(r, next.GetValue()) - Log(r, q)) * (1 / (next.GetTime() - a.GetTime()));
		Quaternion c0 = q * Exp(velocityA * (span / 3)), c1 = r * Exp(velocityB * (-span / 3));
		Quaternion p = Blend(q, c0, u), m = Blend(c0, c1, u), n = Blend(c1, r, u);
		return Blend(Blend(p, m, u), Blend(m, n, u), u);
	}

private:
	static Vector3 Log(Quaternion q, Quaternion r)
	{
		q = q.Normalized();
		r = r.Normalized();
		if (DotProduct(q, r) < 0)
			r = -1.0 * r;
		Vector3 axis;
		double angle = q.GetAng(r, axis);
		return axis * (angle / 2);
	}
	static Quaternion Exp(const Vector3 & v)
	{
		double length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
		double scale = length > 1e-12 ? std::sin(length) / length : 1;
		return Quaternion(std::cos(length), v.X * scale, v.Y * scale, v.Z * scale);
	}
	static Quaternion Blend(Quaternion q, Quaternion r, double u)
	{
		if (DotProduct(q, r) < 0)
			r = -1.0 * r;
		return q.Slerp(r, u).Normalized();
	}
	CInterpolationMapView<TMap, Quaternion> * m_View;
	bool m_Step;
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
