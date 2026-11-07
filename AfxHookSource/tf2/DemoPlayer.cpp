#include "DemoPlayer.h"
#include "stdafx.h"

#include "WrpConsole.h"
#include "WrpVEngineClient.h"
#include "addresses.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <deps/release/Detours/src/detours.h>
#include <deps/release/rapidxml/rapidxml.hpp>
#include <deps/release/rapidxml/rapidxml_print.hpp>
#include <iterator>
#include <limits>
#include <set>
#include <shared/binutils.h>
#include <stdexcept>
#include <string>

extern HMODULE g_H_EngineDll;
extern WrpVEngineClient *g_VEngineClient;

namespace AfxHookSource
{
namespace Tf2Demo
{

#ifdef _WIN64
namespace
{

// Research probe for the engine recorded in doc/TF2_DEMO_SEEKING.md. Never use
// these layouts on an engine that has not passed the header and code checks.
constexpr DWORD EngineTimestamp = 0x6ac41507, EngineImageSize = 0x822000, EngineChecksum = 0x4fb7d1;
bool g_Attempted = false, g_Ready = false;
void **g_PlayerTable = nullptr, **g_PlayerSlot = nullptr;
std::uint64_t g_Identity = 0, g_SeekSerial = 0;
unsigned int g_SeekDepth = 0;
int g_LastSeekTarget = -1;
using StartFn = bool (*)(void *, const char *, bool);
using SeekFn = void (*)(void *, int, bool, bool);
using EntitiesFn = bool (*)(void *, void *);
StartFn g_Start = nullptr;
SeekFn g_Seek = nullptr;
EntitiesFn g_Entities = nullptr;

// This build uses the original, 32-byte bf_read/bf_write header, rather than
// the newer CBitRead layout used by the CS:GO demo parser.
struct BitBuffer
{
	void *Data = nullptr;
	int Bytes = 0, Bits = 0, Position = 0;
	bool Overflow = false, Assert = false;
	const char *Debug = nullptr;
};
static_assert(sizeof(BitBuffer) == 32 && offsetof(BitBuffer, Position) == 16);
using DecodeFn = bool (*)(void *, void *, BitBuffer *, int, bool);
using MergeFn = int (*)(void *, BitBuffer *, BitBuffer *, BitBuffer *, int, int *, bool);
DecodeFn g_Decode = nullptr;
MergeFn g_Merge = nullptr;
void **g_EntityListSlot = nullptr;
struct CheckpointCapture
{
	bool Armed = false, Seeded = false, Boundary = false;
	const char *Error = nullptr;
	char ErrorText[256]{};
	const void *Client = nullptr;
	int ServerTick = 0;
	std::uint64_t Messages = 0, Decodes = 0;
	EncodedEntities Entities;
	std::array<void *, EncodedEntities::MaxEntities> Tables{};
	void Reset()
	{
		Seeded = Boundary = false;
		Error = nullptr;
		Client = nullptr;
		Messages = Decodes = 0;
		Entities.Clear();
		Tables.fill(nullptr);
	}
} g_Checkpoint;
// Scope is restricted to a native packet-entity processing call. A rejected
// packet invalidates the capture after the original handler returns.
thread_local const void *g_DecodeClient = nullptr;

void CaptureFailed(const char *reason)
{
	if (!g_Checkpoint.Error)
	{
		std::snprintf(g_Checkpoint.ErrorText, sizeof(g_Checkpoint.ErrorText), "%s", reason);
		g_Checkpoint.Error = g_Checkpoint.ErrorText;
	}
	g_Checkpoint.Boundary = false;
}

enum Event
{
	State,
	SeekBegin,
	SeekEnd,
	StartBegin,
	StartEnd,
	Entities
};
struct Record
{
	std::uint64_t Ms = 0, Identity = 0, SeekSerial = 0;
	Event Type = State;
	int Tick = 0, Target = -1, Playing = 0, Loading = 0, Skipping = 0;
	int ArgTick = -1, Relative = -1, Pause = -1, Accepted = -1;
	int ServerTick = -1, DeltaFrom = -1, Delta = -1, UpdateBaseline = -1, Bank = -1, UpdatedEntities = -1,
	    Frames = -1, ReferencePresent = -1, Tables = -1, BaselineStrings = -1;
};
struct Trace
{
	HANDLE File = INVALID_HANDLE_VALUE;
	std::array<Record, 4096> Records;
	size_t Head = 0, Count = 0;
	std::uint64_t Dropped = 0, Written = 0, Started = 0;
	Status Last;
	bool HasLast = false, Failed = false;
	~Trace()
	{
		if (File != INVALID_HANDLE_VALUE)
			CloseHandle(File);
	}
} g_Trace;

bool Readable(const void *pointer, size_t bytes)
{
	auto address = reinterpret_cast<std::uintptr_t>(pointer);
	if (!address || address + bytes < address)
		return false;
	const auto end = address + bytes;
	while (address < end)
	{
		MEMORY_BASIC_INFORMATION memory{};
		if (!VirtualQuery(reinterpret_cast<void *>(address), &memory, sizeof(memory)) ||
		    memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
			return false;
		const auto next = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
		if (next <= address)
			return false;
		address = next;
	}
	return true;
}

template <class T> T Read(const void *object, size_t offset)
{
	T value;
	std::memcpy(&value, static_cast<const unsigned char *>(object) + offset, sizeof(value));
	return value;
}

bool Match(const void *address, const char *pattern)
{
	const auto begin = reinterpret_cast<size_t>(address);
	return Readable(address, 256) &&
	       !Afx::BinUtils::FindPatternString(Afx::BinUtils::MemRange(begin, begin + 256), pattern).IsEmpty();
}

// An error branch may have its own chained unwind entry. Follow that chain to
// the true entry before installing a detour; the string xref alone is not one.
void *FunctionContaining(size_t address)
{
	DWORD64 base = 0;
	auto entry = RtlLookupFunctionEntry(address, &base, nullptr);
	if (!entry || base != reinterpret_cast<DWORD64>(g_H_EngineDll))
		return nullptr;
	RUNTIME_FUNCTION function = *entry;
	for (int depth = 0; depth < 16; ++depth)
	{
		if (function.BeginAddress >= function.EndAddress || function.EndAddress > EngineImageSize ||
		    function.UnwindData >= EngineImageSize - 4)
			return nullptr;
		const auto unwind = reinterpret_cast<const unsigned char *>(base + function.UnwindData);
		if (!Readable(unwind, 4))
			return nullptr;
		if (!(unwind[0] & (4 << 3)))
			return reinterpret_cast<void *>(base + function.BeginAddress);
		const size_t offset = 4 + ((size_t(unwind[2]) + 1) & ~size_t(1)) * 2;
		if (function.UnwindData + offset + sizeof(function) > EngineImageSize ||
		    !Readable(unwind + offset, sizeof(function)))
			return nullptr;
		std::memcpy(&function, unwind + offset, sizeof(function));
	}
	return nullptr;
}

void *FindDiagnosticFunction(const char *diagnostic, const char *codePattern)
{
	using namespace Afx::BinUtils;
	MemRange text, strings;
	for (ImageSectionsReader sections(g_H_EngineDll); !sections.Eof(); sections.Next())
	{
		const auto section = sections.Get();
		if (!std::memcmp(section->Name, ".text", 5))
			text = sections.GetMemRange();
		if (!std::memcmp(section->Name, ".rdata", 6))
			strings = sections.GetMemRange();
	}
	if (text.IsEmpty() || strings.IsEmpty())
		return nullptr;
	const auto anchor = FindCString(strings, diagnostic);
	if (anchor.IsEmpty())
		return nullptr;
	void *result = nullptr;
	while (!text.IsEmpty())
	{
		const auto ref = FindAddrInt32OffsetRefInContext(text, anchor.Start, 4, "48 8d 0d", nullptr);
		if (ref.IsEmpty())
			break;
		auto function = FunctionContaining(ref.Start - 3);
		// Several functions share the decoder's error string. Only accept the
		// inspected body, and reject an ambiguous second matching function.
		if (function && Match(function, codePattern))
		{
			if (result && result != function)
				return nullptr;
			result = function;
		}
		text.Start = ref.End;
	}
	return result;
}

void *Networkable(int index)
{
	if (!g_EntityListSlot || index < 0 || index >= EncodedEntities::MaxEntities)
		return nullptr;
	auto list = *g_EntityListSlot;
	if (!Readable(list, sizeof(void *)))
		return nullptr;
	auto vf = Read<void **>(list, 0);
	if (!Readable(vf, sizeof(void *)))
		return nullptr;
	return reinterpret_cast<void *(*)(void *, int)>(vf[0])(list, index);
}

bool EntityIdentity(int index, std::uint32_t &handle, int &classId, void *&table, void *&data)
{
	auto entity = Networkable(index);
	if (!Readable(entity, sizeof(void *)))
		return false;
	auto vf = Read<void **>(entity, 0);
	if (!Readable(vf, 12 * sizeof(void *)))
		return false;
	auto unknown = reinterpret_cast<void *(*)(void *)>(vf[0])(entity);
	auto clientClass = reinterpret_cast<void *(*)(void *)>(vf[2])(entity);
	if (!Readable(unknown, sizeof(void *)) || !Readable(clientClass, 0x2c))
		return false;
	auto unknownVf = Read<void **>(unknown, 0);
	if (!Readable(unknownVf, 3 * sizeof(void *)))
		return false;
	auto ref = reinterpret_cast<const void *(*)(void *)>(unknownVf[2])(unknown);
	if (!Readable(ref, sizeof(handle)))
		return false;
	handle = Read<std::uint32_t>(ref, 0);
	classId = Read<int>(clientClass, 0x28);
	table = Read<void *>(clientClass, 0x18);
	data = reinterpret_cast<void *(*)(void *)>(vf[11])(entity);
	return handle != 0xffffffffu && classId >= 0 && Readable(table, 0x20) && data;
}

bool MyDecode(void *table, void *data, BitBuffer *stream, int index, bool debug)
{
	const bool collect = g_Checkpoint.Armed && g_Checkpoint.Seeded && !g_Checkpoint.Error && g_DecodeClient;
	BitBuffer before;
	if (collect && Readable(stream, sizeof(before)))
		before = *stream;
	const bool accepted = g_Decode(table, data, stream, index, debug);
	if (!collect)
		return accepted;
	try
	{
		if (!accepted || !Readable(stream, sizeof(before)) || before.Overflow || stream->Overflow ||
		    before.Data != stream->Data || before.Bytes < 0 || before.Bits < 0 ||
		    std::int64_t(before.Bits) > std::int64_t(before.Bytes) * 8 || before.Position < 0 ||
		    stream->Position <= before.Position || stream->Position > before.Bits)
			throw std::runtime_error("invalid native decode boundary");
		std::uint32_t handle;
		int classId;
		void *entityTable, *entityData;
		if (!EntityIdentity(index, handle, classId, entityTable, entityData) || table != entityTable ||
		    data != entityData)
			throw std::runtime_error("decode target is not a current network entity");
		auto decoder = Read<void *>(table, 0x10);
		if (!Readable(decoder, 0x70))
			throw std::runtime_error("missing receive decoder");
		const int properties = Read<int>(decoder, 0x68);
		if (properties < 0 || properties > EncodedEntities::MaxProperties)
			throw std::runtime_error("invalid flattened property count");
		const auto old = g_Checkpoint.Entities.Get(index);
		const bool same = old && old->Handle == handle && old->ClassId == classId &&
		                  g_Checkpoint.Tables[index] == table &&
		                  old->PropertyTicks.size() == size_t(properties);
		// Worst case: no property overlap, with every field index using its longest
		// header. Reject before calling the engine, whose overflow path is fatal.
		const std::int64_t worstBits = (same ? old->Bits : 0) +
		                               std::int64_t(stream->Position - before.Position) +
		                               std::int64_t(properties) * 35 + 1;
		if (worstBits > EncodedEntities::MaxBytes * 8)
			throw std::runtime_error("merge exceeds conservative native output bound");
		alignas(16) std::array<unsigned char, EncodedEntities::MaxBytes + 8> output{};
		std::array<int, EncodedEntities::MaxProperties> changed{};
		BitBuffer previous;
		if (same)
			previous = {const_cast<unsigned char *>(old->Data.data()), int(old->Data.size()), old->Bits};
		BitBuffer result{output.data(), EncodedEntities::MaxBytes, EncodedEntities::MaxBytes * 8};
		before.Bits = stream->Position;
		const int count =
		    g_Merge(table, same ? &previous : nullptr, &before, &result, -1, changed.data(), false);
		if (before.Overflow || previous.Overflow || result.Overflow || before.Position != before.Bits ||
		    !g_Checkpoint.Entities.Put(index, handle, classId, properties, output.data(), result.Position,
		                               changed.data(), count, Read<int>(g_DecodeClient, 0x1b0)))
			throw std::runtime_error("invalid merged state or capture memory limit");
		g_Checkpoint.Tables[index] = table;
		++g_Checkpoint.Decodes;
	}
	catch (const std::exception &error)
	{
		CaptureFailed(error.what());
	}
	catch (...)
	{
		CaptureFailed("Unknown property capture failure.");
	}
	return accepted;
}

bool InitializeCapture()
{
	if (g_Decode)
		return true;
	auto decode =
	    FindDiagnosticFunction("RecvTable_Decode: table '%s' missing a decoder.", "48 8B 71 10 49 8B D8");
	auto merge = FindDiagnosticFunction("RecvTable_MergeDeltas: invalid parameters passed.",
	                                    "48 8B 41 10 48 89 44 24 48");
	auto copy = FindDiagnosticFunction("CL_CopyExistingEntity: missing client entity %d.\n",
	                                   "40 53 56 41 57 48 83 EC 30");
	if (!decode || !merge || !copy)
		return false;
	using namespace Afx::BinUtils;
	const auto start = reinterpret_cast<size_t>(copy);
	const auto load = FindPatternString(MemRange(start, start + 48), "48 8B 0D ?? ?? ?? ?? 44 8B 78 10");
	if (load.IsEmpty())
		return false;
	const auto code = reinterpret_cast<const unsigned char *>(load.Start);
	g_EntityListSlot = reinterpret_cast<void **>(const_cast<unsigned char *>(code + 7) + Read<int>(code, 3));
	if (!Readable(g_EntityListSlot, sizeof(void *)) || !Readable(*g_EntityListSlot, sizeof(void *)))
		return false;
	g_Decode = reinterpret_cast<DecodeFn>(decode);
	g_Merge = reinterpret_cast<MergeFn>(merge);
	LONG error = DetourTransactionBegin();
	if (error == NO_ERROR)
	{
		error = DetourUpdateThread(GetCurrentThread());
		if (error == NO_ERROR)
			error = DetourAttach(reinterpret_cast<PVOID *>(&g_Decode), reinterpret_cast<PVOID>(MyDecode));
		if (error == NO_ERROR)
			error = DetourTransactionCommit();
		else
			DetourTransactionAbort();
	}
	if (error != NO_ERROR)
	{
		g_Decode = nullptr;
		g_Merge = nullptr;
		return false;
	}
	return true;
}

Record Capture(Event type)
{
	Status status;
	GetStatus(status);
	Record result;
	result.Ms = GetTickCount64() - g_Trace.Started;
	result.Type = type;
	result.Identity = status.Identity;
	result.SeekSerial = status.SeekSerial;
	result.Tick = status.Tick;
	result.Target = status.Target;
	result.Playing = status.Playing;
	result.Loading = status.Loading;
	result.Skipping = status.Skipping;
	return result;
}

void Append(const Record &record)
{
	if (g_Trace.File == INVALID_HANDLE_VALUE || g_Trace.Failed)
		return;
	if (g_Trace.Count == g_Trace.Records.size())
	{
		++g_Trace.Dropped;
		return;
	}
	g_Trace.Records[(g_Trace.Head + g_Trace.Count++) % g_Trace.Records.size()] = record;
}

bool Write(const char *text, size_t bytes)
{
	while (bytes)
	{
		DWORD written = 0;
		const DWORD requested = static_cast<DWORD>((std::min)(bytes, size_t(65536)));
		if (!WriteFile(g_Trace.File, text, requested, &written, nullptr) || !written)
		{
			g_Trace.Failed = true;
			return false;
		}
		text += written;
		bytes -= written;
	}
	return true;
}

void Flush(size_t limit)
{
	static const char *names[] = {"state", "seek_begin", "seek_end", "start_begin", "start_end", "entities"};
	std::string buffer;
	buffer.reserve(65536);
	size_t rows = 0;
	while (limit-- && g_Trace.Count && !g_Trace.Failed)
	{
		const auto &r = g_Trace.Records[g_Trace.Head];
		char line[512];
		const int bytes = std::snprintf(
		    line, sizeof(line),
		    "%llu,%s,%llu,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
		    static_cast<unsigned long long>(r.Ms), names[r.Type], static_cast<unsigned long long>(r.Identity),
		    static_cast<unsigned long long>(r.SeekSerial), r.Tick, r.Target, r.Playing, r.Loading, r.Skipping,
		    r.ArgTick, r.Relative, r.Pause, r.Accepted, r.ServerTick, r.DeltaFrom, r.Delta, r.UpdateBaseline,
		    r.Bank, r.UpdatedEntities, r.Frames, r.ReferencePresent, r.Tables, r.BaselineStrings);
		if (bytes <= 0 || bytes >= sizeof(line))
		{
			g_Trace.Failed = true;
			break;
		}
		buffer.append(line, size_t(bytes));
		g_Trace.Head = (g_Trace.Head + 1) % g_Trace.Records.size();
		--g_Trace.Count;
		++rows;
	}
	if (!buffer.empty() && Write(buffer.data(), buffer.size()))
		g_Trace.Written += rows;
}

bool MyStart(void *player, const char *file, bool timedemo)
{
	if (g_Checkpoint.Armed)
		g_Checkpoint.Reset();
	// An internal backward restart belongs to the same playback identity.
	if (!g_SeekDepth)
		++g_Identity;
	if (g_Trace.File != INVALID_HANDLE_VALUE)
		Append(Capture(StartBegin));
	const bool accepted = g_Start(player, file, timedemo);
	if (g_Trace.File != INVALID_HANDLE_VALUE)
	{
		auto record = Capture(StartEnd);
		record.Accepted = accepted;
		Append(record);
	}
	return accepted;
}

void MySeek(void *player, int tick, bool relative, bool pause)
{
	if (g_Checkpoint.Armed)
		g_Checkpoint.Boundary = false;
	Status before;
	GetStatus(before);
	const auto absolute = std::int64_t(tick) + (relative ? before.Tick : 0);
	g_LastSeekTarget =
	    absolute >= 0 && absolute <= (std::numeric_limits<int>::max)() ? static_cast<int>(absolute) : -1;
	++g_SeekSerial;
	if (g_Trace.File != INVALID_HANDLE_VALUE)
	{
		auto record = Capture(SeekBegin);
		record.ArgTick = tick;
		record.Relative = relative;
		record.Pause = pause;
		Append(record);
	}
	++g_SeekDepth;
	g_Seek(player, tick, relative, pause);
	--g_SeekDepth;
	if (g_Trace.File != INVALID_HANDLE_VALUE)
		Append(Capture(SeekEnd));
}

void InspectFrames(const void *client, Record &record)
{
	const auto manager = static_cast<const unsigned char *>(client) + 0x8c00;
	record.Frames = Read<int>(manager, 0x18);
	if (record.Frames < 0 || record.Frames > 128)
		return;
	const void *frame = Read<void *>(manager, 8);
	record.ReferencePresent = record.Delta ? 0 : -1;
	for (int i = 0; frame && i < record.Frames; ++i)
	{
		if (!Readable(frame, 0x128))
		{
			record.ReferencePresent = -1;
			return;
		}
		if (Read<int>(frame, 0x0c) == record.DeltaFrom && record.Delta)
			record.ReferencePresent = 1;
		frame = Read<void *>(frame, 0x120);
	}
	const void *tables = Read<void *>(client, 0x8bf0);
	if (!Readable(tables, sizeof(void *)))
		return;
	auto table = Read<void **>(tables, 0);
	if (!Readable(table, 6 * sizeof(void *)))
		return;
	using CountFn = int (*)(const void *);
	using FindFn = const void *(*)(const void *, const char *);
	record.Tables = reinterpret_cast<CountFn>(table[5])(tables);
	const void *baseline = reinterpret_cast<FindFn>(table[3])(tables, "instancebaseline");
	if (Readable(baseline, sizeof(void *)))
	{
		auto baselineTable = Read<void **>(baseline, 0);
		if (Readable(baselineTable, 4 * sizeof(void *)))
			record.BaselineStrings = reinterpret_cast<CountFn>(baselineTable[3])(baseline);
	}
}

bool MyEntities(void *handler, void *message)
{
	Status status;
	const bool trace =
	    g_Trace.File != INVALID_HANDLE_VALUE && !g_Trace.Failed && GetStatus(status) && status.Playing;
	Record record;
	if (trace)
	{
		record = Capture(Entities);
		// The message-handler subobject is 16 bytes into CClientState on this build.
		const auto client = static_cast<const unsigned char *>(handler) - 0x10;
		if (Readable(message, 0x40) && Readable(client, 0x8c20))
		{
			record.ServerTick = Read<int>(client, 0x1b0);
			record.Delta = Read<unsigned char>(message, 0x30) != 0;
			record.UpdateBaseline = Read<unsigned char>(message, 0x31) != 0;
			record.Bank = Read<int>(message, 0x34);
			record.DeltaFrom = Read<int>(message, 0x38);
			record.UpdatedEntities = Read<int>(message, 0x2c);
			InspectFrames(client, record);
		}
	}
	const auto client = static_cast<const unsigned char *>(handler) - 0x10;
	const auto previousClient = g_DecodeClient;
	if (g_Checkpoint.Armed && GetStatus(status) && status.Playing)
	{
		g_Checkpoint.Boundary = false;
		if (!Readable(message, 0x40) || !Readable(client, 0x8c20))
			CaptureFailed("Invalid packet-entity layout.");
		else
		{
			// A fresh full packet is required: beginning halfway through a delta
			// chain would silently omit properties that have not changed again.
			if (!Read<unsigned char>(message, 0x30))
			{
				g_Checkpoint.Reset();
				g_Checkpoint.Seeded = true;
			}
			if (g_Checkpoint.Seeded && !g_Checkpoint.Error)
				g_DecodeClient = client;
		}
	}
	const bool accepted = g_Entities(handler, message);
	if (g_DecodeClient && g_DecodeClient != previousClient)
	{
		if (!accepted)
			CaptureFailed("Native packet-entity handler rejected a message.");
		else
		{
			g_Checkpoint.Client = client;
			g_Checkpoint.ServerTick = Read<int>(client, 0x1b0);
			++g_Checkpoint.Messages;
		}
	}
	g_DecodeClient = previousClient;
	if (trace)
	{
		record.Accepted = accepted;
		Append(record);
	}
	return accepted;
}

bool Initialize()
{
	if (g_Attempted)
		return g_Ready;
	if (!g_H_EngineDll || !g_VEngineClient)
		return false;
	g_Attempted = true;
	if (g_SourceSdkVer != SourceSdkVer_TF2)
		return false;
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(g_H_EngineDll);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;
	const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(
	    reinterpret_cast<const unsigned char *>(dos) + dos->e_lfanew);
	if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
	    pe->FileHeader.TimeDateStamp != EngineTimestamp ||
	    pe->OptionalHeader.SizeOfImage != EngineImageSize || pe->OptionalHeader.CheckSum != EngineChecksum)
		return false;
	g_PlayerTable =
	    reinterpret_cast<void **>(Afx::BinUtils::FindClassVtable(g_H_EngineDll, ".?AVCDemoPlayer@@", 0, 0));
	auto entities = reinterpret_cast<void **>(
	    Afx::BinUtils::FindClassVtable(g_H_EngineDll, ".?AVCClientState@@", 0, 0x10));
	auto engine = g_VEngineClient->GetVEngineClient_css();
	if (!g_PlayerTable || !entities || !engine)
		return false;
	auto isPlaying = reinterpret_cast<const unsigned char *>((*reinterpret_cast<void ***>(engine))[76]);
	// IVEngineClient013::IsPlayingDemo loads the global player and tail-calls slot 6.
	const unsigned char prefix[] = {0x48, 0x8b, 0x0d};
	const unsigned char suffix[] = {0x48, 0x8b, 0x01, 0x48, 0xff, 0x60, 0x30};
	if (!Readable(isPlaying, 14) || std::memcmp(isPlaying, prefix, 3) ||
	    std::memcmp(isPlaying + 7, suffix, 7))
		return false;
	g_PlayerSlot = reinterpret_cast<void **>(const_cast<unsigned char *>(isPlaying + 7) +
	                                         Read<std::int32_t>(isPlaying, 3));
	if (!Readable(g_PlayerSlot, sizeof(void *)))
		return false;
	if (!Readable(*g_PlayerSlot, 0x6a0) || Read<void **>(*g_PlayerSlot, 0) != g_PlayerTable)
		return false;
	if (!Match(g_PlayerTable[6], "0F B6 81 B0 05 00 00 C3") ||
	    !Match(g_PlayerTable[9], "80 B9 B0 05 00 00 00 74 0C 83 B9 BC 05 00 00 FF") ||
	    !Match(g_PlayerTable[5], "C6 81 C4 05 00 00 01") || !Match(g_PlayerTable[14], "89 9F BC 05 00 00") ||
	    !Match(entities[23], "80 7A 30 00"))
		return false;
	g_Start = reinterpret_cast<StartFn>(g_PlayerTable[5]);
	g_Seek = reinterpret_cast<SeekFn>(g_PlayerTable[14]);
	g_Entities = reinterpret_cast<EntitiesFn>(entities[23]);
	LONG error = DetourTransactionBegin();
	if (error != NO_ERROR)
		return false;
	error = DetourUpdateThread(GetCurrentThread());
	if (error == NO_ERROR)
		error = DetourAttach(reinterpret_cast<PVOID *>(&g_Start), reinterpret_cast<PVOID>(MyStart));
	if (error == NO_ERROR)
		error = DetourAttach(reinterpret_cast<PVOID *>(&g_Seek), reinterpret_cast<PVOID>(MySeek));
	if (error == NO_ERROR)
		error = DetourAttach(reinterpret_cast<PVOID *>(&g_Entities), reinterpret_cast<PVOID>(MyEntities));
	if (error != NO_ERROR)
	{
		DetourTransactionAbort();
		return false;
	}
	g_Ready = DetourTransactionCommit() == NO_ERROR;
	return g_Ready;
}

// Pointer-free diagnostic artifact. This deliberately has no load/restore path:
// packet encodings do not include interpolation history, physics or effects.
struct CheckpointXml
{
	rapidxml::xml_document<> Document;
	size_t Bytes = 0;
	void Budget(size_t bytes)
	{
		constexpr size_t limit = 128 * 1024 * 1024;
		if (bytes > limit || Bytes > limit - bytes)
			throw std::runtime_error("checkpoint XML memory limit");
		Bytes += bytes;
	}
	rapidxml::xml_node<> *Node(rapidxml::xml_node<> *parent, const char *name)
	{
		Budget(256);
		auto node = Document.allocate_node(rapidxml::node_element, name);
		parent->append_node(node);
		return node;
	}
	void Attr(rapidxml::xml_node<> *node, const char *name, const std::string &value)
	{
		Budget(value.size() + 128);
		node->append_attribute(Document.allocate_attribute(name, Document.allocate_string(value.c_str())));
	}
	void Number(rapidxml::xml_node<> *node, const char *name, std::int64_t value)
	{
		Attr(node, name, std::to_string(value));
	}
	void Blob(rapidxml::xml_node<> *node, const char *name, const void *data, size_t bytes)
	{
		if (bytes > 64 * 1024 * 1024 || (bytes && !Readable(data, bytes)))
			throw std::runtime_error("unreadable or oversized checkpoint payload");
		// Charge before allocating the temporary hex string as well.
		Budget(bytes * 2);
		const auto source = static_cast<const unsigned char *>(data);
		static const char hex[] = "0123456789abcdef";
		std::string encoded(bytes * 2, '0');
		for (size_t i = 0; i < bytes; ++i)
		{
			encoded[2 * i] = hex[source[i] >> 4];
			encoded[2 * i + 1] = hex[source[i] & 15];
		}
		Attr(node, name, encoded);
	}
	void Text(rapidxml::xml_node<> *node, const char *name, const char *text, size_t limit = 4096)
	{
		if (!text)
			throw std::runtime_error("missing checkpoint string");
		size_t length = 0;
		while (length < limit)
		{
			if (!Readable(text + length, 1))
				throw std::runtime_error("unreadable checkpoint string");
			if (!text[length])
			{
				Blob(node, name, text, length);
				return;
			}
			++length;
		}
		throw std::runtime_error("unterminated checkpoint string");
	}
};

void ExportProperty(CheckpointXml &xml, rapidxml::xml_node<> *parent, const void *property, int depth)
{
	if (depth > 8 || !Readable(property, 0x80))
		throw std::runtime_error("invalid send property");
	const int type = Read<int>(property, 0x10);
	if (type < 0 || type > 5)
		throw std::runtime_error("unsupported flattened property type");
	xml.Number(parent, "type", type);
	xml.Number(parent, "bits", Read<int>(property, 0x14));
	xml.Number(parent, "flags", Read<unsigned int>(property, 0x54));
	xml.Blob(parent, "range_le", static_cast<const unsigned char *>(property) + 0x18, 8);
	xml.Text(parent, "name_hex", Read<const char *>(property, 0x48));
	if (type == 5)
	{
		xml.Number(parent, "elements", Read<int>(property, 0x30));
		ExportProperty(xml, xml.Node(parent, "element"), Read<void *>(property, 0x20), depth + 1);
	}
}

void ExportSchema(CheckpointXml &xml, rapidxml::xml_node<> *parent, int classId, const void *table)
{
	if (!Readable(table, 0x20))
		throw std::runtime_error("unreadable receive table");
	const auto decoder = Read<void *>(table, 0x10);
	if (!Readable(decoder, 0x70))
		throw std::runtime_error("unreadable receive decoder");
	const int count = Read<int>(decoder, 0x68);
	const auto props = Read<void **>(decoder, 0x58);
	if (count < 0 || count > EncodedEntities::MaxProperties ||
	    (count && !Readable(props, size_t(count) * sizeof(void *))))
		throw std::runtime_error("invalid receive schema");
	auto schema = xml.Node(parent, "class");
	xml.Number(schema, "id", classId);
	xml.Text(schema, "table_hex", Read<const char *>(table, 0x18));
	for (int i = 0; i < count; ++i)
	{
		auto property = xml.Node(schema, "property");
		xml.Number(property, "index", i);
		ExportProperty(xml, property, props[i], 0);
	}
}

void ExportTables(CheckpointXml &xml, rapidxml::xml_node<> *root, const void *client)
{
	const auto container = Read<void *>(client, 0x8bf0);
	if (!Readable(container, sizeof(void *)))
		throw std::runtime_error("missing string table container");
	const auto vf = Read<void **>(container, 0);
	if (!Readable(vf, 6 * sizeof(void *)))
		throw std::runtime_error("invalid string table container");
	using IntFn = int (*)(const void *);
	using TableFn = const void *(*)(const void *, int);
	const int count = reinterpret_cast<IntFn>(vf[5])(container);
	if (count < 0 || count > 128)
		throw std::runtime_error("invalid string table count");
	auto tables = xml.Node(root, "string_tables");
	for (int i = 0; i < count; ++i)
	{
		const auto table = reinterpret_cast<TableFn>(vf[4])(container, i);
		if (!Readable(table, 0x34))
			throw std::runtime_error("invalid string table");
		const auto tv = Read<void **>(table, 0);
		if (!Readable(tv, 12 * sizeof(void *)))
			throw std::runtime_error("invalid string table interface");
		const int entries = reinterpret_cast<IntFn>(tv[3])(table);
		const int maximum = reinterpret_cast<IntFn>(tv[4])(table);
		if (entries < 0 || entries > maximum || maximum > 65536)
			throw std::runtime_error("invalid string table size");
		auto node = xml.Node(tables, "table");
		xml.Number(node, "id", reinterpret_cast<IntFn>(tv[2])(table));
		xml.Number(node, "maximum", maximum);
		xml.Number(node, "entry_bits", reinterpret_cast<IntFn>(tv[5])(table));
		xml.Number(node, "flags", Read<unsigned char>(table, 0x28));
		xml.Number(node, "userdata_bytes", Read<int>(table, 0x2c));
		xml.Number(node, "userdata_bits", Read<int>(table, 0x30));
		xml.Text(node, "name_hex", reinterpret_cast<const char *(*)(const void *)>(tv[1])(table));
		for (int j = 0; j < entries; ++j)
		{
			auto entry = xml.Node(node, "entry");
			xml.Number(entry, "index", j);
			xml.Text(entry, "string_hex",
			         reinterpret_cast<const char *(*)(const void *, int)>(tv[9])(table, j), 65536);
			int bytes = 0;
			const auto data =
			    reinterpret_cast<const void *(*)(const void *, int, int *)>(tv[11])(table, j, &bytes);
			if (bytes < 0 || bytes > 65536)
				throw std::runtime_error("invalid string table userdata");
			xml.Blob(entry, "userdata_hex", data, size_t(bytes));
		}
	}
}

void ExportBaselinesAndFrames(CheckpointXml &xml, rapidxml::xml_node<> *root, const void *client)
{
	auto banks = xml.Node(root, "entity_baselines");
	for (int bank = 0; bank < 2; ++bank)
		for (int index = 0; index < EncodedEntities::MaxEntities; ++index)
		{
			const auto baseline =
			    Read<void *>(client, 0x3d8 + (bank * EncodedEntities::MaxEntities + index) * sizeof(void *));
			if (!baseline)
				continue;
			if (!Readable(baseline, 0x58))
				throw std::runtime_error("invalid entity baseline");
			const auto packedBits = Read<std::uint32_t>(baseline, 0x40);
			const int bits = static_cast<int>(packedBits & 0x7fffffffu);
			const auto cls = Read<void *>(baseline, 8);
			if (bits < 0 || bits > EncodedEntities::MaxBytes * 8 || !Readable(cls, 0x2c))
				throw std::runtime_error("invalid packed baseline");
			auto entry = xml.Node(banks, "baseline");
			xml.Number(entry, "bank", bank);
			xml.Number(entry, "index", index);
			xml.Number(entry, "class", Read<int>(cls, 0x28));
			xml.Number(entry, "bits", bits);
			xml.Number(entry, "packed_bits", packedBits);
			xml.Blob(entry, "data_hex", Read<void *>(baseline, 0x38), size_t(bits + 7) / 8);
		}
	auto frames = xml.Node(root, "reference_frames");
	xml.Attr(frames, "complete", "0");
	const auto manager = static_cast<const unsigned char *>(client) + 0x8c00;
	const int count = Read<int>(manager, 0x18);
	if (count < 0 || count > 128)
		throw std::runtime_error("invalid reference frame count");
	const auto first = Read<void *>(manager, 8);
	const void *frame = first;
	std::set<const void *> seen;
	for (int i = 0; i < count; ++i)
	{
		if (!Readable(frame, 0x130) || !seen.insert(frame).second)
			throw std::runtime_error("invalid reference frame chain");
		auto node = xml.Node(frames, "frame");
		xml.Number(node, "tick", Read<int>(frame, 0x0c));
		xml.Number(node, "last_entity", Read<int>(frame, 8));
		xml.Blob(node, "transmitted_hex", static_cast<const unsigned char *>(frame) + 0x10, 256);
		frame = Read<void *>(frame, 0x120);
	}
	if (frame || (count == 0 && first))
		throw std::runtime_error("reference frame count mismatch");
}

std::string ExportCheckpoint(const Status &status)
{
	CheckpointXml xml;
	auto root = xml.Node(&xml.Document, "tf2_checkpoint_capture");
	xml.Attr(root, "version", "1");
	xml.Attr(root, "restorable", "0");
	xml.Attr(root, "reference_engine_sha256",
	         "4802a74ce0abb1175851d00e37fc88609085a96238aa52ac0eb224ae4a08ec80");
	xml.Attr(root, "limitations",
	         "No client effects, interpolation history, complete frame state, or validated resume boundary");
	xml.Number(root, "demo_tick", status.Tick);
	xml.Number(root, "server_tick", Read<int>(g_Checkpoint.Client, 0x1b0));
	xml.Number(root, "last_entity_tick", g_Checkpoint.ServerTick);
	xml.Number(root, "messages", g_Checkpoint.Messages);
	xml.Number(root, "decodes", g_Checkpoint.Decodes);
	const auto player = *g_PlayerSlot;
	xml.Text(root, "demo_filename_hex", static_cast<const char *>(player) + 8, 260);
	// This cursor is evidence only: demo packet read-ahead and stream clock
	// bookkeeping still need validation before it can be used for restoration.
	const auto buffer = Read<void *>(player, 0x540);
	if (!Readable(buffer, 0x28) || Read<unsigned char>(buffer, 0x18))
		throw std::runtime_error("unavailable demo input cursor");
	const int cursor = Read<int>(buffer, 0x10), maximum = Read<int>(buffer, 0x20);
	if (cursor < 0 || cursor > maximum)
		throw std::runtime_error("invalid demo input cursor");
	xml.Number(root, "input_cursor", cursor);
	xml.Number(root, "input_maximum", maximum);
	auto entities = xml.Node(root, "entities");
	auto schemas = xml.Node(root, "schemas");
	std::set<int> classes;
	for (int index = 0; index < EncodedEntities::MaxEntities; ++index)
	{
		if (!Networkable(index))
			continue;
		std::uint32_t handle;
		int classId;
		void *table, *data;
		const auto state = g_Checkpoint.Entities.Get(index);
		if (!EntityIdentity(index, handle, classId, table, data) || !state || state->Handle != handle ||
		    state->ClassId != classId || g_Checkpoint.Tables[index] != table)
			throw std::runtime_error("current entity has no matching captured encoding");
		auto node = xml.Node(entities, "entity");
		xml.Number(node, "index", index);
		xml.Number(node, "handle", handle);
		xml.Number(node, "class", classId);
		xml.Number(node, "bits", state->Bits);
		xml.Blob(node, "data_hex", state->Data.data(), size_t(state->Bits + 7) / 8);
		xml.Blob(node, "property_ticks_le32_hex", state->PropertyTicks.data(),
		         state->PropertyTicks.size() * sizeof(int));
		if (classes.insert(classId).second)
			ExportSchema(xml, schemas, classId, table);
	}
	if (classes.empty())
		throw std::runtime_error("no captured entities");
	ExportTables(xml, root, g_Checkpoint.Client);
	ExportBaselinesAndFrames(xml, root, g_Checkpoint.Client);
	std::string result;
	rapidxml::print(std::back_inserter(result), xml.Document, 0);
	return result;
}

} // namespace
#endif

bool GetStatus(Status &status)
{
	status = {};
#ifdef _WIN64
	if (!g_Ready)
		return false;
	const void *player = *g_PlayerSlot;
	if (!Readable(player, 0x6a0) || Read<void **>(player, 0) != g_PlayerTable)
		return false;
	status.Native = true;
	status.Playing = Read<unsigned char>(player, 0x5b0) != 0;
	status.Loading = Read<unsigned char>(player, 0x5c4) != 0;
	const int pending = Read<int>(player, 0x5bc);
	status.Skipping = status.Playing && pending != -1;
	status.Target = pending == -1 ? -1 : static_cast<int>(static_cast<unsigned int>(pending) & ~0x88000000u);
	using TickFn = int (*)(const void *);
	status.Tick = reinterpret_cast<TickFn>(g_PlayerTable[3])(player);
	status.Identity = g_Identity;
	status.SeekSerial = g_SeekSerial;
	status.LastSeekTarget = g_LastSeekTarget;
	return true;
#else
	return false;
#endif
}

bool StartTrace(const wchar_t *file)
{
#ifdef _WIN64
	Status status;
	if (g_Trace.File != INVALID_HANDLE_VALUE || !file || !*file || !Initialize() || !GetStatus(status))
		return false;
	g_Trace.File = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
	                           FILE_ATTRIBUTE_NORMAL, nullptr);
	if (g_Trace.File == INVALID_HANDLE_VALUE)
		return false;
	g_Trace.Head = g_Trace.Count = 0;
	g_Trace.Dropped = g_Trace.Written = 0;
	g_Trace.Started = GetTickCount64();
	g_Trace.HasLast = g_Trace.Failed = false;
	const char header[] =
	    "# TF2 native demo observation v1; timestamp=6ac41507; image_size=822000; checksum=4fb7d1\n"
	    "ms,event,identity,seek_serial,demo_tick,pending_tick,playing,loading,skipping,arg_tick,relative,"
	    "pause,accepted,server_tick,delta_from,delta,update_baseline,bank,updated_entities,frames_before,"
	    "reference_present,tables,baseline_strings\n";
	if (!Write(header, sizeof(header) - 1))
	{
		StopTrace();
		return false;
	}
	UpdateTrace();
	return true;
#else
	return false;
#endif
}

bool BeginCheckpointCapture()
{
#ifdef _WIN64
	Status status;
	if (g_Checkpoint.Armed || !Initialize() || !GetStatus(status) || !status.Playing || status.Loading ||
	    status.Skipping || !InitializeCapture())
		return false;
	g_Checkpoint.Reset();
	g_Checkpoint.Armed = true;
	Tier0_Msg("Checkpoint capture armed. Restart this demo normally (playdemo) to capture its initial full "
	          "entity packet, then seek forward to the desired tick and wait for completion.\n");
	return true;
#else
	return false;
#endif
}

bool SaveCheckpointCapture(const wchar_t *file)
{
#ifdef _WIN64
	Status before;
	if (!file || !*file || !g_Checkpoint.Armed || !g_Checkpoint.Seeded || g_Checkpoint.Error ||
	    !g_Checkpoint.Boundary || g_DecodeClient || !GetStatus(before) || !before.Playing || before.Loading ||
	    before.Skipping || !Readable(g_Checkpoint.Client, 0x8c20))
		return false;
	try
	{
		const auto xml = ExportCheckpoint(before);
		Status after;
		if (!GetStatus(after) || !after.Playing || after.Loading || after.Skipping ||
		    after.Tick != before.Tick || after.Identity != before.Identity ||
		    after.SeekSerial != before.SeekSerial)
			throw std::runtime_error("playback changed during capture");
		HANDLE output = CreateFileW(file, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
		                            FILE_ATTRIBUTE_NORMAL, nullptr);
		if (output == INVALID_HANDLE_VALUE)
			return false;
		size_t position = 0;
		while (position < xml.size())
		{
			DWORD written = 0;
			const auto bytes = static_cast<DWORD>((std::min)(xml.size() - position, size_t(65536)));
			if (!WriteFile(output, xml.data() + position, bytes, &written, nullptr) || !written)
				break;
			position += written;
		}
		const bool closed = CloseHandle(output) != 0;
		if (position != xml.size() || !closed)
		{
			Tier0_Warning("Checkpoint file write failed; the new file may be incomplete.\n");
			return false;
		}
		Tier0_Msg("Captured checkpoint evidence at demo tick %d (%zu bytes). This file is not restorable.\n",
		          before.Tick, xml.size());
		return true;
	}
	catch (const std::exception &error)
	{
		Tier0_Warning("Checkpoint export rejected: %s.\n", error.what());
		return false;
	}
#else
	return false;
#endif
}

void EndCheckpointCapture()
{
#ifdef _WIN64
	g_Checkpoint.Armed = false;
	g_Checkpoint.Reset();
#endif
}

void PrintCheckpointStatus()
{
#ifdef _WIN64
	Tier0_Msg("Checkpoint capture: %s; initial full packet %s; messages %llu, decodes %llu, owned payload "
	          "%zu bytes.\n",
	          g_Checkpoint.Armed ? "on" : "off", g_Checkpoint.Seeded ? "seen" : "not seen",
	          static_cast<unsigned long long>(g_Checkpoint.Messages),
	          static_cast<unsigned long long>(g_Checkpoint.Decodes), g_Checkpoint.Entities.Bytes());
	if (g_Checkpoint.Error)
		Tier0_Warning("%s\n", g_Checkpoint.Error);
	Tier0_Msg("Capture experiment only: checkpoint restoration is not implemented.\n");
#else
	Tier0_Msg("Checkpoint capture requires the verified Windows x64 TF2 engine.\n");
#endif
}

void UpdateTrace()
{
#ifdef _WIN64
	if (g_Trace.File == INVALID_HANDLE_VALUE || g_Trace.Failed)
		return;
	Status status;
	if (GetStatus(status) &&
	    (!g_Trace.HasLast || status.Playing != g_Trace.Last.Playing ||
	     status.Loading != g_Trace.Last.Loading || status.Skipping != g_Trace.Last.Skipping ||
	     status.Target != g_Trace.Last.Target || status.Identity != g_Trace.Last.Identity))
	{
		Append(Capture(State));
		g_Trace.Last = status;
		g_Trace.HasLast = true;
	}
	// No per-packet console/file writes. Limit the work done on an editor frame.
	Flush(256);
#endif
}

void OnNetUpdateEnd()
{
#ifdef _WIN64
	if (g_Checkpoint.Armed && g_Checkpoint.Seeded && !g_Checkpoint.Error && g_Checkpoint.Client)
		g_Checkpoint.Boundary = true;
	// Fast-forward may skip rendering. Reuse the existing client frame-stage hook
	// to drain full batches without writing from every entity-message callback.
	if (g_Trace.File != INVALID_HANDLE_VALUE && !g_Trace.Failed && g_Trace.Count >= 256)
		Flush(256);
#endif
}

void StopTrace()
{
#ifdef _WIN64
	if (g_Trace.File == INVALID_HANDLE_VALUE)
		return;
	Flush(g_Trace.Count);
	char footer[160];
	const int bytes = std::snprintf(footer, sizeof(footer), "# records=%llu dropped=%llu write_failed=%d\n",
	                                static_cast<unsigned long long>(g_Trace.Written),
	                                static_cast<unsigned long long>(g_Trace.Dropped), g_Trace.Failed ? 1 : 0);
	if (!g_Trace.Failed)
		Write(footer, size_t(bytes));
	CloseHandle(g_Trace.File);
	g_Trace.File = INVALID_HANDLE_VALUE;
#endif
}

void PrintTraceStatus()
{
#ifdef _WIN64
	Status status;
	const bool supported = GetStatus(status);
	Tier0_Msg("TF2 demo probe: %s; trace %s; written %llu, buffered %zu, dropped %llu%s.\n",
	          supported     ? "verified engine"
	          : g_Attempted ? "unsupported engine"
	                        : "not enabled (start a trace to enable)",
	          g_Trace.File != INVALID_HANDLE_VALUE ? "on" : "off",
	          static_cast<unsigned long long>(g_Trace.Written), g_Trace.Count,
	          static_cast<unsigned long long>(g_Trace.Dropped), g_Trace.Failed ? "; file write failed" : "");
	if (supported)
		Tier0_Msg("Demo tick %d; target %d; playing %d, loading %d, skipping %d.\n", status.Tick,
		          status.Target, status.Playing, status.Loading, status.Skipping);
#else
	Tier0_Msg("TF2 demo probe: requires the verified Windows x64 TF2 engine. Ordinary seeking remains "
	          "available.\n");
#endif
}

} // namespace Tf2Demo
} // namespace AfxHookSource
