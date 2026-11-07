#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

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

// Returns false until an explicit trace/capture enables the verified observer. Other engines and
// the default configuration retain ordinary console seeking.
bool GetStatus(Status &status);
bool StartTrace(const wchar_t *file);
void StopTrace();
void UpdateTrace();
void OnNetUpdateEnd();
void PrintTraceStatus();

// Capture experiment only. It never writes captured state back into TF2.
bool BeginCheckpointCapture();
bool SaveCheckpointCapture(const wchar_t *file);
void EndCheckpointCapture();
void PrintCheckpointStatus();

struct EncodedEntity
{
	std::uint32_t Handle = 0;
	int ClassId = -1, Bits = 0;
	std::vector<unsigned char> Data;
	// Original server ticks. Receive proxies may also require client clock state.
	std::vector<int> PropertyTicks;
};

// Owned network encodings, not client object memory. Extra zero padding permits
// the native bit reader's aligned word loads without retaining engine buffers.
class EncodedEntities
{
public:
	static constexpr int MaxEntities = 2048, MaxProperties = 4096, MaxBytes = 16384;
	static constexpr int UnseenTick = (std::numeric_limits<int>::min)();
	explicit EncodedEntities(size_t budget = 64 * 1024 * 1024) : m_Budget(budget)
	{
	}
	const EncodedEntity *Get(int index) const
	{
		return index >= 0 && index < MaxEntities ? m_Entities[index].get() : nullptr;
	}
	size_t Bytes() const
	{
		return m_Bytes;
	}
	void Clear()
	{
		for (auto &entity : m_Entities)
			entity.reset();
		m_Bytes = 0;
	}
	bool Put(int index, std::uint32_t handle, int classId, int properties, const unsigned char *data,
	         int bits, const int *changed, int changedCount, int tick)
	{
		if (index < 0 || index >= MaxEntities || handle == 0xffffffffu || classId < 0 || properties < 0 ||
		    properties > MaxProperties || !data || bits < 1 || bits > MaxBytes * 8 || changedCount < 0 ||
		    changedCount > properties || (changedCount && !changed))
			return false;
		int previous = -1;
		for (int i = 0; i < changedCount; ++i)
		{
			if (changed[i] <= previous || changed[i] >= properties)
				return false;
			previous = changed[i];
		}
		const size_t used = (size_t(bits) + 7) / 8, padded = ((used + 3) & ~size_t(3)) + 8;
		auto &slot = m_Entities[index];
		const size_t oldBytes = slot ? slot->Data.size() + slot->PropertyTicks.size() * sizeof(int) : 0;
		const size_t newBytes = padded + size_t(properties) * sizeof(int);
		if (newBytes > m_Budget || m_Bytes - oldBytes > m_Budget - newBytes)
			return false;
		auto next = std::make_unique<EncodedEntity>();
		next->Handle = handle;
		next->ClassId = classId;
		next->Bits = bits;
		next->Data.assign(padded, 0);
		for (size_t i = 0; i < used; ++i)
			next->Data[i] = data[i];
		if (bits & 7)
			next->Data[used - 1] &= (1u << (bits & 7)) - 1;
		if (slot && slot->Handle == handle && slot->ClassId == classId &&
		    slot->PropertyTicks.size() == size_t(properties))
			next->PropertyTicks = slot->PropertyTicks;
		else
			next->PropertyTicks.assign(size_t(properties), UnseenTick);
		for (int i = 0; i < changedCount; ++i)
			next->PropertyTicks[changed[i]] = tick;
		m_Bytes = m_Bytes - oldBytes + newBytes;
		slot = std::move(next);
		return true;
	}

private:
	std::array<std::unique_ptr<EncodedEntity>, MaxEntities> m_Entities;
	size_t m_Budget, m_Bytes = 0;
};

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
