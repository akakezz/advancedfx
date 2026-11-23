#pragma once

#include <cmath>
#include <limits>

namespace advancedfx {

// Optional, bounded experiment: supply a monotonic rendering time based on the
// public demo tick and interpolation fraction, maintaining the engine's
// initial server-time offset. This never runs during network updates.
// Calling code must restore the original CGlobalVars::curtime at render end.
class DemoRenderClockStabilizer
{
public:
	void Reset()
	{
		m_HaveAnchor = false;
		m_LastDemoTime = 0;
		m_Offset = 0;
	}
	bool HasAnchor() const { return m_HaveAnchor; }
	double Offset() const { return m_Offset; }

	// Returns true when a correction should be applied. False means keep the
	// engine's time unchanged; an initial or discontinuous sample anchors only.
	bool Evaluate(double demoTime, double rawCurtime, double tickInterval, double &target)
	{
		if (!std::isfinite(demoTime) || !std::isfinite(rawCurtime) ||
		    !std::isfinite(tickInterval) || tickInterval <= 0 || tickInterval > 1)
		{
			Reset();
			return false;
		}
		if (!m_HaveAnchor)
		{
			m_Offset = rawCurtime - demoTime;
			m_LastDemoTime = demoTime;
			m_HaveAnchor = true;
			return false;
		}
		double delta = demoTime - m_LastDemoTime;
		if (delta < -0.00001 || delta > 0.25)
		{
			Reset();
			m_Offset = rawCurtime - demoTime;
			m_LastDemoTime = demoTime;
			m_HaveAnchor = true;
			return false;
		}
		m_LastDemoTime = demoTime;
		double predicted = demoTime + m_Offset;
		// Intentional timebase switches are not jitter. Do not repair changes
		// beyond the observed 1-2 tick offset or any large unexpected shift.
		if (!std::isfinite(predicted) || std::abs(predicted - rawCurtime) > 2.5 * tickInterval)
		{
			Reset();
			m_Offset = rawCurtime - demoTime;
			m_LastDemoTime = demoTime;
			m_HaveAnchor = true;
			return false;
		}
		target = predicted;
		return std::abs(predicted - rawCurtime) > 0.0001;
	}

private:
	bool m_HaveAnchor = false;
	double m_Offset = 0;
	double m_LastDemoTime = 0;
};

} // namespace advancedfx
