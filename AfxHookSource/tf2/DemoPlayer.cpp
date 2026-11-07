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
#include <limits>
#include <shared/binutils.h>
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
	const bool accepted = g_Entities(handler, message);
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
