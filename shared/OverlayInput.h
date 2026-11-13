#pragma once
#include <cstdint>
#include <set>
#include <vector>
namespace advancedfx
{
// Public Source InputEvent_t layout (InputSystemVersion001).
struct OverlayInputEvent
{
	int Type, Tick, Data, Data2, Data3;
};
class OverlayInputFilter
{
  public:
	void Capture(bool value, int poll)
	{
		if (value == m_Capture)
			return;
		m_Capture = value;
		m_CapturePoll = poll;
		Invalidate();
	}
	bool Capturing() const { return m_Capture; }
	void EatButton(int button)
	{
		if (button <= 0 || button >= 4096)
			return;
		m_Blocked.insert(button);
		m_ReleaseNext.erase(button);
		Invalidate();
	}
	void ResetButtons()
	{
		m_Blocked.clear();
		m_ReleaseNext.clear();
		Invalidate();
	}
	bool NeedsFilter(int poll)
	{
		Advance(poll);
		return m_Capture || m_CapturePoll == poll || !m_Blocked.empty();
	}
	const std::vector<OverlayInputEvent> &Filter(const OverlayInputEvent *events, int count, int poll)
	{
		Advance(poll);
		if (m_Ready && m_Source == events && m_Count == count)
			return m_Filtered;
		m_Source = events;
		m_Count = count;
		m_Ready = true;
		m_Filtered.clear();
		if (!events || count < 0 || count > 65536)
			return m_Filtered;
		const bool all = m_Capture || m_CapturePoll == poll;
		for (int i = 0; i < count; ++i)
		{
			const auto &e = events[i];
			// Releases from ResetInputState must reach Source to end pre-existing +binds.
			if (e.Type == 1)
			{
				m_Filtered.push_back(e);
				if (m_Blocked.count(e.Data))
					m_ReleaseNext.insert(e.Data);
				continue;
			}
			if (e.Type == 0 || e.Type == 2)
			{
				if (all || m_Blocked.count(e.Data))
					continue;
			}
			else if (all && !(e.Type >= 100 && e.Type < 1000))
				continue;
			m_Filtered.push_back(e);
		}
		return m_Filtered;
	}

  private:
	void Invalidate() { m_Ready = false; }
	void Advance(int poll)
	{
		if (poll == m_Poll)
			return;
		for (int b : m_ReleaseNext)
			m_Blocked.erase(b);
		m_ReleaseNext.clear();
		m_Poll = poll;
		Invalidate();
	}
	bool m_Capture = false, m_Ready = false;
	int m_Poll = -1, m_CapturePoll = -1, m_Count = 0;
	const OverlayInputEvent *m_Source = nullptr;
	std::set<int> m_Blocked, m_ReleaseNext;
	std::vector<OverlayInputEvent> m_Filtered;
};
} // namespace advancedfx
