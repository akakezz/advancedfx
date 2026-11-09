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

public:
	explicit Bits(const std::vector<unsigned char> &data) : m_Data(&data), m_End(data.size() * 8)
	{
	}
	size_t Left() const
	{
		return m_End - m_Pos;
	}
	uint32_t U(unsigned count)
	{
		if (count > 32 || count > Left())
			throw std::runtime_error("Truncated network message");
		uint32_t value = 0;
		for (unsigned i = 0; i < count; ++i, ++m_Pos)
			value |= uint32_t(((*m_Data)[m_Pos / 8] >> (m_Pos % 8)) & 1) << i;
		return value;
	}
	void Skip(size_t count)
	{
		if (count > Left())
			throw std::runtime_error("Truncated message payload");
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
			if (shift == 28 && (byte & 0xf0))
				throw std::runtime_error("Invalid variable-length integer");
			value |= (byte & 127) << shift;
			if (!(byte & 128))
				return value;
		}
		throw std::runtime_error("Invalid variable-length integer");
	}
	std::string String(size_t limit = 4096)
	{
		std::string out;
		while (out.size() <= limit)
		{
			auto c = U(8);
			if (!c)
				return out;
			out.push_back(char(c));
		}
		throw std::runtime_error("Oversized demo string");
	}
	std::vector<unsigned char> Bytes(size_t count)
	{
		if (count > Left() / 8)
			throw std::runtime_error("Truncated byte payload");
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
	if (expected > 16 * 1024 * 1024 || size < 4 || size > data.Left() / 8)
		throw std::runtime_error("Invalid compressed string table size");
	auto bytes = data.Bytes(size);
	std::vector<unsigned char> out;
	out.reserve(expected);
	auto copy = [&](size_t offset, size_t count) {
		if (!offset || offset > out.size() || count > expected - out.size())
			throw std::runtime_error("Invalid compressed string table reference");
		while (count--)
			out.push_back(out[out.size() - offset]);
	};
	if (!std::memcmp(bytes.data(), "SNAP", 4))
	{
		std::vector<unsigned char> raw(bytes.begin() + 4, bytes.end());
		Bits input(raw);
		if (input.Var() != expected)
			throw std::runtime_error("Snappy size mismatch");
		while (out.size() < expected)
		{
			unsigned tag = input.U(8), type = tag & 3;
			if (type == 0)
			{
				size_t n = tag >> 2;
				if (n >= 60)
					n = input.U(unsigned(n - 59) * 8);
				++n;
				if (n > expected - out.size())
					throw std::runtime_error("Oversized Snappy literal");
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
			throw std::runtime_error("LZSS size mismatch");
		size_t p = 8;
		while (out.size() < expected)
		{
			if (p >= bytes.size())
				throw std::runtime_error("Truncated LZSS table");
			unsigned flags = bytes[p++];
			for (int bit = 0; bit < 8 && out.size() < expected; ++bit)
			{
				if (p >= bytes.size())
					throw std::runtime_error("Truncated LZSS token");
				if (!(flags & (1 << bit)))
					out.push_back(bytes[p++]);
				else
				{
					if (p + 2 > bytes.size())
						throw std::runtime_error("Truncated LZSS reference");
					unsigned a = bytes[p++], b = bytes[p++], n = (b & 15) + 1;
					if (n == 1)
						throw std::runtime_error("Early LZSS terminator");
					copy(((a << 4) | (b >> 4)) + 1, n);
				}
			}
		}
	}
	else
		throw std::runtime_error("Unsupported string table compression");
	return out;
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
	std::map<unsigned, Definition> Definitions;
	std::vector<Table> Tables;
	std::map<int, std::string> Players;
	int RecorderId = -1;
	size_t StoredBytes = 0;
	void User(const std::vector<unsigned char> &data)
	{
		if (data.size() < 36)
			return;
		int id = int(LE(data.data() + 32));
		std::string name(reinterpret_cast<const char *>(data.data()), 32);
		name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
		if (id > 0 && !name.empty())
		{
			if (Players.size() >= 65536 && !Players.count(id))
				throw std::runtime_error("Too many demo player identities");
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
			throw std::runtime_error("Invalid string table count");
		std::vector<std::string> history;
		int index = -1;
		for (unsigned i = 0; i < count; ++i)
		{
			index = data.U(1) ? index + 1 : int(data.U(Log2(table.Maximum)));
			if (index < 0 || unsigned(index) >= table.Maximum)
				throw std::runtime_error("Invalid string table index");
			std::string text;
			if (data.U(1))
			{
				if (data.U(1))
				{
					unsigned h = data.U(5), length = data.U(5);
					if (h >= history.size() || length > history[h].size())
						throw std::runtime_error("Invalid string table history");
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
	void Snapshot(Bits data)
	{
		unsigned count = data.U(8);
		for (unsigned i = 0; i < count; ++i)
		{
			auto name = data.String();
			unsigned entries = data.U(16);
			for (unsigned j = 0; j < entries; ++j)
			{
				data.String();
				if (data.U(1))
				{
					auto payload = data.Bytes(data.U(16));
					if (name == "userinfo")
						User(payload);
				}
			}
			if (data.U(1))
				for (unsigned j = 0, n = data.U(16); j < n; ++j)
				{
					data.String();
					if (data.U(1))
						data.Skip(size_t(data.U(16)) * 8);
				}
		}
	}
	void GameEvent(Bits data, int tick)
	{
		unsigned id = data.U(9);
		auto definition = Definitions.find(id);
		if (definition == Definitions.end())
			throw std::runtime_error("Game event has no descriptor");
		Event event;
		event.Tick = tick;
		event.Name = definition->second.Name;
		for (const auto &field : definition->second.Fields)
		{
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
				throw std::runtime_error("Unknown event field type");
			}
			event.Fields[field.first] = value;
		}
		auto &f = event.Fields;
		const auto &name = event.Name;
		if (name == "player_connect" || name == "player_connect_client" || name == "player_changename")
		{
			int user = std::atoi(f["userid"].c_str());
			auto label = name == "player_changename" ? f["newname"] : f["name"];
			if (user > 0 && !label.empty())
			{
				if (Players.size() >= 65536 && !Players.count(user))
					throw std::runtime_error("Too many demo player identities");
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
				throw std::runtime_error("Event index exceeds 64 MiB");
			for (auto &c : event.Search)
				if ('A' <= c && c <= 'Z')
					c = char(c - 'A' + 'a');
			if (Result.Events.size() >= 250000)
				throw std::runtime_error("Event index exceeds 250000 entries");
			Result.Events.push_back(std::move(event));
		}
	}
	void Packet(Bits b, int tick)
	{
		while (b.Left() >= 6)
		{
			unsigned type = b.U(6);
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
				for (unsigned i = 0, n = b.U(8); i < n; ++i)
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
					for (unsigned i = 0; i < n; ++i)
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
					throw std::runtime_error("Invalid string table header");
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
						Entries(Bits(bytes), table, n);
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
					throw std::runtime_error("String table update has no descriptor");
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
				for (unsigned i = 0; i < n; ++i)
				{
					unsigned id = data.U(9);
					Definition def;
					def.Name = data.String(256);
					while (unsigned kind = data.U(3))
					{
						auto name = data.String(256);
						if (def.Fields.size() >= 128)
							throw std::runtime_error("Oversized game event definition");
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
			case 32:
				b.Skip(size_t(b.U(32)) * 8);
				break;
			default:
				throw std::runtime_error("Unsupported network message " + std::to_string(type) +
				                         " at demo tick " + std::to_string(tick));
			}
		}
	}
};
} // namespace

Index Read(const std::filesystem::path &file, const std::atomic_bool &cancel, std::atomic<float> &progress)
{
	Reader reader;
	reader.Result.File = file;
	try
	{
		const auto stamp = std::filesystem::last_write_time(file);
		const auto size = std::filesystem::file_size(file);
		std::ifstream stream(file, std::ios::binary);
		auto bytes = [&](size_t count) {
			if (count > 16 * 1024 * 1024)
				throw std::runtime_error("Oversized demo packet");
			std::vector<unsigned char> out(count);
			if (count && !stream.read(reinterpret_cast<char *>(out.data()), count))
				throw std::runtime_error("Truncated demo file");
			return out;
		};
		auto header = bytes(1072);
		if (std::memcmp(header.data(), "HL2DEMO\0", 8) || LE(header.data() + 8) != 3)
			throw std::runtime_error("Expected a Source demo protocol 3 file (TF2/TF2C)");
		reader.Protocol = LE(header.data() + 12);
		if (reader.Protocol < 15 || reader.Protocol > 24)
			throw std::runtime_error("Unsupported demo network protocol");
		auto stringAt = [&](size_t pos) {
			std::string s(reinterpret_cast<char *>(header.data() + pos), 260);
			s.resize(s.find('\0') == std::string::npos ? s.size() : s.find('\0'));
			return s;
		};
		reader.Result.Map = stringAt(536);
		reader.Result.Recorder = stringAt(276);
		reader.Result.TotalTicks = int(LE(header.data() + 1060));
		if (reader.Result.TotalTicks < 0)
			throw std::runtime_error("Invalid demo tick count");
		bool stopped = false;
		while (!stopped)
		{
			if (cancel.load())
				throw std::runtime_error("Indexing cancelled");
			auto command = bytes(1);
			unsigned type = command[0];
			if (type == 7)
			{
				stopped = true;
				break;
			}
			auto head = bytes(4);
			int tick = int(LE(head.data()));
			switch (type)
			{
			case 1:
			case 2: {
				bytes(84);
				auto length = bytes(4);
				auto packet = bytes(LE(length.data()));
				reader.Packet(Bits(packet), tick);
				break;
			}
			case 3:
				break;
			case 5:
				bytes(4);
				[[fallthrough]];
			case 4:
			case 6:
			case 8: {
				auto length = bytes(4);
				auto payload = bytes(LE(length.data()));
				if (type == 8)
					reader.Snapshot(Bits(payload));
				break;
			}
			case 7:
				stopped = true;
				break;
			default:
				throw std::runtime_error("Unsupported demo packet " + std::to_string(type));
			}
			progress.store(size ? float(double(stream.tellg()) / double(size)) : 0);
		}
		if (std::filesystem::last_write_time(file) != stamp || std::filesystem::file_size(file) != size)
			throw std::runtime_error("Demo file changed while indexing");
		std::stable_sort(reader.Result.Events.begin(), reader.Result.Events.end(),
		                 [](const Event &a, const Event &b) { return a.Tick < b.Tick; });
		progress.store(1);
	}
	catch (const std::exception &error)
	{
		reader.Result.Error = error.what();
		reader.Result.Events.clear();
	}
	return std::move(reader.Result);
}
} // namespace DemoTimeline
} // namespace advancedfx
