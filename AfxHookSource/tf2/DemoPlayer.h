#pragma once

#include <cstdint>

namespace AfxHookSource
{
namespace Tf2Demo
{

struct Status
{
	bool Native = false, Playing = false, Loading = false, Skipping = false;
	int Tick = 0, Target = -1, LastSeekTarget = -1;
	std::uint64_t Identity = 0, SeekSerial = 0;
};

// Returns false until a trace enables the verified observer. Other engines and
// the default configuration retain ordinary console seeking.
bool GetStatus(Status &status);
bool StartTrace(const wchar_t *file);
void StopTrace();
void UpdateTrace();
void OnNetUpdateEnd();
void PrintTraceStatus();

struct SeekRequest
{
	int Tick = 0, Mode = 0;
	double Scale = 0;
};

// The queue is independent of engine pointers so reload/seek ordering is testable.
// Only one request is dispatched at a time; a newer queued destination replaces
// the previous queued destination, without restarting the in-flight seek.
class SeekQueue
{
public:
	enum class Action
	{
		None,
		Dispatch,
		Apply,
		Cancel,
		Timeout
	};
	struct Result
	{
		Action Type = Action::None;
		SeekRequest Request;
	};
	bool IsActive() const
	{
		return m_Pending || m_HasQueued;
	}

	void Request(const SeekRequest &request, std::uint64_t now)
	{
		m_Queued = request;
		m_HasQueued = true;
		if (!m_Pending)
		{
			m_LastProgress = now;
			m_Waiting = false;
		}
	}

	Result Update(const Status &status, std::uint64_t now)
	{
		if (!m_Pending && !m_HasQueued)
			return {};
		if (m_Pending)
		{
			if (m_Start.Native &&
			    (!status.Native || status.Identity != m_Start.Identity ||
			     status.SeekSerial > m_Start.SeekSerial + 1 ||
			     (status.SeekSerial != m_Start.SeekSerial && status.LastSeekTarget != m_Active.Tick) ||
			     (status.Skipping && status.SeekSerial != m_Start.SeekSerial &&
			      status.Target != m_Active.Tick)))
				return End(Action::Cancel);
			if (status.Playing && !status.Loading && status.Tick != m_LastTick)
			{
				m_LastTick = status.Tick;
				m_LastProgress = now;
			}
			const bool arrived = m_Start.Tick <= m_Active.Tick
			                         ? status.Tick >= m_Active.Tick
			                         : status.Tick < m_Start.Tick && status.Tick >= m_Active.Tick - 1;
			const bool nativeDone = status.Native && status.SeekSerial == m_Start.SeekSerial + 1 &&
			                        !status.Skipping && !status.Loading;
			if (status.Playing && !status.Loading && !status.Skipping && arrived &&
			    (!m_Start.Native || nativeDone))
			{
				m_Pending = false;
				if (!m_HasQueued)
					return {Action::Apply, m_Active};
				m_LastProgress = now;
			}
			else
			{
				if (m_Start.Native && !status.Playing && !status.Loading &&
				    status.SeekSerial != m_Start.SeekSerial)
					return End(Action::Cancel);
				if (now - m_LastProgress > 120000)
					return End(Action::Timeout);
				return {};
			}
		}
		if (m_Waiting && (!status.Native || status.Identity != m_WaitIdentity))
			return End(Action::Cancel);
		if (status.Loading || (status.Native && status.Skipping))
		{
			m_Waiting = status.Native;
			m_WaitIdentity = status.Identity;
			if (status.Playing && status.Tick != m_LastTick)
			{
				m_LastTick = status.Tick;
				m_LastProgress = now;
			}
			if (now - m_LastProgress > 120000)
				return End(Action::Timeout);
			return {};
		}
		if (!status.Playing || status.Tick < 0)
			return End(Action::Cancel);
		m_Active = m_Queued;
		m_Waiting = false;
		m_HasQueued = false;
		m_Pending = true;
		m_Start = status;
		m_LastTick = status.Tick;
		m_LastProgress = now;
		return {Action::Dispatch, m_Active};
	}

private:
	Result End(Action action)
	{
		m_Pending = m_HasQueued = m_Waiting = false;
		return {action, {}};
	}
	bool m_Pending = false, m_HasQueued = false, m_Waiting = false;
	SeekRequest m_Active, m_Queued;
	Status m_Start;
	int m_LastTick = 0;
	std::uint64_t m_LastProgress = 0;
	std::uint64_t m_WaitIdentity = 0;
};

} // namespace Tf2Demo
} // namespace AfxHookSource
