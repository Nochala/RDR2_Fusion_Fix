#include "SkipIntro.h"
#include "../../Headers/Logger.h"
#include <windows.h>
#include <string>
#include <vector>
#include <cstring>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace
{
    const char startupData[] = R"GFSTART(<?xml version="1.0" encoding="UTF-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
 <disabledFiles />
 <includedXmlFiles itemType="CDataFileMgr__DataFileArray" />
 <includedDataFiles />
 <dataFiles itemType="CDataFileMgr__DataFile">
  <Item>
   <filename>platform:/data/cdimages/scaleform_platform_pc.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/value_conversion.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/widgets.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_photo_stickers.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_platform.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/stylesCatalog</filename>
   <fileType>aWeaponizeDisputants</fileType>
  </Item>
  <Item>
   <filename>platform:/data/cdimages/scaleform_frontend.rpf</filename>
   <fileType>RPF_FILE_PRE_INSTALL</fileType>
  </Item>
  <Item>
   <filename>platform:/textures/ui/ui_startup_textures.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/data/ui/startup_data.rpf</filename>
   <fileType>RPF_FILE</fileType>
  </Item>
  <Item>
   <filename>platform:/GameplayFixesRDR2_data/boot_launcher_flow.ymt</filename>
   <fileType>STREAMING_FILE</fileType>
   <registerAs>boot_flow/boot_launcher_flow</registerAs>
   <overlay value="false"/>
   <patchFile value="false"/>
  </Item>
 </dataFiles>
 <contentChangeSets itemType="CDataFileMgr__ContentChangeSet" />
 <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
)GFSTART";
    const char bootFlowData[] = R"GFBOOT(<?xml version="1.0" encoding="UTF-8"?>
<rage__fwuiFlowBlock>
 <ID>boot_flow</ID>
 <EntryPoints>
  <Item>
   <ID>default_entry</ID>
   <Target>wait_for_profile</Target>
  </Item>
  <Item>
   <ID>sign_out</ID>
   <Target>wait_for_profile</Target>
  </Item>
 </EntryPoints>
 <ExitPoints>
  <Item>
   <ID>exit</ID>
  </Item>
 </ExitPoints>
 <FlowRoot>
  <ID>input_context_switch</ID>
  <State type="StateSetInputContext">
   <ContextType>BOOT_FLOW</ContextType>
  </State>
  <Children>
   <Item>
    <ID>wait_for_profile</ID>
    <State type="StateWaitForProfileLoad" />
    <LinkMap>
     <Item key="next"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
     <Item key="exit"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
     <Item key="yes"><Target>exit</Target><LinkInfo>LINK_TO_EXTERNAL</LinkInfo></Item>
    </LinkMap>
   </Item>
  </Children>
 </FlowRoot>
</rage__fwuiFlowBlock>
)GFBOOT";

    std::wstring GameRoot()
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        std::wstring root(path, length);
        const auto separator = root.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};

        root.resize(separator + 1);
        return root;
    }

    bool Exists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    bool ReadExact(const std::wstring& path, const char* expected, size_t length)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER fileSize = {};
        bool matching = false;
        if (GetFileSizeEx(file, &fileSize) && fileSize.QuadPart == static_cast<LONGLONG>(length))
        {
            std::vector<char> contents(length);
            DWORD read = 0;
            matching = ReadFile(file, contents.data(), static_cast<DWORD>(contents.size()), &read, nullptr) &&
                read == length && memcmp(contents.data(), expected, length) == 0;
        }
        CloseHandle(file);
        return matching;
    }

    bool RemoveOwned(const std::wstring& path, const char* data, size_t length)
    {
        if (!Exists(path))
            return true;
        return ReadExact(path, data, length) && DeleteFileW(path.c_str());
    }

    using BinkOpenWithOptionsFn = void* (*)(const char*, const void*, unsigned int);

    BinkOpenWithOptionsFn originalBinkOpen = nullptr;
    std::atomic<unsigned int> binkOpensObserved{ 0 };
    std::atomic<ULONGLONG> splashHookStartedAt{ 0 };

    bool ReadableRegion(const void* pointer, size_t required, size_t* available = nullptr)
    {
        if (pointer == nullptr)
            return false;

        MEMORY_BASIC_INFORMATION information = {};
        if (VirtualQuery(pointer, &information, sizeof(information)) != sizeof(information) ||
            information.State != MEM_COMMIT || (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        const DWORD protection = information.Protect & 0xFF;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
            protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return false;

        const uintptr_t start = reinterpret_cast<uintptr_t>(pointer);
        const uintptr_t regionStart = reinterpret_cast<uintptr_t>(information.BaseAddress);
        if (start < regionStart || start - regionStart >= information.RegionSize)
            return false;

        const size_t remaining = information.RegionSize - (start - regionStart);
        if (available != nullptr)
            *available = remaining;
        return remaining >= required;
    }

    bool ReadMovieName(const char* name, std::string& movie)
    {
        size_t available = 0;
        if (!ReadableRegion(name, 1, &available))
            return false;

        const size_t limit = (std::min)(available, static_cast<size_t>(384));
        const char* terminator = static_cast<const char*>(std::memchr(name, 0, limit));
        if (terminator == nullptr || terminator == name)
            return false;

        for (const char* current = name; current != terminator; ++current)
        {
            if (static_cast<unsigned char>(*current) < 32 || static_cast<unsigned char>(*current) > 126)
                return false;
        }

        movie.assign(name, terminator);
        std::transform(movie.begin(), movie.end(), movie.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        return true;
    }

    struct MemoryMovieHeader
    {
        char magic[4];
        unsigned int fileSizeMinus8;
        unsigned int frames;
        unsigned int largestFrameSize;
        unsigned int internalFrames;
        unsigned int width;
        unsigned int height;
        unsigned int frameRate;
        unsigned int frameRateDiv;
    };

    bool ReadMemoryMovieHeader(const char* name, unsigned int flags, MemoryMovieHeader& header)
    {
        if ((flags & 0x04000000u) == 0 || !ReadableRegion(name, sizeof(header)))
            return false;

        std::memcpy(&header, name, sizeof(header));
        if (std::memcmp(header.magic, "KB2", 3) != 0 ||
            header.magic[3] < 'a' || header.magic[3] > 'z' ||
            header.fileSizeMinus8 < 4096 || header.fileSizeMinus8 > 0x40000000u ||
            header.frames < 2 || header.frames > 1800 ||
            header.width < 320 || header.width > 7680 ||
            header.height < 180 || header.height > 4320 ||
            header.frameRateDiv == 0 ||
            (static_cast<double>(header.frameRate) / header.frameRateDiv) < 10.0 ||
            (static_cast<double>(header.frameRate) / header.frameRateDiv) > 120.0)
            return false;
        return true;
    }

    void* BinkOpenHook(const char* name, const void* options, unsigned int flags)
    {
        std::string movie;
        const bool hasMovieName = (flags & 0x04000000u) == 0 && ReadMovieName(name, movie);
        MemoryMovieHeader sourceHeader = {};
        const bool hasMemoryHeader = ReadMemoryMovieHeader(name, flags, sourceHeader);
        void* handle = originalBinkOpen(name, options, flags);

        const unsigned int openIndex = binkOpensObserved.fetch_add(1);
        const ULONGLONG installTime = splashHookStartedAt.load();
        const ULONGLONG now = GetTickCount64();
        const bool withinStartup = installTime != 0 && now >= installTime && now - installTime <= 30000;
        if (Logger::IsDebugEnabled() && openIndex < 6)
        {
            std::ostringstream line;
            line << "SkipIntroSplashes: Bink open #" << (openIndex + 1)
                << ", source=" << (hasMovieName ? movie.substr(0, 140) :
                    (hasMemoryHeader ? "Bink2 memory" : "memory or unrecognized"))
                << ", flags=0x" << std::uppercase << std::hex << flags << std::dec
                << ", handle=" << (handle != nullptr ? "valid" : "null")
                << ", startupWindow=" << (withinStartup ? "yes" : "no");
            if (hasMemoryHeader)
                line << ", sourceVideo=" << sourceHeader.width << "x" << sourceHeader.height
                    << ", sourceFrames=" << sourceHeader.frames;
            else if ((flags & 0x04000000u) != 0 && ReadableRegion(name, 16))
            {
                unsigned char bytes[16] = {};
                std::memcpy(bytes, name, sizeof(bytes));
                line << ", sourceFirst16=";
                for (size_t index = 0; index < sizeof(bytes); ++index)
                    line << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
                        << static_cast<unsigned int>(bytes[index]);
                line << std::dec;
            }
            line << ".";
            Logger::Debug(line.str().c_str());
        }

        return handle;
    }

    bool InstallSplashHook()
    {
        auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        HMODULE bink = GetModuleHandleW(L"bink2w64.dll");
        if (base == nullptr || bink == nullptr)
        {
            Logger::Log("[WARNING] SkipIntroSplashes: executable or Bink video library unavailable.");
            return false;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return false;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
            return false;

        const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
        const auto& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imports.VirtualAddress == 0 || imports.VirtualAddress >= imageSize ||
            imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
            imports.Size > imageSize - imports.VirtualAddress)
            return false;

        const auto* descriptors = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress);
        const size_t descriptorCount = imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
        IMAGE_THUNK_DATA64* slot = nullptr;
        for (size_t i = 0; i < descriptorCount && descriptors[i].Name != 0; ++i)
        {
            const auto& descriptor = descriptors[i];
            if (descriptor.Name >= imageSize ||
                _stricmp(reinterpret_cast<const char*>(base + descriptor.Name), "bink2w64.dll") != 0)
                continue;

            if (descriptor.OriginalFirstThunk == 0 || descriptor.FirstThunk == 0 ||
                descriptor.OriginalFirstThunk >= imageSize || descriptor.FirstThunk >= imageSize)
                break;

            const size_t count = (std::min)(static_cast<size_t>((imageSize - descriptor.OriginalFirstThunk) / sizeof(IMAGE_THUNK_DATA64)),
                static_cast<size_t>((imageSize - descriptor.FirstThunk) / sizeof(IMAGE_THUNK_DATA64)));
            const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + descriptor.OriginalFirstThunk);
            auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor.FirstThunk);
            for (size_t j = 0; j < count && names[j].u1.AddressOfData != 0; ++j)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names[j].u1.Ordinal))
                    continue;

                const uintptr_t nameRva = names[j].u1.AddressOfData;
                if (nameRva >= imageSize || imageSize - nameRva < sizeof(IMAGE_IMPORT_BY_NAME))
                    continue;

                const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + nameRva);
                if (std::strcmp(reinterpret_cast<const char*>(imported->Name), "BinkOpenWithOptions") == 0)
                {
                    slot = &addresses[j];
                    break;
                }
            }
            break;
        }

        if (slot == nullptr || slot->u1.Function == 0)
        {
            Logger::Log("[WARNING] SkipIntroSplashes: BinkOpenWithOptions import was not found; game left unchanged.");
            return false;
        }

        const auto saved = reinterpret_cast<BinkOpenWithOptionsFn>(slot->u1.Function);
        DWORD oldProtection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtection))
        {
            Logger::Log("[WARNING] SkipIntroSplashes: could not make Bink import writable.");
            return false;
        }

        originalBinkOpen = saved;
        splashHookStartedAt.store(GetTickCount64());
        const void* replaced = InterlockedCompareExchangePointer(
            reinterpret_cast<PVOID volatile*>(&slot->u1.Function),
            reinterpret_cast<PVOID>(&BinkOpenHook),
            reinterpret_cast<PVOID>(saved));
        DWORD restoredProtection = 0;
        const bool restored = VirtualProtect(slot, sizeof(*slot), oldProtection, &restoredProtection) != FALSE;
        if (replaced != reinterpret_cast<const void*>(saved))
        {
            originalBinkOpen = nullptr;
            Logger::Log("[WARNING] SkipIntroSplashes: another module changed the Bink import; interception refused.");
            return false;
        }

        Logger::Log(restored ?
            "[SUCCESS] SkipIntroSplashes: Bink interception installed in diagnostic-only mode; premature seek disabled to prevent startup crash." :
            "[WARNING] SkipIntroSplashes: interception installed, but import page protection could not be restored.");
        return true;
    }

    constexpr unsigned char parserSignature[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10,
        0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x41, 0x56,
        0x48, 0x83, 0xEC, 0x20, 0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xDA,
        0x4C, 0x8B, 0x41, 0x08
    };

    constexpr size_t overwrittenLength = 15;
    constexpr size_t absoluteJumpLength = 14;
    static_assert(overwrittenLength >= absoluteJumpLength, "Hook branch exceeds the overwritten instruction boundary");
    static_assert(overwrittenLength <= sizeof(parserSignature), "Hook overwrite exceeds the verified signature");
    using ParseFlowBlockFn = void(*)(void*, void*, const char*);
    ParseFlowBlockFn originalParser = nullptr;
    std::atomic<bool> legalHit{ false };

    struct FlowKeys
    {
        void* unused[9];
        const char* key;
    };

    static_assert(offsetof(FlowKeys, key) == 72, "Unexpected parser key offset");

    void RedirectFlowBlock(void* first, void* second, const char* third)
    {
        if (second != nullptr)
        {
            auto* keys = static_cast<FlowKeys*>(second);
            if (keys->key != nullptr &&
                std::strcmp(keys->key,
                    "feedback_context_switch.boot_screen_host.sign_in_flow_activity_sentinel.account_picker_activity_sentinel.account_picker_wrapper") == 0)
            {
                keys->key = "feedback_context_switch.boot_screen_host.profile_flow_activity_sentinel.wait_for_profile";
                if (!legalHit.exchange(true))
                    Logger::Log("[SUCCESS] SkipLegal: account-picker flow redirected at runtime.");
            }
        }
        originalParser(first, second, third);
    }

    void MakeAbsoluteJump(unsigned char (&code)[absoluteJumpLength], const void* destination)
    {
        code[0] = 0xFF;
        code[1] = 0x25;
        std::memset(code + 2, 0, 4);
        const uintptr_t address = reinterpret_cast<uintptr_t>(destination);
        std::memcpy(code + 6, &address, sizeof(address));
    }

    constexpr size_t diagnosticMatchLimit = 6;

    struct SignatureMatch
    {
        const unsigned char* address = nullptr;
        size_t available = 0;
    };

    struct SignatureResult
    {
        unsigned char* address = nullptr;
        size_t matches = 0;
        bool limited = false;
        SignatureMatch candidates[diagnosticMatchLimit] = {};
    };

    void LogExecutableDiagnostics()
    {
        if (!Logger::IsDebugEnabled())
            return;

        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        if (base == nullptr)
        {
            Logger::Debug("Executable: main module unavailable.");
            return;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        {
            Logger::Debug("Executable: unexpected DOS header.");
            return;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            Logger::Debug("Executable: unexpected PE header.");
            return;
        }

        std::ostringstream line;
        line << "Executable: PE timestamp=0x" << std::uppercase << std::hex << nt->FileHeader.TimeDateStamp
            << ", image size=0x" << nt->OptionalHeader.SizeOfImage
            << ", checksum=0x" << nt->OptionalHeader.CheckSum << ".";
        Logger::Debug(line.str().c_str());
    }

    SignatureResult FindExecutableSignature(const unsigned char* pattern, const char* mask, size_t patternLength)
    {
        SignatureResult result;
        auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
        if (base == nullptr || pattern == nullptr || patternLength == 0)
            return result;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return result;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0 || nt->FileHeader.NumberOfSections == 0)
            return result;

        size_t anchor = 0;
        if (mask != nullptr)
        {
            while (anchor < patternLength && mask[anchor] == '?')
                ++anchor;
            if (anchor == patternLength)
                return result;
        }

        const size_t matchLimit = Logger::IsDebugEnabled() ? diagnosticMatchLimit : 2;
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if ((sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const size_t offset = sections[i].VirtualAddress;
            const size_t length = sections[i].Misc.VirtualSize;
            if (offset >= nt->OptionalHeader.SizeOfImage ||
                length > nt->OptionalHeader.SizeOfImage - offset || length < patternLength)
                continue;

            const unsigned char* begin = base + offset;
            for (size_t j = 0; j <= length - patternLength; ++j)
            {
                if (begin[j + anchor] != pattern[anchor])
                    continue;

                bool matching = true;
                if (mask == nullptr)
                    matching = std::memcmp(begin + j, pattern, patternLength) == 0;
                else
                {
                    for (size_t k = 0; k < patternLength; ++k)
                    {
                        if (mask[k] != '?' && begin[j + k] != pattern[k])
                        {
                            matching = false;
                            break;
                        }
                    }
                }

                if (!matching)
                    continue;

                result.candidates[result.matches] = { begin + j, length - j };
                ++result.matches;
                result.address = result.matches == 1 ? const_cast<unsigned char*>(begin + j) : nullptr;
                if (result.matches == matchLimit)
                {
                    result.limited = true;
                    return result;
                }
            }
        }
        return result;
    }

    void LogSignatureDiagnostics(const char* label, const SignatureResult& result, size_t previewBytes = 32)
    {
        if (!Logger::IsDebugEnabled())
            return;

        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        std::ostringstream line;
        line << "SkipLegal: " << label << " candidates=" << result.matches;
        if (result.limited)
            line << " (at least; scan limit reached)";
        Logger::Debug(line.str().c_str());

        for (size_t i = 0; i < result.matches; ++i)
        {
            const auto& candidate = result.candidates[i];
            std::ostringstream candidateLine;
            candidateLine << "SkipLegal: " << label << " candidate " << (i + 1)
                << " RVA=0x" << std::uppercase << std::hex
                << (reinterpret_cast<uintptr_t>(candidate.address) - reinterpret_cast<uintptr_t>(base)) << " bytes=";
            const size_t byteCount = (std::min)(candidate.available, previewBytes);
            for (size_t j = 0; j < byteCount; ++j)
            {
                if (j != 0)
                    candidateLine << ' ';
                candidateLine << std::setfill('0') << std::setw(2)
                    << static_cast<unsigned int>(candidate.address[j]);
            }
            Logger::Debug(candidateLine.str().c_str());
        }
    }

    void LogParserCandidateComparison(const SignatureResult& result)
    {
        if (!Logger::IsDebugEnabled() || result.matches != 2 || result.limited)
            return;

        const SignatureMatch& first = result.candidates[0];
        const SignatureMatch& second = result.candidates[1];
        const size_t length = (std::min)((std::min)(first.available, second.available), static_cast<size_t>(192));
        size_t difference = 0;
        while (difference < length && first.address[difference] == second.address[difference])
            ++difference;

        std::ostringstream line;
        line << "SkipLegal: full prologue candidate comparison: ";
        if (difference == length)
            line << "first " << length << " bytes are identical; neither candidate is trusted.";
        else
            line << "first difference at +0x" << std::uppercase << std::hex << difference
                << " (candidate 1=0x" << static_cast<unsigned int>(first.address[difference])
                << ", candidate 2=0x" << static_cast<unsigned int>(second.address[difference])
                << "); neither candidate is trusted.";
        Logger::Debug(line.str().c_str());
    }

    void LogParserCandidateReferences(const SignatureResult& result)
    {
        if (!Logger::IsDebugEnabled() || result.matches != 2 || result.limited)
            return;

        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        if (base == nullptr)
            return;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0 || nt->FileHeader.NumberOfSections == 0)
            return;

        struct RuntimeFunctionRecord
        {
            DWORD beginAddress;
            DWORD endAddress;
            DWORD unwindAddress;
        };
        static_assert(sizeof(RuntimeFunctionRecord) == 12, "Unexpected runtime function record size");

        const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        const bool validDirectory = nt->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION &&
            directory.VirtualAddress < imageSize &&
            directory.Size <= imageSize - directory.VirtualAddress &&
            directory.Size >= sizeof(RuntimeFunctionRecord);

        if (validDirectory)
        {
            const auto* records = reinterpret_cast<const RuntimeFunctionRecord*>(base + directory.VirtualAddress);
            const size_t recordCount = directory.Size / sizeof(RuntimeFunctionRecord);
            for (size_t candidateIndex = 0; candidateIndex < result.matches; ++candidateIndex)
            {
                const uintptr_t candidateRva = reinterpret_cast<uintptr_t>(result.candidates[candidateIndex].address) -
                    reinterpret_cast<uintptr_t>(base);
                const RuntimeFunctionRecord* matchingRecord = nullptr;
                for (size_t i = 0; i < recordCount; ++i)
                {
                    if (records[i].beginAddress <= candidateRva && candidateRva < records[i].endAddress &&
                        records[i].endAddress <= imageSize)
                    {
                        matchingRecord = &records[i];
                        break;
                    }
                }

                std::ostringstream line;
                line << "SkipLegal: candidate " << (candidateIndex + 1) << " unwind metadata: ";
                if (matchingRecord != nullptr)
                {
                    line << "function=[0x" << std::uppercase << std::hex << matchingRecord->beginAddress
                        << ", 0x" << matchingRecord->endAddress << "), size=" << std::dec
                        << (matchingRecord->endAddress - matchingRecord->beginAddress)
                        << ", entry=" << (matchingRecord->beginAddress == candidateRva ? "yes" : "no")
                        << ", unwind RVA=0x" << std::uppercase << std::hex << matchingRecord->unwindAddress << ".";
                }
                else
                    line << "no matching function record.";
                Logger::Debug(line.str().c_str());
            }
        }
        else
            Logger::Debug("SkipLegal: exception directory unavailable; function-boundary diagnostics skipped.");

        constexpr size_t referenceSampleLimit = 6;
        size_t referenceCounts[2] = {};
        uintptr_t referenceSamples[2][referenceSampleLimit] = {};
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned int sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections; ++sectionIndex)
        {
            if ((sections[sectionIndex].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const size_t offset = sections[sectionIndex].VirtualAddress;
            const size_t length = sections[sectionIndex].Misc.VirtualSize;
            if (offset >= imageSize || length > imageSize - offset || length < 5)
                continue;

            const unsigned char* bytes = base + offset;
            for (size_t i = 0; i <= length - 5; ++i)
            {
                if (bytes[i] != 0xE8 && bytes[i] != 0xE9)
                    continue;

                int32_t displacement = 0;
                std::memcpy(&displacement, bytes + i + 1, sizeof(displacement));
                const uintptr_t destination = reinterpret_cast<uintptr_t>(bytes + i + 5) +
                    static_cast<intptr_t>(displacement);
                for (size_t candidateIndex = 0; candidateIndex < result.matches; ++candidateIndex)
                {
                    if (destination != reinterpret_cast<uintptr_t>(result.candidates[candidateIndex].address))
                        continue;
                    if (referenceCounts[candidateIndex] < referenceSampleLimit)
                        referenceSamples[candidateIndex][referenceCounts[candidateIndex]] = offset + i;
                    ++referenceCounts[candidateIndex];
                }
            }
        }

        for (size_t candidateIndex = 0; candidateIndex < result.matches; ++candidateIndex)
        {
            std::ostringstream line;
            line << "SkipLegal: candidate " << (candidateIndex + 1)
                << " possible E8/E9 relative references=" << referenceCounts[candidateIndex];
            const size_t samples = (std::min)(referenceCounts[candidateIndex], referenceSampleLimit);
            if (samples != 0)
            {
                line << ", site RVAs=";
                for (size_t i = 0; i < samples; ++i)
                    line << (i == 0 ? "0x" : ", 0x") << std::uppercase << std::hex << referenceSamples[candidateIndex][i];
            }
            line << "; references are unverified byte patterns, not confirmed callers.";
            Logger::Debug(line.str().c_str());
        }
    }

    void LogParserCandidatePointerTables(const SignatureResult& result)
    {
        if (!Logger::IsDebugEnabled() || result.matches != 2 || result.limited)
            return;

        const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
        if (base == nullptr)
            return;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
            return;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0 || nt->FileHeader.NumberOfSections == 0)
            return;

        constexpr size_t sampleLimit = 4;
        static_assert(sizeof(uintptr_t) == 8, "Parser pointer diagnostics require an x64 build");
        const size_t imageSize = nt->OptionalHeader.SizeOfImage;
        const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(base);
        const uintptr_t candidates[2] = {
            reinterpret_cast<uintptr_t>(result.candidates[0].address),
            reinterpret_cast<uintptr_t>(result.candidates[1].address)
        };
        size_t counts[2] = {};
        size_t firstTableRvas[2] = {};
        const auto* sections = IMAGE_FIRST_SECTION(nt);

        for (unsigned int sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections; ++sectionIndex)
        {
            const auto& section = sections[sectionIndex];
            if ((section.Characteristics & IMAGE_SCN_MEM_READ) == 0 ||
                (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
                continue;

            const size_t offset = section.VirtualAddress;
            const size_t length = (std::min)(static_cast<size_t>(section.Misc.VirtualSize),
                static_cast<size_t>(section.SizeOfRawData));
            if (offset >= imageSize || length > imageSize - offset || length < sizeof(uintptr_t))
                continue;

            const auto* data = base + offset;
            for (size_t i = 0; i <= length - sizeof(uintptr_t); i += sizeof(uintptr_t))
            {
                uintptr_t value = 0;
                std::memcpy(&value, data + i, sizeof(value));
                for (size_t candidateIndex = 0; candidateIndex < 2; ++candidateIndex)
                {
                    if (value != candidates[candidateIndex])
                        continue;

                    ++counts[candidateIndex];
                    if (counts[candidateIndex] == 1)
                        firstTableRvas[candidateIndex] = offset + i;
                    if (counts[candidateIndex] > sampleLimit)
                        continue;

                    char sectionName[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
                    std::memcpy(sectionName, section.Name, IMAGE_SIZEOF_SHORT_NAME);
                    std::ostringstream line;
                    line << "SkipLegal: candidate " << (candidateIndex + 1)
                        << " pointer table reference in " << sectionName
                        << " at RVA=0x" << std::uppercase << std::hex << (offset + i)
                        << ", adjacent entries=";

                    const size_t first = i >= 2 * sizeof(uintptr_t) ? i - 2 * sizeof(uintptr_t) : i;
                    const size_t last = (std::min)(length - sizeof(uintptr_t), i + 2 * sizeof(uintptr_t));
                    for (size_t entry = first; entry <= last; entry += sizeof(uintptr_t))
                    {
                        uintptr_t pointer = 0;
                        std::memcpy(&pointer, data + entry, sizeof(pointer));
                        if (entry != first)
                            line << ", ";
                        line << "0x" << (offset + entry) << "=>";
                        if (pointer >= moduleBase && pointer - moduleBase < imageSize)
                            line << "RVA 0x" << (pointer - moduleBase);
                        else
                            line << "non-image";
                    }
                    Logger::Debug(line.str().c_str());
                }
            }
        }

        for (size_t candidateIndex = 0; candidateIndex < 2; ++candidateIndex)
        {
            std::ostringstream line;
            line << "SkipLegal: candidate " << (candidateIndex + 1)
                << " aligned data-pointer references=" << counts[candidateIndex]
                << "; table entries are diagnostic only and do not identify the legal-flow parser.";
            Logger::Debug(line.str().c_str());
        }

        if (counts[0] != 1 || counts[1] != 1)
            return;

        const size_t firstRva = (std::min)(firstTableRvas[0], firstTableRvas[1]);
        const size_t lastRva = (std::max)(firstTableRvas[0], firstTableRvas[1]);
        if (lastRva - firstRva != sizeof(uintptr_t))
        {
            Logger::Debug("SkipLegal: pointer entries are not adjacent; code-reference diagnostics skipped.");
            return;
        }

        constexpr size_t neighborhood = 0x180;
        constexpr size_t referenceLimit = 8;
        const size_t rangeBegin = firstRva > neighborhood ? firstRva - neighborhood : 0;
        const size_t rangeEnd = (std::min)(imageSize, lastRva + neighborhood + sizeof(uintptr_t));
        size_t referenceCount = 0;
        size_t exactReferences = 0;

        for (unsigned int sectionIndex = 0; sectionIndex < nt->FileHeader.NumberOfSections; ++sectionIndex)
        {
            const auto& section = sections[sectionIndex];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
                continue;

            const size_t offset = section.VirtualAddress;
            const size_t length = section.Misc.VirtualSize;
            if (offset >= imageSize || length > imageSize - offset || length < 7)
                continue;

            const auto* bytes = base + offset;
            for (size_t i = 0; i <= length - 7; ++i)
            {
                if ((bytes[i] & 0xF8) != 0x48 ||
                    (bytes[i + 1] != 0x8D && bytes[i + 1] != 0x8B) ||
                    (bytes[i + 2] & 0xC7) != 0x05)
                    continue;

                int32_t displacement = 0;
                std::memcpy(&displacement, bytes + i + 3, sizeof(displacement));
                const int64_t destination = static_cast<int64_t>(offset) +
                    static_cast<int64_t>(i) + 7 + displacement;
                if (destination < static_cast<int64_t>(rangeBegin) ||
                    destination >= static_cast<int64_t>(rangeEnd))
                    continue;

                ++referenceCount;
                const bool exact = destination == static_cast<int64_t>(firstTableRvas[0]) ||
                    destination == static_cast<int64_t>(firstTableRvas[1]);
                if (exact)
                    ++exactReferences;
                if (referenceCount > referenceLimit)
                    continue;

                std::ostringstream line;
                line << "SkipLegal: possible " << (bytes[i + 1] == 0x8D ? "LEA" : "MOV")
                    << " RIP-relative table reference: instruction RVA=0x"
                    << std::uppercase << std::hex << (offset + i)
                    << ", target RVA=0x" << static_cast<uint64_t>(destination)
                    << ", exact candidate entry=" << (exact ? "yes" : "no") << ".";
                Logger::Debug(line.str().c_str());
            }
        }

        std::ostringstream line;
        line << "SkipLegal: possible RIP-relative references near candidate pointer table="
            << referenceCount << ", exact entry references=" << exactReferences
            << "; raw byte-pattern diagnostics only, not verified instructions or callers.";
        Logger::Debug(line.str().c_str());
    }

    unsigned char* FindParser()
    {
        LogExecutableDiagnostics();
        const SignatureResult strict = FindExecutableSignature(parserSignature, nullptr, sizeof(parserSignature));
        LogSignatureDiagnostics("full prologue", strict, 192);
        LogParserCandidateComparison(strict);
        LogParserCandidateReferences(strict);
        LogParserCandidatePointerTables(strict);
        if (strict.matches == 1 && !strict.limited)
            return strict.address;

        constexpr unsigned char commonPrologue[] = {
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10,
            0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20
        };
        const SignatureResult relaxed = FindExecutableSignature(commonPrologue, nullptr, sizeof(commonPrologue));
        LogSignatureDiagnostics("common prologue (unverified)", relaxed);
        const std::string message = std::string("[INFO] SkipLegal: full prologue matches=") +
            std::to_string(strict.matches) + (strict.limited ? " (at least)" : "") +
            ", common prologue matches=" + std::to_string(relaxed.matches) + (relaxed.limited ? " (at least)" : "") +
            "; multiple or unverified candidates are never hooked.";
        Logger::Log(message.c_str());
        return nullptr;
    }

    bool InstallFlowHook()
    {
        unsigned char* target = FindParser();
        if (target == nullptr)
        {
            Logger::Log("[WARNING] SkipLegal: unique compatible flow-parser signature not found; memory remains unchanged.");
            return false;
        }

        if (std::memcmp(target, parserSignature, sizeof(parserSignature)) != 0)
        {
            Logger::Log("[WARNING] SkipLegal: parser bytes changed after resolution; memory remains unchanged.");
            return false;
        }

        auto* gateway = static_cast<unsigned char*>(VirtualAlloc(nullptr,
            overwrittenLength + absoluteJumpLength, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (gateway == nullptr)
        {
            Logger::Log("[ERROR] SkipLegal: unable to allocate trampoline; memory remains unchanged.");
            return false;
        }
        std::memcpy(gateway, parserSignature, overwrittenLength);
        unsigned char backJump[absoluteJumpLength] = {};
        MakeAbsoluteJump(backJump, target + overwrittenLength);
        std::memcpy(gateway + overwrittenLength, backJump, sizeof(backJump));
        DWORD gatewayProtection = 0;
        if (!VirtualProtect(gateway, overwrittenLength + absoluteJumpLength,
            PAGE_EXECUTE_READ, &gatewayProtection))
        {
            VirtualFree(gateway, 0, MEM_RELEASE);
            Logger::Log("[ERROR] SkipLegal: could not protect trampoline; memory remains unchanged.");
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), gateway, overwrittenLength + absoluteJumpLength);

        unsigned char branch[overwrittenLength] = {};
        std::memset(branch, 0x90, sizeof(branch));
        unsigned char jump[absoluteJumpLength] = {};
        MakeAbsoluteJump(jump, reinterpret_cast<const void*>(&RedirectFlowBlock));
        std::memcpy(branch, jump, sizeof(jump));
        DWORD oldProtection = 0;
        if (!VirtualProtect(target, overwrittenLength, PAGE_EXECUTE_READWRITE, &oldProtection))
        {
            VirtualFree(gateway, 0, MEM_RELEASE);
            Logger::Log("[ERROR] SkipLegal: executable page protection failed; memory remains unchanged.");
            return false;
        }
        originalParser = reinterpret_cast<ParseFlowBlockFn>(gateway);
        std::memcpy(target, branch, sizeof(branch));
        FlushInstructionCache(GetCurrentProcess(), target, overwrittenLength);
        DWORD restoredProtection = 0;
        if (!VirtualProtect(target, overwrittenLength, oldProtection, &restoredProtection))
            Logger::Log("[WARNING] SkipLegal: hook installed but executable page protection could not be restored.");
        Logger::Log("[INFO] SkipLegal: experimental memory hook installed; waiting for a matching startup flow.");
        return true;
    }

    bool ClearLegacyAssets()
    {
        const std::wstring root = GameRoot();
        if (root.empty())
        {
            Logger::Log("[ERROR] SkipIntro: unable to resolve RDR2 game directory.");
            return false;
        }
        const std::wstring directory = root + L"x64\\GameplayFixesRDR2_data";
        const std::wstring startup = root + L"x64\\data\\startup.meta";
        const std::wstring flow = directory + L"\\boot_launcher_flow.ymt";
        if ((Exists(startup) && !ReadExact(startup, startupData, sizeof(startupData) - 1)) ||
            (Exists(flow) && !ReadExact(flow, bootFlowData, sizeof(bootFlowData) - 1)))
        {
            Logger::Log("[ERROR] SkipIntro: existing startup assets are not owned by this mod; files left unchanged and memory patch skipped.");
            return false;
        }
        if (!RemoveOwned(startup, startupData, sizeof(startupData) - 1) ||
            !RemoveOwned(flow, bootFlowData, sizeof(bootFlowData) - 1))
        {
            Logger::Log("[ERROR] SkipIntro: unable to remove owned legacy startup files; memory patch skipped.");
            return false;
        }
        RemoveDirectoryW(directory.c_str());
        Logger::Log("[SUCCESS] SkipIntro: legacy owned startup replacements removed or already absent.");
        return true;
    }

}

namespace Main::SkipIntro
{
    void Initialize(bool skipIntroSplashes, bool skipLegal)
    {
        if (!ClearLegacyAssets())
            return;

        if (skipIntroSplashes)
            InstallSplashHook();
        else
            Logger::Log("[INFO] SkipIntroSplashes: disabled.");

        if (skipLegal)
            InstallFlowHook();
        else
            Logger::Log("[INFO] SkipLegal: disabled; no legal-flow memory patch applied.");
    }
}
