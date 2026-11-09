#include "shared/CommandSystem.h"
#include "shared/DemoTimeline.h"
#include "shared/PlayerRepair.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <codecvt>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <stdexcept>

bool UTF8StringToWideString(const char *text, std::wstring &out)
{
	try
	{
		out = std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(text);
		return true;
	}
	catch (...)
	{
		return false;
	}
}
bool StringIBeginsWith(const char *a, const char *b)
{
	for (; *b; ++a, ++b)
		if (!*a ||
		    std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
			return false;
	return true;
}
namespace
{
int checks = 0;
void Check(bool value, const char *message)
{
	++checks;
	if (!value)
		throw std::runtime_error(message);
}
struct Executor : IExecuteClientCmdForCommandSystem
{
	std::vector<std::string> Values;
	std::function<void(const char *)> Callback;
	void ExecuteClientCmd(const char *value) override
	{
		Values.emplace_back(value);
		if (Callback)
			Callback(value);
	}
};
void Console(CommandSystem &system, std::initializer_list<const char *> parts)
{
	advancedfx::CFakeCommandArgs args("mirv_cmd");
	for (auto part : parts)
		args.AddArg(part);
	system.Console_Command(&args);
}
void Scheduler(const std::filesystem::path &dir)
{
	Executor executor;
	CommandSystem system(&executor, nullptr, nullptr);
	auto at = [&](int tick, bool seeking = false, bool paused = false) {
		system.UpdateTickMarkers(true, tick, seeking, paused);
		system.OnExecuteCommands();
	};
	auto second = system.AddTickMarker(20, "second", "second"),
	     first = system.AddTickMarker(10, "first", "first");
	auto equal = system.AddTickMarker(20, "equal", "equal");
	Check(first && second && equal && !system.TickMarkersEnabled(), "New tracks begin disarmed");
	at(0);
	at(25);
	Check(executor.Values.empty(), "Disarmed playback cannot execute markers");
	system.EnableTickMarkers(true);
	at(0);
	at(25);
	Check(executor.Values == std::vector<std::string>({"first", "second", "equal"}),
	      "Crossed commands run in tick order with stable ordering at equal ticks");
	at(25);
	at(25);
	Check(executor.Values.size() == 3, "Repeated renders at one tick do not repeat commands");
	system.BeginTickMarkerSeek();
	at(5, true);
	system.OnLevelInitPreEntity(true);
	at(5);
	Check(executor.Values.size() == 3 && system.TickMarkersEnabled(),
	      "Backward seek reconstruction retains arming without firing ordinary markers");
	at(10);
	Check(executor.Values.back() == "first" && executor.Values.size() == 4,
	      "Backward seeking rearms later markers");
	auto state = system.GetTickMarkers()[0];
	state.RunOnSeek = true;
	system.SetTickMarker(state);
	executor.Values.clear();
	system.BeginTickMarkerSeek();
	at(15, true);
	at(1000, true);
	Check(executor.Values.empty(), "Catch-up ticks never execute commands");
	system.EndTickMarkerSeek(25);
	at(25);
	Check(executor.Values == std::vector<std::string>({"first"}),
	      "Seek completion reapplies flagged settings once and skips ordinary crossed commands");
	at(25);
	Check(executor.Values.size() == 1, "A completed seek cannot replay its prefix on each render");
	system.CancelTickMarkerSeek();
	system.OnLevelInitPreEntity(false);
	at(0);
	at(25);
	Check(!system.TickMarkersEnabled() && executor.Values.size() == 1,
	      "A new playback session disarms the track");
	system.EnableTickMarkers(true);
	at(100);
	auto current = system.AddTickMarker(100, "current", "current");
	at(100);
	Check(executor.Values.size() == 1, "Adding a marker at the current tick never executes it");
	auto future = system.AddTickMarker(102, "future", "future");
	state = system.GetTickMarkers()[0];
	state.Name = "renamed";
	system.SetTickMarker(state);
	at(103);
	Check(executor.Values.back() == "future",
	      "Editing an unrelated marker cannot suppress a normal frame crossing");
	state = *std::find_if(system.GetTickMarkers().begin(), system.GetTickMarkers().end(),
	                      [&](const auto &m) { return m.Id == future; });
	state.Tick = 90;
	system.SetTickMarker(state);
	at(104);
	Check(executor.Values.size() == 2, "Moving a command into the past does not catch up its execution");
	system.EnableTickMarkers(false);
	Check(system.RunTickMarker(current),
	      "Run now is explicit and works while automatic execution is disabled");
	system.OnExecuteCommands();
	Check(executor.Values.back() == "current", "Run now dispatches the saved command text");
	executor.Values.clear();
	auto recursive = system.AddTickMarker(200, "recursive", "recursive");
	executor.Callback = [&](const char *value) {
		if (!std::strcmp(value, "recursive"))
		{
			system.RunTickMarker(recursive);
			system.OnExecuteCommands();
		}
	};
	system.RunTickMarker(recursive);
	system.OnExecuteCommands();
	Check(executor.Values.size() == 1,
	      "Self-scheduling and reentrant callbacks cannot stall one engine update");
	executor.Callback = {};
	system.OnExecuteCommands();
	Check(executor.Values.size() == 2, "Commands queued during execution are deferred to the next update");
	Check(!system.AddTickMarker(-1, "bad", "bad") && !system.AddTickMarker(1, "bad", std::string(16385, 'x')),
	      "Invalid ticks and oversized bodies reject");
	auto xml = dir / "commands.xml";
	auto special = system.AddTickMarker(300, "A & <B>", "echo \"a&b<z>\";\necho 100%");
	state = system.GetTickMarkers().back();
	state.Enabled = false;
	state.RunOnSeek = true;
	system.SetTickMarker(state);
	Check(system.SaveTickMarkers(xml.wstring().c_str()),
	      "Marker tracks save through the existing XML library");
	system.EnableTickMarkers(true);
	Check(system.LoadTickMarkers(xml.wstring().c_str()) && !system.TickMarkersEnabled(),
	      "Loading command files never arms execution");
	const auto &loaded = system.GetTickMarkers().back();
	Check(loaded.Id != special && loaded.Name == state.Name && loaded.Command == state.Command &&
	          !loaded.Enabled && loaded.RunOnSeek,
	      "XML preserves names, flags, quotes, percent signs and multiline commands with fresh stable IDs");
	auto size = system.GetTickMarkers().size();
	auto retained = system.GetTickMarkers()[0].Id;
	for (const auto &tick : {"-1", "nan", "1.5", "2147483648", ""})
	{
		std::ofstream(xml) << "<commandSystem><sequencer version=\"1\"><marker tick=\"" << tick
		                   << "\"><body>x</body></marker></sequencer></commandSystem>";
		system.EnableTickMarkers(true);
		Check(!system.LoadTickMarkers(xml.wstring().c_str()) && !system.TickMarkersEnabled() &&
		          system.GetTickMarkers().size() == size && system.GetTickMarkers()[0].Id == retained,
		      "Malformed imports are transactional and disarm execution");
	}
	Console(system, {"markers", "add", "420", "console", "echo console"});
	Check(system.GetTickMarkers().back().Tick == 420, "Console and overlay share marker storage");
	Console(system,
	        {"markers", "edit", std::to_string(system.GetTickMarkers().back().Id).c_str(), "runOnSeek", "1"});
	Check(system.GetTickMarkers().back().RunOnSeek, "Console exposes the seek policy");
	const size_t markerCount = system.GetTickMarkers().size();
	Console(system, {"save", xml.u8string().c_str()});
	Check(system.LoadTickMarkers(xml.wstring().c_str()), "Legacy scheduler save includes the marker track");
	system.ClearTickMarkers();
	Console(system, {"load", xml.u8string().c_str()});
	Check(system.GetTickMarkers().size() == markerCount && !system.TickMarkersEnabled(),
	      "Legacy scheduler load restores markers without arming");
	std::ofstream(xml) << "<commandSystem><commands/></commandSystem>";
	Console(system, {"load", xml.u8string().c_str()});
	Check(system.GetTickMarkers().empty(),
	      "Loading an older scheduler file clears the previous marker track");

	system.UpdateTickMarkers(false, 0, false, false);
	Check(!system.TickMarkersEnabled(), "Leaving demo playback disarms automatic commands");
	CommandSystem budget(&executor, nullptr, nullptr);
	for (int i = 0; i < 64; ++i)
		Check(budget.AddTickMarker(i, "", std::string(16384, '&')) != 0,
		      "Track accepts its documented text budget");
	Check(!budget.AddTickMarker(65, "", "x"), "Total marker text has a bounded size");
	Check(budget.SaveTickMarkers(xml.wstring().c_str()) && budget.LoadTickMarkers(xml.wstring().c_str()),
	      "Maximum-sized escaped tracks can be loaded again");
}

void Repair(const std::filesystem::path &dir)
{
	using Repair = advancedfx::PlayerRepair;
	Repair model;
	model.Demo = "demo identity";
	model.Player = "A & B";
	model.UserId = 42;
	for (int tick : {0, 10, 15, 20, 30})
	{
		Repair::Sample sample;
		sample.Mask = Repair::All;
		sample.Value = {double(tick), 2.0 * tick, 0, tick == 15 ? 90.0 : 0.0, tick < 15 ? 179.0 : -179.0};
		Check(model.Observe(tick, sample), "Original samples captured");
	}
	Repair::Range range;
	range.First = range.Last = 15;
	range.Mask = Repair::All;
	Check(model.Exclude(range), "A single bad pose can be marked");
	std::array<double, 5> value{};
	Check(model.Evaluate(15, value) == Repair::All && std::abs(value[0] - 15) < 1e-8 && value[3] == 0 &&
	          std::abs(value[4] - 180) < 1e-8,
	      "Reconstruction bridges positions and wrapped angles without a revolution");
	Check(!model.Evaluate(14, value), "Unmarked ticks are untouched");
	Check(!model.Exclude(range), "Overlapping channel exclusions reject");
	Check(model.ToggleCut(16) && !model.Evaluate(15, value),
	      "A manual cut blocks reconstruction across a real discontinuity");
	Check(model.Undo(false) && model.Evaluate(15, value), "Cut removal undoes atomically");
	Check(model.Undo(true) && !model.Evaluate(15, value), "Manual boundaries redo");
	model.Undo(false);
	auto edited = model.Samples().at(20);
	edited.Epoch = 1;
	model.Observe(20, edited);
	Check(!model.Evaluate(15, value), "Life-state boundaries cannot be interpolated away");
	edited.Epoch = 0;
	model.Observe(20, edited);
	model.MaxBridgeTicks = 5;
	Check(!model.Evaluate(15, value), "Large untrusted gaps are rejected");
	model.MaxBridgeTicks = 330;
	auto xml = dir / "repair.xml";
	Check(model.Save(xml.wstring().c_str()), "Repair sidecar saves original samples and marks");
	Repair loaded;
	Check(loaded.Load(xml.wstring().c_str()) && loaded.UserId == 42 && loaded.Player == "A & B" &&
	          loaded.Evaluate(15, value) == Repair::All,
	      "Repair sidecar round trips without executing commands");
	std::ofstream(xml) << "<playerRepair version=\"1\" user=\"42\" maxBridge=\"330\" demo=\"demo\"><sample "
	                      "tick=\"0\" mask=\"31\" epoch=\"0\" v0=\"nan\"/></playerRepair>";
	Check(!loaded.Load(xml.wstring().c_str()) && loaded.Samples().size() == 5,
	      "Malformed sidecars leave existing work intact");
	Check(model.RemoveRange(0) && !model.Evaluate(15, value) && model.Undo(false) &&
	          model.Evaluate(15, value),
	      "Exclusion removal participates in undo");
	Repair edge;
	edge.Observe(0, model.Samples().at(0));
	range.First = 0;
	range.Last = 10;
	Check(edge.Exclude(range) && !edge.Evaluate(5, value), "Missing anchors do not extrapolate");
	Repair smooth;
	for (int t : {0, 10, 20, 30})
	{
		Repair::Sample x;
		x.Mask = Repair::All;
		x.Value = {t < 20 ? double(t) : 20, 0, 0, 0, 0};
		smooth.Observe(t, x);
	}
	range.First = 11;
	range.Last = 19;
	range.Mask = Repair::Position;
	smooth.Exclude(range);
	for (int t = 11; t < 20; ++t)
	{
		Check(smooth.Evaluate(t, value) == Repair::Position && value[0] >= 10 && value[0] <= 20,
		      "Smooth reconstruction preserves its anchor bounds");
	}
}

struct Writer
{
	std::vector<unsigned char> Data;
	size_t Position = 0;
	void U(uint32_t value, unsigned count)
	{
		for (unsigned i = 0; i < count; ++i, ++Position)
		{
			if (Position / 8 >= Data.size())
				Data.push_back(0);
			Data[Position / 8] |= ((value >> i) & 1) << (Position % 8);
		}
	}
	void Bytes(const std::vector<unsigned char> &v)
	{
		for (auto c : v)
			U(c, 8);
	}
	void String(const std::string &s)
	{
		for (unsigned char c : s)
			U(c, 8);
		U(0, 8);
	}
	void Var(uint32_t value)
	{
		do
		{
			unsigned byte = value & 127;
			value >>= 7;
			U(byte | (value ? 128 : 0), 8);
		} while (value);
	}
};
void LE(std::vector<unsigned char> &data, uint32_t value)
{
	for (int i = 0; i < 4; ++i)
		data.push_back((value >> (8 * i)) & 255);
}
std::vector<unsigned char> User(const std::string &name, unsigned id)
{
	std::vector<unsigned char> data(32, 0);
	std::memcpy(data.data(), name.data(), (std::min)(size_t(31), name.size()));
	LE(data, id);
	return data;
}
void Packet(std::vector<unsigned char> &demo, unsigned type, int tick,
            const std::vector<unsigned char> &payload)
{
	demo.push_back(type);
	LE(demo, tick);
	if (type == 1 || type == 2)
		demo.resize(demo.size() + 84, 0);
	LE(demo, uint32_t(payload.size()));
	demo.insert(demo.end(), payload.begin(), payload.end());
}
std::vector<unsigned char> Demo(unsigned compression = 0)
{
	std::vector<unsigned char> data(1072, 0);
	std::memcpy(data.data(), "HL2DEMO", 7);
	data[8] = 3;
	data[12] = 24;
	std::memcpy(data.data() + 276, "Recorder", 8);
	std::memcpy(data.data() + 536, "ctf_test", 8);
	data[1060] = 100;
	Writer definitions;
	auto definition = [&](unsigned id, const char *name,
	                      std::initializer_list<std::pair<const char *, unsigned>> fields) {
		definitions.U(id, 9);
		definitions.String(name);
		for (auto field : fields)
		{
			definitions.U(field.second, 3);
			definitions.String(field.first);
		}
		definitions.U(0, 3);
	};
	definition(1, "player_death", {{"userid", 4}, {"attacker", 4}, {"assister", 4}, {"weapon", 1}});
	definition(2, "teamplay_round_start", {});
	definition(3, "teamplay_flag_event", {{"player", 4}, {"eventtype", 5}});
	definition(4, "player_changename", {{"userid", 4}, {"newname", 1}});
	Writer signon;
	signon.U(30, 6);
	signon.U(4, 9);
	signon.U(unsigned(definitions.Position), 20);
	for (size_t i = 0; i < definitions.Position; ++i)
		signon.U((definitions.Data[i / 8] >> (i % 8)) & 1, 1);
	if (compression)
	{
		Writer entries;
		unsigned id = 0;
		for (const char *name : {"Recorder", "Soldier", "Medic"})
		{
			entries.U(1, 1);
			entries.U(1, 1);
			entries.U(0, 1);
			entries.String(std::to_string(id));
			entries.U(1, 1);
			entries.U(36, 14);
			entries.Bytes(User(name, ++id));
		}
		std::vector<unsigned char> encoded;
		if (compression == 1)
		{
			Writer snappy;
			snappy.Var(uint32_t(entries.Data.size()));
			snappy.U(60 << 2, 8);
			snappy.U(uint32_t(entries.Data.size() - 1), 8);
			snappy.Bytes(entries.Data);
			encoded = snappy.Data;
		}
		else
		{
			LE(encoded, uint32_t(entries.Data.size()));
			for (size_t i = 0; i < entries.Data.size(); i += 8)
			{
				encoded.push_back(0);
				encoded.insert(encoded.end(), entries.Data.begin() + i,
				               entries.Data.begin() + (std::min)(entries.Data.size(), i + 8));
			}
		}
		Writer block;
		block.U(uint32_t(entries.Data.size()), 32);
		block.U(uint32_t(encoded.size() + 4), 32);
		for (auto c : std::string(compression == 1 ? "SNAP" : "LZSS"))
			block.U(c, 8);
		block.Bytes(encoded);
		signon.U(12, 6);
		signon.String("userinfo");
		signon.U(4, 16);
		signon.U(3, 3);
		signon.Var(uint32_t(block.Position));
		signon.U(0, 1);
		signon.U(1, 1);
		signon.Bytes(block.Data);
	}
	Packet(data, 1, -1, signon.Data);
	if (!compression)
	{
		Writer table;
		table.U(1, 8);
		table.String("userinfo");
		table.U(3, 16);
		unsigned id = 0;
		for (const char *name : {"Recorder", "Soldier", "Medic"})
		{
			table.String(std::to_string(id));
			table.U(1, 1);
			table.U(36, 16);
			table.Bytes(User(name, ++id));
		}
		table.U(0, 1);
		Packet(data, 8, 0, table.Data);
	}
	auto event = [](Writer &packet, Writer &body) {
		packet.U(25, 6);
		packet.U(unsigned(body.Position), 11);
		for (size_t i = 0; i < body.Position; ++i)
			packet.U((body.Data[i / 8] >> (i % 8)) & 1, 1);
	};
	Writer packet, death;
	death.U(1, 9);
	death.U(1, 16);
	death.U(2, 16);
	death.U(3, 16);
	death.String("rocket & 100%");
	event(packet, death);
	Writer objective;
	objective.U(3, 9);
	objective.U(2, 16);
	objective.U(1, 8);
	event(packet, objective);
	packet.U(4, 6);
	packet.String("quit;echo never execute demo strings");
	Packet(data, 2, 10, packet.Data);
	Writer later, round;
	round.U(2, 9);
	event(later, round);
	Packet(data, 2, 20, later.Data);
	Writer changed, name;
	name.U(4, 9);
	name.U(2, 16);
	name.String("New name");
	event(changed, name);
	event(changed, death);
	Packet(data, 2, 30, changed.Data);
	data.push_back(7);
	data.push_back(100);
	data.push_back(0);
	data.push_back(0);
	return data;
}
void Indexing(const std::filesystem::path &dir)
{
	std::atomic_bool cancel{false};
	std::atomic<float> progress{0};
	auto file = dir / "events.dem";
	auto write = [&](const auto &bytes) {
		std::ofstream out(file, std::ios::binary);
		out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	};
	for (unsigned compression : {0u, 1u, 2u})
	{
		write(Demo(compression));
		auto index = advancedfx::DemoTimeline::Read(file, cancel, progress);
		Check(index.Error.empty() && index.Events.size() == 5 && progress.load() == 1,
		      "Whole-demo indexing supports snapshots, Snappy and LZSS user information");
		Check(index.Events[0].Tick == 10 && index.Events[1].Tick == 10 && index.Events[2].Tick == 20 &&
		          index.Events.back().Tick == 30,
		      "All events at a tick retain their ordering and later events are indexed before playback");
		Check(index.Events[0].RecorderDeath && index.Events[0].Fields.at("attacker_name") == "Soldier" &&
		          index.Events[0].Fields.at("assister_name") == "Medic",
		      "Death details resolve victim, attacker and assister and identify the recorder");
		Check(index.Events.back().Fields.at("attacker_name") == "New name" &&
		          index.Events[0].Fields.at("attacker_name") == "Soldier",
		      "Renaming a player preserves historical event names");
		Check(index.Events[0].Search.find("soldier") != std::string::npos &&
		          index.Events[0].Summary.find("100%") != std::string::npos,
		      "Search text is indexed while summaries preserve punctuation");
	}
	auto bytes = Demo();
	bytes.resize(bytes.size() - 8);
	write(bytes);
	Check(!advancedfx::DemoTimeline::Read(file, cancel, progress).Error.empty(),
	      "Truncated demos cannot be presented as complete indexes");
	bytes = Demo();
	bytes[8] = 4;
	write(bytes);
	Check(!advancedfx::DemoTimeline::Read(file, cancel, progress).Error.empty(),
	      "Unsupported demo protocols fail explicitly");
	write(Demo());
	cancel = true;
	Check(advancedfx::DemoTimeline::Read(file, cancel, progress).Events.empty(),
	      "Cancelled indexing never publishes partial events");
}
} // namespace
int main(int argc, char **argv)
{
	auto dir =
	    std::filesystem::temp_directory_path() /
	    ("hlae-sequencer-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	try
	{
		std::filesystem::create_directories(dir);
		Scheduler(dir);
        Check(advancedfx::DemoTimeline::SameMap("maps/CP_Gullywash.bsp","cp_gullywash"),"Map identities normalize the engine prefix and BSP extension");
        Check(advancedfx::DemoTimeline::SameMap("C:\\tf\\maps\\workshop\\cp_process.ugc123.BSP","workshop/cp_process.ugc123"),"Workshop map identities retain their UGC suffix");
        Check(!advancedfx::DemoTimeline::SameMap("maps/workshop/cp_process.ugc123.bsp","workshop/cp_process.ugc456"),"Different workshop versions cannot match");
		Indexing(dir);
		Repair(dir);
		for (int i = 1; i < argc; ++i)
		{
			std::atomic_bool cancel{false};
			std::atomic<float> progress{0};
			auto result = advancedfx::DemoTimeline::Read(argv[i], cancel, progress);
			Check(result.Error.empty(), result.Error.c_str());
			std::cout << argv[i] << ": " << result.Events.size() << " events\n";
		}
		std::filesystem::remove_all(dir);
		std::cout << checks << " sequencer checks passed\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::filesystem::remove_all(dir);
		std::cerr << "After " << checks << ": " << e.what() << "\n";
		return 1;
	}
}
