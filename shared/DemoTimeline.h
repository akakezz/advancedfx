#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace advancedfx
{
namespace DemoTimeline
{
enum class Category
{
	Death,
	Round,
	Objective,
	Player
};
struct Event
{
	int Tick = 0;
	Category Kind = Category::Player;
	bool RecorderDeath = false;
	std::string Name, Summary, Search;
	std::map<std::string, std::string> Fields;
};
struct Index
{
	std::filesystem::path File;
	std::string Map, Recorder, Error;
	int TotalTicks = 0;
	double Interval = 1.0 / 66;
	std::vector<Event> Events;
};
// Compare Source map identities, preserving workshop .ugc suffixes.
bool SameMap(const std::string &left, const std::string &right);
// Read-only, engine-independent indexing. Safe to run on a worker thread.
// Failure leaves Error populated; partial results must not be presented as complete.
Index Read(const std::filesystem::path &file, const std::atomic_bool &cancel, std::atomic<float> &progress);
} // namespace DemoTimeline
} // namespace advancedfx
