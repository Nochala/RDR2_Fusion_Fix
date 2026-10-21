#include "CameraSmoothing.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uint32_t knownImageSize = 0x7397600;
    constexpr std::uint32_t knownTimestamp = 0x65EEC92E;
    constexpr std::uint32_t knownChecksum = 0x05576F9C;
    constexpr std::uint32_t openBulkRva = 0x25A82CC;
    constexpr std::uint32_t getDeviceRva = 0x25ADDD4;
    constexpr std::uint32_t parseCallsiteRva = 0x25AF8E1;
    constexpr std::uint32_t parseTargetRva = 0x25B260C;
    bool diagnostics = false;
    ULONGLONG earlyTick = 0;

    struct Pattern
    {
        const char* name;
        std::uint32_t rva;
        const unsigned char* bytes;
        const unsigned char* mask;
        SIZE_T size;
    };

    constexpr unsigned char openBytes[] = {
        0x40, 0x53, 0x48, 0x81, 0xEC, 0, 0, 0, 0, 0x49, 0x8B, 0xD8, 0x48,
        0x8D, 0x4C, 0x24, 0, 0x4C, 0x8B, 0xC2, 0xBA, 0, 0, 0, 0, 0xE8,
        0, 0, 0, 0, 0x48, 0x83, 0x64, 0x24
    };
    constexpr unsigned char openMask[] = {
        1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,
        0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1
    };
    constexpr unsigned char deviceBytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0, 0x48, 0x89, 0x6C, 0x24, 0, 0x48,
        0x89, 0x74, 0x24, 0, 0x57, 0x48, 0x83, 0xEC, 0, 0x41, 0x8A,
        0xF0, 0x40, 0x8A, 0xEA, 0x41, 0xB8
    };
    constexpr unsigned char deviceMask[] = {
        1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 1
    };
    constexpr unsigned char callBytes[] = {
        0x48, 0x8B, 0xCE, 0x88, 0x44, 0x24, 0, 0x4C, 0x89,
        0x7C, 0x24, 0, 0xE8, 0, 0, 0, 0
    };
    constexpr unsigned char callMask[] = {
        1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0
    };

    bool BuildLogPath(wchar_t (&path)[MAX_PATH])
    {
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (!length || length >= MAX_PATH)
            return false;
        DWORD offset = length;
        while (offset && path[offset - 1] != L'\\' && path[offset - 1] != L'/')
            --offset;
        if (!offset)
            return false;
        constexpr wchar_t filename[] = L"GameplayFixesRDR2_CameraResource.log";
        if (offset + sizeof(filename) / sizeof(filename[0]) > MAX_PATH)
            return false;
        std::memcpy(path + offset, filename, sizeof(filename));
        return true;
    }

    HANDLE OpenLog(bool replace)
    {
        wchar_t path[MAX_PATH] = {};
        if (!BuildLogPath(path))
            return INVALID_HANDLE_VALUE;
        return CreateFileW(path, replace ? GENERIC_WRITE : FILE_APPEND_DATA,
            FILE_SHARE_READ, nullptr, replace ? CREATE_ALWAYS : OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    void WriteLine(HANDLE file, const char* line)
    {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
    }

    bool ReadBytes(std::uintptr_t address, void* out, SIZE_T size)
    {
        SIZE_T received = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
            out, size, &received) && received == size;
    }

    void HexLine(HANDLE file, const char* label, std::uintptr_t base, std::uint32_t rva, SIZE_T size)
    {
        unsigned char buffer[64] = {};
        if (size > sizeof(buffer) || !ReadBytes(base + rva, buffer, size))
        {
            char line[160] = {};
            std::snprintf(line, sizeof(line), "  %s RVA=0x%X read_failed", label, rva);
            WriteLine(file, line);
            return;
        }
        char line[256] = {};
        int length = std::snprintf(line, sizeof(line), "  %s RVA=0x%X bytes=", label, rva);
        if (length < 0 || static_cast<SIZE_T>(length) >= sizeof(line))
            return;
        SIZE_T offset = static_cast<SIZE_T>(length);
        for (SIZE_T i = 0; i < size && offset + 4 < sizeof(line); ++i)
        {
            length = std::snprintf(line + offset, sizeof(line) - offset, "%02X%s",
                static_cast<unsigned int>(buffer[i]), i + 1 == size ? "" : " ");
            if (length <= 0 || static_cast<SIZE_T>(length) >= sizeof(line) - offset)
                break;
            offset += static_cast<SIZE_T>(length);
        }
        WriteLine(file, line);
    }

    bool Validate(HANDLE file, std::uintptr_t base, const Pattern& pattern)
    {
        unsigned char buffer[48] = {};
        if (pattern.size > sizeof(buffer) || !ReadBytes(base + pattern.rva, buffer, pattern.size))
        {
            WriteLine(file, "  Signature read failed; aborting further analysis.");
            return false;
        }
        bool match = true;
        for (SIZE_T i = 0; i < pattern.size; ++i)
            if (pattern.mask[i] && buffer[i] != pattern.bytes[i])
                match = false;
        char line[160] = {};
        std::snprintf(line, sizeof(line), "  %s RVA=0x%X expected_bytes=%s",
            pattern.name, pattern.rva, match ? "MATCH" : "MISMATCH");
        WriteLine(file, line);
        return match;
    }

    bool InExecutableSection(std::uint32_t rva, const IMAGE_NT_HEADERS64& nt,
        std::uintptr_t ntBase)
    {
        const std::uintptr_t table = ntBase + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
            nt.FileHeader.SizeOfOptionalHeader;
        for (unsigned int i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        {
            IMAGE_SECTION_HEADER section = {};
            if (!ReadBytes(table + i * sizeof(section), &section, sizeof(section)))
                return false;
            if (std::memcmp(section.Name, ".text", 5) != 0 || !section.Misc.VirtualSize)
                continue;
            const std::uint64_t from = section.VirtualAddress;
            const std::uint64_t to = from + section.Misc.VirtualSize;
            if (static_cast<std::uint64_t>(rva) >= from && static_cast<std::uint64_t>(rva) < to)
                return true;
        }
        return false;
    }

    void CallTarget(HANDLE file, const char* name, std::uintptr_t base, std::uint32_t callerRva,
        std::uint32_t callOffset, const IMAGE_NT_HEADERS64& nt, std::uintptr_t ntBase)
    {
        unsigned char call[5] = {};
        char line[176] = {};
        if (!ReadBytes(base + callerRva + callOffset, call, sizeof(call)) || call[0] != 0xE8)
        {
            std::snprintf(line, sizeof(line), "  %s: expected direct E8 call absent", name);
            WriteLine(file, line);
            return;
        }
        std::int32_t displacement = 0;
        std::memcpy(&displacement, call + 1, sizeof(displacement));
        const std::int64_t target = static_cast<std::int64_t>(callerRva) + callOffset + 5 + displacement;
        const bool inSection = target >= 0 && static_cast<std::uint64_t>(target) < nt.OptionalHeader.SizeOfImage &&
            InExecutableSection(static_cast<std::uint32_t>(target), nt, ntBase);
        std::snprintf(line, sizeof(line), "  %s: target_RVA=0x%llX in_executable_section=%s%s",
            name, static_cast<unsigned long long>(target), inSection ? "yes" : "no",
            name[0] == 'P' && target == parseTargetRva ? " matches_prior_log=yes" : "");
        WriteLine(file, line);
        if (inSection)
            HexLine(file, "call_target_first_32", base, static_cast<std::uint32_t>(target), 32);
    }

    bool InDataSection(std::uint32_t rva, SIZE_T size, const IMAGE_NT_HEADERS64& nt,
        std::uintptr_t ntBase)
    {
        if (rva >= nt.OptionalHeader.SizeOfImage || size > nt.OptionalHeader.SizeOfImage - rva)
            return false;
        const std::uintptr_t table = ntBase + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
            nt.FileHeader.SizeOfOptionalHeader;
        for (unsigned int i = 0; i < nt.FileHeader.NumberOfSections; ++i)
        {
            IMAGE_SECTION_HEADER section = {};
            if (!ReadBytes(table + i * sizeof(section), &section, sizeof(section)))
                return false;
            if ((section.Characteristics & IMAGE_SCN_MEM_READ) &&
                !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
                rva >= section.VirtualAddress &&
                static_cast<std::uint64_t>(rva) + size <=
                    static_cast<std::uint64_t>(section.VirtualAddress) + section.Misc.VirtualSize)
                return true;
        }
        return false;
    }

    void InspectDispatchOwner(HANDLE file, std::uintptr_t base,
        const IMAGE_NT_HEADERS64& nt, std::uintptr_t ntBase)
    {
        constexpr std::uint32_t slotRva = 0x3612660;
        constexpr std::uint32_t dispatchOffset = 0x28;
        constexpr std::uint32_t tableRva = slotRva - dispatchOffset;
        std::uintptr_t entries[6] = {};
        if (!InDataSection(tableRva, sizeof(entries), nt, ntBase) ||
            !ReadBytes(base + tableRva, entries, sizeof(entries)) || entries[5] != base + openBulkRva)
        {
            WriteLine(file, "OpenBulk dispatch anchor changed or unreadable; reference inspection skipped.");
            return;
        }
        for (std::uintptr_t entry : entries)
        {
            if (entry < base || entry - base >= nt.OptionalHeader.SizeOfImage ||
                !InExecutableSection(static_cast<std::uint32_t>(entry - base), nt, ntBase))
            {
                WriteLine(file, "Candidate table has a non-code entry; reference inspection skipped.");
                return;
            }
        }
        WriteLine(file, "Conditional table candidate RVA=0x3612638: six code entries, OpenBulk at +0x28.");
        WriteLine(file, "This assumes the observed dispatch can select OpenBulk; no live device table has been observed.");
        constexpr std::uint32_t lowerTargetRva = tableRva - 0x40;
        constexpr std::uint32_t upperTargetRva = tableRva + 0x80;
        constexpr SIZE_T chunk = 0x10000;
        unsigned char buffer[chunk + 6] = {};
        unsigned int matches = 0;
        unsigned int exactMatches = 0;
        unsigned int failures = 0;
        unsigned int sectionsScanned = 0;
        std::uint64_t checked = 0;
        const std::uintptr_t sectionTable = ntBase + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
            nt.FileHeader.SizeOfOptionalHeader;
        for (unsigned int sectionIndex = 0; sectionIndex < nt.FileHeader.NumberOfSections; ++sectionIndex)
        {
            IMAGE_SECTION_HEADER section = {};
            if (!ReadBytes(sectionTable + sectionIndex * sizeof(section), &section, sizeof(section)))
            {
                WriteLine(file, "Section header read failed; full reference inspection stopped.");
                return;
            }
            if (std::memcmp(section.Name, ".text", 5) != 0 ||
                !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE) ||
                section.VirtualAddress >= nt.OptionalHeader.SizeOfImage)
                continue;
            const std::uint32_t from = section.VirtualAddress;
            const std::uint32_t available = nt.OptionalHeader.SizeOfImage - from;
            const std::uint32_t length = section.Misc.VirtualSize < available ?
                section.Misc.VirtualSize : available;
            if (!length)
                continue;
            const std::uint32_t to = from + length;
            ++sectionsScanned;
            for (std::uint32_t rva = from; rva < to; rva += static_cast<std::uint32_t>(chunk))
            {
                const SIZE_T remaining = to - rva;
                const SIZE_T size = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
                if (!ReadBytes(base + rva, buffer, size))
                {
                    ++failures;
                    continue;
                }
                checked += remaining < chunk ? remaining : chunk;
                for (SIZE_T i = 0; i < chunk && i + 7 <= size; ++i)
                {
                    if ((buffer[i] & 0xF8) != 0x48 || buffer[i + 1] != 0x8D ||
                        (buffer[i + 2] & 0xC7) != 0x05)
                        continue;
                    std::int32_t displacement = 0;
                    std::memcpy(&displacement, buffer + i + 3, sizeof(displacement));
                    const std::int64_t target = static_cast<std::int64_t>(rva) + i + 7 + displacement;
                    if (target < lowerTargetRva || target > upperTargetRva)
                        continue;
                    ++matches;
                    if (target == tableRva)
                        ++exactMatches;
                    if (matches <= 16)
                    {
                        const std::uint32_t reference = rva + static_cast<std::uint32_t>(i);
                        const unsigned int reg = ((buffer[i + 2] >> 3) & 7) +
                            ((buffer[i] & 4) ? 8 : 0);
                        char line[192] = {};
                        std::snprintf(line, sizeof(line),
                            "  Nearby table LEA candidate RVA=0x%X target_RVA=0x%llX delta=%+lld destination_register=%u",
                            reference, static_cast<unsigned long long>(target),
                            static_cast<long long>(target - tableRva), reg);
                        WriteLine(file, line);
                        if (reference < to && to - reference >= 32)
                            HexLine(file, "table_reference_and_following_code", base, reference, 32);
                    }
                }
            }
        }
        char line[224] = {};
        std::snprintf(line, sizeof(line),
            "Executable table reference scan: sections=%u bytes=%llu failed_chunks=%u nearby_LEA=%u exact_table_LEA=%u (at most 16 shown)",
            sectionsScanned, static_cast<unsigned long long>(checked), failures, matches, exactMatches);
        WriteLine(file, line);
        WriteLine(file, "LEA candidates require instruction-boundary and object-assignment validation; absence does not exclude the table.");
    }
    void InspectResourceDispatch(HANDLE file, std::uintptr_t base,
        const IMAGE_NT_HEADERS64& nt, std::uintptr_t ntBase)
    {
        constexpr std::uint32_t callerRva = 0x25AF776;
        constexpr unsigned char prefix[] = {
            0xE8, 0x59, 0xE6, 0xFF, 0xFF, 0x48, 0x89, 0x46, 0x10,
            0x4C, 0x8D, 0x46, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x50,
            0x48, 0x8B, 0xC8, 0x4C, 0x8B, 0x08, 0x41, 0xFF, 0x51, 0x28
        };
        unsigned char actual[sizeof(prefix)] = {};
        if (!InExecutableSection(callerRva, nt, ntBase) ||
            !InExecutableSection(callerRva + 63, nt, ntBase) ||
            !ReadBytes(base + callerRva, actual, sizeof(actual)) ||
            std::memcmp(actual, prefix, sizeof(prefix)) != 0)
        {
            WriteLine(file, "GetDevice dispatch prefix differs from prior log; continuation skipped.");
            return;
        }
        WriteLine(file, "GetDevice caller 0x25AF776 prefix verified: RCX=result, RDX=stack path candidate, R8=object+0x30.");

        WriteLine(file, "Verified call [r9+0x28] at RVA=0x25AF78E: slot 5 of the returned device; concrete target unobserved.");
        InspectDispatchOwner(file, base, nt, ntBase);
    }

    void InspectExecutable(HANDLE file)
    {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        IMAGE_DOS_HEADER dos = {};
        if (!base || !ReadBytes(base, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x10000)
        {
            WriteLine(file, "Invalid DOS header; no inspection.");
            return;
        }
        const std::uintptr_t ntBase = base + static_cast<SIZE_T>(dos.e_lfanew);
        IMAGE_NT_HEADERS64 nt = {};
        if (!ReadBytes(ntBase, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 32)
        {
            WriteLine(file, "Invalid NT header; no inspection.");
            return;
        }
        char line[192] = {};
        std::snprintf(line, sizeof(line), "Executable: image_size=0x%X timestamp=0x%08X checksum=0x%08X",
            static_cast<unsigned int>(nt.OptionalHeader.SizeOfImage),
            static_cast<unsigned int>(nt.FileHeader.TimeDateStamp),
            static_cast<unsigned int>(nt.OptionalHeader.CheckSum));
        WriteLine(file, line);
        if (nt.OptionalHeader.SizeOfImage != knownImageSize ||
            nt.FileHeader.TimeDateStamp != knownTimestamp || nt.OptionalHeader.CheckSum != knownChecksum)
        {
            WriteLine(file, "Executable version differs from validated log; fixed-RVA analysis skipped.");
            return;
        }
        const Pattern patterns[] = {
            { "fiDevice_OpenBulk", openBulkRva, openBytes, openMask, sizeof(openBytes) },
            { "fiDevice_GetDevice", getDeviceRva, deviceBytes, deviceMask, sizeof(deviceBytes) },
            { "fiPackfile_ParseHeader_callsite", parseCallsiteRva, callBytes, callMask, sizeof(callBytes) }
        };
        bool passed = true;
        for (const Pattern& pattern : patterns)
        {
            if (!InExecutableSection(pattern.rva, nt, ntBase) || !Validate(file, base, pattern))
                passed = false;
        }
        if (!passed)
        {
            WriteLine(file, "One or more expected code signatures changed; analysis safely stopped.");
            return;
        }
        HexLine(file, "OpenBulk_prologue", base, openBulkRva, 48);
        HexLine(file, "GetDevice_prologue", base, getDeviceRva, 48);
        HexLine(file, "ParseHeader_callsite_context", base, parseCallsiteRva - 16, 48);
        CallTarget(file, "ParseHeader_call", base, parseCallsiteRva, 12, nt, ntBase);
        CallTarget(file, "OpenBulk_inner_call", base, openBulkRva, 25, nt, ntBase);
        InspectResourceDispatch(file, base, nt, ntBase);
        WriteLine(file, "Read-only validation only. No loader calls, path observations, hooks, or writes.");
    }
}

namespace Main::CameraSmoothing
{
    void InitializeEarly(bool enabled)
    {
        diagnostics = enabled && Logger::IsDebugEnabled();
        if (!enabled)
            return;
        Logger::Log("[WARNING] DisableCameraSmoothing: no verified camera modifier removal; native behavior retained.");
        if (!diagnostics)
            return;
        earlyTick = GetTickCount64();
        HANDLE file = OpenLog(true);
        if (file == INVALID_HANDLE_VALUE)
            return;
        WriteLine(file, "Camera resource table-reference diagnostic v4; executable-section references, no hooks or writes.");
        char line[128] = {};
        std::snprintf(line, sizeof(line), "Phase EARLY_INIT: tick_ms=%llu",
            static_cast<unsigned long long>(earlyTick));
        WriteLine(file, line);
        InspectExecutable(file);
        CloseHandle(file);
    }

    void Initialize(bool enabled)
    {
        if (!enabled || !diagnostics)
            return;
        HANDLE file = OpenLog(false);
        if (file == INVALID_HANDLE_VALUE)
            return;
        const ULONGLONG now = GetTickCount64();
        char line[160] = {};
        std::snprintf(line, sizeof(line), "Phase SCRIPT_INIT: tick_ms=%llu elapsed_since_early_ms=%llu",
            static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - earlyTick));
        WriteLine(file, line);
        CloseHandle(file);
    }
}
