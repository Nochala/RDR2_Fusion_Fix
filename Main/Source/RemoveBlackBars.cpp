#include "RemoveBlackBars.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace
{
    bool MatchesLetterboxInstruction(const std::uint8_t* bytes)
    {
        return bytes[0] == 0xC6 && bytes[1] == 0x05 &&
            (bytes[6] == 0x00 || bytes[6] == 0x01) &&
            bytes[7] == 0x77 && bytes[8] == 0x07 &&
            bytes[9] == 0x40 && bytes[10] == 0x88 && bytes[11] == 0x3D;
    }

    std::uint8_t* FindLetterboxInstruction()
    {
        const auto* image = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!image)
            return nullptr;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew > 4096)
            return nullptr;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
            return nullptr;

        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        std::uint8_t* candidate = nullptr;
        unsigned matches = 0;

        for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index)
        {
            const auto& section = sections[index];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const std::size_t offset = section.VirtualAddress;
            const std::size_t size = section.Misc.VirtualSize;
            if (offset >= imageSize || size < 16 || size > imageSize - offset)
                continue;

            const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(image) + offset + size;
            std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(image) + offset;
            while (cursor < end)
            {
                MEMORY_BASIC_INFORMATION region = {};
                if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &region, sizeof(region)) != sizeof(region))
                    break;

                const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
                if (regionEnd <= cursor)
                    break;

                const std::uintptr_t scanEnd = regionEnd < end ? regionEnd : end;
                const DWORD protection = region.Protect & 0xFF;
                const bool readable = protection == PAGE_EXECUTE_READ ||
                    protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY ||
                    protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY;
                if (region.State == MEM_COMMIT && (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 && readable &&
                    scanEnd - cursor >= 16)
                {
                    for (std::uintptr_t position = cursor; position <= scanEnd - 16; ++position)
                    {
                        if (!MatchesLetterboxInstruction(reinterpret_cast<const std::uint8_t*>(position)))
                            continue;

                        candidate = reinterpret_cast<std::uint8_t*>(position);
                        if (++matches > 1)
                            return nullptr;
                    }
                }
                cursor = scanEnd;
            }
        }

        return matches == 1 ? candidate : nullptr;
    }
}

namespace Main::RemoveBlackBars
{
    void Initialize(bool enabled)
    {
        if (!enabled)
            return;

        std::uint8_t* instruction = FindLetterboxInstruction();
        if (!instruction)
        {
            Logger::Log("[WARNING] RemoveBlackBars: unique supported game-code signature not found; no changes made.");
            return;
        }

        std::uint8_t* immediate = instruction + 6;
        if (*immediate == 0x00)
        {
            Logger::Log("[INFO] RemoveBlackBars: cinematic letterbox suppression already active.");
            return;
        }

        DWORD oldProtection = 0;
        if (!VirtualProtect(immediate, 1, PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            Logger::Log("[WARNING] RemoveBlackBars: unable to change code protection; no changes made.");
            return;
        }

        *immediate = 0x00;
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), immediate, 1) != FALSE;
        DWORD ignored = 0;
        const bool restored = VirtualProtect(immediate, 1, oldProtection, &ignored) != FALSE;
        if (*immediate == 0x00 && flushed && restored)
            Logger::Log("[SUCCESS] RemoveBlackBars: cinematic letterbox draw flag suppressed.");
        else
            Logger::Log("[WARNING] RemoveBlackBars: code patch applied, but verification or page protection restoration failed.");
    }
}
