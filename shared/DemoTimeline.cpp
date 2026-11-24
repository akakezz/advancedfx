#include "DemoTimeline.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace advancedfx
{
namespace DemoTimeline
{
namespace
{
// Source demo protocol 3, network protocols 15..24. Layout references:
// Valve's public demoformat.h and demostf/parser (see doc/CINEMATIC_CAMERA.md).
class Bits
{
	const std::vector<unsigned char> *m_Data;
	size_t m_Pos = 0, m_End;
	std::string *m_Error;

public:
	explicit Bits(const std::vector<unsigned char> &data, std::string &error) : m_Data(&data), m_End(data.size() * 8), m_Error(&error)
	{
	}
	bool Failed() const { return !m_Error->empty(); }
	std::string &Error() const { return *m_Error; }
	void Fail(const char *message) { if (!Failed()) *m_Error = message; }
    size_t Position() const { return m_Pos; }
	size_t Left() const
	{
		return Failed() ? 0 : m_End - m_Pos;
	}
	uint32_t U(unsigned count)
	{
		if (Failed()) return 0;
		if (count > 32 || count > Left())
		{ Fail("Truncated network message"); return 0; }
		uint32_t value = 0;
		for (unsigned i = 0; i < count; ++i, ++m_Pos)
			value |= uint32_t(((*m_Data)[m_Pos / 8] >> (m_Pos % 8)) & 1) << i;
		return value;
	}
	void Skip(size_t count)
	{
		if (Failed()) return;
		if (count > Left())
		{ Fail("Truncated message payload"); return; }
		m_Pos += count;
	}
	Bits Part(size_t count)
	{
		Bits out = *this;
		Skip(count);
		out.m_End = m_Pos;
		return out;
	}
	uint32_t Var()
	{
		uint32_t value = 0;
		for (unsigned shift = 0; shift < 35; shift += 7)
		{
			unsigned byte = U(8);
			if (Failed()) return 0;
			if (shift == 28 && (byte & 0xf0))
				{ Fail("Invalid variable-length integer"); return 0; }
			value |= (byte & 127) << shift;
			if (!(byte & 128))
				return value;
		}
		{ Fail("Invalid variable-length integer"); return 0; }
	}
	std::string String(size_t limit = 4096)
	{
		std::string out;
		while (out.size() <= limit)
		{
			auto c = U(8);
			if (Failed()) return {};
			if (!c)
				return out;
			out.push_back(char(c));
		}
		{ Fail("Oversized demo string"); return {}; }
	}
	std::vector<unsigned char> Bytes(size_t count)
	{
		if (Failed()) return {};
		if (count > Left() / 8)
		{ Fail("Truncated byte payload"); return {}; }
		std::vector<unsigned char> out(count);
		for (auto &b : out)
			b = static_cast<unsigned char>(U(8));
		return out;
	}
};

unsigned Log2(unsigned n)
{
	unsigned bits = 0;
	while (n >>= 1)
		++bits;
	return bits;
}
uint32_t LE(const unsigned char *p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
std::vector<unsigned char> Decompress(Bits data)
{
	const size_t expected = data.U(32), size = data.U(32);
	if (data.Failed()) return {};
	if (expected > 16 * 1024 * 1024 || size < 4 || size > data.Left() / 8)
	{ data.Fail("Invalid compressed string table size"); return {}; }
	auto bytes = data.Bytes(size);
	std::vector<unsigned char> out;
	out.reserve(expected);
	auto copy = [&](size_t offset, size_t count) {
		if (!offset || offset > out.size() || count > expected - out.size())
			{ data.Fail("Invalid compressed string table reference"); return; }
		while (count--)
			out.push_back(out[out.size() - offset]);
	};
	if (!std::memcmp(bytes.data(), "SNAP", 4))
	{
		std::vector<unsigned char> raw(bytes.begin() + 4, bytes.end());
		Bits input(raw, data.Error());
		if (input.Var() != expected)
			{ data.Fail("Snappy size mismatch"); return {}; }
		while (!data.Failed() && out.size() < expected)
		{
			unsigned tag = input.U(8), type = tag & 3;
			if (type == 0)
			{
				size_t n = tag >> 2;
				if (n >= 60)
					n = input.U(unsigned(n - 59) * 8);
				if (data.Failed() || n >= expected - out.size())
				{ data.Fail("Oversized Snappy literal"); return {}; }
				++n;
				auto literal = input.Bytes(n);
				out.insert(out.end(), literal.begin(), literal.end());
			}
			else if (type == 1)
			{
				unsigned next = input.U(8);
				copy(((tag & 224) << 3) | next, 4 + ((tag >> 2) & 7));
			}
			else
			{
				unsigned offset = input.U(type == 2 ? 16 : 32);
				copy(offset, 1 + (tag >> 2));
			}
		}
	}
	else if (!std::memcmp(bytes.data(), "LZSS", 4))
	{
		if (bytes.size() < 8 || LE(bytes.data() + 4) != expected)
			{ data.Fail("LZSS size mismatch"); return {}; }
		size_t p = 8;
		while (!data.Failed() && out.size() < expected)
		{
			if (p >= bytes.size())
				{ data.Fail("Truncated LZSS table"); return {}; }
			unsigned flags = bytes[p++];
			for (int bit = 0; bit < 8 && !data.Failed() && out.size() < expected; ++bit)
			{
				if (p >= bytes.size())
					{ data.Fail("Truncated LZSS token"); return {}; }
				if (!(flags & (1 << bit)))
					out.push_back(bytes[p++]);
				else
				{
					if (p + 2 > bytes.size())
						{ data.Fail("Truncated LZSS reference"); return {}; }
					unsigned a = bytes[p++], b = bytes[p++], n = (b & 15) + 1;
					if (n == 1)
						{ data.Fail("Early LZSS terminator"); return {}; }
					copy(((a << 4) | (b >> 4)) + 1, n);
				}
			}
		}
	}
	else
		{ data.Fail("Unsupported string table compression"); return {}; }
	return data.Failed() ? std::vector<unsigned char>{} : std::move(out);
}

struct Definition
{
	std::string Name;
	std::vector<std::pair<std::string, unsigned>> Fields;
};
struct Table
{
	std::string Name;
	unsigned Maximum = 0;
	int FixedBits = -1;
};
struct Reader
{
	Index Result;
	unsigned Protocol = 0;
    int LastCommand = -1, LastTick = -1, LastMessage = -1;
    size_t LastMessageBit = 0;
    std::streamoff LastOffset = -1;
	std::map<unsigned, Definition> Definitions;
	std::vector<Table> Tables;
	std::map<int, std::string> Players;
	int RecorderId = -1;
	size_t StoredBytes = 0;
	bool Failed() const { return !Result.Error.empty(); }
	void Fail(std::string error) { if (!Failed()) Result.Error = std::move(error); }
	void User(const std::vector<unsigned char> &data)
	{
		if (Failed() || data.size() < 36)
			return;
		int id = int(LE(data.data() + 32));
		std::string name(reinterpret_cast<const char *>(data.data()), 32);
		name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
		if (id > 0 && !name.empty())
		{
			if (Players.size() >= 65536 && !Players.count(id))
				{ Fail("Too many demo player identities"); return; }
			Players[id] = name;
			if (name == Result.Recorder)
				RecorderId = id;
		}
	}
	std::string Player(const std::string &id)
	{
		int value = std::atoi(id.c_str());
		auto found = Players.find(value);
		return found == Players.end() ? (value == 0 ? "World" : "User #" + id) : found->second;
	}
	void Entries(Bits data, const Table &table, unsigned count)
	{
		if (count > table.Maximum || count > 65536)
			{ Fail("Invalid string table count"); return; }
		std::vector<std::string> history;
		int index = -1;
		for (unsigned i = 0; !Failed() && i < count; ++i)
		{
			index = data.U(1) ? index + 1 : int(data.U(Log2(table.Maximum)));
			if (index < 0 || unsigned(index) >= table.Maximum)
				{ Fail("Invalid string table index"); return; }
			std::string text;
			if (data.U(1))
			{
				if (data.U(1))
				{
					unsigned h = data.U(5), length = data.U(5);
					if (h >= history.size() || length > history[h].size())
						{ Fail("Invalid string table history"); return; }
					text = history[h].substr(0, length) + data.String();
				}
				else
					text = data.String();
			}
			if (data.U(1))
			{
				size_t bits = table.FixedBits >= 0 ? size_t(table.FixedBits) : size_t(data.U(14)) * 8;
				auto payload = data.Part(bits);
				User(payload.Bytes(payload.Left() / 8));
			}
			if (history.size() == 32)
				history.erase(history.begin());
			history.push_back(text);
		}
	}
	void Snapshot(const std::vector<unsigned char> &payload, bool openFortress)
	{
		// Source 2013 can write a full 512 KiB string-table buffer even when
		// the serialized tables overflow it. The event index needs userinfo,
		// not the later model/sound/scene tables. Keep errors local until we
		// know whether the complete userinfo table was recovered.
		std::string error;
		Bits data(payload, error);
		bool completeUserInfo = false;
		unsigned count = data.U(8);
		for (unsigned i = 0; !Failed() && !data.Failed() && i < count; ++i)
		{
			auto name = data.String();
			unsigned entries = data.U(16);
			for (unsigned j = 0; !Failed() && !data.Failed() && j < entries; ++j)
			{
				data.String();
				if (data.U(1))
				{
					auto payload = data.Bytes(data.U(16));
					if (name == "userinfo" && !data.Failed())
						User(payload);
				}
			}
			if (data.U(1))
				for (unsigned j = 0, n = data.U(16); !Failed() && !data.Failed() && j < n; ++j)
				{
					data.String();
					if (data.U(1))
						data.Skip(size_t(data.U(16)) * 8);
				}
			if (name == "userinfo" && !data.Failed() && !Failed())
				completeUserInfo = true;
		}
		if (data.Failed() && !Failed())
		{
			const bool truncated = error == "Truncated network message" ||
				error == "Truncated message payload" || error == "Truncated byte payload";
			if (openFortress && payload.size() == 512 * 1024 && completeUserInfo && truncated)
				Result.Warning = "OF string-table snapshot reached its 512 KiB limit after userinfo; remaining network packets were indexed normally.";
			else Fail(std::move(error));
		}
	}
	void GameEvent(Bits data, int tick)
	{
		unsigned id = data.U(9);
		auto definition = Definitions.find(id);
		if (definition == Definitions.end())
			{ Fail("Game event has no descriptor"); return; }
		Event event;
		event.Tick = tick;
		event.Name = definition->second.Name;
		for (const auto &field : definition->second.Fields)
		{
			if (Failed()) return;
			std::string value;
			switch (field.second)
			{
			case 1:
				value = data.String();
				break;
			case 2: {
				uint32_t raw = data.U(32);
				float number;
				std::memcpy(&number, &raw, 4);
				value = std::to_string(number);
				break;
			}
			case 3:
				value = std::to_string(int32_t(data.U(32)));
				break;
			case 4:
				value = std::to_string(int16_t(data.U(16)));
				break;
			case 5:
				value = std::to_string(data.U(8));
				break;
			case 6:
				value = data.U(1) ? "1" : "0";
				break;
			case 7:
				break;
			default:
				{ Fail("Unknown event field type"); return; }
			}
			event.Fields[field.first] = value;
		}
		if (Failed()) return;
		auto &f = event.Fields;
		const auto &name = event.Name;
		if (name == "player_connect" || name == "player_connect_client" || name == "player_changename")
		{
			int user = std::atoi(f["userid"].c_str());
			auto label = name == "player_changename" ? f["newname"] : f["name"];
			if (user > 0 && !label.empty())
			{
				if (Players.size() >= 65536 && !Players.count(user))
					{ Fail("Too many demo player identities"); return; }
				Players[user] = label;
				if (label == Result.Recorder)
					RecorderId = user;
			}
		}
		if (name == "player_death")
		{
			event.Kind = Category::Death;
			f["victim_name"] = Player(f["userid"]);
			f["attacker_name"] = Player(f["attacker"]);
			if (std::atoi(f["assister"].c_str()) > 0)
				f["assister_name"] = Player(f["assister"]);
			event.Summary = f["attacker_name"] + " killed " + f["victim_name"];
			if (!f["weapon"].empty())
				event.Summary += " (" + f["weapon"] + ")";
			event.RecorderDeath = RecorderId > 0 && std::atoi(f["userid"].c_str()) == RecorderId;
		}
		else if (name.find("teamplay_round_") == 0 || name == "teamplay_game_over")
			event.Kind = Category::Round;
		else if (name.find("teamplay_point_") == 0 || name.find("teamplay_flag_") == 0 ||
		         name == "teamplay_capture_blocked" || name == "object_destroyed" || name == "object_sapped")
			event.Kind = Category::Objective;
		else if (name == "player_spawn" || name == "player_changeclass" || name == "player_team" ||
		         name == "player_connect" || name == "player_disconnect" || name == "player_changename" ||
		         name == "player_chargedeployed")
			event.Kind = Category::Player;
		else
			return; // Exclude high-frequency hurt/heal/achievement traffic from the cinematic index.
		if (event.Summary.empty())
			event.Summary = name;
		if (f.count("userid") && !f.count("victim_name"))
			f["player_name"] = Player(f["userid"]);
		if (tick >= 0)
		{
			event.Search = event.Name + " " + event.Summary;
			for (const auto &field : event.Fields)
			{
				event.Search += " " + field.first + " " + field.second;
				StoredBytes += field.first.size() + field.second.size() + 64;
			}
			StoredBytes += event.Search.size() + event.Summary.size() + event.Name.size() + sizeof(Event);
			if (StoredBytes > 64 * 1024 * 1024)
				{ Fail("Event index exceeds 64 MiB"); return; }
			for (auto &c : event.Search)
				if ('A' <= c && c <= 'Z')
					c = char(c - 'A' + 'a');
			if (Result.Events.size() >= 250000)
				{ Fail("Event index exceeds 250000 entries"); return; }
			Result.Events.push_back(std::move(event));
		}
	}
	void Packet(Bits b, int tick)
	{
		while (b.Left() >= 6)
		{
            LastMessageBit = b.Position();
			unsigned type = b.U(6);
            LastMessage = int(type);
			switch (type)
			{
			case 0:
				break;
			case 1:
				b.String();
				break;
			case 2:
				b.Skip(32);
				b.String();
				b.Skip(1);
				break;
			case 3:
				b.Skip(64);
				break;
			case 4:
			case 7:
				b.String();
				break;
			case 5:
				for (unsigned i = 0, n = b.U(8); !Failed() && i < n; ++i)
				{
					b.String();
					b.String();
				}
				break;
			case 6:
				b.Skip(40);
				break;
			case 8: {
				b.Skip(98 + (Protocol > 17 ? 128 : 32));
				b.Skip(16);
				uint32_t raw = b.U(32);
				float interval;
				std::memcpy(&interval, &raw, 4);
				if (std::isfinite(interval) && interval > 0 && interval < 1)
					Result.Interval = interval;
				b.Skip(8);
				for (int i = 0; i < 4; ++i)
					b.String();
				if (Protocol > 15)
					b.Skip(1);
				break;
			}
			case 10: {
				unsigned n = b.U(16);
				if (!b.U(1))
					for (unsigned i = 0; !Failed() && i < n; ++i)
					{
						b.Skip(Log2(n) + 1);
						b.String();
						b.String();
					}
				break;
			}
			case 11:
				b.Skip(1);
				break;
			case 12: {
				Table table;
				table.Name = b.String();
				table.Maximum = b.U(16);
				if (!table.Maximum || Tables.size() >= 32)
					{ Fail("Invalid string table header"); return; }
				unsigned n = b.U(Log2(table.Maximum) + 1), length = Protocol > 23 ? b.Var() : b.U(20);
				if (b.U(1))
				{
					b.Skip(12);
					table.FixedBits = int(b.U(4));
				}
				bool compressed = b.U(1) != 0;
				auto part = b.Part(length);
				if (table.Name == "userinfo")
				{
					if (compressed)
					{
						auto bytes = Decompress(part);
						if (!Failed()) Entries(Bits(bytes, Result.Error), table, n);
					}
					else
						Entries(part, table, n);
				}
				Tables.push_back(table);
				break;
			}
			case 13: {
				unsigned id = b.U(5), n = b.U(1) ? b.U(16) : 1, length = b.U(20);
				auto part = b.Part(length);
				if (id >= Tables.size())
					{ Fail("String table update has no descriptor"); return; }
				if (Tables[id].Name == "userinfo")
					Entries(part, Tables[id], n);
				break;
			}
			case 14: {
				b.String();
				if (b.U(8) == 255)
					b.Skip(16);
				break;
			}
			case 15:
				b.Skip(16);
				b.Skip(b.U(16));
				break;
			case 17: {
				bool reliable = b.U(1) != 0;
				if (!reliable)
					b.Skip(8);
				b.Skip(b.U(reliable ? 8 : 16));
				break;
			}
			case 18:
				b.Skip(11);
				break;
			case 19:
				b.Skip(49);
				break;
			case 20:
				b.Skip(48);
				break;
			case 21: {
				bool axis[3] = {b.U(1) != 0, b.U(1) != 0, b.U(1) != 0};
				for (bool present : axis)
					if (present)
					{
						bool integral = b.U(1) != 0, fraction = b.U(1) != 0;
						if (integral || fraction)
							b.Skip(1 + (integral ? 14 : 0) + (fraction ? 5 : 0));
					}
				b.Skip(9);
				if (b.U(1))
					b.Skip(24);
				b.Skip(1);
				break;
			}
			case 23:
				b.Skip(8);
				b.Skip(b.U(11));
				break;
			case 24:
				b.Skip(20);
				b.Skip(b.U(11));
				break;
			case 25: {
				auto part = b.Part(b.U(11));
				GameEvent(part, tick);
				break;
			}
			case 26: {
				b.Skip(11);
				if (b.U(1))
					b.Skip(32);
				b.Skip(12);
				unsigned n = b.U(20);
				b.Skip(size_t(n) + 1);
				break;
			}
			case 27: {
				b.Skip(8);
				unsigned n = Protocol > 23 ? b.Var() : b.U(17);
				b.Skip(n);
				break;
			}
			case 28:
				b.Skip(Protocol > 22 ? 14 : 13);
				break;
			case 29:
				b.Skip(16);
				b.Skip(size_t(b.U(16)) * 8);
				break;
			case 30: {
				unsigned n = b.U(9);
				auto data = b.Part(b.U(20));
				for (unsigned i = 0; !Failed() && i < n; ++i)
				{
					unsigned id = data.U(9);
					Definition def;
					def.Name = data.String(256);
					while (unsigned kind = data.U(3))
					{
						auto name = data.String(256);
						if (def.Fields.size() >= 128)
							{ Fail("Oversized game event definition"); return; }
						def.Fields.emplace_back(name, kind);
					}
					Definitions[id] = std::move(def);
				}
				break;
			}
			case 31:
				b.Skip(32);
				b.String();
				break;
			case 32: {
				const auto bytes = b.U(32);
				if (bytes > b.Left() / 8) { Fail("Truncated message payload"); return; }
				b.Skip(size_t(bytes) * 8);
				break;
			}
			default:
				Fail("Unsupported network message " + std::to_string(type) +
				     " at demo tick " + std::to_string(tick));
				return;
			}
		}
	}
};
} // namespace

bool SameMap(const std::string &left, const std::string &right)
{
	auto normalized = [](std::string value) {
		for (auto &c : value)
		{
			if (c == '\\')
				c = '/';
			if (c >= 'A' && c <= 'Z')
				c = char(c - 'A' + 'a');
		}
		auto maps = value.rfind("/maps/");
		if (maps != std::string::npos)
			value.erase(0, maps + 6);
		else if (value.compare(0, 5, "maps/") == 0)
			value.erase(0, 5);
		if (value.size() >= 4 && value.compare(value.size() - 4, 4, ".bsp") == 0)
			value.resize(value.size() - 4);
		return value;
	};
	auto a = normalized(left), b = normalized(right);
	if (a.empty() || b.empty())
		return false;
	if (a == b)
		return true;
	if (a.find('/') != std::string::npos && b.find('/') != std::string::npos)
		return false;
	return a.substr(a.find_last_of('/') + 1) == b.substr(b.find_last_of('/') + 1);
}
Index Read(const std::filesystem::path &file, const std::atomic_bool &cancel, std::atomic<float> &progress)
{
	Reader reader;
	reader.Result.File = file;
	progress.store(0);
	// Unsupported/truncated demo data is an expected outcome, not a C++ exception.
	// Some games install first-chance exception reporters which can intercept a
	// throw even when this worker would subsequently catch it.
	try
	{
		auto parse = [&]() {
			std::error_code ec;
			const auto stamp = std::filesystem::last_write_time(file, ec);
			if (ec) { reader.Fail("Cannot read demo timestamp: " + ec.message()); return; }
			const auto size = std::filesystem::file_size(file, ec);
			if (ec) { reader.Fail("Cannot read demo size: " + ec.message()); return; }
			std::ifstream stream(file, std::ios::binary);
			if (!stream) { reader.Fail("Cannot open demo file"); return; }
			auto bytes = [&](size_t count) {
				if (reader.Failed()) return std::vector<unsigned char>{};
				if (count > 16 * 1024 * 1024)
				{ reader.Fail("Oversized demo packet"); return std::vector<unsigned char>{}; }
				std::vector<unsigned char> out(count);
				if (count && !stream.read(reinterpret_cast<char *>(out.data()), count))
				{ reader.Fail("Truncated demo file"); return std::vector<unsigned char>{}; }
				return out;
			};
			auto header = bytes(1072);
			if (reader.Failed()) return;
			if (std::memcmp(header.data(), "HL2DEMO\0", 8))
            { reader.Fail("Expected a Source HL2DEMO header"); return; }
            const auto demoProtocol = LE(header.data() + 8);
			reader.Protocol = LE(header.data() + 12);
            if (demoProtocol != 3)
            { reader.Fail("Unsupported demo protocol " + std::to_string(demoProtocol) + " (expected 3)"); return; }
			if (reader.Protocol < 15 || reader.Protocol > 24)
			{ reader.Fail("Unsupported demo network protocol " + std::to_string(reader.Protocol)); return; }
			auto stringAt = [&](size_t pos) {
				std::string s(reinterpret_cast<char *>(header.data() + pos), 260);
				s.resize(s.find('\0') == std::string::npos ? s.size() : s.find('\0'));
				return s;
			};
			reader.Result.Map = stringAt(536);
			reader.Result.Recorder = stringAt(276);
			const bool openFortress = stringAt(796) == "open_fortress";
			reader.Result.TotalTicks = int(LE(header.data() + 1060));
			if (reader.Result.TotalTicks < 0)
			{ reader.Fail("Invalid demo tick count"); return; }
			while (!reader.Failed())
			{
				if (cancel.load()) { reader.Fail("Indexing cancelled"); return; }
                reader.LastOffset = stream.tellg();
                reader.LastCommand = reader.LastTick = reader.LastMessage = -1;
                reader.LastMessageBit = 0;
				auto command = bytes(1);
				if (reader.Failed()) return;
				unsigned type = command[0];
                reader.LastCommand = int(type);
				if (type == 7) break;
				auto head = bytes(4);
				if (reader.Failed()) return;
				int tick = int(LE(head.data()));
                reader.LastTick = tick;
				switch (type)
				{
				case 1:
				case 2: {
					bytes(84);
					auto length = bytes(4);
					if (reader.Failed()) return;
					auto packet = bytes(LE(length.data()));
					if (!reader.Failed()) reader.Packet(Bits(packet, reader.Result.Error), tick);
					break;
				}
				case 3: break;
				case 5: bytes(4); [[fallthrough]];
				case 4:
				case 6:
				case 8: {
					auto length = bytes(4);
					if (reader.Failed()) return;
					auto payload = bytes(LE(length.data()));
					if (!reader.Failed() && type == 8) reader.Snapshot(payload, openFortress);
					break;
				}
				default: reader.Fail("Unsupported demo packet " + std::to_string(type)); return;
				}
				if (reader.Failed()) return;
				const auto position = stream.tellg();
				if (position < 0) { reader.Fail("Cannot read demo position"); return; }
				progress.store(size ? float(double(position) / double(size)) : 0);
			}
			if (reader.Failed()) return;
			const auto finalStamp = std::filesystem::last_write_time(file, ec);
			if (ec || finalStamp != stamp) { reader.Fail("Demo file changed while indexing"); return; }
			const auto finalSize = std::filesystem::file_size(file, ec);
			if (ec || finalSize != size) { reader.Fail("Demo file changed while indexing"); return; }
			std::stable_sort(reader.Result.Events.begin(), reader.Result.Events.end(),
			                 [](const Event &a, const Event &b) { return a.Tick < b.Tick; });
			progress.store(1);
		};
		parse();
	}
	catch (const std::exception &error)
	{
		reader.Fail(error.what());
	}
    if (reader.Failed())
    {
        reader.Result.Events.clear();
        // Preserve first failure and provide enough context to inspect an OF demo
        // without guessing a different network layout or publishing partial data.
        if (reader.Protocol) reader.Result.Error += " [network protocol " + std::to_string(reader.Protocol) + "]";
        if (reader.LastOffset >= 0) reader.Result.Error += " [file offset " + std::to_string(reader.LastOffset) +
            ", command " + std::to_string(reader.LastCommand) + ", tick " + std::to_string(reader.LastTick) + "]";
        if (reader.LastMessage >= 0) reader.Result.Error += " [message " + std::to_string(reader.LastMessage) +
            ", packet bit " + std::to_string(reader.LastMessageBit) + "]";
    }
	return std::move(reader.Result);
}
} // namespace DemoTimeline
} // namespace advancedfx
