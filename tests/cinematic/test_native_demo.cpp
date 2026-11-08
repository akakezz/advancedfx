// Optional Linux x64 test; requires the externally supplied, recorded engine.dll.
// Execute only isolated bitstream routines from the recorded Windows engine.
// No PE entry point, Steam initialization, game world, or engine globals setup.
#include "../../AfxHookSource/tf2/DemoPlayer.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#define MSABI __attribute__((ms_abi))
template <class T> T get(const void *p, size_t offset)
{
	T value;
	std::memcpy(&value, static_cast<const unsigned char *>(p) + offset, sizeof(T));
	return value;
}
template <class T> void put(void *p, size_t offset, T value)
{
	std::memcpy(static_cast<unsigned char *>(p) + offset, &value, sizeof(T));
}
extern "C" MSABI void *host_copy(void *a, const void *b, size_t n)
{
	return std::memcpy(a, b, n);
}
extern "C" MSABI void *host_move(void *a, const void *b, size_t n)
{
	return std::memmove(a, b, n);
}
extern "C" MSABI void *host_set(void *a, int b, size_t n)
{
	return std::memset(a, b, n);
}
extern "C" MSABI void host_error(const char *format, ...)
{
	std::cerr << "Native error: " << format << '\n';
	_exit(91);
}

struct Image
{
	unsigned char *Base = nullptr;
	size_t Size = 0;
	Image(const char *path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f)
			throw std::runtime_error("missing engine");
		std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
		auto pe = get<std::uint32_t>(b.data(), 0x3c);
		auto optional = pe + 24;
		if (get<std::uint32_t>(b.data(), pe + 8) != 0x6ac41507)
			throw std::runtime_error("unverified engine timestamp");
		Size = get<std::uint32_t>(b.data(), optional + 56);
		if (Size != 0x822000 || get<std::uint32_t>(b.data(), optional + 64) != 0x4fb7d1)
			throw std::runtime_error("unverified image");
		Base = (unsigned char *)mmap((void *)0x180000000, Size, PROT_READ | PROT_WRITE | PROT_EXEC,
		                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (Base == MAP_FAILED)
			throw std::runtime_error("preferred image address unavailable");
		std::memcpy(Base, b.data(), get<std::uint32_t>(b.data(), optional + 60));
		const auto sections = pe + 24 + get<std::uint16_t>(b.data(), pe + 20);
		for (int i = 0; i < get<std::uint16_t>(b.data(), pe + 6); ++i)
		{
			auto section = sections + i * 40;
			auto rva = get<std::uint32_t>(b.data(), section + 12),
			     n = get<std::uint32_t>(b.data(), section + 16),
			     raw = get<std::uint32_t>(b.data(), section + 20);
			if (rva + n > Size || raw + n > b.size())
				throw std::runtime_error("bad section");
			std::memcpy(Base + rva, b.data() + raw, n);
		}
		auto imports = get<std::uint32_t>(b.data(), optional + 112 + 8);
		for (size_t descriptor = imports; get<std::uint32_t>(Base, descriptor + 12); descriptor += 20)
		{
			auto table = get<std::uint32_t>(Base, descriptor),
			     iat = get<std::uint32_t>(Base, descriptor + 16);
			for (size_t j = 0; auto entry = get<std::uint64_t>(Base, table + j * 8); ++j)
			{
				if (entry >> 63)
					continue;
				std::string name((char *)Base + entry + 2);
				void *target = nullptr;
				if (name == "memcpy")
					target = (void *)host_copy;
				if (name == "memmove")
					target = (void *)host_move;
				if (name == "memset")
					target = (void *)host_set;
				if (name == "Error")
					target = (void *)host_error;
				if (target)
					put(Base, iat + j * 8, target);
			}
		}
	}
	~Image()
	{
		if (Base && Base != MAP_FAILED)
			munmap(Base, Size);
	}

	std::pair<unsigned char *, size_t> section(const char *name)
	{
		const auto pe = get<std::uint32_t>(Base, 0x3c);
		const auto start = pe + 24 + get<std::uint16_t>(Base, pe + 20);
		for (int i = 0; i < get<std::uint16_t>(Base, pe + 6); ++i)
		{
			const auto p = Base + start + i * 40;
			if (!std::memcmp(p, name, std::strlen(name)))
				return {Base + get<std::uint32_t>(p, 12), get<std::uint32_t>(p, 8)};
		}
		throw std::runtime_error("missing image section");
	}

	void *diagnostic(const char *message, std::initializer_list<unsigned char> pattern)
	{
		const auto strings = section(".rdata"), text = section(".text");
		const auto target = std::search(strings.first, strings.first + strings.second, message,
		                                message + std::strlen(message) + 1);
		if (target == strings.first + strings.second)
			throw std::runtime_error("missing diagnostic");
		const auto pe = get<std::uint32_t>(Base, 0x3c);
		const auto directory = pe + 24 + 112 + 3 * 8;
		const auto entries = Base + get<std::uint32_t>(Base, directory);
		const size_t count = get<std::uint32_t>(Base, directory + 4) / 12;
		void *result = nullptr;
		for (size_t i = 0; i + 7 <= text.second; ++i)
		{
			auto instruction = text.first + i;
			if (std::memcmp(instruction, "\x48\x8d\x0d", 3) ||
			    reinterpret_cast<std::intptr_t>(instruction + 7) + get<std::int32_t>(instruction, 3) !=
			        reinterpret_cast<std::intptr_t>(target))
				continue;
			const size_t pc = instruction - Base;
			size_t low = 0, high = count;
			while (low < high)
			{
				const auto mid = low + (high - low) / 2;
				if (get<std::uint32_t>(entries, mid * 12) <= pc)
					low = mid + 1;
				else
					high = mid;
			}
			if (!low)
				throw std::runtime_error("missing unwind function");
			const auto entry = entries + (low - 1) * 12;
			if (pc >= get<std::uint32_t>(entry, 4))
				throw std::runtime_error("unwind function gap");
			std::uint32_t begin = get<std::uint32_t>(entry, 0), unwind = get<std::uint32_t>(entry, 8);
			for (int depth = 0;; ++depth)
			{
				if (depth == 16 || unwind >= Size - 4)
					throw std::runtime_error("invalid unwind chain");
				if (!(Base[unwind] & (4 << 3)))
					break;
				const size_t chain = unwind + 4 + ((size_t(Base[unwind + 2]) + 1) & ~size_t(1)) * 2;
				if (chain + 12 > Size)
					throw std::runtime_error("unwind chain out of image");
				begin = get<std::uint32_t>(Base, chain);
				unwind = get<std::uint32_t>(Base, chain + 8);
			}
			if (begin > Size - 256)
				throw std::runtime_error("function out of image");
			if (std::search(Base + begin, Base + begin + 256, pattern.begin(), pattern.end()) ==
			    Base + begin + 256)
				continue;
			if (result && result != Base + begin)
				throw std::runtime_error("ambiguous diagnostic binding");
			result = Base + begin;
		}
		if (!result)
			throw std::runtime_error("missing matching diagnostic binding");
		return result;
	}
};

struct Buffer
{
	void *Data = nullptr;
	int Bytes = 0, Bits = 0, Position = 0;
	bool Overflow = false, Assert = false;
	const char *Debug = nullptr;
};
static_assert(sizeof(Buffer) == 32 && offsetof(Buffer, Position) == 16 && offsetof(Buffer, Overflow) == 20);
struct Bits
{
	std::vector<unsigned char> Data = std::vector<unsigned char>(65544, 0);
	int Count = 0;
	void value(std::uint32_t n, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			if ((n >> i) & 1)
				Data[Count / 8] |= 1u << (Count % 8);
			++Count;
		}
	}
	void field(int diff)
	{
		value(1, 1);
		int tag = diff < 16 ? 0 : diff < 256 ? 1 : diff < 4096 ? 2 : 3;
		value(tag, 2);
		value(diff, tag == 0 ? 4 : tag == 1 ? 8 : tag == 2 ? 12 : 32);
	}
	Buffer reader()
	{
		return {Data.data(), int(Data.size()), Count, 0, false, false, nullptr};
	}
};
using MergeFn = int(MSABI *)(void *, Buffer *, Buffer *, Buffer *, int, int *, bool);

using Field = std::pair<int, Bits>;
Bits encode(const std::vector<Field> &fields, int prefix = 0)
{
	Bits out;
	out.value(0, prefix);
	int last = -1;
	for (const auto &field : fields)
	{
		out.field(field.first - last - 1);
		for (int i = 0; i < field.second.Count; ++i)
			out.value((field.second.Data[i / 8] >> (i % 8)) & 1, 1);
		last = field.first;
	}
	out.value(0, 1);
	return out;
}
Bits raw(std::initializer_list<std::uint32_t> values)
{
	Bits bits;
	for (auto value : values)
		bits.value(value, 32);
	return bits;
}
Bits stringBits(const std::string &value)
{
	Bits bits;
	bits.value(value.size(), 9);
	for (unsigned char c : value)
		bits.value(c, 8);
	return bits;
}
Bits arrayBits(std::initializer_list<std::uint32_t> values)
{
	Bits bits;
	bits.value(values.size(), 2);
	for (auto value : values)
		bits.value(value, 32);
	return bits;
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 2)
			throw std::runtime_error("pass engine.dll path");
		Image image(argv[1]);
		auto decode = image.diagnostic("RecvTable_Decode: table '%s' missing a decoder.",
		                               {0x48, 0x8b, 0x71, 0x10, 0x49, 0x8b, 0xd8});
		auto merge = (MergeFn)image.diagnostic("RecvTable_MergeDeltas: invalid parameters passed.",
		                                       {0x48, 0x8b, 0x41, 0x10, 0x48, 0x89, 0x44, 0x24, 0x48});
		auto copy = image.diagnostic("CL_CopyExistingEntity: missing client entity %d.\n",
		                             {0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30});
		if (decode != image.Base + 0x18b740 || (void *)merge != image.Base + 0x18bca0 ||
		    copy != image.Base + 0x88db0)
			throw std::runtime_error("diagnostic bindings differ from recorded functions");
		alignas(16) std::array<unsigned char, 0x300> decoder{};
		alignas(16) std::array<unsigned char, 0x28> table{};
		std::array<std::array<unsigned char, 0x80>, 7> descriptors{};
		for (int type = 0; type < 6; ++type)
		{
			put(descriptors[type].data(), 0x10, type);
			put(descriptors[type].data(), 0x14, 32);
			put(descriptors[type].data(), 0x54, type == 0 ? 1 : 4);
		}
		put(descriptors[5].data(), 0x20, descriptors[0].data());
		put(descriptors[5].data(), 0x30, 3);
		// Additional variable-width unsigned int codec.
		put(descriptors[6].data(), 0x10, 0);
		put(descriptors[6].data(), 0x14, 32);
		put(descriptors[6].data(), 0x54, 33);
		std::array<void *, 4096> props{};
		props.fill(descriptors[0].data());
		for (int type = 0; type < 7; ++type)
			props[type] = descriptors[type].data();
		put(decoder.data(), 0x58, props.data());
		put(decoder.data(), 0x68, int(props.size()));
		put(table.data(), 0x10, decoder.data());
		int cases = 0;
		auto test = [&](const std::vector<Field> &oldFields, const std::vector<Field> &deltaFields,
		                int prefix = 0, bool nullOld = false, bool oversized = false) {
			std::map<int, Bits> all;
			for (const auto &f : oldFields)
				all.insert_or_assign(f.first, f.second);
			for (const auto &f : deltaFields)
				all.insert_or_assign(f.first, f.second);
			std::vector<Field> expectedFields(all.begin(), all.end());
			Bits original = encode(oldFields), delta = encode(deltaFields, prefix),
			     expected = encode(expectedFields), out;
			Buffer from = original.reader(), change = delta.reader();
			change.Position = prefix;
			using Store = AfxHookSource::Tf2Demo::EncodedEntities;
			const auto capacity =
			    Store::MergeCapacity(nullOld ? 0 : original.Count, delta.Count - prefix, int(props.size()));
			if (!capacity)
				throw std::runtime_error("invalid production merge capacity");
			out.Data.assign(capacity + 8, 0);
			Buffer output{out.Data.data(), int(capacity), int(capacity * 8), 0, false, false, nullptr};
			std::array<int, 4096> changed;
			changed.fill(-1);
			const auto originalInput = original.Data, deltaInput = delta.Data;
			int result =
			    merge(table.data(), nullOld ? nullptr : &from, &change, &output, -1, changed.data(), false);
			if (result != int(deltaFields.size()))
				throw std::runtime_error("incorrect changed-field count");
			for (int i = 0; i < result; ++i)
				if (changed[i] != deltaFields[i].first)
					throw std::runtime_error("incorrect changed-field indices");
			if (output.Overflow || from.Overflow || change.Overflow || output.Position != expected.Count ||
			    change.Position != delta.Count)
				throw std::runtime_error("merge size/overflow differs, case " + std::to_string(cases));
			for (int i = 0; i < expected.Count; ++i)
				if (((out.Data[i / 8] ^ expected.Data[i / 8]) >> (i % 8)) & 1)
					throw std::runtime_error("native merged bits differ");
			if (originalInput != original.Data || deltaInput != delta.Data)
				throw std::runtime_error("merge changed its input payload");
			Store owned;
			const bool stored = owned.Put(0, 1, 0, int(props.size()), out.Data.data(), output.Position,
			                              changed.data(), result, 1000);
			if (stored == oversized)
				throw std::runtime_error("actual output size acceptance differs");
			++cases;
		};
		std::cout << "Calling recorded native RecvTable_MergeDeltas on isolated buffers\n" << std::flush;
		Bits varint;
		varint.value(0xac, 8);
		varint.value(0x02, 8);
		std::vector<Field> all{{0, raw({42})},   {1, raw({0x3f800000})},   {2, raw({1, 2, 3})},
		                       {3, raw({4, 5})}, {4, stringBits("hello")}, {5, arrayBits({7, 8, 9})},
		                       {6, varint},      {300, raw({99})},         {4095, raw({1234})}};
		std::vector<Field> delta{{0, raw({73})},
		                         {1, raw({0x40000000})},
		                         {2, raw({4, 5, 6})},
		                         {3, raw({7, 8})},
		                         {4, stringBits("replacement")},
		                         {5, arrayBits({1})},
		                         {6, varint},
		                         {4095, raw({6})}};
		test(all, delta);
		test(all, delta, 13);
		test({}, all, 7, true);
		test(all, {});
		test({}, {}, 0, true);
		test(all, {{4, stringBits("")}, {5, arrayBits({})}});
		test(all, {{4, stringBits(std::string(511, 'x'))}, {5, arrayBits({1, 2, 3})}});
		// Deterministic insertion, replacement, field gaps and empty updates.
		std::uint32_t random = 0x5a17;
		for (int trial = 0; trial < 1000; ++trial)
		{
			std::vector<Field> previous, next;
			for (int i = 7; i < 4096; ++i)
			{
				random = random * 1664525 + 1013904223;
				if ((random & 31) == 0)
					previous.emplace_back(i, raw({random}));
				if ((random & 127) == 1)
					next.emplace_back(i, raw({random ^ 1234}));
			}
			test(previous, next, trial % 31);
		}
		std::vector<Field> dense, replacement, disjoint;
		for (int i = 7; i < 2007; ++i)
		{
			dense.emplace_back(i, raw({42}));
			replacement.emplace_back(i, raw({99}));
		}
		test(dense, replacement); // Large overlap: upper bound exceeds 16 KiB, final encoding fits.
		dense.resize(1800);
		for (int i = 1807; i < 3607; ++i)
			disjoint.emplace_back(i, raw({7}));
		test(dense, disjoint, 0, false, true); // Actual union exceeds 16 KiB; owned storage must reject it.
		std::cout << "PASS: " << cases
		          << " native merges; all six property types, variable integers, changed-field indices, "
		             "unaligned input, input isolation\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
