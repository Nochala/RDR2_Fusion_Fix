#include "GameplayFixes.h"
#include "../Headers/Logger.h"
#include "Main/Headers/Modules.h"
#include <windows.h>
#include <algorithm>
#include <string>

namespace
{
    GameplayFixes::Settings settings;
    INIT_ONCE earlyInitialization = INIT_ONCE_STATIC_INIT;

    std::wstring GetIniPath()
    {
        wchar_t path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return {};

        std::wstring iniPath(path, length);
        const size_t separator = iniPath.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
            return {};

        iniPath.resize(separator + 1);
        iniPath += L"GameplayFixesRDR2.ini";
        return iniPath;
    }

    bool ReadIniBoolean(const std::wstring& path, const wchar_t* section, const wchar_t* key, bool defaultValue)
    {
        wchar_t value[16] = {};
        GetPrivateProfileStringW(section, key, defaultValue ? L"true" : L"false", value, 16, path.c_str());

        if (CompareStringOrdinal(value, -1, L"true", -1, TRUE) == CSTR_EQUAL)
            return true;
        if (CompareStringOrdinal(value, -1, L"false", -1, TRUE) == CSTR_EQUAL)
            return false;

        return defaultValue;
    }

    bool WriteIniValue(const std::wstring& path, const wchar_t* section, const wchar_t* key,
        const wchar_t* value, const char* description)
    {
        if (WritePrivateProfileStringW(section, key, value, path.c_str()))
            return true;

        const DWORD error = GetLastError();
        Logger::Log((std::string("[ERROR] INI: could not write ") + description +
            " (Win32 error " + std::to_string(error) + ").").c_str());
        return false;
    }

    bool EnsureIniExists(const std::wstring& path)
    {
        bool success = true;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                Logger::Log((std::string("[WARNING] INI: unable to inspect existing file (Win32 error ") +
                    std::to_string(error) + ").").c_str());

            success = WriteIniValue(path, L"General", L"Enabled", L"true", "General.Enabled") && success;
            success = WriteIniValue(path, L"General", L"UpdateIntervalMs", L"250", "General.UpdateIntervalMs") && success;
            if (success)
                Logger::Log("[SUCCESS] INI: created default configuration.");
        }
        else if (attributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            Logger::Log("[ERROR] INI: configuration path points to a directory, not a file.");
            return false;
        }

        wchar_t existing[8] = {};
        GetPrivateProfileStringW(L"General", L"Logging", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"General", L"Logging", L"true", "General.Logging") && success;

        existing[0] = L'\0';
        GetPrivateProfileStringW(L"Debug", L"Enabled", L"", existing, 8, path.c_str());
        if (existing[0] == L'\0')
            success = WriteIniValue(path, L"Debug", L"Enabled", L"true", "Debug.Enabled") && success;

        wchar_t legacy[16] = {};
        GetPrivateProfileStringW(L"Startup", L"SkipIntroSplashesAndLegal", L"", legacy, 16, path.c_str());
        const wchar_t* inherited = legacy[0] == L'\0' ? L"true" :
            ReadIniBoolean(path, L"Startup", L"SkipIntroSplashesAndLegal", true) ? L"true" : L"false";
        const wchar_t* keys[] = { L"SkipIntroSplashes", L"SkipLegal" };
        for (const wchar_t* key : keys)
        {
            existing[0] = L'\0';
            GetPrivateProfileStringW(L"Startup", key, L"", existing, 8, path.c_str());
            if (existing[0] == L'\0')
                success = WriteIniValue(path, L"Startup", key, inherited, "Startup skip option") && success;
        }

        if (success && legacy[0] != L'\0')
        {
            if (WritePrivateProfileStringW(L"Startup", L"SkipIntroSplashesAndLegal", nullptr, path.c_str()))
                Logger::Log("[SUCCESS] INI: migrated SkipIntroSplashesAndLegal into separate startup options.");
            else
                Logger::Log("[WARNING] INI: old combined skip option could not be removed; the new options take priority.");
        }

        return success;
    }
}

namespace GameplayFixes
{
    BOOL CALLBACK DoEarlyInitialization(PINIT_ONCE, PVOID, PVOID*)
    {
        const std::wstring iniPath = GetIniPath();
        if (!iniPath.empty())
            Logger::SetEnabled(ReadIniBoolean(iniPath, L"General", L"Logging", true));

        Logger::Log("[BOOT] Early startup: reading configuration before Story Mode.");
        if (!iniPath.empty())
        {
            Logger::Log("[INFO] INI: reading GameplayFixesRDR2.ini from the game directory.");
            const bool iniReady = EnsureIniExists(iniPath);
            Logger::SetDebugEnabled(ReadIniBoolean(iniPath, L"Debug", L"Enabled", true));
            if (!iniReady)
                Logger::Log("[WARNING] INI: some settings could not be saved; defaults may be used.");

            settings.enabled = ReadIniBoolean(iniPath, L"General", L"Enabled", true);
            settings.skipIntroSplashes = ReadIniBoolean(iniPath, L"Startup", L"SkipIntroSplashes", true);
            settings.skipLegal = ReadIniBoolean(iniPath, L"Startup", L"SkipLegal", true);
            const unsigned int interval = GetPrivateProfileIntW(L"General", L"UpdateIntervalMs", 250, iniPath.c_str());
            settings.updateIntervalMs = (std::min)(interval, 60000u);
            Logger::Log(iniReady ? "[SUCCESS] INI: configuration settings read." :
                "[WARNING] INI: settings read attempted with configuration errors; defaults may be in use.");
        }
        else
            Logger::Log("[ERROR] INI: game directory could not be resolved; using default settings.");

        Logger::Log((std::string("[INFO] Settings: Enabled=") + (settings.enabled ? "true" : "false") +
            ", Debug=" + (Logger::IsDebugEnabled() ? "true" : "false") +
            ", SkipIntroSplashes=" + (settings.skipIntroSplashes ? "true" : "false") +
            ", SkipLegal=" + (settings.skipLegal ? "true" : "false") +
            ", UpdateIntervalMs=" + std::to_string(settings.updateIntervalMs) + ".").c_str());
        Main::InitializeEarly(settings);
        Logger::Log("[SUCCESS] Early startup: pre-Story-Mode initialization completed.");
        return TRUE;
    }

    void InitializeEarly()
    {
        if (!InitOnceExecuteOnce(&earlyInitialization, DoEarlyInitialization, nullptr, nullptr))
            Logger::Log("[ERROR] Early startup: initialization could not be completed.");
    }

    void Initialize()
    {
        InitializeEarly();
        Logger::LogStartup();
        Main::Initialize(settings);
        Logger::Log("[INFO] GameplayFixesRDR2: gameplay module initialization finished.");
    }

    void Update()
    {
        if (settings.enabled)
            Main::Update(settings);
    }

    unsigned int GetUpdateIntervalMs()
    {
        return settings.updateIntervalMs;
    }
}
