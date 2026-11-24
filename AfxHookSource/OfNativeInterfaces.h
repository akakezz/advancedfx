#pragma once
// Preflight for integrated legacy Open Fortress candidate adapters. Interface
// names and executable addresses do not prove semantic ABI compatibility.
#include <windows.h>
#include <initializer_list>
#include <cstdint>
#include <cstring>
namespace OfNativeInterfaces {
inline bool Read(const void *address, void *result, size_t size)
{
    SIZE_T n = 0;
    return address && ReadProcessMemory(GetCurrentProcess(), address, result, size, &n) && n == size;
}
inline bool Image(HMODULE module, DWORD timestamp, DWORD size)
{
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    return module && Read(module, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew >= 0x40 && dos.e_lfanew <= 0x1000 &&
        Read(reinterpret_cast<const unsigned char *>(module) + dos.e_lfanew, &nt, sizeof(nt)) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        (!timestamp || nt.FileHeader.TimeDateStamp == timestamp) && (!size || nt.OptionalHeader.SizeOfImage == size);
}
inline bool KnownClient()
{
#ifdef _WIN64
    return false;
#else
    auto module = GetModuleHandleA("client.dll");
    if (!Image(module, 0x66b4fe85, 0x00a27000)) return false;
    const auto base = reinterpret_cast<const unsigned char *>(module);
    unsigned char a[5]{}, b[5]{};
    const unsigned char expectA[] = {0xe8,0x22,0x92,0x04,0x00}, expectB[] = {0xe8,0xf4,0xb9,0x04,0x00};
    // The interpolation detour can replace the target entry but not these CALL sites.
    return Read(base+0x0bff79,a,sizeof(a)) && Read(base+0x0bd7a7,b,sizeof(b)) &&
        !std::memcmp(a,expectA,sizeof(a)) && !std::memcmp(b,expectB,sizeof(b));
#endif
}
inline bool Executable(const void *address, HMODULE module)
{
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !module || !VirtualQuery(address,&info,sizeof(info)) ||
        info.State != MEM_COMMIT || info.AllocationBase != module || (info.Protect & PAGE_GUARD)) return false;
    auto p = info.Protect & 0xff;
    return p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
}
inline bool Slots(void *object, const char *moduleName, std::initializer_list<unsigned> slots)
{
    auto module = GetModuleHandleA(moduleName);
    void **table = nullptr;
    MEMORY_BASIC_INFORMATION info{};
    if (!Image(module,0,0) || !Read(object,&table,sizeof(table)) || !table ||
        !VirtualQuery(table,&info,sizeof(info)) || info.State != MEM_COMMIT || info.AllocationBase != module) return false;
    for (unsigned slot : slots) {
        void *function = nullptr;
        if (!Read(table+slot,&function,sizeof(function)) || !Executable(function,module)) return false;
    }
    return true;
}
}
