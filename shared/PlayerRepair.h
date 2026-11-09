#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace advancedfx
{
// A selected player's original network poses, with non-destructive manual exclusions.
// Ticks are demo ticks; angles are eye pitch/yaw. This does not reconstruct bones.
class PlayerRepair
{
public:
	enum
	{
		Position = 7,
		Angles = 24,
		All = 31
	};
	struct Sample
	{
		std::array<double, 5> Value{};
		unsigned Mask = 0, Epoch = 0;
	};
	struct Range
	{
		int First = 0, Last = 0;
		unsigned Mask = All;
		bool Smooth = true;
	};
	struct Edits
	{
		std::vector<Range> Ranges;
		std::set<int> Cuts;
	};
	std::string Demo, Player;
	int UserId = 0;
	int MaxBridgeTicks = 330;
	void Reset();
	bool Observe(int tick, const Sample &sample);
	void ClearSamples()
	{
		m_Samples.clear();
		++m_Revision;
	}
	uint64_t Revision() const
	{
		return m_Revision;
	}
	const std::map<int, Sample> &Samples() const
	{
		return m_Samples;
	}
	const Edits &GetEdits() const
	{
		return m_Edits;
	}
	bool Exclude(const Range &range);
	bool RemoveRange(size_t index);
	bool ToggleCut(int tick);
	bool Undo(bool redo);
	// Returns corrected channel bits. A missing/unsafe bridge leaves that channel untouched.
	unsigned Evaluate(int tick, std::array<double, 5> &out, std::string *reason = nullptr) const;
	bool Save(const wchar_t *file) const;
	bool Load(const wchar_t *file);

private:
	std::map<int, Sample> m_Samples;
	Edits m_Edits;
	uint64_t m_Revision = 0;
	std::vector<Edits> m_Undo, m_Redo;
	void Remember();
	bool Excluded(int tick, unsigned mask) const;
};
} // namespace advancedfx
