#include "NativeDiagnostics.h"
#include "../../Headers/Logger.h"
#include "main.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace
{
    struct NativeEntry
    {
        const char* name;
        std::uint64_t hash;
    };

    constexpr NativeEntry cameraNatives[] =
    {
        { "CAM::_IS_IN_FULL_FIRST_PERSON_MODE", 0xD1BA66940E94C547ULL },
        { "CAM::IS_GAMEPLAY_CAM_RENDERING", 0x8660EA714834E412ULL },
        { "CAM::IS_FIRST_PERSON_AIM_CAM_ACTIVE", 0xF63134C54B6EC212ULL },
        { "CAM::IS_FOLLOW_VEHICLE_CAM_ACTIVE", 0xA40C2F51FB589E9AULL },
        { "CAM::DISABLE_FIRST_PERSON_FLASH_EFFECT_THIS_UPDATE", 0x77D65669A05D1A1AULL },
        { "CAM::_IS_IN_CINEMATIC_MODE", 0x74F1D22EFA71FAB8ULL },
        { "CAM::INVALIDATE_CINEMATIC_VEHICLE_IDLE_MODE", 0x634F4A0562CF19B8ULL },
        { "PLAYER::PLAYER_ID", 0x217E9DC48139933DULL },
        { "PLAYER::IS_PLAYER_CONTROL_ON", 0x7964097FCE4C244BULL },
        { "CAM::IS_SCREEN_FADED_IN", 0x37F9A426FBCF4AF2ULL },
        { "CAM::IS_SCREEN_FADED_OUT", 0xF5472C80DF2FF847ULL },
        { "CAM::IS_SCREEN_FADING_OUT", 0x02F39BEFE7B88D00ULL },
        { "CAM::IS_SCREEN_FADING_IN", 0x0CECCC63FFA2EF24ULL }
    };

    constexpr std::uint64_t kFirstPersonMode = 0xD1BA66940E94C547ULL;
    constexpr std::uint64_t kGameplayCameraRendering = 0x8660EA714834E412ULL;
    constexpr std::uint64_t kPlayerId = 0x217E9DC48139933DULL;
    constexpr std::uint64_t kPlayerControlOn = 0x7964097FCE4C244BULL;
    constexpr std::uint64_t kScreenFadedIn = 0x37F9A426FBCF4AF2ULL;
    constexpr std::uint64_t kScreenFadedOut = 0xF5472C80DF2FF847ULL;
    constexpr std::uint64_t kScreenFadingOut = 0x02F39BEFE7B88D00ULL;
    constexpr std::uint64_t kScreenFadingIn = 0x0CECCC63FFA2EF24ULL;

    bool traceEnabled = false;
    bool initializedPerspective = false;
    bool previousFirstPerson = false;
    bool captureActive = false;
    unsigned int captureCount = 0;
    unsigned int captureNumber = 0;
    ULONGLONG captureStart = 0;
    ULONGLONG nextCaptureSample = 0;
    bool capturePerspective = false;

    bool BuildGamePath(const wchar_t* filename, wchar_t (&path)[MAX_PATH])
    {
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;

        DWORD directoryEnd = length;
        while (directoryEnd > 0 && path[directoryEnd - 1] != L'\\' && path[directoryEnd - 1] != L'/')
            --directoryEnd;
        if (directoryEnd == 0)
            return false;

        DWORD position = directoryEnd;
        for (DWORD i = 0; filename[i] != L'\0'; ++i)
        {
            if (position + 1 >= MAX_PATH)
                return false;
            path[position++] = filename[i];
        }
        path[position] = L'\0';
        return true;
    }

    bool WriteDiagnostic(const wchar_t* filename, const char* message, bool reset = false)
    {
        wchar_t path[MAX_PATH] = {};
        if (!BuildGamePath(filename, path))
            return false;

        HANDLE file = CreateFileW(path, reset ? GENERIC_WRITE : FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, reset ? CREATE_ALWAYS : OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        SYSTEMTIME now = {};
        GetLocalTime(&now);
        char prefix[80] = {};
        const int prefixLength = std::snprintf(prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
            static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
            static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
            static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
            static_cast<unsigned>(now.wMilliseconds));
        const size_t length = std::strlen(message);
        DWORD written = 0;
        bool success = prefixLength > 0 &&
            WriteFile(file, prefix, static_cast<DWORD>(prefixLength), &written, nullptr) &&
            written == static_cast<DWORD>(prefixLength);
        if (success)
            success = WriteFile(file, message, static_cast<DWORD>(length), &written, nullptr) && written == length;
        if (success)
            success = WriteFile(file, "\r\n", 2, &written, nullptr) && written == 2;
        CloseHandle(file);
        return success;
    }

    std::uint64_t NativeValue(std::uint64_t hash)
    {
        nativeInit(hash);
        return *nativeCall();
    }

    bool NativeBoolean(std::uint64_t hash)
    {
        return static_cast<std::uint32_t>(NativeValue(hash)) != 0;
    }

    bool PlayerHasControl()
    {
        nativeInit(kPlayerId);
        const auto player = *nativeCall();
        nativeInit(kPlayerControlOn);
        nativePush64(player);
        return static_cast<std::uint32_t>(*nativeCall()) != 0;
    }

    using ResolveNative = void* (__cdecl*)(std::uint64_t);

    ResolveNative FindNativeResolver()
    {
        const HMODULE scriptHook = GetModuleHandleW(L"ScriptHookRDR2.dll");
        if (!scriptHook)
            return nullptr;

        FARPROC symbol = GetProcAddress(scriptHook, "getCommandFromHash");
        if (!symbol)
            symbol = GetProcAddress(scriptHook, "?getCommandFromHash@@YAPEAX_K@Z");
        return reinterpret_cast<ResolveNative>(symbol);
    }

    void DescribeAddress(const void* address, char (&buffer)[160])
    {
        if (!address)
        {
            std::snprintf(buffer, sizeof(buffer), "unavailable");
            return;
        }

        HMODULE module = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module))
        {
            wchar_t filename[MAX_PATH] = {};
            if (GetModuleFileNameW(module, filename, MAX_PATH))
            {
                const wchar_t* name = filename;
                for (const wchar_t* cursor = filename; *cursor; ++cursor)
                    if (*cursor == L'\\' || *cursor == L'/')
                        name = cursor + 1;
                std::snprintf(buffer, sizeof(buffer), "%ls+0x%llX", name,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address) -
                        reinterpret_cast<std::uintptr_t>(module)));
                return;
            }
        }
        std::snprintf(buffer, sizeof(buffer), "0x%llX (unmapped module)",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(address)));
    }

    void DescribeInstructionBytes(const void* address, char (&buffer)[64])
    {
        std::snprintf(buffer, sizeof(buffer), "unavailable");
        if (!address)
            return;

        MEMORY_BASIC_INFORMATION region = {};
        if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region) || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return;

        const DWORD protection = region.Protect & 0xFF;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY && protection != PAGE_EXECUTE)
            return;

        constexpr SIZE_T byteCount = 16;
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (start >= regionEnd || regionEnd - start < byteCount)
            return;

        unsigned char bytes[byteCount] = {};
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), address, bytes, byteCount, &bytesRead) || bytesRead != byteCount)
            return;

        size_t offset = 0;
        for (unsigned char byte : bytes)
        {
            const int count = std::snprintf(buffer + offset, sizeof(buffer) - offset,
                "%s%02X", offset == 0 ? "" : " ", static_cast<unsigned>(byte));
            if (count <= 0 || static_cast<size_t>(count) >= sizeof(buffer) - offset)
                break;
            offset += static_cast<size_t>(count);
        }
    }

    void DumpHash(const wchar_t* filename, const char* name, std::uint64_t hash, ResolveNative resolve)
    {
        const void* handler = resolve ? resolve(hash) : nullptr;
        char location[160] = {};
        char instructions[64] = {};
        DescribeAddress(handler, location);
        DescribeInstructionBytes(handler, instructions);
        char message[440] = {};
        std::snprintf(message, sizeof(message), "%s hash=0x%016llX handler=%s bytes=%s",
            name, static_cast<unsigned long long>(hash), location, instructions);
        WriteDiagnostic(filename, message);
    }

    void DumpAdditionalHashes(const wchar_t* filename, ResolveNative resolve)
    {
        wchar_t path[MAX_PATH] = {};
        if (!BuildGamePath(L"GameplayFixesRDR2_NativeHashes.txt", path))
            return;

        FILE* file = nullptr;
        if (_wfopen_s(&file, path, L"rb") != 0 || !file)
        {
            WriteDiagnostic(filename, "Optional GameplayFixesRDR2_NativeHashes.txt not found; skipping custom hashes.");
            return;
        }

        WriteDiagnostic(filename, "Additional user-provided hashes:");
        unsigned int count = 0;
        char line[256] = {};
        while (count < 128 && std::fgets(line, sizeof(line), file))
        {
            char* start = line;
            while (*start && std::isspace(static_cast<unsigned char>(*start)))
                ++start;
            if (!*start || *start == '#' || *start == ';')
                continue;

            char* end = nullptr;
            const auto value = std::strtoull(start, &end, 0);
            if (end == start || value == 0)
                continue;
            while (*end && std::isspace(static_cast<unsigned char>(*end)))
                ++end;
            if (*end && *end != '#' && *end != ';')
                continue;

            DumpHash(filename, "CUSTOM", static_cast<std::uint64_t>(value), resolve);
            ++count;
        }
        std::fclose(file);
        char message[130] = {};
        std::snprintf(message, sizeof(message), "Custom hash entries processed: %u (maximum 128).", count);
        WriteDiagnostic(filename, message);
    }

    void DumpCameraHashes()
    {
        constexpr wchar_t filename[] = L"GameplayFixesRDR2_NativeDump.log";
        if (!WriteDiagnostic(filename, "Camera native hash catalog (documented and candidate hashes; not a full RDR2 native registry dump).", true))
        {
            Logger::Log("[WARNING] NativeDiagnostics: native dump log could not be created in the game directory.");
            return;
        }
        WriteDiagnostic(filename, "Handlers are read-only address lookups; no native handler hooks or patches are installed.");
        WriteDiagnostic(filename, "Note: DISABLE_FIRST_PERSON_FLASH_EFFECT_THIS_UPDATE is documented as a no-op in known RDR2 builds; handler resolution alone does not establish an effective flash fix.");

        ResolveNative resolve = FindNativeResolver();
        WriteDiagnostic(filename, resolve ?
            "ScriptHookRDR2 V2 getCommandFromHash: available." :
            "ScriptHookRDR2 getCommandFromHash: unavailable; hashes still listed, handler addresses cannot be resolved.");

        for (const auto& entry : cameraNatives)
            DumpHash(filename, entry.name, entry.hash, resolve);
        DumpAdditionalHashes(filename, resolve);
        WriteDiagnostic(filename, "Completed. This catalog does not intercept or enumerate all in-game native calls.");
        Logger::Log("[SUCCESS] NativeDiagnostics: camera native hashes written to GameplayFixesRDR2_NativeDump.log.");
    }

    void CaptureCameraSample(ULONGLONG now)
    {
        const bool gameplayCamera = NativeBoolean(kGameplayCameraRendering);
        const bool playerControl = PlayerHasControl();
        const bool fadedIn = NativeBoolean(kScreenFadedIn);
        const bool fadedOut = NativeBoolean(kScreenFadedOut);
        const bool fadingIn = NativeBoolean(kScreenFadingIn);
        const bool fadingOut = NativeBoolean(kScreenFadingOut);

        char message[280] = {};
        std::snprintf(message, sizeof(message),
            "capture=%u elapsed=%llums firstPerson=%u gameplayCam=%u playerControl=%u fadedIn=%u fadedOut=%u fadingIn=%u fadingOut=%u",
            captureNumber, static_cast<unsigned long long>(now - captureStart),
            capturePerspective ? 1U : 0U, gameplayCamera ? 1U : 0U, playerControl ? 1U : 0U,
            fadedIn ? 1U : 0U, fadedOut ? 1U : 0U, fadingIn ? 1U : 0U, fadingOut ? 1U : 0U);
        WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log", message);
    }
}

namespace Main::NativeDiagnostics
{
    bool IsCameraTraceActive()
    {
        return traceEnabled;
    }

    void Initialize(bool dumpNativeHashes, bool traceCameraNatives)
    {
        traceEnabled = traceCameraNatives;
        initializedPerspective = false;
        previousFirstPerson = false;
        captureActive = false;
        captureCount = 0;
        captureNumber = 0;
        captureStart = 0;
        nextCaptureSample = 0;
        capturePerspective = false;

        if (dumpNativeHashes)
            DumpCameraHashes();

        if (traceEnabled)
        {
            if (WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log",
                "Camera native-value capture started. Samples come from this mod's read-only native probes; this is not a global native-call hook.", true))
            {
                WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log",
                    "Trace samples: CAM::_IS_IN_FULL_FIRST_PERSON_MODE, CAM::IS_GAMEPLAY_CAM_RENDERING, PLAYER::PLAYER_ID, PLAYER::IS_PLAYER_CONTROL_ON, and four CAM screen-fade-state natives.");
                WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log",
                    "Each perspective switch captures approximately 400ms; the trace is capped at 24 switches per session.");
                Logger::Log("[SUCCESS] NativeDiagnostics: camera tracing enabled; writing GameplayFixesRDR2_CameraTrace.log.");
            }
            else
            {
                traceEnabled = false;
                Logger::Log("[WARNING] NativeDiagnostics: camera trace log could not be created in the game directory.");
            }
        }
    }

    void Update()
    {
        if (!traceEnabled)
            return;

        const bool firstPerson = NativeBoolean(kFirstPersonMode);
        const ULONGLONG now = GetTickCount64();
        if (!initializedPerspective)
        {
            initializedPerspective = true;
            previousFirstPerson = firstPerson;
            WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log",
                firstPerson ? "Initial perspective: firstPerson=1." : "Initial perspective: firstPerson=0.");
        }
        else if (firstPerson != previousFirstPerson)
        {
            previousFirstPerson = firstPerson;
            if (captureActive)
            {
                WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log", "Previous capture interrupted by another perspective change.");
                captureActive = false;
            }
            if (captureCount < 24)
            {
                captureStart = now;
                nextCaptureSample = now;
                capturePerspective = firstPerson;
                captureActive = true;
                captureNumber = ++captureCount;
                WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log",
                    firstPerson ? "Perspective transition: third-person -> first-person." :
                                  "Perspective transition: first-person -> third-person.");
            }
        }

        if (captureActive && now >= nextCaptureSample)
        {
            CaptureCameraSample(now);
            nextCaptureSample = now + 50;
            if (now - captureStart >= 400)
            {
                captureActive = false;
                WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log", "Capture completed.");
                if (captureCount == 24)
                {
                    traceEnabled = false;
                    WriteDiagnostic(L"GameplayFixesRDR2_CameraTrace.log", "Trace stopped after 24 perspective changes.");
                }
            }
        }
    }
}
